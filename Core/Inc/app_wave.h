#ifndef APP_WAVE_H
#define APP_WAVE_H
#include "app_audio_view.h"

/* Main-loop-only module. No UI, UDP, HAL, capture or app_range dependency.
 * Borrow the L-channel ring for this call only. Snapshot must return coherent
 * count/epoch after an acquire barrier; the producer may run during copying. */
typedef struct {
  uint64_t periodPs,originNs;
  uint32_t calibrationMs;
} AppWaveClock;

void AppWave_Reset(void);
const AppWaveClock *AppWave_GetClock(void); /* valid until next module update */
/* Caller checks A role, WAVE page, lock and sample-time readiness. Returns 1
 * only when calibration first locks; nowNs is read on completion to establish the shared origin. */
int AppWave_Calibrate(const AppAudioView *audio,uint64_t blockEndNs,
                      double samplePeriodNs,uint64_t (*nowNs)(void));
/* Caller checks peer/session/revision/epoch. Invalid payload returns 0;
 * valid duplicate/old snapshots return 1 without changing the clock. */
int AppWave_AcceptClock(uint32_t id,uint64_t periodPs,uint64_t originNs,uint32_t calibrationMs);
/* 10 ms frame. Caller checks page/readiness/lock and maps the audio time.
 * synchronized=0 uses free-running time without a shared period/origin. */
int AppWave_Read(const AppAudioView *audio,const AppAudioTime *time,
                 uint64_t presentNs,int synchronized,int16_t *out,unsigned points);
#endif
