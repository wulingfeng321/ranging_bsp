#ifndef POSITION_DSP_H
#define POSITION_DSP_H
#include <math.h>
#include <stdint.h>
#include <string.h>
/* Dedicated 12 ms, 2-6 kHz Hann chirp; stereo pairs averaged to 24 kHz.
 * Main-loop only, bounded scan slices. Physical layout: L top, R bottom (confirmed on the boards). */
#define POS_FS 24000U
#define POS_N 288U
#define POS_RING 4096U
static float Position_Template(double t,int quadrature)
{
  double w;
  if(t<0 || t>=0.012) return 0;
  w=0.5-0.5*cos(6.283185307179586*t/0.012);
  return (float)(w*(quadrature ? cos(6.283185307179586*(2000*t+0.5*(4000/0.012)*t*t)) :
    sin(6.283185307179586*(2000*t+0.5*(4000/0.012)*t*t))));
}
static float Position_Tone(double t) { return Position_Template(t,0); }
typedef struct {
  float templ[POS_N],quad[POS_N],energy,quadEnergy;
  float best[2],left[2],right[2],prev[2];
  uint64_t peak[2],start;
  uint8_t active;
} PositionDetector;
static void Position_Init(PositionDetector *s)
{
  unsigned i; float mean=0,quadMean=0;
  memset(s,0,sizeof(*s));
  for(i=0;i<POS_N;++i) {
    s->templ[i]=(Position_Tone((2*i)/48000.0)+Position_Tone((2*i+1)/48000.0))*0.5f;
    s->quad[i]=(Position_Template((2*i)/48000.0,1)+Position_Template((2*i+1)/48000.0,1))*0.5f;
    mean+=s->templ[i];quadMean+=s->quad[i];
  }
  mean/=POS_N;quadMean/=POS_N;
  for(i=0;i<POS_N;++i) { s->templ[i]-=mean; s->energy+=s->templ[i]*s->templ[i];
    s->quad[i]-=quadMean;s->quadEnergy+=s->quad[i]*s->quad[i]; }
}
/* Cheap energy gate avoids full template correlation throughout silence. */
static float Position_Energy(const int16_t *v)
{
  unsigned i;float sum=0,energy=0;
  for(i=0;i<POS_N;++i) { float x=v[i];sum+=x;energy+=x*x; }
  return (energy-sum*sum/POS_N)/POS_N;
}
static void Position_ResetPeaks(PositionDetector *s)
{
  memset(s->best,0,sizeof(s->best));memset(s->left,0,sizeof(s->left));
  memset(s->right,0,sizeof(s->right));memset(s->prev,0,sizeof(s->prev));
  memset(s->peak,0,sizeof(s->peak));s->start=0;s->active=0;
}
static float Position_Score(const PositionDetector *s,const int16_t *v)
{
  float sum=0,energy=0,cross=0; unsigned i;
  for(i=0;i<POS_N;++i) { float x=v[i]; if(x<=-32700 || x>=32700) return 0; sum+=x; energy+=x*x; cross+=x*s->templ[i]; }
  energy-=sum*sum/POS_N;
  if(energy<POS_N*32.0f*32.0f) return 0;
  return cross*cross/(s->energy*energy+1.0f);
}
/* Phase-independent coarse match. Eight-sample stride cannot rely on a
 * sine-only score: its carrier zeros would miss perfectly good chirps. */
/* Read the ring directly: one traversal combines DC removal, level and both
 * projections. No temporary copy or separate energy pass in the hot scan. */
static float Position_CoarseRing(const PositionDetector *s,const volatile int16_t *v,
                                uint32_t start,uint32_t mask,float *variance)
{
  float sum=0,energy=0,a=0,b=0;unsigned i,clipped=0;
  for(i=0;i<POS_N;++i) {
    float x=v[(start+i)&mask];
    if(x<=-32700 || x>=32700) clipped=1;
    sum+=x;energy+=x*x;a+=x*s->templ[i];b+=x*s->quad[i];
  }
  energy-=sum*sum/POS_N;
  *variance=energy>0 ? energy/POS_N : 0;
  if(clipped || energy<POS_N*32.0f*32.0f) return 0;
  return (a*a/s->energy+b*b/s->quadEnergy)/(energy+1.0f);
}
static float Position_Coarse(const PositionDetector *s,const int16_t *v)
{
  float variance;
  return Position_CoarseRing(s,v,0,UINT32_MAX,&variance);
}
/* Fine search uses consecutive starts and finishes 2 ms after first match.
 * Local mic skew is <=0.167 ms including the allowed fixed bias. */
static int Position_Peak(PositionDetector *s,uint64_t sample,float q0,float q1,double at[2],uint32_t *quality)
{
  float q[2]; unsigned k; q[0]=q0; q[1]=q1;
  if(!s->active && (q0>0.30f || q1>0.30f)) {
    s->active=1; s->start=sample; s->best[0]=s->best[1]=0;
  }
  if(s->active) {
    for(k=0;k<2;++k) {
      if(sample==s->peak[k]+1) s->right[k]=q[k];
      if(q[k]>s->best[k]) { s->best[k]=q[k]; s->peak[k]=sample; s->left[k]=s->prev[k]; s->right[k]=q[k]; }
    }
  }
  s->prev[0]=q0; s->prev[1]=q1;
  if(!s->active || sample-s->start<48) return 0;
  s->active=0;
  if(s->best[0]<0.35f || s->best[1]<0.35f) return -1;
  for(k=0;k<2;++k) {
    float divisor=s->left[k]-2*s->best[k]+s->right[k];
    float fraction=divisor<-0.000001f ? 0.5f*(s->left[k]-s->right[k])/divisor : 0;
    if(fraction>0.5f) fraction=0.5f;
    if(fraction< -0.5f) fraction= -0.5f;
    at[k]=(double)s->peak[k]+fraction;
  }
  /* Raw channel delay includes an uncalibrated hardware offset. The final
   * geometry check remains strict AFTER bias correction. */
  if(fabs(at[0]-at[1])>4.0) return -1;
  *quality=(uint32_t)(1000*sqrtf(s->best[0]<s->best[1] ? s->best[0]:s->best[1]));
  return 1;
}
/* Four arrival times in A-clock ns: AL, AR, BL, BR. 0 deg = top, clockwise.
 * No distance estimate. Model assumes source roughly >=0.5m from rectangle. */
static int Position_Solve(const double t[4],float sound,int32_t *angle,uint32_t *residual)
{
  double dx=((t[2]-t[0])+(t[3]-t[1]))*0.5;
  double dy=((t[0]-t[1])+(t[2]-t[3]))*0.5; /* top minus bottom */
  double mismatch=fabs((t[1]-t[0])-(t[3]-t[2]));
  double ux=-dx*sound/130000000.0,uy=-dy*sound/20000000.0;
  double norm=sqrt(ux*ux+uy*uy),deg;
  *residual=(uint32_t)(mismatch+0.5);
  if(fabs(t[1]-t[0])>75000 || fabs(t[3]-t[2])>75000 ||
     fabs(t[2]-t[0])>420000 || fabs(t[3]-t[1])>420000 ||
     mismatch>18000 || norm<0.70 || norm>1.30) return 0;
  deg=atan2(ux,uy)*57.29577951308232;
  if(deg<0) deg+=360;
  *angle=((int32_t)(deg+0.5))%360;
  return 1;
}
#endif
