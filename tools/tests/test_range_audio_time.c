#include <stdio.h>
#include <math.h>
#include "range_audio_time.h"
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d\n",__LINE__); return 1; } } while(0)
int main(void)
{
  RangeAudioTime s;
  uint64_t origin=1000000000000ULL, sample, truth;
  unsigned i;
  double worst=0;
  RangeAudioTime_Reset(&s);
  for(i=1;i<=160;++i) {
    uint64_t delay=(i%5)*5000ULL;
    sample=(uint64_t)i*256; truth=origin+sample*62505ULL;
    if(i%17==0) delay+=1000000ULL;
    RangeAudioTime_Add(&s,sample,truth+delay);
    if(i>40) {
      double error=fabs(s.anchorNs-(double)truth);
      CHECK(s.ready);
      CHECK(fabs(s.periodNs-62505.0)<2.0);
      CHECK(error<25000.0);
      if(error>worst) worst=error;
    }
  }
  CHECK(s.inliers>=8);
  /* Reading the same DMA observation again must not increase its weight. */
  i=s.next; RangeAudioTime_Add(&s,sample,truth+999999); CHECK(s.next==i);
  RangeAudioTime_Reset(&s); CHECK(!s.ready && !s.count);
  for(i=1;i<=7;++i) RangeAudioTime_Add(&s,i*256,origin+i*256ULL*62500);
  CHECK(!s.ready);
  RangeAudioTime_Add(&s,8*256,origin+8*256ULL*62500);
  CHECK(s.ready && fabs(s.periodNs-62500)<0.001);
  CHECK(fabs(s.anchorNs-(double)(origin+8*256ULL*62500))<1);
  printf("PASS: period drift, 1 ms delayed callbacks, duplicate samples, warm-up/reset; worst anchor error %.1f us\n",worst/1000);
  return 0;
}
