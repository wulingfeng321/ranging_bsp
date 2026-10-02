#ifndef APP_CLAP_H
#define APP_CLAP_H
#include "app_audio_view.h"
/* All calls and status updates occur in the main loop. No HAL, UDP or UI
 * dependencies. The coordinator validates page/session/revision/epoch and
 * maps local onset time into master time before publishing a local event. */
typedef struct {
  uint32_t events, received, rejected, drops, noise, quality, distanceCm, updatedMs;
  uint64_t arrivalUs; /* Latest local detection expressed in master clock us. */
  int32_t direction; /* +1 source on A side; -1 on B side; 0 ambiguous. */
  uint16_t recentCm[6]; /* Newest first; A-authoritative rolling window. */
  uint8_t recentCount;
  uint8_t ready, valid;
} AppClapStatus;
extern AppClapStatus appClapStatus;

typedef struct { double localNs;uint32_t quality; } AppClapOnset;
typedef enum { APP_CLAP_TX_EVENT,APP_CLAP_TX_RESULT } AppClapTxKind;
typedef struct {
  AppClapTxKind kind;
  uint32_t id,quality,distanceCm,side;
  uint64_t arrivalNs,history;
} AppClapTx;
void AppClap_Reset(uint64_t sampleCount,uint32_t epoch);
void AppClap_Restart(uint64_t sampleCount,uint32_t epoch); /* retain drop count */
void AppClap_PauseDetection(void);
/* 4096 sample / 1.5 ms budget; no transport or callbacks other than time and
 * coherent audio snapshot. Return 1 for a new onset, then publish it below. */
int AppClap_Process(const AppAudioView *audio,const AppAudioTime *time,
                     uint64_t (*nowNs)(void),AppClapOnset *onset);
void AppClap_PublishLocal(uint64_t masterNs,uint32_t quality,uint32_t id,uint32_t nowMs);
/* Payload validation and deduplication. Return 0 on invalid payload; valid
 * duplicates return 1 without extending event/result lifetime. */
int AppClap_ReceiveEvent(uint32_t id,uint64_t masterNs,uint64_t quality,uint64_t currentNs,uint32_t nowMs);
int AppClap_ReceiveResult(uint32_t id,uint64_t cm,uint64_t side,uint64_t quality,uint64_t history,uint32_t nowMs);
/* Role-specific pairing, expiry and retry selection. nextId shares the
 * coordinator's sequence; return 1 only when a message should be sent. */
int AppClap_Tick(uint32_t nowMs,int active,int32_t temperatureDeciC,uint32_t syncErrorNs,
                 uint32_t (*nextId)(void),AppClapTx *message);
#endif
