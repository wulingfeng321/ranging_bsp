#include "range_dsp.h"
#if APP_RANGE_AUDIO_PROFILE == APP_RANGE_AUDIO_LEGACY
#if APP_AUDIO_SAMPLE_RATE == 48000U
#include "range_template_48k.h"
#else
#include "range_template.h"
#endif
#else
#if APP_AUDIO_SAMPLE_RATE == 48000U
#include "range_template_wide_48k.h"
#else
#include "range_template_wide.h"
#endif
#endif
#include "app_board_config.h"
#include <math.h>
#include <string.h>
#if APP_RANGE_EARLY && defined(__ICCARM__)
#include "cmsis_compiler.h"
#endif
typedef char TemplateLengthCheck[(sizeof(rangeUp)/sizeof(rangeUp[0]) == RANGE_PULSE_SAMPLES) ? 1 : -1];
uint32_t rangeDspPeakSpreadSamples;
RangeDspDiagnostics rangeDspDiagnostics;
#ifdef RANGE_DSP_PROFILE
uint64_t rangeDspMacs;
#define COUNT_MACS(n) (rangeDspMacs+=(n))
#else
#define COUNT_MACS(n) ((void)0)
#endif
#define CANDIDATE_RADIUS (14U * APP_AUDIO_SCALE)
#define CANDIDATE_POINTS (2U * CANDIDATE_RADIUS + 1U)

#define MIN_SCORE ((APP_RANGE_MIN_QUALITY / 1000.0f) * \
                   (APP_RANGE_MIN_QUALITY / 1000.0f))

static float Score(const int16_t *x, const int16_t *tpl)
{
  unsigned i;
  float dot = 0, xx = 0, sum = 0;
  COUNT_MACS(RANGE_PULSE_SAMPLES);
#if APP_RANGE_EARLY
  /* Exact 64-bit accumulation prevents Q15 overflow. Cortex-M7 performs two
   * signed products per SMLALD; host fallback has identical integer sums. */
  int64_t d=0,e=0;int32_t s=0;
  for(i=0;i<RANGE_PULSE_SAMPLES;i+=2) {
#if defined(__ICCARM__)
    uint32_t a=__UNALIGNED_UINT32_READ(x+i);
    uint32_t b=__UNALIGNED_UINT32_READ(tpl+i);
    d=(int64_t)__SMLALD(a,b,(uint64_t)d);
    e=(int64_t)__SMLALD(a,a,(uint64_t)e);
    s+=(int16_t)a+(int16_t)(a>>16);
#else
    int32_t a=x[i],b=x[i+1];
    d+=(int64_t)a*tpl[i]+(int64_t)b*tpl[i+1];
    e+=(int64_t)a*a+(int64_t)b*b;s+=a+b;
#endif
  }
  dot=(float)d;xx=(float)e;sum=(float)s;
#else
  for (i = 0; i < RANGE_PULSE_SAMPLES; ++i)
  {
    float v = x[i];
    dot += v * tpl[i]; xx += v * v; sum += v;
  }
#endif
  xx -= sum * sum / (float)RANGE_PULSE_SAMPLES;
  if (xx < (float)RANGE_PULSE_SAMPLES * APP_RANGE_MIN_RMS * APP_RANGE_MIN_RMS) return 0;
  /* Templates are generated DC-removed and have separately measured energy. */
  return dot * dot / (xx * (tpl == rangeUp ? RANGE_UP_ENERGY : RANGE_DOWN_ENERGY));
}

/* Sparse first-stage gate only. Full-rate correlations determine all final
 * peak positions/qualities. The 48 kHz gate keeps the same 512 MACs and
 * 187.5 us start spacing as 16 kHz; no PCM is discarded from fine matching. */
static float CoarseScore(const int16_t *x)
{
#if APP_AUDIO_SAMPLE_RATE == 48000U
  unsigned i;
  float dot=0,xx=0,sum=0;
  COUNT_MACS(512);
#if APP_RANGE_EARLY
  int64_t d=0,e=0;int32_t s=0;
  for(i=0;i<RANGE_PULSE_SAMPLES;i+=APP_AUDIO_SCALE) {
    int32_t a=x[i];d+=(int64_t)a*rangeUp[i];e+=(int64_t)a*a;s+=a;
  }
  dot=(float)d;xx=(float)e;sum=(float)s;
#else
  for(i=0;i<RANGE_PULSE_SAMPLES;i+=APP_AUDIO_SCALE) {
    float v=x[i]; dot+=v*rangeUp[i]; xx+=v*v; sum+=v;
  }
#endif
  xx-=sum*sum/512.0f;
  if(xx<512.0f*APP_RANGE_MIN_RMS*APP_RANGE_MIN_RMS) return 0;
  return dot*dot/(xx*RANGE_UP_COARSE_ENERGY);
#else
  return Score(x,rangeUp);
#endif
}

#if APP_RANGE_EARLY
#include "range_early.h"
#else
void RangeDsp_Reset(void) {}
#endif

#if APP_RANGE_AUDIO_PROFILE == APP_RANGE_AUDIO_LEGACY
#if !APP_RANGE_EARLY
static float Energy(const int16_t *x, unsigned n)
{
  unsigned i; float e = 0, s = 0;
  for (i = 0; i < n; ++i) { float v = x[i]; e += v*v; s += v; }
  return (e - s*s/n) / n;
}
#endif

int RangeDsp_Find(const int16_t *x, unsigned first, unsigned end,
                  float *position, uint32_t *quality)
{
#if APP_RANGE_EARLY
  return EarlyFind(x,first,end,position,quality);
#else
  unsigned k, j, best;
  float v, peak, down, up2, left, right, shift, q, joint, bestJoint;
  if (end > RANGE_WINDOW_SAMPLES - RANGE_SIGNATURE_SAMPLES + 1U)
    end = RANGE_WINDOW_SAMPLES - RANGE_SIGNATURE_SAMPLES + 1U;
  /* Preserve the 187.5 us coarse grid: a 4 kHz-centred chirp would
   * repeatedly hit a correlation null on a 125 us grid. */
  for (k = first; k < end; k += 3*APP_AUDIO_SCALE)
  {
    unsigned passed=0;
    v=CoarseScore(x + k);
    if(v>rangeDspDiagnostics.coarseMax) rangeDspDiagnostics.coarseMax=v;
    if (v < APP_RANGE_COARSE_SCORE) continue;
    ++rangeDspDiagnostics.coarsePassed;
    peak = 0; best = k; bestJoint = 0;
    /* Use all three pulses to choose a common arrival, rather than letting
     * a distorted first pulse choose a sidelobe for the complete signature.
     * Only refine candidates that passed the inexpensive coarse search. */
    for (j = k > 8*APP_AUDIO_SCALE ? k - 8*APP_AUDIO_SCALE : 0; j <= k + 8*APP_AUDIO_SCALE && j <= RANGE_SCAN_ADVANCE; ++j)
    {
      v = Score(x + j, rangeUp);
      if(v>rangeDspDiagnostics.pulseMax[0]) rangeDspDiagnostics.pulseMax[0]=v;
      if (v < MIN_SCORE) continue;
      if(passed<1) passed=1;
      down = Score(x + j + RANGE_PULSE_STEP, rangeDown);
      if(down>rangeDspDiagnostics.pulseMax[1]) rangeDspDiagnostics.pulseMax[1]=down;
      if (down < MIN_SCORE) continue;
      if(passed<2) passed=2;
      up2 = Score(x + j + (2*RANGE_PULSE_STEP), rangeUp);
      if(up2>rangeDspDiagnostics.pulseMax[2]) rangeDspDiagnostics.pulseMax[2]=up2;
      if (up2 < MIN_SCORE) continue;
      joint = v + down + up2;
      if (joint > bestJoint) { bestJoint = joint; peak = v; best = j; }
    }
    if (peak < MIN_SCORE) { ++rangeDspDiagnostics.failPulse[passed]; continue; }
    down = Score(x + best + RANGE_PULSE_STEP, rangeDown);
    up2 = Score(x + best + (2*RANGE_PULSE_STEP), rangeUp);
    if (down < MIN_SCORE || up2 < MIN_SCORE) {
      ++rangeDspDiagnostics.failPulse[down<MIN_SCORE ? 1:2]; continue;
    }
    v = Energy(x + best, RANGE_PULSE_SAMPLES);
    {
      float gap1=Energy(x + best + RANGE_PULSE_SAMPLES,128*APP_AUDIO_SCALE);
      float gap2=Energy(x + best + RANGE_PULSE_STEP+RANGE_PULSE_SAMPLES,128*APP_AUDIO_SCALE);
      float ratio=(gap1>gap2 ? gap1:gap2)/v;
      if(!rangeDspDiagnostics.haveGap || ratio<rangeDspDiagnostics.gapMinRatio)
        rangeDspDiagnostics.gapMinRatio=ratio;
      rangeDspDiagnostics.haveGap=1;
      if(gap1>v*APP_RANGE_MAX_GAP_ENERGY_RATIO || gap2>v*APP_RANGE_MAX_GAP_ENERGY_RATIO) {
        ++rangeDspDiagnostics.gapRejected; continue;
      }
    }
    shift = 0;
    if (best > 0 && best < RANGE_SCAN_ADVANCE)
    {
      left = Score(x + best - 1, rangeUp) +
             Score(x + best + (RANGE_PULSE_STEP-1), rangeDown) + Score(x + best + (2*RANGE_PULSE_STEP-1), rangeUp);
      right = Score(x + best + 1, rangeUp) +
              Score(x + best + (RANGE_PULSE_STEP+1), rangeDown) + Score(x + best + (2*RANGE_PULSE_STEP+1), rangeUp);
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
        unsigned center=best+pulse*RANGE_PULSE_STEP;
        const int16_t *tpl=pulse==1 ? rangeDown : rangeUp;
        float strongest=-1;
        chosen=center; start=center>3*APP_AUDIO_SCALE ? center-3*APP_AUDIO_SCALE : 0;
        finish=center+3*APP_AUDIO_SCALE;
        if(finish>RANGE_WINDOW_SAMPLES-RANGE_PULSE_SAMPLES) finish=RANGE_WINDOW_SAMPLES-RANGE_PULSE_SAMPLES;
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
    ++rangeDspDiagnostics.signatures;
    return 1;
  }
  return 0;
#endif
}

#else
int RangeDsp_Find(const int16_t *x, unsigned first, unsigned end,
                  float *position, uint32_t *quality)
{
  /* Fixed-size scratch, no heap. Compare before/after slice boundaries so an
   * earlier sidelobe is not accepted merely because the main peak is in the
   * next slice. Extra signature margin keeps the lookahead inside the window. */
  float joint[104*APP_AUDIO_SCALE], weakest[104*APP_AUDIO_SCALE];
  const float threshold=APP_RANGE_WIDE_SCORE>MIN_SCORE ? APP_RANGE_WIDE_SCORE : MIN_SCORE;
  unsigned start,finish,k,j,n;
  if(first>=RANGE_SCAN_ADVANCE || end<=first) return 0;
  if(end>first+RANGE_SCAN_SLICE) end=first+RANGE_SCAN_SLICE;
  if(end>RANGE_SCAN_ADVANCE) end=RANGE_SCAN_ADVANCE;
  start=first>3*APP_AUDIO_SCALE ? first-3*APP_AUDIO_SCALE : 0;
  finish=end+APP_RANGE_WIDE_LOOKAHEAD*APP_AUDIO_SCALE+3*APP_AUDIO_SCALE;
  if(finish>RANGE_WINDOW_SAMPLES-RANGE_SIGNATURE_SAMPLES+1)
    finish=RANGE_WINDOW_SAMPLES-RANGE_SIGNATURE_SAMPLES+1;
  n=finish-start;
  if(n>104*APP_AUDIO_SCALE) return 0;
  for(k=0;k<n;++k) {
    float a,b,c;
    if(CoarseScore(x+start+k)<threshold*0.5f) { joint[k]=0; weakest[k]=0; continue; }
    a=Score(x+start+k,rangeUp);
    joint[k]=0; weakest[k]=0;
    if(a<threshold) continue;
    b=Score(x+start+k+RANGE_PULSE_STEP,rangeDown);
    c=Score(x+start+k+(2*RANGE_PULSE_STEP),rangeUp);
    if(b<threshold || c<threshold) continue;
    joint[k]=a+b+c; weakest[k]=fminf(a,fminf(b,c));
  }
  for(k=first;k<end;++k) {
    unsigned at=k-start,lo=at>3*APP_AUDIO_SCALE ? at-3*APP_AUDIO_SCALE : 0,hi=at+3*APP_AUDIO_SCALE;
    float peak=joint[at],strongest=peak,shift=0;
    int local=1;
    if(peak==0) continue;
    if(hi>=n) hi=n-1;
    for(j=lo;j<=hi;++j)
      if(joint[j]>peak || (j<at && joint[j]==peak)) local=0;
    if(!local) continue;
    hi=at+APP_RANGE_WIDE_LOOKAHEAD*APP_AUDIO_SCALE;
    if(hi>=n) hi=n-1;
    for(j=at+1;j<=hi;++j) if(joint[j]>strongest) strongest=joint[j];
    if(peak<strongest*APP_RANGE_WIDE_RELATIVE) continue;
    /* Each pulse must independently support this local arrival. */
    {
      int minOffset=3*APP_AUDIO_SCALE,maxOffset=-(int)(3*APP_AUDIO_SCALE);
      unsigned pulse;
      for(pulse=0;pulse<3;++pulse) {
        unsigned center=k+pulse*RANGE_PULSE_STEP,chosen=center;
        const int16_t *tpl=pulse==1 ? rangeDown : rangeUp;
        float best=-1;
        for(j=center>2*APP_AUDIO_SCALE ? center-2*APP_AUDIO_SCALE : 0;j<=center+2*APP_AUDIO_SCALE;++j) {
          float score=Score(x+j,tpl);
          if(score>best) { best=score; chosen=j; }
        }
        if((int)chosen-(int)center<minOffset) minOffset=(int)chosen-(int)center;
        if((int)chosen-(int)center>maxOffset) maxOffset=(int)chosen-(int)center;
      }
      if(maxOffset-minOffset>APP_AUDIO_SCALE) continue;
      rangeDspPeakSpreadSamples=(uint32_t)(maxOffset-minOffset);
    }
    /* Use un-gated adjacent correlations for sub-sample interpolation. */
    if(k>0) {
      float left=Score(x+k-1,rangeUp)+Score(x+k+(RANGE_PULSE_STEP-1),rangeDown)+Score(x+k+(2*RANGE_PULSE_STEP-1),rangeUp);
      float right=Score(x+k+1,rangeUp)+Score(x+k+(RANGE_PULSE_STEP+1),rangeDown)+Score(x+k+(2*RANGE_PULSE_STEP+1),rangeUp);
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
#if APP_RANGE_EARLY
  (void)x;(void)position;
  memset(out,0,sizeof(*out));
  if(earlyValid) { out->peak[0]=earlySelected;out->count=1; }
#else
  float score[CANDIDATE_POINTS][3],joint[CANDIDATE_POINTS],strongest=0;
  RangeDspPeak candidates[CANDIDATE_POINTS];
  unsigned center=(unsigned)(position+0.5f),lo,hi,k,p,n=0,i,j;
  unsigned limit=RANGE_WINDOW_SAMPLES-RANGE_SIGNATURE_SAMPLES;
  memset(out,0,sizeof(*out));
  if(center>limit) return;
  lo=center>CANDIDATE_RADIUS ? center-CANDIDATE_RADIUS : 0; hi=center+CANDIDATE_RADIUS;
  if(hi>limit) hi=limit;
  for(k=lo;k<=hi;++k) {
    joint[k-lo]=0;
    for(p=0;p<3;++p) {
      score[k-lo][p]=Score(x+k+p*RANGE_PULSE_STEP,p==1 ? rangeDown:rangeUp);
      joint[k-lo]+=score[k-lo][p];
    }
  }
  /* Locate a joint peak, then locate each pulse independently within +/-62.5 us.
   * Requiring all three maxima at the exact same integer lag rejects small
   * channel/phase distortions before the other board can establish agreement. */
  for(k=lo;k<=hi;++k) {
    RangeDspPeak candidate; float q=1; int valid=1;
    if((k==lo && lo!=0) || k==hi) continue;
    if((k>lo && joint[k-lo]<joint[k-lo-1]) || joint[k-lo]<=joint[k-lo+1]) continue;
    for(p=0;p<3;++p) {
      unsigned at=k,search,begin=k>APP_AUDIO_SCALE ? k-APP_AUDIO_SCALE : 0,finish=k+APP_AUDIO_SCALE;
      float v,l,r,shift=0,d;
      if(begin<lo) begin=lo;
      if(finish>hi) finish=hi;
      for(search=begin;search<=finish;++search)
        if(score[search-lo][p]>score[at-lo][p]) at=search;
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
        if(fabsf(candidate.position[p]-candidates[i].position[p])>0.75f*APP_AUDIO_SCALE) same=0;
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
  if(!out->count) ++rangeDspDiagnostics.noCandidates;
  if(out->overflow) ++rangeDspDiagnostics.candidateOverflow;
#endif
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
