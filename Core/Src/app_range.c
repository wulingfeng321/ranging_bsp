#include "app_range.h"
#include "app_capture.h"
#include "app_board_config.h"
#include "app_net.h"
#include "app_mic_scope.h"
#include "range_dsp.h"
#include "range_sync.h"
#include "range_pps.h"
#include "range_audio_time.h"
#if APP_RANGE_JOINT_PEAKS
#include "range_peak_pair.h"
#endif
#if APP_RANGE_STATISTICS
#include "range_batch.h"
static RangeBatch batch;
static uint32_t batchStateMs, batchStateId;
static uint32_t batchResultFloor;
static uint64_t batchFirstEventNs, batchLastEventNs;
#endif
#include "main.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include <string.h>

#define WIRE_SIZE 76U
#define SYNC_REQ 1U
#define SYNC_RESP 2U
#define SYNC_FOLLOW 3U
#define SYNC_STATE 4U
#define EVENT 5U
#define RESULT 6U
#define ACK 7U
#define BATCH_STATE 8U
#define PEAK_EVENT 9U
#define PEAK_STATE 10U
#define PEAK_DIAG 11U
#define UI_STATE 12U
#define UI_REQUEST 13U
#define UI_ACK 14U
#define AUDIO_RING (APP_AUDIO_SAMPLE_RATE == 48000U ? 32768U : 8192U)
#define RANGE_WIRE_VERSION (APP_AUDIO_SAMPLE_RATE == 48000U ? 5U : 4U)
extern struct netif gnetif;
AppRangeStatus appRangeStatus;
AppRangeArrival appRangeArrival;
static AppPage uiPage = APP_PAGE_STANDARD;
static int32_t temperature = APP_TEMPERATURE_DECI_C;
static uint32_t uiRevision=1, uiAckRevision, uiLastSend;
static uint32_t uiRequestId, uiPendingId, uiRequestKind, uiRequestValue, uiLastRequest;
static uint8_t uiKnown;
static uint64_t syncOriginNs, uiPeerSession;
static struct udp_pcb *pcb;
static ip_addr_t peer;
static RangeSync syncModel;
static uint64_t connection, b1, a2, b4, lastSyncNs;
static uint32_t sequence, requestId, syncEpoch = 1, peerEpoch;
static uint32_t lastSyncMs, lastStateMs, lastResultMs, lastStateId;
static uint8_t pendingRequest, haveResponse, clockReady;
static volatile int16_t audioRing[AUDIO_RING];
static volatile uint64_t audioCount, audioAnchor;
static volatile uint32_t audioEpoch, audioBlocks;
static uint64_t readSample, lastDetection;
static uint32_t seenEpoch, scan;
static double sampleNs = APP_AUDIO_SAMPLE_NS;
static int16_t window[RANGE_WINDOW_SAMPLES];
static uint64_t windowBase, windowAnchorCount, windowAnchorNs;
static double windowSampleNs;
static uint8_t windowTimeReady;
static RangeAudioTime audioTime;
static uint32_t timeModelBlock;
static uint8_t haveWindow;
static uint64_t dspPeriodStart, dspBusyNs;

typedef struct {
  uint64_t time;
  uint32_t id, quality, received;
  uint8_t used;
#if APP_RANGE_JOINT_PEAKS
  RangePeaks peaks;
#endif
} Detection;
#if APP_RANGE_JOINT_PEAKS
static RangePeaks pendingPeaks, detectedPeaks;
static uint32_t peakStateMs,peakStateId;
#endif
static Detection localEvents[4], remoteEvents[4];
static uint32_t localIndex, remoteIndex;
static uint64_t pendingEventTime;
static uint32_t pendingEventId, eventRetry, eventStart;
#if !APP_RANGE_JOINT_PEAKS
static uint32_t pendingEventQuality;
#endif
static uint32_t pendingResultId, resultRetry, resultStart;
static uint32_t resultMm, resultQuality, resultSide;
static uint32_t txResultMm, txResultQuality, txResultSide, txResultKind;

static void P32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static uint32_t G32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void P64(uint8_t *p, uint64_t v) { P32(p,(uint32_t)(v>>32)); P32(p+4,(uint32_t)v); }
static uint64_t G64(const uint8_t *p) { return ((uint64_t)G32(p)<<32)|G32(p+4); }

/* Main-loop only. ARP must be resolved before accepting a TX timestamp.
 * Otherwise udp_sendto may merely queue the packet behind an ARP request. */
static uint64_t Send(uint8_t type, uint32_t id, uint32_t epoch,
                     uint64_t x, uint64_t y, uint64_t z, uint64_t u, uint64_t v)
{
  uint8_t bytes[WIRE_SIZE] = {'R','A','N','2',RANGE_WIRE_VERSION,0,APP_BOARD_ROLE,WIRE_SIZE};
  struct pbuf *p;
  struct eth_addr *mac;
  const ip4_addr_t *ip;
  uint32_t stampSerial;
  err_t err;
  if (!appNetStatus.online || pcb == NULL) return 0;
  if (etharp_find_addr(&gnetif, ip_2_ip4(&peer), &mac, &ip) < 0)
  { etharp_request(&gnetif, ip_2_ip4(&peer)); return 0; }
  bytes[5]=type; P64(bytes+8,AppNet_LocalSession()); P64(bytes+16,AppNet_PeerSession());
  P32(bytes+24,id); P32(bytes+28,epoch);
  P64(bytes+32,x); P64(bytes+40,y); P64(bytes+48,z); P64(bytes+56,u); P64(bytes+64,v);
  P32(bytes+72,uiRevision);
  p=pbuf_alloc(PBUF_TRANSPORT,WIRE_SIZE,PBUF_RAM);
  if (!p) return 0;
  err=pbuf_take(p,bytes,WIRE_SIZE); stampSerial=rangeTxStampSerial;
  if (err==ERR_OK) err=udp_sendto(pcb,p,&peer,APP_RANGE_PORT);
  pbuf_free(p);
  return err==ERR_OK && rangeTxStampSerial!=stampSerial ? rangeTxTimestamp : 0;
}

static void ClearMeasurements(void)
{
  memset(&appRangeArrival,0,sizeof(appRangeArrival));
#if APP_RANGE_STATISTICS
  RangeBatch_Reset(&batch);
  appRangeStatus.batchStage=0; appRangeStatus.batchCount=0;
  appRangeStatus.batchUsed=0; appRangeStatus.batchSpanMm=0; batchStateId=0;
  batchResultFloor=0;
#endif
  memset(localEvents,0,sizeof(localEvents)); memset(remoteEvents,0,sizeof(remoteEvents));
  pendingEventId=0; pendingResultId=0; appRangeStatus.valid=0;
  appRangeStatus.pairDeltaValid=0;
  appRangeStatus.peakUncertain=0;
  memset(&appRangeStatus.pairFailure,0,sizeof(appRangeStatus.pairFailure));
#if APP_RANGE_JOINT_PEAKS
  peakStateId=0;
#endif
  MicScope_SetRangeState(MIC_SCOPE_RANGE_WAITING);
}

static void Unlock(void)
{
  syncOriginNs=0;
  RangePps_Update(NULL,0);
  RangeSync_Reset(&syncModel); appRangeStatus.locked=0;
  pendingRequest=0; haveResponse=0; ++syncEpoch;
  ClearMeasurements();
}

AppPage AppRange_Page(void) { return uiPage; }
int32_t AppRange_Temperature(void) { return temperature; }
int AppRange_SettingsReady(void)
{
  return appNetStatus.online && !uiPendingId &&
    (APP_BOARD_ROLE==APP_BOARD_A ? uiAckRevision==uiRevision : uiKnown);
}
static int RangingEnabled(void)
{ return uiPage==APP_PAGE_STANDARD && AppRange_SettingsReady(); }

/* Changing mode/temperature discards detector backlog and all old results,
 * but preserves the clock fit, PCM recorder and synchronization counter. */
static void ApplyUi(AppPage page,int32_t temp)
{
  uint32_t mask=__get_PRIMASK();
  uiPage=page; temperature=temp;
  ClearMeasurements();
  appRangeStatus.quality=0; appRangeStatus.eventQuality=0;
  appRangeStatus.resultIsStat=0; appRangeStatus.audioTimeReady=0;
  RangeAudioTime_Reset(&audioTime);
  __disable_irq(); readSample=audioCount; seenEpoch=audioEpoch; __set_PRIMASK(mask);
  haveWindow=0; lastDetection=0;
  AppCapture_Log("ui,%lu,%u,%ld,%lu\n",(unsigned long)HAL_GetTick(),
    (unsigned)page,(long)temp,(unsigned long)uiRevision);
}
static int RequestUi(uint32_t kind,uint32_t value)
{
  if(AppCapture_Busy() || !AppRange_SettingsReady()) return 0;
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    ++uiRevision; if(!uiRevision) ++uiRevision;
    ApplyUi(kind==0 ? (AppPage)value : uiPage,kind==1 ? (int32_t)value-100 : temperature);
    uiLastSend=HAL_GetTick()-250U;
  } else {
    uiPendingId=++uiRequestId; if(!uiPendingId) uiPendingId=++uiRequestId;
    uiRequestKind=kind; uiRequestValue=value; uiLastSend=HAL_GetTick()-250U;
    ClearMeasurements();
  }
  return 1;
}
int AppRange_RequestPage(AppPage page)
{
  if((unsigned)page>(unsigned)APP_PAGE_POSITION) return 0;
  if(page==uiPage) return 1;
  return RequestUi(0,(uint32_t)page);
}
int AppRange_AdjustTemperature(int32_t stepDeciC)
{
  int32_t next=temperature+stepDeciC;
  if((stepDeciC!=5 && stepDeciC!=-5) || next < -100 || next > 500) return 0;
  return RequestUi(1,(uint32_t)(next+100));
}
int AppRange_SyncElapsed(uint64_t *elapsedUs,uint64_t *localUs)
{
  uint64_t master=0;
  *elapsedUs=0; *localUs=RangeClock_Now()/1000ULL;
  if(!syncOriginNs || !AppRange_MasterTime(&master) || master<syncOriginNs) return 0;
  *elapsedUs=(master-syncOriginNs)/1000ULL;
  return 1;
}
static void UiProcess(uint32_t now)
{
  if(!appRangeStatus.locked) syncOriginNs=0;
  if(now-uiLastSend<250U) return;
  uiLastSend=now;
  if(APP_BOARD_ROLE==APP_BOARD_A)
    Send(UI_STATE,uiRevision,0,(uint64_t)(temperature+100),uiPage,syncOriginNs,peerEpoch,uiLastRequest);
  else if(uiPendingId)
    Send(UI_REQUEST,uiPendingId,0,uiRequestKind,uiRequestValue,0,0,0);
}

void AppRange_ResetRound(void)
{
  uint32_t mask=__get_PRIMASK();
  Unlock();
  memset(&appRangeStatus,0,sizeof(appRangeStatus));
  memset(&rangeDspDiagnostics,0,sizeof(rangeDspDiagnostics));
  __disable_irq();readSample=audioCount;seenEpoch=audioEpoch;__set_PRIMASK(mask);
  haveWindow=0;lastDetection=0;RangeAudioTime_Reset(&audioTime);
  lastSyncNs=0;lastStateId=0;lastSyncMs=HAL_GetTick()-100U;
  localIndex=remoteIndex=0;
}

static void DisplayResult(uint32_t id, uint32_t mm, uint32_t side, uint32_t quality)
{
  if (id==appRangeStatus.resultId) return; /* Retries never refresh stale results. */
  appRangeStatus.resultId=id; appRangeStatus.distanceMm=mm;
  appRangeStatus.direction=side==1 ? 1 : (side==2 ? -1 : 0);
  appRangeStatus.quality=quality; appRangeStatus.valid=1;
#if APP_RANGE_JOINT_PEAKS
  if(APP_BOARD_ROLE==APP_BOARD_A || !peakStateId || id-peakStateId<0x80000000UL)
#endif
  appRangeStatus.peakUncertain=0;
  txResultMm=mm; txResultQuality=quality; txResultSide=side;
  txResultKind=appRangeStatus.resultIsStat;
  ++appRangeStatus.results; lastResultMs=HAL_GetTick();
  MicScope_SetDistanceMm(mm);
}

static void Receive(void *arg, struct udp_pcb *socket, struct pbuf *p,
                    const ip_addr_t *addr, u16_t port)
{
  uint8_t bytes[WIRE_SIZE], type;
  uint32_t id, epoch, now=HAL_GetTick(), i;
  uint64_t x,y,z,rx=rangeRxTimestamp,tx;
  (void)arg; (void)socket;
  if (!p) return;
  if(AppCapture_Busy()) { pbuf_free(p);return; }
  if (!appNetStatus.online || port!=APP_RANGE_PORT || !ip_addr_cmp(addr,&peer) ||
      p->tot_len!=WIRE_SIZE || pbuf_copy_partial(p,bytes,WIRE_SIZE,0)!=WIRE_SIZE)
  { pbuf_free(p); ++appRangeStatus.rejected; return; }
  pbuf_free(p);
  if (memcmp(bytes,"RAN2",4) || bytes[4]!=RANGE_WIRE_VERSION || bytes[6]!=APP_PEER_ROLE || bytes[7]!=WIRE_SIZE ||
      G64(bytes+8)!=AppNet_PeerSession() || G64(bytes+16)!=AppNet_LocalSession()) goto reject;
  type=bytes[5]; id=G32(bytes+24); epoch=G32(bytes+28);
  x=G64(bytes+32); y=G64(bytes+40); z=G64(bytes+48);
  if(APP_BOARD_ROLE==APP_BOARD_A && type==UI_ACK) {
    if(id==uiRevision && G32(bytes+72)==uiRevision) uiAckRevision=id;
    return;
  }
  if(APP_BOARD_ROLE==APP_BOARD_A && type==UI_REQUEST) {
    if(!id || x>1 || (x==0 && y>APP_PAGE_POSITION) ||
       (x==1 && (y>600 || y%5))) goto reject;
    if(!uiLastRequest || (id!=uiLastRequest && id-uiLastRequest<0x80000000UL)) {
      /* Request values are absolute, so duplicates cannot double-step temperature. */
      uiLastRequest=id;
      if((x==0 && uiPage!=(AppPage)y) || (x==1 && temperature!=(int32_t)y-100)) {
        ++uiRevision; if(!uiRevision) ++uiRevision;
        ApplyUi(x==0 ? (AppPage)y : uiPage,x==1 ? (int32_t)y-100 : temperature);
      }
    }
    uiLastSend=now-250U;
    return;
  }
  if(APP_BOARD_ROLE==APP_BOARD_B && type==UI_STATE) {
    if(!id || id!=G32(bytes+72) || x>600 || x%5 || y>APP_PAGE_POSITION) goto reject;
    if(uiKnown && id!=uiRevision && id-uiRevision>=0x80000000UL) goto reject;
    if(!uiKnown || id!=uiRevision) {
      uiRevision=id; ApplyUi((AppPage)y,(int32_t)x-100); uiKnown=1;
    }
    if(uiPendingId && G64(bytes+64)==uiPendingId) uiPendingId=0;
    if(appRangeStatus.locked && G64(bytes+56)==syncEpoch) syncOriginNs=z;
    Send(UI_ACK,id,0,0,0,0,0,0);
    return;
  }
  /* Clock traffic is independent of UI configuration. Every measurement packet
   * carries the setting revision so delayed results cannot cross a page/temp edit. */
  if(type>=EVENT && type<=PEAK_DIAG &&
     (!RangingEnabled() || G32(bytes+72)!=uiRevision)) goto reject;
  if (APP_BOARD_ROLE==APP_BOARD_A)
  {
    if (type==SYNC_REQ && rx)
    {
      tx=Send(SYNC_RESP,id,epoch,rx,0,0,0,0);
      if (tx) Send(SYNC_FOLLOW,id,epoch,tx,0,0,0,0);
      return;
    }
    if (type==SYNC_STATE && x<=1 && y<=500000)
    {
      if (lastStateId && (id-lastStateId==0 || id-lastStateId>=0x80000000UL)) goto reject;
      lastStateId=id;
      if (epoch!=peerEpoch || !x) { ClearMeasurements(); syncOriginNs=0; }
      if(x && !syncOriginNs)
        syncOriginNs=(RangeClock_Now()/1000000000ULL+1U)*1000000000ULL;
      peerEpoch=epoch; appRangeStatus.locked=(uint8_t)x;
      appRangeStatus.syncErrorNs=(uint32_t)y; lastStateMs=now;
      return;
    }
#if APP_RANGE_JOINT_PEAKS
    if(type==PEAK_EVENT && appRangeStatus.locked && epoch==peerEpoch) {
      RangePeaks peaks;
      uint64_t masterNow=RangeClock_Now();
      uint32_t count=(uint32_t)(y&255),quality=0;
      /* y: profile (bits 16..23), overflow (bit 8), candidate count (0..7). */
      if((y&~0xFF01FFULL)!=0 || ((y>>16)&255)!=APP_RANGE_AUDIO_PROFILE ||
         !count || count>3 || x>masterNow || masterNow-x>1500000000ULL) goto reject;
      memset(&peaks,0,sizeof(peaks)); peaks.count=count; peaks.overflow=(uint32_t)((y>>8)&1);
      for(i=0;i<3;++i) {
        uint64_t word=G64(bytes+48+8*i);
        if(i>=count) { if(word) goto reject; continue; }
        if(!RangePeak_Unpack(word,&peaks.peak[i])) goto reject;
        if(peaks.peak[i].quality>quality) quality=peaks.peak[i].quality;
      }
      for(i=0;i<4;++i) if(remoteEvents[i].id==id && remoteEvents[i].time==x) {
        Send(ACK,id,epoch,PEAK_EVENT,0,0,0,0); return;
      }
      remoteEvents[remoteIndex].time=x; remoteEvents[remoteIndex].id=id;
      remoteEvents[remoteIndex].quality=quality; remoteEvents[remoteIndex].received=now;
      remoteEvents[remoteIndex].peaks=peaks; remoteEvents[remoteIndex].used=1;
      remoteIndex=(remoteIndex+1)%4; ++appRangeStatus.eventRx;
      Send(ACK,id,epoch,PEAK_EVENT,0,0,0,0); return;
    }
#else
    if (type==EVENT && appRangeStatus.locked && epoch==peerEpoch &&
        y>=APP_RANGE_MIN_QUALITY && y<=1000)
    {
      uint64_t masterNow=RangeClock_Now();
      if (x>masterNow || masterNow-x>1500000000ULL) goto reject;
      /* Keep recently consumed entries to suppress event retransmission. */
      for (i=0;i<4;++i) if (remoteEvents[i].id==id && remoteEvents[i].time==x)
      { Send(ACK,id,epoch,EVENT,0,0,0,0); return; }
      remoteEvents[remoteIndex].time=x; remoteEvents[remoteIndex].id=id;
      remoteEvents[remoteIndex].quality=(uint32_t)y; remoteEvents[remoteIndex].received=now;
      remoteEvents[remoteIndex].used=1; remoteIndex=(remoteIndex+1)%4;
      ++appRangeStatus.eventRx;
      Send(ACK,id,epoch,EVENT,0,0,0,0); return;
    }
#endif
    if (type==ACK && x==RESULT && id==pendingResultId && epoch==peerEpoch)
    { pendingResultId=0; return; }
  }
  else
  {
#if APP_RANGE_JOINT_PEAKS
    if(type==PEAK_DIAG && appRangeStatus.locked && epoch==syncEpoch) {
      RangePairDiag diag;
      if(!RangePairDiag_Decode(&diag,x,y,z,G64(bytes+56),G64(bytes+64))) goto reject;
      if(!appRangeStatus.pairFailure.serial ||
         (diag.serial-appRangeStatus.pairFailure.serial!=0 &&
          diag.serial-appRangeStatus.pairFailure.serial<0x80000000UL))
        appRangeStatus.pairFailure=diag;
      return;
    }
    if(type==PEAK_STATE && appRangeStatus.locked && epoch==syncEpoch && x<=1 &&
       y<=UINT32_MAX && z<=UINT32_MAX && G64(bytes+56)<=3 && G64(bytes+64)<=APP_RANGE_PEAK_SPREAD_NS) {
      if(peakStateId && (id==peakStateId || id-peakStateId>=0x80000000UL)) goto reject;
      if(appRangeStatus.resultId && id-appRangeStatus.resultId>=0x80000000UL) goto reject;
      peakStateId=id; appRangeStatus.peakUncertain=(uint8_t)x;
      appRangeStatus.peakAmbiguous=(uint32_t)y; appRangeStatus.peakInconsistent=(uint32_t)z;
      appRangeStatus.peakCandidates=(uint32_t)G64(bytes+56);
      appRangeStatus.peakPairSpreadNs=(uint32_t)G64(bytes+64);
      return;
    }
#endif
#if APP_RANGE_STATISTICS
    if(type==BATCH_STATE && appRangeStatus.locked && epoch==syncEpoch &&
       x<=5 && y<=15 && z<=y && G64(bytes+56)<=10000 && G64(bytes+64)<=UINT32_MAX)
    {
      if(batchStateId && (id-batchStateId==0 || id-batchStateId>=0x80000000UL)) goto reject;
      if(appRangeStatus.resultId && id-appRangeStatus.resultId>=0x80000000UL) goto reject;
      batchStateId=id;
      batchResultFloor=(uint32_t)G64(bytes+64);
      appRangeStatus.batchStage=(uint32_t)x; appRangeStatus.batchCount=(uint32_t)y;
      appRangeStatus.batchUsed=(uint32_t)z; appRangeStatus.batchSpanMm=(uint32_t)G64(bytes+56);
      if(x==0) { appRangeStatus.valid=0; MicScope_SetRangeState(MIC_SCOPE_RANGE_WAITING); }
      return;
    }
#endif
    if (type==SYNC_RESP && pendingRequest && id==requestId && epoch==syncEpoch && rx && x)
    { a2=x; b4=rx; haveResponse=1; return; }
    if (type==SYNC_FOLLOW && pendingRequest && haveResponse && id==requestId && epoch==syncEpoch)
    {
      pendingRequest=0;
      if (RangeSync_Add(&syncModel,b1,a2,x,b4))
      {
        ++appRangeStatus.syncSamples; lastSyncNs=RangeClock_Now();
        if (appRangeStatus.locked && !syncModel.locked) Unlock();
        appRangeStatus.locked=syncModel.locked;
        appRangeStatus.syncErrorNs=syncModel.uncertainty;
        Send(SYNC_STATE,++sequence,syncEpoch,syncModel.locked,syncModel.uncertainty,0,0,0);
      }
      return;
    }
    if (type==ACK && x==(APP_RANGE_JOINT_PEAKS ? PEAK_EVENT:EVENT) && id==pendingEventId && epoch==syncEpoch)
    { pendingEventId=0; ++appRangeStatus.eventAck; return; }
    if (type==RESULT && appRangeStatus.locked && epoch==syncEpoch && x<=5000 && y<=2 && z<=1000)
    {
#if APP_RANGE_STATISTICS
      if(batchStateId && appRangeStatus.batchStage!=2 && id-batchStateId>=0x80000000UL && id!=batchResultFloor) goto reject;
#endif
      if (appRangeStatus.resultId && id!=appRangeStatus.resultId &&
          id-appRangeStatus.resultId>=0x80000000UL) goto reject;
      if(G64(bytes+56)>1) goto reject;
      if(id!=appRangeStatus.resultId) appRangeStatus.resultIsStat=(uint8_t)G64(bytes+56);
      DisplayResult(id,(uint32_t)x,(uint32_t)y,(uint32_t)z);
      Send(ACK,id,epoch,RESULT,0,0,0,0); return;
    }
  }
reject:
  ++appRangeStatus.rejected;
}

void AppRange_AudioError(void) { ++audioEpoch; }

void AppRange_Audio(const volatile int16_t *pcm, uint32_t frames)
{
  uint32_t i, epoch;
  uint64_t now=RangeClock_Now(), previous=audioAnchor;
  uint64_t base=audioCount;
  if(frames!=APP_AUDIO_HALF_FRAMES) { ++audioEpoch; return; }
  if (previous && (now-previous<12000000ULL || now-previous>20000000ULL))
    ++audioEpoch;
  for(i=0;i<frames;++i)
  {
    int16_t value=pcm[i*2+APP_RANGE_CHANNEL];
    audioRing[(uint32_t)(base+i)&(AUDIO_RING-1)] = value;
  }
  audioCount+=frames; audioAnchor=now; ++audioBlocks;
  epoch=audioEpoch;
  AppCapture_Audio(pcm,frames,base+frames,now,epoch);
}

static void DetectionReady(uint64_t stamp, uint32_t quality)
{
  uint32_t now=HAL_GetTick();
  ++appRangeStatus.events;
  appRangeStatus.eventQuality=quality;
  appRangeStatus.eventPeakSpreadSamples=rangeDspPeakSpreadSamples;
  AppCapture_Trigger();
  AppCapture_Log("event,%lu,%llu,%lu,%u\n",(unsigned long)appRangeStatus.events,
    (unsigned long long)stamp,(unsigned long)quality,appRangeStatus.locked);
  AppCapture_Log("clock,%lu,%llu,%.3f,%.3f,%.12f\n",(unsigned long)appRangeStatus.events,
    (unsigned long long)(APP_BOARD_ROLE==APP_BOARD_B ? RangeSync_Master(&syncModel,stamp) : stamp),
    syncModel.origin,syncModel.offset,syncModel.slope);
#if APP_RANGE_JOINT_PEAKS
  {
    unsigned c;
    for(c=0;c<detectedPeaks.count;++c)
      AppCapture_Log("candidate,%lu,%u,%ld,%ld,%ld,%lu\n",(unsigned long)appRangeStatus.events,c,
        (long)detectedPeaks.peak[c].offsetNs[0],(long)detectedPeaks.peak[c].offsetNs[1],
        (long)detectedPeaks.peak[c].offsetNs[2],(unsigned long)detectedPeaks.peak[c].quality);
  }
#endif
  if (!appRangeStatus.locked) { ++appRangeStatus.unlockedEvents; return; }
#if APP_RANGE_JOINT_PEAKS
  {
    unsigned p;
    memset(&appRangeArrival,0,sizeof(appRangeArrival));
    for(p=0;detectedPeaks.count && p<3;++p) {
      int64_t offset=(int64_t)p*RANGE_PULSE_STEP*1000000000LL/APP_AUDIO_SAMPLE_RATE;
      uint64_t local,master;
      offset+=detectedPeaks.peak[0].offsetNs[p];
      local=(uint64_t)((int64_t)stamp+offset);
      master=APP_BOARD_ROLE==APP_BOARD_B ? RangeSync_Master(&syncModel,local) : local;
      appRangeArrival.localUs[p]=local/1000ULL;
      if(syncOriginNs && master>=syncOriginNs) {
        appRangeArrival.syncUs[p]=(master-syncOriginNs)/1000ULL;
        appRangeArrival.valid|=(uint8_t)(1U<<p);
      }
    }
    appRangeArrival.eventId=appRangeStatus.events;
  }
#endif
  if (APP_BOARD_ROLE==APP_BOARD_A)
  {
    localEvents[localIndex].time=stamp; localEvents[localIndex].id=++sequence;
    localEvents[localIndex].quality=quality; localEvents[localIndex].received=now;
#if APP_RANGE_JOINT_PEAKS
    localEvents[localIndex].peaks=detectedPeaks;
#endif
    localEvents[localIndex].used=1; localIndex=(localIndex+1)%4;
  }
  else
  {
    pendingEventTime=RangeSync_Master(&syncModel,stamp);
#if APP_RANGE_JOINT_PEAKS
    {
      unsigned i,p;
      pendingPeaks=detectedPeaks;
      for(i=0;i<pendingPeaks.count;++i) for(p=0;p<3;++p) {
        int64_t nominal=(int64_t)p*RANGE_PULSE_STEP*1000000000LL/APP_AUDIO_SAMPLE_RATE;
        int64_t local=(int64_t)stamp+detectedPeaks.peak[i].offsetNs[p]+nominal;
        pendingPeaks.peak[i].offsetNs[p]=(int32_t)((int64_t)RangeSync_Master(&syncModel,(uint64_t)local)-
          (int64_t)pendingEventTime-nominal);
      }
    }
#endif
    pendingEventId=++sequence;
#if !APP_RANGE_JOINT_PEAKS
    pendingEventQuality=quality;
#endif
    eventStart=now; eventRetry=now-200;
  }
}

static void AudioProcess(void)
{
  uint64_t count,anchor;
  uint32_t epoch,blocks,mask,i,q;
  float position;
  mask=__get_PRIMASK(); __disable_irq();
  count=audioCount; anchor=audioAnchor; epoch=audioEpoch; blocks=audioBlocks;
  __set_PRIMASK(mask);
  appRangeStatus.backlogSamples=(uint32_t)(count-readSample);
  if(appRangeStatus.backlogSamples>appRangeStatus.maxBacklogSamples)
    appRangeStatus.maxBacklogSamples=appRangeStatus.backlogSamples;
  if (epoch!=seenEpoch || count-readSample>AUDIO_RING-RANGE_WINDOW_SAMPLES)
  {
    if(epoch!=seenEpoch) ++appRangeStatus.audioGapDrops;
    if(count-readSample>AUDIO_RING-RANGE_WINDOW_SAMPLES) ++appRangeStatus.audioOverruns;
    seenEpoch=epoch; readSample=count; haveWindow=0; lastDetection=0;
    ++appRangeStatus.audioDrops; appRangeStatus.error=-4;
    RangeAudioTime_Reset(&audioTime); timeModelBlock=blocks;
    appRangeStatus.audioTimeReady=0;
    ClearMeasurements(); return;
  }
  if (blocks!=timeModelBlock && anchor)
  {
    timeModelBlock=blocks;
    RangeAudioTime_Add(&audioTime,count,anchor);
    appRangeStatus.audioTimeReady=audioTime.ready;
    appRangeStatus.audioJitterNs=audioTime.jitterNs;
    if(audioTime.ready) {
      sampleNs=audioTime.periodNs;
      appRangeStatus.samplePeriodPs=(uint32_t)(sampleNs*1000.0+0.5);
    }
  }
  if (!haveWindow)
  {
    if (count<readSample+RANGE_WINDOW_SAMPLES) return;
    windowBase=readSample; windowAnchorCount=count; windowAnchorNs=anchor;
    windowTimeReady=audioTime.ready;
    if(windowTimeReady) windowAnchorNs=(uint64_t)(audioTime.anchorNs+0.5);
    /* The window spans several main-loop calls. Its timestamp mapping must
     * not change if a new sample-period estimate arrives during its scan. */
    windowSampleNs=sampleNs;
    for(i=0;i<RANGE_WINDOW_SAMPLES;++i)
      window[i]=audioRing[(uint32_t)(readSample+i)&(AUDIO_RING-1)];
    scan=0; haveWindow=1;
  }
  if ((!lastDetection || windowBase+scan>lastDetection+RANGE_REFRACTORY_SAMPLES) &&
      RangeDsp_Find(window,scan,scan+RANGE_SCAN_SLICE,&position,&q))
  {
    uint64_t detected=windowBase+(uint64_t)position;
    double age=((double)(windowAnchorCount-windowBase)-position)*windowSampleNs;
    if ((!lastDetection || detected>lastDetection+RANGE_REFRACTORY_SAMPLES) && age>=0 && age<windowAnchorNs)
    {
      lastDetection=detected;
      if(windowTimeReady) {
#if APP_RANGE_JOINT_PEAKS
        RangeDspPeaks candidates; unsigned c,p;
        RangeDsp_Candidates(window,position,&candidates);
        memset(&detectedPeaks,0,sizeof(detectedPeaks));
        detectedPeaks.count=candidates.count; detectedPeaks.overflow=candidates.overflow;
        appRangeStatus.peakCandidates=candidates.count;
        for(c=0;c<candidates.count;++c) {
          detectedPeaks.peak[c].quality=candidates.peak[c].quality;
          for(p=0;p<3;++p) {
            double offset=(candidates.peak[c].position[p]-position)*windowSampleNs-
                          (double)p*RANGE_PULSE_STEP*APP_AUDIO_SAMPLE_NS;
            detectedPeaks.peak[c].offsetNs[p]=(int32_t)(offset>=0 ? offset+0.5:offset-0.5);
          }
        }
        if(!candidates.count) { ++appRangeStatus.peakInconsistent; appRangeStatus.peakUncertain=1; }
        else
#endif
        DetectionReady(windowAnchorNs-(uint64_t)(age+0.5),q);
      }
      else ++appRangeStatus.audioTimingRejected;
    }
  }
  scan+=RANGE_SCAN_SLICE;
  /* Four bounded scan slices per 16 ms DMA half. */
  if(scan>=RANGE_SCAN_ADVANCE) { haveWindow=0; readSample+=RANGE_SCAN_ADVANCE; }
}

int AppRange_MasterTime(uint64_t *masterNs)
{
  uint64_t localNs;
  if (!clockReady || !appNetStatus.online || !appRangeStatus.locked || AppCapture_Busy()) return 0;
  localNs = RangeClock_Now();
  *masterNs = APP_BOARD_ROLE == APP_BOARD_B ?
              RangeSync_Master(&syncModel, localNs) : localNs;
  return 1;
}

int AppRange_DisplayReady(void)
{
  uint64_t count;
  uint32_t mask;
  if(!clockReady || !pcb || AppCapture_Busy() || !RangingEnabled()) return 1;
  mask=__get_PRIMASK(); __disable_irq();
  count=audioCount;
  __set_PRIMASK(mask);
  /* One full signature window is normal latency, not an overrun. Defer
   * expensive LCD work when more than one additional DMA block is queued. */
  return count-readSample<=RANGE_WINDOW_SAMPLES+APP_AUDIO_HALF_FRAMES;
}

static void PairEvents(void)
{
  uint32_t i,j,now=HAL_GetTick();
  uint64_t nearest=UINT64_MAX;
  /* Retain the last comparison when no candidates remain. ClearMeasurements
   * invalidates it, so an old clock generation is never shown as current. */
  for(i=0;i<4;++i) if(localEvents[i].used && now-localEvents[i].received<=1500)
    for(j=0;j<4;++j) if(remoteEvents[j].used && now-remoteEvents[j].received<=1500)
    {
      int64_t dt=(int64_t)remoteEvents[j].time-(int64_t)localEvents[i].time;
      uint64_t magnitude=(uint64_t)(dt<0 ? -dt : dt);
      if(magnitude<nearest)
      {
        int64_t us=dt/1000;
        nearest=magnitude;
        appRangeStatus.pairDeltaUs=us>INT32_MAX ? INT32_MAX :
          (us<INT32_MIN ? INT32_MIN : (int32_t)us);
        appRangeStatus.pairDeltaValid=1;
      }
    }
  for(i=0;i<4;++i)
  {
    if(now-localEvents[i].received>1500) localEvents[i].used=0;
    if(now-remoteEvents[i].received>1500) remoteEvents[i].used=0;
  }
  for(i=0;i<4;++i) if(localEvents[i].used)
    for(j=0;j<4;++j) if(remoteEvents[j].used)
    {
      int64_t delta=(int64_t)remoteEvents[j].time-(int64_t)localEvents[i].time;
      int32_t direction;
      if(delta < -20000000LL || delta > 20000000LL) continue;
      localEvents[i].used=0; remoteEvents[j].used=0;
#if APP_RANGE_JOINT_PEAKS
      {
        RangePairDiag diag;
        int paired=RangePeak_PairDetailed(&localEvents[i].peaks,&remoteEvents[j].peaks,delta,
                                 &delta,&resultQuality,&appRangeStatus.peakPairSpreadNs,&diag);
        AppCapture_Log("pair,%lu,%lu,%llu,%llu,%d,%u,%ld,%ld,%lu,%lu,%lu,%lu,%u,%u,%u,%u\n",
          (unsigned long)localEvents[i].id,(unsigned long)remoteEvents[j].id,
          (unsigned long long)localEvents[i].time,(unsigned long long)remoteEvents[j].time,
          paired,diag.reason,(long)diag.bestNs,(long)diag.runnerNs,(unsigned long)diag.bestScore,
          (unsigned long)diag.runnerScore,(unsigned long)diag.bestSpanNs,(unsigned long)diag.runnerSpanNs,
          diag.bestA,diag.bestB,diag.runnerA,diag.runnerB);
        if(paired!=1) {
          diag.serial=appRangeStatus.pairFailure.serial+1U;
          if(!diag.serial) diag.serial=1;
          appRangeStatus.pairFailure=diag;
          if(paired==2) ++appRangeStatus.peakAmbiguous;
          else ++appRangeStatus.peakInconsistent;
          appRangeStatus.peakUncertain=1;
          break; /* Never reuse the consumed local event. */
        }
        appRangeStatus.peakUncertain=0;
      }
#endif
      appRangeStatus.resultDeltaUs=(int32_t)(delta/1000);
      delta-=APP_RANGE_BIAS_NS;
      if(!RangeDsp_Distance(delta,temperature,&resultMm,&direction))
      { ++appRangeStatus.rejected; ++appRangeStatus.distanceRejects; continue; }
      /* Include clock uncertainty in the ambiguous-direction band. */
      if (delta <= (int64_t)appRangeStatus.syncErrorNs && delta >= -(int64_t)appRangeStatus.syncErrorNs)
        direction=0;
      resultSide=direction>0 ? 1 : (direction<0 ? 2 : 0);
#if !APP_RANGE_JOINT_PEAKS
      resultQuality=localEvents[i].quality<remoteEvents[j].quality ?
                    localEvents[i].quality:remoteEvents[j].quality;
#endif
#if APP_RANGE_STATISTICS
      /* Keep an accepted estimate visible for its normal hold time while the
       * next batch collects. A later single shot must not turn it yellow or
       * replace its pending retransmission payload. Do not extend its expiry. */
      if(!appRangeStatus.valid || !appRangeStatus.resultIsStat ||
         now-lastResultMs>=APP_RANGE_RESULT_HOLD_MS) {
        appRangeStatus.resultIsStat=0;
        pendingResultId=++sequence; resultStart=HAL_GetTick(); resultRetry=resultStart-200;
        DisplayResult(pendingResultId,resultMm,resultSide,resultQuality);
      }
      if(batch.stage==1) {
        uint64_t event=localEvents[i].time;
        uint64_t phase;
        if(event<=batchLastEventNs || event-batchLastEventNs<350000000ULL) {
          ++appRangeStatus.batchCadenceRejected; continue;
        }
        phase=(event-batchFirstEventNs)%500000000ULL;
        if(phase>50000000ULL && phase<450000000ULL) {
          ++appRangeStatus.batchCadenceRejected; continue;
        }
      }
      if(batch.stage!=1) {
        batchFirstEventNs=localEvents[i].time;
        appRangeStatus.batchUsed=0; appRangeStatus.batchSpanMm=0;
      }
      batchLastEventNs=localEvents[i].time;
      RangeBatch_Add(&batch,delta<0 ? -(int32_t)resultMm : (int32_t)resultMm,resultQuality,now);
      appRangeStatus.batchStage=1; appRangeStatus.batchCount=batch.count;
#else
      pendingResultId=++sequence; resultStart=now; resultRetry=now-200;
      DisplayResult(pendingResultId,resultMm,resultSide,resultQuality);
#endif
      break;
    }
}

void AppRange_Init(void)
{
  clockReady=(uint8_t)RangeClock_Init();
  if(!clockReady) { appRangeStatus.error=-1; return; }
  if(!RangePps_Init()) { clockReady=0; appRangeStatus.error=-1; return; }
  IP_ADDR4(&peer,192,168,10,APP_PEER_HOST);
  pcb=udp_new();
  if(!pcb) { appRangeStatus.error=-2; return; }
  if(udp_bind(pcb,IP_ADDR_ANY,APP_RANGE_PORT)!=ERR_OK)
  { udp_remove(pcb); pcb=NULL; appRangeStatus.error=-2; return; }
  udp_recv(pcb,Receive,NULL);
}

void AppRange_Process(void)
{
  uint32_t now=HAL_GetTick();
  if(!clockReady || !pcb) return;
  if(AppCapture_Busy()) {
    uint32_t mask=__get_PRIMASK();
    __disable_irq();readSample=audioCount;seenEpoch=audioEpoch;__set_PRIMASK(mask);
    haveWindow=0;return;
  }
  {
    uint64_t start=RangeClock_Now(), finish, elapsed;
    if(RangingEnabled()) AudioProcess(); /* Never run DSP in the DMA ISR. */
    else {
      uint32_t mask=__get_PRIMASK();
      __disable_irq(); readSample=audioCount; seenEpoch=audioEpoch; __set_PRIMASK(mask);
      haveWindow=0; lastDetection=0;
    }
    finish=RangeClock_Now(); elapsed=finish-start; dspBusyNs+=elapsed;
    if(elapsed/1000>appRangeStatus.dspMaxUs) appRangeStatus.dspMaxUs=(uint32_t)(elapsed/1000);
    if(!dspPeriodStart) dspPeriodStart=start;
    if(finish-dspPeriodStart>=1000000000ULL) {
      appRangeStatus.dspLoadPermille=(uint32_t)(dspBusyNs*1000/(finish-dspPeriodStart));
      dspBusyNs=0; dspPeriodStart=finish;
    }
  }
  /* DetectionReady may have created an event after the entry tick. Using
   * that older tick makes unsigned event age wrap and expire immediately. */
  now=HAL_GetTick();
  if(!appNetStatus.online || connection!=AppNet_PeerSession())
  {
    RangePps_Update(NULL,0);
    if(connection || appNetStatus.online)
    {
      connection=appNetStatus.online?AppNet_PeerSession():0;
      Unlock(); appRangeStatus.resultId=0; lastSyncNs=0; lastStateId=0;
      uiKnown=0; uiAckRevision=0; uiPendingId=0;
      if(connection && connection!=uiPeerSession) {
        uiPeerSession=connection; uiLastRequest=0;
      }
      uiLastSend=now-250U;
      lastSyncMs=now-100; lastStateMs=now;
    }
    return;
  }
  UiProcess(now);
  if(APP_BOARD_ROLE==APP_BOARD_B)
  {
    if(lastSyncNs && RangeClock_Now()-lastSyncNs>1500000000ULL)
    {
      Unlock(); lastSyncNs=0; appRangeStatus.error=-3;
      Send(SYNC_STATE,++sequence,syncEpoch,0,0,0,0,0);
    }
    if(now-lastSyncMs>=100)
    {
      lastSyncMs=now; requestId=++sequence; haveResponse=0;
      b1=Send(SYNC_REQ,requestId,syncEpoch,0,0,0,0,0);
      pendingRequest=b1!=0;
    }
    if(pendingEventId && now-eventStart>1000) pendingEventId=0;
    if(pendingEventId && now-eventRetry>=200)
    {
      eventRetry=now;
      ++appRangeStatus.eventTxAttempts;
#if APP_RANGE_JOINT_PEAKS
      Send(PEAK_EVENT,pendingEventId,syncEpoch,pendingEventTime,
           ((uint64_t)APP_RANGE_AUDIO_PROFILE<<16)|(pendingPeaks.overflow<<8)|pendingPeaks.count,
           pendingPeaks.count>0 ? RangePeak_Pack(&pendingPeaks.peak[0]):0,
           pendingPeaks.count>1 ? RangePeak_Pack(&pendingPeaks.peak[1]):0,
           pendingPeaks.count>2 ? RangePeak_Pack(&pendingPeaks.peak[2]):0);
#else
      Send(EVENT,pendingEventId,syncEpoch,pendingEventTime,pendingEventQuality,0,0,0);
#endif
    }
  }
  else
  {
    if(appRangeStatus.locked && now-lastStateMs>1500) Unlock();
#if APP_RANGE_STATISTICS
    if(RangingEnabled() && appRangeStatus.locked && batch.stage==1 && now-batch.startMs>=APP_RANGE_BATCH_MS) {
      RangeBatch_Finish(&batch);
      appRangeStatus.batchStage=batch.stage; appRangeStatus.batchUsed=batch.used;
      appRangeStatus.batchSpanMm=batch.span;
      if(batch.stage==2) {
        resultMm=(uint32_t)(batch.estimate<0 ? -batch.estimate : batch.estimate);
        resultSide=batch.estimate<0 ? 2 : 1; resultQuality=batch.resultQuality;
        appRangeStatus.resultIsStat=1;
        pendingResultId=++sequence; resultStart=HAL_GetTick(); resultRetry=resultStart-200;
        DisplayResult(pendingResultId,resultMm,resultSide,resultQuality);
      } /* On failure retain the last single-shot preview, explicitly labelled. */
    }
#endif
    if(RangingEnabled() && appRangeStatus.locked) PairEvents();
#if APP_RANGE_JOINT_PEAKS
    if(RangingEnabled() && appRangeStatus.locked && now-peakStateMs>=500) {
      peakStateMs=now;
      Send(PEAK_STATE,++sequence,peerEpoch,appRangeStatus.peakUncertain,
           appRangeStatus.peakAmbiguous,appRangeStatus.peakInconsistent,
           appRangeStatus.peakCandidates,appRangeStatus.peakPairSpreadNs);
      if(appRangeStatus.pairFailure.serial) {
        const RangePairDiag *d=&appRangeStatus.pairFailure;
        Send(PEAK_DIAG,++sequence,peerEpoch,RangePairDiag_Meta(d),
             ((uint64_t)(uint32_t)d->bestNs<<32)|(uint32_t)d->runnerNs,
             ((uint64_t)d->bestScore<<32)|d->runnerScore,
             ((uint64_t)d->bestSpanNs<<32)|d->runnerSpanNs,d->serial);
      }
    }
#endif
    /* PairEvents/DisplayResult stamp their new result using fresh ticks. */
    now=HAL_GetTick();
    if(pendingResultId && now-resultStart>1000) pendingResultId=0;
    if(pendingResultId && now-resultRetry>=200)
    {
      resultRetry=now;
      Send(RESULT,pendingResultId,peerEpoch,txResultMm,txResultSide,txResultQuality,txResultKind,0);
    }
#if APP_RANGE_STATISTICS
    if(RangingEnabled() && appRangeStatus.locked && now-batchStateMs>=500) {
      batchStateMs=now;
      Send(BATCH_STATE,++sequence,peerEpoch,batch.stage,batch.count,batch.used,batch.span,appRangeStatus.resultId);
    }
#endif
  }
  now=HAL_GetTick();
  if(appRangeStatus.valid && now-lastResultMs>=APP_RANGE_RESULT_HOLD_MS) appRangeStatus.valid=0;
  RangePps_Update(APP_BOARD_ROLE==APP_BOARD_B ? &syncModel : NULL,
                  appRangeStatus.locked);
}
