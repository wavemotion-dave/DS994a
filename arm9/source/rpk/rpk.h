// =====================================================================================
// Copyright (c) 2023-2024 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The DS994a emulator is offered as-is, without any warranty.
//
// Please see the README.md file as it contains much useful info.
// =====================================================================================

#ifndef RPK_H
#define RPK_H


#define MAX_XML_ROMS     6
#define MAX_XML_SOCKETS  6

#define PCB_NONE            0x00
#define PCB_STANDARD        0x01
#define PCB_PAGED           0x02
#define PCB_GROMEMU         0x03
#define PCB_PAGED377        0x04
#define PCB_PAGED378        0x05
#define PCB_PAGED379i       0x06
#define PCB_MBX             0x07
#define PCB_MINIMEM         0x08
#define PCB_PAGEDCRU        0x09
#define PCB_SUPER           0x0A
#define PCB_PAGED7          0x0B

#define TYPE_NONE           0x00
#define TYPE_ROM_SOCKET     0x01
#define TYPE_ROM2_SOCKET    0x02
#define TYPE_GROM_SOCKET    0x03
#define TYPE_RAM_SOCKET     0x04

typedef struct
{
    char rom_file[64];
    char rom_id[32];
} ROM_t;

typedef struct
{
    char socket_uses[32];
    char socket_id[32];
} Socket_t;

typedef struct
{
    u8       pcb;
    ROM_t    roms[MAX_XML_ROMS];
    u8       num_roms;
    Socket_t sockets[MAX_XML_SOCKETS];
    u8       num_sockets;
    char     listname[64];
} Layout_t;

extern Layout_t cart_layout;

u8 rpk_load(const char* filename);
char *rpk_get_pcb_name(void);

#endif

