#include <stdio.h>
#include "range_batch.h"
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d\n",__LINE__); return 1; } } while(0)
int main(void)
{
  RangeBatch b; unsigned i;
  RangeBatch_Reset(&b);
  for(i=0;i<15;++i) RangeBatch_Add(&b,i<12 ? 177+(int)(i%7) : 400,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==2 && b.used==12 && b.estimate>=178 && b.estimate<=181);
  RangeBatch_Reset(&b);
  for(i=0;i<15;++i) RangeBatch_Add(&b,-180,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==2 && b.estimate==-180);
  RangeBatch_Reset(&b);
  for(i=0;i<5;++i) RangeBatch_Add(&b,180,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==3);
  RangeBatch_Reset(&b);
  for(i=0;i<6;++i) RangeBatch_Add(&b,180,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==2 && b.used==6);
  RangeBatch_Reset(&b);
  for(i=0;i<14;++i) RangeBatch_Add(&b,i<9 ? 110+(int)i*5 : 400,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==2 && b.used==9 && b.span==40);
  CHECK(b.estimate==130);
  /* Mirrored one-sided outliers must preserve source direction as well. */
  RangeBatch_Reset(&b);
  for(i=0;i<14;++i) RangeBatch_Add(&b,i<9 ? -110-(int)i*5 : -400,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==2 && b.used==9 && b.estimate==-130);
  /* Quality belongs to selected observations, not the discarded outliers. */
  RangeBatch_Reset(&b);
  for(i=0;i<15;++i) RangeBatch_Add(&b,i<9 ? 180 : 400,i<9 ? 800 : 300,0);
  RangeBatch_Finish(&b); CHECK(b.stage==2 && b.used==9 && b.resultQuality==800);
  RangeBatch_Reset(&b);
  for(i=0;i<12;++i) RangeBatch_Add(&b,i<6 ? 110 : 180,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==4); /* Two competing equal clusters. */
  RangeBatch_Reset(&b);
  for(i=0;i<15;++i) RangeBatch_Add(&b,100+(int)i*50,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==4); /* No concentration. */
  RangeBatch_Reset(&b);
  for(i=0;i<15;++i) RangeBatch_Add(&b,i<8 ? 110 : 180,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==4);
  RangeBatch_Reset(&b);
  for(i=0;i<15;++i) RangeBatch_Add(&b,240,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==5 && b.estimate==240);
  /* A systematic error remains an error: never force 110 mm to known 180. */
  RangeBatch_Reset(&b);
  for(i=0;i<15;++i) RangeBatch_Add(&b,110,600,0);
  RangeBatch_Finish(&b); CHECK(b.stage==2 && b.estimate==110);
  puts("PASS: outliers, negative direction, sparse/bimodal/outside data, no forced calibration");
  return 0;
}
