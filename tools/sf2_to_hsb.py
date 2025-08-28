#!/usr/bin/env python3
"""
zefie note: This is not an accurate converter, in fact, it sucks.
But I am leaving it here, because for some SF2, its an interesting experiment.

-----------------------------------------------------------------------------

SF2 to HSB (Headspace Bank) Converter for miniBAE

This tool converts SoundFont 2 (SF2) files to Headspace Bank (HSB) format
compatible with miniBAE. The HSB format uses the IREZ resource structure
with INST (instrument), SND (sample), and other resources.

Usage: python3 sf2_to_hsb.py input.sf2 output.hsb [options]

Options:
    --bank-name "Name"      Set bank name (default: derived from filename)
    --bank-url "URL"        Set bank URL (default: empty)
    --bsn                   Add footer for Beatnik Editor
    --verbose               Enable verbose output
    --max-instruments N     Maximum instruments to convert (default: all)
    --sample-quality N      Sample quality (1-4, default: 3)
                           1 = 8-bit mono, 2 = 8-bit stereo
                           3 = 16-bit mono, 4 = 16-bit stereo

This converter creates a miniBAE-compatible HSB file containing:
- BANK resource with bank metadata
- VERS resource with version information  
- SND resources for each sample
- INST resources for each instrument/preset

The generated HSB file can be loaded directly by miniBAE using
BAEMixer_AddBankFromFile() or BAEMixer_AddBankFromMemory().
"""

import sys
import struct
import os
import argparse
from typing import Dict, List, Tuple, Optional


# FOURCC constants
FOURCC_RIFF = b'RIFF'
FOURCC_SFBK = b'sfbk'
FOURCC_LIST = b'LIST'
FOURCC_PDTA = b'pdta'
FOURCC_SHDR = b'shdr'
FOURCC_PHDR = b'phdr'
FOURCC_INST = b'inst'
FOURCC_IBAG = b'ibag'
FOURCC_IMOD = b'imod'
FOURCC_IGEN = b'igen'
FOURCC_PBAG = b'pbag'
FOURCC_PGEN = b'pgen'

BEPF_String = b'BEPF\x00\x00\x00\x01\x00\x00\x00\x00\x07'

# miniBAE resource types
ID_BANK = b'BANK'
ID_VERS = b'VERS'
ID_SND = b'snd '
ID_INST = b'INST'
IREZ_ID = b'IREZ'

# SF2 Generator IDs
SF2_GEN_START_ADDRS_OFFSET = 0
SF2_GEN_END_ADDRS_OFFSET = 1
SF2_GEN_STARTLOOP_ADDRS_OFFSET = 2
SF2_GEN_ENDLOOP_ADDRS_OFFSET = 3
SF2_GEN_START_ADDRS_COARSE_OFFSET = 4
SF2_GEN_MOD_LFO_TO_PITCH = 5
SF2_GEN_VIB_LFO_TO_PITCH = 6
SF2_GEN_MOD_ENV_TO_PITCH = 7
SF2_GEN_INITIAL_FILTER_FC = 8
SF2_GEN_INITIAL_FILTER_Q = 9
SF2_GEN_MOD_LFO_TO_FILTER_FC = 10
SF2_GEN_MOD_ENV_TO_FILTER_FC = 11
SF2_GEN_END_ADDRS_COARSE_OFFSET = 12
SF2_GEN_MOD_LFO_TO_VOLUME = 13
SF2_GEN_CHORUS_EFFECTS_SEND = 15
SF2_GEN_REVERB_EFFECTS_SEND = 16
SF2_GEN_PAN = 17
SF2_GEN_DELAY_MOD_LFO = 21
SF2_GEN_FREQ_MOD_LFO = 22
SF2_GEN_DELAY_VIB_LFO = 23
SF2_GEN_FREQ_VIB_LFO = 24
SF2_GEN_DELAY_MOD_ENV = 25
SF2_GEN_ATTACK_MOD_ENV = 26
SF2_GEN_HOLD_MOD_ENV = 27
SF2_GEN_DECAY_MOD_ENV = 28
SF2_GEN_SUSTAIN_MOD_ENV = 29
SF2_GEN_RELEASE_MOD_ENV = 30
SF2_GEN_KEYNUM_TO_MOD_ENV_HOLD = 31
SF2_GEN_KEYNUM_TO_MOD_ENV_DECAY = 32
SF2_GEN_DELAY_VOL_ENV = 33
SF2_GEN_ATTACK_VOL_ENV = 34
SF2_GEN_HOLD_VOL_ENV = 35
SF2_GEN_DECAY_VOL_ENV = 36
SF2_GEN_SUSTAIN_VOL_ENV = 37
SF2_GEN_RELEASE_VOL_ENV = 38
SF2_GEN_KEYNUM_TO_VOL_ENV_HOLD = 39
SF2_GEN_KEYNUM_TO_VOL_ENV_DECAY = 40
SF2_GEN_INSTRUMENT = 41
SF2_GEN_KEYRANGE = 43
SF2_GEN_VELRANGE = 44
SF2_GEN_STARTLOOP_ADDRS_COARSE_OFFSET = 45
SF2_GEN_KEYNUM = 46
SF2_GEN_VELOCITY = 47
SF2_GEN_INITIAL_ATTENUATION = 48
SF2_GEN_ENDLOOP_ADDRS_COARSE_OFFSET = 50
SF2_GEN_COARSE_TUNE = 51
SF2_GEN_FINE_TUNE = 52
SF2_GEN_SAMPLE_ID = 53
SF2_GEN_SAMPLE_MODES = 54
SF2_GEN_SCALE_TUNING = 56
SF2_GEN_EXCLUSIVE_CLASS = 57
SF2_GEN_OVERRIDING_ROOT_KEY = 58


class SF2Sample:
    def __init__(self, data: bytes, offset: int):
        fields = struct.unpack_from('<20s5I2BHH', data, offset)
        self.name = fields[0].split(b'\x00', 1)[0].decode('latin1', errors='ignore')
        self.start = fields[1]
        self.end = fields[2]
        self.startloop = fields[3]
        self.endloop = fields[4]
        self.sample_rate = fields[5]
        self.original_pitch = fields[6]
        self.pitch_correction = struct.unpack('b', struct.pack('B', fields[7]))[0]
        self.sample_link = fields[8]
        self.sample_type = fields[9]


class SF2Preset:
    def __init__(self, data: bytes, offset: int):
        fields = struct.unpack_from('<20sHHHIII', data, offset)
        self.name = fields[0].split(b'\x00', 1)[0].decode('latin1', errors='ignore')
        self.preset = fields[1]
        self.bank = fields[2]
        self.bag_index = fields[3]
        self.library = fields[4]
        self.genre = fields[5]
        self.morphology = fields[6]


class SF2Instrument:
    def __init__(self, data: bytes, offset: int):
        fields = struct.unpack_from('<20sH', data, offset)
        self.name = fields[0].split(b'\x00', 1)[0].decode('latin1', errors='ignore')
        self.bag_index = fields[1]


class SF2Bag:
    def __init__(self, data: bytes, offset: int):
        fields = struct.unpack_from('<HH', data, offset)
        self.gen_index = fields[0]
        self.mod_index = fields[1]


class SF2Generator:
    def __init__(self, data: bytes, offset: int):
        fields = struct.unpack_from('<HH', data, offset)
        self.type = fields[0]
        self.amount = fields[1]


class SF2ADSRParams:
    """SF2 ADSR envelope parameters extracted from generators"""
    def __init__(self):
        # Volume envelope (timecents and centibels)
        self.vol_delay = -12000      # SF2_GEN_DELAY_VOL_ENV
        self.vol_attack = -12000     # SF2_GEN_ATTACK_VOL_ENV  
        self.vol_hold = -12000       # SF2_GEN_HOLD_VOL_ENV
        self.vol_decay = -12000      # SF2_GEN_DECAY_VOL_ENV
        self.vol_sustain = 0         # SF2_GEN_SUSTAIN_VOL_ENV (centibels)
        self.vol_release = -12000    # SF2_GEN_RELEASE_VOL_ENV
        
        # Modulation envelope (timecents and 1/10ths of a percent)
        self.mod_delay = -12000      # SF2_GEN_DELAY_MOD_ENV
        self.mod_attack = -12000     # SF2_GEN_ATTACK_MOD_ENV
        self.mod_hold = -12000       # SF2_GEN_HOLD_MOD_ENV
        self.mod_decay = -12000      # SF2_GEN_DECAY_MOD_ENV
        self.mod_sustain = 0         # SF2_GEN_SUSTAIN_MOD_ENV
        self.mod_release = -12000    # SF2_GEN_RELEASE_MOD_ENV
        
        # Initial attenuation
        self.initial_attenuation = 0 # SF2_GEN_INITIAL_ATTENUATION
    
    def extract_from_generators(self, generators: List[SF2Generator]):
        """Extract ADSR parameters from SF2 generators"""
        for gen in generators:
            if gen.type == SF2_GEN_DELAY_VOL_ENV:
                self.vol_delay = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_ATTACK_VOL_ENV:
                self.vol_attack = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_HOLD_VOL_ENV:
                self.vol_hold = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_DECAY_VOL_ENV:
                self.vol_decay = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_SUSTAIN_VOL_ENV:
                self.vol_sustain = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_RELEASE_VOL_ENV:
                self.vol_release = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_DELAY_MOD_ENV:
                self.mod_delay = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_ATTACK_MOD_ENV:
                self.mod_attack = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_HOLD_MOD_ENV:
                self.mod_hold = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_DECAY_MOD_ENV:
                self.mod_decay = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_SUSTAIN_MOD_ENV:
                self.mod_sustain = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_RELEASE_MOD_ENV:
                self.mod_release = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_INITIAL_ATTENUATION:
                self.initial_attenuation = self._signed_16(gen.amount)
    
    @staticmethod
    def _signed_16(value: int) -> int:
        """Convert unsigned 16-bit to signed"""
        return struct.unpack('<h', struct.pack('<H', value))[0]


class SF2LFOParams:
    """SF2 LFO parameters extracted from generators"""
    def __init__(self):
        # Modulation LFO
        self.mod_lfo_delay = -12000      # SF2_GEN_DELAY_MOD_LFO (timecents)
        self.mod_lfo_freq = 0            # SF2_GEN_FREQ_MOD_LFO (cent Hz)
        self.mod_lfo_to_pitch = 0        # SF2_GEN_MOD_LFO_TO_PITCH (cents)
        self.mod_lfo_to_volume = 0       # SF2_GEN_MOD_LFO_TO_VOLUME (centibels)
        self.mod_lfo_to_filter_fc = 0    # SF2_GEN_MOD_LFO_TO_FILTER_FC (cents)
        
        # Vibrato LFO  
        self.vib_lfo_delay = -12000      # SF2_GEN_DELAY_VIB_LFO (timecents)
        self.vib_lfo_freq = 0            # SF2_GEN_FREQ_VIB_LFO (cent Hz)
        self.vib_lfo_to_pitch = 0        # SF2_GEN_VIB_LFO_TO_PITCH (cents)
        
        # Modulation envelope to pitch
        self.mod_env_to_pitch = 0        # SF2_GEN_MOD_ENV_TO_PITCH (cents)
        self.mod_env_to_filter_fc = 0    # SF2_GEN_MOD_ENV_TO_FILTER_FC (cents)
    
    def extract_from_generators(self, generators: List[SF2Generator]):
        """Extract LFO parameters from SF2 generators"""
        for gen in generators:
            if gen.type == SF2_GEN_DELAY_MOD_LFO:
                self.mod_lfo_delay = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_FREQ_MOD_LFO:
                self.mod_lfo_freq = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_MOD_LFO_TO_PITCH:
                self.mod_lfo_to_pitch = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_MOD_LFO_TO_VOLUME:
                self.mod_lfo_to_volume = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_MOD_LFO_TO_FILTER_FC:
                self.mod_lfo_to_filter_fc = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_DELAY_VIB_LFO:
                self.vib_lfo_delay = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_FREQ_VIB_LFO:
                self.vib_lfo_freq = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_VIB_LFO_TO_PITCH:
                self.vib_lfo_to_pitch = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_MOD_ENV_TO_PITCH:
                self.mod_env_to_pitch = self._signed_16(gen.amount)
            elif gen.type == SF2_GEN_MOD_ENV_TO_FILTER_FC:
                self.mod_env_to_filter_fc = self._signed_16(gen.amount)
    
    @staticmethod
    def _signed_16(value: int) -> int:
        """Convert unsigned 16-bit to signed"""
        return struct.unpack('<h', struct.pack('<H', value))[0]


class SF2Bank:
    def __init__(self, filepath: str):
        self.filepath = filepath
        self.samples = []
        self.presets = []
        self.instruments = []
        self.ibags = []
        self.pbags = []
        self.igens = []
        self.pgens = []
        self.sample_data = b''
        
        with open(filepath, 'rb') as f:
            self.data = f.read()
        
        self._parse()
    
    def _parse(self):
        """Parse the SF2 file structure"""
        # Find PDTA LIST chunk
        pdta_start, pdta_size = self._find_pdta_chunk()
        if pdta_start == -1:
            raise ValueError("No PDTA LIST found in SF2 file")
        
        pdta_end = pdta_start + pdta_size
        
        # Parse all subchunks within PDTA
        pos = pdta_start
        while pos + 8 <= pdta_end:
            chunk_id = self.data[pos:pos+4]
            chunk_size = struct.unpack_from('<I', self.data, pos+4)[0]
            chunk_data_start = pos + 8
            
            if chunk_id == FOURCC_SHDR:
                self._parse_samples(chunk_data_start, chunk_size)
            elif chunk_id == FOURCC_PHDR:
                self._parse_presets(chunk_data_start, chunk_size)
            elif chunk_id == b'inst':
                self._parse_instruments(chunk_data_start, chunk_size)
            elif chunk_id == FOURCC_IBAG:
                self._parse_ibags(chunk_data_start, chunk_size)
            elif chunk_id == FOURCC_PBAG:
                self._parse_pbags(chunk_data_start, chunk_size)
            elif chunk_id == FOURCC_IGEN:
                self._parse_igens(chunk_data_start, chunk_size)
            elif chunk_id == FOURCC_PGEN:
                self._parse_pgens(chunk_data_start, chunk_size)
            
            pos = chunk_data_start + chunk_size
        
        # Find sample data
        self._find_sample_data()
    
    def _find_pdta_chunk(self) -> Tuple[int, int]:
        """Find the PDTA LIST chunk"""
        pos = 0
        while pos < len(self.data) - 12:
            if self.data[pos:pos+4] == FOURCC_LIST:
                list_size = struct.unpack_from('<I', self.data, pos+4)[0]
                list_type = self.data[pos+8:pos+12]
                if list_type == FOURCC_PDTA:
                    return pos + 12, list_size - 4
            pos += 1
        return -1, 0
    
    def _find_sample_data(self):
        """Find the sample data chunk (sdta/smpl)"""
        pos = 0
        while pos < len(self.data) - 12:
            if self.data[pos:pos+4] == FOURCC_LIST:
                list_size = struct.unpack_from('<I', self.data, pos+4)[0]
                list_type = self.data[pos+8:pos+12]
                if list_type == b'sdta':
                    # Look for smpl chunk within sdta
                    sdta_start = pos + 12
                    sdta_end = sdta_start + list_size - 4
                    sub_pos = sdta_start
                    while sub_pos + 8 <= sdta_end:
                        sub_id = self.data[sub_pos:sub_pos+4]
                        sub_size = struct.unpack_from('<I', self.data, sub_pos+4)[0]
                        if sub_id == b'smpl':
                            self.sample_data = self.data[sub_pos+8:sub_pos+8+sub_size]
                            return
                        sub_pos += 8 + sub_size
            pos += 1
    
    def _parse_samples(self, start: int, size: int):
        """Parse sample headers"""
        count = size // 46  # SF2 sample header is 46 bytes
        for i in range(count):
            sample = SF2Sample(self.data, start + i * 46)
            if sample.name != 'EOS':  # Skip terminal sample
                self.samples.append(sample)
    
    def _parse_presets(self, start: int, size: int):
        """Parse preset headers"""
        count = size // 38  # SF2 preset header is 38 bytes
        for i in range(count):
            preset = SF2Preset(self.data, start + i * 38)
            if preset.name != 'EOP':  # Skip terminal preset
                self.presets.append(preset)
    
    def _parse_instruments(self, start: int, size: int):
        """Parse instrument headers"""
        count = size // 22  # SF2 instrument header is 22 bytes
        for i in range(count):
            inst = SF2Instrument(self.data, start + i * 22)
            if inst.name != 'EOI':  # Skip terminal instrument
                self.instruments.append(inst)
    
    def _parse_ibags(self, start: int, size: int):
        """Parse instrument bags"""
        count = size // 4
        for i in range(count):
            bag = SF2Bag(self.data, start + i * 4)
            self.ibags.append(bag)
    
    def _parse_pbags(self, start: int, size: int):
        """Parse preset bags"""
        count = size // 4
        for i in range(count):
            bag = SF2Bag(self.data, start + i * 4)
            self.pbags.append(bag)
    
    def _parse_igens(self, start: int, size: int):
        """Parse instrument generators"""
        count = size // 4
        for i in range(count):
            gen = SF2Generator(self.data, start + i * 4)
            self.igens.append(gen)
    
    def _parse_pgens(self, start: int, size: int):
        """Parse preset generators"""
        count = size // 4
        for i in range(count):
            gen = SF2Generator(self.data, start + i * 4)
            self.pgens.append(gen)


# SND third-format FourCC and codec types
X_THIRD_SOUND_FORMAT = 0x0003  # XSndHeader3
C_NONE = b'none'               # Uncompressed PCM
SINE = b'SINE'                 # LFO wave shape

# Utility packers (big-endian)
def _be16(n: int) -> bytes:
    return struct.pack('>H', n & 0xFFFF)

def _be32(n: int) -> bytes:
    return struct.pack('>I', n & 0xFFFFFFFF)

class HSBWriter:
    """Write IREZ resource files in the exact layout miniBAE expects.

    Layout:
      Header: 'IREZ' (4) + version(4, BE, value 1) + resourceCount(4, BE)
      For each resource entry:
        nextOffset (uint32 BE) -> absolute file offset of next entry, or EOF for last
        resourceType (4 bytes)
        resourceID (uint32 BE)
        resourceName (Pascal string: 1 byte len, then bytes)
        resourceLength (uint32 BE)
        resourceData (resourceLength bytes)
    """

    def __init__(self, filepath: str):
        self.filepath = filepath
        self.resources: List[Dict[str, object]] = []

    def add_resource(self, res_type: bytes, res_id: int, data: bytes, name: str = ""):
        self.resources.append({
            'type': res_type,
            'id': int(res_id),
            'data': data,
            'name': name or ""
        })

    def write(self, bsn: Optional[bytes] = None):
        entries: List[bytes] = []
        # Prebuild entries with placeholder next pointer to compute offsets
        for res in self.resources:
            name_bytes = res['name'].encode('utf-8')[:255] if res['name'] else b''
            name_p = bytes([len(name_bytes)]) + name_bytes
            entry = bytearray()
            entry.extend(b"\xFF\xFF\xFF\xFF")  # placeholder for nextOffset
            entry.extend(res['type'])
            entry.extend(_be32(int(res['id'])))
            entry.extend(name_p)
            entry.extend(_be32(len(res['data'])))
            entry.extend(res['data'])
            entries.append(bytes(entry))

        header = IREZ_ID + _be32(1) + _be32(len(entries))
        # Compute absolute offsets for each entry
        offsets: List[int] = []
        cur = len(header)
        for ent in entries:
            offsets.append(cur)
            cur += len(ent)

        # Patch nextOffset for each entry
        patched_entries: List[bytes] = []
        for i, ent in enumerate(entries):
            next_off = cur if i == len(entries) - 1 else offsets[i + 1]
            patched_entries.append(_be32(next_off) + ent[4:])

        # Write file
        with open(self.filepath, 'wb') as f:
            f.write(header)
            for ent in patched_entries:
                f.write(ent)
            if bsn:
                f.write(BEPF_String + bsn.encode('utf-8') if isinstance(bsn, str) else BEPF_String + bsn)

def sf2_timecents_to_microseconds(timecents: int) -> int:
    """Convert SF2 timecents to microseconds (miniBAE time unit)"""
    if timecents <= -12000:
        return 0
    if timecents >= 8000:
        return 100000000  # 100 seconds max
    
    # SF2 formula: time = 2^(timecents/1200) seconds
    # Convert to microseconds: * 1,000,000
    import math
    seconds = math.pow(2.0, timecents / 1200.0)
    microseconds = int(seconds * 1000000)
    
    # Clamp to reasonable range
    return max(1000, min(100000000, microseconds))


def sf2_centibels_to_level(centibels: int, volume_range: int = 65536) -> int:
    """Convert SF2 centibels to miniBAE level (0-65536)"""
    if centibels >= 96000:  # Silence
        return 0
    if centibels <= 0:  # Full volume
        return volume_range
    
    # SF2 formula: level = 10^(-centibels/200)
    # 200 centibels = 20 dB = 10:1 ratio
    import math
    ratio = math.pow(10.0, -centibels / 200.0)
    level = int(volume_range * ratio)
    
    return max(0, min(volume_range, level))


def sf2_cent_hz_to_period_us(cent_hz: int) -> int:
    """Convert SF2 cent Hz to LFO period in microseconds"""
    if cent_hz <= -16000:
        return 1000000  # 1 Hz default
    
    # SF2 formula: freq = 8.176 * 2^(cent_hz/1200) Hz
    # Period = 1/freq seconds = 1,000,000/freq microseconds
    import math
    frequency = 8.176 * math.pow(2.0, cent_hz / 1200.0)
    
    # Clamp frequency to reasonable range (0.1 Hz to 100 Hz)
    frequency = max(0.1, min(100.0, frequency))
    
    period_us = int(1000000.0 / frequency)
    return max(10000, min(10000000, period_us))  # 10ms to 10s


def create_minibae_adsr(adsr_params: SF2ADSRParams) -> bytes:
    """Create miniBAE ADSR structure following GenSF2.c format"""
    
    # Convert SF2 timecents to microseconds
    attack_us = sf2_timecents_to_microseconds(adsr_params.vol_attack)
    sustain_level = sf2_centibels_to_level(adsr_params.vol_sustain, 65536)
    release_us = sf2_timecents_to_microseconds(adsr_params.vol_release)
    
    # Ensure minimum times
    attack_us = max(attack_us, 1000)
    release_us = max(release_us, 1000)
    
    # Create ADSR data following exact GenSF2 logic:
    # Unit type: 'ADSR'
    # Unit count: number of stages
    # Stages: Level (big-endian), Time (big-endian), Flag (FOURCC)
    adsr_data = bytearray()
    adsr_data.extend(b'ADSR')
    
    stages = []
    # Stage 0: Attack to full volume
    stages.append((65536, attack_us, b'TERM'))
    # Stage 1: Sustain
    stages.append((sustain_level, 0, b'SUST'))
    # Stage 2: Release to silence (if release > 0)
    if release_us > 0:
        stages.append((0, release_us, b'RELS'))
    
    adsr_data.append(len(stages))
    for level, time, flag in stages:
        adsr_data.extend(struct.pack('>I', level & 0xFFFFFFFF))
        adsr_data.extend(struct.pack('>I', time))
        adsr_data.extend(flag)
    
    return bytes(adsr_data)


def create_minibae_lfo_units(lfo_params: SF2LFOParams, lfo_type: str) -> bytes:
    """Create miniBAE LFO unit(s) matching GenPatch.c extended INST parsing.

    Encodes one unit with:
      - unitType (FOURCC: 'PITC', 'VOLU', 'LPFR')
      - unitSubCount (1 or 2)
      - unitSub entries (level,time,flag) x N
      - period (usec), waveShape ('SINE'), DC_feed (0), level (scaled depth)
    """
    if lfo_type == 'modulation':
        delay_tc = lfo_params.mod_lfo_delay
        freq_ch = lfo_params.mod_lfo_freq
        to_pitch = lfo_params.mod_lfo_to_pitch
        to_volume = lfo_params.mod_lfo_to_volume
        to_filter = lfo_params.mod_lfo_to_filter_fc
    elif lfo_type == 'vibrato':
        delay_tc = lfo_params.vib_lfo_delay
        freq_ch = lfo_params.vib_lfo_freq
        to_pitch = lfo_params.vib_lfo_to_pitch
        to_volume = 0
        to_filter = 0
    else:
        return b''

    # choose target and depth
    target = None
    depth = 0
    if to_pitch:
        target = b'PITC'
        # map cents to 0..256 approx
        depth = max(1, min(256, abs(to_pitch) // 8))
    elif to_volume:
        target = b'VOLU'
        depth = max(1, min(256, abs(to_volume) // 64))
    elif to_filter:
        target = b'LPFR'
        depth = max(1, min(256, abs(to_filter) // 16))
    else:
        return b''

    delay_us = sf2_timecents_to_microseconds(delay_tc) if delay_tc > -12000 else 0
    period_us = sf2_cent_hz_to_period_us(freq_ch)

    unit = bytearray()
    unit.extend(target)

    # Envelope for LFO amount
    if delay_us > 0:
        unit.append(2)  # two stages
        # stage 1: 0 level for delay duration
        unit.extend(_be32(0))
        unit.extend(_be32(delay_us))
        unit.extend(b'LINE')
        # stage 2: full level and terminate
        unit.extend(_be32(65536))
        unit.extend(_be32(1))
        unit.extend(b'LAST')
    else:
        unit.append(1)
        unit.extend(_be32(65536))
        unit.extend(_be32(0))
        unit.extend(b'LAST')

    # Period, WaveShape, DC_feed, level
    unit.extend(_be32(period_us))
    unit.extend(SINE)
    unit.extend(_be32(0))
    unit.extend(_be32(depth))
    return bytes(unit)


def convert_preset_to_inst(sf2_bank: SF2Bank, preset: SF2Preset, hsb_writer: HSBWriter, snd_cache: dict, current_snd_id: int, args) -> tuple:
    """Convert an SF2 preset to miniBAE INST format with ADSR and LFO support
    Returns: (inst_data_bytes, snd_resource_id_used, new_current_snd_id)
    """
    
    # Extract generators for this preset
    generators = []
    adsr_params = SF2ADSRParams()
    lfo_params = SF2LFOParams()
    sample_id = None
    midi_root_key: Optional[int] = None
    sf2_sample_idx_used: Optional[int] = None
    coarse_tune_semitones: int = 0
    fine_tune_cents: int = 0
    
    # Get preset bags and generators
    if preset.bag_index < len(sf2_bank.pbags):
        # Find end of preset bags
        next_bag_index = len(sf2_bank.pbags)
        for p in sf2_bank.presets:
            if p.bag_index > preset.bag_index:
                next_bag_index = p.bag_index
                break
        
        # Process all bags for this preset
        for bag_idx in range(preset.bag_index, min(next_bag_index, len(sf2_bank.pbags))):
            bag = sf2_bank.pbags[bag_idx]
            
            # Find end of generators for this bag
            next_gen_index = len(sf2_bank.pgens)
            if bag_idx + 1 < len(sf2_bank.pbags):
                next_gen_index = sf2_bank.pbags[bag_idx + 1].gen_index
            
            # Extract generators
            for gen_idx in range(bag.gen_index, min(next_gen_index, len(sf2_bank.pgens))):
                if gen_idx < len(sf2_bank.pgens):
                    generators.append(sf2_bank.pgens[gen_idx])
    
    # If we found an instrument reference, get instrument-level generators too
    instrument_id = None
    for gen in generators:
        if gen.type == SF2_GEN_INSTRUMENT:
            instrument_id = gen.amount
            break
    
    if instrument_id is not None and instrument_id < len(sf2_bank.instruments):
        instrument = sf2_bank.instruments[instrument_id]
        
        # Get instrument bags and generators
        if instrument.bag_index < len(sf2_bank.ibags):
            next_bag_index = len(sf2_bank.ibags)
            for i in sf2_bank.instruments:
                if i.bag_index > instrument.bag_index:
                    next_bag_index = i.bag_index
                    break
            
            # Process instrument bags
            for bag_idx in range(instrument.bag_index, min(next_bag_index, len(sf2_bank.ibags))):
                bag = sf2_bank.ibags[bag_idx]
                
                next_gen_index = len(sf2_bank.igens)
                if bag_idx + 1 < len(sf2_bank.ibags):
                    next_gen_index = sf2_bank.ibags[bag_idx + 1].gen_index
                
                # Extract instrument generators
                for gen_idx in range(bag.gen_index, min(next_gen_index, len(sf2_bank.igens))):
                    if gen_idx < len(sf2_bank.igens):
                        gen = sf2_bank.igens[gen_idx]
                        generators.append(gen)
                        # Look for sample ID in instrument generators
                        if gen.type == SF2_GEN_SAMPLE_ID:
                            sf2_sample_idx_used = gen.amount
                        if gen.type == SF2_GEN_OVERRIDING_ROOT_KEY:
                            # signed 16 per SF2 spec
                            midi_root_key = struct.unpack('<h', struct.pack('<H', gen.amount))[0]
    
    # If no specific sample found, skip
    if sf2_sample_idx_used is None:
        return None, None, current_snd_id
    
    # Extract ADSR and LFO parameters from generators
    if args.enable_adsr:
        adsr_params.extract_from_generators(generators)
        if args.verbose and generators:
            print(f"    ADSR: A={adsr_params.vol_attack}tc D={adsr_params.vol_decay}tc S={adsr_params.vol_sustain}cb R={adsr_params.vol_release}tc")
    
    if args.enable_lfo:
        lfo_params.extract_from_generators(generators)
        if args.verbose and (lfo_params.mod_lfo_to_pitch != 0 or lfo_params.mod_lfo_to_volume != 0 or lfo_params.vib_lfo_to_pitch != 0):
            print(f"    LFO: ModFreq={lfo_params.mod_lfo_freq}ch VibFreq={lfo_params.vib_lfo_freq}ch")

    # Gather tuning offsets from combined preset+instrument generators
    def _signed16(u: int) -> int:
        return struct.unpack('<h', struct.pack('<H', u))[0]
    for g in generators:
        if g.type == SF2_GEN_COARSE_TUNE:
            coarse_tune_semitones += _signed16(g.amount)
        elif g.type == SF2_GEN_FINE_TUNE:
            fine_tune_cents += _signed16(g.amount)

    # Compute midi root key: override > sample.original_pitch > 60
    if midi_root_key is None:
        if sf2_sample_idx_used is not None and 0 <= sf2_sample_idx_used < len(sf2_bank.samples):
            sp = sf2_bank.samples[sf2_sample_idx_used]
            if sp.original_pitch != 255:
                midi_root_key = int(sp.original_pitch)
    if midi_root_key is None:
        midi_root_key = 60

    # Note on tuning strategy:
    # - Use BOTH approaches: apply coarse tuning to SND baseKey, fine tuning via masterRootKey
    # - This gives us precise control over both discrete semitones and fractional cents
    if sf2_sample_idx_used is not None and 0 <= sf2_sample_idx_used < len(sf2_bank.samples):
        sp = sf2_bank.samples[sf2_sample_idx_used]
        fine_total_cents = fine_tune_cents + int(sp.pitch_correction)
    else:
        fine_total_cents = fine_tune_cents
    semitone_adjust = int(round(fine_total_cents / 100.0)) if fine_total_cents else 0
    
    # Apply coarse tuning to SND, fine tuning via masterRootKey
    coarse_adjusted_root = midi_root_key + coarse_tune_semitones
    coarse_adjusted_root = max(0, min(127, coarse_adjusted_root))
    
    # Fine tuning goes through masterRootKey (can be fractional)
    fine_adjusted_root = coarse_adjusted_root + (fine_total_cents / 100.0)
    fine_adjusted_root = max(0, min(127, fine_adjusted_root))
    
    if args.verbose:
        print(f"    Tuning: SF2 root≈{midi_root_key} (coarse {coarse_tune_semitones:+} st, fine {fine_tune_cents:+} ct, sampCorr {fine_total_cents - fine_tune_cents:+} ct). SND baseKey: {coarse_adjusted_root}, masterRootKey: {fine_adjusted_root:.2f}")
    
    # Create SND with coarse tuning applied
    cache_key = (sf2_sample_idx_used, coarse_tune_semitones)  # Cache by sample + coarse tuning
    if cache_key in snd_cache:
        sample_id = snd_cache[cache_key]
    else:
        sample = sf2_bank.samples[sf2_sample_idx_used]
        snd_data = convert_sample_to_snd(sf2_bank, sample, current_snd_id, args.sample_quality, coarse_tune_semitones)
        if snd_data:
            hsb_writer.add_resource(ID_SND, current_snd_id, snd_data, sample.name)
            snd_cache[cache_key] = current_snd_id
            sample_id = current_snd_id
            current_snd_id += 1
        else:
            return None, None, current_snd_id
    
    # Create InstrumentResource base structure (24 bytes) per X_Formats.h
    inst_data = bytearray(24)
    struct.pack_into('>H', inst_data, 0, sample_id)                     # sndResourceID
    # Use masterRootKey for fine tuning (coarse tuning already applied to SND)
    struct.pack_into('>h', inst_data, 2, int(round(fine_adjusted_root))) # midiRootKey (masterRootKey)
    struct.pack_into('>b', inst_data, 4, 0)                             # panPlacement
    # flags1: ZBF_extendedFormat (0x02) | ZBF_useSampleRate (0x08)
    flags1 = 0x02 | 0x08
    struct.pack_into('>B', inst_data, 5, flags1)
    # flags2: ZBF_playAtSampledFreq (0x40) for percussion bank 128, else 0x00
    flags2 = 0x40 if getattr(preset, 'bank', 0) == 128 else 0x00
    struct.pack_into('>B', inst_data, 6, flags2)
    struct.pack_into('>b', inst_data, 7, 0)                             # smodResourceID
    struct.pack_into('>h', inst_data, 8, 0)                             # miscParameter1
    # Apply initial attenuation to volume
    volume = 127
    if adsr_params.initial_attenuation > 0:
        attenuation_level = sf2_centibels_to_level(adsr_params.initial_attenuation, 65536)
        volume = (127 * attenuation_level) // 65536
    volume = max(1, min(127, volume))  # Ensure volume is not zero
    
    struct.pack_into('>h', inst_data, 10, volume)  # miscParameter2 (volume)
    struct.pack_into('>h', inst_data, 12, 0)                            # keySplitCount
    struct.pack_into('>h', inst_data, 14, 0)                            # tremoloCount
    struct.pack_into('>h', inst_data, 16, -32768)                       # tremoloEnd (0x8000)
    struct.pack_into('>h', inst_data, 18, 0)                            # reserved_3
    struct.pack_into('>h', inst_data, 20, 0)                            # descriptorName (unused)
    struct.pack_into('>h', inst_data, 22, 0)                            # descriptorFlags (unused)

    # Extended payload
    ext = bytearray()
    # Two zero-length Pascal strings (descriptor name + flags)
    ext.extend(b"\x00\x00")
    # 12 bytes global reserved
    ext.extend(b"\x00" * 12)

    # Collect units
    units: List[bytes] = []
    if args.enable_adsr:
        units.append(create_minibae_adsr(adsr_params))
    if args.enable_lfo:
        mod = create_minibae_lfo_units(lfo_params, 'modulation')
        if mod:
            units.append(mod)
        vib = create_minibae_lfo_units(lfo_params, 'vibrato')
        if vib:
            units.append(vib)

    # Unit count then units
    ext.append(len(units))
    for u in units:
        ext.extend(u)

    return bytes(inst_data + ext), sample_id, current_snd_id


def create_bank_resource(bank_name: str, bank_url: str) -> bytes:
    """Create a BANK resource"""
    bank_data = bytearray(1024)  # Large enough for bank name and URL
    
    struct.pack_into('>I', bank_data, 0, 1)  # version (big-endian)
    
    # Bank URL (first 4096 bytes)
    bank_url_bytes = bank_url.encode('utf-8')[:511]
    bank_data[4:4+len(bank_url_bytes)] = bank_url_bytes
    bank_data[4+len(bank_url_bytes)] = 0  # null terminator
    
    # Bank name (next 4096 bytes)
    bank_name_bytes = bank_name.encode('utf-8')[:511]
    bank_data[512:512+len(bank_name_bytes)] = bank_name_bytes
    bank_data[512+len(bank_name_bytes)] = 0  # null terminator
    
    return bytes(bank_data)


def create_version_resource() -> bytes:
    """Create a VERS resource (big-endian shorts)"""
    return struct.pack('>hhh', 1, 0, 0)  # version 1.0.0


def convert_sample_to_snd(sf2_bank: SF2Bank, sample: SF2Sample, resource_id: int, quality: int, coarse_tune_semitones: int = 0) -> bytes:
    """Convert an SF2 sample to a valid miniBAE 'snd ' resource (XSndHeader3, C_NONE)."""
    if not sf2_bank.sample_data or sample.start >= sample.end:
        return b''

    # SF2 sample data is 16-bit little-endian PCM in 'smpl'
    data_start = sample.start * 2
    data_end = sample.end * 2
    if data_start >= len(sf2_bank.sample_data) or data_end > len(sf2_bank.sample_data):
        return b''

    pcm16 = sf2_bank.sample_data[data_start:data_end]

    # Optionally convert to 8-bit if requested (not recommended)
    is_16_bit = quality >= 3
    if not is_16_bit:
        import array
        src = array.array('h')
        src.frombytes(pcm16)
        dst = array.array('B')
        for v in src:
            b = (v // 256) + 128
            dst.append(max(0, min(255, b)))
        pcm = dst.tobytes()
        bit_size = 8
    else:
        pcm = pcm16
        bit_size = 16

    channels = 1  # mono only for now
    frames = len(pcm) // (channels * (bit_size // 8))
    
    # Force all samples to 22050 Hz for consistency
    sample_rate = 22050
    
    # Resample if necessary
    original_rate = sample.sample_rate or 22050
    if original_rate != sample_rate and original_rate > 0:
        import math
        ratio = sample_rate / original_rate
        new_frames = int(frames * ratio)
        if new_frames > 0:
            # Simple linear interpolation resampling
            new_pcm = bytearray()
            for i in range(new_frames):
                src_pos = i / ratio
                src_idx = int(src_pos)
                if src_idx >= frames - 1:
                    # Use last sample
                    sample_val = pcm[frames - 1 * 2:frames * 2] if is_16_bit else pcm[frames - 1:frames]
                else:
                    # Linear interpolation
                    frac = src_pos - src_idx
                    if is_16_bit:
                        val1 = struct.unpack('<h', pcm[src_idx * 2:(src_idx + 1) * 2])[0]
                        val2 = struct.unpack('<h', pcm[(src_idx + 1) * 2:(src_idx + 2) * 2])[0]
                        val = int(val1 + (val2 - val1) * frac)
                        new_pcm.extend(struct.pack('<h', val))
                    else:
                        val1 = pcm[src_idx]
                        val2 = pcm[src_idx + 1]
                        val = int(val1 + (val2 - val1) * frac)
                        new_pcm.append(val)
            pcm = bytes(new_pcm)
            frames = new_frames

    # Loop points in frames (clamped) - scale with resampling
    loop_scale = sample_rate / original_rate if original_rate > 0 else 1.0
    loop_start = max(0, int((sample.startloop - sample.start) * loop_scale))
    loop_end = max(0, int((sample.endloop - sample.start) * loop_scale))
    loop_start = min(loop_start, frames)
    loop_end = min(loop_end, frames)
    if loop_start >= loop_end:
        loop_start = 0
        loop_end = 0

    # Build XSndHeader3
    hdr = bytearray()
    hdr.extend(_be16(X_THIRD_SOUND_FORMAT))  # type 3

    # XSoundHeader3
    hdr.extend(C_NONE)                       # subType
    hdr.extend(_be32(sample_rate << 16))     # sampleRate (XFIXED)
    hdr.extend(_be32(len(pcm)))              # decodedBytes (same as encoded for PCM)
    hdr.extend(_be32(frames))                # frameCount
    hdr.extend(_be32(len(pcm)))              # encodedBytes
    hdr.extend(_be32(0))                     # blockBytes (unused)
    hdr.extend(_be32(0))                     # startFrame
    # loopStart[6], loopEnd[6]
    for i in range(6):
        hdr.extend(_be32(loop_start))
    for i in range(6):
        hdr.extend(_be32(loop_end))
    # name resource type/id
    hdr.extend(_be32(0))                     # nameResourceType = ID_NULL
    hdr.extend(_be32(0))                     # nameResourceID = 0
    # baseKey, channels, bitSize, isEmbedded, isEncrypted, isSampleIntelOrder, 2 reserved
    base_key = sample.original_pitch if sample.original_pitch != 255 else 60
    base_key += coarse_tune_semitones  # Apply coarse tuning to baseKey only
    base_key = max(0, min(127, base_key))
    hdr.extend(bytes([base_key & 0xFF]))
    hdr.extend(bytes([channels & 0xFF]))
    hdr.extend(bytes([bit_size & 0xFF]))
    hdr.extend(bytes([1]))                   # isEmbedded
    hdr.extend(bytes([0]))                   # isEncrypted
    hdr.extend(bytes([1]))                   # isSampleIntelOrder (PCM little-endian)
    hdr.extend(b"\x00\x00")                 # reserved2
    hdr.extend(b"\x00" * (4 * 8))           # reserved3
    # sampleArea
    hdr.extend(pcm)

    return bytes(hdr)


def main():
    parser = argparse.ArgumentParser(
        description='Convert SF2 SoundFont files to miniBAE HSB format',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument('input', help='Input SF2 file')
    parser.add_argument('output', help='Output HSB/BSN file')
    parser.add_argument('--bank-name', help='Bank name (default: derived from filename)')
    parser.add_argument('--bank-url', default='', help='Bank URL (default: empty)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    parser.add_argument('--bsn', '-b', action='store_true', help='Output BSN format instead of HSB')
    parser.add_argument('--max-instruments', type=int, default=0, help='Maximum instruments to convert (0 = all)')
    parser.add_argument('--sample-quality', type=int, choices=[1,2,3,4], default=3,
                        help='Sample quality: 1=8mono, 2=8stereo, 3=16mono, 4=16stereo')
    parser.add_argument('--enable-adsr', action='store_true', default=True,
                        help='Enable ADSR envelope extraction (default: enabled)')
    parser.add_argument('--enable-lfo', action='store_true', default=True,
                        help='Enable LFO extraction (default: enabled)')
    parser.add_argument('--disable-adsr', action='store_true',
                        help='Disable ADSR envelope extraction')
    parser.add_argument('--disable-lfo', action='store_true', 
                        help='Disable LFO extraction')
    
    args = parser.parse_args()
    
    # Handle ADSR/LFO enable/disable flags
    if args.disable_adsr:
        args.enable_adsr = False
    if args.disable_lfo:
        args.enable_lfo = False
    if not args.bank_name:
        args.bank_name = os.path.splitext(os.path.basename(args.input))[0]
    
    try:
        if args.verbose:
            print(f"Loading SF2 file: {args.input}")
        
        # Load SF2 file
        sf2_bank = SF2Bank(args.input)
        
        print(f"Loaded SF2 bank: {len(sf2_bank.presets)} presets, {len(sf2_bank.samples)} samples")
        
        if args.verbose:
            print(f"Creating HSB file: {args.output}")
        
        # Create HSB writer
        hsb_writer = HSBWriter(args.output)
        
        # Cache for SND resources with tuning
        snd_cache = {}
        current_snd_id = 1
        
        # Convert and write INST resources FIRST (as shown in working file)
        current_inst_id = 1
        converted_presets = 0
        
        if args.verbose:
            print("Converting presets...")
        
        for i, preset in enumerate(sf2_bank.presets):
            if args.max_instruments > 0 and converted_presets >= args.max_instruments:
                break
            
            if args.verbose:
                print(f"  Preset {i+1}/{len(sf2_bank.presets)}: {preset.name} (bank {preset.bank}, preset {preset.preset})")
            
            # Convert preset with proper sample mapping
            result = convert_preset_to_inst(sf2_bank, preset, hsb_writer, snd_cache, current_snd_id, args)
            if result[0] is not None:
                inst_data, sample_id_used, current_snd_id = result
                hsb_writer.add_resource(ID_INST, current_inst_id, inst_data, preset.name)
                if args.verbose and sample_id_used:
                    print(f"    Using sample resource ID: {sample_id_used}")
                current_inst_id += 1
                converted_presets += 1
        
        # Add version info before bank
        version_data = create_version_resource()
        hsb_writer.add_resource(ID_VERS, 0, version_data)
        
        # Add bank info after instruments
        bank_data = create_bank_resource(args.bank_name, args.bank_url)
        hsb_writer.add_resource(ID_BANK, 0, bank_data)
    
        
        # Write HSB file
        if args.verbose:
            if args.bsn:
                print("Writing BSN file...")
            else:
                print("Writing HSB file...")
        
        if args.bsn:
            out = args.bank_name
        else:
            out = False

        hsb_writer.write(bsn=out)
        
        print(f"Successfully converted {converted_presets} presets and {len(snd_cache)} unique samples")
        print(f"Output written to: {args.output}")
        
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        if args.verbose:
            import traceback
            traceback.print_exc()
        return 1
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
