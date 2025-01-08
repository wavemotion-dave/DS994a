// =====================================================================================
// Copyright (c) 2023-2025 Dave Bernazzani (wavemotion-dave)
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
#include "rpk/rpk.h"
#include "DS99mngt.h"
#include "DS99_utils.h"
#include "disk.h"
#include "pcode.h"
#include "SAMS.h"
#include "speech.h"

u32 file_crc __attribute__((section(".dtcm")))  = 0x00000000;  // Our global file CRC32 to uniquiely identify this game. For split files (C/D/G) it will be the CRC of the main file (C or G if no C)

// ---------------------------------------------------------------------------
// Setup the main DS video modes. As usual, the top screen is primary and
// where we map the main emulation of the TI99/4a. The bottom screen is for
// the keyboard and various status texts. We need all of VRAM_A and VRAM_C
// but we can utilize the other VRAM banks as extra 16-bit CPU mapped memory.
// ---------------------------------------------------------------------------
void DS_SetVideoModes(void)
{
    u8 uBcl;
    u16 uVide;

    // ------------------------------------------------
    // Change graphic mode to initiate emulation.
    // The main (top) screen only needs the VRAM_A
    // and we set VRAM_B as memory-mapped for CPU use.
    // ------------------------------------------------
    videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D_LAYOUT | DISPLAY_SPR_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);      // This is our top emulation screen (where the game is played)

    REG_BG3CNT = BG_BMP8_256x256;
    REG_BG3PA = (1<<8);
    REG_BG3PB = 0;
    REG_BG3PC = 0;
    REG_BG3PD = (1<<8);
    REG_BG3X = 0;
    REG_BG3Y = 0;

    // ---------------------------------------------------------------------------------
    // Init the page flipping buffer... The uBCL/12 below produces a stripped pattern
    // which shows briefly while the emulation loads. You can change this to 0x0000
    // instead which will just show a black background. I debated between the two...
    // ---------------------------------------------------------------------------------
    for (uBcl=0;uBcl<192;uBcl++)
    {
        uVide=(uBcl/12);
        dmaFillWords(uVide | (uVide<<16),pVidFlipBuf+uBcl*128,256);
    }
}

// -----------------------------------------------------------------------------
// Init TI99 Engine for that game. The filename of the game is passed in to
// this function and we will read the binary ROM(s) into memory.
// -----------------------------------------------------------------------------
u8 TI99Init(char *szGame, u8 bInitDisks)
{
    // ------------------------------------------------------------------
    // Set the DS video modes and initialize the page-flipping buffer...
    // ------------------------------------------------------------------
    DS_SetVideoModes();

    // ------------------------------------------------------------
    // Reset the CPU and setup for all the different memory types
    // in our system. This gets the CPU ready to rock and roll!
    // ------------------------------------------------------------
    TMS9900_Reset();

    // ---------------------------------------------------------------------
    // Perform a standard system RESET - this will also clear disk buffers
    // if the bInitDisks parameter is set TRUE (otherwise, soft reset)
    // ---------------------------------------------------------------------
    ResetTI(bInitDisks);

    // ------------------------------------------------------------------------------------------------
    // We're now ready to load the actual binary files and place them into memory.
    // We always do a re-read of the TI99 console ROM and GROM as well as load
    // the user-selected file into memory. We setup for banking possibilities.
    //
    // This is a bit complicated as the game is likely to be in several file parts as follows:
    // xxxC.bin is a CPU ROM loaded at 6000h
    // xxxD.bin is a banked CPU ROM for 6000h
    // xxxG.bin is a GROM loaded into GROM memory space (also 6000h but in different GROM memory space)
    // xxx8.bin is a multi-bank non-inverted file load
    // xxx9.bin is a multi-bank inverted file load (sometimes files will use '3' here but that's been deprecated by the community)
    // xxx0.bin is a System GROM replacement that will be mapped at 0000h in GROM memory space
    // ------------------------------------------------------------------------------------------------
    FILE *infile;           // We use this to read the various files in our system
    u16 numCartBanks = 1;   // Number of CART banks (8K each)
    pCodeEmulation = 0;     // Default to no p-code card emulation

    // ------------------------------------------------------------------
    // Grab the main 16-bit console ROM and place into our MemCPU[]
    // ------------------------------------------------------------------
    memcpy(&MemCPU[0], MAIN_BIOS, 0x2000);

    // ------------------------------------------------------------------
    // Grab the system console GROM and place into our MemGROM[]
    // ------------------------------------------------------------------
    memcpy(&MemGROM[0], MAIN_GROM, 0x6000);

    // --------------------------------------------------------------------------
    // Note: DSRs are not mapped in by default (such as the TI Disk Controller).
    //       Those will get 'turned on' by CRU writes as needed by the system.
    // --------------------------------------------------------------------------

    // ------------------------------------------------------------
    // First check if we are trying to load a ROM Pack (.rpk) file
    // ------------------------------------------------------------
    if (strcasecmp(strrchr(szGame, '.'), ".rpk") == 0)
    {
        DS_Print(7,0,6, "DECOMPRESSING RPK...");
        u8 retval = rpk_load(szGame); // TODO check retval
        if (retval != 0) // Any errors?
        {
            memset(&MemCPU[0x6000], 0xFF, 0x2000);
            DS_Print(7,0,6, "ERROR LOADING RPK!!!");
            WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
            WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        }
        DS_Print(7,0,6, "                    ");
    }
    else // Otherwise, assume mixed-mode (c/d/g/8/9 etc)
    {
        // -----------------------------------------------------------------------------
        // We're going to be manipulating the filename a bit so copy it into a buffer
        // -----------------------------------------------------------------------------
        strcpy(tmpBuf, szGame);
        
        // --------------------------------------------------
        // First check if this is the special 'p-code' card?
        // This card has a DSR that essentially takes over 
        // the whole system - it must be handled uniquely.
        // --------------------------------------------------
        if ((strstr(szGame, "pcode_c.bin") != 0) || (strstr(szGame, "PCODE_C.BIN") != 0))
        {
            tmpBuf[strlen(tmpBuf)-5] = 'C';   // Load the 'c' file 
            infile = fopen(tmpBuf, "rb");
            if (infile != NULL)
            {
                fread(MemCART, 1, MAX_CART_SIZE, infile);     // Read the P-Code ROM into the cart space (it's actually a DSR but we'll keep it here for now)
                fclose(infile);

                tmpBuf[strlen(tmpBuf)-5] = 'G';   // Load the 'g' file
                infile = fopen(tmpBuf, "rb");
                if (infile != NULL)
                {
                    fread(MemCART+(64*1024), 1, (64*1024), infile);     // Read the P-Code GROM into the cart space... special handling here
                    fclose(infile);
                }
            }
            pCodeEmulation = 1; // This will force special handling for the DSR space to allow mapping in the p-code DSR and make use of the special internal GROM
        }
        else // Not the special p-code card / system and so we just load normally
        {        
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
                    tms9900.bankMask = 0x0001;                  // If there is a 'D' file, it's always only 1 bank
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
                if (file_size >= (256 * 1024))  DS_Print(3,0,6, "LOADING ROM - PLEASE WAIT...");

                infile = fopen(tmpBuf, "rb");
                int numRead = fread(MemCART, 1, MAX_CART_SIZE, infile);   // Whole cart memory as needed....
                fclose(infile);
                numCartBanks = (numRead / 0x2000) + ((numRead % 0x2000) ? 1:0);
                tms9900.bankMask = BankMasks[numCartBanks-1];

                if (numCartBanks > 1)
                {
                    // If the image is inverted we need to swap 8K banks
                    if ((fileType == '9') || (fileType == '3')) // '3' is deprecated but there are still cart names using it...
                    {
                        for (u16 i=0; i<numCartBanks/2; i++)
                        {
                            // Swap 8k bank...
                            memcpy(fileBuf, MemCART + (i*0x2000), 0x2000);
                            memcpy(MemCART+(i*0x2000), MemCART + ((numCartBanks-i-1)*0x2000), 0x2000);
                            memcpy(MemCART + ((numCartBanks-i-1)*0x2000), fileBuf, 0x2000);
                        }
                    }
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
        }

        // ------------------------------------------------------
        // Now handle some of the special machine types...
        // Supercart is 32K of RAM that is CRU-banked into >6000
        // ------------------------------------------------------
        if (myConfig.cartType == CART_TYPE_SUPERCART)
        {
            for (u16 address = 0x6000; address < 0x8000; address++)
            {
                MemType[address>>4] = MF_RAM8; // Supercart maps ram into the cart slot
            }
            memset(MemCPU+0x6000, 0x00, 0x2000); // Clear the Super Cart working RAM to start (located at cart-space >6000)
            memset(MemCART+(MAX_CART_SIZE - 0x8000), 0x00, 0x8000); // Clear the back-end 32K of the cartridge... we will repurpose to 32K of SUPER CART RAM
        }

        // --------------------------------------------------------------
        // Mini Memory maps RAM into the upper 4K of the cart slot...
        // Since there is no banking, we can just use the 4K of MemCPU[]
        // at that location.
        // --------------------------------------------------------------
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
    }

    // -----------------------------------------------------------------------------
    // Ensure we're in the first bank... (which might be the only bank for 8K ROMs)
    // -----------------------------------------------------------------------------
    tms9900.cartBankPtr = MemCPU+0x6000;

    // -----------------------------------------------------------------------
    // If bInitDisks parameter is TRUE, we look and load up any .dsk files 
    // associated with  this program / cart. Otherwise we are considering
    // this a 'soft reset' and leave the disks mounted (or unmounted) as-is...
    // -----------------------------------------------------------------------
    if (bInitDisks)
    {
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

        // -------------------------------------------------------------------
        // Alternate filename check for loaded disks... This one just checks
        // for the same filename with a '1' '2' or '3' tacked on to the end.
        // -------------------------------------------------------------------
        strcpy(tmpBuf, szGame);
        strcat(tmpBuf, "X");
        tmpBuf[strlen(tmpBuf)-4] = '.';
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
    }

    // --------------------------------------------------------------------
    // Now that we're loaded up in memory, we set the initial CPU pointers
    // to the reset vector and kick off the CPU so we're ready to emulate!
    // --------------------------------------------------------------------
    TMS9900_Kickoff();

    // Return with result
    return (0);
}

// -------------------------------------------------------------------------------
// Run the emulation - we start by showing the main menu screen (user picks game)
// -------------------------------------------------------------------------------
void TI99Run(void)
{
  showMainMenu();                       // Show the game-related screen
}

// -----------------------------------------------------------------------
// Set TI99/4a Palette We support only one NTSC pallete for simplicity.
// -----------------------------------------------------------------------
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
    DS_Print(1,5,6, "COMPUTING CRC - PLEASE WAIT...");
    file_crc = getFileCrc(path);        // The CRC is used as a unique ID to save out High Scores and Configuration...
    DS_Print(1,5,6, "                              ");
}


// --------------------------------------------------------------------------------
// The main CPU loop... here we run one scanline of CPU instructions and then go
// check in with the VDP video chip to see if we are done rendering a frame...
// --------------------------------------------------------------------------------
ITCM_CODE u32 LoopTMS9900()
{
    // -----------------------------------------------------------------
    // Run one scanline worth of CPU instructions.
    //
    // Accurate emulation is enabled if we see an IDLE instruction or
    // TIMER enabled or SAMS use as all these need special attention.
    // Most games don't need this handling and we save the precious DS
    // CPU cycles as this is 10-15% slower.
    // -----------------------------------------------------------------
    if (tms9900.accurateEmuFlags) TMS9900_RunAccurate();
    else TMS9900_Run();

    // Refresh VDP for this scanline
    if(Loop9918())
    {
        TMS9901_RaiseVDPInterrupt();
    }

    // Drop out unless end of screen is reached
    return ((CurLine == tms_end_line) ? 0:1);
}

// End of file
