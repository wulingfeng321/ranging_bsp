#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static uint32_t testDmaBuffer[128];
#define CAPTURE_SD_BUFFER testDmaBuffer
#include "../../Core/Src/capture_sd.c"
DMA_Stream_TypeDef stream3,stream6;
SD_HandleTypeDef hsd1;
static uint32_t ticks,doneTick,missing,busyCard,stallDma,dmaError,abortCount,readCount,writeCount;
static uint8_t disk[4][512];
uint32_t HAL_GetTick(void) { return ticks++; }
void HAL_NVIC_DisableIRQ(int irq) { (void)irq; }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
void HAL_NVIC_SetPriority(int irq,int pre,int sub) { (void)irq;(void)pre;(void)sub; }
void HAL_NVIC_ClearPendingIRQ(int irq) { (void)irq; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h) { (void)h; }
void HAL_SD_IRQHandler(SD_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *h) { h->ErrorCode=0;return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h)
{ assert(h->Init.Channel==4 && h->Init.MemDataAlignment==DMA_MDATAALIGN_WORD);
  assert(h->Init.Mode==DMA_PFCTRL);return HAL_OK; }
HAL_StatusTypeDef HAL_SD_DeInit(SD_HandleTypeDef *h) { h->ErrorCode=0;return HAL_OK; }
HAL_StatusTypeDef HAL_SD_Abort(SD_HandleTypeDef *h) { ++abortCount;h->ErrorCode=0;h->State=HAL_SD_STATE_READY;return HAL_OK; }
HAL_StatusTypeDef HAL_SD_ReadBlocks_DMA(SD_HandleTypeDef *h,uint8_t *data,uint32_t sector,uint32_t n)
{ assert(data==(uint8_t *)testDmaBuffer && n==1 && sector<4);++readCount;memcpy(data,disk[sector],512);h->ErrorCode=0;h->State=HAL_SD_STATE_BUSY;doneTick=ticks+3;return HAL_OK; }
HAL_StatusTypeDef HAL_SD_WriteBlocks_DMA(SD_HandleTypeDef *h,uint8_t *data,uint32_t sector,uint32_t n)
{ assert(data==(uint8_t *)testDmaBuffer && n==1 && sector<4);++writeCount;memcpy(disk[sector],data,512);h->ErrorCode=0;h->State=HAL_SD_STATE_BUSY;doneTick=ticks+3;return HAL_OK; }
uint32_t HAL_SD_GetState(SD_HandleTypeDef *h)
{ if(!stallDma && ticks>=doneTick) { h->State=HAL_SD_STATE_READY;h->ErrorCode=dmaError; }return h->State; }
uint32_t HAL_SD_GetCardState(SD_HandleTypeDef *h) { (void)h;return busyCard?7:HAL_SD_CARD_TRANSFER; }
HAL_StatusTypeDef HAL_SD_GetCardInfo(SD_HandleTypeDef *h,HAL_SD_CardInfoTypeDef *info)
{ (void)h;info->LogBlockNbr=4;return HAL_OK; }
int BSP_SD_Init(void) { hsd1.State=HAL_SD_STATE_READY;return MSD_OK; }
int BSP_SD_IsDetected(void) { return missing?0:SD_PRESENT; }
int main(void)
{
  uint8_t src[1025],dst[1025];uint32_t i,start;
  assert(Initialize(0)==0 && hsd1.hdmarx==&rxDma && hsd1.hdmatx==&txDma);
  assert(rxDma.Parent==&hsd1 && txDma.Parent==&hsd1);
  for(i=0;i<sizeof(src);++i) src[i]=(uint8_t)i;
  assert(Write(0,src+1,0,2)==RES_OK && writeCount==2);
  assert(Read(0,dst+1,0,2)==RES_OK && readCount==2 && !memcmp(src+1,dst+1,1024));
  dmaError=0x10;assert(Write(0,src+1,2,1)==RES_ERROR && abortCount==1);
  assert(failure.error==0x10 && hsd1.ErrorCode==0 && strstr(CaptureSD_ErrorText(),"SD W H1 E00000010"));
  assert(Write(0,src+1,2,1)==RES_NOTRDY && failure.error==0x10); /* first failure survives */
  dmaError=0;assert(Initialize(0)==0 && !CaptureSD_ErrorText()[0]);
  stallDma=1;start=ticks;assert(Read(0,dst+1,0,1)==RES_ERROR);
  assert(ticks-start<=1005 && failure.halStatus==HAL_TIMEOUT && failure.phase=='R');
  stallDma=0;assert(Initialize(0)==0);busyCard=1;start=ticks;
  assert(Write(0,src+1,0,1)==RES_ERROR && failure.phase=='B' && ticks-start<=1010);
  busyCard=0;missing=1;assert(Initialize(0)&STA_NODISK);assert(failure.phase=='N');
  puts("PASS: SD DMA bounce, completion, timeout, card busy, removal and first-error latch");return 0;
}
