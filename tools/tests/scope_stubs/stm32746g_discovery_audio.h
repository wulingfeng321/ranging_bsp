#ifndef TEST_SCOPE_AUDIO_H
#define TEST_SCOPE_AUDIO_H
#include <stdint.h>
typedef struct { uint32_t ErrorCode; } SAI_HandleTypeDef;
#define AUDIO_OK 0
#define AUDIO_IN_INT_IRQ 0
#define HAL_SAI_STATE_BUSY_RX 1
uint32_t HAL_GetTick(void);
int BSP_AUDIO_IN_Init(uint32_t rate, int bits, int channels);
int BSP_AUDIO_IN_Record(uint16_t *data, uint32_t words);
int HAL_SAI_GetState(SAI_HandleTypeDef *sai);
void HAL_NVIC_DisableIRQ(int irq);
#define __DMB() ((void)0)
#define __DSB() ((void)0)
#endif
