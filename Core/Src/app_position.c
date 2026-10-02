#include "app_position.h"
#include "app_board_config.h"
#include "position_dsp.h"
#if APP_POSITION_RING_SAMPLES != POS_RING
#error Position ring size must match the detector
#endif

AppPositionStatus appPositionStatus;
static PositionDetector positionDetector;
static uint64_t positionCursor,positionLocal[2],positionRemote[2];
static uint32_t positionEpoch,positionLocalQ,positionRemoteQ,positionRemoteId;
static uint32_t positionEventId,positionResultId,positionSendMs,positionEventMs;
static uint32_t positionReceivedId,positionSearchLeft;
static uint8_t positionHaveLocal,positionHaveRemote;
static double positionBias[3],positionCalSum[3],positionCalSquare[3];
void AppPosition_Reset(uint64_t count,uint32_t epoch)
{
  positionCursor=count;positionEpoch=epoch;
  positionHaveLocal=positionHaveRemote=0;
  positionEventId=positionResultId=positionRemoteId=positionReceivedId=0;
  memset(&appPositionStatus,0,sizeof(appPositionStatus));
  memset(positionBias,0,sizeof(positionBias));
  memset(positionCalSum,0,sizeof(positionCalSum));
  memset(positionCalSquare,0,sizeof(positionCalSquare));
  positionSearchLeft=0;
  if(!positionDetector.energy) Position_Init(&positionDetector);
  else Position_ResetPeaks(&positionDetector);
}
static void PositionRestartAudio(uint64_t count,uint32_t epoch,unsigned gap)
{
  positionCursor=count;positionEpoch=epoch;
  positionSearchLeft=0;Position_ResetPeaks(&positionDetector);
  positionHaveLocal=positionHaveRemote=0;positionEventId=0;
  ++appPositionStatus.overruns;
  if(gap) ++appPositionStatus.gapResets;else ++appPositionStatus.backlogResets;
  appPositionStatus.valid=0;
}
void AppPosition_RestartTimebase(uint64_t count,uint32_t epoch)
{
  PositionRestartAudio(count,epoch,1);
  /* Preserve a requested calibration through audio warm-up/recovery. */
  memset(positionBias,0,sizeof(positionBias));
  memset(positionCalSum,0,sizeof(positionCalSum));memset(positionCalSquare,0,sizeof(positionCalSquare));
  appPositionStatus.calibrationCount=0;
  if(appPositionStatus.calibration!=1) appPositionStatus.calibration=0;
}
void AppPosition_BeginCalibration(void) { appPositionStatus.calibration=1; }
void AppPosition_PublishLocal(const uint64_t stamp[2],uint32_t quality,uint32_t id,uint32_t now)
{
  positionLocal[0]=stamp[0];positionLocal[1]=stamp[1];
  ++appPositionStatus.events;positionHaveLocal=1;positionLocalQ=quality;
  positionEventId=id;positionEventMs=now;positionSendMs=now-200U;
}
int AppPosition_Process(const AppPositionAudio *audio,const AppAudioTime *time,
                         uint64_t (*nowNs)(void),AppPositionOnset *onset)
{
  uint64_t count=audio->count,anchorSample,budgetStart=nowNs();
  uint32_t epoch=audio->epoch,n,k,j;
  static int16_t clip[2][POS_N];double at[2];uint32_t quality=0;
  if(epoch!=positionEpoch || (count>positionCursor && count-positionCursor>POS_RING-768U)) {
    PositionRestartAudio(count,epoch,epoch!=positionEpoch);return 0;
  }
  if(count>positionCursor && count-positionCursor>appPositionStatus.lagMaxSamples)
    appPositionStatus.lagMaxSamples=(uint32_t)(count-positionCursor);
  anchorSample=time->anchorSample;
  for(n=0;n<64 && positionCursor+POS_N<=count;++n,++positionCursor) {
    float q[2];int found;
    if(n && nowNs()-budgetStart>=1500000ULL) break;
    /* A match at the end of a fine window must finish, not be reset by the
     * next coarse gate. This also holds across time-budget yields. */
    if(!positionSearchLeft && positionDetector.active) positionSearchLeft=64;
    if(!positionSearchLeft) {
      float energy,score[2];
      for(k=0;k<2;++k) {
        score[k]=Position_CoarseRing(&positionDetector,audio->channel[k],
                                    (uint32_t)positionCursor,POS_RING-1U,&energy);
        /* sqrt is only needed on a new maximum, not on every scan start. */
        if(energy>(float)appPositionStatus.inputLevel*appPositionStatus.inputLevel)
          appPositionStatus.inputLevel=(uint32_t)(sqrtf(energy)+0.5f);
      }
      { uint64_t newest;uint32_t currentEpoch;
        audio->snapshot(&newest,&currentEpoch);
        if(epoch!=currentEpoch || newest-positionCursor>=POS_RING) {
          PositionRestartAudio(newest,currentEpoch,epoch!=currentEpoch);return 0;
        }
      }
      if(score[0]<0.08f && score[1]<0.08f) { positionCursor+=7;continue; }
      positionSearchLeft=96;Position_ResetPeaks(&positionDetector);
      /* Rewind 16 starts then refine all starts; preserve both mic peaks. */
      if(positionCursor>=16) positionCursor-=16;
      else positionCursor=0;
      if(positionCursor) --positionCursor; /* for-loop increments before retry */
      continue;
    }
    --positionSearchLeft;
    for(k=0;k<2;++k)
      for(j=0;j<POS_N;++j) clip[k][j]=audio->channel[k][(uint32_t)(positionCursor+j)&(POS_RING-1U)];
    for(k=0;k<2;++k) {
      uint32_t peak;
      q[k]=Position_Score(&positionDetector,clip[k]);
      if(q[k]*1000000.0f>(float)appPositionStatus.peakQuality[k]*appPositionStatus.peakQuality[k]) {
        peak=(uint32_t)(1000*sqrtf(q[k]));if(peak>1000) peak=1000;
        if(peak>appPositionStatus.peakQuality[k]) appPositionStatus.peakQuality[k]=peak;
      }
    }
    { uint64_t newest;uint32_t currentEpoch;
      audio->snapshot(&newest,&currentEpoch);
      if(epoch!=currentEpoch || newest-positionCursor>=POS_RING) {
        PositionRestartAudio(newest,currentEpoch,epoch!=currentEpoch);return 0;
      }
    }
    found=Position_Peak(&positionDetector,positionCursor,q[0],q[1],at,&quality);
    if(!found) continue;
    positionSearchLeft=0;positionCursor+=4800U;
    if(found<0) { ++appPositionStatus.rejected;return 0; }
    for(k=0;k<2;++k)
      onset->localNs[k]=time->anchorNs+(2*at[k]-(double)anchorSample)*time->periodNs;
    onset->quality=quality;return 1;
  }
  return 0;
}

int AppPosition_ReceiveEvent(uint32_t id,uint64_t left,uint64_t right,uint64_t quality,uint64_t current)
{
  if(quality>1000 || !left || !right || left>current || right>current ||
     current-left>1000000000ULL || current-right>1000000000ULL ||
     (left>right ? left-right:right-left)>175000) return 0;
  if(positionRemoteId && (id==positionRemoteId || id-positionRemoteId>=0x80000000UL)) return 1;
  positionRemoteId=id;positionRemote[0]=left;positionRemote[1]=right;
  positionRemoteQ=(uint32_t)quality;positionHaveRemote=1;++appPositionStatus.received;return 1;
}
int AppPosition_ReceiveResult(uint32_t id,uint64_t angle,uint64_t quality,uint64_t valid,
                               uint64_t residual,uint64_t calibration,uint32_t now)
{
  if(angle>=360 || quality>1000 || valid>1 || residual>1000000 ||
     calibration>56 || (calibration&15)>8) return 0;
  if(positionReceivedId && (id==positionReceivedId || id-positionReceivedId>=0x80000000UL)) return 1;
  positionReceivedId=id;++appPositionStatus.received;appPositionStatus.angleDeg=(int32_t)angle;
  appPositionStatus.quality=(uint32_t)quality;appPositionStatus.valid=(uint8_t)valid;
  appPositionStatus.residualNs=(uint32_t)residual;
  appPositionStatus.calibration=(uint8_t)(calibration>>4);
  appPositionStatus.calibrationCount=(uint8_t)(calibration&15);
  appPositionStatus.updatedMs=now;return 1;
}

int AppPosition_Tick(uint32_t now,int active,int32_t temperature,
                      uint32_t (*nextId)(void),AppPositionTx *message)
{
  if(!active) {
    appPositionStatus.valid=0; return 0;
  }
  if(now-appPositionStatus.updatedMs>1500U) appPositionStatus.valid=0;
  if(APP_BOARD_ROLE==APP_BOARD_B) {
    if(positionEventId && now-positionEventMs<1000U && now-positionSendMs>=150U) {
      positionSendMs=now;
      memset(message,0,sizeof(*message));message->kind=APP_POSITION_TX_EVENT;
      message->id=positionEventId;message->arrivalNs[0]=positionLocal[0];message->arrivalNs[1]=positionLocal[1];
      message->quality=positionLocalQ;return 1;
    }
  } else {
    if(positionHaveLocal && positionHaveRemote) {
      int64_t delta=(int64_t)positionRemote[0]-(int64_t)positionLocal[0];
      if(delta>2000000) positionHaveLocal=0;
      else if(delta< -2000000) positionHaveRemote=0;
      else {
        double t[4]; int32_t angle=0; uint32_t residual=0; unsigned i;
        for(i=0;i<2;++i) { t[i]=(double)((int64_t)positionLocal[i]-(int64_t)positionLocal[0]);
          t[i+2]=(double)((int64_t)positionRemote[i]-(int64_t)positionLocal[0]); }
        if(appPositionStatus.calibration==1) {
          /* Operator places source 0.5m from rectangle centre on the TOP axis.
           * Account for actual near-field path differences, not zero all pairs. */
          double sound=331.3+0.0606*temperature;
          double lower=(sqrt(0.065*0.065+0.51*0.51)-sqrt(0.065*0.065+0.49*0.49))*1e9/sound;
          if(positionLocalQ>=700 && positionRemoteQ>=700) {
            for(i=0;i<3;++i) {
              double expected=i==1 ? 0 : lower;
              double error=t[i+1]-expected;
              positionCalSum[i]+=error; positionCalSquare[i]+=error*error;
            }
            ++appPositionStatus.calibrationCount;
            if(appPositionStatus.calibrationCount==8) {
              appPositionStatus.calibration=2;
              for(i=0;i<3;++i) {
                double mean=positionCalSum[i]/8;
                if(positionCalSquare[i]/8-mean*mean>100000000.0 || fabs(mean)>80000)
                  appPositionStatus.calibration=3;
                positionBias[i]=mean;
              }
              if(appPositionStatus.calibration==3) memset(positionBias,0,sizeof(positionBias));
            }
          }
          appPositionStatus.valid=0;
        } else {
          for(i=0;i<3;++i) t[i+1]-=positionBias[i];
          appPositionStatus.valid=(uint8_t)Position_Solve(t,331.3f+0.0606f*temperature,&angle,&residual);
        }
        appPositionStatus.angleDeg=angle; appPositionStatus.residualNs=residual;
        appPositionStatus.quality=positionLocalQ<positionRemoteQ ? positionLocalQ:positionRemoteQ;
        appPositionStatus.updatedMs=now;
        if(!appPositionStatus.valid) ++appPositionStatus.rejected;
        positionResultId=nextId(); positionSendMs=now-200U;
        positionHaveLocal=positionHaveRemote=0;
      }
    }
    if(positionResultId && now-appPositionStatus.updatedMs<1200U && now-positionSendMs>=150U) {
      positionSendMs=now;
      memset(message,0,sizeof(*message));message->kind=APP_POSITION_TX_RESULT;message->id=positionResultId;
      message->angleDeg=(uint32_t)appPositionStatus.angleDeg;message->quality=appPositionStatus.quality;
      message->valid=appPositionStatus.valid;message->residualNs=appPositionStatus.residualNs;
      message->calibration=((uint32_t)appPositionStatus.calibration<<4)|appPositionStatus.calibrationCount;
      return 1;
    }
  }
  return 0;
}
