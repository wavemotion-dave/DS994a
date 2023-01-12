// =====================================================================================
// Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, it's source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave (Phoenix-Edition),
// Alekmaul (original port) and Marat Fayzullin (ColEM core) are thanked profusely.
//
// The DS99 emulator is offered as-is, without any warranty.
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
#include "ecranBas.h"
#include "ecranBasSel.h"
#include "ecranHaut.h"

#include "soundbank.h"
#include "soundbank_bin.h"
#include "cpu/sn76496/SN76496.h"

u32 debug[32];

// -------------------------------------------------------------------------------------------
// All emulated systems have ROM, RAM and possibly BIOS or SRAM. So we create generic buffers 
// for all this here... these are sized big enough to handle the largest memory necessary
// to render games playable. There are a few MSX games that are larger than 512k but they 
// are mostly demos or foreign-language adventures... not enough interest to try to squeeze
// in a larger ROM buffer to include them - we are still trying to keep compatible with the
// smaller memory model of the original DS/DS-LITE.
//
// These memory buffers will be pointed to by the MemoryMap[] array. This array contains 8
// pointers that can break down the Z80 memory into 8k chunks.  For the few games that have
// a smaller than 8k boundary (e.g. Creativision uses a 2k BIOS) we can just stage/build
// up the memory into the RAM_Memory[] buffer and point into that as a single 64k layout.
// -------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------
// For machines that have a full keybaord, we use the Left and Right
// shoulder buttons on the NDS to emulate the SHIFT and CTRL keys...
// --------------------------------------------------------------------------
u8 key_shift __attribute__((section(".dtcm"))) = false;
u8 key_ctrl  __attribute__((section(".dtcm"))) = false;

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
// For the various BIOS files ... only the coleco.rom is required - everything else is optional.
// -----------------------------------------------------------------------------------------------
u8 bTIBIOSFound      = false;
u8 bTIDISKFound      = false;

u8 soundEmuPause     __attribute__((section(".dtcm"))) = 1;       // Set to 1 to pause (mute) sound, 0 is sound unmuted (sound channels active)

u8 kbd_key           __attribute__((section(".dtcm"))) = 0;       // 0 if no key pressed, othewise the ASCII key (e.g. 'A', 'B', '3', etc)
u16 nds_key          __attribute__((section(".dtcm"))) = 0;       // 0 if no key pressed, othewise the NDS keys from keysCurrent() or similar

u8 bStartSoundEngine = false;  // Set to true to unmute sound after 1 frame of rendering...
int bg0, bg1, bg0b, bg1b;      // Some vars for NDS background screen handling
volatile u16 vusCptVBL = 0;    // We use this as a basic timer for the Mario sprite... could be removed if another timer can be utilized
u8 last_pal_mode = 99;
UINT8 tmpBuf[40];

// The DS/DSi has 12 keys that can be mapped
u16 NDS_keyMap[12] __attribute__((section(".dtcm"))) = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_START, KEY_SELECT};

char *myDskFile = NULL;

// --------------------------------------------------------------------
// The key map for the Colecovision... mapped into the NDS controller
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
// sounds about right for the Colecovision.
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
  //  The SN sound chip is for normal colecovision sound handling
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
        else AffChaine(12,0,6, "          ");
    }
    else if (driveReadCounter)
    {
        if (--driveReadCounter) AffChaine(12,0,6, "DISK READ ");
        else AffChaine(12,0,6, "          ");
    }
}

// ------------------------------------------------------------------------
// Swap in a new .cas Cassette/Tape - reset position counter to zero.
// ------------------------------------------------------------------------
void DiskMount(char *filename)
{
    FILE *infile = fopen(filename, "rb");
    if (infile)
    {
        fread(DiskImage, (180*1024), 1, infile);
        fclose(infile);
        bDiskIsMounted = true;
        AccurateEmulationFlags |= EMU_DISK;
    }
    else
    {
        memset(DiskImage, 0xFF, (180*1024));
        bDiskIsMounted = false;
        AccurateEmulationFlags &= ~EMU_DISK;
    }
}

void DiskUnmount(void)
{
    memset(DiskImage, 0xFF, (180*1024));
    bDiskIsMounted = false;
    AccurateEmulationFlags &= ~EMU_DISK;
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
      // ---------------------------------------------------    
      // Put up a generic background for this mini-menu...
      // ---------------------------------------------------    
      dmaCopy((void*) bgGetMapPtr(bg0b)+30*32*2,(void*) bgGetMapPtr(bg0b),32*24*2);
      unsigned short dmaVal = *(bgGetMapPtr(bg0b)+24*32); 
      dmaFillWords(dmaVal | (dmaVal<<16),(void*) bgGetMapPtr(bg1b)+5*32*2,32*19*2);
      swiWaitForVBlank();
    }
    
    AffChaine(8,7,6,                                                 " TI DISK MENU  ");
    AffChaine(8,9+cassete_menu_items,(sel==cassete_menu_items)?2:0,  " MOUNT   DISK  ");  cassete_menu_items++;
    AffChaine(8,9+cassete_menu_items,(sel==cassete_menu_items)?2:0,  " UNMOUNT DISK  ");  cassete_menu_items++;
    AffChaine(8,9+cassete_menu_items,(sel==cassete_menu_items)?2:0,  " SAVE    DISK  ");  cassete_menu_items++;
    AffChaine(8,9+cassete_menu_items,(sel==cassete_menu_items)?2:0,  " EXIT    MENU  ");  cassete_menu_items++;

    if (bDiskIsMounted) AffChaine(8,9+cassete_menu_items+2,(sel==cassete_menu_items)?2:0," DISK IS MOUNTED   ");
    else AffChaine(8,9+cassete_menu_items+2,(sel==cassete_menu_items)?2:0," DISK IS NOT MOUNTED");   
    
    if (bDiskIsMounted && (myDskFile != NULL))
    {
        UINT16 numSectors = (DiskImage[0x0A] << 8) | DiskImage[0x0B];
        siprintf(tmpBuf, " DISK IS %s/%s %3dKB", (DiskImage[0x12] == 2 ? "DS":"SS"), (DiskImage[0x13] == 2 ? "DD":"SD"), (numSectors*256)/1024);
        AffChaine(8,9+cassete_menu_items+3,(sel==cassete_menu_items)?2:0,tmpBuf);
        
        UINT8 col=0;
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
                if  (showMessage("DO YOU REALLY WANT TO","WRITE .DSK DATA?") == ID_SHM_YES) 
                {
                    {
                        showMessage("SORRY - .DSK WRITE IS","NOT YET SUPPORTED...");
                    }
                }
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
            if (menuSelection == 5)
            {
                  break;
            }
            if (menuSelection == 6)
            {
                  break;
            }
            if (menuSelection == 7)
            {
                  break;
            }
            if (menuSelection == 8)
            {
                  break;
            }
            if (menuSelection == 9)
            {
                break;
            }
            if (menuSelection == 10)
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

// ------------------------------------------------------------------------
// The main emulation loop is here... call into the Z80, VDP and PSG 
// ------------------------------------------------------------------------
ITCM_CODE void ds99_main(void) 
{
  u16 iTx,  iTy;
  u8 ResetNow  = 0, SaveNow = 0, LoadNow = 0;

  // Returns when  user has asked for a game to run...
  showMainMenu();
    
  // Get the Coleco Machine Emualtor ready
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
  // Stay in this loop running the Coleco game until the user exits...
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
                if (emuFps == 61) emuFps=60;
                else if (emuFps == 59) emuFps=60;            
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
        // We only support NTSC 60 frames... there are PAL colecovisions
        // but the games really don't adjust well and so we stick to basics.
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
        while(TIMER2_DATA < ((myConfig.isPAL ? 656:546)*(timingFrames+1)))
        {
            if (myConfig.showFPS == 2) break;   // If Full Speed, break out...
        }
        
      // ------------------------------------------
      // Handle any screen touch events
      // ------------------------------------------
      kbd_key = 0;

      // Clear out the Joystick and Keyboard table - we'll check for keys below
      memset(m_Joystick, 0x00, sizeof(m_Joystick));
      memset(m_StateTable, 0x00, sizeof(m_StateTable));
        
      m_StateTable[VK_CAPSLOCK] = myConfig.capsLock;        
        
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
          LoadNow = 0;


        // --------------------------------------------------------------------------
        // Test the touchscreen rendering of the ADAM/MSX/SVI full keybaord
        // --------------------------------------------------------------------------
        if ((iTy >= 28) && (iTy < 56))        // Row 1 (top row)
        {
            if      ((iTx >= 1)   && (iTx < 34))   {m_StateTable[VK_1]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 34)  && (iTx < 65))   {m_StateTable[VK_2]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 65)  && (iTx < 96))   {m_StateTable[VK_3]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 96)  && (iTx < 127))  {m_StateTable[VK_4]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 127) && (iTx < 158))  {m_StateTable[VK_5]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 158) && (iTx < 189))  {m_StateTable[VK_6]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 189) && (iTx < 220))  {m_StateTable[VK_7]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 220) && (iTx < 255))  {m_StateTable[VK_8]=1; if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 56) && (iTy < 84))   // Row 2
        {
            if      ((iTx >= 1)   && (iTx < 34))   {m_StateTable[VK_9]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 34)  && (iTx < 65))   {m_StateTable[VK_0]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 65)  && (iTx < 96))   {m_StateTable[VK_A]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 96)  && (iTx < 127))  {m_StateTable[VK_B]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 127) && (iTx < 158))  {m_StateTable[VK_C]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 158) && (iTx < 189))  {m_StateTable[VK_D]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 189) && (iTx < 220))  {m_StateTable[VK_E]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 220) && (iTx < 255))  {m_StateTable[VK_F]=1; if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 84) && (iTy < 112))  // Row 3
        {
            if      ((iTx >= 1)   && (iTx < 34))   {m_StateTable[VK_G]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 34)  && (iTx < 65))   {m_StateTable[VK_H]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 65)  && (iTx < 96))   {m_StateTable[VK_I]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 96)  && (iTx < 127))  {m_StateTable[VK_J]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 127) && (iTx < 158))  {m_StateTable[VK_K]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 158) && (iTx < 189))  {m_StateTable[VK_L]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 189) && (iTx < 220))  {m_StateTable[VK_M]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 220) && (iTx < 255))  {m_StateTable[VK_N]=1; if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 112) && (iTy < 140))  // Row 4
        {
            if      ((iTx >= 1)   && (iTx < 34))   {m_StateTable[VK_O]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 34)  && (iTx < 65))   {m_StateTable[VK_P]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 65)  && (iTx < 96))   {m_StateTable[VK_Q]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 96)  && (iTx < 127))  {m_StateTable[VK_R]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 127) && (iTx < 158))  {m_StateTable[VK_S]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 158) && (iTx < 189))  {m_StateTable[VK_T]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 189) && (iTx < 220))  {m_StateTable[VK_U]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 220) && (iTx < 255))  {m_StateTable[VK_V]=1; if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 140) && (iTy < 169))  // Row 5
        {
            if      ((iTx >= 1)   && (iTx < 34))   {m_StateTable[VK_W]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 34)  && (iTx < 65))   {m_StateTable[VK_X]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 65)  && (iTx < 96))   {m_StateTable[VK_Y]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 96)  && (iTx < 127))  {m_StateTable[VK_Z]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 127) && (iTx < 158))  {m_StateTable[VK_PERIOD]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 158) && (iTx < 189))  {m_StateTable[VK_6]=1; m_StateTable[VK_FCTN]=1; if (!bKeyClick) bKeyClick=1;} //PRO'C
            else if ((iTx >= 189) && (iTx < 220))  {m_StateTable[VK_8]=1; m_StateTable[VK_FCTN]=1; if (!bKeyClick) bKeyClick=1;} //REDO
            else if ((iTx >= 220) && (iTx < 255))  {m_StateTable[VK_9]=1; m_StateTable[VK_FCTN]=1; if (!bKeyClick) bKeyClick=1;} //BACK
        }
        else if ((iTy >= 169) && (iTy < 192))  // Row 6
        {
            if      ((iTx >= 1)   && (iTx < 35))   CassetteMenu();
            else if ((iTx >= 174) && (iTx < 214))  {m_StateTable[VK_SPACE]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 214) && (iTx < 256))  {m_StateTable[VK_ENTER]=1; if (!bKeyClick) bKeyClick=1;}
        }

        if (bKeyClick == 1)
        {
            mmEffect(SFX_KEYCLICK);  // Play short key click for feedback...
            bKeyClick = 2;
        }
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
                      case JOY1_UP:         m_Joystick[0].y_Axis = +1;    break;
                      case JOY1_DOWN:       m_Joystick[0].y_Axis = -1;    break;
                      case JOY1_LEFT:       m_Joystick[0].x_Axis = -1;    break;
                      case JOY1_RIGHT:      m_Joystick[0].x_Axis = +1;    break;
                      case JOY1_FIRE:       m_Joystick[0].isPressed = 1;  break;
                          
                      case JOY2_UP:         m_Joystick[1].y_Axis = +1;    break;
                      case JOY2_DOWN:       m_Joystick[1].y_Axis = -1;    break;
                      case JOY2_LEFT:       m_Joystick[1].x_Axis = -1;    break;
                      case JOY2_RIGHT:      m_Joystick[1].x_Axis = +1;    break;
                      case JOY2_FIRE:       m_Joystick[1].isPressed = 1;  break;

                      case KBD_A:           m_StateTable[VK_A]=1;         break;
                      case KBD_B:           m_StateTable[VK_B]=1;         break;
                      case KBD_C:           m_StateTable[VK_C]=1;         break;
                      case KBD_D:           m_StateTable[VK_D]=1;         break;
                      case KBD_E:           m_StateTable[VK_E]=1;         break;
                      case KBD_F:           m_StateTable[VK_F]=1;         break;
                      case KBD_G:           m_StateTable[VK_G]=1;         break;
                      case KBD_H:           m_StateTable[VK_H]=1;         break;
                      case KBD_I:           m_StateTable[VK_I]=1;         break;
                      case KBD_J:           m_StateTable[VK_J]=1;         break;
                      case KBD_K:           m_StateTable[VK_K]=1;         break;
                      case KBD_L:           m_StateTable[VK_L]=1;         break;
                      case KBD_M:           m_StateTable[VK_M]=1;         break;
                      case KBD_N:           m_StateTable[VK_N]=1;         break;
                      case KBD_O:           m_StateTable[VK_O]=1;         break;
                      case KBD_P:           m_StateTable[VK_P]=1;         break;
                      case KBD_Q:           m_StateTable[VK_Q]=1;         break;
                      case KBD_R:           m_StateTable[VK_R]=1;         break;
                      case KBD_S:           m_StateTable[VK_S]=1;         break;
                      case KBD_T:           m_StateTable[VK_T]=1;         break;
                      case KBD_U:           m_StateTable[VK_U]=1;         break;
                      case KBD_V:           m_StateTable[VK_V]=1;         break;
                      case KBD_W:           m_StateTable[VK_W]=1;         break;
                      case KBD_X:           m_StateTable[VK_X]=1;         break;
                      case KBD_Y:           m_StateTable[VK_Y]=1;         break;
                      case KBD_Z:           m_StateTable[VK_Z]=1;         break;

                      case KBD_1:           m_StateTable[VK_1]=1;         break;
                      case KBD_2:           m_StateTable[VK_2]=1;         break;
                      case KBD_3:           m_StateTable[VK_3]=1;         break;
                      case KBD_4:           m_StateTable[VK_4]=1;         break;
                      case KBD_5:           m_StateTable[VK_5]=1;         break;
                      case KBD_6:           m_StateTable[VK_6]=1;         break;
                      case KBD_7:           m_StateTable[VK_7]=1;         break;
                      case KBD_8:           m_StateTable[VK_8]=1;         break;
                      case KBD_9:           m_StateTable[VK_9]=1;         break;
                      case KBD_0:           m_StateTable[VK_0]=1;         break;
                          
                      case KBD_SPACE:       m_StateTable[VK_SPACE]=1;     break;
                      case KBD_ENTER:       m_StateTable[VK_ENTER]=1;     break;

                      case KBD_FNCT:        m_StateTable[VK_FCTN]=1;      break;
                      case KBD_CTRL:        m_StateTable[VK_CTRL]=1;      break;
                      case KBD_SHIFT:       m_StateTable[VK_SHIFT]=1;     break;
                          
                      case KBD_PLUS:        m_StateTable[VK_EQUALS]=1;  m_StateTable[VK_SHIFT]=1;   break;
                      case KBD_MINUS:       m_StateTable[VK_DIVIDE]=1;  m_StateTable[VK_SHIFT]=1;   break;
                      case KBD_PROC:        m_StateTable[VK_6]=1;       m_StateTable[VK_FCTN]=1;    break;
                      case KBD_REDO:        m_StateTable[VK_8]=1;       m_StateTable[VK_FCTN]=1;    break;
                      case KBD_BACK:        m_StateTable[VK_9]=1;       m_StateTable[VK_FCTN]=1;    break;
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
void colecoDSInit(void) 
{
  //  Init graphic mode (bitmap mode)
  videoSetMode(MODE_0_2D  | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D_LAYOUT | DISPLAY_SPR_ACTIVE);
  videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE  | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D_LAYOUT | DISPLAY_SPR_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG);
  vramSetBankB(VRAM_B_MAIN_SPRITE);          // Once emulation of game starts, we steal this back for an additional 128K of VRAM at 0x6820000
  vramSetBankC(VRAM_C_SUB_BG);
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
  decompress(ecranBasSelTiles,  bgGetGfxPtr(bg0b), LZ77Vram);
  decompress(ecranBasSelMap,  (void*) bgGetMapPtr(bg0b), LZ77Vram);
  dmaCopy((void*) ecranBasSelPal,(void*)  BG_PALETTE_SUB,256*2);
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
void colecoDSInitCPU(void) 
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
// Look for the coleco.rom bios in several possible locations...
// ----------------------------------------------------------------
void LoadBIOSFiles(void)
{
    // --------------------------------------------------
    // We will look for all 3 BIOS files here but only 
    // the Colecovision coleco.rom is critical.
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
    colecoDSInit();

    // ---------------------------------------------------------------
    // Let the user know what BIOS files were found - the only BIOS 
    // that must exist is coleco.rom or else the show is off...
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
        while(1) ;  // We're done... Need a coleco bios to run a CV emulator
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
      colecoDSInitCPU();
      ds99_main();
    }
  }
  return(0);
}


// End of file

