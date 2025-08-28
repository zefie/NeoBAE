
## ADSR Envelope Processing

The engine implements ADSR (Attack, Decay, Sustain, Release) envelopes for dynamic volume control of voices. This system is used by HSB, DLS, and SoundFont instruments.

### Core Structures

```c
// Main ADSR envelope structure (from GenSnd.h)
struct GM_ADSR {
    XSDWORD currentTime;           // Current time position in envelope
    XSDWORD currentLevel;          // Current envelope level
    XSDWORD previousTarget;        // Previous target level
    XFIXED sustainingDecayLevel;   // Sustain level (fixed-point)
    XSDWORD ADSRLevel[ADSR_STAGES]; // Level for each stage (0-4096)
    XSDWORD ADSRTime[ADSR_STAGES]; // Time for each stage (in engine ticks)
    UNIT_TYPE ADSRFlags[ADSR_STAGES]; // Stage type flags
    UNIT_TYPE mode;                // Envelope mode
    XBYTE currentPosition;         // Current stage (0-7)
};

// Key constants (from GenPriv.h)
#define VOLUME_RANGE 4096          // Maximum envelope level
#define ADSR_STAGES 8              // Number of envelope stages
```

### Nuances of sustainingDecayLevel

The `sustainingDecayLevel` field is a critical XFIXED (16.16 fixed-point) value that modulates the envelope's sustain phase:

- **Default Value**: `XFIXED_1` (65536, or 1.0) - represents full scale sustain level
- **Range**: Can be any XFIXED value, including negative numbers
- **Usage**: Multiplied with `currentLevel` during real-time voice processing:
  ```c
  pVoice->NoteVolumeEnvelope = (INT16)XFixedMultiply(
      pVoice->volumeADSRRecord.currentLevel,
      pVoice->volumeADSRRecord.sustainingDecayLevel);
  ```

#### Special Behaviors

**Negative Values (Envelope Inversion)**:
- Values like `-XFIXED_1` (-65536) invert the envelope
- Used in HSB instruments for special effects
- Can create inverted sustain characteristics

**Zero Value (Voice Silencing)**:
- `sustainingDecayLevel = 0` immediately silences the voice
- Used for voice termination and note stealing
- Triggers automatic voice cleanup in the engine

#### Common Pitfalls

1. **Initialization Issues**: 
   - DLS/SF2 loaders may not set this field correctly
   - Default should be `XFIXED_1` for normal operation

2. **Scale Mismatches**:
   - Ensure consistent fixed-point scaling across all ADSR calculations
   - Mixing different scales can cause silent instruments

3. **Voice Termination Problems**:
   - Setting to 0 without proper ADSR mode can cause stuck voices
   - Check `ADSRFlags` when modifying this field

#### Debugging

- Monitor `sustainingDecayLevel` during voice processing
- Check for unexpected negative values in normal instruments
- Verify consistent scaling with `XFIXED_1` (65536) standard

### Envelope Stages

Each ADSR envelope supports up to 8 stages with different behaviors:

- **ADSR_LINEAR_RAMP**: Linear interpolation between levels
- **ADSR_SUSTAIN**: Hold current level until note off
- **ADSR_TERMINATE**: End envelope processing
- **ADSR_GOTO**: Jump to specific stage
- **ADSR_GOTO_CONDITIONAL**: Conditional jump based on sustain pedal
- **ADSR_RELEASE**: Release phase trigger

### Processing Flow

1. **Attack Phase**: Envelope rises from 0 to peak level
2. **Decay Phase**: Falls from peak to sustain level  
3. **Sustain Phase**: Holds sustain level while note is held
4. **Release Phase**: Falls from current level to 0 when note released

### Time Conversion

Envelope times are converted from DLS/SF2 milliseconds to engine ticks:

```c
// Convert milliseconds to engine ticks
engine_ticks = (milliseconds * sample_rate) / 1000;
```

### Integration with Instruments

- **DLS Instruments**: ADSR parameters loaded from DLS articulation data
- **SoundFont Instruments**: ADSR parameters loaded from SF2 preset zones
- **Fallback**: Default ADSR used when instrument lacks envelope data

### Voice-Level Processing

Each active voice (`GM_Voice`) contains:
```c
GM_ADSR volumeADSRRecord;           // ADSR envelope state
XSWORD NoteVolumeEnvelope;          // Current envelope scalar (0-VOLUME_RANGE)
XSWORD NoteVolumeEnvelopeBeforeLFO; // Pre-LFO envelope value
```

### Real-time Updates

The envelope is updated during each audio processing slice:
1. Advance `currentTime` by buffer slice duration
2. Calculate new `currentLevel` based on current stage
3. Apply envelope to voice volume
4. Handle stage transitions and note off events

### Debugging Tips

- Check `volumeADSRRecord.currentPosition` for stuck envelopes
- Verify `ADSRTime[]` values are reasonable (not 0 or extremely large)
- Monitor `NoteVolumeEnvelope` for proper scaling (0-4096 range)
- Use debug builds to trace envelope progression

### Pitfalls
- [GenSF2.c](../minibae/src/BAE_Source/Common/GenSF2.c)'s SF2 ADSR implementation is INCORRECT.
- miniBAE's built-in implementation for HSB is CORRECT.

### GenSF2 Issues
ADSR is not operating correctly for SF2 in our [GenSF2.c](../minibae/src/BAE_Source/Common/GenSF2.c).
- The notes hold when you hold them, but notes that should persist after release instantly cut off when the note is stopped (key release).
- Decay does not seem to work, notes that are supposed to decay even when held do not fade out smoothly, instead they step down in volume one single time before cutting off.
- The attack portion does not seem to work at all, as instruments that have an attack do not sound correct.