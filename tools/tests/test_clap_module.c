#include <assert.h>
#include <stdio.h>
#include "app_board_config.h"
#include "app_clap.h"

static uint32_t sequence;
static uint32_t NextId(void) { return ++sequence; }
static int Tick(uint32_t now,AppClapTx *tx)
{ return AppClap_Tick(now,1,250,0,NextId,tx); }

static void CheckSnapshots(void)
{
  AppClapTx tx;
  uint32_t start=0xfffffff0U;
  uint64_t history=(3ULL<<54)|103ULL|(102ULL<<9)|(101ULL<<18);
  AppClap_Reset(0,1);
  assert(!AppClap_ReceiveResult(1,103,1,800,7ULL<<54,start));
  assert(!AppClap_ReceiveResult(1,400,1,800,(1ULL<<54)|400,start));
  assert(!AppClap_ReceiveResult(1,103,1,800,history|(1ULL<<27),start));
  assert(!AppClap_ReceiveResult(1,104,1,800,history,start));
  assert(!AppClap_ReceiveResult(1,103,3,800,history,start));
  assert(!AppClap_ReceiveResult(1,103,1,1001,history,start));
  assert(appClapStatus.received==0);
  assert(AppClap_ReceiveResult(0xfffffffeU,103,2,800,history,start));
  assert(appClapStatus.recentCount==3 && appClapStatus.recentCm[2]==101);
  assert(appClapStatus.direction==-1);
  assert(AppClap_ReceiveResult(0xfffffffeU,103,2,800,history,start+100));
  assert(appClapStatus.received==1 && appClapStatus.updatedMs==start);
  assert(!Tick(start+10000U,&tx) && appClapStatus.valid);
  assert(!Tick(start+10001U,&tx) && !appClapStatus.valid);
  assert(AppClap_ReceiveResult(1,103,1,800,history,start+10002U));
  assert(appClapStatus.received==2 && appClapStatus.valid);
  assert(AppClap_ReceiveResult(0xffffffffU,103,1,800,history,start+10003U));
  assert(appClapStatus.received==2); /* old packet after sequence wrap */
  AppClap_Restart(100,2);
  assert(appClapStatus.drops==1 && !appClapStatus.valid && !appClapStatus.recentCount);
}

static void CheckPairingAndRetry(void)
{
  AppClapTx tx;
  unsigned i,j;
  uint32_t start=0xffffff00U;
  uint64_t local=10000000000ULL;
  AppClap_Reset(0,1);
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    for(i=1;i<=8;++i) {
      uint32_t now=start+i*1200U;
      uint64_t delta=i*1000000ULL;
      AppClap_PublishLocal(local,850,NextId(),now);
      assert(AppClap_ReceiveEvent(i,local+delta,700,local+20000000ULL,now));
      assert(Tick(now,&tx) && tx.kind==APP_CLAP_TX_RESULT);
      assert(tx.distanceCm==(uint32_t)(i*34.645f+0.5f));
      assert(tx.side==1 && tx.quality==700);
      assert(appClapStatus.recentCount==(i<6 ? i:6));
      for(j=0;j<appClapStatus.recentCount;++j) {
        assert(appClapStatus.recentCm[j]==(uint32_t)((i-j)*34.645f+0.5f));
        assert(((tx.history>>(9*j))&511U)==appClapStatus.recentCm[j]);
      }
      assert(tx.history>>54==appClapStatus.recentCount);
      assert(!Tick(now+99U,&tx));
      assert(Tick(now+100U,&tx));
      assert(!Tick(now+1000U,&tx));
    }
    /* Rejected out-of-range pair must not enter the rolling history. */
    AppClap_PublishLocal(local,850,NextId(),10000);
    assert(AppClap_ReceiveEvent(9,local+9000000,700,local+20000000,10000));
    assert(!Tick(10000,&tx));
    assert(appClapStatus.rejected==1 && appClapStatus.recentCm[0]==277);
    /* Expired remote event is not revived by a retransmitted duplicate. */
    AppClap_Reset(0,1);
    assert(AppClap_ReceiveEvent(10,local,700,local+1000000,start));
    assert(AppClap_ReceiveEvent(10,local,700,local+1000000,start+1001U));
    AppClap_PublishLocal(local,850,NextId(),start+1001U);
    assert(!Tick(start+1001U,&tx) && !appClapStatus.valid);
    assert(!AppClap_ReceiveEvent(11,local+1,700,local,start));
    assert(!AppClap_ReceiveEvent(11,local-1000000001ULL,700,local,start));
  } else {
    AppClap_PublishLocal(local,850,42,start);
    assert(Tick(start,&tx) && tx.kind==APP_CLAP_TX_EVENT);
    assert(tx.id==42 && tx.arrivalNs==local && tx.quality==850);
    assert(!Tick(start+99U,&tx));
    assert(Tick(start+100U,&tx));
    assert(Tick(start+1000U,&tx));
    assert(!Tick(start+1100U,&tx));
    AppClap_Reset(0,1);
    assert(!Tick(start+1200U,&tx));
  }
  assert(!AppClap_Tick(20000,0,250,0,NextId,&tx));
  assert(!appClapStatus.valid && !appClapStatus.ready);
}

int main(void)
{
  CheckSnapshots();CheckPairingAndRetry();
  puts("PASS: independent clap module; rolling history, payload validation, duplicate/sequence wrap, retry and expiry across timer wrap");
  return 0;
}
