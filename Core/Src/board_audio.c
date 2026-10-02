#include "board_audio.h"
#include "app_board_config.h"
#include "stm32746g_discovery_audio.h"

/* Preserve the baseline non-cacheable SDRAM address and 16 ms DMA halves. */
#define AUDIO_BUFFER ((uint16_t *)0xC0100000U)
#define DMA_WORDS (APP_AUDIO_HALF_FRAMES * 4U)
extern SAI_HandleTypeDef haudio_in_sai;
static volatile uint32_t receivedBlocks,dmaErrors;
static uint32_t seenBlocks,lastReceived;
static BoardAudioState state=BOARD_AUDIO_STARTING;
static BoardAudioBlockHandler onBlock;
static BoardAudioErrorHandler onError;

static void StoreHalf(uint32_t offset)
{
  if(onBlock) onBlock((const volatile int16_t *)AUDIO_BUFFER+offset,APP_AUDIO_HALF_FRAMES);
  ++receivedBlocks;
}
void BSP_AUDIO_IN_HalfTransfer_CallBack(void) { StoreHalf(0); }
void BSP_AUDIO_IN_TransferComplete_CallBack(void) { StoreHalf(APP_AUDIO_HALF_FRAMES*2U); }
void BSP_AUDIO_IN_Error_CallBack(void) { ++dmaErrors;if(onError) onError(); }

void BoardAudio_Init(BoardAudioBlockHandler block,BoardAudioErrorHandler error)
{
  onBlock=block;onError=error;receivedBlocks=dmaErrors=seenBlocks=0;
  state=BOARD_AUDIO_STARTING;
  if(BSP_AUDIO_IN_Init(APP_AUDIO_SAMPLE_RATE,16,2)!=AUDIO_OK) {
    state=BOARD_AUDIO_INIT_ERROR;return;
  }
  HAL_NVIC_DisableIRQ(AUDIO_IN_INT_IRQ);
  if(BSP_AUDIO_IN_Record(AUDIO_BUFFER,DMA_WORDS)!=AUDIO_OK ||
     HAL_SAI_GetState(&haudio_in_sai)!=HAL_SAI_STATE_BUSY_RX) {
    state=BOARD_AUDIO_START_ERROR;return;
  }
  lastReceived=HAL_GetTick();state=BOARD_AUDIO_RUNNING;
}
void BoardAudio_Process(void)
{
  uint32_t now=HAL_GetTick();
  if(state!=BOARD_AUDIO_RUNNING) return;
  if(seenBlocks!=receivedBlocks) { seenBlocks=receivedBlocks;lastReceived=now; }
  if(dmaErrors || haudio_in_sai.ErrorCode || now-lastReceived>1000U) {
    state=BOARD_AUDIO_DATA_ERROR;if(onError) onError();
  }
}
BoardAudioState BoardAudio_GetState(void) { return state; }
