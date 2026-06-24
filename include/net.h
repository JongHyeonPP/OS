#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_MAX_IFACES 4
#define NET_MAX_ROUTES 8
#define NET_ARP_MAX    8
#define NET_FRAME_MAX  1518
#define NET_UDP_PAYLOAD_MAX 256
#define NET_NAME_MAX   16

typedef struct {
    const char *name;
    uint8_t mac[6];
    uint32_t ipv4;
    uint32_t mtu;
    int up;
    uint32_t tx_packets;
    uint32_t rx_packets;
} netif_t;

typedef struct {
    uint32_t dest;
    uint32_t mask;
    uint32_t gateway;
    uint32_t ifindex;
} net_route_t;

typedef struct {
    uint32_t ifindex;
    uint32_t ipv4;
    uint8_t mac[6];
} net_arp_entry_t;

typedef int (*net_driver_send_fn)(void *ctx, const void *frame, uint32_t len);
typedef void (*net_driver_poll_fn)(void *ctx);

void net_init(void);
int net_register_if(const char *name,
                    const uint8_t mac[6],
                    uint32_t ipv4,
                    uint32_t mtu,
                    net_driver_send_fn send,
                    net_driver_poll_fn poll,
                    void *ctx);
uint32_t netif_count(void);
uint32_t net_route_count(void);
uint32_t net_arp_count(void);
uint32_t net_rx_queue_count(void);
uint32_t net_udp_queue_count(void);
const netif_t *netif_at(uint32_t index);
const net_route_t *net_route_at(uint32_t index);
const net_arp_entry_t *net_arp_at(uint32_t index);
int net_route_add(uint32_t dest, uint32_t mask, uint32_t gateway, uint32_t ifindex);
int net_route_del(uint32_t dest, uint32_t mask, uint32_t gateway, uint32_t ifindex);
int net_route_lookup4(uint32_t dst_ipv4, uint32_t *ifindex, uint32_t *gateway);
int net_arp_learn(uint32_t ifindex, uint32_t ipv4, const uint8_t mac[6]);
int net_arp_delete(uint32_t ifindex, uint32_t ipv4);
int net_arp_lookup(uint32_t ifindex, uint32_t ipv4, uint8_t mac[6]);
int netif_set_up(uint32_t index, int up);
int netif_set_ipv4(uint32_t index, uint32_t ipv4);
int net_send(uint32_t ifindex, const void *frame, uint32_t len);
int net_recv(uint32_t ifindex, void *frame, uint32_t max);
int net_receive_from_driver(uint32_t ifindex, const void *frame, uint32_t len);
int net_ping4(uint32_t ifindex, uint32_t dst_ipv4);
int net_udp_send4(uint32_t ifindex,
                  uint32_t dst_ipv4,
                  uint16_t src_port,
                  uint16_t dst_port,
                  const void *payload,
                  uint32_t len);
int net_udp_recv4(uint16_t port,
                  void *payload,
                  uint32_t max,
                  uint32_t *src_ipv4,
                  uint16_t *src_port);
int net_udp_pending4(uint16_t port);
void net_poll(void);

#endif
