#ifndef APP_NET_H
#define APP_NET_H
#include <stdint.h>

/* All functions and fields belong to the main loop (not an ISR).
 * Counters wrap at 32 bits; tick/RTT units are milliseconds, not synced time. */
typedef struct {
  uint32_t tx, rx, rejected, txErrors, testAck, testTimeout, lastRttMs;
  uint8_t online;
  int32_t initError; /* 0 OK, -1 RNG, -2 allocation, -3 bind */
} AppNetStatus;
extern AppNetStatus appNetStatus;
void AppNet_Init(void); /* Once, after MX_LWIP_Init. */
void AppNet_Process(void);
#endif
