#ifndef APP_RANGE_H
#define APP_RANGE_H
#include <stdint.h>
#include "range_pair_diag.h"
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
  uint32_t dspMaxUs, dspLoadPermille; /* Main-loop DSP wall time, includes IRQ preemption. */
  uint8_t audioTimeReady;
  uint32_t eventPeakSpreadSamples;
  uint32_t batchStage, batchCount, batchUsed, batchSpanMm;
  uint32_t batchCadenceRejected;
  uint8_t resultIsStat; /* 0 single-shot preview, 1 accepted batch estimate. */
  uint32_t peakAmbiguous, peakInconsistent, peakCandidates, peakPairSpreadNs;
  uint8_t peakUncertain; /* Last compared event rejected; old display is not refreshed. */
  RangePairDiag pairFailure; /* Latched until reset/session change or next failure. */
} AppRangeStatus;
extern AppRangeStatus appRangeStatus;
typedef enum { APP_PAGE_STANDARD, APP_PAGE_CLAP, APP_PAGE_WAVE, APP_PAGE_POSITION } AppPage;
/* Main-loop owned UI state. A is authoritative; B requests changes reliably.
 * Temperature is in 0.1 C, volatile across reboot, restricted to -10..50 C.
 * Pulse times are the strongest LOCAL candidate's three pulse starts, NOT
 * the selected cross-board pair. All buffers are owned by app_range. */
typedef struct {
  uint64_t localUs[3], syncUs[3];
  uint32_t eventId;
  uint8_t valid;
} AppRangeArrival;
extern AppRangeArrival appRangeArrival;
AppPage AppRange_Page(void);
int32_t AppRange_Temperature(void);
int AppRange_SettingsReady(void);
int AppRange_RequestPage(AppPage page);
int AppRange_AdjustTemperature(int32_t stepDeciC);
/* Common counter starts at an A-clock second boundary after lock; zero while
 * unlocked or awaiting the common origin. Local clock never resets. */
int AppRange_SyncElapsed(uint64_t *elapsedUs, uint64_t *localUs);
void AppRange_Init(void);
void AppRange_Process(void);
/* Recorder transaction only: clears round counters and restarts synchronization. */
void AppRange_ResetRound(void);
/* Main-loop display admission: give the detector time to drain its backlog. */
int AppRange_DisplayReady(void);
/* Main loop only: nanoseconds in board A's epoch, available while synchronized. */
int AppRange_MasterTime(uint64_t *masterNs);
/* ISR only: interleaved stereo input, frame count APP_AUDIO_HALF_FRAMES, selected channel L.
 * Data is copied before returning. AudioError invalidates pending events. */
void AppRange_Audio(const volatile int16_t *pcm, uint32_t frames);
void AppRange_AudioError(void);
uint64_t RangeClock_Now(void);
int RangeClock_Init(void);
extern uint64_t rangeRxTimestamp, rangeTxTimestamp;
extern uint32_t rangeTxStampSerial;
#endif
