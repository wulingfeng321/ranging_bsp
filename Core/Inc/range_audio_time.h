#ifndef RANGE_AUDIO_TIME_H
#define RANGE_AUDIO_TIME_H
#include <stdint.h>
#include <string.h>
#include <math.h>

/* Main-loop only. Fit sample number against 32 distinct DMA observations.
 * A second fit excludes timing outliers; never fit across an audio gap. */
typedef struct {
  uint64_t sample[32], time[32];
  uint32_t count, next, inliers, jitterNs;
  double periodNs, anchorNs;
  uint8_t ready;
} RangeAudioTime;

static void RangeAudioTime_Reset(RangeAudioTime *s)
{ memset(s,0,sizeof(*s)); }

static double RangeAudioTime_Median(double *v, unsigned n)
{
  unsigned i,j;
  for(i=1;i<n;++i) {
    double a=v[i]; j=i;
    while(j && v[j-1]>a) { v[j]=v[j-1]; --j; }
    v[j]=a;
  }
  return (v[(n-1)/2]+v[n/2])*0.5;
}

static void RangeAudioTime_Add(RangeAudioTime *s, uint64_t sample, uint64_t time)
{
  double x[32],y[32],r[32],sorted[32],a=0,b=0,median,limit;
  unsigned i,pass,n;
  uint8_t keep[32];
  if(s->count && sample<=s->sample[(s->next+31)%32]) return;
  s->sample[s->next]=sample; s->time[s->next]=time;
  s->next=(s->next+1)%32; if(s->count<32) ++s->count;
  s->ready=0;
  if(s->count<8) return;
  for(i=0;i<s->count;++i) {
    x[i]=(double)((int64_t)s->sample[i]-(int64_t)sample);
    y[i]=(double)((int64_t)s->time[i]-(int64_t)time);
    keep[i]=1;
  }
  for(pass=0;pass<2;++pass) {
    double mx=0,my=0,xx=0,xy=0;
    n=0;
    for(i=0;i<s->count;++i) if(keep[i]) { mx+=x[i]; my+=y[i]; ++n; }
    if(n<8) return;
    mx/=n; my/=n;
    for(i=0;i<s->count;++i) if(keep[i]) {
      double dx=x[i]-mx; xx+=dx*dx; xy+=dx*(y[i]-my);
    }
    if(xx<=0) return;
    b=xy/xx; a=my-b*mx;
    if(pass==0) {
      for(i=0;i<s->count;++i) sorted[i]=r[i]=y[i]-a-b*x[i];
      median=RangeAudioTime_Median(sorted,s->count);
      for(i=0;i<s->count;++i) sorted[i]=fabs(r[i]-median);
      limit=4.5*RangeAudioTime_Median(sorted,s->count);
      if(limit<20000.0) limit=20000.0;
      for(i=0;i<s->count;++i) keep[i]=fabs(r[i]-median)<=limit;
    }
  }
  s->inliers=n; s->jitterNs=0;
  for(i=0;i<s->count;++i) if(keep[i]) {
    double error=fabs(y[i]-a-b*x[i]);
    if(error>s->jitterNs) s->jitterNs=(uint32_t)error;
  }
  if(b<60000.0 || b>65000.0 || s->jitterNs>100000U) return;
  s->periodNs=b; s->anchorNs=(double)time+a; s->ready=1;
}
#endif
