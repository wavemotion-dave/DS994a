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

u8 *MemSAMS             __attribute__((section(".dtcm"))) = 0;  // Allocated to support 512K for DS-Lite and 1MB/2MB for DSi and above
SAMS theSAMS            __attribute__((section(".dtcm")));      // The entire state of the SAMS memory map handler
u16 sams_highwater_bank __attribute__((section(".dtcm"))) = 0;  // To track how far into SAMS memory we have used

// ---------------------------------------------------------------------------------------
// SAMS is handled via the CRU and has registers mapped into the DSR space but it does
// not have a proper DSR. The two CRU bits used are:
//
// CRU >1E00  - Enable or Disable the visibility of the SAMS registers
// CRU >1E02  - Enable SAMS memory mapping or 'Pass-Thru' mode (acting like a traditional 32K expansion)
//
// If the SAMS registers are mapped in, there are 16 word-registers (covering 32 bytes) but
// only a subset are actually used to map in 4K memory banks into the TI-99 system:
//
// > 4000 - Banking for TI-99/4a Memory Range 0000-0FFF (not mappable)
// > 4002 - Banking for TI-99/4a Memory Range 1000-1FFF (not mappable)
// > 4004 - Banking for TI-99/4a Memory Range 2000-2FFF (mappable)
// > 4006 - Banking for TI-99/4a Memory Range 3000-3FFF (mappable)
// > 4008 - Banking for TI-99/4a Memory Range 4000-4FFF (not mappable)
// > 400A - Banking for TI-99/4a Memory Range 5000-5FFF (not mappable)
// > 400C - Banking for TI-99/4a Memory Range 6000-6FFF (not mappable)
// > 400E - Banking for TI-99/4a Memory Range 7000-7FFF (not mappable)
// > 4010 - Banking for TI-99/4a Memory Range 8000-8FFF (not mappable)
// > 4012 - Banking for TI-99/4a Memory Range 9000-9FFF (not mappable)
// > 4014 - Banking for TI-99/4a Memory Range A000-AFFF (mappable)
// > 4016 - Banking for TI-99/4a Memory Range B000-BFFF (mappable)
// > 4018 - Banking for TI-99/4a Memory Range C000-CFFF (mappable)
// > 401A - Banking for TI-99/4a Memory Range D000-DFFF (mappable)
// > 401C - Banking for TI-99/4a Memory Range E000-EFFF (mappable)
// > 401E - Banking for TI-99/4a Memory Range F000-FFFF (mappable)
//
// When these banking registers are written to, and the SAMS memory mapper is enabled 
// (via CRU bits), the SAMS card will swap in a 4K bank into one of these mappable
// regions above. For the 1MB or smaller cards, this only requires one byte (256 banks
// of 4K = 1024K of SAMS memory). For the > 1MB SAMS cards, it requires writing a WORD 
// where the high byte contains the 4K page within a 1MB chunk of SAMS and the low byte
// contains up to 4 extra bits of paging that allows the card to index beyond the first
// 1MB chunk of SAMS memory.
//
// This is due to the clever use of the LS612 chip that has 12 bits of latch capability
// for a total of 4096 pages of SAMS memory addressable which is 16MB of memory max!
// The LS612 chip will latch in the low byte - preserving those extra 4 bits until
// the high byte comes in - and it presents this as a full 12 bits to the mapper circuit.
// 
// Note: the DSi only has that much memory total and we need lots of memory for the 
// actual emulation core - so the best the DS994a can do is 8MB of SAMS memory. Still,
// that should be plenty for most real-world software (very little SW currently takes 
// advantage of the full 1MB of SAMS - let alone the extra memory beyond 1MB).
//
// One caveat is that the extra 4 latched bits for > 1MB memory have no provision to be
// read back - it wasn't designed into the > 1MB SAMS cards. So when someone reads the
// banking registers, they will only get the low order (page) byte repeated in both 
// the low byte and high byte.  So while it's possible on a 4MB card to page in the
// second 4K chunk at the 3MB boundary by writing >0103 to one of the memory regions
// above... when read back, you will simply get >0101 to indicate that the second page
// is mapped in. Programs like AMSTEST4 will actually ensure that a WORD readback of
// the page register mirrors the low and high bytes so it's best to emulate that quirk
// of hardware design correctly.
//
// Please see "SAMS registers explained_Srt_AP edits.rtf" in the techdocs area of this
// github page for an excellent set of examples for how this >1MB SAMS access works.
// ---------------------------------------------------------------------------------------

// ---------------------------------------------------------
// Setup for SAMS 512K (DS) or 1MB-8MB (DSi)
// ---------------------------------------------------------
void SAMS_Initialize(void)
{
    // Start with everything cleared out...
    memset(&theSAMS, 0x00, sizeof(theSAMS));

    // --------------------------------------------------------------
    // For SAMS memory, ensure everything points to the right banks
    // and default the system to pass-thru mode...
    // --------------------------------------------------------------
    theSAMS.cruSAMS[0] = 0;
    theSAMS.cruSAMS[1] = 0;

    // ------------------------------------------------------------------------
    // SAMS memory is bigger for the DSi where we have more room... See a bit
    // further below since with the DS-Lite/Phat we need to reduce the size
    // of the max cart buffer in order to support the 512K SAMS memory.
    // ------------------------------------------------------------------------
    if (isDSiMode())
    {
        switch (myConfig.machineType)
        {
            case MACH_TYPE_SAMS_1MB: theSAMS.numBanks = 256;   MAX_CART_SIZE = (8*1024*1024);  break;
            case MACH_TYPE_SAMS_2MB: theSAMS.numBanks = 512;   MAX_CART_SIZE = (8*1024*1024);  break;
            case MACH_TYPE_SAMS_4MB: theSAMS.numBanks = 1024;  MAX_CART_SIZE = (6*1024*1024);  break;
            case MACH_TYPE_SAMS_8MB: theSAMS.numBanks = 2048;  MAX_CART_SIZE = (2*1024*1024);  break;
            default:                 theSAMS.numBanks = 256;   MAX_CART_SIZE = (8*1024*1024);  break;
        }
        
        MemSAMS = SharedMemBufferBig+MAX_CART_SIZE;   // SAMS is always the back-end of this big buffer (Cart Image is the front end)
    }
    else
    {
        theSAMS.numBanks = 128; // On DSLite:  128 * 4K = 512K is the best we can support...
        MAX_CART_SIZE = ((myConfig.machineType != MACH_TYPE_NORMAL32K ? 256:512) * 1024);  // If we are DS-Lite/Phat, we reduce the size of the cart to support larger SAMS
    }

    // For each bank... set the default memory banking pointers
    for (u8 i=0; i<16; i++)
    {
        theSAMS.bankMapSAMS[i] = i;
        theSAMS.memoryPtr[i] = MemSAMS + (i * 0x1000);
    }

    // --------------------------------------------------------------------------
    // We don't map the MemType[] here.. only when CRU bit is written but we do
    // clear out the SAMS memory to all zeros (helps with savestate compression)
    // --------------------------------------------------------------------------
    memset(MemSAMS, 0x00, ((theSAMS.numBanks) * 0x1000));

    // -----------------------------------------------------------------
    // If we are configured for SAMS operation... set the accuracy flag
    // to map in slower (but more accurate) emulation handling.
    // -----------------------------------------------------------------
    if (myConfig.machineType >= MACH_TYPE_SAMS_1MB)
    {
        TMS9900_SetAccurateEmulationFlag(ACCURATE_EMU_SAMS);
        SAMS_cru_write(0,0);    // Swap out the visibility of the SAMS memory mapped registers (so they are not visible)
        SAMS_cru_write(1,0);    // Mapper Disabled... (pass-thru mode which is basically like having a 32K expansion)
    }

    sams_highwater_bank = 0x0000; // So we can track how much SAMS memory is being used (for debug use)
}


// --------------------------------------------------------------------------------------
// SAMS memory bank swapping will point into the 4K region of the large SAMS memory pool.
// We only allow mapping of SAMS 4K memory banks into the TI-99/4a memory map that would
// hold expansion RAM ... that is: >2000 and >A000 areas. We cannot map RAM into areas
// that hold the console ROMS, cart ROM or other peripherals.
// --------------------------------------------------------------------------------------
const u8 IsSwappableSAMS[16] = {0,0,1,1,0,0,0,0,0,0,1,1,1,1,1,1};

inline void SAMS_SwapBank(u8 memory_region, u16 bank)
{
    // -----------------------------------------------------------------------------------------
    // If the software tries to access a bank beyond the maximum configured, those upper bits 
    // are simply ignored and it will look like a 'mirror' of the SAMS memory in a lower region.
    // -----------------------------------------------------------------------------------------
    bank &= (theSAMS.numBanks - 1);

    if (IsSwappableSAMS[memory_region])    // Make sure this is an area we allow swapping... (memory_region is already masked to lower 4 bits)
    {
        theSAMS.memoryPtr[memory_region] = MemSAMS + ((u32)bank * 0x1000);
        if (bank > sams_highwater_bank) sams_highwater_bank = bank;
    }
}

// -------------------------------------------------------------------------------------------
// The SAMS banks are 4K and we only allow mapping of the banks at >2000-3FFF and >A000-FFFF
// Traditional SAMS cards only go to 1MB and so there are 256 banks of 4K that can be mapped.
// The larger than 1MB SAMS cards have a latch that can hold additional high-order bits that
// are preserved on a WORD write to the bank and allows the larger SAMS cards to essentially
// bank in different 1MB chunks of pages. In the techdocs folder, I've placed the best SAMS
// register reference I've found with clear examples of how these banking registers work.
// -------------------------------------------------------------------------------------------
void SAMS_WriteBank(u16 address, u16 data)
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

// ----------------------------------------------------------------------------------------
// Return the current bank mapped at a particular address. SAMS will return the same 1MB
// banked value in both the upper and lower byte of a WORD read even if the SAMS card has
// more than 1MB of memory. Basically the LS612 hardware latch on the larger boards handles
// (and latches) the high-order bits on a write but does not handle them on a read-back. 
// So the board never returns the high order bits in the bank on the > 1MB SAMS cards.
// ----------------------------------------------------------------------------------------
u16 SAMS_ReadBank(u16 address)
{
    u16 retVal = theSAMS.bankMapSAMS[(address & 0x1E) >> 1] & 0xFF; // Only the lower 256 page number
    return (retVal << 8) | retVal;                                  // Returned in both upper and lower bytes
}

// ---------------------------------------------------------------------
// The SAMS CRU is at CRU base >1E00 and has only 2 bits.. the first
// turns on the visibility of the SAMS register map at >4000 and the
// second enables mapping vs "pass-thru" of the memory. In "pass-thru"
// we end up looking just like a normal 32K expanded memory system.
// ---------------------------------------------------------------------
void SAMS_cru_write(u16 cruAddress, u8 dataBit)
{
    // -----------------------------------------------------------
    // If the machine has been configured for SAMS operation...
    // -----------------------------------------------------------
    if (myConfig.machineType >= MACH_TYPE_SAMS_1MB)
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
        else // We are dealing with the enabled bit (there is no DSR for the SAMS, but it's more a card enable so that registers are visible and can be written to)
        {
            SAMS_EnableDisable(dataBit);
        }
    }
}

// -----------------------------------------------------------
// It's unclear if SAMS hardware allows the readback of the
// CRU bits... but it doesn't hurt to provide the capability.
// -----------------------------------------------------------
u8 SAMS_cru_read(u16 cruAddress)
{
    // -----------------------------------------------------------
    // If the machine has been configured for SAMS operation...
    // -----------------------------------------------------------
    if (myConfig.machineType >= MACH_TYPE_SAMS_1MB)
    {
        return theSAMS.cruSAMS[cruAddress & 1];
    }
    return 1;
}

// ------------------------------------------------------------------
// Map the SAMS registers in/out at address 0>4000 which is shared
// with the Disk Controller (and other periprhals in the future)
// Note: SAMS does not have a traditional DSR rom - so this CRU
// bit is really more like a SAMS enable/disable as we are just
// enabling the memory mapped registers here (no ROM is swapped).
// ------------------------------------------------------------------
void SAMS_EnableDisable(u8 dataBit)
{
    if (dataBit == 1) // Mapping SAMS card in
    {
        for (u16 address = 0x4000; address < 0x4020; address += 16)
        {
            MemType[address>>4] = MF_SAMS;    // SAMS expanded memory handling
        }
    }
    else // Mapping the SAMS card out
    {
        for (u16 address = 0x4000; address < 0x4020; address += 16)
        {
            MemType[address>>4] = MF_PERIF;   // Map back to original handling (peripheral ROM)
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
