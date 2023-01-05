
//----------------------------------------------------------------------------
//
// File:        tms9901.hpp
// Date:        18-Dec-2001
// Programmer:  Marc Rousseau
//
// Description:
//
// Copyright (c) 2001-2004 Marc Rousseau, All Rights Reserved.
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

#ifndef TMS9901_HPP_
#define TMS9901_HPP_

#include <stdbool.h>

typedef signed char             INT8;
typedef unsigned char           UINT8;
typedef signed short            INT16;
typedef unsigned short          UINT16;
typedef signed int              INT32;
typedef unsigned int            UINT32;
typedef signed long long int    INT64;  // C99 C++11
typedef unsigned long long int  UINT64; // C99 C++11
typedef char                    CHAR;
typedef unsigned short          ADDRESS;

typedef UINT8(*TRAP_FUNCTION)( void *, int, int, ADDRESS, UINT8 );
typedef UINT16(*BREAKPOINT_FUNCTION)( void *, ADDRESS, int, UINT16, int, int );

#define true                 1
#define false                0

typedef struct _sJoystickInfo
{
    int     isPressed;
    int      x_Axis;
    int      y_Axis;
} sJoystickInfo;

typedef enum _VIRTUAL_KEY_E
{
    VK_NONE,
    VK_ENTER, VK_SPACE, VK_COMMA, VK_PERIOD, VK_DIVIDE, VK_SEMICOLON, VK_EQUALS,
    VK_CAPSLOCK, VK_SHIFT, VK_CTRL, VK_FCTN,
    VK_0, VK_1, VK_2, VK_3, VK_4, VK_5, VK_6, VK_7, VK_8, VK_9,
    VK_A, VK_B, VK_C, VK_D, VK_E, VK_F, VK_G, VK_H, VK_I, VK_J, VK_K, VK_L, VK_M,
    VK_N, VK_O, VK_P, VK_Q, VK_R, VK_S, VK_T, VK_U, VK_V, VK_W, VK_X, VK_Y, VK_Z,
    VK_MAX
} VIRTUAL_KEY_E;


extern bool                m_TimerActive;
extern int                 m_ReadRegister;
extern int                 m_Decrementer;
extern int                 m_ClockRegister;
extern UINT8               m_PinState[ 32 ][ 2 ];
extern int                 m_InterruptRequested;
extern int                 m_ActiveInterrupts;
extern int                 m_LastDelta;
extern UINT32              m_DecrementClock;
extern bool                m_CapsLock;
extern int                 m_ColumnSelect;
extern int                 m_HideShift;
extern UINT8               m_StateTable[ VK_MAX ];
extern sJoystickInfo       m_Joystick[ 2 ];

extern const char *GetName( ) ;
extern void WriteCRU( ADDRESS cru, UINT8 count, UINT16 data ) ;
extern UINT16 ReadCRU( ADDRESS cru, UINT8 count) ;
extern void UpdateTimer( UINT32 ) ;
extern void HardwareReset( ) ;
extern void SoftwareReset( ) ;
extern void tms9901_SignalInterrupt( int ) ;
extern void tms9901_ClearInterrupt( int ) ;
extern void VKeyUp( int sym ) ;
extern void VKeyDown( int sym, VIRTUAL_KEY_E vkey ) ;
extern void VKeysDown(int sym, VIRTUAL_KEY_E vkey1, VIRTUAL_KEY_E vkey2) ;
extern void HideShiftKey( ) ;
extern void UnHideShiftKey( ) ;
extern UINT8 GetKeyState( VIRTUAL_KEY_E ) ;
extern void SetJoystickX( int, int ) ;
extern void SetJoystickY( int, int ) ;
extern void SetJoystickButton( int, bool ) ;


extern void TMS9901_Reset(void);
#endif
