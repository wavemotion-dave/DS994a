// See disk.c for info 

#include "DS99.h"
#include "DS99_utils.h"

#define MAX_DSK_SIZE    (360*1024)     // 360K maximum .dsk size

// For now just two disks supported.. that should be fine for just about anything
enum
{
    DSK1=0,
    DSK2,
    DSK3,
    MAX_DSKS
};


typedef struct
{
    u8   isMounted;             // Is this disk mounted?
    u8   isDirty;               // Does this disk need writing back to the SD card on the NDS?
    u8   driveReadCounter;      // Set to some non-zero value to show 'DISK READ' briefly on screen
    u8   driveWriteCounter;     // Set to some non-zero value to show 'DISK WRITE' briefly on screen (takes priority over READ display)
    char filename[MAX_PATH];    // The name of the .dsk file
    char path[MAX_PATH];        // The directory where the .dsk file was found
    u8   image[MAX_DSK_SIZE];   // The (up to) 360K disk image in sector format (V9T9 sector dump format)
}  _Disk;

extern _Disk Disk[MAX_DSKS];

extern void disk_init(void);
extern u8 ReadTICCRegister(u16 address);
extern void WriteTICCRegister(u16 address, u8 val);
extern void HandleTICCSector(void);
extern void disk_cru_write(u16 address, u8 data);
extern u8 disk_cru_read(u16 address);

extern void disk_mount(u8 drive, char *path, char *filename);
extern void disk_unmount(u8 drive);
extern void disk_read_from_sd(u8 drive);
extern void disk_write_to_sd(u8 disk);

// End of file
