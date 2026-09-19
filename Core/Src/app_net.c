#include "app_net.h"
#include "app_board_config.h"
#include "main.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <string.h>

/* Wire: 32 bytes, big endian. Magic(4), version(1), type(1), role(1),
 * size(1), sender session(8), sequence(4), target session(8), value(4).
 * ACKs echo the request sequence; TEST_ACK also echoes value.
 * HELLO alone may have a zero target session. No raw C structs on wire. */
#define MSG_SIZE 32U
#define HELLO 1U
#define HELLO_ACK 2U
#define HEARTBEAT 3U
#define TEST 4U
#define TEST_ACK 5U
#define PERIOD_MS 1000U
#define TIMEOUT_MS 3000U

extern struct netif gnetif;
AppNetStatus appNetStatus;
static struct udp_pcb *socket;
static ip_addr_t peer;
static uint64_t session, peerSession;
static uint32_t seq, helloSeq, testSeq, testValue, testSent;
static uint32_t lastRx, lastSend, peerSeq;
static uint8_t helloPending, testPending, havePeerSeq;
uint64_t AppNet_LocalSession(void) { return session; }
uint64_t AppNet_PeerSession(void) { return peerSession; }

static void Put32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t Get32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}
static void Put64(uint8_t *p, uint64_t v)
{
  Put32(p, (uint32_t)(v >> 32)); Put32(p + 4, (uint32_t)v);
}
static uint64_t Get64(const uint8_t *p)
{
  return ((uint64_t)Get32(p) << 32) | Get32(p + 4);
}

/* Existing clock setup supplies PLLSAIP = 48 MHz. Hardware entropy keeps
 * boot sessions distinct even after power loss; never substitute a fixed ID. */
static int NewSession(void)
{
  uint32_t words[2], i, start = HAL_GetTick();
  __HAL_RCC_RNG_CLK_ENABLE();
  __HAL_RCC_RNG_FORCE_RESET();
  __HAL_RCC_RNG_RELEASE_RESET();
  RNG->CR = RNG_CR_RNGEN;
  for (i = 0; i < 2; ++i)
  {
    while ((RNG->SR & RNG_SR_DRDY) == 0U)
    {
      if ((RNG->SR & (RNG_SR_CECS | RNG_SR_SECS)) != 0U ||
          HAL_GetTick() - start >= 20U) goto fail;
    }
    if ((RNG->SR & (RNG_SR_CECS | RNG_SR_SECS)) != 0U) goto fail;
    words[i] = RNG->DR;
  }
  RNG->CR = 0;
  __HAL_RCC_RNG_CLK_DISABLE();
  session = ((uint64_t)words[0] << 32) | words[1];
  return session != 0;
fail:
  RNG->CR = 0;
  __HAL_RCC_RNG_CLK_DISABLE();
  return 0;
}

static int Send(uint8_t type, uint32_t number, uint64_t target, uint32_t value)
{
  uint8_t b[MSG_SIZE] = {'R', 'N', 'G', '1', 1, 0, APP_BOARD_ROLE, MSG_SIZE};
  struct pbuf *p;
  err_t err;
  b[5] = type;
  Put64(b + 8, session); Put32(b + 16, number);
  Put64(b + 20, target); Put32(b + 28, value);
  p = pbuf_alloc(PBUF_TRANSPORT, MSG_SIZE, PBUF_RAM);
  if (p == NULL) { ++appNetStatus.txErrors; return 0; }
  err = pbuf_take(p, b, MSG_SIZE);
  if (err == ERR_OK) err = udp_sendto(socket, p, &peer, APP_UDP_PORT);
  pbuf_free(p);
  if (err != ERR_OK) { ++appNetStatus.txErrors; return 0; }
  ++appNetStatus.tx;
  return 1;
}

static void Offline(void)
{
  appNetStatus.online = 0;
  peerSession = 0; havePeerSeq = 0;
  helloPending = 0; testPending = 0;
  lastSend = HAL_GetTick() - PERIOD_MS;
}

static void Receive(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                    const ip_addr_t *addr, u16_t port)
{
  uint8_t b[MSG_SIZE], type;
  uint64_t remote, target;
  uint32_t number, value, now = HAL_GetTick();
  (void)arg; (void)pcb;
  if (p == NULL) return;
  if (port != APP_UDP_PORT || !ip_addr_cmp(addr, &peer) ||
      p->tot_len != MSG_SIZE || pbuf_copy_partial(p, b, MSG_SIZE, 0) != MSG_SIZE)
  { pbuf_free(p); ++appNetStatus.rejected; return; }
  pbuf_free(p);
  type = b[5]; remote = Get64(b + 8); number = Get32(b + 16);
  target = Get64(b + 20); value = Get32(b + 28);
  if (memcmp(b, "RNG1", 4) || b[4] != 1 || b[6] != APP_PEER_ROLE ||
      b[7] != MSG_SIZE || remote == 0 || type < HELLO || type > TEST_ACK)
    goto reject;
  if (type == HELLO)
  {
    if (target != 0 || value != 0) goto reject;
    if (peerSession != remote)
    {
      /* Preserve our outstanding HELLO: simultaneous startup is normal. */
      appNetStatus.online = 0; testPending = 0; havePeerSeq = 0;
      peerSession = remote;
      lastSend = now - PERIOD_MS;
    }
    Send(HELLO_ACK, number, remote, 0);
  }
  else if (type == HELLO_ACK)
  {
    if (target != session || !helloPending || number != helloSeq || value != 0)
      goto reject;
    if (peerSession != remote) { havePeerSeq = 0; testPending = 0; }
    peerSession = remote;
    helloPending = 0;
    appNetStatus.online = 1;
    lastRx = now;
  }
  else
  {
    if (!appNetStatus.online || remote != peerSession || target != session)
      goto reject;
    if (type == TEST_ACK)
    {
      if (!testPending || number != testSeq || value != testValue) goto reject;
      testPending = 0; ++appNetStatus.testAck;
      appNetStatus.lastRttMs = now - testSent;
    }
    else
    {
      if (type == HEARTBEAT && value != 0) goto reject;
      /* ACK duplicate TEST requests again, but never count them as fresh data. */
      if (havePeerSeq && (number - peerSeq == 0U || number - peerSeq >= 0x80000000UL))
      {
        if (type == TEST) Send(TEST_ACK, number, remote, value);
        goto reject;
      }
      peerSeq = number; havePeerSeq = 1;
      if (type == TEST) Send(TEST_ACK, number, remote, value);
    }
    lastRx = now;
  }
  ++appNetStatus.rx;
  return;
reject:
  ++appNetStatus.rejected;
}

void AppNet_Init(void)
{
  if (socket != NULL) return;
  memset(&appNetStatus, 0, sizeof(appNetStatus));
  if (!NewSession()) { appNetStatus.initError = -1; return; }
  IP_ADDR4(&peer, 192, 168, 10, APP_PEER_HOST);
  socket = udp_new();
  if (socket == NULL) { appNetStatus.initError = -2; return; }
  if (udp_bind(socket, IP_ADDR_ANY, APP_UDP_PORT) != ERR_OK)
  {
    udp_remove(socket); socket = NULL;
    appNetStatus.initError = -3; return;
  }
  udp_recv(socket, Receive, NULL);
  Offline();
}

void AppNet_Process(void)
{
  uint32_t now = HAL_GetTick();
  if (socket == NULL) return;
  if (!netif_is_link_up(&gnetif)) { Offline(); return; }
  if (appNetStatus.online && now - lastRx >= TIMEOUT_MS) Offline();
  if (testPending && now - testSent >= PERIOD_MS)
  { testPending = 0; ++appNetStatus.testTimeout; }
  if (now - lastSend < PERIOD_MS) return;
  lastSend = now;
  if (!appNetStatus.online)
  {
    helloSeq = ++seq;
    helloPending = (uint8_t)Send(HELLO, helloSeq, 0, 0);
  }
  else
  {
    Send(HEARTBEAT, ++seq, peerSession, 0);
    testSeq = ++seq; testValue = testSeq ^ 0x12345678UL;
    testSent = now;
    testPending = (uint8_t)Send(TEST, testSeq, peerSession, testValue);
  }
}
