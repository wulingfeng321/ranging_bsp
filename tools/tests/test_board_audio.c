/* Test the actual board adapter without touching the SDRAM DMA address. */
#include <assert.h>
#include <stdio.h>
#include "../../Core/Src/board_audio.c"
SAI_HandleTypeDef haudio_in_sai;
static uint32_t tick,blocksSeen,errorsSeen,irqDisabled;
static int initResult,recordResult,saiState=HAL_SAI_STATE_BUSY_RX;
static const volatile int16_t *lastPcm;
uint32_t HAL_GetTick(void) { return tick; }
int BSP_AUDIO_IN_Init(uint32_t rate,int bits,int channels)
{ assert(rate==48000 && bits==16 && channels==2);return initResult; }
int BSP_AUDIO_IN_Record(uint16_t *pcm,uint32_t words)
{ assert(pcm==AUDIO_BUFFER && words==3072);return recordResult; }
int HAL_SAI_GetState(SAI_HandleTypeDef *sai)
{ assert(sai==&haudio_in_sai);return saiState; }
void HAL_NVIC_DisableIRQ(int irq)
{ assert(irq==AUDIO_IN_INT_IRQ);++irqDisabled; }
static void Block(const volatile int16_t *pcm,uint32_t frames)
{ assert(frames==768);lastPcm=pcm;++blocksSeen; }
static void Error(void) { ++errorsSeen; }
int main(void)
{
  unsigned before;
  BoardAudio_Init(Block,Error);assert(BoardAudio_GetState()==BOARD_AUDIO_RUNNING && irqDisabled==1);
  BSP_AUDIO_IN_HalfTransfer_CallBack();assert(blocksSeen==1 && lastPcm==(int16_t *)AUDIO_BUFFER);
  BSP_AUDIO_IN_TransferComplete_CallBack();assert(blocksSeen==2 && lastPcm==(int16_t *)AUDIO_BUFFER+1536);
  tick=16;BoardAudio_Process();tick=1016;BoardAudio_Process();assert(!errorsSeen);
  tick=1017;BoardAudio_Process();assert(errorsSeen==1 && BoardAudio_GetState()==BOARD_AUDIO_DATA_ERROR);
  BoardAudio_Process();assert(errorsSeen==1);
  tick=UINT32_MAX-10;BoardAudio_Init(Block,Error);tick=5;BoardAudio_Process();
  assert(BoardAudio_GetState()==BOARD_AUDIO_RUNNING); /* millisecond wrap */
  before=errorsSeen;BSP_AUDIO_IN_Error_CallBack();assert(errorsSeen==before+1);
  BoardAudio_Process();assert(BoardAudio_GetState()==BOARD_AUDIO_DATA_ERROR);
  initResult=1;BoardAudio_Init(Block,Error);assert(BoardAudio_GetState()==BOARD_AUDIO_INIT_ERROR);
  initResult=0;recordResult=1;BoardAudio_Init(Block,Error);assert(BoardAudio_GetState()==BOARD_AUDIO_START_ERROR);
  recordResult=0;saiState=0;BoardAudio_Init(Block,Error);assert(BoardAudio_GetState()==BOARD_AUDIO_START_ERROR);
  saiState=HAL_SAI_STATE_BUSY_RX;BoardAudio_Init(Block,Error);haudio_in_sai.ErrorCode=1;
  BoardAudio_Process();assert(BoardAudio_GetState()==BOARD_AUDIO_DATA_ERROR);
  puts("PASS: board audio DMA halves, startup failures, timeout, errors and tick wrap");return 0;
}
