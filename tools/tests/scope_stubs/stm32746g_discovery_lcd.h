#ifndef TEST_SCOPE_LCD_H
#define TEST_SCOPE_LCD_H
#include "../../../../../Utilities/Fonts/fonts.h"
typedef struct { struct { uint32_t FBStartAdress; } LayerCfg[1]; } LTDC_HandleTypeDef;
typedef struct { uint32_t SRCR; } TestLtdc;
extern TestLtdc testLtdc;
#define LTDC (&testLtdc)
#define LTDC_SRCR_VBR 1U
#define LCD_RELOAD_VERTICAL_BLANKING 1U
#define LCD_COLOR_BLACK 0xFF000000U
#define LCD_COLOR_WHITE 0xFFFFFFFFU
#define LCD_COLOR_RED 0xFFFF0000U
#define LCD_COLOR_GREEN 0xFF00FF00U
#define LCD_COLOR_CYAN 0xFF00FFFFU
#define LCD_COLOR_YELLOW 0xFFFFFF00U
#define LEFT_MODE 0
void BSP_LCD_SetTextColor(uint32_t color);
void BSP_LCD_SetBackColor(uint32_t color);
void BSP_LCD_SetFont(sFONT *font);
void BSP_LCD_DisplayStringAt(uint16_t x, uint16_t y, uint8_t *text, int mode);
void BSP_LCD_DrawHLine(uint16_t x, uint16_t y, uint16_t width);
void BSP_LCD_DrawVLine(uint16_t x, uint16_t y, uint16_t height);
void BSP_LCD_Clear(uint32_t color);
void BSP_LCD_SetLayerAddress_NoReload(int layer, uint32_t address);
void BSP_LCD_Reload(int mode);
#endif
