#ifndef RANGE_PPS_H
#define RANGE_PPS_H

#include "range_sync.h"

/* D9 / PA15 / TIM2_CH1. The timer runs at 1 MHz and is kept low until lock. */
int RangePps_Init(void);
void RangePps_Update(const RangeSync *model, int locked);

#endif
