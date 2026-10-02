#ifndef APP_POSITION_H
#define APP_POSITION_H
#include "app_audio_view.h"
/* Main-loop-only detection/pairing/calibration. The coordinator owns ISR
 * rings, full-rate timing, clock mapping, session/page checks and transport. */
#define APP_POSITION_RING_SAMPLES 4096U
typedef struct {
  int32_t angleDeg; /* 0 top/front, 90 right, clockwise */
  uint32_t quality, residualNs, updatedMs, events, rejected;
  uint32_t inputLevel, peakQuality[2], overruns, received, gapResets, backlogResets; /* Local diagnostics since page entry. */
  uint32_t lagMaxSamples, dspMaxUs, lcdMaxUs; /* Position-only high water marks; lag at 24 kHz. */
  uint8_t valid, calibration, calibrationCount; /* 0 uncal, 1 collecting, 2 ready, 3 failed */
} AppPositionStatus;
extern AppPositionStatus appPositionStatus;

/* Two borrowed 24 kHz rings, each APP_POSITION_RING_SAMPLES long. Count and
 * snapshot use decimated samples; AppAudioTime retains full-rate 48 kHz units. */
typedef struct {
  const volatile int16_t *channel[2];
  uint64_t count;
  uint32_t epoch;
  AppAudioSnapshot snapshot;
} AppPositionAudio;
typedef struct { double localNs[2];uint32_t quality; } AppPositionOnset;
typedef enum { APP_POSITION_TX_EVENT,APP_POSITION_TX_RESULT } AppPositionTxKind;
typedef struct {
  AppPositionTxKind kind;
  uint32_t id,quality,angleDeg,valid,residualNs,calibration;
  uint64_t arrivalNs[2];
} AppPositionTx;
void AppPosition_Reset(uint64_t decimatedCount,uint32_t epoch);
/* Actual timebase change discards old channel bias and restarts calibration
 * samples, retaining calibration intent; backlog recovery preserves both. */
void AppPosition_RestartTimebase(uint64_t decimatedCount,uint32_t epoch);
void AppPosition_BeginCalibration(void); /* after reset through linked UI */
/* Returns 1 for a new stereo onset. Caller maps both local doubles to master
 * time before rounding and publishing. Fixed 64-start / 1.5 ms slice budget. */
int AppPosition_Process(const AppPositionAudio *audio,const AppAudioTime *time,
                         uint64_t (*nowNs)(void),AppPositionOnset *onset);
void AppPosition_PublishLocal(const uint64_t masterNs[2],uint32_t quality,uint32_t id,uint32_t nowMs);
/* Return 0 for invalid payload. Valid duplicates do not extend lifetime. */
int AppPosition_ReceiveEvent(uint32_t id,uint64_t leftNs,uint64_t rightNs,uint64_t quality,uint64_t currentNs);
int AppPosition_ReceiveResult(uint32_t id,uint64_t angle,uint64_t quality,uint64_t valid,
                               uint64_t residualNs,uint64_t calibration,uint32_t nowMs);
int AppPosition_Tick(uint32_t nowMs,int active,int32_t temperatureDeciC,
                      uint32_t (*nextId)(void),AppPositionTx *message);
#endif
