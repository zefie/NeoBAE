# miniBAE

A cross-platform audio engine and music player supporting multiple audio formats including MIDI, RMF, WAV, AIFF, AU, FLAC, and more.

## About

[miniBAE](miniBAE_README.md) (Beatnik Audio Engine, mini edition) is a mature, well-tested audio engine originally developed by Beatnik, Inc. for embedded applications and small footprint environments. The engine has been used in everything from set-top boxes like Microsoft's WebTV to programming languages like Java, as well as several mobile phones, and has found its way into dozens of games and applications over its lifetime.

When Beatnik ended business in December 2009, the source code was released under a BSD license. A big thanks to [Steve Hales](https://github.com/heyigor/miniBAE) for not letting this valuable piece of technology disappear.

## Features

- **Multi-format audio support**: MIDI, RMF (Rich Music Format), WAV, AIFF, AU, FLAC, OGG Vorbis, MP2, MP3
- **Multiple Soundbanks**: Experience your favorite MIDIs with several Beatnik HSB (HeadSpace Bank), and additional SoundFont 2/3 support
- **Real-time synthesis**: Built-in software synthesizer with General MIDI support
- **Cross-platform**: Runs on Linux, Windows, macOS, and can be compiled to WebAssembly
- **Command-line player**: `playbae` - a versatile audio file player
- **GUI application**: `zefidi` - a graphical interface with playlist support
- **WebAssembly API**: Bring Beatnik back to the web with modern WebAssembly
- **Hardware MIDI support**: MIDI input/output on supported platforms (GUI Only)
- **Audio export**: Convert MIDI files to audio formats like WAV, MP3, FLAC, and Vorbis
- **Low latency**: Designed for real-time audio applications
- **Embeddable**: Can be integrated into other applications
- **Modular Build System**: Can be slimmed down to its core, to restore the original minimal footprint
- **Portable Design**: Easily add new and unique platform support

## Building

All compilation has been tested on Debian Linux 11+ and Windows via MinGW cross-compilation.

### Linux (SDL3)

```bash
# Install dependencies
apt-get update
apt-get install libc6-dev libsdl3-dev

# Build
cd minibae
make clean
make -j$(nproc)

# Run
./bin/playbae -h
```

### Windows (MinGW cross-compile)

```bash
# Install MinGW toolchain
apt-get install binutils-mingw-w64-x86_64 g++-mingw-w64-x86_64 gcc-mingw-w64-x86_64

# Build with SDL3 support
cd minibae
make clean
make -f Makefile.mingw USE_SDL3=1 -j$(nproc)
```

### GUI Application

```bash
# Linux
apt-get install libsdl3-dev libsdl3-ttf-dev
make clean
make -f Makefile.gui -j$(nproc)

# Windows
make clean
make -f Makefile.gui-mingw -j$(nproc)
```

### WebAssembly (Emscripten)

```bash
apt-get install emscripten
make clean
make -f Makefile.emcc -j$(nproc)
```

For detailed build instructions, including slim builds, see [HowToBuild.md](HowToBuild.md).

## Usage

### Command Line Player

```bash
# Play a MIDI file
./bin/playbae song.mid

# Export MIDI to WAV
./bin/playbae input.mid -o output.wav 

# Export to MP3 with custom bitrate
./bin/playbae input.mid -o output.mp3 -b 192

# Show karaoke lyrics (for supported files)
./bin/playbae song.mid -k

# Play with custom sample rate
./bin/playbae audio.wav -r 48000
```

### GUI Application

The GUI provides an intuitive interface for:
- Loading and playing audio files
- Managing playlists
- Real-time audio visualization
- MIDI channel control and muting
- Audio export functionality (WAV/FLAC/MP3/Vorbis)
- Hardware MIDI device integration
  - Hardware input support - play the beatnik synth your way
  - Utilitizes [RtMidi](https://github.com/thestk/rtmidi) for ultra low latency input
  - Record incoming MIDI events - to MIDI! (or WAV/FLAC/MP3)
  - Also supports MIDI Output
- Cross-platform: Runs on any device SDL2 does
- Dark mode support (Default for Linux, on Windows it will default to your theme settings (10/11))

### WebAssembly

A brand new WebAssembly API for the miniBAE engine is available thanks to [webcd](https://github.com/charlie3684)!

The new API can be used to build your own player, but a sample of webcd's Music Studio is included to demonstrate the WebAssembly's capabilities.

Please note that the WebAssembly engine does not yet support FluidSynth, and is limited to Beatnik HSB banks.

## Supported Formats

| Format | Extensions | Notes |
|--------|------------|-------|
| MIDI | `.mid`, `.midi` | Standard MIDI files |
| RMF | `.rmf` | Rich Music Format (Beatnik's proprietary format) |
| Audio | `.wav`, `.aiff`, `.au` | Uncompressed audio |
| Compressed | `.mp2`, `.mp3`, `.flac`, `.ogg` | Various compressed formats |

## Architecture

miniBAE consists of several key components:

- **Audio Engine**: Core synthesis and playback engine
- **Platform Layer**: Abstraction for different operating systems
- **Format Handlers**: Support for various audio/music file formats
- **Applications**: Command-line and GUI frontends

## License

miniBAE is released under the BSD 3-Clause License. See [LICENSE](LICENSE) for details.

## Contributing

This project welcomes contributions! Whether it's bug fixes, new features, or platform support, feel free to submit pull requests.

## History

Originally created by Steve Hales and Jim Nitchals at Halestorm/Igor Labs, miniBAE evolved through several companies including Beatnik and Danger. The audio engine powered everything from web browser plugins to mobile devices, demonstrating its versatility and reliability.

The project has been modernized with contemporary build systems, updated dependencies, and new features while maintaining compatibility with the original API.

## Acknowledgments

See [ACKNOWLEDGEMENTS](minibae/ACKNOWLEDGEMENTS) for the complete list of contributors who made this project possible.
