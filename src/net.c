#include "net.h"
#include "debug.h"
#include "driver.h"

#define NET_RX_QUEUE 8
#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_UDP 17
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define NET_UDP_QUEUE 8

typedef struct {
    uint32_t ifindex;
    uint32_t len;
    uint8_t data[NET_FRAME_MAX];
} net_packet_t;

typedef struct {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_len;
    uint16_t ident;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed)) ipv4_hdr_t;

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

typedef struct {
    uint32_t src_ipv4;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t len;
    uint8_t data[NET_UDP_PAYLOAD_MAX];
} udp_packet_t;

static netif_t g_netifs[NET_MAX_IFACES];
static net_route_t g_routes[NET_MAX_ROUTES];
static net_arp_entry_t g_arp[NET_ARP_MAX];
static char g_net_names[NET_MAX_IFACES][NET_NAME_MAX];
static net_driver_send_fn g_send[NET_MAX_IFACES];
static net_driver_poll_fn g_poll[NET_MAX_IFACES];
static void *g_ctx[NET_MAX_IFACES];
static uint32_t g_netif_count;
static uint32_t g_route_count;
static uint32_t g_arp_count;
static net_packet_t g_rxq[NET_RX_QUEUE];
static udp_packet_t g_udpq[NET_UDP_QUEUE];
static uint32_t g_rx_head;
static uint32_t g_rx_tail;
static uint32_t g_rx_count;
static uint32_t g_udp_head;
static uint32_t g_udp_tail;
static uint32_t g_udp_count;

static uint32_t net_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void net_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

static void set_mac(uint8_t mac[6], uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f) {
    mac[0] = a; mac[1] = b; mac[2] = c; mac[3] = d; mac[4] = e; mac[5] = f;
}

static uint16_t bswap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FFU) << 24) |
           ((v & 0x0000FF00U) << 8) |
           ((v & 0x00FF0000U) >> 8) |
           ((v & 0xFF000000U) >> 24);
}

static void memcopy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static int mac_unicast_valid(const uint8_t mac[6]) {
    uint32_t i;
    int nonzero = 0;
    if (!mac) return 0;
    if (mac[0] & 1U) return 0;
    for (i = 0; i < 6; i++) {
        if (mac[i] != 0) nonzero = 1;
    }
    return nonzero;
}

static int streq(const char *a, const char *b) {
    while (a && b && *a && *b && *a == *b) {
        a++;
        b++;
    }
    return a && b && *a == 0 && *b == 0;
}

static int name_char_ok(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' ||
           ch == '-';
}

static int net_name_valid(const char *name) {
    uint32_t len = 0;
    if (!name || !name[0]) return 0;
    while (name[len]) {
        if (len + 1U >= NET_NAME_MAX || !name_char_ok(name[len])) return 0;
        len++;
    }
    return len > 0;
}

static int route_mask_valid(uint32_t mask) {
    uint32_t inv = ~mask;
    return (inv & (inv + 1U)) == 0;
}

static uint32_t route_prefix_len(uint32_t mask) {
    uint32_t bits = 0;
    while (mask & 0x80000000U) {
        bits++;
        mask <<= 1;
    }
    return bits;
}

static uint32_t connected_mask_for(uint32_t ipv4) {
    return ((ipv4 >> 24) == 127U) ? 0xFF000000U : 0xFFFFFF00U;
}

static void copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    while (src && src[i] && i + 1U < NET_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static uint16_t checksum16(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint16_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t checksum_add_bytes(uint32_t sum, const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint16_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return sum;
}

static uint16_t checksum_finish(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t udp_checksum4(const ipv4_hdr_t *ip, const udp_hdr_t *udp, uint32_t udp_len) {
    uint32_t sum = 0;
    if (!ip || !udp || udp_len < sizeof(udp_hdr_t)) return 0;
    sum = checksum_add_bytes(sum, &ip->src, sizeof(ip->src));
    sum = checksum_add_bytes(sum, &ip->dst, sizeof(ip->dst));
    sum += IPV4_PROTO_UDP;
    sum += udp_len;
    sum = checksum_add_bytes(sum, udp, udp_len);
    return checksum_finish(sum);
}

static int enqueue_rx_locked(uint32_t ifindex, const void *frame, uint32_t len) {
    net_packet_t *pkt;
    if (!frame || len == 0 || len > NET_FRAME_MAX || g_rx_count >= NET_RX_QUEUE) return -1;
    pkt = &g_rxq[g_rx_tail];
    pkt->ifindex = ifindex;
    pkt->len = len;
    memcopy(pkt->data, (const uint8_t *)frame, len);
    g_rx_tail = (g_rx_tail + 1) % NET_RX_QUEUE;
    g_rx_count++;
    return (int)len;
}

static int enqueue_rx(uint32_t ifindex, const void *frame, uint32_t len) {
    uint32_t flags = net_irq_save();
    int r = enqueue_rx_locked(ifindex, frame, len);
    net_irq_restore(flags);
    return r;
}

static int dequeue_rx_locked(net_packet_t *out) {
    if (g_rx_count == 0 || !out) return -1;
    *out = g_rxq[g_rx_head];
    g_rx_head = (g_rx_head + 1) % NET_RX_QUEUE;
    g_rx_count--;
    return 0;
}

static int dequeue_rx(net_packet_t *out) {
    uint32_t flags = net_irq_save();
    int r = dequeue_rx_locked(out);
    net_irq_restore(flags);
    return r;
}

static int dequeue_rx_for(uint32_t ifindex, net_packet_t *out) {
    uint32_t flags = net_irq_save();
    uint32_t scan = g_rx_count;
    while (scan--) {
        net_packet_t pkt;
        if (dequeue_rx_locked(&pkt) < 0) {
            net_irq_restore(flags);
            return -1;
        }
        if (pkt.ifindex == ifindex) {
            if (out) *out = pkt;
            net_irq_restore(flags);
            return 0;
        }
        (void)enqueue_rx_locked(pkt.ifindex, pkt.data, pkt.len);
    }
    net_irq_restore(flags);
    return -1;
}

static int enqueue_udp_locked(uint32_t src_ipv4,
                              uint16_t src_port,
                              uint16_t dst_port,
                              const void *payload,
                              uint32_t len) {
    udp_packet_t *pkt;
    if ((len > 0 && !payload) || len > NET_UDP_PAYLOAD_MAX || g_udp_count >= NET_UDP_QUEUE) return -1;
    pkt = &g_udpq[g_udp_tail];
    pkt->src_ipv4 = src_ipv4;
    pkt->src_port = src_port;
    pkt->dst_port = dst_port;
    pkt->len = len;
    if (len > 0) memcopy(pkt->data, (const uint8_t *)payload, len);
    g_udp_tail = (g_udp_tail + 1) % NET_UDP_QUEUE;
    g_udp_count++;
    return (int)len;
}

static int enqueue_udp(uint32_t src_ipv4,
                       uint16_t src_port,
                       uint16_t dst_port,
                       const void *payload,
                       uint32_t len) {
    uint32_t flags = net_irq_save();
    int r = enqueue_udp_locked(src_ipv4, src_port, dst_port, payload, len);
    net_irq_restore(flags);
    return r;
}

static int dequeue_udp_locked(udp_packet_t *out) {
    if (g_udp_count == 0 || !out) return -1;
    *out = g_udpq[g_udp_head];
    g_udp_head = (g_udp_head + 1) % NET_UDP_QUEUE;
    g_udp_count--;
    return 0;
}

static int dequeue_udp(udp_packet_t *out) {
    uint32_t flags = net_irq_save();
    int r = dequeue_udp_locked(out);
    net_irq_restore(flags);
    return r;
}

static void ipv4_fill(ipv4_hdr_t *ip, uint32_t src, uint32_t dst, uint16_t len, uint8_t proto) {
    ip->version_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_len = bswap16(len);
    ip->ident = bswap16(0x1234);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = proto;
    ip->checksum = 0;
    ip->src = bswap32(src);
    ip->dst = bswap32(dst);
    ip->checksum = bswap16(checksum16(ip, sizeof(*ip)));
}

static int net_handle_ipv4(uint32_t ifindex, uint8_t *frame, uint32_t len) {
    ipv4_hdr_t *ip;
    icmp_hdr_t *icmp;
    udp_hdr_t *udp;
    uint32_t ihl;
    uint32_t dst;
    uint32_t src;
    uint32_t ip_len;
    uint32_t udp_len;
    if (ifindex >= g_netif_count || len < sizeof(ipv4_hdr_t)) return 0;
    ip = (ipv4_hdr_t *)frame;
    ihl = (uint32_t)(ip->version_ihl & 0x0F) * 4U;
    if ((ip->version_ihl >> 4) != 4 || ihl < sizeof(ipv4_hdr_t) || len < ihl) return 0;
    ip_len = bswap16(ip->total_len);
    if (ip_len < ihl || ip_len > len) return 0;
    if (checksum16(ip, ihl) != 0) return 0;
    dst = bswap32(ip->dst);
    src = bswap32(ip->src);
    if (dst != g_netifs[ifindex].ipv4) return 0;
    if (ip->protocol == IPV4_PROTO_UDP) {
        if (ip_len < ihl + sizeof(udp_hdr_t)) return 0;
        udp = (udp_hdr_t *)(frame + ihl);
        udp_len = bswap16(udp->len);
        if (udp_len < sizeof(udp_hdr_t) || udp_len > ip_len - ihl) return 0;
        if (udp->checksum == 0 || udp_checksum4(ip, udp, udp_len) != 0) return 0;
        if (enqueue_udp(src,
                        bswap16(udp->src_port),
                        bswap16(udp->dst_port),
                        frame + ihl + sizeof(udp_hdr_t),
                        udp_len - sizeof(udp_hdr_t)) >= 0) {
            return 1;
        }
        return 0;
    }
    if (ip->protocol != IPV4_PROTO_ICMP || ip_len < ihl + sizeof(icmp_hdr_t)) return 0;
    icmp = (icmp_hdr_t *)(frame + ihl);
    if (icmp->type != ICMP_ECHO_REQUEST || icmp->code != 0) return 0;
    if (checksum16(icmp, ip_len - ihl) != 0) return 0;
    icmp->type = ICMP_ECHO_REPLY;
    icmp->checksum = 0;
    icmp->checksum = bswap16(checksum16(icmp, ip_len - ihl));
    ipv4_fill(ip, dst, src, (uint16_t)ip_len, IPV4_PROTO_ICMP);
    if (net_send(ifindex, frame, ip_len) > 0) {
        return 1;
    }
    return 0;
}

void net_init(void) {
    uint32_t i;
    for (i = 0; i < NET_MAX_IFACES; i++) {
        g_netifs[i].name = 0;
        g_netifs[i].ipv4 = 0;
        g_netifs[i].mtu = 0;
        g_netifs[i].up = 0;
        g_netifs[i].tx_packets = 0;
        g_netifs[i].rx_packets = 0;
        g_net_names[i][0] = 0;
        set_mac(g_netifs[i].mac, 0, 0, 0, 0, 0, 0);
        g_send[i] = 0;
        g_poll[i] = 0;
        g_ctx[i] = 0;
    }
    g_rx_head = 0;
    g_rx_tail = 0;
    g_rx_count = 0;
    g_udp_head = 0;
    g_udp_tail = 0;
    g_udp_count = 0;
    g_netif_count = 0;
    g_route_count = 0;
    g_arp_count = 0;
    {
        uint8_t mac[6] = {0, 0, 0, 0, 0, 1};
        (void)net_register_if("lo0", mac, 0x7F000001U, NET_FRAME_MAX, 0, 0, 0);
    }
    driver_register("loopback-net", DRIVER_BUS_NET, 0x7F000001U, 0);
    debug_puts("[net] lo0 up\n");
}

int net_register_if(const char *name,
                    const uint8_t mac[6],
                    uint32_t ipv4,
                    uint32_t mtu,
                    net_driver_send_fn send,
                    net_driver_poll_fn poll,
                    void *ctx) {
    netif_t *n;
    uint32_t idx;
    uint32_t i;
    if (!net_name_valid(name) || mtu == 0 || mtu > NET_FRAME_MAX || g_netif_count >= NET_MAX_IFACES) return -1;
    for (i = 0; i < g_netif_count; i++) {
        if (streq(name, g_netifs[i].name)) return -1;
    }
    idx = g_netif_count;
    n = &g_netifs[idx];
    copy_name(g_net_names[idx], name);
    n->name = g_net_names[idx];
    n->ipv4 = ipv4;
    n->mtu = mtu;
    n->up = 1;
    n->tx_packets = 0;
    n->rx_packets = 0;
    for (i = 0; i < 6; i++) n->mac[i] = mac ? mac[i] : 0;
    g_send[idx] = send;
    g_poll[idx] = poll;
    g_ctx[idx] = ctx;
    g_netif_count++;
    if (ipv4) {
        uint32_t mask = connected_mask_for(ipv4);
        (void)net_route_add(ipv4 & mask, mask, 0, idx);
        (void)net_arp_learn(idx, ipv4, n->mac);
    }
    return (int)idx;
}

uint32_t netif_count(void) {
    return g_netif_count;
}

uint32_t net_route_count(void) {
    return g_route_count;
}

uint32_t net_arp_count(void) {
    uint32_t flags = net_irq_save();
    uint32_t count = g_arp_count;
    net_irq_restore(flags);
    return count;
}

uint32_t net_rx_queue_count(void) {
    uint32_t flags = net_irq_save();
    uint32_t count = g_rx_count;
    net_irq_restore(flags);
    return count;
}

uint32_t net_udp_queue_count(void) {
    uint32_t flags = net_irq_save();
    uint32_t count = g_udp_count;
    net_irq_restore(flags);
    return count;
}

const netif_t *netif_at(uint32_t index) {
    if (index >= g_netif_count) return 0;
    return &g_netifs[index];
}

const net_route_t *net_route_at(uint32_t index) {
    if (index >= g_route_count) return 0;
    return &g_routes[index];
}

const net_arp_entry_t *net_arp_at(uint32_t index) {
    if (index >= g_arp_count) return 0;
    return &g_arp[index];
}

int net_route_add(uint32_t dest, uint32_t mask, uint32_t gateway, uint32_t ifindex) {
    uint32_t i;
    if (ifindex >= g_netif_count || !route_mask_valid(mask)) return -1;
    dest &= mask;
    for (i = 0; i < g_route_count; i++) {
        if (g_routes[i].dest == dest &&
            g_routes[i].mask == mask &&
            g_routes[i].gateway == gateway &&
            g_routes[i].ifindex == ifindex) return 0;
    }
    if (g_route_count >= NET_MAX_ROUTES) return -1;
    g_routes[g_route_count].dest = dest;
    g_routes[g_route_count].mask = mask;
    g_routes[g_route_count].gateway = gateway;
    g_routes[g_route_count].ifindex = ifindex;
    g_route_count++;
    return 0;
}

int net_route_del(uint32_t dest, uint32_t mask, uint32_t gateway, uint32_t ifindex) {
    uint32_t i;
    if (ifindex >= g_netif_count || !route_mask_valid(mask)) return -1;
    dest &= mask;
    for (i = 0; i < g_route_count; i++) {
        if (g_routes[i].dest == dest &&
            g_routes[i].mask == mask &&
            g_routes[i].gateway == gateway &&
            g_routes[i].ifindex == ifindex) {
            uint32_t j;
            for (j = i; j + 1U < g_route_count; j++) {
                g_routes[j].dest = g_routes[j + 1U].dest;
                g_routes[j].mask = g_routes[j + 1U].mask;
                g_routes[j].gateway = g_routes[j + 1U].gateway;
                g_routes[j].ifindex = g_routes[j + 1U].ifindex;
            }
            g_route_count--;
            g_routes[g_route_count].dest = 0;
            g_routes[g_route_count].mask = 0;
            g_routes[g_route_count].gateway = 0;
            g_routes[g_route_count].ifindex = 0;
            return 0;
        }
    }
    return -1;
}

int net_arp_learn(uint32_t ifindex, uint32_t ipv4, const uint8_t mac[6]) {
    uint32_t flags;
    uint32_t i;
    if (ifindex >= g_netif_count || ipv4 == 0 || !mac_unicast_valid(mac)) return -1;
    flags = net_irq_save();
    for (i = 0; i < g_arp_count; i++) {
        if (g_arp[i].ifindex == ifindex && g_arp[i].ipv4 == ipv4) {
            memcopy(g_arp[i].mac, mac, 6);
            net_irq_restore(flags);
            return 0;
        }
    }
    if (g_arp_count >= NET_ARP_MAX) {
        net_irq_restore(flags);
        return -1;
    }
    g_arp[g_arp_count].ifindex = ifindex;
    g_arp[g_arp_count].ipv4 = ipv4;
    memcopy(g_arp[g_arp_count].mac, mac, 6);
    g_arp_count++;
    net_irq_restore(flags);
    return 0;
}

int net_arp_delete(uint32_t ifindex, uint32_t ipv4) {
    uint32_t flags;
    uint32_t i;
    if (ifindex >= g_netif_count || ipv4 == 0) return -1;
    flags = net_irq_save();
    for (i = 0; i < g_arp_count; i++) {
        if (g_arp[i].ifindex == ifindex && g_arp[i].ipv4 == ipv4) {
            uint32_t j;
            for (j = i; j + 1U < g_arp_count; j++) {
                uint32_t k;
                g_arp[j].ifindex = g_arp[j + 1U].ifindex;
                g_arp[j].ipv4 = g_arp[j + 1U].ipv4;
                for (k = 0; k < 6; k++) g_arp[j].mac[k] = g_arp[j + 1U].mac[k];
            }
            g_arp_count--;
            g_arp[g_arp_count].ifindex = 0;
            g_arp[g_arp_count].ipv4 = 0;
            for (j = 0; j < 6; j++) g_arp[g_arp_count].mac[j] = 0;
            net_irq_restore(flags);
            return 0;
        }
    }
    net_irq_restore(flags);
    return -1;
}

int net_arp_lookup(uint32_t ifindex, uint32_t ipv4, uint8_t mac[6]) {
    uint32_t flags;
    uint32_t i;
    if (ifindex >= g_netif_count || ipv4 == 0 || !mac) return -1;
    flags = net_irq_save();
    for (i = 0; i < g_arp_count; i++) {
        if (g_arp[i].ifindex == ifindex && g_arp[i].ipv4 == ipv4) {
            memcopy(mac, g_arp[i].mac, 6);
            net_irq_restore(flags);
            return 0;
        }
    }
    net_irq_restore(flags);
    return -1;
}

int net_route_lookup4(uint32_t dst_ipv4, uint32_t *ifindex, uint32_t *gateway) {
    int best = -1;
    uint32_t best_prefix = 0;
    uint32_t i;
    for (i = 0; i < g_route_count; i++) {
        const net_route_t *route = &g_routes[i];
        uint32_t prefix;
        if ((dst_ipv4 & route->mask) != route->dest) continue;
        if (route->ifindex >= g_netif_count || !g_netifs[route->ifindex].up) continue;
        prefix = route_prefix_len(route->mask);
        if (best < 0 || prefix >= best_prefix) {
            best = (int)i;
            best_prefix = prefix;
        }
    }
    if (best < 0) return -1;
    if (ifindex) *ifindex = g_routes[best].ifindex;
    if (gateway) *gateway = g_routes[best].gateway;
    return 0;
}

int netif_set_up(uint32_t index, int up) {
    if (index >= g_netif_count) return -1;
    g_netifs[index].up = up ? 1 : 0;
    return 0;
}

int netif_set_ipv4(uint32_t index, uint32_t ipv4) {
    uint32_t old_ipv4;
    if (index >= g_netif_count) return -1;
    old_ipv4 = g_netifs[index].ipv4;
    g_netifs[index].ipv4 = ipv4;
    if (ipv4) {
        uint32_t mask = connected_mask_for(ipv4);
        int updated = 0;
        if (old_ipv4) {
            uint32_t old_mask = connected_mask_for(old_ipv4);
            uint32_t old_dest = old_ipv4 & old_mask;
            uint32_t i;
            for (i = 0; i < g_route_count; i++) {
                if (g_routes[i].ifindex == index &&
                    g_routes[i].gateway == 0 &&
                    g_routes[i].dest == old_dest &&
                    g_routes[i].mask == old_mask) {
                    g_routes[i].dest = ipv4 & mask;
                    g_routes[i].mask = mask;
                    updated = 1;
                    break;
                }
            }
        }
        if (!updated) (void)net_route_add(ipv4 & mask, mask, 0, index);
        (void)net_arp_learn(index, ipv4, g_netifs[index].mac);
    }
    return 0;
}

int net_send(uint32_t ifindex, const void *frame, uint32_t len) {
    int sent;
    if (ifindex >= g_netif_count ||
        !frame ||
        len == 0 ||
        len > g_netifs[ifindex].mtu ||
        !g_netifs[ifindex].up) return -1;
    if (ifindex == 0) {
        sent = enqueue_rx(ifindex, frame, len);
        if (sent >= 0) g_netifs[ifindex].rx_packets++;
    }
    else if (g_send[ifindex]) sent = g_send[ifindex](g_ctx[ifindex], frame, len);
    else sent = -1;
    if (sent < 0) return -1;
    g_netifs[ifindex].tx_packets++;
    return sent;
}

int net_recv(uint32_t ifindex, void *frame, uint32_t max) {
    net_packet_t pkt;
    uint32_t n;
    if (ifindex >= g_netif_count) return -1;
    if (max == 0) return 0;
    if (!frame) return -1;
    if (dequeue_rx_for(ifindex, &pkt) < 0) return 0;
    n = pkt.len;
    if (n > max) n = max;
    memcopy((uint8_t *)frame, pkt.data, n);
    return (int)n;
}

int net_receive_from_driver(uint32_t ifindex, const void *frame, uint32_t len) {
    if (ifindex >= g_netif_count ||
        !frame ||
        len == 0 ||
        !g_netifs[ifindex].up ||
        len > g_netifs[ifindex].mtu) return -1;
    if (enqueue_rx(ifindex, frame, len) < 0) return -1;
    g_netifs[ifindex].rx_packets++;
    return (int)len;
}

void net_poll(void) {
    uint32_t budget;
    uint32_t i;
    for (i = 0; i < g_netif_count; i++) {
        if (g_netifs[i].up && g_poll[i]) g_poll[i](g_ctx[i]);
    }
    budget = g_rx_count;
    while (budget--) {
        net_packet_t pkt;
        if (dequeue_rx(&pkt) < 0) return;
        (void)net_handle_ipv4(pkt.ifindex, pkt.data, pkt.len);
    }
}

int net_ping4(uint32_t ifindex, uint32_t dst_ipv4) {
    uint8_t packet[sizeof(ipv4_hdr_t) + sizeof(icmp_hdr_t)];
    uint8_t reply[sizeof(packet)];
    ipv4_hdr_t *ip = (ipv4_hdr_t *)packet;
    icmp_hdr_t *icmp = (icmp_hdr_t *)(packet + sizeof(ipv4_hdr_t));
    int n;
    if (ifindex >= g_netif_count || !g_netifs[ifindex].up || g_netifs[ifindex].ipv4 == 0) return -1;
    ipv4_fill(ip, g_netifs[ifindex].ipv4, dst_ipv4, sizeof(packet), IPV4_PROTO_ICMP);
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->ident = bswap16(1);
    icmp->seq = bswap16(1);
    icmp->checksum = bswap16(checksum16(icmp, sizeof(icmp_hdr_t)));
    if (net_send(ifindex, packet, sizeof(packet)) < 0) return -1;
    net_poll();
    n = net_recv(ifindex, reply, sizeof(reply));
    if (n == (int)sizeof(reply)) {
        ipv4_hdr_t *rip = (ipv4_hdr_t *)reply;
        icmp_hdr_t *ricmp = (icmp_hdr_t *)(reply + sizeof(ipv4_hdr_t));
        uint32_t ihl = (uint32_t)(rip->version_ihl & 0x0F) * 4U;
        if ((rip->version_ihl >> 4) == 4 &&
            ihl == sizeof(ipv4_hdr_t) &&
            bswap16(rip->total_len) == sizeof(reply) &&
            checksum16(rip, ihl) == 0 &&
            rip->protocol == IPV4_PROTO_ICMP &&
            bswap32(rip->src) == dst_ipv4 &&
            bswap32(rip->dst) == g_netifs[ifindex].ipv4 &&
            checksum16(ricmp, sizeof(icmp_hdr_t)) == 0 &&
            ricmp->type == ICMP_ECHO_REPLY &&
            ricmp->code == 0 &&
            ricmp->ident == bswap16(1) &&
            ricmp->seq == bswap16(1)) return 0;
    }
    return -1;
}

int net_udp_send4(uint32_t ifindex,
                  uint32_t dst_ipv4,
                  uint16_t src_port,
                  uint16_t dst_port,
                  const void *payload,
                  uint32_t len) {
    uint8_t packet[sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) + NET_UDP_PAYLOAD_MAX];
    ipv4_hdr_t *ip = (ipv4_hdr_t *)packet;
    udp_hdr_t *udp = (udp_hdr_t *)(packet + sizeof(ipv4_hdr_t));
    uint32_t i;
    uint32_t total_len;
    if (ifindex >= g_netif_count ||
        !g_netifs[ifindex].up ||
        g_netifs[ifindex].ipv4 == 0 ||
        (len > 0 && !payload) ||
        len > NET_UDP_PAYLOAD_MAX) return -1;
    total_len = sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) + len;
    ipv4_fill(ip, g_netifs[ifindex].ipv4, dst_ipv4, (uint16_t)total_len, IPV4_PROTO_UDP);
    udp->src_port = bswap16(src_port);
    udp->dst_port = bswap16(dst_port);
    udp->len = bswap16((uint16_t)(sizeof(udp_hdr_t) + len));
    udp->checksum = 0;
    for (i = 0; i < len; i++) packet[sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) + i] = ((const uint8_t *)payload)[i];
    udp->checksum = udp_checksum4(ip, udp, sizeof(udp_hdr_t) + len);
    if (udp->checksum == 0) udp->checksum = 0xFFFFU;
    udp->checksum = bswap16(udp->checksum);
    if (net_send(ifindex, packet, total_len) < 0) return -1;
    net_poll();
    return (int)len;
}

int net_udp_recv4(uint16_t port,
                  void *payload,
                  uint32_t max,
                  uint32_t *src_ipv4,
                  uint16_t *src_port) {
    uint32_t flags;
    uint32_t scan;
    if (max > 0 && !payload) return -1;
    flags = net_irq_save();
    scan = g_udp_count;
    while (scan--) {
        udp_packet_t pkt;
        uint32_t n;
        if (dequeue_udp_locked(&pkt) < 0) {
            net_irq_restore(flags);
            return 0;
        }
        if (pkt.dst_port != port) {
            (void)enqueue_udp_locked(pkt.src_ipv4, pkt.src_port, pkt.dst_port, pkt.data, pkt.len);
            continue;
        }
        if (pkt.len > 0 && max == 0) {
            (void)enqueue_udp_locked(pkt.src_ipv4, pkt.src_port, pkt.dst_port, pkt.data, pkt.len);
            net_irq_restore(flags);
            return -1;
        }
        n = pkt.len;
        if (n > max) n = max;
        if (n > 0) memcopy((uint8_t *)payload, pkt.data, n);
        if (src_ipv4) *src_ipv4 = pkt.src_ipv4;
        if (src_port) *src_port = pkt.src_port;
        net_irq_restore(flags);
        return (int)n;
    }
    net_irq_restore(flags);
    return 0;
}

int net_udp_pending4(uint16_t port) {
    uint32_t flags;
    uint32_t i;
    uint32_t count = 0;
    if (port == 0) return 0;
    flags = net_irq_save();
    for (i = 0; i < g_udp_count; i++) {
        uint32_t idx = (g_udp_head + i) % NET_UDP_QUEUE;
        if (g_udpq[idx].dst_port == port) count++;
    }
    net_irq_restore(flags);
    return (int)count;
}
