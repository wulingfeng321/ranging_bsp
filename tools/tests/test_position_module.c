#include <assert.h>
#include <stdio.h>
#include "app_board_config.h"
#include "app_position.h"
#include "position_dsp.h"

static int16_t ring[2][APP_POSITION_RING_SAMPLES];
static uint64_t newest,clockNs;
static uint32_t epoch,sequence,remoteId;
static void Snapshot(uint64_t *count,uint32_t *generation)
{ *count=newest;*generation=epoch; }
static uint64_t Now(void) { clockNs+=2000000ULL;return clockNs; }
static uint32_t NextId(void) { return ++sequence; }
static int Tick(uint32_t now,AppPositionTx *tx)
{ return AppPosition_Tick(now,1,250,NextId,tx); }
static AppPositionAudio Audio(void)
{
  AppPositionAudio a;
  a.channel[0]=ring[0];a.channel[1]=ring[1];
  a.count=newest;a.epoch=epoch;a.snapshot=Snapshot;return a;
}

static void CheckDetectionAndRaces(void)
{
  unsigned i,k;int found=0;
  AppPositionAudio audio;
  AppAudioTime time={0,10000000000.0,1e9/48000};
  AppPositionOnset onset;
  newest=768;epoch=1;clockNs=0;audio=Audio();AppPosition_Reset(0,epoch);
  memset(ring,0,sizeof(ring));
  for(i=0;i<768;++i) for(k=0;k<2;++k) {
    double t=(2.0*i-200-2*k)/48000;
    ring[k][i]=(int16_t)(10000*(Position_Tone(t)+Position_Tone(t+1.0/48000)));
  }
  /* A one-start time budget forces coarse/fine search to survive many yields. */
  for(i=0;i<400 && !found;++i) found=AppPosition_Process(&audio,&time,Now,&onset);
  assert(found && i>2 && onset.quality>700);
  for(k=0;k<2;++k)
    assert(fabs(onset.localNs[k]-time.anchorNs-(100+k)*2*time.periodNs)<15000);
  assert(!appPositionStatus.events); /* publication follows master-clock mapping */
  AppPosition_Reset(0,epoch);
  newest=APP_POSITION_RING_SAMPLES+10; /* producer overwrites during scan */
  assert(!AppPosition_Process(&audio,&time,Now,&onset));
  assert(appPositionStatus.backlogResets==1 && !appPositionStatus.events);
  newest=768;audio=Audio();AppPosition_Reset(0,epoch);++epoch;
  assert(!AppPosition_Process(&audio,&time,Now,&onset));
  assert(appPositionStatus.gapResets==1 && !appPositionStatus.events);
}

static void CheckReceiveAndExpiry(void)
{
  AppPositionTx tx;uint32_t start=0xffffff00U;
  AppPosition_Reset(0,1);
  assert(!AppPosition_ReceiveEvent(1,0,10,700,100));
  assert(!AppPosition_ReceiveEvent(1,101,100,700,100));
  assert(!AppPosition_ReceiveEvent(1,1,1,700,1000000002ULL));
  assert(!AppPosition_ReceiveEvent(1,1000000,1175001,700,2000000));
  assert(!AppPosition_ReceiveEvent(1,100,100,1001,100));
  assert(AppPosition_ReceiveEvent(0xfffffffeU,100,100,700,100));
  assert(AppPosition_ReceiveEvent(0xfffffffeU,100,100,700,100));
  assert(appPositionStatus.received==1);
  assert(AppPosition_ReceiveEvent(1,100,100,700,100));
  assert(AppPosition_ReceiveEvent(0xffffffffU,100,100,700,100));
  assert(appPositionStatus.received==2);
  AppPosition_Reset(0,1);
  assert(!AppPosition_ReceiveResult(1,360,900,1,1000,0,start));
  assert(!AppPosition_ReceiveResult(1,45,1001,1,1000,0,start));
  assert(!AppPosition_ReceiveResult(1,45,900,2,1000,0,start));
  assert(!AppPosition_ReceiveResult(1,45,900,1,1000001,0,start));
  assert(!AppPosition_ReceiveResult(1,45,900,1,1000,57,start));
  assert(!AppPosition_ReceiveResult(1,45,900,1,1000,25,start));
  assert(AppPosition_ReceiveResult(0xfffffffeU,45,900,1,1000,40,start));
  assert(AppPosition_ReceiveResult(0xfffffffeU,45,900,1,1000,40,start+100));
  assert(appPositionStatus.updatedMs==start && appPositionStatus.received==1);
  assert(!Tick(start+1500,&tx) && appPositionStatus.valid);
  assert(!Tick(start+1501,&tx) && !appPositionStatus.valid);
  assert(AppPosition_ReceiveResult(1,90,900,1,1000,40,start+1502));
  assert(AppPosition_ReceiveResult(0xffffffffU,45,900,1,1000,40,start+1503));
  assert(appPositionStatus.angleDeg==90 && appPositionStatus.received==2);
}

static void Pair(uint32_t now,double bias,uint32_t quality,AppPositionTx *tx)
{
  uint64_t local[2],remote[2],base=10000000000ULL;
  double lower=(sqrt(.065*.065+.51*.51)-sqrt(.065*.065+.49*.49))*1e9/346.45;
  local[0]=base;local[1]=(uint64_t)(base+lower+bias);
  remote[0]=base+17000;remote[1]=(uint64_t)(base+lower+12000);
  AppPosition_PublishLocal(local,quality,NextId(),now);
  assert(AppPosition_ReceiveEvent(++remoteId,remote[0],remote[1],950,base+1000000));
  assert(Tick(now,tx) && tx->kind==APP_POSITION_TX_RESULT);
}

static void CheckCalibrationAndRetry(void)
{
  AppPositionTx tx;unsigned i;uint32_t start=0xffffff00U;
  AppPosition_Reset(0,1);
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    AppPositionAudio audio;AppPositionOnset onset;
    AppAudioTime time={0,10000000000.0,1e9/48000};
    AppPosition_BeginCalibration();
    Pair(start,8000,699,&tx);assert(!appPositionStatus.calibrationCount);
    for(i=0;i<3;++i) Pair(start+(i+1)*500U,8000,950,&tx);
    assert(appPositionStatus.calibrationCount==3);
    newest=4000;epoch=1;audio=Audio();
    assert(!AppPosition_Process(&audio,&time,Now,&onset));
    assert(appPositionStatus.backlogResets==1 && appPositionStatus.calibrationCount==3);
    for(i=3;i<8;++i) Pair(start+(i+1)*500U,8000,950,&tx);
    assert(appPositionStatus.calibration==2 && tx.calibration==40);
    Pair(start+4500,8000,950,&tx);assert(tx.valid && tx.angleDeg<2);
    assert(!Tick(start+4649,&tx));assert(Tick(start+4650,&tx));
    assert(!Tick(start+5700,&tx));
    assert(!Tick(start+6000,&tx) && appPositionStatus.valid);
    assert(!Tick(start+6001,&tx) && !appPositionStatus.valid);
    AppPosition_RestartTimebase(4000,2);
    assert(appPositionStatus.calibration==0 && !appPositionStatus.calibrationCount);
    AppPosition_Reset(0,2);AppPosition_BeginCalibration();
    for(i=0;i<8;++i) Pair(i*500,90000,950,&tx);
    assert(appPositionStatus.calibration==3 && !tx.valid);
    AppPosition_Reset(0,2);AppPosition_BeginCalibration();Pair(0,8000,950,&tx);
    AppPosition_RestartTimebase(0,3);
    assert(appPositionStatus.calibration==1 && !appPositionStatus.calibrationCount);
  } else {
    uint64_t stamp[2]={10000000000ULL,10000050000ULL};
    AppPosition_PublishLocal(stamp,900,123,start);
    assert(Tick(start,&tx) && tx.kind==APP_POSITION_TX_EVENT && tx.id==123);
    assert(tx.arrivalNs[1]==stamp[1] && tx.quality==900);
    assert(!Tick(start+149,&tx));assert(Tick(start+150,&tx));
    assert(!Tick(start+1000,&tx));
    AppPosition_Reset(0,1);assert(!Tick(start+1200,&tx));
  }
  assert(!AppPosition_Tick(10000,0,250,NextId,&tx) && !appPositionStatus.valid);
}

int main(void)
{
  CheckDetectionAndRaces();CheckReceiveAndExpiry();CheckCalibrationAndRetry();
  puts("PASS: independent position module; budget yields, ring races, payload/sequence/tick boundaries, calibration and recovery");
  return 0;
}
