#include "app_range.h"
#include "range_pps.h"
#include "tim.h"
#include "main.h"

#define PPS_TICKS_PER_SECOND 1000000U
#define PPS_PULSE_TICKS 10U
#define PPS_ALIGN_FIRST_NS 800000000ULL
#define PPS_ALIGN_LAST_NS 980000000ULL

static uint64_t ppsAlignedSecond = UINT64_MAX;
static uint32_t ppsAlignedMs;
static uint8_t ppsActive;

uint64_t rangeRxTimestamp, rangeTxTimestamp;
uint32_t rangeTxStampSerial;

int RangeClock_Init(void)
{
  uint32_t start = HAL_GetTick();
  /* RM0385: coarse update increments every HCLK; digital rollover at 1 s.
   * This board runs at 200 MHz => exactly 5 ns per HCLK. Fail on clock changes. */
  if (HAL_RCC_GetHCLKFreq() != 200000000U) return 0;
  ETH->PTPTSCR = ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR | ETH_PTPTSCR_TSSARFE;
  ETH->PTPSSIR = 5U;
  ETH->PTPTSHUR = 0U; ETH->PTPTSLUR = 0U;
  ETH->PTPTSCR |= ETH_PTPTSCR_TSSTI;
  while (ETH->PTPTSCR & ETH_PTPTSCR_TSSTI)
    if (HAL_GetTick() - start > 20U) return 0;
  return 1;
}

uint64_t RangeClock_Now(void)
{
  uint32_t a, b, ns;
  do { a = ETH->PTPTSHR; ns = ETH->PTPTSLR; b = ETH->PTPTSHR; } while (a != b);
  return (uint64_t)a * 1000000000ULL + ns;
}

int RangePps_Init(void)
{
  /* The CubeMX setup must provide a 1 MHz TIM2 counter and 10 us PWM1 pulse.
   * APB1 is 50 MHz; its timer clock is doubled to 100 MHz. */
  if (HAL_RCC_GetPCLK1Freq() != 50000000U || htim2.Instance != TIM2 ||
      htim2.Init.Prescaler != 99U || htim2.Init.Period != PPS_TICKS_PER_SECOND-1U ||
      __HAL_TIM_GET_COMPARE(&htim2,TIM_CHANNEL_1) != PPS_PULSE_TICKS)
    return 0;

  /* HAL enables CCR preload for PWM. Disable it so loss of lock forces D9 low
   * immediately, even if the timer is far from its next update event. */
  CLEAR_BIT(TIM2->CCMR1,TIM_CCMR1_OC1PE);
  __HAL_TIM_SET_COMPARE(&htim2,TIM_CHANNEL_1,0U);
  TIM2->CNT=PPS_TICKS_PER_SECOND/2U;
  if (HAL_TIM_PWM_Start(&htim2,TIM_CHANNEL_1) != HAL_OK) return 0;
  ppsAlignedSecond=UINT64_MAX; ppsActive=0;
  return 1;
}

void RangePps_Update(const RangeSync *model, int locked)
{
  uint64_t localNs, masterNs, second, phaseNs;
  uint32_t timerAtSample, timerNow, elapsed, desired;
  if (!locked) {
    if (ppsActive) __HAL_TIM_SET_COMPARE(&htim2,TIM_CHANNEL_1,0U);
    ppsActive=0; ppsAlignedSecond=UINT64_MAX;
    return;
  }

  /* Cross-sample the ETH clock and the free-running timer. TIM2 keeps the
   * eventual edge in hardware; no GPIO write is made at the second boundary. */
  localNs=RangeClock_Now();
  timerAtSample=TIM2->CNT;
  masterNs=model ? RangeSync_Master(model,localNs) : localNs;
  second=masterNs/1000000000ULL;
  phaseNs=masterNs%1000000000ULL;
  if (phaseNs<PPS_ALIGN_FIRST_NS || phaseNs>=PPS_ALIGN_LAST_NS) return;
  if (ppsAlignedSecond==second && HAL_GetTick()-ppsAlignedMs<10U) return;

  /* Only correct while the output is low. Account for computation time using
   * timer ticks elapsed since the clock sample, including counter rollover. */
  desired=(uint32_t)(phaseNs/1000ULL);
  timerNow=TIM2->CNT;
  elapsed=(timerNow+PPS_TICKS_PER_SECOND-timerAtSample)%PPS_TICKS_PER_SECOND;
  /* A long interrupt here could make a counter write near rollover produce
   * an immediate spurious pulse. Leave the previous phase in that case. */
  if (elapsed>2000U || desired+elapsed>=990000U) return;
  desired+=elapsed;
  TIM2->CNT=desired;
  __HAL_TIM_SET_COMPARE(&htim2,TIM_CHANNEL_1,PPS_PULSE_TICKS);
  ppsActive=1; ppsAlignedSecond=second; ppsAlignedMs=HAL_GetTick();
}
