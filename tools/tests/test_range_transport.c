#define main original_test_main
#include "test_app_range.c"
#undef main

static uint64_t SendProbe(void)
{
  uint64_t stamp=Send(APP_RANGE_MSG_SYNC_RESP,0x12345678,7,
                      0x0102030405060708ULL,2,3,4,5);
  assert(balance==0);return stamp;
}
static void RawReceive(const uint8_t *bytes,unsigned length,unsigned port,int wrongPeer)
{
  struct pbuf *p=pbuf_alloc(0,(uint16_t)length,0);
  ip_addr_t source=peer;
  if(wrongPeer) source.value^=1;
  memcpy(p->bytes,bytes,APP_RANGE_WIRE_SIZE);
  Receive(NULL,pcb,p,&source,(u16_t)port);assert(balance==0);
}
int main(void)
{
  AppRangePacket packet;uint8_t bytes[APP_RANGE_WIRE_SIZE];
  uint32_t rejected;unsigned sends,i;
  AppRange_Init();appNetStatus.online=1;AppRange_Process();
  testUdpSends=0;testArpRequests=0;
  rangeTxTimestamp=clockNs-1000000; /* stale timestamp from an unrelated send */
  appNetStatus.online=0;assert(!SendProbe() && !testUdpSends);appNetStatus.online=1;
  testArpMiss=1;assert(!SendProbe() && !testUdpSends && testArpRequests==1);testArpMiss=0;
  testAllocFail=1;assert(!SendProbe() && !testUdpSends);testAllocFail=0;
  testTakeFail=1;assert(!SendProbe() && !testUdpSends);testTakeFail=0;
  testNoTxStamp=1;assert(!SendProbe() && testUdpSends==1);testNoTxStamp=0;
  testSendFail=1;assert(!SendProbe() && testUdpSends==2);testSendFail=0;
  rangeTxStampSerial=UINT32_MAX;assert(SendProbe()==clockNs && rangeTxStampSerial==0);
  assert(AppRangeProtocol_Decode(&packet,sent,sizeof(sent),APP_BOARD_ROLE,111,222));
  assert(packet.type==APP_RANGE_MSG_SYNC_RESP && packet.id==0x12345678 && packet.epoch==7);
  assert(packet.revision==uiRevision && packet.payload[0]==0x0102030405060708ULL);
  for(i=1;i<5;++i) assert(packet.payload[i]==i+1);
  sends=testUdpSends;
  memset(&packet,0,sizeof(packet));packet.type=255;packet.role=APP_PEER_ROLE;
  packet.senderSession=222;packet.receiverSession=111;
  AppRangeProtocol_Encode(bytes,&packet);
  rejected=appRangeStatus.rejected;
  /* A valid but unknown type reaches dispatch and is rejected without replying. */
  RawReceive(bytes,sizeof(bytes),APP_RANGE_PORT,0);assert(appRangeStatus.rejected==++rejected);
  RawReceive(bytes,sizeof(bytes),APP_RANGE_PORT+1,0);assert(appRangeStatus.rejected==++rejected);
  RawReceive(bytes,sizeof(bytes),APP_RANGE_PORT,1);assert(appRangeStatus.rejected==++rejected);
  RawReceive(bytes,sizeof(bytes)-1,APP_RANGE_PORT,0);assert(appRangeStatus.rejected==++rejected);
  testCopyShort=1;RawReceive(bytes,sizeof(bytes),APP_RANGE_PORT,0);
  assert(appRangeStatus.rejected==++rejected);testCopyShort=0;
  for(i=0;i<24;++i) if(i!=5) {
    bytes[i]^=0x80;RawReceive(bytes,sizeof(bytes),APP_RANGE_PORT,0);bytes[i]^=0x80;
    assert(appRangeStatus.rejected==++rejected);
  }
  captureBusy=1;RawReceive(bytes,sizeof(bytes),APP_RANGE_PORT,0);captureBusy=0;
  assert(appRangeStatus.rejected==rejected && testUdpSends==sends && balance==0);
  puts("PASS: RAN2 coordinator transport; ARP/TX timestamp gating, failures, peer/session/header rejection and pbuf lifetime");
  return 0;
}
