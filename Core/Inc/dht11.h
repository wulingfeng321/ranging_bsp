#ifndef DHT11_H
#define DHT11_H
#include <stdint.h>
#define DHT11_EDGE_COUNT 83U
typedef enum { DHT11_OK, DHT11_TIMING_ERROR, DHT11_CHECKSUM_ERROR, DHT11_DATA_ERROR } Dht11Result;
/* Main-loop decoder. Edges start with the sensor's response falling edge,
 * alternate low/high, and use microseconds from release of the start pulse.
 * Outputs are untouched on failure. Supports integer and V1.3 decimal/sign data. */
Dht11Result Dht11_Decode(const uint16_t edges[DHT11_EDGE_COUNT],
                         int32_t *temperatureDeciC,uint32_t *humidityDeciPercent);
#endif
