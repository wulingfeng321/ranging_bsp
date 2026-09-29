#ifndef TEST_TS_H
#define TEST_TS_H
#define TS_OK 0
#include <stdint.h>
typedef struct { uint8_t touchDetected; uint16_t touchX[5],touchY[5]; } TS_StateTypeDef;
uint8_t BSP_TS_Init(uint16_t x,uint16_t y);
uint8_t BSP_TS_GetState(TS_StateTypeDef *state);
#endif
