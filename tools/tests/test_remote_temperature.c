#define main original_test_main
#include "test_app_range.c"
#undef main
static int32_t sensorValue=263;
static int sensorGood=1;
static unsigned sensorReads;
static int ReadSensor(int32_t *value) { ++sensorReads;*value=sensorValue;return sensorGood; }
static void Ready(void) { uiKnown=1;uiAckRevision=uiRevision;uiPendingId=0; }
int main(void)
{
  uint32_t id,rev,count,serial;
  AppRange_Init();appNetStatus.online=1;AppRange_Process();Ready();
  uiPage=APP_PAGE_STANDARD;temperature=250;appRangeStatus.valid=1;
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    AppRange_SetTemperatureReader(ReadSensor);rev=uiRevision;
    Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,1,0,0,0,0);
    assert(sensorReads==1 && temperature==265 && uiRevision==rev+1 && autoStatus==1);
    serial=autoSerial;testRevision=rev;
    Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,1,0,0,0,0);
    assert(sensorReads==1 && autoSerial==serial && temperature==265);
    testRevision=0;Ready();appRangeStatus.valid=1;rev=uiRevision;
    Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,2,0,0,0,0);
    assert(sensorReads==2 && uiRevision==rev && appRangeStatus.valid && autoStatus==1);
    /* Old setting request cannot apply a newly changed sensor value. */
    sensorValue=300;testRevision=rev-1;
    Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,3,0,0,0,0);
    assert(sensorReads==2 && autoStatus==3 && temperature==265);testRevision=0;
    sensorGood=0;Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,4,0,0,0,0);
    assert(autoStatus==2 && temperature==265 && appRangeStatus.valid);
    sensorGood=1;sensorValue=501;Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,5,0,0,0,0);
    assert(autoStatus==4 && temperature==265);
    count=sensorReads;captureBusy=1;Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,6,0,0,0,0);
    assert(sensorReads==count && autoSeenRequest==5);captureBusy=0;
    sensorValue=280;Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,6,0,0,0,0);
    assert(sensorReads==count+1 && temperature==280);Ready();
    count=sensorReads;uiPage=APP_PAGE_CLAP;
    Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,7,0,0,0,0);assert(sensorReads==count && autoStatus==3);
    uiPage=APP_PAGE_STANDARD;
    Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,8,0,1,0,0);assert(sensorReads==count && autoSeenRequest==7);
    testPayloadV=1;Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,8,0,0,0,0);
    assert(autoSeenRequest==7);testPayloadV=0;
    /* Status and read operations share a dedupe sequence; a newer manual
     * feedback cancels an older read even if packets arrive in reverse order. */
    Inject(APP_RANGE_MSG_AUTO_STATUS_REQUEST,9,0,0,0,0);
    Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,8,0,0,0,0);assert(sensorReads==count && autoStatus==0);
    autoSeenRequest=UINT32_MAX;sensorValue=280;
    Inject(APP_RANGE_MSG_AUTO_READ_REQUEST,1,0,0,0,0);assert(autoSeenRequest==1 && sensorReads==count+1);
  } else {
    assert(AppRange_AutoTemperature()==1 && autoStatus==3 && autoPendingRead && appRangeStatus.valid);
    id=autoPendingId;rev=uiRevision;
    assert(AppRange_AutoTemperature()==1 && autoPendingId==id);
    UiProcess(HAL_GetTick());assert(sent[5]==APP_RANGE_MSG_AUTO_READ_REQUEST && AppWire_Get32BE(sent+24)==id);
    clockNs+=250000000;UiProcess(HAL_GetTick());assert(AppWire_Get32BE(sent+24)==id);
    Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,10,0,2,id+1,0);assert(autoPendingId==id && autoStatus==3);
    testRevision=rev+1;Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,11,0,1,id,0);
    assert(!autoPendingRead && !autoPendingId && autoStatus==1 && !AppRange_SettingsReady());
    Inject(APP_RANGE_MSG_UI_STATE,rev+1,0,365,APP_PAGE_STANDARD,0);
    assert(temperature==265 && AppRange_SettingsReady());testRevision=0;
    /* Same temperature OK does not discard an active result. */
    appRangeStatus.valid=1;assert(AppRange_AutoTemperature()==1);id=autoPendingId;
    Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,12,0,1,id,0);
    assert(appRangeStatus.valid && AppRange_SettingsReady() && !autoPendingRead);
    assert(AppRange_AutoTemperature()==1);id=autoPendingId;rev=uiRevision;
    /* A can change a setting before seeing the read. Retry still carries
     * the ORIGINAL revision so it cannot overwrite that newer choice. */
    testRevision=rev+1;Inject(APP_RANGE_MSG_UI_STATE,rev+1,0,370,APP_PAGE_STANDARD,0);testRevision=0;
    clockNs+=250000000;UiProcess(HAL_GetTick());
    assert(sent[5]==APP_RANGE_MSG_AUTO_READ_REQUEST && AppWire_Get32BE(sent+72)==rev);
    Inject(APP_RANGE_MSG_AUTO_STATUS_STATE,13,0,3,id,0);assert(!autoPendingRead && autoStatus==3);
    assert(AppRange_AutoTemperature()==1);id=autoPendingId;
    assert(AppRange_AdjustTemperature(5));assert(!autoPendingRead && autoPendingId!=id && uiPendingId);
    uiPendingId=0;autoPendingId=0;Ready();assert(AppRange_AutoTemperature()==1);
    appNetStatus.online=0;AppRange_Process();assert(!autoPendingRead && !autoPendingId);
    assert(AppRange_AutoTemperature()==-1);
  }
  puts("PASS: A sensor remote AUTO, retries/dedupe, revision ordering, faults, B completion, cancellation and disconnect");
  return 0;
}
