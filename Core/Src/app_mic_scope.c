#include "app_mic_scope.h"
#include "app_net.h"
#include "app_range.h"
#include "app_board_config.h"
#include "stm32746g_discovery_audio.h"
#include "stm32746g_discovery_lcd.h"
#include <stdint.h>
#include <stdio.h>
#include "mic_scope_trigger.h"

/* Nominal 16 kHz stereo PCM, 160 ms visible, 16 ms per DMA half. */
#define SAMPLE_RATE       16000U
#define WINDOW_FRAMES    2560U
#define RING_FRAMES      WINDOW_FRAMES
#define HALF_FRAMES      256U
#define DMA_WORDS        (HALF_FRAMES * 2U * 2U)
#define PLOT_WIDTH       480U
#define REFRESH_MS       150U
#define RESULT_TIMEOUT_MS APP_RANGE_RESULT_HOLD_MS
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
static int16_t heldWave[WINDOW_FRAMES][2];
static volatile uint32_t sweepNumber;
static uint32_t triggerSweep;
static uint32_t holdTick;
static uint32_t rearmTick;
/* 0 = live, 1 = finish current sweep, 2 = held event snapshot. */
static uint8_t holdState;
static uint8_t rearmWaiting;
static uint32_t sweepColumn;
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
  char netText[32];
  const char *state = "WAITING FOR RESULT";
  uint32_t color = LCD_COLOR_YELLOW;
  uint8_t fresh = rangeState == MIC_SCOPE_RANGE_VALID &&
                  now - resultTick < RESULT_TIMEOUT_MS;
  BSP_LCD_SetBackColor(LCD_COLOR_BLACK);
  BSP_LCD_SetFont(&Font16);
  Text(12, 4, "BOARD RANGE TEST3", LCD_COLOR_CYAN);
  BSP_LCD_SetFont(&Font12);
  (void)snprintf(netText, sizeof(netText), "%s NET %s OK:%lu", APP_BOARD_NAME,
                 appNetStatus.initError ? "ERROR" :
                 (appNetStatus.online ? "ONLINE" : "WAIT"),
                 (unsigned long)appNetStatus.testAck);
  Text(250, 4, netText, appNetStatus.online ? LCD_COLOR_GREEN : LCD_COLOR_YELLOW);
  Text(12, 27, "A(L) - B(L) DISTANCE", LCD_COLOR_WHITE);
  if (fresh)
  {
    (void)snprintf(value, sizeof(value), "%lu.%03lu m",
                   (unsigned long)(distanceMm / 1000U),
                   (unsigned long)(distanceMm % 1000U));
    state = appRangeStatus.direction > 0 ? "SOURCE: A SIDE" :
            (appRangeStatus.direction < 0 ? "SOURCE: B SIDE" : "SOURCE: UNKNOWN");
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
  BSP_LCD_SetFont(fresh ? &Font16 : &Font12);
  Text(250, 29, state, color);
  BSP_LCD_SetFont(&Font12);
  if(fresh)
  {
    Text(250,47,appRangeStatus.direction>0 ? "SOUND >>> A --- B" :
      (appRangeStatus.direction<0 ? "A --- B <<< SOUND" : "ARRIVAL SIDE UNCERTAIN"),
      LCD_COLOR_CYAN);
    if(APP_BOARD_ROLE==APP_BOARD_A)
    {
      (void)snprintf(netText,sizeof(netText),"RAW B-A:%+ldus",
                     (long)appRangeStatus.resultDeltaUs);
      Text(250,63,netText,LCD_COLOR_YELLOW);
    }
    else Text(250,63,"COLLINEAR / OUTSIDE",LCD_COLOR_YELLOW);
  }
  else
  {
    Text(250,47,audioStatus,started ? LCD_COLOR_CYAN : LCD_COLOR_RED);
    Text(250,63,"SETUP: SOUND--A--B",LCD_COLOR_YELLOW);
  }
  (void)snprintf(netText, sizeof(netText), "SYNC %s AT:%s Q:%lu",
                 (appRangeStatus.error==-1 || appRangeStatus.error==-2) ? "ERROR" :
                 (appRangeStatus.locked ? "READY" : "WAIT"),
                 appRangeStatus.audioTimeReady ? "OK" : "WAIT",
                 (unsigned long)appRangeStatus.quality);
  Text(12, 78, netText, appRangeStatus.locked ? LCD_COLOR_GREEN : LCD_COLOR_YELLOW);
  (void)snprintf(netText, sizeof(netText), "T:%ldC EVT:%lu",
                 (long)(APP_TEMPERATURE_DECI_C/10), (unsigned long)appRangeStatus.events);
  Text(280, 78, netText, LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(0xFF404040U);
  BSP_LCD_DrawHLine(0, 95, PLOT_WIDTH);
}

/* Fit diagnostics beside the microphone labels, above the waveform grid.
 * LCD counters wrap at 10000; Watch retains full cumulative counters. */
static void DrawRangeDiagnostics(void)
{
  char text[64], delta[16];
  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetBackColor(LCD_COLOR_BLACK);
  (void)snprintf(text,sizeof(text),"D:%lu G:%lu O:%lu EQ:%lu",
    (unsigned long)(appRangeStatus.audioDrops%10000U),
    (unsigned long)(appRangeStatus.audioGapDrops%10000U),
    (unsigned long)(appRangeStatus.audioOverruns%10000U),
    (unsigned long)appRangeStatus.eventQuality);
  Text(120,96,text,LCD_COLOR_YELLOW);
  /* Alternate with the existing counters, keeping the waveform area free. */
  if((HAL_GetTick()/2000U)&1U) {
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    BSP_LCD_FillRect(120,96,360,12);
    (void)snprintf(text,sizeof(text),"AJ:%luus PS:%lu EQ:%lu",
      (unsigned long)(appRangeStatus.audioJitterNs/1000U),
      (unsigned long)appRangeStatus.eventPeakSpreadSamples,
      (unsigned long)appRangeStatus.eventQuality);
    Text(120,96,text,LCD_COLOR_YELLOW);
  }
  if(APP_BOARD_ROLE==APP_BOARD_A)
  {
    if(appRangeStatus.pairDeltaValid)
      (void)snprintf(delta,sizeof(delta),"%ld",(long)appRangeStatus.pairDeltaUs);
    else (void)snprintf(delta,sizeof(delta),"--");
    (void)snprintf(text,sizeof(text),"RX:%lu DT:%sus R:%lu J:%lu",
      (unsigned long)(appRangeStatus.eventRx%10000U),delta,
      (unsigned long)(appRangeStatus.results%10000U),
      (unsigned long)(appRangeStatus.rejected%10000U));
  }
  else
    (void)snprintf(text,sizeof(text),"TX:%lu ACK:%lu R:%lu J:%lu",
      (unsigned long)(appRangeStatus.eventTxAttempts%10000U),
      (unsigned long)(appRangeStatus.eventAck%10000U),
      (unsigned long)(appRangeStatus.results%10000U),
      (unsigned long)(appRangeStatus.rejected%10000U));
  Text(120,184,text,LCD_COLOR_YELLOW);
}

/* Called only for the DMA half that has finished receiving. */
static void StoreHalf(uint32_t wordOffset)
{
  uint32_t i;
  uint32_t pos = writeFrame;
  const volatile int16_t *src = (const volatile int16_t *)AUDIO_BUFFER;
  AppRange_Audio(src + wordOffset, HALF_FRAMES);
  for (i = 0; i < HALF_FRAMES; ++i)
  {
    history[pos][0] = src[wordOffset + 2U * i];
    history[pos][1] = src[wordOffset + 2U * i + 1U];
    /* Fixed horizontal slots: wrap at one screen, never shift old samples. */
    if (++pos == RING_FRAMES) { pos = 0; ++sweepNumber; }
  }
  writeFrame = pos;
  if (validFrames < RING_FRAMES)
    validFrames += HALF_FRAMES;
  ++micDmaBlocks;
}

void BSP_AUDIO_IN_HalfTransfer_CallBack(void) { StoreHalf(0); }
void BSP_AUDIO_IN_TransferComplete_CallBack(void) { StoreHalf(HALF_FRAMES * 2U); }
void BSP_AUDIO_IN_Error_CallBack(void) { ++micErrors; AppRange_AudioError(); }

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
    BSP_LCD_SetTextColor(channel == 0 ?
        (holdState == 2 || x < sweepColumn ? LCD_COLOR_GREEN : 0xFF008000U) :
        (holdState == 2 || x < sweepColumn ? LCD_COLOR_CYAN : 0xFF008080U));
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
  uint32_t now, i, c, irqMask, capturedWrite, capturedSweep;
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
    AppRange_AudioError();
    started = 0;
    holdState = 0;
  }
  if (now - lastDraw < REFRESH_MS ||
      (LTDC->SRCR & LTDC_SRCR_VBR) || !AppRange_DisplayReady()) return;
  lastDraw = now;

  if (started && validFrames >= WINDOW_FRAMES)
  {
    if (holdState == 2 && now - holdTick >= MIC_SCOPE_HOLD_MS)
    {
      holdState = 0;
      rearmWaiting = 1;
      rearmTick = now;
    }
    if (rearmWaiting && now - rearmTick >= 160U) rearmWaiting = 0;
    /* Do not mask DMA interrupts during the waveform copy: their arrival
     * anchors are used for ranging. Retry next frame if an ISR changed data. */
    irqMask = micDmaBlocks;
    /* Copy in physical slot order, not oldest-to-newest order. Both channels
     * and the cursor are captured together so they cannot drift apart. */
    sweepColumn = writeFrame * PLOT_WIDTH / WINDOW_FRAMES;
    capturedWrite = writeFrame;
    capturedSweep = sweepNumber;
    for (i = 0; i < WINDOW_FRAMES; ++i)
    {
      snapshot[i][0] = history[i][0];
      snapshot[i][1] = history[i][1];
    }
    __DMB();
    if (irqMask != micDmaBlocks) return;
    if (holdState == 1 && capturedSweep != triggerSweep)
    {
      holdState = 2;
      holdTick = now;
    }
    if (holdState == 0 && !rearmWaiting && MIC_SCOPE_HOLD_MS > 0U &&
        MicScope_IsChirpCandidate(snapshot, WINDOW_FRAMES, capturedWrite,
                                 MIC_SCOPE_TRIGGER_MIN_LEVEL))
    {
      /* Keep a chronological event window, including a boundary-crossing
       * signature. Display it only once the current live sweep has ended. */
      for (i = 0; i < WINDOW_FRAMES; ++i)
        for (c = 0; c < 2; ++c)
          heldWave[i][c] = snapshot[(capturedWrite + i) % WINDOW_FRAMES][c];
      triggerSweep = capturedSweep;
      holdState = 1;
    }
    if (holdState == 2)
      for (i = 0; i < WINDOW_FRAMES; ++i)
        for (c = 0; c < 2; ++c) snapshot[i][c] = heldWave[i][c];
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
    DrawRangeDiagnostics();
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
