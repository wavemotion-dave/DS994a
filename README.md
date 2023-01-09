# DS99/4a
DS99 - A Texas Instruments TI99/4a Emulator for the DS/DSi

To run requires the TI BIOS ROMS in the /roms/bios directory.
See BIOS files further down for the ones you need.


Features :
-----------------------
* Cart loads up to 512K Banked
* 32K RAM Expansion
* Save and Load State
* High score saving for up to 10 scores per game
* Mull mapping of any of the 12 DS keys to any combination of TI Joysticks/Keyboard
* Disk Support using the standard TI Disk Controller (you need 994adisk.bin - see BIOS files below)
* No Speech Synth yet (but games requiring the Speech Synth will run/play - just no voice)

Copyright :
-----------------------
DS99/4a is Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)

As long as there is no commercial use (i.e. no profit is made),
copying and distribution of this emulator, it's source code
and associated readme files, with or without modification, are
permitted in any medium without royalty provided this copyright
 notice is used and wavemotion-dave is thanked profusely.

The DS99 emulator is offered as-is, without any warranty.

Credits :
-----------------------
Thanks to Alekmaul who provided the baseline code to work with and to lobo for the menu graphical design.

Thanks to Flubba for the SN76496 sound core.

Thanks to the 99ers over on the AtariAge site for their help in grokking memory layouts and banking schemes.

Thanks to Mike Brent for Classic99 and letting me use some of the simplfied disk code (and help me hook it in!).

Thanks to Marc Rousseau for TI-99/Sim as the TMS9900 and TMS9901 core are borrowed from that project.


Installation :
-----------------------
* You will need the two console BIOS files as described below. Place both .bin BIOS files into /roms/bios (you can just make the directory on your SD card).
* You will also need the emulator itself. You can get this from the GitHub page - the only file you need here is DS994a.nds (the .nds is a Nintendo executable file).
* If you want to play disk based games (Adventure, Tunnels of Doom, etc) you will need 994adisk.bin (often just named disk.bin but you need to rename it and put it into /roms/bios).
* You will need games to play... right now the emulator supports C/D/G files and '8' non-inverted files. Basically just try loading a file to see if it works... the ROMs should have a .bin extension. 
* Recommend you put your games into /roms/ti99 as the emulator will default to that directory. That's where the cool kids keep them.


Known Issues :
-----------------------
* Skyway will not load/play - cause unknown.
* The 512K megademo8.bin will play (and is really cool!) but fails when it gets to the scanline stuff near the end.
* Borzork has audio squealing during gameplay.

BIOS Files :
-----------------------
Here are the BIOS file CRC32 hashes I'm using with all of my testing - seek these out if you want maximum compatibility:
```
* db8f33e5	994aROM.bin (8K)
* af5c2449	994aGROM.bin (24K)
* 8f7df93f	994aDISK.bin (8K) - this is needed only if you want .DSK support
```


Blend Mode (DSi) :
-----------------------
ColecoDS supports a "blend mode" which I borrowed from my scheme on StellaDS. In this mode, 
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
V0.4: 9-Jan-2023 by wavemotion-dave
* Added .DSK support for 90K and 180K disks (read and write both work but write doesn't yet persist back to SD card). Use the Cassette icon in the lower left of the keyboard to mount disks.
* Added High Score support for 10 scores per game. Use the new 'HI' button on the main keyboard.
* Fixed loading of 8K "banked" games (Tuthankam, Mancala, etc).
* Fixed right-side border rendering for "TEXT MODE" games (Adventure, Zork, etc).
* Added new option to mirror console RAM. This is more accurate but slows down the emualation. Congo Bongo needs this to render level 2+ properly.
* Games that were hanging looking for Speech Synthesis module no longer freeze up (no voice yet but you can play them).
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
