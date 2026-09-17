#include "app_mic_scope.h"
#include "stm32746g_discovery_audio.h"
#include "stm32746g_discovery_lcd.h"
#include <stdint.h>
#include <stdio.h>

/* Nominal 16 kHz stereo PCM, 160 ms visible, 16 ms per DMA half. */
#define SAMPLE_RATE       16000U
#define WINDOW_FRAMES    2560U
#define RING_FRAMES      4096U
#define HALF_FRAMES      256U
#define DMA_WORDS        (HALF_FRAMES * 2U * 2U)
#define PLOT_WIDTH       480U
#define REFRESH_MS       40U
#define RESULT_TIMEOUT_MS 5000U
#define PLOT_AMPLITUDE    28
#define FRAME_A          0xC0000000U
#define FRAME_B          0xC0080000U
/* Reserved SDRAM, covered by main.c's non-cacheable 8 MB MPU region.
 * Two LCD buffers end before C0100000; DMA buffer occupies 2048 bytes. */
#define AUDIO_BUFFER     ((uint16_t *)0xC0100000U)

extern SAI_HandleTypeDef haudio_in_sai;
extern LTDC_HandleTypeDef hLtdcHandler;
static volatile int16_t history[RING_FRAMES][2];
static int16_t snapshot[WINDOW_FRAMES][2];
static volatile uint32_t writeFrame;
static volatile uint32_t validFrames;
volatile uint32_t micDmaBlocks;
volatile uint32_t micErrors;
static uint32_t lastDraw;
static uint32_t lastReceived;
static uint32_t seenBlocks;
static uint32_t backBuffer = FRAME_B;
static uint8_t started;
static const char *audioStatus = "MIC STARTING";
static uint32_t distanceMm;
static uint32_t resultTick;
static MicScope_RangeState rangeState = MIC_SCOPE_RANGE_WAITING;

/* Submit from the main loop, never from a DMA/network ISR. */
void MicScope_SetDistanceMm(uint32_t millimeters)
{
  if (millimeters > 999999U)
  {
    MicScope_SetRangeState(MIC_SCOPE_RANGE_INVALID);
    return;
  }
  distanceMm = millimeters;
  resultTick = HAL_GetTick();
  rangeState = MIC_SCOPE_RANGE_VALID;
}

void MicScope_SetRangeState(MicScope_RangeState state)
{
  /* Only SetDistanceMm may mark a result valid. */
  rangeState = (state == MIC_SCOPE_RANGE_WAITING ||
                state == MIC_SCOPE_RANGE_MEASURING) ? state : MIC_SCOPE_RANGE_INVALID;
}

static void Text(uint16_t x, uint16_t y, const char *text, uint32_t color)
{
  BSP_LCD_SetTextColor(color);
  BSP_LCD_DisplayStringAt(x, y, (uint8_t *)text, LEFT_MODE);
}

static void DrawDashboard(uint32_t now)
{
  char value[20];
  const char *state = "WAITING FOR RESULT";
  uint32_t color = LCD_COLOR_YELLOW;
  uint8_t fresh = rangeState == MIC_SCOPE_RANGE_VALID &&
                  now - resultTick < RESULT_TIMEOUT_MS;
  BSP_LCD_SetBackColor(LCD_COLOR_BLACK);
  BSP_LCD_SetFont(&Font16);
  Text(12, 4, "ACOUSTIC RANGING", LCD_COLOR_CYAN);
  BSP_LCD_SetFont(&Font12);
  Text(12, 27, "BOARD DISTANCE", LCD_COLOR_WHITE);
  if (fresh)
  {
    (void)snprintf(value, sizeof(value), "%lu.%03lu m",
                   (unsigned long)(distanceMm / 1000U),
                   (unsigned long)(distanceMm % 1000U));
    state = "VALID RESULT";
    color = LCD_COLOR_GREEN;
  }
  else
  {
    (void)snprintf(value, sizeof(value), "--.--- m");
    if (rangeState == MIC_SCOPE_RANGE_VALID) state = "RESULT EXPIRED";
    else if (rangeState == MIC_SCOPE_RANGE_MEASURING) state = "MEASURING...";
    else if (rangeState == MIC_SCOPE_RANGE_INVALID) state = "INVALID RESULT";
  }
  BSP_LCD_SetFont(&Font24);
  Text(12, 44, value, color);
  BSP_LCD_SetFont(&Font12);
  Text(250, 29, state, color);
  Text(250, 47, audioStatus, started ? LCD_COLOR_CYAN : LCD_COLOR_RED);
  Text(12, 78, "LOCAL L/R | 16kHz | 160ms | 20ms/div | AUTO", LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(0xFF404040U);
  BSP_LCD_DrawHLine(0, 95, PLOT_WIDTH);
}

/* Called only for the DMA half that has finished receiving. */
static void StoreHalf(uint32_t wordOffset)
{
  uint32_t i;
  uint32_t pos = writeFrame;
  const volatile int16_t *src = (const volatile int16_t *)AUDIO_BUFFER;
  for (i = 0; i < HALF_FRAMES; ++i)
  {
    history[pos][0] = src[wordOffset + 2U * i];
    history[pos][1] = src[wordOffset + 2U * i + 1U];
    pos = (pos + 1U) & (RING_FRAMES - 1U);
  }
  writeFrame = pos;
  if (validFrames < RING_FRAMES)
    validFrames += HALF_FRAMES;
  ++micDmaBlocks;
}

void BSP_AUDIO_IN_HalfTransfer_CallBack(void) { StoreHalf(0); }
void BSP_AUDIO_IN_TransferComplete_CallBack(void) { StoreHalf(HALF_FRAMES * 2U); }
void BSP_AUDIO_IN_Error_CallBack(void) { ++micErrors; }

void MicScope_Init(void)
{
  BSP_LCD_Clear(LCD_COLOR_BLACK);
  DrawDashboard(HAL_GetTick());
  /* The BSP selects DMIC2 left/right, SAI slots 1/3 -> L,R,L,R PCM. */
  if (BSP_AUDIO_IN_Init(SAMPLE_RATE, 16, 2) != AUDIO_OK)
  {
    audioStatus = "MIC INIT ERROR";
    DrawDashboard(HAL_GetTick());
    return;
  }
  /* BSP enables this codec signal-detect IRQ, but this demo uses only DMA. */
  HAL_NVIC_DisableIRQ(AUDIO_IN_INT_IRQ);
  if (BSP_AUDIO_IN_Record(AUDIO_BUFFER, DMA_WORDS) != AUDIO_OK ||
      HAL_SAI_GetState(&haudio_in_sai) != HAL_SAI_STATE_BUSY_RX)
  {
    audioStatus = "MIC START ERROR";
    DrawDashboard(HAL_GetTick());
    return;
  }
  lastReceived = HAL_GetTick();
  started = 1;
  audioStatus = "MIC RUNNING";
}

static int32_t PixelY(int32_t sample, int32_t mean, int32_t scale, int32_t center)
{
  int32_t offset = (sample - mean) * PLOT_AMPLITUDE / scale;
  if (offset > PLOT_AMPLITUDE) offset = PLOT_AMPLITUDE;
  if (offset < -PLOT_AMPLITUDE) offset = -PLOT_AMPLITUDE;
  return center - offset;
}

static void DrawChannel(uint32_t channel, int32_t mean, int32_t scale)
{
  uint32_t x, i, begin, end;
  uint16_t top = (uint16_t)(96U + channel * 88U);
  int32_t center = top + 49;
  int32_t low, high, y0, y1;

  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetBackColor(LCD_COLOR_BLACK);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_DisplayStringAt(0, top, (uint8_t *)(channel == 0 ?
      "LOCAL MIC L" : "LOCAL MIC R"), LEFT_MODE);
  BSP_LCD_SetTextColor(0xFF404040U);
  for (x = 0; x < PLOT_WIDTH; x += 60)
    BSP_LCD_DrawVLine((uint16_t)x, top + 20, 61);
  for (i = top + 21; i <= top + 77; i += 14)
    BSP_LCD_DrawHLine(0, (uint16_t)i, PLOT_WIDTH);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_DrawHLine(0, (uint16_t)center, PLOT_WIDTH);
  BSP_LCD_DrawHLine(0, top + 87, PLOT_WIDTH);
  BSP_LCD_SetTextColor(channel == 0 ? LCD_COLOR_GREEN : LCD_COLOR_CYAN);
  for (x = 0; x < PLOT_WIDTH; ++x)
  {
    begin = x * WINDOW_FRAMES / PLOT_WIDTH;
    end = (x + 1U) * WINDOW_FRAMES / PLOT_WIDTH;
    low = high = snapshot[begin][channel];
    for (i = begin + 1U; i < end; ++i)
    {
      if (snapshot[i][channel] < low) low = snapshot[i][channel];
      if (snapshot[i][channel] > high) high = snapshot[i][channel];
    }
    y0 = PixelY(high, mean, scale, center);
    y1 = PixelY(low, mean, scale, center);
    BSP_LCD_DrawVLine((uint16_t)x, (uint16_t)y0, (uint16_t)(y1 - y0 + 1));
  }
}

void MicScope_Process(void)
{
  uint32_t now, i, c, pos, irqMask;
  int32_t mean[2] = {0, 0};
  int32_t scale = 512; /* Limit amplification of the silence noise floor. */
  int32_t magnitude;
  now = HAL_GetTick();
  if (started && seenBlocks != micDmaBlocks)
  {
    seenBlocks = micDmaBlocks;
    lastReceived = now;
  }
  if (started && (micErrors || haudio_in_sai.ErrorCode || now - lastReceived > 1000U))
  {
    audioStatus = "MIC DATA ERROR";
    started = 0;
  }
  if (now - lastDraw < REFRESH_MS ||
      (LTDC->SRCR & LTDC_SRCR_VBR)) return;
  lastDraw = now;

  if (started && validFrames >= WINDOW_FRAMES)
  {
    /* Short coherent copy; never keep interrupts masked during LCD drawing. */
    irqMask = __get_PRIMASK();
    __disable_irq();
    pos = (writeFrame + RING_FRAMES - WINDOW_FRAMES) & (RING_FRAMES - 1U);
    for (i = 0; i < WINDOW_FRAMES; ++i)
    {
      snapshot[i][0] = history[pos][0];
      snapshot[i][1] = history[pos][1];
      pos = (pos + 1U) & (RING_FRAMES - 1U);
    }
    __set_PRIMASK(irqMask);
    for (i = 0; i < WINDOW_FRAMES; ++i)
      for (c = 0; c < 2; ++c) mean[c] += snapshot[i][c];
    mean[0] /= (int32_t)WINDOW_FRAMES;
    mean[1] /= (int32_t)WINDOW_FRAMES;
    for (i = 0; i < WINDOW_FRAMES; ++i)
      for (c = 0; c < 2; ++c)
      {
        magnitude = snapshot[i][c] - mean[c];
        if (magnitude < 0) magnitude = -magnitude;
        if (magnitude > scale) scale = magnitude;
      }

  }

  /* Draw into the hidden buffer using BSP's software address, then switch
   * the hardware address only at vertical blank to avoid clearing flicker. */
  hLtdcHandler.LayerCfg[0].FBStartAdress = backBuffer;
  BSP_LCD_Clear(LCD_COLOR_BLACK);
  DrawDashboard(now);
  if (started && validFrames >= WINDOW_FRAMES)
  {
    DrawChannel(0, mean[0], scale);
    DrawChannel(1, mean[1], scale);
  }
  else
  {
    BSP_LCD_SetFont(&Font16);
    Text(12, 140, started ? "Waiting for audio samples..." :
         "Audio unavailable: check MIC / DMA", LCD_COLOR_YELLOW);
  }
  __DSB();
  BSP_LCD_SetLayerAddress_NoReload(0, backBuffer);
  BSP_LCD_Reload(LCD_RELOAD_VERTICAL_BLANKING);
  backBuffer = (backBuffer == FRAME_A) ? FRAME_B : FRAME_A;
}
