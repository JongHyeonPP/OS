.RECIPEPREFIX := >

CC      := gcc
LD      := ld
CFLAGS  := -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector \
            -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
            -fno-unwind-tables -Iinclude -std=gnu99
ASFLAGS := -m32
LIBGCC  := $(shell gcc -m32 -print-libgcc-file-name)
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

C_SRCS  := kernel.c src/gdt.c src/idt.c src/pic.c src/vga.c \
            src/keyboard.c src/mouse.c \
            src/gui.c src/gui_overlays.c src/gui_draw.c \
            src/gui_apps1.c src/gui_apps2.c src/gui_apps3.c src/gui_apps4.c \
            src/gui_events.c \
            src/timer.c src/pmm.c src/heap.c src/paging.c \
            src/datetime.c src/runtime_info.c \
            src/task.c src/syscall.c src/debug.c src/tty.c src/uts.c src/vfs.c \
            src/elf.c src/process.c src/usermode.c src/driver.c src/pci.c \
            src/block.c src/ata.c src/simplefs.c src/net.c src/rtl8139.c \
            src/shell.c src/selftest.c
AS_SRCS := boot.S src/idt_stubs.S src/task_switch.S
OBJS    := $(C_SRCS:.c=.o) $(AS_SRCS:.S=.o)
DEPS    := $(C_SRCS:.c=.d)

.PHONY: all clean run run-serial

all: myos.iso

%.o: %.c
>$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

%.o: %.S
>$(CC) $(ASFLAGS) -c $< -o $@

myos.bin: $(OBJS) linker.ld
>$(LD) $(LDFLAGS) $(OBJS) $(LIBGCC) -o myos.bin
>grub-file --is-x86-multiboot myos.bin
>@echo "OK: Multiboot verified"

myos.iso: myos.bin grub.cfg
>mkdir -p iso/boot/grub
>cp myos.bin iso/boot/myos.bin
>cp grub.cfg iso/boot/grub/grub.cfg
>grub-mkrescue -o myos.iso iso 2>/dev/null

run: myos.iso
>qemu-system-i386 -cdrom myos.iso -boot d -m 64M -vga std -serial stdio

run-serial: myos.iso
>qemu-system-i386 -cdrom myos.iso -boot d -m 64M -display none -serial stdio

clean:
>rm -rf $(OBJS) $(DEPS) myos.bin myos.iso iso

-include $(DEPS)
