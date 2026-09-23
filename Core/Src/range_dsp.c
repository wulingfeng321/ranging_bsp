#include "range_dsp.h"
#if APP_RANGE_AUDIO_PROFILE == APP_RANGE_AUDIO_LEGACY
#include "range_template.h"
#else
#include "range_template_wide.h"
#endif
#include "app_board_config.h"
#include <math.h>
#include <string.h>
uint32_t rangeDspPeakSpreadSamples;

#define MIN_SCORE ((APP_RANGE_MIN_QUALITY / 1000.0f) * \
                   (APP_RANGE_MIN_QUALITY / 1000.0f))

static float Score(const int16_t *x, const int16_t *tpl)
{
  unsigned i;
  float dot = 0, xx = 0, sum = 0;
  for (i = 0; i < 512; ++i)
  {
    float v = x[i];
    dot += v * tpl[i]; xx += v * v; sum += v;
  }
  xx -= sum * sum / 512.0f;
  if (xx < 512.0f * APP_RANGE_MIN_RMS * APP_RANGE_MIN_RMS) return 0;
  /* Templates are generated DC-removed and have separately measured energy. */
  return dot * dot / (xx * (tpl == rangeUp ? RANGE_UP_ENERGY : RANGE_DOWN_ENERGY));
}

#if APP_RANGE_AUDIO_PROFILE == APP_RANGE_AUDIO_LEGACY
static float Energy(const int16_t *x, unsigned n)
{
  unsigned i; float e = 0, s = 0;
  for (i = 0; i < n; ++i) { float v = x[i]; e += v*v; s += v; }
  return (e - s*s/n) / n;
}

int RangeDsp_Find(const int16_t *x, unsigned first, unsigned end,
                  float *position, uint32_t *quality)
{
  unsigned k, j, best;
  float v, peak, down, up2, left, right, shift, q, joint, bestJoint;
  if (end > RANGE_WINDOW_SAMPLES - RANGE_SIGNATURE_SAMPLES + 1U)
    end = RANGE_WINDOW_SAMPLES - RANGE_SIGNATURE_SAMPLES + 1U;
  /* Do not use stride 2: a 4 kHz-centred chirp at 16 kHz can have near-zero
   * correlation at every odd lag. Stride 3 also visits the opposite parity. */
  for (k = first; k < end; k += 3)
  {
    if (Score(x + k, rangeUp) < APP_RANGE_COARSE_SCORE) continue;
    peak = 0; best = k; bestJoint = 0;
    /* Use all three pulses to choose a common arrival, rather than letting
     * a distorted first pulse choose a sidelobe for the complete signature.
     * Only refine candidates that passed the inexpensive coarse search. */
    for (j = k > 8 ? k - 8 : 0; j <= k + 8 && j <= 256; ++j)
    {
      v = Score(x + j, rangeUp);
      if (v < MIN_SCORE) continue;
      down = Score(x + j + 640, rangeDown);
      if (down < MIN_SCORE) continue;
      up2 = Score(x + j + 1280, rangeUp);
      if (up2 < MIN_SCORE) continue;
      joint = v + down + up2;
      if (joint > bestJoint) { bestJoint = joint; peak = v; best = j; }
    }
    if (peak < MIN_SCORE) continue;
    down = Score(x + best + 640, rangeDown);
    up2 = Score(x + best + 1280, rangeUp);
    if (down < MIN_SCORE || up2 < MIN_SCORE) continue;
    v = Energy(x + best, 512);
    if (Energy(x + best + 512, 128) > v * APP_RANGE_MAX_GAP_ENERGY_RATIO ||
        Energy(x + best + 1152, 128) > v * APP_RANGE_MAX_GAP_ENERGY_RATIO) continue;
    shift = 0;
    if (best > 0 && best < 256)
    {
      left = Score(x + best - 1, rangeUp) +
             Score(x + best + 639, rangeDown) + Score(x + best + 1279, rangeUp);
      right = Score(x + best + 1, rangeUp) +
              Score(x + best + 641, rangeDown) + Score(x + best + 1281, rangeUp);
      v = left - 2.0f * bestJoint + right;
      if (v < -0.00001f) shift = 0.5f * (left - right) / v;
      if (shift < -0.5f || shift > 0.5f) shift = 0;
    }
    q = peak < down ? peak : down;
    if (up2 < q) q = up2;
    *position = best + shift;
    *quality = (uint32_t)(sqrtf(q) * 1000.0f);
    if (*quality > 1000) *quality = 1000;
    {
      unsigned pulse,at,start,finish,chosen;
      int lo=100,hi=-100;
      for(pulse=0;pulse<3;++pulse) {
        unsigned center=best+pulse*640;
        const int16_t *tpl=pulse==1 ? rangeDown : rangeUp;
        float strongest=-1;
        chosen=center; start=center>3 ? center-3 : 0;
        finish=center+3;
        if(finish>RANGE_WINDOW_SAMPLES-512) finish=RANGE_WINDOW_SAMPLES-512;
        for(at=start;at<=finish;++at) {
          float score=Score(x+at,tpl);
          if(score>strongest) { strongest=score; chosen=at; }
        }
        {
          int offset=(int)chosen-(int)center;
          if(offset<lo) lo=offset;
          if(offset>hi) hi=offset;
        }
      }
      rangeDspPeakSpreadSamples=(uint32_t)(hi-lo);
    }
    return 1;
  }
  return 0;
}

#else
int RangeDsp_Find(const int16_t *x, unsigned first, unsigned end,
                  float *position, uint32_t *quality)
{
  /* Fixed-size scratch, no heap. Compare before/after slice boundaries so an
   * earlier sidelobe is not accepted merely because the main peak is in the
   * next slice. Extra signature margin keeps the lookahead inside the window. */
  float joint[104], weakest[104];
  const float threshold=APP_RANGE_WIDE_SCORE>MIN_SCORE ? APP_RANGE_WIDE_SCORE : MIN_SCORE;
  unsigned start,finish,k,j,n;
  if(first>=256 || end<=first) return 0;
  if(end>first+64) end=first+64;
  if(end>256) end=256;
  start=first>3 ? first-3 : 0;
  finish=end+APP_RANGE_WIDE_LOOKAHEAD+3;
  if(finish>RANGE_WINDOW_SAMPLES-RANGE_SIGNATURE_SAMPLES+1)
    finish=RANGE_WINDOW_SAMPLES-RANGE_SIGNATURE_SAMPLES+1;
  n=finish-start;
  if(n>104) return 0;
  for(k=0;k<n;++k) {
    float a=Score(x+start+k,rangeUp),b,c;
    joint[k]=0; weakest[k]=0;
    if(a<threshold) continue;
    b=Score(x+start+k+576,rangeDown);
    c=Score(x+start+k+1152,rangeUp);
    if(b<threshold || c<threshold) continue;
    joint[k]=a+b+c; weakest[k]=fminf(a,fminf(b,c));
  }
  for(k=first;k<end;++k) {
    unsigned at=k-start,lo=at>3 ? at-3 : 0,hi=at+3;
    float peak=joint[at],strongest=peak,shift=0;
    int local=1;
    if(peak==0) continue;
    if(hi>=n) hi=n-1;
    for(j=lo;j<=hi;++j)
      if(joint[j]>peak || (j<at && joint[j]==peak)) local=0;
    if(!local) continue;
    hi=at+APP_RANGE_WIDE_LOOKAHEAD;
    if(hi>=n) hi=n-1;
    for(j=at+1;j<=hi;++j) if(joint[j]>strongest) strongest=joint[j];
    if(peak<strongest*APP_RANGE_WIDE_RELATIVE) continue;
    /* Each pulse must independently support this local arrival. */
    {
      int minOffset=3,maxOffset=-3;
      unsigned pulse;
      for(pulse=0;pulse<3;++pulse) {
        unsigned center=k+pulse*576,chosen=center;
        const int16_t *tpl=pulse==1 ? rangeDown : rangeUp;
        float best=-1;
        for(j=center>2 ? center-2 : 0;j<=center+2;++j) {
          float score=Score(x+j,tpl);
          if(score>best) { best=score; chosen=j; }
        }
        if((int)chosen-(int)center<minOffset) minOffset=(int)chosen-(int)center;
        if((int)chosen-(int)center>maxOffset) maxOffset=(int)chosen-(int)center;
      }
      if(maxOffset-minOffset>1) continue;
      rangeDspPeakSpreadSamples=(uint32_t)(maxOffset-minOffset);
    }
    /* Use un-gated adjacent correlations for sub-sample interpolation. */
    if(k>0) {
      float left=Score(x+k-1,rangeUp)+Score(x+k+575,rangeDown)+Score(x+k+1151,rangeUp);
      float right=Score(x+k+1,rangeUp)+Score(x+k+577,rangeDown)+Score(x+k+1153,rangeUp);
      float denom=left-2*peak+right;
      if(denom < -0.00001f) shift=0.5f*(left-right)/denom;
      if(shift < -0.5f || shift > 0.5f) shift=0;
    }
    *position=k+shift;
    *quality=(uint32_t)(sqrtf(weakest[at])*1000.0f);
    if(*quality>1000) *quality=1000;
    return 1;
  }
  return 0;
}
#endif

void RangeDsp_Candidates(const int16_t *x,float position,RangeDspPeaks *out)
{
  float score[29][3],joint[29],strongest=0;
  RangeDspPeak candidates[27];
  unsigned center=(unsigned)(position+0.5f),lo,hi,k,p,n=0,i,j;
  unsigned limit=RANGE_WINDOW_SAMPLES-RANGE_SIGNATURE_SAMPLES;
  memset(out,0,sizeof(*out));
  if(center>limit) return;
  lo=center>14 ? center-14 : 0; hi=center+14;
  if(hi>limit) hi=limit;
  for(k=lo;k<=hi;++k) {
    joint[k-lo]=0;
    for(p=0;p<3;++p) {
      score[k-lo][p]=Score(x+k+p*RANGE_PULSE_STEP,p==1 ? rangeDown:rangeUp);
      joint[k-lo]+=score[k-lo][p];
    }
  }
  /* Locate a joint peak, then locate each pulse independently within +/-1.
   * Requiring all three maxima at the exact same integer lag rejects small
   * channel/phase distortions before the other board can establish agreement. */
  for(k=lo;k<=hi;++k) {
    RangeDspPeak candidate; float q=1; int valid=1;
    if((k==lo && lo!=0) || k==hi) continue;
    if((k>lo && joint[k-lo]<joint[k-lo-1]) || joint[k-lo]<=joint[k-lo+1]) continue;
    for(p=0;p<3;++p) {
      unsigned at=k;
      float v,l,r,shift=0,d;
      if(k>lo && score[k-lo-1][p]>score[at-lo][p]) at=k-1;
      if(k<hi && score[k-lo+1][p]>score[at-lo][p]) at=k+1;
      if((at==lo && lo!=0) || at==hi) { valid=0; break; }
      v=score[at-lo][p]; l=at ? score[at-lo-1][p]:0; r=score[at-lo+1][p];
      if(v<MIN_SCORE || v<l || v<=r) { valid=0; break; }
      d=l-2*v+r;
      if(at && d < -0.00001f) shift=0.5f*(l-r)/d;
      if(shift < -0.5f || shift > 0.5f) shift=0;
      candidate.position[p]=at+p*RANGE_PULSE_STEP+shift;
      if(v<q) q=v;
    }
    if(!valid) continue;
    candidate.quality=(uint32_t)(sqrtf(q)*1000.0f);
    if(candidate.quality>1000) candidate.quality=1000;
    /* Several joint seeds may converge to the same three pulse peaks. */
    for(i=0;i<n;++i) {
      int same=1;
      for(p=0;p<3;++p)
        if(fabsf(candidate.position[p]-candidates[i].position[p])>0.75f) same=0;
      if(same) break;
    }
    if(i<n) continue;
    candidates[n++]=candidate;
    if(candidate.quality>strongest) strongest=(float)candidate.quality;
  }
  /* Rank by weakest pulse. Do not silently truncate near-equal alternatives. */
  for(i=0;i<n;++i) for(j=i+1;j<n;++j)
    if(candidates[j].quality>candidates[i].quality) {
      RangeDspPeak temp=candidates[i]; candidates[i]=candidates[j]; candidates[j]=temp;
    }
  out->count=n>RANGE_MAX_PEAKS ? RANGE_MAX_PEAKS:n;
  for(i=0;i<out->count;++i) out->peak[i]=candidates[i];
  if(n>RANGE_MAX_PEAKS && candidates[RANGE_MAX_PEAKS].quality*100>=strongest*APP_RANGE_PEAK_RUNNER_PERCENT)
    out->overflow=1;
}

int RangeDsp_Distance(int64_t deltaNs, int32_t temperatureDeciC,
                      uint32_t *mm, int32_t *direction)
{
  uint64_t magnitude;
  int32_t speed; /* mm/s, c = 331.3 + 0.606*T Celsius */
  if (temperatureDeciC < -100 || temperatureDeciC > 500 ||
      deltaNs < -20000000LL || deltaNs > 20000000LL) return 0;
  magnitude = (uint64_t)(deltaNs < 0 ? -deltaNs : deltaNs);
  speed = 331300 + (606 * temperatureDeciC) / 10;
  *mm = (uint32_t)((magnitude * (uint32_t)speed + 500000000ULL) / 1000000000ULL);
  *direction = magnitude <= 10000 ? 0 : (deltaNs > 0 ? 1 : -1);
  return *mm <= 5000U;
}
