# DS99/4a
DS99 - A Texas Instruments TI99/4a Emulator for the DS/DSi

To run requires the TI BIOS ROMS in the /roms/bios directory.
See BIOS files further down for the ones you need.


Features :
-----------------------
* Cart loads up to 512K Banked
* 32K RAM Expansion
* Save and Load State
* Mull mapping of any of the 12 DS keys to any combination of TI Joysticks/Keyboard
* No cassette or Disk support yet... look for the Tunnels of Doom SSS hack that works without cassette.
* No Speech Synth yet

Copyright :
-----------------------
DS99/4a is Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)

As long as there is no commercial use (i.e. no profit is made),
copying and distribution of this emulator, it's source code
and associated readme files, with or without modification, 
are permitted in any medium without royalty provided this 
copyright notice is used and wavemotion-dave (Phoenix-Edition),
Alekmaul (original port) and Marat Fayzullin (ColEM core) are 
thanked profusely.

The DS99 emulator is offered as-is, without any warranty.

Credits :
-----------------------
Thanks to Alekmaul who provided the baseline code to work with and to lobo for the menu graphical design.

Thanks to Flubba for the SN76496 sound core.

Thanks to the 99ers over on the AtariAge site for their help in grokking memory layouts and banking schemes.

Hugest thanks to Marc Rousseau for TI-99/Sim as the TMS9900 and TMS9901 core are borrowed from that project.


Installation :
-----------------------
* You will need the two console BIOS files as described below. Place both .bin BIOS files into /roms/bios (you can just make the directory on your SD card).
* You will also need the emulator itself. You can get this from the GitHub page - the only file you need here is DS994a.nds (the .nds is a Nintendo executable file).
* You will need games to play... right now the emulator supports C/D/G files and '8' non-inverted files. Basically just try loading a file to see if it works... the ROMs should have a .bin extension. 
* Recommend you put your games into /roms/ti99 as the emulator will default to that directory. That's where the cool kids keep them.


Known Issues :
-----------------------
* Stuff... will fill in later as Alpha progresses into Beta

BIOS Files :
-----------------------
Here are the BIOS file CRC32 hashes I'm using with all of my testing - seek these out if you want maximum compatibility:
```
* db8f33e5	994aROM.bin (8K)
* af5c2449	994aGROM.bin (24K)
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
* DSi non XL/LL - use Blend Mode for the games that benefit from it (Space Fury, Galaxian, etc).
* DSi XL/LL - don't bother... the XL/LL screen decay is slower and games look great as-is.
* DS-LITE/PHAT - you can try it but the framerate might drop below 60 on some games.

To enable this new blend mode, pick your game and go into the "Game Options" sub-menu and turn it on.

Versions :
-----------------------
V0.2: 6-Jan-2023 by wavemotion-dave
* Increased bankswitch support to 512k
* Fixed IDLE instruction so games like Slymoids works
* Improved speed by 10-15% across the board... 
* Other minor cleanups as time permitted

V0.1: 5-Jan-2023 by wavemotion-dave
* It works! Almost...
