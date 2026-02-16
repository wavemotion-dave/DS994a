// =====================================================================================
// Copyright (c) 2023-2026 Dave Bernazzani (wavemotion-dave)
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
#include <ctype.h>
#include <fat.h>
#include <dirent.h>
#include "printf.h"
#include "DS99.h"
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "cpu/tms9918a/tms9918a.h"
#include "pcode.h"

// ------------------------------------------------------------------------------------------
// The p-code card has 12K of DSR where the first 4K is fixed and the second 4K is banked.
// The p-code card has 64K of internal GROM which is not accessed via the normal system
// GROM read/write addresses but is instead mapped to some hotspot addresses within the DSR.
// ------------------------------------------------------------------------------------------

u8  pCodeEmulation __attribute__((section(".dtcm")))  = 0;     // Default to no p-code card emulation. Will be set '1' only if the user picks the p-code 'cart' and matching 64K special internal GROM.
u8  pcode_bank = 0;
u8  pcode_visible = 0;
u16 pcode_gromAddress = 0x0000;
u8  pcode_gromWriteLoHi = 0;
u8  pcode_gromReadLoHi = 0;

// ------------------------------------------------------
// Set the p-code system to start on bank 0, not visible
// and initialize the internal GROM counters.
// ------------------------------------------------------
void pcode_init(void)
{
    pcode_bank = 0;
    pcode_visible = 0;
    pcode_gromAddress = 0x0000;
    pcode_gromWriteLoHi = 0;
    pcode_gromReadLoHi = 0;    
}

// ------------------------------------------------------
// The p-code card does not support CRU readback so we
// simply return 0 if anyone should try to read them.
// ------------------------------------------------------
u8 pcode_cru_read(u16 address)
{
    return 0;
}

// ---------------------------------------------------------------
// The p-code system responds mainly to two CRU bits...
//
// >1F00 is the enable/disable of the DSR
// >1F80 is the banking bit to swap out the upper 4K of the DSR.
//
// There is also a special LED bit that we don't bother to handle.
// ---------------------------------------------------------------
void pcode_cru_write(u16 address, u8 data)
{
    if (!pCodeEmulation) return; // If we are not p-code emulation enabled, do nothing
    
    address &= 0x007F;
    
    if (address == 0) // Is this the enable/disable bit?
    {
        if (data)
        {
            // The P-Code DSR is visible
            memcpy(&MemCPU[0x4000], MemCART, 0x1000);                                       // This is the fixed 4K bank 
            memcpy(&MemCPU[0x5000], MemCART + (0x1000 + (0x1000 * pcode_bank)), 0x1000);    // Make sure the right 4K bank is in place
            MemType[0x5BFC>>4] = MF_PCODE;
            MemType[0x5FFC>>4] = MF_PCODE;
            pcode_visible = 1;
        }
        else
        {
            // The P-Code DSR is not visible
            memset(&MemCPU[0x4000], 0xFF, 0x2000);
            MemType[0x5BFC>>4] = MF_PERIF;
            MemType[0x5FFC>>4] = MF_PERIF;
            pcode_visible = 0;
        }
        
    }
    else if (address == 0x40) // Is this the DSR banking bit? (which responds to >1F80 due to p-code card logic... by the time it gets here it's weight >40)
    {
        pcode_bank = (data & 1);
        if (pcode_visible)
        {
            memcpy(&MemCPU[0x5000], MemCART + (0x1000 + (0x1000 * pcode_bank)), 0x1000);    // Make sure the right 4K bank is in place
        }
    }
}

// ----------------------------------------------------------
// Special internal GROMs map into some addresses of the 
// internal DSR as follows:
//
//  >5BFC (read data), >5BFE (read address)
//  >5FFC (write data, not used) and >5FFE (write address)
// ----------------------------------------------------------
void pcode_dsr_write(u16 address, u8 data)
{
    // Are we writing the internal GROM address?
    if (address == 0x5FFE)
    {
        pcode_gromReadLoHi = 0;

        if (pcode_gromWriteLoHi) // Low byte
        {
            pcode_gromAddress = (pcode_gromAddress & 0xff00) | data;
            pcode_gromWriteLoHi = 0;
        }
        else // High byte
        {
            pcode_gromAddress = (pcode_gromAddress & 0x00ff) | (data << 8);
            pcode_gromWriteLoHi = 1;
        }
    }
    else
    {
        // No other writes are supported...
    }
}

// ------------------------------------------------------------------------------------
// There are two special read addresses within the DSR rom space that will trigger the
// internal GROM read of data or GROM address. Otherwise we will return the normal 
// program memory in this DSR space. Apparently the p-code DSR carefully jumps around
// these two hotspots in the memory space.
// ------------------------------------------------------------------------------------
u8 pcode_dsr_read(u16 address)
{
    if (address == 0x5BFC)  // Read GROM Data
    {
        u8 data = MemCART[0x10000 + pcode_gromAddress];
        pcode_gromAddress = (pcode_gromAddress & 0xE000) | ((pcode_gromAddress+1) & 0x1FFF);        
        return data;
    }
    else if (address == 0x5BFE) // Read GROM address
    {
        u8 data;

        pcode_gromWriteLoHi = 0;

        if (pcode_gromReadLoHi) // Low byte
        {
            // Reads are always address + 1
            data = (u8)((pcode_gromAddress+1) & 0xFF);
            pcode_gromReadLoHi = 0;
        }
        else // High byte
        {
            // Reads are always address + 1 - be careful to not bump the high bits as that's our GROM select
            u16 addr = (pcode_gromAddress & 0xE000) | ((pcode_gromAddress+1) & 0x1FFF);
            data = (u8)(addr >> 8);
            pcode_gromReadLoHi = 1;
        }

        return data;
    }
    
    return MemCPU[address];
}

// End of file
