#ifndef RANGE_WAVE_H
#define RANGE_WAVE_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "app_board_config.h"
/* Main-loop-only one-shot tone clock fit. Never aligns a trace to its own
 * zero crossing: only the common period is estimated, then held fixed. */
typedef struct {
  uint64_t baseSample, baseNs, nextSample;
  double sx,sy,sxx,sxy,firstCross,lastCross;
  uint32_t blocks,crossings;
  float previous;
  uint8_t armed;
  uint64_t periodPs;
} RangeWaveTone;
static void RangeWave_Reset(RangeWaveTone *s) { memset(s,0,sizeof(*s)); }
static void RangeWave_Add(RangeWaveTone *s,const int16_t *pcm,unsigned n,
                          uint64_t endSample,uint64_t endNs)
{
  unsigned i; int32_t sum=0,lo=32767,hi=-32768;
  double mean,x,y;
  if(s->periodPs) return;
  for(i=0;i<n;++i) { int v=pcm[i]; sum+=v; if(v<lo) lo=v; if(v>hi) hi=v; }
  if(hi-lo<256 || lo<=-32700 || hi>=32700) { RangeWave_Reset(s); return; }
  if(s->blocks && endSample-n!=s->nextSample) RangeWave_Reset(s);
  mean=(double)sum/n;
  if(!s->blocks) { s->baseSample=endSample; s->baseNs=endNs; }
  x=(double)(endSample-s->baseSample); y=(double)(endNs-s->baseNs);
  s->sx+=x; s->sy+=y; s->sxx+=x*x; s->sxy+=x*y;
  for(i=0;i<n;++i) {
    float v=(float)(pcm[i]-mean);
    if(v<-(hi-lo)*0.15f) s->armed=1;
    if(s->armed && (s->blocks || i) && s->previous<=0 && v>0) {
      s->armed=0;
      double at=(double)(endSample-n+i)-1.0-s->previous/(v-s->previous);
      if(s->crossings) {
        double step=at-s->lastCross;
        if(step<APP_AUDIO_SAMPLE_RATE/650.0 || step>APP_AUDIO_SAMPLE_RATE/350.0) {
          RangeWave_Reset(s); return;
        }
      } else s->firstCross=at;
      s->lastCross=at; ++s->crossings;
    }
    s->previous=v;
  }
  s->nextSample=endSample; ++s->blocks;
  if(endNs-s->baseNs>=5000000000ULL && s->crossings>1500 && s->blocks>100) {
    double divisor=s->blocks*s->sxx-s->sx*s->sx;
    double sampleNs=(s->blocks*s->sxy-s->sx*s->sy)/divisor;
    double period=sampleNs*(s->lastCross-s->firstCross)/(s->crossings-1);
    if(divisor>0 && sampleNs>APP_AUDIO_SAMPLE_NS*0.96 && sampleNs<APP_AUDIO_SAMPLE_NS*1.04 &&
       period>=1000000000.0/600 && period<=1000000000.0/400)
      s->periodPs=(uint64_t)(period*1000.0+0.5);
    else RangeWave_Reset(s);
  }
}
/* An absolute common window, not a per-board amplitude/phase trigger. */
static uint64_t RangeWave_Start(uint64_t presentNs,uint64_t originNs,uint64_t periodPs)
{
  uint64_t start=presentNs>100000000ULL ? presentNs-100000000ULL : 0;
  if(periodPs && originNs && start>=originNs) {
    uint64_t cycles=(start-originNs)*1000ULL/periodPs;
    start=originNs+(cycles*periodPs)/1000ULL;
  }
  return start;
}
#endif
