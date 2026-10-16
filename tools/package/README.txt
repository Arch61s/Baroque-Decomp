BAROQUE - native PC port (version @VERSION@)
=============================================

A native Windows version of Baroque (PlayStation 2, Atlus/Sting 2008) translated from the original game code.
NO GAME DATA IS INCLUDED. The port uses the data from your own copy of Baroque (USA, SLUS-21714).


FIRST START - GAME DATA FROM YOUR DISC
--------------------------------------
1. Make an .iso image of your Baroque (USA) disc (any disc-imaging tool that writes a plain .iso; an "undub"
   image of the USA release works too).
2. Put the .iso file into this folder (next to Baroque.exe).
3. Start Baroque.exe. A small window copies what the game needs out of the image (about 2.5 GB, takes a minute
   or two). This happens only once; afterwards the .iso can be deleted or moved.

   Alternatives: drag the .iso file onto Baroque.exe, or run
       Baroque.exe --setup-data "D:\path\to\Baroque.iso"
   from a command prompt (copies the data and exits).

After the setup this folder contains:
   assets\     game data (DATA.BIN contents)
   disc\       movies (MOVIE_EN), sound (SOUND_EN) and the menu text (SLUS_217.14) from the disc
Delete both folders to run the setup again.


VOICES - ENGLISH OR JAPANESE (OPTIONAL SECOND DISC)
---------------------------------------------------
The USA disc has the English voices. For the Japanese voices, also put your Baroque (Japan) .iso (SLPM-66747) next
to Baroque.exe and start the game again: it copies the Japanese voices once (about 350 MB). Then pick
PC SETTINGS -> VOICES -> Japanese. The text stays English. Without the Japan disc the option says what is missing.
With an undubbed USA image it is the other way round: Japanese voices first, and your Baroque (Europe) .iso adds
the English ones.

Keep the game in a normal folder you can write to (for example Documents or D:\Games\Baroque), not in
"Program Files": settings, saves and the log file are written next to Baroque.exe.


FILES WRITTEN NEXT TO Baroque.exe
---------------------------------
   controls.ini        key / mouse bindings (written with the defaults on the first start; edit to rebind)
   baroque_port.ini    PC display settings (frame rate cap, interpolation, HUD layout, aspect), also in-game
   save\               save files
   baroque.log         log of the last run (baroque.old.log = the run before). Please attach it to bug reports.


CONTROLS (keyboard + mouse; a gamepad works too)
------------------------------------------------
  WASD move (hold Left Ctrl = walk)        Mouse = camera (F1 frees the mouse cursor)
  Left click / C = Attack                  Right click / Space = Special attack (Space also confirms menus)
  F = back / conversation zoom             Tab = item screen
  Q / E or mouse wheel = item slot         G = throw (hold to aim)
  Left Shift = lock-on                     R / middle click = camera reset
  M = map                                  Enter = pause / skip / start, Esc = back (menus) or pause
  Menus: hover + click with the mouse, or arrows + Space / F.
  Hold START (Enter / Esc) or hold the left mouse button to skip a cutscene.
PC settings: in game Tab -> M (GAME CONFIG), or the Option menu on the title screen.


TROUBLESHOOTING
---------------
- "Game data needed": no .iso was found next to Baroque.exe - see FIRST START.
- "not Baroque (USA)": the image is another release; the port needs the North American disc. The Japan and
  Europe discs only add voices, next to the USA one.
- The window does not open: update the graphics driver (OpenGL 3.3 is required).
- Anything else: look at baroque.log in this folder.
- Requirements: Windows 10 or 11 (64-bit), OpenGL 3.3 graphics. No other installs are needed.


Third-party components: see THIRD_PARTY_NOTICES.txt.
Baroque is (c) STING / ATLUS. This is an unofficial fan project, not affiliated with the rights holders.
