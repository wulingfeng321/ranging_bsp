#ifndef RANGE_SYNC_H
#define RANGE_SYNC_H
#include <stdint.h>
typedef struct {
  double x[16], y[16], origin, offset, slope;
  uint32_t count, next, bestRtt, residual, uncertainty;
  uint8_t locked;
} RangeSync;
void RangeSync_Reset(RangeSync *s);
int RangeSync_Add(RangeSync *s, uint64_t b1, uint64_t a2, uint64_t a3, uint64_t b4);
uint64_t RangeSync_Master(const RangeSync *s, uint64_t localNs);
#endif
