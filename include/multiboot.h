#pragma once
#include "types.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

typedef struct {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;          /* 1 = usable RAM */
} __attribute__((packed)) mb_mmap_entry_t;

typedef struct {
    u32 flags;
    u32 mem_lower, mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count, mods_addr;
    u32 syms[4];
    u32 mmap_length, mmap_addr;
    u32 drives_length, drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
} __attribute__((packed)) multiboot_info_t;
