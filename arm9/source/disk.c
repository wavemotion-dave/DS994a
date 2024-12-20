// The original version of this came from Clasic99 (C) Mike Brent 
// who has graciously allowed me to use a bit of this code for
// very simplified DSK support. We utilize the TI99 Disk Controller
// DSR along with support for simple 90K, 180K and 360K raw sector disks.
// This should be enough to load up Scott Adam's Adventure Games 
// and Tunnels of Doom quests.  Despite the heavy similification
// and porting of the code, Mike's original copyright remains below:
//
//
// (C) 2013 Mike Brent aka Tursi aka HarmlessLion.com
// This software is provided AS-IS. No warranty
// express or implied is provided.
//
// This notice defines the entire license for this code.
// All rights not explicity granted here are reserved by the
// author.
//
// You may redistribute this software provided the original
// archive is UNCHANGED and a link back to my web page,
// http://harmlesslion.com, is provided as the author's site.
// It is acceptable to link directly to a subpage at harmlesslion.com
// provided that page offers a URL for that purpose
//
// Source code, if available, is provided for educational purposes
// only. You are welcome to read it, learn from it, mock
// it, and hack it up - for your own use only.
//
// Please contact me before distributing derived works or
// ports so that we may work out terms. I don't mind people
// using my code but it's been outright stolen before. In all
// cases the code must maintain credit to the original author(s).
//
// -COMMERCIAL USE- Contact me first. I didn't make
// any money off it - why should you? ;) If you just learned
// something from this, then go ahead. If you just pinched
// a routine or two, let me know, I'll probably just ask
// for credit. If you want to derive a commercial tool
// or use large portions, we need to talk. ;)
//
// If this, itself, is a derived work from someone else's code,
// then their original copyrights and licenses are left intact
// and in full force.
//
// http://harmlesslion.com - visit the web page for contact info

#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fat.h>
#include <dirent.h>
#include "printf.h"
#include "DS99.h"
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "cpu/tms9918a/tms9918a.h"
#include "disk.h"

u8 TICC_REG[8] = {0,0,0,0,0,0,0,0};
u8 TICC_DIR=0;   // 0 means towards track 0

u8 bDiskDeviceInstalled  = 0;       // DSR installed or not installed... We don't do much with this yet.
u8 diskSideSelected      = 0;       // Side 0 or Side 1
u8 driveSelected         = DSK1;    // We support DSK1, DSK2 and DSK3

Disk_t Disk[MAX_DSKS];              // Contains all the Disk sector data plus some metadata for DSK1, DSK2 and DSK3
u8 Disk1_ImageBuf[MAX_DSK_SIZE];    // Full buffering of 360K
u8 Disk2_ImageBuf[MAX_DSK_SIZE];    // Full buffering of 360K
u8 Disk3_ImageBuf[512];             // First two sectors only for the DS-Lite/Phat but full buffering on DSi (who will use the SharedMemBuffer[])

char backup_filename[MAX_PATH];     // If the user wants, they can backup either of DSK1 or DSK2 (the writeable disks)

#define MAX_DSK_SECTORS     1440    // For all disks we support 1440x256=360K

#define ERR_DEVICEERROR     6       // This is the only error we support. Good enough.

// ------------------------------------------------------
// Start with no disk mounted and all image data clear
// ------------------------------------------------------
void disk_init(void)
{
    memset(Disk, 0x00, sizeof(Disk));
    
    memset(Disk1_ImageBuf, 0x00, MAX_DSK_SIZE);
    memset(Disk2_ImageBuf, 0x00, MAX_DSK_SIZE);
    memset(Disk3_ImageBuf, 0x00, 512);
    
    Disk[DSK1].image = Disk1_ImageBuf;  // Buffered
    Disk[DSK2].image = Disk2_ImageBuf;  // Buffered
    
    if (isDSiMode())
    {
        extern u8 SharedMemBuffer[];
        Disk[DSK3].image = SharedMemBuffer; // For DSi we fully buffer 360K for DSK3. The DSi is otherwise not really using the SharedMemBuffer[]
    }
    else
    {
        Disk[DSK3].image = Disk3_ImageBuf;  // Non-Buffered. This one is read-only. We cache the first two sectors only. Saves us valuable memory on the older handhelds.
    }
}

// ------------------------------------------------------
// I don't know if anyone will read the disk CRU bits
// but we do our best to fill in the required data.
// ------------------------------------------------------
u8 disk_cru_read(u16 address)
{
    switch (address & 0x07)
    {
        case 0:     return 0;
        case 1:     return ((driveSelected == DSK1) ? 1:0);
        case 2:     return ((driveSelected == DSK2) ? 1:0);
        case 3:     return ((driveSelected == DSK3) ? 1:0);
        case 4:     return 0;
        case 5:     return 0;
        case 6:     return 1;
        case 7:     return (diskSideSelected ? 1:0);
    }
    return 0;
}

// ------------------------------------------------------
// The CRU write selects which drive (DSK1, DSK2 or DSK3)
// is currently selected and, quite importantly, also 
// maps the disk DSR into memory or out of memory.
// ------------------------------------------------------
void disk_cru_write(u16 address, u8 data)
{
    switch (address & 0x07)
    {
        case 0:
            bDiskDeviceInstalled = data;
            if (data)
            {
                // The Disk Controller DSR is visible
                memcpy(&MemCPU[0x4000], DISK_DSR, 0x2000);
            }
            else
            {
                // The Disk Controller DSR is not visible
                memset(&MemCPU[0x4000], 0xFF, 0x2000);
            }
            break;
            
        case 4:     // select drive 1
        case 5:     // select drive 2
        case 6:     // select drive 3
            driveSelected = DSK1 + (address & 0x03);    // This will work out to DSK1, DSK2 or DSK3
            break;
            
        case 7: 
            diskSideSelected = (data ? 1:0);
            break;
            
        default:
            break;
    }    
}

u8 ReadTICCRegister(u16 address) 
{
    if (address >= 0x5ff8) return 0xFF;
    
    switch (address & 0xFFFE)
    {
    case 0x5ff0:
        // status register
        TICC_REG[0] = 0x20; // head loaded, ready, not busy, not track 0, no index pulse, etc
        if (TICC_REG[1]==0) 
        {
            TICC_REG[0]|=0x04;
        }
        TICC_REG[0]=~TICC_REG[0];
        return TICC_REG[0];

    case 0x5ff2:
        // track register
        return TICC_REG[1];

    case 0x5ff4:
        // sector register
        return TICC_REG[2];

    case 0x5ff6:
        // data register
        return TICC_REG[3];
    }

    return 0x00;
}

void WriteTICCRegister(u16 address, u8 val) 
{
    if ((address < 0x5ff8) || (address > 0x5fff)) return;
 
    switch (address&0xfffe) 
    {
    case 0x5ff8:
        // command register
        switch (val & 0xe0) 
        {
        case 0x00:
            // restore or seek
            if (val&0x10) {
                // seek to data reg
                TICC_REG[1]=TICC_REG[3];
            } else {
                // seek to track 0
                TICC_REG[1]=0;
            }
            break;

        case 0x20:
            // step
            if (val&0x10)   {   // if update track register
                if (TICC_DIR) {
                    if (TICC_REG[1] < 255) TICC_REG[1]++;
                } else {
                    if (TICC_REG[1] > 0) TICC_REG[1]--;
                }
            }
            break;

        case 0x40:
            // step in
            if (val&0x10)   {   // if update track register
                if (TICC_REG[1] < 255) TICC_REG[1]++;
            }
            break;

        case 0x60:
            // step out
            if (val&0x10)   {   // if update track register
                if (TICC_REG[1] > 0) TICC_REG[1]--;
            }
            break;
        }
        break;

    case 0x5ffA:
        // track register
        TICC_REG[1] = val;
        break;

    case 0x5ffC:
        // sector register
        TICC_REG[2] = val;
        break;

    case 0x5ffE:
        // data register
        TICC_REG[3] = val;
        break;
    }
}

// --------------------------------------------------------------------------
// This is really only used for DSK3 on the older DS-Lite/Phat where the 
// disk is not fully buffered in memory and so we must go out to the actual
// .dsk file and seek/read in the sector.
// --------------------------------------------------------------------------
 static void ReadSector(u8 drive, u16 sector, u8 *buf)
{
    u8 error = true; // Until proven otherwise...
    
    // Change into the last known DSKs directory for this file
    chdir(Disk[drive].path);

    // Make sure the sector being asked for is sensible...
    if (sector < MAX_DSK_SECTORS)
    {
        // Seek to the right sector and read in 256 bytes...
        FILE *infile = fopen(Disk[drive].filename, "rb");
        if (infile)
        {
            if (!fseek(infile, (256*sector), SEEK_SET))
            {
                fread(buf, 1, 256, infile);
                error = false;
            }
            fclose(infile);
        }
    }
    
    // If we had any error, clear the buffer
    if (error)
    {
        memset(buf, 0x00, 256); // Just return zeros... good enough on failure        
    }
}

// ----------------------------------------------------------------------------------------------------------------
// This is where the magic happens.. this ruotine is called to handle a sector and is done cleverly by way of 
// looking at when the PC counter is at >40E8 whcih is the TI disk controller DSR's entry to handle sector
// reads and writes. In this way we can utilize the existing TI disk controller DSR and just handle the actual
// sector read/write. This allows us up to the standard 1600 bits x 256 sectors or 400K of disk space. We limit
// to 360K which is the sort of standard Double-Sided Double-Density drive. If we wanted to go beyond this limit
// we would have to switch to a different disk controller DSR or create our own. This shouldn't be much of a
// problem as virtually anything that has come out on disk for the TI99 will run on a 360K or smaller floppy.
// ----------------------------------------------------------------------------------------------------------------
 void HandleTICCSector(void)
{
    bool success = true;
    extern u8 pVDPVidMem[];
    
    if (driveSelected != 1 && driveSelected != 2  && driveSelected != 3) // We only support DSK1, DSK2 or DSK3
    {
        MemCPU[0x8350] = ERR_DEVICEERROR;  
        tms9900.PC = 0x42a0;                // error 31 (not found)        
    }
    
    // 834A = sector number
    // 834C = drive (1-3)
    // 834D = 0: write, anything else = read
    // 834E = VDP buffer address
    u8  drive        = MemCPU[0x834C];
    u8  isRead       = MemCPU[0x834D];
    u16 sectorNumber = (MemCPU[0x834A]<<8) | MemCPU[0x834B];
    u16 destVDP      = (MemCPU[0x834E]<<8) | MemCPU[0x834F];
    u32 index        = (sectorNumber * 256);
    
    if ((drive == 1) || (drive == 2) || (drive == 3))
    {
        drive = drive-1;    // Zero based for struct array lookup
        
        // --------------------------------------------------
        // Make sure the sector asked for is within reason...
        // --------------------------------------------------
        if (sectorNumber >=  MAX_DSK_SECTORS)
        {
            success = false;
        }
        else
        {
            if (isRead)
            {
                // -----------------------------------------------------------------
                // Move the 256 byte sector from the .DSK image to the VDP memory
                // -----------------------------------------------------------------
                if (!isDSiMode() && (drive == DSK3))
                {
                    // DSK3 is not cached in memory on older DS-Lite/Phat - so read the sector out from the file.
                    ReadSector(drive, sectorNumber, &pVDPVidMem[destVDP]);
                }
                else // Fully buffered - just grab from memory
                {
                    memcpy(&pVDPVidMem[destVDP], &Disk[drive].image[index], 256);
                }
                *((u16*)&MemCPU[0x834A]) = sectorNumber;     // fill in the return data
                Disk[drive].driveReadCounter = 2;            // briefly show that we are reading from the disk
                MemCPU[0x8350] = 0;                          // should still be 0 if no error occurred
            } 
            else  // Must be write
            {
                if (isDSiMode() || (drive != DSK3)) // We only support write-back on DSK1 and DSK2 for DS-Lite/Phat
                {
                    memcpy(&Disk[drive].image[index], &pVDPVidMem[destVDP],256);
                    *((u16*)&MemCPU[0x834A]) = sectorNumber;     // fill in the return data
                    Disk[drive].isDirty = 1;                     // Mark this disk as needing a write-back to SD card
                    Disk[drive].driveWriteCounter = 2;           // And briefly show that we are writing to the disk
                    MemCPU[0x8350] = 0;                          // should still be 0 if no error occurred
                }
                else
                {
                    success = false;
                }
                
            } 
        }
    } else success = false;

    
    if (success)
    {
        tms9900.PC = 0x4676;        // return from read or write (write normally goes through read to verify)    
    }
    else
    {
        MemCPU[0x8350] = ERR_DEVICEERROR; 
        tms9900.PC = 0x42a0;                // error 31 (not found)
    }
}


// --------------------------------------------------------------------------------------------------
// Routines below this comment are all related to reading and writing .DSK files to and from the
// DS Fat file system on the SD Card. We take care to handle .BAK files in case we run into any
// problems with writing the .DSK back to the SD card. Better safe than sorry.
// --------------------------------------------------------------------------------------------------

void disk_mount(u8 drive, char *path, char *filename)
{
    Disk[drive].isMounted = true;
    Disk[drive].isDirty = 0;
    strcpy(Disk[drive].path, path);
    strcpy(Disk[drive].filename, filename);
    sprintf(backup_filename, "%s.bak", Disk[drive].filename);
    
    // ---------------------------------------------------------
    // Check if a backup file exists... if one exists and
    // the normal .DSK is not at least 90K that means something
    // went wrong and we now need to restore the backup...
    // ---------------------------------------------------------
    FILE *tmpFile = fopen(backup_filename, "rb");
    if (tmpFile)
    {
        fclose(tmpFile);
        FILE *tmpFile = fopen(Disk[drive].filename, "rb");
        fseek(tmpFile, 0L, SEEK_END);
        int sz = ftell(tmpFile);
        fclose(tmpFile);
        if (sz < (90*1024))
        {
            remove(Disk[drive].filename);
            rename(backup_filename, Disk[drive].filename);
        }
    }
    
    // ---------------------------------------
    // Now we can read the file as intended...
    // ---------------------------------------
    disk_read_from_sd(drive);
}

void disk_unmount(u8 drive)
{
    if (Disk[drive].isDirty) disk_write_to_sd(drive);
    Disk[drive].isMounted = false;
    if (isDSiMode() || (drive != DSK3))
    {
        memset(Disk[drive].image, 0x00, MAX_DSK_SIZE);
    }
    else // For DS-Lite/Phat we only have a 2 sector buffer
    {
        memset(Disk[drive].image, 0x00, 512);
    }
}

ITCM_CODE void disk_read_from_sd(u8 drive)
{
    // Change into the last known DSKs directory for this file
    chdir(Disk[drive].path);

    FILE *infile = fopen(Disk[drive].filename, "rb");
    if (infile)
    {
        if (isDSiMode() || (drive != DSK3))
        {
            fread(Disk[drive].image, 1, MAX_DSK_SIZE, infile); // For DSK1 and DSK2 we buffer it all. DSi can also buffer DSK3
        }
        else
        {
            fread(Disk[drive].image, 1, 512, infile);   // Just the first two sectors for DSK3
        }
        fclose(infile);
    }
}

ITCM_CODE void disk_write_to_sd(u8 drive)
{
    // Only DSK1 and DSK2 support write-back on DS-Lite/Phat
    if (isDSiMode() || (drive != DSK3))
    {
        // Change into the last known DSKs directory for this file
        chdir(Disk[drive].path);

        sprintf(backup_filename, "%s.bak", Disk[drive].filename);
        remove(backup_filename);    
        rename(Disk[drive].filename, backup_filename);
        FILE *outfile = fopen(Disk[drive].filename, "wb");
        if (outfile)
        {
            u16 numSectors = (Disk[drive].image[0x0A] << 8) | Disk[drive].image[0x0B];
            size_t diskSize = (numSectors*256);
            fwrite((void*)Disk[drive].image, 1, diskSize, outfile);
            fclose(outfile);
        }
        remove(backup_filename);
        Disk[drive].isDirty = 0;
    }
}

void disk_backup_to_sd(u8 drive)
{
    // Only DSK1 and DSK2 support backup on DS-Lite/Phat
    if (isDSiMode() || (drive != DSK3))
    {
        // Change into the last known DSKs directory for this file
        chdir(Disk[drive].path);

        DIR* dir = opendir("bak");
        if (dir) closedir(dir);    // Directory exists... close it out and move on.
        else mkdir("bak", 0777);   // Otherwise create the directory...
        sprintf(backup_filename, "bak/%s", Disk[drive].filename);
        remove(backup_filename);
        FILE *outfile = fopen(backup_filename, "wb");
        if (outfile)
        {
            u16 numSectors = (Disk[drive].image[0x0A] << 8) | Disk[drive].image[0x0B];
            size_t diskSize = (numSectors*256);
            fwrite((void*)Disk[drive].image, 1, diskSize, outfile);
            fclose(outfile);
        }
    }
}

// ----------------------------------------------------------------------
// Utility function to get a list of current files on the mounted disk. 
// DS994a will use this listing to present a list of files to the user
// so they can pick a file and easily past it into the keyboard buffer.
// ----------------------------------------------------------------------
char dsk_listing[MAX_FILES_PER_DSK][12];    // We store the disk listing here...
u8   dsk_num_files = 0;                     // And this is how many files we found (never more than MAX_FILES_PER_DSK)

void disk_get_file_listing(u8 drive)
{
    u16 sectorPtr = 0;
    
    dsk_num_files = 0;
    for (u16 i=0; i<256; i += 2)
    {
        sectorPtr = (Disk[drive].image[256 + (i+0)] << 8) | (Disk[drive].image[256 + (i+1)] << 0);
        if (sectorPtr == 0) break;
        if (!isDSiMode() && (drive == DSK3))
        {
            ReadSector(drive, sectorPtr, fileBuf);
            for (u8 j=0; j<10; j++) 
            {
                dsk_listing[dsk_num_files][j] = fileBuf[j];
            }
        }
        else
        {
            for (u8 j=0; j<10; j++) 
            {
                dsk_listing[dsk_num_files][j] = Disk[drive].image[(256*sectorPtr) + j];
            }
        }
        dsk_listing[dsk_num_files][10] = 0; // Make sure it's NULL terminated
        if (++dsk_num_files >= MAX_FILES_PER_DSK) break;
    }    
}

// End of file
