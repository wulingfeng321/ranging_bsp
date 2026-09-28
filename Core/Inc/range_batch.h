#ifndef RANGE_BATCH_H
#define RANGE_BATCH_H
#include <stdint.h>
#include <string.h>
#include "app_board_config.h"
/* Signed millimetres preserve source-side information. No range-based
 * sample clipping: apply the known operating range only AFTER estimation. */
typedef struct {
  int32_t value[15], estimate;
  uint32_t quality[15], count, used, span, resultQuality, startMs, stage;
} RangeBatch;
/* stage: 0 idle, 1 collecting, 2 estimate, 3 too few, 4 unstable, 5 outside */
static void RangeBatch_Reset(RangeBatch *b) { memset(b,0,sizeof(*b)); }
static int32_t RangeBatch_Median(int32_t *a,uint32_t n)
{
  uint32_t i,j;
  for(i=1;i<n;++i) { int32_t v=a[i]; j=i;
    while(j && a[j-1]>v) { a[j]=a[j-1]; --j; } a[j]=v; }
  return (a[(n-1)/2]+a[n/2])/2;
}
static void RangeBatch_Finish(RangeBatch *b)
{
  int32_t sorted[15],center,lo,hi;
  uint32_t i,j,best=0,bestCount=0,bestSpan=0;
  if(b->stage!=1) return;
  b->used=0; b->span=0;
  if(b->count<APP_RANGE_BATCH_MIN_SAMPLES) { b->stage=3; return; }
  memcpy(sorted,b->value,b->count*sizeof(int32_t));
  (void)RangeBatch_Median(sorted,b->count); /* Sort without choosing a global center. */
  /* Find the most populated interval with the allowed total span. One-sided
   * outliers must not shift a global median and clip a valid majority cluster.
   * Equal counts prefer the tighter interval; the >50% population gate below
   * prevents two disjoint equal populations from producing a result. */
  for(i=0;i<b->count;++i) {
    uint32_t count,span;
    for(j=i+1;j<b->count;++j)
      if((uint32_t)(sorted[j]-sorted[i])>APP_RANGE_BATCH_MAX_SPAN_MM) break;
    count=j-i; span=(uint32_t)(sorted[j-1]-sorted[i]);
    if(count>bestCount || (count==bestCount && span<bestSpan)) {
      best=i; bestCount=count; bestSpan=span;
    }
  }
  b->used=bestCount; b->span=bestSpan;
  lo=sorted[best]; hi=sorted[best+bestCount-1];
  b->resultQuality=1000;
  for(i=0;i<b->count;++i) {
    if(b->value[i]<lo || b->value[i]>hi) continue;
    if(b->quality[i]<b->resultQuality) b->resultQuality=b->quality[i];
  }
  if(b->used<APP_RANGE_BATCH_MIN_SAMPLES ||
     b->used*2<=b->count ||
     b->used*100<b->count*APP_RANGE_BATCH_MIN_PERCENT ||
     b->span>APP_RANGE_BATCH_MAX_SPAN_MM) { b->stage=4; return; }
  b->estimate=RangeBatch_Median(sorted+best,b->used);
  center=b->estimate<0 ? -b->estimate : b->estimate;
  b->stage=(center>=APP_RANGE_BATCH_MIN_MM && center<=APP_RANGE_BATCH_MAX_MM) ? 2 : 5;
}
static void RangeBatch_Add(RangeBatch *b,int32_t mm,uint32_t quality,uint32_t now)
{
  if(b->stage!=1) { RangeBatch_Reset(b); b->stage=1; b->startMs=now; }
  if(b->count>=15) return;
  b->value[b->count]=mm; b->quality[b->count++]=quality;
}
#endif
