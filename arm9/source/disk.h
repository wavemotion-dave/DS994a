// See disk.c for info 

extern u8 bDiskDeviceInstalled;
extern u8 bDiskIsMounted;
extern u8 driveReadCounter;
extern u8 driveWriteCounter;

extern u8 ReadTICCRegister(u16 address);
extern void WriteTICCRegister(u16 address, u8 val);
extern void HandleTICCSector(void);
extern void disk_cru_write(u16 address, u8 data);

