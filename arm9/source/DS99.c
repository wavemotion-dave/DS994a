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
#include <nds/fifomessages.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <malloc.h>    // for mallinfo()
#include <unistd.h>    // for sbrk()
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
#include "cpu/sn76496/SN76496.h"
#include "rpk/rpk.h"
#include "disk.h"
#include "SAMS.h"
#include "intro.h"
#include "ds99kbd.h"
#include "ti99kbd.h"
#include "alphakbd.h"
#include "debug.h"
#include "options.h"
#include "splash.h"
#include "screenshot.h"
#include "soundbank.h"
#include "soundbank_bin.h"

// --------------------------------------------------------------------------
// A small bank of 32-bit debug registers we can use for profiling or other
// sundry debug purposes. Pressing X when loading a game shows the debug
// registers. It's amazing how incredibly useful this proves to be.
// --------------------------------------------------------------------------
u32 debug[0x10]     __attribute__((section(".dtcm")));

// ---------------------------------------------------------------------------------------
// The master sound chip for the TI99. The SN sound chip is the same as the TI9919 chip.
// ---------------------------------------------------------------------------------------
SN76496 snti99      __attribute__((section(".dtcm")));

// ---------------------------------------------------------------------------
// Some timing and frame rate comutations to keep the emulation on pace...
// ---------------------------------------------------------------------------
u16 emuFps          __attribute__((section(".dtcm"))) = 0;
u16 emuActFrames    __attribute__((section(".dtcm"))) = 0;
u16 timingFrames    __attribute__((section(".dtcm"))) = 0;
u8  bShowDebug      __attribute__((section(".dtcm"))) = 0;
u8  speech_dampen   __attribute__((section(".dtcm"))) = 0;
u8  debug_screen = 0;

// ------------------------------------------------------------------------------------------------
// For the various BIOS files ... only the TI BIOS Roms are required - everything else is optional.
// ------------------------------------------------------------------------------------------------
u8 bTIBIOSFound      = false;
u8 bTIDISKFound      = false;

u8 soundEmuPause     __attribute__((section(".dtcm"))) = 1;       // Set to 1 to pause (mute) sound, 0 is sound unmuted (sound channels active)

u16 nds_key          __attribute__((section(".dtcm"))) = 0;       // 0 if no key pressed, othewise the NDS keys from keysCurrent() or similar

u8 alpha_lock       __attribute__((section(".dtcm"))) = 0;        // 0 if Alpha Lock is not pressed. 1 if pressed (CAPS)
u8 meta_next_key    __attribute__((section(".dtcm"))) = 0;        // Used to handle special meta keys like FNCT and CTRL and SHIFT
u8 handling_meta    __attribute__((section(".dtcm"))) = 0;        // Used to handle special meta keys like FNCT and CTRL and SHIFT
u16 vusCptVBL       __attribute__((section(".dtcm"))) = 0;        // We use this as a basic timer ticked every 1/60th of a second

char tmpBuf[256];               // For simple printf-type output and other sundry uses.
u8 fileBuf[8192];               // For DSK sector cache, general file I/O and file CRC generation use.

u8 *SharedMemBuffer;            // This is used mostly by the DS-Lite/Phat so it can share a block of memory for CART and SAMS

u8 bStartSoundEngine = false;   // Set to true to unmute sound after 1 frame of rendering...
int bg0, bg1, bg0b, bg1b;       // Some vars for NDS background screen handling
u8 last_pal_mode = 99;          // So we show PAL properly in the upper right of the lower DS screen
u16 floppy_sfx_dampen = 0;      // For Floppy Sound Effects - don't start the playback too often

u8 key_push_write = 0;          // For inserting DSK filenames into the keyboard buffer
u8 key_push_read  = 0;          // For inserting DSK filenames into the keyboard buffer
char key_push[0x20];            // A small array for when inserting DSK filenames into the keyboard buffer
char dsk_filename[16];          // Short filename to show on DISK Menu

u16 NTSC_Timing[] __attribute__((section(".dtcm"))) = {546, 496, 454, 422, 387, 360, 610, 695};    // 100%, 110%, 120%, 130%, 140%, 150% and then the slower 90% and 80%
u16 PAL_Timing[]  __attribute__((section(".dtcm"))) = {656, 596, 546, 504, 470, 435, 728, 795};    // 100%, 110%, 120%, 130%, 140%, 150% and then the slower 90% and 80%

u8 disk_menu_items = 0;        // Start with the top menu item
u8 disk_drive_select = DSK1;   // Start with DSK1

// The DS/DSi has 12 keys that can be mapped to virtually any TI key (joystick or keyboard)
u16 NDS_keyMap[12] __attribute__((section(".dtcm"))) = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_START, KEY_SELECT};

char myDskFile[MAX_PATH] = {0}; // This will be filled in with the full filename (including .DSK) of the disk file
char myDskPath[MAX_PATH] = {0}; // This will point to the path where the .DSK file was loaded from (and will be written back to)s
char initial_file[MAX_PATH];    // In case something was passed on the command line into the emulator (TWL++)

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

u8 mem_debug = 0;

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

mm_ds_system    sys      __attribute__((section(".dtcm")));
mm_stream       myStream __attribute__((section(".dtcm")));

// -----------------------------------------------------------------------------------
// For Wave Direct sampling, we sample every scanline and place into a buffer so that
// when the system calls OurSoundMixer(), we have samples we can load up and play...
// Don't make WAVE DIRECT size too small (choppy audio) or too large (laggy audio).
// -----------------------------------------------------------------------------------
#define WAVE_DIRECT_BUF_SIZE 0x7FF
u16 wave_mixer_read=0;
u16 wave_mixer_write=0;
s16 wave_mixer[WAVE_DIRECT_BUF_SIZE+1];
u8  wave_direct_skip=0;
s16 wave_breather = 0;
s16 wave_mixbuf[16];

// -------------------------------------------------------------------------
// For direct sampling, this tells us for a given scanline how many samples
// to process... this gets us to the magic sample_rate. Crude but effective.
// -------------------------------------------------------------------------
const u8 wave_direct_sample_table[256] =
{
  2,1,2,2,2,2,1,2,     2,2,1,2,2,2,2,2,
  2,2,1,2,2,2,1,2,     2,2,2,1,2,2,2,1,
  2,2,2,1,2,2,2,2,     2,1,2,2,1,2,2,2,
  2,2,2,2,1,2,2,2,     2,2,2,2,2,1,2,2,

  2,2,1,2,2,1,2,2,     2,2,2,1,2,2,1,2,
  2,2,2,2,2,2,1,2,     2,1,2,2,2,2,2,1,
  2,2,2,2,2,2,2,1,     1,2,2,2,2,2,2,2,
  1,2,2,2,2,2,2,2,     2,1,2,2,2,2,1,2,

  2,1,2,2,2,1,2,2,     2,2,2,2,2,2,1,2,
  2,2,2,2,2,2,1,2,     2,1,2,2,2,1,2,1,
  2,2,2,2,2,2,2,1,     1,2,2,1,2,2,2,2,
  1,2,2,2,2,1,2,2,     2,1,2,2,2,2,2,1,

  2,1,2,2,2,2,2,2,     2,2,1,2,2,2,1,2,
  2,2,1,2,2,2,2,2,     2,1,2,1,2,2,2,1,
  2,2,2,1,2,2,2,2,     2,2,2,2,1,2,2,2,
  2,2,2,2,1,2,2,2,     2,1,2,2,2,1,2,2,
};

// -------------------------------------------------------------------------------------------------
// This is called when we are configured for 'Wave Direct' and will process samples to synchronize
// with the scanline processing. This is not as smooth as the normal sound driver and takes more
// CPU power but will help render sound those few games that utilize digitize speech techniques.
// -------------------------------------------------------------------------------------------------
ITCM_CODE void processDirectAudio(void)
{
    int len = wave_direct_sample_table[wave_direct_skip++];

    sn76496Mixer(len*2, wave_mixbuf, &snti99);
    if (wave_breather) {return;}
    for (int i=0; i<len*2; i++)
    {
        // ------------------------------------------------------------------------
        // We normalize the samples and mix them carefully to minimize clipping...
        // ------------------------------------------------------------------------
        wave_mixer[wave_mixer_write] = (s16)wave_mixbuf[i];
        wave_mixer_write++; wave_mixer_write &= WAVE_DIRECT_BUF_SIZE;
        if (((wave_mixer_write+1)&WAVE_DIRECT_BUF_SIZE) == wave_mixer_read) {wave_breather = (WAVE_DIRECT_BUF_SIZE+1) >> 1; break;}
    }
}


// -------------------------------------------------------------------------------------------
// maxmod will call this routine when the buffer is half-empty and requests that
// we fill the sound buffer with more samples. They will request 'len' samples and
// we will fill exactly that many. If the sound is paused, we fill with 'mute' samples.
// -------------------------------------------------------------------------------------------
s16 last_sample __attribute__((section(".dtcm"))) = 0;
ITCM_CODE mm_word OurSoundMixer(mm_word len, mm_addr dest, mm_stream_formats format)
{
    if (soundEmuPause)  // If paused, just keep outputting the last sample which will produce no tones
    {
        s16 *p = (s16*)dest;
        for (int i=0; i<len*2; i++)
        {
           *p++ = last_sample;      // To prevent pops and clicks... just keep outputting the last sample
        }
    }
    else if (myConfig.sounddriver == 2) // Wave Direct
    {
        s16 *p = (s16*)dest;
        for (int i=0; i<len*2; i++)
        {
            if (wave_breather) {wave_breather--;}
            if (wave_mixer_read == wave_mixer_write) processDirectAudio();
            *p++ = wave_mixer[wave_mixer_read];
            wave_mixer_read++; wave_mixer_read &= WAVE_DIRECT_BUF_SIZE;
        }
        p--; last_sample = *p;
    }
    else
    {
        sn76496Mixer(len*2, dest, &snti99);         // Otherwise mix the channels into the buffer
        last_sample = ((s16*)dest)[len*2 - 1];      // And save off the last sample in case we need to mute...
    }

    return  len;
}


// -------------------------------------------------------------------------------------------
// Setup the maxmod audio stream - this will be a 16-bit Stereo PCM output at 55KHz which
// sounds about right for the TI99.
// -------------------------------------------------------------------------------------------
void setupStream(void)
{
  //---------------------------------------------------------------------
  //  initialize maxmod with our soundbank including all speech samples
  //---------------------------------------------------------------------
  mmInitDefaultMem((mm_addr)soundbank_bin);

  // --------------------------------------------------------------------------------------
  // And load up all our WAV sound effects... most of these are for various speech modules
  // --------------------------------------------------------------------------------------
  mmLoadEffect(SFX_KEYCLICK);
  mmLoadEffect(SFX_MUS_INTRO);
  mmLoadEffect(SFX_PRESS_FIRE);
  mmLoadEffect(SFX_ADVANCING);
  mmLoadEffect(SFX_GOODSHOT);
  mmLoadEffect(SFX_ATTACKING);
  mmLoadEffect(SFX_ASTEROID);
  mmLoadEffect(SFX_DESTROYED);
  mmLoadEffect(SFX_COUNTDOWN);
  mmLoadEffect(SFX_5);
  mmLoadEffect(SFX_4);
  mmLoadEffect(SFX_3);
  mmLoadEffect(SFX_2);
  mmLoadEffect(SFX_1);
  mmLoadEffect(SFX_BEWARE);
  mmLoadEffect(SFX_LOOKOUT);
  mmLoadEffect(SFX_WATCHOUT);
  mmLoadEffect(SFX_UH);
  mmLoadEffect(SFX_OOOOH);
  mmLoadEffect(SFX_OHNO);
  mmLoadEffect(SFX_YIKES);
  mmLoadEffect(SFX_OUCH);
  mmLoadEffect(SFX_OOPS);
  mmLoadEffect(SFX_ONWARD);
  mmLoadEffect(SFX_GOAGAIN);
  mmLoadEffect(SFX_YUCK);
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
  mmLoadEffect(SFX_DUCK);
  mmLoadEffect(SFX_MEANTO);
  mmLoadEffect(SFX_SPORT);
  mmLoadEffect(SFX_THANITLOOKS);
  mmLoadEffect(SFX_WALKEDINTO);
  mmLoadEffect(SFX_HELP);
  mmLoadEffect(SFX_ANYKEYTOGO);
  mmLoadEffect(SFX_GETTINGTIRED);
  mmLoadEffect(SFX_GAMEOVER);
  mmLoadEffect(SFX_BETTERLUCK);
  mmLoadEffect(SFX_ADVANCELEVEL);
  mmLoadEffect(SFX_CONTINUEGAME);
  mmLoadEffect(SFX_COOLANTLOW);
  mmLoadEffect(SFX_OUTOFWATER);
  mmLoadEffect(SFX_CONGRATSCAP);
  mmLoadEffect(SFX_LASERONTARGET);
  mmLoadEffect(SFX_NICESHOOTING);
  mmLoadEffect(SFX_GREATSHOT);
  mmLoadEffect(SFX_EXTRASHIP);
  mmLoadEffect(SFX_WARNINGFUEL);
  mmLoadEffect(SFX_SORRYFUEL);
  mmLoadEffect(SFX_MOONADVANCE);
  mmLoadEffect(SFX_EXTRACREW);
  mmLoadEffect(SFX_BONUSPOINTS);
  mmLoadEffect(SFX_BIG_GETYOU);
  mmLoadEffect(SFX_BIG_FALL);
  mmLoadEffect(SFX_BIG_ROAR);
  mmLoadEffect(SFX_BIG_CAW);
  mmLoadEffect(SFX_BIG_GOTYOU);
  mmLoadEffect(SFX_WELCOMEABOARD);
  mmLoadEffect(SFX_AVOIDMINES);
  mmLoadEffect(SFX_DAMAGEREPAIRED);
  mmLoadEffect(SFX_EXCELLENTMANUVER);
  mmLoadEffect(SFX_OHYES_SF);
  mmLoadEffect(SFX_OHNO_SF);
  mmLoadEffect(SFX_WHEREFLY_SF);
  mmLoadEffect(SFX_NEVERTRUST_SF);
  mmLoadEffect(SFX_GETIT_SF);
  mmLoadEffect(SFX_PATIENTREADY);
  mmLoadEffect(SFX_VIRUS);
  mmLoadEffect(SFX_DRLAVINE);
  mmLoadEffect(SFX_CONDITIONCRITICAL);
  mmLoadEffect(SFX_POWERLOW);
  mmLoadEffect(SFX_ENTERINGLUNG);
  mmLoadEffect(SFX_ENTERINGHEART);
  mmLoadEffect(SFX_ENTERINGKIDNEY);
  mmLoadEffect(SFX_ENTERINGSPLEEN);
  mmLoadEffect(SFX_GOFORTH);
  mmLoadEffect(SFX_EVILOCTOPUS);
  mmLoadEffect(SFX_ATTENDENERGY);
  mmLoadEffect(SFX_VOLCANICBLAST);
  mmLoadEffect(SFX_FREEME);
  mmLoadEffect(SFX_SEAHORSE);
  mmLoadEffect(SFX_TRIUMPTHED);
  mmLoadEffect(SFX_AVOIDPOSTS);
  mmLoadEffect(SFX_WATCHHOPPERS);
  mmLoadEffect(SFX_ALIENSAPPROACH);
  mmLoadEffect(SFX_BZK_KILLED);
  mmLoadEffect(SFX_BZK_CHICKEN);
  mmLoadEffect(SFX_BZK_ESCAPE);
  mmLoadEffect(SFX_BZK_INTRUDERALERT);
  mmLoadEffect(SFX_BZK_ATTACKHUMANOID);
  mmLoadEffect(SFX_WELCOMEKOREA);
  mmLoadEffect(SFX_ATTENTIONALL);
  mmLoadEffect(SFX_CHOPPERS);
  mmLoadEffect(SFX_REPORTSURGERY);
  mmLoadEffect(SFX_OVERHERE);
  mmLoadEffect(SFX_MEDIC);
  mmLoadEffect(SFX_SURGERYOOPS);
  mmLoadEffect(SFX_BUTTERFINGERS);
  mmLoadEffect(SFX_IGIVEUP);
  mmLoadEffect(SFX_YOUREOKAY);
  mmLoadEffect(SFX_NEXT);
  mmLoadEffect(SFX_THANKSDOC);
  mmLoadEffect(SFX_ANALIGATOR);
  mmLoadEffect(SFX_DEFUSEBOMB);
  mmLoadEffect(SFX_FINDTHEBOMB);
  mmLoadEffect(SFX_FOUNDTHEBOMB);
  mmLoadEffect(SFX_FLOPPY);

  //----------------------------------------------------------------
  //  open stream
  //----------------------------------------------------------------
  myStream.sampling_rate  = sample_rate;            // sampling rate
  myStream.buffer_length  = buffer_size;            // buffer length
  myStream.callback       = OurSoundMixer;          // set callback function
  myStream.format         = MM_STREAM_16BIT_STEREO; // format = stereo  16-bit
  myStream.timer          = MM_TIMER0;              // use hardware timer 0
  myStream.manual         = false;                  // use automatic filling
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
// The TI9919 sound chip is basically the same as the Colecovision SN76496
// chip and we already have a driver for that one and will use it.
// ------------------------------------------------------------------------
void dsInstallSoundEmuFIFO(void)
{
  SoundPause();

  //  ------------------------------------------------------------------
  //  The SN sound chip is for normal TI99 sound handling
  //  ------------------------------------------------------------------
  sn76496Reset(1, &snti99);         // Reset the SN sound chip

  sn76496W(0x80 | 0x00,&snti99);    // Write new Frequency for Channel A
  sn76496W(0x00 | 0x00,&snti99);    // Write new Frequency for Channel A
  sn76496W(0x90 | 0x0F,&snti99);    // Write new Volume for Channel A

  sn76496W(0xA0 | 0x00,&snti99);    // Write new Frequency for Channel B
  sn76496W(0x00 | 0x00,&snti99);    // Write new Frequency for Channel B
  sn76496W(0xB0 | 0x0F,&snti99);    // Write new Volume for Channel B

  sn76496W(0xC0 | 0x00,&snti99);    // Write new Frequency for Channel C
  sn76496W(0x00 | 0x00,&snti99);    // Write new Frequency for Channel C
  sn76496W(0xD0 | 0x0F,&snti99);    // Write new Volume for Channel C

  sn76496W(0xFF,  &snti99);         // Disable Noise Channel

  sn76496Mixer(8, (s16*)tmpBuf, &snti99);  // Do an initial mix conversion to clear the output

  setupStream();    // Setup maxmod stream...

  bStartSoundEngine = true; // Volume will 'unpause' after 1 frame in the main loop.
}

// --------------------------------------------------------------
// When we reset the machine, there are many small utility flags
// for various expansion peripherals that must be reset.
// --------------------------------------------------------------
void ResetStatusFlags(void)
{
    last_pal_mode = 99; // Just force the re-display
}


// --------------------------------------------------------------
// When we first load a ROM/DISK or when the user presses
// the RESET button on the touch-screen...
// --------------------------------------------------------------
void ResetTI(void)
{
  Reset9918();                           //  Reset video chip

  sn76496Reset(1, &snti99);              //  Reset the SN/TI sound chip
  sn76496W(0x90 | 0x0F  ,&snti99);       //  Write new Volume for Channel A (off)
  sn76496W(0xB0 | 0x0F  ,&snti99);       //  Write new Volume for Channel B (off)
  sn76496W(0xD0 | 0x0F  ,&snti99);       //  Write new Volume for Channel C (off)

  // For the direct sound driver...
  memset(wave_mixer,   0x00, sizeof(wave_mixer));
  wave_mixer_read=0;
  wave_mixer_write=0;

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

  // -----------------------------------------------------
  // Reset some common handling for keys, caps lock, etc.
  // -----------------------------------------------------
  alpha_lock = myConfig.capsLock;
  meta_next_key    = 0;
  handling_meta    = 0;

  key_push_write = 0;
  key_push_read  = 0;
  strcpy(dsk_filename,"");

  // ---------------------------------------------
  // Make sure the disk systems is up and running
  // ---------------------------------------------
  disk_init();
  disk_drive_select = DSK1; // Start with DSK1

  // ---------------------------------------------------
  // And reset our debugger registers on every new load
  // ---------------------------------------------------
  memset(debug, 0x00, sizeof(debug));
}

void __attribute__ ((noinline))  DisplayStatusLine(bool bForce)
{
    static u8 bShiftKeysBlanked = 0;
    static u8 bCapsKeysBlanked = 0;

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
            if (Disk[drive].driveWriteCounter)
            {
                sprintf(tmpBuf, "DISK %d WRITE", drive+1);
                DS_Print(11,0,6, tmpBuf);
                if (globalConfig.floppySound)
                {
                    if (++floppy_sfx_dampen & 1) mmEffect(SFX_FLOPPY);
                }
            }
            else
            {
                // Persist the disk - write it back to the SD card
                disk_write_to_sd(drive);
                DS_Print(11,0,6, "            ");
                floppy_sfx_dampen = 0;
            }
        }
        else if (Disk[drive].driveReadCounter)
        {
            Disk[drive].driveReadCounter--;
            if (Disk[drive].driveReadCounter)
            {
                sprintf(tmpBuf, "DISK %d READ ", drive+1);
                DS_Print(11,0,6, tmpBuf);
                if (globalConfig.floppySound)
                {
                    if (++floppy_sfx_dampen & 1) mmEffect(SFX_FLOPPY);
                }
            }
            else
            {
                 DS_Print(11,0,6, "            ");
                 floppy_sfx_dampen = 0;
            }
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
            else
            {
                if (!bShiftKeysBlanked)
                {
                    DS_Print(0,0,6, "     ");
                    bShiftKeysBlanked = 1;
                }
            }
        }

        // -------------------------------------------------------------
        // For the caps lock, we show a small dot symbol on our virtual
        // keyboard so the user knows that the caps lock is enabled.
        // -------------------------------------------------------------
        if(tms9901.CapsLock)
        {
            DS_Print((myConfig.overlay == 2) ? 23:2,23,6, "@");
            bCapsKeysBlanked = 0;
        }
        else
        {
            if (!bCapsKeysBlanked)
            {
                bCapsKeysBlanked = 1;
                DS_Print((myConfig.overlay == 2) ? 23:2,23,2, "@");
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

// ------------------------------------------------------------------
// Handles most of the characters that could be in a TI filename...
// it's not exhaustive but good enough for our use. The vast majority
// have letters/numbers and the occasional dash or underscore.
// ------------------------------------------------------------------
void KeyPushFilename(char *filename)
{
    for (int i=0; i<strlen(filename); i++)
    {
        if (filename[i] >= 'A' && filename[i] <= 'Z')       KeyPush(TMS_KEY_A + (filename[i]-'A'));
        else if (filename[i] >= 'a' && filename[i] <= 'z')  KeyPush(TMS_KEY_A + (filename[i]-'a'));
        else if (filename[i] >= '1' && filename[i] <= '9')  KeyPush(TMS_KEY_1 + (filename[i]-'1'));
        else if (filename[i] == '0')                        KeyPush(TMS_KEY_0);
        else if (filename[i] == '/')                        KeyPush(TMS_KEY_SLASH);
        else if (filename[i] == ';')                        KeyPush(TMS_KEY_SEMI);
        else if (filename[i] == '=')                        KeyPush(TMS_KEY_EQUALS);
        else if (filename[i] == '-')                        {KeyPush(TMS_KEY_SHIFT);KeyPush(TMS_KEY_SLASH);}
        else if (filename[i] == '_')                        {KeyPush(TMS_KEY_FUNCTION);KeyPush(TMS_KEY_U);}
        else if (filename[i] == '~')                        {KeyPush(TMS_KEY_FUNCTION);KeyPush(TMS_KEY_W);}
    }
}

// -------------------------------------------------------------------------------
// Simple little utlity function to show the user the first 32 files on a disk
// and allow them to select one to put into the keyboard paste buffer. This makes
// it easier for the user (especially on the little DS handheld) to paste in
// a filename or DSK.filename (useful for Adventure, Tunnels of Doom, etc).
// -------------------------------------------------------------------------------
void ShowDiskListing(void)
{
    // Clear the screen...
    for (u8 i=0; i<20; i++)
    {
        DS_Print(1,3+i,6, "                                ");
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
    DS_Print(5,4,6,      "=== DISK CONTENTS ===");
    DS_Print(1,23,6,     "PRESS A TO PUT IN PASTE BUFFER");

    if (Disk[disk_drive_select].isMounted)
    {
        // -------------------------------------------------------
        // First find all files and store them into our array...
        // This will update dsk_num_files and dsk_listing[]
        // -------------------------------------------------------
        disk_get_file_listing(disk_drive_select);

        // -----------------------------------------
        // Now display the files for the user...
        // -----------------------------------------
        u16 key = 0;
        u8 last_sel = 255;
        u8 sel = 0;
        while (key != KEY_A)
        {
            key = keysCurrent();
            if (key != 0) while (key == keysCurrent()) {WAITVBL;} // wait for release
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
                    if (i < (MAX_FILES_PER_DSK/2)) DS_Print(5, 6+i, (i==sel)?2:0, tmpBuf);
                    else DS_Print(18, 6+(i-(MAX_FILES_PER_DSK/2)), (i==sel)?2:0, tmpBuf);
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
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
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
    sprintf(tmpBuf, " BACKUP  DSK%d ", disk_drive_select+1); DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  tmpBuf);  disk_menu_items++;
    DS_Print(8,8+disk_menu_items,(sel==disk_menu_items)?2:0,  " EXIT    MENU ");  disk_menu_items++;

    if (Disk[disk_drive_select].isMounted)
    {
        u16 numSectors = (Disk[disk_drive_select].image[0x0A] << 8) | Disk[disk_drive_select].image[0x0B];
        u16 usedSectors = 0;
        for (u16 i=0; i<numSectors/8; i++)
        {
            usedSectors += __builtin_popcount(Disk[disk_drive_select].image[0x38+i]);
        }

        sprintf(tmpBuf, "DSK%d MOUNTED %s/%s %3dKB", disk_drive_select+1, (Disk[disk_drive_select].image[0x12] == 2 ? "DS":"SS"), (Disk[disk_drive_select].image[0x13] == 2 ? "DD":"SD"), (numSectors*256)/1024);
        DS_Print(16-(strlen(tmpBuf)/2),8+disk_menu_items+1,(sel==disk_menu_items)?2:0,tmpBuf);
        sprintf(tmpBuf, "(%dKB USED - %dKB FREE)", (usedSectors*256)/1024, (((numSectors-usedSectors)*256)+1023)/1024);
        DS_Print(16-(strlen(tmpBuf)/2),9+disk_menu_items+1,(sel==disk_menu_items)?2:0,tmpBuf);

        u8 col=0;
        if (strlen(Disk[disk_drive_select].filename) < 32)
        {
            strncpy(tmpBuf, Disk[disk_drive_select].filename, 32);
            tmpBuf[31] = 0;
        }
        else
        {
            strncpy(&tmpBuf[128], Disk[disk_drive_select].filename, 25);
            tmpBuf[25] = 0;
            sprintf(tmpBuf, "%s...dsk", &tmpBuf[128]);
        }
        if (strlen(tmpBuf) < 32) col=16-(strlen(tmpBuf)/2);
        if (strlen(tmpBuf) & 1) col--;
        DS_Print(col,9+disk_menu_items+3,(sel==disk_menu_items)?2:0,tmpBuf);
    }
    else
    {
        DS_Print(1,9+disk_menu_items+1,(sel==disk_menu_items)?2:0,"      DISK NOT MOUNTED       ");
    }

    DS_Print(2,22,0, "A TO SELECT, X SWITCH DRIVES");
}

// -------------------------------------------------------------------------
// Handle Disk mini-menu interface... This is the menu that pops up when
// the user presses the small TI-Logo in the corner of the virtual keyboard.
// -------------------------------------------------------------------------
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
                if (myDskFile[0])  // Was a .DSK picked?
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
            if (menuSelection == 2) // LIST DISK
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
                      DS_Print(10,2,6, "BACKUP DISK");
                      disk_backup_to_sd(disk_drive_select);
                      WAITVBL;WAITVBL;
                      DiskMenuShow(true, menuSelection);
                      DS_Print(10,2,6, "           ");
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

  while (keysCurrent()) WAITVBL; // While any key is pressed...
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
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " QUIT   GAME   ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " HIGH   SCORE  ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " SAVE   STATE  ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " LOAD   STATE  ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " DISK   MENU   ");  mini_menu_items++;
    DS_Print(8,9+mini_menu_items,(sel==mini_menu_items)?2:0,  " EXIT   MENU   ");  mini_menu_items++;
}

// -------------------------------------------------------------------------
// Handle mini-menu interface... Lets the user select things like Save/Load
// State, High Scores and quitting the current game being played.
// --------------------------------------------------------------------------
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
            if      (menuSelection == 0) retVal = META_KEY_QUIT;
            else if (menuSelection == 1) retVal = META_KEY_HIGHSCORE;
            else if (menuSelection == 2) retVal = META_KEY_SAVESTATE;
            else if (menuSelection == 3) retVal = META_KEY_LOADSTATE;
            else if (menuSelection == 4) retVal = META_KEY_DISKMENU;
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


// -------------------------------------------------------------------------------------------
// A minimal debugger with just a few keys at the bottom... Enough to let us run most stuff.
// The debugger has a few different 'pages' that we can display - including some basic memory
// dumps that will show contents of various memory ranges. We don't show everything - this
// isn't meant to be a full-fledged debugger... it's mostly used when some cart/program isn't
// running right for the emulator and it can help shed light on why that might be... If you 
// want a full-featured debugger, try Classic99 which has some great features for developers.
// -------------------------------------------------------------------------------------------
u8 CheckDebugerInput(u16 iTy, u16 iTx)
{
    if ((iTy >= 155) && (iTy <= 192))       // Row 5 (SPACE BAR row)
    {
        if      ((iTx >=  1)  && (iTx < 23))   {tms9901.Keyboard[TMS_KEY_1]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 23)  && (iTx < 44))   {tms9901.Keyboard[TMS_KEY_2]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 44)  && (iTx < 65))   {tms9901.Keyboard[TMS_KEY_3]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 65)  && (iTx < 86))   {tms9901.Keyboard[TMS_KEY_A]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 86)  && (iTx < 107))  {tms9901.Keyboard[TMS_KEY_S]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 107) && (iTx < 127))  {tms9901.Keyboard[TMS_KEY_D]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 127) && (iTx < 149))  return META_KEY_DEBUG_NEXT;
        else if ((iTx >= 149) && (iTx < 199))  {tms9901.Keyboard[TMS_KEY_SPACE]=1;   if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 199) && (iTx < 221))  return MiniMenu();
        else if ((iTx >= 221) && (iTx < 255))  {tms9901.Keyboard[TMS_KEY_ENTER]=1;  if (!bKeyClick) bKeyClick=1;}
    }
    if ((iTy >= 20) && (iTy <= 155))       // Debugger Area
    {
        if (iTx > 128) {if (mem_debug < 16) mem_debug++;}
        else {if (mem_debug > 0) mem_debug--;}
        mmEffect(SFX_KEYCLICK);
        WAITVBL;WAITVBL;
    }

    return META_KEY_NONE;
}

// ------------------------------------------------------------------------------------------------
// Here we've already determined that a touch event has taken place on the DS so we are going to
// look up (row-by-row) what key was pressed. The routine will set the appopriate bit in the
// Keyboard[] array to let the emulation know that a key has been pressed. Special attention
// is made for the 'sticky' special keys like Function, Control and Shift.
// ------------------------------------------------------------------------------------------------
u8 CheckKeyboardInput(u16 iTy, u16 iTx)
{
    if (bShowDebug) return CheckDebugerInput(iTy, iTx);

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

    // ----------------------------------------------------------------
    // This is only set to 1 if a normal (non meta) key is pressed...
    // ----------------------------------------------------------------
    if (bKeyClick == 1)
    {
        mmEffect(SFX_KEYCLICK);  // Play short key click for feedback...
        bKeyClick = 2;           // No more click - one was enough

        if (handling_meta) handling_meta = 4;   // This will force the meta key to deactivate as soon as nothing is touched on the virtual keyboard screen
    }

    return META_KEY_NONE;
}

// -------------------------------------------------------------------------------------------------------------
// The Alpha-Numeric Keyboard has a slightly different layout with some Text-Adventure macros across the top...
// -------------------------------------------------------------------------------------------------------------
u8 CheckKeyboardInput_alpha(u16 iTy, u16 iTx)
{
    if (bShowDebug) return CheckDebugerInput(iTy, iTx);

    // --------------------------------------------------------------------------
    // Test the touchscreen rendering of the keyboard
    // --------------------------------------------------------------------------
    if ((iTy >= 8) && (iTy < 42))        // Row 1 (macro row)
    {
        if (key_push_read == key_push_write) // Only process if we have nothing in the macro key queue
        {
            if      ((iTx >= 1)   && (iTx < 53))   {KeyPush(TMS_KEY_T);KeyPush(TMS_KEY_A);KeyPush(TMS_KEY_K);KeyPush(TMS_KEY_E); KeyPush(TMS_KEY_SPACE); if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 53)  && (iTx < 103))  {KeyPush(TMS_KEY_D);KeyPush(TMS_KEY_R);KeyPush(TMS_KEY_O);KeyPush(TMS_KEY_P); KeyPush(TMS_KEY_SPACE); if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 103) && (iTx < 153))  {KeyPush(TMS_KEY_L);KeyPush(TMS_KEY_O);KeyPush(TMS_KEY_O);KeyPush(TMS_KEY_K); KeyPush(TMS_KEY_SPACE); if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 153) && (iTx < 203))  {KeyPush(TMS_KEY_E);KeyPush(TMS_KEY_X);KeyPush(TMS_KEY_A);KeyPush(TMS_KEY_M); KeyPush(TMS_KEY_I);KeyPush(TMS_KEY_N);KeyPush(TMS_KEY_E); KeyPush(TMS_KEY_SPACE); if (!bKeyClick) bKeyClick=1;}
            else if ((iTx >= 203) && (iTx < 254))  {KeyPush(TMS_KEY_O);KeyPush(TMS_KEY_P);KeyPush(TMS_KEY_E);KeyPush(TMS_KEY_N); KeyPush(TMS_KEY_SPACE); if (!bKeyClick) bKeyClick=1;}
            WAITVBL;
        }
    }
    else if ((iTy >= 42) && (iTy < 81))        // Row 2 (QWERTY row)
    {
        if      ((iTx >= 1)   && (iTx < 28))   {tms9901.Keyboard[TMS_KEY_Q]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 28)  && (iTx < 53))   {tms9901.Keyboard[TMS_KEY_W]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 53)  && (iTx < 78))   {tms9901.Keyboard[TMS_KEY_E]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 78)  && (iTx < 103))  {tms9901.Keyboard[TMS_KEY_R]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 103) && (iTx < 128))  {tms9901.Keyboard[TMS_KEY_T]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 128) && (iTx < 153))  {tms9901.Keyboard[TMS_KEY_Y]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 153) && (iTx < 178))  {tms9901.Keyboard[TMS_KEY_U]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 178) && (iTx < 203))  {tms9901.Keyboard[TMS_KEY_I]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 203) && (iTx < 228))  {tms9901.Keyboard[TMS_KEY_O]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 228) && (iTx < 254))  {tms9901.Keyboard[TMS_KEY_P]=1;      if (!bKeyClick) bKeyClick=1;}
    }
    else if ((iTy >= 81) && (iTy < 120))       // Row 3 (ASDF row)
    {
        if      ((iTx >= 1)   && (iTx < 28))   {tms9901.Keyboard[TMS_KEY_A]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 28)  && (iTx < 53))   {tms9901.Keyboard[TMS_KEY_S]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 53)  && (iTx < 78))   {tms9901.Keyboard[TMS_KEY_D]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 78)  && (iTx < 103))  {tms9901.Keyboard[TMS_KEY_F]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 103) && (iTx < 128))  {tms9901.Keyboard[TMS_KEY_G]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 128) && (iTx < 153))  {tms9901.Keyboard[TMS_KEY_H]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 153) && (iTx < 178))  {tms9901.Keyboard[TMS_KEY_J]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 178) && (iTx < 203))  {tms9901.Keyboard[TMS_KEY_K]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 203) && (iTx < 228))  {tms9901.Keyboard[TMS_KEY_L]=1;      if (!bKeyClick) bKeyClick=1;}
        if (key_push_read == key_push_write) // Only process if we have nothing in the macro key queue
        {
            if ((iTx >= 228) && (iTx < 254))  {KeyPush(TMS_KEY_FUNCTION);KeyPush(TMS_KEY_S); if (!bKeyClick) bKeyClick=1;}
            WAITVBL;WAITVBL;
        }
    }
    else if ((iTy >= 120) && (iTy < 159))       // Row 4 (ZXCV row)
    {
        if      ((iTx >= 1)   && (iTx < 28))   {tms9901.Keyboard[TMS_KEY_Z]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 28)  && (iTx < 53))   {tms9901.Keyboard[TMS_KEY_X]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 53)  && (iTx < 78))   {tms9901.Keyboard[TMS_KEY_C]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 78)  && (iTx < 103))  {tms9901.Keyboard[TMS_KEY_V]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 103) && (iTx < 128))  {tms9901.Keyboard[TMS_KEY_B]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 128) && (iTx < 153))  {tms9901.Keyboard[TMS_KEY_N]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 153) && (iTx < 178))  {tms9901.Keyboard[TMS_KEY_M]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 178) && (iTx < 203))  {tms9901.Keyboard[TMS_KEY_PERIOD]=1; if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 203) && (iTx < 254))  {tms9901.Keyboard[TMS_KEY_ENTER]=1;  if (!bKeyClick) bKeyClick=1;}
    }
    else if ((iTy >= 159) && (iTy <= 192))       // Row 5 (SPACE BAR row)
    {
             if ((iTx >= 0)   && (iTx < 21))   {tms9901.Keyboard[TMS_KEY_1]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 21)  && (iTx < 40))   {tms9901.Keyboard[TMS_KEY_2]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 40)  && (iTx < 59))   {tms9901.Keyboard[TMS_KEY_3]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 59)  && (iTx < 78))   {tms9901.Keyboard[TMS_KEY_4]=1;      if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 78)  && (iTx < 179))  {tms9901.Keyboard[TMS_KEY_SPACE]=1;  if (!bKeyClick) bKeyClick=1;}
        else if ((iTx >= 179) && (iTx < 201))  return META_KEY_ALPHALOCK;
        else if ((iTx >= 201) && (iTx < 221))  return MiniMenu();
        else if ((iTx >= 221) && (iTx < 255))  DiskMenu();
    }

    // ----------------------------------------------------------------
    // This is only set to 1 if a normal (non meta) key is pressed...
    // ----------------------------------------------------------------
    if (bKeyClick == 1)
    {
        mmEffect(SFX_KEYCLICK);  // Play short key click for feedback...
        bKeyClick = 2;           // No more click - one was enough

        if (handling_meta) handling_meta = 4;   // This will force the meta key to deactivate as soon as nothing is touched on the virtual keyboard screen
    }

    return META_KEY_NONE;
}

// ---------------------------------------------------------------------------
// A streamlined routine to display the frame counter in the upper left.
// ---------------------------------------------------------------------------
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


// -------------------------------------------------------------------
// Our mini-debugger shows various VDP, CPU and SN sound chip values
// as well as some of the more relevant bits of under-the-hood
// values. This is useful when trying to find bugs in emulation.
// There is also the ability to dump memory into a sort of grid
// to allow inspection of VDP and core memories... The user can
// pull up the internal debugger if they load any cartridge using
// the X button instead of the normal A button.
// -------------------------------------------------------------------
char *VDP_Mode_Str[] = {"G1","G2","MC","BT","TX","--","HB","--"};

void ds99_clear_debugger(void)
{
    for (u8 idx=0; idx<19; idx++)
    {
        DS_Print(0,1+idx,6,"                                ");
    }
    WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
}

extern u8 *fake_heap_end;     // current heap start
extern u8 *fake_heap_start;   // current heap end

u8* getHeapStart() {return fake_heap_start;}
u8* getHeapEnd()   {return (u8*)sbrk(0);}
u8* getHeapLimit() {return fake_heap_end;}

int getMemUsed() { // returns the amount of used memory in bytes
   struct mallinfo mi = mallinfo();
   return mi.uordblks;
}

int getMemFree() { // returns the amount of free memory in bytes
   struct mallinfo mi = mallinfo();
   return mi.fordblks + (getHeapLimit() - getHeapEnd());
}
void __attribute__ ((noinline)) ds99_show_debugger(void)
{
    u8 idx=0;

    if (debug_screen == 0) // Show first page of debug info
    {
        for (idx=0; idx < (tms9900.accurateEmuFlags ? 12:16); idx++)
        {
            sprintf(tmpBuf, "%-7u %04X", debug[idx], debug[idx]&0x0000FFFF);
            DS_Print(20,1+idx,6,tmpBuf);
        }
        idx++;
        if (tms9900.accurateEmuFlags)
        {
            extern u32 idle_counter;
            sprintf(tmpBuf, "TimerSt %04X", tms9901.TimerStart);    DS_Print(20,idx++,6,tmpBuf);
            sprintf(tmpBuf, "TimerCo %04X", tms9901.TimerCounter);  DS_Print(20,idx++,6,tmpBuf);
            sprintf(tmpBuf, "IdleCo  %04X", idle_counter & 0xFFFF); DS_Print(20,idx++,6,tmpBuf);
            idx++;
        }
        sprintf(tmpBuf, "ILOP: %c %04X", (tms9900.illegalOPs ? 'Y':'N'), tms9900.lastIllegalOP);
        DS_Print(20,idx++,6,tmpBuf);
        if (tms9900.illegalOPs)
        {
            sprintf(tmpBuf, "ILOP: %6d", tms9900.illegalOPs);
            DS_Print(20,idx,6,tmpBuf);
        }

        // Sound Register debug
        idx = 1;
        sprintf(tmpBuf, "SN0 %04X %04X %04X", snti99.ch0Frq, snti99.ch0Reg, snti99.ch0Att);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "SN1 %04X %04X %04X", snti99.ch1Frq, snti99.ch1Reg, snti99.ch1Att);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "SN2 %04X %04X %04X", snti99.ch2Frq, snti99.ch2Reg, snti99.ch2Att);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "NOI %04X %04X %04X", snti99.ch3Frq, snti99.ch3Reg, snti99.ch3Att);
        DS_Print(0,idx++,6,tmpBuf);
        idx++;

        // Video Chip (VDP) debug
        sprintf(tmpBuf, "VDP %02X %02X %02X %02X %2s", VDP[0], VDP[1], VDP[2], VDP[3], VDP_Mode_Str[TMS9918_Mode]);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "VDP %02X %02X %02X %02X %-3s", VDP[4], VDP[5], VDP[6], VDP[7], (TMS9918_VRAMMask == 0xFFF) ? "4K":"16K");
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "VDP AD=%04X  ST=%02X", VAddr, VDPStatus);
        DS_Print(0,idx++,6,tmpBuf);
        idx++;

        // CPU debug
        sprintf(tmpBuf, "CPU.PC     %04X", tms9900.PC);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "CPU.WP     %04X", tms9900.WP);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "CPU.ST     %04X", tms9900.ST);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "CPU.GR     %04X", tms9900.gromAddress);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "CPU.OP     %04X", tms9900.currentOp);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "CPU.IR     %04X", tms9900.idleReq);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "CPU.Bank   %08X", tms9900.bankOffset);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "CPU.Cycl %10u", tms9900.cycles);
        DS_Print(0,idx++,6,tmpBuf);
        idx++;
        sprintf(tmpBuf, "SAMS %02X %02X %02X %02X %02X %02X %02X %02X H%02X", theSAMS.bankMapSAMS[2], theSAMS.bankMapSAMS[3],
                theSAMS.bankMapSAMS[0xA], theSAMS.bankMapSAMS[0xB], theSAMS.bankMapSAMS[0xC], theSAMS.bankMapSAMS[0xD], theSAMS.bankMapSAMS[0xE], theSAMS.bankMapSAMS[0xF], sams_highwater_bank);
        DS_Print(0,idx++,6,tmpBuf);
        if ((vusCptVBL & 3) == 0) sams_highwater_bank = 0;  // Reset this periodically (every 4 seconds).
    }
    else if (debug_screen == 1)   // Show 2nd page of debug info
    {
        extern u32 file_size;
        idx = 1;
        sprintf(tmpBuf, "FILE SIZE: %-9d", file_size);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "FILE  CRC: %08X", file_crc);
        DS_Print(0,idx++,6,tmpBuf);
        struct mallinfo mi = mallinfo();
        sprintf(tmpBuf, "HEAP USED: %-9d", mi.uordblks);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "FREE MEM:  %-9d", getMemFree());
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "RPK PCB:   %d [%s]", cart_layout.pcb, rpk_get_pcb_name());
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "RPK ROMS:  %d", cart_layout.num_roms);
        DS_Print(0,idx++,6,tmpBuf);
        sprintf(tmpBuf, "RPK SOCK:  %d", cart_layout.num_sockets);
        DS_Print(0,idx++,6,tmpBuf);
    }
    else if (debug_screen == 2) // Show VDP Memory
    {
        idx = 1;
        DS_Print(0,idx++,6, "VDP MEMORY DUMP");
        idx++;
        for (u16 i=0x0000+(0x80*mem_debug); i<0x0080+(0x80*(mem_debug)); i+=8)
        {
            sprintf(tmpBuf, "%04X: %02X %02X %02X %02X %02X %02X %02X %02X", i, pVDPVidMem[i+0], pVDPVidMem[i+1], pVDPVidMem[i+2], pVDPVidMem[i+3], pVDPVidMem[i+4], pVDPVidMem[i+5], pVDPVidMem[i+6], pVDPVidMem[i+7]);
            DS_Print(0,idx++,6,tmpBuf);
        }
    }
    else if (debug_screen == 3) // Show Main Memory
    {
        idx = 1;
        DS_Print(0,idx++,6, "CPU MEMORY DUMP");
        idx++;
        for (u16 i=0x8000+(0x80*mem_debug); i<0x8080+(0x80*(mem_debug)); i+=8)
        {
            sprintf(tmpBuf, "%04X: %02X %02X %02X %02X %02X %02X %02X %02X", i, MemCPU[i+0], MemCPU[i+1], MemCPU[i+2], MemCPU[i+3], MemCPU[i+4], MemCPU[i+5], MemCPU[i+6], MemCPU[i+7]);
            DS_Print(0,idx++,6,tmpBuf);
        }
    }
    else if (debug_screen == 4) // Show Extended Memory
    {
        idx = 1;
        DS_Print(0,idx++,6, "EXTENDED MEMORY DUMP");
        idx++;
        for (u16 i=0xA000+(0x80*mem_debug); i<0xA080+(0x80*(mem_debug)); i+=8)
        {
            sprintf(tmpBuf, "%04X: %02X %02X %02X %02X %02X %02X %02X %02X", i, MemCPU[i+0], MemCPU[i+1], MemCPU[i+2], MemCPU[i+3], MemCPU[i+4], MemCPU[i+5], MemCPU[i+6], MemCPU[i+7]);
            DS_Print(0,idx++,6,tmpBuf);
        }
    }
}

// -------------------------------------------------------------------
// Check if we have a touch-screen event and map it to the right
// input for emulation use.  This doesn't need to be in fast memory.
// -------------------------------------------------------------------
u8 handle_touch_input(void)
{
    touchPosition touch;
    touchRead(&touch);
    u16 iTx = touch.px;
    u16 iTy = touch.py;

    // ---------------------------------
    // Check the Keyboard for input...
    // ---------------------------------
    u8 meta = (myConfig.overlay == 2) ? CheckKeyboardInput_alpha(iTy, iTx) : CheckKeyboardInput(iTy, iTx);

    switch (meta)
    {
        case META_KEY_QUIT:
        {
          //  Stop sound
          SoundPause();

          //  Ask for verification
          if  (showMessage("DO YOU REALLY WANT TO","QUIT THE CURRENT GAME ?") == ID_SHM_YES)
          {
              memset((u8*)0x06000000, 0x00, 0x20000);    // Reset main screen VRAM to 0x00 to clear any potential display garbage on way out
              return 1;
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

        case META_KEY_DEBUG_NEXT:
            debug_screen = (debug_screen+1) % 5; // We have several debug screens we can flip-flop between.
            mmEffect(SFX_KEYCLICK);
            WAITVBL;
            ds99_clear_debugger();
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

    return 0;
}

// --------------------------------------------------------------------------------
// If there are any keys in the key buffer, we process them here one at a time...
// This is used if we are pasting a disk filename or the Adventure macros. It
// does not need to be bullet-fast so it can stay outside our fast-memory core.
// --------------------------------------------------------------------------------
void ProcessKeyBuffer(void)
{
    // Shift is always followed by the key it is modifying...
    if ((u8)key_push[key_push_read] == TMS_KEY_SHIFT)
    {
      tms9901.Keyboard[(u8)key_push[key_push_read]]=1;
      key_push_read = (key_push_read+1) & 0x1F;
    }
    if ((u8)key_push[key_push_read] == TMS_KEY_FUNCTION)
    {
      tms9901.Keyboard[(u8)key_push[key_push_read]]=1;
      key_push_read = (key_push_read+1) & 0x1F;
    }
    tms9901.Keyboard[(u8)key_push[key_push_read]]=1;
    key_push_read = (key_push_read+1) & 0x1F;

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
                    ds99_show_debugger();
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

      // -----------------------------------------------------------------------
      // We don't want to inject keypresses into the system too fast (nor 
      // too slow) so we dampen down the key buffer processing here... 
      // Arbitrary but this seems to work for any programs I've tried so far...
      // -----------------------------------------------------------------------
      if (!(++dampen & 3))
      {
          if (key_push_read != key_push_write) // There are keys to process in the Push buffer
          {
              ProcessKeyBuffer();
          }
      }

      // Check if the DS touch-screen has been pressed...
      if (keysCurrent() & KEY_TOUCH)
      {
          if (handle_touch_input()) return;
      }
      else  // No Screen Touch...
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

          bKeyClick = 0;
      }

      // ------------------------------------------------------------------------
      //  Test NDS keypresses (ABXY, L/R) and map to corresponding TI99 keys
      // ------------------------------------------------------------------------
      nds_key  = keysCurrent();

      if ((nds_key & KEY_L) && (nds_key & KEY_R) && (nds_key & KEY_X)) // Check for the LCD swap key sequence
      {
            lcdSwap();
            WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
      }
      else // Check for the screen snapshot key sequence...
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
                      case JOY1_UP:
                        if (myConfig.dpadDiagonal) {tms9901.Keyboard[TMS_KEY_JOY1_UP]=1; tms9901.Keyboard[TMS_KEY_JOY1_RIGHT]=1;}
                        else tms9901.Keyboard[TMS_KEY_JOY1_UP]=1;
                        break;

                      case JOY1_DOWN:
                        if (myConfig.dpadDiagonal) {tms9901.Keyboard[TMS_KEY_JOY1_DOWN]=1; tms9901.Keyboard[TMS_KEY_JOY1_LEFT]=1;}
                        else tms9901.Keyboard[TMS_KEY_JOY1_DOWN]=1;
                        break;

                      case JOY1_LEFT:
                        if (myConfig.dpadDiagonal) {tms9901.Keyboard[TMS_KEY_JOY1_LEFT]=1; tms9901.Keyboard[TMS_KEY_JOY1_UP]=1;}
                        else tms9901.Keyboard[TMS_KEY_JOY1_LEFT]=1;
                        break;

                      case JOY1_RIGHT:
                        if (myConfig.dpadDiagonal) {tms9901.Keyboard[TMS_KEY_JOY1_RIGHT]=1; tms9901.Keyboard[TMS_KEY_JOY1_DOWN]=1;}
                        else tms9901.Keyboard[TMS_KEY_JOY1_RIGHT]=1;
                        break;

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
  videoSetMode(MODE_0_2D  | DISPLAY_BG0_ACTIVE);
  videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE  | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D_LAYOUT | DISPLAY_SPR_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG);
  vramSetBankC(VRAM_C_SUB_BG);

  //  Stop blending effect of intro
  REG_BLDCNT=0; REG_BLDCNT_SUB=0; REG_BLDY=0; REG_BLDY_SUB=0;

  //  Render the top screen
  bg0 = bgInit(0, BgType_Text8bpp,  BgSize_T_256x256, 31,0);
  bgSetPriority(bg0,1);
  decompress(splashTiles,  bgGetGfxPtr(bg0), LZ77Vram);
  decompress(splashMap,  (void*) bgGetMapPtr(bg0), LZ77Vram);
  dmaCopy((void*) splashPal,(void*)  BG_PALETTE,256*2);

  //  Render the bottom screen (we have different keyboards/overlays)
  DrawCleanBackground();

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

    if (bShowDebug)
    {
        decompress(debugTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
        decompress(debugMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
        dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
        dmaCopy((void*) debugPal,(void*) BG_PALETTE_SUB,256*2);

        unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
        dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
    }
    else
    {
        if (myConfig.overlay == 0)  // TI99 3D Keyboard
        {
            decompress(ds99kbdTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
            decompress(ds99kbdMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
            dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
            dmaCopy((void*) ds99kbdPal,(void*) BG_PALETTE_SUB,256*2);

            unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
            dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
        }
        else if (myConfig.overlay == 2)  // Alpha Keyboard
        {
            decompress(alphakbdTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
            decompress(alphakbdMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
            dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
            dmaCopy((void*) alphakbdPal,(void*) BG_PALETTE_SUB,256*2);

            unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
            dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
        }
        else // Must be TI99 Flat Keyboard
        {
            decompress(ti99kbdTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
            decompress(ti99kbdMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
            dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
            dmaCopy((void*) ti99kbdPal,(void*) BG_PALETTE_SUB,256*2);

            unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
            dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
        }
    }

    DisplayStatusLine(true);
}

// -------------------------------------------------------------------
// Init the system to get ready for playing the currently loaded game.
// -------------------------------------------------------------------
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
// Only used for basic timing of the splash screen
// -------------------------------------------------------------
void irqVBlank(void)
{
 // Manage time
  vusCptVBL++;
  if (speech_dampen) speech_dampen--;
}

// -------------------------------------------------------------------------------------------
// We need VRAM_A and VRAM_C for emulation use but otherwise we can utilize these other areas
// of VRAM for CPU use - gives us a bit more memory and it's reasonably fast 16-bit to boot!
// -------------------------------------------------------------------------------------------
void StealVideoRAM(void)
{
    vramSetBankB(VRAM_B_LCD);   // Not using this for video but 128K of faster RAM always useful! Mapped at 0x06820000 (used for opcode tables)
    vramSetBankD(VRAM_D_LCD);   // Not using this for video but 128K of faster RAM always useful! Mapped at 0x06860000 (used for opcode tables)
    vramSetBankE(VRAM_E_LCD);   // Not using this for video but 64K of faster RAM always useful!  Mapped at 0x06880000 (unused)
    vramSetBankF(VRAM_F_LCD);   // Not using this for video but 16K of faster RAM always useful!  Mapped at 0x06890000 (unused)
    vramSetBankG(VRAM_G_LCD);   // Not using this for video but 16K of faster RAM always useful!  Mapped at 0x06894000 (unused)
    vramSetBankH(VRAM_H_LCD);   // Not using this for video but 32K of faster RAM always useful!  Mapped at 0x06898000 (cache the system console GROM 24K - 8K free at beginning)
    vramSetBankI(VRAM_I_LCD);   // Not using this for video but 16K of faster RAM always useful!  Mapped at 0x068A0000 (cache the 8K TI-99/4a main BIOS and the 8K TI Disk DSR)
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

    // ------------------------------------------------
    // Grab chunks of the DS/DSi Video RAM for CPU use
    // ------------------------------------------------
    StealVideoRAM();

    // ------------------------------------------------------------------------
    // Find the main console ROM which is 8K in size... load this into cache.
    // ------------------------------------------------------------------------
    inFile1 = fopen("/roms/bios/994aROM.bin", "rb");
    if (!inFile1) inFile1 = fopen("/roms/ti99/994aROM.bin", "rb");
    if (!inFile1) inFile1 = fopen("994aROM.bin", "rb");
    if (inFile1)
    {
        fread(fileBuf, 1, 0x2000, inFile1);
        memcpy(MAIN_BIOS, fileBuf, 0x2000);
    }

    // ------------------------------------------------------------------------
    // Find the main system GROMs which is 24K in size... load this into cache.
    // ------------------------------------------------------------------------
    inFile2 = fopen("/roms/bios/994aGROM.bin", "rb");
    if (!inFile2) inFile2 = fopen("/roms/ti99/994aGROM.bin", "rb");
    if (!inFile2) inFile2 = fopen("994aGROM.bin", "rb");
    if (inFile2)
    {
        // The shared buffer is not used yet - so we steal it for the 24K file read
        fread(SharedMemBuffer, 1, 0x6000, inFile2);
        memcpy(MAIN_GROM, SharedMemBuffer, 0x6000);
    }

    // ----------------------------------------------------------------
    // We consider the TI BIOS requirement fulfilled only if we found
    // both the main system ROM and the system console GROMs.
    // ----------------------------------------------------------------
    if (inFile1 && inFile2)
    {
        bTIBIOSFound = true;
    }
    else
    {
        bTIBIOSFound = false;
    }
    if (inFile1) fclose(inFile1);
    if (inFile2) fclose(inFile2);

    // ------------------------------------------------------------------------
    // Find the TI Disk Controller ROM (optional) and load this into cache
    // ------------------------------------------------------------------------
    inFile1 = fopen("/roms/bios/994aDISK.bin", "rb");
    if (!inFile1) inFile1 = fopen("/roms/ti99/994aDISK.bin", "rb");
    if (!inFile1) inFile1 = fopen("994aDISK.bin", "rb");
    if (!inFile1) inFile1 = fopen("/roms/bios/disk.bin", "rb");
    if (!inFile1) inFile1 = fopen("/roms/ti99/disk.bin", "rb");
    if (!inFile1) inFile1 = fopen("disk.bin", "rb");
    if (inFile1)
    {
        fread(fileBuf, 1, 0x2000, inFile1);
        memcpy(DISK_DSR, fileBuf, 0x2000);
    }
    else
    {
        memset(DISK_DSR, 0xFF, 0x2000);
    }

    if (inFile1) bTIDISKFound = true; else bTIDISKFound = false;
    if (inFile1) fclose(inFile1);
}

// --------------------------------------------------------------------
// We call this function once at startup to allocate and setup memory.
// This is done only once when the emulator is launched (not on every
// cart load) which ensures that we do not have to contend with any 
// sort of memory leaking or garbage collection. 
// --------------------------------------------------------------------
static void StartupMemoryAllocation(void)
{
    SharedMemBuffer = malloc(768*1024);   // This is mostly used by the older DS machines for CART and SAMS storage but we steal a bit for DSK3 on the DSi
    memset(SharedMemBuffer, 0x00, 768*1024);
    
    if (isDSiMode())
    {
        theSAMS.numBanks = 256;                         // 256 * 4K = 1024K for DSi
        MemSAMS = malloc((theSAMS.numBanks) * 0x1000);  // Allocate the SAMS memory

        MAX_CART_SIZE = (u32)(8192 * 1024);             // 8MB (8192K) Max Cart for DSi
        MemCART = malloc(MAX_CART_SIZE);                // Allocate the Cartridge Buffer
    }
    else
    {
        // ---------------------------------------------------------------------------------------
        // For the DS-Lite/Phat we are using a shared memory pool to help us conserve resources.
        // Basically we have 768K to play with - normally 512K of that is cart space but if the
        // SAMS is enabled, we drop the Cartridge max size to 256K and enable a 512K SAMS which
        // still allows us to play virtually any SAMS game including Realms of Antiquity!
        // ---------------------------------------------------------------------------------------
        theSAMS.numBanks = 128;                         // 128 * 4K = 512K for DS-Lite/Phat
        MemSAMS = SharedMemBuffer + (256 * 1024);       // Set the SAMS memory area. When SAMS is enabled for the DS-Lite/Phat, cart size will drop to 256K

        MAX_CART_SIZE = (u32)(512 * 1024);              // 512K Max Cart for DS-Lite/Phat (may get adjusted down to 256K if SAMS enabled)
        MemCART = SharedMemBuffer;                      // Set the Cartridge Buffer
    }
}


// ------------------------------------------------------
// Program entry point - check if an argument has been
// passed in on the command line (probably from TWL++)
// ------------------------------------------------------
int main(int argc, char **argv)
{
  //  Init sound
  consoleDemoInit();

  if  (!fatInitDefault()) {
     iprintf("Unable to initialize libfat!\n");
     return -1;
  }

  // Allocate some memory for the SAMS and Cart Buffers - done one time only at startup
  StartupMemoryAllocation();

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

  // -----------------------------------------------------------------
  // Handle command line argument... mostly for TWL++ to launch game.
  // -----------------------------------------------------------------
  if (argc > 1)
  {
      //  We want to start in the directory where the file is being launched...
      if  (strchr(argv[1], '/') != NULL)
      {
          strcpy(tmpBuf,  argv[1]);
          char  *ptr = &tmpBuf[strlen(tmpBuf)-1];
          while (*ptr !=  '/') ptr--;
          ptr++;
          strcpy(initial_file,  ptr);
          *ptr=0;
          chdir(tmpBuf);
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
        DS_Print(2,10,0,"ERROR: TI994a BIOS NOT FOUND");
        DS_Print(2,12,0,"ERROR: CANT RUN WITHOUT BIOS");
        DS_Print(3,12,0,"ERROR: SEE README.MD FILE");
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

// -------------------------------------------------------------------------------------------
// We use this routine as a bit of a cheat for handling TI99 speech. Most of the games that
// use speech use the 0x60 'Speak External' command to handle speech. We look for this byte
// and the three bytes after it as a sort of digital signature (a fingerprint if you will)
// to determine what phrase is trying to be spoken. Rather than real emulation, we simply
// look for the digital signature of the prhase and then ask our DS MAXMOD sound system to
// play the speech sample. It's not perfect, but requires very little CPU power and will
// render speech fairly well even on the oldest DS handheld systems.
//
// To ensure that the digital signature is unique, we can use a 'prev-sig' to help identify
// some sound samples. This is only needed in a few cases where the primary digital signature
// might conflict across two games. It is possible that future games that utilize speech could
// hit one of these signatures - but that's a problem for another day.
//
// The delay below can be used to ensure no other speech sample (including the one that will
// be played) is interrupted by another speech sample. A real TI-09/4a Speech Synth will
// queue them up but that's not how it works with MaxMod and the SFX sound effect handling.
// -------------------------------------------------------------------------------------------
u32 speechData32 __attribute__((section(".dtcm"))) = 0;

static const SpeechTable_t SpeechTable[] =
{
  // ----------------------------------------------------
  // Digital-Sig    Prev-Sig    Delay   SFX to play
  // ----------------------------------------------------
    {0x60108058,    0x00000000,     0,  SFX_PRESS_FIRE},            // Parsec - Press Fire to Begin
    {0x604D7399,    0x00000000,     0,  SFX_DESTROYED},             // Parsec - Alien Destroyed
    {0x604BCBD6,    0x00000000,     0,  SFX_GOODSHOT},              // Parsec - Good Shot
    {0x60C6703A,    0x00000000,     0,  SFX_NICESHOOTING},          // Parsec - Nice Shooting
    {0x6046E3B2,    0x00000000,     0,  SFX_GREATSHOT},             // Parsec - Great Shot
    {0x60E00025,    0x00000000,     0,  SFX_LASERONTARGET},         // Parsec - Laser on Target
    {0x6040066E,    0x00000000,     0,  SFX_ATTACKING},             // Parsec - Aliens Attacking
    {0x6043F77E,    0x00000000,     0,  SFX_ADVANCING},             // Parsec - Aliens Advancing
    {0x600E0821,    0x00000000,     0,  SFX_ASTEROID},              // Parsec - Beware the Asteroid Belt
    {0x60090846,    0x00000000,     0,  SFX_COUNTDOWN},             // Parsec - Countdown Begins
    {0x6071A647,    0x00000000,     0,  SFX_5},                     // Parsec - Five
    {0x600A48A5,    0x00000000,     0,  SFX_4},                     // Parsec - Four
    {0x60080826,    0x00000000,     0,  SFX_3},                     // Parsec - Three
    {0x600D586E,    0x00000000,     0,  SFX_2},                     // Parsec - Two
    {0x604967BB,    0x00000000,     0,  SFX_1},                     // Parsec - One
    {0x6030B4EA,    0x00000000,     0,  SFX_ADVANCELEVEL},          // Parsec - Advance to Next Level
    {0x604B8B41,    0x00000000,     0,  SFX_EXTRASHIP},             // Parsec - Extra Ship
    {0x6049E3B3,    0x00000000,     0,  SFX_WARNINGFUEL},           // Parsec - Warning Fuel Low
    {0x6006F8DA,    0x00000000,     0,  SFX_SORRYFUEL},             // Parsec - Fuel Empty

    {0x60CEE4F9,    0x00000000,     0,  SFX_BEWARE},                // Alpiner - Beware Falling Objects
    {0x604AD7AA,    0x00000000,     0,  SFX_LOOKOUT},               // Alpiner - Lookout!
    {0x604E6839,    0x00000000,     0,  SFX_WATCHOUT},              // Alpiner - Watch Out!
    {0x60A26A54,    0x00000000,    35,  SFX_YUCK},                  // Alpiner - Yuck!
    {0x60AADB82,    0x00000000,    35,  SFX_YIKES},                 // Alpiner - Yikes!
    {0x602530B1,    0x00000000,    35,  SFX_UH},                    // Alpiner - Uh!
    {0x60A5F222,    0x00000000,    35,  SFX_OOPS},                  // Alpiner - Oops!
    {0x602BCE6E,    0x00000000,    35,  SFX_OUCH},                  // Alpiner - Ouch!
    {0x60A574FE,    0x00000000,    35,  SFX_OHNO},                  // Alpiner - Oh No!
    {0x60293565,    0x00000000,     0,  SFX_OOOOH},                 // Alpiner - Oooooh (falling)
    {0x60A375FE,    0x00000000,     0,  SFX_ONWARD},                // Alpiner - Onwards and Upwards
    {0x6008485C,    0x00000000,     0,  SFX_GOAGAIN},               // Alpiner - Go Again
    {0x6042A369,    0x00000000,     0,  SFX_WALKEDINTO},            // Alpiner - Walked Right Into That
    {0x6050B498,    0x00000000,     0,  SFX_SPORT},                 // Alpiner - Great Move Sport
    {0x600248BE,    0x00000000,     0,  SFX_THANITLOOKS},           // Alpiner - Harder than it Looks, Isn't It?
    {0x60C130D8,    0x00000000,     0,  SFX_DUCK},                  // Alpiner - You forgot to duck
    {0x60108002,    0x00000000,     0,  SFX_MEANTO},                // Alpiner - Did you mean to do that?
    {0x600828D2,    0x00000000,     0,  SFX_HELP},                  // Alpiner - Heeeeeeelp!
    {0x600A08C1,    0x00000000,     0,  SFX_ANYKEYTOGO},            // Alpiner - Press any key to go on
    {0x600EA856,    0x00000000,     0,  SFX_GETTINGTIRED},          // Alpiner - Getting Tired Already?
    {0x60E08096,    0x00000000,     0,  SFX_GAMEOVER},              // Alpiner - Game Over
    {0x60C8B1CE,    0x00000000,     0,  SFX_BETTERLUCK},            // Alpiner - Better luck next time

    {0x60C2E42E,    0x00000000,     0,  SFX_LASEROVERHEAT},         // Moonmine - Laser Overheated
    {0x604C91D2,    0x00000000,     0,  SFX_MONSTERDAMAGEDSHIP},    // Moonmine - Monster Damaged Ship
    {0x6006A83A,    0x00000000,     0,  SFX_UNKNOWNOBJECT},         // Moonmine - Unknown Object Ahead
    {0x604CFFBE,    0x00000000,     0,  SFX_ZYGAPPROACH},           // Moonmine - Zygonaut Approaching
    {0x6004B0C7,    0x00000000,     0,  SFX_CREWLOST},              // Moonmine - Crew Member Lost
    {0x6044D55C,    0x00000000,     0,  SFX_MONSTERDESTROYED},      // Moonmine - Monster Destroyed
    {0x604E1DB1,    0x00000000,     0,  SFX_GOODSHOTCAPTAIN},       // Moonmine - Good Shot Captain
    {0x602EADC1,    0x00000000,     0,  SFX_ZYGNEVERGET},           // Moonmine - You'll Never Get Me!
    {0x604377A9,    0x00000000,     0,  SFX_ZYGHAHA},               // Moonmine - Hahahahahah!
    {0x60CA64B7,    0x00000000,     0,  SFX_WATERAHEAD},            // Moonmine - Water Ahead
    {0x604691D2,    0x00000000,     0,  SFX_MONSTERATTACKEDCREW},   // Moonmine - Monster Attacked Crew
    {0x604AE227,    0x00000000,     0,  SFX_WAYTOGOCAP},            // Moonmine - Way to go Captain
    {0x60418FC6,    0x00000000,     0,  SFX_MOONADVANCE},           // Moonmine - Advanced to next level
    {0x6006A851,    0x00000000,     0,  SFX_CONTINUEGAME},          // Moonmine - Continue game, Captain
    {0x600A70C7,    0x00000000,     0,  SFX_COOLANTLOW},            // Moonmine - Coolant Low
    {0x60C508AD,    0x00000000,     0,  SFX_OUTOFWATER},            // Moonmine - Out of Water
    {0x6002889A,    0x00000000,     0,  SFX_CONGRATSCAP},           // Moonmine - Congratulations Captain
    {0x60492BC9,    0x00000000,     0,  SFX_EXTRACREW},             // Moonmine - Extra Crew Member
    {0x604955A9,    0x00000000,     0,  SFX_BONUSPOINTS},           // Moonmine - Bonus Points Gained

    {0x60CCAEBE,    0x00000000,     0,  SFX_BIG_GETYOU},            // Bigfoot - I'll get you Bigfoot!
    {0x6044A6B5,    0x00000000,     0,  SFX_BIG_FALL},              // Bigfoot - Falling noise
    {0x60C97263,    0x00000000,     0,  SFX_BIG_ROAR},              // Bigfoot - Bigfoot Roar
    {0x608272B9,    0x00000000,     0,  SFX_BIG_CAW},               // Bigfoot - Bird Screech
    {0x60100230,    0x6042173C,     0,  SFX_BIG_GOTYOU},            // Bigfoot - Now I've got you!

    {0x60261765,    0x00000000,     0,  SFX_WELCOMEABOARD},         // Star Trek - Welcome Aboard, Captain
    {0x60ABC96A,    0x00000000,     0,  SFX_AVOIDMINES},            // Star Trek - Avoid Mines, Captain
    {0x600AF022,    0x00000000,     0,  SFX_DAMAGEREPAIRED},        // Star Trek - Damage Repaired, Captain
    {0x60ADC8DE,    0x00000000,     0,  SFX_EXCELLENTMANUVER},      // Star Trek - Excellent Maneuvering, Captain

    {0x60AAA061,    0x00000000,     0,  SFX_WHEREFLY_SF},           // Superfly - Where's the Fly?
    {0x60A6704A,    0x00000000,     0,  SFX_NEVERTRUST_SF},         // Superfly - Never Trust a Worm
    {0x608E54A7,    0x00000000,     0,  SFX_OHNO_SF},               // Superfly - Oh No!
    {0x602D4E8E,    0x00000000,     0,  SFX_GETIT_SF},              // Superfly - Get It!
    {0x60000318,    0x60A9942F,     0,  SFX_OHYES_SF},              // Superfly - Ohhhh Yeeesss! (digital sig also used by Fathom)

    {0x60430D39,    0x00000000,     0,  SFX_AVOIDPOSTS},            // Buck Rogers - Avoid Electron Posts, Buck
    {0x604953D6,    0x00000000,     0,  SFX_WATCHHOPPERS},          // Buck Rogers - Watch the Hoppers
    {0x60431999,    0x00000000,     0,  SFX_ALIENSAPPROACH},        // Buck Rogers - Aliens are Approaching

    {0x6004702D,    0x00000000,     0,  SFX_GOFORTH},               // Fathom - Go Forth
    {0x6000030F,    0x00000000,     0,  SFX_SEAHORSE},              // Fathom - Find Another Seahorse
    {0x6050D416,    0x00000000,     0,  SFX_VOLCANICBLAST},         // Fathom - Beware the Volcanic Blast
    {0x60438BD1,    0x00000000,     0,  SFX_ATTENDENERGY},          // Fathom - Attend to your Energy Mortal
    {0x6074E3B2,    0x00000000,     0,  SFX_FREEME},                // Fathom - Free Me Mortal
    {0x604E711A,    0x00000000,     0,  SFX_EVILOCTOPUS},           // Fathom - Beware The Evil Octopus!
    {0x604C1D3A,    0x00000000,     0,  SFX_TRIUMPTHED},            // Fathom - You have Triumphed!

    {0x60222763,    0x00000000,     0,  SFX_WELCOMEKOREA},          // MASH - Welcome to Korea
    {0x60A74EA2,    0x00000000,     0,  SFX_ATTENTIONALL},          // MASH - Attention all Personnel
    {0x600AB8F7,    0x00000000,     0,  SFX_CHOPPERS},              // MASH - Choppers
    {0x60550000,    0x00000000,     0,  SFX_OVERHERE},              // MASH - Over Here
    {0x60249286,    0x00000000,     0,  SFX_MEDIC},                 // MASH - Medic
    {0x60A631D5,    0x00000000,     0,  SFX_REPORTSURGERY},         // MASH - Report to Surgery
    {0x60274F66,    0x00000000,     0,  SFX_IGIVEUP},               // MASH - I Give Up
    {0x60AB0FEE,    0x00000000,     0,  SFX_BUTTERFINGERS},         // MASH - Butterfingers
    {0x600A403D,    0x00000000,     0,  SFX_SURGERYOOPS},           // MASH - Oops!
    {0x6004282A,    0x00000000,     0,  SFX_NEXT},                  // MASH - Next...
    {0x60EADE8D,    0x00000000,     0,  SFX_YOUREOKAY},             // MASH - You're Okay
    {0x60AA761A,    0x00000000,     0,  SFX_YOUREOKAY},             // MASH - You're Okay (give second possibility)
    {0x60E06263,    0x00000000,     0,  SFX_THANKSDOC},             // MASH - Thanks Doc

    {0x600A20B2,    0x00000000,     0,  SFX_FINDTHEBOMB},           // Sewermania - Dave, Find the Bomb!
    {0x6001B0DE,    0x00000000,     0,  SFX_FOUNDTHEBOMB},          // Sewermania - Found the Bomb, Boss
    {0x600EC8CC,    0x00000000,   120,  SFX_DEFUSEBOMB},            // Sewermania - Defused the Bomb, Boss
    {0x602150A9,    0x00000000,     0,  SFX_ANALIGATOR},            // Sewermania - Oh No! An Alligator!

    {0x6008102A,    0x00000000,     0,  SFX_PATIENTREADY},          // Microsurgeon - Patient is ready doctor
    {0x600608A3,    0x00000000,     0,  SFX_DRLAVINE},              // Microsurgeon - Paging Dr. Lavine
    {0x60D61BB4,    0x00000000,     0,  SFX_CONDITIONCRITICAL},     // Microsurgeon - Patient in Critical Condition
    {0x60AB9AAD,    0x00000000,   120,  SFX_POWERLOW},              // Microsurgeon - Probe Energy Low
    {0x600C0821,    0x6068291F,     0,  SFX_ENTERINGHEART},         // Microsurgeon - Probe Entering Heart
    {0x602A60E9,    0x6068291F,     0,  SFX_ENTERINGLUNG},          // Microsurgeon - Probe Entering Lungs
    {0x60068828,    0x6068291F,     0,  SFX_ENTERINGKIDNEY},        // Microsurgeon - Probe Entering Kidney
    {0x6008F8D1,    0x6068291F,     0,  SFX_ENTERINGSPLEEN},        // Microsurgeon - Probe Entering Spleen
    {0x60C491CA,    0x00000000,    20,  SFX_VIRUS},                 // Microsurgeon - Virus!

    {0x60EDAE42,    0x00000000,   180,  SFX_BZK_KILLED},            // Borzork - Got the Humanoid!
    {0x60054C82,    0x00000000,   180,  SFX_BZK_CHICKEN},           // Borzork - Chicken Fight Like a Robot
    {0x60A5B0DA,    0x00000000,   180,  SFX_BZK_ESCAPE},            // Borzork - The Humanoid must not Escape
    {0x60258F42,    0x00000000,   120,  SFX_BZK_ATTACKHUMANOID},    // Borzork - Attack the Humanoid
    {0x60280327,    0x00000000,   120,  SFX_BZK_INTRUDERALERT},     // Borzork - Intruder Alert! Intruder Alert! (start screen)
    {0x60C3AF06,    0x60A38F3E,   120,  SFX_BZK_INTRUDERALERT},     // Borzork - Intruder Alert! Intruder Alert! (otto)

    {0x00000000,    0x00000000,     0,  255},                       // End of table...
};


void WriteSpeechData(u8 data)
{
    static u32 prev_speechData32 = 0x00000000;
    if (myConfig.sounddriver == 1) return; // Check if the Speech Module is disabled...

    // Reading Address 0 from the Speech ROM
    if ((speechData32 == 0x40404040) && (data == 0x10))
    {
        readSpeech = 0xAA;                      // The first byte of the actual speech ROM is 0xAA and this will be enough to fool many games into thinking a Speech Synth module is attached.
    } else readSpeech = SPEECH_SENTINAL_VAL;    // This indicates just normal status read...

    speechData32 = (speechData32 << 8) | data;

    if ((speechData32 & 0xFF000000) == 0x60000000) // Speak External
    {
        if (speech_dampen) return;  // We are in a delay period... do not speak anything

        u16 idx=0;
        // ----------------------------------------------------------------------------------------
        // Look for this digital signature in our table map... if found, we play the speech sample
        // ----------------------------------------------------------------------------------------
        while (SpeechTable[idx].signature != 0x00000000)
        {
            if (SpeechTable[idx].signature == speechData32)
            {
                if ((SpeechTable[idx].prev_signature == 0x00000000) || (SpeechTable[idx].prev_signature == prev_speechData32))
                {
                    mmEffect(SpeechTable[idx].sfx);                 // Play the speech (.wav) sound effect now. If another happens to be playing, both will be heard (I think we have like 5 channels)
                    speech_dampen = SpeechTable[idx].delay_after;   // The delay for "no more speech until" is in 1/60th of a second units ticked by the DS irqVBlank() handler.
                    break;
                }
            }
            idx++;
        }
        prev_speechData32 = speechData32;

#if 0 // Enable this to debug speech and add additional sound effects by signature
        // Output the digital signature into a file... we can use this to try and pick out other phrases for other games
        sprintf(tmpBuf, "%5d", vusCptVBL);
        DS_Print(0,0,6, tmpBuf);
        FILE *fp = fopen("994a_speech.txt", "a+");
        fprintf(fp, "%5d: 0x%08X  [%3d]\n", vusCptVBL, (unsigned int)speechData32, SpeechTable[idx].sfx);
        fclose(fp);
#endif
    }
}

void _putchar(char character) {};   // Not used but needed to link printf()

// End of file
