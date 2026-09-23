#define APP_RANGE_STATISTICS 1
#define main legacy_test_main
#include "test_app_range.c"
#undef main
int main(void)
{
  unsigned i;
  AppRange_Init(); appNetStatus.online=1; AppRange_Process();
  appRangeStatus.locked=1;
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    peerEpoch=7;
    for(i=0;i<15;++i) {
      uint32_t ms=(i/5)*4000+(i%5)*500;
      uint64_t stamp;
      if(i==2) {
        clockNs=20750000000ULL; stamp=clockNs-200000000;
        DetectionReady(stamp,900); Inject(EVENT,500,peerEpoch,stamp+524140,800,0);
        PairEvents();
        if(batch.count!=2 || appRangeStatus.batchCadenceRejected!=1) return 1;
      }
      clockNs=20000000000ULL+(uint64_t)ms*1000000ULL;
      stamp=clockNs-200000000;
      DetectionReady(stamp,900);
      Inject(EVENT,100+i,peerEpoch,stamp+524140,800,0);
      PairEvents();
      if(!appRangeStatus.valid || appRangeStatus.resultIsStat || appRangeStatus.distanceMm!=180)
      { puts("FAIL: missing single-shot preview"); return 1; }
    }
    clockNs=31001000000ULL; lastStateMs=HAL_GetTick();
    AppRange_Process();
    if(displays!=17 || !appRangeStatus.resultIsStat || appRangeStatus.distanceMm!=180 || batch.stage!=2 || batch.used!=15)
    { puts("FAIL: batch not published correctly"); return 1; }
    {
      uint32_t savedId=appRangeStatus.resultId, savedTick=lastResultMs;
      /* The next observation starts a new batch without replacing the green
       * estimate, its expiry, or the result awaiting ACK/retransmission. */
      clockNs+=500000000ULL;
      DetectionReady(clockNs-200000000ULL,900);
      Inject(EVENT,600,peerEpoch,clockNs-200000000ULL+291205,800,0);
      PairEvents();
      if(batch.count!=1 || batch.stage!=1 || displays!=17 ||
         !appRangeStatus.resultIsStat || appRangeStatus.distanceMm!=180 ||
         appRangeStatus.resultId!=savedId || lastResultMs!=savedTick ||
         txResultKind!=1 || txResultMm!=180 || pendingResultId!=savedId) return 1;
      /* Failure of the following batch must not revoke a fresh estimate. */
      clockNs+=11000000000ULL; lastStateMs=HAL_GetTick(); AppRange_Process();
      if(batch.stage!=3 || !appRangeStatus.valid || !appRangeStatus.resultIsStat) return 1;
      /* After the original hold expires, previews are allowed again. */
      clockNs+=4000000000ULL; lastStateMs=HAL_GetTick(); AppRange_Process();
      if(appRangeStatus.valid) return 1;
      DetectionReady(clockNs-200000000ULL,900);
      Inject(EVENT,601,peerEpoch,clockNs-200000000ULL+291205,800,0);
      PairEvents();
      if(!appRangeStatus.valid || appRangeStatus.resultIsStat || appRangeStatus.distanceMm!=100) return 1;
    }
  } else {
    syncEpoch=7;
    Inject(BATCH_STATE,100,7,1,4,0);
    if(appRangeStatus.batchCount!=4 || appRangeStatus.valid) return 1;
    Inject(RESULT,110,7,180,1,800);
    Inject(BATCH_STATE,105,7,1,5,0); /* Reordered old collecting state. */
    if(!appRangeStatus.valid || displays!=1) return 1;
    Inject(BATCH_STATE,120,7,1,1,0);
    Inject(RESULT,115,7,190,1,800); /* Old batch after a new collection. */
    if(!appRangeStatus.valid || displays!=1) return 1;
    Inject(BATCH_STATE,125,7,4,5,0);
    if(!appRangeStatus.valid || appRangeStatus.resultIsStat) return 1;
    testPayloadV=140; Inject(BATCH_STATE,142,7,1,6,0); testPayloadV=0;
    Inject(RESULT,140,7,175,1,800); /* STATE arrived ahead of its referenced preview. */
    if(!appRangeStatus.valid || appRangeStatus.distanceMm!=175) return 1;
    testPayloadV=150; Inject(BATCH_STATE,152,7,2,15,12); testPayloadV=0;
    testPayloadU=1; Inject(RESULT,150,7,180,1,800); testPayloadU=0;
    if(!appRangeStatus.resultIsStat || appRangeStatus.distanceMm!=180) return 1;
    Inject(RESULT,150,7,180,1,800); /* Duplicate cannot relabel an existing result. */
    if(!appRangeStatus.resultIsStat) return 1;
  }
  puts("PASS: batch state machine"); return 0;
}
