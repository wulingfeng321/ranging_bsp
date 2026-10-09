#ifndef BOARD_TEMPERATURE_H
#define BOARD_TEMPERATURE_H
#include <stdint.h>
typedef enum {
  BOARD_TEMP_STARTING, BOARD_TEMP_READY, BOARD_TEMP_TIMEOUT,
  BOARD_TEMP_TIMING, BOARD_TEMP_CHECKSUM, BOARD_TEMP_DATA, BOARD_TEMP_CLOCK
} BoardTemperatureState;
typedef struct {
  uint32_t attempts,goodFrames,timeouts,timingErrors,checksumErrors,dataErrors;
  uint32_t lastValidMs,humidityDeciPercent;
  int32_t temperatureDeciC;
  BoardTemperatureState state;
} BoardTemperatureStatus;
/* A only: PG6/D2 open drain, external 3.3 V pull-up, TIM7 + EXTI6.
 * Init after all BSP initialization (which can configure EXTI routing).
 * Main-loop process/read/status; IRQ handlers own the edge buffer until done.
 * allowStart=0 defers new transactions while retaining bounded IRQ completion. */
void BoardTemperature_Init(void);
void BoardTemperature_Process(int allowStart);
int BoardTemperature_Read(int32_t *temperatureDeciC);
const BoardTemperatureStatus *BoardTemperature_GetStatus(void);
void BoardTemperature_TimerIRQ(void);
void BoardTemperature_EdgeIRQ(void);
#endif
