
# Fix Attempts for ARM64 Patchbank Loading Issue

## Fix Attempt #1: Enable pragma pack(1) for Android
**Root Cause Hypothesis:** The codebase was excluding Android from structure packing directives (`#pragma pack(1)`), which could cause structure layouts in memory to differ from the on-disk file format.

**Fix Applied:** Removed Android-specific exclusions from pragma pack directives in `X_PackStructures.h`.

**Result:** **FAILED** - Stack corruption crash:
```
stack corruption detected (-fstack-protector)
Fatal signal 6 (SIGABRT), code -1 (SI_QUEUE) in tid 22279 (Thread-3), pid 22106 (ie.miniBAEDroid)
Cmdline: com.zefie.miniBAEDroid
pid: 22106, tid: 22279, name: Thread-3  >>> com.zefie.miniBAEDroid <<<
      #02 pc 00000000000c52d8  /data/app/~~f88QesXf2kNewQrNwuzTQA==/com.zefie.miniBAEDroid-4cVPwe1Xg6o4DXDkQWpTmA==/base.apk!libminiBAE.so (offset 0x141c000) (BAEMixer_AddBankFromMemory+452) (BuildId: b297c1397a8ade6797a435deae8c4d0018e4c7f4)
      #03 pc 00000000000e3338  /data/app/~~f88QesXf2kNewQrNwuzTQA==/com.zefie.miniBAEDroid-4cVPwe1Xg6o4DXDkQWpTmA==/base.apk!libminiBAE.so (offset 0x141c000) (Java_org_minibae_Mixer__1addBuiltInPatches+60) (BuildId: b297c1397a8ade6797a435deae8c4d0018e4c7f4)
```

**Conclusion:** Forcing `pragma pack(1)` on ARM64 causes stack corruption, confirming the original exclusion was intentional.

---

## Fix Attempt #2: Fix X_PACKBY1 Alignment for Android
**Root Cause Hypothesis:** Android's `X_PACKBY1` definition was using `__attribute__ ((packed))` without the `aligned(__alignof__(short))` modifier that other GCC platforms use. This creates truly 1-byte aligned structures, which ARM64 cannot handle due to strict alignment requirements. Unaligned 16/32/64-bit memory access on ARM64 either traps (causing crashes) or produces garbage data (causing the instrument loading issue).

**Fix Applied:** Changed Android's `X_PACKBY1` definition from:
```c
#define X_PACKBY1 __attribute__ ((packed))
```
to:
```c
#define X_PACKBY1 __attribute__((packed,aligned(__alignof__(short))))
```

This ensures structures are packed but maintain at least 2-byte alignment, matching other GCC platforms and satisfying ARM64's alignment requirements.

**Result:** Testing failed, the program does not crash but the original issue remains.

---

## Fix Attempt #3: Fix Signed Shift and Add Debug Logging
**Root Cause Hypothesis:** The bit calculation in `GM_IsInstrumentUsed()` was using a signed cast before left-shifting: `bit = (int32_t)thePatch << 7`. Left-shifting signed integers can produce undefined behavior and compiler-specific results, especially on ARM64. Additionally, without proper debug output, we can't see exactly which instruments are being marked as "used" vs which ones are actually being attempted to load.

**Fix Applied:**
1. Changed `bit = (int32_t)thePatch << 7` to `bit = (uint32_t)thePatch << 7` to ensure unsigned arithmetic
2. Added debug logging to show which instruments are marked as used during loading
3. Added debug logging to show bit calculations for instruments 125-130 on Android

**Result:** 
```
Debug: Instrument 104 loaded successfully, refCount=1, stored=yes
  GM_IsInstrumentUsed: thePatch=125, bit=16000 (0x3e80)
  GM_IsInstrumentUsed: thePatch=126, bit=16128 (0x3f00)
  GM_IsInstrumentUsed: thePatch=127, bit=16256 (0x3f80)
  GM_IsInstrumentUsed: thePatch=128, bit=16384 (0x4000)
  GM_IsInstrumentUsed: thePatch=129, bit=16512 (0x4080)
  GM_IsInstrumentUsed: thePatch=130, bit=16640 (0x4100)
  Instrument 255 marked as used
Failed loading extra bank instrument 255, falling back to GM.
Trying to load instrument 255
```

---

## Platform Status
- **x86_64:** Working  
- **x86:** Working (probably)
- **ARM (32-bit):** Working (probably)
- **ARM64-v8a:** Fix #2 applied, testing failed
