#include "board_temperature.h"
#include "dht11.h"
#include "app_board_config.h"
#include "stm32f7xx_hal.h"
#include <string.h>

#define DATA_PIN GPIO_PIN_6
#define POLL_MS 2500U
#define FRESH_MS 7500U
enum { IDLE, START_LOW, RECEIVING, COMPLETE, TIMED_OUT, BAD_EDGE };
static volatile uint32_t phase,edgeCount;
static volatile uint16_t edges[DHT11_EDGE_COUNT];
static uint32_t lastAttempt,initialized,primed,valid;
static BoardTemperatureStatus status;

static void TimerStart(uint32_t us)
{
  TIM7->CR1=0;TIM7->DIER=0;TIM7->ARR=us-1;TIM7->CNT=0;
  TIM7->EGR=TIM_EGR_UG;TIM7->SR=0;TIM7->DIER=TIM_DIER_UIE;
  TIM7->CR1=TIM_CR1_OPM|TIM_CR1_CEN;
}
static void Finish(uint32_t result)
{
  EXTI->IMR&=~DATA_PIN;TIM7->DIER=0;TIM7->CR1=0;TIM7->SR=0;
  GPIOG->BSRR=DATA_PIN;__DMB();phase=result;
}
void BoardTemperature_Init(void)
{
#if APP_BOARD_ROLE==APP_BOARD_A
  GPIO_InitTypeDef gpio={0};
#endif
  memset(&status,0,sizeof(status));phase=IDLE;valid=primed=initialized=0;
#if APP_BOARD_ROLE==APP_BOARD_A
  /* Same fixed clock tree as RangeClock; TIMPRE=0 => APB1 timer clock 100 MHz. */
  if(HAL_RCC_GetHCLKFreq()!=200000000U || HAL_RCC_GetPCLK1Freq()!=50000000U ||
     (RCC->DCKCFGR1&RCC_DCKCFGR1_TIMPRE)) { status.state=BOARD_TEMP_CLOCK;return; }
  __HAL_RCC_GPIOG_CLK_ENABLE();__HAL_RCC_SYSCFG_CLK_ENABLE();__HAL_RCC_TIM7_CLK_ENABLE();
  GPIOG->BSRR=DATA_PIN;
  gpio.Pin=DATA_PIN;gpio.Mode=GPIO_MODE_OUTPUT_OD;gpio.Pull=GPIO_NOPULL;
  gpio.Speed=GPIO_SPEED_FREQ_LOW;HAL_GPIO_Init(GPIOG,&gpio);
  /* EXTI6 was the unused PD6 audio event in CubeMX. Route only this line to
   * PG6; keep the pin open drain and read IDR while its output is released. */
  EXTI->IMR&=~DATA_PIN;EXTI->EMR&=~DATA_PIN;
  MODIFY_REG(SYSCFG->EXTICR[1],0xFU<<8,6U<<8);
  EXTI->RTSR|=DATA_PIN;EXTI->FTSR|=DATA_PIN;EXTI->PR=DATA_PIN;
  TIM7->CR1=0;TIM7->DIER=0;TIM7->PSC=99;TIM7->EGR=TIM_EGR_UG;TIM7->SR=0;
  HAL_NVIC_ClearPendingIRQ(TIM7_IRQn);HAL_NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
  /* Short edge ISR preempts audio DMA (15) and SD (6); no decoding in IRQ. */
  HAL_NVIC_SetPriority(TIM7_IRQn,3,0);HAL_NVIC_SetPriority(EXTI9_5_IRQn,3,0);
  HAL_NVIC_EnableIRQ(TIM7_IRQn);HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
  lastAttempt=HAL_GetTick()-POLL_MS+1500U;initialized=1;
#endif
}
void BoardTemperature_TimerIRQ(void)
{
  if(!initialized || !(TIM7->SR&TIM_SR_UIF)) return;
  TIM7->SR=0;
  if(phase==START_LOW) {
    edgeCount=0;phase=RECEIVING;EXTI->PR=DATA_PIN;
    TimerStart(6000U);EXTI->IMR|=DATA_PIN;GPIOG->BSRR=DATA_PIN;
  } else if(phase==RECEIVING) Finish(TIMED_OUT);
}
void BoardTemperature_EdgeIRQ(void)
{
  uint32_t at,level,n;
  if(!initialized || !(EXTI->PR&DATA_PIN)) return;
  at=TIM7->CNT;level=(GPIOG->IDR&DATA_PIN)!=0;EXTI->PR=DATA_PIN;
  if(phase!=RECEIVING) return;
  n=edgeCount;
  if(!n && level) return; /* Our own release rising edge precedes the response. */
  if(level!=(n&1U) || n>=DHT11_EDGE_COUNT || (TIM7->SR&TIM_SR_UIF)) {
    Finish(BAD_EDGE);return;
  }
  edges[n]=(uint16_t)at;edgeCount=n+1;
  if(n+1==DHT11_EDGE_COUNT) Finish(COMPLETE);
}
void BoardTemperature_Process(int allowStart)
{
  uint32_t now=HAL_GetTick(),p=phase;unsigned i;
  uint16_t frame[DHT11_EDGE_COUNT];
  int32_t temperature;uint32_t humidity;Dht11Result result;
  if(!initialized) return;
  if(p==COMPLETE || p==TIMED_OUT || p==BAD_EDGE) {
    valid=0;
    if(p==COMPLETE) {
      __DMB();for(i=0;i<DHT11_EDGE_COUNT;++i) frame[i]=edges[i];
      result=Dht11_Decode(frame,&temperature,&humidity);
      if(result==DHT11_OK && now-lastAttempt<FRESH_MS) {
        ++status.goodFrames;status.temperatureDeciC=temperature;status.humidityDeciPercent=humidity;
        /* DHT11 returns the PREVIOUS conversion. Prime once after startup,
         * errors or a long pause; only the following good read is publishable. */
        if(primed) { valid=1;status.lastValidMs=now;status.state=BOARD_TEMP_READY; }
        else status.state=BOARD_TEMP_STARTING;
        primed=1;
      } else {
        primed=0;
        if(result==DHT11_CHECKSUM_ERROR) { ++status.checksumErrors;status.state=BOARD_TEMP_CHECKSUM; }
        else if(result==DHT11_DATA_ERROR) { ++status.dataErrors;status.state=BOARD_TEMP_DATA; }
        else { ++status.timingErrors;status.state=BOARD_TEMP_TIMING; }
      }
    } else {
      primed=0;
      if(p==TIMED_OUT) { ++status.timeouts;status.state=BOARD_TEMP_TIMEOUT; }
      else { ++status.timingErrors;status.state=BOARD_TEMP_TIMING; }
    }
    phase=IDLE;
  }
  if(phase!=IDLE || !allowStart || now-lastAttempt<POLL_MS) return;
  if(now-lastAttempt>=FRESH_MS) { primed=valid=0;status.state=BOARD_TEMP_STARTING; }
  lastAttempt=now;++status.attempts;
  EXTI->IMR&=~DATA_PIN;EXTI->PR=DATA_PIN;phase=START_LOW;
  GPIOG->BSRR=(uint32_t)DATA_PIN<<16;TimerStart(20000U);
}
int BoardTemperature_Read(int32_t *temperatureDeciC)
{
  if(!initialized || !valid || HAL_GetTick()-status.lastValidMs>=FRESH_MS) return 0;
  *temperatureDeciC=status.temperatureDeciC;return 1;
}
const BoardTemperatureStatus *BoardTemperature_GetStatus(void) { return &status; }
