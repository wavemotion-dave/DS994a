// See disk.c for info 

extern UINT8 bDiskDeviceInstalled;
extern UINT8 bDiskIsMounted;
extern UINT8 driveReadCounter;
extern UINT8 driveWriteCounter;

extern UINT8 ReadTICCRegister(UINT16 address);
extern void WriteTICCRegister(UINT16 address, UINT8 val);
extern void HandleTICCSector(void);
extern void disk_cru_write(UINT16 address, UINT8 data);

