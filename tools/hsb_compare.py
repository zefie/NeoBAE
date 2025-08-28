#!/usr/bin/env python3
import sys, struct, json

def parse_hsb_inst_keymaps(path):
    with open(path,'rb') as f:
        data = f.read()
    if data[0:4] != b'IREZ':
        raise SystemExit('Not an IREZ file')
    num = struct.unpack('>I', data[8:12])[0]
    offset = 12
    idx = 0
    insts = {}
    while offset + 12 <= len(data) and idx < num+1000:
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
        except Exception:
            break
        if rtype == 'INST':
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
                # extract key splits
                keymaps = []
                key_offset = off
                for i in range(keySplitCount):
                    if key_offset + 8 <= len(body):
                        low = body[key_offset]
                        high = body[key_offset+1]
                        ks_snd_be = struct.unpack(be+'H', body[key_offset+2:key_offset+4])[0]
                        ks_misc1_be = struct.unpack(be+'h', body[key_offset+4:key_offset+6])[0]
                        ks_misc2_be = struct.unpack(be+'h', body[key_offset+6:key_offset+8])[0]
                        keymaps.append((low,high,ks_snd_be,ks_misc1_be,ks_misc2_be))
                        key_offset += 8
                    else:
                        break
                insts[name or f'id{rid}'] = {'id': rid, 'keymaps': keymaps}
            except Exception:
                insts[name or f'id{rid}'] = {'id': rid, 'keymaps': []}
        if next_off == 0:
            offset = p+4+body_len
        else:
            offset = next_off
        idx += 1
    return insts

def compare(insts_a, insts_b, max_show=20):
    names = sorted(set(list(insts_a.keys()) + list(insts_b.keys())))
    diffs = []
    for name in names:
        a = insts_a.get(name, {'keymaps': []})['keymaps']
        b = insts_b.get(name, {'keymaps': []})['keymaps']
        if a != b:
            diffs.append({'name': name, 'a_count': len(a), 'b_count': len(b), 'a': a, 'b': b})
    print(f"Compared {len(names)} instruments, differences: {len(diffs)}")
    for i, d in enumerate(diffs[:max_show]):
        print(f"[{i+1}] {d['name']}: counts A={d['a_count']} B={d['b_count']}")
    if len(diffs) > max_show:
        print(f"... {len(diffs)-max_show} more differences suppressed ...")
    return diffs

if __name__=='__main__':
    if len(sys.argv) < 3:
        print('usage: hsb_compare.py a.hsb b.hsb [max_show]')
        sys.exit(1)
    a = sys.argv[1]
    b = sys.argv[2]
    max_show = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    ia = parse_hsb_inst_keymaps(a)
    ib = parse_hsb_inst_keymaps(b)
    diffs = compare(ia, ib, max_show=max_show)
    # write JSON to /tmp for inspection
    out = {'a': a, 'b': b, 'diffs': diffs}
    with open('/tmp/hsb_compare.json','w') as f:
        json.dump(out, f, indent=2)
    print('Wrote /tmp/hsb_compare.json')
