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
#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fat.h>

#include "DS99.h"
#include "DS99_utils.h"
#include "SAMS.h"
#include "cpu/tms9918a/tms9918a.h"
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "cpu/sn76496/SN76496.h"

u8 *MemSAMS     __attribute__((section(".dtcm"))) = 0;     // Allocated to support 512K for DS-Lite and 1MB for DSi and above

SAMS theSAMS    __attribute__((section(".dtcm")));         // The entire state of the SAMS memory map handler

// ---------------------------------------------------------
// Allocate enough memory for a SAMS 512K (DS) or 1MB (DSi)
// ---------------------------------------------------------
void SAMS_Initialize(void)
{
    // Start with everything cleared out...
    memset(&theSAMS, 0x00, sizeof(theSAMS));
    
    // Only allocate the memory once...
    if (isDSiMode())
    {
        theSAMS.numBanks = 256;                         // 256 * 4K = 1024K
        if (MemSAMS == 0)
        {
            MemSAMS = malloc((theSAMS.numBanks) * 0x1000);  // Allocate the SAMS memory 
        }
    }
    else
    {
        theSAMS.numBanks = 128;                         // 128 * 4K = 512K
        if (MemSAMS == 0)
        {
            MemSAMS = malloc((theSAMS.numBanks) * 0x1000);  // Allocate the SAMS memory 
        }
    }
    
    // --------------------------------------------------------------
    // For SAMS memory, ensure everything points to the right banks
    // and default the system to pass-thru mode...
    // --------------------------------------------------------------
    theSAMS.cruSAMS[0] = 0;
    theSAMS.cruSAMS[1] = 0;
    
    // For each bank... set the default memory banking pointers 
    for (u8 i=0; i<16; i++)
    {
        theSAMS.bankMapSAMS[i] = i;
        theSAMS.memoryPtr[i] = MemSAMS + (i * 0x1000);
    }
    
    // We don't map the MemType[] here.. only when CRU bit is written
    memset(MemSAMS, 0x00, ((theSAMS.numBanks) * 0x1000));
    
    // -----------------------------------------------------------------
    // If we are configured for SAMS operation... set the accuracy flag 
    // to map in slower (but more accurate) mapping.
    // -----------------------------------------------------------------
    if (myConfig.machineType == MACH_TYPE_SAMS)
    {
        TMS9900_SetAccurateEmulationFlag(ACCURATE_EMU_SAMS);
        SAMS_cru_write(0,0);    // Swap out the DSR
        SAMS_cru_write(1,0);    // Mapper Disabled... (pass-thru mode)
    }
}


// --------------------------------------------------------------------------------------
// SAMS memory bank swapping will point into the 4K region of the large SAMS memory pool
// --------------------------------------------------------------------------------------
const u8 IsSwappableSAMS[16] = {0,0,1,1,0,0,0,0,0,0,1,1,1,1,1,1};

inline void SAMS_SwapBank(u8 memory_region, u8 bank)
{
    if (IsSwappableSAMS[(memory_region&0xF)])    // Make sure this is an area we allow swapping...
    {
        theSAMS.memoryPtr[memory_region] = MemSAMS + ((u32)bank * 0x1000);
    }
}

// -------------------------------------------------------------------------------------------
// The SAMS banks are 4K and we only allow mapping of the banks at >2000-3FFF and >A000-FFFF
// -------------------------------------------------------------------------------------------
void SAMS_WriteBank(u16 address, u8 data)
{
    if (theSAMS.cruSAMS[0] == 1)    // Do we have access to the registers?
    {
        u8 memory_region = (address >> 1) & 0xF;

        if (theSAMS.cruSAMS[1] == 1)    // If the mapper is enabled, swap banks
        {
            SAMS_SwapBank(memory_region, data);
        }

        // Set this as the new bank for that memory region
        theSAMS.bankMapSAMS[memory_region] = data;
    }
}

// ---------------------------------------------------------------------
// The SAMS CRU is at CRU base >1E00 and has only 2 bits.. the first
// turns on the DSR at >4000 and the second enables the mapping vs
// "pass-thru" of the memory. In "pass-thru" we end up looking just
// like a normal 32K expanded memory system.
// ---------------------------------------------------------------------
void SAMS_cru_write(u16 cruAddress, u8 dataBit)
{
    // -----------------------------------------------------------
    // If the machine has been configured for SAMS operation...
    // -----------------------------------------------------------
    if (myConfig.machineType == MACH_TYPE_SAMS)
    {
        theSAMS.cruSAMS[cruAddress & 1] = dataBit;
        if (cruAddress & 1)    // If we are writing the mapper enabled bit...
        {
            if (theSAMS.cruSAMS[1] == 1)  // If the mapper is enabled...
            {
                SAMS_SwapBank(0x02, theSAMS.bankMapSAMS[0x2]);
                SAMS_SwapBank(0x03, theSAMS.bankMapSAMS[0x3]);
                SAMS_SwapBank(0x0A, theSAMS.bankMapSAMS[0xA]);
                SAMS_SwapBank(0x0B, theSAMS.bankMapSAMS[0xB]);
                SAMS_SwapBank(0x0C, theSAMS.bankMapSAMS[0xC]);
                SAMS_SwapBank(0x0D, theSAMS.bankMapSAMS[0xD]);
                SAMS_SwapBank(0x0E, theSAMS.bankMapSAMS[0xE]);
                SAMS_SwapBank(0x0F, theSAMS.bankMapSAMS[0xF]);
            }
            else    // Pass-thru mode - map the lower 32K in transparently...
            {
                SAMS_SwapBank(0x02, 0x2);
                SAMS_SwapBank(0x03, 0x3);
                SAMS_SwapBank(0x0A, 0xA);
                SAMS_SwapBank(0x0B, 0xB);
                SAMS_SwapBank(0x0C, 0xC);
                SAMS_SwapBank(0x0D, 0xD);
                SAMS_SwapBank(0x0E, 0xE);
                SAMS_SwapBank(0x0F, 0xF);
            }
        }
        else // We are dealing with the DSR enabled bit
        {
            SAMS_MapDSR(dataBit);
        }
    }
}


u8 SAMS_cru_read(u16 cruAddress)
{
    // -----------------------------------------------------------
    // If the machine has been configured for SAMS operation...
    // -----------------------------------------------------------
    if (myConfig.machineType == MACH_TYPE_SAMS)
    {
        return theSAMS.cruSAMS[cruAddress & 1];
    }
    return 1;
}

// ------------------------------------------------------------------
// Map the SAMS DSR in/out at address 0>4000 which is shared
// with the Disk Controller (and other periprhals in the future)
// ------------------------------------------------------------------
void SAMS_MapDSR(u8 dataBit)
{
    if (dataBit == 1) // Mapping DSR in
    {
        for (u16 address = 0x4000; address < 0x4020; address += 16)
        {
            MemType[address>>4] = MF_SAMS;    // SAMS expanded memory handling
        }
    }
    else // Mapping DSR out
    {
        for (u16 address = 0x4000; address < 0x4020; address += 16)
        {
            MemType[address>>4] = MF_UNUSED;    // Map back to original handling (unused)
        }
    }
}

// --------------------------------------------------------------------------------------------------
// These 32-bit read/write functions are used only for the Load/Save state handlers in savegame.c 
// and are mainly needed so we can do simple Run-Length-Encoding (RLE) on the big SAMS memory area.
// --------------------------------------------------------------------------------------------------
u32 SAMS_Read32(u32 address)
{
    u32* ptr = (u32*)MemSAMS;
    return ptr[address>>2];
}

void SAMS_Write32(u32 address, u32 data)
{
    u32* ptr = (u32*)MemSAMS;
    ptr[address>>2] = data;
}


// End of file

