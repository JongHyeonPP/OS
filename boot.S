/* Multiboot flags:
 *   bit 0 = align modules on page boundaries
 *   bit 1 = provide memory map
 *   bit 2 = request video mode info (provides framebuffer address)
 */
.set ALIGN,    1 << 0
.set MEMINFO,  1 << 1
.set VIDINFO,  1 << 2
.set FLAGS,    ALIGN | MEMINFO | VIDINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
/* Reserved fields (header_addr etc. -- only needed when bit 16 set) */
.long 0   /* header_addr     */
.long 0   /* load_addr       */
.long 0   /* load_end_addr   */
.long 0   /* bss_end_addr    */
.long 0   /* entry_addr      */
/* Video mode: 0 = linear graphics, 1 = EGA text */
.long 0   /* mode_type  = linear framebuffer */
.long 800 /* width              */
.long 600 /* height             */
.long 32  /* depth (bpp)        */

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    /* Save multiboot magic and info pointer before clobbering registers */
    mov $stack_top, %esp
    /* eax = 0x2BADB002, ebx = pointer to multiboot_info_t */
    push %ebx   /* multiboot info ptr */
    push %eax   /* magic */
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

.size _start, . - _start
