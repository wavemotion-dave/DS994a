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
#include <nds/fifomessages.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fat.h>
#include <maxmod9.h>

#include "DS99.h"
#include "highscore.h"
#include "DS99_utils.h"
#include "DS99mngt.h"
#include "cpu/tms9918a/tms9918a.h"
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "disk.h"
#include "intro.h"
#include "alpha.h"
#include "options.h"
#include "ecranHaut.h"

#include "soundbank.h"
#include "soundbank_bin.h"
#include "cpu/sn76496/SN76496.h"

u32 debug[32];


// ------------------------------------------------------------------------------------------
// Various sound chips in the system. We emulate the SN and AY sound chips but both of 
// these really still use the underlying SN76496 sound chip driver for siplicity and speed.
// ------------------------------------------------------------------------------------------
extern SN76496 sncol;       // The SN sound chip is the main TI99/4a sound
       SN76496 snmute;      // We keep this handy as a simple way to mute the sound

// ---------------------------------------------------------------------------
// Some timing and frame rate comutations to keep the emulation on pace...
// ---------------------------------------------------------------------------
u16 emuFps          __attribute__((section(".dtcm"))) = 0;
u16 emuActFrames    __attribute__((section(".dtcm"))) = 0;
u16 timingFrames    __attribute__((section(".dtcm"))) = 0;
u8  bShowDebug      __attribute__((section(".dtcm"))) = 1;

// -----------------------------------------------------------------------------------------------
// For the various BIOS files ... only the TI BIOS Roms are required - everything else is optional.
// -----------------------------------------------------------------------------------------------
u8 bTIBIOSFound      = false;
u8 bTIDISKFound      = false;

u8 soundEmuPause     __attribute__((section(".dtcm"))) = 1;       // Set to 1 to pause (mute) sound, 0 is sound unmuted (sound channels active)

u16 nds_key          __attribute__((section(".dtcm"))) = 0;       // 0 if no key pressed, othewise the NDS keys from keysCurrent() or similar

u8 bStartSoundEngine = false;  // Set to true to unmute sound after 1 frame of rendering...
int bg0, bg1, bg0b, bg1b;      // Some vars for NDS background screen handling
volatile u16 vusCptVBL = 0;    // We use this as a basic timer for the Mario sprite... could be removed if another timer can be utilized
u8 last_pal_mode = 99;
u8 tmpBuf[40];

// The DS/DSi has 12 keys that can be mapped to virtually any TI key (joystick or keyboard)
u16 NDS_keyMap[12] __attribute__((section(".dtcm"))) = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_START, KEY_SELECT};

char *myDskFile = NULL; // This will point to .DSK file on disk or NULL if no disk is mounted

// --------------------------------------------------------------------
// The key map for the TI... mapped into the NDS controller
// --------------------------------------------------------------------
u8 keyCoresp[MAX_KEY_OPTIONS] __attribute__((section(".dtcm"))) = {
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
    KBD_1, KBD_2, KBD_3, KBD_4, KBD_5,
    KBD_6, KBD_7, KBD_8, KBD_9, KBD_0,
    KBD_A, KBD_B, KBD_C, KBD_D, KBD_E,
    KBD_F, KBD_G, KBD_H, KBD_I, KBD_J,
    KBD_K, KBD_L, KBD_M, KBD_N, KBD_O,
    KBD_P, KBD_Q, KBD_R, KBD_S, KBD_T,
    KBD_U, KBD_V, KBD_W, KBD_X, KBD_Y, KBD_Z,
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
    KBD_PLUS,
    KBD_MINUS
};


// ------------------------------------------------------------
// Utility function to show the background for the main menu
// ------------------------------------------------------------
void showMainMenu(void) 
{
  dmaCopy((void*) bgGetMapPtr(bg0b),(void*) bgGetMapPtr(bg1b),32*24*2);
}

// ------------------------------------------------------------
// Utility function to pause the sound... 
// ------------------------------------------------------------
void SoundPause(void)
{
    soundEmuPause = 1;
}

// ------------------------------------------------------------
// Utility function to un pause the sound... 
// ------------------------------------------------------------
void SoundUnPause(void)
{
    soundEmuPause = 0;
}

// --------------------------------------------------------------------------------------------
// MAXMOD streaming setup and handling...
// We were using the normal ARM7 sound core but it sounded "scratchy" and so with the help
// of FluBBa, we've swiched over to the maxmod sound core which performs much better.
// --------------------------------------------------------------------------------------------
#define sample_rate  27965      // To match the driver in sn76496 - this is good enough quality for the DS
#define buffer_size  (512+12)   // Enough buffer that we don't have to fill it too often

mm_ds_system sys  __attribute__((section(".dtcm")));
mm_stream myStream __attribute__((section(".dtcm")));
u16 mixbuf1[2048];      // When we have SN and AY sound we have to mix 3+3 channels

// -------------------------------------------------------------------------------------------
// maxmod will call this routine when the buffer is half-empty and requests that
// we fill the sound buffer with more samples. They will request 'len' samples and
// we will fill exactly that many. If the sound is paused, we fill with 'mute' samples.
// -------------------------------------------------------------------------------------------
u16 last_sample = 0;
ITCM_CODE mm_word OurSoundMixer(mm_word len, mm_addr dest, mm_stream_formats format)
{
    if (soundEmuPause)  // If paused, just "mix" in mute sound chip... all channels are OFF
    {
        u16 *p = (u16*)dest;
        for (int i=0; i<len*2; i++)
        {
           *p++ = last_sample;      // To prevent pops and clicks... just keep outputting the last sample
        }
    }
    else
    {
        sn76496Mixer(len*4, dest, &sncol);
        last_sample = ((u16*)dest)[len*2 - 1];
    }
    
    return  len;
}


// -------------------------------------------------------------------------------------------
// Setup the maxmod audio stream - this will be a 16-bit Stereo PCM output at 55KHz which
// sounds about right for the TI99.
// -------------------------------------------------------------------------------------------
void setupStream(void) 
{
  //----------------------------------------------------------------
  //  initialize maxmod with our small 3-effect soundbank
  //----------------------------------------------------------------
  mmInitDefaultMem((mm_addr)soundbank_bin);

  mmLoadEffect(SFX_CLICKNOQUIT);
  mmLoadEffect(SFX_KEYCLICK);
  mmLoadEffect(SFX_MUS_INTRO);

  //----------------------------------------------------------------
  //  open stream
  //----------------------------------------------------------------
  myStream.sampling_rate  = sample_rate;        // sampling rate =
  myStream.buffer_length  = buffer_size;        // buffer length =
  myStream.callback   = OurSoundMixer;          // set callback function
  myStream.format     = MM_STREAM_16BIT_STEREO; // format = stereo  16-bit
  myStream.timer      = MM_TIMER0;              // use hardware timer 0
  myStream.manual     = false;                  // use automatic filling
  mmStreamOpen( &myStream );

  //----------------------------------------------------------------
  //  when using 'automatic' filling, your callback will be triggered
  //  every time half of the wave buffer is processed.
  //
  //  so: 
  //  25000 (rate)
  //  ----- = ~21 Hz for a full pass, and ~42hz for half pass
  //  1200  (length)
  //----------------------------------------------------------------
  //  with 'manual' filling, you must call mmStreamUpdate
  //  periodically (and often enough to avoid buffer underruns)
  //----------------------------------------------------------------
}


// -----------------------------------------------------------------------
// We setup the sound chips - disabling all volumes to start.
// -----------------------------------------------------------------------
void dsInstallSoundEmuFIFO(void) 
{
  SoundPause();
    
  // ---------------------------------------------------------------------
  // We setup a mute channel to cut sound for pause
  // ---------------------------------------------------------------------
  sn76496Reset(1, &snmute);         // Reset the SN sound chip
    
  sn76496W(0x80 | 0x00,&snmute);    // Write new Frequency for Channel A
  sn76496W(0x00 | 0x00,&snmute);    // Write new Frequency for Channel A
  sn76496W(0x90 | 0x0F,&snmute);    // Write new Volume for Channel A
    
  sn76496W(0xA0 | 0x00,&snmute);    // Write new Frequency for Channel B
  sn76496W(0x00 | 0x00,&snmute);    // Write new Frequency for Channel B
  sn76496W(0xB0 | 0x0F,&snmute);    // Write new Volume for Channel B
    
  sn76496W(0xC0 | 0x00,&snmute);    // Write new Frequency for Channel C
  sn76496W(0x00 | 0x00,&snmute);    // Write new Frequency for Channel C
  sn76496W(0xD0 | 0x0F,&snmute);    // Write new Volume for Channel C

  sn76496W(0xFF,  &snmute);         // Disable Noise Channel
    
  sn76496Mixer(8, mixbuf1, &snmute);  // Do  an initial mix conversion to clear the output
      
  //  ------------------------------------------------------------------
  //  The SN sound chip is for normal TI99 sound handling
  //  ------------------------------------------------------------------
  sn76496Reset(1, &sncol);         // Reset the SN sound chip
    
  sn76496W(0x80 | 0x00,&sncol);    // Write new Frequency for Channel A
  sn76496W(0x00 | 0x00,&sncol);    // Write new Frequency for Channel A
  sn76496W(0x90 | 0x0F,&sncol);    // Write new Volume for Channel A
    
  sn76496W(0xA0 | 0x00,&sncol);    // Write new Frequency for Channel B
  sn76496W(0x00 | 0x00,&sncol);    // Write new Frequency for Channel B
  sn76496W(0xB0 | 0x0F,&sncol);    // Write new Volume for Channel B
    
  sn76496W(0xC0 | 0x00,&sncol);    // Write new Frequency for Channel C
  sn76496W(0x00 | 0x00,&sncol);    // Write new Frequency for Channel C
  sn76496W(0xD0 | 0x0F,&sncol);    // Write new Volume for Channel C

  sn76496W(0xFF,  &sncol);         // Disable Noise Channel
    
  sn76496Mixer(8, mixbuf1, &sncol);  // Do an initial mix conversion to clear the output

  setupStream();    // Setup maxmod stream...

  bStartSoundEngine = true; // Volume will 'unpause' after 1 frame in the main loop.
}

// --------------------------------------------------------------
// When we reset the machine, there are many small utility flags 
// for various expansion peripherals that must be reset.
// --------------------------------------------------------------
void ResetStatusFlags(void)
{
    last_pal_mode = 99;
}


// --------------------------------------------------------------
// When we first load a ROM/CASSETTE or when the user presses
// the RESET button on the touch-screen...
// --------------------------------------------------------------
void ResetTI(void)
{
  Reset9918();                          // Reset video chip
   
  sn76496Reset(1, &sncol);              // Reset the SN sound chip
  sn76496W(0x90 | 0x0F  ,&sncol);       //  Write new Volume for Channel A (off) 
  sn76496W(0xB0 | 0x0F  ,&sncol);       //  Write new Volume for Channel B (off)
  sn76496W(0xD0 | 0x0F  ,&sncol);       //  Write new Volume for Channel C (off)

  // -----------------------------------------------------------
  // Timer 1 is used to time frame-to-frame of actual emulation
  // -----------------------------------------------------------
  TIMER1_CR = 0;
  TIMER1_DATA=0;
  TIMER1_CR=TIMER_ENABLE  | TIMER_DIV_1024;
    
  // -----------------------------------------------------------
  // Timer 2 is used to time once per second events
  // -----------------------------------------------------------
  TIMER2_CR=0;
  TIMER2_DATA=0;
  TIMER2_CR=TIMER_ENABLE  | TIMER_DIV_1024;
  timingFrames  = 0;
  emuFps=0;
    
  XBuf = XBuf_A;                      // Set the initial screen ping-pong buffer to A
    
  ResetStatusFlags();   // Some static status flags for the UI mostly
    
  memset(debug, 0x00, sizeof(debug));
}

// ------------------------------------------------------------
// The status line shows the status of the Super Game Moudle,
// AY sound chip support and MegaCart support.  Game players
// probably don't care, but it's really helpful for devs.
// ------------------------------------------------------------
void DisplayStatusLine(bool bForce)
{
    if (bForce) last_pal_mode = 98;
    if (last_pal_mode != myConfig.isPAL)
    {
        last_pal_mode = myConfig.isPAL;
        AffChaine(29,0,6, myConfig.isPAL ? "PAL":"   ");
    }

    if (driveWriteCounter)
    {
        if (--driveWriteCounter) AffChaine(12,0,6, "DISK WRITE");
        else
        {
            if (myDskFile != NULL)
            {
                DiskSave(myDskFile);
            }
            AffChaine(12,0,6, "          ");
        }
    }
    else if (driveReadCounter)
    {
        if (--driveReadCounter) AffChaine(12,0,6, "DISK READ ");
        else AffChaine(12,0,6, "          ");
    }
}

// --------------------------------
// Swap in a new .DSK Disk Image
// --------------------------------
void DiskMount(char *filename)
{
    FILE *infile = fopen(filename, "rb");
    if (infile)
    {
        fread(DiskImage, (180*1024), 1, infile);
        fclose(infile);
        bDiskIsMounted = true;
    }
    else
    {
        memset(DiskImage, 0xFF, (180*1024));
        bDiskIsMounted = false;
    }
}

void DiskUnmount(void)
{
    memset(DiskImage, 0xFF, (180*1024));
    bDiskIsMounted = false;
}

void DiskSave(char *filename)
{
    FILE *outfile = fopen(filename, "wb");
    if (outfile)
    {
        u16 numSectors = (DiskImage[0x0A] << 8) | DiskImage[0x0B];
        u32 diskSize = (numSectors*256);
        fwrite(DiskImage, diskSize, 1, outfile);
        fclose(outfile);
    }
}


void ShowDiskListing(void)
{
    char fileName[12];
    
    // Clear the screen...
    for (u8 i=0; i<20; i++)
    {
        AffChaine(1,4+i,6, "                                ");
    }

    while (keysCurrent()) WAITVBL; // While any key is pressed...

    u8 idx=0;
    AffChaine(5,7,6,      "=== DISK CONTENTS ===");
    if (bDiskIsMounted && (myDskFile != NULL))
    {
        u16 numFiles = 0;
        u16 sectorPtr = 0;
        for (u16 i=0; i<256; i += 2)
        {
            sectorPtr = (DiskImage[256 + (i+0)] << 8) | (DiskImage[256 + (i+1)] << 0);
            if (sectorPtr == 0) break;
            if (++numFiles > 24) break;
            for (u8 j=0; j<10; j++) 
            {
                fileName[j] = DiskImage[(256*sectorPtr) + j];
            }
            fileName[10] = 0;
            AffChaine((numFiles<12) ? 5:18,9+((numFiles<12) ? numFiles:(numFiles-11)),0, fileName);
        }
    }
    else
    {
        idx++;idx++;
        AffChaine(9,9+idx,0,  "NO DISK MOUNTED"); idx++;
    }

    // Wait for any press and release...
    while (!keysCurrent()) WAITVBL; // While no key is pressed...
    WAITVBL;WAITVBL;
}

// ------------------------------------------------------------------------
// Show the Cassette Menu text - highlight the selected row.
// ------------------------------------------------------------------------
u8 cassete_menu_items = 0;
void CassetteMenuShow(bool bClearScreen, u8 sel)
{
    cassete_menu_items = 0;
    if (bClearScreen)
    {
        DrawCleanBackground();
    }
    
    AffChaine(8,7,6,                                                 " TI DISK MENU  ");
    AffChaine(8,9+cassete_menu_items,(sel==cassete_menu_items)?2:0,  " MOUNT   DISK  ");  cassete_menu_items++;
    AffChaine(8,9+cassete_menu_items,(sel==cassete_menu_items)?2:0,  " UNMOUNT DISK  ");  cassete_menu_items++;
    AffChaine(8,9+cassete_menu_items,(sel==cassete_menu_items)?2:0,  " LIST    DISK  ");  cassete_menu_items++;
    AffChaine(8,9+cassete_menu_items,(sel==cassete_menu_items)?2:0,  " EXIT    MENU  ");  cassete_menu_items++;

    if (bDiskIsMounted) AffChaine(8,9+cassete_menu_items+2,(sel==cassete_menu_items)?2:0," DISK IS MOUNTED   ");
    else AffChaine(8,9+cassete_menu_items+2,(sel==cassete_menu_items)?2:0," DISK IS NOT MOUNTED");   
    
    if (bDiskIsMounted && (myDskFile != NULL))
    {
        u16 numSectors = (DiskImage[0x0A] << 8) | DiskImage[0x0B];
        siprintf(tmpBuf, " DISK IS %s/%s %3dKB", (DiskImage[0x12] == 2 ? "DS":"SS"), (DiskImage[0x13] == 2 ? "DD":"SD"), (numSectors*256)/1024);
        AffChaine(8,9+cassete_menu_items+3,(sel==cassete_menu_items)?2:0,tmpBuf);
        
        u8 col=0;
        if (strlen(myDskFile) < 32) col=16-(strlen(myDskFile)/2);
        if (strlen(myDskFile) & 1) col--;
        AffChaine(col,9+cassete_menu_items+5,(sel==cassete_menu_items)?2:0,myDskFile);   
    }
}

// ------------------------------------------------------------------------
// Handle Cassette mini-menu interface...
// ------------------------------------------------------------------------
void CassetteMenu(void)
{
  u8 menuSelection = 0;
    
  SoundPause();
  while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_A ))!=0);

  CassetteMenuShow(true, menuSelection);

  while (true) 
  {
    nds_key = keysCurrent();
    if (nds_key)
    {
        if (nds_key & KEY_UP)  
        {
            menuSelection = (menuSelection > 0) ? (menuSelection-1):(cassete_menu_items-1);
            CassetteMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_DOWN)  
        {
            menuSelection = (menuSelection+1) % cassete_menu_items;
            CassetteMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_A)  
        {
            if (menuSelection == 0) // MOUNT .DSK FILE
            {
                myDskFile = TILoadDiskFile();
                if (myDskFile != NULL)
                {
                    DiskMount(myDskFile);
                    CassetteMenuShow(true, menuSelection);
                }
                else
                {
                    CassetteMenuShow(true, menuSelection);
                }
            }
            if (menuSelection == 1) // UNMOUNT .DSK FILE
            {
                DiskUnmount();
                CassetteMenuShow(true, menuSelection);
            }
            if (menuSelection == 2)
            {
                  ShowDiskListing();
                  CassetteMenuShow(true, menuSelection);
            }
            if (menuSelection == 3)
            {
                  break;
            }
            if (menuSelection == 4)
            {
                  break;
            }
        }
        if (nds_key & KEY_B)  
        {
            break;
        }
        
        while ((keysCurrent() & (KEY_UP | KEY_DOWN | KEY_A ))!=0);
        WAITVBL;WAITVBL;
    }
  }

  while ((keysCurrent() & (KEY_UP | KEY_DOWN | KEY_A ))!=0);
  WAITVBL;WAITVBL;
    
  InitBottomScreen();  // Could be generic or overlay...

  SoundUnPause();
}


// ------------------------------------------------------------------------
// Return 1 if we are showing full keyboard... otherwise 0
// ------------------------------------------------------------------------
inline u8 IsFullKeyboard(void) {return 1;}
u8 bKeyClick = 0;

u16 PAL_Timing[] = {656, 596, 546, 504};
u16 NTSC_Timing[] = {546, 496, 454, 420};


void CheckKeyboardInput(u16 iTy, u16 iTx)
{          
    // --------------------------------------------------------------------------
    // Test the touchscreen rendering of the keyboard
    // --------------------------------------------------------------------------
    if ((iTy >= 28) && (iTy < 56))        // Row 1 (top row)
    {
        if      ((iTx >= 1)   && (iTx < 34))   {tms9901.Keyboard[TMS_KEY_1]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 34)  && (iTx < 65))   {tms9901.Keyboard[TMS_KEY_2]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 65)  && (iTx < 96))   {tms9901.Keyboard[TMS_KEY_3]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 96)  && (iTx < 127))  {tms9901.Keyboard[TMS_KEY_4]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 127) && (iTx < 158))  {tms9901.Keyboard[TMS_KEY_5]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 158) && (iTx < 189))  {tms9901.Keyboard[TMS_KEY_6]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 189) && (iTx < 220))  {tms9901.Keyboard[TMS_KEY_7]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 220) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_8]=1; if (!bKeyClick) bKeyClick=1;}
    }
    else if ((iTy >= 56) && (iTy < 84))   // Row 2
    {
        if      ((iTx >= 1)   && (iTx < 34))   {tms9901.Keyboard[TMS_KEY_9]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 34)  && (iTx < 65))   {tms9901.Keyboard[TMS_KEY_0]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 65)  && (iTx < 96))   {tms9901.Keyboard[TMS_KEY_A]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 96)  && (iTx < 127))  {tms9901.Keyboard[TMS_KEY_B]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 127) && (iTx < 158))  {tms9901.Keyboard[TMS_KEY_C]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 158) && (iTx < 189))  {tms9901.Keyboard[TMS_KEY_D]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 189) && (iTx < 220))  {tms9901.Keyboard[TMS_KEY_E]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 220) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_F]=1; if (!bKeyClick) bKeyClick=1;}
    }
    else if ((iTy >= 84) && (iTy < 112))  // Row 3
    {
        if      ((iTx >= 1)   && (iTx < 34))   {tms9901.Keyboard[TMS_KEY_G]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 34)  && (iTx < 65))   {tms9901.Keyboard[TMS_KEY_H]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 65)  && (iTx < 96))   {tms9901.Keyboard[TMS_KEY_I]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 96)  && (iTx < 127))  {tms9901.Keyboard[TMS_KEY_J]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 127) && (iTx < 158))  {tms9901.Keyboard[TMS_KEY_K]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 158) && (iTx < 189))  {tms9901.Keyboard[TMS_KEY_L]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 189) && (iTx < 220))  {tms9901.Keyboard[TMS_KEY_M]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 220) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_N]=1; if (!bKeyClick) bKeyClick=1;}
    }
    else if ((iTy >= 112) && (iTy < 140))  // Row 4
    {
        if      ((iTx >= 1)   && (iTx < 34))   {tms9901.Keyboard[TMS_KEY_O]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 34)  && (iTx < 65))   {tms9901.Keyboard[TMS_KEY_P]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 65)  && (iTx < 96))   {tms9901.Keyboard[TMS_KEY_Q]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 96)  && (iTx < 127))  {tms9901.Keyboard[TMS_KEY_R]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 127) && (iTx < 158))  {tms9901.Keyboard[TMS_KEY_S]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 158) && (iTx < 189))  {tms9901.Keyboard[TMS_KEY_T]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 189) && (iTx < 220))  {tms9901.Keyboard[TMS_KEY_U]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 220) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_V]=1; if (!bKeyClick) bKeyClick=1;}
    }
    else if ((iTy >= 140) && (iTy < 169))  // Row 5
    {
        if      ((iTx >= 1)   && (iTx < 34))   {tms9901.Keyboard[TMS_KEY_W]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 34)  && (iTx < 65))   {tms9901.Keyboard[TMS_KEY_X]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 65)  && (iTx < 96))   {tms9901.Keyboard[TMS_KEY_Y]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 96)  && (iTx < 127))  {tms9901.Keyboard[TMS_KEY_Z]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 127) && (iTx < 158))  {tms9901.Keyboard[TMS_KEY_PERIOD]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 158) && (iTx < 189))  {tms9901.Keyboard[TMS_KEY_6]=1; tms9901.Keyboard[TMS_KEY_FUNCTION]=1; if (!bKeyClick) bKeyClick=1;} //PRO'C
        else if ((iTx >= 189) && (iTx < 220))  {tms9901.Keyboard[TMS_KEY_8]=1; tms9901.Keyboard[TMS_KEY_FUNCTION]=1; if (!bKeyClick) bKeyClick=1;} //REDO
        else if ((iTx >= 220) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_9]=1; tms9901.Keyboard[TMS_KEY_FUNCTION]=1; if (!bKeyClick) bKeyClick=1;} //BACK
    }
    else if ((iTy >= 169) && (iTy < 192))  // Row 6
    {
        if      ((iTx >= 1)   && (iTx < 35))   CassetteMenu();
        else if ((iTx >= 174) && (iTx < 214))  {tms9901.Keyboard[TMS_KEY_SPACE]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 214) && (iTx < 256))  {tms9901.Keyboard[TMS_KEY_ENTER]=1; if (!bKeyClick) bKeyClick=1;}
    }

    if (bKeyClick == 1)
    {
        mmEffect(SFX_KEYCLICK);  // Play short key click for feedback...
        bKeyClick = 2;
    }    
}

// ------------------------------------------------------------------------
// The main emulation loop is here... call into the Z80, VDP and PSG 
// ------------------------------------------------------------------------
ITCM_CODE void ds99_main(void) 
{
  u16 iTx,  iTy;
  u8 ResetNow  = 0, SaveNow = 0, LoadNow = 0;

  // Returns when  user has asked for a game to run...
  showMainMenu();
    
  // Get the TI99 Machine Emualtor ready
  TI99Init(gpFic[ucGameAct].szName);

  TI99SetPal();
  TI99Run();
  
  // Frame-to-frame timing...
  TIMER1_CR = 0;
  TIMER1_DATA=0;
  TIMER1_CR=TIMER_ENABLE  | TIMER_DIV_1024;

  // Once/second timing...
  TIMER2_CR=0;
  TIMER2_DATA=0;
  TIMER2_CR=TIMER_ENABLE  | TIMER_DIV_1024;
  timingFrames  = 0;
  emuFps=0;
    
  // Force the sound engine to turn on when we start emulation
  bStartSoundEngine = true;
    
  // -------------------------------------------------------------------
  // Stay in this loop running the TI99 game until the user exits...
  // -------------------------------------------------------------------
  while(1)  
  {
    // Take a tour of the TMS9900 and display the screen if necessary
    if (!LoopTMS9900()) 
    {   
        // If we've been asked to start the sound engine, rock-and-roll!
        if (bStartSoundEngine)
        {
              bStartSoundEngine = false;
              SoundUnPause();
        }
        
        // -------------------------------------------------------------
        // Stuff to do once/second such as FPS display and Debug Data
        // -------------------------------------------------------------
        if (TIMER1_DATA >= 32728)   //  1000MS (1 sec)
        {
            char szChai[33];
            
            TIMER1_CR = 0;
            TIMER1_DATA = 0;
            TIMER1_CR=TIMER_ENABLE | TIMER_DIV_1024;
            emuFps = emuActFrames;
            if (myConfig.showFPS)
            {
                // If not asked to run full-speed... adjust FPS so it's stable near 60
                if (myConfig.showFPS != 2)
                {
                    if (emuFps == 61) emuFps=60;
                    else if (emuFps == 59) emuFps=60;            
                }
                if (emuFps/100) szChai[0] = '0' + emuFps/100;
                else szChai[0] = ' ';
                szChai[1] = '0' + (emuFps%100) / 10;
                szChai[2] = '0' + (emuFps%100) % 10;
                szChai[3] = 0;
                AffChaine(0,0,6,szChai);
            }
            DisplayStatusLine(false);
            emuActFrames = 0;
            
            if (bShowDebug)
            {
                siprintf(szChai, "%u %u %u %u %u", (unsigned int)debug[0], (unsigned int)debug[1], (unsigned int)debug[2], (unsigned int)debug[3], (unsigned int)debug[4]); 
                AffChaine(5,0,6,szChai);
            }
        }
        emuActFrames++;

        // -------------------------------------------------------------------
        // Framing timing needs to handle both NTSC and PAL 
        // -------------------------------------------------------------------
        if (++timingFrames == (myConfig.isPAL ? 50:60))
        {
            TIMER2_CR=0;
            TIMER2_DATA=0;
            TIMER2_CR=TIMER_ENABLE | TIMER_DIV_1024;
            timingFrames = 0;
        }

        // --------------------------------------------
        // Time 1 frame... 546 ticks of Timer2
        // This is how we time frame-to frame
        // to keep the game running at 60FPS
        // --------------------------------------------
        while(TIMER2_DATA < ((myConfig.isPAL ? PAL_Timing[myConfig.emuSpeed] : NTSC_Timing[myConfig.emuSpeed])*(timingFrames+1)))
        {
            if (myConfig.showFPS == 2) break;   // If Full Speed, break out...
        }
        
      // Clear out the Joystick and Keyboard table - we'll check for keys below
      TMS9901_ClearJoyKeyData();
      
      tms9901.CapsLock = myConfig.capsLock; // Set the state of our Caps Lock
        
      if  (keysCurrent() & KEY_TOUCH)
      {
        touchPosition touch;
        touchRead(&touch);
        iTx = touch.px;
        iTy = touch.py;
          
        // Test if "End Game" selected
        if  (((iTx>=35) && (iTy>=169) && (iTx<70) && (iTy<192)))
        {
          //  Stop sound
          SoundPause();

          //  Ask for verification
          if  (showMessage("DO YOU REALLY WANT TO","QUIT THE CURRENT GAME ?") == ID_SHM_YES) 
          { 
              memset((u8*)0x6820000, 0x00, 0x20000);    // Reset VRAM to 0x00 to clear any potential display garbage on way out
              return;
          }
          showMainMenu();
          DisplayStatusLine(true);            
          SoundUnPause();
        }

        // Test if "High Score" selected
        if  (((iTx>=70) && (iTy>=169) && (iTx< 104) && (iTy<192)))
        {
          //  Stop sound
          SoundPause();
          highscore_display(file_crc);
          DisplayStatusLine(true);
          SoundUnPause();
        }

        // Test if "Save State" selected
        if  (((iTx>=104) && (iTy>=169) && (iTx< 139) && (iTy<192)))
        {
          if  (!SaveNow) 
          {
              // Stop sound
              SoundPause();
              if (IsFullKeyboard())
              {
                  if  (showMessage("DO YOU REALLY WANT TO","SAVE GAME STATE ?") == ID_SHM_YES) 
                  {                      
                    SaveNow = 1;
                    TI99SaveState();
                  }
              }
              else
              {
                    SaveNow = 1;
                    TI99SaveState();
              }
              SoundUnPause();
          }
        }
        else
          SaveNow = 0;

        // Test if "Load State" selected
        if  (((iTx>=139) && (iTy>=169) && (iTx<174) && (iTy<192)))
        {
          if  (!LoadNow) 
          {
              // Stop sound
              SoundPause();
              if (IsFullKeyboard())
              {
                  if  (showMessage("DO YOU REALLY WANT TO","LOAD GAME STATE ?") == ID_SHM_YES) 
                  {                      
                    LoadNow = 1;
                    TI99LoadState();
                  }
              }
              else
              {
                    LoadNow = 1;
                    TI99LoadState();
              }
              SoundUnPause();
          }
        }
        else
        {
          LoadNow = 0;
        }
        
        // ---------------------------------
        // Check the Keyboard for input...
        // ---------------------------------
        CheckKeyboardInput(iTy, iTx);
          
      } // No Screen Touch...
      else  
      {
        ResetNow=SaveNow=LoadNow = 0;
        bKeyClick = 0;
      }

      // ------------------------------------------------------------------------
      //  Test DS keypresses (ABXY, L/R) and map to corresponding TI99 keys
      // ------------------------------------------------------------------------
      nds_key  = keysCurrent();
       
      if ((nds_key & KEY_L) && (nds_key & KEY_R) && (nds_key & KEY_X)) 
      {
            lcdSwap();
            WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
      }
      else        
      if  (nds_key & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A | KEY_B | KEY_START | KEY_SELECT | KEY_R | KEY_L | KEY_X | KEY_Y)) 
      {
          // --------------------------------------------------------------------------------------------------
          // There are 12 NDS buttons (D-Pad, XYAB, L/R and Start+Select) - we allow mapping of any of these.
          // --------------------------------------------------------------------------------------------------
          for (u8 i=0; i<12; i++)
          {
              if (nds_key & NDS_keyMap[i])
              {
                  u8 map = keyCoresp[myConfig.keymap[i]];
                  switch(map)
                  {
                      case JOY1_UP:         tms9901.Keyboard[TMS_KEY_JOY1_UP]=1;    break;
                      case JOY1_DOWN:       tms9901.Keyboard[TMS_KEY_JOY1_DOWN]=1;  break;
                      case JOY1_LEFT:       tms9901.Keyboard[TMS_KEY_JOY1_LEFT]=1;  break;
                      case JOY1_RIGHT:      tms9901.Keyboard[TMS_KEY_JOY1_RIGHT]=1; break;
                      case JOY1_FIRE:       tms9901.Keyboard[TMS_KEY_JOY1_FIRE]=1;  break;
                          
                      case JOY2_UP:         tms9901.Keyboard[TMS_KEY_JOY2_UP]=1;    break;
                      case JOY2_DOWN:       tms9901.Keyboard[TMS_KEY_JOY2_DOWN]=1;  break;
                      case JOY2_LEFT:       tms9901.Keyboard[TMS_KEY_JOY2_LEFT]=1;  break;
                      case JOY2_RIGHT:      tms9901.Keyboard[TMS_KEY_JOY2_RIGHT]=1; break;
                      case JOY2_FIRE:       tms9901.Keyboard[TMS_KEY_JOY2_FIRE]=1;  break;

                      case KBD_A:           tms9901.Keyboard[TMS_KEY_A]=1;         break;
                      case KBD_B:           tms9901.Keyboard[TMS_KEY_B]=1;         break;
                      case KBD_C:           tms9901.Keyboard[TMS_KEY_C]=1;         break;
                      case KBD_D:           tms9901.Keyboard[TMS_KEY_D]=1;         break;
                      case KBD_E:           tms9901.Keyboard[TMS_KEY_E]=1;         break;
                      case KBD_F:           tms9901.Keyboard[TMS_KEY_F]=1;         break;
                      case KBD_G:           tms9901.Keyboard[TMS_KEY_G]=1;         break;
                      case KBD_H:           tms9901.Keyboard[TMS_KEY_H]=1;         break;
                      case KBD_I:           tms9901.Keyboard[TMS_KEY_I]=1;         break;
                      case KBD_J:           tms9901.Keyboard[TMS_KEY_J]=1;         break;
                      case KBD_K:           tms9901.Keyboard[TMS_KEY_K]=1;         break;
                      case KBD_L:           tms9901.Keyboard[TMS_KEY_L]=1;         break;
                      case KBD_M:           tms9901.Keyboard[TMS_KEY_M]=1;         break;
                      case KBD_N:           tms9901.Keyboard[TMS_KEY_N]=1;         break;
                      case KBD_O:           tms9901.Keyboard[TMS_KEY_O]=1;         break;
                      case KBD_P:           tms9901.Keyboard[TMS_KEY_P]=1;         break;
                      case KBD_Q:           tms9901.Keyboard[TMS_KEY_Q]=1;         break;
                      case KBD_R:           tms9901.Keyboard[TMS_KEY_R]=1;         break;
                      case KBD_S:           tms9901.Keyboard[TMS_KEY_S]=1;         break;
                      case KBD_T:           tms9901.Keyboard[TMS_KEY_T]=1;         break;
                      case KBD_U:           tms9901.Keyboard[TMS_KEY_U]=1;         break;
                      case KBD_V:           tms9901.Keyboard[TMS_KEY_V]=1;         break;
                      case KBD_W:           tms9901.Keyboard[TMS_KEY_W]=1;         break;
                      case KBD_X:           tms9901.Keyboard[TMS_KEY_X]=1;         break;
                      case KBD_Y:           tms9901.Keyboard[TMS_KEY_Y]=1;         break;
                      case KBD_Z:           tms9901.Keyboard[TMS_KEY_Z]=1;         break;

                      case KBD_1:           tms9901.Keyboard[TMS_KEY_1]=1;         break;
                      case KBD_2:           tms9901.Keyboard[TMS_KEY_2]=1;         break;
                      case KBD_3:           tms9901.Keyboard[TMS_KEY_3]=1;         break;
                      case KBD_4:           tms9901.Keyboard[TMS_KEY_4]=1;         break;
                      case KBD_5:           tms9901.Keyboard[TMS_KEY_5]=1;         break;
                      case KBD_6:           tms9901.Keyboard[TMS_KEY_6]=1;         break;
                      case KBD_7:           tms9901.Keyboard[TMS_KEY_7]=1;         break;
                      case KBD_8:           tms9901.Keyboard[TMS_KEY_8]=1;         break;
                      case KBD_9:           tms9901.Keyboard[TMS_KEY_9]=1;         break;
                      case KBD_0:           tms9901.Keyboard[TMS_KEY_0]=1;         break;
                          
                      case KBD_SPACE:       tms9901.Keyboard[TMS_KEY_SPACE]=1;     break;
                      case KBD_ENTER:       tms9901.Keyboard[TMS_KEY_ENTER]=1;     break;

                      case KBD_FNCT:        tms9901.Keyboard[TMS_KEY_FUNCTION]=1;  break;
                      case KBD_CTRL:        tms9901.Keyboard[TMS_KEY_CONTROL]=1;   break;
                      case KBD_SHIFT:       tms9901.Keyboard[TMS_KEY_SHIFT]=1;     break;
                          
                      case KBD_PLUS:        tms9901.Keyboard[TMS_KEY_EQUALS]=1;    tms9901.Keyboard[TMS_KEY_SHIFT]=1;    break;
                      case KBD_MINUS:       tms9901.Keyboard[TMS_KEY_DIV]=1;       tms9901.Keyboard[TMS_KEY_SHIFT]=1;    break;
                      case KBD_PROC:        tms9901.Keyboard[TMS_KEY_6]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                      case KBD_REDO:        tms9901.Keyboard[TMS_KEY_8]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                      case KBD_BACK:        tms9901.Keyboard[TMS_KEY_9]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                  }
              }
          }
      }     
    }
  }
}


/*********************************************************************************
 * Init DS Emulator - setup VRAM banks and background screen rendering banks
 ********************************************************************************/
void TI99DSInit(void) 
{
  //  Init graphic mode (bitmap mode)
  videoSetMode(MODE_0_2D  | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D_LAYOUT | DISPLAY_SPR_ACTIVE);
  videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE  | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D_LAYOUT | DISPLAY_SPR_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG);
  vramSetBankC(VRAM_C_SUB_BG);
  vramSetBankB(VRAM_B_LCD);                  // Not using this for video but 128K of faster RAM always useful! Mapped at 0x06820000 
  vramSetBankD(VRAM_D_LCD );                 // Not using this for video but 128K of faster RAM always useful! Mapped at 0x06860000 
  vramSetBankE(VRAM_E_LCD );                 // Not using this for video but 64K of faster RAM always useful!  Mapped at 0x06880000
  vramSetBankF(VRAM_F_LCD );                 // Not using this for video but 16K of faster RAM always useful!  Mapped at 0x06890000
  vramSetBankG(VRAM_G_LCD );                 // Not using this for video but 16K of faster RAM always useful!  Mapped at 0x06894000
  vramSetBankH(VRAM_H_LCD );                 // Not using this for video but 32K of faster RAM always useful!  Mapped at 0x06898000
  vramSetBankI(VRAM_I_LCD );                 // Not using this for video but 16K of faster RAM always useful!  Mapped at 0x068A0000

  //  Stop blending effect of intro
  REG_BLDCNT=0; REG_BLDCNT_SUB=0; REG_BLDY=0; REG_BLDY_SUB=0;
  
  //  Render the top screen
  bg0 = bgInit(0, BgType_Text8bpp,  BgSize_T_256x512, 31,0);
  bg1 = bgInit(1, BgType_Text8bpp,  BgSize_T_256x512, 29,0);
  bgSetPriority(bg0,1);bgSetPriority(bg1,0);
  decompress(ecranHautTiles,  bgGetGfxPtr(bg0), LZ77Vram);
  decompress(ecranHautMap,  (void*) bgGetMapPtr(bg0), LZ77Vram);
  dmaCopy((void*) ecranHautPal,(void*)  BG_PALETTE,256*2);
  unsigned  short dmaVal =*(bgGetMapPtr(bg0)+51*32);
  dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1),32*24*2);

  // Render the bottom screen for "options select" mode
  bg0b  = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x512, 31,0);
  bg1b  = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x512, 29,0);
  bgSetPriority(bg0b,1);bgSetPriority(bg1b,0);
  decompress(optionsTiles,  bgGetGfxPtr(bg0b), LZ77Vram);
  decompress(optionsMap,  (void*) bgGetMapPtr(bg0b), LZ77Vram);
  dmaCopy((void*) optionsPal,(void*)  BG_PALETTE_SUB,256*2);
  dmaVal  = *(bgGetMapPtr(bg0b)+24*32);
  dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
    
  //  Find the files
  TI99FindFiles();
}

// ---------------------------------------------------------------------------
// Setup the bottom screen - mostly for menu, high scores, options, etc.
// ---------------------------------------------------------------------------
void InitBottomScreen(void)
{
    //  Init bottom screen
    decompress(alphaTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
    decompress(alphaMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
    dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
    dmaCopy((void*) alphaPal,(void*) BG_PALETTE_SUB,256*2);
    
    unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
    dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
    
    DisplayStatusLine(true);
}

/*********************************************************************************
 * Init CPU for the current game
 ********************************************************************************/
void TI99DSInitCPU(void) 
{ 
  //  -----------------------------------------
  //  Init Main Memory and VDP Video Memory
  //  -----------------------------------------
  memset(pVDPVidMem, 0x00, 0x4000);

  // -----------------------------------------------
  // Init bottom screen do display correct overlay
  // -----------------------------------------------
  InitBottomScreen();
}

// -------------------------------------------------------------
// Only used for basic timing of moving the Mario sprite...
// -------------------------------------------------------------
void irqVBlank(void) 
{ 
 // Manage time
  vusCptVBL++;
}

// ----------------------------------------------------------------
// Look for the TI99 BIOS ROMs in several possible locations...
// ----------------------------------------------------------------
void LoadBIOSFiles(void)
{
    // --------------------------------------------------
    // We will look for the BIOS files here
    // --------------------------------------------------
    FILE *inFile1;
    FILE *inFile2;
    
    inFile1 = fopen("/roms/bios/994aROM.bin", "rb");
    inFile2 = fopen("/roms/bios/994aGROM.bin", "rb");
    if (inFile1 && inFile2)
    {
        bTIBIOSFound = true;
    }
    else
    {
        bTIBIOSFound = false;
    }
    fclose(inFile1);
    fclose(inFile2);
    
    inFile1 = fopen("/roms/bios/994aDISK.bin", "rb");
    if (inFile1) bTIDISKFound = true; else bTIDISKFound = false;
    fclose(inFile1);
    
}

/*********************************************************************************
 * Program entry point - check if an argument has been passed in probably from TWL++
 ********************************************************************************/
char initial_file[256];
int main(int argc, char **argv) 
{
  //  Init sound
  consoleDemoInit();
    
  if  (!fatInitDefault()) {
     iprintf("Unable to initialize libfat!\n");
     return -1;
  }
    
  highscore_init();

  lcdMainOnTop();
    
  //  Init timer for frame management
  TIMER2_DATA=0;
  TIMER2_CR=TIMER_ENABLE|TIMER_DIV_1024;  
  dsInstallSoundEmuFIFO();

  //  Show the fade-away intro logo...
  intro_logo();
  
  SetYtrigger(190); //trigger 2 lines before vsync    
    
  irqSet(IRQ_VBLANK,  irqVBlank);
  irqEnable(IRQ_VBLANK);
    
  // -----------------------------------------------------------------
  // Grab the BIOS before we try to switch any directories around...
  // -----------------------------------------------------------------
  LoadBIOSFiles();
    
  //  Handle command line argument... mostly for TWL++
  if  (argc > 1) 
  {
      //  We want to start in the directory where the file is being launched...
      if  (strchr(argv[1], '/') != NULL)
      {
          char  path[128];
          strcpy(path,  argv[1]);
          char  *ptr = &path[strlen(path)-1];
          while (*ptr !=  '/') ptr--;
          ptr++;  
          strcpy(initial_file,  ptr);
          *ptr=0;
          chdir(path);
      }
      else
      {
          strcpy(initial_file,  argv[1]);
      }
  }
  else
  {
      initial_file[0]=0; // No file passed on command line...
      chdir("/roms");    // Try to start in roms area... doesn't matter if it fails
      chdir("ti99");     // And try to start in the subdir /ti99... doesn't matter if it fails.
  }
    
  SoundPause();
  
  //  ------------------------------------------------------------
  //  We run this loop forever until game exit is selected...
  //  ------------------------------------------------------------
  while(1)  
  {
    TI99DSInit();

    // ---------------------------------------------------------------
    // Let the user know what BIOS files were found
    // ---------------------------------------------------------------
    if (bTIBIOSFound)
    {
        u8 idx = 6;
        AffChaine(2,idx++,0,"LOADING BIOS FILES ..."); idx++;
        if (bTIBIOSFound)          {AffChaine(2,idx++,0,"994aROM.bin   BIOS FOUND"); }
        if (bTIBIOSFound)          {AffChaine(2,idx++,0,"994aGROM.bin  GROM FOUND"); }
        if (bTIDISKFound)          {AffChaine(2,idx++,0,"994aDISK.bin  DSR  FOUND"); }
        idx++;
        AffChaine(2,idx++,0,"TOUCH SCREEN / KEY TO BEGIN"); idx++;
        
        while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_DOWN | KEY_UP | KEY_A | KEY_B | KEY_L | KEY_R))!=0);
        while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_DOWN | KEY_UP | KEY_A | KEY_B | KEY_L | KEY_R))==0);
        while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_DOWN | KEY_UP | KEY_A | KEY_B | KEY_L | KEY_R))!=0);
    }
    else
    {
        AffChaine(2,10,0,"ERROR: TI99 BIOS NOT FOUND");
        AffChaine(2,12,0,"ERROR: CANT RUN WITHOUT BIOS");
        AffChaine(3,12,0,"ERROR: SEE README FILE");
        while(1) ;  // We're done... Need a TI99 bios to run a CV emulator
    }
  
    while(1) 
    {
      SoundPause();
      //  Choose option
      if  (initial_file[0] != 0)
      {
          ucGameChoice=0;
          ucGameAct=0;
          strcpy(gpFic[ucGameAct].szName, initial_file);
          initial_file[0] = 0;    // No more initial file...
          ReadFileCRCAndConfig(); // Get CRC32 of the file and read the config/keys
      }
      else  
      {
          tiDSChangeOptions();
      }

      //  Run Machine
      TI99DSInitCPU();
      ds99_main();
    }
  }
  return(0);
}


// End of file

