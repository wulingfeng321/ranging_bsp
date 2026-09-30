#ifndef CLAP_DSP_H
#define CLAP_DSP_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "app_board_config.h"
/* Streaming transient onset, main loop only. No chirp/template dependency. */
typedef struct {
  float previous,hp,noise,threshold,peak,energy;
  uint64_t onset,holdUntil;
  uint32_t warm,hits;
  uint8_t active,clipped;
} ClapDetector;
static void Clap_Reset(ClapDetector *s)
{ memset(s,0,sizeof(*s));s->noise=32; }
/* Returns 1 for qualified onset, -1 for isolated/clipped transient, 0 otherwise.
 * Onset is the FIRST threshold crossing, not the later peak/decision time. */
static int Clap_Push(ClapDetector *s,int16_t value,uint64_t sample,uint64_t *onset,uint32_t *quality)
{
  float x=value,level;
  s->hp=x-s->previous+0.98f*s->hp;s->previous=x;level=fabsf(s->hp);
  if(s->warm<APP_AUDIO_SAMPLE_RATE/2U) {
    s->noise+=(level-s->noise)*0.001f;++s->warm;return 0;
  }
  if(sample<s->holdUntil) return 0;
  if(!s->active) {
    float threshold=s->noise*8.0f;if(threshold<240) threshold=240;
    if(level<threshold) { s->noise+=(level-s->noise)*0.0005f;return 0; }
    s->onset=sample;s->threshold=threshold;s->peak=0;s->energy=0;s->hits=0;
    s->active=1;s->clipped=0;
  }
  if(level>s->peak) s->peak=level;
  s->energy+=level*level;
  if(level>s->threshold) ++s->hits;
  if(value>=32700 || value<=-32700) s->clipped=1;
  if(sample-s->onset<APP_AUDIO_SAMPLE_RATE/500U) return 0;
  s->active=0;
  if(s->clipped || s->hits<4 || s->energy<s->peak*s->peak*4.0f || s->peak<s->threshold*1.5f) {
    s->holdUntil=sample+APP_AUDIO_SAMPLE_RATE/20U;return -1;
  }
  *onset=s->onset;
  *quality=(uint32_t)(s->peak/s->threshold*100.0f);
  if(*quality>1000) *quality=1000;
  s->holdUntil=s->onset+APP_AUDIO_SAMPLE_RATE*3U/5U;
  return 1;
}
#endif
