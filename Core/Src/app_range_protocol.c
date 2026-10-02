#include "app_range_protocol.h"
#include "app_wire.h"
#include <string.h>

void AppRangeProtocol_Encode(uint8_t bytes[APP_RANGE_WIRE_SIZE],const AppRangePacket *packet)
{
  unsigned i;
  memcpy(bytes,"RAN2",4);bytes[4]=APP_RANGE_WIRE_VERSION;
  bytes[5]=packet->type;bytes[6]=packet->role;bytes[7]=APP_RANGE_WIRE_SIZE;
  AppWire_Put64BE(bytes+8,packet->senderSession);
  AppWire_Put64BE(bytes+16,packet->receiverSession);
  AppWire_Put32BE(bytes+24,packet->id);AppWire_Put32BE(bytes+28,packet->epoch);
  for(i=0;i<5;++i) AppWire_Put64BE(bytes+32+8*i,packet->payload[i]);
  AppWire_Put32BE(bytes+72,packet->revision);
}
int AppRangeProtocol_Decode(AppRangePacket *packet,const uint8_t *bytes,size_t length,
                             uint8_t peerRole,uint64_t peerSession,uint64_t localSession)
{
  unsigned i;
  if(length!=APP_RANGE_WIRE_SIZE || memcmp(bytes,"RAN2",4) ||
     bytes[4]!=APP_RANGE_WIRE_VERSION || bytes[6]!=peerRole || bytes[7]!=APP_RANGE_WIRE_SIZE ||
     AppWire_Get64BE(bytes+8)!=peerSession || AppWire_Get64BE(bytes+16)!=localSession) return 0;
  packet->type=bytes[5];packet->role=bytes[6];
  packet->senderSession=peerSession;packet->receiverSession=localSession;
  packet->id=AppWire_Get32BE(bytes+24);packet->epoch=AppWire_Get32BE(bytes+28);
  for(i=0;i<5;++i) packet->payload[i]=AppWire_Get64BE(bytes+32+8*i);
  packet->revision=AppWire_Get32BE(bytes+72);return 1;
}
