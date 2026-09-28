#include <stdint.h>
typedef struct { struct { uint32_t BusWide,ClockDiv; } Init; } SD_HandleTypeDef;
extern SD_HandleTypeDef hsd1;
#define SDMMC_BUS_WIDE_1B 0
void MX_SDMMC1_SD_Init(void);
