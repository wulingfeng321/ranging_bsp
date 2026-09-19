#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>
typedef struct { uint32_t CR, SR, DR; } TestRng;
extern TestRng testRng;
#define RNG (&testRng)
#define RNG_CR_RNGEN 4U
#define RNG_SR_DRDY 1U
#define RNG_SR_CECS 2U
#define RNG_SR_SECS 4U
#define __HAL_RCC_RNG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_RNG_CLK_DISABLE() ((void)0)
#define __HAL_RCC_RNG_FORCE_RESET() ((void)0)
#define __HAL_RCC_RNG_RELEASE_RESET() ((void)0)
uint32_t HAL_GetTick(void);
#define __get_PRIMASK() 0U
#define __disable_irq() ((void)0)
#define __set_PRIMASK(x) ((void)(x))
#endif
