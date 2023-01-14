// =====================================================================================
// Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, it's source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The TI99DS emulator is offered as-is, without any warranty.
// =====================================================================================
#ifndef _DS99_UTILS_H_
#define _DS99_UTILS_H_

#define MAX_ROMS            512
#define MAX_ROM_LENGTH      160

#define MAX_CONFIGS         750
#define CONFIG_VER          0x0002

#define TI99ROM             0x01
#define DIRECT              0x02

#define ID_SHM_CANCEL       0x00
#define ID_SHM_YES          0x01
#define ID_SHM_NO           0x02

#define DPAD_NORMAL         0
#define DPAD_DIAGONALS      1

typedef struct {
  char szName[MAX_ROM_LENGTH];
  u8 uType;
  u32 uCrc;
} FIC_TI99;

struct __attribute__((__packed__)) Config_t
{
    u16 config_ver;
    u32 game_crc;
    u8  keymap[12];
    u8  showFPS;
    u8  frameSkip;
    u8  frameBlend;
    u8  maxSprites;
    u8  memWipe;
    u8  isPAL;
    u8  capsLock;
    u8  RAMMirrors;
    u8  keyboard;
    u8  emuSpeed;
    u8  reservedE;
    u8  reservedF;
    u8  reservedG;
    u8  reservedH;
    u8  reservedI;
    u8  reservedJ;
    u8  reservedK;
    u8  reservedL;
    u8  reservedM;
    u8  reservedN;
    u8  reservedZ;
    u32 reservedA32;
};
 

extern struct Config_t myConfig;

extern void FindAndLoadConfig(void);

extern FIC_TI99 gpFic[MAX_ROMS];  
extern int uNbRoms;
extern int ucGameAct;
extern int ucGameChoice;

extern u8 showMessage(char *szCh1, char *szCh2);
extern void TI99FindFiles(void);
extern void tiDSChangeOptions(void);

extern void AffChaine(int iX,int iY,int iScr,char *szMessage);
extern void dsPrintValue(int iX,int iY,int iScr,char *szMessage);
extern unsigned int crc32 (unsigned int crc, const unsigned char *buf, unsigned int len);

extern char *TILoadDiskFile(void);

extern void FadeToColor(unsigned char ucSens, unsigned short ucBG, unsigned char ucScr, unsigned char valEnd, unsigned char uWait);
extern void DisplayFileName(void);

#endif
