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
#ifndef _DS99_UTILS_H_
#define _DS99_UTILS_H_

#define MAX_ROMS                512
#define MAX_DISKS               256
#define MAX_ROM_LENGTH          128
#define MAX_PATH                128

#define MAX_CONFIGS             800
#define CONFIG_VER              0x0007

#define TI99ROM                 0x01
#define DIRECT                  0x02

#define ID_SHM_CANCEL           0x00
#define ID_SHM_YES              0x01
#define ID_SHM_NO               0x02

#define DPAD_NORMAL             0
#define DPAD_DIAGONALS          1

#define MACH_TYPE_NORMAL32K     0
#define MACH_TYPE_SAMS          1

#define CART_TYPE_NORMAL        0
#define CART_TYPE_SUPERCART     1
#define CART_TYPE_MINIMEM       2
#define CART_TYPE_MBX_NO_RAM    3
#define CART_TYPE_MBX_WITH_RAM  4

typedef struct {
  char szName[MAX_ROM_LENGTH];
  u8 uType;
  u32 uCrc;
} FIC_TI99;

struct __attribute__((__packed__)) GlobalConfig_t
{
    u16 config_ver;
    u16 configCRC;
    u8  showFPS;
    u8  skipBIOS;
    u8  romsDIR;
    u8  maxSprites;
    u8  machineType;
    u8  overlay;
    u8  floppySound;
    u8  reservedH;
    u8  reservedI;
    u8  reservedJ;
    u8  reservedK;
    u8  reservedL;
    u8  reservedM;
    u8  reservedN;
    u8  reservedO;
    u8  reservedP;
    u8  reserved[512+128]; // Not sure what we will use this for... But we had the room
};

struct __attribute__((__packed__)) Config_t
{
    u32 game_crc;
    u8  keymap[12];
    u8  frameSkip;
    u8  frameBlend;
    u8  maxSprites;
    u8  memWipe;
    u8  isPAL;
    u8  capsLock;
    u8  RAMMirrors;
    u8  overlay;
    u8  emuSpeed;
    u8  machineType;
    u8  cartType;
    u8  dpadDiagonal;
    u8  spriteCheck;
    u8  reservedI;
    u8  reservedJ;
    u8  reservedK;
    u8  reservedL;
    u8  reservedM;
    u8  reservedN;
    u8  reservedO;
    u8  reservedP;
    u8  reservedQ;
    u8  reservedY;
    u8  reservedZ;
};

extern struct Config_t myConfig;
extern struct GlobalConfig_t globalConfig;

extern char currentDirROMs[];
extern char currentDirDSKs[];

extern void FindAndLoadConfig(void);

extern FIC_TI99 gpFic[MAX_ROMS];  
extern int uNbRoms;
extern int ucGameAct;
extern int ucGameChoice;

extern u8 showMessage(char *szCh1, char *szCh2);
extern void TI99FindFiles(void);
extern void tiDSChangeOptions(void);
extern void DrawCleanBackground(void);

extern void DS_Print(int iX,int iY,int iScr,char *szMessage);
extern unsigned int crc32 (unsigned int crc, const unsigned char *buf, unsigned int len);

extern void TILoadDiskFile(void);

extern void FadeToColor(unsigned char ucSens, unsigned short ucBG, unsigned char ucScr, unsigned char valEnd, unsigned char uWait);
extern void DisplayFileName(void);

#endif
