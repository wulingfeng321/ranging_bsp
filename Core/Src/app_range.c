#include "app_range.h"
#include "app_board_config.h"
#include "app_net.h"
#include "app_mic_scope.h"
#include "range_dsp.h"
#include "range_sync.h"
#include "range_audio_time.h"
#include "main.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include <string.h>

#define WIRE_SIZE 72U
#define SYNC_REQ 1U
#define SYNC_RESP 2U
#define SYNC_FOLLOW 3U
#define SYNC_STATE 4U
#define EVENT 5U
#define RESULT 6U
#define ACK 7U
#define AUDIO_RING 8192U
extern struct netif gnetif;
AppRangeStatus appRangeStatus;
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
static double sampleNs = 62500.0;
static int16_t window[RANGE_WINDOW_SAMPLES];
static uint64_t windowBase, windowAnchorCount, windowAnchorNs;
static double windowSampleNs;
static uint8_t windowTimeReady;
static RangeAudioTime audioTime;
static uint32_t timeModelBlock;
static uint8_t haveWindow;

typedef struct {
  uint64_t time;
  uint32_t id, quality, received;
  uint8_t used;
} Detection;
static Detection localEvents[4], remoteEvents[4];
static uint32_t localIndex, remoteIndex;
static uint64_t pendingEventTime;
static uint32_t pendingEventId, pendingEventQuality, eventRetry, eventStart;
static uint32_t pendingResultId, resultRetry, resultStart;
static uint32_t resultMm, resultQuality, resultSide;

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
  uint8_t bytes[WIRE_SIZE] = {'R','A','N','2',2,0,APP_BOARD_ROLE,WIRE_SIZE};
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
  p=pbuf_alloc(PBUF_TRANSPORT,WIRE_SIZE,PBUF_RAM);
  if (!p) return 0;
  err=pbuf_take(p,bytes,WIRE_SIZE); stampSerial=rangeTxStampSerial;
  if (err==ERR_OK) err=udp_sendto(pcb,p,&peer,APP_RANGE_PORT);
  pbuf_free(p);
  return err==ERR_OK && rangeTxStampSerial!=stampSerial ? rangeTxTimestamp : 0;
}

static void ClearMeasurements(void)
{
  memset(localEvents,0,sizeof(localEvents)); memset(remoteEvents,0,sizeof(remoteEvents));
  pendingEventId=0; pendingResultId=0; appRangeStatus.valid=0;
  appRangeStatus.pairDeltaValid=0;
  MicScope_SetRangeState(MIC_SCOPE_RANGE_WAITING);
}

static void Unlock(void)
{
  RangeSync_Reset(&syncModel); appRangeStatus.locked=0;
  pendingRequest=0; haveResponse=0; ++syncEpoch;
  ClearMeasurements();
}

static void DisplayResult(uint32_t id, uint32_t mm, uint32_t side, uint32_t quality)
{
  if (id==appRangeStatus.resultId) return; /* Retries never refresh stale results. */
  appRangeStatus.resultId=id; appRangeStatus.distanceMm=mm;
  appRangeStatus.direction=side==1 ? 1 : (side==2 ? -1 : 0);
  appRangeStatus.quality=quality; appRangeStatus.valid=1;
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
  if (!appNetStatus.online || port!=APP_RANGE_PORT || !ip_addr_cmp(addr,&peer) ||
      p->tot_len!=WIRE_SIZE || pbuf_copy_partial(p,bytes,WIRE_SIZE,0)!=WIRE_SIZE)
  { pbuf_free(p); ++appRangeStatus.rejected; return; }
  pbuf_free(p);
  if (memcmp(bytes,"RAN2",4) || bytes[4]!=2 || bytes[6]!=APP_PEER_ROLE || bytes[7]!=WIRE_SIZE ||
      G64(bytes+8)!=AppNet_PeerSession() || G64(bytes+16)!=AppNet_LocalSession()) goto reject;
  type=bytes[5]; id=G32(bytes+24); epoch=G32(bytes+28);
  x=G64(bytes+32); y=G64(bytes+40); z=G64(bytes+48);
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
      if (epoch!=peerEpoch || !x) ClearMeasurements();
      peerEpoch=epoch; appRangeStatus.locked=(uint8_t)x;
      appRangeStatus.syncErrorNs=(uint32_t)y; lastStateMs=now;
      return;
    }
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
    if (type==ACK && x==RESULT && id==pendingResultId && epoch==peerEpoch)
    { pendingResultId=0; return; }
  }
  else
  {
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
    if (type==ACK && x==EVENT && id==pendingEventId && epoch==syncEpoch)
    { pendingEventId=0; ++appRangeStatus.eventAck; return; }
    if (type==RESULT && appRangeStatus.locked && epoch==syncEpoch && x<=5000 && y<=2 && z<=1000)
    {
      if (appRangeStatus.resultId && id!=appRangeStatus.resultId &&
          id-appRangeStatus.resultId>=0x80000000UL) goto reject;
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
  uint32_t i;
  uint64_t now=RangeClock_Now(), previous=audioAnchor;
  uint64_t base=audioCount;
  if (previous && (now-previous<12000000ULL || now-previous>20000000ULL))
    ++audioEpoch;
  for(i=0;i<frames;++i)
  {
    int16_t value=pcm[i*2+APP_RANGE_CHANNEL];
    audioRing[(uint32_t)(base+i)&(AUDIO_RING-1)] = value;
  }
  audioCount+=frames; audioAnchor=now; ++audioBlocks;
}

static void DetectionReady(uint64_t stamp, uint32_t quality)
{
  uint32_t now=HAL_GetTick();
  ++appRangeStatus.events;
  appRangeStatus.eventQuality=quality;
  appRangeStatus.eventPeakSpreadSamples=rangeDspPeakSpreadSamples;
  if (!appRangeStatus.locked) { ++appRangeStatus.unlockedEvents; return; }
  if (APP_BOARD_ROLE==APP_BOARD_A)
  {
    localEvents[localIndex].time=stamp; localEvents[localIndex].id=++sequence;
    localEvents[localIndex].quality=quality; localEvents[localIndex].received=now;
    localEvents[localIndex].used=1; localIndex=(localIndex+1)%4;
  }
  else
  {
    pendingEventTime=RangeSync_Master(&syncModel,stamp);
    pendingEventId=++sequence; pendingEventQuality=quality;
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
  if ((!lastDetection || windowBase+scan>lastDetection+4000) &&
      RangeDsp_Find(window,scan,scan+64,&position,&q))
  {
    uint64_t detected=windowBase+(uint64_t)position;
    double age=((double)(windowAnchorCount-windowBase)-position)*windowSampleNs;
    if ((!lastDetection || detected>lastDetection+4000) && age>=0 && age<windowAnchorNs)
    {
      lastDetection=detected;
      if(windowTimeReady) DetectionReady(windowAnchorNs-(uint64_t)(age+0.5),q);
      else ++appRangeStatus.audioTimingRejected;
    }
  }
  scan+=64;
  /* We searched starts [0,256). Advancing only 128 searched half of those
   * starts twice and required eight slices per incoming 256-sample block. */
  if(scan>=256) { haveWindow=0; readSample+=256; }
}

int AppRange_DisplayReady(void)
{
  uint64_t count;
  uint32_t mask;
  if(!clockReady || !pcb) return 1; /* Keep initialization errors visible. */
  mask=__get_PRIMASK(); __disable_irq();
  count=audioCount;
  __set_PRIMASK(mask);
  /* One full signature window is normal latency, not an overrun. Defer
   * expensive LCD work when more than one additional DMA block is queued. */
  return count-readSample<=RANGE_WINDOW_SAMPLES+256U;
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
      delta-=APP_RANGE_BIAS_NS;
      if(!RangeDsp_Distance(delta,APP_TEMPERATURE_DECI_C,&resultMm,&direction))
      { ++appRangeStatus.rejected; ++appRangeStatus.distanceRejects; continue; }
      /* Include clock uncertainty in the ambiguous-direction band. */
      if (delta <= (int64_t)appRangeStatus.syncErrorNs && delta >= -(int64_t)appRangeStatus.syncErrorNs)
        direction=0;
      resultSide=direction>0 ? 1 : (direction<0 ? 2 : 0);
      resultQuality=localEvents[i].quality<remoteEvents[j].quality ?
                    localEvents[i].quality:remoteEvents[j].quality;
      appRangeStatus.resultDeltaUs=(int32_t)(
        ((int64_t)remoteEvents[j].time-(int64_t)localEvents[i].time)/1000);
      pendingResultId=++sequence; resultStart=now; resultRetry=now-200;
      DisplayResult(pendingResultId,resultMm,resultSide,resultQuality);
      break;
    }
}

void AppRange_Init(void)
{
  clockReady=(uint8_t)RangeClock_Init();
  if(!clockReady) { appRangeStatus.error=-1; return; }
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
  AudioProcess(); /* Continues during waveform hold and while offline. */
  /* DetectionReady may have created an event after the entry tick. Using
   * that older tick makes unsigned event age wrap and expire immediately. */
  now=HAL_GetTick();
  if(!appNetStatus.online || connection!=AppNet_PeerSession())
  {
    if(connection || appNetStatus.online)
    {
      connection=appNetStatus.online?AppNet_PeerSession():0;
      Unlock(); appRangeStatus.resultId=0; lastSyncNs=0; lastStateId=0;
      lastSyncMs=now-100; lastStateMs=now;
    }
    return;
  }
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
      Send(EVENT,pendingEventId,syncEpoch,pendingEventTime,pendingEventQuality,0,0,0);
    }
  }
  else
  {
    if(appRangeStatus.locked && now-lastStateMs>1500) Unlock();
    if(appRangeStatus.locked) PairEvents();
    /* PairEvents/DisplayResult stamp their new result using fresh ticks. */
    now=HAL_GetTick();
    if(pendingResultId && now-resultStart>1000) pendingResultId=0;
    if(pendingResultId && now-resultRetry>=200)
    {
      resultRetry=now;
      Send(RESULT,pendingResultId,peerEpoch,resultMm,resultSide,resultQuality,0,0);
    }
  }
  now=HAL_GetTick();
  if(appRangeStatus.valid && now-lastResultMs>=APP_RANGE_RESULT_HOLD_MS) appRangeStatus.valid=0;
}
