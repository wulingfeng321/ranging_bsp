#define APP_RANGE_JOINT_PEAKS 1
#define APP_RANGE_AUDIO_PROFILE 1
#define APP_RANGE_STATISTICS 1
#define main legacy_test_main
#include "test_app_range.c"
#undef main
static RangePeaks OnePeak(int32_t offset,uint32_t quality)
{
  RangePeaks peaks; unsigned p;
  memset(&peaks,0,sizeof(peaks)); peaks.count=1;
  for(p=0;p<3;++p) peaks.peak[0].offsetNs[p]=offset;
  peaks.peak[0].quality=quality; return peaks;
}
static void InjectPeaks(uint32_t id,uint64_t stamp,const RangePeaks *peaks)
{
  testPayloadU=peaks->count>1 ? RangePeak_Pack(&peaks->peak[1]):0;
  testPayloadV=peaks->count>2 ? RangePeak_Pack(&peaks->peak[2]):0;
  Inject(PEAK_EVENT,id,peerEpoch,stamp,((uint64_t)APP_RANGE_AUDIO_PROFILE<<16)|
         (peaks->overflow<<8)|peaks->count,RangePeak_Pack(&peaks->peak[0]));
  testPayloadU=0; testPayloadV=0;
}
int main(void)
{
  RangePeaks remote; uint64_t stamp; unsigned i,k;
  AppRange_Init(); appNetStatus.online=1; AppRange_Process();
  appRangeStatus.locked=1; peerEpoch=7; syncEpoch=7;
  detectedPeaks=OnePeak(0,950); remote=OnePeak(0,950);
  stamp=clockNs-200000000ULL;
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    DetectionReady(stamp,950);
    Inject(EVENT,10,7,stamp+378546,950,0); PairEvents();
    assert(!displays); /* Mixed old firmware cannot bypass joint matching. */
    InjectPeaks(11,stamp+378546,&remote); PairEvents();
    assert(displays==1 && appRangeStatus.distanceMm==130 && batch.count==1);
    InjectPeaks(11,stamp+378546,&remote); PairEvents();
    assert(displays==1 && appRangeStatus.eventRx==1); /* Retransmission is not a new event. */
    clockNs+=500000000; stamp+=500000000;
    remote.count=2; remote.peak[1]=remote.peak[0];
    for(i=0;i<3;++i) remote.peak[1].offsetNs[i]=145000;
    remote.peak[1].quality=930;
    DetectionReady(stamp,950); InjectPeaks(12,stamp+378546,&remote); PairEvents();
    assert(displays==1 && batch.count==1 && appRangeStatus.peakAmbiguous==1 && appRangeStatus.peakUncertain);
    /* Malformed fields, old epoch and wrong profile never create events. */
    i=appRangeStatus.eventRx;
    Inject(PEAK_EVENT,13,6,stamp,65537,RangePeak_Pack(&remote.peak[0]));
    Inject(PEAK_EVENT,13,7,stamp,65540,RangePeak_Pack(&remote.peak[0]));
    Inject(PEAK_EVENT,13,7,stamp,131073,RangePeak_Pack(&remote.peak[0]));
    Inject(PEAK_EVENT,13,7,stamp,65537,0);
    assert(appRangeStatus.eventRx==i);
    clockNs+=500000000; stamp+=500000000;
    remote=OnePeak(-145594,900);
    DetectionReady(stamp,950); InjectPeaks(14,stamp+524140,&remote); PairEvents();
    assert(displays==2 && appRangeStatus.distanceMm==130 && !appRangeStatus.peakUncertain && batch.count==2);
  } else {
    /* Master conversion includes clock slope across the full pulse spacing. */
    syncModel.slope=0.00004; syncModel.origin=(double)stamp; syncModel.offset=1000000;
    for(i=0;i<3;++i) detectedPeaks.peak[0].offsetNs[i]=(int32_t)i*1600;
    DetectionReady(stamp,950);
    assert(pendingPeaks.count==1 && pendingPeaks.peak[0].offsetNs[2]>=-2 && pendingPeaks.peak[0].offsetNs[2]<=2);
    lastSyncMs=HAL_GetTick(); lastSyncNs=clockNs;
    AppRange_Process(); assert(sent[5]==PEAK_EVENT && G32(sent+24)==pendingEventId);
    {
      uint8_t original[72]; uint32_t id=pendingEventId;
      memcpy(original,sent,72);
      clockNs+=200000000; lastSyncMs=HAL_GetTick(); AppRange_Process();
      assert(!memcmp(original,sent,72));
      Inject(ACK,id,syncEpoch,EVENT,0,0); assert(pendingEventId==id);
      Inject(ACK,id,syncEpoch,PEAK_EVENT,0,0); assert(!pendingEventId);
    }
    Inject(RESULT,100,7,130,1,900);
    Inject(PEAK_STATE,102,7,1,1,0);
    assert(appRangeStatus.peakUncertain && appRangeStatus.distanceMm==130);
    Inject(RESULT,101,7,131,1,900); /* Older result must not erase newer uncertainty. */
    assert(appRangeStatus.peakUncertain);
    Inject(PEAK_STATE,99,7,0,0,0); assert(appRangeStatus.peakUncertain);
    Inject(RESULT,103,7,130,1,900); assert(!appRangeStatus.peakUncertain);
  }
  /* Continuous old audio at the enlarged window, no network pairing mocks. */
  {
    int16_t pcm[512]; uint32_t before=appRangeStatus.events;
    ClearMeasurements(); appRangeStatus.locked=1;
    for(i=0;i<24576;i+=256) {
      for(k=0;k<256;++k) {
        int at=(int)((i+k)%8000)-129; int16_t v=0;
        if(at>=0 && at<512) v=rangeUp[at];
        else if(at>=640 && at<1152) v=rangeDown[at-640];
        else if(at>=1280 && at<1792) v=rangeUp[at-1280];
        pcm[2*k]=v; pcm[2*k+1]=0;
      }
      clockNs=20000000000ULL+(uint64_t)(i+256)*62500;
      AppRange_Audio(pcm,256); for(k=0;k<4;++k) AudioProcess();
    }
    assert(appRangeStatus.events-before==3 && appRangeStatus.audioDrops==0 && appRangeStatus.peakCandidates>0);
    AppRange_AudioError(); AudioProcess();
    assert(!appRangeStatus.valid && !pendingEventId && !appRangeStatus.peakUncertain);
  }
  printf("PASS: joint role %s transport, ambiguity, timestamps, stream, reset\n",APP_BOARD_NAME);
  return 0;
}
