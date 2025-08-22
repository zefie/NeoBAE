## How to compile
All compilation was tested on **Debian Linux 11 (Buster) amd64** and **Debian Linux 13 (Trixie) amd64 (WSL2)**

Package names and commands may vary if using a different OS.

Note: These makefiles aren't that great, and don't play well with `-j#`, so please do not use any `-j` flag with make, unless it is `-j1`

#### Setup & Compile Linux SDL2
- Install dependencies support (one time):
    - `apt-get update`
    - `apt-get install libc6-dev libsdl2-dev`
- Build playbae
    - `cd minibae`
    - `make clean all`
- Using Build:
    - Run `./bin/playbae -h` for information on usage

#### Setup & Compile Linux SDL2 with clang
- Install dependencies support (one time):
    - `apt-get update`
    - `apt-get install libc6-dev clang libsdl2-dev`
- Build playbae
    - `cd minibae`
    - `make -f Makefile.clang clean all`
- Using Build:
    - Run `./bin/playbae -h` for information on usage

#### Setup & Compile Win32 mingw build
- Install mingw32 (one time):
    - `apt-get update`
    - `apt-get install binutils-mingw-w64-x86_64 g++-mingw-w64-x86_64 g++-mingw-w64-x86_64-posix g++-mingw-w64-x86_64-win32 gcc-mingw-w64-base gcc-mingw-w64-x86_64 gcc-mingw-w64-x86_64-posix gcc-mingw-w64-x86_64-posix-runtime gcc-mingw-w64-x86_64-win32 gcc-mingw-w64-x86_64-win32-runtime mingw-w64-common mingw-w64-x86_64-dev`
- Build playbae
    - `cd minibae/Tools/playbae`
    - For DirectSound support:
       - `make -f Makefile.mingw clean all`
    - For SDL2 support:
       - `make -f Makefile.mingw USE_SDL=1 clean all`
- Using Build:
    - Copy `./bin/playbae.exe` to a Windows system
    - Run `playbae.exe -h`, or drag a supported file over the exe

#### Setup & Compile miniBAE GUI
- If mingw32, all deps are already provided, you can just skip to build
- Linux: Install dependencies:
    - `apt-get update`
    - `apt-get install libsdl2-dev libsdl2-ttf-dev`
- Linux cross-dev (Windows):
    - `apt-get install binutils-mingw-w64-x86_64 g++-mingw-w64-x86_64 g++-mingw-w64-x86_64-posix g++-mingw-w64-x86_64-win32 gcc-mingw-w64-base gcc-mingw-w64-x86_64 gcc-mingw-w64-x86_64-posix gcc-mingw-w64-x86_64-posix-runtime gcc-mingw-w64-x86_64-win32 gcc-mingw-w64-x86_64-win32-runtime mingw-w64-common mingw-w64-x86_64-dev`
    - SDL dependency for mingw is provided in the repo
- Build GUI
    - Linux: `make -f Makefile.gui clean all`
       - If you want hardware MIDI (in/out) support, you will have to choose if you want the ALSA backend, JACK backend, or both:
          - ALSA: `make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_ALSA=1 clean all`
          - Jack: `make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_JACK=1 clean all`
          - Both: `make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_ALSA=1 ENABLE_JACK=1 clean all`
          - Note: If `/dev/snd/seq` does not exist on your system (eg, WSL), ALSA will cause the miniBAE GUI to segfault when accessing the MIDI Devices dropdowns.
       - It is up to you to install the development dependencies for Jack and/or ALSA.
    - MingW: `make -f Makefile.gui-mingw clean all`
        - Hardware MIDI support is already enabled for mingw builds
- Using Build (Linux):
    - Copy the file to `/usr/local/bin` or your prefered path.
    - Note: The miniBAE GUI looks for `zenity`, `kdialog`, or `yad` (in this order) for file browsing.
    - If you do not have these installed, the `Open`, `Load Bank`, `Export`, and `Record` buttons may not function.
- Using Build (Windows):
    - Copy `./bin/minibae_gui.exe` to a Windows system
    - Run `minibae_gui.exe`, or drag a supported file over the exe

#### Setup & Compile Emscripten WASM32 build (no sound card support)
- Install emscripten (one time):
    - `apt-get update`
    - `apt-get install emscripten`
- Build playbae
    - `cd minibae`
    - `make -f Makefile.emcc clean all`
- Using Build:
    - Copy `bin/minibae-audio.js`, `bin/minibae-audio.htm`, and `bin/playbae.js` to a web server (cannot use local filesystem due to browser security)
    - Modify `minibae-audio.htm` to point to desired audio file (and optionally patches.hsb)
    - Load in browser, and click the page to enable the Audio Context, miniBAE should start playing automatically.
    - Current build utilizes realtime streaming to WebAudio, no longer converting to WAV first. You should now be able to safely play endless MIDIs with the emscripten build.
    - Emscripten build is WIP, usage of browser debugger/inspector suggested.
    - **NOTE:** The Emscripten build and the old musicObject JS are completely different, and not compatible with each other, *yet*...
