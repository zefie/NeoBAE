## How to compile
All compilation was tested on **Debian Linux 11 (Buster) amd64**
Package names and commands may vary if using a different OS.

#### Setup & Compile Linux SDL2
- Install dependancies support (one time):
    - `apt-get update`
    - `apt-get install libc6-dev libsdl2-dev`
- Build playbae
    - `cd minibae`
    - `make clean all`
- Using Build:
    - Run `./bin/playbae -h` for information on usage

#### Setup & Compile Linux SDL2 with clang
- Install dependancies support (one time):
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
    - Run `playbae.exe -h` for information on usage

#### Setup & Compile Prototype GUI
- If mingw32, all deps are already provided, you can just skip to build
- Linux: Install dependancies:
    - apt-get update
    - apt-get install libsdl2-dev libsdl2-ttf-dev
- Build GUI
    - Linux: `make -f Makefile.gui clean all`
    - MingW: `make -f Makefile.gui-mingw clean all`
- Using Build:
    - Copy `./bin/minibae_gui.exe` to a Windows system
    - Run `minibae_gui.exe`, or drag a supported file over the exe
    - Linux:
        - The program looks for "zenity", "kdialog", or "yad" (in this order) for file browsing.
        If you do not have these installed, the Open and Export WAV buttons may not function.


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
    - Load in browser, wait for conversion, press play on player when the button is no longer greyed out
    - Emscripten build is WIP, usage of browser debugger/inspector suggested
    - **NOTE:** The Emscripten build and the old musicObject JS are completely different, and not compatible with each other.
