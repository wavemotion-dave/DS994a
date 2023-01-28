// =====================================================================================
// Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The TI99DS emulator is offered as-is, without any warranty.
//
// Bits of this code came from Clasic99 (C) Mike Brent who has graciously allowed
// me to use it to help with the core TMS9900 emualation. Please see Classic99 for
// the original CPU core and please adhere to the copyright wishes of Mike Brent
// for any code that is duplicated here.
// =====================================================================================

#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fat.h>
#include "tms9901.h"
#include "tms9900.h"
#include "../../DS99.h"
#include "../../disk.h"
#include "../../DS99_utils.h"
#include "../tms9918a/tms9918a.h"
#include "../sn76496/SN76496.h"
extern SN76496 sncol;

// -------------------------------------------------------------------
// These are all too big to fit into DTCM fast memory on the DS...
// -------------------------------------------------------------------
u8           MemCPU[0x10000];            // 64K of CPU MemCPU Space
u8           MemGROM[0x10000];           // 64K of GROM MemCPU Space
u8           MemCART[MAX_CART_SIZE];     // Cart C/D/8 memory up to 512K banked at >6000
u8           MemType[0x10000];           // Memory type for each address
u8           DiskDSR[0x2000];            // Memory for the DiskDSR to be mapped at >4000

u8           FastCartBuffer[0x2000] __attribute__((section(".dtcm")));     // We can speed up 8K carts... use .DTCM memory (even for multi-banks it will help with bank 0)

TMS9900 tms9900  __attribute__((section(".dtcm")));  // Put the entire TMS9900 set of registers and helper vars into fast .DTCM RAM on the DS

#define OpcodeLookup            ((u16*)0x06860000)   // We use 128K of semi-fast VDP memory to help with the OpcodeLookup[] lookup table
#define CompareZeroLookup16     ((u16*)0x06880000)   // We use 128K of semi-fast VDP memory to help with the CompareZeroLookup16[] lookup table
#define MemSAMS_fast            ((u8*)0x06820000)    // We use 128K of semi-fast VDP memory to help with the CompareZeroLookup16[] lookup table

u16 SAMS_BANKS  = 256;                  // 1MB of SAMS memory depending on DS vs DSi. See main() for allocation details.
u8  MemSAMS[(1024*1024) - (128*1024)];  // We use 128K of VRAM and the rest comes from this memory pool (1MB total)

#define AddCycleCount(x) (tms9900.cycles += (x))     // Our main way of bumping up the cycle counts during execution - each opcode handles their own timing increments

u16 MemoryRead16(u16 address);

char tmpFilename[256];

// Supporting banking up to the full 2MB (256 x 8KB = 2048KB)
u8 BankMasks[256];

// Pre-fill the parity table for fast look-up based on Classic99 'black magic'
u16 ParityTable[256]     __attribute__((section(".dtcm")));

// ---------------------------------------------------------------------------------
// And the Classic99 handling of compare-to-zero status bits for 8-bit instructions.
// Note this will only handle LAE and P - other bits to be set by instruction.
// This is small enough that we can place it into the fast .DTCM data memory.
// ---------------------------------------------------------------------------------
u16 CompareZeroLookup8[0x100] __attribute__((section(".dtcm")));


////////////////////////////////////////////////////////////////////////////
// CPU Opcode 02 helper function
////////////////////////////////////////////////////////////////////////////
void opcode02(u16 in)
{
    u16 x;

    x=(in&0x00e0)>>4;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_li;   break;
    case 2: OpcodeLookup[in]=op_ai;   break;
    case 4: OpcodeLookup[in]=op_andi; break;
    case 6: OpcodeLookup[in]=op_ori;  break;
    case 8: OpcodeLookup[in]=op_ci;   break;
    case 10:OpcodeLookup[in]=op_stwp; break;
    case 12:OpcodeLookup[in]=op_stst; break;
    case 14:OpcodeLookup[in]=op_lwpi; break;
    default: OpcodeLookup[in]=op_bad; break;
    }
}

////////////////////////////////////////////////////////////////////////////
// CPU Opcode 03 helper function
////////////////////////////////////////////////////////////////////////////
void opcode03(u16 in)
{
    u16 x;

    x=(in&0x00e0)>>4;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_limi; break;
    case 4: OpcodeLookup[in]=op_idle; break;
    case 6: OpcodeLookup[in]=op_rset; break;
    case 8: OpcodeLookup[in]=op_rtwp; break;
    case 10:OpcodeLookup[in]=op_ckon; break;
    case 12:OpcodeLookup[in]=op_ckof; break;
    case 14:OpcodeLookup[in]=op_lrex; break;
    default: OpcodeLookup[in]=op_bad; break;
    }
}

///////////////////////////////////////////////////////////////////////////
// CPU Opcode 04 helper function
///////////////////////////////////////////////////////////////////////////
void opcode04(u16 in)
{
    u16 x;

    x=(in&0x00c0)>>4;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_blwp; break;
    case 4: OpcodeLookup[in]=op_b;    break;
    case 8: OpcodeLookup[in]=op_x;    break;
    case 12:OpcodeLookup[in]=op_clr;  break;
    default: OpcodeLookup[in]=op_bad; break;
    }
}

//////////////////////////////////////////////////////////////////////////
// CPU Opcode 05 helper function
//////////////////////////////////////////////////////////////////////////
void opcode05(u16 in)
{
    u16 x;

    x=(in&0x00c0)>>4;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_neg;  break;
    case 4: OpcodeLookup[in]=op_inv;  break;
    case 8: OpcodeLookup[in]=op_inc;  break;
    case 12:OpcodeLookup[in]=op_inct; break;
    default: OpcodeLookup[in]=op_bad; break;
    }
}

////////////////////////////////////////////////////////////////////////
// CPU Opcode 06 helper function
////////////////////////////////////////////////////////////////////////
void opcode06(u16 in)
{
    u16 x;

    x=(in&0x00c0)>>4;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_dec;  break;
    case 4: OpcodeLookup[in]=op_dect; break;
    case 8: OpcodeLookup[in]=op_bl;   break;
    case 12:OpcodeLookup[in]=op_swpb; break;
    default: OpcodeLookup[in]=op_bad; break;
    }
}

////////////////////////////////////////////////////////////////////////
// CPU Opcode 07 helper function
////////////////////////////////////////////////////////////////////////
void opcode07(u16 in)
{
    u16 x;

    x=(in&0x00c0)>>4;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_seto; break;
    case 4: OpcodeLookup[in]=op_abs;  break;
    default: OpcodeLookup[in]=op_bad; break;
    }
}

////////////////////////////////////////////////////////////////////////
// CPU Opcode 1 helper function
////////////////////////////////////////////////////////////////////////
void opcode1(u16 in)
{
    u16 x;

    x=(in&0x0f00)>>8;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_jmp; break;
    case 1: OpcodeLookup[in]=op_jlt; break;
    case 2: OpcodeLookup[in]=op_jle; break;
    case 3: OpcodeLookup[in]=op_jeq; break;
    case 4: OpcodeLookup[in]=op_jhe; break;
    case 5: OpcodeLookup[in]=op_jgt; break;
    case 6: OpcodeLookup[in]=op_jne; break;
    case 7: OpcodeLookup[in]=op_jnc; break;
    case 8: OpcodeLookup[in]=op_joc; break;
    case 9: OpcodeLookup[in]=op_jno; break;
    case 10:OpcodeLookup[in]=op_jl;  break;
    case 11:OpcodeLookup[in]=op_jh;  break;
    case 12:OpcodeLookup[in]=op_jop; break;
    case 13:OpcodeLookup[in]=op_sbo; break;
    case 14:OpcodeLookup[in]=op_sbz; break;
    case 15:OpcodeLookup[in]=op_tb;  break;
    default: OpcodeLookup[in]=op_bad;break;
    }
}

////////////////////////////////////////////////////////////////////////
// CPU Opcode 2 helper function
////////////////////////////////////////////////////////////////////////
void opcode2(u16 in)
{
    u16 x;

    x=(in&0x0c00)>>8;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_coc; break;
    case 4: OpcodeLookup[in]=op_czc; break;
    case 8: OpcodeLookup[in]=op_xor; break;
    case 12:OpcodeLookup[in]=op_xop; break;
    default: OpcodeLookup[in]=op_bad;break;
    }
}

////////////////////////////////////////////////////////////////////////
// CPU Opcode 3 helper function
////////////////////////////////////////////////////////////////////////
void opcode3(u16 in)
{
    u16 x;

    x=(in&0x0c00)>>8;

    switch(x)
    {
    case 0: OpcodeLookup[in]=op_ldcr; break;
    case 4: OpcodeLookup[in]=op_stcr; break;
    case 8: OpcodeLookup[in]=op_mpy;  break;
    case 12:OpcodeLookup[in]=op_div;  break;
    default: OpcodeLookup[in]=op_bad; break;
    }
}


///////////////////////////////////////////////////////////////////////////
// CPU Opcode 0 helper function
///////////////////////////////////////////////////////////////////////////
void opcode0(u16 in)
{
    u16 x;

    x=(in&0x0f00)>>8;

    switch(x)
    {
    case 2:  opcode02(in);       break;
    case 3:  opcode03(in);       break;
    case 4:  opcode04(in);       break;
    case 5:  opcode05(in);       break;
    case 6:  opcode06(in);       break;
    case 7:  opcode07(in);       break;
    case 8:  OpcodeLookup[in]=op_sra; break;
    case 9:  OpcodeLookup[in]=op_srl; break;
    case 10: OpcodeLookup[in]=op_sla; break;
    case 11: OpcodeLookup[in]=op_src; break;
    default: OpcodeLookup[in]=op_bad; break;
    }
}
////////////////////////////////////////////////////////////////////////
// Fill the CPU Opcode Address table
// WARNING: called more than once, so be careful about anything you can't do twice!
////////////////////////////////////////////////////////////////////////
void TMS9900_buildopcodes(void)
{
    u16 in,x,z;
    unsigned int i;

    // -----------------------------------------------------
    // Build the streamined Opcode table... this will let
    // us take any TMS9900 16-bit Opcode and turn it into
    // a simple enum lookup for relatively blazing speed.
    // -----------------------------------------------------
    for (i=0; i<65536; i++)
    {
        in=(u16)i;

        x=(in&0xf000)>>12;
        switch(x)
        {
        case 0: opcode0(in);        break;
        case 1: opcode1(in);        break;
        case 2: opcode2(in);        break;
        case 3: opcode3(in);        break;
        case 4: OpcodeLookup[in]=op_szc; break;
        case 5: OpcodeLookup[in]=op_szcb;break;
        case 6: OpcodeLookup[in]=op_s;   break;
        case 7: OpcodeLookup[in]=op_sb;  break;
        case 8: OpcodeLookup[in]=op_c;   break;
        case 9: OpcodeLookup[in]=op_cb;  break;
        case 10:OpcodeLookup[in]=op_a;   break;
        case 11:OpcodeLookup[in]=op_ab;  break;
        case 12:OpcodeLookup[in]=op_mov; break;
        case 13:OpcodeLookup[in]=op_movb;break;
        case 14:OpcodeLookup[in]=op_soc; break;
        case 15:OpcodeLookup[in]=op_socb;break;
        default: OpcodeLookup[in]=op_bad;break;
        }
    }

    // ------------------------------------------
    // Create the Parity Table for fast look-up
    // ------------------------------------------
    for (i=0; i<256; i++)
    {
        u8 x = i;
        for (z=0; x; x&=(x-1)) z++;               // black magic?
        ParityTable[i] = (z&1) ? ST_OP : 0;
    }

    // build the Word status lookup table. This handles Logical, Arithmetic and Equal and
    // the other bits will be handled manually on a per-instruction basis...
    for (i=0; i<0x10000; i++)
    {
        CompareZeroLookup16[i]=0x0000;
        // LGT
        if (i>0) CompareZeroLookup16[i]|=ST_LGT;
        // AGT
        if ((i>0)&&(i<0x8000)) CompareZeroLookup16[i]|=ST_AGT;
        // EQ
        if (i==0) CompareZeroLookup16[i]|=ST_EQ;
    }

    // And now the Byte status lookup table. This handles Logical, Arithmetic and Equal plus Parity
    // and the other bits will be handled manually on a per-instruction basis...
    for (i=0; i<256; i++)
    {
        u8 x=(u8)(i&0xff);
        CompareZeroLookup8[i]=0;
        // LGT
        if (i>0) CompareZeroLookup8[i]|=ST_LGT;
        // AGT
        if ((i>0)&&(i<0x80)) CompareZeroLookup8[i]|=ST_AGT;
        // EQ
        if (i==0) CompareZeroLookup8[i]|=ST_EQ;
        // OP
        for (z=0; x; x&=(x-1)) z++;                // black magic?
        if (z&1) CompareZeroLookup8[i]|=ST_OP;     // set bit if an odd number
    }
}


// ---------------------------------------------------------
// Allocate enough memory for a SAMS 512K (DS) or 1MB (DSi)
// ---------------------------------------------------------
void InitMemoryPoolSAMS(void)
{
    // Only allocate the memory once...
#if 0    
    if (MemSAMS == 0)
    {
        if (isDSiMode())
        {
            SAMS_BANKS = 256;                           // 256 * 4K = 1024K
            MemSAMS = malloc((SAMS_BANKS-32) * 0x1000); // We can save 128K since we're getting the first 128K from our DS VRAM
        }
        else
        {
            SAMS_BANKS = 128;                           // 128 * 4K = 512K
            MemSAMS = malloc((SAMS_BANKS-32) * 0x1000); // We can save 128K since we're getting the first 128K from our DS VRAM
        }
    }
#endif    
}

// ----------------------------------------------------------------------------------------------------
// Called when a new CART is loaded and the game system needs to be started at a RESET condition.
// Caller passes in the game filename (ROM) they wish to load... C/D/G files will find their siblings.
// ----------------------------------------------------------------------------------------------------
void TMS9900_Reset(char *szGame)
{
    // -------------------------------------------------------------------------------------------------
    // Clear out the entire TMS9900 registers, helper vars and such... we'll update what we need below.
    // -------------------------------------------------------------------------------------------------
    memset(&tms9900, 0x00, sizeof(tms9900));

    // ----------------------------------------------------------------------------------------------
    // This builds up the fast opcode lookup table based on the excellent Classic99 helper functions
    // ----------------------------------------------------------------------------------------------
    TMS9900_buildopcodes();

    // ----------------------------------------------------------------------
    // Fill in the BankMasks[] for any number of possible banks up to the
    // full limit of 256 banks (2MB of Cart Space!!)
    // ----------------------------------------------------------------------
    for (u16 numBanks=1; numBanks<=256; numBanks++)
    {
        if      (numBanks <= 2)    BankMasks[numBanks-1] = 0x01;
        else if (numBanks <= 4)    BankMasks[numBanks-1] = 0x03;
        else if (numBanks <= 8)    BankMasks[numBanks-1] = 0x07;
        else if (numBanks <= 16)   BankMasks[numBanks-1] = 0x0F;
        else if (numBanks <= 32)   BankMasks[numBanks-1] = 0x1F;
        else if (numBanks <= 64)   BankMasks[numBanks-1] = 0x3F;
        else if (numBanks <= 128)  BankMasks[numBanks-1] = 0x7F;
        else                       BankMasks[numBanks-1] = 0xFF;
    }

    // --------------------------------------------------------------
    // For SAMS memory, ensure everything points to the right banks
    // and default the system to pass-thru mode...
    // --------------------------------------------------------------
    tms9900.cruSAMS[0] = 0;
    tms9900.cruSAMS[1] = 0;
    for (u8 i=0; i<16; i++)
    {
        tms9900.dataSAMS[i] = i;
    }

    // ---------------------------------------------------------------------------
    // Default all memory to MF_UNUSED until we prove otherwise below.
    // ---------------------------------------------------------------------------
    for (u32 address=0x0000; address<0x10000; address++)
    {
        MemType[address] = MF_UNUSED;
    }

    // ------------------------------------------------------------------------------
    // Mark off the memory regions that are 16-bit and incur no cycle access pentaly
    // so we set this to MF_MEM16 which is always zero so we can easily find it
    // when we do memory fetches (this will be a very common access type).
    // ------------------------------------------------------------------------------
    for(u16 address = 0x0000; address < 0x2000; address++)
    {
        MemType[address] = MF_MEM16;
    }

    for(u16 address = 0x8000; address < 0x8400; address++ )
    {
        MemType[address] = MF_MEM16;
    }

    // ------------------------------------------------------------------------------
    // Now mark off the memory hotspots where we need to take special action on
    // read/write plus all of the possible mirrors, etc. that can be used...
    // ------------------------------------------------------------------------------
    for (u16 address = 0x8400; address < 0x8800; address += 2)
    {
        MemType[address] = MF_SOUND;   // TI Sound Chip
    }

    for (u16 address = 0x8800; address < 0x8C00; address += 4)
    {
        MemType[address+0] = MF_VDP;    // VDP Read Data
        MemType[address+2] = MF_VDP;    // VDP Read Status
    }

    for (u16 address = 0x8C00; address < 0x9000; address += 4)
    {
        MemType[address+0] = MF_VDP;   // VDP Write Data
        MemType[address+2] = MF_VDP;   // VDP Write Address
    }

    for (u16 address = 0x9000; address < 0x9800; address += 4)
    {
        MemType[address+0] = MF_SPEECH;   // Speech Synth Read
        MemType[address+2] = MF_SPEECH;   // Speech Synth Write
    }

    for (u16 address = 0x9800; address < 0x9C00; address += 4)
    {
        MemType[address+0] = MF_GROMR;   // GROM Read Data
        MemType[address+2] = MF_GROMR;   // GROM Read Address
    }

    for (u16 address = 0x9C00; address < 0xA000; address += 4)
    {
        MemType[address+0] = MF_GROMW;   // GROM Write Data
        MemType[address+2] = MF_GROMW;   // GROM Write Address
    }

    for (u16 address = 0x5ff0; address < 0x6000; address++)
    {
        MemType[address] = MF_DISK;     // TI Disk Controller area
    }

    for (u16 address = 0x6000; address < 0x8000; address++)
    {
        MemType[address] = MF_CART;   // Cart Read Access and Bank Write
    }

    for (u16 address = 0x2000; address < 0x4000; address++)
    {
        MemType[address] = MF_RAM8;   // Expanded 32K RAM (low area)
    }

    for (u32 address = 0xA000; address < 0x10000; address++)
    {
        MemType[address] = MF_RAM8;   // Expanded 32K RAM (high area)
    }

    // ---------------------------------------------------------
    // Now setup for all the CART and Console roms ...
    // ---------------------------------------------------------
    memset(MemCART,         0xFF, MAX_CART_SIZE);   // The cart is not inserted to start...
    memset(MemCPU,          0xFF, 0x10000);         // Set all of memory to 0xFF (nothing mapped until proven otherwise)
    memset(MemGROM,         0xFF, 0x10000);         // Set all of GROM memory to 0xFF (nothing mapped until proven otherwise)
    memset(&MemCPU[0x8000], 0x00, 0x400);           // Mark off RAM area by clearing bytes
    memset(&MemCPU[0x2000], 0x00, 0x2000);          // 8K of Low MemCPU Expansion from 0x2000 to 0x3FFF
    memset(&MemCPU[0xA000], 0x00, 0x6000);          // 24K of High MemCPU Expansion from 0xA000 to 0xFFFF

    // ---------------------------------------------------------------------------
    // By default we clear memory but if this game is marked as 'RANDOMIZE' we
    // will randomize all of the RAM locations... probably not needed but fun!
    // ---------------------------------------------------------------------------
    if (myConfig.memWipe == 1) // RANDOMize memory if asked for
    {
        for (u16 addr = 0x8000; addr < 0x8400; addr++)
        {
            MemCPU[addr] = (rand() & 0xFF);
        }
        for (u16 addr = 0x2000; addr < 0x4000; addr++)
        {
            MemCPU[addr] = (rand() & 0xFF);
        }
        for (u32 addr = 0xA000; addr < 0x10000; addr++)
        {
            MemCPU[addr] = (rand() & 0xFF);
        }
    }


    FILE *infile;

    // ------------------------------------------------------------------
    // Read in the main 16-bit console ROM and place into our MemCPU[]
    // ------------------------------------------------------------------
    infile = fopen("/roms/bios/994aROM.bin", "rb");
    fread(&MemCPU[0], 0x2000, 1, infile);
    fclose(infile);

    // ----------------------------------------------------------------------------
    // Read in the main console GROM and place into the 24K of GROM memory space
    // ----------------------------------------------------------------------------
    infile = fopen("/roms/bios/994aGROM.bin", "rb");
    fread(&MemGROM[0x0000], 0x6000, 1, infile);
    fclose(infile);

    // -------------------------------------------
    // Read the TI Disk DSR into buffered memory
    // -------------------------------------------
    infile = fopen("/roms/bios/994aDISK.bin", "rb");
    if (infile)
    {
        fread(DiskDSR, 0x2000, 1, infile);
        fclose(infile);
    }
    else
    {
        memset(DiskDSR, 0xFF, 0x2000);
    }

    // -----------------------------------------------------------------------------
    // We're going to be manipulating the filename a bit so copy it into a buffer
    // -----------------------------------------------------------------------------
    strcpy(tmpFilename, szGame);

    u8 fileType = toupper(tmpFilename[strlen(tmpFilename)-5]);

    if ((fileType == 'C') || (fileType == 'G') || (fileType == 'D'))
    {
        tms9900.bankMask = 0x003F;
        tmpFilename[strlen(tmpFilename)-5] = 'C';   // Try to find a 'C' file
        infile = fopen(tmpFilename, "rb");
        if (infile != NULL)
        {
            int numRead = fread(MemCART, 1, MAX_CART_SIZE, infile);     // Whole cart C memory as needed...
            fclose(infile);
            if (numRead <= 0x2000)   // If 8K... repeat
            {
                memcpy(MemCART+0x2000, MemCART, 0x2000);
                memcpy(MemCART+0x4000, MemCART, 0x2000);
                memcpy(MemCART+0x6000, MemCART, 0x2000);
                memcpy(MemCART+0x8000, MemCART, 0x2000);
                memcpy(MemCART+0xA000, MemCART, 0x2000);
                memcpy(MemCART+0xC000, MemCART, 0x2000);
                memcpy(MemCART+0xE000, MemCART, 0x2000);
                tms9900.bankMask = 0x0007;
            }
            else // More than 8K needs banking support
            {
                u16 numBanks = (numRead / 0x2000) + ((numRead % 0x2000) ? 1:0); // If not multiple of 8K we need to add a bank...
                tms9900.bankMask = BankMasks[numBanks-1];
            }
        }

        tmpFilename[strlen(tmpFilename)-5] = 'D';   // Try to find a 'D' file
        infile = fopen(tmpFilename, "rb");
        if (infile != NULL)
        {
            tms9900.bankMask = 0x0001;                  // If there is a 'D' file, it's always only 2 banks
            fread(MemCART+0x2000, 0x2000, 1, infile);   // Read 'D' file
            fclose(infile);
        }
        memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // First bank loaded into main memory

        // And see if there is a GROM file to go along with this load...
        tmpFilename[strlen(tmpFilename)-5] = 'G';
        infile = fopen(tmpFilename, "rb");
        if (infile != NULL)
        {
            fread(&MemGROM[0x6000], 0xA000, 1, infile); // We support up to 40K of GROM
            fclose(infile);
        }
    }
    else // Full Load
    {
        infile = fopen(tmpFilename, "rb");
        int numRead = fread(MemCART, 1, MAX_CART_SIZE, infile);   // Whole cart memory as needed....
        fclose(infile);
        u16 numBanks = (numRead / 0x2000) + ((numRead % 0x2000) ? 1:0);
        tms9900.bankMask = BankMasks[numBanks-1];
        
        if (numBanks > 1)
        {
            // If the image is inverted we need to swap 8K banks
            if ((fileType == '3') || (fileType == '9'))
            {
                for (u16 i=0; i<numBanks/2; i++)
                {
                    // Swap 8k bank...
                    memcpy(FastCartBuffer, MemCART + (i*0x2000), 0x2000);  
                    memcpy(MemCART+(i*0x2000), MemCART + ((numBanks-i-1)*0x2000), 0x2000);
                    memcpy(MemCART + ((numBanks-i-1)*0x2000), FastCartBuffer, 0x2000);
                }
            }
        }
        
        memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // First bank loaded into main memory
    }

    memcpy(FastCartBuffer, MemCPU+0x6000, 0x2000);

    // ------------------------------------------------------
    // Now handle some of the special machine types...
    // ------------------------------------------------------
    if (myConfig.cartType == CART_TYPE_SUPERCART)
    {
        for (u16 address = 0x6000; address < 0x8000; address++)
        {
            MemType[address] = MF_RAM8; // Supercart maps ram into the cart slot
        }
        memset(MemCPU+0x6000, 0x00, 0x2000);
    }

    // ------------------------------------------------------------
    // Mini Memory maps RAM into the upper 4K of the cart slot...
    // ------------------------------------------------------------
    if (myConfig.cartType == CART_TYPE_MINIMEM)
    {
        for (u16 address = 0x7000; address < 0x8000; address++)
        {
            MemType[address] = MF_RAM8; // Mini Memory maps ram into the upper half of the cart slot
        }
        memset(MemCPU+0x7000, 0x00, 0x1000);
    }

    // ----------------------------------------------------------------
    // MBX usually has 1K of RAM mapped in... plus odd bank switching.
    // ----------------------------------------------------------------
    if ((myConfig.cartType == CART_TYPE_MBX_NO_RAM) || (myConfig.cartType == CART_TYPE_MBX_WITH_RAM))
    {
        for (u16 address = 0x6000; address < 0x8000; address++)
        {
            MemType[address] = MF_CART_NB;    // We'll do the banking manually for MBX carts
        }
        
        if ((myConfig.cartType == CART_TYPE_MBX_WITH_RAM))
        {
            for (u16 address = 0x6C00; address < 0x7000; address++)
            {
                MemType[address] = MF_RAM8; // MBX carts have 1K of memory... with the last word at >6ffe being the bank switch
                MemCPU[address] = 0x00;     // Clear out the RAM
            }
        }
        MemType[0x6ffe] = MF_MBX;   // Special bank switching register...
        MemType[0x6fff] = MF_MBX;   // Special bank switching register...
        WriteBankMBX(0);
    }

    // -------------------------------------------------
    // SAMS support... 512K for DS and 1MB for DSi
    // -------------------------------------------------
    if (myConfig.machineType == MACH_TYPE_SAMS)
    {
        InitMemoryPoolSAMS();   // Make sure we have the memory...
        
        // We don't map the MemType[] here.. only when CRU bit is written
        memset(MemSAMS,         0x00, ((SAMS_BANKS-32) * 0x1000));
        memset(MemSAMS_fast,    0x00, (128 * 1024));
    }

    // Ensure we're in the first bank...
    tms9900.cartBankPtr = FastCartBuffer;

    // Reset the TMS9901 peripheral IO chip
    TMS9901_Reset( );

    // And away we go!
    tms9900.WP = MemoryRead16(0);       // Initial WP is from the first word address in memory
    tms9900.PC = MemoryRead16(2);       // Initial PC is from the second word address in memory
    tms9900.ST = 0x3cf0;                // bulWIP uses this - probably doesn't matter... but smart guys know stuff...
}

// -----------------------------------------------------------------------------------------------
// We only handle the VDP interrupt so we only need to track that we have an interrupt request...
// -----------------------------------------------------------------------------------------------
void TMS9900_RaiseInterrupt(void)
{
    tms9900.cpuInt = 2; // The only interrupt we support is the VDP
}

// -----------------------------------------------------------------------------------------------
// We only handle the VDP interrupt so we only need to track that we have an interrupt request...
// -----------------------------------------------------------------------------------------------
void TMS9900_ClearInterrupt(void)
{
    tms9900.cpuInt = 0; // We only handle one level of interrupts
}

// -----------------------------------------------------------------------------------------------
// Accurate emulation will handle things like the PC hack for Disk Access and the IDLE instruction
// -----------------------------------------------------------------------------------------------
void TMS9900_SetAccurateEmulationFlag(u16 flag)
{
    tms9900.accurateEmuFlags |= flag;
}

// -----------------------------------------------------------------------------------------------
// If we don't have to handle DISK or IDLE or similar, we run faster... most games don't need that
// -----------------------------------------------------------------------------------------------
void TMS9900_ClearAccurateEmulationFlag(u16 flag)
{
    tms9900.accurateEmuFlags &= ~flag;
}

//-------------------------------------------------------------------
// A GROM increment should take into account that it's only really
// incrementing and wrapping at the 8K boundary. But I've yet to
// find any game that relies on the wrap and games are always setting
// the address before reads/increments and so this saves us some
// processing power necessary for the old DS-handhelds.
//-------------------------------------------------------------------
static inline u8 ReadGROM(void)
{
    AddCycleCount(GROM_READ_CYCLES);
    return MemGROM[tms9900.gromAddress++];
}

// -------------------------------------------------------------------------
// The GROM address is 16 bits but is always read/written 8-bits at a time
// so we have to handle the high byte and low byte transfers...
// -------------------------------------------------------------------------
ITCM_CODE u8 ReadGROMAddress(void)
{
    u8 data;

    AddCycleCount(GROM_READ_ADDR_CYCLES);
    tms9900.gromWriteLoHi = 0;
    if (tms9900.gromReadLoHi) // Low byte
    {
        // Reads are always address + 1 - we are not handling WRAP
        data = (u8)(tms9900.gromAddress+1);
        tms9900.gromReadLoHi = 0;
    }
    else // High byte
    {
        // Reads are always address + 1 - we are not handling WRAP
        data = (u8)((tms9900.gromAddress+1) >> 8);
        tms9900.gromReadLoHi = 1;
    }

    return data;
}

// -------------------------------------------------------------------------
// The GROM address is 16 bits but is always read/written 8-bits at a time
// so we have to handle the high byte and low byte transfers...
// -------------------------------------------------------------------------
ITCM_CODE void WriteGROMAddress(u8 data)
{
    tms9900.gromReadLoHi = 0;

    if (tms9900.gromWriteLoHi) // Low byte
    {
        AddCycleCount(GROM_WRITE_ADDR_LO_CYCLES);
        tms9900.gromAddress = (tms9900.gromAddress & 0xff00) | data;
        tms9900.gromWriteLoHi = 0;
    }
    else // High byte
    {
        AddCycleCount(GROM_WRITE_ADDR_HI_CYCLES);
        tms9900.gromAddress = (tms9900.gromAddress & 0x00ff) | (data << 8);
        tms9900.gromWriteLoHi = 1;
    }
}


ITCM_CODE void WriteGROM(u8 data)
{
    // Does nothing... should we chew up cycles?
}

// -------------------------------------------------------------------------------------------------------------------------------------------
// At one time I was using memcpy() to put the bank into the right spot into the MemCPU[] which is GREAT when you want to read (simplifies
// the memory access) but sucks if there is a lot of bank switching going on... turns out carts like Donkey Kong and Dig Dug were doing a
// ton of bankswitches and the memory copy was too slow to render those games... so we have to do this using a cart offset which is fast
// enough to render those games full-speed but does mean our memory reads and PC fetches are a little more complicated...
// -------------------------------------------------------------------------------------------------------------------------------------------
inline void WriteBank(u16 address)
{
    u8 bank = (address >> 1);                               // Divide by 2 as we are always looking at bit 1 (not bit 0)
    bank &= tms9900.bankMask;                               // Support up to the maximum bank size using mask (based on file size as read in)
    tms9900.bankOffset = (0x2000 * bank);                   // Memory Reads will now use this offset into the Cart space...
    if (bank == 0) tms9900.cartBankPtr = FastCartBuffer;    // If bank 0 we can use the fast cart buffer...
    else tms9900.cartBankPtr = MemCART+tms9900.bankOffset;  // Pre-calculate and store a pointer to the start of the right bank
}

// ------------------------------------------------------------------------
// For the MBX, there are up to 4 banks of 4K each... These map into >7000
// ------------------------------------------------------------------------
void WriteBankMBX(u8 bank)
{
    bank &= 0x3;
    memcpy(MemCPU+0x7000, MemCART+(bank*0x1000), 0x1000);
}

// ----------------------------------------------------------------------------------------------------------------------------------------
// SAMS memory bank swapping is not at all optmized... we don't want to disrupt the normal fast memory read handling for non SAMS games
// which is 99.9% of the library. So we do this the 'hard way' but moving the 4K chunks in and out of actual mapped memory rather than
// just tracking a bank number and handling it on the READ like Classic99 or MAME would do.  This works... is slow but so far has been
// fine for the few SAMS enabled games I've tried:  texcast, Dungeons of Asgard, etc.
// ----------------------------------------------------------------------------------------------------------------------------------------
const u8 IsSwappableSAMS[16] = {0,0,1,1,0,0,0,0,0,0,1,1,1,1,1,1};
ITCM_CODE void SwapBankSAMS(u16 address, u8 oldBank, u8 newBank)
{
    u32 *oldA = (u32*) (MemCPU + ((address & 0xF) * 0x1000));
    u32 *newC = (u32*) oldA;
    u32 *oldB;
    u32 *newD;

    if (oldBank < 32)
    {
        oldB = (u32*) (MemSAMS_fast + (oldBank * 0x1000));
    }
    else
    {
        oldB = (u32*) (MemSAMS + (oldBank * 0x1000) - (128 * 1024));
    }

    if (newBank < 32)
    {
        newD = (u32*) (MemSAMS_fast + (newBank * 0x1000));
    }
    else
    {
        newD = (u32*) (MemSAMS + (newBank * 0x1000)  - (128 * 1024));
    }

    if (!IsSwappableSAMS[(address&0xF)]) return;    // Make sure this is an area we allow swapping...

    // -----------------------------------------------------
    // If the new bank is within our SAMS memory range...
    // -----------------------------------------------------
    if (newBank < SAMS_BANKS)
    {
        if (oldBank < SAMS_BANKS)
        {
            // Move 4K out of main memory to the banked memory...
            for (u16 i=0; i<0x1000; i+=4)
            {
                *oldB++ = *oldA++;
            }
        }
        else
        {
            // Else we need to re-enable the RAM8 at this bank
            for (u16 i=0; i<0x1000; i++)
            {
                MemType[((address&0xF)<<12) | i] = MF_RAM8;   // RAM is enabled
            }
        }

        // Move 4K out of banked memory to main memory...
        for (u16 i=0; i<0x1000; i+=4)
        {
            *newC++ = *newD++;
        }
    }
    else // Turn off access to that memory... need to return 0xFF
    {
        if (oldBank < SAMS_BANKS)
        {
            // Move 4K out of main memory to the banked memory...
            for (u16 i=0; i<0x1000; i+=4)
            {
                *oldB++ = *oldA++;
            }
        }

        memset(newC, 0xFF, 0x1000);
        for (u16 i=0; i<0x1000; i++)
        {
            MemType[((address&0xF)<<12) | i] = MF_UNUSED;   // Expanded 32K RAM (low area)
        }
    }
}

// -------------------------------------------------------------------------------------------
// The SAMS banks are 4K and we only allow mapping of the banks at >2000-3FFF and >A000-FFFF
// -------------------------------------------------------------------------------------------
void WriteBankSAMS(u16 address, u8 data)
{
    if (tms9900.cruSAMS[0] == 1)    // Do we have access to the registers?
    {
        address = address >> 1;

        if (tms9900.cruSAMS[1] == 1)    // If the mapper is enabled, swap banks
        {
            SwapBankSAMS(address, tms9900.dataSAMS[address&0xF], data);
        }

        // Set this as the new bank
        tms9900.dataSAMS[address&0xF] = data;
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
        u8 oldEnable = tms9900.cruSAMS[1];
        tms9900.cruSAMS[cruAddress & 1] = dataBit;
        if (cruAddress & 1)    // If we are writing the mapper enabled bit...
        {
            if (oldEnable != tms9900.cruSAMS[1])    // Did this bit change?!
            {
                if (tms9900.cruSAMS[1] == 1)  // If the mapper was just enabled...
                {
                    SwapBankSAMS(0x0002, 0x02, tms9900.dataSAMS[0x2]);
                    SwapBankSAMS(0x0003, 0x03, tms9900.dataSAMS[0x3]);
                    SwapBankSAMS(0x000A, 0x0A, tms9900.dataSAMS[0xA]);
                    SwapBankSAMS(0x000B, 0x0B, tms9900.dataSAMS[0xB]);
                    SwapBankSAMS(0x000C, 0x0C, tms9900.dataSAMS[0xC]);
                    SwapBankSAMS(0x000D, 0x0D, tms9900.dataSAMS[0xD]);
                    SwapBankSAMS(0x000E, 0x0E, tms9900.dataSAMS[0xE]);
                    SwapBankSAMS(0x000F, 0x0F, tms9900.dataSAMS[0xF]);
                }
                else    // Pass-thru mode - map the lower 32K in...
                {
                    SwapBankSAMS(0x0002, tms9900.dataSAMS[0x2], 0x02);
                    SwapBankSAMS(0x0003, tms9900.dataSAMS[0x3], 0x03);
                    SwapBankSAMS(0x000A, tms9900.dataSAMS[0xA], 0xA);
                    SwapBankSAMS(0x000B, tms9900.dataSAMS[0xB], 0xB);
                    SwapBankSAMS(0x000C, tms9900.dataSAMS[0xC], 0xC);
                    SwapBankSAMS(0x000D, tms9900.dataSAMS[0xD], 0xD);
                    SwapBankSAMS(0x000E, tms9900.dataSAMS[0xE], 0xE);
                    SwapBankSAMS(0x000F, tms9900.dataSAMS[0xF], 0xF);
                }
            }
        }
        else // We are dealing with the DSR enabled bit
        {
            if (dataBit == 1) // Mapping DSR in
            {
                for (u16 address = 0x4000; address < 0x4020; address += 2)
                {
                    MemType[address] = MF_SAMS;    // SAMS expanded memory handling
                }
            }
            else // Mapping DSR out
            {
                for (u16 address = 0x4000; address < 0x4020; address += 2)
                {
                    MemType[address] = MF_UNUSED;    // Map back to original handling (disk controller sits here by default)
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------------
// When we know we're reading RAM from the use of the Workspace Pointer (WP) and register access, we can just do this
// quickly. In theory the WP can point to 8-bit expanded RAM with a pentalty but it's so uncommon that we will not
// incur the pentaly. This means the emulation will not be cycle accurate but it's good enough to play classic TI games...
// ------------------------------------------------------------------------------------------------------------------------
inline u16 ReadRAM16(u16 address)
{
    return __builtin_bswap16(*(u16*) (&MemCPU[address]));
}

// ----------------------------------------------------------------------------------------
// See comment above for ReadRAM16() - not cycle accurate but good enough for WP use...
// ----------------------------------------------------------------------------------------
inline void WriteRAM16(u16 address, u16 data)
{
    *((u16*)(MemCPU+address)) = (data << 8) | (data >> 8);
}

// -----------------------------------------------------------------------------------------------
// A PC fetch is always from main memory and won't trigger anything like VDP or GROM access...
// so we can be a little smarter/faster in getting the memory here.
// -----------------------------------------------------------------------------------------------
ITCM_CODE u16 ReadPC16(void)
{
    u16 address = tms9900.PC; tms9900.PC+=2;

    // This will trap out anything that isn't below 0x2000 which is console ROM and heavily utilized...
    if (address & 0xC000)
    {
        u8 memType = MemType[address];
        if (memType)
        {
            AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM
            if (memType == MF_CART)
            {
                u16 data16 = *((u16*)(tms9900.cartBankPtr + (address&0x1FFF)));
                return (data16 << 8) | (data16 >> 8);

            }
        }
    }
    u16 data16 = *((u16*)(MemCPU+address));
    return (data16 << 8) | (data16 >> 8);
}


// -----------------------------------------------------------------------------------------
// We don't perform the actual read - nothing in the TI world should trigger or count on
// this phantom read... we only take into account any possible cycle penalty.
// -----------------------------------------------------------------------------------------
inline void MemoryReadHidden(u16 address)
{
    if (MemType[address]) AddCycleCount(4); // Any bit set is a penalty... this includes the VDP which is, technically, on the 16-bit bus
}


// -------------------------------------------------------------------------------------------
// Technically everything in the system is a 16-bit read with an 8-bit multiplexer... but
// we can be a little smarter and not waste the same time that an actual TI-99/4a does.
// -------------------------------------------------------------------------------------------
ITCM_CODE u16 MemoryRead16(u16 address)
{
    u16 retVal;
    address &= 0xFFFE;
    u8 memType = MemType[address];

    if (memType)
    {
        AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM

        switch (memType)
        {
            case MF_CART:
                retVal = *((u16*)(tms9900.cartBankPtr + (address&0x1FFF)));
                return (retVal << 8) | (retVal >> 8);
                break;
            case MF_VDP:
                if (address & 2) retVal = (u16)RdCtrl9918()<<8; else retVal = (u16)RdData9918()<<8;
                return retVal;
                break;
            case MF_GROMR:
                if (address & 2) { retVal = ReadGROMAddress(); retVal |= (u16)ReadGROMAddress() << 8; }
                else { retVal = ReadGROM(); retVal |= (u16)ReadGROM() << 8; }
                return retVal;
                break;
            case MF_SPEECH:
                return (0x40 | 0x20);   //TBD for now... satisfies the games that look for the module... Bits are empty and buffer low
                break;
            case MF_DISK:
                return ReadTICCRegister(address);
                break;
            default:
                retVal = *((u16*)(MemCPU+address));
                return (retVal << 8) | (retVal >> 8);
                break;
        }
    }

    // This is either console ROM or workspace RAM
    retVal = *((u16*)(MemCPU+address));
    return (retVal << 8) | (retVal >> 8);
}


ITCM_CODE u8 MemoryRead8(u16 address)
{
    u8 memType = MemType[address];

    if (memType)
    {
        AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM

        switch (memType)
        {
            case MF_VDP:
                if (address & 2) return (u8)RdCtrl9918();
                else return (u8)RdData9918();
                break;
            case MF_GROMR:
                if (address & 2) return ReadGROMAddress();
                else return ReadGROM();
                break;
            case MF_CART:
                return tms9900.cartBankPtr[(address&0x1FFF)];
                break;
            case MF_SPEECH:
                return (0x40 | 0x20);   //TBD for now... satisfies the games that look for the module... Bits are empty and buffer low
                break;
            case MF_DISK:
                return ReadTICCRegister(address);
                break;
            case MF_SAMS:
                return tms9900.dataSAMS[(address & 0x1E) >> 1];
                break;
            default:
                return MemCPU[address];
                break;
        }
    }

    return MemCPU[address]; // This is either console ROM or workspace RAM
}


// -----------------------------------------------------------------------------------------------------------------------
// It's unclear as I don't seem to see any 16-bit writes for things like Sound or VDP which makes sense... but I think
// it's technically allowed so we are doing the same switch checks here as we would in the 8-bit write world... it's
// probably okay as the switch statement will be unrolled into a jump-table by the GCC compiler anyway.
// -----------------------------------------------------------------------------------------------------------------------
ITCM_CODE void MemoryWrite16(u16 address, u16 data)
{
    address &= 0xFFFE;

    u8 memType = MemType[address];

    if (memType)
    {
        AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM

        switch (memType)
        {
            case MF_SOUND:
                sn76496W(data, &sncol);
                break;
            case MF_VDP:
                if (address & 2) WrCtrl9918(data>>8);
                else WrData9918(data>>8);
                break;
            case MF_GROMW:
                if (address & 2) { WriteGROMAddress(data&0x00ff); WriteGROMAddress((data&0xFF00)>>8); }
                else { WriteGROM(data&0x00ff); WriteGROM((data&0xFF00)>>8); }
                break;
            case MF_CART:
                WriteBank(address);
                break;
            case MF_SAMS:
                WriteBankSAMS(address, data>>8);
                break;
            case MF_MBX:
                WriteBankMBX(data>>8);
                WriteBankMBX(data);
                if (myConfig.cartType == CART_TYPE_MBX_WITH_RAM) MemCPU[address] = data>>8;
                break;
            case MF_RAM8:
                MemCPU[address] = (data>>8);
                MemCPU[address+1] = data & 0xFF;
                break;
            default:    // Nothing to write... ignore
                break;
        }
    }
    else    // This has to be workspace RAM which has to deal with mirrors...
    {
        if (myConfig.RAMMirrors)    // We write to all mirrors so that read-back is quick and easy
        {
            *((u16*)(MemCPU+(0x8000 | (address&0xff)))) = (data << 8) | (data >> 8);
            *((u16*)(MemCPU+(0x8100 | (address&0xff)))) = (data << 8) | (data >> 8);
            *((u16*)(MemCPU+(0x8200 | (address&0xff)))) = (data << 8) | (data >> 8);
            *((u16*)(MemCPU+(0x8300 | (address&0xff)))) = (data << 8) | (data >> 8);
        }
        else
        {
            *((u16*)(MemCPU+address)) = (data << 8) | (data >> 8);
        }
    }
}

ITCM_CODE void MemoryWrite8(u16 address, u8 data)
{
    u8 memType = MemType[address];

    if (memType)
    {
        AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM

        switch (memType)
        {
            case MF_SOUND:
                 sn76496W(data, &sncol);
                break;
            case MF_VDP:
                if (address & 2) WrCtrl9918(data);
                else WrData9918(data);
                break;
            case MF_GROMW:
                if (address & 2) WriteGROMAddress(data);
                else WriteGROM(data);
                break;
            case MF_CART:
                WriteBank(address);
                break;
            case MF_SPEECH:
                // Not yet
                break;
            case MF_SAMS:
                WriteBankSAMS(address, data);
                break;
            case MF_MBX:
                WriteBankMBX(data);
                if (myConfig.cartType == CART_TYPE_MBX_WITH_RAM) MemCPU[address] = data;
                break;
            case MF_DISK:
                WriteTICCRegister(address, data);  // Disk Controller
                break;
            case MF_RAM8:
                MemCPU[address] = data;    // Expanded 32K RAM
                break;
            default:    // Nothing to write... ignore
                break;
        }
    }
    else    // This has to be workspace RAM which has to deal with mirrors...
    {
        if (myConfig.RAMMirrors)    // We write to all mirrors so that read-back is quick and easy
        {
            MemCPU[0x8000 | (address&0xff)] = data;
            MemCPU[0x8100 | (address&0xff)] = data;
            MemCPU[0x8200 | (address&0xff)] = data;
            MemCPU[0x8300 | (address&0xff)] = data;
        }
        else
        {
            MemCPU[address] = data;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Source Address extracted from the Opcode. For this addressing mode the opcode is in the format:
// 15 14 13  12   11 10  9 8 7 6  5 4  3 2 1 0
// [OPCODE ] [B]  [TD ]  [DEST ] [TS ] [SOURCE]
// The [B] bit tells us if this is a byte addressing (0 implies word addressing)
// ------------------------------------------------------------------------------------------------
inline void Ts(u16 bytes)
{
    u16 rData = REG_GET_FROM_OPCODE();

    switch ((tms9900.currentOp >> 4) & 0x03)
    {
        case 0: // Rx  2c
            tms9900.srcAddress = WP_REG(rData);
            break;

        case 1: // *Rx  6c
            tms9900.srcAddress = ReadRAM16(WP_REG(rData));
            AddCycleCount(4);
            break;

        case 2: // @yyyy(Rx) or @yyyy if Rx=0  10c
            tms9900.srcAddress = ReadPC16();
            if (rData) tms9900.srcAddress += ReadRAM16(WP_REG(rData));
            AddCycleCount(8);
            break;

        default: // *Rx+   10c
            tms9900.srcAddress = ReadRAM16(WP_REG(rData));
            WriteRAM16(WP_REG(rData), (tms9900.srcAddress + bytes));
            AddCycleCount((bytes==1?6:8)); // Add 6 cycles for byte address... 8 for word address
            break;
    }

    tms9900.srcAddress &= (0xFFFE | bytes); // bytes is either 1 (in which case we will utilize the LSB) or 2 (in which case we mask off to 16-bits)
}


// ------------------------------------------------------------------------------------------------------
// Destination Address extracted from the Opcode. For this addressing mode the opcode is in the format:
// 15 14 13  12   11 10  9 8 7 6  5 4  3 2 1 0
// [OPCODE ] [B]  [TD ]  [DEST ] [TS ] [SOURCE]
// The [B] bit tells us if this is a byte addressing (0 implies word addressing)
// ------------------------------------------------------------------------------------------------------
inline void Td(u16 bytes)
{
    u16 rData = (tms9900.currentOp>>6) & 0x0F;

    switch ((tms9900.currentOp >> 10) & 0x03)
    {
        case 0: // Rx  2c
            tms9900.dstAddress = WP_REG(rData);
            break;

        case 1: // *Rx  6c
            tms9900.dstAddress = ReadRAM16(WP_REG(rData));
            AddCycleCount(4);
            break;

        case 2: // @yyyy(Rx) or @yyyy if Rx=0  10c
            tms9900.dstAddress = ReadPC16();
            if (rData) tms9900.dstAddress += ReadRAM16(WP_REG(rData));
            AddCycleCount(8);
            break;

        default: // *Rx+   10c
            tms9900.dstAddress = ReadRAM16(WP_REG(rData));
            // Post increment should happen LATER - unsure what this will cause... see Classic99 to improve but don't want the complexity yet...
            WriteRAM16(WP_REG(rData), (tms9900.dstAddress + bytes));
            AddCycleCount((bytes==1?6:8)); // Add 6 cycles for byte address... 8 for word address
            break;
    }

    tms9900.srcAddress &= (0xFFFE | bytes); // bytes is either 1 (in which case we will utilize the LSB) or 2 (in which case we mask off to 16-bits)
}

// -------------------------------------------------------------------------------
// Destination uses workspace addressing only - for instructions like MPY or DIV
// -------------------------------------------------------------------------------
inline void TdWA(void)
{
    u16 rData = (tms9900.currentOp>>6) & 0x0F;     // The register to use for destination addressing
    tms9900.dstAddress = WP_REG(rData);            // Destination address is directly from the register
    tms9900.dstAddress &= 0xFFFE;                  // We are always in WORD mode for Workspace Addressing
}

// ----------------------------------------------------------
// Many opcodes want both a source and destination address
// and for this decoding mode, we will look at the current
// opcode to determine if this is byte or word addressing...
// ----------------------------------------------------------
ITCM_CODE void TsTd(void)
{
    u16 bytes = (tms9900.currentOp & 0x1000) ? 1:2;     // This handles both Word and Byte addresses
    Ts(bytes); Td(bytes);
}


// --------------------------------------------------------------------------------------
// The context switch saves the WP, PC and Status and sets up for the new workspace.
// Classic99 checks for a return address of zero but we don't handle that in DS99/4a.
// --------------------------------------------------------------------------------------
ITCM_CODE void TMS9900_ContextSwitch(u16 address)
{
    // Do it the Classic99 way...
    u16 x1 = tms9900.WP;                        // Save old WP
    tms9900.WP = MemoryRead16(address+0);       // Set the new Workspace
    MemoryWrite16(WP_REG(13), x1);              // Set the old Workspace Pointer
    MemoryWrite16(WP_REG(14), tms9900.PC);      // Set the old PC
    MemoryWrite16(WP_REG(15), tms9900.ST);      // Set the old Status
    tms9900.PC = MemoryRead16(address+2);       // Set the new PC based on original workspace
}

// ---------------------------------------------------------------------------------------
// We are only supporting a single external interrupt souce - the VDP as it comes
// from the TMS9901. There is also the timer but that's only used for Cassette routines
// and possibly an odd demo program or two... so we aren't going to worry about it.
// Users of DS99/4a are interested in playing Hunt the Wumpus and Tunnels of Doom and
// so we won't overcomplicate things...
// ---------------------------------------------------------------------------------------
ITCM_CODE void TMS9900_HandlePendingInterrupts(void)
{
    // --------------------------------------------------------------------------------
    // Literally any level except 0 will allow VDP interrupt.
    // Someday we might be more sophisticated than this... but that day is not today.
    // --------------------------------------------------------------------------------
    if (tms9900.cpuInt && (tms9900.ST & ST_INTMASK))
    {
        TMS9900_ContextSwitch(1<<2);        // We are only supporting the VDP
        tms9900.ST &= ~ST_INTMASK;          // De-escalate the interrupt. Since we only support one level we can clear it all...
        tms9900.idleReq=0;                  // And if we were waiting on IDLE, this will start the CPU back up
    }
}


// ---------------------------------------------------------------------------------
// Mainly for the X = Execute instruction but also for the accurate emulation flag.
// I'd love for this to be in fast .ITCM cache memory on the DS but we only have
// 32K of that fast instruction memory available and we're just about full. So only
// the main TMS9900_Run() execution loop will use this fast .ITCM opcode handling.
// ---------------------------------------------------------------------------------
void ExecuteOneInstruction(u16 opcode)
{
    u8 data8;
    u16 data16;
    u8 op8 = (u8)OpcodeLookup[opcode];
    switch (op8)
    {
    #include "tms9900.inc"
    }
}

// --------------------------------------------------------------------------------------------------------------
// Execute 1 scanline worth of TMS9900 instructions. We keep track of the cycle delta - that is, the overage
// from one scanline to the next and we compensate the next scanline by the appopriate delta to keep things
// running at the right speed. In practice this has been good enough to render all the TI games properly.
//
// This is chewing up a big chunk of the ITCM memory. Right now the entire opcode handling is about 16K of
// fast instruction memory... but it buys us a solid 10% speed by keeping those instructions in the cache.
// --------------------------------------------------------------------------------------------------------------
ITCM_CODE void TMS9900_Run(void)
{
    u32 myCounter = tms9900.cycles+228-tms9900.cycleDelta;

    // --------------------------------------------------------------------
    // Accurate emulation is enabled if we see an IDLE instruction which
    // needs special attention.  Most games don't need this handling
    // and we save the precious DS CPU cycles as this is almost 10% slower.
    // --------------------------------------------------------------------
    if (tms9900.accurateEmuFlags)
    {
        do
        {
            if (tms9900.cpuInt) TMS9900_HandlePendingInterrupts();
            if (tms9900.idleReq)
            {
                tms9900.cycles += 4;
            }
            else
            {
                if (tms9900.PC == 0x40e8) HandleTICCSector();
                tms9900.currentOp = ReadPC16();
                ExecuteOneInstruction(tms9900.currentOp);
            }
        }
        while(tms9900.cycles < myCounter);    // There are 228 CPU clocks per line on the TI
    }
    else    // No special Idle handling... we can do this a bit faster here...
    {
        do
        {
            u8 data8;
            u16 data16;
            if (tms9900.cpuInt) TMS9900_HandlePendingInterrupts();
            if (tms9900.PC == 0x40e8) HandleTICCSector();
            tms9900.currentOp = ReadPC16();
            u8 op8 = (u8)OpcodeLookup[tms9900.currentOp];
            switch (op8)
            {
            #include "tms9900.inc"
            }
        }
        while(tms9900.cycles < myCounter);    // There are 228 CPU clocks per line on the TI
    }

    tms9900.cycleDelta = tms9900.cycles-myCounter;
}

// End of file
