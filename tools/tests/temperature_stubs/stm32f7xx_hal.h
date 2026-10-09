#ifndef TEMPERATURE_HAL_STUB_H
#define TEMPERATURE_HAL_STUB_H
#include <stdint.h>
typedef struct { uint32_t CR1,DIER,ARR,CNT,EGR,SR,PSC; } FakeTim;
typedef struct { uint32_t IMR,EMR,RTSR,FTSR,PR; } FakeExti;
typedef struct { uint32_t BSRR,IDR; } FakeGpio;
typedef struct { uint32_t DCKCFGR1; } FakeRcc;
typedef struct { uint32_t EXTICR[4]; } FakeSyscfg;
typedef struct { uint32_t Pin,Mode,Pull,Speed; } GPIO_InitTypeDef;
extern FakeTim fakeTim;
extern FakeExti fakeExti;
extern FakeGpio fakeGpio;
extern FakeRcc fakeRcc;
extern FakeSyscfg fakeSyscfg;
#define TIM7 (&fakeTim)
#define EXTI (&fakeExti)
#define GPIOG (&fakeGpio)
#define RCC (&fakeRcc)
#define SYSCFG (&fakeSyscfg)
#define GPIO_PIN_6 (1U<<6)
#define TIM_EGR_UG 1U
#define TIM_DIER_UIE 1U
#define TIM_SR_UIF 1U
#define TIM_CR1_CEN 1U
#define TIM_CR1_OPM 8U
#define RCC_DCKCFGR1_TIMPRE (1U<<24)
#define GPIO_MODE_OUTPUT_OD 17U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define TIM7_IRQn 55
#define EXTI9_5_IRQn 23
#define __DMB() ((void)0)
#define MODIFY_REG(reg,clear,set) ((reg)=((reg)&~(clear))|(set))
#define __HAL_RCC_GPIOG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SYSCFG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM7_CLK_ENABLE() ((void)0)
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
void HAL_GPIO_Init(FakeGpio *gpio,GPIO_InitTypeDef *init);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_NVIC_SetPriority(int irq,int preempt,int sub);
void HAL_NVIC_EnableIRQ(int irq);
#endif
