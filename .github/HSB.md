# HSB (Headspace Sound Bank) Format Specification

## Overview

HSB (Headspace Sound Bank) is a container format originally developed by Headspace, Inc. for storing audio samples, instrument definitions, and music data. The format uses a hierarchical resource structure to organize different types of multimedia content, with support for compression, encryption, and efficient access patterns.

**Note**: This format was originally documented as RMF (Rich Music Format) with HIRF (Headspace Internal Resource Format) containers and IREZ file signatures. For consistency with the miniBAE project, all references use HSB terminology.

## File Structure

### HSB File Header
Every HSB file begins with a standard header:

```
Offset 0-3:   File signature "IREZ" (HSB identifier)
Offset 4-7:   Format version (32-bit, currently 0x00000001)
Offset 8-11:  Number of HSB resources in file (32-bit)
```

### HSB Resource Structure
Each resource in the file follows this structure:

```
Offset 0-3:   Offset to next resource (32-bit)
Offset 4-7:   Resource type (4-character code)
Offset 8-11:  Resource ID (32-bit)
Offset 12:    Name length (8-bit, Pascal string)
Offset 13+:   Resource name (variable length)
Offset N:     Resource body length (32-bit)
Offset N+4:   Resource body (variable length)
```

## Resource Types

### 'snd ' - Audio Sample Resources
Audio samples are stored in PCM format with optional compression and encryption.

#### Type 1 Resources (Basic/Extended/Compressed)
- **Type 1 Basic**: Simple uncompressed audio
- **Type 1 Extended**: Enhanced format with IEEE sample rates
- **Type 1 Compressed**: Compressed audio with multiple algorithms

**Common Fields**:
- Sample rate (32-bit fixed-point or 80-bit IEEE float)
- Loop start/end points (32-bit frame numbers)
- Base note (MIDI note number, 8-bit)
- Bit depth (16-bit unsigned)
- Channel count (32-bit)
- Byte order (big-endian preferred)

**Compression Types**:
- `'none'`: Uncompressed PCM
- `'ima4'`: IMA 4:1 ADPCM
- `'ulaw'`: µLaw (2:1 compression)
- `'alaw'`: aLaw (2:1 compression)

#### Type 2 Resources (Legacy)
Similar to Type 1 but with different header layout for compatibility.

#### Type 3 Resources (Streaming)
Optimized for network streaming with MPEG compression support:

**MPEG Variants**:
- `'mpga'` through `'mpgn'`: Various bitrates (32k-320k bps)
- Support for multiple channels (mono/stereo/5.1)
- Separate loop bounds per channel

### 'SONG' - Song Configuration Resources
Contains metadata and playback configuration for musical compositions.

**Key Fields**:
- Music resource ID reference
- Reverb type selection
- Tempo settings
- Voice limits (digital audio and synth voices)
- Volume scaling parameters
- Encryption flags

**Embedded Subresources**:
- `'TITL'`: Song title
- `'COMP'`: Composer name
- `'COPD'`: Copyright date
- `'LICC'`: Licensing contact
- `'LUSE'`: License usage terms
- `'LDOM'`: Licensed domain
- `'LTRM'`: License term
- `'EXPD'`: License expiration
- `'NOTE'`: General notes
- `'INDX'`: Index number
- `'PERF'`: Performed by

### 'ecmi' - Encrypted Compressed MIDI
Standard MIDI files that have been encrypted and compressed for protection.

### 'CACH' - Resource Index/Cache
Optional optimization resource that provides fast lookup of resource names and offsets within the HSB file.

#### Purpose and Benefits
- **Performance Optimization**: Eliminates need to parse entire file structure for resource access
- **Fast Loading**: Direct offset lookup instead of sequential parsing
- **Memory Efficiency**: Reduces memory usage by avoiding full file structure parsing
- **Optional**: Files work perfectly without CACH resources, but load faster with them

#### When to Include CACH Resources
- **Large Files**: Files with many resources (>10-20)
- **Frequent Access**: Applications that access resources repeatedly
- **Embedded Systems**: Memory-constrained environments
- **Production Builds**: Optimize for end-user performance

#### CACH Resource Structure
   (Byte numbers are relative to the start of this Toplevel 'CACH' Resource Body)

 Field       Bytes                Name               Meaning and Data Format
   1         0-3          cacheVersion              Cache format version (32-bit, currently 0x00000001)
   2         4-7          numEntries                Number of cache entries (32-bit unsigned)
   3         8-11         flags                     Cache control flags (32-bit)
   4         12+          cacheEntries              Array of cache entry structures

#### Cache Entry Structure
Each cache entry maps a resource name to its file offset:

   (Byte numbers are relative to the start of each cache entry)

 Field       Bytes                Name               Meaning and Data Format
   1         0-3          resourceType              Resource type code (4 ASCII characters)
   2         4-7          resourceID                Resource ID number (32-bit)
   3         8-11         fileOffset                Offset from file start to resource (32-bit)
   4         12-15        resourceSize              Size of resource body in bytes (32-bit)
   5         16           nameLength                Length of resource name (8-bit)
   6         17+          resourceName              Resource name (Pascal string, variable length)

#### Cache Control Flags
| Bit | Name | Description |
|-----|------|-------------|
| 0 | caseSensitive | Whether name lookups are case-sensitive |
| 1 | includeUnnamed | Include unnamed resources in cache |
| 2 | compressed | Cache data is compressed |
| 3 | encrypted | Cache data is encrypted |
| 4-31 | (reserved) | Set to 0 |

#### Cache Generation Process
1. **Parse File Structure**: Read all resource headers to collect metadata
2. **Sort Entries**: Organize by resource type and/or name for efficient lookup
3. **Calculate Offsets**: Determine exact file offsets for each resource
4. **Build Index**: Create cache entry structures
5. **Write CACH Resource**: Append to end of HSB file

#### Lookup Process with CACH
1. **Load Cache**: Read CACH resource into memory
2. **Find Entry**: Search cache for desired resource (by name or ID)
3. **Direct Access**: Use stored offset to jump directly to resource
4. **Validate**: Verify resource type and size match expectations

#### Cache Maintenance
- **Update on Modification**: Regenerate cache when file is modified
- **Version Compatibility**: Check cache version before using
- **Fallback Support**: Gracefully handle missing or invalid caches
- **Incremental Updates**: Update only changed entries when possible

#### Performance Characteristics
- **Memory Usage**: ~20-50 bytes per cached resource
- **Lookup Time**: O(1) for ID-based, O(log n) for name-based with sorting
- **Load Time Savings**: 50-90% reduction for large files
- **Storage Overhead**: 1-5% increase in file size

#### Implementation Notes
- **Tool Support**: HSB creation tools should auto-generate CACH resources
- **Validation**: Always validate cache entries against actual file structure
- **Error Handling**: Fall back to sequential parsing if cache is invalid
- **Cross-Platform**: Ensure byte order consistency in cache entries

#### Example Cache Entry
```
Resource Type: 'snd '
Resource ID:   0x00010001
File Offset:   0x00002E30
Resource Size: 0x00002E08
Name Length:   0x0B
Resource Name: "piano_c4" (11 bytes)
```

#### Best Practices
1. **Always Include**: For files with >5 resources
2. **Sort by Usage**: Place frequently accessed resources first
3. **Validate Integrity**: Check cache against file structure
4. **Version Control**: Update cache when file format changes
5. **Compression**: Consider compressing large caches
6. **Documentation**: Document cache generation process

### 'VERS' - Version Information
Simple 6-byte resource for version tracking:
- Major version (16-bit)
- Minor version (16-bit)
- Patch version (16-bit)

### 'INST' - Instrument Definitions
Links audio samples to MIDI program changes and defines playback parameters. Instruments can be either single-sample or multi-sample with keymapping.

#### Basic INST Resource Structure
   (Byte numbers are relative to the start of this Toplevel 'INST' Resource Body)

 Field       Bytes                Name               Meaning and Data Format
   1         0-1          INSTflags                  16-bit bitfield controlling instrument behavior
   2         2-3          programNumber              MIDI program change number (0-127)
   3         4-5          volume                     Master volume (0-100%)
   4         6-7          panPosition                Stereo pan position (-63 to +63)
   5         8-9          rootKey                    Root MIDI note for single-sample instruments
   6        10-11         fineTune                   Fine tuning adjustment (-99 to +99 cents)
   7        12-13         lowNote                    Lowest playable MIDI note
   8        14-15         highNote                   Highest playable MIDI note
   9        16-19         lowVelocity                Lowest playable velocity
  10        20-23         highVelocity               Highest playable velocity
  11        24-27         optionCount                Number of option subresources
  12        28-31         keymapCount                Number of keymap zones (0 = single sample)
  13       n bytes        optionsList                List of option subresources
  14       n bytes        keymapList                 List of keymap zones (multi-sample only)

#### INSTflags Bitfield
Controls various instrument behaviors:

| Bit | Name | Description |
|-----|------|-------------|
| 0 | (reserved) | Set to 0 |
| 1 | (reserved) | Set to 0 |
| 2 | notPolyphonic | Under development |
| 3 | miscParmsMode | How to interpret rootKey/volume fields |
| 4 | rescaleSamples | Adjust sample data volume on loading |
| 5 | (reserved) | Set to 0 |
| 6 | neverTranspose | Disable note transposition |
| 7 | (reserved) | Set to 0 |
| 8 | noReverb | Mute reverb send |
| 9 | extendedFormat | Use modern format (recommended) |
| 10 | releaseLoops | Continue sample loops after note release |
| 11 | honorSndRate | Respect original sample rates |
| 12 | (reserved) | Set to 0 |
| 13 | noLoops | Ignore sample loop definitions |
| 14 | (reserved) | Set to 0 |
| 15 | (reserved) | Set to 0 |

### Single-Sample Instruments
For instruments using one audio sample across the entire keyboard range:

- Set `keymapCount` to 0
- Use `rootKey` (field 5) to specify the sample's natural pitch
- Use `volume` (field 3) for overall instrument volume
- Sample selection is implicit (first 'snd ' resource in file)

### Multi-Sample Instruments
For instruments using different samples across the keyboard range:

#### Keymap Zones
Each keymap zone connects a MIDI note range to a specific sample:

   (Byte numbers are relative to the start of this keymapZone)

 Field       Bytes                Name               Meaning and Data Format
   1           1         bottomNote                 Lowest MIDI note in zone (0-127)
   2           2         topNote                    Highest MIDI note in zone (0-127)
   3         3-4         sampleNum                  HIRF Resource ID of sample (16-bit)
   4         5-6         rootKey1                   Root MIDI note for this sample
   5         7-8         volume1                    Volume for this zone (0-100%)

#### Keymap Organization
- Zones are listed sequentially with no gaps
- `keymapCount` specifies total number of zones
- Zones can overlap (later zones take precedence)
- Sample IDs reference 'snd ', 'csnd', or 'esnd' resources
- Each zone can have different root key and volume

#### Multi-Sample Best Practices
1. **Velocity Layering**: Use overlapping note ranges with different samples for velocity sensitivity
2. **Keyboard Splitting**: Divide keyboard into ranges (e.g., low/mid/high octaves)
3. **Crossfading**: Overlap adjacent zones slightly to avoid artifacts
4. **Volume Balancing**: Adjust zone volumes to ensure consistent levels
5. **Sample Selection**: Choose samples that complement each other tonally

### Option Subresources
Optional features that enhance instrument capabilities. Each option subresource starts with a 4-character type code followed by type-specific data.

#### 'ADSR' - Volume Envelope Generator
Controls amplitude changes over time for instrument voices.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'ADSR'` (type identifier)
- **4**: Segment count (1-8, 8-bit unsigned)
- **5+**: Array of envelope segments

**Envelope Segment Structure** (12 bytes each):
- **0-3**: Target level (32-bit, destination-dependent)
- **4-7**: Ending time (32-bit microseconds)
- **8-11**: Segment type (`'LINE'`, `'SUST'`, `'LAST'`, `'GOTO'`, `'GOTO_CONDITIONAL'`)

#### 'LPGF' - Low-Pass Filter Settings
Static low-pass filter parameters for all voices using this instrument.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'LPGF'` (type identifier)
- **4-7**: Corner frequency (32-bit, see frequency mapping table)
- **8-11**: Resonance (32-bit, 0-256)
- **12-15**: Dry/wet mix (32-bit, 0-256)

**Frequency Mapping**:
- 0-511: Linear mapping to MIDI note frequencies 0-127
- 512-16384: Direct frequency calculation

#### 'PITC' - Pitch Modulation
LFO-based pitch modulation with optional envelope control.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'PITC'` (type identifier)
- **4**: Envelope segment count (0-8, 8-bit)
- **5+**: Envelope segments (if count > 0)
- **N+1 to N+4**: LFO period (32-bit microseconds)
- **N+5 to N+8**: LFO waveform (`'SINE'`, `'SAWT'`, `'SAW2'`, `'SQUA'`, `'SQU2'`, `'TRIA'`)
- **N+9 to N+12**: Envelope depth (32-bit, -65536 to 65536)
- **N+13 to N+16**: LFO depth (32-bit, -65536 to 65536)

#### 'VOLU' - Volume Modulation
LFO-based volume modulation (envelope control not available for volume).

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'VOLU'` (type identifier)
- **4**: Envelope segment count (must be 0, 8-bit)
- **5-8**: LFO period (32-bit microseconds)
- **9-12**: LFO waveform (4-character code)
- **13-16**: Envelope depth (unused, set to 0)
- **17-20**: LFO depth (32-bit, 0-4096)

#### 'SPAN' - Pan Modulation
LFO-based stereo pan modulation with envelope control.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'SPAN'` (type identifier)
- **4**: Envelope segment count (0-8, 8-bit)
- **5+**: Envelope segments (if count > 0)
- **N+1 to N+4**: LFO period (32-bit microseconds)
- **N+5 to N+8**: LFO waveform (4-character code)
- **N+9 to N+12**: Envelope depth (32-bit, -63 to 63)
- **N+13 to N+16**: LFO depth (32-bit, 0-63)

#### 'LPFR' - LPF Frequency Modulation
Dynamic low-pass filter cutoff frequency modulation.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'LPFR'` (type identifier)
- **4**: Envelope segment count (0-8, 8-bit)
- **5+**: Envelope segments (if count > 0)
- **N+1 to N+4**: LFO period (32-bit microseconds)
- **N+5 to N+8**: LFO waveform (4-character code)
- **N+9 to N+12**: Envelope depth (32-bit, 0-511 for MIDI notes, 512-16384 for Hz)
- **N+13 to N+16**: LFO depth (32-bit, 0-8192)

#### 'LPRE' - LPF Resonance Modulation
Dynamic low-pass filter resonance modulation.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'LPRE'` (type identifier)
- **4**: Envelope segment count (0-8, 8-bit)
- **5+**: Envelope segments (if count > 0)
- **N+1 to N+4**: LFO period (32-bit microseconds)
- **N+5 to N+8**: LFO waveform (4-character code)
- **N+9 to N+12**: Envelope depth (32-bit, 0-256)
- **N+13 to N+16**: LFO depth (32-bit, 0-128)

#### 'LPAM' - LPF Mix Modulation
Dynamic low-pass filter dry/wet mix modulation.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'LPAM'` (type identifier)
- **4**: Envelope segment count (0-8, 8-bit)
- **5+**: Envelope segments (if count > 0)
- **N+1 to N+4**: LFO period (32-bit microseconds)
- **N+5 to N+8**: LFO waveform (4-character code)
- **N+9 to N+12**: Envelope depth (32-bit, 0-256)
- **N+13 to N+16**: LFO depth (32-bit, 0-128)

#### 'CURV' - Performance Controller Mapping
Maps MIDI controllers to voice parameters using transfer functions.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'CURV'` (type identifier)
- **4-7**: Source controller (4-character code: `'PITC'`, `'VOLU'`, `'MODW'`, `'SAMP'`)
- **8-11**: Destination parameter (4-character code: `'NVOL'`, `'ATIM'`, `'ALEV'`, `'SUST'`, `'SLEV'`, `'RELS'`, `'PITF'`, `'PITC'`, `'VOLF'`, `'VOLU'`, `'LPAM'`, `'WAVE'`)
- **12**: Transfer function point count (1-8, 8-bit)
- **13+**: Transfer function points (3 bytes each: 1-byte source level + 2-byte destination level)

#### 'DMOD' - Simple Modulation Wheel Setup
Quick LFO setup connecting modulation wheel to pitch with fixed parameters.

**Structure** (Byte numbers relative to subresource start):
- **0-3**: `'DMOD'` (type identifier)
- *(No additional fields - uses fixed sine LFO at 5.55 Hz, 1 semitone depth)*

### Option Subresource Usage Guidelines

#### Common Combinations
- **Basic Sustain**: ADSR with 'LINE' → 'SUST' → 'LINE' → 'LAST' segments
- **Percussive**: ADSR with fast attack, no sustain, fast release
- **Pad/Layer**: ADSR with slow attack, long sustain, slow release
- **Filtered Sweep**: LPGF + LPFR for dynamic filter effects
- **Tremolo**: VOLU with sine LFO for amplitude modulation
- **Vibrato**: PITC with sine LFO for pitch modulation
- **Auto-pan**: SPAN with triangle LFO for stereo movement

#### Performance Considerations
- Each option subresource adds CPU overhead
- LFO processing is more expensive than envelope processing
- Multiple modulation sources on same parameter sum together
- Transfer functions allow complex controller mapping
- Simple modulation wheel is most efficient for basic needs

#### Parameter Ranges and Scaling
- **Pitch**: ±1 octave (-65536 to 65536 for 1 semitone per unit)
- **Volume**: 0-4096 (4096 = full scale)
- **Pan**: -63 (full left) to +63 (full right)
- **LPF Frequency**: 0-511 (MIDI notes) or 512-16384 (Hz)
- **LPF Resonance**: 0-256 (0 = no resonance, 256 = maximum)
- **LPF Mix**: 0-256 (0 = dry, 256 = wet)

#### Debugging Option Subresources
- Verify subresource type codes are correct
- Check parameter ranges are within valid limits
- Monitor CPU usage with multiple active modulations
- Test edge cases (extreme controller values, fast LFO rates)
- Use debug builds to trace modulation calculations

### Envelope Processing
Instruments support sophisticated envelope generation:

#### Envelope Segments
Each envelope can have up to 8 segments with different behaviors:

| Type | Description |
|------|-------------|
| 'LINE' | Linear interpolation between levels |
| 'SUST' | Hold level until note release |
| 'LAST' | End of envelope |
| 'GOTO' | Jump to specific segment |
| 'GOTO_CONDITIONAL' | Conditional jumps |

#### Segment Structure
   (Byte numbers are relative to the start of this envelopeSegment)

 Field       Bytes                Name               Meaning and Data Format
   1         0-3         targetLevel                Target level value
   2         4-7         endingTime                 Time in microseconds
   3         8-11        segmentType                Segment type code

### Performance Modulation
Connect MIDI controllers to voice parameters using transfer functions:

#### Transfer Functions
- Piecewise linear mapping from controller to parameter
- Up to 8 points define the mapping curve
- Supports creative parameter shaping

#### Available Sources
- 'PITC': MIDI note number
- 'VOLU': MIDI note velocity
- 'MODW': MIDI modulation wheel
- 'SAMP': Keymap zone number

#### Available Destinations
- 'NVOL': Note volume
- 'ATIM': ADSR attack time
- 'ALEV': ADSR attack level
- 'SUST': ADSR sustain level
- 'SLEV': Sustain level
- 'RELS': Release time
- 'PITF': Pitch LFO period
- 'PITC': Pitch LFO amount
- 'VOLF': Volume LFO period
- 'VOLU': Volume LFO amount
- 'LPAM': LPF amount
- 'WAVE': Sample pointer offset

### Sample Rescaling (Legacy)
Archaic feature for adjusting 8-bit mono sample amplitude:

- Set `rescaleSamples` flag in INSTflags
- Clear `miscParmsMode` flag
- Fields reinterpret as multiplier/divisor values
- Only works with 8-bit mono samples

### Development Guidelines

#### Creating Multi-Sample Instruments
1. **Sample Preparation**: Record or select samples for different ranges
2. **Zone Planning**: Design keymap zones for smooth transitions
3. **Volume Balancing**: Ensure consistent levels across zones
4. **Testing**: Verify smooth transitions and no artifacts
5. **Optimization**: Minimize zone count while maintaining quality

#### Common Multi-Sample Configurations
- **Piano**: Low/mid/high ranges with velocity layers
- **Drums**: Individual samples for each drum piece
- **Guitar**: Different samples for different playing techniques
- **Strings**: Multiple articulations across keyboard

#### Performance Considerations
- More zones = higher CPU usage
- Balance quality vs. performance requirements
- Use efficient sample compression
- Consider memory constraints

#### Debugging Multi-Sample Issues
- Check zone boundaries for gaps/overlaps
- Verify sample resource IDs are correct
- Monitor voice allocation and CPU usage
- Test edge cases (velocity extremes, note boundaries)

### 'BANK' - Instrument Banks
Collections of instrument definitions organized for efficient access.

## Audio Processing Features

### Envelope Generation
HSB supports sophisticated envelope processing with up to 8 segments per envelope:

**Segment Types**:
- `'LINE'`: Linear interpolation between levels
- `'SUST'`: Sustain until note release
- `'LAST'`: End of envelope
- `'GOTO'`: Jump to specific segment
- `'GOTO_CONDITIONAL'`: Conditional jumps

**Envelope Parameters**:
- Target level (32-bit, format depends on destination)
- Ending time (32-bit microseconds)
- Segment type (4-character code)

### LFO Processing
Low-frequency oscillator support for pitch and amplitude modulation:

**Simple LFO** (`'DMOD'`):
- Sine wave at 5.55 Hz (0.18 second period)
- MIDI ModWheel control
- 1 semitone maximum depth

**Advanced LFO**:
- Configurable waveform, rate, and depth
- Multiple destinations (pitch, amplitude, filter)

### Filter Support
Digital filter definitions for tone shaping and effects processing.

## Technical Specifications

### Data Formats

#### Fixed-Point Arithmetic
Sample rates and other continuous values use 32-bit fixed-point format:
- High 16 bits: Integer part
- Low 16 bits: Fractional part
- Example: 44,100 Hz = 0xAC440000

#### IEEE Floating-Point
Extended format uses 80-bit MacOS floats:
- 1 sign bit, 15 exponent bits, 1 integer bit, 63 fraction bits

#### String Encoding
- Pascal strings (length byte followed by characters)
- No null termination
- Encrypted strings in metadata resources

### Byte Order
- **Preferred**: Big-endian (Motorola format)
- **Supported**: Little-endian (Intel format)
- Flag in resource headers indicates actual byte order

### Memory Management
- Resources can be embedded in instrument banks
- Reference counting for shared samples
- Optional encryption for content protection

## Integration with miniBAE

### Build Configuration
HSB support is enabled through these build flags:
```makefile
SF2_SUPPORT=1      # SoundFont 2.0 compatibility
DLS_SUPPORT=1      # DLS instrument support
MP3_DEC=1          # MP3 decoding
FLAC_DEC=1         # FLAC decoding
VORBIS_DEC=1       # OGG Vorbis decoding
```

### Playback Architecture
1. **Resource Loading**: Parse HSB file structure and extract resources
2. **Sample Management**: Load and cache audio samples with reference counting
3. **Voice Allocation**: Manage polyphony limits and voice stealing
4. **Real-time Synthesis**: Apply envelopes, LFOs, and filters during playback
5. **Effects Processing**: Handle reverb, chorus, and other effects

### Debugging Support
- Debug builds include resource validation
- Memory usage tracking with `BAE_GetSizeOfMemoryUsed()`
- Voice status monitoring with `GM_GetRealtimeAudioInformation()`

## File Extensions and Usage

- **`.rmf`**: Original Rich Music Format files
- **`.hsb`**: Headspace Sound Bank files
- **`.sbk`**: Sound bank files (DLS/SoundFont compatible)

## Compatibility Notes

- HSB is backward compatible with older RMF files
- Encryption is optional and can be disabled for development
- Compression algorithms vary by platform and build configuration
- Some legacy features may require specific build flags

## Development Guidelines

### Creating HSB Files
1. Design resource structure and assign IDs
2. Prepare audio samples in supported formats
3. Generate instrument definitions
4. Create song configurations if needed
5. Test with miniBAE playback engine

### Performance Optimization
- Use 'CACH' resources for large files
- Compress audio samples when possible
- Organize resources by usage frequency
- Consider memory constraints for embedded systems

### Error Handling
- Validate resource headers before processing
- Check for supported compression types
- Handle missing optional resources gracefully
- Provide fallbacks for unsupported features

---

*This specification is derived from the original Headspace RMF Format Documentation v0.08 (1998) and adapted for the miniBAE project.*
