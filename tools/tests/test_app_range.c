/* Actual ranging state machine, with deterministic transport and hardware clock. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../Core/Src/app_range.c"
#include "range_template.h"
struct netif gnetif={1};
AppNetStatus appNetStatus;
uint64_t rangeRxTimestamp, rangeTxTimestamp;
uint32_t rangeTxStampSerial;
static uint64_t clockNs=10000000000ULL;
static struct udp_pcb fakePcb;
static int balance, displays;
static uint8_t sent[72];
static uint32_t tickAdvanceMs;
uint32_t HAL_GetTick(void)
{
  uint32_t tick=(uint32_t)(clockNs/1000000ULL);
  clockNs+=(uint64_t)tickAdvanceMs*1000000ULL;
  return tick;
}
uint64_t RangeClock_Now(void) { return clockNs; }
int RangeClock_Init(void) { return 1; }
uint64_t AppNet_LocalSession(void) { return 111; }
uint64_t AppNet_PeerSession(void) { return 222; }
void MicScope_SetRangeState(MicScope_RangeState s) { (void)s; }
void MicScope_SetDistanceMm(uint32_t mm) { assert(mm<=5000); ++displays; }
int etharp_find_addr(struct netif *n,const ip4_addr_t *i,struct eth_addr **m,const ip4_addr_t **p)
{ (void)n;(void)i;(void)m;(void)p;return 0; }
int etharp_request(struct netif *n,const ip4_addr_t *p) { (void)n;(void)p;return 0; }
struct pbuf *pbuf_alloc(int l,uint16_t n,int t)
{ struct pbuf *p=malloc(sizeof(*p));(void)l;(void)t;assert(p);p->tot_len=n;++balance;return p; }
void pbuf_free(struct pbuf *p) { free(p);--balance; }
err_t pbuf_take(struct pbuf *p,const void *b,uint16_t n) { memcpy(p->bytes,b,n);return 0; }
uint16_t pbuf_copy_partial(struct pbuf *p,void *b,uint16_t n,uint16_t o)
{ memcpy(b,p->bytes+o,n);return n; }
struct udp_pcb *udp_new(void) { return &fakePcb; }
err_t udp_bind(struct udp_pcb *p,const ip_addr_t *a,uint16_t port)
{ (void)p;(void)a;assert(port==5001);return 0; }
void udp_remove(struct udp_pcb *p) { (void)p; }
void udp_recv(struct udp_pcb *p,void (*f)(void *,struct udp_pcb *,struct pbuf *,const ip_addr_t *,u16_t),void *a)
{ (void)p;(void)f;(void)a; }
err_t udp_sendto(struct udp_pcb *p,struct pbuf *b,const ip_addr_t *a,uint16_t port)
{
  (void)p;(void)a;assert(port==5001 && b->tot_len==72);
  memcpy(sent,b->bytes,72);rangeTxTimestamp=clockNs;++rangeTxStampSerial;return 0;
}
static void Inject(uint8_t type,uint32_t id,uint32_t epoch,uint64_t x,uint64_t y,uint64_t z)
{
  struct pbuf *p=pbuf_alloc(0,72,0);
  memset(p->bytes,0,72);memcpy(p->bytes,"RAN2",4);
  p->bytes[4]=2;p->bytes[5]=type;p->bytes[6]=APP_PEER_ROLE;p->bytes[7]=72;
  P64(p->bytes+8,222);P64(p->bytes+16,111);P32(p->bytes+24,id);P32(p->bytes+28,epoch);
  P64(p->bytes+32,x);P64(p->bytes+40,y);P64(p->bytes+48,z);
  rangeRxTimestamp=clockNs;Receive(NULL,pcb,p,&peer,5001);assert(balance==0);
}
int main(void)
{
  unsigned i;
  AppRange_Init();appNetStatus.online=1;AppRange_Process();
  if(APP_BOARD_ROLE==APP_BOARD_A)
  {
    Inject(SYNC_STATE,1,7,1,1000,0);assert(appRangeStatus.locked);
    Inject(SYNC_STATE,1,6,0,0,0);assert(appRangeStatus.locked && peerEpoch==7);
    DetectionReady(clockNs-200000000ULL,950);
    Inject(EVENT,19,7,clockNs-197087950ULL,APP_RANGE_MIN_QUALITY-1,0);
    PairEvents();assert(displays==0);
    Inject(EVENT,20,7,clockNs-197087950ULL,APP_RANGE_MIN_QUALITY,0);
    PairEvents();assert(displays==1 && appRangeStatus.distanceMm==1000 && appRangeStatus.direction==1);
    assert(appRangeStatus.eventRx==1 && appRangeStatus.pairDeltaValid &&
           appRangeStatus.pairDeltaUs==2912 && appRangeStatus.results==1);
    Inject(EVENT,20,7,clockNs-197087950ULL,900,0);
    PairEvents();assert(displays==1);
    assert(appRangeStatus.eventRx==1); /* Retries are not new received events. */
    Inject(ACK,pendingResultId,7,RESULT,0,0);assert(pendingResultId==0);
    /* Wrong generation and non-matching event must not produce a range. */
    DetectionReady(clockNs-100000000ULL,950);
    Inject(EVENT,21,6,clockNs-99000000ULL,900,0);PairEvents();assert(displays==1);
    Inject(EVENT,22,7,clockNs-50000000ULL,900,0);PairEvents();assert(displays==1);
    assert(appRangeStatus.pairDeltaValid && appRangeStatus.pairDeltaUs==50000);
    clockNs+=1600000000ULL;AppRange_Process();assert(!appRangeStatus.locked && !appRangeStatus.valid);
  }
  else
  {
    /* A is 700 ms behind B, symmetric 12 us network legs. */
    for(i=0;i<12;++i)
    {
      uint64_t t=clockNs;AppRange_Process();assert(pendingRequest);
      clockNs=t+64000;
      Inject(SYNC_RESP,requestId,syncEpoch,t-700000000ULL+12000,0,0);
      Inject(SYNC_FOLLOW,requestId,syncEpoch,t-700000000ULL+52000,0,0);
      clockNs=t+100000000ULL;
    }
    assert(appRangeStatus.locked);
    DetectionReady(clockNs-200000000ULL,900);assert(pendingEventId);
    Inject(ACK,pendingEventId,syncEpoch,EVENT,0,0);assert(!pendingEventId);
    assert(appRangeStatus.eventAck==1 && appRangeStatus.eventQuality==900);
    Inject(RESULT,100,syncEpoch,1000,2,900);
    Inject(RESULT,100,syncEpoch,1000,2,900);
    Inject(RESULT,99,syncEpoch,2000,1,900);
    assert(displays==1 && appRangeStatus.distanceMm==1000 && appRangeStatus.direction==-1);
    Inject(RESULT,101,syncEpoch+1,2000,1,900);assert(displays==1);
    clockNs+=1600000000ULL;AppRange_Process();assert(!appRangeStatus.locked && !appRangeStatus.valid);
  }
  appNetStatus.online=0;AppRange_Process();assert(!appRangeStatus.valid);
  /* Continuous audio detector runs even without a network lock, and can
   * detect three signatures with only four slices per incoming DMA block.
   * The old 128-sample advance falls behind and overruns in this scenario. */
  {
    int16_t pcm[512];
    uint32_t n,k,eventsBefore=appRangeStatus.events;
    for(n=0;n<24064;n+=256)
    {
      for(k=0;k<256;++k)
      {
        int32_t at=(int32_t)((n+k)%8000)-129;
        int16_t v=0;
        if(at>=0 && at<512) v=rangeUp[at];
        else if(at>=640 && at<1152) v=rangeDown[at-640];
        else if(at>=1280 && at<1792) v=rangeUp[at-1280];
        pcm[k*2]=v;pcm[k*2+1]=0;
      }
      clockNs=20000000000ULL+(uint64_t)(n+256)*62500ULL;
      AppRange_Audio(pcm,256);
      for(k=0;k<4;++k) AudioProcess();
    }
    assert(appRangeStatus.events-eventsBefore==3 && appRangeStatus.audioDrops==0);
    assert(AppRange_DisplayReady());
    AppRange_AudioError();AudioProcess();assert(appRangeStatus.audioDrops==1);
    assert(appRangeStatus.audioGapDrops==1 && appRangeStatus.audioOverruns==0);
    audioCount+=6400;assert(!AppRange_DisplayReady());AudioProcess();
    assert(appRangeStatus.audioDrops==2 && appRangeStatus.audioGapDrops==1 &&
           appRangeStatus.audioOverruns==1 && !appRangeStatus.pairDeltaValid);
  }
  /* Time advances during real DSP/pairing. A frozen mock clock hid unsigned
   * age underflow when the loop's cached 'now' predates a new event/result.
   * Exercise detection -> transmit in one call, including tick wraparound. */
  for(i=0;i<2;++i)
  {
    uint32_t k,txBefore=appRangeStatus.eventTxAttempts;
    uint32_t resultsBefore=appRangeStatus.results;
    uint64_t stamp;
    ClearMeasurements();
    clockNs=i ? (uint64_t)(UINT32_MAX-1U)*1000000ULL : 40000000000ULL;
    appNetStatus.online=1; connection=AppNet_PeerSession();
    appRangeStatus.locked=1; appRangeStatus.resultId=0;
    lastSyncNs=clockNs; lastSyncMs=HAL_GetTick(); lastStateMs=lastSyncMs;
    RangeSync_Reset(&syncModel); syncModel.locked=1;
    audioCount=2048; audioAnchor=clockNs; readSample=0;
    seenEpoch=audioEpoch;
    /* A newer period estimate must not alter this already captured window. */
    sampleNs=64000.0; windowSampleNs=62500.0; windowTimeReady=1;
    haveWindow=1; scan=0; lastDetection=0;
    windowBase=0; windowAnchorCount=2048; windowAnchorNs=audioAnchor;
    memset(window,0,sizeof(window));
    for(k=0;k<512;++k)
    {
      window[17+k]=rangeUp[k]; window[17+640+k]=rangeDown[k];
      window[17+1280+k]=rangeUp[k];
    }
    stamp=windowAnchorNs-(2048ULL-17)*62500ULL;
    if(APP_BOARD_ROLE==APP_BOARD_A)
      Inject(EVENT,1000+i,peerEpoch,stamp+2912050ULL,900,0);
    tickAdvanceMs=1;
    AppRange_Process();
    tickAdvanceMs=0;
    if(APP_BOARD_ROLE==APP_BOARD_B)
    {
      if(!pendingEventId || appRangeStatus.eventTxAttempts!=txBefore+1 || sent[5]!=EVENT)
      { puts("FAIL: new detection expired before EVENT transmission"); return 1; }
      if(pendingEventTime+2000<stamp || pendingEventTime>stamp+2000)
      { puts("FAIL: window timestamp changed with newer sample period"); return 1; }
    }
    else if(!pendingResultId || appRangeStatus.results!=resultsBefore+1 ||
            !appRangeStatus.valid || sent[5]!=RESULT || appRangeStatus.distanceMm!=1000)
    { puts("FAIL: new result expired before RESULT transmission"); return 1; }
  }
  printf("Role %s range FSM: pairing/sync, replay, generations, timeout, disconnect, advancing tick PASS\n",APP_BOARD_NAME);
  return 0;
}
