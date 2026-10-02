#ifndef BOARD_AUDIO_H
#define BOARD_AUDIO_H
#include <stdint.h>

typedef enum {
  BOARD_AUDIO_STARTING, BOARD_AUDIO_RUNNING, BOARD_AUDIO_INIT_ERROR,
  BOARD_AUDIO_START_ERROR, BOARD_AUDIO_DATA_ERROR
} BoardAudioState;
typedef void (*BoardAudioBlockHandler)(const volatile int16_t *pcm,uint32_t frames);
typedef void (*BoardAudioErrorHandler)(void);

/* Initialize once after SDRAM/LCD setup. Block and DMA error handlers run in
 * ISR context: bounded buffer copies/counters only, no SD/network/DSP work.
 * Process runs in the main loop and reports stalled/failed acquisition. */
void BoardAudio_Init(BoardAudioBlockHandler block,BoardAudioErrorHandler error);
void BoardAudio_Process(void);
BoardAudioState BoardAudio_GetState(void);
#endif
