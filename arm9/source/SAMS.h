// =====================================================================================
// Copyright (c) 2023-2024 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The DS994a emulator is offered as-is, without any warranty.
// =====================================================================================
#ifndef _SAMS_H_
#define _SAMS_H_

#include <nds.h>
#include <string.h>

// ----------------------------------------------------------------------------
// The entire state of the SAMS memory expansion card for easy access
// ----------------------------------------------------------------------------
typedef struct _SAMS
{
    u16     numBanks;           // The number of 4K banks available (128 for 512K or 256 for full 1MB SAMS)
    u8      cruSAMS[2];         // The CRU bits for SAMS handling (only two of them!)
    u8      bankMapSAMS[16];    // What banks are currently mapped in
    u8     *memoryPtr[16];      // Where do the 16 regions of 4K point to
} SAMS;

extern SAMS theSAMS;

extern u8 *MemSAMS;
extern u8 sams_highwater_bank;

extern void SAMS_Initialize(void);
extern void SAMS_WriteBank(u16 address, u8 data);
extern u8   SAMS_ReadBank(u16 address);
extern u8   SAMS_cru_read(u16 cruAddress);
extern void SAMS_cru_write(u16 cruAddress, u8 dataBit);
extern void SAMS_MapDSR(u8 dataBit);
extern u32  SAMS_Read32(u32 address);
extern void SAMS_Write32(u32 address, u32 data);
    
#endif
