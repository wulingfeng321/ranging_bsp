#ifndef TEST_NETIF_H
#define TEST_NETIF_H
struct netif { int link; };
#define netif_is_link_up(p) ((p)->link)
#endif
