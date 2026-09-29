/* Private implementation included only by range_dsp.c, after Score/CoarseScore.
 * Fixed static scratch; no malloc or large stack. Fine correlations are spread
 * across main-loop calls so network and UI service continue between slices. */
#define EARLY_BACK 192U
#define EARLY_AHEAD 576U
#define EARLY_POINTS (EARLY_BACK+EARLY_AHEAD+9U)
#define EARLY_WORK 16U
typedef struct { float pos[3],q,mean; } EarlyPeak;
static float earlyScore[EARLY_POINTS][3];
static EarlyPeak earlyPeaks[EARLY_POINTS/2U+1U];
static uint32_t earlyState,earlyLo,earlyHi,earlyAt,earlyCount,earlyValid;
static RangeDspPeak earlySelected;
void RangeDsp_Reset(void) { earlyState=0;earlyValid=0; }

static int EarlyFinish(float *position,uint32_t *quality)
{
  uint32_t k,p,i,j,n=0,chosen=0;
  float strongest=0;
  /* Independent pulse maxima, with the same three-sample spread requirement
   * as the offline experiment. Local maxima cannot be adjacent. */
  for(k=4;k+4<earlyCount;++k) {
    float v=earlyScore[k][0]+earlyScore[k][1]+earlyScore[k][2];
    float left=earlyScore[k-1][0]+earlyScore[k-1][1]+earlyScore[k-1][2];
    float right=earlyScore[k+1][0]+earlyScore[k+1][1]+earlyScore[k+1][2];
    EarlyPeak candidate;float lo=1e9f,hi=-1e9f,sum=0,q=1;
    if(v<=left || v<right) continue;
    for(p=0;p<3;++p) {
      uint32_t at=k-3,t;float l,c,r,den,shift=0,pos;
      for(t=k-2;t<=k+3;++t) if(earlyScore[t][p]>earlyScore[at][p]) at=t;
      l=earlyScore[at-1][p];c=earlyScore[at][p];r=earlyScore[at+1][p];
      if(c<MIN_SCORE || c<l || c<=r) break;
      den=l-2*c+r;if(den < -0.00001f) shift=0.5f*(l-r)/den;
      if(shift < -0.5f || shift > 0.5f) shift=0;
      pos=(float)(earlyLo+at)+shift;candidate.pos[p]=pos;
      if(pos<lo)lo=pos;if(pos>hi)hi=pos;sum+=pos;
      if(c<q)q=c;
    }
    if(p!=3 || hi-lo>3)continue;
    candidate.q=sqrtf(q);candidate.mean=sum/3;
    if(n>=sizeof(earlyPeaks)/sizeof(earlyPeaks[0])) { ++rangeDspDiagnostics.candidateOverflow;return 0; }
    earlyPeaks[n++]=candidate;
  }
  /* Quality-order NMS merges the carrier sidelobes within 0.4 ms. */
  for(i=0;i<n;++i)for(j=i+1;j<n;++j)if(earlyPeaks[j].q>earlyPeaks[i].q) {
    EarlyPeak tmp=earlyPeaks[i];earlyPeaks[i]=earlyPeaks[j];earlyPeaks[j]=tmp;
  }
  for(i=0;i<n;++i) {
    if(earlyPeaks[i].q==0)continue;
    if(earlyPeaks[i].q>strongest)strongest=earlyPeaks[i].q;
    for(j=i+1;j<n;++j)if(fabsf(earlyPeaks[j].mean-earlyPeaks[i].mean)<=19.2f)earlyPeaks[j].q=0;
  }
  if(!strongest) { ++rangeDspDiagnostics.noCandidates;return 0; }
  for(i=0;i<n;++i)if(earlyPeaks[i].q>=strongest*0.65f &&
      (earlyPeaks[chosen].q<strongest*0.65f || earlyPeaks[i].mean<earlyPeaks[chosen].mean))chosen=i;
  earlySelected.quality=(uint32_t)(earlyPeaks[chosen].q*1000);
  for(p=0;p<3;++p)earlySelected.position[p]=earlyPeaks[chosen].pos[p]+p*RANGE_PULSE_STEP;
  *position=earlyPeaks[chosen].mean;*quality=earlySelected.quality;earlyValid=1;
  {
    float lo=earlyPeaks[chosen].pos[0],hi=lo;
    for(p=1;p<3;++p) {
      if(earlyPeaks[chosen].pos[p]<lo)lo=earlyPeaks[chosen].pos[p];
      if(earlyPeaks[chosen].pos[p]>hi)hi=earlyPeaks[chosen].pos[p];
    }
    rangeDspPeakSpreadSamples=(uint32_t)(hi-lo);
  }
  ++rangeDspDiagnostics.signatures;return 1;
}

static int EarlyFind(const int16_t *x,unsigned first,unsigned end,float *position,uint32_t *quality)
{
  uint32_t k,p,stop,limit=RANGE_WINDOW_SAMPLES-RANGE_SIGNATURE_SAMPLES;
  if(!earlyState) {
    earlyValid=0;
    if(end>RANGE_SCAN_ADVANCE)end=RANGE_SCAN_ADVANCE;
    for(k=first;k<end;k+=9) {
      float c=CoarseScore(x+k);
      if(c>rangeDspDiagnostics.coarseMax)rangeDspDiagnostics.coarseMax=c;
      if(c<0.04f)continue;
      ++rangeDspDiagnostics.coarsePassed;
      earlyLo=k>EARLY_BACK+4 ? k-EARLY_BACK-4:0;
      earlyHi=k+EARLY_AHEAD+4;if(earlyHi>limit)earlyHi=limit;
      earlyCount=earlyHi-earlyLo+1;earlyAt=0;earlyState=1;break;
    }
    if(!earlyState)return 0;
  }
  stop=earlyAt+EARLY_WORK;if(stop>earlyCount)stop=earlyCount;
  for(k=earlyAt;k<stop;++k)for(p=0;p<3;++p) {
    float c=Score(x+earlyLo+k+p*RANGE_PULSE_STEP,p==1?rangeDown:rangeUp);
    earlyScore[k][p]=c;
    if(c>rangeDspDiagnostics.pulseMax[p])rangeDspDiagnostics.pulseMax[p]=c;
  }
  earlyAt=stop;if(earlyAt<earlyCount)return -1;
  earlyState=0;
  return EarlyFinish(position,quality);
}
