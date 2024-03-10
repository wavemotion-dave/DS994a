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
#ifndef _DS99_H_
#define _DS99_H_

#include <nds.h>
#include <string.h>

extern u32 MAX_CART_SIZE;   // Dynamic buffer size - if DSi we go to 8MB and for DS only 512K
extern u8  *MemCART;        // The actual cart buffer gets allocated here.
extern char tmpBuf[256];    // For simple printf-type output and other sundry uses.
extern u8 fileBuf[0x2000];  // For DSK sector cache, general file I/O and file CRC generation use. Must be at least 8K

#define WAITVBL {swiWaitForVBlank(); swiWaitForVBlank(); swiWaitForVBlank(); swiWaitForVBlank(); swiWaitForVBlank();}

#define MAIN_GROM ((u16*)0x0689A000)   // 24K of fast VDP memory for the system console GROM cache
#define MAIN_BIOS ((u16*)0x068A0000)   // 8K  of fast VDP memory for the main TI-99 BIOS cache
#define DISK_DSR  ((u16*)0x068A2000)   // 8K  of fast VDP memory for the Disk Controller DSR cache

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
    KBD_1, KBD_2, KBD_3, KBD_4, KBD_5, KBD_6, KBD_7, KBD_8, KBD_9, KBD_0,
    KBD_A, KBD_B, KBD_C, KBD_D, KBD_E, KBD_F, KBD_G, KBD_H, KBD_I, KBD_J,
    KBD_K, KBD_L, KBD_M, KBD_N, KBD_O, KBD_P, KBD_Q, KBD_R, KBD_S, KBD_T,
    KBD_U, KBD_V, KBD_W, KBD_X, KBD_Y, KBD_Z,
    KBD_EQUALS,
    KBD_SLASH,
    KBD_PERIOD,
    KBD_COMMA,    
    KBD_SEMI,
    KBD_PLUS,
    KBD_MINUS,    
    KBD_UP_ARROW,
    KBD_DOWN_ARROW,
    KBD_LEFT_ARROW,
    KBD_RIGHT_ARROW,
    KBD_PROC,
    KBD_REDO,
    KBD_BACK,
    KBD_FNCT,
    KBD_CTRL,
    KBD_SHIFT,
    KBD_FNCT_E,
    KBD_FNCT_S,
    KBD_FNCT_D,
    KBD_FNCT_X,
    MAX_KEY_OPTIONS
};

#define META_KEY_NONE           0
#define META_KEY_QUIT           1 
#define META_KEY_HIGHSCORE      2
#define META_KEY_SAVESTATE      3
#define META_KEY_LOADSTATE      4
#define META_KEY_MINIMENU       5
#define META_KEY_ALPHALOCK      6
#define META_KEY_SHIFT          7
#define META_KEY_CONTROL        8
#define META_KEY_FUNCTION       9
#define META_KEY_DISKMENU       10
#define META_KEY_DEBUG_NEXT     11

extern u16 emuFps;
extern u16 emuActFrames;
extern u16 timingFrames;
extern u16 readSpeech;

extern u16 nds_key;
extern u8  kbd_key;
extern u8 keyCoresp[MAX_KEY_OPTIONS];
extern u16 NDS_keyMap[];

extern u16 vusCptVBL;                            // Screen Refresh Timer

extern u8 soundEmuPause;
extern u8  bShowDebug;

extern int bg0, bg1, bg0b,bg1b;

extern u16 *pVidFlipBuf;                         // Video flipping buffer

extern void showMainMenu(void);
extern void InitBottomScreen(void);
extern void PauseSound(void);
extern void UnPauseSound(void);
extern void ResetStatusFlags(void);
extern void ReadFileCRCAndConfig(void);
extern void DisplayStatusLine(bool bForce);
extern void ResetTI(void);
extern void DiskSave(char *filename);
extern void DrawCleanBackground(void);
extern void WriteSpeechData(u8 data);

#endif
