/* Runs the actual firmware state machine with mocked HAL/LwIP transport.
 * gcc -std=c99 -Wall -Wextra -Werror -Itools/tests/net_stubs -ICore/Inc
 *     tools/tests/test_app_net.c -o <temporary executable>
 * Repeat with -DAPP_BOARD_ROLE=1 and =2. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../Core/Src/app_net.c"

TestRng testRng = {0, RNG_SR_DRDY, 0x98765432};
struct netif gnetif = {1};
static uint32_t clockMs;
static struct udp_pcb testSocket;
static uint8_t sent[32];
static int allocated, sendCount, failSend;
uint32_t HAL_GetTick(void) { return clockMs; }
struct pbuf *pbuf_alloc(int layer, uint16_t size, int type)
{
  struct pbuf *p = malloc(sizeof(*p));
  (void)layer; (void)type;
  assert(p); p->tot_len = size; ++allocated; return p;
}
void pbuf_free(struct pbuf *p) { free(p); --allocated; }
err_t pbuf_take(struct pbuf *p, const void *data, uint16_t size)
{ memcpy(p->bytes, data, size); return 0; }
uint16_t pbuf_copy_partial(struct pbuf *p, void *data, uint16_t size, uint16_t off)
{ memcpy(data, p->bytes + off, size); return size; }
struct udp_pcb *udp_new(void) { return &testSocket; }
err_t udp_bind(struct udp_pcb *p, const ip_addr_t *addr, uint16_t port)
{ (void)p; (void)addr; assert(port == 5000); return 0; }
void udp_remove(struct udp_pcb *p) { (void)p; }
void udp_recv(struct udp_pcb *p, void (*cb)(void *, struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t), void *arg)
{ (void)p; (void)cb; (void)arg; }
err_t udp_sendto(struct udp_pcb *p, struct pbuf *b, const ip_addr_t *addr, uint16_t port)
{
  (void)p; assert(addr->value == (0xc0a80a00U | APP_PEER_HOST));
  assert(port == 5000 && b->tot_len == 32);
  memcpy(sent, b->bytes, 32); ++sendCount; return failSend ? -1 : 0;
}
static void Inject(uint8_t type, uint64_t remote, uint32_t number, uint64_t target,
                   uint32_t value, uint8_t version, uint16_t port, uint16_t size)
{
  struct pbuf *p = pbuf_alloc(0, size, 0);
  memset(p->bytes, 0, sizeof(p->bytes));
  memcpy(p->bytes, "RNG1", 4);
  p->bytes[4] = version; p->bytes[5] = type;
  p->bytes[6] = APP_PEER_ROLE; p->bytes[7] = 32;
  Put64(p->bytes + 8, remote); Put32(p->bytes + 16, number);
  Put64(p->bytes + 20, target); Put32(p->bytes + 28, value);
  Receive(NULL, socket, p, &peer, port);
  assert(allocated == 0);
}
#define RX(t,r,n,s,v) Inject(t,r,n,s,v,1,5000,32)
int main(void)
{
  uint64_t remote = 0x0102030405060708ULL;
  uint32_t before, oldTest;
  AppNet_Init(); assert(appNetStatus.initError == 0);
  AppNet_Process(); assert(sent[5] == HELLO && helloPending);
  /* Simultaneous HELLO must not destroy our pending handshake. */
  RX(HELLO, remote, 17, 0, 0);
  assert(helloPending && sent[5] == HELLO_ACK && !appNetStatus.online);
  RX(HELLO_ACK, remote, helloSeq, session, 0);
  assert(appNetStatus.online);
  AppNet_Process(); assert(sent[5] == TEST && testPending);
  oldTest = testSeq;
  RX(TEST_ACK, remote, testSeq, session, testValue ^ 1);
  assert(testPending && appNetStatus.testAck == 0);
  RX(TEST_ACK, remote, testSeq, session, testValue);
  RX(TEST_ACK, remote, oldTest, session, testValue);
  assert(appNetStatus.testAck == 1 && !testPending);
  RX(TEST, remote, 18, session, 1234);
  assert(sent[5] == TEST_ACK && Get32(sent + 28) == 1234);
  before = appNetStatus.rx;
  RX(TEST, remote, 18, session, 1234); assert(appNetStatus.rx == before);
  Inject(HEARTBEAT, remote, 19, session, 0, 2, 5000, 32);
  Inject(HEARTBEAT, remote, 19, session, 0, 1, 5001, 32);
  Inject(HEARTBEAT, remote, 19, session, 0, 1, 5000, 31);
  RX(HEARTBEAT, remote, 19, session ^ 1, 0);
  assert(appNetStatus.rx == before);
  clockMs = 3001; AppNet_Process(); assert(!appNetStatus.online && sent[5] == HELLO);
  /* Peer reboot, new session, then stale packets from old session. */
  RX(HELLO_ACK, remote + 1, helloSeq, session, 0);
  assert(appNetStatus.online);
  RX(HEARTBEAT, remote, 20, session, 0);
  assert(peerSession == remote + 1);
  gnetif.link = 0; AppNet_Process(); assert(!appNetStatus.online);
  gnetif.link = 1; AppNet_Process(); assert(sent[5] == HELLO);
  RX(HELLO_ACK, remote + 1, helloSeq, session, 0);
  /* Millisecond wrap and packet sequence wrap. */
  clockMs = 0xfffffff0U; lastRx = clockMs; lastSend = clockMs;
  peerSeq = 0xfffffffeU; havePeerSeq = 1;
  RX(HEARTBEAT, remote + 1, 1, session, 0);
  clockMs = 20; AppNet_Process(); assert(appNetStatus.online && peerSeq == 1);
  before = appNetStatus.txErrors; failSend = 1;
  clockMs += 1000; AppNet_Process();
  assert(appNetStatus.txErrors > before && allocated == 0);
  /* ACK messages never cause an ACK loop. */
  before = (uint32_t)sendCount;
  RX(TEST_ACK, remote + 1, testSeq, session, testValue);
  assert((uint32_t)sendCount == before);
  printf("Role %s: handshake, echo, validation, replay, timeout, reconnect, wrap and send failure PASS\n", APP_BOARD_NAME);
  return 0;
}
