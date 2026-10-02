#define APP_RANGE_JOINT_PEAKS 1
#define main original_test_main
#include "test_app_range.c"
#undef main
#include "clap_dsp.h"
static double Pulse(double t)
{
  if(t<0 || t>.09) return 0;
  return (1-exp(-t/.0001))*exp(-t/.008)*(sin(t*31415.9265359)+.6*sin(t*18849.5559215));
}
static void Detect(double delay,int amplitude)
{
  int16_t pcm[APP_AUDIO_HALF_FRAMES*2];unsigned block,i;uint32_t rng=7;
  double onset=1.003137+delay;
  double period=1e9/APP_AUDIO_SAMPLE_RATE*(APP_BOARD_ROLE==APP_BOARD_A ? 1.000075:.999935);
  audioCount=audioAnchor=audioBlocks=0;++audioEpoch;RangeAudioTime_Reset(&audioTime);
  uiPage=APP_PAGE_CLAP;uiKnown=1;uiAckRevision=uiRevision;appRangeStatus.locked=1;
  syncModel.offset=APP_BOARD_ROLE==APP_BOARD_B ? 700000000:0;
  syncModel.slope=APP_BOARD_ROLE==APP_BOARD_B ? .00005:0;syncModel.origin=0;ClapReset();observedAudioEpoch=audioEpoch;observedAudioBlock=UINT32_MAX;
  for(block=0;block<100;++block) {
    uint64_t base=audioCount;
    for(i=0;i<APP_AUDIO_HALF_FRAMES;++i) {
      double t=(base+i)*period/1e9-onset;
      rng=rng*1664525U+1013904223U;
      pcm[2*i]=pcm[2*i+1]=(int16_t)(amplitude*(Pulse(t)+.7*Pulse(t-.035))+(int)((rng>>16)%81)-40);
    }
    clockNs=(uint64_t)((1e10+(base+APP_AUDIO_HALF_FRAMES)*period+syncModel.offset)/(1-syncModel.slope));
    AppRange_Audio(pcm,APP_AUDIO_HALF_FRAMES);ObservePageAudioTime();ClapProcess();
  }
  if(APP_BOARD_ROLE==APP_BOARD_B) { ClapNetwork(HAL_GetTick());assert(sent[5]==CLAP_EVENT); }
  assert(appClapStatus.events==1 && appClapStatus.drops==0);
  assert(fabs((double)(appClapStatus.arrivalUs*1000ULL)-1e10-onset*1e9)<300000); /* <=10.4cm onset budget */
}
/* Seed a received snapshot; rolling A-side history is tested independently. */
static void SeedHistory(uint32_t cm)
{
  static uint32_t id=2000;
  assert(AppClap_ReceiveResult(++id,cm,0,800,(1ULL<<54)|cm,HAL_GetTick()));
}
static void CheckHistory(void)
{
  uint32_t rev,id;
  uiPage=APP_PAGE_CLAP;uiKnown=1;uiAckRevision=uiRevision;uiPendingId=0;appRangeStatus.locked=1;
  ClapReset();
  SeedHistory(80);
  rev=uiRevision;
  assert(AppRange_ClearClapStats());assert(!AppRange_SettingsReady());
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    assert(!appClapStatus.recentCount && !appClapStatus.events && !appClapStatus.ready);
    testRevision=rev;id=appRangeStatus.rejected;
    Inject(CLAP_EVENT,222,7,clockNs-1000000,800,0);assert(appRangeStatus.rejected==id+1);
    testRevision=0;Inject(UI_ACK,uiRevision,0,0,0,0);assert(AppRange_SettingsReady());
    SeedHistory(100);Inject(UI_REQUEST,999,0,4,0,0);assert(!appClapStatus.recentCount);
    rev=uiRevision;SeedHistory(110);Inject(UI_REQUEST,999,0,4,0,0);
    assert(uiRevision==rev && appClapStatus.recentCount==1);
  } else {
    id=uiPendingId;UiProcess(HAL_GetTick());assert(sent[5]==UI_REQUEST && AppWire_Get64BE(sent+32)==4);
    testRevision=rev+1;testPayloadU=syncEpoch;testPayloadV=id;
    Inject(UI_STATE,rev+1,0,350,APP_PAGE_CLAP,0);
    assert(AppRange_SettingsReady() && !appClapStatus.recentCount);
    testRevision=rev;testPayloadU=(1ULL<<54)|104;
    Inject(CLAP_RESULT,1001,7,104,1,800);assert(!appClapStatus.recentCount);
    testRevision=0;testPayloadV=0;
    /* Missing two result packets is repaired by the next complete snapshot. */
    testPayloadU=(3ULL<<54)|103ULL|(102ULL<<9)|(101ULL<<18);
    Inject(CLAP_RESULT,1004,7,103,1,800);
    assert(appClapStatus.recentCount==3 && appClapStatus.recentCm[2]==101);
    Inject(CLAP_RESULT,1004,7,103,1,800);assert(appClapStatus.recentCount==3);
  }
  testRevision=0;testPayloadU=testPayloadV=0;
}
int main(void)
{
  unsigned i;uint32_t id;uint64_t onset,reference;uint32_t q;ClapDetector d;
  AppRange_Init();appNetStatus.online=1;AppRange_Process();syncEpoch=peerEpoch=7;
  Detect(0,8000);reference=(appClapStatus.arrivalUs*1000ULL);
  Detect(.004,1800);assert(fabs((double)(appClapStatus.arrivalUs*1000ULL)-reference-4000000)<500000);
  Detect(-.004,12000);assert(fabs((double)(appClapStatus.arrivalUs*1000ULL)-reference+4000000)<500000);
  Clap_Reset(&d);
  for(i=0;i<APP_AUDIO_SAMPLE_RATE*2;++i) assert(!Clap_Push(&d,(int16_t)(30*sin(i*.7)),i,&onset,&q));
  assert(!Clap_Push(&d,32000,i++,&onset,&q));
  { int rejected=0;unsigned end=i+APP_AUDIO_SAMPLE_RATE/100;
    for(;i<end;++i) { int r=Clap_Push(&d,0,i,&onset,&q);assert(r!=1);if(r<0) rejected=1; }
    assert(rejected);
  }
  Clap_Reset(&d);d.warm=APP_AUDIO_SAMPLE_RATE/2U;
  { int rejected=0;
    for(i=0;i<APP_AUDIO_SAMPLE_RATE/100U;++i) {
      int r=Clap_Push(&d,(int16_t)(i&1 ? 32767:-32767),i,&onset,&q);
      assert(r!=1);if(r<0) rejected=1;
    }
    assert(rejected);
  }
  uiPage=APP_PAGE_CLAP;uiKnown=1;uiAckRevision=uiRevision;appRangeStatus.locked=1;
  /* Audio discontinuity and backlog discard candidate state and relearn noise. */
  audioTime.ready=1;++audioEpoch;ClapProcess();assert(appClapStatus.drops==1 && !appClapStatus.ready);
  audioCount+=AUDIO_RING;ClapProcess();assert(appClapStatus.drops==2 && !appClapStatus.ready);
  ClapProcess();assert(appClapStatus.drops==2); /* restart caught up to producer */
  id=appRangeStatus.rejected;
  Inject(RESULT,991,7,2000,1,900);assert(appRangeStatus.rejected==id+1);
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    for(i=0;i<2;++i) {
      uint64_t local=clockNs-100000000ULL;
      AppClap_PublishLocal(local,850,NextSequence(),HAL_GetTick());
      Inject(CLAP_EVENT,100+i,7,local+(i ? -3000000LL:3000000LL),700,0);
      ClapNetwork(HAL_GetTick());assert(appClapStatus.valid);
      assert(appClapStatus.distanceCm==104 && appClapStatus.direction==(i ? -1:1));
    }
    id=appClapStatus.received;Inject(CLAP_EVENT,101,7,(appClapStatus.arrivalUs*1000ULL),700,0);assert(appClapStatus.received==id);
    Inject(CLAP_EVENT,102,8,(appClapStatus.arrivalUs*1000ULL),700,0);assert(appClapStatus.received==id);
  } else {
    testPayloadU=(1ULL<<54)|104;
    Inject(CLAP_RESULT,100,7,104,2,800);
    assert(appClapStatus.valid && appClapStatus.distanceCm==104 && appClapStatus.direction==-1);
    id=appClapStatus.updatedMs;clockNs+=100000000ULL;
    Inject(CLAP_RESULT,100,7,104,2,800);assert(appClapStatus.updatedMs==id);
    testRevision=uiRevision+1;Inject(CLAP_RESULT,101,7,200,1,800);
    assert(appClapStatus.distanceCm==104);testRevision=0;
  }
  clockNs+=11000000000ULL;ClapNetwork(HAL_GetTick());assert(!appClapStatus.valid);
  ApplyUi(APP_PAGE_STANDARD,250);assert(!appClapStatus.events && !appClapStatus.valid && !appClapStatus.recentCount);
  id=appClapStatus.received;Inject(CLAP_RESULT,200,7,10,1,800);assert(appClapStatus.received==id);
  CheckHistory();
  puts("PASS: history snapshot and linked clear; clap DMA onset at three levels/delays, echo suppression, silence/spike rejection, direction, transport, expiry and page isolation");
  return 0;
}
