#include "rtl8139.h"
#include "debug.h"
#include "driver.h"
#include "io.h"
#include "net.h"
#include "pci.h"
#include <stdint.h>

#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139

#define REG_IDR0      0x00
#define REG_TSD0      0x10
#define REG_TSAD0     0x20
#define REG_RBSTART   0x30
#define REG_CR        0x37
#define REG_CAPR      0x38
#define REG_IMR       0x3C
#define REG_ISR       0x3E
#define REG_TCR       0x40
#define REG_RCR       0x44
#define REG_CONFIG1   0x52

#define CR_BUFE 0x01
#define CR_TE   0x04
#define CR_RE   0x08
#define CR_RST  0x10

#define ISR_ROK 0x0001
#define ISR_TOK 0x0004
#define ISR_RXERR 0x0002
#define ISR_TXERR 0x0008

#define RCR_AAP  0x00000001U
#define RCR_APM  0x00000002U
#define RCR_AM   0x00000004U
#define RCR_AB   0x00000008U
#define RCR_WRAP 0x00000080U
#define RCR_MXDMA_UNLIMITED (7U << 8)

#define RX_BUF_LEN 8192
#define RX_BUF_PAD 16
#define TX_BUF_LEN 2048
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806

typedef struct {
    uint16_t io;
    uint8_t mac[6];
    uint8_t rx_buf[RX_BUF_LEN + RX_BUF_PAD + NET_FRAME_MAX] __attribute__((aligned(4)));
    uint8_t tx_buf[4][TX_BUF_LEN] __attribute__((aligned(4)));
    uint32_t cur_rx;
    uint32_t tx_cur;
    uint32_t ifindex;
    int present;
} rtl8139_t;

static rtl8139_t g_rtl;

static void mmio_copy(uint8_t *dst, const uint8_t *src, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) dst[i] = src[i];
}

static uint16_t load_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           p[3];
}

static void store_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

static uint16_t rx_read16(rtl8139_t *r, uint32_t off) {
    uint16_t lo = r->rx_buf[off % RX_BUF_LEN];
    uint16_t hi = r->rx_buf[(off + 1) % RX_BUF_LEN];
    return (uint16_t)(lo | (hi << 8));
}

static void rx_copy(rtl8139_t *r, uint32_t off, uint8_t *dst, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) dst[i] = r->rx_buf[(off + i) % RX_BUF_LEN];
}

static int rtl_transmit(rtl8139_t *r, const uint8_t *frame, uint32_t len) {
    uint32_t slot;
    if (!r || !r->present || !frame || len == 0 || len > TX_BUF_LEN) return -1;
    slot = r->tx_cur & 3U;
    mmio_copy(r->tx_buf[slot], frame, len);
    outl((uint16_t)(r->io + REG_TSAD0 + slot * 4), (uint32_t)r->tx_buf[slot]);
    outl((uint16_t)(r->io + REG_TSD0 + slot * 4), len);
    r->tx_cur++;
    return (int)len;
}

static int rtl_send_arp_reply(rtl8139_t *r, const uint8_t target_mac[6], uint32_t target_ip) {
    const netif_t *iface;
    uint8_t frame[42];
    uint32_t i;
    if (!r || !target_mac || target_ip == 0) return -1;
    iface = netif_at(r->ifindex);
    if (!iface || iface->ipv4 == 0) return -1;
    for (i = 0; i < 6; i++) frame[i] = target_mac[i];
    for (i = 0; i < 6; i++) frame[6 + i] = r->mac[i];
    store_be16(frame + 12, ETH_TYPE_ARP);
    store_be16(frame + 14, 1);
    store_be16(frame + 16, ETH_TYPE_IPV4);
    frame[18] = 6;
    frame[19] = 4;
    store_be16(frame + 20, 2);
    for (i = 0; i < 6; i++) frame[22 + i] = r->mac[i];
    store_be32(frame + 28, iface->ipv4);
    for (i = 0; i < 6; i++) frame[32 + i] = target_mac[i];
    store_be32(frame + 38, target_ip);
    return rtl_transmit(r, frame, sizeof(frame));
}

static int rtl_send_arp_request(rtl8139_t *r, uint32_t target_ip) {
    const netif_t *iface;
    uint8_t frame[42];
    uint32_t i;
    if (!r || target_ip == 0) return -1;
    iface = netif_at(r->ifindex);
    if (!iface || iface->ipv4 == 0) return -1;
    for (i = 0; i < 6; i++) frame[i] = 0xFF;
    for (i = 0; i < 6; i++) frame[6 + i] = r->mac[i];
    store_be16(frame + 12, ETH_TYPE_ARP);
    store_be16(frame + 14, 1);
    store_be16(frame + 16, ETH_TYPE_IPV4);
    frame[18] = 6;
    frame[19] = 4;
    store_be16(frame + 20, 1);
    for (i = 0; i < 6; i++) frame[22 + i] = r->mac[i];
    store_be32(frame + 28, iface->ipv4);
    for (i = 0; i < 6; i++) frame[32 + i] = 0;
    store_be32(frame + 38, target_ip);
    return rtl_transmit(r, frame, sizeof(frame));
}

static void rtl_handle_arp(rtl8139_t *r, const uint8_t *packet, uint32_t frame_len) {
    const netif_t *iface;
    uint16_t op;
    uint32_t sender_ip;
    uint32_t target_ip;
    if (!r || !packet || frame_len < 42) return;
    if (load_be16(packet + 14) != 1 ||
        load_be16(packet + 16) != ETH_TYPE_IPV4 ||
        packet[18] != 6 ||
        packet[19] != 4) return;
    op = load_be16(packet + 20);
    sender_ip = load_be32(packet + 28);
    target_ip = load_be32(packet + 38);
    (void)net_arp_learn(r->ifindex, sender_ip, packet + 22);
    iface = netif_at(r->ifindex);
    if (op == 1 && iface && target_ip == iface->ipv4) {
        (void)rtl_send_arp_reply(r, packet + 22, sender_ip);
    }
}

static int rtl_send(void *ctx, const void *frame, uint32_t len) {
    rtl8139_t *r = (rtl8139_t *)ctx;
    uint32_t slot;
    uint32_t tx_len = len;
    uint8_t *tx;
    uint8_t dst_mac[6];
    uint32_t i;
    if (!r || !r->present || !frame || len == 0 || len + 14 > TX_BUF_LEN) return -1;
    slot = r->tx_cur & 3U;
    tx = r->tx_buf[slot];
    if ((((const uint8_t *)frame)[0] >> 4) == 4) {
        uint32_t dst_ip = 0;
        uint32_t target_ip = 0;
        uint32_t route_if = 0;
        uint32_t gateway = 0;
        int arp_hit = 0;
        for (i = 0; i < 6; i++) dst_mac[i] = 0xFF;
        if (len < 20) return -1;
        dst_ip = load_be32(((const uint8_t *)frame) + 16);
        target_ip = dst_ip;
        if (net_route_lookup4(dst_ip, &route_if, &gateway) == 0 &&
            route_if == r->ifindex &&
            gateway != 0) target_ip = gateway;
        arp_hit = net_arp_lookup(r->ifindex, target_ip, dst_mac) == 0;
        if (!arp_hit) {
            (void)rtl_send_arp_request(r, target_ip);
            return -1;
        }
        for (i = 0; i < 6; i++) tx[i] = dst_mac[i];
        for (i = 0; i < 6; i++) tx[6 + i] = r->mac[i];
        tx[12] = (uint8_t)(ETH_TYPE_IPV4 >> 8);
        tx[13] = (uint8_t)(ETH_TYPE_IPV4 & 0xFF);
        mmio_copy(tx + 14, (const uint8_t *)frame, len);
        tx_len = len + 14;
    } else {
        mmio_copy(tx, (const uint8_t *)frame, len);
    }
    outl((uint16_t)(r->io + REG_TSAD0 + slot * 4), (uint32_t)r->tx_buf[slot]);
    outl((uint16_t)(r->io + REG_TSD0 + slot * 4), tx_len);
    r->tx_cur++;
    return (int)len;
}

static void rtl_poll(void *ctx) {
    rtl8139_t *r = (rtl8139_t *)ctx;
    uint16_t isr;
    uint32_t budget = 8;
    if (!r || !r->present) return;
    isr = inw((uint16_t)(r->io + REG_ISR));
    if (isr) outw((uint16_t)(r->io + REG_ISR), isr);
    while (budget-- && !(inb((uint16_t)(r->io + REG_CR)) & CR_BUFE)) {
        uint32_t off = r->cur_rx % RX_BUF_LEN;
        uint16_t status = rx_read16(r, off);
        uint16_t len = rx_read16(r, off + 2);
        uint8_t packet[NET_FRAME_MAX];
        if ((status & 0x0001) && len >= 4 && len <= NET_FRAME_MAX + 4) {
            uint32_t frame_len = (uint32_t)len - 4;
            rx_copy(r, off + 4, packet, frame_len);
            if (frame_len > 14 &&
                packet[12] == (uint8_t)(ETH_TYPE_IPV4 >> 8) &&
                packet[13] == (uint8_t)(ETH_TYPE_IPV4 & 0xFF)) {
                (void)net_receive_from_driver(r->ifindex, packet + 14, frame_len - 14);
            } else if (frame_len >= 42 &&
                       packet[12] == (uint8_t)(ETH_TYPE_ARP >> 8) &&
                       packet[13] == (uint8_t)(ETH_TYPE_ARP & 0xFF)) {
                rtl_handle_arp(r, packet, frame_len);
            }
        }
        r->cur_rx = (r->cur_rx + len + 4 + 3) & ~3U;
        r->cur_rx %= RX_BUF_LEN;
        outw((uint16_t)(r->io + REG_CAPR), (uint16_t)(r->cur_rx - 16));
    }
}

void rtl8139_init(void) {
    const pci_device_t *dev = pci_find_vendor_device(RTL_VENDOR, RTL_DEVICE);
    uint32_t bar;
    uint32_t timeout;
    uint32_t i;
    int ifindex;
    if (!dev) return;
    bar = dev->bar[0];
    if ((bar & 1U) == 0) return;
    g_rtl.io = (uint16_t)(bar & ~3U);
    g_rtl.cur_rx = 0;
    g_rtl.tx_cur = 0;
    g_rtl.present = 0;
    pci_enable_busmaster(dev);
    outb((uint16_t)(g_rtl.io + REG_CONFIG1), 0x00);
    outb((uint16_t)(g_rtl.io + REG_CR), CR_RST);
    timeout = 100000;
    while (timeout-- && (inb((uint16_t)(g_rtl.io + REG_CR)) & CR_RST)) {}
    if (inb((uint16_t)(g_rtl.io + REG_CR)) & CR_RST) return;
    for (i = 0; i < 6; i++) g_rtl.mac[i] = inb((uint16_t)(g_rtl.io + REG_IDR0 + i));
    for (i = 0; i < sizeof(g_rtl.rx_buf); i++) g_rtl.rx_buf[i] = 0;
    for (i = 0; i < 4; i++) {
        uint32_t j;
        for (j = 0; j < TX_BUF_LEN; j++) g_rtl.tx_buf[i][j] = 0;
    }
    outl((uint16_t)(g_rtl.io + REG_RBSTART), (uint32_t)g_rtl.rx_buf);
    outw((uint16_t)(g_rtl.io + REG_IMR), 0x0000);
    outl((uint16_t)(g_rtl.io + REG_RCR),
         RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_WRAP | RCR_MXDMA_UNLIMITED);
    outl((uint16_t)(g_rtl.io + REG_TCR), 0x00000600U);
    outb((uint16_t)(g_rtl.io + REG_CR), CR_RE | CR_TE);
    ifindex = net_register_if("eth0", g_rtl.mac, 0x0A00020FU, 1500,
                              rtl_send, rtl_poll, &g_rtl);
    if (ifindex < 0) return;
    g_rtl.ifindex = (uint32_t)ifindex;
    g_rtl.present = 1;
    driver_register("rtl8139-net", DRIVER_BUS_NET,
                    ((uint32_t)dev->vendor_id << 16) | dev->device_id,
                    g_rtl.io);
    debug_puts("[rtl8139] eth0 up io=");
    debug_hex32(g_rtl.io);
    debug_puts("\n");
}
