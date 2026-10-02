#ifndef APP_AUDIO_VIEW_H
#define APP_AUDIO_VIEW_H
#include <stdint.h>
/* Read-only borrowed ring, main loop only. Snapshot acquires count/epoch
 * atomically after a memory barrier. Consumers never retain these pointers. */
typedef void (*AppAudioSnapshot)(uint64_t *count,uint32_t *epoch);
typedef struct {
  const volatile int16_t *ring;
  uint32_t ringSamples,epoch;
  uint64_t count;
  AppAudioSnapshot snapshot;
} AppAudioView;
typedef struct {
  uint64_t anchorSample;
  double anchorNs,periodNs; /* already mapped to the requested time domain */
} AppAudioTime;
#endif
