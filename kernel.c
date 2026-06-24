#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "pmm.h"
#include "heap.h"
#include "paging.h"
#include "keyboard.h"
#include "mouse.h"
#include "vga.h"
#include "gui.h"
#include "task.h"
#include "syscall.h"
#include "debug.h"
#include "tty.h"
#include "vfs.h"
#include "process.h"
#include "driver.h"
#include "pci.h"
#include "block.h"
#include "ata.h"
#include "simplefs.h"
#include "net.h"
#include "rtl8139.h"
#include "shell.h"

/* Kernel heap: 512KB static buffer */
static uint8_t kheap_buf[512 * 1024];
static uint8_t kernel_ring0_stack[8192];

/* Multiboot1 info structure — full layout per spec */
typedef struct {
    uint32_t flags;             /* 0  */
    uint32_t mem_lower;         /* 4  */
    uint32_t mem_upper;         /* 8  */
    uint32_t boot_device;       /* 12 */
    uint32_t cmdline;           /* 16 */
    uint32_t mods_count;        /* 20 */
    uint32_t mods_addr;         /* 24 */
    uint32_t syms[4];           /* 28–43 */
    uint32_t mmap_length;       /* 44 */
    uint32_t mmap_addr;         /* 48 */
    uint32_t drives_length;     /* 52 */
    uint32_t drives_addr;       /* 56 */
    uint32_t config_table;      /* 60 */
    uint32_t boot_loader_name;  /* 64 */
    uint32_t apm_table;         /* 68 */
    uint32_t vbe_control_info;  /* 72 */
    uint32_t vbe_mode_info;     /* 76 */
    uint16_t vbe_mode;          /* 80 */
    uint16_t vbe_interface_seg; /* 82 */
    uint16_t vbe_interface_off; /* 84 */
    uint16_t vbe_interface_len; /* 86 */
    uint64_t framebuffer_addr;  /* 88 — 64-bit LFB address */
    uint32_t framebuffer_pitch; /* 96 */
    uint32_t framebuffer_width; /* 100 */
    uint32_t framebuffer_height;/* 104 */
    uint8_t  framebuffer_bpp;   /* 108 */
    uint8_t  framebuffer_type;  /* 109 */
} __attribute__((packed)) mb_info_t;

/* EAX = magic (0x2BADB002), EBX = pointer to mb_info_t */
void kernel_main(uint32_t mb_magic, uint32_t mb_info_addr) {
    uint32_t pmm_start = 0x800000U;
    uint32_t pmm_size = 8U * 1024U * 1024U;
    debug_init();
    debug_puts("[1] kernel_main start\n");

    /* Use GRUB-provided framebuffer if available (avoids BGA mode switch) */
    if (mb_magic == 0x2BADB002U && mb_info_addr) {
        mb_info_t *mbi = (mb_info_t *)mb_info_addr;
        debug_puts("[1a] MB flags="); debug_hex32(mbi->flags); debug_puts("\n");
        if (mbi->flags & 1U) {
            uint32_t total_bytes;
            if (mbi->mem_upper > ((0xFFFFFFFFU / 1024U) - 1024U)) {
                total_bytes = 0xFFFFFFFFU;
            } else {
                total_bytes = (1024U + mbi->mem_upper) * 1024U;
            }
            if (total_bytes > 64U * 1024U * 1024U) total_bytes = 64U * 1024U * 1024U;
            if (total_bytes > pmm_start) pmm_size = total_bytes - pmm_start;
        }
        if (mbi->flags & (1 << 12)) {
            debug_puts("[1b] FB: addr="); debug_hex32((uint32_t)mbi->framebuffer_addr);
            debug_puts(" pitch="); debug_hex32(mbi->framebuffer_pitch);
            debug_puts(" w="); debug_hex32(mbi->framebuffer_width);
            debug_puts(" h="); debug_hex32(mbi->framebuffer_height);
            debug_puts(" bpp="); debug_hex32(mbi->framebuffer_bpp);
            debug_puts("\n");
            if (mbi->framebuffer_bpp == 32 &&
                mbi->framebuffer_width  == 800 &&
                mbi->framebuffer_height == 600 &&
                mbi->framebuffer_addr <= 0xFFFFFFFFULL) {
                vga_set_fb_override((uint32_t)mbi->framebuffer_addr,
                                    mbi->framebuffer_pitch);
                debug_puts("[1c] GRUB framebuffer override set!\n");
            }
        } else {
            debug_puts("[1b] No GRUB framebuffer (bit12 not set)\n");
        }
    }

    gdt_init();
    tss_set_kernel_stack((uint32_t)(kernel_ring0_stack + sizeof(kernel_ring0_stack)));
    debug_puts("[2] gdt_init done\n");

    idt_init();
    debug_puts("[3] idt_init done\n");

    /* Physical memory: start at 8MB, cap to the low identity-mapped range. */
    pmm_init(pmm_start, pmm_size);
    heap_init(kheap_buf, sizeof(kheap_buf));
    paging_init();
    debug_puts("[4] memory+paging init done\n");

    tty_init();
    vfs_init();
    vfs_mount_ramfs();
    process_init();
    driver_init();
    pci_init();
    block_init();
    ata_init();
    simplefs_init();
    net_init();
    rtl8139_init();
    shell_init();
    debug_puts("[4a] kernel services init done\n");

    keyboard_init();
    mouse_init();
    debug_puts("[5] input drivers init done\n");

    scheduler_init();
    timer_init(1000);  /* 1000 Hz timer */
    scheduler_set_preemption(1, 10);
    syscall_init();
    __asm__ volatile("sti");
    debug_puts("[6] interrupts + timer + scheduler enabled\n");

    vga_init();
    debug_puts("[7] vga_init done\n");

    gui_init();
    debug_puts("[8] entering gui_run\n");
    gui_run();
}
