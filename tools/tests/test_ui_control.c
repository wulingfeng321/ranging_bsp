#define APP_RANGE_JOINT_PEAKS 1
#define main original_test_main
#include "test_app_range.c"
#undef main
static int32_t sensorValue;
static int SensorRead(int32_t *out) { *out=sensorValue;return 1; }
int main(void)
{
  uint64_t elapsed,local,origin; uint32_t rev,id,events;
  AppRange_Init(); appNetStatus.online=1; AppRange_Process();
  uiKnown=1; uiAckRevision=uiRevision;
  peerEpoch=syncEpoch=7; appRangeStatus.locked=1;
  syncOriginNs=9000000000ULL;
  assert(AppRange_SyncElapsed(&elapsed,&local) && elapsed==1000000 && local==10000000);
  appRangeStatus.locked=0;
  assert(!AppRange_SyncElapsed(&elapsed,&local) && !elapsed && local==10000000);
  appRangeStatus.locked=1;
  syncModel.offset=APP_BOARD_ROLE==APP_BOARD_B ? 1000000000.0 : 0;
  assert(AppRange_SyncElapsed(&elapsed,&local));
  assert(elapsed==(APP_BOARD_ROLE==APP_BOARD_B ? 0 : 1000000));
  detectedPeaks.count=1; detectedPeaks.peak[0].quality=950;
  detectedPeaks.peak[0].offsetNs[0]=-1000;
  detectedPeaks.peak[0].offsetNs[1]=2000;
  detectedPeaks.peak[0].offsetNs[2]=3000;
  DetectionReady(clockNs+1000000,950);
  assert(appRangeArrival.valid==7 && appRangeArrival.localUs[0]==10000999);
  assert(appRangeArrival.localUs[1]==10041002 && appRangeArrival.localUs[2]==10081003);
  assert(appRangeArrival.syncUs[0]==(APP_BOARD_ROLE==APP_BOARD_B ? 999 : 1000999));
  assert(AppRange_AutoTemperature()==(APP_BOARD_ROLE==APP_BOARD_A ? 0:1));
  autoPendingId=0;autoPendingRead=0;
  AppRange_SetTemperatureReader(SensorRead);sensorValue=501;
  assert(AppRange_AutoTemperature()==-2);
  sensorValue=253;assert(AppRange_AutoTemperature()==1);
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    assert(temperature==255);Inject(APP_RANGE_MSG_UI_ACK,uiRevision,0,0,0,0);
  } else {
    assert(uiPendingId && uiRequestKind==1 && uiRequestValue==355);
    uiPendingId=0;
  }
  uiKnown=1;uiAckRevision=uiRevision;uiPendingId=0;
  sensorValue=temperature;rev=uiRevision;appRangeStatus.valid=1;
  assert(AppRange_AutoTemperature()==1 && uiRevision==rev && !uiPendingId && appRangeStatus.valid);
  sensorValue=-101;assert(AppRange_AutoTemperature()==-2);
  captureBusy=1;assert(AppRange_AutoTemperature()==-1);captureBusy=0;
  AppRange_SetTemperatureReader(NULL);temperature=250;
  origin=syncOriginNs;
  assert(AppRange_RequestPage(APP_PAGE_WAVE));
  assert(!AppRange_SettingsReady());
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    assert(uiPage==APP_PAGE_WAVE && !appRangeStatus.valid && !appRangeArrival.valid);
    rev=uiRevision; testRevision=rev-1;
    Inject(APP_RANGE_MSG_UI_ACK,rev-1,0,0,0,0); assert(!AppRange_SettingsReady());
    testRevision=0; Inject(APP_RANGE_MSG_UI_ACK,rev,0,0,0,0); assert(AppRange_SettingsReady());
    Inject(APP_RANGE_MSG_UI_REQUEST,13,0,1,355,0); assert(temperature==255);
    rev=uiRevision; Inject(APP_RANGE_MSG_UI_REQUEST,13,0,1,355,0); assert(uiRevision==rev && temperature==255);
    Inject(APP_RANGE_MSG_UI_REQUEST,12,0,0,APP_PAGE_CLAP,0); assert(uiPage==APP_PAGE_WAVE);
    Inject(APP_RANGE_MSG_UI_REQUEST,14,0,1,601,0); assert(temperature==255);
    Inject(APP_RANGE_MSG_UI_ACK,rev,0,0,0,0);
  } else {
    id=uiPendingId; UiProcess(HAL_GetTick());
    assert(sent[5]==APP_RANGE_MSG_UI_REQUEST && AppWire_Get32BE(sent+24)==id);
    clockNs+=250000000; UiProcess(HAL_GetTick());
    assert(sent[5]==APP_RANGE_MSG_UI_REQUEST && AppWire_Get32BE(sent+24)==id);
    testRevision=2; testPayloadU=7; testPayloadV=id;
    Inject(APP_RANGE_MSG_UI_STATE,2,0,350,APP_PAGE_WAVE,origin);
    assert(uiPage==APP_PAGE_WAVE && AppRange_SettingsReady() && !appRangeArrival.valid);
    assert(sent[5]==APP_RANGE_MSG_UI_ACK);
    testRevision=1; Inject(APP_RANGE_MSG_UI_STATE,1,0,350,APP_PAGE_CLAP,origin); assert(uiPage==APP_PAGE_WAVE);
    testRevision=0;
    assert(AppRange_AdjustTemperature(5)); id=uiPendingId;
    assert(uiRequestValue==355);
    testRevision=3; testPayloadV=id;
    Inject(APP_RANGE_MSG_UI_STATE,3,0,355,APP_PAGE_WAVE,origin); assert(temperature==255);
    testPayloadU=testPayloadV=testRevision=0;
  }
  assert(syncOriginNs==origin && appRangeStatus.locked); /* No clock reset on tab/temp changes. */
  captureBusy=1;
  assert(!AppRange_AdjustTemperature(5) && !AppRange_RequestPage(APP_PAGE_STANDARD));
  captureBusy=0;
  assert(!AppRange_AdjustTemperature(1));
  temperature=500; assert(!AppRange_AdjustTemperature(5));
  temperature=-100; assert(!AppRange_AdjustTemperature(-5)); temperature=255;
  /* Pending/backlogged audio must not reach the detector on a placeholder page. */
  events=appRangeStatus.events; audioCount=100000; readSample=0;
  lastStateMs=lastSyncMs=HAL_GetTick(); lastSyncNs=clockNs;
  AppRange_Process(); assert(readSample==audioCount && !haveWindow);
  assert(events==appRangeStatus.events && AppRange_DisplayReady());
  /* Measurement wire revisions reject delayed packets after a config edit. */
  uiPage=APP_PAGE_STANDARD; uiKnown=1; uiAckRevision=uiRevision;
  testRevision=uiRevision+1;
  Inject(APP_RANGE_MSG_RESULT,700,syncEpoch,1234,1,950); assert(!appRangeStatus.valid);
  testRevision=0;
  if(APP_BOARD_ROLE==APP_BOARD_B) {
    Inject(APP_RANGE_MSG_RESULT,701,syncEpoch,1234,1,950); assert(appRangeStatus.valid);
  }
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    uint64_t stamp=clockNs-200000000ULL; unsigned pulse;
    for(pulse=0;pulse<3;++pulse) detectedPeaks.peak[0].offsetNs[pulse]=0;
    temperature=250; ClearMeasurements();
    DetectionReady(stamp,950);
    Inject(APP_RANGE_MSG_PEAK_EVENT,800,peerEpoch,stamp+5000000ULL,65537,RangePeak_Pack(&detectedPeaks.peak[0]));
    PairEvents(); assert(appRangeStatus.distanceMm==1732);
    temperature=255; ClearMeasurements();
    DetectionReady(stamp,950);
    Inject(APP_RANGE_MSG_PEAK_EVENT,801,peerEpoch,stamp+5000000ULL,65537,RangePeak_Pack(&detectedPeaks.peak[0]));
    PairEvents(); assert(appRangeStatus.distanceMm==1734);
  }
  captureBusy=1;
  assert(!AppRange_MasterTime(&elapsed) && !AppRange_SyncElapsed(&elapsed,&local) && !elapsed);
  captureBusy=0;
  Unlock(); assert(!syncOriginNs && !appRangeArrival.valid);
  assert(!AppRange_SyncElapsed(&elapsed,&local) && !elapsed);
  appNetStatus.online=0; assert(!AppRange_RequestPage(APP_PAGE_CLAP));
  appNetStatus.online=1;uiKnown=1;uiAckRevision=uiRevision;uiPendingId=0;
  autoPendingId=autoSeenState=autoSeenRequest=0;
  rev=uiRevision;events=appRangeStatus.events;
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    AutoFeedback(2);assert(AppRange_AutoTemperatureStatus()==2);
    Inject(APP_RANGE_MSG_AUTO_STATUS_REQUEST,100,0,3,0,0);assert(autoStatus==3);
    id=autoSerial;Inject(APP_RANGE_MSG_AUTO_STATUS_REQUEST,100,0,3,0,0);assert(autoSerial==id);
    AutoFeedback(4);Inject(APP_RANGE_MSG_AUTO_STATUS_REQUEST,99,0,2,0,0);assert(autoStatus==4);
    Inject(APP_RANGE_MSG_AUTO_STATUS_REQUEST,101,0,0,0,0);assert(!autoStatus);
  } else {
    AutoFeedback(2);id=autoPendingId;
    Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,100,0,1,0,0);assert(autoStatus==2 && autoPendingId==id);
    Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,101,0,2,id,0);assert(autoStatus==2 && !autoPendingId);
    Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,102,0,3,id,0);assert(autoStatus==3);
    Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,101,0,2,id,0);assert(autoStatus==3);
    Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,103,0,0,id,0);assert(!autoStatus);
  }
  assert(uiRevision==rev && appRangeStatus.events==events);
  puts("PASS: linked UI, retransmission, stale revisions, temperature bounds, paused DSP, pulse clocks, unlock reset");
  return 0;
}
