//----------------------------------------------------------------------------
//
// File:        tms9900.cpp
// Date:        23-Feb-1998
// Programmer:  Marc Rousseau
//
// Description:
//
// Copyright (c) 1998-2004 Marc Rousseau, All Rights Reserved.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
//
// Revision History:
//
//----------------------------------------------------------------------------
#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fat.h>
#include "tms9901.h"
#include "tms9900.h"

//#define MIRRORS   // Uncomment if RAM mirrors should be enabled (slows down emulation slightly)

extern UINT8 WrCtrl9918(UINT8 value);
extern UINT8 RdData9918(void);
extern UINT8 RdCtrl9918(void);
extern void WrData9918(UINT8 value);
extern void coleco_sound(UINT16 value);


extern UINT32 debug[32];

#define nullptr NULL
static UINT16   parity[ 256 ] __attribute__((section(".dtcm")));

UINT8           Memory[0x10000];            // 64K of CPU Memory Space
UINT8           MemGROM[0x10000];           // 64K of GROM Memory Space
UINT8           CartMem[1024*512];          // Cart C memory up to 512K
UINT16          MemFlags[0x10000];          // Memory flags for each address

UINT16          InterruptFlag       __attribute__((section(".dtcm")));
UINT16          WorkspacePtr        __attribute__((section(".dtcm")));
UINT16          Status              __attribute__((section(".dtcm")));
UINT32          ClockCycleCounter   __attribute__((section(".dtcm")));
UINT16          fetchPtr            __attribute__((section(".dtcm")));
UINT16          curOpCode           __attribute__((section(".dtcm")));
UINT32          bankOffset          __attribute__((section(".dtcm"))) = 0x00000000;
UINT16          gromAddress         __attribute__((section(".dtcm"))) = 0x0000;
UINT16          bankMask            __attribute__((section(".dtcm"))) = 0x003F;

UINT8           m_GromWriteShift    __attribute__((section(".dtcm")))  = 8;
UINT8           m_GromReadShift     __attribute__((section(".dtcm")))  = 8;

UINT8           bCPUIdleRequest     __attribute__((section(".dtcm")))  = 0;

UINT16 *OpCodeSpeedup __attribute__((section(".dtcm")))  = (UINT16*)0x06860000;

char tmpFilename[160];

#define WP  WorkspacePtr
#define PC  fetchPtr
#define ST  Status

#define EVER    ;;

#define FUNCTION_ENTRY(a,b,c)   // Nothing


void InvalidOpcode( )
{
    FUNCTION_ENTRY( nullptr, "InvalidOpcode", true );
}

void TI_Reset( )
{
    SetPC( 0x0000 );
    SetWP( 0x0000 );
    SetST( 0x0000 );

    // Simulate a hardware powerup
    ContextSwitch( 0 );
}

void TMS9900_Reset(char *szGame)
{
    FUNCTION_ENTRY( this, "cTMS9900 ctor", true );

    InitOpCodeLookup( );

    // Default all memory to 8-bit and then mark off the 16-bit regions directly below
    for (unsigned i=0x0000; i<=0xFFFF; i++)
    {
        MemFlags[i] = MEMFLG_8BIT;
    }

    // Mark off the memory regions that are 16-bit (for access cycle counting)
    for( unsigned i = 0x0000; i < 0x2000; i++ )
    {
        MemFlags[ i ] &= ~MEMFLG_8BIT;
    }
    for( unsigned i = 0x8000; i < 0x8400; i++ )
    {
        MemFlags[ i ] &= ~MEMFLG_8BIT;
    }
    
    // ------------------------------------------------------------------------------
    // Now mark off the hotspots where we need to take special action on read/write
    // plus all of the possible mirrors that can be used...
    // ------------------------------------------------------------------------------
    for (UINT16 address = 0x8400; address < 0x8800; address += 2)
    {
        MemFlags[address] |= MEMFLG_SOUND;   // TI Sound Chip
    }

    for (UINT16 address = 0x8800; address < 0x8C00; address += 4)
    {
        MemFlags[address+0] |= MEMFLG_VDPR;   // VDP Read Data
        MemFlags[address+2] |= MEMFLG_VDPR;   // VDP Read Status
    }

    for (UINT16 address = 0x8C00; address < 0x9000; address += 4)
    {
        MemFlags[address+0] |= MEMFLG_VDPW;   // VDP Write Data
        MemFlags[address+2] |= MEMFLG_VDPW;   // VDP Write Address
    }

    for (UINT16 address = 0x9800; address < 0x9C00; address += 4)
    {
        MemFlags[address+0] |= MEMFLG_GROMR;   // GROM Read Data
        MemFlags[address+2] |= MEMFLG_GROMR;   // GROM Read Address
    }

    for (UINT16 address = 0x9C00; address < 0xA000; address += 4)
    {
        MemFlags[address+0] |= MEMFLG_GROMW;   // GROM Write Data
        MemFlags[address+2] |= MEMFLG_GROMW;   // GROM Write Address
    }

    for (UINT16 address = 0x6000; address < 0x8000; address++)
    {
        MemFlags[address] |= MEMFLG_BANKW;   // Bank Write Data
    }
    
    bankOffset = 0x00000000;
    bCPUIdleRequest = 0;
    
    // ---------------------------------------------------------
    // Now setup for TI-Invaders as a sort of test program...
    // ---------------------------------------------------------
    FILE *infile;
    
    memset(CartMem,         0xFF, 0x10000);  // The cart is not inserted to start... 
    memset(Memory,          0xFF, 0x10000);  // Set all of memory to 0xFF (nothing mapped until proven otherwise)
    memset(&Memory[0x8000], 0x00, 0x400);    // Mark off RAM area by clearing bytes (TODO: randomize?)
    memset(&Memory[0x2000], 0x00, 0x2000);   // 8K of Low Memory Expansion from 0x2000 to 0x3FFF
    memset(&Memory[0xA000], 0x00, 0x6000);   // 24K of High Memory Expansion from 0xA000 to 0xFFFF
    
    // ------------------------------------------------------------------
    // Read in the main 16-bit console ROM and place into our Memory[]
    // ------------------------------------------------------------------
    infile = fopen("/roms/bios/994aROM.bin", "rb");
    fread(&Memory[0], 0x2000, 1, infile);       
    fclose(infile);
    
    // ----------------------------------------------------------------------------
    // Read in the main console GROM and place into the 24K of GROM memory space    
    // ----------------------------------------------------------------------------
    memset(MemGROM, 0xFF, 0x10000);
    infile = fopen("/roms/bios/994aGROM.bin", "rb");
    fread(&MemGROM[0x0000], 0x6000, 1, infile);
    fclose(infile);
    
    // -----------------------------------------------------------------------------
    // We're going to be manipulating the filename a bit so copy it into a buffer
    // -----------------------------------------------------------------------------
    strcpy(tmpFilename, szGame);
    
    u8 fileType = toupper(tmpFilename[strlen(tmpFilename)-5]);
    
    if ((fileType == 'C') || (fileType == 'G') || (fileType == 'D'))
    {
        tmpFilename[strlen(tmpFilename)-5] = 'C';   // Try to find a 'C' file
        infile = fopen(tmpFilename, "rb");
        fread(CartMem, 0x10000, 1, infile);         // Whole cart C memory up to 64K if needed...
        fclose(infile);    

        tmpFilename[strlen(tmpFilename)-5] = 'D';   // Try to find a 'D' file
        infile = fopen(tmpFilename, "rb");
        bankMask = 0x003F;
        if (infile != NULL)
        {
            bankMask = 0x0001;                          // If there is a 'D' file, it's always only 2 banks
            fread(CartMem+0x2000, 0x2000, 1, infile);   // Read 'D' file
            fclose(infile);    
        }
        memcpy(&Memory[0x6000], CartMem, 0x2000);   // First bank loaded into main memory

        // And see if there is a GROM file to go along with this load...
        tmpFilename[strlen(tmpFilename)-5] = 'G';
        infile = fopen(tmpFilename, "rb");
        if (infile != NULL)
        {
            fread(&MemGROM[0x6000], 0xA000, 1, infile);
            fclose(infile);
        }        
    }
    else // Full Load
    {
        infile = fopen(tmpFilename, "rb");
        fread(CartMem, 512*1024, 1, infile);        // Whole cart memory up to 512K if needed...
        fclose(infile);    
        memcpy(&Memory[0x6000], CartMem, 0x2000);   // First bank loaded into main memory
        bankMask = 0x003F;                          // And set the bank mask to allow any of the 8K banks
    }
    
    TI_Reset( );
    TMS9901_Reset( );
    ResetClocks();
}


void SignalInterrupt( UINT8 level )
{
    InterruptFlag |= 1 << level;
}

void ClearInterrupt( UINT8 level )
{
    InterruptFlag &= ~( 1 << level );
}

void SetPC( ADDRESS address )
{
    PC = address;
}

void SetWP( ADDRESS address )
{
    WorkspacePtr = address;
}

void SetST( UINT16 value )
{
    Status = value;
}

ADDRESS GetPC( )
{
    return PC;
}

ADDRESS GetWP( )
{
    return WorkspacePtr;
}

UINT16 GetST( )
{
    return Status;
}

UINT32 GetClocks( )
{
    return ClockCycleCounter;
}


void ResetClocks( )
{
    ClockCycleCounter = 0;
}


// ==================================================================================================
// OPCODES
// ==================================================================================================
sOpCode OpCodes[ 70 ] __attribute__((section(".dtcm"))) =
{
    { "MOVB", 0xD000, 0xF000, 1, opcode_MOVB, 14 }, // 14
    { "MOV",  0xC000, 0xF000, 1, opcode_MOV,  14 }, // 14
    { "LI",   0x0200, 0xFFE0, 8, opcode_LI,   12 }, // 12
    { "DEC",  0x0600, 0xFFC0, 6, opcode_DEC,  10 }, // 10
    { "JGT",  0x1500, 0xFF00, 2, opcode_JGT,   8 }, // 10
    { "JEQ",  0x1300, 0xFF00, 2, opcode_JEQ,   8 }, // 8/10
    { "JNE",  0x1600, 0xFF00, 2, opcode_JNE,   8 }, // 10
    { "CB",   0x9000, 0xF000, 1, opcode_CB,   14 }, // 14
    { "A",    0xA000, 0xF000, 1, opcode_A,    14 }, // 14
    { "AB",   0xB000, 0xF000, 1, opcode_AB,   14 }, // 14
    { "ABS",  0x0740, 0xFFC0, 6, opcode_ABS,  12 }, // 12/14
    { "AI",   0x0220, 0xFFE0, 8, opcode_AI,   14 }, // 14
    { "ANDI", 0x0240, 0xFFE0, 8, opcode_ANDI, 14 }, // 14
    { "B",    0x0440, 0xFFC0, 6, opcode_B,     8 }, // 8
    { "BL",   0x0680, 0xFFC0, 6, opcode_BL,   12 }, // 12
    { "BLWP", 0x0400, 0xFFC0, 6, opcode_BLWP, 26 }, // 26
    { "C",    0x8000, 0xF000, 1, opcode_C,    14 }, // 14
    { "CI",   0x0280, 0xFFE0, 8, opcode_CI,   14 }, // 14
    { "CKOF", 0x03C0, 0xFFFF, 7, opcode_CKOF, 12 }, // 12
    { "CKON", 0x03A0, 0xFFFF, 7, opcode_CKON, 12 }, // 12
    { "CLR",  0x04C0, 0xFFC0, 6, opcode_CLR,  10 }, // 10
    { "COC",  0x2000, 0xFC00, 3, opcode_COC,  14 }, // 14
    { "CZC",  0x2400, 0xFC00, 3, opcode_CZC,  14 }, // 14
    { "DECT", 0x0640, 0xFFC0, 6, opcode_DECT, 10 }, // 10
    { "DIV",  0x3C00, 0xFC00, 9, opcode_DIV,  16 }, // 16/92-124
    { "IDLE", 0x0340, 0xFFFF, 7, opcode_IDLE, 12 }, // 12
    { "INC",  0x0580, 0xFFC0, 6, opcode_INC,  10 }, // 10
    { "INCT", 0x05C0, 0xFFC0, 6, opcode_INCT, 10 }, // 10
    { "INV",  0x0540, 0xFFC0, 6, opcode_INV,  10 }, // 10
    { "JH",   0x1B00, 0xFF00, 2, opcode_JH,    8 }, // 10
    { "JHE",  0x1400, 0xFF00, 2, opcode_JHE,   8 }, // 10
    { "JL",   0x1A00, 0xFF00, 2, opcode_JL,    8 }, // 10
    { "JLE",  0x1200, 0xFF00, 2, opcode_JLE,   8 }, // 10
    { "JLT",  0x1100, 0xFF00, 2, opcode_JLT,   8 }, // 10
    { "JMP",  0x1000, 0xFF00, 2, opcode_JMP,   8 }, // 10
    { "JNC",  0x1700, 0xFF00, 2, opcode_JNC,   8 }, // 10
    { "JNO",  0x1900, 0xFF00, 2, opcode_JNO,   8 }, // 10
    { "JOC",  0x1800, 0xFF00, 2, opcode_JOC,   8 }, // 10
    { "JOP",  0x1C00, 0xFF00, 2, opcode_JOP,   8 }, // 10
    { "LDCR", 0x3000, 0xFC00, 4, opcode_LDCR, 20 }, // 20+2*bits
    { "LIMI", 0x0300, 0xFFE0, 8, opcode_LIMI, 16 }, // 16
    { "LREX", 0x03E0, 0xFFFF, 7, opcode_LREX, 12 }, // 12
    { "LWPI", 0x02E0, 0xFFE0, 8, opcode_LWPI, 10 }, // 10
    { "MPY",  0x3800, 0xFC00, 9, opcode_MPY,  52 }, // 52
    { "NEG",  0x0500, 0xFFC0, 6, opcode_NEG,  12 }, // 12
    { "ORI",  0x0260, 0xFFE0, 8, opcode_ORI,  14 }, // 14
    { "RSET", 0x0360, 0xFFFF, 7, opcode_RSET, 12 }, // 12
    { "RTWP", 0x0380, 0xFFFF, 7, opcode_RTWP, 14 }, // 14
    { "S",    0x6000, 0xF000, 1, opcode_S,    14 }, // 14
    { "SB",   0x7000, 0xF000, 1, opcode_SB,   14 }, // 14
    { "SBO",  0x1D00, 0xFF00, 2, opcode_SBO,  12 }, // 12
    { "SBZ",  0x1E00, 0xFF00, 2, opcode_SBZ,  12 }, // 12
    { "SETO", 0x0700, 0xFFC0, 6, opcode_SETO, 10 }, // 10
    { "SLA",  0x0A00, 0xFF00, 5, opcode_SLA,  12 }, // 12+2*disp/20+2*disp
    { "SOC",  0xE000, 0xF000, 1, opcode_SOC,  14 }, // 14
    { "SOCB", 0xF000, 0xF000, 1, opcode_SOCB, 14 }, // 14
    { "SRA",  0x0800, 0xFF00, 5, opcode_SRA,  12 }, // 12+2*disp/20+2*disp
    { "SRC",  0x0B00, 0xFF00, 5, opcode_SRC,  12 }, // 12+2*disp/20+2*disp
    { "SRL",  0x0900, 0xFF00, 5, opcode_SRL,  12 }, // 12+2*disp/20+2*disp
    { "STCR", 0x3400, 0xFC00, 4, opcode_STCR, 42 }, // 42/44/58/60
    { "STST", 0x02C0, 0xFFE0, 8, opcode_STST,  8 }, // 8
    { "STWP", 0x02A0, 0xFFE0, 8, opcode_STWP,  8 }, // 8
    { "SWPB", 0x06C0, 0xFFC0, 6, opcode_SWPB, 10 }, // 10
    { "SZC",  0x4000, 0xF000, 1, opcode_SZC,  14 }, // 14
    { "SZCB", 0x5000, 0xF000, 1, opcode_SZCB, 14 }, // 14
    { "TB",   0x1F00, 0xFF00, 2, opcode_TB,   12 }, // 12
    { "X",    0x0480, 0xFFC0, 6, opcode_X,     8 }, // 8
    { "XOP",  0x2C00, 0xFC00, 9, opcode_XOP,  36 }, // 36
    { "XOR",  0x2800, 0xFC00, 3, opcode_XOR,  14 }, // 14
    { "INVL", 0x0000, 0x0000, 0, InvalidOpcode,6 }    
};


//#define GROM_INC(x) ((x&0xE000) | ((x+1)&0x1FFF))
#define GROM_INC(x) (x+1)

inline UINT8 ReadGROM(void)
{
    ClockCycleCounter += 19;
    UINT8 retval = MemGROM[gromAddress];
    gromAddress = GROM_INC(gromAddress);
    return retval;
}

ITCM_CODE UINT8 ReadGROMAddress(void)
{
    m_GromWriteShift = 8;    
    ClockCycleCounter += 13;
    UINT8 data = ( UINT8 ) ((( gromAddress + 1 ) >> m_GromReadShift ) & 0x00FF );
    m_GromReadShift  = 8 - m_GromReadShift;
    return data;    
}

ITCM_CODE void WriteGROM(UINT8 data)
{
    // Does nothing...
}

ITCM_CODE void WriteGROMAddress(UINT8 data)
{
    ClockCycleCounter += ( m_GromWriteShift ? 15 : 21 );
    
    gromAddress &= ( ADDRESS ) ( 0xFF00 >> m_GromWriteShift );
    gromAddress |= ( ADDRESS ) ( data << m_GromWriteShift );
    m_GromWriteShift = 8 - m_GromWriteShift;
    m_GromReadShift  = 8;
}


inline UINT16 ReadPCMemoryW( UINT16 address )
{
    UINT8 flags = MemFlags[ address ];
    if (flags)
    {
        if (flags & MEMFLG_8BIT) ClockCycleCounter += 4; // Penalty for 8-bit access...
        if (flags & MEMFLG_BANKW)
        {
            return (CartMem[bankOffset | (address&0x1FFF)] << 8) | (CartMem[bankOffset | ((address+1)&0x1FFF)]);
        }
        else return (Memory[address] << 8) | (Memory[address+1]);
    }
    else return (Memory[address] << 8) | (Memory[address+1]);
}
    
ITCM_CODE UINT16 ReadMemoryW( UINT16 address )
{
    UINT16 retVal;
    address &= 0xFFFE;
    UINT8 flags = MemFlags[ address ];

    // Add 4 clock cycles if we're accessing 8-bit memory
    ClockCycleCounter += (flags & MEMFLG_8BIT) ? 6:2;
    
    if (flags)
    {
        if (flags & MEMFLG_VDPR)
        {
            if (address & 2) retVal = (UINT16)RdCtrl9918()<<8;
            else retVal = (UINT16)RdData9918()<<8;
        }
        else if (flags & MEMFLG_GROMR)
        {
            if (address & 2) 
            {
                retVal = ReadGROMAddress();
                retVal |= (UINT16)ReadGROMAddress() << 8;
            }
            else
            {
                retVal = ReadGROM();
                retVal |= (UINT16)ReadGROM() << 8;
            }
        }
        else if (flags & MEMFLG_BANKW)
        {
            retVal = (CartMem[bankOffset | (address&0x1FFF)] << 8) | (CartMem[bankOffset | ((address+1)&0x1FFF)]);
        }
        else
        {
            retVal = (Memory[address] << 8) | (Memory[address+1]);
        }
    }
    else
    {
        retVal = (Memory[address] << 8) | (Memory[address+1]);
    }

    return retVal;
}

ITCM_CODE UINT8 ReadMemoryB( UINT16 address )
{
    UINT8 flags = MemFlags[ address ];

    UINT8 retVal;

    // Add 4 clock cycles if we're accessing 8-bit memory
    ClockCycleCounter += (flags & MEMFLG_8BIT) ? 6:2;

    if (flags & MEMFLG_VDPR)
    {
        if (address & 2)
        {
            retVal = (UINT8)RdCtrl9918();
        }
        else 
        {
            retVal = (UINT8)RdData9918();
        }
    }
    else if (flags & MEMFLG_GROMR)
    {
        if (address & 2) 
        {
            retVal = ReadGROMAddress();
        }
        else
        {
            retVal = ReadGROM();
        }
    }
    else if (flags & MEMFLG_BANKW)
    {
        retVal = (CartMem[bankOffset | (address&0x1FFF)]);
    }
    else
    {
        retVal = Memory[address];
    }
    return retVal;
}

inline void WriteBank(UINT16 address)
{
    address = (address >> 1);   // Divide by 2 as we are always looking at bit 1
    address &= bankMask;        // Support up to 8 banks of 8K (64K total)
    bankOffset = (0x2000 * address);
}

ITCM_CODE void WriteMemoryW( UINT16 address, UINT16 value )
{
    address &= 0xFFFE;

    UINT8 flags = MemFlags[ address ];

    // Add 4 clock cycles if we're accessing 8-bit memory
    ClockCycleCounter += (flags & MEMFLG_8BIT) ? 6:2;

    if (flags)
    {
        if (flags & MEMFLG_SOUND)
        {
            coleco_sound(value);
        }
        else if (flags & MEMFLG_VDPW)
        {
            if (address & 2) {if (WrCtrl9918(value>>8)) tms9901_SignalInterrupt(2);}
            else WrData9918(value>>8);
        }
        else if (flags & MEMFLG_GROMW)
        {
            if (address & 2) 
            {
                WriteGROM(value&0x00FF);
                WriteGROM((value&0xFF00)>>8);
            }
            else
            {
                WriteGROM(value&0x00FF);
                WriteGROM((value&0xFF00)>>8);
            }
        }
        else if (flags & MEMFLG_BANKW)
        {
            WriteBank(address);
        }
        
        // Possible 8-bit expanded RAM write
        if ((address >= 0x2000 && address <= 0x4000) || (address >= 0xA000))
        {
            Memory[address] = (value >> 8);
            Memory[address+1] = value & 0xFF;
        }
    }
    else
    {
#ifdef MIRRORS
        Memory[0x8000 | ((address+0)&0xFF)] = (value>>8)&0xFF;
        Memory[0x8000 | ((address+1)&0xFF)] = (value>>0)&0xFF;
        Memory[0x8100 | ((address+0)&0xFF)] = (value>>8)&0xFF;
        Memory[0x8100 | ((address+1)&0xFF)] = (value>>0)&0xFF;
        Memory[0x8200 | ((address+0)&0xFF)] = (value>>8)&0xFF;
        Memory[0x8200 | ((address+1)&0xFF)] = (value>>0)&0xFF;
        Memory[0x8300 | ((address+0)&0xFF)] = (value>>8)&0xFF;
        Memory[0x8300 | ((address+1)&0xFF)] = (value>>0)&0xFF;
#else
        Memory[address] = (value >> 8);
        Memory[address+1] = value;
#endif        
    }
}

ITCM_CODE void WriteMemoryB( UINT16 address, UINT8 value )
{
    UINT8 flags = MemFlags[ address ];

    // Add 4 clock cycles if we're accessing 8-bit memory
    ClockCycleCounter += (flags & MEMFLG_8BIT) ? 6:2;

    if (flags & 0xFB)
    {
        if (flags & MEMFLG_SOUND)
        {
            coleco_sound(value);
        }
        else if (flags & MEMFLG_VDPW)
        {
            if (address & 2) {if (WrCtrl9918(value)) tms9901_SignalInterrupt(2);}
            else WrData9918(value);
        }
        else if (flags & MEMFLG_GROMW)
        {
            if (address & 2) 
            {
                WriteGROMAddress(value);
            }
            else
            {
                WriteGROM(value);
            }
        }
        else if (flags & MEMFLG_BANKW)
        {
            WriteBank(address);
        }
    }
    else
    {
        if (address >= 0x8000 && address < 0x8400)
        {
    #ifdef MIRRORS        
            Memory[0x8000 | (address&0xFF)] = value;
            Memory[0x8100 | (address&0xFF)] = value;
            Memory[0x8200 | (address&0xFF)] = value;
            Memory[0x8300 | (address&0xFF)] = value;
    #else
            Memory[address] = value;
    #endif        
        }
        else if ((address >= 0x2000 && address < 0x4000) || (address >= 0xA000))
        {
            Memory[address] = value;
        }
    }
}

inline UINT16 Fetch( )
{
    ClockCycleCounter += 2; // For the memory fetch pre-compensated
    UINT16 retVal = ReadPCMemoryW( PC ); PC += 2;
    return retVal;
}

void LookupOpCode( UINT16 opcode )
{
    for (UINT8 i=0; i<69; i++)
    {
        if ((opcode & OpCodes[i].mask) == OpCodes[i].opCode) 
        {
            OpCodeSpeedup[opcode] = i;
            return;
        }
    }
    OpCodeSpeedup[opcode] = 69; //Invalid
    return;
}


void InitOpCodeLookup(void)
{
    // Fill in the parity table
    for( UINT16 i = 0; i < 256; i++ )
    {
        UINT16 value = i;
        value ^= value >> 1;
        value ^= value >> 4;
        value ^= value >> 2;
        parity[ i ] = ( value & 1 ) ? TMS_PARITY : 0;
    }
    
    memset(OpCodeSpeedup, 0xFF, 0x20000);
    for (int i=0; i<65536; i++)
    {
        (void)LookupOpCode(i);
    }
}


inline void _ExecuteInstruction( UINT16 opCode )
{
    sOpCode *op = &OpCodes[OpCodeSpeedup[opCode]];
    ClockCycleCounter += (op->clocks-2);
    ((void (*)( ))op->function )( );
}


//             T   Clk Acc
// Rx          00   0   0          Register
// *Rx         01   4   1          Register Indirect
// *Rx+        11   6   2 (byte)   Auto-increment
// *Rx+        11   8   2 (word)   Auto-increment
// @>xxxx      10   8   1          Symbolic Memory
// @>xxxx(Rx)  10   8   2          Indexed Memory
//

ITCM_CODE UINT16 GetAddress( UINT16 opCode, size_t size )
{
    UINT16 address = 0x0000;
    int reg = opCode & 0x0F;

    switch( opCode & 0x0030 )
    {
        case 0x0000 :
            address = ( UINT16 ) ( WP + 2 * reg );
            break;
        case 0x0010 :
            address = ReadMemoryW( WP + 2 * reg );
            break;
        case 0x0030 :
            address = ReadMemoryW( WP + 2 * reg );
            WriteMemoryW( WP + 2 * reg, ( UINT16 ) ( address + size ));
            break;
        case 0x0020 :
            address = Fetch( );
            if( reg )
            {
                address += ReadMemoryW( WP + 2 * reg );
            }
            break;
    }

    if( size == 2 )
    {
        address &= 0xFFFE;
    }

    return address;
}

ITCM_CODE void ContextSwitch( UINT16 address )
{
    UINT16 newWP = ReadMemoryW( address );
    UINT16 newPC = ReadMemoryW( address + 2 );

    UINT16 oldWP = WP;
    UINT16 oldPC = PC;
    WP = newWP;
    PC = newPC;

    WriteMemoryW( WP + 2 * 13, oldWP );
    WriteMemoryW( WP + 2 * 14, oldPC );
    WriteMemoryW( WP + 2 * 15, ST    );
}

ITCM_CODE bool CheckInterrupt( )
{
    // Tell the PIC to update it's timer and turn off old interrupts
    if (m_ClockRegister)
    {
        UpdateTimer( ClockCycleCounter );
    }

    // Look for pending unmasked interrupts
    UINT16 mask = ( UINT16 ) (( 2 << ( ST & 0x0F )) - 1 );
    UINT16 pending = InterruptFlag & mask;

    if( pending == 0 )
    {
        return false;
    }

    // Find the highest priority interrupt
    int level = 0;
    mask = 1;
    while(( pending & mask ) == 0 )
    {
        level++;
        mask <<= 1;
    }

    ContextSwitch( level * 4 );
    
    if( level != 0 )
    {
        ST &= 0xFFF0;
        ST |= level - 1;
    }

    return true;
}

// Execute 1 scanline...
ITCM_CODE void TMS9900_Run()
{
    UINT32 myCounter = ClockCycleCounter+228;

    do
    {
        if (CheckInterrupt()) bCPUIdleRequest=0;

        if (bCPUIdleRequest)
        {
            ClockCycleCounter += 4;
        }
        else
        {
            curOpCode = ReadPCMemoryW( PC ); PC+=2;
            sOpCode *op = &OpCodes[OpCodeSpeedup[curOpCode]];
            ClockCycleCounter += op->clocks;
            ((void (*)( ))op->function )( );
        }
    }
    while(ClockCycleCounter < myCounter);    // There are 228 CPU clocks per line on the TI
}


static void SetFlags_LAE( UINT16 val )
{
    if(( INT16 ) val > 0 )
    {
        ST |= TMS_LOGICAL | TMS_ARITHMETIC;
    }
    else if(( INT16 ) val < 0 )
    {
        ST |= TMS_LOGICAL;
    }
    else
    {
        ST |= TMS_EQUAL;
    }
}

static void SetFlags_LAE2( UINT16 val1, UINT16 val2 )
{
    if( val1 == val2 )
    {
        ST |= TMS_EQUAL;
    }
    else
    {
        if(( INT16 ) val1 > ( INT16 ) val2 )
        {
            ST |= TMS_ARITHMETIC;
        }
        if( val1 > val2 )
        {
            ST |= TMS_LOGICAL;
        }
    }
}

static void SetFlags_difW( UINT16 val1, UINT16 val2, UINT32 res )
{
    if( !( res & 0x00010000 ))
    {
        ST |= TMS_CARRY;
    }
    if(( val1 ^ val2 ) & ( val2 ^ res ) & 0x8000 )
    {
        ST |= TMS_OVERFLOW;
    }
    SetFlags_LAE(( UINT16 ) res );
}

static void SetFlags_difB( UINT8 val1, UINT8 val2, UINT32 res )
{
    if( !( res & 0x0100 ))
    {
        ST |= TMS_CARRY;
    }
    if(( val1 ^ val2 ) & ( val2 ^ res ) & 0x80 )
    {
        ST |= TMS_OVERFLOW;
    }
    SetFlags_LAE(( INT8 ) res );
    ST |= parity[ ( UINT8 ) res ];
}

static void SetFlags_sumW( UINT16 val1, UINT16 val2, UINT32 res )
{
    if( res & 0x00010000 )
    {
        ST |= TMS_CARRY;
    }
    if(( res ^ val1 ) & ( res ^ val2 ) & 0x8000 )
    {
        ST |= TMS_OVERFLOW;
    }
    SetFlags_LAE(( UINT16 ) res );
}

static void SetFlags_sumB( UINT8 val1, UINT8 val2, UINT32 res )
{
    if( res & 0x0100 )
    {
        ST |= TMS_CARRY;
    }
    if(( res ^ val1 ) & ( res ^ val2 ) & 0x80 )
    {
        ST |= TMS_OVERFLOW;
    }
    SetFlags_LAE(( INT8 ) res );
    ST |= parity[ ( UINT8 ) res ];
}

//-----------------------------------------------------------------------------
//   LI     Format: VIII    Op-code: 0x0200     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_LI( )
{
    UINT16 value = Fetch( );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE( value );

    WriteMemoryW( WP + 2 * ( curOpCode & 0x000F ), value );
}

//-----------------------------------------------------------------------------
//   AI     Format: VIII    Op-code: 0x0220     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_AI( )
{
    int reg = curOpCode & 0x000F;

    UINT32 src = ReadMemoryW( WP + 2 * reg );
    UINT32 dst = Fetch( );
    UINT32 sum = src + dst;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_sumW(( UINT16 ) src, ( UINT16 ) dst, sum );

    WriteMemoryW( WP + 2 * reg, ( UINT16 ) sum );
}

//-----------------------------------------------------------------------------
//   ANDI   Format: VIII    Op-code: 0x0240     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_ANDI( )
{
    int reg = ( UINT16 ) ( curOpCode & 0x000F );
    UINT16 value = ReadMemoryW( WP + 2 * reg );
    value &= Fetch( );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE( value );

    WriteMemoryW( WP + 2 * reg, value );
}

//-----------------------------------------------------------------------------
//   ORI    Format: VIII    Op-code: 0x0260     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_ORI( )
{
    int reg = ( UINT16 ) ( curOpCode & 0x000F );
    UINT16 value = ReadMemoryW( WP + 2 * reg );
    value |= Fetch( );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE( value );

    WriteMemoryW( WP + 2 * reg, value );
}

//-----------------------------------------------------------------------------
//   CI     Format: VIII    Op-code: 0x0280     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_CI( )
{
    UINT16 src = ReadMemoryW( WP + 2 * ( curOpCode & 0x000F ));
    UINT16 dst = Fetch( );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE2( src, dst );
}

//-----------------------------------------------------------------------------
//   STWP   Format: VIII    Op-code: 0x02A0     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_STWP( )
{
    WriteMemoryW( WP + 2 * ( curOpCode & 0x000F ), WP );
}

//-----------------------------------------------------------------------------
//   STST   Format: VIII    Op-code: 0x02C0     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_STST( )
{
    WriteMemoryW( WP + 2 * ( curOpCode & 0x000F ), ST );
}

//-----------------------------------------------------------------------------
//   LWPI   Format: VIII    Op-code: 0x02E0     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_LWPI( )
{
    WP = Fetch( );
}

//-----------------------------------------------------------------------------
//   LIMI   Format: VIII    Op-code: 0x0300     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_LIMI( )
{
    ST = ( UINT16 ) (( ST & 0xFFF0 ) | ( Fetch( ) & 0x0F ));
}

//-----------------------------------------------------------------------------
//   IDLE   Format: VII Op-code: 0x0340     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_IDLE( )
{
    bCPUIdleRequest = true;
}

//-----------------------------------------------------------------------------
//   RSET   Format: VII Op-code: 0x0360     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_RSET( )
{
    // Set the interrupt mask to 0
    ST &= 0xFFF0;
}

//-----------------------------------------------------------------------------
//   RTWP   Format: VII Op-code: 0x0380     Status: L A E C O P X
//-----------------------------------------------------------------------------
void opcode_RTWP( )
{
    ST = ReadMemoryW( WP + 2 * 15 );
    PC = ReadMemoryW( WP + 2 * 14 );
    WP = ReadMemoryW( WP + 2 * 13 );
}

//-----------------------------------------------------------------------------
//   CKON   Format: VII Op-code: 0x03A0     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_CKON( )
{
}

//-----------------------------------------------------------------------------
//   CKOF   Format: VII Op-code: 0x03C0     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_CKOF( )
{
}

//-----------------------------------------------------------------------------
//   LREX   Format: VII Op-code: 0x03E0     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_LREX( )
{
}

//-----------------------------------------------------------------------------
//   BLWP   Format: VI  Op-code: 0x0400     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_BLWP( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    ContextSwitch( address );
}

//-----------------------------------------------------------------------------
//   B      Format: VI  Op-code: 0x0440     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_B( )
{
    PC = GetAddress( curOpCode, 2 );
    PC &= 0xFFFE;
}

//-----------------------------------------------------------------------------
//   X      Format: VI  Op-code: 0x0480     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_X( )
{
    curOpCode = ReadMemoryW( GetAddress( curOpCode, 2 ));
    _ExecuteInstruction( curOpCode );
}

//-----------------------------------------------------------------------------
//   CLR    Format: VI  Op-code: 0x04C0     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_CLR( )
{
    UINT16 address = GetAddress( curOpCode, 2 );

    // Hidden memory access
    ReadMemoryW( address );

    WriteMemoryW( address, ( UINT16 ) 0 );
}

//-----------------------------------------------------------------------------
//   NEG    Format: VI  Op-code: 0x0500     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_NEG( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( address );

    UINT32 dst = 0 - src;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_LAE(( UINT16 ) dst );
    if( src == 0x8000 )
    {
        ST |= TMS_OVERFLOW;
    }
    if( src == 0x0000 )
    {
        ST |= TMS_CARRY;
    }

    WriteMemoryW( address, ( UINT16 ) dst );
}

//-----------------------------------------------------------------------------
//   INV    Format: VI  Op-code: 0x0540     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_INV( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT16 value = ~ReadMemoryW( address );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE( value );

    WriteMemoryW( address, value );
}

//-----------------------------------------------------------------------------
//   INC    Format: VI  Op-code: 0x0580     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_INC( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( address );

    UINT32 sum = src + 1;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_sumW(( UINT16 ) src, 1, sum );

    WriteMemoryW( address, ( UINT16 ) sum );
}

//-----------------------------------------------------------------------------
//   INCT   Format: VI  Op-code: 0x05C0     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_INCT( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( address );

    UINT32 sum = src + 2;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_sumW(( UINT16 ) src, 2, sum );

    WriteMemoryW( address, ( UINT16 ) sum );
}

//-----------------------------------------------------------------------------
//   DEC    Format: VI  Op-code: 0x0600     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_DEC( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( address );

    UINT32 dif = src - 1;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_difW(1, ( UINT16 ) src, dif );

    WriteMemoryW( address, ( UINT16 ) dif );
}

//-----------------------------------------------------------------------------
//   DECT   Format: VI  Op-code: 0x0640     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_DECT( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( address );

    UINT32 dif = src - 2;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_difW( 2, ( UINT16 ) src, dif );

    WriteMemoryW( address, ( UINT16 ) dif );
}

//-----------------------------------------------------------------------------
//   BL     Format: VI  Op-code: 0x0680     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_BL( )
{
    UINT16 address = GetAddress( curOpCode, 2 );

    WriteMemoryW( WP + 2 * 11, PC );

    PC = address;
}

//-----------------------------------------------------------------------------
//   SWPB   Format: VI  Op-code: 0x06C0     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_SWPB( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT16 value = ReadMemoryW( address );

    value = ( UINT16 ) (( value << 8 ) | ( value >> 8 ));

    WriteMemoryW( address, ( UINT16 ) value );
}

//-----------------------------------------------------------------------------
//   SETO   Format: VI  Op-code: 0x0700     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_SETO( )
{
    UINT16 address = GetAddress( curOpCode, 2 );

    // Hidden memory access
    ReadMemoryW( address );

    WriteMemoryW( address, ( UINT16 ) -1 );
}

//-----------------------------------------------------------------------------
//   ABS    Format: VI  Op-code: 0x0740     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_ABS( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT16 dst = ReadMemoryW( address );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_LAE( dst );

    if( dst & 0x8000 )
    {
        ClockCycleCounter += 2;
        WriteMemoryW( address, -dst );
        if( dst == 0x8000 )
        {
            ST |= TMS_OVERFLOW;
        }
    }
}

//-----------------------------------------------------------------------------
//   SRA    Format: V   Op-code: 0x0800     Status: L A E C - - -
//-----------------------------------------------------------------------------
void opcode_SRA( )
{
    int reg = curOpCode & 0x000F;
    unsigned int count = ( curOpCode >> 4 ) & 0x000F;
    if( count == 0 )
    {
        ClockCycleCounter += 8;
        count = ReadMemoryW( WP + 2 * 0 ) & 0x000F;
        if( count == 0 )
        {
            count = 16;
        }
    }

    ClockCycleCounter += 2 * count;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY );

    INT16 value = ( INT16 ) ((( INT16 ) ReadMemoryW( WP + 2 * reg )) >> --count );
    if( value & 1 )
    {
        ST |= TMS_CARRY;
    }
    value >>= 1;
    SetFlags_LAE( value );

    WriteMemoryW( WP + 2 * reg, ( UINT16 ) value );
}

//-----------------------------------------------------------------------------
//   SRL    Format: V   Op-code: 0x0900     Status: L A E C - - -
//-----------------------------------------------------------------------------
void opcode_SRL( )
{
    int reg = curOpCode & 0x000F;
    unsigned int count = ( curOpCode >> 4 ) & 0x000F;
    if( count == 0 )
    {
        ClockCycleCounter += 8;
        count = ReadMemoryW( WP + 2 * 0 ) & 0x000F;
        if( count == 0 )
        {
            count = 16;
        }
    }

    ClockCycleCounter += 2 * count;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY );

    UINT16 value = ( UINT16 ) ( ReadMemoryW( WP + 2 * reg ) >> --count );
    if( value & 1 )
    {
        ST |= TMS_CARRY;
    }
    value >>= 1;
    SetFlags_LAE( value );

    WriteMemoryW( WP + 2 * reg, value );
}

//-----------------------------------------------------------------------------
//   SLA    Format: V   Op-code: 0x0A00     Status: L A E C O - -
//
// Comments: The overflow bit is set if the sign changes during the shift
//-----------------------------------------------------------------------------
void opcode_SLA( )
{
    int reg = curOpCode & 0x000F;
    unsigned int count = ( curOpCode >> 4 ) & 0x000F;
    if( count == 0 )
    {
        ClockCycleCounter += 8;
        count = ReadMemoryW( WP + 2 * 0 ) & 0x000F;
        if( count == 0 )
        {
            count = 16;
        }
    }

    ClockCycleCounter += 2 * count;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );

    UINT32 value = ReadMemoryW( WP + 2 * reg ) << count;

    UINT32 mask = (( UINT16 ) -1 << count ) & 0xFFFF8000;
    int bits = value & mask;

    if( value & 0x00010000 )
    {
        ST |= TMS_CARRY;
    }
    if( bits && (( bits ^ mask ) || ( count == 16 )))
    {
        ST |= TMS_OVERFLOW;
    }
    SetFlags_LAE(( UINT16 ) value );

    WriteMemoryW( WP + 2 * reg, ( UINT16 ) value );
}

//-----------------------------------------------------------------------------
//   SRC    Format: V   Op-code: 0x0B00     Status: L A E C - - -
//-----------------------------------------------------------------------------
void opcode_SRC( )
{
    int reg = curOpCode & 0x000F;
    unsigned int count = ( curOpCode >> 4 ) & 0x000F;
    if( count == 0 )
    {
        ClockCycleCounter += 8;
        count = ReadMemoryW( WP + 2 * 0 ) & 0x000F;
        if( count == 0 )
        {
            count = 16;
        }
    }

    ClockCycleCounter += 2 * count;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY );

    int value = ReadMemoryW( WP + 2 * reg );
    value = (( value << 16 ) | value ) >> count;
    if( value & 0x8000 )
    {
        ST |= TMS_CARRY;
    }
    SetFlags_LAE(( UINT16 ) value );

    WriteMemoryW( WP + 2 * reg, ( UINT16 ) value );
}

//-----------------------------------------------------------------------------
//   JMP    Format: II  Op-code: 0x1000     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JMP( )
{
    ClockCycleCounter += 2;
    PC += 2 * ( INT8 ) curOpCode;
}

//-----------------------------------------------------------------------------
//   JLT    Format: II  Op-code: 0x1100     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JLT( )
{
    if( !( ST & ( TMS_ARITHMETIC | TMS_EQUAL )))
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JLE    Format: II  Op-code: 0x1200     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JLE( )
{
    if(( !( ST & TMS_LOGICAL )) | ( ST & TMS_EQUAL ))
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JEQ    Format: II  Op-code: 0x1300     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JEQ( )
{
    if( ST & TMS_EQUAL )
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JHE    Format: II  Op-code: 0x1400     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JHE( )
{
    if( ST & ( TMS_LOGICAL | TMS_EQUAL ))
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JGT    Format: II  Op-code: 0x1500     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JGT( )
{
    if( ST & TMS_ARITHMETIC )
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JNE    Format: II  Op-code: 0x1600     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JNE( )
{
    if( !( ST & TMS_EQUAL ))
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JNC    Format: II  Op-code: 0x1700     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JNC( )
{
    if( !( ST & TMS_CARRY ))
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JOC    Format: II  Op-code: 0x1800     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JOC( )
{
    if( ST & TMS_CARRY )
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JNO    Format: II  Op-code: 0x1900     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JNO( )
{
    if( !( ST & TMS_OVERFLOW ))
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JL     Format: II  Op-code: 0x1A00     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JL( )
{
    if( !( ST & ( TMS_LOGICAL | TMS_EQUAL )))
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JH     Format: II  Op-code: 0x1B00     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JH( )
{
    if(( ST & TMS_LOGICAL ) && !( ST & TMS_EQUAL ))
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   JOP    Format: II  Op-code: 0x1C00     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_JOP( )
{
    if( ST & TMS_PARITY )
    {
        opcode_JMP( );
    }
}

//-----------------------------------------------------------------------------
//   SBO    Format: II  Op-code: 0x1D00     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_SBO( )
{
    int cru = ( ReadMemoryW( WP + 2 * 12 ) >> 1 ) + ( curOpCode & 0x00FF );
    ClockCycleCounter += 2;
    WriteCRU( cru, 1, 1 );
}

//-----------------------------------------------------------------------------
//   SBZ    Format: II  Op-code: 0x1E00     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_SBZ( )
{
    int cru = ( ReadMemoryW( WP + 2 * 12 ) >> 1 ) + ( curOpCode & 0x00FF );
    ClockCycleCounter += 2;
    WriteCRU( cru, 1, 0 );
}

//-----------------------------------------------------------------------------
//   TB     Format: II  Op-code: 0x1F00     Status: - - E - - - -
//-----------------------------------------------------------------------------
void opcode_TB( )
{
    int cru = ( ReadMemoryW( WP + 2 * 12 ) >> 1 ) + ( curOpCode & 0x00FF );
    ClockCycleCounter += 2;
    if( ReadCRU( cru, 1 ) & 1 )
    {
        ST |= TMS_EQUAL;
    }
    else
    {
        ST &= ~TMS_EQUAL;
    }
}

//-----------------------------------------------------------------------------
//   COC    Format: III Op-code: 0x2000     Status: - - E - - - -
//-----------------------------------------------------------------------------
void opcode_COC( )
{
    UINT16 src = ReadMemoryW( WP + 2 * (( curOpCode >> 6 ) & 0x000F ));
    UINT16 dst = ReadMemoryW( GetAddress( curOpCode, 2 ));
    if(( src & dst ) == dst )
    {
        ST |= TMS_EQUAL;
    }
    else
    {
        ST &= ~TMS_EQUAL;
    }
}

//-----------------------------------------------------------------------------
//   CZC    Format: III Op-code: 0x2400     Status: - - E - - - -
//-----------------------------------------------------------------------------
void opcode_CZC( )
{
    UINT16 src = ReadMemoryW( WP + 2 * (( curOpCode >> 6 ) & 0x000F ));
    UINT16 dst = ReadMemoryW( GetAddress( curOpCode, 2 ));
    if(( ~src & dst ) == dst )
    {
        ST |= TMS_EQUAL;
    }
    else
    {
        ST &= ~TMS_EQUAL;
    }
}

//-----------------------------------------------------------------------------
//   XOR    Format: III Op-code: 0x2800     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_XOR( )
{
    int reg = ( curOpCode >> 6 ) & 0x000F;
    UINT16 address = GetAddress( curOpCode, 2 );
    UINT16 value = ReadMemoryW( WP + 2 * reg );
    value ^= ReadMemoryW( address );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE( value );

    WriteMemoryW( WP + 2 * reg, value );
}

//-----------------------------------------------------------------------------
//   XOP    Format: IX  Op-code: 0x2C00     Status: - - - - - - X
//-----------------------------------------------------------------------------
void opcode_XOP( )
{
    UINT16 address = GetAddress( curOpCode, 2 );
    int level = (( curOpCode >> 4 ) & 0x003C ) + 64;
    ContextSwitch( level );
    WriteMemoryW( WP + 2 * 11, address );
    ST |= TMS_XOP;
}

//-----------------------------------------------------------------------------
//   LDCR   Format: IV  Op-code: 0x3000     Status: L A E - - P -
//-----------------------------------------------------------------------------
void opcode_LDCR( )
{
    UINT16 value;
    int cru = ( ReadMemoryW( WP + 2 * 12 ) >> 1 ) & 0x0FFF;
    unsigned int count = ( curOpCode >> 6 ) & 0x000F;
    if( count == 0 )
    {
        count = 16;
    }

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_OVERFLOW | TMS_PARITY );

    ClockCycleCounter += 2 * count;

    if( count < 9 )
    {
        UINT16 address = GetAddress( curOpCode, 1 );
        value = ReadMemoryB( address );
        ST |= parity[ ( UINT8 ) value ];
        SetFlags_LAE(( INT8 ) value );
    }
    else
    {
        UINT16 address = GetAddress( curOpCode, 2 );
        value = ReadMemoryW( address );
        SetFlags_LAE( value );
    }

    WriteCRU( cru, count, value );
}

//-----------------------------------------------------------------------------
//   STCR   Format: IV  Op-code: 0x3400     Status: L A E - - P -
//-----------------------------------------------------------------------------
void opcode_STCR( )
{
    int cru = ( ReadMemoryW( WP + 2 * 12 ) >> 1 ) & 0x0FFF;
    unsigned int count = ( curOpCode >> 6 ) & 0x000F;
    if( count == 0 )
    {
        count = 16;
    }

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_OVERFLOW | TMS_PARITY );

    ClockCycleCounter += ( count & 0x07 ) ? 0 : 2;

    UINT16 value = ReadCRU(cru, count );

    if( count < 9 )
    {
        ST |= parity[ ( UINT8 ) value ];
        SetFlags_LAE(( INT8 ) value );
        UINT16 address = GetAddress( curOpCode, 1 );
        // Hidden memory access
        ReadMemoryB( address );
        WriteMemoryB( address, ( UINT8 ) value );
    }
    else
    {
        ClockCycleCounter += 58 - 42;
        SetFlags_LAE( value );
        UINT16 address = GetAddress( curOpCode, 2 );
        // Hidden memory access
        ReadMemoryW( address );
        WriteMemoryW( address, value );
    }
}

//-----------------------------------------------------------------------------
//   MPY    Format: IX  Op-code: 0x3800     Status: - - - - - - -
//-----------------------------------------------------------------------------
void opcode_MPY( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( srcAddress );
    UINT16 dstAddress = GetAddress(( curOpCode >> 6 ) & 0x0F, 2 );
    UINT32 dst = ReadMemoryW( dstAddress );

    dst *= src;

    WriteMemoryW( dstAddress, ( UINT16 ) ( dst >> 16 ));
    WriteMemoryW( dstAddress + 2, ( UINT16 ) dst );
}

//-----------------------------------------------------------------------------
//   DIV    Format: IX  Op-code: 0x3C00     Status: - - - - O - -
//-----------------------------------------------------------------------------
void opcode_DIV( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( srcAddress );
    UINT16 dstAddress = GetAddress(( curOpCode >> 6 ) & 0x0F, 2 );
    UINT32 dst = ReadMemoryW( dstAddress );

    if( dst < src )
    {
        ST &= ~TMS_OVERFLOW;
        dst = ( dst << 16 ) | ReadMemoryW( dstAddress + 2 );
        WriteMemoryW( dstAddress, ( UINT16 ) ( dst / src ));
        WriteMemoryW( dstAddress + 2, ( UINT16 ) ( dst % src ));
        ClockCycleCounter += ( 92 + 124 ) / 2 - 16;
    }
    else
    {
        ST |= TMS_OVERFLOW;
    }
}

//-----------------------------------------------------------------------------
//   SZC    Format: I   Op-code: 0x4000     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_SZC( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 2 );
    UINT16 src = ReadMemoryW( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 2 );
    UINT16 dst = ReadMemoryW( dstAddress );

    src = ~src & dst;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE( src );

    WriteMemoryW( dstAddress, src );
}

//-----------------------------------------------------------------------------
//   SZCB   Format: I   Op-code: 0x5000     Status: L A E - - P -
//-----------------------------------------------------------------------------
void opcode_SZCB( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 1 );
    UINT8 src = ReadMemoryB( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 1 );
    UINT8 dst = ReadMemoryB( dstAddress );

    src = ~src & dst;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_PARITY );
    ST |= parity[ src ];
    SetFlags_LAE(( INT8 ) src );

    WriteMemoryB( dstAddress, src );
}

//-----------------------------------------------------------------------------
//   S      Format: I   Op-code: 0x6000     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_S( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 2 );
    UINT32 dst = ReadMemoryW( dstAddress );

    UINT32 sum = dst - src;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_difW(( UINT16 ) src, ( UINT16 ) dst, sum );

    WriteMemoryW( dstAddress, ( UINT16 ) sum );
}

//-----------------------------------------------------------------------------
//   SB     Format: I   Op-code: 0x7000     Status: L A E C O P -
//-----------------------------------------------------------------------------
void opcode_SB( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 1 );
    UINT32 src = ReadMemoryB( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 1 );
    UINT32 dst = ReadMemoryB( dstAddress );

    UINT32 sum = dst - src;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW | TMS_PARITY );
    SetFlags_difB(( UINT8 ) src, ( UINT8 ) dst, sum );

    WriteMemoryB( dstAddress, ( UINT8 ) sum );
}

//-----------------------------------------------------------------------------
//   C      Format: I   Op-code: 0x8000     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_C( )
{
    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );

    UINT16 src = ReadMemoryW( GetAddress( curOpCode, 2 ));
    UINT16 dst = ReadMemoryW( GetAddress( curOpCode >> 6, 2 ));

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE2( src, dst );
}

//-----------------------------------------------------------------------------
//   CB     Format: I   Op-code: 0x9000     Status: L A E - - P -
//-----------------------------------------------------------------------------
void opcode_CB( )
{
    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_PARITY );

    UINT8 src = ReadMemoryB( GetAddress( curOpCode, 1 ));
    UINT8 dst = ReadMemoryB( GetAddress( curOpCode >> 6, 1 ));

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_PARITY );
    ST |= parity[ src ];
    SetFlags_LAE2(( INT8 ) src, ( INT8 ) dst );
}

//-----------------------------------------------------------------------------
//   A      Format: I   Op-code: 0xA000     Status: L A E C O - -
//-----------------------------------------------------------------------------
void opcode_A( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 2 );
    UINT32 src = ReadMemoryW( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 2 );
    UINT32 dst = ReadMemoryW( dstAddress );

    UINT32 sum = src + dst;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW );
    SetFlags_sumW(( UINT16 ) src, ( UINT16 ) dst, sum );

    WriteMemoryW( dstAddress, ( UINT16 ) sum );
}

//-----------------------------------------------------------------------------
//   AB     Format: I   Op-code: 0xB000     Status: L A E C O P -
//-----------------------------------------------------------------------------
void opcode_AB( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 1 );
    UINT32 src = ReadMemoryB( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 1 );
    UINT32 dst = ReadMemoryB( dstAddress );

    UINT32 sum = src + dst;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_CARRY | TMS_OVERFLOW | TMS_PARITY );
    ST |= parity[ ( UINT8 ) sum ];
    SetFlags_sumB(( UINT8 ) src, ( UINT8 ) dst, sum );

    WriteMemoryB( dstAddress, ( UINT8 ) sum );
}

//-----------------------------------------------------------------------------
//   MOV    Format: I   Op-code: 0xC000     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_MOV( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 2 );
    UINT16 src = ReadMemoryW( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 2 );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE( src );

    // Hidden memory access
    ReadMemoryW( dstAddress );

    WriteMemoryW( dstAddress, src );
}

//-----------------------------------------------------------------------------
//   MOVB   Format: I   Op-code: 0xD000     Status: L A E - - P -
//-----------------------------------------------------------------------------
void opcode_MOVB( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 1 );
    UINT8 src = ReadMemoryB( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 1 );

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_PARITY );
    ST |= parity[ src ];
    SetFlags_LAE(( INT8 ) src );

    // Hidden memory access
    ReadMemoryB( dstAddress );

    WriteMemoryB( dstAddress, src );
}

//-----------------------------------------------------------------------------
//   SOC    Format: I   Op-code: 0xE000     Status: L A E - - - -
//-----------------------------------------------------------------------------
void opcode_SOC( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 2 );
    UINT16 src = ReadMemoryW( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 2 );
    UINT16 dst = ReadMemoryW( dstAddress );

    src = src | dst;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL );
    SetFlags_LAE( src );

    WriteMemoryW( dstAddress, src );
}

//-----------------------------------------------------------------------------
//   SOCB   Format: I   Op-code: 0xF000     Status: L A E - - P -
//-----------------------------------------------------------------------------
void opcode_SOCB( )
{
    UINT16 srcAddress = GetAddress( curOpCode, 1 );
    UINT8 src = ReadMemoryB( srcAddress );
    UINT16 dstAddress = GetAddress( curOpCode >> 6, 1 );
    UINT8 dst = ReadMemoryB( dstAddress );

    src = src | dst;

    ST &= ~( TMS_LOGICAL | TMS_ARITHMETIC | TMS_EQUAL | TMS_PARITY );
    ST |= parity[ src ];
    SetFlags_LAE(( INT8 ) src );

    WriteMemoryB( dstAddress, src );
}

