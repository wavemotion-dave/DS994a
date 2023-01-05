// =====================================================================================
// Copyright (c) 2021-2002 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, it's source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave (Phoenix-Edition),
// Alekmaul (original port) and Marat Fayzullin (ColEM core) are thanked profusely.
//
// The ColecoDS emulator is offered as-is, without any warranty.
// =====================================================================================
#ifndef _COLECODS_H_
#define _COLECODS_H_

#include <nds.h>
#include <string.h>

#define VERSIONDS99 "0.2"

enum
{
    JOY1_UP, 
    JOY1_DOWN, 
    JOY1_LEFT, 
    JOY1_RIGHT, 
    JOY1_FIRE, 
    JOY2_UP, 
    JOY2_DOWN, 
    JOY2_LEFT, 
    JOY2_RIGHT, 
    JOY2_FIRE,
    KBD_SPACE,
    KBD_ENTER,
    KBD_1,
    KBD_2,
    KBD_3,
    KBD_4,
    KBD_5,
    KBD_6,
    KBD_7,
    KBD_8,
    KBD_9,
    KBD_0,
    KBD_A,
    KBD_B,
    KBD_C,
    KBD_D,
    KBD_E,
    KBD_F,
    KBD_G,
    KBD_H,
    KBD_I,
    KBD_J,
    KBD_K,
    KBD_L,
    KBD_M,
    KBD_N,
    KBD_O,
    KBD_P,
    KBD_Q,
    KBD_R,
    KBD_S,
    KBD_T,
    KBD_U,
    KBD_V,
    KBD_W,
    KBD_X,
    KBD_Y,
    KBD_Z,
    KBD_UP_ARROW,
    KBD_DOWN_ARROW,
    KBD_LEFT_ARROW,
    KBD_RIGHT_ARROW,
    KBD_PROC,
    KBD_REDO,
    KBD_BACK,
    KBD_FNCT,
    KBD_CTRL,
    KBD_PLUS,
    KBD_MINUS,
    MAX_KEY_OPTIONS
};

extern u16 emuFps;
extern u16 emuActFrames;
extern u16 timingFrames;

extern u8 spinX_left;
extern u8 spinX_right;
extern u8 spinY_left;
extern u8 spinY_right;

extern u16 nds_key;
extern u8  kbd_key;

extern u32 tape_pos, tape_len;

#define WAITVBL swiWaitForVBlank(); swiWaitForVBlank(); swiWaitForVBlank(); swiWaitForVBlank(); swiWaitForVBlank();

extern volatile u16 vusCptVBL;                   // Video Management

extern u8 keyCoresp[MAX_KEY_OPTIONS];
extern u16 NDS_keyMap[];

extern u8 soundEmuPause;

extern int bg0, bg1, bg0b,bg1b;

extern u16 *pVidFlipBuf;                         // Video flipping buffer

extern u8 adam_ram_lo, adam_ram_hi;
extern u8 io_show_status;

extern void showMainMenu(void);
extern void InitBottomScreen(void);
extern void PauseSound(void);
extern void UnPauseSound(void);
extern void ResetStatusFlags(void);
extern void ReadFileCRCAndConfig(void);
extern void SetupAdam(bool);
extern void DisplayStatusLine(bool bForce);
extern void colecoSaveEEPROM(void);    
extern void colecoLoadEEPROM(void);    
extern void ResetTI(void);
#endif
