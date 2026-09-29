#define APP_RANGE_JOINT_PEAKS 1
#define main original_test_main
#include "test_app_range.c"
#undef main
#include <math.h>
static double signalPhase,adcPeriod;
static uint64_t trueMaster;
static void Feed(unsigned blocks)
{
  unsigned b,i; int16_t pcm[APP_AUDIO_HALF_FRAMES*2];
  for(b=0;b<blocks;++b) {
    uint64_t begin=audioCount;
    for(i=0;i<APP_AUDIO_HALF_FRAMES;++i) {
      double seconds=(double)(begin+i)*adcPeriod/1e9;
      pcm[2*i]=(int16_t)(1000+12000*sin(seconds*500.037*6.283185307179586-signalPhase));
      pcm[2*i+1]=123; /* Right channel is deliberately unrelated. */
    }
    trueMaster=10000000000ULL+(uint64_t)((begin+APP_AUDIO_HALF_FRAMES)*adcPeriod);
    clockNs=APP_BOARD_ROLE==APP_BOARD_B ? (uint64_t)((trueMaster+700000000.0)/(1.0-syncModel.slope)) : trueMaster;
    clockNs+=(b%3)*1500U; /* Callback jitter is not a sample clock. */
    AppRange_Audio(pcm,APP_AUDIO_HALF_FRAMES); if(b%3==2) WaveObserve();
  }
}
static double CheckTrace(void)
{
  int16_t trace[456]; unsigned i; double maxError=0;
  uint64_t present=(trueMaster/50000000ULL+1)*50000000ULL;
  uint64_t start=RangeWave_Start(present,waveOriginNs,wavePeriodPs);
  assert(AppRange_WaveRead(present,trace,456));
  for(i=0;i<456;++i) {
    double seconds=((double)start-10000000000.0+i*10000000.0/455)/1e9;
    double expected=1000+12000*sin(seconds*500.037*6.283185307179586-signalPhase);
    double error=fabs(expected-trace[i]); if(error>maxError) maxError=error;
  }
  assert(maxError<200); /* <0.54% cycle worst-case around the zero crossing. */
  return maxError;
}
int main(void)
{
  uint64_t period,origin; double before,after; int16_t dummy[456];
  AppRange_Init(); appNetStatus.online=1; AppRange_Process();
  uiKnown=1; uiAckRevision=uiRevision; uiPage=APP_PAGE_WAVE;
  appRangeStatus.locked=1; peerEpoch=syncEpoch=7;
  syncModel.slope=APP_BOARD_ROLE==APP_BOARD_B ? 0.000060 : 0;
  syncModel.offset=APP_BOARD_ROLE==APP_BOARD_B ? 700000000.0 : 0; syncModel.origin=0;
  adcPeriod=APP_AUDIO_SAMPLE_NS*(APP_BOARD_ROLE==APP_BOARD_A ? 1.000080 : 0.999920);
  Feed(450);
  assert(audioTime.ready && !appRangeStatus.events);
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    assert(wavePeriodPs && fabs(1e12/wavePeriodPs-500.037)<0.002);
  } else {
    testPayloadU=testPayloadV=0;
    Inject(WAVE_CLOCK,900,syncEpoch,1999852011ULL,11000000000ULL,5000);
    assert(wavePeriodPs==1999852011ULL);
    Inject(WAVE_CLOCK,899,syncEpoch,2000000000ULL,11000000000ULL,5000);
    assert(wavePeriodPs==1999852011ULL); /* Out-of-order state never reverses phase. */
    Inject(WAVE_CLOCK,901,syncEpoch+1,2000000000ULL,11000000000ULL,5000);
    assert(wavePeriodPs==1999852011ULL);
  }
  period=wavePeriodPs; origin=waveOriginNs; before=CheckTrace();
  signalPhase=1.1; Feed(80); after=CheckTrace();
  assert(period==wavePeriodPs && origin==waveOriginNs); /* Movement must NOT retrigger either axis. */
  captureBusy=1; assert(!AppRange_WaveRead(trueMaster,dummy,456)); captureBusy=0;
  appRangeStatus.locked=0;
  assert(!AppRange_WaveRead(trueMaster,dummy,456));
  assert(AppRange_WaveRead(0,dummy,456)); /* Free running before synchronization. */
  AppRange_AudioError(); assert(!AppRange_WaveRead(0,dummy,456));
  Feed(3); assert(!audioTime.ready && !wavePeriodPs);
  printf("PASS: %s real PCM, 80ppm ADC drift, clock mapping, 500.037Hz fit, phase preserved; errors %.1f/%.1f counts\n",APP_BOARD_NAME,before,after);
  return 0;
}
