// --------------------------------------------------------------------------
// The original version of this file came from TI-99/Sim from Marc Rousseau:
//
// https://www.mrousseau.org/programs/ti99sim/
//
// The code has been altered from its original to be streamlined, and heavily
// optmized for the DS CPU and run as fast as possible on the 67MHz handheld.
//
// This modified code is released under the same GPL License as mentioned in
// Marc's original copyright statement below.
// --------------------------------------------------------------------------

//----------------------------------------------------------------------------
//
// File:        tms9900.hpp
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
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
//
// Revision History:
//
//----------------------------------------------------------------------------

#ifndef TMS9900_HPP_
#define TMS9900_HPP_

#include <stdbool.h>

#define TMS_LOGICAL       0x8000
#define TMS_ARITHMETIC    0x4000
#define TMS_EQUAL         0x2000
#define TMS_CARRY         0x1000
#define TMS_OVERFLOW      0x0800
#define TMS_PARITY        0x0400
#define TMS_XOP           0x0200

#define MEMFLG_SPEECH     0x0001
#define MEMFLG_CART       0x0002
#define MEMFLG_8BIT       0x0004
#define MEMFLG_VDPR       0x0008
#define MEMFLG_VDPW       0x0010
#define MEMFLG_GROMR      0x0020
#define MEMFLG_GROMW      0x0040
#define MEMFLG_SOUND      0x0080

extern void TMS9900_Reset(char *szGame);
extern void TMS9900_Run( ) ;
extern void TMS9900_Stop( ) ;
extern bool IsRunning( ) ;
extern void Reset( ) ;
extern void SignalInterrupt( UINT8 ) ;
extern void ClearInterrupt( UINT8 ) ;

extern void AddClocks( int ) ;
extern void ResetClocks( ) ;
extern UINT32 GetCounter( ) ;
extern void ResetCounter( ) ;

typedef struct _sOpCode
{
    void      (*function)( );
    UINT32      clocks;
    UINT16      opCode;
    UINT16      mask;
    char        mnemonic[ 8 ];
} sOpCode;


// Functions required by opcodes.cpp

extern void InvalidOpcode( );

// Functions provided by opcodes.cpp

extern bool IsRunning( );
extern void ContextSwitch( UINT16 address );
extern void InitOpCodeLookup( );

extern UINT8           Memory[0x10000];            // 64K of CPU Memory Space
extern UINT8           MemGROM[0x10000];           // 64K of GROM Memory Space
extern UINT8           CartMem[];                  // Cart C memory 
extern UINT8           DiskImage[];                // The .DSK image up to 180K

extern UINT16          InterruptFlag;
extern UINT32          WorkspacePtr;
extern UINT16          Status;
extern UINT32          ClockCycleCounter;
extern UINT32          ProgramCounter;
extern UINT16          curOpCode;
extern UINT32          bankOffset;
extern UINT8           m_GromWriteShift;
extern UINT8           m_GromReadShift;
extern UINT32          gromAddress;
extern UINT8           bCPUIdleRequest;
extern UINT8           AccurateEmulationFlags;
extern UINT32          InterruptOrTimerPossible;

#define EMU_DISK       0x01
#define EMU_IDLE       0x02

void opcode_A   ( );
void opcode_AB  ( );
void opcode_ABS ( );
void opcode_AI  ( );
void opcode_ANDI( );
void opcode_B   ( );
void opcode_BL  ( );
void opcode_BLWP( );
void opcode_C   ( );
void opcode_CB  ( );
void opcode_CI  ( );
void opcode_CKOF( );
void opcode_CKON( );
void opcode_CLR ( );
void opcode_COC ( );
void opcode_CZC ( );
void opcode_DEC ( );
void opcode_DECT( );
void opcode_DIV ( );
void opcode_IDLE( );
void opcode_INC ( );
void opcode_INCT( );
void opcode_INV ( );
void opcode_JEQ ( );
void opcode_JGT ( );
void opcode_JH  ( );
void opcode_JHE ( );
void opcode_JL  ( );
void opcode_JLE ( );
void opcode_JLT ( );
void opcode_JMP ( );
void opcode_JNC ( );
void opcode_JNE ( );
void opcode_JNO ( );
void opcode_JOC ( );
void opcode_JOP ( );
void opcode_LDCR( );
void opcode_LI  ( );
void opcode_LIMI( );
void opcode_LREX( );
void opcode_LWPI( );
void opcode_MOV ( );
void opcode_MOVB( );
void opcode_MPY ( );
void opcode_NEG ( );
void opcode_ORI ( );
void opcode_RSET( );
void opcode_RTWP( );
void opcode_S   ( );
void opcode_SB  ( );
void opcode_SBO ( );
void opcode_SBZ ( );
void opcode_SETO( );
void opcode_SLA ( );
void opcode_SOC ( );
void opcode_SOCB( );
void opcode_SRA ( );
void opcode_SRC ( );
void opcode_SRL ( );
void opcode_STCR( );
void opcode_STST( );
void opcode_STWP( );
void opcode_SWPB( );
void opcode_SZC ( );
void opcode_SZCB( );
void opcode_TB  ( );
void opcode_X   ( );
void opcode_XOP ( );
void opcode_XOR ( );

#endif
