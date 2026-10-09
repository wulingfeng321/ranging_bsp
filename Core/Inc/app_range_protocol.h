#ifndef APP_RANGE_PROTOCOL_H
#define APP_RANGE_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
/* RAN2 version 12, fixed 76-byte big-endian message. These constants describe
 * the existing wire contract; no HAL, LwIP, board-role or application state. */
#define APP_RANGE_WIRE_SIZE 76U
#define APP_RANGE_WIRE_VERSION 12U
typedef enum {
  APP_RANGE_MSG_SYNC_REQ=1,
  APP_RANGE_MSG_SYNC_RESP=2,
  APP_RANGE_MSG_SYNC_FOLLOW=3,
  APP_RANGE_MSG_SYNC_STATE=4,
  APP_RANGE_MSG_EVENT=5,
  APP_RANGE_MSG_RESULT=6,
  APP_RANGE_MSG_ACK=7,
  APP_RANGE_MSG_BATCH_STATE=8,
  APP_RANGE_MSG_PEAK_EVENT=9,
  APP_RANGE_MSG_PEAK_STATE=10,
  APP_RANGE_MSG_PEAK_DIAG=11,
  APP_RANGE_MSG_UI_STATE=12,
  APP_RANGE_MSG_UI_REQUEST=13,
  APP_RANGE_MSG_UI_ACK=14,
  APP_RANGE_MSG_WAVE_CLOCK=15,
  APP_RANGE_MSG_POSITION_EVENT=16,
  APP_RANGE_MSG_POSITION_RESULT=17,
  APP_RANGE_MSG_CLAP_EVENT=18,
  APP_RANGE_MSG_CLAP_RESULT=19,
  APP_RANGE_MSG_AUTO_STATUS_REQUEST=20,
  APP_RANGE_MSG_AUTO_STATUS_STATE=21,
  APP_RANGE_MSG_AUTO_READ_REQUEST=22
} AppRangeMessageType;
/* Host representation only: never copy this structure onto the wire. */
typedef struct {
  uint64_t senderSession,receiverSession,payload[5];
  uint32_t id,epoch,revision;
  uint8_t type,role;
} AppRangePacket;
/* Caller supplies non-NULL, non-overlapping buffers; encode writes 76 bytes. */
void AppRangeProtocol_Encode(uint8_t bytes[APP_RANGE_WIRE_SIZE],const AppRangePacket *packet);
/* Exact length/header/peer/session check before modifying packet. Unknown
 * types are retained for the coordinator to reject. Payload, revision, epoch,
 * page and replay rules remain message-specific responsibilities. */
int AppRangeProtocol_Decode(AppRangePacket *packet,const uint8_t *bytes,size_t length,
                             uint8_t peerRole,uint64_t peerSession,uint64_t localSession);
#endif
