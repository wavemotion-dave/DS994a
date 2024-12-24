// See disk.c for info 

#include "DS99.h"
#include "DS99_utils.h"

#define MAX_DSK_SIZE    (360*1024)     // 360K maximum .dsk size

// Three disks supported.. that should be fine for just about anything
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
    u8   *image;                // The (up to) 360K disk image in sector format (V9T9 sector dump format)
}  Disk_t;

extern Disk_t Disk[MAX_DSKS];

#define MAX_FILES_PER_DSK           32          // We allow 32 files shown per disk... that's enough for our purposes and it's what we can show on screen comfortably
#define MAX_DSK_FILE_LEN            12          // And room for 12 characters per file (really 10 plus NULL but we keep it on an even-byte boundary)

extern char dsk_listing[MAX_FILES_PER_DSK][MAX_DSK_FILE_LEN];   // We store the disk listing here...
extern u8   dsk_num_files;                                      // And we found this many files...

extern u8 TICC_REG[8];
extern u8 TICC_DIR;
extern u8 bDiskDeviceInstalled;
extern u8 diskSideSelected;
extern u8 driveSelected;
extern u8 motorOn;

extern void disk_init(void);
extern u8   ReadTICCRegister(u16 address);
extern void WriteTICCRegister(u16 address, u8 val);
extern void HandleTICCSector(void);
extern void disk_cru_write(u16 address, u8 data);
extern u8   disk_cru_read(u16 address);
extern void disk_mount(u8 drive, char *path, char *filename);
extern void disk_unmount(u8 drive);
extern void disk_read_from_sd(u8 drive);
extern void disk_write_to_sd(u8 disk);
extern void disk_backup_to_sd(u8 disk);
extern void disk_get_file_listing(u8 drive);

// End of file
