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
#include <stdlib.h>
#include <string.h>
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
    if(p) {
      size_t used=strlen(line);
      if(appRangeStatus.locked && (appRangeArrival.valid&(3U<<(p-1)))==(3U<<(p-1))) {
        uint64_t current=appRangeArrival.localUs[p],previous=appRangeArrival.localUs[p-1];
        (void)snprintf(line+used,sizeof(line)-used," %c%lluus",current>=previous ? '+':'-',
          (unsigned long long)(current>=previous ? current-previous : previous-current));
      } else (void)snprintf(line+used,sizeof(line)-used," +--us");
    }
    Text(12,(uint16_t)(145+p*14),line,ACCENT);
  }
  Font(&Font16);
  (void)snprintf(line,sizeof(line),"TEMP %s%ld.%ld C",temp<0 ? "-" : "",(long)(absolute/10),(long)(absolute%10));
  Text(12,197,line,LCD_COLOR_WHITE); Font(&Font12);
  (void)snprintf(line,sizeof(line),"SOUND %ld.%03ld m/s  UNCAL",(long)(speed/1000),(long)(speed%1000));
  Text(12,218,line,MUTED);
  { unsigned status=AppRange_AutoTemperatureStatus();
    if(status) Text(174,197,status==1 ? "OK" : (status==2 ? "N/A" : (status==3 ? "WAIT":"ERR")),WARN);
  }
  Button(222,191,74,36,"AUTO",AppRange_SettingsReady() && !AppCapture_Busy());
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
static void ClapPage(uint32_t now)
{
  char line[80];
  int valid=appClapStatus.valid && appRangeStatus.locked && AppRange_SettingsReady() &&
            now-appClapStatus.updatedMs<=10000U;
  Common();Font(&Font16);Text(12,78,"CLAP / SINGLE SHOT",ACCENT);
  Font(&Font24);
  if(valid) {
    (void)snprintf(line,sizeof(line),"%lu cm",(unsigned long)appClapStatus.distanceCm);
    Text(12,106,line,GOOD);
  } else Text(12,106,"-- cm",MUTED);
  Font(&Font12);
  Text(254,113,valid ? (appClapStatus.direction>0 ? "SOURCE: A SIDE" :
    (appClapStatus.direction<0 ? "SOURCE: B SIDE":"SOURCE: UNCERTAIN")) : "SOURCE: --",valid ? GOOD:MUTED);
  if(!appRangeStatus.locked) Text(12,145,"WAIT FOR CLOCK SYNC",WARN);
  else if(!AppRange_SettingsReady()) Text(12,145,"WAIT FOR SETTINGS ACK",WARN);
  else Text(12,145,appClapStatus.ready ? "READY / CLAP EVERY 1s":"LEARNING / KEEP QUIET",ACCENT);
  (void)snprintf(line,sizeof(line),"TEMP:%s%ld.%ld C",AppRange_Temperature()<0 ? "-":"",
    (long)abs(AppRange_Temperature()/10),(long)abs(AppRange_Temperature()%10));
  Text(326,145,line,MUTED);
  (void)snprintf(line,sizeof(line),"E:%lu RX:%lu REJ:%lu DROP:%lu NOISE:%lu Q:%lu",
    (unsigned long)appClapStatus.events,(unsigned long)appClapStatus.received,
    (unsigned long)appClapStatus.rejected,(unsigned long)appClapStatus.drops,
    (unsigned long)appClapStatus.noise,(unsigned long)appClapStatus.quality);
  Text(12,165,line,MUTED);
  Text(12,184,"RECENT (cm) / NEWEST FIRST",ACCENT);
  {
    unsigned i,n=appClapStatus.recentCount;uint32_t sum=0;
    Font(&Font16);
    for(i=0;i<6;++i) {
      if(i<n) { sum+=appClapStatus.recentCm[i];
        (void)snprintf(line,sizeof(line),"%u: %u",i+1,appClapStatus.recentCm[i]); }
      else (void)snprintf(line,sizeof(line),"%u: --",i+1);
      Text((uint16_t)(12+(i%3)*154),(uint16_t)(200+(i/3)*19),line,MUTED);
    }
    if(n) { uint32_t mean=(sum*10+n/2)/n;
      (void)snprintf(line,sizeof(line),"AVG(%u): %lu.%lu cm",n,(unsigned long)(mean/10),(unsigned long)(mean%10)); }
    else (void)snprintf(line,sizeof(line),"AVG: -- cm");
    Text(12,247,line,GOOD);
  }
  Button(340,241,128,28,"CLEAR STATS",AppRange_SettingsReady() && !AppCapture_Busy());
}
static void WaveLive(void)
{
  static int16_t trace[456],candidate[456];
  static uint32_t lastGood;
  static int valid;
  uint64_t ps=AppRange_WavePeriodPs();
  uint32_t mhz=ps ? (uint32_t)(1000000000000000ULL/ps) : 0;
  unsigned x; int32_t lo=32767,hi=-32768,sum=0,mean,scale=512,previous=160;
  int fresh=started && AppRange_WaveRead(presentAt,candidate,456);
  int have;
  if(fresh) { memcpy(trace,candidate,sizeof(trace)); lastGood=HAL_GetTick(); valid=1; }
  if(!started || HAL_GetTick()-lastGood>200U) valid=0;
  have=valid;
  char line[80];
  Common(); Font(&Font12);
  Text(12,76,"LOCAL MIC L / LIVE / 10ms / RANGE OFF",ACCENT);
  if(ps) (void)snprintf(line,sizeof(line),"TONE LOCK %lu.%03lu Hz / AUTO GAIN",
    (unsigned long)(mhz/1000),(unsigned long)(mhz%1000));
  else (void)snprintf(line,sizeof(line),"500Hz CAL %lu/5s / KEEP SOURCE + BOARDS STILL",
    (unsigned long)(AppRange_WaveCalMs()/1000U));
  Text(12,93,line,ps ? GOOD : WARN);
  BSP_LCD_SetTextColor(PANEL);
  for(x=12;x<=468;x+=45) BSP_LCD_DrawVLine((uint16_t)x,114,93);
  for(x=116;x<=204;x+=22) BSP_LCD_DrawHLine(12,(uint16_t)x,456);
  if(have) {
    for(x=0;x<456;++x) {
      int32_t v=trace[x]; sum+=v; if(v<lo) lo=v; if(v>hi) hi=v;
    }
    mean=sum/456;
    if(hi-mean>scale) scale=hi-mean;
    if(mean-lo>scale) scale=mean-lo;
    BSP_LCD_SetTextColor(lo<=-32700 || hi>=32700 ? BAD : GOOD);
    for(x=0;x<456;++x) {
      int32_t y=160-(trace[x]-mean)*43/scale;
      int32_t top;
      if(!x) previous=y;
      top=y<previous ? y : previous;
      BSP_LCD_DrawVLine((uint16_t)(x+12),(uint16_t)top,(uint16_t)(abs(y-previous)+1));
      previous=y;
    }
    (void)snprintf(line,sizeof(line),"P-P:%ld %s",(long)(hi-lo),
      lo<=-32700 || hi>=32700 ? "CLIPPING" : (hi-lo<256 ? "LOW SIGNAL" : "PCM"));
    Text(12,213,line,hi-lo<256 ? WARN : MUTED);
  } else Text(12,153,AppCapture_Busy() ? "SAVING / WAVE PAUSED" :
    (!started ? "MIC ERROR" : "WAITING FOR AUDIO TIME / FRAME"),WARN);
  Button(338,211,130,26,"RELOCK TONE",AppRange_SettingsReady() && !AppCapture_Busy());
  Text(12,241,"LIVE VIEW / 20Hz TARGET / 10ms WINDOW",MUTED);
  Text(12,257,audioStatus,MUTED);
}

static void PositionLine(int x0,int y0,int x1,int y1,uint32_t color)
{
  int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
  for(;;) {
    int twice;
    if(x0>=0 && x0<480 && y0>=0 && y0<272) Box((uint16_t)x0,(uint16_t)y0,2,2,color);
    if(x0==x1 && y0==y1) break;
    twice=2*err; if(twice>=dy) { err+=dy;x0+=sx; } if(twice<=dx) { err+=dx;y0+=sy; }
  }
}
static void Outline(unsigned x,unsigned y,unsigned w,unsigned h,uint32_t color)
{ Box(x,y,w,2,color); Box(x,y+h-2,w,2,color); Box(x,y,2,h,color); Box(x+w-2,y,2,h,color); }
static void PositionPage(uint32_t now)
{
  char line[80];
  int valid=appPositionStatus.valid && appRangeStatus.locked && AppRange_SettingsReady() &&
            now-appPositionStatus.updatedMs<=1500U;
  Common(); Font(&Font12);
  if(APP_AUDIO_SAMPLE_RATE!=48000U) { Text(12,100,"POSITION REQUIRES 48kHz FIRMWARE",WARN); return; }
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    Text(12,76,"A LEFT / B RIGHT / KEEP STILL",ACCENT);
    Button(340,74,128,26,"CAL FRONT 50cm",AppRange_SettingsReady());
    PositionLine(208,103,434,103,MUTED); Text(293,87,"13 cm",ACCENT);
    Outline(12,116,226,100,0xFFFFFFFFU); Outline(238,116,230,100,0xFFFFFFFFU);
    Outline(24,128,166,76,0xFF168DFFU); Outline(250,128,166,76,0xFF168DFFU);
    Font(&Font24); Text(94,154,"A",0xFFFFFFFFU); Text(320,154,"B",0xFFFFFFFFU); Font(&Font12);
    Box(204,145,10,10,0xFFFF4444U); Box(204,180,10,10,0xFFFF4444U);
    Box(430,145,10,10,0xFFFF4444U); Box(430,180,10,10,0xFFFF4444U);
    Text(219,145,"L",MUTED); Text(219,180,"R",MUTED);
    Text(447,145,"L",MUTED); Text(447,180,"R",MUTED);
  } else {
    /* 480/9.5 and 272/5.5 px per cm: +0.5cm right, -0.5cm up.
     * Visual origin only; physical microphone coordinates remain unchanged. */
    const int ox=25,oy=-25;
    Text(12,76,"DIRECTION / 0 = TOP",ACCENT);
    Outline(107+ox,146+oy,166,36,PANEL);
    Box(106+ox,146+oy,8,8,0xFFFF4444U); Box(106+ox,174+oy,8,8,0xFFFF4444U);
    Box(266+ox,146+oy,8,8,0xFFFF4444U); Box(266+ox,174+oy,8,8,0xFFFF4444U);
    Text(88+ox,189+oy,"A",MUTED); Text(277+ox,189+oy,"B",MUTED);
    Text(168+ox,208+oy,"13 cm",MUTED); Text(282+ox,157+oy,"2 cm",MUTED);
    Text(169+ox,94+oy,"FRONT",MUTED);
    if(valid) {
      float rad=appPositionStatus.angleDeg*0.01745329252f;
      float dx=sinf(rad),dy=-cosf(rad);
      int x=190+ox+(int)(dx*54),y=164+oy+(int)(dy*54);
      PositionLine(190+ox,164+oy,x,y,GOOD);
      PositionLine(x,y,x-(int)(dx*12+dy*7),y-(int)(dy*12-dx*7),GOOD);
      PositionLine(x,y,x-(int)(dx*12-dy*7),y-(int)(dy*12+dx*7),GOOD);
      Font(&Font24); (void)snprintf(line,sizeof(line),"%ld deg",(long)appPositionStatus.angleDeg);
      Text(345,117,line,GOOD); Font(&Font12);
      (void)snprintf(line,sizeof(line),"Q:%lu",(unsigned long)appPositionStatus.quality); Text(345,151,line,MUTED);
      (void)snprintf(line,sizeof(line),"FIT:%lu us",(unsigned long)(appPositionStatus.residualNs/1000)); Text(345,169,line,MUTED);
    } else Text(345,128,"NO DIRECTION",WARN);
    Text(345,193,appPositionStatus.calibration==2 ? "CAL OK" :
      (appPositionStatus.calibration==1 ? "CALIBRATING" : (appPositionStatus.calibration==3 ? "CAL FAILED":"UNCAL")),WARN);
    if(valid) {
      static const char *names[]={"FRONT","FRONT-RIGHT","RIGHT","BACK-RIGHT","BACK","BACK-LEFT","LEFT","FRONT-LEFT"};
      Text(345,211,names[((appPositionStatus.angleDeg+22)/45)%8],GOOD);
    }
  }
  (void)snprintf(line,sizeof(line),"E:%lu RX:%lu LV:%lu Q:%lu/%lu G:%lu B:%lu AT:%s",
    (unsigned long)appPositionStatus.events,(unsigned long)appPositionStatus.received,
    (unsigned long)appPositionStatus.inputLevel,(unsigned long)appPositionStatus.peakQuality[0],
    (unsigned long)appPositionStatus.peakQuality[1],(unsigned long)appPositionStatus.gapResets,
    (unsigned long)appPositionStatus.backlogResets,
    appRangeStatus.audioTimeReady ? "OK":"WAIT");
  Font(&Font12);Text(12,225,line,MUTED);
  (void)snprintf(line,sizeof(line),"LAG:%lums DSP:%luus LCD:%luus",
    (unsigned long)(appPositionStatus.lagMaxSamples/24U),
    (unsigned long)appPositionStatus.dspMaxUs,(unsigned long)appPositionStatus.lcdMaxUs);
  Text(12,239,line,MUTED);
  if(!appRangeStatus.locked) Text(12,253,"WAIT FOR CLOCK SYNC",WARN);
  else if(!AppRange_SettingsReady()) Text(12,253,"WAIT FOR SETTINGS ACK",WARN);
  else if(appPositionStatus.calibration==1) {
    (void)snprintf(line,sizeof(line),"CAL FRONT 50cm: %u/8 / KEEP SOURCE STILL",appPositionStatus.calibrationCount);
    Text(12,253,line,WARN);
  }
  else if(valid) {
    (void)snprintf(line,sizeof(line),"DIRECTION %ld DEG / Q %lu / STANDARD CHIRP",(long)appPositionStatus.angleDeg,(unsigned long)appPositionStatus.quality);
    Text(12,253,line,GOOD);
  } else Text(12,253,appPositionStatus.updatedMs && now-appPositionStatus.updatedMs<1500U ?
    "UNCERTAIN / CHECK PLACEMENT + REFLECTIONS" : "PLAY POSITION CHIRP / WAITING FOR SOUND",WARN);
}

static void Draw(uint32_t now,AppPage page)
{
  textBackground=BG; BSP_LCD_Clear(BG); Tabs(page);
  switch(page) {
    case APP_PAGE_STANDARD: if(diagnostics) Diagnostics(); else Standard(now); break;
    case APP_PAGE_CLAP: ClapPage(now); break;
    case APP_PAGE_WAVE: WaveLive(); break;
    case APP_PAGE_POSITION: PositionPage(now); break;
    default: break;
  }
}
static int Hit(uint16_t x,uint16_t y)
{
  if(x>=480 || y>=272) return 0;
  if(y<32) return 1+x/120;
  if(AppRange_Page()==APP_PAGE_POSITION) return APP_BOARD_ROLE==APP_BOARD_A && x>=340 && y>=74 && y<100 ? 10 : 0;
  if(AppRange_Page()==APP_PAGE_WAVE) return x>=338 && y>=211 && y<237 ? 9 : 0;
  if(AppRange_Page()==APP_PAGE_CLAP) return x>=340 && x<468 && y>=241 && y<269 ? 11 : 0;
  if(AppRange_Page()!=APP_PAGE_STANDARD) return 0;
  if(diagnostics) return x>=356 && y>=241 ? 8 : 0;
  if(x>=382 && y>=122 && y<144) return 7;
  if(y>=191 && y<227) {
    if(x>=222 && x<296) return 12;
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
  else if(hit==5 || hit==6) { (void)AppRange_AdjustTemperature(hit==5 ? -5 : 5); }
  else if(hit==12) {
    (void)AppRange_AutoTemperature();dirty=1;
  }
  else if(hit==11) { (void)AppRange_ClearClapStats(); dirty=1; }
  else if(hit==10) { (void)AppRange_PositionCalibrate(); dirty=1; }
  else if(hit==9) { (void)AppRange_WaveRelock(); dirty=1; }
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
  refresh=page==APP_PAGE_WAVE ? MIC_SCOPE_WAVE_REFRESH_MS :
    (page==APP_PAGE_POSITION ? MIC_SCOPE_POSITION_REFRESH_MS :
    (page==APP_PAGE_CLAP ? MIC_SCOPE_CLAP_REFRESH_MS : MIC_SCOPE_REFRESH_MS));
  period=(uint64_t)refresh*1000000ULL;
  /* Standard/DETAILS and POSITION use each board's local 2 Hz cadence.
   * WAVE and CLAP retain common presentation times. */
  synced=(page==APP_PAGE_WAVE || page==APP_PAGE_CLAP) && AppRange_MasterTime(&master);
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
  {
    uint64_t drawStart=RangeClock_Now();
    Draw(now,page);
    if(page==APP_PAGE_STANDARD || page==APP_PAGE_POSITION) {
      /* Retain the local 500 ms grid; skip missed slots instead of queuing
       * old frames or shifting every deadline after a slow DSP iteration. */
      lastDraw+=((now-lastDraw)/refresh)*refresh;
    } else lastDraw=now;
    __DSB();
    if(page==APP_PAGE_POSITION) {
      uint32_t us=(uint32_t)((RangeClock_Now()-drawStart)/1000U);
      if(us>appPositionStatus.lcdMaxUs) appPositionStatus.lcdMaxUs=us;
    }
  }
  if(synced) { framePending=1; return; }
  BSP_LCD_SetLayerAddress_NoReload(0,backBuffer); BSP_LCD_Reload(LCD_RELOAD_VERTICAL_BLANKING);
  backBuffer=backBuffer==FRAME_A ? FRAME_B : FRAME_A;
}
