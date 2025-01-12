// Borrowed from Godemode9i from Rocket Robz
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fat.h>
#include <dirent.h>
#include <unistd.h>

#include "screenshot.h"
#include "printf.h"
#include "DS99.h"
#include "DS99_utils.h"

#pragma GCC push_options
#pragma GCC optimize ("Os")


void write16(void *address, u16 value) {

    u8* first = (u8*)address;
    u8* second = first + 1;

    *first = value & 0xff;
    *second = value >> 8;
}

void write32(void *address, u32 value) {

    u8* first = (u8*)address;
    u8* second = first + 1;
    u8* third = first + 2;
    u8* fourth = first + 3;

    *first = value & 0xff;
    *second = (value >> 8) & 0xff;
    *third = (value >> 16) & 0xff;
    *fourth = (value >> 24) & 0xff;
}

bool screenshotbmp(const char* filename) {
    FILE *file = fopen(filename, "wb");

    if(!file)  return false;

    REG_DISPCAPCNT = DCAP_BANK(DCAP_BANK_VRAM_D) | DCAP_SIZE(DCAP_SIZE_256x192) | DCAP_ENABLE;
    while(REG_DISPCAPCNT & DCAP_ENABLE);

    // ---------------------------------------------------------------
    // This is 100K off the back end of the shared memory pool.
    // The DSi doesn't use this pool otherwise so it's fine and
    // for the DS-Lite/Phat, the only possibility is if they are
    // using a SAMS enabled game and they require the memory
    // in the last 100K area... in which case this will not end well.
    // ---------------------------------------------------------------
    u8 *temp = (u8*)(SharedMemBuffer + (668*1024));

    if(!temp) {
        fclose(file);
        return false;
    }

    HEADER *header= (HEADER*)temp;
    INFOHEADER *infoheader = (INFOHEADER*)(temp + sizeof(HEADER));

    write16(&header->type, 0x4D42);
    write32(&header->size, 256 * 192 * 2 + sizeof(INFOHEADER) + sizeof(HEADER));
    write32(&header->reserved1, 0);
    write32(&header->reserved2, 0);
    write32(&header->offset, sizeof(INFOHEADER) + sizeof(HEADER));

    write32(&infoheader->size, sizeof(INFOHEADER));
    write32(&infoheader->width, 256);
    write32(&infoheader->height, 192);
    write16(&infoheader->planes, 1);
    write16(&infoheader->bits, 16);
    write32(&infoheader->compression, 3);
    write32(&infoheader->imagesize, 256 * 192 * 2);
    write32(&infoheader->xresolution, 2835);
    write32(&infoheader->yresolution, 2835);
    write32(&infoheader->ncolours, 0);
    write32(&infoheader->importantcolours, 0);
    write32(&infoheader->redBitmask, 0xF800);
    write32(&infoheader->greenBitmask, 0x07E0);
    write32(&infoheader->blueBitmask, 0x001F);
    write32(&infoheader->reserved, 0);

    u16 *ptr = (u16*)(temp + sizeof(HEADER) + sizeof(INFOHEADER));
    for(int y = 0; y < 192; y++) {
        for(int x = 0; x < 256; x++) {
            u16 color = VRAM_D[256 * 191 - y * 256 + x];
            *(ptr++) = ((color >> 10) & 0x1F) | (color & (0x1F << 5)) << 1 | ((color & 0x1F) << 11);
        }
    }

    DC_FlushAll();
    fwrite(temp, 1, 256 * 192 * 2 + sizeof(INFOHEADER) + sizeof(HEADER), file);
    fclose(file);

    // Since we over-wrote the VRAM_D[] which has our opcode table, we have to rebuild it...
    extern void TMS9900_buildopcodes(void);
    TMS9900_buildopcodes();
    return true;
}


char snapPath[64];
void screenshot(void)
{
    time_t unixTime = time(NULL);
    struct tm* timeStruct = gmtime((const time_t *)&unixTime);

    DS_Print(12,0,0,"SNAPSHOT");
    sprintf(snapPath, "SNAP-%02d-%02d-%04d-%02d-%02d-%02d.bmp", timeStruct->tm_mday, timeStruct->tm_mon+1, timeStruct->tm_year+1900, timeStruct->tm_hour, timeStruct->tm_min, timeStruct->tm_sec);

    // Take top screenshot
    screenshotbmp(snapPath);
    
    WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
    DS_Print(12,0,0,"        ");
}

#pragma GCC pop_options
// End of file
