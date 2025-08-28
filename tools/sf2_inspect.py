#!/usr/bin/env python3
import sys
import struct

# Minimal SF2 inspector: prints sample headers (name,start,end,sampleRate,originalPitch,pitchCorrection)
# Usage: python3 tools/sf2_inspect.py /path/to/file.sf2

if len(sys.argv) < 2:
    print("Usage: sf2_inspect.py [--full] <sf2-file>")
    sys.exit(1)

full = False
args = sys.argv[1:]
if args[0] == '--full':
    full = True
    if len(args) < 2:
        print("Usage: sf2_inspect.py [--full] <sf2-file>")
        sys.exit(1)
    path = args[1]
else:
    path = args[0]

# Optional: --inst <id> to dump generators for a specific instrument
inst_to_dump = None
if path == '--inst':
    if len(args) < 3:
        print('Usage: sf2_inspect.py --inst <id> <sf2-file>')
        sys.exit(1)
    try:
        inst_to_dump = int(args[1])
    except Exception:
        print('Invalid instrument id')
        sys.exit(1)
    path = args[2]

with open(path, 'rb') as f:
    data = f.read()

# Find 'pdta' LIST then 'shdr' chunk
# Simple search for 'shdr' FOURCC
idx = data.find(b'shdr')
if idx == -1:
    print('No shdr chunk found')
    sys.exit(1)
# size is 4 bytes after 'shdr'
size = struct.unpack_from('<I', data, idx+4)[0]
start = idx + 8
end = start + size
entry_size = 46  # 20 name + 6*4 + 1 + 1 + 2 = 46 (SF2 sample record is 46 bytes)
count = size // entry_size
if full:
    print(f'shdr entries: {count}')
for i in range(count):
    off = start + i*entry_size
    name = data[off:off+20].split(b'\x00',1)[0].decode('latin1')
    s, e, sl, el, sr = struct.unpack_from('<5I', data, off+20)
    origPitch = struct.unpack_from('<B', data, off+40)[0]
    pitchCorr = struct.unpack_from('<b', data, off+41)[0]
    link, stype = struct.unpack_from('<HH', data, off+42)
if full:
    print(f'{i}: name="{name}" start={s} end={e} startloop={sl} endloop={el} sampleRate={sr} origPitch={origPitch} pitchCorr={pitchCorr} link={link} type={stype}')

if full:
    print('done')


print('\n-- FULL PDTA DUMP --')

# Find the PDTA LIST chunk and bound our parsing to it to avoid false positives
pdta_start = -1
pdta_end = -1
search_off = 0
while True:
    list_idx = data.find(b'LIST', search_off)
    if list_idx == -1:
        break
    # read size and type
    try:
        list_size = struct.unpack_from('<I', data, list_idx+4)[0]
        list_type = data[list_idx+8:list_idx+12]
    except Exception:
        break
    if list_type == b'pdta':
        # subchunks follow after the 12-byte LIST header
        pdta_start = list_idx + 12
        # list_size includes the 4-byte type, so actual PDTA data size is list_size - 4
        pdta_size = list_size - 4
        pdta_end = pdta_start + pdta_size
        break
    search_off = list_idx + 4

if pdta_start == -1:
    print('No PDTA LIST found; cannot perform full dump')
    sys.exit(0)

def iter_subchunks(start, end):
    pos = start
    while pos + 8 <= end:
        id = data[pos:pos+4]
        size = struct.unpack_from('<I', data, pos+4)[0]
        chunk_start = pos + 8
        yield id, chunk_start, size
        pos = chunk_start + size

# Walk subchunks within PDTA
phdr_found = False
inst_found = False
pbag_found = False
ibag_found = False
pgen_found = False
igen_found = False

for id, off, size in iter_subchunks(pdta_start, pdta_end):
    if id == b'phdr':
        phdr_found = True
        count = size // 38
        # Always print count; only dump full entries if not asking for a single instrument
        print(f'phdr entries: {count}')
        if inst_to_dump is None and full:
            for i in range(count):
                entry_off = off + i*38
                vals = struct.unpack_from('<20sHHHIII', data, entry_off)
                name = vals[0].split(b'\x00',1)[0].decode('latin1')
                preset, bank, bagIndex = vals[1], vals[2], vals[3]
                print(f'{i}: name="{name}" preset={preset} bank={bank} bagIndex={bagIndex}')
    elif id == b'inst':
        inst_found = True
        count = size // 22
        print(f'inst entries: {count}')
        if 'inst_list' not in globals():
            inst_list = []
        for i in range(count):
            entry_off = off + i*22
            vals = struct.unpack_from('<20sH', data, entry_off)
            name = vals[0].split(b'\x00',1)[0].decode('latin1')
            bagIndex = vals[1]
            # Collect instrument list for lookup. Only print details when not targeting a single instrument.
            inst_list.append({'id': i, 'name': name, 'bagIndex': bagIndex})
            if inst_to_dump is None and full:
                print(f'{i}: name="{name}" bagIndex={bagIndex}')
    elif id == b'pbag':
        pbag_found = True
        count = size // 4
        print(f'pbags entries: {count}')
        if inst_to_dump is None and full:
            for i in range(count if count < 200 else 200):
                genIndex, modIndex = struct.unpack_from('<HH', data, off + i*4)
                print(f'pbag {i}: genIndex={genIndex} modIndex={modIndex}')
    elif id == b'ibag':
        ibag_found = True
        count = size // 4
        print(f'ibags entries: {count}')
        ibag_list = []
        # Always collect ibag entries (needed for --inst); only print when not targeting a single instrument
        for i in range(count):
            genIndex, modIndex = struct.unpack_from('<HH', data, off + i*4)
            ibag_list.append({'genIndex': genIndex, 'modIndex': modIndex})
            if inst_to_dump is None and full and i < 2000:
                print(f'ibag {i}: genIndex={genIndex} modIndex={modIndex}')
    elif id == b'pgen':
        pgen_found = True
        count = size // 4
        print(f'pgen entries: {count}')
        if inst_to_dump is None and full:
            for i in range(count if count < 200 else 200):
                generator, amount = struct.unpack_from('<HH', data, off + i*4)
                print(f'pgen {i}: gen={generator} amount=0x{amount:04X} ({amount})')
    elif id == b'igen':
        igen_found = True
        count = size // 4
        print(f'igen entries: {count}')
        igen_list = []
        # Always collect igen entries (needed for --inst). Only print details when not targeting a single instrument
        for i in range(count):
            generator, amount = struct.unpack_from('<HH', data, off + i*4)
            igen_list.append({'gen': generator, 'amount': amount})
            if inst_to_dump is None and full and i < 50000:
                print(f'igen {i}: gen={generator} amount=0x{amount:04X} ({amount})')

if not phdr_found:
    print('No phdr found in PDTA')
if not inst_found:
    print('No inst found in PDTA')
if not pbag_found:
    print('No pbag found in PDTA')
if not ibag_found:
    print('No ibag found in PDTA')
if not pgen_found:
    print('No pgen found in PDTA')
if not igen_found:
    print('No igen found in PDTA')

print('\n-- END FULL DUMP --')

# If user requested a specific instrument, dump its instrument gens
if inst_to_dump is not None:
    if 'inst_list' not in globals() or 'ibag_list' not in globals() or 'igen_list' not in globals():
        print('Required PDTA subchunks not parsed to dump instrument')
        sys.exit(1)

    # find instrument entry
    inst_entry = None
    for ins in inst_list:
        if ins['id'] == inst_to_dump:
            inst_entry = ins
            break
    if not inst_entry:
        print(f'Instrument {inst_to_dump} not found')
        sys.exit(1)

    inst_bag = inst_entry['bagIndex']
    # find next instrument's bagIndex or use ibag length as end
    next_bag = None
    for ins in inst_list:
        if ins['id'] > inst_to_dump:
            next_bag = ins['bagIndex']
            break
    if next_bag is None:
        next_bag = len(ibag_list)

    print(f'\nInstrument {inst_to_dump} "{inst_entry["name"]}" bag range: {inst_bag}..{next_bag-1}')

    # For each ibag in that range, dump gens between genIndex..next.genIndex
    for ib in range(inst_bag, next_bag):
        genStart = ibag_list[ib]['genIndex']
        genEnd = ibag_list[ib+1]['genIndex'] if (ib+1) < len(ibag_list) else len(igen_list)
        print(f'  zone {ib}: genIndex={genStart}..{genEnd-1}')
        for g in range(genStart, genEnd):
            if g < 0 or g >= len(igen_list):
                continue
            gen = igen_list[g]['gen']
            amt = igen_list[g]['amount']
            # Map known generator IDs to names
            gen_names = {
                51: 'COARSE_TUNE', 52: 'FINE_TUNE', 53: 'SAMPLE_ID', 58: 'OVERRIDING_ROOT_KEY',
                43: 'KEY_RANGE', 41: 'INSTRUMENT', 46: 'KEYNUM', 48: 'INITIAL_ATTENUATION'
            }
            name = gen_names.get(gen, str(gen))
            # For key range, decode low/high
            if gen == 43:
                lo = amt & 0xFF
                hi = (amt >> 8) & 0xFF
                print(f'    gen {g}: {name} ({gen}) amount=0x{amt:04X} ({lo}-{hi})')
            else:
                print(f'    gen {g}: {name} ({gen}) amount=0x{amt:04X} ({amt})')
