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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fat.h>

#include "printf.h"
#include "DS99.h"
#include "CRC32.h"
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "DS99mngt.h"
#include "DS99_utils.h"
#include "disk.h"
#include "SAMS.h"

u32 file_crc __attribute__((section(".dtcm")))  = 0x00000000;  // Our global file CRC32 to uniquiely identify this game. For split files (C/D/G) it will be the CRC of the main file (C or G if no C)

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
    videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE  | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D_LAYOUT | DISPLAY_SPR_ACTIVE);
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
    
    // ------------------------------------------------------------
    // Reset the CPU and setup for all the different memory types
    // in our system. This gets the CPU ready to rock and roll!
    // ------------------------------------------------------------
    TMS9900_Reset();  
  
    // --------------------------------------------------------------------------------
    // We're now ready to load the actual binary files and place them into memory.
    // We always do a re-read of the TI99 console ROM and GROM as well as load
    // the user-selected file into memory. We setup for banking possibilities.
    //
    // This is a bit complicated as the game is likely to be in several file parts as follows:
    // xxxC.bin is a CPU ROM loaded at 6000h
    // xxxD.bin is a banked CPU ROM for 6000h
    // xxxG.bin is a GROM loaded into GROM memory space (also 6000h but in different GROM memory space)
    // xxx8.bin is a multi-bank non-inverted file load
    // xxx3.bin is a multi-bank inverted file load (sometimes xxx9.bin)  
    // --------------------------------------------------------------------------------
    FILE *infile;           // We use this to read the various files in our system
    u16 numCartBanks = 1;   // Number of CART banks (8K each)

    // ------------------------------------------------------------------
    // Read in the main 16-bit console ROM and place into our MemCPU[]
    // ------------------------------------------------------------------
    infile = fopen("/roms/bios/994aROM.bin", "rb");
    if (!infile) infile = fopen("/roms/ti99/994aROM.bin", "rb");
    if (!infile) infile = fopen("994aROM.bin", "rb");    
    if (infile)
    {
        fread(&MemCPU[0], 0x2000, 1, infile);
        fclose(infile);
    }

    // ---------------------------------------------------------------------------------
    // Read in the main console GROM and place into the first 24K of GROM memory space
    // ---------------------------------------------------------------------------------
    infile = fopen("/roms/bios/994aGROM.bin", "rb");
    if (!infile) infile = fopen("/roms/ti99/994aGROM.bin", "rb");
    if (!infile) infile = fopen("994aGROM.bin", "rb");
    if (infile)
    {
        fread(&MemGROM[0x0000], 0x6000, 1, infile);
        fclose(infile);
    }

    // -------------------------------------------
    // Read the TI Disk DSR into buffered memory
    // -------------------------------------------
    infile = fopen("/roms/bios/994aDISK.bin", "rb");
    if (!infile) infile = fopen("/roms/ti99/994aDISK.bin", "rb");
    if (!infile) infile = fopen("994aDISK.bin", "rb");
    if (infile)
    {
        fread(DiskDSR, 0x2000, 1, infile);
        fclose(infile);
    }
    else
    {
        memset(DiskDSR, 0xFF, 0x2000);
    }
    memcpy(DSR1, DiskDSR, 0x2000);

    // -----------------------------------------------------------------------------
    // We're going to be manipulating the filename a bit so copy it into a buffer
    // -----------------------------------------------------------------------------
    strcpy(tmpBuf, szGame);

    u8 fileType = toupper(tmpBuf[strlen(tmpBuf)-5]);

    // Look for the classic C/D/G "mixed mode" files... 
    if ((fileType == 'C') || (fileType == 'G') || (fileType == 'D'))
    {
        tms9900.bankMask = 0x003F;
        tmpBuf[strlen(tmpBuf)-5] = 'C';   // Try to find a 'C' file
        infile = fopen(tmpBuf, "rb");
        if (infile != NULL)
        {
            int numRead = fread(MemCART, 1, MAX_CART_SIZE, infile);     // Whole cart C memory as needed...
            fclose(infile);
            if (numRead <= 0x2000)   // If 8K... repeat
            {
                memcpy(MemCART+0x2000, MemCART, 0x2000);
                memcpy(MemCART+0x4000, MemCART, 0x2000);
                memcpy(MemCART+0x6000, MemCART, 0x2000);
                memcpy(MemCART+0x8000, MemCART, 0x2000);
                memcpy(MemCART+0xA000, MemCART, 0x2000);
                memcpy(MemCART+0xC000, MemCART, 0x2000);
                memcpy(MemCART+0xE000, MemCART, 0x2000);
                tms9900.bankMask = 0x0007;
            }
            else // More than 8K needs banking support
            {
                numCartBanks = (numRead / 0x2000) + ((numRead % 0x2000) ? 1:0); // If not multiple of 8K we need to add a bank...
                tms9900.bankMask = BankMasks[numCartBanks-1];
            }
        }

        tmpBuf[strlen(tmpBuf)-5] = 'D';   // Try to find a 'D' file
        infile = fopen(tmpBuf, "rb");
        if (infile != NULL)
        {
            tms9900.bankMask = 0x0001;                  // If there is a 'D' file, it's always only 2 banks
            fread(MemCART+0x2000, 0x2000, 1, infile);   // Read 'D' file but never more than 8K
            fclose(infile);
        }
        memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // First bank loaded into main memory

        // And see if there is a GROM file to go along with this load...
        tmpBuf[strlen(tmpBuf)-5] = 'G';
        infile = fopen(tmpBuf, "rb");
        if (infile != NULL)
        {
            fread(&MemGROM[0x6000], 0xA000, 1, infile); // We support up to 40K of GROM loaded at GROM address >6000
            fclose(infile);
        }
    }
    else if (fileType != '0') // Full Load - this is either going to be a non-inverted '8' file (very common) or the less common inverted type
    {
        infile = fopen(tmpBuf, "rb");
        int numRead = fread(MemCART, 1, MAX_CART_SIZE, infile);   // Whole cart memory as needed....
        fclose(infile);
        numCartBanks = (numRead / 0x2000) + ((numRead % 0x2000) ? 1:0);
        tms9900.bankMask = BankMasks[numCartBanks-1];
        
        if (numCartBanks > 1)
        {
            // If the image is inverted we need to swap 8K banks
            if ((fileType == '3') || (fileType == '9'))
            {
                for (u16 i=0; i<numCartBanks/2; i++)
                {
                    // Swap 8k bank...
                    memcpy(FastCartBuffer, MemCART + (i*0x2000), 0x2000);  
                    memcpy(MemCART+(i*0x2000), MemCART + ((numCartBanks-i-1)*0x2000), 0x2000);
                    memcpy(MemCART + ((numCartBanks-i-1)*0x2000), FastCartBuffer, 0x2000);
                }
            }
        }
        
        memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // First bank loaded into main memory
    }

    // Put the first 8K bank into fast memory - a bit of a speedup on access and there are plenty of games that only need this 8K
    memcpy(FastCartBuffer, MemCPU+0x6000, 0x2000);
    
    // --------------------------------------------------------
    // Look for a special '0' file that we will try to load 
    // into GROM bank 0,1,2 to replace System GROMs (e.g. SOB)
    // --------------------------------------------------------
    tmpBuf[strlen(tmpBuf)-5] = '0';
    infile = fopen(tmpBuf, "rb");
    if (infile != NULL)
    {
        fread(&MemGROM[0x0000], 0x6000, 1, infile);
        fclose(infile);
    }    
    
    // ------------------------------------------------------
    // Now handle some of the special machine types...
    // ------------------------------------------------------
    if (myConfig.cartType == CART_TYPE_SUPERCART)
    {
        for (u16 address = 0x6000; address < 0x8000; address++)
        {
            MemType[address>>4] = MF_RAM8; // Supercart maps ram into the cart slot
        }
        memset(MemCPU+0x6000, 0x00, 0x2000);
    }

    // ------------------------------------------------------------
    // Mini Memory maps RAM into the upper 4K of the cart slot...
    // ------------------------------------------------------------
    if (myConfig.cartType == CART_TYPE_MINIMEM)
    {
        for (u16 address = 0x7000; address < 0x8000; address++)
        {
            MemType[address>>4] = MF_RAM8; // Mini Memory maps ram into the upper half of the cart slot
        }
        memset(MemCPU+0x7000, 0x00, 0x1000);
    }

    // ----------------------------------------------------------------
    // MBX usually has 1K of RAM mapped in... plus odd bank switching.
    // ----------------------------------------------------------------
    if ((myConfig.cartType == CART_TYPE_MBX_NO_RAM) || (myConfig.cartType == CART_TYPE_MBX_WITH_RAM))
    {
        for (u16 address = 0x6000; address < 0x7000; address++)
        {
            MemType[address>>4] = MF_CART_NB;    // We'll do the banking manually for MBX carts
        }
        for (u16 address = 0x7000; address < 0x8000; address++)
        {
            MemType[address>>4] = MF_CART;    // We'll do the banking manually for MBX carts
        }
        
        if ((myConfig.cartType == CART_TYPE_MBX_WITH_RAM))
        {
            for (u16 address = 0x6C00; address < 0x7000; address++)
            {
                MemType[address>>4] = MF_RAM8; // MBX carts have 1K of memory... with the last word at >6ffe being the bank switch
                MemCPU[address] = 0x00;     // Clear out the RAM
            }
        }
        
        MemType[0x6ffe>>4] = MF_MBX;   // Special bank switching register... sits in the last 16-bit word of MBX RAM
        MemType[0x6fff>>4] = MF_MBX;   // Special bank switching register... sits in the last 16-bit word of MBX RAM
        WriteBankMBX(0);
    }
    
    // Ensure we are reading status byte
    readSpeech = SPEECH_SENTINAL_VAL;
    
    // -------------------------------------------------
    // SAMS support... 512K for DS and 1MB for DSi
    // -------------------------------------------------
    SAMS_Initialize();   // Make sure we have the memory... and map in SAMS if enabled globally

    // Ensure we're in the first bank...
    tms9900.cartBankPtr = FastCartBuffer;
    
    // Perform a standard system RESET
    ResetTI();
  
    // ---------------------------------------------------------------
    // Check if there are any .dsk files associated with this load...
    // This will allow quick mounting of various .dsk files into mem.
    // ---------------------------------------------------------------
    strcpy(tmpBuf, szGame);
    tmpBuf[strlen(tmpBuf)-3] = 'd';
    tmpBuf[strlen(tmpBuf)-2] = 's';
    tmpBuf[strlen(tmpBuf)-1] = 'k';
    for (u8 drivesel = DSK1; drivesel < MAX_DSKS; drivesel++)
    {
        tmpBuf[strlen(tmpBuf)-5] = '1' + drivesel;
        FILE *infile = fopen(tmpBuf, "rb");
        if (infile) // Does the .dsk file exist?
        {
            extern char currentDirDSKs[];
            fclose(infile);
            getcwd(currentDirDSKs, MAX_PATH);
            disk_mount(drivesel, currentDirDSKs, tmpBuf);
        }  
    }
    
    // --------------------------------------------------------------------
    // Now that we're loaded up in memory, we set the initial CPU pointers
    // to the reset vector and kick off the CPU so we're ready to emulate!
    // --------------------------------------------------------------------
    TMS9900_Kickoff();

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
  for (uBcl=0;uBcl<16;uBcl++) 
  {
    r = (u8) ((float) TMS9918A_palette[uBcl*3+0]*0.121568f);
    g = (u8) ((float) TMS9918A_palette[uBcl*3+1]*0.121568f);
    b = (u8) ((float) TMS9918A_palette[uBcl*3+2]*0.121568f);

    SPRITE_PALETTE[uBcl] = RGB15(r,g,b);
    BG_PALETTE[uBcl] = RGB15(r,g,b);
  }
}


// --------------------------------------------------------------------------
// Update the screen for the current cycle. On the DSi this will generally
// be called right after swiWaitForVBlank() in TMS9918a.c which will help
// reduce visual tearing and other artifacts. It's not strictly necessary
// and that does slow down the loop a bit... but DSi can handle it.
// --------------------------------------------------------------------------
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


// --------------------------------------------------------------------------
// Compute the file CRC - this will be our unique identifier for the game
// for saving HI SCORES and Configuration / Key Mapping data.
// --------------------------------------------------------------------------
void getfile_crc(const char *path)
{
    file_crc = getFileCrc(path);        // The CRC is used as a unique ID to save out High Scores and Configuration...
}


// --------------------------------------------------------------------------------
// The main CPU loop... here we run one scanline of CPU instructions and then go 
// check in with the VDP video chip to see if we are done rendering a frame... 
// --------------------------------------------------------------------------------
ITCM_CODE u32 LoopTMS9900() 
{
    // Run one scanline worth of CPU instructions
    TMS9900_Run();
    
    // Refresh VDP for this scanline
    if(Loop9918()) 
    {
        TMS9901_RaiseVDPInterrupt();
    }
    
    // Drop out unless end of screen is reached 
    return ((CurLine == tms_end_line) ? 0:1);
}

// End of file
