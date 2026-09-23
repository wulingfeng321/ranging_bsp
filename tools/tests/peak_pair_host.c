#include "range_peak_pair.h"
int Test_Pair(const RangePeaks *a,const RangePeaks *b,int64_t base,
              int64_t *delta,uint32_t *quality,uint32_t *spread)
{ return RangePeak_Pair(a,b,base,delta,quality,spread); }
uint64_t Test_Pack(const RangePeak *p) { return RangePeak_Pack(p); }
int Test_Unpack(uint64_t word,RangePeak *p) { return RangePeak_Unpack(word,p); }
