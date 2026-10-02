/* Exercise the wave module as a separate translation unit without app_range. */
#include "app_wave.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static int16_t ring[32768];
static uint64_t newest=10000;
static uint32_t generation=7;
static void Snapshot(uint64_t *count,uint32_t *epoch) { *count=newest;*epoch=generation; }
int main(void)
{
  AppAudioView audio={ring,32768,7,10000,Snapshot};
  AppAudioTime time={10000,1000000000.0,1000000000.0/48000.0};
  int16_t trace[11];unsigned i;
  for(i=0;i<32768;++i) ring[i]=(int16_t)i;
  AppWave_Reset();
  assert(AppWave_Read(&audio,&time,1050000000ULL,0,trace,11));
  for(i=0;i<11;++i) assert(abs(trace[i]-(7600+(int)i*48))<=1);
  /* DMA completion during a read is OK until the historical window is overwritten. */
  newest+=768;assert(AppWave_Read(&audio,&time,1050000000ULL,0,trace,11));
  newest+=32768;assert(!AppWave_Read(&audio,&time,1050000000ULL,0,trace,11));
  newest=10000;generation=8;assert(!AppWave_Read(&audio,&time,1050000000ULL,0,trace,11));
  generation=7;assert(!AppWave_Read(&audio,&time,1200000000ULL,0,trace,11));
  assert(!AppWave_Read(&audio,&time,1050000000ULL,0,trace,1));
  assert(!AppWave_AcceptClock(1,1,1000000000ULL,5000));
  assert(!AppWave_AcceptClock(1,2000000000ULL,0,5000));
  assert(!AppWave_AcceptClock(1,2000000000ULL,1000000000ULL,5001));
  assert(AppWave_AcceptClock(UINT32_MAX,2000000000ULL,1000000000ULL,5000));
  assert(AppWave_AcceptClock(1,2100000000ULL,2000000000ULL,5000)); /* sequence wrap */
  assert(AppWave_GetClock()->periodPs==2100000000ULL);
  assert(AppWave_AcceptClock(UINT32_MAX,2000000000ULL,1000000000ULL,5000));
  assert(AppWave_GetClock()->originNs==2000000000ULL); /* old snapshot ignored */
  AppWave_Reset();assert(!AppWave_GetClock()->periodPs && !AppWave_GetClock()->originNs);
  puts("PASS: separate wave module interpolation, overwrite/epoch rejection and clock validation/replay");
  return 0;
}
