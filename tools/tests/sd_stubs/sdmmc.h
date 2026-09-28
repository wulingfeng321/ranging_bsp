#include <stdint.h>
typedef enum { HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT } HAL_StatusTypeDef;
typedef int DMA_Stream_TypeDef;
extern DMA_Stream_TypeDef stream3,stream6;
#define DMA2_Stream3 (&stream3)
#define DMA2_Stream6 (&stream6)
#define DMA2_Stream3_IRQn 3
#define DMA2_Stream6_IRQn 6
#define SDMMC1_IRQn 49
#define DMA_CHANNEL_4 4
#define DMA_PERIPH_TO_MEMORY 0
#define DMA_MEMORY_TO_PERIPH 1
#define DMA_PINC_DISABLE 0
#define DMA_MINC_ENABLE 1
#define DMA_PDATAALIGN_WORD 2
#define DMA_MDATAALIGN_WORD 2
#define DMA_NORMAL 0
#define DMA_PFCTRL 32
#define DMA_PRIORITY_VERY_HIGH 3
#define DMA_FIFOMODE_ENABLE 1
#define DMA_FIFO_THRESHOLD_FULL 3
#define DMA_MBURST_INC4 1
#define DMA_PBURST_INC4 1
#define SDMMC_BUS_WIDE_1B 0
#define HAL_SD_STATE_READY 1
#define HAL_SD_STATE_BUSY 2
#define HAL_SD_ERROR_NONE 0
#define HAL_SD_CARD_TRANSFER 4
typedef struct {
  uint32_t Channel,Direction,PeriphInc,MemInc,PeriphDataAlignment,MemDataAlignment;
  uint32_t Mode,Priority,FIFOMode,FIFOThreshold,MemBurst,PeriphBurst;
} TestDmaInit;
typedef struct { DMA_Stream_TypeDef *Instance;TestDmaInit Init;void *Parent;uint32_t ErrorCode; } DMA_HandleTypeDef;
typedef struct { struct {uint32_t BusWide,ClockDiv;} Init;DMA_HandleTypeDef *hdmarx,*hdmatx;uint32_t ErrorCode,State; } SD_HandleTypeDef;
typedef struct {uint32_t LogBlockNbr;} HAL_SD_CardInfoTypeDef;
extern SD_HandleTypeDef hsd1;
#define __HAL_RCC_DMA2_CLK_ENABLE() ((void)0)
#define __HAL_LINKDMA(h,field,dma) ((h)->field=&(dma),(dma).Parent=(h))
#define __DSB() ((void)0)
uint32_t HAL_GetTick(void);
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_NVIC_SetPriority(int irq,int pre,int sub);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h);
void HAL_SD_IRQHandler(SD_HandleTypeDef *h);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *h);
HAL_StatusTypeDef HAL_SD_DeInit(SD_HandleTypeDef *h);
HAL_StatusTypeDef HAL_SD_Abort(SD_HandleTypeDef *h);
HAL_StatusTypeDef HAL_SD_ReadBlocks_DMA(SD_HandleTypeDef *h,uint8_t *data,uint32_t sector,uint32_t n);
HAL_StatusTypeDef HAL_SD_WriteBlocks_DMA(SD_HandleTypeDef *h,uint8_t *data,uint32_t sector,uint32_t n);
HAL_StatusTypeDef HAL_SD_GetCardInfo(SD_HandleTypeDef *h,HAL_SD_CardInfoTypeDef *info);
uint32_t HAL_SD_GetState(SD_HandleTypeDef *h);
uint32_t HAL_SD_GetCardState(SD_HandleTypeDef *h);
