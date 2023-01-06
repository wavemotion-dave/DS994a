// =====================================================================================
// Copyright (c) 2021 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, it's source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave (Phoenix-Edition),
// Alekmaul (original port) and Marat Fayzullin (ColEM core) are thanked profusely.
//
// The ColecoDS emulator is offered as-is, without any warranty.
// =====================================================================================
#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fat.h>
#include <dirent.h>

#include "DS99.h"
#include "CRC32.h"
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "DS99mngt.h"
#include "DS99_utils.h"
#define NORAM 0xFF

#define TI_SAVE_VER   0x0002        // Change this if the basic format of the .SAV file changes. Invalidates older .sav files.

/*********************************************************************************
 * Save the current state - save everything we need to a single .sav file.
 ********************************************************************************/
u8  spare[512] = {0x00};    // We keep some spare bytes so we can use them in the future without changing the structure
static char szFile[160];
static char szCh1[33];
void colecoSaveState() 
{
  u32 uNbO;
  long pSvg;
    
  // Init filename = romname and SAV in place of ROM
  DIR* dir = opendir("sav");
  if (dir) closedir(dir);  // Directory exists... close it out and move on.
  else mkdir("sav", 0777);   // Otherwise create the directory...
  siprintf(szFile,"sav/%s", gpFic[ucGameAct].szName);

  int len = strlen(szFile);
  if (szFile[len-3] == '.') // In case of .sg or .sc
  {
      szFile[len-2] = 's';
      szFile[len-1] = 'a';
      szFile[len-0] = 'v';
      szFile[len+1] = 0;
  }
  else
  {
      szFile[len-3] = 's';
      szFile[len-2] = 'a';
      szFile[len-1] = 'v';
  }
  strcpy(szCh1,"SAVING...");
  AffChaine(6,0,0,szCh1);
  
  FILE *handle = fopen(szFile, "wb+");  
  if (handle != NULL) 
  {
    // Write Version
    u16 save_ver = TI_SAVE_VER;
    uNbO = fwrite(&save_ver, sizeof(u16), 1, handle);
      
    // Write TMS9900 CPU
    uNbO = fwrite(&InterruptFlag,       sizeof(InterruptFlag),       1, handle);
    uNbO = fwrite(&WorkspacePtr,        sizeof(WorkspacePtr),        1, handle);
    uNbO = fwrite(&Status,              sizeof(Status),              1, handle);
    uNbO = fwrite(&ClockCycleCounter,   sizeof(ClockCycleCounter),   1, handle);
    uNbO = fwrite(&fetchPtr,            sizeof(fetchPtr),            1, handle);
    uNbO = fwrite(&curOpCode,           sizeof(curOpCode),           1, handle);
    uNbO = fwrite(&bankOffset,          sizeof(bankOffset),          1, handle);
    uNbO = fwrite(&m_GromWriteShift,    sizeof(m_GromWriteShift),    1, handle);
    uNbO = fwrite(&m_GromReadShift,     sizeof(m_GromReadShift),     1, handle);
    uNbO = fwrite(&gromAddress,         sizeof(gromAddress),         1, handle);
    uNbO = fwrite(&bCPUIdleRequest,     sizeof(bCPUIdleRequest),     1, handle);
      
    uNbO = fwrite(&m_TimerActive,       sizeof(m_TimerActive),       1, handle);
    uNbO = fwrite(&m_ReadRegister,      sizeof(m_ReadRegister),      1, handle);
    uNbO = fwrite(&m_Decrementer,       sizeof(m_Decrementer),       1, handle);
    uNbO = fwrite(&m_ClockRegister,     sizeof(m_ClockRegister),     1, handle);
    uNbO = fwrite(&m_PinState,          sizeof(m_PinState),          1, handle);
    uNbO = fwrite(&m_InterruptRequested,sizeof(m_InterruptRequested),1, handle);
    uNbO = fwrite(&m_ActiveInterrupts,  sizeof(m_ActiveInterrupts),  1, handle);
    uNbO = fwrite(&m_LastDelta,         sizeof(m_LastDelta),         1, handle);
    uNbO = fwrite(&m_DecrementClock,    sizeof(m_DecrementClock),    1, handle);
    uNbO = fwrite(&m_CapsLock,          sizeof(m_CapsLock),          1, handle);
    uNbO = fwrite(&m_ColumnSelect,      sizeof(m_ColumnSelect),      1, handle);
    uNbO = fwrite(&m_HideShift,         sizeof(m_HideShift),         1, handle);
     
    // Save TI Memory (yes, all of it!)
    if (uNbO) uNbO = fwrite(Memory, 0x10000,1, handle); 
      
    // A few frame counters
    if (uNbO) uNbO = fwrite(&emuActFrames, sizeof(emuActFrames), 1, handle); 
    if (uNbO) uNbO = fwrite(&timingFrames, sizeof(timingFrames), 1, handle); 
      
    // Write VDP
    if (uNbO) uNbO = fwrite(VDP, sizeof(VDP),1, handle); 
    if (uNbO) uNbO = fwrite(&VDPCtrlLatch, sizeof(VDPCtrlLatch),1, handle); 
    if (uNbO) uNbO = fwrite(&VDPStatus, sizeof(VDPStatus),1, handle); 
    if (uNbO) uNbO = fwrite(&FGColor, sizeof(FGColor),1, handle); 
    if (uNbO) uNbO = fwrite(&BGColor, sizeof(BGColor),1, handle); 
    if (uNbO) uNbO = fwrite(&OH, sizeof(OH),1, handle); 
    if (uNbO) uNbO = fwrite(&IH, sizeof(IH),1, handle);       
    if (uNbO) uNbO = fwrite(&ScrMode, sizeof(ScrMode),1, handle); 
    if (uNbO) uNbO = fwrite(&VDPDlatch, sizeof(VDPDlatch),1, handle); 
    if (uNbO) uNbO = fwrite(&VAddr, sizeof(VAddr),1, handle); 
    if (uNbO) uNbO = fwrite(&CurLine, sizeof(CurLine),1, handle); 
    if (uNbO) uNbO = fwrite(&ColTabM, sizeof(ColTabM),1, handle); 
    if (uNbO) uNbO = fwrite(&ChrGenM, sizeof(ChrGenM),1, handle); 
    if (uNbO) uNbO = fwrite(pVDPVidMem, 0x4000,1, handle); 
    pSvg = ChrGen-pVDPVidMem;
    if (uNbO) uNbO = fwrite(&pSvg, sizeof(pSvg),1, handle); 
    pSvg = ChrTab-pVDPVidMem;
    if (uNbO) uNbO = fwrite(&pSvg, sizeof(pSvg),1, handle); 
    pSvg = ColTab-pVDPVidMem;
    if (uNbO) uNbO = fwrite(&pSvg, sizeof(pSvg),1, handle); 
    pSvg = SprGen-pVDPVidMem;
    if (uNbO) uNbO = fwrite(&pSvg, sizeof(pSvg),1, handle); 
    pSvg = SprTab-pVDPVidMem;
    if (uNbO) uNbO = fwrite(&pSvg, sizeof(pSvg),1, handle); 

    // Write PSG sound chips...
    if (uNbO) uNbO = fwrite(&sncol, sizeof(sncol),1, handle); 
      
    // Some spare memory we can eat into...
    if (uNbO) uNbO = fwrite(&spare, 512,1, handle); 

    fclose(handle);
      
    if (uNbO) 
      strcpy(szCh1,"OK ");
    else
      strcpy(szCh1,"ERR");
     AffChaine(15,0,0,szCh1);
    WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
    AffChaine(6,0,0,"             "); 
    DisplayStatusLine(true);
  }
  else 
  {
    strcpy(szCh1,"Error opening SAV file ...");
  }
}


/*********************************************************************************
 * Load the current state - read everything back from the .sav file.
 ********************************************************************************/
void colecoLoadState() 
{
    u32 uNbO;
    long pSvg;

    // Init filename = romname and .SAV in place of ROM
    siprintf(szFile,"sav/%s", gpFic[ucGameAct].szName);
    int len = strlen(szFile);
    if (szFile[len-3] == '.') // In case of .sg or .sc
    {
      szFile[len-2] = 's';
      szFile[len-1] = 'a';
      szFile[len-0] = 'v';
      szFile[len+1] = 0;
    }
    else
    {
      szFile[len-3] = 's';
      szFile[len-2] = 'a';
      szFile[len-1] = 'v';
    }
    FILE* handle = fopen(szFile, "rb"); 
    if (handle != NULL) 
    {    
         strcpy(szCh1,"LOADING...");
         AffChaine(6,0,0,szCh1);
       
        // Read Version
        u16 save_ver = 0xBEEF;
        uNbO = fread(&save_ver, sizeof(u16), 1, handle);
        
        if (save_ver == TI_SAVE_VER)
        {
            // Load TMS9900 CPU
            if (uNbO) uNbO = fread(&InterruptFlag,       sizeof(InterruptFlag),       1, handle);
            if (uNbO) uNbO = fread(&WorkspacePtr,        sizeof(WorkspacePtr),        1, handle);
            if (uNbO) uNbO = fread(&Status,              sizeof(Status),              1, handle);
            if (uNbO) uNbO = fread(&ClockCycleCounter,   sizeof(ClockCycleCounter),   1, handle);
            if (uNbO) uNbO = fread(&fetchPtr,            sizeof(fetchPtr),            1, handle);
            if (uNbO) uNbO = fread(&curOpCode,           sizeof(curOpCode),           1, handle);
            if (uNbO) uNbO = fread(&bankOffset,          sizeof(bankOffset),          1, handle);
            if (uNbO) uNbO = fread(&m_GromWriteShift,    sizeof(m_GromWriteShift),    1, handle);
            if (uNbO) uNbO = fread(&m_GromReadShift,     sizeof(m_GromReadShift),     1, handle);
            if (uNbO) uNbO = fread(&gromAddress,         sizeof(gromAddress),         1, handle);
            if (uNbO) uNbO = fread(&bCPUIdleRequest,     sizeof(bCPUIdleRequest),     1, handle);
            
            if (uNbO) uNbO = fread(&m_TimerActive,       sizeof(m_TimerActive),       1, handle);
            if (uNbO) uNbO = fread(&m_ReadRegister,      sizeof(m_ReadRegister),      1, handle);
            if (uNbO) uNbO = fread(&m_Decrementer,       sizeof(m_Decrementer),       1, handle);
            if (uNbO) uNbO = fread(&m_ClockRegister,     sizeof(m_ClockRegister),     1, handle);
            if (uNbO) uNbO = fread(&m_PinState,          sizeof(m_PinState),          1, handle);
            if (uNbO) uNbO = fread(&m_InterruptRequested,sizeof(m_InterruptRequested),1, handle);
            if (uNbO) uNbO = fread(&m_ActiveInterrupts,  sizeof(m_ActiveInterrupts),  1, handle);
            if (uNbO) uNbO = fread(&m_LastDelta,         sizeof(m_LastDelta),         1, handle);
            if (uNbO) uNbO = fread(&m_DecrementClock,    sizeof(m_DecrementClock),    1, handle);
            if (uNbO) uNbO = fread(&m_CapsLock,          sizeof(m_CapsLock),          1, handle);
            if (uNbO) uNbO = fread(&m_ColumnSelect,      sizeof(m_ColumnSelect),      1, handle);
            if (uNbO) uNbO = fread(&m_HideShift,         sizeof(m_HideShift),         1, handle);
            
            // Load TI Memory (yes, all of it!)
            if (uNbO) uNbO = fread(Memory, 0x10000,1, handle); 
            
            // A few frame counters
            if (uNbO) uNbO = fread(&emuActFrames, sizeof(emuActFrames), 1, handle); 
            if (uNbO) uNbO = fread(&timingFrames, sizeof(timingFrames), 1, handle); 
            
            // Load VDP
            if (uNbO) uNbO = fread(VDP, sizeof(VDP),1, handle); 
            if (uNbO) uNbO = fread(&VDPCtrlLatch, sizeof(VDPCtrlLatch),1, handle); 
            if (uNbO) uNbO = fread(&VDPStatus, sizeof(VDPStatus),1, handle); 
            if (uNbO) uNbO = fread(&FGColor, sizeof(FGColor),1, handle); 
            if (uNbO) uNbO = fread(&BGColor, sizeof(BGColor),1, handle); 
            if (uNbO) uNbO = fread(&OH, sizeof(OH),1, handle); 
            if (uNbO) uNbO = fread(&IH, sizeof(IH),1, handle); 
            if (uNbO) uNbO = fread(&ScrMode, sizeof(ScrMode),1, handle); 
            extern void (*RefreshLine)(u8 uY);  RefreshLine = SCR[ScrMode].Refresh;
            if (uNbO) uNbO = fread(&VDPDlatch, sizeof(VDPDlatch),1, handle); 
            if (uNbO) uNbO = fread(&VAddr, sizeof(VAddr),1, handle); 
            if (uNbO) uNbO = fread(&CurLine, sizeof(CurLine),1, handle); 
            if (uNbO) uNbO = fread(&ColTabM, sizeof(ColTabM),1, handle); 
            if (uNbO) uNbO = fread(&ChrGenM, sizeof(ChrGenM),1, handle); 
            
            if (uNbO) uNbO = fread(pVDPVidMem, 0x4000,1, handle); 
            if (uNbO) uNbO = fread(&pSvg, sizeof(pSvg),1, handle); 
            ChrGen = pSvg + pVDPVidMem;
            if (uNbO) uNbO = fread(&pSvg, sizeof(pSvg),1, handle); 
            ChrTab = pSvg + pVDPVidMem;
            if (uNbO) uNbO = fread(&pSvg, sizeof(pSvg),1, handle); 
            ColTab = pSvg + pVDPVidMem;
            if (uNbO) uNbO = fread(&pSvg, sizeof(pSvg),1, handle); 
            SprGen = pSvg + pVDPVidMem;
            if (uNbO) uNbO = fread(&pSvg, sizeof(pSvg),1, handle); 
            SprTab = pSvg + pVDPVidMem;
            
            // Load PSG Sound Stuff
            if (uNbO) uNbO = fread(&sncol, sizeof(sncol),1, handle); 
            
            // Load spare memory for future use
            if (uNbO) uNbO = fread(&spare, 512,1, handle); 
            
            // Fix up transparency
            if (BGColor)
            {
              u8 r = (u8) ((float) TMS9918A_palette[BGColor*3+0]*0.121568f);
              u8 g = (u8) ((float) TMS9918A_palette[BGColor*3+1]*0.121568f);
              u8 b = (u8) ((float) TMS9918A_palette[BGColor*3+2]*0.121568f);
              BG_PALETTE[0] = RGB15(r,g,b);
            }
            else
            {
               BG_PALETTE[0] = RGB15(0x00,0x00,0x00);
            }
        }
        else uNbO = 0;
        
        if (uNbO) 
          strcpy(szCh1,"OK ");
        else
          strcpy(szCh1,"ERR");
         AffChaine(15,0,0,szCh1);
        
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        AffChaine(6,0,0,"             ");  
        DisplayStatusLine(true);
      }
      else
      {
        AffChaine(6,0,0,"NO SAVED GAME");
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        AffChaine(6,0,0,"             ");  
      }

    fclose(handle);
}

// End of file
