#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_range_protocol.h"

/* Fixed external wire vector, independent of the production byte helpers. */
static const uint8_t golden[76]={
  0x52,0x41,0x4e,0x32,0x0c,0x09,0x02,0x4c,
  1,2,3,4,5,6,7,8,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
  0x21,0x22,0x23,0x24,0x31,0x32,0x33,0x34,
  0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,
  0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,
  0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,
  0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,
  0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x91,0x92,0x93,0x94
};
static int Decode(AppRangePacket *p,const uint8_t *bytes,size_t length)
{ return AppRangeProtocol_Decode(p,bytes,length,2,0x0102030405060708ULL,0x1112131415161718ULL); }
int main(void)
{
  AppRangePacket in,out,before;unsigned i,role,type;
  uint8_t bytes[78],bad[76];
  memset(&in,0,sizeof(in));in.type=APP_RANGE_MSG_PEAK_EVENT;in.role=2;
  in.senderSession=0x0102030405060708ULL;in.receiverSession=0x1112131415161718ULL;
  in.id=0x21222324;in.epoch=0x31323334;in.revision=0x91929394;
  for(i=0;i<5;++i) in.payload[i]=0x4041424344454647ULL+i*0x1010101010101010ULL;
  memset(bytes,0xa5,sizeof(bytes));AppRangeProtocol_Encode(bytes+1,&in);
  assert(bytes[0]==0xa5 && bytes[77]==0xa5 && !memcmp(bytes+1,golden,76));
  assert(Decode(&out,bytes+1,76));
  assert(out.type==9 && out.role==2 && out.id==in.id && out.epoch==in.epoch && out.revision==in.revision);
  assert(out.senderSession==in.senderSession && out.receiverSession==in.receiverSession);
  for(i=0;i<5;++i) assert(out.payload[i]==in.payload[i]);
  memset(&out,0xa5,sizeof(out));memcpy(&before,&out,sizeof(out));
  for(i=0;i<=78;++i) if(i!=76) {
    assert(!Decode(&out,bytes,i));assert(!memcmp(&out,&before,sizeof(out)));
  }
  for(i=0;i<24;++i) if(i!=5) {
    memcpy(bad,golden,76);bad[i]^=0x80;
    assert(!Decode(&out,bad,76));assert(!memcmp(&out,&before,sizeof(out)));
  }
  memcpy(bad,golden,76);bad[5]=255;assert(Decode(&out,bad,76) && out.type==255);
  for(role=1;role<=2;++role) for(type=1;type<=APP_RANGE_MSG_AUTO_READ_REQUEST;++type) {
    in.type=(uint8_t)type;in.role=(uint8_t)role;
    in.id=UINT32_MAX;in.epoch=0;in.revision=UINT32_MAX;
    for(i=0;i<5;++i) in.payload[i]=(i&1) ? 0:UINT64_MAX;
    AppRangeProtocol_Encode(bytes+1,&in);
    assert(AppRangeProtocol_Decode(&out,bytes+1,76,(uint8_t)role,in.senderSession,in.receiverSession));
    assert(out.id==UINT32_MAX && out.epoch==0 && out.revision==UINT32_MAX);
    for(i=0;i<5;++i) assert(out.payload[i]==in.payload[i]);
  }
  puts("PASS: RAN2 fixed byte vector, unaligned buffers, all message types/roles, header/session rejection and full-width fields");
  return 0;
}
