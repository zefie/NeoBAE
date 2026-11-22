# BAEScript ReadMe

BAEScript is a lightweight scripting language embedded in NeoBAE for real-time MIDI and mixer manipulation.
Scripts execute once per playback tick (roughly every 15 ms) and can read/write song, channel, and mixer state.

## Quick Start

1. Create a script file with extension `.bscript`.
2. Run playbae with `--script`.

Example:

```bash
neobae/bin/playbae -f content/midi/1barloop.mid --script neobae/examples/advanced_controls.bscript -t 5
```

BAEScripts can also be used in the zefidi GUI, with a syntax-highlighting and linting editor.

## Language Basics

### Variables and Assignment

```javascript
var x = 42;
x = x + 1;
```

### Conditionals

```javascript
if (midi.timestamp < 5000) {
    ch[0].volume = 100;
} else {
    ch[0].volume = 80;
}
```

### Loops

```javascript
while (i < 4) {
    ch[i].pan = 64;
    i = i + 1;
}

for (i = 0; i < 4; i++) {
    ch[i].reverb = 48;
}
```

Notes:
- `for` supports `i++` and `i--` in the step clause.
- `while` loops are guarded to max 10000 iterations per tick.

### Operators

- Arithmetic: `+ - * / %`
- Comparison: `== != < > <= >=`
- Logical: `&& || !`

### Data Types

- Numbers: integer (`42`, `0xFF`)
- Strings: `'text'` or `"text"`
- Booleans: `true`, `false`

### Comments

```javascript
// line comment
/* block comment */
```

## Built-in Functions

### MIDI note events

```javascript
noteOn(ch, note, velocity);
noteOff(ch, note, velocity);
```

### Utility output/help

```javascript
print("Tempo", midi.tempo);
help();
```

### Math helpers

```javascript
abs(x)
min(a, b)
max(a, b)
clamp(x, lo, hi)
```

## Objects and Properties

## Event Handlers

BAEScript supports declarative event handlers:

```javascript
on.start({
    print("Playback started");
});
```

Supported events:

- `on.script({ ... });`
- `on.start({ ... });`
- `on.pause({ ... });`
- `on.resume({ ... });`
- `on.stop({ ... });`
- `on.loop({ ... });`
- `on.seek({ ... });`

Semantics:

- Event handlers are one-shot per occurrence.
- Handlers do not execute every tick unless the event re-occurs.
- If the same event is declared multiple times, the last declaration wins.

Notes:

- `on.script` fires once after script bind.
- `on.start` fires when playback session starts.
- `on.pause` and `on.resume` fire on state transitions.
- `on.stop` fires when the song reaches/stops at end.
- `on.loop` fires when playback naturally wraps from end back to start.
- `on.seek` fires when position jumps, including script-driven seeks via `midi.timestamp`, `midi.position`, or `midi.ticks` writes.

## `ch[N]` channel properties

`N` is MIDI channel index used by the existing BAEScript integration (typically 0..15).

Readable and writable:

- `ch[N].instrument` (0..127)
- `ch[N].volume` (CC7, 0..127)
- `ch[N].pan` (CC10, 0..127)
- `ch[N].expression` (CC11, 0..127)
- `ch[N].pitchbend` (0..16383, center 8192)
- `ch[N].mute` (0/1)
- `ch[N].reverb` (CC91, 0..127)
- `ch[N].chorus` (CC93, 0..127)
- `ch[N].solo` (0/1)

## `midi` properties and commands

Properties:

- `midi.timestamp` (ms, read/write)
- `midi.position` (alias of timestamp)
- `midi.ticks` (raw MIDI ticks, read/write)
- `midi.length` (ms, read-only)
- `midi.exporting` (0/1, read-only)
- `midi.volume` (song volume percent, read/write)
- `midi.tempo` (master tempo factor in percent, read/write, `100` = normal speed)
- `midi.tempobpm` (raw BPM, read/write)
- `midi.transpose` (semitones, read/write)

Commands:

- `midi.stop()`
- `midi.allnotesoff()`

### Tempo Notes

`midi.tempo` and `midi.tempobpm` are related but distinct:

- `midi.tempo` controls master tempo factor (percent scale).
- `midi.tempobpm` controls the song tempo directly in BPM.

Use `midi.tempobpm` when you want explicit musical tempo values like `120`.

### Position Notes

- `midi.timestamp` and `midi.position` are millisecond-based.
- `midi.ticks` is raw MIDI tick position (sequencer tick domain), not microseconds.
- Use `midi.ticks` when you need deterministic musical-grid positioning.

## `mixer` properties and commands

Properties:

- `mixer.volume` (global volume percent, read/write)
- `mixer.voices` (currently active voices, read-only)
- `mixer.reverbtype` (default reverb enum value, read/write)
- `mixer.classicchorus` (0/1, read/write)
- `mixer.stereodcpanfix` (0/1, read/write)
- `mixer.webtvmode` (0/1, read/write; WebTV velocity curve, LFO shapes and filter glide apply at once, default channel pans apply to songs started afterwards; only when built with `USE_WEBTV_MODE`)

Command:

- `mixer.reset()`

## `exporter` properties (only used when exporting)

- `exporter.loopcount` (Sets the number of loops when exporting)

## Execution Model and Important Behavior

BAEScript runs every tick, not once.

That means top-level statements are re-executed continuously while the song is running.
Event declarations are registered and triggered separately by transitions.

Example:

```javascript
var x = 0;
```

The declaration is evaluated each tick; if it includes an initializer it can reset values repeatedly.
Guard one-time initialization with conditions based on runtime state.

## Practical Patterns

### One-time init pattern

```javascript
if (midi.timestamp == 0 && mixer.volume != 90) {
    mixer.reset();
    mixer.volume = 90;
}
```

### Event-driven control pattern

```javascript
on.script({
    mixer.reset();
    midi.tempobpm = 120;
});

on.loop({
    print("looped");
});

on.seek({
    print("seek", midi.ticks);
});
```

### Tempo ramp with clamp

```javascript
if (midi.timestamp < 4000) {
    midi.tempo = clamp(100 + (midi.timestamp * 40 / 4000), 100, 140);
}
```

### BPM control

```javascript
if (midi.timestamp == 0) {
    midi.tempobpm = 120;
}
```

### Raw MIDI tick position control

```javascript
// Jump to MIDI tick 960 (often one quarter-note at TPQ=960, depending on file).
if (midi.timestamp == 0) {
    midi.ticks = 960;
}
```

### Channel batch edits with for-loop

```javascript
for (i = 0; i < 4; i++) {
    ch[i].reverb = 40;
    ch[i].chorus = 24;
}
```

### Repeat the song once when exporting

```javascript
on.script({
    exporter.loopcount = 2;
});
```

## Error Handling and Limits

- Parser errors report line/column to stderr.
- Undefined variable reads print an error and return `0`.
- Maximum script variables: 256.
- Safety cap: while/for execution is limited by the while guard (10000 iterations per tick).
- Some properties are read-only and ignore writes (`midi.length`, `midi.exporting`, `mixer.voices`).

## Files in the BAEScript system

- `neobae/src/script/baescript.h`
- `neobae/src/script/baescript_internal.h`
- `neobae/src/script/baescript_lexer.c`
- `neobae/src/script/baescript_parser.c`
- `neobae/src/script/baescript_vm.c`
- `neobae/src/script/baescript.c`

## Examples

- `neobae/examples/mixer_controls.bscript`
- `neobae/examples/advanced_controls.bscript`

These examples are good starting points for automation and live playback control.
