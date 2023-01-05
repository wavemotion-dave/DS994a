# DS99/4a
DS99 - A Texas Instruments TI99/4a Emulator for the DS/DSi

To run requires the TI BIOS ROMS in the /roms/bios directory.
See BIOS files further down for the ones you need.


Features :
-----------------------
* 32K RAM Expansion 
* Currently limited to 64K bankswitch
* No cassette or Disk support yet... look for the Tunnels of Doom SSS hack that works without cassette.
* No Speech Synth yet

Copyright :
-----------------------
TI99/4a is Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)

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
Thanks to Alekmaul who provided the 
baseline code to work with and to lobo
for the menu graphical design.

Thanks to Flubba for the SN76496 sound core.

Hugest thanks to Marc Rousseau for TI-99/Sim as the TMS9900 and TMS9901 core are borrowed from that project.

Known Issues :
-----------------------
* Stuff

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
V0.1: 5-Jan-2023 by wavemotion-dave
* It works! Almost...
