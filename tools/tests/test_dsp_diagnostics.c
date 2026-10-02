/* Deliberate failures in the actual detector, not a duplicate implementation. */
#include <assert.h>
#include <stdio.h>
#include "../../Core/Src/range_dsp.c"
static int16_t audio[RANGE_WINDOW_SAMPLES];
static void Reset(unsigned pulses)
{
  unsigned p;
  memset(audio,0,sizeof(audio));
  memset(&rangeDspDiagnostics,0,sizeof(rangeDspDiagnostics));
  for(p=0;p<pulses;++p)
    memcpy(audio+p*RANGE_PULSE_STEP,p==1 ? rangeDown:rangeUp,RANGE_PULSE_SAMPLES*sizeof(int16_t));
}
static int Find(void)
{
  float position=0; uint32_t quality=0;
  int found=RangeDsp_Find(audio,0,RANGE_SCAN_SLICE,&position,&quality);
  assert(rangeDspDiagnostics.coarsePassed==rangeDspDiagnostics.failPulse[0]+
    rangeDspDiagnostics.failPulse[1]+rangeDspDiagnostics.failPulse[2]+
    rangeDspDiagnostics.gapRejected+rangeDspDiagnostics.signatures);
  return found;
}
int main(void)
{
  unsigned i,p; RangeDspPeaks candidates;
  Reset(0); assert(!Find() && !rangeDspDiagnostics.coarsePassed && !rangeDspDiagnostics.haveGap);
  Reset(1); assert(!Find() && rangeDspDiagnostics.failPulse[1]>0);
  Reset(2); assert(!Find() && rangeDspDiagnostics.failPulse[2]>0);
  Reset(3); assert(Find() && rangeDspDiagnostics.signatures==1);
  assert(rangeDspDiagnostics.haveGap && rangeDspDiagnostics.gapMinRatio==0);
  for(p=0;p<3;++p) assert(rangeDspDiagnostics.pulseMax[p]>0.99f);
  /* Decimated gate sees a chirp, full-rate fine search sees cancellation. */
  Reset(1);
  for(i=0;i<RANGE_PULSE_SAMPLES;++i) if(i%3) audio[i]=(int16_t)(-rangeUp[i]/2);
  assert(!Find() && rangeDspDiagnostics.failPulse[0]>0);
  Reset(3);
  for(p=0;p<2;++p) for(i=0;i<384U;++i)
    audio[p*RANGE_PULSE_STEP+RANGE_PULSE_SAMPLES+i]=(int16_t)(i%2 ? 32767:-32767);
  assert(!Find() && rangeDspDiagnostics.gapRejected>0);
  assert(rangeDspDiagnostics.haveGap && rangeDspDiagnostics.gapMinRatio>APP_RANGE_MAX_GAP_ENERGY_RATIO);
  Reset(0); RangeDsp_Candidates(audio,0,&candidates);
  assert(candidates.count==0 && rangeDspDiagnostics.noCandidates==1);
  puts("PASS: actual 48k DSP coarse/fine stages, gap rejection, success, candidate-empty diagnostics");
  return 0;
}
