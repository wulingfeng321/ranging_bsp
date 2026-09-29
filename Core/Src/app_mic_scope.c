#include "app_mic_scope.h"
#include "app_capture.h"
#include "app_net.h"
#include "app_range.h"
#include "app_board_config.h"
#include "range_dsp.h"
#include "stm32746g_discovery_audio.h"
#include "stm32746g_discovery_lcd.h"
#include "stm32746g_discovery_ts.h"
#include <stdio.h>
#include <math.h>

#define FRAME_A 0xC0000000U
#define FRAME_B 0xC0080000U
/* Same non-cacheable DMA region and 16 ms half buffers as the ranging baseline. */
#define AUDIO_BUFFER ((uint16_t *)0xC0100000U)
#define DMA_WORDS (APP_AUDIO_HALF_FRAMES * 4U)
#define BG 0xFF101820U
#define PANEL 0xFF20313EU
#define MUTED 0xFFA7BBC9U
#define ACCENT 0xFF54D9E8U
#define GOOD 0xFF80E3ADU
#define WARN 0xFFFFD17AU
#define BAD 0xFFFF8888U

extern SAI_HandleTypeDef haudio_in_sai;
extern LTDC_HandleTypeDef hLtdcHandler;
volatile uint32_t micDmaBlocks, micErrors;
static uint32_t lastDraw, lastReceived, seenBlocks, backBuffer=FRAME_B;
static uint32_t distanceMm, resultTick, touchTick, touchSince;
static int32_t drawnTemperature=APP_TEMPERATURE_DECI_C;
static uint64_t presentAt;
static uint8_t framePending, started, touchReady, touchHeld, dirty=1, diagnostics;
static int touchCandidate;
static AppPage drawnPage=APP_PAGE_POSITION;
static MicScope_RangeState rangeState=MIC_SCOPE_RANGE_WAITING;
static const char *audioStatus="MIC STARTING";
static sFONT *uiFont;
static uint32_t textBackground=BG;

void MicScope_SetDistanceMm(uint32_t mm)
{
  if(mm>999999U) { MicScope_SetRangeState(MIC_SCOPE_RANGE_INVALID); return; }
  distanceMm=mm; resultTick=HAL_GetTick(); rangeState=MIC_SCOPE_RANGE_VALID;
}
void MicScope_SetRangeState(MicScope_RangeState state)
{
  rangeState=(state==MIC_SCOPE_RANGE_WAITING || state==MIC_SCOPE_RANGE_MEASURING) ?
    state : MIC_SCOPE_RANGE_INVALID;
}
static void Font(sFONT *font) { uiFont=font; BSP_LCD_SetFont(font); }
static void Text(uint16_t x,uint16_t y,const char *text,uint32_t color)
{
  /* Bound every string to the physical LCD even if a diagnostic grows. */
  char safe[81]; unsigned n=0,limit=(480U-x)/uiFont->Width;
  if(limit>80U) limit=80U;
  while(n<limit && text[n]) { safe[n]=text[n]; ++n; } safe[n]=0;
  BSP_LCD_SetBackColor(textBackground); BSP_LCD_SetTextColor(color);
  BSP_LCD_DisplayStringAt(x,y,(uint8_t *)safe,LEFT_MODE);
}
static void Box(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint32_t color)
{ BSP_LCD_SetTextColor(color); BSP_LCD_FillRect(x,y,w,h); }
static void Button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *label,int enabled)
{
  Box(x,y,w,h,PANEL); textBackground=PANEL; Font(&Font12);
  Text(x+8,y+(h-12)/2,label,enabled ? ACCENT : MUTED); textBackground=BG;
}
static void Tabs(AppPage page)
{
  static const char *names[]={"STANDARD","CLAP","SYNC WAVE","POSITION"};
  unsigned i;
  Font(&Font12);
  for(i=0;i<4;++i) {
    Box((uint16_t)(i*120),0,119,32,PANEL); textBackground=PANEL;
    Text((uint16_t)(i*120+12),10,names[i],i==(unsigned)page ? ACCENT : MUTED);
    if(i==(unsigned)page) Box((uint16_t)(i*120),29,119,3,ACCENT);
  }
  textBackground=BG;
}
static void Common(void)
{
  char line[80]; uint64_t elapsed,local;
  int synced=AppRange_SyncElapsed(&elapsed,&local);
  Font(&Font12);
  (void)snprintf(line,sizeof(line),"%s | NET %s | SYNC %s | %s",APP_BOARD_NAME,
    appNetStatus.online ? "ONLINE" : "WAIT",
    AppCapture_Busy() ? "PAUSE" : (appRangeStatus.locked ? "LOCK" : "WAIT"),
    !touchReady ? "TOUCH ERROR" : (AppCapture_Busy() ? "SAVING" :
    (AppRange_SettingsReady() ? "LINKED" : "SETTINGS WAIT")));
  Text(12,38,line,appNetStatus.online && touchReady ? MUTED : WARN);
  (void)snprintf(line,sizeof(line),"SYNC %lu.%06lu s  LOCAL %llu us",
    (unsigned long)(elapsed/1000000ULL),(unsigned long)(elapsed%1000000ULL),
    (unsigned long long)local);
  Text(12,53,line,synced ? ACCENT : WARN);
}
static const char *BatchText(void)
{
  switch(appRangeStatus.batchStage) {
    case 1:return "BATCH COLLECTING / 11s";
    case 2:return "BATCH ACCEPTED";
    case 3:return "BATCH TOO FEW";
    case 4:return "BATCH UNSTABLE";
    case 5:return "BATCH OUT OF RANGE";
    default:return "WAITING FOR SOUND";
  }
}
static void Footer(void)
{
  Font(&Font12);
  Text(12,241,AppCapture_Text(),AppCapture_Busy() ? WARN : ACCENT);
  Text(270,241,"SAVE: A BOARD USER KEY",MUTED);
  Text(12,257,AppCapture_Detail()[0] ? AppCapture_Detail() : audioStatus,
    AppCapture_Detail()[0] ? WARN : MUTED);
}
static void Standard(uint32_t now)
{
  char line[80]; unsigned p;
  int32_t temp=AppRange_Temperature(),absolute=temp<0 ? -temp : temp;
  int32_t speed=331300+(606*temp)/10;
  int fresh=rangeState==MIC_SCOPE_RANGE_VALID && now-resultTick<APP_RANGE_RESULT_HOLD_MS &&
    appRangeStatus.locked && AppRange_SettingsReady();
  uint32_t color=fresh ? (appRangeStatus.resultIsStat ? GOOD : WARN) : MUTED;
  Common(); Font(&Font24);
  if(fresh) (void)snprintf(line,sizeof(line),"%lu.%03lu m",
    (unsigned long)(distanceMm/1000),(unsigned long)(distanceMm%1000));
  else (void)snprintf(line,sizeof(line),"--.--- m");
  Text(12,73,line,color); Font(&Font12);
  (void)snprintf(line,sizeof(line),"%s | SOURCE %s",
    fresh ? (appRangeStatus.resultIsStat ? "STAT" : "SINGLE") :
      (rangeState==MIC_SCOPE_RANGE_VALID ? "EXPIRED" :
       (rangeState==MIC_SCOPE_RANGE_INVALID ? "INVALID" : "WAIT")),
    fresh ? (appRangeStatus.direction>0 ? "A" : (appRangeStatus.direction<0 ? "B" : "?")) : "--");
  Text(248,75,line,color);
  Text(248,91,appRangeStatus.peakUncertain ? "LAST / PEAK UNCERTAIN" : BatchText(),WARN);
  (void)snprintf(line,sizeof(line),"Q:%lu EQ:%lu  N:%lu/%lu SPAN:%lumm AT:%s",
    (unsigned long)appRangeStatus.quality,(unsigned long)appRangeStatus.eventQuality,
    (unsigned long)appRangeStatus.batchUsed,(unsigned long)appRangeStatus.batchCount,
    (unsigned long)appRangeStatus.batchSpanMm,appRangeStatus.audioTimeReady ? "OK" : "WAIT");
  Text(12,108,line,MUTED);
  (void)snprintf(line,sizeof(line),"LOCAL L LAST #%lu / TOP CANDIDATE",
    (unsigned long)appRangeArrival.eventId);
  Text(12,128,line,MUTED);
  Button(382,122,86,22,"DETAILS",1);
  Font(&Font12);
  for(p=0;p<3;++p) {
    if((appRangeArrival.valid&(1U<<p)) && appRangeStatus.locked)
      (void)snprintf(line,sizeof(line),"P%u  SYNC %lu.%06lu s  LOCAL %llu us",p+1,
        (unsigned long)(appRangeArrival.syncUs[p]/1000000ULL),
        (unsigned long)(appRangeArrival.syncUs[p]%1000000ULL),
        (unsigned long long)appRangeArrival.localUs[p]);
    else (void)snprintf(line,sizeof(line),"P%u  SYNC --.------ s  LOCAL -- us",p+1);
    Text(12,(uint16_t)(145+p*14),line,ACCENT);
  }
  Font(&Font16);
  (void)snprintf(line,sizeof(line),"TEMP %s%ld.%ld C",temp<0 ? "-" : "",(long)(absolute/10),(long)(absolute%10));
  Text(12,197,line,LCD_COLOR_WHITE); Font(&Font12);
  (void)snprintf(line,sizeof(line),"SOUND %ld.%03ld m/s  UNCAL",(long)(speed/1000),(long)(speed%1000));
  Text(12,218,line,MUTED);
  Button(306,191,76,36,"-0.5 C",AppRange_SettingsReady() && !AppCapture_Busy() && temp>-100);
  Button(392,191,76,36,"+0.5 C",AppRange_SettingsReady() && !AppCapture_Busy() && temp<500);
  Footer();
}
static void Diagnostics(void)
{
  char line[80]; const RangePairDiag *d=&appRangeStatus.pairFailure;
  Common(); Font(&Font12);
  Text(12,74,"QUALITY / DETECTOR / LAST REJECTED PAIR",ACCENT);
  (void)snprintf(line,sizeof(line),"D:%lu G:%lu O:%lu EQ:%lu Q:%lu",
    (unsigned long)(appRangeStatus.audioDrops%10000U),(unsigned long)(appRangeStatus.audioGapDrops%10000U),
    (unsigned long)(appRangeStatus.audioOverruns%10000U),(unsigned long)appRangeStatus.eventQuality,
    (unsigned long)appRangeStatus.quality); Text(12,93,line,WARN);
  (void)snprintf(line,sizeof(line),"PK:%lu AM:%lu IC:%lu DS:%luus",
    (unsigned long)appRangeStatus.peakCandidates,(unsigned long)(appRangeStatus.peakAmbiguous%10000U),
    (unsigned long)(appRangeStatus.peakInconsistent%10000U),(unsigned long)(appRangeStatus.peakPairSpreadNs/1000));
  Text(12,109,line,WARN);
  (void)snprintf(line,sizeof(line),"RX:%lu TX:%lu ACK:%lu R:%lu J:%lu DT:%ldus%s",
    (unsigned long)(appRangeStatus.eventRx%10000U),(unsigned long)(appRangeStatus.eventTxAttempts%10000U),
    (unsigned long)(appRangeStatus.eventAck%10000U),(unsigned long)(appRangeStatus.results%10000U),
    (unsigned long)(appRangeStatus.rejected%10000U),(long)appRangeStatus.pairDeltaUs,
    appRangeStatus.pairDeltaValid ? "" : " (OLD/NA)"); Text(12,125,line,MUTED);
  (void)snprintf(line,sizeof(line),"CG:%lu F1:%lu F2:%lu F3:%lu",
    (unsigned long)(rangeDspDiagnostics.coarsePassed%1000000U),
    (unsigned long)(rangeDspDiagnostics.failPulse[0]%1000000U),
    (unsigned long)(rangeDspDiagnostics.failPulse[1]%1000000U),
    (unsigned long)(rangeDspDiagnostics.failPulse[2]%1000000U)); Text(12,141,line,MUTED);
  (void)snprintf(line,sizeof(line),"GP:%lu OK:%lu NC:%lu OV:%lu",
    (unsigned long)(rangeDspDiagnostics.gapRejected%1000000U),(unsigned long)(rangeDspDiagnostics.signatures%1000000U),
    (unsigned long)(rangeDspDiagnostics.noCandidates%1000000U),(unsigned long)(rangeDspDiagnostics.candidateOverflow%1000000U));
  Text(12,157,line,MUTED);
  (void)snprintf(line,sizeof(line),"FAIL#%lu %s A:%u B:%u N:%u RR:%lu%%",
    (unsigned long)(d->serial%10000U),!d->serial ? "--" : (d->reason==1 ? "AM" : (d->reason==2 ? "OV" : "IC")),
    (unsigned)d->countA,(unsigned)d->countB,(unsigned)d->pairs,
    (unsigned long)(d->bestScore ? 100ULL*d->runnerScore/d->bestScore : 0)); Text(12,173,line,WARN);
  if(d->bestA) (void)snprintf(line,sizeof(line),"BEST:%ldus Q:%lu S:%luns A%uB%u",(long)(d->bestNs/1000),
    (unsigned long)d->bestScore,(unsigned long)d->bestSpanNs,(unsigned)d->bestA,(unsigned)d->bestB);
  else (void)snprintf(line,sizeof(line),"BEST:--"); Text(12,189,line,ACCENT);
  if(d->runnerA) (void)snprintf(line,sizeof(line),"NEXT:%ldus Q:%lu S:%luns A%uB%u",(long)(d->runnerNs/1000),
    (unsigned long)d->runnerScore,(unsigned long)d->runnerSpanNs,(unsigned)d->runnerA,(unsigned)d->runnerB);
  else (void)snprintf(line,sizeof(line),"NEXT:--"); Text(12,205,line,MUTED);
  (void)snprintf(line,sizeof(line),"SYNC +/- %luns  DSP:%luus LOAD:%lu/1000",
    (unsigned long)appRangeStatus.syncErrorNs,(unsigned long)appRangeStatus.dspMaxUs,
    (unsigned long)appRangeStatus.dspLoadPermille); Text(12,222,line,MUTED);
  Button(356,241,112,28,"BACK",1); Text(12,249,"Counters wrap; logs retain full values",MUTED);
}
static void ClapDemo(void)
{
  Common(); Font(&Font16); Text(12,80,"CLAP / DEMO ONLY",WARN);
  Font(&Font24); Text(12,111,"0.500 m",MUTED);
  Font(&Font12); Text(250,117,"SAMPLE SOURCE: A",MUTED);
  Text(12,153,"SAMPLE Q:920  N:10/10  SPAN:2mm",MUTED);
  Text(12,174,"SAMPLE ARRIVAL: 12.345678 s",MUTED);
  Text(12,205,"Fixed illustration. Clap detector is not implemented.",WARN);
  Text(12,223,"Ranging paused; clock sync and audio recording remain.",MUTED);
  Footer();
}
static void WaveDemo(void)
{
  unsigned x,c; Common(); Font(&Font12);
  Text(12,76,"DEMO WAVE / NOT LIVE AUDIO / RANGING PAUSED",WARN);
  for(c=0;c<2;++c) {
    int center=133+(int)c*69;
    Text(12,(uint16_t)(center-32),c ? "LOCAL R (DEMO)" : "LOCAL L (DEMO)",MUTED);
    BSP_LCD_SetTextColor(PANEL);
    for(x=12;x<468;x+=38) BSP_LCD_DrawVLine((uint16_t)x,(uint16_t)(center-18),37);
    BSP_LCD_DrawHLine(12,(uint16_t)center,456);
    for(x=12;x<468;++x) {
      int y=center-(int)(16.0f*sinf((float)(x-12)*0.075f));
      BSP_LCD_DrawPixel((uint16_t)x,(uint16_t)y,c ? ACCENT : GOOD);
    }
  }
  Text(12,229,"Shared refresh target: 20 Hz (hardware check pending)",MUTED);
  Footer();
}
static void Draw(uint32_t now,AppPage page)
{
  textBackground=BG; BSP_LCD_Clear(BG); Tabs(page);
  switch(page) {
    case APP_PAGE_STANDARD: if(diagnostics) Diagnostics(); else Standard(now); break;
    case APP_PAGE_CLAP: ClapDemo(); break;
    case APP_PAGE_WAVE: WaveDemo(); break;
    default: break; /* Reserved positioning page is intentionally blank. */
  }
}
static int Hit(uint16_t x,uint16_t y)
{
  if(x>=480 || y>=272) return 0;
  if(y<32) return 1+x/120;
  if(AppRange_Page()!=APP_PAGE_STANDARD) return 0;
  if(diagnostics) return x>=356 && y>=241 ? 8 : 0;
  if(x>=382 && y>=122 && y<144) return 7;
  if(y>=191 && y<227) {
    if(x>=306 && x<382) return 5;
    if(x>=392 && x<468) return 6;
  }
  return 0;
}
static void Touch(uint32_t now)
{
  TS_StateTypeDef state={0}; int hit; uint32_t readStart;
  if(!touchReady || now-touchTick<20U) return;
  touchTick=now;
  readStart=HAL_GetTick();
  if(BSP_TS_GetState(&state)!=TS_OK || HAL_GetTick()-readStart>10U) {
    /* The shared BSP I2C read has a 1 s fault timeout and may still return
     * TS_OK. Latch a slow/faulty device off instead of stalling every poll. */
    touchReady=0; dirty=1; return;
  }
  /* Multitouch or a drag never becomes an accidental button repeat. */
  hit=state.touchDetected==1 ? Hit(state.touchX[0],state.touchY[0]) : 0;
  if(state.touchDetected>1) touchHeld=1;
  if(hit!=touchCandidate) { touchCandidate=hit; touchSince=now; }
  if(!state.touchDetected) {
    if(now-touchSince>=40U) touchHeld=0;
    return;
  }
  if(!hit || touchHeld || now-touchSince<40U) return;
  touchHeld=1;
  if(hit<=4) (void)AppRange_RequestPage((AppPage)(hit-1));
  else if(hit==5 || hit==6) (void)AppRange_AdjustTemperature(hit==5 ? -5 : 5);
  else { diagnostics=(uint8_t)(hit==7); dirty=1; }
}
static void StoreHalf(uint32_t offset)
{
  AppRange_Audio((const volatile int16_t *)AUDIO_BUFFER+offset,APP_AUDIO_HALF_FRAMES);
  ++micDmaBlocks;
}
void BSP_AUDIO_IN_HalfTransfer_CallBack(void) { StoreHalf(0); }
void BSP_AUDIO_IN_TransferComplete_CallBack(void) { StoreHalf(APP_AUDIO_HALF_FRAMES*2U); }
void BSP_AUDIO_IN_Error_CallBack(void) { ++micErrors; AppRange_AudioError(); }
void MicScope_Init(void)
{
  touchReady=BSP_TS_Init(480,272)==TS_OK;
  Draw(HAL_GetTick(),AppRange_Page());
  if(BSP_AUDIO_IN_Init(APP_AUDIO_SAMPLE_RATE,16,2)!=AUDIO_OK) { audioStatus="MIC INIT ERROR"; return; }
  HAL_NVIC_DisableIRQ(AUDIO_IN_INT_IRQ);
  if(BSP_AUDIO_IN_Record(AUDIO_BUFFER,DMA_WORDS)!=AUDIO_OK ||
    HAL_SAI_GetState(&haudio_in_sai)!=HAL_SAI_STATE_BUSY_RX) { audioStatus="MIC START ERROR"; return; }
  lastReceived=HAL_GetTick(); started=1; audioStatus="MIC RUNNING";
}
void MicScope_Process(void)
{
  uint32_t now=HAL_GetTick(),refresh;
  uint64_t master=0,period;
  int synced; AppPage page;
  if(started && seenBlocks!=micDmaBlocks) { seenBlocks=micDmaBlocks; lastReceived=now; }
  if(started && (micErrors || haudio_in_sai.ErrorCode || now-lastReceived>1000U)) {
    audioStatus="MIC DATA ERROR"; AppRange_AudioError(); started=0;
  }
  Touch(now); page=AppRange_Page();
  if(drawnTemperature!=AppRange_Temperature()) {
    drawnTemperature=AppRange_Temperature(); dirty=1;
  }
  if(page!=drawnPage) { diagnostics=0; drawnPage=page; dirty=1; }
  refresh=page==APP_PAGE_WAVE ? MIC_SCOPE_WAVE_REFRESH_MS : MIC_SCOPE_REFRESH_MS;
  period=(uint64_t)refresh*1000000ULL;
  synced=AppRange_MasterTime(&master);
  if(dirty) { framePending=0; presentAt=0; lastDraw=now-refresh; dirty=0; }
  if(!synced) {
    if(presentAt) lastDraw=now-refresh;
    presentAt=0; framePending=0;
  }
  if(LTDC->SRCR & LTDC_SRCR_VBR) return;
  if(synced) {
    if(framePending) {
      if(master<presentAt) return;
      if(master-presentAt<period) {
        BSP_LCD_SetLayerAddress_NoReload(0,backBuffer); BSP_LCD_Reload(LCD_RELOAD_VERTICAL_BLANKING);
        backBuffer=backBuffer==FRAME_A ? FRAME_B : FRAME_A;
      }
      framePending=0; presentAt=(master/period+1U)*period; return;
    }
    if(!presentAt || master>=presentAt) presentAt=(master/period+1U)*period;
    if(master+(uint64_t)MIC_SCOPE_PREPARE_MS*1000000ULL<presentAt) return;
  } else if(now-lastDraw<refresh) return;
  if(!AppRange_DisplayReady()) return;
  hLtdcHandler.LayerCfg[0].FBStartAdress=backBuffer;
  Draw(now,page); lastDraw=now; __DSB();
  if(synced) { framePending=1; return; }
  BSP_LCD_SetLayerAddress_NoReload(0,backBuffer); BSP_LCD_Reload(LCD_RELOAD_VERTICAL_BLANKING);
  backBuffer=backBuffer==FRAME_A ? FRAME_B : FRAME_A;
}
