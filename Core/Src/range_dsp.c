#include "range_dsp.h"
#include "range_template.h"
#include "app_board_config.h"
#include <math.h>
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
