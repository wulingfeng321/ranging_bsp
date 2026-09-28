#include <stdint.h>
#define BUTTON_KEY 2
#define BUTTON_MODE_GPIO 0
void BSP_PB_Init(int b,int mode);
uint32_t BSP_PB_GetState(int b);
