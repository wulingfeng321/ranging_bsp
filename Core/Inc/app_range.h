#ifndef APP_RANGE_H
#define APP_RANGE_H
#include <stdint.h>
/* Hardware PTP timestamps and audio anchors are nanoseconds, local monotonic
 * epoch. No UTC and no timestamp is taken at the end of signal processing. */
typedef struct {
  uint32_t events, rejected, audioDrops, syncSamples, syncErrorNs;
  uint32_t distanceMm, quality, resultId, results;
  int32_t direction; /* +1 source on A side, -1 B side, 0 unresolved */
  uint8_t locked, valid;
  int32_t error; /* -1 clock, -2 UDP, -3 timestamp/sync, -4 audio gap */
  uint32_t eventRx, eventTxAttempts, eventAck, eventQuality;
  uint32_t audioGapDrops, audioOverruns, backlogSamples, maxBacklogSamples;
  uint32_t unlockedEvents, distanceRejects;
  int32_t pairDeltaUs; /* Nearest compared B-A event delta before bias. */
  uint8_t pairDeltaValid;
  int32_t resultDeltaUs; /* A only: raw B-A delta for the displayed result. */
  uint32_t audioJitterNs, samplePeriodPs, audioTimingRejected;
  uint8_t audioTimeReady;
  uint32_t eventPeakSpreadSamples;
} AppRangeStatus;
extern AppRangeStatus appRangeStatus;
void AppRange_Init(void);
void AppRange_Process(void);
/* Main-loop display admission: give the detector time to drain its backlog. */
int AppRange_DisplayReady(void);
/* ISR only: interleaved stereo input, frame count 256, selected channel L.
 * Data is copied before returning. AudioError invalidates pending events. */
void AppRange_Audio(const volatile int16_t *pcm, uint32_t frames);
void AppRange_AudioError(void);
uint64_t RangeClock_Now(void);
int RangeClock_Init(void);
extern uint64_t rangeRxTimestamp, rangeTxTimestamp;
extern uint32_t rangeTxStampSerial;
#endif
