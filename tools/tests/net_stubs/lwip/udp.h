#ifndef TEST_UDP_H
#define TEST_UDP_H
#include <stdint.h>
typedef uint16_t u16_t;
typedef int err_t;
#define ERR_OK 0
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
typedef struct { uint32_t value; } ip_addr_t;
#define IP_ADDR_ANY ((const ip_addr_t *)0)
#define IP_ADDR4(p,a,b,c,d) ((p)->value = ((uint32_t)(a)<<24)|((b)<<16)|((c)<<8)|(d))
#define ip_addr_cmp(a,b) ((a)->value == (b)->value)
struct pbuf { uint16_t tot_len; uint8_t bytes[64]; };
struct udp_pcb { int unused; };
struct pbuf *pbuf_alloc(int layer, uint16_t size, int type);
void pbuf_free(struct pbuf *p);
err_t pbuf_take(struct pbuf *p, const void *data, uint16_t size);
uint16_t pbuf_copy_partial(struct pbuf *p, void *data, uint16_t size, uint16_t off);
struct udp_pcb *udp_new(void);
err_t udp_bind(struct udp_pcb *p, const ip_addr_t *addr, uint16_t port);
void udp_remove(struct udp_pcb *p);
void udp_recv(struct udp_pcb *p, void (*cb)(void *, struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t), void *arg);
err_t udp_sendto(struct udp_pcb *p, struct pbuf *b, const ip_addr_t *addr, uint16_t port);
#endif
