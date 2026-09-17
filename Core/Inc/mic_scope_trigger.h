#ifndef MIC_SCOPE_TRIGGER_H
#define MIC_SCOPE_TRIGGER_H
#include <stdint.h>

/* Debug-only Up/Down/Up candidate detector, not a matched filter or timestamp.
 * 4 ms features, 1 ms start search, 32/8/32/8/32 ms signature at 16 kHz.
 * x holds physical sweep slots; oldest is the next write slot. */
static int MicScope_IsChirpCandidate(const int16_t x[][2], uint32_t frames,
                                    uint32_t oldest, uint32_t minLevel)
{
  uint32_t ch, phase, block, j, start, part, k, n;
  uint32_t level[40], crossings[40];
  int32_t sum, mean, v, prev;
  uint32_t count, energy, low, high, reference;
  int good;
  if (frames != 2560U) return 0;
  for (ch = 0; ch < 2; ++ch)
    for (phase = 0; phase < 64; phase += 16)
    {
      count = (frames - phase) / 64;
      for (block = 0; block < count; ++block)
      {
        sum = 0;
        for (j = 0; j < 64; ++j)
          sum += x[(oldest + phase + block * 64 + j) % frames][ch];
        mean = sum / 64;
        energy = 0;
        crossings[block] = 0;
        prev = x[(oldest + phase + block * 64) % frames][ch] - mean;
        for (j = 0; j < 64; ++j)
        {
          v = x[(oldest + phase + block * 64 + j) % frames][ch] - mean;
          energy += (uint32_t)(v < 0 ? -v : v);
          if (j && ((v >= 0) != (prev >= 0))) ++crossings[block];
          prev = v;
        }
        level[block] = energy / 64;
      }
      for (start = 0; start + 28 <= count; ++start)
      {
        good = 1;
        reference = 0;
        for (part = 0; part < 3; ++part)
        {
          n = start + part * 10;
          for (k = 1; k <= 6; ++k)
          {
            if (level[n + k] < minLevel || crossings[n + k] < 14 ||
                crossings[n + k] > 52) good = 0;
            reference += level[n + k];
          }
          low = crossings[n + 1] + crossings[n + 2];
          high = crossings[n + 5] + crossings[n + 6];
          if (part == 1) { k = low; low = high; high = k; }
          if (low > 64 || high < 65 || high < low + 12) good = 0;
        }
        reference /= 18;
        /* At least one quiet 4 ms block in each 8 ms gap. */
        for (part = 0; part < 2; ++part)
        {
          n = start + part * 10 + 8;
          energy = level[n] < level[n + 1] ? level[n] : level[n + 1];
          if (energy * 3U >= reference) good = 0;
        }
        if (good) return 1;
      }
    }
  return 0;
}
#endif
