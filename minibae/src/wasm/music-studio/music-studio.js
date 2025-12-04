// Dual Beatnik Player by webcd - Two separate WASM modules for mixing
class DualBeatnikPlayer {
    constructor() {
        this.moduleLeft = null;
        this.moduleRight = null;
        this.audioContext = null;
        this.scriptNode = null;
        this.leftReady = false;
        this.rightReady = false;
        this.songData = null;
        this.isPlaying = false;
        this.isPaused = false;
        this.pausedPosition = 0;
        this.wasStopped = false;
        this.balance = 0.5;
        this.volumeLeft = 1.0;
        this.volumeRight = 1.0;
        this.soundbankPtrLeft = null;
        this.soundbankPtrRight = null;
        this.channelMutesLeft = new Array(17).fill(false);
        this.channelMutesRight = new Array(17).fill(false);
        this.channelSolosLeft = new Array(17).fill(false);
        this.channelSolosRight = new Array(17).fill(false);
        this.channelProgramsLeft = new Array(17).fill(-1);
        this.channelProgramsRight = new Array(17).fill(-1);
        this.panLeft = 0;
        this.panRight = 0;
        this.widthLeft = 1.0;
        this.widthRight = 1.0;
        this.outputGain = 0.6;
        this.gainNode = null;
    }

    async init() {
        this.audioContext = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 44100 });
        this.gainNode = this.audioContext.createGain();
        this.gainNode.gain.value = this.outputGain;
        this.gainNode.connect(this.audioContext.destination);

        this.moduleLeft = await BeatnikModule({ locateFile: (path) => path });
        this.moduleRight = await BeatnikModule({ locateFile: (path) => path });

        const initLeft = this.moduleLeft._BAE_WASM_Init(44100, 64);
        const initRight = this.moduleRight._BAE_WASM_Init(44100, 64);

        if (initLeft !== 0) throw new Error(`Left mixer init failed: ${initLeft}`);
        if (initRight !== 0) throw new Error(`Right mixer init failed: ${initRight}`);

        this.startAudioProcessing();
    }

    primeMixer() {
        if (!this.leftReady) return;
        const silentMidi = new Uint8Array([
            0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x60,
            0x4D, 0x54, 0x72, 0x6B, 0x00, 0x00, 0x00, 0x06, 0xBF, 0x80, 0x00, 0xFF, 0x2F, 0x00
        ]);
        const ptr = this.moduleLeft._malloc(silentMidi.length);
        this.moduleLeft.HEAPU8.set(silentMidi, ptr);
        this.moduleLeft._BAE_WASM_LoadSong(ptr, silentMidi.length);
        this.moduleLeft._free(ptr);
        this.moduleLeft._BAE_WASM_Play();
    }

    async loadSoundbankLeft(url) {
        const data = await this.fetchBinary(url);
        if (this.soundbankPtrLeft) { this.moduleLeft._free(this.soundbankPtrLeft); this.soundbankPtrLeft = null; }
        const ptr = this.moduleLeft._malloc(data.length);
        this.moduleLeft.HEAPU8.set(data, ptr);
        const result = this.moduleLeft._BAE_WASM_LoadSoundbank(ptr, data.length);
        if (result !== 0) { this.moduleLeft._free(ptr); throw new Error(`Failed to load left soundbank: ${result}`); }
        this.soundbankPtrLeft = ptr;
        this.leftReady = true;
    }

    async loadSoundbankRight(url) {
        const data = await this.fetchBinary(url);
        if (this.soundbankPtrRight) { this.moduleRight._free(this.soundbankPtrRight); this.soundbankPtrRight = null; }
        const ptr = this.moduleRight._malloc(data.length);
        this.moduleRight.HEAPU8.set(data, ptr);
        const result = this.moduleRight._BAE_WASM_LoadSoundbank(ptr, data.length);
        if (result !== 0) { this.moduleRight._free(ptr); throw new Error(`Failed to load right soundbank: ${result}`); }
        this.soundbankPtrRight = ptr;
        this.rightReady = true;
    }

    loadSongToModule(module, data, side) {
        const ptr = module._malloc(data.length);
        module.HEAPU8.set(data, ptr);
        module._BAE_WASM_LoadSong(ptr, data.length);
        module._free(ptr);
        if (side === 'left') { this.applyChannelStateLeft(); setTimeout(() => this.applyChannelProgramsLeft(), 100); }
        else if (side === 'right') { this.applyChannelStateRight(); setTimeout(() => this.applyChannelProgramsRight(), 100); }
    }

    async loadSong(arrayBuffer) {
        this.songData = new Uint8Array(arrayBuffer);
        if (this.leftReady) this.loadSongToModule(this.moduleLeft, this.songData, 'left');
        if (this.rightReady) this.loadSongToModule(this.moduleRight, this.songData, 'right');
    }

    play() {
        if (this.audioContext.state === 'suspended') this.audioContext.resume();
        if (this.songData && this.wasStopped) {
            if (this.leftReady) this.loadSongToModule(this.moduleLeft, this.songData, 'left');
            if (this.rightReady) this.loadSongToModule(this.moduleRight, this.songData, 'right');
            this.wasStopped = false;
        }
        if (this.leftReady) this.moduleLeft._BAE_WASM_Play();
        if (this.rightReady) this.moduleRight._BAE_WASM_Play();
        this.isPlaying = true;
        this.startAudioProcessing();
    }

    pause() {
        this.pausedPosition = this.getPosition();
        if (this.leftReady) this.moduleLeft._BAE_WASM_Pause();
        if (this.rightReady) this.moduleRight._BAE_WASM_Pause();
        this.isPlaying = false;
        this.isPaused = true;
    }

    resume() {
        if (this.audioContext.state === 'suspended') this.audioContext.resume();
        if (this.leftReady) this.moduleLeft._BAE_WASM_Play();
        if (this.rightReady) this.moduleRight._BAE_WASM_Play();
        if (this.pausedPosition > 0) { setTimeout(() => { this.seek(this.pausedPosition); this.pausedPosition = 0; }, 10); }
        this.isPlaying = true;
        this.isPaused = false;
        this.startAudioProcessing();
    }

    stop() {
        if (this.leftReady) this.moduleLeft._BAE_WASM_Stop();
        if (this.rightReady) this.moduleRight._BAE_WASM_Stop();
        this.isPlaying = false;
        this.isPaused = false;
        this.pausedPosition = 0;
        this.wasStopped = true;
    }

    seek(positionMs) {
        if (this.leftReady) this.moduleLeft._BAE_WASM_SetPosition(positionMs);
        if (this.rightReady) this.moduleRight._BAE_WASM_SetPosition(positionMs);
        setTimeout(() => { this.applyChannelProgramsLeft(); this.applyChannelProgramsRight(); }, 50);
    }

    getPosition() {
        if (this.leftReady) return this.moduleLeft._BAE_WASM_GetPosition();
        if (this.rightReady) return this.moduleRight._BAE_WASM_GetPosition();
        return 0;
    }

    getDuration() {
        if (this.leftReady) return this.moduleLeft._BAE_WASM_GetDuration();
        if (this.rightReady) return this.moduleRight._BAE_WASM_GetDuration();
        return 0;
    }

    setBalance(value) { this.balance = value; }
    setOutputGain(value) { this.outputGain = value; if (this.gainNode) this.gainNode.gain.value = value; }
    setVolumeLeft(value) { this.volumeLeft = value; }
    setVolumeRight(value) { this.volumeRight = value; }
    setPanLeft(value) { this.panLeft = value; }
    setPanRight(value) { this.panRight = value; }
    setWidthLeft(value) { this.widthLeft = value; }
    setWidthRight(value) { this.widthRight = value; }

    muteChannelLeft(channel, muted) { this.channelMutesLeft[channel] = muted; this.applyChannelStateLeft(); }
    muteChannelRight(channel, muted) { this.channelMutesRight[channel] = muted; this.applyChannelStateRight(); }
    soloChannelLeft(channel, solo) { this.channelSolosLeft[channel] = solo; this.applyChannelStateLeft(); }
    soloChannelRight(channel, solo) { this.channelSolosRight[channel] = solo; this.applyChannelStateRight(); }

    applyChannelStateLeft() {
        if (!this.leftReady) return;
        const hasSolo = this.channelSolosLeft.some(s => s);
        for (let ch = 0; ch < 17; ch++) {
            const shouldMute = this.channelMutesLeft[ch] || (hasSolo && !this.channelSolosLeft[ch]);
            this.moduleLeft._BAE_WASM_MuteChannel(ch, shouldMute ? 1 : 0);
        }
    }

    applyChannelStateRight() {
        if (!this.rightReady) return;
        const hasSolo = this.channelSolosRight.some(s => s);
        for (let ch = 0; ch < 17; ch++) {
            const shouldMute = this.channelMutesRight[ch] || (hasSolo && !this.channelSolosRight[ch]);
            this.moduleRight._BAE_WASM_MuteChannel(ch, shouldMute ? 1 : 0);
        }
    }

    resetChannelsLeft() { this.channelMutesLeft.fill(false); this.channelSolosLeft.fill(false); this.applyChannelStateLeft(); }
    resetChannelsRight() { this.channelMutesRight.fill(false); this.channelSolosRight.fill(false); this.applyChannelStateRight(); }
    muteAllChannelsLeft(muted) { this.channelMutesLeft.fill(muted); this.applyChannelStateLeft(); }
    muteAllChannelsRight(muted) { this.channelMutesRight.fill(muted); this.applyChannelStateRight(); }

    applyChannelProgramsLeft() {
        if (!this.leftReady) return;
        for (let ch = 0; ch < 17; ch++) { if (this.channelProgramsLeft[ch] >= 0) this.moduleLeft._BAE_WASM_ProgramChange(ch, this.channelProgramsLeft[ch]); }
    }

    applyChannelProgramsRight() {
        if (!this.rightReady) return;
        for (let ch = 0; ch < 17; ch++) { if (this.channelProgramsRight[ch] >= 0) this.moduleRight._BAE_WASM_ProgramChange(ch, this.channelProgramsRight[ch]); }
    }

    setChannelProgramLeft(channel, program) {
        this.channelProgramsLeft[channel] = program;
        if (this.leftReady) this.moduleLeft._BAE_WASM_ProgramChange(channel, program);
    }

    setChannelProgramRight(channel, program) {
        this.channelProgramsRight[channel] = program;
        if (this.rightReady) this.moduleRight._BAE_WASM_ProgramChange(channel, program);
    }

    // Shared stereo mixing helper
    processStereoSample(l, r, pan, width, vol) {
        const mid = (l + r) * 0.5;
        const side = (l - r) * 0.5 * width;
        l = mid + side;
        r = mid - side;
        const angle = (pan + 1) * Math.PI / 4;
        const panGainL = Math.cos(angle);
        const panGainR = Math.sin(angle);
        return {
            outL: (l * panGainL + r * (1 - panGainR)) * vol,
            outR: (r * panGainR + l * (1 - panGainL)) * vol
        };
    }

    startAudioProcessing() {
        if (this.scriptNode) this.scriptNode.disconnect();
        const bufferSize = 512;
        this.scriptNode = this.audioContext.createScriptProcessor(bufferSize, 0, 2);

        this.scriptNode.onaudioprocess = (e) => {
            const wasmEffectPlaying = this.leftReady && this.moduleLeft._BAE_WASM_IsEffectPlaying && this.moduleLeft._BAE_WASM_IsEffectPlaying();
            const jsEffectPlaying = Date.now() < effectPlayingUntil;
            if (!this.isPlaying && !wasmEffectPlaying && !jsEffectPlaying) {
                e.outputBuffer.getChannelData(0).fill(0);
                e.outputBuffer.getChannelData(1).fill(0);
                return;
            }

            const outputL = e.outputBuffer.getChannelData(0);
            const outputR = e.outputBuffer.getChannelData(1);
            const frames = outputL.length;

            let leftSamples = this.leftReady ? new Int16Array(this.moduleLeft.HEAP16.buffer, this.moduleLeft._BAE_WASM_GenerateAudio(frames), frames * 2) : null;
            let rightSamples = this.rightReady ? new Int16Array(this.moduleRight.HEAP16.buffer, this.moduleRight._BAE_WASM_GenerateAudio(frames), frames * 2) : null;

            const leftGain = this.balance <= 0.5 ? 1.0 : 2.0 * (1.0 - this.balance);
            const rightGain = this.balance >= 0.5 ? 1.0 : 2.0 * this.balance;
            const panL = this.panLeft / 100;
            const panR = this.panRight / 100;

            let peakLeft = 0, peakRight = 0, peakMasterL = 0, peakMasterR = 0;

            for (let i = 0; i < frames; i++) {
                let leftBankL = 0, leftBankR = 0, rightBankL = 0, rightBankR = 0;

                if (leftSamples) {
                    const out = this.processStereoSample(leftSamples[i * 2] / 32768.0, leftSamples[i * 2 + 1] / 32768.0, panL, this.widthLeft, leftGain * this.volumeLeft);
                    leftBankL = out.outL; leftBankR = out.outR;
                }
                if (rightSamples) {
                    const out = this.processStereoSample(rightSamples[i * 2] / 32768.0, rightSamples[i * 2 + 1] / 32768.0, panR, this.widthRight, rightGain * this.volumeRight);
                    rightBankL = out.outL; rightBankR = out.outR;
                }

                outputL[i] = leftBankL + rightBankL;
                outputR[i] = leftBankR + rightBankR;

                peakLeft = Math.max(peakLeft, Math.abs(leftBankL), Math.abs(leftBankR));
                peakRight = Math.max(peakRight, Math.abs(rightBankL), Math.abs(rightBankR));
                peakMasterL = Math.max(peakMasterL, Math.abs(outputL[i]));
                peakMasterR = Math.max(peakMasterR, Math.abs(outputR[i]));
            }

            this.onMeterUpdate && this.onMeterUpdate(peakLeft, peakRight, peakMasterL, peakMasterR);
        };

        this.scriptNode.connect(this.gainNode);
    }

    async fetchBinary(url) {
        const response = await fetch(url);
        return new Uint8Array(await response.arrayBuffer());
    }

    async exportToRawAudio(onProgress) {
        if (!this.songData) throw new Error('No song loaded');
        const wasPlaying = this.isPlaying;
        if (wasPlaying) this.stop();

        if (this.leftReady) {
            const ptrL = this.moduleLeft._malloc(this.songData.length);
            this.moduleLeft.HEAPU8.set(this.songData, ptrL);
            this.moduleLeft._BAE_WASM_LoadSong(ptrL, this.songData.length);
            this.moduleLeft._free(ptrL);
            this.applyChannelStateLeft();
            this.moduleLeft._BAE_WASM_Play();
        }
        if (this.rightReady) {
            const ptrR = this.moduleRight._malloc(this.songData.length);
            this.moduleRight.HEAPU8.set(this.songData, ptrR);
            this.moduleRight._BAE_WASM_LoadSong(ptrR, this.songData.length);
            this.moduleRight._free(ptrR);
            this.applyChannelStateRight();
            this.moduleRight._BAE_WASM_Play();
        }

        const warmupFrames = 16;
        if (this.leftReady) this.moduleLeft._BAE_WASM_GenerateAudio(warmupFrames);
        if (this.rightReady) this.moduleRight._BAE_WASM_GenerateAudio(warmupFrames);
        if (this.leftReady) this.applyChannelProgramsLeft();
        if (this.rightReady) this.applyChannelProgramsRight();

        const sampleRate = 44100;
        const duration = this.getDuration();
        const totalFrames = Math.ceil((duration / 1000) * sampleRate) + sampleRate;
        const chunkSize = 512;

        const leftChannel = new Float32Array(totalFrames);
        const rightChannel = new Float32Array(totalFrames);

        const panL = this.panLeft / 100;
        const panR = this.panRight / 100;
        const leftGain = this.balance <= 0.5 ? 1.0 : 2.0 * (1.0 - this.balance);
        const rightGain = this.balance >= 0.5 ? 1.0 : 2.0 * this.balance;

        let framesRendered = 0;
        while (framesRendered < totalFrames) {
            const framesToRender = Math.min(chunkSize, totalFrames - framesRendered);

            let leftSamples = this.leftReady ? new Int16Array(this.moduleLeft.HEAP16.buffer, this.moduleLeft._BAE_WASM_GenerateAudio(framesToRender), framesToRender * 2) : null;
            let rightSamples = this.rightReady ? new Int16Array(this.moduleRight.HEAP16.buffer, this.moduleRight._BAE_WASM_GenerateAudio(framesToRender), framesToRender * 2) : null;

            for (let i = 0; i < framesToRender; i++) {
                let leftBankL = 0, leftBankR = 0, rightBankL = 0, rightBankR = 0;
                if (leftSamples) {
                    const out = this.processStereoSample(leftSamples[i * 2] / 32768.0, leftSamples[i * 2 + 1] / 32768.0, panL, this.widthLeft, leftGain * this.volumeLeft);
                    leftBankL = out.outL; leftBankR = out.outR;
                }
                if (rightSamples) {
                    const out = this.processStereoSample(rightSamples[i * 2] / 32768.0, rightSamples[i * 2 + 1] / 32768.0, panR, this.widthRight, rightGain * this.volumeRight);
                    rightBankL = out.outL; rightBankR = out.outR;
                }
                leftChannel[framesRendered + i] = leftBankL + rightBankL;
                rightChannel[framesRendered + i] = leftBankR + rightBankR;
            }

            framesRendered += framesToRender;
            if (onProgress) onProgress(framesRendered / totalFrames);
            if (framesRendered % (chunkSize * 20) === 0) await new Promise(r => setTimeout(r, 0));
        }

        if (this.leftReady) this.moduleLeft._BAE_WASM_Stop();
        if (this.rightReady) this.moduleRight._BAE_WASM_Stop();

        return { leftChannel, rightChannel, sampleRate };
    }

    createWavFile(leftChannel, rightChannel, sampleRate) {
        const numChannels = 2, bitsPerSample = 16;
        const bytesPerSample = bitsPerSample / 8;
        const numSamples = leftChannel.length;
        const dataSize = numSamples * numChannels * bytesPerSample;
        const fileSize = 44 + dataSize;

        const buffer = new ArrayBuffer(fileSize);
        const view = new DataView(buffer);

        const writeString = (offset, string) => { for (let i = 0; i < string.length; i++) view.setUint8(offset + i, string.charCodeAt(i)); };

        writeString(0, 'RIFF');
        view.setUint32(4, fileSize - 8, true);
        writeString(8, 'WAVE');
        writeString(12, 'fmt ');
        view.setUint32(16, 16, true);
        view.setUint16(20, 1, true);
        view.setUint16(22, numChannels, true);
        view.setUint32(24, sampleRate, true);
        view.setUint32(28, sampleRate * numChannels * bytesPerSample, true);
        view.setUint16(32, numChannels * bytesPerSample, true);
        view.setUint16(34, bitsPerSample, true);
        writeString(36, 'data');
        view.setUint32(40, dataSize, true);

        let offset = 44;
        for (let i = 0; i < numSamples; i++) {
            view.setInt16(offset, Math.max(-1, Math.min(1, leftChannel[i])) * 32767, true);
            view.setInt16(offset + 2, Math.max(-1, Math.min(1, rightChannel[i])) * 32767, true);
            offset += 4;
        }

        return buffer;
    }
}

// UI Controller
let player = null, songFile = null, updateInterval = null, currentTranspose = 0, currentTempo = 100, currentSongName = '';
let beatnikRmfData = null, tellMeRmfData = null, effectPlayingUntil = 0;

const elements = {};
const elementIds = ['soundbankLeft', 'soundbankRight', 'volumeLeft', 'volumeRight', 'volumeLeftValue', 'volumeRightValue',
    'panLeft', 'panRight', 'panLeftValue', 'panRightValue', 'widthLeft', 'widthRight', 'widthLeftValue', 'widthRightValue',
    'statusLeft', 'statusRight', 'channelGridLeft', 'channelGridRight', 'dropZone', 'fileInput', 'playerPanel', 'nowPlaying',
    'progressContainer', 'progressBar', 'currentTime', 'duration', 'playPauseBtn', 'stopBtn', 'exportBtn', 'exportProgress',
    'exportStatus', 'exportProgressBar', 'balanceSlider', 'gainSlider', 'gainValue', 'reverbSelect', 'meterLeft', 'meterRight',
    'masterMeterL', 'masterMeterR', 'instrumentPicker', 'instrumentCategories', 'instrumentList', 'pickerChannel', 'pickerSide',
    'pickerClose', 'presetHyperactive', 'beatnikLogo', 'mascotGif'];

const GM_CATEGORIES = [
    { name: 'Piano', start: 0 }, { name: 'Chromatic', start: 8 }, { name: 'Organ', start: 16 }, { name: 'Guitar', start: 24 },
    { name: 'Bass', start: 32 }, { name: 'Strings', start: 40 }, { name: 'Ensemble', start: 48 }, { name: 'Brass', start: 56 },
    { name: 'Reed', start: 64 }, { name: 'Pipe', start: 72 }, { name: 'Synth Lead', start: 80 }, { name: 'Synth Pad', start: 88 },
    { name: 'Synth FX', start: 96 }, { name: 'Ethnic', start: 104 }, { name: 'Percussive', start: 112 }, { name: 'SFX', start: 120 }
];

const GM_INSTRUMENTS = [
    'Acoustic Grand Piano', 'Bright Acoustic Piano', 'Electric Grand Piano', 'Honky-tonk Piano',
    'Electric Piano 1', 'Electric Piano 2', 'Harpsichord', 'Clavinet',
    'Celesta', 'Glockenspiel', 'Music Box', 'Vibraphone', 'Marimba', 'Xylophone', 'Tubular Bells', 'Dulcimer',
    'Drawbar Organ', 'Percussive Organ', 'Rock Organ', 'Church Organ', 'Reed Organ', 'Accordion', 'Harmonica', 'Tango Accordion',
    'Acoustic Guitar (nylon)', 'Acoustic Guitar (steel)', 'Electric Guitar (jazz)', 'Electric Guitar (clean)',
    'Electric Guitar (muted)', 'Overdriven Guitar', 'Distortion Guitar', 'Guitar Harmonics',
    'Acoustic Bass', 'Electric Bass (finger)', 'Electric Bass (pick)', 'Fretless Bass',
    'Slap Bass 1', 'Slap Bass 2', 'Synth Bass 1', 'Synth Bass 2',
    'Violin', 'Viola', 'Cello', 'Contrabass', 'Tremolo Strings', 'Pizzicato Strings', 'Orchestral Harp', 'Timpani',
    'String Ensemble 1', 'String Ensemble 2', 'Synth Strings 1', 'Synth Strings 2',
    'Choir Aahs', 'Voice Oohs', 'Synth Choir', 'Orchestra Hit',
    'Trumpet', 'Trombone', 'Tuba', 'Muted Trumpet', 'French Horn', 'Brass Section', 'Synth Brass 1', 'Synth Brass 2',
    'Soprano Sax', 'Alto Sax', 'Tenor Sax', 'Baritone Sax', 'Oboe', 'English Horn', 'Bassoon', 'Clarinet',
    'Piccolo', 'Flute', 'Recorder', 'Pan Flute', 'Blown Bottle', 'Shakuhachi', 'Whistle', 'Ocarina',
    'Lead 1 (square)', 'Lead 2 (sawtooth)', 'Lead 3 (calliope)', 'Lead 4 (chiff)',
    'Lead 5 (charang)', 'Lead 6 (voice)', 'Lead 7 (fifths)', 'Lead 8 (bass + lead)',
    'Pad 1 (new age)', 'Pad 2 (warm)', 'Pad 3 (polysynth)', 'Pad 4 (choir)',
    'Pad 5 (bowed)', 'Pad 6 (metallic)', 'Pad 7 (halo)', 'Pad 8 (sweep)',
    'FX 1 (rain)', 'FX 2 (soundtrack)', 'FX 3 (crystal)', 'FX 4 (atmosphere)',
    'FX 5 (brightness)', 'FX 6 (goblins)', 'FX 7 (echoes)', 'FX 8 (sci-fi)',
    'Sitar', 'Banjo', 'Shamisen', 'Koto', 'Kalimba', 'Bagpipe', 'Fiddle', 'Shanai',
    'Tinkle Bell', 'Agogo', 'Steel Drums', 'Woodblock', 'Taiko Drum', 'Melodic Tom', 'Synth Drum', 'Reverse Cymbal',
    'Guitar Fret Noise', 'Breath Noise', 'Seashore', 'Bird Tweet', 'Telephone Ring', 'Helicopter', 'Applause', 'Gunshot'
];

let pickerState = { channel: 0, side: 'left', currentProgram: 0 };

function showInstrumentPicker(channel, side, x, y) {
    if (channel === 9 || channel === 16) return;
    pickerState.channel = channel;
    pickerState.side = side;
    const module = side === 'left' ? player.moduleLeft : player.moduleRight;
    pickerState.currentProgram = module._BAE_WASM_GetProgram(channel);
    if (pickerState.currentProgram < 0) pickerState.currentProgram = 0;

    elements.pickerChannel.textContent = channel + 1;
    elements.pickerSide.textContent = side === 'left' ? 'A' : 'B';

    elements.instrumentCategories.innerHTML = '';
    const currentCategory = Math.floor(pickerState.currentProgram / 8);
    GM_CATEGORIES.forEach((cat, idx) => {
        const btn = document.createElement('div');
        btn.className = 'instrument-category' + (idx === currentCategory ? ' active' : '');
        btn.textContent = cat.name;
        btn.onclick = () => showCategoryInstruments(idx);
        elements.instrumentCategories.appendChild(btn);
    });

    showCategoryInstruments(currentCategory);
    const picker = elements.instrumentPicker;
    picker.style.left = Math.min(x, window.innerWidth - 300) + 'px';
    picker.style.top = Math.min(y, window.innerHeight - 420) + 'px';
    picker.classList.add('visible');
}

function showCategoryInstruments(categoryIndex) {
    elements.instrumentCategories.querySelectorAll('.instrument-category').forEach((btn, idx) => btn.classList.toggle('active', idx === categoryIndex));
    const start = categoryIndex * 8;
    elements.instrumentList.innerHTML = '';
    for (let i = 0; i < 8; i++) {
        const program = start + i;
        const item = document.createElement('div');
        item.className = 'instrument-item' + (program === pickerState.currentProgram ? ' current' : '');
        item.innerHTML = `<span class="inst-num">${program}</span>${GM_INSTRUMENTS[program]}`;
        item.onclick = () => selectInstrument(program);
        elements.instrumentList.appendChild(item);
    }
}

function selectInstrument(program) {
    if (pickerState.side === 'left') {
        player.setChannelProgramLeft(pickerState.channel, program);
        setTimeout(() => player.setChannelProgramLeft(pickerState.channel, program), 50);
    } else {
        player.setChannelProgramRight(pickerState.channel, program);
        setTimeout(() => player.setChannelProgramRight(pickerState.channel, program), 50);
    }
    pickerState.currentProgram = program;
    elements.instrumentList.querySelectorAll('.instrument-item').forEach((item, idx) => {
        item.classList.toggle('current', Math.floor(pickerState.currentProgram / 8) * 8 + idx === program);
    });
    updateChannelTooltip(pickerState.side, pickerState.channel, program);
    hideInstrumentPicker();
}

function updateChannelTooltip(side, channel, program) {
    const grid = side === 'left' ? elements.channelGridLeft : elements.channelGridRight;
    const chNum = grid.querySelector(`.ch-num[data-ch="${channel}"]`);
    if (chNum && program >= 0 && program < GM_INSTRUMENTS.length) chNum.title = GM_INSTRUMENTS[program] + ' (click to change)';
}

function updateAllChannelTooltips(side) {
    const module = side === 'left' ? player.moduleLeft : player.moduleRight;
    if (!module) return;
    for (let ch = 0; ch < 16; ch++) {
        if (ch === 9) continue;
        const program = module._BAE_WASM_GetProgram(ch);
        if (program >= 0) updateChannelTooltip(side, ch, program);
    }
}

function hideInstrumentPicker() { elements.instrumentPicker.classList.remove('visible'); }

function formatTime(ms) {
    const seconds = Math.floor(ms / 1000);
    return `${Math.floor(seconds / 60)}:${(seconds % 60).toString().padStart(2, '0')}`;
}

function formatPan(value) {
    if (value === 0) return 'C';
    return value < 0 ? `L${Math.abs(value)}` : `R${value}`;
}

async function init() {
    try {
        if (typeof BeatnikModule === 'undefined') {
            elements.statusLeft.textContent = 'WASM not loaded';
            elements.statusLeft.className = 'status error';
            elements.statusRight.textContent = 'WASM not loaded';
            elements.statusRight.className = 'status error';
            return;
        }

        player = new DualBeatnikPlayer();
        await player.init();
        preloadEffectRmfs();

        player.onMeterUpdate = (left, right, masterL, masterR) => {
            elements.meterLeft.style.width = (left * 100) + '%';
            elements.meterRight.style.width = (right * 100) + '%';
            elements.masterMeterL.style.width = (masterL * 100) + '%';
            elements.masterMeterR.style.width = (masterR * 100) + '%';
        };

        player.channelActivityInterval = setInterval(() => {
            if (!player.isPlaying) return;
            if (player.leftReady && player.moduleLeft._BAE_WASM_GetAllChannelActivities) {
                const activityPtr = player.moduleLeft._malloc(16);
                player.moduleLeft._BAE_WASM_GetAllChannelActivities(activityPtr);
                const activities = new Uint8Array(player.moduleLeft.HEAPU8.buffer, activityPtr, 16);
                for (let ch = 0; ch < 16; ch++) {
                    const vu = document.getElementById(`vu-left-${ch}`);
                    if (vu) vu.style.width = (activities[ch] / 127 * 100) + '%';
                }
                player.moduleLeft._free(activityPtr);
            }
            if (player.rightReady && player.moduleRight._BAE_WASM_GetAllChannelActivities) {
                const activityPtr = player.moduleRight._malloc(16);
                player.moduleRight._BAE_WASM_GetAllChannelActivities(activityPtr);
                const activities = new Uint8Array(player.moduleRight.HEAPU8.buffer, activityPtr, 16);
                for (let ch = 0; ch < 16; ch++) {
                    const vu = document.getElementById(`vu-right-${ch}`);
                    if (vu) vu.style.width = (activities[ch] / 127 * 100) + '%';
                }
                player.moduleRight._free(activityPtr);
            }
        }, 50);

        initChannelGrid('left');
        initChannelGrid('right');

        const defaultReverb = parseInt(elements.reverbSelect.value);
        player.moduleLeft._BAE_WASM_SetReverbType(defaultReverb);
        player.moduleRight._BAE_WASM_SetReverbType(defaultReverb);

        if (elements.soundbankLeft.value) {
            await loadLeftSoundbank();
            player.primeMixer();
        } else {
            elements.statusLeft.textContent = 'Ready - select soundbank';
            elements.statusLeft.className = 'status ready';
        }
        if (elements.soundbankRight.value) {
            loadRightSoundbank();
        } else {
            elements.statusRight.textContent = 'Ready - select soundbank';
            elements.statusRight.className = 'status ready';
        }
    } catch (error) {
        console.error('Init error:', error);
        elements.statusLeft.textContent = 'Error: ' + error.message;
        elements.statusLeft.className = 'status error';
    }
}

function initChannelGrid(side) {
    const grid = side === 'left' ? elements.channelGridLeft : elements.channelGridRight;
    grid.innerHTML = '';
    const channelNames = ['1', '2', '3', '4', '5', '6', '7', '8', '9', 'Dr', '11', '12', '13', '14', '15', '16', 'SFX'];

    for (let ch = 0; ch < 17; ch++) {
        const strip = document.createElement('div');
        strip.className = 'channel-strip' + (ch === 9 ? ' drums' : '');
        let tooltip = 'Double-click to change instrument';
        if (ch === 9) tooltip = 'Drums (channel 10)';
        else if (ch === 16) tooltip = 'SFX channel';
        strip.innerHTML = `
            <div class="ch-num" data-ch="${ch}" data-side="${side}" title="${tooltip}">${channelNames[ch]}</div>
            <div class="ch-vu"><div class="ch-vu-fill" id="vu-${side}-${ch}"></div></div>
            <div class="ch-buttons">
                <button class="mute-btn" data-ch="${ch}" data-side="${side}">M</button>
                <button class="solo-btn" data-ch="${ch}" data-side="${side}">S</button>
            </div>
        `;
        grid.appendChild(strip);
    }

    grid.querySelectorAll('.mute-btn').forEach(btn => {
        btn.addEventListener('click', (e) => toggleMute(e.target.dataset.side, parseInt(e.target.dataset.ch)));
    });
    grid.querySelectorAll('.solo-btn').forEach(btn => {
        btn.addEventListener('click', (e) => toggleSolo(e.target.dataset.side, parseInt(e.target.dataset.ch)));
    });
    grid.querySelectorAll('.ch-num').forEach(num => {
        num.addEventListener('click', (e) => {
            if (player && (player.leftReady || player.rightReady)) {
                showInstrumentPicker(parseInt(e.target.dataset.ch), e.target.dataset.side, e.clientX, e.clientY);
            }
        });
    });
}

function toggleMute(side, channel) {
    if (!player) return;
    if (side === 'left') { player.channelMutesLeft[channel] = !player.channelMutesLeft[channel]; player.applyChannelStateLeft(); }
    else { player.channelMutesRight[channel] = !player.channelMutesRight[channel]; player.applyChannelStateRight(); }
    updateChannelUI(side);
}

function toggleSolo(side, channel) {
    if (!player) return;
    if (side === 'left') { player.channelSolosLeft[channel] = !player.channelSolosLeft[channel]; player.applyChannelStateLeft(); }
    else { player.channelSolosRight[channel] = !player.channelSolosRight[channel]; player.applyChannelStateRight(); }
    updateChannelUI(side);
}

function updateChannelUI(side) {
    if (!player) return;
    const grid = side === 'left' ? elements.channelGridLeft : elements.channelGridRight;
    const mutes = side === 'left' ? player.channelMutesLeft : player.channelMutesRight;
    const solos = side === 'left' ? player.channelSolosLeft : player.channelSolosRight;
    grid.querySelectorAll('.mute-btn').forEach((btn, i) => btn.classList.toggle('active', mutes[i]));
    grid.querySelectorAll('.solo-btn').forEach((btn, i) => btn.classList.toggle('active', solos[i]));
}

function resetChannelsLeft() { if (player) { player.resetChannelsLeft(); updateChannelUI('left'); } }
function resetChannelsRight() { if (player) { player.resetChannelsRight(); updateChannelUI('right'); } }
function muteAllLeft() { if (player) { player.muteAllChannelsLeft(!player.channelMutesLeft.every(m => m)); updateChannelUI('left'); } }
function muteAllRight() { if (player) { player.muteAllChannelsRight(!player.channelMutesRight.every(m => m)); updateChannelUI('right'); } }

async function loadLeftSoundbank() {
    const url = elements.soundbankLeft.value;
    if (!url || !player) return;
    const wasPlaying = player.isPlaying;
    const savedPosition = player.getPosition();
    elements.statusLeft.textContent = 'Loading...';
    elements.statusLeft.className = 'status loading';
    try {
        await player.loadSoundbankLeft(url);
        elements.statusLeft.textContent = 'Loaded: ' + elements.soundbankLeft.options[elements.soundbankLeft.selectedIndex].text;
        elements.statusLeft.className = 'status ready';
        if (player.songData) {
            player.loadSongToModule(player.moduleLeft, player.songData, 'left');
            if (wasPlaying && savedPosition > 0) { player.moduleLeft._BAE_WASM_SetPosition(savedPosition); player.moduleLeft._BAE_WASM_Play(); }
            setTimeout(() => updateAllChannelTooltips('left'), 200);
        } else { player.primeMixer(); }
    } catch (error) {
        elements.statusLeft.textContent = 'Error: ' + error.message;
        elements.statusLeft.className = 'status error';
    }
}

async function loadRightSoundbank() {
    const url = elements.soundbankRight.value;
    if (!url || !player) return;
    const wasPlaying = player.isPlaying;
    const savedPosition = player.getPosition();
    elements.statusRight.textContent = 'Loading...';
    elements.statusRight.className = 'status loading';
    try {
        await player.loadSoundbankRight(url);
        elements.statusRight.textContent = 'Loaded: ' + elements.soundbankRight.options[elements.soundbankRight.selectedIndex].text;
        elements.statusRight.className = 'status ready';
        if (player.songData) {
            player.loadSongToModule(player.moduleRight, player.songData, 'right');
            if (wasPlaying && savedPosition > 0) { player.moduleRight._BAE_WASM_SetPosition(savedPosition); player.moduleRight._BAE_WASM_Play(); }
            setTimeout(() => updateAllChannelTooltips('right'), 200);
        }
    } catch (error) {
        elements.statusRight.textContent = 'Error: ' + error.message;
        elements.statusRight.className = 'status error';
    }
}

async function handleFile(file) {
    if (!player) return;
    songFile = file;
    currentSongName = file.name.toLowerCase();
    elements.nowPlaying.textContent = file.name;
    elements.playerPanel.style.display = 'block';
    elements.presetHyperactive.textContent = file.name;
    elements.presetHyperactive.classList.add('loaded');
    updateMascotState();
    currentTranspose = 0;
    currentTempo = 100;
    await player.loadSong(await file.arrayBuffer());
    elements.duration.textContent = formatTime(player.getDuration());
    setTimeout(() => { updateAllChannelTooltips('left'); updateAllChannelTooltips('right'); }, 300);
    player.play();
    elements.playPauseBtn.textContent = 'PAUSE';
    startProgressUpdate();
    updateMascotState();
}

function updateMascotState() {
    if (currentSongName.includes('hyperactive') && player && player.isPlaying) elements.mascotGif.classList.add('visible');
    else elements.mascotGif.classList.remove('visible');
}

async function preloadEffectRmfs() {
    try { const r = await fetch('./content/beatnik.rmf'); if (r.ok) beatnikRmfData = await r.arrayBuffer(); } catch (e) {}
    try { const r = await fetch('./content/tell-me-about.rmf'); if (r.ok) tellMeRmfData = await r.arrayBuffer(); } catch (e) {}
}

function playEffectRmf(data) {
    if (!player || !player.leftReady || !data) return;
    if (player.audioContext && player.audioContext.state === 'suspended') player.audioContext.resume();
    const module = player.moduleLeft;
    const ptr = module._malloc(data.byteLength);
    module.HEAPU8.set(new Uint8Array(data), ptr);
    module._BAE_WASM_LoadEffect(ptr, data.byteLength);
    module._free(ptr);
    module._BAE_WASM_PlayEffect();
    effectPlayingUntil = Date.now() + 5000;
}

async function loadPresetSong(songUrl, songName) {
    if (!player) return;
    const isRMF = songUrl.toLowerCase().endsWith('.rmf');
    const soundbankB = isRMF ? './content/patchesp.hsb' : './content/minibae-wtv.hsb';
    const soundbankBName = isRMF ? 'Beatnik Pro 3.0' : 'WebTV Classic';

    elements.soundbankLeft.value = './content/patches-wtv-uncompressed.hsb';
    elements.soundbankRight.value = soundbankB;
    elements.panLeft.value = -40; elements.panLeftValue.textContent = 'L40'; player.setPanLeft(-40);
    elements.panRight.value = 40; elements.panRightValue.textContent = 'R40'; player.setPanRight(40);

    elements.statusLeft.textContent = 'Loading WebTV Plus...'; elements.statusLeft.className = 'status';
    await player.loadSoundbankLeft('./content/patches-wtv-uncompressed.hsb');
    elements.statusLeft.textContent = 'WebTV Plus Uncompressed'; elements.statusLeft.className = 'status loaded';

    elements.statusRight.textContent = 'Loading ' + soundbankBName + '...'; elements.statusRight.className = 'status';
    await player.loadSoundbankRight(soundbankB);
    elements.statusRight.textContent = soundbankBName; elements.statusRight.className = 'status loaded';

    try {
        const response = await fetch(songUrl);
        if (!response.ok) throw new Error('Failed to fetch ' + songName);
        const arrayBuffer = await response.arrayBuffer();
        const blob = new Blob([arrayBuffer], { type: 'audio/midi' });
        songFile = new File([blob], songName, { type: 'audio/midi' });
        currentSongName = songUrl;
        elements.nowPlaying.textContent = songName;
        elements.playerPanel.style.display = 'block';
        elements.presetHyperactive.textContent = songName;
        elements.presetHyperactive.classList.add('loaded');
        currentTranspose = 0; currentTempo = 100;
        await player.loadSong(arrayBuffer);
        elements.duration.textContent = formatTime(player.getDuration());
        setTimeout(() => { updateAllChannelTooltips('left'); updateAllChannelTooltips('right'); }, 300);
        player.play();
        elements.playPauseBtn.textContent = 'PAUSE';
        startProgressUpdate();
        updateMascotState();
    } catch (err) {
        console.error('Failed to load preset:', err);
        elements.nowPlaying.textContent = 'Error loading ' + songName;
    }
}

function togglePlayPause() {
    if (!player) return;
    if (player.isPlaying) { player.pause(); elements.playPauseBtn.textContent = 'PLAY'; clearInterval(updateInterval); }
    else if (player.isPaused) { player.resume(); elements.playPauseBtn.textContent = 'PAUSE'; startProgressUpdate(); }
    else { player.play(); elements.playPauseBtn.textContent = 'PAUSE'; startProgressUpdate(); }
    updateMascotState();
}

function stop() {
    if (!player) return;
    player.stop();
    elements.playPauseBtn.textContent = '▶';
    elements.progressBar.style.width = '0%';
    elements.currentTime.textContent = '0:00';
    clearInterval(updateInterval);
    elements.meterLeft.style.width = '0%';
    elements.meterRight.style.width = '0%';
    elements.masterMeterL.style.width = '0%';
    elements.masterMeterR.style.width = '0%';
    for (let ch = 0; ch < 17; ch++) {
        const leftVu = document.getElementById(`vu-left-${ch}`);
        const rightVu = document.getElementById(`vu-right-${ch}`);
        if (leftVu) leftVu.style.width = '0%';
        if (rightVu) rightVu.style.width = '0%';
    }
    updateMascotState();
}

async function exportAudio() {
    if (!player || !songFile) return;
    elements.exportBtn.disabled = true;
    elements.exportProgress.style.display = 'block';
    elements.exportStatus.textContent = 'Rendering audio...';
    elements.exportProgressBar.style.width = '0%';

    try {
        const audioData = await player.exportToRawAudio((progress) => {
            elements.exportProgressBar.style.width = (progress * 70) + '%';
            elements.exportStatus.textContent = `Rendering... ${Math.round(progress * 70)}%`;
        });
        elements.exportStatus.textContent = 'Creating WAV file...';
        elements.exportProgressBar.style.width = '90%';
        await new Promise(r => setTimeout(r, 10));
        const wavData = player.createWavFile(audioData.leftChannel, audioData.rightChannel, audioData.sampleRate);
        elements.exportProgressBar.style.width = '100%';
        elements.exportStatus.textContent = 'Creating download...';
        const blob = new Blob([wavData], { type: 'audio/wav' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `${songFile.name.replace(/\.(mid|midi|rmf|kar|smf)$/i, '')}_mix.wav`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        elements.exportStatus.textContent = 'Export complete!';
        setTimeout(() => { elements.exportProgress.style.display = 'none'; }, 2000);
    } catch (error) {
        console.error('Export error:', error);
        elements.exportStatus.textContent = 'Error: ' + error.message;
    } finally {
        elements.exportBtn.disabled = false;
    }
}

function startProgressUpdate() {
    clearInterval(updateInterval);
    updateInterval = setInterval(() => {
        if (!player) return;
        const pos = player.getPosition();
        const dur = player.getDuration();
        elements.currentTime.textContent = formatTime(pos);
        if (dur > 0) elements.progressBar.style.width = ((pos / dur) * 100) + '%';
    }, 100);
}

function applyTranspose(semitones) {
    currentTranspose = semitones;
    if (player) {
        if (player.moduleLeft) player.moduleLeft._BAE_WASM_SetTranspose(semitones);
        if (player.moduleRight) player.moduleRight._BAE_WASM_SetTranspose(semitones);
    }
}

function applyTempo(percent) {
    currentTempo = percent;
    if (player) {
        if (player.moduleLeft) player.moduleLeft._BAE_WASM_SetTempo(percent);
        if (player.moduleRight) player.moduleRight._BAE_WASM_SetTempo(percent);
    }
}

function setupEventListeners() {
    elements.soundbankLeft.addEventListener('change', loadLeftSoundbank);
    elements.soundbankRight.addEventListener('change', loadRightSoundbank);

    elements.volumeLeft.addEventListener('input', (e) => {
        elements.volumeLeftValue.textContent = e.target.value + '%';
        if (player) player.setVolumeLeft(e.target.value / 100);
    });
    elements.volumeRight.addEventListener('input', (e) => {
        elements.volumeRightValue.textContent = e.target.value + '%';
        if (player) player.setVolumeRight(e.target.value / 100);
    });
    elements.panLeft.addEventListener('input', (e) => {
        elements.panLeftValue.textContent = formatPan(parseInt(e.target.value));
        if (player) player.setPanLeft(parseInt(e.target.value));
    });
    elements.panRight.addEventListener('input', (e) => {
        elements.panRightValue.textContent = formatPan(parseInt(e.target.value));
        if (player) player.setPanRight(parseInt(e.target.value));
    });
    elements.widthLeft.addEventListener('input', (e) => {
        elements.widthLeftValue.textContent = e.target.value == 0 ? 'Mono' : e.target.value + '%';
        if (player) player.setWidthLeft(e.target.value / 100);
    });
    elements.widthRight.addEventListener('input', (e) => {
        elements.widthRightValue.textContent = e.target.value == 0 ? 'Mono' : e.target.value + '%';
        if (player) player.setWidthRight(e.target.value / 100);
    });

    elements.balanceSlider.addEventListener('input', (e) => { if (player) player.setBalance(e.target.value / 100); });
    elements.gainSlider.addEventListener('input', (e) => {
        elements.gainValue.textContent = e.target.value + '%';
        if (player) player.setOutputGain(parseInt(e.target.value) / 100);
    });
    elements.reverbSelect.addEventListener('change', (e) => {
        const reverbType = parseInt(e.target.value);
        if (player) {
            if (player.moduleLeft) player.moduleLeft._BAE_WASM_SetReverbType(reverbType);
            if (player.moduleRight) player.moduleRight._BAE_WASM_SetReverbType(reverbType);
        }
    });

    document.getElementById('upThirdBtn').addEventListener('click', () => applyTranspose(currentTranspose + 3));
    document.getElementById('downFifthBtn').addEventListener('click', () => applyTranspose(currentTranspose - 5));
    document.getElementById('upOctaveBtn').addEventListener('click', () => applyTranspose(currentTranspose + 12));
    document.getElementById('tempoUpBtn').addEventListener('click', () => applyTempo(Math.min(200, currentTempo + 5)));
    document.getElementById('tempoDownBtn').addEventListener('click', () => applyTempo(Math.max(25, currentTempo - 5)));
    document.getElementById('resetBtn').addEventListener('click', () => {
        currentTranspose = 0; currentTempo = 100;
        applyTranspose(0); applyTempo(100);
        elements.reverbSelect.value = '6';
        if (player) {
            if (player.moduleLeft) player.moduleLeft._BAE_WASM_SetReverbType(6);
            if (player.moduleRight) player.moduleRight._BAE_WASM_SetReverbType(6);
        }
    });

    elements.dropZone.addEventListener('click', () => elements.fileInput.click());
    elements.dropZone.addEventListener('dragover', (e) => { e.preventDefault(); elements.dropZone.classList.add('dragover'); });
    elements.dropZone.addEventListener('dragleave', () => elements.dropZone.classList.remove('dragover'));
    elements.dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        elements.dropZone.classList.remove('dragover');
        if (e.dataTransfer.files.length) handleFile(e.dataTransfer.files[0]);
    });
    elements.fileInput.addEventListener('change', (e) => { if (e.target.files.length) handleFile(e.target.files[0]); });

    const dropOverlay = document.getElementById('dropOverlay');
    let dragCounter = 0;
    document.addEventListener('dragenter', (e) => { e.preventDefault(); dragCounter++; dropOverlay.classList.add('visible'); });
    document.addEventListener('dragleave', (e) => { e.preventDefault(); dragCounter--; if (dragCounter === 0) dropOverlay.classList.remove('visible'); });
    document.addEventListener('dragover', (e) => e.preventDefault());
    document.addEventListener('drop', (e) => {
        e.preventDefault();
        dragCounter = 0;
        dropOverlay.classList.remove('visible');
        if (e.dataTransfer.files.length) handleFile(e.dataTransfer.files[0]);
    });

    elements.playPauseBtn.addEventListener('click', togglePlayPause);
    elements.stopBtn.addEventListener('click', stop);
    elements.exportBtn.addEventListener('click', exportAudio);

    document.querySelectorAll('.preset-menu-item').forEach(item => {
        item.addEventListener('click', () => loadPresetSong(item.dataset.song, item.dataset.name));
    });

    elements.beatnikLogo.addEventListener('mouseenter', () => { if (beatnikRmfData) playEffectRmf(beatnikRmfData); });
    elements.mascotGif.addEventListener('click', () => {
        if (currentSongName.includes('hyperactive') && player && player.isPlaying && tellMeRmfData) playEffectRmf(tellMeRmfData);
    });

    elements.progressContainer.addEventListener('click', (e) => {
        if (!player) return;
        const duration = player.getDuration();
        if (duration <= 0) return;
        const rect = elements.progressContainer.getBoundingClientRect();
        const percent = (e.clientX - rect.left) / rect.width;
        const seekPos = Math.floor(percent * duration);
        player.seek(seekPos);
        elements.progressBar.style.width = (percent * 100) + '%';
        elements.currentTime.textContent = formatTime(seekPos);
    });

    elements.pickerClose.addEventListener('click', hideInstrumentPicker);

    // Hide instrument picker when clicking outside
    document.addEventListener('click', (e) => {
        if (!elements.instrumentPicker.contains(e.target) && !e.target.classList.contains('ch-num')) hideInstrumentPicker();
    });
}

async function zinit() {    
    const waitForContext = async () => {
        if (player && player.audioContext) {
            while (player.audioContext.state === 'suspended') {
                await new Promise(r => setTimeout(r, 100));
                player.audioContext.resume();
            }
        }
    };

    await waitForContext();
   
    const urlParams = new URLSearchParams(window.location.search);
    const fileParam = urlParams.get('file');
    const soloParam = urlParams.get('solofile');
    const soloBankParam = urlParams.get('solobank');
    if (fileParam) {
        const filename = fileParam.split('/').pop() || 'song.mid';
        loadPresetSong(fileParam, filename);
    }
    else if (soloParam) {
        if (soloBankParam) {
            elements.soundbankLeft.value = soloBankParam;
            await loadLeftSoundbank();
            player.primeMixer();
        }
        fetch(soloParam)
            .then(response => {
                if (!response.ok) throw new Error('Failed to fetch file from URL');
                return response.arrayBuffer();
            })
            .then(arrayBuffer => {
                const filename = soloParam.split('/').pop() || 'song.mid';
                const blob = new Blob([arrayBuffer], { type: 'audio/midi' });
                const file = new File([blob], filename, { type: 'audio/midi' });
                handleFile(file);
            })
            .catch(error => {
                console.error('Error loading file from URL:', error);
                elements.nowPlaying.textContent = 'Error loading file from URL';
            });
    }
}

let initCalled = false;
function safeInit() {
    if (initCalled) return;
    if (typeof BeatnikModule !== 'undefined') {
        initCalled = true;
        elementIds.forEach(id => elements[id] = document.getElementById(id));
        setupEventListeners();
        init();
        zinit();
    }
}

window.addEventListener('load', () => setTimeout(safeInit, 100));
// Handle ?file= URL parameter
