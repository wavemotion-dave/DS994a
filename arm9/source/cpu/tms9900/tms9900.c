// =====================================================================================
// Copyright (c) 2023-2025 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated
// readme files, with or without modification, are permitted in any medium without
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The DS994a emulator is offered as-is, without any warranty.
//
// Bits of this code came from Clasic99 Copyright (c) Mike Brent who has graciously allowed
// me to use it to help with the core TMS9900 emualation. Please see Classic99 for
// the original CPU core and please adhere to the copyright wishes of Mike Brent
// for any code that is duplicated here.
//
// Please see the README.md file as it contains much useful info.
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
#include "../../SAMS.h"
#include "../../disk.h"
#include "../../DS99_utils.h"
#include "../tms9918a/tms9918a.h"
#include "../sn76496/SN76496.h"

// ---------------------------------------------------------
// Memory map for the TI-99/4a:
// ---------------------------------------------------------
// >0000 - 1FFF     Console ROM
// >2000 - 3FFF     Expanded RAM (8K, low memory expansion)
// >4000 - 5FFF     Peripheral card ROM (whichever one swapped in)
// >6000 - 7FFF     Cartridge ROM (module port), Sometimes SuperSpace RAM
// >8000 - 83FF     Scratchpad RAM (256 bytes, mirrored)
// >8400 - 87FF     TI Sound chip (write only)
// >8800 - 8BFF     VDP Read   (8800 read, 8802 status)
// >8C00 - 8FFF     VDP Write  (8C00 data, 8C02 address)
// >9000 - 93FF     Speech Synth read
// >9400 - 97FF     Speech Synth write
// >9800 - 9BFF     GROM Read  (9800 read, 9802 read addr+1)
// >9C00 - 9FFF     GROM Write (9C00 data, 9C02 address)
// >A000 - FFFF     Expanded RAM (24K, high memory expansion)

// ------------------------------------------------------------------
// These are all too big to fit into DTCM fast memory on the DS...
// These are the core 64K of memory for the system - there are
// actually two independent 64K memory areas - one for normal CPU
// access and one for GROM access. Come to think of it, there is
// also a separate 16K of VDP memory and 4096 bits of CRU memory...
// heck, this system has more memories than a Harry Potter Pensieve.
// ------------------------------------------------------------------
u8      MemCPU[0x10000];            // 64K of CPU Space  (the lower 8K will contain the system console ROM)
u8      MemGROM[0x10000];           // 64K of GROM Space (the lower 24K will contain system GROMs)

// ----------------------------------------------------------------------------------------------
// Memory type for each address. We divide by 16 which allows for higher density memory lookups.
// This is fine for all but MBX which has special handling. Since this is accessed very often,
// we place it in fast memory though it does chew up a solid 4K of that memory...
// ----------------------------------------------------------------------------------------------
u8      MemType[0x10000>>4] __attribute__((section(".dtcm")));

u8     *MemCART  __attribute__((section(".dtcm")));     // Cart C/D/8/9 memory up to 8MB/512K (DSi vs DS) banked at >6000
u32     MAX_CART_SIZE = (512*1024);                     // Allow carts up to 512K in size (DSi will bump this to 8MB)

TMS9900 tms9900  __attribute__((section(".dtcm")));  // Put the entire TMS9900 set of registers and helper vars into fast .DTCM RAM on the DS

#define OpcodeLookup            ((u16*)0x06820000)   // We use 128K of semi-fast VDP memory to help with the OpcodeLookup[] lookup table (normally VRAM_B)
#define CompareZeroLookup16     ((u16*)0x06860000)   // We use 128K of semi-fast VDP memory to help with the CompareZeroLookup16[] lookup table (normally VRAM_D)

#define AddCycleCount(x) (tms9900.cycles += (x))     // Our main way of bumping up the cycle counts during execution - each opcode handles their own timing increments

u16 MemoryRead16(u16 address);

// --------------------------------------------------------------------------------------------------------------------------------
// This emulator does not handle the full TMS5220 speech synth... but we do ensure that the system responds as if it had one and
// we also can render many of the classic games with full speech by using built-in sound effects. This is a bit of a cheat but
// is simple enough and takes very little emulated CPU time (since WAV output on the DS is largely handled by the ARM7 processor)
// --------------------------------------------------------------------------------------------------------------------------------
u16 readSpeech __attribute__((section(".dtcm"))) = SPEECH_SENTINAL_VAL;

// Some carts use CRU banking... such as the Super Cart (or Super Space II) and some Databiotics carts
u8  super_bank __attribute__((section(".dtcm"))) = 0;
u8  cart_cru_shadow[16] = {0};

u32 idle_counter = 0;   // Only used for debug purposes... so it doesn't need to be in fast memory

// A few externs from other modules...
extern SN76496 snti99;

// Supporting banking up to 8MB (1024 x 8KB = 8192KB) even though our cart buffer might be smaller
u16 BankMasks[1024];

// Pre-fill the parity table for fast look-up based on Classic99 'black magic'
u16 ParityTable[256]     __attribute__((section(".dtcm")));

// ---------------------------------------------------------------------------------
// And the Classic99 handling of compare-to-zero status bits for 8-bit instructions.
// Note this will only handle LAE and P - other bits to be set by instruction.
// This is small enough that we can place it into the fast .DTCM data memory.
// ---------------------------------------------------------------------------------
u16 CompareZeroLookup8[256] __attribute__((section(".dtcm")));


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
    for (i=0; i<0x10000; i++)
    {
        in=(u16)i;

        x=(in&0xf000)>>12;
        switch(x)
        {
        case 0: opcode0(in);             break;
        case 1: opcode1(in);             break;
        case 2: opcode2(in);             break;
        case 3: opcode3(in);             break;
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


// ----------------------------------------------------------------------------------------------------
// Called when a new CART is loaded and the game system needs to be started at a RESET condition.
// ----------------------------------------------------------------------------------------------------
void TMS9900_Reset(void)
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
    // full limit of 1024 banks (8MB of Cart Space!!)
    // ----------------------------------------------------------------------
    for (u16 numBanks=1; numBanks<=1024; numBanks++)
    {
        if      (numBanks <= 1)    BankMasks[numBanks-1] = 0x0000;  // No banking
        else if (numBanks <= 2)    BankMasks[numBanks-1] = 0x0001;
        else if (numBanks <= 4)    BankMasks[numBanks-1] = 0x0003;
        else if (numBanks <= 8)    BankMasks[numBanks-1] = 0x0007;
        else if (numBanks <= 16)   BankMasks[numBanks-1] = 0x000F;
        else if (numBanks <= 32)   BankMasks[numBanks-1] = 0x001F;
        else if (numBanks <= 64)   BankMasks[numBanks-1] = 0x003F;
        else if (numBanks <= 128)  BankMasks[numBanks-1] = 0x007F;
        else if (numBanks <= 256)  BankMasks[numBanks-1] = 0x00FF;
        else if (numBanks <= 512)  BankMasks[numBanks-1] = 0x01FF;
        else                       BankMasks[numBanks-1] = 0x03FF;
    }

    // ---------------------------------------------------------------------------
    // Default all memory to MF_UNUSED until we prove otherwise below.
    // ---------------------------------------------------------------------------
    for (u32 address=0x0000; address<0x10000; address++)
    {
        MemType[address>>4] = MF_UNUSED;
    }

    // ------------------------------------------------------------------------------
    // Mark off the memory regions that are 16-bit and incur no cycle access pentaly
    // so we set this to MF_MEM16 which is always zero so we can easily find it
    // when we do memory fetches (this will be a very common access type).
    // ------------------------------------------------------------------------------
    for(u16 address = 0x0000; address < 0x2000; address++)
    {
        MemType[address>>4] = MF_MEM16;
    }

    for(u16 address = 0x8000; address < 0x8400; address++ )
    {
        MemType[address>>4] = MF_MEM16;
    }

    // ----------------------------------------------------------
    // The 8K at >4000 is for Peripheral ROM use (Disk DSR, etc)
    // ----------------------------------------------------------
    for(u16 address = 0x4000; address < 0x6000; address++)
    {
        MemType[address>>4] = MF_PERIF;
    }

    // ------------------------------------------------------------------------------
    // Now mark off the memory hotspots where we need to take special action on
    // read/write plus all of the possible mirrors, etc. that can be used...
    // ------------------------------------------------------------------------------
    for (u16 address = 0x8400; address < 0x8800; address += 2)
    {
        MemType[address>>4] = MF_SOUND;   // TI Sound Chip responds to any even address between >8400 and >87FE
    }

    for (u16 address = 0x8800; address < 0x8C00; address += 4)
    {
        MemType[(address+0)>>4] = MF_VDP_R;    // VDP Read Data
        MemType[(address+2)>>4] = MF_VDP_R;    // VDP Read Status
    }

    for (u16 address = 0x8C00; address < 0x9000; address += 4)
    {
        MemType[(address+0)>>4] = MF_VDP_W;   // VDP Write Data
        MemType[(address+2)>>4] = MF_VDP_W;   // VDP Write Address
    }

    for (u16 address = 0x9000; address < 0x9800; address += 4)
    {
        MemType[(address+0)>>4] = MF_SPEECH;   // Speech Synth Read
        MemType[(address+2)>>4] = MF_SPEECH;   // Speech Synth Write
    }

    for (u16 address = 0x9800; address < 0x9C00; address += 4)
    {
        MemType[(address+0)>>4] = MF_GROMR;   // GROM Read Data
        MemType[(address+2)>>4] = MF_GROMR;   // GROM Read Address
    }

    for (u16 address = 0x9C00; address < 0xA000; address += 4)
    {
        MemType[(address+0)>>4] = MF_GROMW;   // GROM Write Data
        MemType[(address+2)>>4] = MF_GROMW;   // GROM Write Address
    }

    for (u16 address = 0x6000; address < 0x8000; address++)
    {
        MemType[address>>4] = MF_CART;   // Cart Read Access and Bank Write
    }

    for (u16 address = 0x2000; address < 0x4000; address++)
    {
        MemType[address>>4] = (myConfig.machineType == MACH_TYPE_SAMS ? MF_SAMS8 : MF_RAM8);   // Expanded 32K RAM (low area) - could be SAMS mapped
    }

    for (u32 address = 0xA000; address < 0x10000; address++)
    {
        MemType[address>>4] = (myConfig.machineType == MACH_TYPE_SAMS ? MF_SAMS8 : MF_RAM8);   // Expanded 32K RAM (high area) - could be SAMS mapped
    }

    // ---------------------------------------------------------
    // Now setup for all the CART and Console roms ...
    // ---------------------------------------------------------
    memset(MemCART,         0xFF,(512*1024));       // The cart is not inserted to start... We map larger than this, but don't waste time clearing more than 512K
    memset(MemCPU,          0xFF, 0x10000);         // Set all of memory to 0xFF (nothing mapped until proven otherwise)
    memset(MemGROM,         0xFF, 0x10000);         // Set all of GROM memory to 0xFF (nothing mapped until proven otherwise)
    memset(&MemCPU[0x8000], 0x00, 0x400);           // Clear the RAM area - we might randomize this area a few lines further below
    memset(&MemCPU[0x2000], 0x00, 0x2000);          // 8K of Low MemCPU Expansion from 0x2000 to 0x3FFF
    memset(&MemCPU[0xA000], 0x00, 0x6000);          // 24K of High MemCPU Expansion from 0xA000 to 0xFFFF

    // ---------------------------------------------------------------------------
    // By default we clear memory but if this game is marked as 'RANDOMIZE' we
    // will randomize all of the RAM locations... probably not needed but fun!
    // ---------------------------------------------------------------------------
    if (myConfig.memWipe == 1) // RANDOMize memory if asked for
    {
        for (u16 addr = 0x8000; addr < 0x8100; addr++)
        {
            // Same random value shows up in all mirrors....
            u8 val = rand() & 0xFF;
            MemCPU[addr | 0x000] = (val);
            MemCPU[addr | 0x100] = (val);
            MemCPU[addr | 0x200] = (val);
            MemCPU[addr | 0x300] = (val);
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
    else // For the 32K RAM expansion, a 'clear' pattern is actually >FF00 due to the RAM chips used
    {
        for (u16 addr = 0x2000; addr < 0x4000; addr++)
        {
            MemCPU[addr] = (addr & 1) ? 0x00 : 0xFF;
        }
        for (u32 addr = 0xA000; addr < 0x10000; addr++)
        {
            MemCPU[addr] = (addr & 1) ? 0x00 : 0xFF;
        }
    }

    // Reset the super cart bank to bank 0
    super_bank = 0;
    memset(cart_cru_shadow, 0x00, sizeof(cart_cru_shadow));

    idle_counter = 0;

    // Reset the TMS9901 peripheral IO chip
    TMS9901_Reset();
}

// -----------------------------------------------------------------------------------------------
// Grab the TMS9900 WP and PC from the initial words in the CPU address space to launch us.
// -----------------------------------------------------------------------------------------------
void TMS9900_Kickoff(void)
{
    // And away we go!
    tms9900.WP = MemoryRead16(0) & 0xFFFE;  // Initial WP is from the first word address in memory
    tms9900.PC = MemoryRead16(2) & 0xFFFE;  // Initial PC is from the second word address in memory
    tms9900.ST = 0x3cf0;                    // bulWIP uses this - probably doesn't matter... but smart guys know stuff...
}

// -----------------------------------------------------------------------------------------------
// We only handle the VDP interrupt so we only need to track that we have an interrupt request...
// -----------------------------------------------------------------------------------------------
void TMS9900_RaiseInterrupt(u16 iMask)
{
    tms9900.cpuInt |= iMask; // The only interrupt we support is the VDP
}

// -----------------------------------------------------------------------------------------------
// We only handle the VDP interrupt so we only need to track that we have an interrupt request...
// -----------------------------------------------------------------------------------------------
void TMS9900_ClearInterrupt(u16 iMask)
{
    tms9900.cpuInt &= ~iMask; // We only handle one level of interrupts
}

// -----------------------------------------------------------------------------------------------
// Accurate emulation will handle things like the PC hack for Disk Access and the IDLE instruction
// -----------------------------------------------------------------------------------------------
void TMS9900_SetAccurateEmulationFlag(u16 flag)
{
    tms9900.accurateEmuFlags |= flag;
}

// -------------------------------------------------------------------------------------------------
// If we don't have to handle IDLE or TIMERS or similar, we run faster... most games don't need that
// -------------------------------------------------------------------------------------------------
void TMS9900_ClearAccurateEmulationFlag(u16 flag)
{
    tms9900.accurateEmuFlags &= ~flag;
}

//-----------------------------------------------------------------------------------------
// The GROM increment takes into account that it's only really incrementing and wrapping
// at the 8K boundary. The auto-increment should not bump the upper 3 bits which would,
// effectively, select the next GROM in memory. This comes at a slight speed pentalty
// but it's a minor enough hit and we want this to be bullet-accurate.
//-----------------------------------------------------------------------------------------
static inline __attribute__((always_inline)) u8 ReadGROM(void)
{
    AddCycleCount(GROM_READ_CYCLES);
    u8 ret=MemGROM[tms9900.gromAddress];
    // Auto-increment - be careful not to bump the high bits as that's our GROM select
    tms9900.gromAddress = (tms9900.gromAddress & 0xE000) | ((tms9900.gromAddress+1) & 0x1FFF);
    return ret;

}

// ---------------------------------------------------------------------------------
// The GROM address is 16 bits but is always read/written 8-bits at a time
// so we have to handle the high byte and low byte transfers... This code
// is careful to preserve the upper 3 bits of the GROM address as an auto-increment
// should not bump the GROM that is being accessed (i.e. if we are at the top
// of the GROM address, an increment should wrap to address 0x0000 of the same GROM.
// ---------------------------------------------------------------------------------
ITCM_CODE u8 ReadGROMAddress(void)
{
    u8 data;

    AddCycleCount(GROM_READ_ADDR_CYCLES);
    tms9900.gromWriteLoHi = 0;

    if (tms9900.gromReadLoHi) // Low byte
    {
        // Reads are always address + 1
        data = (u8)((tms9900.gromAddress+1) & 0xFF);
        tms9900.gromReadLoHi = 0;
    }
    else // High byte
    {
        // Reads are always address + 1 - be careful to not bump the high bits as that's our GROM select
        u16 addr = (tms9900.gromAddress & 0xE000) | ((tms9900.gromAddress+1) & 0x1FFF);
        data = (u8)(addr >> 8);
        tms9900.gromReadLoHi = 1;
    }

    return data;
}

// -------------------------------------------------------------------------
// The GROM address is 16 bits but is always read/written 8-bits at a time
// so we have to handle the high byte and low byte transfers...
// -------------------------------------------------------------------------
inline void WriteGROMAddress(u8 data)
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

// ------------------------------------------------------------------------------------
// Well behaved programs should not be writing to GROM space unless it's some kind
// of GRAM-Kraker device... but we don't support that and so we'll simply do nothing.
// ------------------------------------------------------------------------------------
void WriteGROM(u8 data)
{
    // Does nothing... should we chew up cycles?
}

// -------------------------------------------------------------------------------------------------------------------------------------------
// TI carts bank with writes to the ROM area at >6000 and up (e.g. write to >6000 is bank 0, write to >6002 is bank 1, write to >6004 is
// bank 3,etc). At one time I was using memcpy() to put the bank into the right spot into the MemCPU[] which is GREAT when you want to read
// (simplifies the memory access) but sucks if there is a lot of bank switching going on... turns out carts like Donkey Kong and Dig Dug
// were doing a ton of bankswitches and the memory copy was too slow to render those games... so we have to do this using a cart offset
// which is fast enough to render those games full-speed but does mean our memory reads and PC fetches are a little more complicated...
//
// Anyway... the maximum cart size that can be supported by this traditional banking scheme is (8192 / 2 = 4096 banks of 8K or 32MB!).
// That's huge - larger than all of the available 16MB of RAM on a DSi and way bigger than the 4MB of RAM on a DS-Lite/Phat. We don't
// support that large but it's okay - 99% of all carts are less than 512K.
// -------------------------------------------------------------------------------------------------------------------------------------------
inline __attribute__((always_inline)) void WriteBank(u16 address)
{
    // If we are PAGEDCRU, a write to ROM should do nothing... Red Baron appears to hit the ROM for reasons unknown.
    if (myConfig.cartType != CART_TYPE_PAGEDCRU)
    {
        u16 bank = (address >> 1);                              // Divide by 2 as we are always looking at bit 1 (not bit 0)
        bank &= tms9900.bankMask;                               // Support up to the maximum bank size using mask (based on file size as read in)
        tms9900.bankOffset = (0x2000 * bank);                   // Memory Reads will now use this offset into the Cart space...
        tms9900.cartBankPtr = MemCART+tms9900.bankOffset;       // And point to the right place in memory for cart fetches
    }
}

// --------------------------------------------------------------------------
// For the MBX, there are up to 4 banks of 4K each... These map into >7000
// We treat this like a half-banked cart with only the 4K at >7000 swapping.
// --------------------------------------------------------------------------
inline __attribute__((always_inline)) void WriteBankMBX(u8 bank)
{
    bank &= 0x3;                                            // There are up to 4 cart banks
    tms9900.bankOffset = (bank*0x1000) - 0x1000;            // The -0x1000 offsets by 4K so that the memory fetch works correctly at >7000
    tms9900.cartBankPtr = MemCART+tms9900.bankOffset;       // And point to the right place in memory for cart fetches
}

// ------------------------------------------------------------------------------
// For the few carts that use CRU banking... does not need to be in fast memory.
// This is mainly DataBiotics carts (2-8 banks) and Super Carts (only 4 banks).
// This CRU cart bank logic is borrowed from MAME and produces this map:
//   Bank     Writing '1' to CRU Address...
//     0      '1' written to >802  (>401 as we've already shifted down)
//     1      '1' written to >806  (>403 as we've already shifted down)
//     2      '1' written to >80A  (>405 as we've already shifted down)
//     3      '1' written to >80E  (>407 as we've already shifted down)
//     4      '1' written to >812  (>409 as we've already shifted down)
//     5      '1' written to >816  (>40B as we've already shifted down)
//     6      '1' written to >81A  (>40D as we've already shifted down)
//     7      '1' written to >81E  (>40F as we've already shifted down)
// ------------------------------------------------------------------------------
void cart_cru_write(u16 cruAddress, u8 dataBit)
{
    if (myConfig.cartType == CART_TYPE_PAGEDCRU)
    {
        // Paged CRU maps down to no more than 8 banks
        cruAddress &= 0xF;
        if ((cruAddress > 0) && (dataBit != 0)) // Is the bit ON and we are above the base address?
        {
            u8 bank = (cruAddress-1)/2;                         // Swap in the new bank - logic borrowed from MAME
            bank &= tms9900.bankMask;                           // Mask the 8K bank within the size of the ROM
            tms9900.bankOffset = (bank*0x2000);                 // Keep this up to date for SAVE/LOAD state
            tms9900.cartBankPtr = MemCART+tms9900.bankOffset;   // And point to the right place in memory for cart fetches
        }
        cart_cru_shadow[cruAddress] = dataBit;
    }
    else if (myConfig.cartType == CART_TYPE_SUPERCART)
    {
        // Super Cart is 4 banks of 8K for a total of 32K of RAM
        cruAddress &= 0x7;
        if ((cruAddress > 0) && (dataBit != 0)) // Is the bit ON and we are above the base address?
        {
            u8 bank = (cruAddress-1)/2;                                                 // Grab the new bank number - logic borrowed from MAME
            memcpy(MemCART+(MAX_CART_SIZE-(super_bank*0x2000)), MemCPU+0x6000, 0x2000); // Copy out the working 8K of RAM into our Super Cart Space
            memcpy(MemCPU+0x6000, MemCART+(MAX_CART_SIZE-(bank*0x2000)), 0x2000);       // Copy in the new bank of 8K RAM into our working space at >6000
            super_bank = bank;
        }
        cart_cru_shadow[cruAddress] = dataBit;
    }
}

u8 cart_cru_read(u16 cruAddress)
{
    return cart_cru_shadow[cruAddress & 0xF];   // Return shadow value of CRU bit
}

// ------------------------------------------------------------------------------------------------------------------------
// When we know we're reading RAM from the use of the Workspace Pointer (WP) and register access, we can just do this
// quickly. In theory the WP can point to 8-bit expanded RAM with a penalty but it's uncommon enough that we will not
// incur the pentaly. This means the emulation will not be cycle accurate but it's good enough to play classic TI games...
// ------------------------------------------------------------------------------------------------------------------------
inline __attribute__((always_inline)) u16 ReadWP_RAM16(u16 address)
{
    return __builtin_bswap16(*(u16*) (&MemCPU[address])); // Don't need the 0xFFFE mask here as this is always WP aligned 16-bit access
}

// --------------------------------------------------------------------------------------------------------------------
// This is the accurate version of the above which handles SAMS banking - this chews up more emulation CPU time.
// Since this is going to be slower anyway, we take the time to add in the proper cycle penalty for non-16-bit access.
// --------------------------------------------------------------------------------------------------------------------
inline __attribute__((always_inline)) u16 ReadWP_RAM16a(u16 address)
{
    if (MemType[address>>4])
    {
        AddCycleCount(4);
        if (MemType[address>>4] == MF_SAMS8)
        {
            return __builtin_bswap16(*((u16*)(theSAMS.memoryPtr[address>>12] + (address&0x0FFE))));
        }
    }
    return __builtin_bswap16(*(u16*) (&MemCPU[address])); // Don't need the 0xFFFE mask here as this is always WP aligned 16-bit access
}

// ----------------------------------------------------------------------------------------
// See comment above for ReadWP_RAM16() - not cycle accurate but good enough for WP use...
// ----------------------------------------------------------------------------------------
ITCM_CODE void WriteWP_RAM16(u16 address, u16 data)
{
    if (!MemType[address>>4] && myConfig.RAMMirrors) // If RAM mirrors enabled, handle them by writing to all 4 locations - makes the readback faster
    {
        address &= 0x00FE;
        *((u16*)(MemCPU+(0x8000 | address))) = (data << 8) | (data >> 8);
        *((u16*)(MemCPU+(0x8100 | address))) = (data << 8) | (data >> 8);
        *((u16*)(MemCPU+(0x8200 | address))) = (data << 8) | (data >> 8);
        *((u16*)(MemCPU+(0x8300 | address))) = (data << 8) | (data >> 8);
    }
    else
    {
        *((u16*)(MemCPU+address)) = (data << 8) | (data >> 8);  // Don't need the 0xFFFE mask here as this is always WP aligned 16-bit access
    }
}

// --------------------------------------------------------------------------------------------------------------------
// This is the accurate version of the above which handles SAMS banking - this chews up more emulation CPU time.
// Since this is going to be slower anyway, we take the time to add in the proper cycle penalty for non-16-bit access.
// --------------------------------------------------------------------------------------------------------------------
ITCM_CODE void WriteWP_RAM16a(u16 address, u16 data)
{
    u8 memType = MemType[address>>4];

    if (memType) AddCycleCount(4);  // Anything not intrinsic 16-bit memory incurs the penalty

    if (memType == MF_SAMS8)
    {
        *((u16*)(theSAMS.memoryPtr[address>>12] + (address&0xFFE))) = (data << 8) | (data >> 8);
    }
    else
    {
        if (!memType && myConfig.RAMMirrors) // If RAM mirrors enabled, handle them by writing to all 4 locations - makes the readback faster
        {
            address &= 0x00FE;
            *((u16*)(MemCPU+(0x8000 | address))) = (data << 8) | (data >> 8);
            *((u16*)(MemCPU+(0x8100 | address))) = (data << 8) | (data >> 8);
            *((u16*)(MemCPU+(0x8200 | address))) = (data << 8) | (data >> 8);
            *((u16*)(MemCPU+(0x8300 | address))) = (data << 8) | (data >> 8);
        }
        else
        {
            *((u16*)(MemCPU+address)) = (data << 8) | (data >> 8);  // Don't need the 0xFFFE mask here as this is always WP aligned 16-bit access
        }
    }
}

// -----------------------------------------------------------------------------------------------
// A PC fetch is always from main memory and won't trigger anything like VDP or GROM access...
// so we can be a little smarter/faster in getting the memory here. This routine does not handle
// the SAMS memory map for a big speed boost for games that don't use the expanded memory.
// -----------------------------------------------------------------------------------------------
inline __attribute__((always_inline)) u16 ReadPC16(void)
{
    u16 address = tms9900.PC; tms9900.PC+=2;

    // This will trap out anything that isn't below 0x2000 which is console ROM and heavily utilized...
    if (address & 0xE000)
    {
        u8 memType = MemType[address>>4];
        if (memType)
        {
            AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM
            if (memType == MF_CART)
            {
                return __builtin_bswap16(*(u16*) (&tms9900.cartBankPtr[address&0x1ffe]));
            }
        }
    }
    return __builtin_bswap16(*((u16*)(&MemCPU[address&0xFFFE])));
}

// -------------------------------------------------------------------------------------------------------------------------------------
// This is the accurate version of the above which handles SAMS (yes, code can run from SAMS!) - this chews up more emulation CPU time.
// -------------------------------------------------------------------------------------------------------------------------------------
ITCM_CODE u16 ReadPC16a(void)
{
    u16 address = tms9900.PC; tms9900.PC+=2;

    // This will trap out anything that isn't below 0x2000 which is console ROM and heavily utilized...
    if (address & 0xE000)
    {
        u8 memType = MemType[address>>4];
        if (memType)
        {
            AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM
            if (memType == MF_CART)
            {
                return __builtin_bswap16(*(u16*) (&tms9900.cartBankPtr[address&0x1ffe]));
            }
            else if (memType == MF_SAMS8)
            {
                return __builtin_bswap16(*((u16*)(theSAMS.memoryPtr[address>>12] + (address&0x0ffe))));
            }
        }
    }
    return __builtin_bswap16(*((u16*)(&MemCPU[address&0xFFFE])));
}



// ------------------------------------------------------------------------------------
// For some instructions (mov, movb mainly), there is a sort of intermediate read that
// places data on the bus while we wait for the write instruction to finish. We don't
// perform the actual read - nothing  in the TI world should trigger or count on this
// phantom read... we only take into account any possible cycle penalty.
// ------------------------------------------------------------------------------------
inline __attribute__((always_inline)) void PhantomMemoryRead(u16 address)
{
    if (MemType[address>>4]) AddCycleCount(4); // Any bit set is a penalty... this means it's not console ROM or the scratchpad RAM
}

// --------------------------------------------------------------------------------------------
// Technically everything in the system other than console ROM or scratchpad RAM is a 16-bit
// read with an 8-bit multiplexer... but we can be a little smarter and not waste the same
// time that an actual TI-99/4a does. The routines below are general purpose and work for
// both the accurate and fast versions of our CPU driver. We try to call into one of the above
// memory handlers whenever possible as these are a bit complex and can slow down emulation.
//
// It's important to remember that a 16-bit word in the TMS9900 architecture is split into
// the even byte (high byte) and odd byte (low byte). This is opposite of conventional
// ARM architecture so you see us swapping these bytes fairly often by left/right shifts by 8
// or by using the ARM-specific __builtin_bswap16() to accomplish this faster.
//
// So a 16-bit Memory Word on an Even Address boundary looks like this:
//
// MSB ---------------- LSB  |  MSB ---------------------- LSB
//  0  1  2  3  4  5  6  7   |   8  9  10  11  12  13  14  15
//      == EVEN BYTE ==      |          == ODD BYTE ==
//
// Further, for anything on the 8-bit bus (on the other side of the multiplexer), the system
// will always write the odd byte first, followed by the even byte. Most times this doesn't
// really matter and we'll do it however it's fastest for the emulation.
// --------------------------------------------------------------------------------------------
ITCM_CODE u16 MemoryRead16(u16 address)
{
    u16 retVal;
    address &= 0xFFFE;
    u8 memType = MemType[address>>4];

    if (memType)
    {
        AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM

        switch (memType)
        {
            case MF_CART:
                return __builtin_bswap16(*(u16*) (&tms9900.cartBankPtr[address&0x1fff]));
                break;
            case MF_SAMS8:
                return __builtin_bswap16(*((u16*)(theSAMS.memoryPtr[address>>12] + (address&0x0FFF))));
                break;
            case MF_VDP_R:
                if (address & 2) retVal = (u16)RdCtrl9918()<<8; else retVal = (u16)RdData9918()<<8;
                return retVal;
                break;
            case MF_GROMR:
                if (address & 2) { retVal = ReadGROMAddress(); retVal |= (u16)ReadGROMAddress() << 8; }
                else { retVal = ReadGROM(); retVal |= (u16)ReadGROM() << 8; }
                return retVal;
                break;
            case MF_SPEECH:
                retVal = (0x40 | 0x20);             // Satisfies the games that look for the module... Bits are empty and buffer low
                return (retVal << 8) | retVal;
                break;
            case MF_DISK:
                retVal = ReadTICCRegister(address); // Disk controller registers are mapped into this region...
                return (retVal << 8) | retVal;
                break;
            case MF_SAMS:
                retVal = SAMS_ReadBank(address);
                return (retVal << 8) | retVal;      // A 16-bit read of the SAMS register will return the bank number in both the high and low byte (AMSTEST4 requires this)
                break;
            default:
                return __builtin_bswap16(*(u16*) (&MemCPU[address]));
                break;
        }
    }

    // This is either console ROM or workspace RAM
    return __builtin_bswap16(*(u16*) (&MemCPU[address]));
}

// --------------------------------------------------------------------------------------------------
// The TI is a 16-bit system crammed into an 8-bit artchitecture (or, perhaps more acccurately, lots
// of 8-bit peripherals were jammed into a 16-bit architecture). But there are byte commands that
// will result in an 8-bit read of memory. We handle that here.
// --------------------------------------------------------------------------------------------------
ITCM_CODE u8 MemoryRead8(u16 address)
{
    u8 memType = MemType[address>>4];

    if (memType)
    {
        AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM

        switch (memType)
        {
            case MF_SAMS8:
                return *((u8*)(theSAMS.memoryPtr[address>>12] + (address&0x0FFF)));
                break;
            case MF_VDP_R:
                if (address & 2) return (u8)RdCtrl9918(); else return (u8)RdData9918();
                break;
            case MF_GROMR:
                if (address & 2) return ReadGROMAddress(); else return ReadGROM();
                break;
            case MF_CART:
                return tms9900.cartBankPtr[(address&0x1FFF)];
                break;
            case MF_SPEECH:
                // ----------------------------------------------------------------------------------------------------------
                // We use a sentinal value to mean that we will return status. If the readSpeech value is not 0x994a, then
                // we return that value to the caller - this is often used to read the first byte of Speech ROM to make
                // sure that it's an 0xAA value and thus produce TI Speech (as a way to detect if the module is attached).
                // ----------------------------------------------------------------------------------------------------------
                if (readSpeech != SPEECH_SENTINAL_VAL)
                {
                    u8 data = readSpeech;
                    readSpeech = SPEECH_SENTINAL_VAL;
                    return data;
                }
                else
                {
                    return (0x40 | 0x20);   // Satisfies the games that look for the module... Bits are empty and buffer low
                }
                break;
            case MF_DISK:
                return ReadTICCRegister(address); // Disk controller registers are mapped into this region...
                break;
            case MF_SAMS:
                return SAMS_ReadBank(address);
                break;
            default:
                return MemCPU[address];
                break;
        }
    }

    return MemCPU[address]; // This is either console ROM or workspace RAM
}


// -------------------------------------------------------------------------------------------------------------------------
// A few games like Borzork use 16-bit writes for things like Sound or VDP so those do need to be handled here along
// with the more typical 16-bit writes like the internal console RAM... For the peripherals that are 8-bits but written
// using a 16-bit operation, the odd is always written first followed by the even byte (in the TI9900 case - the high byte).
// That's why you will see below many of the 8-bit peripherals just writing the high byte shifted down 8 bits which is
// the end-result of a 16-bit write to an 8-bit periphal. We could actually write twice (odd byte then even byte) to
// more closely mimic real hardware but it's not useful and chews up valuable emulation time for the peripherals that
// don't do anything special on the odd-byte writes (however, 8-bit RAM must handle this as we do want to write both bytes).
// -------------------------------------------------------------------------------------------------------------------------
ITCM_CODE void MemoryWrite16(u16 address, u16 data)
{
    address &= 0xFFFE;

    u8 memType = MemType[address>>4];

    if (memType)
    {
        AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM

        switch (memType)
        {
            case MF_SOUND:
                sn76496W(data>>8, &snti99);
                break;
            case MF_VDP_W:
                if (address & 2) WrCtrl9918(data>>8); else WrData9918(data>>8);
                break;
            case MF_GROMW:
                if (address & 2) { WriteGROMAddress(data&0xff); WriteGROMAddress(data>>8); }
                else { WriteGROM(data&0xff); WriteGROM(data>>8); }
                break;
            case MF_CART:
                WriteBank(address);
                break;
            case MF_SAMS:
                SAMS_WriteBank(address, data>>8);
                break;
            case MF_MBX:
                if (address >= 0x6ffe) WriteBankMBX(data>>8);
                // We purposely don't use an 'else' here as the banking 'register' at >6ffe is also in the RAM area if this cart is mapped as MBX with RAM
                if (myConfig.cartType == CART_TYPE_MBX_WITH_RAM) // If it's got RAM mapped here... we treat it like RAM8
                {
                    *((u16*)(MemCPU+address)) = (data << 8) | (data >> 8);
                }
                break;
            case MF_SAMS8:
                *((u16*)(theSAMS.memoryPtr[address>>12] + (address & 0xFFF))) = (data << 8) | (data >> 8);
                break;
            case MF_RAM8:
                *((u16*)(MemCPU+address)) = (data << 8) | (data >> 8);
                break;
            default:    // Nothing to write... ignore
                break;
        }
    }
    else    // This has to be workspace RAM which deals with mirrors...
    {
        if (address & 0x8000)   // Make sure this is RAM and not an inadvertant write to Console ROM (also 16-bit)
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
}

ITCM_CODE void MemoryWrite8(u16 address, u8 data)
{
    u8 memType = MemType[address>>4];

    if (memType)
    {
        AddCycleCount(4); // Penalty for anything not internal ROM or Workspace RAM

        switch (memType)
        {
            case MF_SOUND:
                 sn76496W(data, &snti99);
                break;
            case MF_VDP_W:
                if (address & 2) WrCtrl9918(data); else WrData9918(data);
                break;
            case MF_GROMW:
                if (address & 2) WriteGROMAddress(data); else WriteGROM(data);
                break;
            case MF_CART:
                WriteBank(address);
                break;
            case MF_SPEECH:
                WriteSpeechData(data);
                break;
            case MF_SAMS:
                SAMS_WriteBank(address, data);
                break;
            case MF_MBX:
                if (address >= 0x6ffe) WriteBankMBX(data);
                if (myConfig.cartType == CART_TYPE_MBX_WITH_RAM) MemCPU[address] = data;
                break;
            case MF_DISK:
                WriteTICCRegister(address, data);  // Disk Controller
                break;
            case MF_SAMS8:
                *(theSAMS.memoryPtr[address>>12] + (address & 0xFFF)) = data;
                break;
            case MF_RAM8:
                MemCPU[address] = data;    // Expanded 32K RAM
                break;
            default:    // Nothing to write... ignore
                break;
        }
    }
    else    // This has to be workspace RAM which deals with mirrors...
    {
        if (address & 0x8000)   // Make sure this is RAM and not an inadvertant write to Console ROM
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
}


// ------------------------------------------------------------------------------------------------
// Source Address extracted from the Opcode. For this addressing mode the opcode is in the format:
// 15 14 13  12   11 10  9 8 7 6  5 4  3 2 1 0
// [OPCODE ] [B]  [TD ]  [DEST ] [TS ] [SOURCE]
// The [B] bit tells us if this is a byte addressing (0 implies word addressing)
//
// This is a heavily utilized function call and we force it as inline even though it really
// balloons our use of ITCM code space in the CPU core.
// ------------------------------------------------------------------------------------------------
static inline __attribute__((always_inline)) void Ts(u16 bytes)
{
    u16 rData = REG_GET_FROM_OPCODE();

    switch (tms9900.currentOp & 0x0030) // We could shift this down, but we're looking for maximum optimization
    {
        case 0x0000: // Rx  2c
            tms9900.srcAddress = WP_REG(rData);
            break;

        case 0x0010: // *Rx  6c
            tms9900.srcAddress = ReadWP_RAM16(WP_REG(rData));
            AddCycleCount(4);
            break;

        case 0x0020: // @yyyy(Rx) or @yyyy if Rx=0  10c
            tms9900.srcAddress = ReadPC16a();   // We use the 'a' version here not so much for accuracy but to prevent the inline which blows our ITCM fast memory
            if (rData) tms9900.srcAddress += ReadWP_RAM16(WP_REG(rData));
            AddCycleCount(8);
            break;

        default: // *Rx+   10c
            tms9900.srcAddress = ReadWP_RAM16(WP_REG(rData));
            WriteWP_RAM16(WP_REG(rData), (tms9900.srcAddress + bytes));
            AddCycleCount(((bytes&1) ? 6:8)); // Add 6 cycles for byte address... 8 for word address
            break;
    }

    if (bytes&2) tms9900.srcAddress &= 0xFFFE;   // bytes is either 1 (in which case we will utilize the LSB) or 2 (in which case we mask off to 16-bits)
}

// -----------------------------------------------------------------------------------------
// This version makes memory calls that take into account the SAMS banking. It's going to
// be a little slower and so we only swap in this version for the 'Accurate' CPU core.
// -----------------------------------------------------------------------------------------
static inline __attribute__((always_inline)) void Ts_Accurate(u16 bytes)
{
    u16 rData = REG_GET_FROM_OPCODE();

    switch (tms9900.currentOp & 0x0030)
    {
        case 0x0000: // Rx  2c
            tms9900.srcAddress = WP_REG(rData);
            break;

        case 0x0010: // *Rx  6c
            tms9900.srcAddress = ReadWP_RAM16a(WP_REG(rData));
            AddCycleCount(4);
            break;

        case 0x0020: // @yyyy(Rx) or @yyyy if Rx=0  10c
            tms9900.srcAddress = ReadPC16a();
            if (rData) tms9900.srcAddress += ReadWP_RAM16a(WP_REG(rData));
            AddCycleCount(8);
            break;

        default: // *Rx+   10c
            tms9900.srcAddress = ReadWP_RAM16a(WP_REG(rData));
            WriteWP_RAM16a(WP_REG(rData), (tms9900.srcAddress + bytes));
            AddCycleCount(((bytes&1) ? 6:8)); // Add 6 cycles for byte address... 8 for word address
            break;
    }

    if (bytes&2) tms9900.srcAddress &= 0xFFFE;   // bytes is either 1 (in which case we will utilize the LSB) or 2 (in which case we mask off to 16-bits)
}

// ------------------------------------------------------------------------------------------------------
// Destination Address extracted from the Opcode. For this addressing mode the opcode is in the format:
// 15 14 13  12   11 10  9 8 7 6  5 4  3 2 1 0
// [OPCODE ] [B]  [TD ]  [DEST ] [TS ] [SOURCE]
// The [B] bit tells us if this is a byte addressing (0 implies word addressing)
// ------------------------------------------------------------------------------------------------------
static inline __attribute__((always_inline)) void Td(u16 bytes)
{
    u16 rData = (tms9900.currentOp>>6) & 0x0F;

    switch (tms9900.currentOp & 0x0C00) // We could shift this down, but we're looking for maximum optimization
    {
        case 0x0000: // Rx  2c
            tms9900.dstAddress = WP_REG(rData);
            break;

        case 0x0400: // *Rx  6c
            tms9900.dstAddress = ReadWP_RAM16(WP_REG(rData));
            AddCycleCount(4);
            break;

        case 0x0800: // @yyyy(Rx) or @yyyy if Rx=0  10c
            tms9900.dstAddress = ReadPC16a();   // We use the 'a' version here not so much for accuracy but to prevent the inline which blows our ITCM fast memory
            if (rData) tms9900.dstAddress += ReadWP_RAM16(WP_REG(rData));
            AddCycleCount(8);
            break;

        default: // *Rx+   10c
            tms9900.dstAddress = ReadWP_RAM16(WP_REG(rData));
            WriteWP_RAM16(WP_REG(rData), (tms9900.dstAddress + bytes));
            AddCycleCount(((bytes&1) ? 6:8)); // Add 6 cycles for byte address... 8 for word address
            break;
    }

    if (bytes&2) tms9900.dstAddress &= 0xFFFE;   // bytes is either 1 (in which case we will utilize the LSB) or 2 (in which case we mask off to 16-bits)
}

// -----------------------------------------------------------------------------------------
// This version makes memory calls that take into account the SAMS banking. It's going to
// be a little slower and so we only swap in this version for the 'Accurate' CPU core.
// -----------------------------------------------------------------------------------------
static inline __attribute__((always_inline)) void Td_Accurate(u16 bytes)
{
    u16 rData = (tms9900.currentOp>>6) & 0x0F;

    switch (tms9900.currentOp & 0x0C00)
    {
        case 0x0000: // Rx  2c
            tms9900.dstAddress = WP_REG(rData);
            break;

        case 0x0400: // *Rx  6c
            tms9900.dstAddress = ReadWP_RAM16a(WP_REG(rData));
            AddCycleCount(4);
            break;

        case 0x0800: // @yyyy(Rx) or @yyyy if Rx=0  10c
            tms9900.dstAddress = ReadPC16a();
            if (rData) tms9900.dstAddress += ReadWP_RAM16a(WP_REG(rData));
            AddCycleCount(8);
            break;

        default: // *Rx+   10c
            tms9900.dstAddress = ReadWP_RAM16a(WP_REG(rData));
            WriteWP_RAM16a(WP_REG(rData), (tms9900.dstAddress + bytes));
            AddCycleCount(((bytes&1) ? 6:8)); // Add 6 cycles for byte address... 8 for word address
            break;
    }

    if (bytes&2) tms9900.dstAddress &= 0xFFFE;   // bytes is either 1 (in which case we will utilize the LSB) or 2 (in which case we mask off to 16-bits)
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
static inline __attribute__((always_inline)) void TsTd(void)
{
    u16 bytes = (tms9900.currentOp & 0x1000) ? SOURCE_BYTE:SOURCE_WORD;     // This handles both Word and Byte addresses
    Ts(bytes); Td(bytes);
}

// ---------------------------------------------------------------
// This is the version for the 'Accurate' CPU core. A bit slower.
// ---------------------------------------------------------------
static inline __attribute__((always_inline)) void TsTd_Accurate(void)
{
    u16 bytes = (tms9900.currentOp & 0x1000) ? SOURCE_BYTE:SOURCE_WORD;     // This handles both Word and Byte addresses
    Ts_Accurate(bytes); Td_Accurate(bytes);
}

// --------------------------------------------------------------------------------------
// The context switch saves the WP, PC and Status and sets up for the new workspace.
// --------------------------------------------------------------------------------------
void TMS9900_ContextSwitch(u16 address)
{
    // Do it the Classic99 way...
    u16 old_wp = tms9900.WP;                    // Save old WP
    tms9900.WP = MemoryRead16(address+0);       // Set the new Workspace
    tms9900.WP &= 0xFFFE;                       // Ensure WP is word-aligned
    MemoryWrite16(WP_REG(13), old_wp);          // Set the old Workspace Pointer
    MemoryWrite16(WP_REG(14), tms9900.PC);      // Set the old PC
    MemoryWrite16(WP_REG(15), tms9900.ST);      // Set the old Status
    tms9900.PC = MemoryRead16(address+2);       // Set the new PC based on original workspace
    tms9900.PC &= 0xFFFE;                       // Ensure PC is word-aligned
}

// ---------------------------------------------------------------------------------------
// We are only supporting two interrupt sources - the VDP as it comes from the TMS9901
// and the internal timer which is mainly used for cassette routines but is used by
// a few odd programs such as Scud Busters and the Sudoku demo from 2010. This emulation
// isn't accurate but users of DS99/4a are interested in playing Hunt the Wumpus and
// Tunnels of Doom and so we won't over complicate things...
// ---------------------------------------------------------------------------------------
void TMS9900_HandlePendingInterrupts(void)
{
    // --------------------------------------------------------------------------------
    // Literally any level except 0 will allow VDP interrupt.
    // Someday we might be more sophisticated than this... but that day is not today.
    // --------------------------------------------------------------------------------
    if ((tms9900.cpuInt & (INT_VDP | INT_TIMER)) && (tms9900.ST & ST_INTMASK))
    {
        TMS9900_ContextSwitch(1<<2);        // The TI99/4a only supports one level of interupts
        tms9900.ST &= ~ST_INTMASK;          // De-escalate the interrupt. Since we only support one level we can clear it all...
        tms9900.ST |= 1;                    // Keep the level 2 interrupt alive
        tms9900.idleReq=0;                  // And if we were waiting on IDLE, this will start the CPU back up
    }
}


// -------------------------------------------------------------
// Mainly for the X = Execute instruction (not frequently used)
// This chews up almost 20K of program space which isn't ideal
// but the other methods of handling the X instruction right
// now look less ideal... and it's only a bit of wasted memory!
// -------------------------------------------------------------
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

// --------------------------------------------------------------------------------------------------------------------------------
// A bit more CPU intensive - this runs somewhere between 10 and 15% slower on the DS but allows us to handle more
// complex emulation such as TMS9901 Timer, SAMS memory and/or the IDLE instruction. This only gets enabled when needed...
// most carts will use TMS9900_Run() instead for much improved emulation speed (mostly needed for the older DS-Lite/Phat hardware)
// --------------------------------------------------------------------------------------------------------------------------------
void TMS9900_RunAccurate(void)
{
    u32 myCounter = tms9900.cycles+228-tms9900.cycleDelta;

    // ---------------------------------------------------------------------------------------------------
    // Timer support is quite preliminary - but it's only used by cassette tape load/timeout and a tiny
    // number of other programs use it. We don't get it quite right here... we are only decrementing the
    // timer by 3 ticks every scanline which approximates the 9901 timer (64 CPU clocks per tick).
    // Classic 99 does this more accurately and checks after every instruction for a possible decrement.
    // But this is good enough for DS use and produces a roughly 46.9KHz timer which isn't too far off.
    // ---------------------------------------------------------------------------------------------------
    if (tms9901.TimerCounter)   // Has a timer been programmed?
    {
        if (tms9901.PinState[PIN_TIMER_OR_IO] == IO_MODE)   // Timer only runs when we are in IO Mode
        {
            // This is a gross misrepresentation of how the timer works... needs to be more accurate but good enough for now
            if (tms9901.TimerCounter > 3) tms9901.TimerCounter -= 3;
            else
            {
                // Timeout... possibly raise interrupt and reload timer
                TMS9901_RaiseTimerInterrupt();
                tms9901.TimerCounter = tms9901.TimerStart;
            }
        }
    }

    do
    {
        if (tms9900.cpuInt) TMS9900_HandlePendingInterrupts();
        if (tms9900.idleReq)
        {
            tms9900.cycles += 4;
            idle_counter++;
        }
        else
        {
            u8 data8;
            u16 data16;

            if (tms9900.PC == 0x40e8) HandleTICCSector();  // Disk access is not common but trap it here...
            tms9900.currentOp = ReadPC16a();
            u8 op8 = (u8)OpcodeLookup[tms9900.currentOp];

            switch (op8)
            {
// We need to swap in the 'a' = accurate versions of the memory fetch handlers
// These handlers are a bit slower but necessary to allow for SAMS banked memory access.
#define ReadWP_RAM16    ReadWP_RAM16a
#define WriteWP_RAM16   WriteWP_RAM16a
#define ReadPC16        ReadPC16a
#define Ts              Ts_Accurate
#define Td              Td_Accurate
#define TsTd            TsTd_Accurate
            #include "tms9900.inc"
#undef ReadWP_RAM16
#undef WriteWP_RAM16
#undef ReadPC16
#undef Ts
#undef Td
#undef TsTd
            }
        }
    }
    while(tms9900.cycles < myCounter);    // There are 228 CPU clocks per line on the TI

    tms9900.cycleDelta = tms9900.cycles-myCounter;
}

// --------------------------------------------------------------------------------------------------------------
// Execute 1 scanline worth of TMS9900 instructions. We keep track of the cycle delta - that is, the overage
// from one scanline to the next and we compensate the next scanline by the appopriate delta to keep things
// running at the right speed. In practice this has been good enough to render all the TI games properly.
//
// This is chewing up a big chunk of the ITCM memory. Right now the entire opcode handling is roughly 20K of
// fast instruction memory... but it buys us at least 10% speed by keeping those instructions in the cache.
// --------------------------------------------------------------------------------------------------------------
ITCM_CODE void TMS9900_Run(void)
{
    u32 myCounter = tms9900.cycles+228-tms9900.cycleDelta;

    do
    {
        u8 data8;
        u16 data16;

        if (tms9900.cpuInt) TMS9900_HandlePendingInterrupts();
        if (tms9900.PC == 0x40e8) HandleTICCSector();  // Disk access is not common but trap it here...
        tms9900.currentOp = ReadPC16();
        u8 op8 = (u8)OpcodeLookup[tms9900.currentOp];

        switch (op8)
        {
        #include "tms9900.inc"
        }
    }
    while(tms9900.cycles < myCounter);    // There are 228 CPU clocks per line on the TI

    tms9900.cycleDelta = tms9900.cycles-myCounter;
}

// End of file
