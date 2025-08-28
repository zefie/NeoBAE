#!/usr/bin/env python3
import sys,struct

# Minimal HSB/IREZ inspector - prints resource list and hexdumps first 64 bytes of INST/SND bodies
# Assumes big-endian on-disk layout (per HSB.md). Falls back to little-endian if header looks wrong.

def read_u32(f, endian='>'):
    return struct.unpack(endian+'I', f.read(4))[0]

def read_u16(f, endian='>'):
    return struct.unpack(endian+'H', f.read(2))[0]

def read_u8(f):
    return struct.unpack('B', f.read(1))[0]


def inspect(path):
    with open(path,'rb') as f:
        data = f.read()
    if len(data) < 12:
        print(path, "too small")
        return
    sig = data[0:4]
    if sig != b'IREZ':
        print(path, "missing IREZ signature:", sig)
        return
    # try big-endian
    ver = struct.unpack('>I', data[4:8])[0]
    num = struct.unpack('>I', data[8:12])[0]
    print(f"File: {path}\n  version: {ver}  resource count (claimed): {num}")
    offset = 12
    idx = 0
    while offset + 12 < len(data) and idx < num+1000:
        # read next resource header using big-endian assumptions
        try:
            next_off = struct.unpack('>I', data[offset:offset+4])[0]
            rtype = data[offset+4:offset+8].decode('ascii',errors='replace')
            rid = struct.unpack('>I', data[offset+8:offset+12])[0]
            p = offset+12
            name_len = data[p]
            name = data[p+1:p+1+name_len].decode('ascii',errors='replace')
            p = p+1+name_len
            body_len = struct.unpack('>I', data[p:p+4])[0]
            body = data[p+4:p+4+body_len]
        except Exception as e:
            print('  parse error at offset', offset, e)
            break
        print(f"[{idx}] type='{rtype}' id={rid} name='{name}' hdr_offset={offset} body_len={body_len}")
        if rtype == 'INST':
            # parse InstrumentResource from body (big-endian on-disk)
            # InstrumentResource layout (partial): sndResourceID (int16), midiRootKey (int16), panPlacement (char), flags1 (ubyte), flags2 (ubyte), smodResourceID (char), misc1 (int16), misc2 (int16), keySplitCount (int16)
            try:
                be = '>'
                off = 0
                sndResourceID = struct.unpack(be+'H', body[off:off+2])[0]; off+=2
                midiRootKey = struct.unpack(be+'h', body[off:off+2])[0]; off+=2
                panPlacement = struct.unpack('b', body[off:off+1])[0]; off+=1
                flags1 = struct.unpack('B', body[off:off+1])[0]; off+=1
                flags2 = struct.unpack('B', body[off:off+1])[0]; off+=1
                smodResourceID = struct.unpack('b', body[off:off+1])[0]; off+=1
                misc1 = struct.unpack(be+'h', body[off:off+2])[0]; off+=2
                misc2 = struct.unpack(be+'h', body[off:off+2])[0]; off+=2
                keySplitCount = struct.unpack(be+'h', body[off:off+2])[0]; off+=2
                # Per X_Formats.h the KeySplit array is inserted immediately after keySplitCount.
                # (tremoloCount and the rest follow after the KeySplit array.)
                print(f"   Instrument: sndID={sndResourceID} rootKey={midiRootKey} pan={panPlacement} flags1=0x{flags1:02x} flags2=0x{flags2:02x} keySplits={keySplitCount}")
                # Also show raw first 16 bytes of body for debugging
                raw_hex = ' '.join(f"{b:02x}" for b in body[:16])
                print(f"   raw[0:16]: {raw_hex}")
                raw_hex2 = ' '.join(f"{b:02x}" for b in body[16:32])
                print(f"   raw[16:32]: {raw_hex2}")
                
                # Per X_Formats.h: if keySplitCount is non-zero, the KeySplit array
                # immediately follows the keySplitCount field. key splits are 8 bytes
                # each: lowMidi(1), highMidi(1), sndResourceID(u16), misc1(i16), misc2(i16).
                key_offset = off  # KeySplit array begins immediately after keySplitCount
                print(f"   key splits start at offset {key_offset}")
                for i in range(keySplitCount):
                    if key_offset + 8 <= len(body):
                        low = body[key_offset]
                        high = body[key_offset+1]
                        # big-endian interpretation (on-disk expected)
                        ks_snd_be = struct.unpack(be+'H', body[key_offset+2:key_offset+4])[0]
                        ks_misc1_be = struct.unpack(be+'h', body[key_offset+4:key_offset+6])[0]
                        ks_misc2_be = struct.unpack(be+'h', body[key_offset+6:key_offset+8])[0]
                        key_offset += 8
                        raw_bytes = body[key_offset-8:key_offset]
                        raw_hex = ' '.join(f"{b:02x}" for b in raw_bytes)
                        # effective sndID: if split snd is zero, use instrument-level sndResourceID
                        effective_snd = ks_snd_be if ks_snd_be != 0 else sndResourceID
                        # miscParameter2 is treated as % volume; if zero the loader may set it to 100.
                        applied_vol = ks_misc2_be
                        if applied_vol == 0:
                            # defaulting behavior observed in C: when useSoundModifierAsRootKey is true,
                            # miscParameter2 of 0 becomes 100. We don't know that flag here per-split,
                            # so print the raw value and note that loader may default to 100.
                            vol_note = "(raw 0 - loader may set 100)"
                        else:
                            vol_note = ""
                        print(f"     split[{i}] range={low}-{high} eff_snd={effective_snd} snd_be={ks_snd_be} rootKey={ks_misc1_be} vol={ks_misc2_be} {vol_note} raw={raw_hex}")
                    else:
                        print('     split parse truncated')
                        break
            except Exception as e:
                print('   INST parse error:', e)
        elif rtype in ('snd ','esnd','csnd'):
                # hexdump first 64 bytes for snd types
                snippet = body[:64]
                hexs = ' '.join(f"{b:02x}" for b in snippet)
                print('   body[0:64]:', hexs)
        if next_off == 0:
            # no next pointer - try to continue after this resource
            offset = p+4+body_len
        else:
            offset = next_off
        idx += 1
    print('\n')

if __name__=='__main__':
    if len(sys.argv) < 2:
        print('usage: inspect_hsb.py file1 [file2 ...]')
        sys.exit(1)
    for path in sys.argv[1:]:
        inspect(path)
