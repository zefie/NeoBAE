# miniBAE Development Guidelines

## Architecture Overview

miniBAE is a cross-platform audio engine with these core components:

- **Audio Engine**: Core synthesis and playback in `src/BAE_Source/Common/`
- **Platform Layer**: OS abstraction in `src/BAE_Source/Platform/` 
- **Format Handlers**: Support for MIDI, RMF, WAV, AIFF, AU, FLAC, OGG VORBIS, MP3
- **Optional Formats**: SoundFont 2.0, SoundFont 3.0, DLS, XMF, MXMF
- **Applications**: `playbae` (CLI) and `zefidi` (GUI) frontends
- **Sample Cache**: Efficient memory management for audio samples
- **Android App**: at `src/miniBAEDroid`

## Build System

### Key Make Variables
- `USE_SDL=1`: Enable SDL2 audio output
- `DEBUG=1`: Enable debug output in builds (playbae: stderr, zefidi: zefidi.log)
- `LDEBUG=1`: Disable optimizations and don't strip for proper gdb debugging
- `NOAUTO=1`: Manual control of features (disable auto-enabling)
- `MP3_DEC=1/MP3_ENC=1`: MP3 support
- `FLAC_DEC=1/FLAC_ENC=1`: FLAC support  
- `VORBIS_DEC=1/VORBIS_ENC=1`: OGG Vorbis support
- `SF2_SUPPORT=1`: SoundFont 2.0 support
    - `USE_BASSMIDI=1`: BASSMIDI SoundFont playback (no DLS)
    - `USE_FLUIDSYNTH=1`: FluidSnyth SF2 with DLS support, required for `XMF_SUPPORT=1`
    - `USE_TSF=1`: TinySoundFont SF2 (no DLS)
- `XMF_SUPPORT=1`: Support for XMF files (requires FluidSynth)
- `KARAOKE=1`: MIDI karaoke lyrics support

### XMF and MXMF Support
- Requires `USE_FLUIDSYNTH=1` for DLS support
- XMF: Extended MIDI with DLS samples
- MXMF: Beatnik's proprietary format with DLS samples
- Loader in `src/BAE_Source/Common/GenXMF.c`
- Details in [XMF.md](XMF.md)

### Build Commands
```bash
# Linux SDL2 build
make clean && make -j$(nproc)

# Windows MinGW cross-compile  
make -f Makefile.mingw USE_SDL=1 -j$(nproc)

# Windows MinGW GUI cross-compile  
make -f Makefile.gui-mingw -j$(nproc)

# GUI application
make -f Makefile.gui -j$(nproc)

# WebAssembly
make -f Makefile.emcc -j$(nproc)

# When debugging
make DEBUG=1 -j$(nproc)

# When debugging crashes
make DEBUG=1 LDEBUG=1 -j$(nproc)

# When debugging XMF or MXMF files (requires FluidSynth)
make DEBUG=1 USE_FLUIDSYNTH=1 -j$(nproc)

# Debugging with playbae (XMF)
bin/playbae -f ../content/xmf/midnightsoul.xmf -t 3 2>&1
```

### FluidSynth Quirks
- When loading a DLS, it will always report `fluidsynth: error: Not a SoundFont file`, we must ignore that "error".

### Platform Support
- **Linux**: SDL2, ALSA/JACK (GUI)
- **Windows**: MinGW, DirectSound/SDL2
- **macOS**: Native audio (TODO)
- **WebAssembly**: Emscripten

## Code Patterns

### Memory Management
```c
// Allocate memory
XPTR data = XNewPtr(size);
if (!data) return MEMORY_ERR;

// Free memory  
XDisposePtr(data);
```

### Error Handling
```c
OPErr result = some_function();
if (result != NO_ERR) {
    // Handle error
    return result;
}
```

### Fixed-Point Arithmetic
```c
// Convert float to 16.16 fixed point
XFIXED fixed = (XFIXED)(float_value * 65536.0f);

// Convert back to float
float float_value = (float)fixed / 65536.0f;
```

### Conditional Compilation
```c
#if USE_SF2_SUPPORT == TRUE
// SF2-specific code
#endif

#if USE_DLS_SUPPORT == TRUE
// DLS-specific code
#endif

#ifdef _ZEFI_GUI
// GUI-specific code  
#endif
```

### Structure Alignment
```c
// Force 8-byte alignment for performance
struct GM_Instrument {
    // ... fields ...
} __attribute__((aligned(8)));
```

## Audio Format Support

### MIDI Processing
- Real-time synthesis with wavetable instruments
- ADSR envelopes, LFOs, filters
- Support for General MIDI, SoundFont 2.0
- Karaoke lyrics via meta events

### Sample Cache System
- Efficient memory management for audio samples
- Reference counting for shared samples
- Cache hit/miss tracking

### Platform Audio Output
- SDL2 for cross-platform compatibility
- Hardware MIDI input/output (GUI)
- Real-time audio mixing and effects

## Integration Points

### External Libraries
- **SDL2**: Cross-platform audio/video
- **FLAC/Ogg/Vorbis**: Audio codecs
- **RtMidi**: Hardware MIDI support
- **LAME**: MP3 encoding

### File Formats
- **MIDI**: Standard MIDI files (.mid)
- **RMF**: Beatnik Rich Music Format (.rmf)  
- **Audio**: WAV, AIFF, AU, FLAC, OGG, MP3
- **Banks**: SoundFont 2.0 (.sf2), DLS (.dls), HSB patches

## Development Workflow

### Adding New Features
1. Check `inc/Makefile.common` for build flags
2. Add conditional compilation blocks
3. Update platform-specific code if needed
4. Test across supported platforms

### Debugging Audio Issues
1. Enable debug builds: `make DEBUG=1`
2. Check debug output in `playbae` with verbose flags
3. Use `GM_GetRealtimeAudioInformation()` for voice status
4. Verify sample cache usage with memory tools

### Debugging crashes
1. Enable debug builds: `make DEBUG=1 LDEBUG=1`
2. Run playbae with gdb: `gdb --args bin/playbae -f /mnt/d/Music/MIDI/Mario/kart-credits.mid -t 3`

### Testing
- Test with various audio formats and sample rates
- Verify cross-platform compatibility
- Check memory usage with `BAE_GetSizeOfMemoryUsed()`
- Test real-time performance and latency

## Common Pitfalls

### Memory Leaks
- Always free `XPTR` allocations with `XDisposePtr()`
- Check reference counting in sample cache
- Use debug builds to track allocations

### Platform Differences
- Audio output APIs vary by platform
- File path handling differs (Unix vs Windows)
- Threading models may differ

### Performance Issues
- Fixed-point math for real-time audio
- Avoid allocations during audio processing
- Use appropriate interpolation modes
- Monitor voice usage and CPU load

### Build Configuration
- Many features are optional - check which are enabled
- Some codecs require external libraries
- Cross-compilation has specific requirements