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
#include <nds.h>

#include <stdlib.h>
#include <stdio.h>
#include <fat.h>
#include <dirent.h>
#include <unistd.h>

#include "printf.h"
#include "DS99.h"
#include "DS99mngt.h"
#include "DS99_utils.h"

#include "splash.h"
#include "options.h"

#include "CRC32.h"

typedef enum {FT_NONE,FT_FILE,FT_DIR} FILE_TYPE;

u16 countTI=0;
int countDSK=0;
int chosenDSK=0;
int ucGameAct=0;
int ucDskAct=0;
int ucGameChoice = -1;
FIC_TI99 gpFic[MAX_ROMS];
FIC_TI99 gpDsk[MAX_DISKS];
char szName[256];
char szDiskName[256];
char szFile[256];
u32 file_size = 0;
char currentDirROMs[MAX_PATH];
char currentDirDSKs[MAX_PATH];
char strBuf[40];

struct GlobalConfig_t globalConfig;
struct Config_t AllConfigs[MAX_CONFIGS];
struct Config_t myConfig __attribute((aligned(4))) __attribute__((section(".dtcm")));
extern u32 file_crc;

extern char myDskFile[];
extern char myDskPath[];

u8 option_table=0;

const char szKeyName[MAX_KEY_OPTIONS][20] = {
  "P1 JOY UP",
  "P1 JOY DOWN",
  "P1 JOY LEFT",
  "P1 JOY RIGHT",
  "P1 JOY FIRE",
  "P2 JOY UP",
  "P2 JOY DOWN",
  "P2 JOY LEFT",
  "P2 JOY RIGHT",
  "P2 JOY FIRE",
  "KEYBOARD SPACE",
  "KEYBOARD ENTER",
  "KEYBOARD 1",
  "KEYBOARD 2",
  "KEYBOARD 3",
  "KEYBOARD 4",
  "KEYBOARD 5",
  "KEYBOARD 6",
  "KEYBOARD 7",
  "KEYBOARD 8",
  "KEYBOARD 9",
  "KEYBOARD 0",
  "KEYBOARD A",
  "KEYBOARD B",
  "KEYBOARD C",
  "KEYBOARD D",
  "KEYBOARD E",
  "KEYBOARD F",
  "KEYBOARD G",
  "KEYBOARD H",
  "KEYBOARD I",
  "KEYBOARD J",
  "KEYBOARD K",
  "KEYBOARD L",
  "KEYBOARD M",
  "KEYBOARD N",
  "KEYBOARD O",
  "KEYBOARD P",
  "KEYBOARD Q",
  "KEYBOARD R",
  "KEYBOARD S",
  "KEYBOARD T",
  "KEYBOARD U",
  "KEYBOARD V",
  "KEYBOARD W",
  "KEYBOARD X",
  "KEYBOARD Y",
  "KEYBOARD Z",

  "KEYBOARD EQUALS",
  "KEYBOARD SLASH",
  "KEYBOARD PERIOD",
  "KEYBOARD COMMA",
  "KEYBOARD SEMI",

  "KEYBOARD PLUS",
  "KEYBOARD MINUS",

  "KEYBOARD UP",
  "KEYBOARD DOWN",
  "KEYBOARD LEFT",
  "KEYBOARD RIGHT",
  "KEYBOARD PRCD",
  "KEYBOARD REDO",
  "KEYBOARD BACK",
  "KEYBOARD FCTN",
  "KEYBOARD CTRL",
  "KEYBOARD SHIFT",
  "KEYBOARD FCTN-E",
  "KEYBOARD FCTN-S",
  "KEYBOARD FCTN-D",
  "KEYBOARD FCTN-X",
};


// -------------------------------------------------------------------------------------
// The bottom (touch area) display - here we put up a generic background that is mostly
// blank so we can draw options and menu choices. It has the TI logo in the corner.
// -------------------------------------------------------------------------------------
void DrawCleanBackground(void)
{
    swiWaitForVBlank();
    // ---------------------------------------------------
    // Put up a generic background for this mini-menu...
    // ---------------------------------------------------
    bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);
    bg1b = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x256, 29,0);
    bgSetPriority(bg0b,1);bgSetPriority(bg1b,0);
    decompress(optionsTiles, bgGetGfxPtr(bg0b), LZ77Vram);
    decompress(optionsMap, (void*) bgGetMapPtr(bg0b), LZ77Vram);
    dmaCopy((void*) optionsPal,(void*) BG_PALETTE_SUB,256*2);
    unsigned short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
    dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b),32*24*2);
}

// ---------------------------------------------------------------------------------
// Show A message with YES / NO and return which option was picked by the user.
// ---------------------------------------------------------------------------------
u8 showMessage(char *szCh1, char *szCh2)
{
  u16 iTx, iTy;
  u8 uRet=ID_SHM_CANCEL;
  u8 ucGau=0x00, ucDro=0x00,ucGauS=0x00, ucDroS=0x00, ucCho = ID_SHM_YES;

  DrawCleanBackground();

  DS_Print(16-strlen(szCh1)/2,10,6,szCh1);
  DS_Print(16-strlen(szCh2)/2,12,6,szCh2);
  DS_Print(8,14,6,("> YES <"));
  DS_Print(20,14,6,("  NO   "));
  while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_A ))!=0);

  while (uRet == ID_SHM_CANCEL)
  {
    WAITVBL;
    if (keysCurrent() & KEY_TOUCH) {
      touchPosition touch;
      touchRead(&touch);
      iTx = touch.px;
      iTy = touch.py;
      if ( (iTx>8*8) && (iTx<8*8+7*8) && (iTy>14*8-4) && (iTy<15*8+4) ) {
        if (!ucGauS) {
          DS_Print(8,14,6,("> YES <"));
          DS_Print(20,14,6,("  NO   "));
          ucGauS = 1;
          if (ucCho == ID_SHM_YES) {
            uRet = ucCho;
          }
          else {
            ucCho  = ID_SHM_YES;
          }
        }
      }
      else
        ucGauS = 0;
      if ( (iTx>20*8) && (iTx<20*8+7*8) && (iTy>14*8-4) && (iTy<15*8+4) ) {
        if (!ucDroS) {
          DS_Print(8,14,6,("  YES  "));
          DS_Print(20,14,6,("> NO  <"));
          ucDroS = 1;
          if (ucCho == ID_SHM_NO) {
            uRet = ucCho;
          }
          else {
            ucCho = ID_SHM_NO;
          }
        }
      }
      else
        ucDroS = 0;
    }
    else {
      ucDroS = 0;
      ucGauS = 0;
    }

    if (keysCurrent() & KEY_LEFT){
      if (!ucGau) {
        ucGau = 1;
        if (ucCho == ID_SHM_YES) {
          ucCho = ID_SHM_NO;
          DS_Print(8,14,6,("  YES  "));
          DS_Print(20,14,6,("> NO  <"));
        }
        else {
          ucCho  = ID_SHM_YES;
          DS_Print(8,14,6,("> YES <"));
          DS_Print(20,14,6,("  NO   "));
        }
        WAITVBL;
      }
    }
    else {
      ucGau = 0;
    }
    if (keysCurrent() & KEY_RIGHT) {
      if (!ucDro) {
        ucDro = 1;
        if (ucCho == ID_SHM_YES) {
          ucCho  = ID_SHM_NO;
          DS_Print(8,14,6,("  YES  "));
          DS_Print(20,14,6,("> NO  <"));
        }
        else {
          ucCho  = ID_SHM_YES;
          DS_Print(8,14,6,("> YES <"));
          DS_Print(20,14,6,("  NO   "));
        }
        WAITVBL;
      }
    }
    else {
      ucDro = 0;
    }
    if (keysCurrent() & KEY_A) {
      uRet = ucCho;
    }
  }
  while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_A ))!=0);

  InitBottomScreen();  // Could be generic or overlay...

  return uRet;
}


// ---------------------------------------------------------------------------------
// Show The 14 games on the list to allow the user to choose a new game.
// ---------------------------------------------------------------------------------
void dsDisplayFiles(u16 NoDebGame, u8 ucSel)
{
  u16 ucBcl,ucGame;
  u8 maxLen;

  DS_Print(30,6,0,(NoDebGame>0 ? "<" : " "));
  DS_Print(30,21,0,(NoDebGame+14<countTI ? ">" : " "));
  sprintf(szName,"%03d/%03d FILES AVAILABLE     ",ucSel+1+NoDebGame,countTI);
  DS_Print(3,3,0, szName);
  for (ucBcl=0;ucBcl<16; ucBcl++) {
    ucGame= ucBcl+NoDebGame;
    if (ucGame < countTI)
    {
      maxLen=strlen(gpFic[ucGame].szName);
      strcpy(szName,gpFic[ucGame].szName);
      if (maxLen>28) szName[28]='\0';
      if (gpFic[ucGame].uType == DIRECT) {
        sprintf(tmpBuf, " %s]",szName);
        tmpBuf[0]='[';
        sprintf(szName,"%-28s",tmpBuf);
        DS_Print(1,6+ucBcl,(ucSel == ucBcl ? 2 :  0),szName);
      }
      else {
        sprintf(szName,"%-28s",strupr(szName));
        DS_Print(1,6+ucBcl,(ucSel == ucBcl ? 2 : 0 ),szName);
      }
    }
    else
    {
        DS_Print(1,6+ucBcl,(ucSel == ucBcl ? 2 : 0 ),"                            ");
    }
  }
}


// ---------------------------------------------------------------------------------
// Show The 14 DSKs on the list to allow the user to choose a new game.
// ---------------------------------------------------------------------------------
void dsDisplayDsks(u16 NoDebGame, u8 ucSel)
{
  u16 ucBcl,ucGame;
  u8 maxLen;

  DS_Print(30,6,0,(NoDebGame>0 ? "<" : " "));
  DS_Print(30,21,0,(NoDebGame+14<countDSK ? ">" : " "));
  sprintf(szName,"%03d/%03d FILES AVAILABLE     ",ucSel+1+NoDebGame,countDSK);
  DS_Print(3,3,0, szName);
  for (ucBcl=0;ucBcl<16; ucBcl++) {
    ucGame= ucBcl+NoDebGame;
    if (ucGame < countDSK)
    {
      maxLen=strlen(gpDsk[ucGame].szName);
      strcpy(szName,gpDsk[ucGame].szName);
      if (maxLen>28) szName[28]='\0';
      if (gpDsk[ucGame].uType == DIRECT) {
        sprintf(tmpBuf, " %s]",szName);
        tmpBuf[0]='[';
        sprintf(szName,"%-28s",tmpBuf);
        DS_Print(1,6+ucBcl,(ucSel == ucBcl ? 2 :  0),szName);
      }
      else {
        sprintf(szName,"%-28s",strupr(szName));
        DS_Print(1,6+ucBcl,(ucSel == ucBcl ? 2 : 0 ),szName);
      }
    }
    else
    {
        DS_Print(1,6+ucBcl,(ucSel == ucBcl ? 2 : 0 ),"                            ");
    }
  }
}


// -------------------------------------------------------------------------
// Standard qsort routine for the TI games - we sort all directory
// listings first and then a case-insenstive sort of all games.
// -------------------------------------------------------------------------
int TI99Filescmp (const void *c1, const void *c2)
{
  FIC_TI99 *p1 = (FIC_TI99 *) c1;
  FIC_TI99 *p2 = (FIC_TI99 *) c2;

  if (p1->szName[0] == '.' && p2->szName[0] != '.')
      return -1;
  if (p2->szName[0] == '.' && p1->szName[0] != '.')
      return 1;
  if ((p1->uType == DIRECT) && !(p2->uType == DIRECT))
      return -1;
  if ((p2->uType == DIRECT) && !(p1->uType == DIRECT))
      return 1;
      
  // We want to do a simple string compare at this point but we want to force '0' files to sort lower
  char saveCh1 = p1->szName[strlen(p1->szName)-5];
  char saveCh2 = p2->szName[strlen(p2->szName)-5];
  if (saveCh1 == '0') p1->szName[strlen(p1->szName)-5] = 'Z';
  if (saveCh2 == '0') p2->szName[strlen(p2->szName)-5] = 'Z';
  int retVal = strcasecmp (p1->szName, p2->szName);
  if (saveCh1 == '0') p1->szName[strlen(p1->szName)-5] = '0';
  if (saveCh2 == '0') p2->szName[strlen(p2->szName)-5] = '0';
  
  return retVal;
}

// -----------------------------------------------------------------------------------
// Find ROM files (.bin) available - sort them for display and selection by the user.
// -----------------------------------------------------------------------------------
void TI99FindFiles(void)
{
  u16 uNbFile;
  DIR *dir;
  struct dirent *pent;

  uNbFile=0;
  countTI=0;

  dir = opendir(".");
  while (((pent=readdir(dir))!=NULL) && (uNbFile<MAX_ROMS))
  {
    strcpy(szFile,pent->d_name);

    if(pent->d_type == DT_DIR)
    {
      if (!( (szFile[0] == '.') && (strlen(szFile) == 1)))
      {
        // Filter out the emulator directories from the list
        if (strcasecmp(szFile, "SAV") == 0) continue;
        if (strcasecmp(szFile, "sav") == 0) continue;
          
        strcpy(gpFic[uNbFile].szName,szFile);
        gpFic[uNbFile].szName[26] = 0;  // No more than 26 chars can be shown in DIR listings
        gpFic[uNbFile].uType = DIRECT;
        uNbFile++;
        countTI++;
      }
    }
    else {
      if ((strlen(szFile)>4) && (strlen(szFile)<(MAX_ROM_LENGTH-4)) ) {
        if ( (strcasecmp(strrchr(szFile, '.'), ".bin") == 0) || (strcasecmp(strrchr(szFile, '.'), ".rpk") == 0) )
        {
          strcpy(gpFic[uNbFile].szName,szFile);
          gpFic[uNbFile].uType = TI99ROM;
          uNbFile++;
          countTI++;
        }
      }
    }
  }
  closedir(dir);

  // ----------------------------------------------
  // If we found any files, go sort the list...
  // ----------------------------------------------
  if (countTI)
  {
      qsort (gpFic, countTI, sizeof(FIC_TI99), TI99Filescmp);

      // And finally we remove 'sibling' files that are part of the same binary package C/D/G files...
      for (s16 i=0; i<countTI-1; i++)
      {
          if (strlen(gpFic[i].szName) > 5)
          {
              if (strlen(gpFic[i].szName) == strlen(gpFic[i+1].szName)) // Strings need to be the same length to be siblings
              {
                  if (strncmp(gpFic[i].szName, gpFic[i+1].szName, strlen(gpFic[i].szName)-5) == 0)
                  {
                      for (u16 j=i+1; j<countTI; j++)
                      {
                          memcpy(&gpFic[j], &gpFic[j+1], sizeof(FIC_TI99));
                      }
                      countTI--;
                      i--;
                  }
              }
          }
      }
  }
}

// ---------------------------------------------------------------------------------
// Find disk files (.dsk) available - sort them for display and selection by user.
// ---------------------------------------------------------------------------------
void TI99FindDskFiles(void)
{
  u16 uNbFile;
  DIR *dir;
  struct dirent *pent;

  uNbFile=0;
  countDSK=0;

  dir = opendir(".");
  while (((pent=readdir(dir))!=NULL) && (uNbFile<MAX_DISKS))
  {
    strcpy(szFile,pent->d_name);

    if(pent->d_type == DT_DIR)
    {
      if (!( (szFile[0] == '.') && (strlen(szFile) == 1)))
      {
        // Filter out the emulator directories from the list
        if (strcasecmp(szFile, "SAV") == 0) continue;
        if (strcasecmp(szFile, "sav") == 0) continue;
          
        strcpy(gpDsk[uNbFile].szName,szFile);
        gpDsk[uNbFile].szName[26] = 0;  // No more than 26 chars can be shown in DIR listings
        gpDsk[uNbFile].uType = DIRECT;
        uNbFile++;
        countDSK++;
      }
    }
    else {
      if ((strlen(szFile)>4) && (strlen(szFile)<(MAX_ROM_LENGTH-4)) ) {
        if ( (strcasecmp(strrchr(szFile, '.'), ".dsk") == 0) )  {
          strcpy(gpDsk[uNbFile].szName,szFile);
          gpDsk[uNbFile].uType = TI99ROM;
          uNbFile++;
          countDSK++;
        }
      }
    }
  }
  closedir(dir);

  // ----------------------------------------------
  // If we found any files, go sort the list...
  // ----------------------------------------------
  if (countDSK)
  {
    qsort(gpDsk, countDSK, sizeof(FIC_TI99), TI99Filescmp);
  }
}


// ---------------------------------------------------------------------------------
// Allow the user to pick and load a .dsk file into memory.
// ---------------------------------------------------------------------------------
void TILoadDiskFile(void)
{
  u8 bDone=false;
  u16 ucHaut=0x00, ucBas=0x00, ucSHaut=0x00, ucSBas=0x00, romSelected=0, firstRomDisplay=0, nbRomPerPage, uNbRSPage;
  s16 uLenFic=0, ucFlip=0, ucFlop=0;

  // Show the menu...
  while ((keysCurrent() & (KEY_TOUCH | KEY_START | KEY_SELECT | KEY_A | KEY_B))!=0);
  unsigned short dmaVal =  *(bgGetMapPtr(bg0b) + 24*32);
  dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+5*32*2,32*19*2);
  DS_Print(7,4,0,"A=SELECT,  B=EXIT");

  // Change into the last known DSKs directory
  chdir(currentDirDSKs);

  TI99FindDskFiles();

  chosenDSK = -1;

  nbRomPerPage = (countDSK>=16 ? 16 : countDSK);
  uNbRSPage = (countDSK>=5 ? 5 : countDSK);

  if (ucDskAct>countDSK-nbRomPerPage)
  {
    firstRomDisplay=countDSK-nbRomPerPage;
    romSelected=ucDskAct-countDSK+nbRomPerPage;
  }
  else
  {
    firstRomDisplay=ucDskAct;
    romSelected=0;
  }
  dsDisplayDsks(firstRomDisplay,romSelected);

  // -----------------------------------------------------
  // Until the user selects a file or exits the menu...
  // -----------------------------------------------------
  while (!bDone)
  {
    if (keysCurrent() & KEY_UP)
    {
      if (!ucHaut)
      {
        ucDskAct = (ucDskAct>0 ? ucDskAct-1 : countDSK-1);
        if (romSelected>uNbRSPage) { romSelected -= 1; }
        else {
          if (firstRomDisplay>0) { firstRomDisplay -= 1; }
          else {
            if (romSelected>0) { romSelected -= 1; }
            else {
              firstRomDisplay=countDSK-nbRomPerPage;
              romSelected=nbRomPerPage-1;
            }
          }
        }
        ucHaut=0x01;
        dsDisplayDsks(firstRomDisplay,romSelected);
      }
      else {

        ucHaut++;
        if (ucHaut>10) ucHaut=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else
    {
      ucHaut = 0;
    }
    if (keysCurrent() & KEY_DOWN)
    {
      if (!ucBas) {
        ucDskAct = (ucDskAct< countDSK-1 ? ucDskAct+1 : 0);
        if (romSelected<uNbRSPage-1) { romSelected += 1; }
        else {
          if (firstRomDisplay<countDSK-nbRomPerPage) { firstRomDisplay += 1; }
          else {
            if (romSelected<nbRomPerPage-1) { romSelected += 1; }
            else {
              firstRomDisplay=0;
              romSelected=0;
            }
          }
        }
        ucBas=0x01;
        dsDisplayDsks(firstRomDisplay,romSelected);
      }
      else
      {
        ucBas++;
        if (ucBas>10) ucBas=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucBas = 0;
    }

    // -------------------------------------------------------------
    // Left and Right on the D-Pad will scroll 1 page at a time...
    // -------------------------------------------------------------
    if (keysCurrent() & KEY_RIGHT)
    {
      if (!ucSBas)
      {
        ucDskAct = (ucDskAct< countDSK-nbRomPerPage ? ucDskAct+nbRomPerPage : countDSK-nbRomPerPage);
        if (firstRomDisplay<countDSK-nbRomPerPage) { firstRomDisplay += nbRomPerPage; }
        else { firstRomDisplay = countDSK-nbRomPerPage; }
        if (ucDskAct == countDSK-nbRomPerPage) romSelected = 0;
        ucSBas=0x01;
        dsDisplayDsks(firstRomDisplay,romSelected);
      }
      else
      {
        ucSBas++;
        if (ucSBas>10) ucSBas=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucSBas = 0;
    }

    // -------------------------------------------------------------
    // Left and Right on the D-Pad will scroll 1 page at a time...
    // -------------------------------------------------------------
    if (keysCurrent() & KEY_LEFT)
    {
      if (!ucSHaut)
      {
        ucDskAct = (ucDskAct> nbRomPerPage ? ucDskAct-nbRomPerPage : 0);
        if (firstRomDisplay>nbRomPerPage) { firstRomDisplay -= nbRomPerPage; }
        else { firstRomDisplay = 0; }
        if (ucDskAct == 0) romSelected = 0;
        if (romSelected > ucDskAct) romSelected = ucDskAct;
        ucSHaut=0x01;
        dsDisplayDsks(firstRomDisplay,romSelected);
      }
      else
      {
        ucSHaut++;
        if (ucSHaut>10) ucSHaut=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucSHaut = 0;
    }

    // -------------------------------------------------------------------------
    // They B key will exit out of the ROM selection without picking a new game
    // -------------------------------------------------------------------------
    if ( keysCurrent() & KEY_B )
    {
      bDone=true;
      while (keysCurrent() & KEY_B);
    }

    // -------------------------------------------------------------------
    // Any of these keys will pick the current ROM and try to load it...
    // -------------------------------------------------------------------
    if (keysCurrent() & KEY_A || keysCurrent() & KEY_Y || keysCurrent() & KEY_X)
    {
      if (keysCurrent() & KEY_X) bShowDebug = 1; else bShowDebug = 0;

      if (gpDsk[ucDskAct].uType != DIRECT)
      {
        bDone=true;
        chosenDSK = ucDskAct;
        WAITVBL;
      }
      else
      {
        chdir(gpDsk[ucDskAct].szName);
        TI99FindDskFiles();
        ucDskAct = 0;
        nbRomPerPage = (countDSK>=16 ? 16 : countDSK);
        uNbRSPage = (countDSK>=5 ? 5 : countDSK);
        if (ucDskAct>countDSK-nbRomPerPage) {
          firstRomDisplay=countDSK-nbRomPerPage;
          romSelected=ucDskAct-countDSK+nbRomPerPage;
        }
        else {
          firstRomDisplay=ucDskAct;
          romSelected=0;
        }
        dsDisplayDsks(firstRomDisplay,romSelected);
        while (keysCurrent() & KEY_A);
      }
    }

    // --------------------------------------------
    // If the filename is too long... scroll it.
    // --------------------------------------------
    if (strlen(gpDsk[ucDskAct].szName) > 29)
    {
      ucFlip++;
      if (ucFlip >= 25)
      {
        ucFlip = 0;
        uLenFic++;
        if ((uLenFic+28)>strlen(gpDsk[ucDskAct].szName))
        {
          ucFlop++;
          if (ucFlop >= 15)
          {
            uLenFic=0;
            ucFlop = 0;
          }
          else
            uLenFic--;
        }
        strncpy(szName,gpDsk[ucDskAct].szName+uLenFic,28);
        szName[28] = '\0';
        DS_Print(1,6+romSelected,2,szName);
      }
    }
    swiWaitForVBlank();
  }

  // If a DSK was selected...
  if (chosenDSK != -1)
  {
      // Remember the directory for the rom
      getcwd(currentDirDSKs, MAX_PATH);
      
      // These need to be stashed away for the caller...
      strcpy(myDskFile, gpDsk[chosenDSK].szName);
      strcpy(myDskPath, currentDirDSKs);
  }
  else
  {
      // Make no changes - keep things as they were...
  }

  // Returns the top screen to bitmap mode
  while ((keysCurrent() & (KEY_TOUCH | KEY_START | KEY_SELECT | KEY_A | KEY_B | KEY_R | KEY_L | KEY_UP | KEY_DOWN))!=0);
}

// ----------------------------------------------------------------
// Let the user select a new game (rom) file and load it up!
// ----------------------------------------------------------------
u8 tiDSLoadFile(void)
{
  u8 bDone=false;
  u16 ucHaut=0x00, ucBas=0x00,ucSHaut=0x00, ucSBas=0x00, romSelected=0, firstRomDisplay=0, nbRomPerPage, uNbRSPage;
  s16 uLenFic=0, ucFlip=0, ucFlop=0;

  // Show the menu...
  while ((keysCurrent() & (KEY_TOUCH | KEY_START | KEY_SELECT | KEY_A | KEY_B))!=0);
  unsigned short dmaVal =  *(bgGetMapPtr(bg0b) + 24*32);
  dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+5*32*2,32*19*2);
  DS_Print(7,4,0,"A=SELECT,  B=EXIT");

  // Change into the last known ROMs directory
  chdir(currentDirROMs);

  TI99FindFiles();

  ucGameChoice = -1;

  nbRomPerPage = (countTI>=16 ? 16 : countTI);
  uNbRSPage = (countTI>=5 ? 5 : countTI);

  if (ucGameAct>countTI)
  {
    firstRomDisplay=ucGameAct=0;
    romSelected=0;
  }
  else if (ucGameAct>countTI-nbRomPerPage)
  {
    firstRomDisplay=countTI-nbRomPerPage;
    romSelected=ucGameAct-countTI+nbRomPerPage;
  }
  else
  {
    firstRomDisplay=ucGameAct;
    romSelected=0;
  }
  dsDisplayFiles(firstRomDisplay,romSelected);

  // -----------------------------------------------------
  // Until the user selects a file or exits the menu...
  // -----------------------------------------------------
  while (!bDone)
  {
    if (keysCurrent() & KEY_UP)
    {
      if (!ucHaut)
      {
        ucGameAct = (ucGameAct>0 ? ucGameAct-1 : countTI-1);
        if (romSelected>uNbRSPage) { romSelected -= 1; }
        else {
          if (firstRomDisplay>0) { firstRomDisplay -= 1; }
          else {
            if (romSelected>0) { romSelected -= 1; }
            else {
              firstRomDisplay=countTI-nbRomPerPage;
              romSelected=nbRomPerPage-1;
            }
          }
        }
        ucHaut=0x01;
        dsDisplayFiles(firstRomDisplay,romSelected);
      }
      else {

        ucHaut++;
        if (ucHaut>10) ucHaut=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else
    {
      ucHaut = 0;
    }
    if (keysCurrent() & KEY_DOWN)
    {
      if (!ucBas) {
        ucGameAct = (ucGameAct< countTI-1 ? ucGameAct+1 : 0);
        if (romSelected<uNbRSPage-1) { romSelected += 1; }
        else {
          if (firstRomDisplay<countTI-nbRomPerPage) { firstRomDisplay += 1; }
          else {
            if (romSelected<nbRomPerPage-1) { romSelected += 1; }
            else {
              firstRomDisplay=0;
              romSelected=0;
            }
          }
        }
        ucBas=0x01;
        dsDisplayFiles(firstRomDisplay,romSelected);
      }
      else
      {
        ucBas++;
        if (ucBas>10) ucBas=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucBas = 0;
    }

    // -------------------------------------------------------------
    // Left and Right on the D-Pad will scroll 1 page at a time...
    // -------------------------------------------------------------
    if (keysCurrent() & KEY_RIGHT)
    {
      if (!ucSBas)
      {
        ucGameAct = (ucGameAct< countTI-nbRomPerPage ? ucGameAct+nbRomPerPage : countTI-nbRomPerPage);
        if (firstRomDisplay<countTI-nbRomPerPage) { firstRomDisplay += nbRomPerPage; }
        else { firstRomDisplay = countTI-nbRomPerPage; }
        if (ucGameAct == countTI-nbRomPerPage) romSelected = 0;
        ucSBas=0x01;
        dsDisplayFiles(firstRomDisplay,romSelected);
      }
      else
      {
        ucSBas++;
        if (ucSBas>10) ucSBas=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucSBas = 0;
    }

    // -------------------------------------------------------------
    // Left and Right on the D-Pad will scroll 1 page at a time...
    // -------------------------------------------------------------
    if (keysCurrent() & KEY_LEFT)
    {
      if (!ucSHaut)
      {
        ucGameAct = (ucGameAct> nbRomPerPage ? ucGameAct-nbRomPerPage : 0);
        if (firstRomDisplay>nbRomPerPage) { firstRomDisplay -= nbRomPerPage; }
        else { firstRomDisplay = 0; }
        if (ucGameAct == 0) romSelected = 0;
        if (romSelected > ucGameAct) romSelected = ucGameAct;
        ucSHaut=0x01;
        dsDisplayFiles(firstRomDisplay,romSelected);
      }
      else
      {
        ucSHaut++;
        if (ucSHaut>10) ucSHaut=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucSHaut = 0;
    }

    // -------------------------------------------------------------------------
    // They B key will exit out of the ROM selection without picking a new game
    // -------------------------------------------------------------------------
    if ( keysCurrent() & KEY_B )
    {
      bDone=true;
      while (keysCurrent() & KEY_B);
    }

    // -------------------------------------------------------------------
    // Any of these keys will pick the current ROM and try to load it...
    // -------------------------------------------------------------------
    if (keysCurrent() & KEY_A || keysCurrent() & KEY_Y || keysCurrent() & KEY_X)
    {
      if (keysCurrent() & KEY_X) bShowDebug = 1; else bShowDebug = 0;

      if (gpFic[ucGameAct].uType != DIRECT)
      {
        bDone=true;
        ucGameChoice = ucGameAct;
        WAITVBL;
      }
      else
      {
        chdir(gpFic[ucGameAct].szName);
        TI99FindFiles();
        ucGameAct = 0;
        nbRomPerPage = (countTI>=16 ? 16 : countTI);
        uNbRSPage = (countTI>=5 ? 5 : countTI);
        if (ucGameAct>countTI-nbRomPerPage) {
          firstRomDisplay=countTI-nbRomPerPage;
          romSelected=ucGameAct-countTI+nbRomPerPage;
        }
        else {
          firstRomDisplay=ucGameAct;
          romSelected=0;
        }
        dsDisplayFiles(firstRomDisplay,romSelected);
        while (keysCurrent() & KEY_A);
      }
    }

    // --------------------------------------------
    // If the filename is too long... scroll it.
    // --------------------------------------------
    if (strlen(gpFic[ucGameAct].szName) > 29)
    {
      ucFlip++;
      if (ucFlip >= 25)
      {
        ucFlip = 0;
        uLenFic++;
        if ((uLenFic+28)>strlen(gpFic[ucGameAct].szName))
        {
          ucFlop++;
          if (ucFlop >= 15)
          {
            uLenFic=0;
            ucFlop = 0;
          }
          else
            uLenFic--;
        }
        strncpy(szName,gpFic[ucGameAct].szName+uLenFic,28);
        szName[28] = '\0';
        DS_Print(1,6+romSelected,2,szName);
      }
    }
    swiWaitForVBlank();
  }

  // Returns the top screen to bitmap mode
  while ((keysCurrent() & (KEY_TOUCH | KEY_START | KEY_SELECT | KEY_A | KEY_B | KEY_R | KEY_L | KEY_UP | KEY_DOWN))!=0);

  // If a game was selected...
  if (ucGameChoice != -1)
  {
      // Remember the directory for the rom
      getcwd(currentDirROMs, MAX_PATH);
  }

  return 0x01;
}


// ---------------------------------------------------------------------------
// Write out the DS994a.DAT configuration file to capture the settings for
// each game.  This one file contains global settings + 400 game settings.
// ---------------------------------------------------------------------------
void SaveConfig(bool bShow)
{
    FILE *fp;
    s16 slot = 0;

    if (bShow) DS_Print(6,2,0, (char*)"SAVING CONFIGURATION");

    // Set the global configuration version number...
    globalConfig.config_ver = CONFIG_VER;

    // If there is a game loaded, save that into a slot... re-use the same slot if it exists
    if (ucGameChoice != -1)
    {
        myConfig.game_crc = file_crc;
        // Find the slot we should save into...
        for (slot=0; slot<MAX_CONFIGS; slot++)
        {
            if (AllConfigs[slot].game_crc == myConfig.game_crc)  // Got a match?!
            {
                break;
            }
            if (AllConfigs[slot].game_crc == 0x00000000)  // Didn't find it... use a blank slot...
            {
                break;
            }
        }

        // --------------------------------------------------------------------------
        // Copy our current game configuration to the main configuration database...
        // --------------------------------------------------------------------------
        memcpy(&AllConfigs[slot], &myConfig, sizeof(struct Config_t));
    }

    // --------------------------------------------------
    // Now save the config file out o the SD card...
    // --------------------------------------------------
    DIR* dir = opendir("/data");
    if (dir)
    {
        closedir(dir);  // Directory exists.
    }
    else
    {
        mkdir("/data", 0777);   // Doesn't exist - make it...
    }
    fp = fopen("/data/DS994a.DAT", "wb+");
    if (fp != NULL)
    {
        fwrite(&globalConfig, sizeof(globalConfig), 1, fp);
        fwrite(&AllConfigs, sizeof(AllConfigs), 1, fp);
        fclose(fp);
    } else DS_Print(4,0,0, (char*)"ERROR SAVING CONFIG FILE");

    if (bShow)
    {
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        DS_Print(4,2,0, (char*)"                        ");
    }
}

void MapPlayer2(void)
{
    myConfig.keymap[0]   = JOY2_UP;      // NDS D-Pad UP
    myConfig.keymap[1]   = JOY2_DOWN;    // NDS D-Pad DOWN
    myConfig.keymap[2]   = JOY2_LEFT;    // NDS D-Pad LEFT
    myConfig.keymap[3]   = JOY2_RIGHT;   // NDS D-Pad RIGHT
    myConfig.keymap[4]   = JOY2_FIRE;    // NDS A Button
    myConfig.keymap[5]   = JOY2_FIRE;    // NDS B Button
    myConfig.keymap[6]   = KBD_1;        // NDS X Button
    myConfig.keymap[7]   = KBD_2;        // NDS Y Button

    myConfig.keymap[8]   = KBD_FNCT;     // NDS L
    myConfig.keymap[9]   = KBD_SHIFT;    // NDS R
    myConfig.keymap[10]  = KBD_ENTER;    // NDS Start  mapped to ENTER
    myConfig.keymap[11]  = KBD_SPACE;    // NDS Select mapped to SPACE
}

void MapPlayer1(void)
{
    myConfig.keymap[0]   = JOY1_UP;      // NDS D-Pad UP
    myConfig.keymap[1]   = JOY1_DOWN;    // NDS D-Pad DOWN
    myConfig.keymap[2]   = JOY1_LEFT;    // NDS D-Pad LEFT
    myConfig.keymap[3]   = JOY1_RIGHT;   // NDS D-Pad RIGHT
    myConfig.keymap[4]   = JOY1_FIRE;    // NDS A Button
    myConfig.keymap[5]   = JOY1_FIRE;    // NDS B Button
    myConfig.keymap[6]   = KBD_1;        // NDS X Button
    myConfig.keymap[7]   = KBD_2;        // NDS Y Button

    myConfig.keymap[8]   = KBD_FNCT;     // NDS L
    myConfig.keymap[9]   = KBD_SHIFT;    // NDS R
    myConfig.keymap[10]  = KBD_ENTER;    // NDS Start  mapped to ENTER
    myConfig.keymap[11]  = KBD_SPACE;    // NDS Select mapped to SPACE
}

void MapESDX(void)
{
    myConfig.keymap[0]   = KBD_E;        // NDS D-Pad UP
    myConfig.keymap[1]   = KBD_X;        // NDS D-Pad DOWN
    myConfig.keymap[2]   = KBD_S;        // NDS D-Pad LEFT
    myConfig.keymap[3]   = KBD_D;        // NDS D-Pad RIGHT
    myConfig.keymap[4]   = KBD_Q;        // NDS A Button
    myConfig.keymap[5]   = KBD_SPACE;    // NDS B Button
    myConfig.keymap[6]   = KBD_Q;        // NDS X Button
    myConfig.keymap[7]   = KBD_SPACE;    // NDS Y Button

    myConfig.keymap[8]   = KBD_FNCT;     // NDS L
    myConfig.keymap[9]   = KBD_SHIFT;    // NDS R
    myConfig.keymap[10]  = KBD_ENTER;    // NDS Start  mapped to ENTER
    myConfig.keymap[11]  = KBD_SPACE;    // NDS Select mapped to SPACE
}

void MapSTSOS(void)
{
    myConfig.keymap[0]   = KBD_K;        // NDS D-Pad UP
    myConfig.keymap[1]   = KBD_X;        // NDS D-Pad DOWN
    myConfig.keymap[2]   = KBD_S;        // NDS D-Pad LEFT
    myConfig.keymap[3]   = KBD_D;        // NDS D-Pad RIGHT
    myConfig.keymap[4]   = KBD_J;        // NDS A Button
    myConfig.keymap[5]   = KBD_L;        // NDS B Button
    myConfig.keymap[6]   = KBD_H;        // NDS X Button
    myConfig.keymap[7]   = KBD_SPACE;    // NDS Y Button

    myConfig.keymap[8]   = KBD_FNCT;     // NDS L
    myConfig.keymap[9]   = KBD_SHIFT;    // NDS R
    myConfig.keymap[10]  = KBD_ENTER;    // NDS Start  mapped to ENTER
    myConfig.keymap[11]  = KBD_SPACE;    // NDS Select mapped to SPACE
}

void SetDiagonals(void) // Useful for games like Q-Bert
{
    myConfig.keymap[0]   = JOY1_RIGHT;
    myConfig.keymap[1]   = JOY1_LEFT;
    myConfig.keymap[2]   = JOY1_UP;
    myConfig.keymap[3]   = JOY1_DOWN;
}


// ---------------------------------------------------------------------------------
// Default settings when a game is loaded but there is no previous configuration
// saved for this game. We make some default / sensible choices that the user can
// override as desired.
// ---------------------------------------------------------------------------------
void SetDefaultGameConfig(void)
{
    MapPlayer1();

    myConfig.frameSkip   = globalConfig.frameSkip;
    myConfig.frameBlend  = 0;
    myConfig.isPAL       = 0;
    myConfig.maxSprites  = globalConfig.maxSprites;
    myConfig.memWipe     = 0;
    myConfig.capsLock    = 0;
    myConfig.RAMMirrors  = (isDSiMode() ? 1:0);    // For DSi we enable the RAM mirrors
    myConfig.overlay     = globalConfig.overlay;
    myConfig.emuSpeed    = 0;
    myConfig.machineType = globalConfig.machineType;
    myConfig.cartType    = 0;   // Normal
    myConfig.dpadDiagonal= 0;   // Normal
    myConfig.spriteCheck = 0;   // Normal
    myConfig.noExtSpeech = 0;   // Default to having speech module attached
    myConfig.reservedJ   = 0;
    myConfig.reservedK   = 0;
    myConfig.reservedL   = 0;
    myConfig.reservedM   = 0;
    myConfig.reservedN   = 0;
    myConfig.reservedO   = 1;
    myConfig.reservedP   = 1;
    myConfig.reservedQ   = 0xFF;
    myConfig.reservedY   = 0xFF;
    myConfig.reservedZ   = 0xFF;

    if (file_crc == 0x48c12b3c) SetDiagonals(); // Q-Bert wants diagonal directions
    if (file_crc == 0xf939439d) SetDiagonals(); // Q-Bert wants diagonal directions

    if (file_crc == 0x4152e0e9) MapESDX();      // Tunnels of Doom uses ESDX for movement
    if (file_crc == 0x163caf9b) MapESDX();      // Tunnels of Doom uses ESDX for movement
    if (file_crc == 0x128818e2) MapESDX();      // Tunnels of Doom uses ESDX for movement
    if (file_crc == 0xc4cd53ad) MapESDX();      // Tunnels of Doom uses ESDX for movement    
    
    if (file_crc == 0xc25be90f) MapESDX();      // Restless II uses ESDX for movement
    
    if (file_crc == 0x55d49b68) MapESDX();      // Zero Zap uses ESDX for movement
    
    if (file_crc == 0x742f88ce) MapSTSOS();     // Star Trek SOS maps keys uniquely

    if (file_crc == 0xb2d6a6f1) MapPlayer2();   // Frogger wants to use Controller for P2

    if (file_crc == 0x0c0d3375) myConfig.dpadDiagonal = 1;  // Topper wants to use diagonal directions
    if (file_crc == 0xcf6c8d64) myConfig.dpadDiagonal = 1;  // Topper wants to use diagonal directions
    if (file_crc == 0x3c124691) myConfig.dpadDiagonal = 1;  // Topper wants to use diagonal directions
    
    if (file_crc == 0x2715313f) myConfig.spriteCheck = 2;   // The megademo ROM needs it this fast at least
    if (file_crc == 0xe92f15ff) myConfig.spriteCheck = 2;   // The megademo DSK needs it this fast at least
    
    if (file_crc == 0xf93533e9) myConfig.noExtSpeech = 1;   // Robots of Death II will not run with our fake Speech module (32K Ver)
    if (file_crc == 0xbc95d21f) myConfig.noExtSpeech = 1;   // Robots of Death II will not run with our fake Speech module (512K Ver)
    if (file_crc == 0xb9cd2072) myConfig.noExtSpeech = 1;   // Nuts V1.1 (AKA Fox&Goat) (2023)(TMOP) will not run with our fake Speech module

    if (file_crc == 0x478d9835) myConfig.RAMMirrors = 1;    // TI-99/4a Congo Bongo requires RAM mirrors to run properly
    if (file_crc == 0x5f85e8ed) myConfig.RAMMirrors = 1;    // TI-99/4a Congo Bongo requires RAM mirrors to run properly (32K FinalGrom ver)
    if (file_crc == 0x0b9ad832) myConfig.RAMMirrors = 1;    // TI-99/4a Buck Rogers requires RAM mirrors to run properly
    
    if (file_crc == 0x3f4c4fe5) myConfig.machineType = MACH_TYPE_SAMS; // Dungeons of Asgard uses SAMS

    if (file_crc == 0x6b911b91) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Meteor Belt requires MBX 1K of RAM
    if (file_crc == 0xe4ce86f5) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Meteor Belt requires MBX 1K of RAM    
    if (file_crc == 0xd872e83e) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Bigfoot requires MBX 1K of RAM
    if (file_crc == 0xc883dde6) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Bigfoot requires MBX 1K of RAM    
    if (file_crc == 0x2807a67f) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // SuperFly requires MBX 1K of RAM
    if (file_crc == 0x06da3412) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // SuperFly requires MBX 1K of RAM    
    if (file_crc == 0x60e66ab1) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Space Bandits requires MBX 1K of RAM
    if (file_crc == 0xc7f74062) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Space Bandits requires MBX 1K of RAM    
    if (file_crc == 0xbc245f56) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Honey Hunt requires MBX 1K of RAM
    if (file_crc == 0x2e071ff6) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Honey Hunt requires MBX 1K of RAM
    if (file_crc == 0x4bb77ca1) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Soundtrack Trolley requires MBX 1K of RAM
    if (file_crc == 0xd35f2c0d) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Soundtrack Trolley requires MBX 1K of RAM    
    if (file_crc == 0x962aca6f) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Sewermania requires MBX 1K of RAM
    if (file_crc == 0xb33dabfe) myConfig.cartType = CART_TYPE_MBX_WITH_RAM;  // Sewermania requires MBX 1K of RAM

    if (file_crc == 0xc705118e) myConfig.cartType = CART_TYPE_MINIMEM;       // The Mini-Memory module uses this special carttype
    if (file_crc == 0xe0bc224d) myConfig.cartType = CART_TYPE_MINIMEM;       // The Mini-Memory module uses this special carttype
    if (file_crc == 0x134144dc) myConfig.cartType = CART_TYPE_MINIMEM;       // The Mini-Memory module uses this special carttype   
}

// -------------------------------------------------------------------------
// Find the DS994a.DAT file and load it... if it doesn't exist, then
// default values will be used for the entire configuration database...
// -------------------------------------------------------------------------
void FindAndLoadConfig(void)
{
    FILE *fp;

    // -----------------------------------------------------------------
    // Start with defaults.. if we find a match in our config database
    // below, we will fill in the config with data read from the file.
    // -----------------------------------------------------------------
    SetDefaultGameConfig();

    fp = fopen("/data/DS994a.DAT", "rb");
    if (fp != NULL)
    {
        fread(&globalConfig, sizeof(globalConfig), 1, fp);
        fread(&AllConfigs, sizeof(AllConfigs), 1, fp);
        fclose(fp);
        
        if (globalConfig.config_ver == 0x0007) // Previous config - upgrade the global database
        {
            globalConfig.frameSkip = (isDSiMode() ? 0:1);    // For DSi we don't need FrameSkip, but for older DS-LITE we turn on light frameskip
            globalConfig.config_ver = CONFIG_VER;
        }

        if (globalConfig.config_ver != CONFIG_VER)
        {
            memset(&globalConfig, 0x00, sizeof(globalConfig));
            memset(&AllConfigs, 0x00, sizeof(AllConfigs));
            globalConfig.overlay=1;     // TI99 Keyboard
            globalConfig.floppySound=1; // Enable sounds
            globalConfig.frameSkip = (isDSiMode() ? 0:1);    // For DSi we don't need FrameSkip, but for older DS-LITE we turn on light frameskip
            SetDefaultGameConfig();
            SaveConfig(FALSE);
        }
        else
        {
            if (ucGameChoice != -1) // If we have a game selected...
            {
                for (u16 slot=0; slot<MAX_CONFIGS; slot++)
                {
                    if (AllConfigs[slot].game_crc == file_crc)  // Got a match?!
                    {
                        memcpy(&myConfig, &AllConfigs[slot], sizeof(struct Config_t));
                        break;
                    }
                }
            }
        }
    }
    else    // Not found... init the entire database...
    {
        memset(&globalConfig, 0x00, sizeof(globalConfig));
        memset(&AllConfigs, 0x00, sizeof(AllConfigs));
        globalConfig.overlay=1; // TI99 Keyboard
        globalConfig.floppySound=1; // Enable sounds
        globalConfig.frameSkip = (isDSiMode() ? 0:1);    // For DSi we don't need FrameSkip, but for older DS-LITE we turn on light frameskip
        SetDefaultGameConfig();
        SaveConfig(FALSE);
    }
}


// ------------------------------------------------------------------------------
// Options are handled here... we have a number of things the user can tweak
// and these options are applied immediately. The user can also save off
// their option choices for the currently running game into the NINTV-DS.DAT
// configuration database. When games are loaded back up, NINTV-DS.DAT is read
// to see if we have a match and the user settings can be restored for the game.
// ------------------------------------------------------------------------------
struct options_t
{
    const char  *label;
    const char  *option[16];    // Up to 16 option choices per option row
    u8          *option_val;
    u8           option_max;
};

const struct options_t Option_Table[2][20] =
{
    {
        {"OVERLAY",        {"TI99 3D KBD", "TI99 FLAT KBD"},                                                                                 &myConfig.overlay,      2},
        {"FRAME SKIP",     {"OFF", "SHOW 3/4", "SHOW 1/2"},                                                                                  &myConfig.frameSkip,    3},
        {"FRAME BLEND",    {"OFF", "ON"},                                                                                                    &myConfig.frameBlend,   2},
        {"MAX SPRITES",    {"4",   "32"},                                                                                                    &myConfig.maxSprites,   2},
        {"TV TYPE",        {"NTSC","PAL"},                                                                                                   &myConfig.isPAL,        2},
        {"MACHINE TYPE",   {"32K EXPANDED", "SAMS 1MB/512K"},                                                                                &myConfig.machineType,  2},
        {"CART TYPE",      {"NORMAL", "SUPERCART 32K", "MINIMEM 4K", "MBX NO RAM", "MBX WITH RAM", "PAGED CRU"},                             &myConfig.cartType,     6},
        {"EMU SPEED",      {"NORMAL", "110 PERCENT", "120 PERCENT", "130 PERCENT", "140 PERCENT", "150 PERCENT", "90 PERCENT", "80 PERCENT"},&myConfig.emuSpeed,     8},
        {"CAPS LOCK",      {"OFF", "ON"},                                                                                                    &myConfig.capsLock,     2},
        {"RAM MIRRORS",    {"OFF", "ON"},                                                                                                    &myConfig.RAMMirrors,   2},
        {"RAM WIPE",       {"CLEAR", "RANDOM",},                                                                                             &myConfig.memWipe,      2},
        {"SPRITE CHECK",   {"NORMAL (32/64)", "4 SCANLINES", "8 SCANLINES", "16 SCANLINES", "32 SCANLINES", "64 SCANLINES", "END OF FRAME"}, &myConfig.spriteCheck,  7},
        {"SPEECH MODL",    {"INSTALLED", "NOT INSTALLED"},                                                                                   &myConfig.noExtSpeech,  2},        
        {"NDS DPAD",       {"NORMAL", "DIAGONALS",},                                                                                         &myConfig.dpadDiagonal, 2},
        {NULL,             {"",      ""},                                                                                                    NULL,                   1},
    },
    // Page 2
    {
        {NULL,             {"",      ""},                                                                                                    NULL,                   1},
    }
};


// ------------------------------------------------------------------
// Display the current list of options for the user.
// ------------------------------------------------------------------
u8 display_options_list(bool bFullDisplay)
{
    s16 len=0;

    DS_Print(1,21, 0, (char *)"                              ");
    if (bFullDisplay)
    {
        while (true)
        {
            sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][len].label, Option_Table[option_table][len].option[*(Option_Table[option_table][len].option_val)]);
            DS_Print(1,5+len, (len==0 ? 2:0), strBuf); len++;
            if (Option_Table[option_table][len].label == NULL) break;
        }

        // Blank out rest of the screen... option menus are of different lengths...
        for (int i=len; i<15; i++)
        {
            DS_Print(1,5+i, 0, (char *)"                               ");
        }
    }

    DS_Print(0,22, 0, (char *)"       B=EXIT, START=SAVE       ");
    return len;
}


// ---------------------------------------------------------------------------------
// Change Game Options for the current game - user can save them out via START
// ---------------------------------------------------------------------------------
void tiDSGameOptions(void)
{
    u8 optionHighlighted;
    u8 idx;
    u8 bDone=false;
    s16 keys_pressed;
    s16 last_keys_pressed = 999;

    idx=display_options_list(true);
    optionHighlighted = 0;
    while (keysCurrent() != 0)
    {
        WAITVBL;
    }
    while (!bDone)
    {
        keys_pressed = keysCurrent();
        if (keys_pressed != last_keys_pressed)
        {
            last_keys_pressed = keys_pressed;
            if (keysCurrent() & KEY_UP) // Previous option
            {
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,0, strBuf);
                if (optionHighlighted > 0) optionHighlighted--; else optionHighlighted=(idx-1);
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_DOWN) // Next option
            {
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,0, strBuf);
                if (optionHighlighted < (idx-1)) optionHighlighted++;  else optionHighlighted=0;
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,2, strBuf);
            }

            if (keysCurrent() & KEY_RIGHT)  // Toggle option clockwise
            {
                *(Option_Table[option_table][optionHighlighted].option_val) = (*(Option_Table[option_table][optionHighlighted].option_val) + 1) % Option_Table[option_table][optionHighlighted].option_max;
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_LEFT)  // Toggle option counterclockwise
            {
                if ((*(Option_Table[option_table][optionHighlighted].option_val)) == 0)
                    *(Option_Table[option_table][optionHighlighted].option_val) = Option_Table[option_table][optionHighlighted].option_max -1;
                else
                    *(Option_Table[option_table][optionHighlighted].option_val) = (*(Option_Table[option_table][optionHighlighted].option_val) - 1) % Option_Table[option_table][optionHighlighted].option_max;
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_START)  // Save Options
            {
                SaveConfig(TRUE);
            }
#if 0   // Future expansion in case we need another page of options
            if (keysCurrent() & (KEY_X)) // Toggle Table
            {
                option_table = (option_table + 1) % 2;
                idx=display_options_list(true);
                optionHighlighted = 0;
                while (keysCurrent() != 0)
                {
                    WAITVBL;
                }
            }
#endif
            if ((keysCurrent() & KEY_B) || (keysCurrent() & KEY_A))  // Exit options
            {
                option_table = 0;   // Reset for next time
                break;
            }
        }
        swiWaitForVBlank();
    }

    // Give a third of a second time delay...
    for (int i=0; i<20; i++)
    {
        swiWaitForVBlank();
    }

    return;
}


const struct options_t GlobalOption_Table[] =
{
    {"FPS",            {"OFF", "ON", "ON FULLSPEED"},                           &globalConfig.showFPS,       3},
    {"BIOS SCREEN",    {"SHOW AT START", "SKIP AT START"},                      &globalConfig.skipBIOS,      2},
    {"ROMS DIR",       {"/ROMS/TI99", "/ROMS", "SAME AS EMU"},                  &globalConfig.romsDIR,       3},
    {"DEF OVERLAY",    {"TI99 3D KBD", "TI99 FLAT KBD"},                        &globalConfig.overlay,       2},
    {"DEF MACHINE",    {"32K EXPANDED", "SAMS 512K/1MB"},                       &globalConfig.machineType,   2},
    {"DEF SPRITES",    {"4", "32"},                                             &globalConfig.maxSprites,    2},
    {"DEF FRAMESKP",   {"OFF", "ON"},                                           &globalConfig.frameSkip,     2},
    {"FLOPPY SFX",     {"OFF", "ON"},                                           &globalConfig.floppySound,   2},

    {NULL,             {"",      ""},                                           NULL,                        1},
};


// ------------------------------------------------------------------
// Display the current list of options for the user.
// ------------------------------------------------------------------
u8 display_global_options_list(bool bFullDisplay)
{
    s16 len=0;

    DS_Print(1,21, 0, (char *)"                              ");
    if (bFullDisplay)
    {
        while (true)
        {
            sprintf(strBuf, " %-12s : %-14s", GlobalOption_Table[len].label, GlobalOption_Table[len].option[*(GlobalOption_Table[len].option_val)]);
            DS_Print(1,5+len, (len==0 ? 2:0), strBuf); len++;
            if (GlobalOption_Table[len].label == NULL) break;
        }

        // Blank out rest of the screen... option menus are of different lengths...
        for (int i=len; i<15; i++)
        {
            DS_Print(1,5+i, 0, (char *)"                               ");
        }
    }

    DS_Print(0,22, 0, (char *)"       B=EXIT, START=SAVE       ");
    return len;
}

void tiDSGlobalOptions(void)
{
    u8 optionHighlighted;
    u8 idx;
    u8 bDone=false;
    s16 keys_pressed;
    s16 last_keys_pressed = 999;

    idx=display_global_options_list(true);
    optionHighlighted = 0;
    while (keysCurrent() != 0)
    {
        WAITVBL;
    }
    while (!bDone)
    {
        keys_pressed = keysCurrent();
        if (keys_pressed != last_keys_pressed)
        {
            last_keys_pressed = keys_pressed;
            if (keysCurrent() & KEY_UP) // Previous option
            {
                sprintf(strBuf, " %-12s : %-14s", GlobalOption_Table[optionHighlighted].label, GlobalOption_Table[optionHighlighted].option[*(GlobalOption_Table[optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,0, strBuf);
                if (optionHighlighted > 0) optionHighlighted--; else optionHighlighted=(idx-1);
                sprintf(strBuf, " %-12s : %-14s", GlobalOption_Table[optionHighlighted].label, GlobalOption_Table[optionHighlighted].option[*(GlobalOption_Table[optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_DOWN) // Next option
            {
                sprintf(strBuf, " %-12s : %-14s", GlobalOption_Table[optionHighlighted].label, GlobalOption_Table[optionHighlighted].option[*(GlobalOption_Table[optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,0, strBuf);
                if (optionHighlighted < (idx-1)) optionHighlighted++;  else optionHighlighted=0;
                sprintf(strBuf, " %-12s : %-14s", GlobalOption_Table[optionHighlighted].label, GlobalOption_Table[optionHighlighted].option[*(GlobalOption_Table[optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,2, strBuf);
            }

            if (keysCurrent() & KEY_RIGHT)  // Toggle option clockwise
            {
                *(GlobalOption_Table[optionHighlighted].option_val) = (*(GlobalOption_Table[optionHighlighted].option_val) + 1) % GlobalOption_Table[optionHighlighted].option_max;
                sprintf(strBuf, " %-12s : %-14s", GlobalOption_Table[optionHighlighted].label, GlobalOption_Table[optionHighlighted].option[*(GlobalOption_Table[optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_LEFT)  // Toggle option counterclockwise
            {
                if ((*(GlobalOption_Table[optionHighlighted].option_val)) == 0)
                    *(GlobalOption_Table[optionHighlighted].option_val) = GlobalOption_Table[optionHighlighted].option_max -1;
                else
                    *(GlobalOption_Table[optionHighlighted].option_val) = (*(GlobalOption_Table[optionHighlighted].option_val) - 1) % GlobalOption_Table[optionHighlighted].option_max;
                sprintf(strBuf, " %-12s : %-14s", GlobalOption_Table[optionHighlighted].label, GlobalOption_Table[optionHighlighted].option[*(GlobalOption_Table[optionHighlighted].option_val)]);
                DS_Print(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_START)  // Save Options
            {
                SaveConfig(TRUE);
            }
            if ((keysCurrent() & KEY_B) || (keysCurrent() & KEY_A))  // Exit options
            {
                break;
            }
        }
        swiWaitForVBlank();
    }

    // Give a third of a second time delay...
    for (int i=0; i<20; i++)
    {
        swiWaitForVBlank();
    }

    return;
}

//*****************************************************************************
// Change Keymap Options for the current game
//*****************************************************************************
void DisplayKeymapName(u32 uY)
{
  static char szCha[34];

  sprintf(szCha," PAD UP    : %-17s",szKeyName[myConfig.keymap[0]]);
  DS_Print(1, 6,(uY==  6 ? 2 : 0),szCha);
  sprintf(szCha," PAD DOWN  : %-17s",szKeyName[myConfig.keymap[1]]);
  DS_Print(1, 7,(uY==  7 ? 2 : 0),szCha);
  sprintf(szCha," PAD LEFT  : %-17s",szKeyName[myConfig.keymap[2]]);
  DS_Print(1, 8,(uY==  8 ? 2 : 0),szCha);
  sprintf(szCha," PAD RIGHT : %-17s",szKeyName[myConfig.keymap[3]]);
  DS_Print(1, 9,(uY== 9 ? 2 : 0),szCha);
  sprintf(szCha," KEY A     : %-17s",szKeyName[myConfig.keymap[4]]);
  DS_Print(1,10,(uY== 10 ? 2 : 0),szCha);
  sprintf(szCha," KEY B     : %-17s",szKeyName[myConfig.keymap[5]]);
  DS_Print(1,11,(uY== 11 ? 2 : 0),szCha);
  sprintf(szCha," KEY X     : %-17s",szKeyName[myConfig.keymap[6]]);
  DS_Print(1,12,(uY== 12 ? 2 : 0),szCha);
  sprintf(szCha," KEY Y     : %-17s",szKeyName[myConfig.keymap[7]]);
  DS_Print(1,13,(uY== 13 ? 2 : 0),szCha);
  sprintf(szCha," KEY L     : %-17s",szKeyName[myConfig.keymap[8]]);
  DS_Print(1,14,(uY== 14 ? 2 : 0),szCha);
  sprintf(szCha," KEY R     : %-17s",szKeyName[myConfig.keymap[9]]);
  DS_Print(1,15,(uY== 15 ? 2 : 0),szCha);
  sprintf(szCha," START     : %-17s",szKeyName[myConfig.keymap[10]]);
  DS_Print(1,16,(uY== 16 ? 2 : 0),szCha);
  sprintf(szCha," SELECT    : %-17s",szKeyName[myConfig.keymap[11]]);
  DS_Print(1,17,(uY== 17 ? 2 : 0),szCha);
}

// ------------------------------------------------------------------------------
// Allow the user to change the key map for the current game and give them
// the option of writing that keymap out to a configuration file for the game.
// ------------------------------------------------------------------------------
void tiDSChangeKeymap(void)
{
  u32 ucHaut=0x00, ucBas=0x00,ucL=0x00,ucR=0x00,ucY= 6, bOK=0, bIndTch=0;

  // ------------------------------------------------------
  // Clear the screen so we can put up Key Map infomation
  // ------------------------------------------------------
  unsigned short dmaVal =  *(bgGetMapPtr(bg0b) + 24*32);
  dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+5*32*2,32*19*2);

  // --------------------------------------------------
  // Give instructions to the user...
  // --------------------------------------------------
  DS_Print(1 ,19,0,("   D-PAD : CHANGE KEY MAP    "));
  DS_Print(1 ,20,0,("       B : RETURN MAIN MENU  "));
  DS_Print(1 ,21,0,("       X : SWAP P1,P2,ESDX   "));
  DS_Print(1 ,22,0,("   START : SAVE KEYMAP       "));
  DisplayKeymapName(ucY);

  // -----------------------------------------------------------------------
  // Clear out any keys that might be pressed on the way in - make sure
  // NDS keys are not being pressed. This prevents the inadvertant A key
  // that enters this menu from also being acted on in the keymap...
  // -----------------------------------------------------------------------
  while ((keysCurrent() & (KEY_TOUCH | KEY_B | KEY_A | KEY_X | KEY_Y | KEY_UP | KEY_DOWN))!=0)
      ;
  WAITVBL;

  while (!bOK) {
    if (keysCurrent() & KEY_UP) {
      if (!ucHaut) {
        DisplayKeymapName(32);
        ucY = (ucY == 6 ? 17 : ucY -1);
        bIndTch = myConfig.keymap[ucY-6];
        ucHaut=0x01;
        DisplayKeymapName(ucY);
      }
      else {
        ucHaut++;
        if (ucHaut>10) ucHaut=0;
      }
    }
    else {
      ucHaut = 0;
    }
    if (keysCurrent() & KEY_DOWN) {
      if (!ucBas) {
        DisplayKeymapName(32);
        ucY = (ucY == 17 ? 6 : ucY +1);
        bIndTch = myConfig.keymap[ucY-6];
        ucBas=0x01;
        DisplayKeymapName(ucY);
      }
      else {
        ucBas++;
        if (ucBas>10) ucBas=0;
      }
    }
    else {
      ucBas = 0;
    }

    if (keysCurrent() & KEY_START)
    {
        SaveConfig(true); // Save options
    }

    if (keysCurrent() & KEY_B)
    {
      bOK = 1;  // Exit menu
    }

    if (keysCurrent() & KEY_LEFT)
    {
        if (ucL == 0) {
          bIndTch = (bIndTch == 0 ? (MAX_KEY_OPTIONS-1) : bIndTch-1);
          ucL=1;
          myConfig.keymap[ucY-6] = bIndTch;
          DisplayKeymapName(ucY);
        }
        else {
          ucL++;
          if (ucL > 10) ucL = 0;
        }
    }
    else
    {
        ucL = 0;
    }

    if (keysCurrent() & KEY_RIGHT)
    {
        if (ucR == 0)
        {
          bIndTch = (bIndTch == (MAX_KEY_OPTIONS-1) ? 0 : bIndTch+1);
          ucR=1;
          myConfig.keymap[ucY-6] = bIndTch;
          DisplayKeymapName(ucY);
        }
        else
        {
          ucR++;
          if (ucR > 10) ucR = 0;
        }
    }
    else
    {
        ucR=0;
    }

    // ------------------------------------------------------
    // Cycle between Player 1, Player 2 and ESDX keymaps
    // ------------------------------------------------------
    if (keysCurrent() & KEY_X)
    {
        if ((myConfig.keymap[0] != JOY2_UP) && (myConfig.keymap[0] != KBD_E))
            MapPlayer2();
        else if (myConfig.keymap[0] == KBD_E)
            MapPlayer1();
        else
            MapESDX();

        bIndTch = myConfig.keymap[ucY-6];
        DisplayKeymapName(ucY);
        while (keysCurrent() & KEY_X)
            ;
        WAITVBL
    }

    swiWaitForVBlank();
  }
  while (keysCurrent() & KEY_B);
}


// ----------------------------------------------------------------------------------
// At the bottom of the main screen we show the currently selected filename and CRC
// ----------------------------------------------------------------------------------
void DisplayFileName(void)
{
    sprintf(szName,"%s",gpFic[ucGameChoice].szName);
    for (u8 i=strlen(szName)-1; i>0; i--) if (szName[i] == '.') {szName[i]=0;break;}
    if (strlen(szName)>30) szName[30]='\0';
    DS_Print((16 - (strlen(szName)/2)),21,0,szName);
    if (strlen(gpFic[ucGameChoice].szName) >= 35)   // If there is more than a few characters left, show it on the 2nd line
    {
        sprintf(szName,"%s",gpFic[ucGameChoice].szName+30);
        for (u8 i=strlen(szName)-1; i>0; i--) if (szName[i] == '.') {szName[i]=0;break;}
        if (strlen(szName)>30) szName[30]='\0';
        DS_Print((16 - (strlen(szName)/2)),22,0,szName);
    }
}

//*****************************************************************************
// Display TI99 screen and change options "main menu"
//*****************************************************************************
void affInfoOptions(u32 uY)
{
    DS_Print(2, 6,(uY==6  ? 2 : 0),("       INSERT  CARTRIDGE    "));
    DS_Print(2, 8,(uY==8  ? 2 : 0),("         BOOT  TI99/4A      "));
    DS_Print(2,10,(uY==10 ? 2 : 0),("     REDEFINE  KEYS         "));
    DS_Print(2,12,(uY==12 ? 2 : 0),("         CART  OPTIONS      "));
    DS_Print(2,14,(uY==14 ? 2 : 0),("       GLOBAL  OPTIONS      "));
    DS_Print(2,16,(uY==16 ? 2 : 0),("         QUIT  EMULATOR     "));
}

// --------------------------------------------------------------------
// Some main menu selections don't make sense without a game loaded.
// --------------------------------------------------------------------
void NoGameSelected(u32 ucY)
{
    unsigned short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
    while (keysCurrent()  & (KEY_START | KEY_A));
    dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+5*32*2,32*18*2);
    DS_Print(5,10,0,("   NO GAME SELECTED   "));
    DS_Print(5,12,0,("  PLEASE, USE MENU TO "));
    DS_Print(5,14,0,("      LOAD  GAME      "));
    while (!(keysCurrent()  & (KEY_START | KEY_A)));
    while (keysCurrent()  & (KEY_START | KEY_A));
    dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+5*32*2,32*18*2);
    affInfoOptions(ucY);
}

void ReadFileCRCAndConfig(void)
{
    getfile_crc(gpFic[ucGameChoice].szName);

    FindAndLoadConfig();    // Try to find keymap and config for this file...
}

// --------------------------------------------------------------------
// Let the user select new options for the currently loaded game...
// --------------------------------------------------------------------
void tiDSChangeOptions(void)
{
  u16 ucHaut=0x00, ucBas=0x00,ucA=0x00,ucY= 6, bOK=0;

  // Display the screen at the top
  videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG);

  bg0 = bgInit(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);
  bgSetPriority(bg0,1);
  decompress(splashTiles, bgGetGfxPtr(bg0), LZ77Vram);
  decompress(splashMap, (void*) bgGetMapPtr(bg0), LZ77Vram);
  dmaCopy((void*) splashPal,(void*) BG_PALETTE,256*2);

  // Display the screen at the bottom
  bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);
  bg1b = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x256, 29,0);
  bgSetPriority(bg0b,1);bgSetPriority(bg1b,0);
  decompress(optionsTiles, bgGetGfxPtr(bg0b), LZ77Vram);
  decompress(optionsMap, (void*) bgGetMapPtr(bg0b), LZ77Vram);
  dmaCopy((void*) optionsPal,(void*) BG_PALETTE_SUB,256*2);
  u16 dmaVal = *(bgGetMapPtr(bg1b)+24*32);
  dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b),32*24*2);

  affInfoOptions(ucY);

  if (ucGameChoice != -1)
  {
      DisplayFileName();
  }

  while (!bOK) {
    if (keysCurrent()  & KEY_UP) {
      if (!ucHaut) {
        affInfoOptions(32);
        ucY = (ucY == 6 ? 16 : ucY -2);
        ucHaut=0x01;
        affInfoOptions(ucY);
      }
      else {
        ucHaut++;
        if (ucHaut>10) ucHaut=0;
      }
    }
    else {
      ucHaut = 0;
    }
    if (keysCurrent()  & KEY_DOWN) {
      if (!ucBas) {
        affInfoOptions(32);
        ucY = (ucY == 16 ? 6 : ucY +2);
        ucBas=0x01;
        affInfoOptions(ucY);
      }
      else {
        ucBas++;
        if (ucBas>10) ucBas=0;
      }
    }
    else {
      ucBas = 0;
    }
    if (keysCurrent()  & KEY_A) {
      if (!ucA) {
        ucA = 0x01;
        switch (ucY) {
          case 6 :      // LOAD GAME
            tiDSLoadFile();
            dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+3*32*2,32*21*2);
            if (ucGameChoice != -1)
            {
                ReadFileCRCAndConfig(); // Get CRC32 of the file and read the config/keys
                DisplayFileName();      // And put up the filename on the bottom screen
            }
            ucY = 8;
            affInfoOptions(ucY);
            break;
          case 8 :     // PLAY GAME
            if (ucGameChoice != -1)
            {
              bOK = 1;
            }
            else
            {
                NoGameSelected(ucY);
            }
            break;
          case 10 :     // REDEFINE KEYS
            if (ucGameChoice != -1)
            {
                tiDSChangeKeymap();
                dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+3*32*2,32*21*2);
                affInfoOptions(ucY);
                DisplayFileName();
            }
            else
            {
                NoGameSelected(ucY);
            }
            break;
          case 12 :     // GAME OPTIONS
            if (ucGameChoice != -1)
            {
                tiDSGameOptions();
                dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+3*32*2,32*21*2);
                affInfoOptions(ucY);
                DisplayFileName();
            }
            else
            {
               NoGameSelected(ucY);
            }
            break;

          case 14 :     // GLOBAL OPTIONS
            tiDSGlobalOptions();
            dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+3*32*2,32*21*2);
            affInfoOptions(ucY);
            DisplayFileName();
            break;

          case 16 :     // QUIT EMULATOR
            exit(1);
            break;

        }
      }
    }
    else
      ucA = 0x00;
    if (keysCurrent()  & KEY_START) {
      if (ucGameChoice != -1)
      {
        bOK = 1;
      }
      else
      {
        NoGameSelected(ucY);
      }
    }
    swiWaitForVBlank();
  }
  while (keysCurrent()  & (KEY_START | KEY_A));
}

// ---------------------------------------------------------------------------------
// Displays a message on the screen using the built in sprite/font.
// ---------------------------------------------------------------------------------
ITCM_CODE void DS_Print(int iX,int iY,int iScr,char *szMessage)
{
  u16 *pusScreen,*pusMap;
  u16 usCharac;
  char *pTrTxt=szMessage;

  if (iScr == 1) return; // We don't support text output to the top screen

  pusScreen=(u16*) (iScr != 1 ? bgGetMapPtr(bg1b) : bgGetMapPtr(bg1))+iX+(iY<<5);
  pusMap=(u16*) (iScr != 1 ? (iScr == 6 ? bgGetMapPtr(bg0b)+24*32 : (iScr == 0 ? bgGetMapPtr(bg0b)+24*32 : bgGetMapPtr(bg0b)+26*32 )) : bgGetMapPtr(bg0)+51*32 );

  while((*pTrTxt)!='\0' )
  {
    char ch = *pTrTxt++;
    if (ch >= 'a' && ch <= 'z') ch -= 32;   // Faster than strcpy/strtoupper

    if (((ch)<' ') || ((ch)>'_'))
      usCharac=*(pusMap);                   // Will render as a vertical bar
    else if((ch)<'@')
      usCharac=*(pusMap+(ch)-' ');          // Number from 0-9 or punctuation
    else
      usCharac=*(pusMap+32+(ch)-'@');       // Character from A-Z
    *pusScreen++=usCharac;
  }
}

// -----------------------------------------------------------------------------------
// Routine FadeToColor-Fade from background to black or white. Used for splash intro.
// -----------------------------------------------------------------------------------
void FadeToColor(unsigned char ucSens, unsigned short ucBG, unsigned char ucScr, unsigned char valEnd, unsigned char uWait) {
  unsigned short ucFade;
  unsigned char ucBcl;

  // Fade-out vers le noir
  if (ucScr & 0x01) REG_BLDCNT=ucBG;
  if (ucScr & 0x02) REG_BLDCNT_SUB=ucBG;
  if (ucSens == 1) {
    for(ucFade=0;ucFade<valEnd;ucFade++) {
      if (ucScr & 0x01) REG_BLDY=ucFade;
      if (ucScr & 0x02) REG_BLDY_SUB=ucFade;
      for (ucBcl=0;ucBcl<uWait;ucBcl++) {
        swiWaitForVBlank();
      }
    }
  }
  else {
    for(ucFade=16;ucFade>valEnd;ucFade--) {
      if (ucScr & 0x01) REG_BLDY=ucFade;
      if (ucScr & 0x02) REG_BLDY_SUB=ucFade;
      for (ucBcl=0;ucBcl<uWait;ucBcl++) {
        swiWaitForVBlank();
      }
    }
  }
}

// End of file
