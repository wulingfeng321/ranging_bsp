#ifndef RANGE_PEAK_PAIR_H
#define RANGE_PEAK_PAIR_H
#include <stdint.h>
#include "app_board_config.h"
#define RANGE_PEAK_COUNT 3U
typedef struct { int32_t offsetNs[3]; uint32_t quality; } RangePeak;
typedef struct { RangePeak peak[RANGE_PEAK_COUNT]; uint32_t count, overflow; } RangePeaks;
/* 0 inconsistent/invalid, 1 unique delay, 2 ambiguous. No distance prior. */
static int RangePeak_Pair(const RangePeaks *a,const RangePeaks *b,int64_t base,
                          int64_t *delta,uint32_t *quality,uint32_t *spread)
{
  int64_t delays[9]; uint32_t scores[9],qualities[9],spans[9],n=0,i,j,p,best=0;
  if(!a->count || !b->count || a->count>3 || b->count>3) return 0;
  if(a->overflow || b->overflow) return 2;
  for(i=0;i<a->count;++i) for(j=0;j<b->count;++j) {
    int64_t lo=INT64_MAX,hi=INT64_MIN,sum=0;
    if(a->peak[i].quality<APP_RANGE_MIN_QUALITY || a->peak[i].quality>1000 ||
       b->peak[j].quality<APP_RANGE_MIN_QUALITY || b->peak[j].quality>1000) continue;
    for(p=0;p<3;++p) {
      int64_t d=base+(int64_t)b->peak[j].offsetNs[p]-a->peak[i].offsetNs[p];
      if(d<lo) lo=d;
      if(d>hi) hi=d;
      sum+=d;
    }
    if(hi-lo>APP_RANGE_PEAK_SPREAD_NS) continue;
    delays[n]=sum/3; spans[n]=(uint32_t)(hi-lo);
    scores[n]=a->peak[i].quality*b->peak[j].quality;
    qualities[n]=a->peak[i].quality<b->peak[j].quality ? a->peak[i].quality:b->peak[j].quality;
    if(n==0 || scores[n]>scores[best]) best=n;
    ++n;
  }
  if(!n) return 0;
  for(i=0;i<n;++i) {
    int64_t d=delays[i]-delays[best];
    if(d<0) d=-d;
    if(d>APP_RANGE_PEAK_SEPARATION_NS &&
       scores[i]*100>=scores[best]*APP_RANGE_PEAK_RUNNER_PERCENT) return 2;
  }
  *delta=delays[best]; *quality=qualities[best]; *spread=spans[best];
  return 1;
}
/* One 64-bit field: quality + three signed 250 ns offsets. */
static uint64_t RangePeak_Pack(const RangePeak *p)
{
  uint64_t word=(uint64_t)p->quality<<48; unsigned i;
  if(p->quality<APP_RANGE_MIN_QUALITY || p->quality>1000) return 0;
  for(i=0;i<3;++i) {
    int32_t v=p->offsetNs[i];
    if(v < -8191750 || v > 8191750) return 0;
    v=v>=0 ? (v+125)/250 : -((-v+125)/250);
    word|=(uint64_t)(uint16_t)v<<(32-16*i);
  }
  return word;
}
static int RangePeak_Unpack(uint64_t word,RangePeak *p)
{
  unsigned i;
  p->quality=(uint32_t)(word>>48);
  if(p->quality<APP_RANGE_MIN_QUALITY || p->quality>1000) return 0;
  for(i=0;i<3;++i) {
    uint32_t u=(uint32_t)(word>>(32-16*i))&65535U;
    if(u==32768U) return 0;
    p->offsetNs[i]=(u>32767U ? (int32_t)u-65536 : (int32_t)u)*250;
  }
  return 1;
}
#endif
