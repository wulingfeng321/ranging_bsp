#include "app_wave.h"
#include "range_wave.h"

static RangeWaveTone tone;
static AppWaveClock clockState;
static uint32_t clockId;
static int16_t block[APP_AUDIO_HALF_FRAMES];

void AppWave_Reset(void)
{
  RangeWave_Reset(&tone);
  memset(&clockState,0,sizeof(clockState));clockId=0;
}
const AppWaveClock *AppWave_GetClock(void) { return &clockState; }

static int AudioValid(const AppAudioView *audio)
{
  return audio && audio->ring && audio->snapshot &&
    audio->ringSamples>2U*APP_AUDIO_HALF_FRAMES &&
    !(audio->ringSamples&(audio->ringSamples-1U));
}
static int Retained(const AppAudioView *audio,uint64_t first)
{
  uint64_t count;uint32_t epoch;
  audio->snapshot(&count,&epoch);
  return epoch==audio->epoch && count>=first && count-first<audio->ringSamples;
}
int AppWave_Calibrate(const AppAudioView *audio,uint64_t blockEndNs,
                      double samplePeriodNs,uint64_t (*nowNs)(void))
{
  uint64_t end;uint32_t i;
  if(!AudioValid(audio) || !nowNs || clockState.periodPs ||
     audio->count<APP_AUDIO_HALF_FRAMES || samplePeriodNs<=0) return 0;
  end=tone.blocks ? tone.nextSample+APP_AUDIO_HALF_FRAMES : audio->count;
  if(end>audio->count || audio->count-end>audio->ringSamples-2U*APP_AUDIO_HALF_FRAMES) {
    RangeWave_Reset(&tone);end=audio->count;
  }
  for(;end<=audio->count;end+=APP_AUDIO_HALF_FRAMES) {
    uint64_t stamp=blockEndNs-(uint64_t)((audio->count-end)*samplePeriodNs);
    for(i=0;i<APP_AUDIO_HALF_FRAMES;++i)
      block[i]=audio->ring[(uint32_t)(end-APP_AUDIO_HALF_FRAMES+i)&(audio->ringSamples-1U)];
    if(!Retained(audio,end-APP_AUDIO_HALF_FRAMES)) { RangeWave_Reset(&tone);return 0; }
    RangeWave_Add(&tone,block,APP_AUDIO_HALF_FRAMES,end,stamp);
  }
  clockState.calibrationMs=tone.blocks ? (uint32_t)((blockEndNs-tone.baseNs)/1000000ULL) : 0;
  if(clockState.calibrationMs>5000) clockState.calibrationMs=5000;
  if(!tone.periodPs) return 0;
  clockState.periodPs=tone.periodPs;
  clockState.originNs=(nowNs()/1000000000ULL+1U)*1000000000ULL;
  return 1;
}
int AppWave_AcceptClock(uint32_t id,uint64_t periodPs,uint64_t originNs,uint32_t calibrationMs)
{
  if(calibrationMs>5000 || (periodPs &&
     (periodPs<1666666666ULL || periodPs>2500000000ULL || !originNs))) return 0;
  if(clockId && (id==clockId || id-clockId>=0x80000000UL)) return 1;
  clockId=id;clockState.periodPs=periodPs;clockState.originNs=originNs;
  clockState.calibrationMs=calibrationMs;return 1;
}
int AppWave_Read(const AppAudioView *audio,const AppAudioTime *time,
                 uint64_t presentNs,int synchronized,int16_t *out,unsigned points)
{
  uint64_t start,base;uint32_t i;
  double first,last;float fraction0,step;
  if(!AudioValid(audio) || !time || time->periodNs<=0 || !out || points<2 || points>480) return 0;
  start=RangeWave_Start(presentNs,synchronized ? clockState.originNs : 0,
                       synchronized ? clockState.periodPs : 0);
  first=(double)time->anchorSample+((double)start-time->anchorNs)/time->periodNs;
  last=first+10000000.0/time->periodNs;
  if(first<0 || last+1>=(double)audio->count ||
     (double)audio->count-first>audio->ringSamples-APP_AUDIO_HALF_FRAMES) return 0;
  /* Keep absolute sample indices integer; interpolate with the single-precision
   * FPU per pixel, preserving the existing 48 kHz display calculation. */
  base=(uint64_t)first;fraction0=(float)(first-(double)base);
  step=(float)((last-first)/(points-1U));
  for(i=0;i<points;++i) {
    float at=fraction0+step*i;
    uint32_t relative=(uint32_t)at;
    uint64_t index=base+relative;
    float fraction=at-relative;
    int32_t a=audio->ring[(uint32_t)index&(audio->ringSamples-1U)];
    int32_t b=audio->ring[(uint32_t)(index+1U)&(audio->ringSamples-1U)];
    float value=a+(b-a)*fraction;
    out[i]=(int16_t)(value>=0 ? value+0.5f : value-0.5f);
  }
  return Retained(audio,base);
}
