#ifndef TEST_ETHARP_H
#define TEST_ETHARP_H
#include "udp.h"
#include "netif.h"
struct eth_addr { unsigned char bytes[6]; };
int etharp_find_addr(struct netif *,const ip4_addr_t *,struct eth_addr **,const ip4_addr_t **);
int etharp_request(struct netif *,const ip4_addr_t *);
#endif
