# The Xbox: "Pokemon Black" and "Pokemon White"

Status (2026-11-08): new and experimental, not yet tried on a console.
The build makes two apps for an Xbox One or Xbox Series console in
**Developer Mode**: **Pokemon Black** and **Pokemon White**. Both play in
the one-screen layout with the controller. Each app takes only its own
game's ROM and keeps its own saves.

A retail Xbox cannot install homebrew outside Developer Mode. You need
Developer Mode (a Microsoft Partner Center developer account), and you
need your own ROM of the game, dumped from your cartridge. Nothing here
contains the game.

## Getting the packages

Run the **xbox** workflow (Actions > xbox > Run workflow). It also runs on
any push that changes the Xbox build's files. When it finishes, download
its **pokemon-xbox** artifact, a zip that holds:

| File | What it is |
|---|---|
| `PokemonBlack_<version>_x64.appx` | the Pokemon Black app |
| `PokemonWhite_<version>_x64.appx` | the Pokemon White app |
| `eidos-xbox.cer` | the certificate both are signed with |
| `Microsoft.VCLibs.x64.14.00.appx` | Microsoft's C++ runtime the apps need |

## Installing (Xbox Device Portal)

1. On the Xbox, in **Dev Home**, turn on **Device Portal** (Remote access
   settings) and note its address, such as `https://192.168.1.20:11443`.
2. On a PC on the same network, open that address in a browser. Accept the
   certificate warning, then sign in with the user name and password you
   set in Dev Home.
3. **Home > Add** (Install app). Choose `PokemonBlack_<version>_x64.appx`.
   Tick "I want to specify optional packages", add
   `Microsoft.VCLibs.x64.14.00.appx`, and also add `eidos-xbox.cer` as the
   certificate if the page asks for one. Install.
4. Do the same with `PokemonWhite_<version>_x64.appx`. VCLibs is already
   installed by then.
5. **Make each app a Game.** In Dev Home, highlight the app and press the
   View button (two squares) > **View details** > **App type: Game**. A
   game gets the console's full memory and graphics power; as an "App" it
   gets very little and the game will not run well, if at all.

## Adding the ROMs

Each app has its own folder, and the ROM goes there:

1. Start the app once. It shows "Pokemon Black or White is needed" and
   the folder's path.
2. In Device Portal, open **File explorer > LocalAppData >
   eidos.PokemonBlack_... > LocalState** and upload your Pokemon Black
   `.nds` file. Use the White app's folder (`eidos.PokemonWhite_...`) for
   Pokemon White.
3. In the app, choose **Look again**. It checks the ROM and extracts the
   game data once, which takes a while. Then the game starts.

The Pokemon Black app takes only a Pokemon Black ROM, and the White app
only a White one. It reads the game code in the ROM's header: IRB for
Black, IRA for White. English, French, German, Italian and Spanish ROMs
are supported.

## Playing

- The controller plays the game directly, and **B** is the game's B, not
  the system's "back".
- Saves, settings and the extracted data stay in the app's LocalState
  folder. Uninstalling the app deletes them, so copy `LocalState\profile`
  out through Device Portal first if you want to keep your save.
- `LocalState\stderr.txt` holds the program's messages. Please attach it
  when you report a problem.
- `LocalState\env.txt` (NAME=value lines) and `args.txt` work as on the
  phones, for example `EIDOS_FPS=1` to log the frame rate.

## How it is built

The Xbox takes the route the Switch takes (docs/consoles.md):

- **The game** is the 64-bit route's C: the wasm32 module through wasm2c,
  with explicit bounds checks and a fixed 512 MiB memory
  (`EIDOS_HOST_BOUNDS_CHECKS`). The workflow's Linux job makes it.
- **The host** is host/ as every platform has it, built by MSVC as a UWP
  app (`WINAPI_FAMILY_APP`, `/APPCONTAINER`, the store CRT): host/xbox/.
  It takes the phones' way (`HOST_MOBILE`): no command line, and its folder
  is the package's LocalState. host/xbox/xbox_host.c has the entry, the
  folders and the ROM "dialog", and the package's `edition.txt` tells it
  which game the app is.
- **SDL.** SDL3 has no UWP port, and SDL2 does. The SDL3-on-SDL2 layer
  written for the Switch (host/switch/sw2.h) answers the host's SDL3 calls.
  Its SDL2 half, sdl2_side.c, is a DLL of its own (`eidos_sdl2.dll`), so
  the program never sees SDL2's names and no renaming is needed. SDL2 runs
  the app (`SDL_WinRTRunApp`) and answers the controller's back button
  (`SDL_HINT_WINRT_HANDLE_BACK_BUTTON`).
- **Graphics.** OpenGL ES 3.0 from ANGLE for UWP (Microsoft's
  ANGLE.WindowsStore package) on Direct3D 11, the same way RetroArch's Xbox
  build draws its OpenGL cores. The OpenGL imports translate the game's
  shaders for OpenGL ES, as on the phones (`HOST_GLES`).
- **Packaging.** tools/xbox_package.py packs the one program twice, with
  two identities (`eidos.PokemonBlack`, `eidos.PokemonWhite`), names,
  tiles (host/xbox/assets/) and edition files. It signs both with the
  workflow's self-signed certificate (`CN=eidos`).

## What to expect

- **Speed.** The Xbox One's CPU (Jaguar, 1.75 GHz) is faster per core than
  the Switch's, which is about 8 fps in an emulator and has not been
  measured on hardware. Set the app type to Game (above). Speed on an Xbox
  One is the first thing to measure. If it is too slow, the next steps are
  the GPU 2D path and the guard-page memory instead of bounds checks
  (UWP allows the reservation).
- **Not in this build:** the launcher, and mods (as on the other
  consoles). Multiplayer is compiled in but untested here.
