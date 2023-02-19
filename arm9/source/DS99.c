// =====================================================================================
// Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)
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
#include <nds/fifomessages.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fat.h>
#include <maxmod9.h>

#include "printf.h"
#include "DS99.h"
#include "highscore.h"
#include "DS99_utils.h"
#include "DS99mngt.h"
#include "cpu/tms9918a/tms9918a.h"
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "disk.h"
#include "intro.h"
#include "ds99kbd.h"
#include "ti99kbd.h"
#include "options.h"
#include "splash.h"
#include "screenshot.h"
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

u8 alpha_lock       __attribute__((section(".dtcm"))) = 0;
u8 meta_next_key    __attribute__((section(".dtcm"))) = 0;
u8 handling_meta    __attribute__((section(".dtcm"))) = 0;

u8 bStartSoundEngine = false;  // Set to true to unmute sound after 1 frame of rendering...
int bg0, bg1, bg0b, bg1b;      // Some vars for NDS background screen handling
volatile u16 vusCptVBL = 0;    // We use this as a basic timer for the Mario sprite... could be removed if another timer can be utilized
u8 last_pal_mode = 99;         // So we show PAL properly in the upper right of the lower DS screen
u8 tmpBuf[40];                 // For simple printf-type output

u8 key_push_write = 0;
u8 key_push_read  = 0;
char key_push[0x20];
char dsk_filename[16];

u16 PAL_Timing[] = {656, 596, 546, 504};    // 100%, 110%, 120% and 130%
u16 NTSC_Timing[] = {546, 496, 454, 420};   // 100%, 110%, 120% and 130%

u8 disk_menu_items = 0;
u8 disk_drive_select  = 0; // Start with DSK1

// The DS/DSi has 12 keys that can be mapped to virtually any TI key (joystick or keyboard)
u16 NDS_keyMap[12] __attribute__((section(".dtcm"))) = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_START, KEY_SELECT};

char myDskFile[MAX_PATH]; // This will be filled in with the full filename (including .DSK) of the disk file
char myDskPath[MAX_PATH]; // This will point to the path where the .DSK file was loaded from (and will be written back to)s

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
  mmLoadEffect(SFX_PRESS_FIRE);
  mmLoadEffect(SFX_ADVANCING);
  mmLoadEffect(SFX_GOODSHOT);
  mmLoadEffect(SFX_ATTACKING);
  mmLoadEffect(SFX_ASTEROID);
  mmLoadEffect(SFX_DESTROYED);
  mmLoadEffect(SFX_BEWARE);
  mmLoadEffect(SFX_LOOKOUT);
  mmLoadEffect(SFX_WATCHOUT);
  mmLoadEffect(SFX_UH);
  mmLoadEffect(SFX_OOOOH);
  mmLoadEffect(SFX_YIKES);
  mmLoadEffect(SFX_OUCH);
  mmLoadEffect(SFX_OOPS);
  mmLoadEffect(SFX_MONSTERDAMAGEDSHIP);
  mmLoadEffect(SFX_LASEROVERHEAT);
  mmLoadEffect(SFX_UNKNOWNOBJECT);
  mmLoadEffect(SFX_ZYGAPPROACH);
  mmLoadEffect(SFX_CREWLOST);
  mmLoadEffect(SFX_ZYGNEVERGET);
  mmLoadEffect(SFX_ZYGHAHA);
  mmLoadEffect(SFX_WATERAHEAD);
  mmLoadEffect(SFX_MONSTERATTACKEDCREW);
  mmLoadEffect(SFX_MONSTERDESTROYED);
  mmLoadEffect(SFX_GOODSHOTCAPTAIN);
  mmLoadEffect(SFX_WAYTOGOCAP);

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


// ------------------------------------------------------------------------
// We setup the sound chips - disabling all volumes to start.
// The TI9919 sound chip is basically the same as the Colecovision
// SN76496 chip and we already have a driver for that one and will use it.
// ------------------------------------------------------------------------
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
// When we first load a ROM/DISK or when the user presses
// the RESET button on the touch-screen...
// --------------------------------------------------------------
void ResetTI(void)
{
  Reset9918();                          // Reset video chip
   
  sn76496Reset(1, &sncol);              // Reset the SN/TI sound chip
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
    
  XBuf = XBuf_A;            // Set the initial screen ping-pong buffer to A
    
  ResetStatusFlags();       // Some static status flags for the UI mostly
    
  alpha_lock = myConfig.capsLock;  
  meta_next_key    = 0;
  handling_meta    = 0;
    
  key_push_write = 0;
  key_push_read  = 0;
  strcpy(dsk_filename,"");    
    
  disk_init();
  disk_drive_select  = 0; // Start with DSK1
    
  memset(debug, 0x00, sizeof(debug));
}

void __attribute__ ((noinline))  DisplayStatusLine(bool bForce)
{
    static u8 bShiftKeysBlanked = 0;
    
    // ----------------------------------------------------------
    // Show PAL or blanks (if NTSC) on screen
    // ----------------------------------------------------------
    if (bForce) last_pal_mode = 98;
    if (last_pal_mode != myConfig.isPAL)
    {
        last_pal_mode = myConfig.isPAL;
        DS_Print(29,0,6, myConfig.isPAL ? "PAL":"   ");
    }

    // ----------------------------------------------------------
    // Show a status indication for Disk Read/Write on screen...
    // ----------------------------------------------------------
    for (u8 drive=0; drive<MAX_DSKS; drive++)
    {
        if (Disk[drive].driveWriteCounter)
        {
            Disk[drive].driveWriteCounter--;
            if (Disk[drive].driveWriteCounter) DS_Print(11,0,6, "DISK WRITE");
            else
            {
                // Persist the disk - write it back to the SD card
                disk_write_to_sd(drive);
                DS_Print(11,0,6, "          ");
            }
        }
        else if (Disk[drive].driveReadCounter)
        {
            Disk[drive].driveReadCounter--;
            if (Disk[drive].driveReadCounter) DS_Print(11,0,6, "DISK READ ");
            else DS_Print(11,0,6, "          ");
        }
    }

    // ------------------------------------------
    // Show the keyboard shift/function/control
    // ------------------------------------------
    if(tms9901.Keyboard[TMS_KEY_FUNCTION] == 1) 
    {
        DS_Print(0,0,6, "FCTN"); 
        bShiftKeysBlanked = 0;
    }
    else 
    {
        if(tms9901.Keyboard[TMS_KEY_SHIFT] == 1)
        {
            DS_Print(0,0,6, "SHFT");
            bShiftKeysBlanked = 0;
        }
        else 
        {
            if(tms9901.Keyboard[TMS_KEY_CONTROL] == 1)
            {
                DS_Print(0,0,6, "CTRL");
                bShiftKeysBlanked = 0;
            }
            else if(tms9901.CapsLock)
            {
                DS_Print(0,0,6, "CAPS");
                bShiftKeysBlanked = 0;
            }
            else
            {
                if (!bShiftKeysBlanked)
                {
                    DS_Print(0,0,6, "     ");
                    bShiftKeysBlanked = 1;
                }
            }
        }
    }
}


// ------------------------------------------------------
// So we can stuff keys into the keyboard buffer...
// ------------------------------------------------------
void KeyPush(u8 key)
{
    key_push[key_push_write] = key;
    key_push_write = (key_push_write+1) & 0x1F;
}

void KeyPushFilename(char *filename)
{
    for (int i=0; i<strlen(filename); i++)
    {
        if (filename[i] >= 'A' && filename[i] <= 'Z')       KeyPush(TMS_KEY_A + (filename[i]-'A'));
        else if (filename[i] >= 'a' && filename[i] <= 'z')  KeyPush(TMS_KEY_A + (filename[i]-'a'));
        else if (filename[i] >= '1' && filename[i] <= '9')  KeyPush(TMS_KEY_1 + (filename[i]-'1'));
        else if (filename[i] == '0')                        KeyPush(TMS_KEY_0);
        else if (filename[i] == '-')                        KeyPush(KBD_MINUS);
    }
}


#define MAX_FILES_PER_DSK           32         // We allow 32 files shown per disk... that's enough for our purposes and it's what we can show on screen
char dsk_listing[MAX_FILES_PER_DSK][16];       // And room for 16 characters per file (really 10 but we keep it on an 16-byte boundary)
u8   dsk_num_files = 0;
void ShowDiskListing(void)
{
    // Clear the screen...
    for (u8 i=0; i<20; i++)
    {
        DS_Print(1,4+i,6, "                                ");
    }

    while (keysCurrent()) WAITVBL; // While any key is pressed...
    
    // ---------------------------------------------------------------
    // Set all dsk listings to spaces until we read disk contents
    // ---------------------------------------------------------------
    for (u8 i=0; i<MAX_FILES_PER_DSK; i++)
    {
        strcpy(dsk_listing[i], "          ");
    }

    u8 idx=0;
    DS_Print(5,5,6,      "=== DISK CONTENTS ===");
    dsk_num_files = 0;
    if (Disk[disk_drive_select].isMounted)
    {
        // --------------------------------------------------------
        // First find all files and store them into our array...
        // --------------------------------------------------------
        u16 sectorPtr = 0;
        for (u16 i=0; i<256; i += 2)
        {
            sectorPtr = (Disk[disk_drive_select].image[256 + (i+0)] << 8) | (Disk[disk_drive_select].image[256 + (i+1)] << 0);
            if (sectorPtr == 0) break;
            for (u8 j=0; j<10; j++) 
            {
                dsk_listing[dsk_num_files][j] = Disk[disk_drive_select].image[(256*sectorPtr) + j];
            }
            dsk_listing[dsk_num_files][10] = 0; // Make sure it's NULL terminated
            if (++dsk_num_files >= MAX_FILES_PER_DSK) break;
        }
        
        u16 key = 0; 
        u8 last_sel = 255;
        u8 sel = 0;
        while (key != KEY_A)
        {
            key = keysCurrent();
            if (key != 0) while (!keysCurrent()) WAITVBL; // wait for release
            if (key == KEY_DOWN){if (sel < (dsk_num_files-1)) sel++;}
            if (key == KEY_UP)  {if (sel > 0) sel--;}
            WAITVBL;
            if (last_sel != sel)
            {
                // ----------------------------------------------------
                // Now display the files and let the user navigate...
                // ----------------------------------------------------
                for (u8 i=0; i<MAX_FILES_PER_DSK; i++)
                {
                    sprintf(tmpBuf, "%-10s", dsk_listing[i+0]);
                    if (i < (MAX_FILES_PER_DSK/2)) DS_Print(5, 7+i, (i==sel)?2:0, tmpBuf);
                    else DS_Print(18, 7+(i-(MAX_FILES_PER_DSK/2)), (i==sel)?2:0, tmpBuf);
                }
                last_sel = sel;
            }
        }
        strcpy(dsk_filename, dsk_listing[sel]);
    }
    else
    {
        idx++;idx++;
        DS_Print(9,9+idx,0,  "NO DISK MOUNTED"); idx++;
    }

    // Wait for any press and release...
    while (keysCurrent()) WAITVBL; // While no key is pressed...
    WAITVBL;
}

// ------------------------------------------------------------------------
// Show the Disk Menu text - highlight the selected row.
// ------------------------------------------------------------------------
void DiskMenuShow(bool bClearScreen, u8 sel)
{
    disk_menu_items = 0;
    if (bClearScreen)
    {
        DrawCleanBackground();
    }
    
    DS_Print(8,6,6,                                                 " TI DISK MENU ");
    sprintf(tmpBuf, " MOUNT   DSK%d ", disk_drive_select+1); DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  tmpBuf);  disk_menu_items++;
    sprintf(tmpBuf, " UNMOUNT DSK%d ", disk_drive_select+1); DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  tmpBuf);  disk_menu_items++;
    sprintf(tmpBuf, " LIST    DSK%d ", disk_drive_select+1); DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  tmpBuf);  disk_menu_items++;
    sprintf(tmpBuf, " PASTE   DSK%d ", disk_drive_select+1); DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  tmpBuf);  disk_menu_items++;
    sprintf(tmpBuf, " PASTE   FILE%d", disk_drive_select+1); DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  tmpBuf);  disk_menu_items++;
    sprintf(tmpBuf, " BACKUP  DSK%d",  disk_drive_select+1); DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  tmpBuf);  disk_menu_items++;
    DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  " EXIT    MENU ");  disk_menu_items++;

    if (Disk[disk_drive_select].isMounted)
    {
        u16 numSectors = (Disk[disk_drive_select].image[0x0A] << 8) | Disk[disk_drive_select].image[0x0B];
        sprintf(tmpBuf, "DSK%d MOUNTED %s/%s %3dKB", disk_drive_select+1, (Disk[disk_drive_select].image[0x12] == 2 ? "DS":"SS"), (Disk[disk_drive_select].image[0x13] == 2 ? "DD":"SD"), (numSectors*256)/1024);
        DS_Print(4,9+disk_menu_items+1,(sel==disk_menu_items)?2:0,tmpBuf);
        
        u8 col=0;
        strncpy(tmpBuf, Disk[disk_drive_select].filename, 32);
        tmpBuf[31] = 0;
        if (strlen(tmpBuf) < 32) col=16-(strlen(tmpBuf)/2);
        if (strlen(tmpBuf) & 1) col--;
        DS_Print(col,9+disk_menu_items+3,(sel==disk_menu_items)?2:0,tmpBuf);   
    } 
    else
    {
        DS_Print(3,9+disk_menu_items+1,(sel==disk_menu_items)?2:0,"      DISK NOT MOUNTED       ");
    }
    
    DS_Print(2,22,0, "A TO SELECT, X SWITCH DRIVES");
}

// ------------------------------------------------------------------------
// Handle Disk mini-menu interface...
// ------------------------------------------------------------------------
void DiskMenu(void)
{
  u8 menuSelection = 0;
    
  SoundPause();
  while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_A ))!=0);

  DiskMenuShow(true, menuSelection);

  while (true) 
  {
    nds_key = keysCurrent();
    if (nds_key)
    {
        if (nds_key & KEY_UP)  
        {
            menuSelection = (menuSelection > 0) ? (menuSelection-1):(disk_menu_items-1);
            DiskMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_DOWN)  
        {
            menuSelection = (menuSelection+1) % disk_menu_items;
            DiskMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_X)  
        {
            // Wait for keyrelease...
            while (keysCurrent() & KEY_X) WAITVBL;
            disk_drive_select = (disk_drive_select+1) % MAX_DSKS;
            DiskMenuShow(true, menuSelection);
        }
        if (nds_key & KEY_A)  
        {
            if (menuSelection == 0) // MOUNT .DSK FILE
            {
                TILoadDiskFile();   // Sets myDskFile[] and myDskPath[]
                if (myDskFile != NULL)
                {
                    disk_mount(disk_drive_select, myDskPath, myDskFile);
                    DiskMenuShow(true, menuSelection);
                }
                else
                {
                    DiskMenuShow(true, menuSelection);
                }
            }
            if (menuSelection == 1) // UNMOUNT .DSK FILE
            {
                disk_unmount(disk_drive_select);
                DiskMenuShow(true, menuSelection);
            }
            if (menuSelection == 2)
            {
                  ShowDiskListing();
                  DiskMenuShow(true, menuSelection);
            }
            if (menuSelection == 3) // PASTE DSK1.FILENAME
            {
                  KeyPush(TMS_KEY_D);KeyPush(TMS_KEY_S);KeyPush(TMS_KEY_K);KeyPush(TMS_KEY_1+disk_drive_select);KeyPush(TMS_KEY_PERIOD);
                  KeyPushFilename(dsk_filename);
                  break;
            }
            if (menuSelection == 4) // PASTE FILENAME
            {
                  KeyPushFilename(dsk_filename);
                  break;
            }
            if (menuSelection == 5) // BACKUP DSKx
            {
                  if (Disk[disk_drive_select].isMounted)
                  {
                      DS_Print(11,0,6, "BACKUP DISK");
                      disk_backup_to_sd(disk_drive_select);
                      WAITVBL;WAITVBL;
                      DiskMenuShow(true, menuSelection);
                      DS_Print(11,0,6, "           ");
                  }
            }
            if (menuSelection == 6) // EXIT
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
// Keyboard Handling Stuff...
// ------------------------------------------------------------------------
u8 bKeyClick = 0;

// ------------------------------------------------------------------------
// Show the Mini Menu - highlight the selected row. 
// ------------------------------------------------------------------------
u8 mini_menu_items = 0;
void MiniMenuShow(bool bClearScreen, u8 sel)
{
    mini_menu_items = 0;
    if (bClearScreen)
    {
        DrawCleanBackground();
    }
    
    DS_Print(8,7,6,                                           " TI MINI MENU  ");
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " HIGH   SCORE  ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " SAVE   STATE  ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " LOAD   STATE  ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " DISK   MENU   ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " QUIT   GAME   ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " EXIT   MENU   ");  mini_menu_items++;
}

// ------------------------------------------------------------------------
// Handle mini-menu interface...
// ------------------------------------------------------------------------
u8 MiniMenu(void)
{
  u8 retVal = META_KEY_NONE;
  u8 menuSelection = 0;
    
  SoundPause();
  while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_A ))!=0);

  MiniMenuShow(true, menuSelection);

  while (true) 
  {
    nds_key = keysCurrent();
    if (nds_key)
    {
        if (nds_key & KEY_UP)  
        {
            menuSelection = (menuSelection > 0) ? (menuSelection-1):(mini_menu_items-1);
            MiniMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_DOWN)  
        {
            menuSelection = (menuSelection+1) % mini_menu_items;
            MiniMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_A)  
        {
            if      (menuSelection == 0) retVal = META_KEY_HIGHSCORE;
            else if (menuSelection == 1) retVal = META_KEY_SAVESTATE;
            else if (menuSelection == 2) retVal = META_KEY_LOADSTATE;
            else if (menuSelection == 3) retVal = META_KEY_DISKMENU;
            else if (menuSelection == 4) retVal = META_KEY_QUIT;
            else retVal = META_KEY_NONE;
            break;
        }
        if (nds_key & KEY_B)  
        {
            retVal = META_KEY_NONE;
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
    
  return retVal;
}


// ------------------------------------------------------------------------------------------------
// Here we've already determined that a touch event has taken place on the DS so we are going to 
// look up (row-by-row) what key was pressed. The routine will set the appopriate bit in the 
// Keyboard[] array to let the emulation know that a key has been pressed. Special attention 
// is made for the 'sticky' special keys like Function, Control and Shift.
// ------------------------------------------------------------------------------------------------
u8 CheckKeyboardInput(u16 iTy, u16 iTx)
{
    if (myConfig.overlay == 0)
    {
        // --------------------------------------------------------------------------
        // Test the touchscreen rendering of the keyboard
        // --------------------------------------------------------------------------
        if ((iTy >= 28) && (iTy < 56))        // Row 1 (top row)
        {
            if      ((iTx >= 1)   && (iTx < 31))   {tms9901.Keyboard[TMS_KEY_1]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 31)  && (iTx < 60))   {tms9901.Keyboard[TMS_KEY_2]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 60)  && (iTx < 90))   {tms9901.Keyboard[TMS_KEY_3]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 90)  && (iTx < 118))  {tms9901.Keyboard[TMS_KEY_4]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 118) && (iTx < 147))  {tms9901.Keyboard[TMS_KEY_5]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 147) && (iTx < 176))  {tms9901.Keyboard[TMS_KEY_6]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 176) && (iTx < 205))  {tms9901.Keyboard[TMS_KEY_7]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 205) && (iTx < 234))  {tms9901.Keyboard[TMS_KEY_8]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 234) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_EQUALS]=1; if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 56) && (iTy < 84))   // Row 2
        {
            if      ((iTx >= 1)   && (iTx < 31))   {tms9901.Keyboard[TMS_KEY_9]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 31)  && (iTx < 60))   {tms9901.Keyboard[TMS_KEY_0]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 60)  && (iTx < 90))   {tms9901.Keyboard[TMS_KEY_A]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 90)  && (iTx < 118))  {tms9901.Keyboard[TMS_KEY_B]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 118) && (iTx < 147))  {tms9901.Keyboard[TMS_KEY_C]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 147) && (iTx < 176))  {tms9901.Keyboard[TMS_KEY_D]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 176) && (iTx < 205))  {tms9901.Keyboard[TMS_KEY_E]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 205) && (iTx < 234))  {tms9901.Keyboard[TMS_KEY_F]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 234) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_SLASH]=1;  if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 84) && (iTy < 112))  // Row 3
        {
            if      ((iTx >= 1)   && (iTx < 31))   {tms9901.Keyboard[TMS_KEY_G]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 31)  && (iTx < 60))   {tms9901.Keyboard[TMS_KEY_H]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 60)  && (iTx < 90))   {tms9901.Keyboard[TMS_KEY_I]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 90)  && (iTx < 118))  {tms9901.Keyboard[TMS_KEY_J]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 118) && (iTx < 147))  {tms9901.Keyboard[TMS_KEY_K]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 147) && (iTx < 176))  {tms9901.Keyboard[TMS_KEY_L]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 176) && (iTx < 205))  {tms9901.Keyboard[TMS_KEY_M]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 205) && (iTx < 234))  {tms9901.Keyboard[TMS_KEY_N]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 234) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_SEMI]=1;   if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 112) && (iTy < 140))  // Row 4
        {
            if      ((iTx >= 1)   && (iTx < 31))   {tms9901.Keyboard[TMS_KEY_O]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 31)  && (iTx < 60))   {tms9901.Keyboard[TMS_KEY_P]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 60)  && (iTx < 90))   {tms9901.Keyboard[TMS_KEY_Q]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 90)  && (iTx < 118))  {tms9901.Keyboard[TMS_KEY_R]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 118) && (iTx < 147))  {tms9901.Keyboard[TMS_KEY_S]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 147) && (iTx < 176))  {tms9901.Keyboard[TMS_KEY_T]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 176) && (iTx < 205))  {tms9901.Keyboard[TMS_KEY_U]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 205) && (iTx < 234))  {tms9901.Keyboard[TMS_KEY_V]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 234) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_S]=1; tms9901.Keyboard[TMS_KEY_FUNCTION]=1;  if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 140) && (iTy < 169))  // Row 5
        {
            if      ((iTx >= 1)   && (iTx < 31))   {tms9901.Keyboard[TMS_KEY_W]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 31)  && (iTx < 60))   {tms9901.Keyboard[TMS_KEY_X]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 60)  && (iTx < 90))   {tms9901.Keyboard[TMS_KEY_Y]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 90)  && (iTx < 118))  {tms9901.Keyboard[TMS_KEY_Z]=1;      if (!bKeyClick) bKeyClick=1;}

            else if ((iTx >= 118) && (iTx < 150))  {tms9901.Keyboard[TMS_KEY_6]=1; tms9901.Keyboard[TMS_KEY_FUNCTION]=1; if (!bKeyClick) bKeyClick=1;} //PRO'C
            else if ((iTx >= 150) && (iTx < 180))  {tms9901.Keyboard[TMS_KEY_8]=1; tms9901.Keyboard[TMS_KEY_FUNCTION]=1; if (!bKeyClick) bKeyClick=1;} //REDO
            else if ((iTx >= 180) && (iTx < 212))  {tms9901.Keyboard[TMS_KEY_9]=1; tms9901.Keyboard[TMS_KEY_FUNCTION]=1; if (!bKeyClick) bKeyClick=1;} //BACK

            else if ((iTx >= 212) && (iTx < 233))  {tms9901.Keyboard[TMS_KEY_COMMA]=1;   if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 233) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_PERIOD]=1;  if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 169) && (iTy < 192))  // Row 6
        {
            if      ((iTx >= 1)   && (iTx < 35))   DiskMenu();
            else if ((iTx >= 174) && (iTx < 214))  {tms9901.Keyboard[TMS_KEY_SPACE]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 214) && (iTx < 256))  {tms9901.Keyboard[TMS_KEY_ENTER]=1; if (!bKeyClick) bKeyClick=1;}
            
            // Meta Keys on the bottom row...
            if  (((iTx>=35) && (iTy>=169) && (iTx<70) && (iTy<192)))    return META_KEY_QUIT;       // Test if "End Game" selected
            if  (((iTx>=70) && (iTy>=169) && (iTx< 104) && (iTy<192)))  return META_KEY_HIGHSCORE;  // Test if "High Score" selected
            if  (((iTx>=104) && (iTy>=169) && (iTx< 139) && (iTy<192))) return META_KEY_SAVESTATE;  // Test if "Save State" selected
            if  (((iTx>=139) && (iTy>=169) && (iTx<174) && (iTy<192)))  return META_KEY_LOADSTATE;  // Test if "Load State" selected            
        }
    }
    else // Must be TI99 keyboard (for now... until custom is available)
    {
        // --------------------------------------------------------------------------
        // Test the touchscreen rendering of the keyboard
        // --------------------------------------------------------------------------
        if ((iTy >= 8) && (iTy < 47))        // Row 1 (top row)
        {
            if      ((iTx >= 3)   && (iTx < 24))   {tms9901.Keyboard[TMS_KEY_1]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 24)  && (iTx < 45))   {tms9901.Keyboard[TMS_KEY_2]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 45)  && (iTx < 66))   {tms9901.Keyboard[TMS_KEY_3]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 66)  && (iTx < 87))   {tms9901.Keyboard[TMS_KEY_4]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 87)  && (iTx < 108))  {tms9901.Keyboard[TMS_KEY_5]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 108) && (iTx < 129))  {tms9901.Keyboard[TMS_KEY_6]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 129) && (iTx < 150))  {tms9901.Keyboard[TMS_KEY_7]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 150) && (iTx < 171))  {tms9901.Keyboard[TMS_KEY_8]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 171) && (iTx < 192))  {tms9901.Keyboard[TMS_KEY_9]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 192) && (iTx < 213))  {tms9901.Keyboard[TMS_KEY_0]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 213) && (iTx < 234))  {tms9901.Keyboard[TMS_KEY_EQUALS]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 234) && (iTx < 256))  return MiniMenu();
        }
        else if ((iTy >= 47) && (iTy < 83))        // Row 2 (QWERTY row)
        {
            if      ((iTx >= 14)  && (iTx < 35))   {tms9901.Keyboard[TMS_KEY_Q]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 35)  && (iTx < 56))   {tms9901.Keyboard[TMS_KEY_W]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 56)  && (iTx < 77))   {tms9901.Keyboard[TMS_KEY_E]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 77)  && (iTx < 98))   {tms9901.Keyboard[TMS_KEY_R]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 98)  && (iTx < 119))  {tms9901.Keyboard[TMS_KEY_T]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 119) && (iTx < 140))  {tms9901.Keyboard[TMS_KEY_Y]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 140) && (iTx < 161))  {tms9901.Keyboard[TMS_KEY_U]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 161) && (iTx < 182))  {tms9901.Keyboard[TMS_KEY_I]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 182) && (iTx < 203))  {tms9901.Keyboard[TMS_KEY_O]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 203) && (iTx < 224))  {tms9901.Keyboard[TMS_KEY_P]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 224) && (iTx < 245))  {tms9901.Keyboard[TMS_KEY_SLASH]=1;  if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 83) && (iTy < 119))       // Row 3 (ASDF row)
        {
            if      ((iTx >= 20)  && (iTx < 42))   {tms9901.Keyboard[TMS_KEY_A]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 42)  && (iTx < 63))   {tms9901.Keyboard[TMS_KEY_S]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 63)  && (iTx < 84))   {tms9901.Keyboard[TMS_KEY_D]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 84)  && (iTx < 105))  {tms9901.Keyboard[TMS_KEY_F]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 105) && (iTx < 126))  {tms9901.Keyboard[TMS_KEY_G]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 126) && (iTx < 147))  {tms9901.Keyboard[TMS_KEY_H]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 147) && (iTx < 167))  {tms9901.Keyboard[TMS_KEY_J]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 167) && (iTx < 189))  {tms9901.Keyboard[TMS_KEY_K]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 189) && (iTx < 208))  {tms9901.Keyboard[TMS_KEY_L]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 208) && (iTx < 231))  {tms9901.Keyboard[TMS_KEY_SEMI]=1;   if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 231) && (iTx < 256))  {tms9901.Keyboard[TMS_KEY_ENTER]=1;  if (!bKeyClick) bKeyClick=1;}
        }
        else if ((iTy >= 119) && (iTy < 155))       // Row 4 (ZXCV row)
        {
            if      ((iTx >= 11)  && (iTx < 32))   return META_KEY_SHIFT;
            else if ((iTx >= 32)  && (iTx < 53))   {tms9901.Keyboard[TMS_KEY_Z]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 53)  && (iTx < 74))   {tms9901.Keyboard[TMS_KEY_X]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 74)  && (iTx < 95))   {tms9901.Keyboard[TMS_KEY_C]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 95)  && (iTx < 116))  {tms9901.Keyboard[TMS_KEY_V]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 116) && (iTx < 137))  {tms9901.Keyboard[TMS_KEY_B]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 137) && (iTx < 158))  {tms9901.Keyboard[TMS_KEY_N]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 158) && (iTx < 179))  {tms9901.Keyboard[TMS_KEY_M]=1;      if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 179) && (iTx < 200))  {tms9901.Keyboard[TMS_KEY_COMMA]=1;  if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 200) && (iTx < 222))  {tms9901.Keyboard[TMS_KEY_PERIOD]=1; if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 222) && (iTx < 255))  return META_KEY_SHIFT;
        }
        else if ((iTy >= 155) && (iTy <= 192))       // Row 5 (SPACE BAR row)
        {
            if      ((iTx >= 11)  && (iTx < 32))   return META_KEY_ALPHALOCK;
            else if ((iTx >= 32)  && (iTx < 53))   return META_KEY_CONTROL;
            else if ((iTx >= 53)  && (iTx < 200))  {tms9901.Keyboard[TMS_KEY_SPACE]=1;   if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 200) && (iTx < 221))  return META_KEY_FUNCTION;
            else if ((iTx >= 221) && (iTx < 255))  DiskMenu();
        }
    }

    // ----------------------------------------------------------------
    // This is only set to 1 if a normal (non meta) key is pressed...
    // ----------------------------------------------------------------
    if (bKeyClick == 1)
    {
        mmEffect(SFX_KEYCLICK);  // Play short key click for feedback...
        bKeyClick = 2;      // No more click - one was enough 
        
        if (handling_meta) handling_meta = 4;   // This will force the meta key to deactivate as soon as nothing is touched on the virtual keyboard screen
    }
    
    return META_KEY_NONE;
}


void  __attribute__ ((noinline)) DisplayFrameCounter(u16 emuFps)
{
    // If not asked to run full-speed... adjust FPS so it's stable near 60
    if (globalConfig.showFPS != 2)
    {
        if (emuFps == 61) emuFps=60;
        else if (emuFps == 59) emuFps=60;            
    }
    if (emuFps/100) tmpBuf[0] = '0' + emuFps/100;
    else tmpBuf[0] = ' ';
    tmpBuf[1] = '0' + (emuFps%100) / 10;
    tmpBuf[2] = '0' + (emuFps%100) % 10;
    tmpBuf[3] = 0;
    DS_Print(0,0,6,tmpBuf);
}

// ---------------------------------------------------------------------------------------
// Some common setup stuff that we don't need to take up space in fast ITCM_CODE memory
// ---------------------------------------------------------------------------------------
void __attribute__ ((noinline)) ds99_main_setup(void)
{
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
   
}

// ------------------------------------------------------------------------
// The main emulation loop is here... call into the TMS9900, VDP and 
// run the sound engine. This is placed in fast ITCM_CODE even though it
// really only runs at 60 fps (or 50 for PAL systems)... mainly because
// it's the heart of our system that drives all other calls and make
// the emulation work.  If we need ITCM_CODE space, we could probably
// give up on putting this in fast memory and not lose much performance.
// ------------------------------------------------------------------------
ITCM_CODE void ds99_main(void) 
{
  u8 dampen = 0;
  u16 iTx,  iTy;
  u8 ResetNow  = 0;

  ds99_main_setup();    // Stuff we don't need in ITCM fast memory...
    
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
        if (TIMER1_DATA >= 16364)   //  500ms (half-sec)
        {
            static u16 once_per_sec = 0;
            TIMER1_CR = 0;
            TIMER1_DATA = 0;
            TIMER1_CR=TIMER_ENABLE | TIMER_DIV_1024;
            if (once_per_sec++ & 1)
            {
                if (globalConfig.showFPS)
                {
                    DisplayFrameCounter(emuActFrames);
                    }
                emuActFrames = 0;

                if (bShowDebug)
                {
                    sprintf(tmpBuf, "%u %u %u %u %u", (unsigned int)debug[0], (unsigned int)debug[1], (unsigned int)debug[2], (unsigned int)debug[3], (unsigned int)debug[4]); 
                    DS_Print(5,0,6,tmpBuf);
                }
            }
            DisplayStatusLine(false);   // This updates twice per second
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
            if (globalConfig.showFPS == 2) break;   // If Full Speed, break out...
        }
        
      // Clear out the Joystick and Keyboard table - we'll check for keys below
      TMS9901_ClearJoyKeyData();
      
      tms9901.CapsLock = alpha_lock; // Set the state of our Caps Lock
        
      if (meta_next_key)
      {
          switch (meta_next_key)
          {
              case META_KEY_SHIFT:      tms9901.Keyboard[TMS_KEY_SHIFT]=1;     break;
              case META_KEY_CONTROL:    tms9901.Keyboard[TMS_KEY_CONTROL]=1;   break;
              case META_KEY_FUNCTION:   tms9901.Keyboard[TMS_KEY_FUNCTION]=1;  break;
          }
      }
      
      if (!(++dampen & 3))
      {
          if (key_push_read != key_push_write) // There are keys to process in the Push buffer
          {
              tms9901.Keyboard[key_push[key_push_read]]=1;
              key_push_read = (key_push_read+1) & 0x1F;
          }
      }
        
      if  (keysCurrent() & KEY_TOUCH)
      {
        touchPosition touch;
        touchRead(&touch);
        iTx = touch.px;
        iTy = touch.py;
          
        // ---------------------------------
        // Check the Keyboard for input...
        // ---------------------------------
        u8 meta = CheckKeyboardInput(iTy, iTx);

        switch (meta)
        {
            case META_KEY_QUIT:
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
            break;
                
            case META_KEY_HIGHSCORE:
            {
              //  Stop sound
              SoundPause();
              highscore_display(file_crc);
              DisplayStatusLine(true);
              SoundUnPause();
            }
            break;

            case META_KEY_SAVESTATE:
            {
              // Stop sound
              SoundPause();
              if  (showMessage("DO YOU REALLY WANT TO","SAVE GAME STATE ?") == ID_SHM_YES) 
              {                      
                TI99SaveState();
              }
              SoundUnPause();
            }
            break;

            case META_KEY_LOADSTATE:
            {
              // Stop sound
              SoundPause();
              if  (showMessage("DO YOU REALLY WANT TO","LOAD GAME STATE ?") == ID_SHM_YES) 
              {                      
                TI99LoadState();
              }
              SoundUnPause();
            }
            break;
                
            case META_KEY_DISKMENU:
                DiskMenu();
                break;             
                
            case META_KEY_ALPHALOCK:
                if (handling_meta == 0)
                {
                    alpha_lock = alpha_lock^1;
                    DisplayStatusLine(false);
                    handling_meta = 1;
                }
                break;
            case META_KEY_SHIFT:
                if (handling_meta == 0)
                {
                    if (meta_next_key == META_KEY_SHIFT) meta_next_key = 0;
                    else meta_next_key = META_KEY_SHIFT;
                    tms9901.Keyboard[TMS_KEY_SHIFT]=1;
                    DisplayStatusLine(false);
                    handling_meta = 1;
                }
                else if (handling_meta == 2)
                {
                    tms9901.Keyboard[TMS_KEY_SHIFT]=0;
                    meta_next_key = 0;
                    handling_meta = 0;
                    DisplayStatusLine(false);
                    handling_meta = 3;
                }
                break;
            case META_KEY_CONTROL:
                if (handling_meta == 0)
                {
                    if (meta_next_key == META_KEY_CONTROL) meta_next_key = 0;
                    else meta_next_key = META_KEY_CONTROL;
                    tms9901.Keyboard[TMS_KEY_CONTROL]=1;
                    DisplayStatusLine(false);
                    handling_meta = 1;
                }
                else if (handling_meta == 2)
                {
                    tms9901.Keyboard[TMS_KEY_CONTROL]=0;
                    meta_next_key = 0;
                    handling_meta = 0;
                    DisplayStatusLine(false);
                    handling_meta = 3;
                }
                break;
            case META_KEY_FUNCTION:
                if (handling_meta == 0)
                {
                    if (meta_next_key == META_KEY_FUNCTION) meta_next_key = 0;
                    else meta_next_key = META_KEY_FUNCTION;
                    tms9901.Keyboard[TMS_KEY_FUNCTION]=1;
                    meta_next_key = META_KEY_FUNCTION;
                    handling_meta = 1;
                    DisplayStatusLine(false);
                } 
                else if (handling_meta == 2)
                {
                    tms9901.Keyboard[TMS_KEY_FUNCTION]=0;
                    meta_next_key = 0;
                    handling_meta = 0;
                    DisplayStatusLine(false);
                    handling_meta = 3;
                }
                break;
        }
      } // No Screen Touch...
      else  
      {
          if (handling_meta < 4)
          {
              if (meta_next_key == 0) // if we were dealing with Alpha Lock
              {
                  handling_meta = 0; 
              }
              else
              {
                  handling_meta = 2;    // We've released a meta key...
              }
          }
          else
          if (handling_meta == 4)
          {
                // And here we only want to reset the meta key if we have released a normal key
                meta_next_key = 0;  // No more meta key        
                handling_meta = 0;  // We've handled a normal key... no more meta
          }
          
          ResetNow = 0;
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
      if ((nds_key & KEY_L) && (nds_key & KEY_R) && (nds_key & KEY_Y)) 
      {
            DS_Print(10,0,0,"SNAPSHOT");
            screenshot();
            WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
            DS_Print(10,0,0,"        ");
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

                      case KBD_A:
                      case KBD_B:
                      case KBD_C:
                      case KBD_D:
                      case KBD_E:
                      case KBD_F:
                      case KBD_G:
                      case KBD_H:
                      case KBD_I:
                      case KBD_J:
                      case KBD_K:
                      case KBD_L:
                      case KBD_M:
                      case KBD_N:
                      case KBD_O:
                      case KBD_P:
                      case KBD_Q:
                      case KBD_R:
                      case KBD_S:
                      case KBD_T:
                      case KBD_U:
                      case KBD_V:
                      case KBD_W:
                      case KBD_X:
                      case KBD_Y:
                      case KBD_Z:
                          tms9901.Keyboard[TMS_KEY_A+(map-KBD_A)]=1;         
                          break;

                      case KBD_1:
                      case KBD_2:
                      case KBD_3:
                      case KBD_4:
                      case KBD_5:
                      case KBD_6:
                      case KBD_7:
                      case KBD_8:
                      case KBD_9:
                      case KBD_0:
                          tms9901.Keyboard[TMS_KEY_1+(map-KBD_1)]=1;         
                          break;
                          
                      case KBD_SPACE:       tms9901.Keyboard[TMS_KEY_SPACE]=1;     break;
                      case KBD_ENTER:       tms9901.Keyboard[TMS_KEY_ENTER]=1;     break;

                      case KBD_FNCT:        tms9901.Keyboard[TMS_KEY_FUNCTION]=1;  break;
                      case KBD_CTRL:        tms9901.Keyboard[TMS_KEY_CONTROL]=1;   break;
                      case KBD_SHIFT:       tms9901.Keyboard[TMS_KEY_SHIFT]=1;     break;
                          
                      case KBD_PLUS:        tms9901.Keyboard[TMS_KEY_EQUALS]=1;    tms9901.Keyboard[TMS_KEY_SHIFT]=1;    break;
                      case KBD_MINUS:       tms9901.Keyboard[TMS_KEY_SLASH]=1;     tms9901.Keyboard[TMS_KEY_SHIFT]=1;    break;
                      case KBD_PROC:        tms9901.Keyboard[TMS_KEY_6]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                      case KBD_REDO:        tms9901.Keyboard[TMS_KEY_8]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                      case KBD_BACK:        tms9901.Keyboard[TMS_KEY_9]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                      case KBD_FNCT_E:      tms9901.Keyboard[TMS_KEY_E]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                      case KBD_FNCT_S:      tms9901.Keyboard[TMS_KEY_S]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                      case KBD_FNCT_D:      tms9901.Keyboard[TMS_KEY_D]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
                      case KBD_FNCT_X:      tms9901.Keyboard[TMS_KEY_X]=1;         tms9901.Keyboard[TMS_KEY_FUNCTION]=1; break;
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
  decompress(splashTiles,  bgGetGfxPtr(bg0), LZ77Vram);
  decompress(splashMap,  (void*) bgGetMapPtr(bg0), LZ77Vram);
  dmaCopy((void*) splashPal,(void*)  BG_PALETTE,256*2);
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
    
    swiWaitForVBlank();
    if (myConfig.overlay == 0)
    {
        decompress(ds99kbdTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
        decompress(ds99kbdMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
        dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
        dmaCopy((void*) ds99kbdPal,(void*) BG_PALETTE_SUB,256*2);

        unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
        dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
    }
    else // Must be TI99 keyboard
    {
        decompress(ti99kbdTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
        decompress(ti99kbdMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
        dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
        dmaCopy((void*) ti99kbdPal,(void*) BG_PALETTE_SUB,256*2);
        
        unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
        dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
    }
    
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

  // Need to load in config file if only for the global options at this point...
  FindAndLoadConfig();
 
  // Read in and store the DS994a.HI file
  highscore_init();
    
  lcdMainOnTop();
    
  //  Init timer for frame management and install the sound handlers
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
      if (globalConfig.romsDIR == 0)
      {
          chdir("/roms");    // Try to start in roms area... doesn't matter if it fails
          chdir("ti99");     // And try to start in the subdir /ti99... doesn't matter if it fails.
      }
      else if (globalConfig.romsDIR == 1)
      {
          chdir("/roms");    // Try to start in roms area... doesn't matter if it fails
      }
  }
    
  // Start off with current directory for both ROMs and DSKs
  getcwd(currentDirROMs, MAX_PATH);
  getcwd(currentDirDSKs, MAX_PATH);
    
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
        if (globalConfig.skipBIOS == 0)
        {
            u8 idx = 6;
            DS_Print(2,idx++,0,"LOADING BIOS FILES ..."); idx++;
            if (bTIBIOSFound)          {DS_Print(2,idx++,0,"994aROM.bin   BIOS FOUND"); }
            if (bTIBIOSFound)          {DS_Print(2,idx++,0,"994aGROM.bin  GROM FOUND"); }
            if (bTIDISKFound)          {DS_Print(2,idx++,0,"994aDISK.bin  DSR  FOUND"); }
            idx++;
            DS_Print(2,idx++,0,"TOUCH SCREEN / KEY TO BEGIN"); idx++;

            while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_DOWN | KEY_UP | KEY_A | KEY_B | KEY_L | KEY_R))!=0);
            while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_DOWN | KEY_UP | KEY_A | KEY_B | KEY_L | KEY_R))==0);
            while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_DOWN | KEY_UP | KEY_A | KEY_B | KEY_L | KEY_R))!=0);
        }
    }
    else
    {
        DS_Print(2,10,0,"ERROR: TI99 BIOS NOT FOUND");
        DS_Print(2,12,0,"ERROR: CANT RUN WITHOUT BIOS");
        DS_Print(3,12,0,"ERROR: SEE README FILE");
        while(1) ;  // We're done... Need a TI99 bios to run this emulator
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


void _putchar(char character) {};   // Not used but needed to link printf()

u32 speechData32 __attribute__((section(".dtcm"))) = 0;

void CheckSpeech(u8 data)
{
    speechData32 = (speechData32 << 8) | data;
 
    if ((speechData32 & 0xFF000000) == 0x60000000)
    {
        // Parsec
        if (speechData32 == 0x60108058) mmEffect(SFX_PRESS_FIRE);
        if (speechData32 == 0x600E0821) mmEffect(SFX_ASTEROID);
        if (speechData32 == 0x604D7399) mmEffect((rand() & 3) ? SFX_DESTROYED:SFX_GOODSHOT);
        if (speechData32 == 0x604BCBD6) mmEffect((rand() & 3) ? SFX_DESTROYED:SFX_GOODSHOT);
        if (speechData32 == 0x60C6703A) mmEffect((rand() & 3) ? SFX_GOODSHOT:SFX_DESTROYED);
        if (speechData32 == 0x6046E3B2) mmEffect((rand() & 3) ? SFX_GOODSHOT:SFX_DESTROYED);

        // Alpiner
        if (speechData32 == 0x60CEE4F9) mmEffect(SFX_BEWARE);
        if (speechData32 == 0x604AD7AA) mmEffect(SFX_LOOKOUT);
        if (speechData32 == 0x604E6839) mmEffect(SFX_WATCHOUT);

        if (speechData32 == 0x60AADB82) mmEffect(SFX_YIKES);
        if (speechData32 == 0x60293565) mmEffect(SFX_UH);   
        if (speechData32 == 0x600828D2) mmEffect(SFX_OOOOH);
        if (speechData32 == 0x60A26A54) mmEffect(SFX_OUCH); 
        if (speechData32 == 0x602BCE6E) mmEffect(SFX_OOPS); 
        if (speechData32 == 0x60A574FE) mmEffect(SFX_UH);   
        if (speechData32 == 0x602530B1) mmEffect(SFX_OOPS); 

        // Moonmine
        if (speechData32 == 0x60C2E42E) mmEffect(SFX_LASEROVERHEAT); 
        if (speechData32 == 0x604C91D2) mmEffect(SFX_MONSTERDAMAGEDSHIP); 
        if (speechData32 == 0x6006A83A) mmEffect(SFX_UNKNOWNOBJECT); 
        if (speechData32 == 0x604CFFBE) mmEffect(SFX_ZYGAPPROACH); 
        if (speechData32 == 0x6004B0C7) mmEffect(SFX_CREWLOST); 
        if (speechData32 == 0x6044D55C) mmEffect(SFX_MONSTERDESTROYED); 
        if (speechData32 == 0x604E1DB1) mmEffect(SFX_GOODSHOTCAPTAIN); 
        if (speechData32 == 0x602EADC1) mmEffect(SFX_ZYGNEVERGET); 
        if (speechData32 == 0x604377A9) mmEffect(SFX_ZYGHAHA);         
        if (speechData32 == 0x60CA64B7) mmEffect(SFX_WATERAHEAD); 
        if (speechData32 == 0x604691D2) mmEffect(SFX_MONSTERATTACKEDCREW); 
        if (speechData32 == 0x604AE227) mmEffect(SFX_WAYTOGOCAP);        
    }
    else
    {
        // Parsec
        if (speechData32 == 0x1A31AF92) mmEffect(SFX_ADVANCING);
        if (speechData32 == 0x1E6575C0) mmEffect(SFX_ATTACKING);
    }
}
// End of file

