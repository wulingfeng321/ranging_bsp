#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "app_wire.h"
int main(void)
{
  const uint8_t be[]={0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
  const uint8_t le[]={0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x01};
  uint8_t buffer[10];memset(buffer,0x5a,sizeof(buffer));
  AppWire_Put64BE(buffer+1,UINT64_C(0x0123456789abcdef));
  assert(!memcmp(buffer+1,be,8) && buffer[0]==0x5a && buffer[9]==0x5a);
  assert(AppWire_Get64BE(be)==UINT64_C(0x0123456789abcdef));
  assert(AppWire_Get32BE(be)==UINT32_C(0x01234567));
  AppWire_Put64LE(buffer+1,UINT64_C(0x0123456789abcdef));
  assert(!memcmp(buffer+1,le,8) && buffer[0]==0x5a && buffer[9]==0x5a);
  assert(AppWire_Get64LE(le)==UINT64_C(0x0123456789abcdef));
  assert(AppWire_Get32LE(le)==UINT32_C(0x89abcdef));
  puts("PASS: independent BE/LE wire vectors and unaligned buffers");return 0;
}
