#include "range_sync.h"
#include <string.h>
#include <math.h>

void RangeSync_Reset(RangeSync *s) { memset(s, 0, sizeof(*s)); }
int RangeSync_Add(RangeSync *s, uint64_t b1, uint64_t a2, uint64_t a3, uint64_t b4)
{
  int64_t rtt;
  double x, y, mx = 0, my = 0, xx = 0, xy = 0, residual = 0;
  uint32_t i;
  if (b4 <= b1 || a3 < a2 || b4-b1 > 100000000ULL) return 0;
  rtt = (int64_t)(b4-b1) - (int64_t)(a3-a2);
  if (rtt <= 0 || rtt > 500000) return 0;
  if (s->bestRtt && rtt > (int64_t)s->bestRtt + 20000) return 0;
  if (!s->bestRtt || rtt < s->bestRtt) s->bestRtt = (uint32_t)rtt;
  x = (double)b1 + (double)(b4-b1)*0.5;
  y = ((double)((int64_t)b1-(int64_t)a2) + (double)((int64_t)b4-(int64_t)a3))*0.5;
  if (s->count && x <= s->origin) return 0;
  s->x[s->next] = x; s->y[s->next] = y;
  s->next = (s->next + 1) % 16;
  if (s->count < 16) ++s->count;
  s->origin = x;
  for (i=0; i<s->count; ++i) { mx += s->x[i]-x; my += s->y[i]; }
  mx /= s->count; my /= s->count;
  for (i=0; i<s->count; ++i)
  { double dx = s->x[i]-x-mx; xx += dx*dx; xy += dx*(s->y[i]-my); }
  s->slope = xx > 0 ? xy/xx : 0;
  s->offset = my - s->slope*mx;
  for (i=0; i<s->count; ++i)
  {
    double e = fabs(s->y[i] - s->offset - s->slope*(s->x[i]-x));
    if (e > residual) residual = e;
  }
  s->residual = (uint32_t)residual;
  s->uncertainty = s->residual + s->bestRtt/2;
  s->locked = s->count >= 8 && residual <= 10000 && fabs(s->slope) < 0.001;
  return 1;
}
uint64_t RangeSync_Master(const RangeSync *s, uint64_t localNs)
{
  double master = (double)localNs - s->offset - s->slope*((double)localNs-s->origin);
  return master > 0 ? (uint64_t)(master+0.5) : 0;
}
