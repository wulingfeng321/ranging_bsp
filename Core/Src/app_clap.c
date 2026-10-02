#include "app_clap.h"
#include "clap_dsp.h"
#include <string.h>

AppClapStatus appClapStatus;
static ClapDetector clapDetector;
static uint64_t clapCursor,clapLocal,clapRemote;
static uint32_t clapEpoch,clapLocalQ,clapRemoteQ,clapEventId,clapRemoteId;
static uint32_t clapResultId,clapReceivedId,clapEventMs,clapRemoteMs,clapSendMs,clapResultMs;
static uint8_t clapHaveLocal,clapHaveRemote;
void AppClap_Reset(uint64_t count,uint32_t epoch)
{
  clapCursor=count;clapEpoch=epoch;
  Clap_Reset(&clapDetector);memset(&appClapStatus,0,sizeof(appClapStatus));
  clapHaveLocal=clapHaveRemote=0;clapEventId=clapRemoteId=clapResultId=clapReceivedId=0;
}
static void ClapRecord(uint32_t cm)
{
  unsigned i;
  for(i=5;i>0;--i) appClapStatus.recentCm[i]=appClapStatus.recentCm[i-1];
  appClapStatus.recentCm[0]=(uint16_t)cm;
  if(appClapStatus.recentCount<6) ++appClapStatus.recentCount;
}
static uint64_t ClapHistoryPack(void)
{
  unsigned i;uint64_t bits=(uint64_t)appClapStatus.recentCount<<54;
  for(i=0;i<appClapStatus.recentCount;++i) bits|=(uint64_t)appClapStatus.recentCm[i]<<(i*9);
  return bits;
}
/* Snapshot, not incremental replication: a lost result cannot desync history. */
static int ClapHistoryValid(uint64_t bits,uint32_t latest)
{
  unsigned i,n=(unsigned)(bits>>54);
  if(!n || n>6 || (bits&511U)!=latest) return 0;
  for(i=0;i<6;++i) {
    unsigned cm=(unsigned)((bits>>(i*9))&511U);
    if((i<n && cm>300) || (i>=n && cm)) return 0;
  }
  return 1;
}

void AppClap_Restart(uint64_t count,uint32_t epoch)
{
  uint32_t drops=appClapStatus.drops+1;
  AppClap_Reset(count,epoch);appClapStatus.drops=drops;
}
void AppClap_PauseDetection(void) { appClapStatus.ready=0; }
void AppClap_PublishLocal(uint64_t stamp,uint32_t quality,uint32_t id,uint32_t now)
{
  clapLocal=stamp;clapLocalQ=quality;clapHaveLocal=1;
  clapEventId=id;clapEventMs=now;clapSendMs=now-150U;
  ++appClapStatus.events;appClapStatus.arrivalUs=stamp/1000U;
}
int AppClap_Process(const AppAudioView *audio,const AppAudioTime *time,
                     uint64_t (*nowNs)(void),AppClapOnset *event)
{
  uint64_t start=nowNs();uint32_t n;int detected=0;
  if(audio->epoch!=clapEpoch || audio->count-clapCursor>audio->ringSamples-APP_AUDIO_HALF_FRAMES) {
    AppClap_Restart(audio->count,audio->epoch);return 0;
  }
  for(n=0;n<4096 && clapCursor<audio->count;++n,++clapCursor) {
    uint64_t onset;uint32_t quality;int found;
    if(n && !(n&63U) && nowNs()-start>=1500000ULL) break;
    found=Clap_Push(&clapDetector,audio->ring[(uint32_t)clapCursor&(audio->ringSamples-1U)],clapCursor,&onset,&quality);
    if(found<0) ++appClapStatus.rejected;
    if(found>0) {
      uint64_t newest;uint32_t epoch;
      double stamp=time->anchorNs+((double)onset-(double)time->anchorSample)*time->periodNs;
      audio->snapshot(&newest,&epoch);
      if(epoch!=audio->epoch || newest-clapCursor>=audio->ringSamples) {
        AppClap_Restart(newest,epoch);return 0;
      }
      event->localNs=stamp;event->quality=quality;detected=1;
      ++clapCursor;break;
    }
  }
  appClapStatus.noise=(uint32_t)clapDetector.noise;
  appClapStatus.ready=(uint8_t)(clapDetector.warm>=APP_AUDIO_SAMPLE_RATE/2U);
  return detected;
}
int AppClap_ReceiveEvent(uint32_t id,uint64_t stamp,uint64_t quality,uint64_t current,uint32_t now)
{
  if(!stamp || stamp>current || current-stamp>1000000000ULL || quality>1000) return 0;
  if(clapRemoteId && (id==clapRemoteId || id-clapRemoteId>=0x80000000UL)) return 1;
  clapRemoteId=id;clapRemote=stamp;clapRemoteQ=(uint32_t)quality;
  clapRemoteMs=now;clapHaveRemote=1;++appClapStatus.received;return 1;
}
int AppClap_ReceiveResult(uint32_t id,uint64_t cm,uint64_t side,uint64_t quality,uint64_t history,uint32_t now)
{
  unsigned i;
  if(cm>300 || side>2 || quality>1000 || !ClapHistoryValid(history,(uint32_t)cm)) return 0;
  if(clapReceivedId && (id==clapReceivedId || id-clapReceivedId>=0x80000000UL)) return 1;
  clapReceivedId=id;++appClapStatus.received;appClapStatus.distanceCm=(uint32_t)cm;
  appClapStatus.recentCount=(uint8_t)(history>>54);
  for(i=0;i<6;++i) appClapStatus.recentCm[i]=(uint16_t)((history>>(9*i))&511U);
  appClapStatus.direction=side==1 ? 1 : (side==2 ? -1:0);
  appClapStatus.quality=(uint32_t)quality;appClapStatus.updatedMs=now;appClapStatus.valid=1;return 1;
}
int AppClap_Tick(uint32_t now,int active,int32_t temperature,uint32_t syncErrorNs,
                 uint32_t (*nextId)(void),AppClapTx *message)
{
  if(!active) {
    appClapStatus.valid=appClapStatus.ready=0;return 0;
  }
  if(appClapStatus.valid && now-appClapStatus.updatedMs>10000U) appClapStatus.valid=0;
  if(clapHaveLocal && now-clapEventMs>1000U) clapHaveLocal=0;
  if(clapHaveRemote && now-clapRemoteMs>1000U) clapHaveRemote=0;
  if(APP_BOARD_ROLE==APP_BOARD_B) {
    if(clapHaveLocal && now-clapSendMs>=100U) {
      clapSendMs=now;memset(message,0,sizeof(*message));
      message->kind=APP_CLAP_TX_EVENT;message->id=clapEventId;
      message->arrivalNs=clapLocal;message->quality=clapLocalQ;return 1;
    }
  } else {
    if(clapHaveLocal && clapHaveRemote) {
      int64_t delta=(int64_t)clapRemote-(int64_t)clapLocal;
      if(delta>10000000) clapHaveLocal=0;
      else if(delta< -10000000) clapHaveRemote=0;
      else {
        uint64_t magnitude=(uint64_t)(delta<0 ? -delta:delta);
        float cm=(float)magnitude*(331.3f+0.0606f*temperature)/10000000.0f;
        clapHaveLocal=clapHaveRemote=0;
        if(cm>300) { ++appClapStatus.rejected;return 0; }
        appClapStatus.distanceCm=(uint32_t)(cm+0.5f);
        ClapRecord(appClapStatus.distanceCm);
        appClapStatus.direction=magnitude>100000U+syncErrorNs ? (delta>0 ? 1:-1):0;
        appClapStatus.quality=clapLocalQ<clapRemoteQ ? clapLocalQ:clapRemoteQ;
        appClapStatus.updatedMs=now;appClapStatus.valid=1;
        clapResultId=nextId();clapResultMs=now;clapSendMs=now-150U;
      }
    }
    if(clapResultId && now-clapResultMs<1000U && now-clapSendMs>=100U) {
      clapSendMs=now;
      memset(message,0,sizeof(*message));message->kind=APP_CLAP_TX_RESULT;message->id=clapResultId;
      message->distanceCm=appClapStatus.distanceCm;message->quality=appClapStatus.quality;
      message->side=appClapStatus.direction>0 ? 1 : (appClapStatus.direction<0 ? 2:0);
      message->history=ClapHistoryPack();return 1;
    }
  }
  return 0;
}
