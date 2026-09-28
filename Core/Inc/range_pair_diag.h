#ifndef RANGE_PAIR_DIAG_H
#define RANGE_PAIR_DIAG_H
#include <stdint.h>
/* Snapshot of the latest rejected event pair; never used to choose a result.
 * reason: 1 multiple delays, 2 overflow, 3 no consistent pair, 4 invalid input.
 * A/B candidate indices are one-based; zero means no candidate. */
typedef struct {
  uint32_t serial;
  uint8_t reason, countA, countB, pairs, bestA, bestB, runnerA, runnerB;
  int32_t bestNs, runnerNs;
  uint32_t bestScore, runnerScore, bestSpanNs, runnerSpanNs;
} RangePairDiag;
static inline uint64_t RangePairDiag_Meta(const RangePairDiag *d)
{
  return (uint64_t)d->reason | ((uint64_t)d->countA<<8) |
    ((uint64_t)d->countB<<16) | ((uint64_t)d->pairs<<24) |
    ((uint64_t)d->bestA<<32) | ((uint64_t)d->bestB<<40) |
    ((uint64_t)d->runnerA<<48) | ((uint64_t)d->runnerB<<56);
}
/* Decode to a temporary snapshot first; bad packets cannot partly update LCD. */
static inline int RangePairDiag_Decode(RangePairDiag *d,uint64_t meta,
    uint64_t delays,uint64_t scores,uint64_t spans,uint64_t serial)
{
  RangePairDiag t;
  t.reason=(uint8_t)meta; t.countA=(uint8_t)(meta>>8);
  t.countB=(uint8_t)(meta>>16); t.pairs=(uint8_t)(meta>>24);
  t.bestA=(uint8_t)(meta>>32); t.bestB=(uint8_t)(meta>>40);
  t.runnerA=(uint8_t)(meta>>48); t.runnerB=(uint8_t)(meta>>56);
  t.bestNs=(int32_t)(uint32_t)(delays>>32); t.runnerNs=(int32_t)(uint32_t)delays;
  t.bestScore=(uint32_t)(scores>>32); t.runnerScore=(uint32_t)scores;
  t.bestSpanNs=(uint32_t)(spans>>32); t.runnerSpanNs=(uint32_t)spans;
  t.serial=(uint32_t)serial;
  if(!serial || serial>UINT32_MAX || t.reason<1 || t.reason>4 ||
     t.countA>3 || t.countB>3 || t.pairs>9 ||
     t.bestA>t.countA || t.runnerA>t.countA ||
     t.bestB>t.countB || t.runnerB>t.countB ||
     t.bestScore>1000000 || t.runnerScore>t.bestScore ||
     t.bestSpanNs>40000 || t.runnerSpanNs>40000 ||
     t.bestNs < -40000000 || t.bestNs>40000000 ||
     t.runnerNs < -40000000 || t.runnerNs>40000000) return 0;
  if(t.reason==1 && (!t.bestScore || !t.runnerScore || !t.bestA ||
     !t.bestB || !t.runnerA || !t.runnerB || t.pairs<2)) return 0;
  *d=t; return 1;
}
#endif
