// =====================================================================================
// Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The TI99DS emulator is offered as-is, without any warranty.
// =====================================================================================
#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fat.h>

#include "DS99.h"
#include "CRC32.h"
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "DS99mngt.h"
#include "DS99_utils.h"
#define NORAM 0xFF


u32 file_crc __attribute__((section(".dtcm")))  = 0x00000000;  // Our global file CRC32 to uniquiely identify this game

// -----------------------------------------------------------
// The master sound chip for the TI99
// -----------------------------------------------------------
SN76496 sncol   __attribute__((section(".dtcm")));


/*********************************************************************************
 * Init TI99 Engine for that game
 ********************************************************************************/
u8 TI99Init(char *szGame) 
{
  u8 uBcl;
  u16 uVide;
   
  // -----------------------------------------------------------------
  // Change graphic mode to initiate emulation.
  // Here we can claim back 128K of VRAM which is otherwise unused
  // but we can use it for fast memory swaps and look-up-tables.
  // -----------------------------------------------------------------
  videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG_0x06000000);      // This is our top emulation screen (where the game is played)
  vramSetBankB(VRAM_B_LCD);                     // 128K of Video Memory mapped at 0x06820000 we can use
  
  REG_BG3CNT = BG_BMP8_256x256;
  REG_BG3PA = (1<<8); 
  REG_BG3PB = 0;
  REG_BG3PC = 0;
  REG_BG3PD = (1<<8);
  REG_BG3X = 0;
  REG_BG3Y = 0;

  // Init the page flipping buffer...
  for (uBcl=0;uBcl<192;uBcl++) 
  {
     uVide=(uBcl/12);
     dmaFillWords(uVide | (uVide<<16),pVidFlipBuf+uBcl*128,256);
  }
    
  // Now load the game... this is a bit complicated as the game is likely to be in several file parts as follows:
  // xxxC.bin is a CPU ROM loaded at 6000h
  // xxxD.bin is a banked CPU ROM for 6000h
  // xxxG.bin is a GROM loaded into GROM memory space (also 6000h but in different GROM memory space)
  TMS9900_Reset(szGame);
    
  // Perform a standard system RESET
  ResetTI();
    
  // Return with result
  return (0);
}

/*********************************************************************************
 * Run the emul
 ********************************************************************************/
void TI99Run(void) 
{
  showMainMenu();                       // Show the game-related screen
}

/*********************************************************************************
 * Set TI99/4a Palette
 ********************************************************************************/
void TI99SetPal(void) 
{
  u8 uBcl,r,g,b;
  
  // -----------------------------------------------------------------------
  // The TI99/4a has a 16 color pallette... we set that up here.
  // We always use the standard NTSC color palette which is fine for now
  // but maybe in the future we add the PAL color palette for a bit more
  // authenticity.
  // -----------------------------------------------------------------------
  for (uBcl=0;uBcl<16;uBcl++) {
    r = (u8) ((float) TMS9918A_palette[uBcl*3+0]*0.121568f);
    g = (u8) ((float) TMS9918A_palette[uBcl*3+1]*0.121568f);
    b = (u8) ((float) TMS9918A_palette[uBcl*3+2]*0.121568f);

    SPRITE_PALETTE[uBcl] = RGB15(r,g,b);
    BG_PALETTE[uBcl] = RGB15(r,g,b);
  }
}


/*********************************************************************************
 * Update the screen for the current cycle. On the DSi this will generally
 * be called right after swiWaitForVBlank() in TMS9918a.c which will help
 * reduce visual tearing and other artifacts. It's not strictly necessary
 * and that does slow down the loop a bit... but DSi can handle it.
 ********************************************************************************/
ITCM_CODE void TI99UpdateScreen(void) 
{
    extern u16 timingFrames;
    // ------------------------------------------------------------   
    // If we are in 'blendMode' we will OR the last two frames. 
    // This helps on some games where things are just 1 pixel 
    // wide and the non XL/LL DSi will just not hold onto the
    // image long enough to render it properly for the eye to 
    // pick up. This takes CPU speed.
    // ------------------------------------------------------------   
    if (myConfig.frameBlend)
    {
      if (XBuf == XBuf_A)
      {
          XBuf = XBuf_B;
      }
      else
      {
          XBuf = XBuf_A;
      }
      u32 *p1 = (u32*)XBuf_A;
      u32 *p2 = (u32*)XBuf_B;
      u32 *destP = (u32*)pVidFlipBuf;
        
      if (timingFrames & 1) // Only need to do this every other frame...
      {
          for (u16 i=0; i<(256*192)/4; i++)
          {
              *destP++ = (*p1++ | *p2++);       // Simple OR blending of 2 frames...
          }
      }
    }
    else
    {
        // -----------------------------------------------------------------
        // Not blend mode... just blast it out via DMA as fast as we can...
        // -----------------------------------------------------------------
        dmaCopyWordsAsynch(2, (u32*)XBuf_A, (u32*)pVidFlipBuf, 256*192);
    }
}


/*******************************************************************************
 * Compute the file CRC - this will be our unique identifier for the game
 * for saving HI SCORES and Configuration / Key Mapping data.
 *******************************************************************************/
void getfile_crc(const char *path)
{
    file_crc = getFileCrc(path);        // The CRC is used as a unique ID to save out High Scores and Configuration...
}


ITCM_CODE u32 LoopTMS9900() 
{
    TMS9900_Run();
    
    // Refresh VDP 
    if(Loop9918()) 
    {
        TMS9901_RaiseVDPInterrupt();
    }
    
    // Drop out unless end of screen is reached 
    if (CurLine == tms_end_line)
    {
      return 0;
    }
    return 1;
}

// End of file
