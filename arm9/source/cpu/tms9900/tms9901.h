// =====================================================================================
// Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, it's source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The TI99DS emulator is offered as-is, without any warranty.
// =====================================================================================

#ifndef TMS9901_H_
#define TMS9901_H_

#include <nds.h>
#include <string.h>

enum KEYS
{
    TMS_KEY_NONE,
    
    TMS_KEY_1, TMS_KEY_2, TMS_KEY_3, TMS_KEY_4, TMS_KEY_5, TMS_KEY_6, TMS_KEY_7, TMS_KEY_8, TMS_KEY_9, TMS_KEY_0,
    TMS_KEY_A, TMS_KEY_B, TMS_KEY_C, TMS_KEY_D, TMS_KEY_E, TMS_KEY_F, TMS_KEY_G, TMS_KEY_H, TMS_KEY_I, TMS_KEY_J,
    TMS_KEY_K, TMS_KEY_L, TMS_KEY_M, TMS_KEY_N, TMS_KEY_O, TMS_KEY_P, TMS_KEY_Q, TMS_KEY_R, TMS_KEY_S, TMS_KEY_T,
    TMS_KEY_U, TMS_KEY_V, TMS_KEY_W, TMS_KEY_X, TMS_KEY_Y, TMS_KEY_Z,
    
    TMS_KEY_ENTER,  TMS_KEY_SHIFT,   TMS_KEY_CONTROL, TMS_KEY_FUNCTION, TMS_KEY_SPACE,
    TMS_KEY_PERIOD, TMS_KEY_COMMA,   TMS_KEY_SLASH,   TMS_KEY_SEMI,     TMS_KEY_EQUALS,
    
    TMS_KEY_JOY1_UP, TMS_KEY_JOY1_DOWN, TMS_KEY_JOY1_LEFT, TMS_KEY_JOY1_RIGHT, TMS_KEY_JOY1_FIRE,
    TMS_KEY_JOY2_UP, TMS_KEY_JOY2_DOWN, TMS_KEY_JOY2_LEFT, TMS_KEY_JOY2_RIGHT, TMS_KEY_JOY2_FIRE,
    
    TMS_KEY_MAX
};

#define MAX_PINS        32      // 32 CRU pins that have to be handled in the TI99/4a

enum PIN_STATE
{
    PIN_LOW = 0, 
    PIN_HIGH           // Pins can either be high or low...
};

#define TIMER_MODE    PIN_HIGH
#define IO_MODE       PIN_LOW

// ---------------------------------------------------------
// Some special pins useful for keyboard decoding logic...
// ---------------------------------------------------------
#define PIN_TIMER_OR_IO     0
#define PIN_VDP_INT         2
#define PIN_TIMER_INT       3
#define PIN_COL1            18
#define PIN_COL2            19
#define PIN_COL3            20
#define PIN_ALPHA_LOCK      21

typedef struct _TMS9901
{
    u8      Keyboard[TMS_KEY_MAX];      // Main TI-99/4a Keyboard plus joystick inputs for both P1 and P2
    u8      PinState[MAX_PINS];         // The state of the 32 PINs
    u8      CapsLock;                   // Set to '1' if the Caps Lock is active
    u8      VDPIntteruptInProcess;      // Set to '1' if the VDP interrupt is in process
    u8      TimerIntteruptInProcess;    // Set to '1' if the Timer interrupt is in process
    u32     TimerStart;                 // The Starting value
    u32     TimerCounter;               // The 14-bit Timer Counter 
} TMS9901;

extern TMS9901 tms9901;

extern void     TMS9901_Reset(void);
extern void     TMS9901_WriteCRU(u16 cruAddress, u16 data, u8 num);
extern u16      TMS9901_ReadCRU(u16 cruAddress, u8 num);
extern void     TMS9901_ClearJoyKeyData(void);
extern void     TMS9901_RaiseVDPInterrupt(void);
extern void     TMS9901_ClearVDPInterrupt(void);
extern void     TMS9901_RaiseTimerInterrupt(void);
extern void     TMS9901_ClearTimerInterrupt(void);

#endif //TMS9901_H_
