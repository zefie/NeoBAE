## How to compile
All compilation was tested on **Debian Linux 11 (Buster) amd64**
Package names and commands may vary if using a different OS.

#### Setup & Compile Linux 32-bit SDL2
- Install i386 support (one time):
    - `dpkg --add-architecture i386`
    - `apt-get update`
    - `apt-get install libc6-dev-x32 libsdl2-dev:i386`
- Build playbae
    - `cd minibae`
    - `make clean all`
- Using Build:
    - Run `./bin/playbae -h` for information on usage

#### Setup & Compile Linux 32-bit SDL2 with clang
- Install i386 support (one time):
    - `dpkg --add-architecture i386`
    - `apt-get update`
    - `apt-get install libc6-dev-x32 clang libsdl2-dev:i386`
- Build playbae
    - `cd minibae`
    - `make -f Makefile.clang clean all`
- Using Build:
    - Run `./bin/playbae -h` for information on usage

#### Setup & Compile Win32 mingw build
- Install mingw32 (one time):
    - `apt-get update`
    - `apt-get install binutils-mingw-w64-i686 g++-mingw-w64-i686 g++-mingw-w64-i686-posix g++-mingw-w64-i686-win32 gcc-mingw-w64-base gcc-mingw-w64-i686 gcc-mingw-w64-i686-posix gcc-mingw-w64-i686-posix-runtime gcc-mingw-w64-i686-win32 gcc-mingw-w64-i686-win32-runtime mingw-w64-common mingw-w64-i686-dev`
- Build playbae
    - `cd minibae/Tools/playbae`
    - For DirectSound support:
       - `make -f Makefile.mingw clean all`
    - For SDL2 support:
       - `make -f Makefile.mingw USE_SDL=1 clean all`
- Using Build:
    - Copy `./bin/playbae.exe` to a Windows system
    - Run `playbae.exe -h` for information on usage

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
