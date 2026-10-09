#include "dht11.h"

static int Between(uint32_t value,uint32_t low,uint32_t high)
{ return value>=low && value<=high; }
Dht11Result Dht11_Decode(const uint16_t edges[DHT11_EDGE_COUNT],
                         int32_t *temperatureDeciC,uint32_t *humidityDeciPercent)
{
  uint8_t bytes[5]={0};
  unsigned i;int32_t temperature;uint32_t humidity,low,high;
  if(!Between(edges[0],10,120) || !Between((uint32_t)edges[1]-edges[0],60,110) ||
     !Between((uint32_t)edges[2]-edges[1],60,110)) return DHT11_TIMING_ERROR;
  for(i=0;i<40;++i) {
    low=(uint32_t)edges[3+2*i]-edges[2+2*i];
    high=(uint32_t)edges[4+2*i]-edges[3+2*i];
    if(!Between(low,35,75) || (!Between(high,10,45) && !Between(high,55,100)))
      return DHT11_TIMING_ERROR;
    bytes[i/8]=(uint8_t)((bytes[i/8]<<1)|(high>=55));
  }
  if((uint8_t)(bytes[0]+bytes[1]+bytes[2]+bytes[3])!=bytes[4]) return DHT11_CHECKSUM_ERROR;
  if(bytes[1]>9 || (bytes[3]&127)>9) return DHT11_DATA_ERROR;
  humidity=bytes[0]*10U+bytes[1];
  temperature=bytes[2]*10+(bytes[3]&127);
  if(bytes[3]&128) temperature=-temperature;
  if(humidity>1000 || temperature< -200 || temperature>600) return DHT11_DATA_ERROR;
  *temperatureDeciC=temperature;*humidityDeciPercent=humidity;return DHT11_OK;
}
