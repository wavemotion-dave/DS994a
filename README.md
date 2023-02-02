# DS99/4a
DS99 - A Texas Instruments TI99/4a Emulator for the DS/DSi

To run requires the TI BIOS ROMS in the /roms/bios directory.
See BIOS files further down for the ones you need.


Features :
-----------------------
* Cart loads up to 512K Banked (+40K of GROM beyond the 24K Console GROM)
* 32K RAM Expansion
* SAMS 512K memory Expansion for the DS and 1MB for the DSi (and above)
* Save and Load State
* High score saving for up to 10 scores per game
* Full mapping of any of the 12 DS keys to any combination of TI Joysticks/Keyboard
* Disk Support for DSK1 and DSK2 up to 360K each using the standard TI Disk Controller (you need 994adisk.bin - see BIOS files below)
* No Speech Synth yet (but games requiring the Speech Synth will run/play - just no voice)

Copyright :
-----------------------
DS99/4a is Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)

This program is made up from a number of constituent bits and pieces of code from
other emulators plus a lot of original code to glue it all together and run it on the DS. 
As such, the following copyrights apply:

The SN76496 sound core is from FluBBa and is used with permission. 

The TI9918A code came from Marat Fayzullin (ColEM) and retains Marat's original copyright
statement. Do not use any of that code without trying to contact Marat.

The TI9900 CPU core is mainly my own with some significant bits of scaffolding from 
Mike Brent's Classic99 emulator - especially in the status bits handling and some 
of the more tricky opcode algorithms so they run correctly. This is used with permission 
and with great thanks!

For the rest of the code: as long as there is no commercial use (i.e. no profit is made),
copying and distribution of this emulator, it's source code and associated readme files, with 
or without modification, are permitted in any medium without royalty provided this copyright
notice is used and wavemotion-dave is thanked profusely.

The DS99 emulator is offered as-is, without any warranty.

Credits :
-----------------------
* Thanks to Alekmaul who provided the original Coleco emulator framework of which this is based.
* Thanks to Flubba for the SN76496 sound core.
* Thanks to Marat Fayzullin (ColEM) for the TI991A video driver.
* Thanks to Mike Brent for Classic99 and letting me use some of the disk and CPU core code.
* Thanks to Pete Eberlein and some great ideas and a bit of code from his upcoming BuLWiP emulator. 
* Thanks to ti99iuc over on AtariAge for the DS99/4a Logo
* Thanks to the 99ers over on the AtariAge site for their help in grokking memory layouts and banking schemes.


Installation :
-----------------------
* You will need the two console BIOS files as described below. Place both .bin BIOS files into /roms/bios (you can just make the directory on your SD card).
* You will also need the emulator itself. You can get this from the GitHub page - the only file you need here is DS994a.nds (the .nds is a Nintendo executable file). You can put this anywhere - most people put the .nds file into the root of the SD card.
* If you want to play disk based games (Adventure, Tunnels of Doom, etc) you will need 994adisk.bin (often just named disk.bin but you need to rename it and put it into /roms/bios).
* You will need games to play... right now the emulator supports C/D/G files and '8' non-inverted files. Basically just try loading a file to see if it works... the ROMs should have a .bin extension. 
* Recommend you put your game ROMs into /roms/ti99 as the emulator will default to that directory. That's where the cool kids keep them.
* Recommend you put any disks needed (.dsk files) into the same directory as your ROMs until I can get a better file manager worked out.

BIOS Files :
-----------------------
Here are the BIOS file CRC32 hashes I'm using with all of my testing - seek these out if you want maximum compatibility. Place these into /roms/bios
```
* db8f33e5	994aROM.bin (8K)
* af5c2449	994aGROM.bin (24K)
* 8f7df93f	994aDISK.bin (8K) - this is needed only if you want .DSK support
```

Known Issues :
-----------------------
* The 512K megademo8.bin will play (and is really cool!) but fails when it gets to the scanline stuff near the end.
* Borzork has audio squealing during gameplay. Cause unknown.
* Congo Bongo requires RAM mirrors enabled so it doesn't glitch on Level 2. Use Options to enable.

File Types Supported :
-----------------------
DS994a supports the following file types:
* Files ending in C/D/G files also known as 'mixed mode' files. If there is a 'D' file, it must be exactly 8K. C is the main binary and G is the GROM binary. If a C/D/G file is detected, only the C (or G if it's GROM-only) will be shown in the file listing.
* Files ending in 3 or 9 are considered "inverted" files and the banks will be swapped appopriately.
* All other files are considered '8' files which is a non-inverted banking up to 512K. 


How do I play Adventure or Tunnels of Doom? :
-----------------------
Some of the most well-remembered games on the TI99/4a were the Scott Adam's Adventure Games and my personal favorite: Tunnels of Doom (and early dungeon crawler that probably has more to do with my wanting a TI99/4a emulator on my DS than anything). To play these games requires that the 'database' for each game is loaded from somewhere. Back in the day, we loaded via Cassette. But for DS99/4a we don't have analog cassette support so you'll have to load from Disk. To do so:
* You will need the 994aDISK.bin as mentioned in the BIOS files section above.
* You will need the cartridge files for the game (AdventureG.bin or TunnelsG.bin or similar)
* You will need the disk (.DSK) image for the game you want to play
* Load the cartridge file normally... then use the Cassette icon to mount the .DSK
* For Adventure you load the game by typing "DSK1.PIRATE" (for Pirate's adventure... or whatever name/game you want... use LIST DISK to see what's on the disk as there were more than a dozen Scott Adams adventure games).
* For Scott Adams adventures you should enable the ALPHA LOCK key in Game Options (lowercase filenames won't load).
* For Tunnels you load the game by selecting 2) DISK and then type in the name of the quest you want to play (usually named just "QUEST" without the DSK1 part).

Memory/System Configurations :
-----------------------
By default each game is configured to run on a 32K expanded system. The SAMS support must be enabled on a per-game basis (you can also set to have the SAMS support enabled globally). Be aware - the SAMS handling does require a more accurate emulation core and will slow down the emulation by almost 20%.  That's generally fine for the DSi and above but my recommendation is to use the default 32K expanded system for virtually all games and only enable the SAMS support for the few things that need it.

Blend Mode (DSi) :
-----------------------
DS99/4a supports a "blend mode" which I borrowed from my scheme on StellaDS. In this mode, 
two frames are blended together - this is really useful when playing games like Space Fury or Galaxian 
where the bullets on screen are only 1 pixel wide and the DSi LCD just doesn't hold onto the pixels 
long enough to be visible. These games were designed to run on an old tube TV with phosphor which 
decays slowly so your eye will see slight traces as the image fades. This emulates that (crudely).
On the DSi using this new mode renders those games really bright and visible.

The DSi XL/LL has a slower refresh on the LCD and it more closely approximates the old tube TVs... 
so blend mode is not needed for the XL/LL models.

However! Using blend mode comes at at 15% CPU cost!! The DSi can handle it... the DS-LITE/PHAT might
struggle a bit on more complicated games. 

So my recommendation is as follows:
* DSi non XL/LL - use Blend Mode for the games that benefit from it (e.g. TI Invaders).
* DSi XL/LL - don't bother... the XL/LL screen decay is slower and games look great as-is.
* DS-LITE/PHAT - you can try it but the framerate might drop below 60 on some games.

To enable this new blend mode, pick your game and go into the "Game Options" sub-menu and turn it on.

Versions :
-----------------------
V0.9: 02-Feb-2023 by wavemotion-dave
* Added DSK3 support. All three drives support up to 360K disks. 
* Improved emulation on SAMS memory such that Realms of Antiquity will run.
* Persist SAMS memory on Save State/Load using RLE compression.
* Other cleanups as time permitted.

V0.8: 29-Jan-2023 by wavemotion-dave
* Added DSK2 support and both drives now handle up to 360K disks. 
* Improved default key mapping so X='1' and Y='2' allowing faster game startup.
* Inverted .bin files now supported with the filenames ending in '3' or '9'.
* Added ability to paste in DSKx.FILENAME from the Disk menu
* Other cleanups as time permitted.

V0.7: 24-Jan-2023 by wavemotion-dave
* New TI99 Keyboard Layout - select in Global Options or on a Per-Game basis.
* Better DSK vs ROM handling - remembers last directory for each.
* SAMS 1MB enabled for DSi and above.
* Fixed save/load state for Mini-Men, SuperCart and MBX Carts (only SAMS save/load is non-functional right now).
* Other cleanups as time permitted.

V0.6: 21-Jan-2023 by wavemotion-dave
* Re-write of the CPU core. It's 20% smaller and 20% faster.
* Added ability to list disk contents so you can see what programs are on it.
* Added write-backing of .DSK files. When they chagne the file is written out in the background.
* Added SAMS 512K expanded memory support - must be enabled in OPTIONS on a per-game basis.
* Added new cartridge types to support SuperCart 8K, MiniMemory 4K and Milton Bradley MBX carts (with and without special 1K RAM).
* New splash screen... new logo icon... a fresh start!

V0.5: 12-Jan-2023 by wavemotion-dave
* Streamlined save and load of save state so it's only 2 blocks of SD card (64K).  Old saves will not work with version 0.5 - so finish your games before you upgrade.
* Fixed banking so that Skyway8.bin (and probably others) will load properly.
* Improved CPU and memory reset so games are less glitchy when starting up after having just played another game.
* Another frame of performance squeezed out of the CPU core.

V0.4: 9-Jan-2023 by wavemotion-dave
* Added .DSK support for 90K and 180K disks (read and write both work but write doesn't yet persist back to SD card). Use the Cassette icon in the lower left of the keyboard to mount disks.
* Added High Score support for 10 scores per game. Use the new 'HI' button on the main keyboard.
* Fixed loading of 8K "banked" games (Tuthankam, Mancala, etc).
* Fixed right-side border rendering for "TEXT MODE" games (Adventure, Zork, etc).
* Added new option to mirror console RAM. This is more accurate but slows down the emualation. Congo Bongo needs this to render level 2+ properly.
* Games that were hanging looking for Speech Synthesis module no longer freeze up (no voice yet but you can play them).
* Keyboard is now more robust - presses are always properly 'clicked' and it's less glitchy. Key repeat now works.
* Other minor cleanups as time permitted.

V0.3: 7-Jan-2023 by wavemotion-dave
* Improved speed 8% across the board
* Fixed spirte cut-off at top of screen
* Fixed X in control mapping so that it toggles P1 vs P2
* Added PAL (along with default NTSC) support
* New splash screen and logo
* Other minor cleanups as time permitted

V0.2: 6-Jan-2023 by wavemotion-dave
* Increased bankswitch support to 512k
* Fixed IDLE instruction so games like Slymoids works
* Improved speed by 10-15% across the board... 
* Other minor cleanups as time permitted

V0.1: 5-Jan-2023 by wavemotion-dave
* It works! Almost...
