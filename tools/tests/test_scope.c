/* Actual framebuffer drawing plus deterministic touch and shared-clock tests. */
#include <assert.h>
#include <string.h>
#include <math.h>
#include "../../Core/Src/app_mic_scope.c"
static int busy;
static unsigned clapClears;
int AppRange_ClearClapStats(void) { if(busy || !AppRange_SettingsReady()) return 0; ++clapClears;memset(&appClapStatus,0,sizeof(appClapStatus));return 1; }
int AppCapture_Busy(void) { return busy; }
const char *AppCapture_Text(void) { return "CAP HELD / A USER"; }
const char *AppCapture_Detail(void) { return ""; }
#include "../../../../Utilities/Fonts/font8.c"
#include "../../../../Utilities/Fonts/font12.c"
#include "../../../../Utilities/Fonts/font16.c"
#include "../../../../Utilities/Fonts/font24.c"
SAI_HandleTypeDef haudio_in_sai;
LTDC_HandleTypeDef hLtdcHandler;
TestLtdc testLtdc;
AppNetStatus appNetStatus;
AppRangeStatus appRangeStatus;
AppPositionStatus appPositionStatus;
AppClapStatus appClapStatus;
AppRangeArrival appRangeArrival;
RangeDspDiagnostics rangeDspDiagnostics;
static uint64_t masterClock;
static uint32_t localOffset,clears,flips,errors,points,tempEdits,pageEdits;
static int ready=1,settingsReady=1,touchFail;
static int32_t temp=250;
static AppPage page=APP_PAGE_STANDARD;
static TS_StateTypeDef finger;
static uint32_t pixels[272][480],fg,bg;
static sFONT *font;
static char screenText[8192];
uint32_t HAL_GetTick(void) { return (uint32_t)(masterClock/1000000ULL)+localOffset; }
uint64_t RangeClock_Now(void) { return masterClock+(uint64_t)localOffset*1000000ULL; }
int AppRange_MasterTime(uint64_t *ns) { *ns=masterClock; return appRangeStatus.locked; }
int AppRange_SyncElapsed(uint64_t *elapsed,uint64_t *local)
{ *local=masterClock/1000+localOffset*1000ULL; *elapsed=appRangeStatus.locked ? (masterClock>=43000000000ULL ? masterClock-43000000000ULL : masterClock)/1000 : 0; return appRangeStatus.locked; }
AppPage AppRange_Page(void) { return page; }
int32_t AppRange_Temperature(void) { return temp; }
int AppRange_SettingsReady(void) { return settingsReady; }
int AppRange_RequestPage(AppPage next)
{ if(busy || !settingsReady) return 0; page=next; ++pageEdits; return 1; }
int AppRange_AdjustTemperature(int32_t step)
{ if(busy || !settingsReady) return 0; temp+=step; ++tempEdits; return 1; }
uint64_t AppRange_WavePeriodPs(void) { return 2000000000ULL; }
uint32_t AppRange_WaveCalMs(void) { return 5000; }
int AppRange_PositionCalibrate(void) { return !busy; }
int AppRange_WaveRelock(void) { return !busy; }
static int waveFail;
int AppRange_WaveRead(uint64_t at,int16_t *out,unsigned n)
{ unsigned i; (void)at; for(i=0;i<n;++i) out[i]=(int16_t)(8000*sin(i*10.0*3.141592653589793/(n-1))); return !busy && !waveFail; }
int AppRange_DisplayReady(void) { return ready; }
void AppRange_AudioError(void) { ++errors; }
void AppRange_Audio(const volatile int16_t *pcm,uint32_t n) { (void)pcm; (void)n; }
uint8_t BSP_TS_Init(uint16_t x,uint16_t y) { assert(x==480 && y==272); return 0; }
uint8_t BSP_TS_GetState(TS_StateTypeDef *s) { *s=finger; return (uint8_t)touchFail; }
int BSP_AUDIO_IN_Init(uint32_t rate,int bits,int channels)
{ assert(rate==APP_AUDIO_SAMPLE_RATE && bits==16 && channels==2); return AUDIO_OK; }
int BSP_AUDIO_IN_Record(uint16_t *data,uint32_t words)
{ (void)data; assert(words==APP_AUDIO_HALF_FRAMES*4); return AUDIO_OK; }
int HAL_SAI_GetState(SAI_HandleTypeDef *sai) { (void)sai; return HAL_SAI_STATE_BUSY_RX; }
void HAL_NVIC_DisableIRQ(int irq) { (void)irq; }
void BSP_LCD_SetTextColor(uint32_t color) { fg=color; }
void BSP_LCD_SetBackColor(uint32_t color) { bg=color; }
void BSP_LCD_SetFont(sFONT *f) { font=f; }
void BSP_LCD_Clear(uint32_t color)
{ unsigned x,y; ++clears; screenText[0]=0; for(y=0;y<272;++y) for(x=0;x<480;++x) pixels[y][x]=color; }
void BSP_LCD_DisplayStringAt(uint16_t x,uint16_t y,uint8_t *text,int mode)
{
  unsigned c,row,col,stride=(font->Width+7)/8;
  (void)mode;
  assert(x+strlen((const char *)text)*font->Width<=480 && y+font->Height<=272);
  assert(strlen(screenText)+strlen((const char *)text)+2<sizeof(screenText));
  strcat(screenText,(const char *)text); strcat(screenText,"\n");
  for(c=0;text[c];++c) for(row=0;row<font->Height;++row) for(col=0;col<font->Width;++col) {
    const uint8_t *glyph=font->table+(text[c]-32)*font->Height*stride;
    pixels[y+row][x+c*font->Width+col]=(glyph[row*stride+col/8]&(128U>>(col%8)))?fg:bg;
  }
}
void BSP_LCD_DrawHLine(uint16_t x,uint16_t y,uint16_t w)
{ unsigned i; assert(x+w<=480 && y<272); for(i=0;i<w;++i) pixels[y][x+i]=fg; }
void BSP_LCD_DrawVLine(uint16_t x,uint16_t y,uint16_t h)
{ unsigned i; assert(x<480 && y+h<=272); for(i=0;i<h;++i) pixels[y+i][x]=fg; }
void BSP_LCD_FillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h)
{ unsigned j; assert(x+w<=480 && y+h<=272); for(j=0;j<h;++j) BSP_LCD_DrawHLine(x,y+j,w); }
void BSP_LCD_DrawPixel(uint16_t x,uint16_t y,uint32_t color)
{ assert(x<480 && y<272); pixels[y][x]=color; ++points; }
void BSP_LCD_SetLayerAddress_NoReload(int layer,uint32_t address)
{ assert(layer==0 && (address==FRAME_A || address==FRAME_B)); }
void BSP_LCD_Reload(int mode)
{ assert(mode==LCD_RELOAD_VERTICAL_BLANKING); ++flips; testLtdc.SRCR=LTDC_SRCR_VBR; }
static void Step(uint32_t ms)
{ masterClock=(uint64_t)ms*1000000ULL; ++micDmaBlocks; MicScope_Process(); }
static void Save(const char *prefix,const char *name)
{
  char path[512]; unsigned x,y; FILE *f;
  snprintf(path,sizeof(path),"%s-%s.ppm",prefix,name); f=fopen(path,"wb"); assert(f);
  fprintf(f,"P6\n480 272\n255\n");
  for(y=0;y<272;++y) for(x=0;x<480;++x) {
    fputc((pixels[y][x]>>16)&255,f); fputc((pixels[y][x]>>8)&255,f); fputc(pixels[y][x]&255,f);
  }
  fclose(f);
}
static void Release(uint32_t t)
{ finger.touchDetected=0; Touch(t); Touch(t+40); }
static void Press(uint32_t t,uint16_t x,uint16_t y)
{ finger.touchDetected=1; finger.touchX[0]=x; finger.touchY[0]=y; Touch(t); Touch(t+40); }
int main(int argc,char **argv)
{
  unsigned i,before; uint64_t target;
  appNetStatus.online=1; MicScope_Init();
  Step(500); assert(flips==1 && !points); /* Standard page has no waveform. */
  testLtdc.SRCR=0; appRangeStatus.locked=1;
  before=clears; Step(569); assert(clears==before);
  Step(570); assert(clears==before+1 && framePending && flips==1);
  Step(599); assert(flips==1);
  ready=0; Step(600); assert(flips==2 && !framePending);
  Step(770); assert(clears==before+1); /* Pending VBlank protects front buffer. */
  testLtdc.SRCR=0; Step(771); assert(clears==before+1); /* Busy DSP skips preparation. */
  ready=1; Step(970); assert(framePending);
  Step(1300); assert(!framePending && flips==2); /* Stale frame is dropped. */
  Step(1370); assert(framePending);
  appRangeStatus.locked=0; Step(1380); assert(!framePending && !presentAt && flips==3);
  assert(strstr(screenText,"SYNC 0.000000 s"));
  testLtdc.SRCR=0; appRangeStatus.locked=1; page=APP_PAGE_WAVE;
  Step(1420); target=presentAt; assert(framePending && target==1450000000ULL);
  Step(1450); assert(flips==4);
  /* Page switch cancels the old frame, then uses the new cadence. */
  testLtdc.SRCR=0; Step(1470); assert(framePending);
  page=APP_PAGE_STANDARD; Step(1480); assert(!framePending && presentAt==1600000000ULL);
  /* One press = one edit. No repeat while held or dragged to a second button. */
  Press(2000,420,208); assert(tempEdits==1 && temp==255);
  Touch(2100); Touch(2300); assert(tempEdits==1);
  finger.touchX[0]=320; Touch(2340); Touch(2380); assert(tempEdits==1);
  Release(2400); Press(2500,320,208); assert(tempEdits==2 && temp==250);
  Release(2600); busy=1; Press(2700,420,208); assert(tempEdits==2); busy=0;
  Release(2800); finger.touchDetected=2; Touch(2900);
  finger.touchDetected=1; Touch(2940); assert(tempEdits==2);
  Release(3000); Press(3100,420,133); assert(diagnostics);
  Release(3200); Press(3300,400,253); assert(!diagnostics);
  Release(3400); Press(3500,160,15); assert(page==APP_PAGE_CLAP && pageEdits==1);
  Release(3600); Press(3700,280,15); assert(page==APP_PAGE_WAVE && pageEdits==2);
  Release(3800); Press(3900,420,15); assert(page==APP_PAGE_POSITION);
  /* Render actual firmware code and built-in fonts for visual inspection. */
  page=APP_PAGE_STANDARD; masterClock=55345678000ULL;
  appRangeStatus.quality=950; appRangeStatus.eventQuality=920;
  appRangeStatus.direction=1; appRangeStatus.resultIsStat=1;
  appRangeStatus.batchStage=2; appRangeStatus.batchCount=15; appRangeStatus.batchUsed=12;
  appRangeStatus.batchSpanMm=3; appRangeStatus.audioTimeReady=1;
  appRangeArrival.valid=7; appRangeArrival.eventId=15;
  for(i=0;i<3;++i) { appRangeArrival.syncUs[i]=12001023ULL+i*40000; appRangeArrival.localUs[i]=55001023ULL+i*40000; }
  MicScope_SetDistanceMm(1000); Draw(HAL_GetTick(),page);
  assert(strstr(screenText,"1.000 m") && strstr(screenText,"P3") && strstr(screenText,"+0.5 C"));
  if(argc>1) Save(argv[1],"standard");
  Draw(HAL_GetTick()+APP_RANGE_RESULT_HOLD_MS,page); assert(strstr(screenText,"EXPIRED") && !strstr(screenText,"1.000 m"));
  diagnostics=1;
  rangeDspDiagnostics.coarsePassed=rangeDspDiagnostics.failPulse[0]=999999;
  appRangeStatus.pairFailure.serial=9999; appRangeStatus.pairFailure.reason=1;
  appRangeStatus.pairFailure.bestA=3; appRangeStatus.pairFailure.bestB=3;
  appRangeStatus.pairFailure.runnerA=2; appRangeStatus.pairFailure.runnerB=1;
  appRangeStatus.pairFailure.bestNs=-39999999; appRangeStatus.pairFailure.runnerNs=39999999;
  appRangeStatus.pairFailure.bestScore=1000000; appRangeStatus.pairFailure.runnerScore=999999;
  appRangeStatus.pairFailure.bestSpanNs=40000; appRangeStatus.pairFailure.runnerSpanNs=40000;
  Draw(HAL_GetTick(),page); assert(strstr(screenText,"FAIL#9999") && strstr(screenText,"NEXT:"));
  if(argc>1) Save(argv[1],"diagnostics");
  page=APP_PAGE_CLAP; Draw(HAL_GetTick(),page); assert(strstr(screenText,"CLAP / SINGLE SHOT") && strstr(screenText,"-- cm"));
  appClapStatus.valid=appClapStatus.ready=1;appClapStatus.updatedMs=HAL_GetTick();
  appClapStatus.distanceCm=130;appClapStatus.direction=1;appClapStatus.quality=850;
  Draw(HAL_GetTick(),page);assert(strstr(screenText,"130 cm") && strstr(screenText,"SOURCE: A SIDE"));
  appClapStatus.recentCount=6;
  for(i=0;i<6;++i) appClapStatus.recentCm[i]=(uint16_t)(128+i);
  Draw(HAL_GetTick(),page);assert(strstr(screenText,"AVG(6): 130.5 cm"));
  assert(!strstr(screenText,"LOCAL ARRIVAL") && !strstr(screenText,"TARGET") && !strstr(screenText,"HOLD"));
  if(argc>1) Save(argv[1],"clap");
  appClapStatus.direction=-1;Draw(HAL_GetTick(),page);assert(strstr(screenText,"SOURCE: B SIDE"));
  appClapStatus.valid=0;Draw(HAL_GetTick(),page);assert(strstr(screenText,"-- cm"));
  page=APP_PAGE_WAVE; Draw(HAL_GetTick(),page); assert(strstr(screenText,"LOCAL MIC L / LIVE") && strstr(screenText,"TONE LOCK 500.000"));
  assert(!strstr(screenText,"SAVE:") && !strstr(screenText,"CAP HELD"));
  if(argc>1) Save(argv[1],"wave");
  waveFail=1; masterClock+=50000000ULL; Draw(HAL_GetTick(),page);
  assert(strstr(screenText,"P-P:") && !strstr(screenText,"WAITING"));
  masterClock+=201000000ULL; Draw(HAL_GetTick(),page);
  assert(strstr(screenText,"WAITING"));
  waveFail=0; Draw(HAL_GetTick(),page); assert(strstr(screenText,"P-P:"));
  page=APP_PAGE_POSITION;
  appPositionStatus.valid=1; appPositionStatus.angleDeg=45; appPositionStatus.quality=900;
  appPositionStatus.updatedMs=HAL_GetTick();
  Draw(HAL_GetTick(),page);
  assert(strstr(screenText,"13") && !strstr(screenText,"SAVE:"));
  if(argc>1) Save(argv[1],"position");
  /* POSITION is capped at 2Hz independently from WAVE's 20Hz. */
  testLtdc.SRCR=0;page=APP_PAGE_POSITION;drawnPage=page;dirty=1;
  Step(10020);assert(!framePending && presentAt==10500000000ULL);
  Step(10470);assert(framePending);before=flips;
  Step(10500);assert(flips==before+1);
  testLtdc.SRCR=0;Step(10969);assert(!framePending);
  Step(10970);assert(framePending);
  testLtdc.SRCR=0;page=APP_PAGE_CLAP;drawnPage=page;dirty=1;
  Step(11020);assert(!framePending && presentAt==11100000000ULL);
  Step(11070);assert(framePending);before=flips;
  Step(11100);assert(flips==before+1);
  testLtdc.SRCR=0;Step(11169);assert(!framePending);
  Step(11170);assert(framePending);
  Release(11200);Press(11300,400,250);assert(clapClears==1);
  Touch(11400);assert(clapClears==1);
  Release(11500);busy=1;Press(11600,400,250);assert(clapClears==1);busy=0;
  touchFail=1; Touch(12000); assert(!touchReady && dirty);
  puts("PASS: actual LCD bounds, four pages, expiry, touch debounce, shared cadence, stale-frame discard");
  return 0;
}
