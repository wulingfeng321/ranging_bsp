#include "app_range.h"
#include "main.h"

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
