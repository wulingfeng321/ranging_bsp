/* Exercise the actual display code with bounded LCD drawing and a fake clock. */
#include <assert.h>
#include <string.h>
#include <math.h>
#include "../../Core/Src/app_mic_scope.c"
#include "../../../../Utilities/Fonts/font12.c"
#include "../../../../Utilities/Fonts/font16.c"
#include "../../../../Utilities/Fonts/font24.c"
SAI_HandleTypeDef haudio_in_sai;
LTDC_HandleTypeDef hLtdcHandler;
TestLtdc testLtdc;
AppNetStatus appNetStatus;
AppRangeStatus appRangeStatus;
RangeDspDiagnostics rangeDspDiagnostics;
static uint64_t masterClock;
static uint32_t localOffset, clears, flips, verticals, errors;
static int ready=1;
static uint32_t pixels[272][480], fg, bg;
static sFONT *font;
static unsigned diagMask;
uint32_t HAL_GetTick(void) { return (uint32_t)(masterClock/1000000ULL)+localOffset; }
int AppRange_MasterTime(uint64_t *ns) { *ns=masterClock; return appRangeStatus.locked; }
int AppRange_DisplayReady(void) { return ready; }
void AppRange_AudioError(void) { ++errors; }
void AppRange_Audio(const volatile int16_t *pcm,uint32_t n) { (void)pcm; (void)n; }
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
{ unsigned x,y; ++clears; diagMask=0; for(y=0;y<272;++y) for(x=0;x<480;++x) pixels[y][x]=color; }
void BSP_LCD_DisplayStringAt(uint16_t x,uint16_t y,uint8_t *text,int mode)
{
  unsigned c,row,col,stride=(font->Width+7)/8;
  (void)mode;
  assert(x+strlen((const char *)text)*font->Width<=480 && y+font->Height<=272);
  if(y==96 && x==120) diagMask|=1;
  if(y==108) diagMask|=2;
  if(y==120) diagMask|=4;
  if(y==184 && x==120) diagMask|=8;
  if(y==196) diagMask|=16;
  if(y==208) diagMask|=32;
  if(y==132) diagMask|=64;
  if(y==220) diagMask|=128;
  for(c=0;text[c];++c) for(row=0;row<font->Height;++row) for(col=0;col<font->Width;++col) {
    const uint8_t *glyph=font->table+(text[c]-32)*font->Height*stride;
    pixels[y+row][x+c*font->Width+col]=(glyph[row*stride+col/8]&(128U>>(col%8)))?fg:bg;
  }
}
void BSP_LCD_DrawHLine(uint16_t x,uint16_t y,uint16_t w)
{ unsigned i; assert(x+w<=480 && y<272); for(i=0;i<w;++i) pixels[y][x+i]=fg; }
void BSP_LCD_DrawVLine(uint16_t x,uint16_t y,uint16_t h)
{ unsigned i; assert(x<480 && y+h<=272); ++verticals; for(i=0;i<h;++i) pixels[y+i][x]=fg; }
void BSP_LCD_SetLayerAddress_NoReload(int layer,uint32_t address)
{ assert(layer==0 && (address==FRAME_A || address==FRAME_B)); }
void BSP_LCD_Reload(int mode)
{ assert(mode==LCD_RELOAD_VERTICAL_BLANKING); ++flips; testLtdc.SRCR=LTDC_SRCR_VBR; }
static void Step(uint32_t ms)
{
  masterClock=(uint64_t)ms*1000000ULL;
  ++micDmaBlocks; /* DMA remains live independently of the LCD. */
  MicScope_Process();
}
static void Save(const char *path)
{
  unsigned x,y; FILE *f=fopen(path,"wb"); assert(f);
  fprintf(f,"P6\n480 272\n255\n");
  for(y=0;y<272;++y) for(x=0;x<480;++x) {
    fputc((pixels[y][x]>>16)&255,f); fputc((pixels[y][x]>>8)&255,f); fputc(pixels[y][x]&255,f);
  }
  fclose(f);
}
int main(int argc,char **argv)
{
  unsigned i, before;
  rangeDspDiagnostics.coarsePassed=999999;
  for(i=0;i<3;++i) {
    rangeDspDiagnostics.failPulse[i]=999999;
    rangeDspDiagnostics.pulseMax[i]=1.0f;
  }
  rangeDspDiagnostics.coarseMax=1.0f;
  rangeDspDiagnostics.gapRejected=999999;
  rangeDspDiagnostics.signatures=999999;
  rangeDspDiagnostics.noCandidates=999999;
  rangeDspDiagnostics.candidateOverflow=999999;
  localOffset=APP_BOARD_ROLE==APP_BOARD_B ? 1703U:0U;
  appNetStatus.online=1;
  MicScope_Init(); validFrames=WINDOW_FRAMES; writeFrame=WINDOW_FRAMES-113;
  for(i=0;i<WINDOW_FRAMES;++i) {
    unsigned pos=(writeFrame+i)%WINDOW_FRAMES;
    history[pos][0]=(int16_t)(8000*sin(i*0.09)*sin(i*0.004));
    history[pos][1]=(int16_t)(6500*sin(i*0.095)*sin(i*0.004));
  }
  Step(500); assert(flips==1 && verticals==0 && diagMask==255); /* No pre-sync wave. */
  rangeDspDiagnostics.haveGap=1; rangeDspDiagnostics.gapMinRatio=1000.0f;
  testLtdc.SRCR=0; appRangeStatus.locked=1;
  appRangeStatus.audioTimeReady=1; appRangeStatus.samplePeriodPs=20833333;
  appRangeStatus.quality=999; appRangeStatus.eventQuality=950;
  appRangeStatus.peakCandidates=3; appRangeStatus.peakAmbiguous=9999;
  appRangeStatus.peakInconsistent=9999; appRangeStatus.syncErrorNs=1500;
  appRangeStatus.dspMaxUs=99999; appRangeStatus.dspLoadPermille=432;
  appRangeStatus.direction=1; appRangeStatus.resultIsStat=1;
  appRangeStatus.batchStage=2; appRangeStatus.batchCount=15; appRangeStatus.batchUsed=12;
  MicScope_SetDistanceMm(130); appRangeStatus.peakUncertain=1;
  before=clears; Step(899); assert(clears==before);
  Step(900); assert(clears==before+1 && flips==1 && framePending && diagMask==255);
  for(i=0;i<WINDOW_FRAMES;++i) {
    assert(snapshot[i][0]==history[(writeFrame+i)%WINDOW_FRAMES][0]);
    assert(snapshot[i][1]==history[(writeFrame+i)%WINDOW_FRAMES][1]);
  }
  appRangeStatus.pairFailure.serial=9999; appRangeStatus.pairFailure.reason=1;
  appRangeStatus.pairFailure.countA=3; appRangeStatus.pairFailure.countB=3;
  appRangeStatus.pairFailure.pairs=9;
  appRangeStatus.pairFailure.bestA=3; appRangeStatus.pairFailure.bestB=3;
  appRangeStatus.pairFailure.runnerA=2; appRangeStatus.pairFailure.runnerB=1;
  appRangeStatus.pairFailure.bestNs=-39999999; appRangeStatus.pairFailure.runnerNs=39999999;
  appRangeStatus.pairFailure.bestScore=1000000; appRangeStatus.pairFailure.runnerScore=999999;
  appRangeStatus.pairFailure.bestSpanNs=40000; appRangeStatus.pairFailure.runnerSpanNs=40000;
  DrawRangeDiagnostics();
  if(argc>1) Save(argv[1]);
  Step(999); assert(flips==1);
  ready=0; Step(1000); assert(flips==2 && !framePending); /* DSP cannot delay prepared flip. */
  Step(1400); assert(clears==before+1); /* Pending VBlank protects front buffer. */
  testLtdc.SRCR=0; Step(1401); assert(clears==before+1); /* Busy DSP skips preparation. */
  Step(1500); assert(flips==2); /* No catch-up burst. */
  ready=1; Step(1900); assert(framePending && presentAt==2000000000ULL);
  appRangeStatus.locked=0; before=verticals; Step(1950);
  assert(!framePending && presentAt==0 && flips==3 && verticals==before);
  testLtdc.SRCR=0; appRangeStatus.locked=1; Step(2400); assert(framePending);
  Step(3100); assert(!framePending && flips==3); /* Stale frame discarded. */
  Step(3400); assert(framePending); Step(3500); assert(flips==4 && errors==0);
  printf("PASS: scope %s sync cadence, backlog, lost lock, ring order, fixed diagnostics, LCD bounds\n",APP_BOARD_NAME);
  return 0;
}
