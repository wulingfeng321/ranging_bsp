#ifndef APP_WIRE_H
#define APP_WIRE_H
#include <stdint.h>
/* Pure byte codecs: no alignment, native-struct or host-endian assumptions. */
static inline void AppWire_Put32BE(uint8_t *p,uint32_t v)
{ p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)(v>>0); }
static inline uint32_t AppWire_Get32BE(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|((uint32_t)p[3]<<0); }
static inline void AppWire_Put64BE(uint8_t *p,uint64_t v)
{ AppWire_Put32BE(p+4,(uint32_t)v);AppWire_Put32BE(p+0,(uint32_t)(v>>32)); }
static inline uint64_t AppWire_Get64BE(const uint8_t *p)
{ return AppWire_Get32BE(p+4)|((uint64_t)AppWire_Get32BE(p+0)<<32); }
static inline void AppWire_Put32LE(uint8_t *p,uint32_t v)
{ p[0]=(uint8_t)(v>>0);p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static inline uint32_t AppWire_Get32LE(const uint8_t *p)
{ return ((uint32_t)p[0]<<0)|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline void AppWire_Put64LE(uint8_t *p,uint64_t v)
{ AppWire_Put32LE(p+0,(uint32_t)v);AppWire_Put32LE(p+4,(uint32_t)(v>>32)); }
static inline uint64_t AppWire_Get64LE(const uint8_t *p)
{ return AppWire_Get32LE(p+0)|((uint64_t)AppWire_Get32LE(p+4)<<32); }
#endif
