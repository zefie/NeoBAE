## How to compile
All compilation was tested on **Debian Linux 11 (Buster) amd64** and **Debian Linux 13 (Trixie) amd64 (WSL2)**

Package names and commands may vary if using a different OS.

When using the `-j` flag with `make`, do not run `clean` and `all` in the same command, it will fail. You can build with the `-j` flag after cleaning without it. See below.

## Get the source (git, easier)
- `git clone https://github.com/zefie/miniBAE`
- `cd miniBAE && git submodule init --update`

## Get the source (tarball or zip, more work)
- Download tarball, extract it
- Get the [minimp3 source](https://github.com/lieff/minimp3)
   - Place it in the `minibae/src/thirdparty/minimp3` folder
- Get the [FLAC source](https://xiph.org/flac/)
   - Place it in the `minibae/src/thirdparty/flac` folder
- Get the [OGG source](https://xiph.org/ogg/)
   - Place it in the `minibae/src/thirdparty/libogg` folder
- Get the [Vorbis source](https://xiph.org/vorbis/)
   - Place it in the `minibae/src/thirdparty/libvorbis` folder
- Get the [RtMidi source](https://github.com/thestk/rtmidi)
   - Place it in the `minibae/src/thirdparty/rtmidi` folder
- A slimmed down copy of Lame v3.100 is included in the main source tree, for mp3 encoding.
- You only need to grab what you want to support, [see below](#modular-build-system).

## Linux
#### Setup & Compile Linux SDL2
- Install dependencies support (one time):
    - `apt-get update`
    - `apt-get install libc6-dev libsdl2-dev`
- Build playbae
    - `cd minibae`
    - `make clean`
    - `make -j$(nproc)`
- Using Build:
    - Run `./bin/playbae -h` for information on usage

#### Setup & Compile Linux SDL2 with clang
- Install dependencies support (one time):
    - `apt-get update`
    - `apt-get install libc6-dev clang libsdl2-dev`
- Build playbae
    - `cd minibae`
    - `make clean`
    - `make -f Makefile.clang -j$(nproc)`
- Using Build:
    - Run `./bin/playbae -h` for information on usage

#### Setup & Compile zefidi GUI
- Install dependencies:
    - `apt-get update`
    - `apt-get install libsdl2-dev libsdl2-ttf-dev`
- Build GUI
    - `make clean && -f Makefile.gui -j$(nproc)`
    - If you want hardware MIDI (in/out) support, you will have to choose if you want the ALSA backend, JACK backend, or both:
       - ALSA: `make clean && make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_ALSA=1 -j$(nproc)`
       - Jack: `make clean && make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_JACK=1 -j$(nproc)`
       - Both: `make clean && make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_ALSA=1 ENABLE_JACK=1 -j$(nproc)`
       - Note: If `/dev/snd/seq` does not exist on your system (eg, WSL), ALSA will cause the miniBAE GUI to segfault when accessing the MIDI Devices dropdowns.
    - It is up to you to install the development dependencies for Jack and/or ALSA.
- Using Build:
    - Copy the file to `/usr/local/bin` or your prefered path.
    - Note: zefidi looks for `zenity`, `kdialog`, or `yad` (in this order) for file browsing.
    - If you do not have these installed, the `Open`, `Load Bank`, `Export`, and `Record` buttons may not function.

## Windows

#### Setup & Compile Win32 mingw build (using Debian WSL2)
- Install mingw into your Debian WSL2:
    - `apt-get update`
    - `apt-get install binutils-mingw-w64-x86_64 g++-mingw-w64-x86_64 g++-mingw-w64-x86_64-posix g++-mingw-w64-x86_64-win32 gcc-mingw-w64-base gcc-mingw-w64-x86_64 gcc-mingw-w64-x86_64-posix gcc-mingw-w64-x86_64-posix-runtime gcc-mingw-w64-x86_64-win32 gcc-mingw-w64-x86_64-win32-runtime mingw-w64-common mingw-w64-x86_64-dev`
- Build playbae
    - `cd minibae`
    - For DirectSound support:
       - `make clean`
       - `make -f Makefile.mingw -j$(nproc)`
    - For SDL2 support:
       - `make clean`
       - `make -f Makefile.mingw USE_SDL=1 -j$(nproc)`
- Using Build:
    - Copy `./bin/playbae.exe` to a Windows system
    - Run `playbae.exe -h`, or drag a supported file over the exe

#### Setup & Compile zefidi GUI (using Debian WSL2)
- All SDL2 dependencies are already provided
- Install mingw into your Debian WSL2:
    - `apt-get install binutils-mingw-w64-x86_64 g++-mingw-w64-x86_64 g++-mingw-w64-x86_64-posix g++-mingw-w64-x86_64-win32 gcc-mingw-w64-base gcc-mingw-w64-x86_64 gcc-mingw-w64-x86_64-posix gcc-mingw-w64-x86_64-posix-runtime gcc-mingw-w64-x86_64-win32 gcc-mingw-w64-x86_64-win32-runtime mingw-w64-common mingw-w64-x86_64-dev`
    - SDL dependency for mingw is provided in the repo
- Build GUI
    - `cd minibae`
    - `make clean`
    - `make -f Makefile.gui-mingw -j$(nproc)`
    - Hardware MIDI support is already enabled for mingw builds
- Using Build (Windows):
    - Copy `./bin/zefidi.exe` to a Windows system
    - Run `zefidi.exe`, or drag a supported file over the exe

## MacOS X
- TODO! It should build but I am not familar with the Mac build environment.

## zefidi GUI (Windows & Linux)
#### Setup & Compile zefidi GUI
- If using mingw32, all deps are already provided, you can just skip to build
- Linux: Install dependencies:
    - `apt-get update`
    - `apt-get install libsdl2-dev libsdl2-ttf-dev`
- Linux cross-dev (Windows):
    - `apt-get install binutils-mingw-w64-x86_64 g++-mingw-w64-x86_64 g++-mingw-w64-x86_64-posix g++-mingw-w64-x86_64-win32 gcc-mingw-w64-base gcc-mingw-w64-x86_64 gcc-mingw-w64-x86_64-posix gcc-mingw-w64-x86_64-posix-runtime gcc-mingw-w64-x86_64-win32 gcc-mingw-w64-x86_64-win32-runtime mingw-w64-common mingw-w64-x86_64-dev`
    - SDL dependency for mingw is provided in the repo
- Build GUI
    - Linux: `make clean && -f Makefile.gui -j$(nproc)`
       - If you want hardware MIDI (in/out) support, you will have to choose if you want the ALSA backend, JACK backend, or both:
          - ALSA: `make clean && make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_ALSA=1 -j$(nproc)`
          - Jack: `make clean && make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_JACK=1 -j$(nproc)`
          - Both: `make clean && make -f Makefile.gui ENABLE_MIDI_HW=1 ENABLE_ALSA=1 ENABLE_JACK=1 -j$(nproc)`
          - Note: If `/dev/snd/seq` does not exist on your system (eg, WSL), ALSA will cause the miniBAE GUI to segfault when accessing the MIDI Devices dropdowns.
       - It is up to you to install the development dependencies for Jack and/or ALSA.
    - MingW: `make clean && make -f Makefile.gui-mingw -j$(nproc)`
       - Hardware MIDI support is already enabled for mingw builds
- Using Build (Linux):
    - Copy the file to `/usr/local/bin` or your prefered path.
    - Note: zefidi looks for `zenity`, `kdialog`, or `yad` (in this order) for file browsing.
    - If you do not have these installed, the `Open`, `Load Bank`, `Export`, and `Record` buttons may not function.
- Using Build (Windows):
    - Copy `./bin/zefidi.exe` to a Windows system
    - Run `zefidi.exe`, or drag a supported file over the exe

## WebAssembly
#### Setup & Compile Emscripten WASM32 build (no sound card support)
- Install emscripten (one time):
    - `apt-get update`
    - `apt-get install emscripten`
- Build playbae
    - `cd minibae`
    - `make clean`
    - `make -f Makefile.emcc -j$(nproc)`
- Using Build:
    - Copy `bin/minibae-audio.js`, `bin/minibae-audio.htm`, and `bin/playbae.js` to a web server (cannot use local filesystem due to browser security)
    - Modify `minibae-audio.htm` to point to desired audio file (and optionally patches.hsb)
    - Load in browser, and click the page to enable the Audio Context, miniBAE should start playing automatically.
    - Current build utilizes realtime streaming to WebAudio, no longer converting to WAV first. You should now be able to safely play endless MIDIs with the emscripten build.
    - Emscripten build is WIP, usage of browser debugger/inspector suggested.
    - **NOTE:** The Emscripten build and the old musicObject JS are completely different, and not compatible with each other, *yet*...

## Modular Build System
zefie's modifications (aside from the core 64-bit port) are designed to be completely modular in the build system. If you don't like the idea of miniBAE having FLAC or Vorbis, you can easily build without them!

To get full control of what is built, pass `NOAUTO=1` to make, eg `make NOAUTO=1`.

When passed `NOAUTO=1`, the build is under your control, and the automatic enabling of all features is disabled. You will have to enable features manually:

- `MP3_DEC=1` - Enable MP3 Decoder (needed for full RMF support)
- `MP3_ENC=1` - Enable MP3 Encoder (enables exporting to MP3)
- `KARAOKE=1` - Enable support for MIDI Karaoke
- `FLAC_DEC=1` - Enable FLAC playback
- `FLAC_ENC=1` - Enable FLAC exporting
- `VORBIS_DEC=1` - Enable OGG Vorbis playback
- `VORBIS_ENC=1` - Enables OGG Vorbis exporting
- `SF2_SUPPORT=1` - Enable SF2 SoundFont 2 support (use with VORBIS_DEC=1 to get SF3/SFO support as well)
- `PLAYLIST=1` - Enable playlist support in the GUI
- `USE_SDL2=1` - To enable usage of SDL2 on platforms that support other native audio
- `USE_SDL3=1` - To enable usage of SDL3 on platforms that support other native audio
- `OGG_SUPPORT=1` - Enable OGG Support (useless without Vorbis)
- `DISABLE_NOKIA_PATCH=1` - If you want to disable the harmless Nokia Patch (MSB5 -> MSB1, MSB6 -> MSB2)
- `DISABLE_BEATNIK_NRPN=1` - Disables SF2 NRPN support for Beatnik extra percussion channel(s).
- `DEBUG=1` - Enable verbose output (console for playbae, logfile for zefidi)
- `LDEBUG=1` - For serious debugging, disables compile-time optimizations and leaves debugging symbols

Some flags set other flags, or only work in certain situations:
- `VORBIS_DEC=1` or `VORBIS_ENC=1` will automatically set `OGG_SUPPORT=1`
- `USE_SDL=1` is currently only effective on `Makefile.mingw`
- Building the zefidi GUI will assume usage of SDL2 since it depends on it
