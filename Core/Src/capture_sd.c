/* Capture-only FatFs driver. DMA feeds/drains the FIFO independently of CPU
 * audio/timer interrupts. Main-loop completion waits remain bounded.
 * Kept outside CubeMX-generated sd_diskio.c (whose template waits forever).
 */
#include "ff_gen_drv.h"
#include "bsp_driver_sd.h"
#include "sdmmc.h"
#include "capture_sd.h"
#include "board_memory.h"
#include <string.h>
#include <stdio.h>
/* SDRAM is DMA-accessible and non-cacheable in main.c. This 512-byte region
 * starts after the recorder's 4096-byte header; no cache-line sharing. */
#ifndef CAPTURE_SD_BUFFER
#define CAPTURE_SD_BUFFER ((uint32_t *)BOARD_SD_BUFFER_BASE)
#endif
#define sectorBuffer CAPTURE_SD_BUFFER
static DMA_HandleTypeDef rxDma,txDma;
static char errorText[32];
typedef struct {
  uint32_t error, dmaError, sector, elapsed, halStatus;
  char phase;
} SdFailure;
static SdFailure failure;
static DSTATUS status=STA_NOINIT;
const char *CaptureSD_ErrorText(void) { return errorText; }
static int Failed(char phase,uint32_t sector,HAL_StatusTypeDef result,uint32_t elapsed)
{
  if(!failure.phase) {
    failure.phase=phase;failure.sector=sector;failure.halStatus=(uint32_t)result;
    failure.error=hsd1.ErrorCode;failure.elapsed=elapsed;
    failure.dmaError=rxDma.ErrorCode;
    failure.dmaError|=txDma.ErrorCode;
    (void)snprintf(errorText,sizeof(errorText),"SD %c H%lu E%08lX",phase,
      (unsigned long)result,(unsigned long)failure.error);
  }
  status=STA_NOINIT;return 0;
}
void DMA2_Stream3_IRQHandler(void) { HAL_DMA_IRQHandler(&rxDma); }
void DMA2_Stream6_IRQHandler(void) { HAL_DMA_IRQHandler(&txDma); }
void SDMMC1_IRQHandler(void) { HAL_SD_IRQHandler(&hsd1); }
static int DmaInit(DMA_HandleTypeDef *dma,DMA_Stream_TypeDef *stream,uint32_t direction)
{
  dma->Instance=stream;
  (void)HAL_DMA_DeInit(dma);
  dma->Init.Channel=DMA_CHANNEL_4;
  dma->Init.Direction=direction;
  dma->Init.PeriphInc=DMA_PINC_DISABLE;dma->Init.MemInc=DMA_MINC_ENABLE;
  dma->Init.PeriphDataAlignment=DMA_PDATAALIGN_WORD;dma->Init.MemDataAlignment=DMA_MDATAALIGN_WORD;
  /* SDMMC owns the final FIFO burst (Discovery BSP uses peripheral flow).
   * DMA_NORMAL left the last four words pending on the actual SD read. */
  dma->Init.Mode=DMA_PFCTRL;dma->Init.Priority=DMA_PRIORITY_VERY_HIGH;
  dma->Init.FIFOMode=DMA_FIFOMODE_ENABLE;dma->Init.FIFOThreshold=DMA_FIFO_THRESHOLD_FULL;
  dma->Init.MemBurst=DMA_MBURST_INC4;dma->Init.PeriphBurst=DMA_PBURST_INC4;
  return HAL_DMA_Init(dma)==HAL_OK;
}
static int Ready(void)
{
  uint32_t start=HAL_GetTick();
  do {
    if(BSP_SD_IsDetected()!=SD_PRESENT) return Failed('N',0,HAL_ERROR,HAL_GetTick()-start);
    if(HAL_SD_GetCardState(&hsd1)==HAL_SD_CARD_TRANSFER) return 1;
  } while(HAL_GetTick()-start<1000U);
  return Failed('B',0,HAL_TIMEOUT,HAL_GetTick()-start);
}
static DSTATUS Initialize(BYTE lun)
{
  (void)lun;status=STA_NOINIT;
  memset(&failure,0,sizeof(failure));errorText[0]=0;
  if(BSP_SD_IsDetected()!=SD_PRESENT) { (void)Failed('N',0,HAL_ERROR,0);return status|STA_NODISK; }
  HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
  HAL_NVIC_DisableIRQ(DMA2_Stream3_IRQn);HAL_NVIC_DisableIRQ(DMA2_Stream6_IRQn);
  (void)HAL_SD_DeInit(&hsd1);
  __HAL_RCC_DMA2_CLK_ENABLE();
  if(!DmaInit(&rxDma,DMA2_Stream3,DMA_PERIPH_TO_MEMORY) ||
     !DmaInit(&txDma,DMA2_Stream6,DMA_MEMORY_TO_PERIPH)) {
    (void)Failed('D',0,HAL_ERROR,0);return status;
  }
  __HAL_LINKDMA(&hsd1,hdmarx,rxDma);__HAL_LINKDMA(&hsd1,hdmatx,txDma);
  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn,6,0);HAL_NVIC_SetPriority(DMA2_Stream6_IRQn,6,0);
  HAL_NVIC_SetPriority(SDMMC1_IRQn,6,0);
  HAL_NVIC_ClearPendingIRQ(DMA2_Stream3_IRQn);HAL_NVIC_ClearPendingIRQ(DMA2_Stream6_IRQn);
  HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);
  HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
  hsd1.Init.BusWide=SDMMC_BUS_WIDE_1B;hsd1.Init.ClockDiv=2;
  if(BSP_SD_Init()!=MSD_OK) (void)Failed('I',0,HAL_ERROR,0);
  else if(Ready()) status=0;
  return status;
}
static int Transfer(BYTE *buffer,DWORD sector,int write)
{
  uint32_t start=HAL_GetTick();
  HAL_StatusTypeDef result;
  if(write) memcpy(sectorBuffer,buffer,512);
  __DSB();
  result=write ? HAL_SD_WriteBlocks_DMA(&hsd1,(uint8_t *)sectorBuffer,sector,1) :
                 HAL_SD_ReadBlocks_DMA(&hsd1,(uint8_t *)sectorBuffer,sector,1);
  if(result==HAL_OK) {
    while(HAL_SD_GetState(&hsd1)!=HAL_SD_STATE_READY && hsd1.ErrorCode==HAL_SD_ERROR_NONE) {
      if(HAL_GetTick()-start>=1000U) { result=HAL_TIMEOUT;break; }
    }
    if(hsd1.ErrorCode!=HAL_SD_ERROR_NONE) result=HAL_ERROR;
  }
  if(result!=HAL_OK) {
    (void)Failed(write?'W':'R',sector,result,HAL_GetTick()-start);
    /* Abort only after latching: HAL abort/status commands can change ErrorCode. */
    (void)HAL_SD_Abort(&hsd1);return 0;
  }
  if(!Ready()) return 0;
  __DSB();
  if(!write) memcpy(buffer,sectorBuffer,512);
  return 1;
}
static DSTATUS Status(BYTE lun)
{
  (void)lun;
  if(BSP_SD_IsDetected()!=SD_PRESENT) status=STA_NOINIT|STA_NODISK;
  return status;
}
static DRESULT Read(BYTE lun,BYTE *buffer,DWORD sector,UINT count)
{
  (void)lun;
  if(status) return RES_NOTRDY;
  while(count--) {
    if(!Transfer(buffer,sector++,0)) return RES_ERROR;
    buffer+=512;
  }
  return RES_OK;
}
static DRESULT Write(BYTE lun,const BYTE *buffer,DWORD sector,UINT count)
{
  (void)lun;
  if(status) return RES_NOTRDY;
  while(count--) {
    if(!Transfer((BYTE *)buffer,sector++,1)) return RES_ERROR;
    buffer+=512;
  }
  return RES_OK;
}
static DRESULT Ioctl(BYTE lun,BYTE cmd,void *buffer)
{
  HAL_SD_CardInfoTypeDef info;
  (void)lun;
  if(status) return RES_NOTRDY;
  if(cmd==CTRL_SYNC) return Ready()?RES_OK:RES_ERROR;
  if(HAL_SD_GetCardInfo(&hsd1,&info)!=HAL_OK) return RES_ERROR;
  switch(cmd) {
    case GET_SECTOR_COUNT: *(DWORD *)buffer=info.LogBlockNbr;return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buffer=512;return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buffer=1;return RES_OK;
    default: return RES_PARERR;
  }
}
const Diskio_drvTypeDef CaptureSD_Driver={Initialize,Status,Read,Write,Ioctl};
