/**
 * Beatnik Web SDK
 *
 * JavaScript API for playing MIDI and RMF files using the Beatnik Audio Engine
 * compiled to WebAssembly.
 */

class BeatnikPlayer {
    constructor() {
        this._wasmModule = null;
        this._audioContext = null;
        this._workletNode = null;
        this._isInitialized = false;
        this._isPlaying = false;
        this._soundbankLoaded = false;
        this._songLoaded = false;
        this._stopping = false;
        this._unloading = false;

        // Event handlers
        this._eventListeners = {
            ready: [],
            play: [],
            pause: [],
            stop: [],
            end: [],
            error: [],
            timeupdate: [],
            lyric: []
        };

        // Internal state
        this._volume = 1.0;
        this._tempo = 1.0;
        this._transpose = 0;
        this._reverbType = 3; // Acoustic Lab (Beatnik default)
        this._outputGain = 0.5; // Default -6dB to reduce distortion on hot mixes

        // Karaoke state
        this._karaokeLineCurrent = '';
        this._karaokeLinePrevious = '';
        this._karaokeLastFragment = '';
        this._karaokeHaveLyrics = false;

        // Memory pointers that must stay alive
        this._soundbankPtr = null;

        // Song data for export
        this._songData = null;

        // Time update interval
        this._timeUpdateInterval = null;
    }

    /**
     * Initialize the Beatnik engine
     * @param {Object} options - Configuration options
     * @param {number} [options.sampleRate=44100] - Audio sample rate
     * @param {number} [options.maxVoices=64] - Maximum polyphony
     * @param {string} [options.soundbankUrl] - URL to preload soundbank
     * @returns {Promise<BeatnikPlayer>}
     */
    static async init(options = {}) {
        const player = new BeatnikPlayer();
        await player._init(options);
        return player;
    }

    async _init(options) {
        const {
            sampleRate = 44100,
            maxVoices = 64,
            soundbankUrl = null
        } = options;

        try {
            // Load WebAssembly module
            console.log('Waiting for Beatnik WebAssembly module (engine.js)...');
            this._wasmModule = await this._waitForWasm();

            // Create AudioContext
            this._audioContext = new (window.AudioContext || window.webkitAudioContext)({
                sampleRate: sampleRate
            });

            // Initialize BAE in WASM
            const result = this._wasmModule._BAE_WASM_Init(sampleRate, maxVoices);
            if (result !== 0) {
                throw new Error(`Failed to initialize Beatnik engine: error ${result}`);
            }

            console.log("Beatnik engine initialized");

            // Set up audio processing
            await this._setupAudioWorklet();

            // Apply default output gain (reduces distortion on hot mixes)
            this._wasmModule._BAE_WASM_SetOutputGain(Math.floor(this._outputGain * 256));

            // Set up karaoke lyric callback
            this._setupLyricCallback();

            // Preload soundbank if specified
            if (soundbankUrl) {
                await this.loadSoundbank(soundbankUrl);
            }

            this._isInitialized = true;

            this._dispatchEvent('ready');

        } catch (error) {
            this._dispatchEvent('error', error);
            throw error;
        }
    }

    async _waitForWasm() {
        // BeatnikModule should be loaded from engine.js
        // Wait for it to be available (it's loaded via script tag)
        let attempts = 0;
        while (typeof BeatnikModule === 'undefined' && attempts < 100) {
            await new Promise(r => setTimeout(r, 50));
            attempts++;
        }

        if (typeof BeatnikModule === 'undefined') {
            throw new Error('BeatnikModule not loaded. Make sure minibae.js is included.');
        }

        // Initialize the Emscripten module
        const module = await BeatnikModule();
        return module;
    }

    async _setupAudioWorklet() {
        // AudioWorklet path is stubbed; use ScriptProcessorNode for now
        this._setupScriptProcessor();
    }

    _setupScriptProcessor() {
        // Match the native mixer buffer (AUDIO_BUFFER_FRAMES) to avoid underruns/clicks
        const bufferSize = 512;
        const processor = this._audioContext.createScriptProcessor(bufferSize, 0, 2);

        processor.onaudioprocess = (event) => {
            if (!this._isPlaying || !this._wasmModule) {
                return;
            }

            const left = event.outputBuffer.getChannelData(0);
            const right = event.outputBuffer.getChannelData(1);
            const frames = left.length;

            // Generate audio in WASM
            const bufferPtr = this._wasmModule._BAE_WASM_GenerateAudio(frames);

            // Copy from WASM memory to output buffers
            const wasmBuffer = new Int16Array(
                this._wasmModule.HEAP16.buffer,
                bufferPtr,
                frames * 2
            );

            // Convert 16-bit samples to float and deinterleave
            for (let i = 0; i < frames; i++) {
                left[i] = wasmBuffer[i * 2] / 32768.0;
                right[i] = wasmBuffer[i * 2 + 1] / 32768.0;
            }
        };

        // Create analyser nodes for VU meter / audioscope
        this._analyserLeft = this._audioContext.createAnalyser();
        this._analyserRight = this._audioContext.createAnalyser();
        this._analyserLeft.fftSize = 256;
        this._analyserRight.fftSize = 256;
        this._analyserLeft.smoothingTimeConstant = 0.8;
        this._analyserRight.smoothingTimeConstant = 0.8;

        // Create channel splitter to separate L/R for analysers
        const splitter = this._audioContext.createChannelSplitter(2);

        // Connect: processor -> splitter -> analysers (for monitoring)
        //          processor -> destination (for playback)
        processor.connect(splitter);
        splitter.connect(this._analyserLeft, 0);
        splitter.connect(this._analyserRight, 1);
        processor.connect(this._audioContext.destination);

        this._workletNode = processor;
    }

    /**
     * Set up lyric callback for karaoke support
     * @private
     */
    _setupLyricCallback() {
        if (!this._wasmModule._BAE_WASM_SetLyricCallback) {
            console.warn('Lyric callback not available in this build');
            return;
        }

        // Create JavaScript callback function that WASM can call
        const lyricCallback = this._wasmModule.addFunction((lyricPtr, timeUs) => {
            // Read as ASCII/Latin-1 instead of UTF-8 to handle control characters properly
            const heap = this._wasmModule.HEAPU8;
            let len = 0;
            while (heap[lyricPtr + len] !== 0 && len < 256) len++;
            
            // Build string from raw bytes (treating as Latin-1/ISO-8859-1)
            let lyric = '';
            for (let i = 0; i < len; i++) {
                const byte = heap[lyricPtr + i];
                // Filter out control characters except newline/tab
                if (byte === 0x18 || (byte < 0x20 && byte !== 0x0A && byte !== 0x0D && byte !== 0x09)) {
                    // Skip control characters like 0x18
                    continue;
                }
                lyric += String.fromCharCode(byte);
            }
            
            this._handleLyric(lyric, timeUs);
        }, 'vii');  // void function(int, int) signature

        // Register with WASM
        this._wasmModule._BAE_WASM_SetLyricCallback(lyricCallback);
    }

    /**
     * Handle incoming lyric event (called from WASM)
     * @private
     */
    _handleLyric(lyric, timeUs) {
        if (!lyric) return;

        // Filter out preroll lyrics dump: only accept lyrics that are near current playback position
        // During preroll, all lyrics fire with their actual timestamps but we're at position 0
        // So we ignore any lyric that's more than 2 seconds ahead of current position
        const currentTimeMs = this.currentTime;
        const lyricTimeMs = timeUs / 1000;
        const timeDelta = Math.abs(lyricTimeMs - currentTimeMs);
        
        // Allow 2 second window (handles both preroll dump and slight timing variations)
        if (timeDelta > 2000) {
            return;
        }

        this._karaokeHaveLyrics = true;

        // Handle empty lyric as newline
        if (lyric === '') {
            this._karaokeNewline();
            this._dispatchEvent('lyric', {
                previous: this._karaokeLinePrevious,
                current: this._karaokeLineCurrent,
                fragment: ''
            });
            return;
        }

        // Process '/' or '\\' as newline delimiters
        const segments = lyric.split(/[/\\\\]/);
        
        for (let i = 0; i < segments.length; i++) {
            const segment = segments[i];
            
            if (segment) {
                this._karaokeAddFragment(segment);
            }
            
            // Add newline between segments (but not after last segment)
            if (i < segments.length - 1) {
                this._karaokeNewline();
            }
        }

        // Dispatch event with current state
        this._dispatchEvent('lyric', {
            previous: this._karaokeLinePrevious,
            current: this._karaokeLineCurrent,
            fragment: this._karaokeLastFragment
        });
    }

    /**
     * Add lyric fragment to current line
     * @private
     */
    _karaokeAddFragment(fragment) {
        if (!fragment) return;

        const fragLen = fragment.length;
        const lastLen = this._karaokeLastFragment.length;
        
        // Check if this is a cumulative extension (growing substring)
        const cumulativeExtension = lastLen > 0 && fragLen > lastLen &&
                                    fragment.startsWith(this._karaokeLastFragment);

        if (cumulativeExtension) {
            // Replace with growing cumulative substring
            this._karaokeLineCurrent = fragment;
        } else {
            // Append raw fragment
            this._karaokeLineCurrent += fragment;
        }

        this._karaokeLastFragment = fragment;
    }

    /**
     * Process newline in karaoke lyrics
     * @private
     */
    _karaokeNewline() {
        if (this._karaokeLineCurrent) {
            this._karaokeLinePrevious = this._karaokeLineCurrent;
            this._karaokeLineCurrent = '';
        }
        this._karaokeLastFragment = '';
    }

    /**
     * Reset karaoke state
     * @private
     */
    _resetKaraoke() {
        this._karaokeLineCurrent = '';
        this._karaokeLinePrevious = '';
        this._karaokeLastFragment = '';
        this._karaokeHaveLyrics = false;

        if (this._wasmModule && this._wasmModule._BAE_WASM_ResetLyricState) {
            this._wasmModule._BAE_WASM_ResetLyricState();
        }
    }

    /**
     * Get current karaoke state
     * @returns {Object} {haveLyrics: boolean, current: string, previous: string, fragment: string}
     */
    getKaraokeState() {
        return {
            haveLyrics: this._karaokeHaveLyrics,
            current: this._karaokeLineCurrent,
            previous: this._karaokeLinePrevious,
            fragment: this._karaokeLastFragment
        };
    }

    /**
     * Get the left channel analyser node for VU meter / audioscope
     * @returns {AnalyserNode|null}
     */
    get analyserLeft() {
        return this._analyserLeft || null;
    }

    /**
     * Get the right channel analyser node for VU meter / audioscope
     * @returns {AnalyserNode|null}
     */
    get analyserRight() {
        return this._analyserRight || null;
    }

    /**
     * Load a soundbank file
     * @param {string|ArrayBuffer} source - URL or ArrayBuffer of HSB/GM file
     * @returns {Promise<void>}
     */
    async loadSoundbank(source) {
        if (!this._isInitialized) {
            const error = new Error('BeatnikPlayer not initialized');
            this._dispatchEvent('error', error);
            throw error;
        }
        
        try {
            let data;
            if (typeof source === 'string') {
                // Fetch from URL
                const response = await fetch(source);
                if (!response.ok) {
                    throw new Error(`Failed to fetch soundbank: ${response.status} ${response.statusText}`);
                }
                data = await response.arrayBuffer();
            } else {
                data = source;
            }

            // Free previous soundbank memory if exists
            if (this._soundbankPtr) {
                this._wasmModule._free(this._soundbankPtr);
                this._soundbankPtr = null;
            }

            // Copy to WASM memory
            const ptr = this._wasmModule._malloc(data.byteLength);
            const heapBytes = new Uint8Array(this._wasmModule.HEAPU8.buffer, ptr, data.byteLength);
            heapBytes.set(new Uint8Array(data));

            // Load in WASM
            const result = this._wasmModule._BAE_WASM_LoadSoundbank(ptr, data.byteLength);

            if (result !== 0) {
                this._wasmModule._free(ptr);
                const error = new Error(`Failed to load soundbank: error ${result}`);
                this._dispatchEvent('error', error);
                throw error;
            }

            // Keep soundbank memory alive - BAE doesn't copy it
            this._soundbankPtr = ptr;
            this._soundbankLoaded = true;
        } catch (error) {
            this._dispatchEvent('error', error);
            throw error;
        }
    }

    /**
     * Load a MIDI or RMF file
     * @param {string|ArrayBuffer} source - URL or ArrayBuffer
     * @param {string} [type='auto'] - File type: 'midi', 'rmf', or 'auto'
     * @returns {Promise<void>}
     */
    async load(source, type = 'auto') {
        if (!this._soundbankLoaded) {
            const error = new Error('No soundbank loaded');
            this._dispatchEvent('error', error);
            throw error;
        }

        try {
            let data;
            if (typeof source === 'string') {
                const response = await fetch(source);
                if (!response.ok) {
                    throw new Error(`Failed to fetch song: ${response.status} ${response.statusText}`);
                }
                data = await response.arrayBuffer();
            } else {
                data = source;
            }

            // Copy to WASM memory
            const ptr = this._wasmModule._malloc(data.byteLength);
            const heapBytes = new Uint8Array(this._wasmModule.HEAPU8.buffer, ptr, data.byteLength);
            heapBytes.set(new Uint8Array(data));

            // Store song data for export
            this._songData = new Uint8Array(data);

            // Load in WASM
            const result = this._wasmModule._BAE_WASM_LoadSong(ptr, data.byteLength);

            this._wasmModule._free(ptr);

            if (result !== 0) {
                const error = new Error(`Failed to load song: error ${result}`);
                this._dispatchEvent('error', error);
                throw error;
            }

            this._songLoaded = true;

            // Reset karaoke state for new song
            this._resetKaraoke();

            // Apply current settings
            this.volume = this._volume;
            this.tempo = this._tempo;
            this.transpose = this._transpose;
            this.reverbType = this._reverbType;
            this.outputGain = this._outputGain;
        } catch (error) {
            this._dispatchEvent('error', error);
            throw error;
        }
    }

    /**
     * Start playback
     */
    play() {
        try {
            if (!this._songLoaded) {
                const error = new Error('No song loaded');
                this._dispatchEvent('error', error);
                throw error;
            }

            // Resume audio context if suspended (browser autoplay policy)
            if (this._audioContext.state === 'suspended') {
                this._audioContext.resume();
            }

            const result = this._wasmModule._BAE_WASM_Play();
            if (result !== 0) {
                const error = new Error(`Failed to start playback: error ${result}`);
                this._dispatchEvent('error', error);
                throw error;
            }

            this._isPlaying = true;
            this._startTimeUpdates();

            this._dispatchEvent('play');
        } catch (error) {
            this._dispatchEvent('error', error);
            throw error;
        }
    }

    /**
     * Pause playback
     */
    pause() {
        if (!this._isPlaying) return;

        this._wasmModule._BAE_WASM_Pause();
        this._isPlaying = false;
        this._stopTimeUpdates();

        this._dispatchEvent('pause');
    }

    /**
     * Resume playback
     */
    resume() {
        if (this._isPlaying) return;

        this._wasmModule._BAE_WASM_Resume();
        this._isPlaying = true;
        this._startTimeUpdates();

        this._dispatchEvent('play');
    }

    /**
     * Stop playback
     */
    stop() {
        if (this._stopping) {
            return;
        }
        this._stopping = true;
        this._wasmModule._BAE_WASM_Stop();
        this._isPlaying = false;
        this._stopTimeUpdates();

        this._dispatchEvent('stop');
        this._stopping = false;
    }

    unload() {
        if (this._wasmModule) {
            this.stop();
            this._wasmModule._BAE_WASM_UnloadSong();
        }
    }

    unloadSoundbank() {
        if (this._unloading) {
            return;
        }
        if (this._wasmModule) {
            this._unloading = true;
            this.stop();
            this._wasmModule._BAE_WASM_UnloadSoundbank();
            this._songLoaded = false;
            this._songData = null;
            this._soundbankLoaded = false;
            if (this._soundbankPtr) {
                this._wasmModule._free(this._soundbankPtr);
                this._soundbankPtr = null;
            }
            this._unloading = false;
        }
    }
    /**
     * Current playback position in milliseconds
     */
    get currentTime() {
        if (!this._wasmModule) return 0;
        return this._wasmModule._BAE_WASM_GetPosition();
    }

    set currentTime(ms) {
        if (!this._wasmModule) return;
        this._wasmModule._BAE_WASM_SetPosition(Math.floor(ms));
    }

    /**
     * Duration in milliseconds
     */
    get duration() {
        if (!this._wasmModule) return 0;
        return this._wasmModule._BAE_WASM_GetDuration();
    }

    /**
     * Is currently playing
     */
    get isPlaying() {
        return this._isPlaying;
    }

    /**
     * Volume (0.0 to 1.0)
     */
    get volume() {
        return this._volume;
    }

    set volume(value) {
        this._volume = Math.max(0, Math.min(1, value));
        if (this._wasmModule) {
            this._wasmModule._BAE_WASM_SetVolume(Math.floor(this._volume * 100));
        }
    }

    /**
     * Tempo multiplier (1.0 = normal)
     */
    get tempo() {
        return this._tempo;
    }

    set tempo(value) {
        this._tempo = Math.max(0.25, Math.min(4, value));
        if (this._wasmModule) {
            this._wasmModule._BAE_WASM_SetTempo(Math.floor(this._tempo * 100));
        }
    }

    /**
     * Transpose in semitones (-12 to +12)
     */
    get transpose() {
        return this._transpose;
    }

    set transpose(value) {
        this._transpose = Math.max(-12, Math.min(12, Math.floor(value)));
        if (this._wasmModule) {
            this._wasmModule._BAE_WASM_SetTranspose(this._transpose);
        }
    }

    /**
     * Reverb type (0-11)
     */
    get reverbType() {
        return this._reverbType;
    }

    set reverbType(value) {
        this._reverbType = Math.max(0, Math.min(11, Math.floor(value)));
        if (this._wasmModule) {
            this._wasmModule._BAE_WASM_SetReverbType(this._reverbType);
        }
    }

    /**
     * Output gain (0.0 to 2.0, where 1.0 = unity gain)
     * Use values below 1.0 to reduce distortion on loud files
     * Default is 0.75 (~-2.5dB) to prevent clipping
     */
    get outputGain() {
        return this._outputGain;
    }

    set outputGain(value) {
        this._outputGain = Math.max(0, Math.min(2, value));
        if (this._wasmModule) {
            // Convert 0-2 to 0-512 (256 = unity)
            this._wasmModule._BAE_WASM_SetOutputGain(Math.floor(this._outputGain * 256));
        }
    }

    /**
     * Mute/unmute a MIDI channel
     * @param {number} channel - Channel number (0-15)
     * @param {boolean} muted - Mute state
     */
    muteChannel(channel, muted) {
        if (this._wasmModule) {
            this._wasmModule._BAE_WASM_MuteChannel(channel, muted ? 1 : 0);
        }
    }

    /**
     * Mute/unmute a MIDI track
     * @param {number} track - Track number (1-based)
     * @param {boolean} muted - Mute state
     */
    muteTrack(track, muted) {
        if (this._wasmModule) {
            this._wasmModule._BAE_WASM_MuteTrack(track, muted ? 1 : 0);
        }
    }

    /**
     * Solo/unsolo a MIDI track
     * When a track is soloed, only soloed tracks produce sound
     * @param {number} track - Track number (1-based)
     * @param {boolean} soloed - Solo state
     */
    soloTrack(track, soloed) {
        if (this._wasmModule) {
            this._wasmModule._BAE_WASM_SoloTrack(track, soloed ? 1 : 0);
        }
    }

    /**
     * Get the mute status of a track
     * @param {number} track - Track number (1-based)
     * @returns {boolean} True if muted
     */
    isTrackMuted(track) {
        if (!this._wasmModule) return false;
        return this._wasmModule._BAE_WASM_GetTrackMuteStatus(track) === 1;
    }

    /**
     * Get the solo status of a track
     * @param {number} track - Track number (1-based)
     * @returns {boolean} True if soloed
     */
    isTrackSoloed(track) {
        if (!this._wasmModule) return false;
        return this._wasmModule._BAE_WASM_GetTrackSoloStatus(track) === 1;
    }

    /**
     * Change the program (instrument) on a MIDI channel
     * @param {number} channel - Channel number (0-15)
     * @param {number} program - General MIDI program number (0-127)
     */
    setProgram(channel, program) {
        if (this._wasmModule) {
            return this._wasmModule._BAE_WASM_ProgramChange(channel, program);
        }
        return -1;
    }

    /**
     * Change the program (instrument) on a MIDI channel with bank selection
     * @param {number} channel - Channel number (0-15)
     * @param {number} bank - Bank number (0-127)
     * @param {number} program - General MIDI program number (0-127)
     * @returns {number} 0 on success, error code on failure
     */
    setProgramBank(channel, bank, program) {
        if (this._wasmModule) {
            return this._wasmModule._BAE_WASM_ProgramBankChange(channel, bank, program);
        }
        return -1;
    }

    /**
     * Get the current program (instrument) on a MIDI channel
     * @param {number} channel - Channel number (0-15)
     * @returns {number} Program number (0-127), or -1 on error
     */
    getProgram(channel) {
        if (this._wasmModule) {
            return this._wasmModule._BAE_WASM_GetProgram(channel);
        }
        return -1;
    }

    /**
     * Get song metadata
     * @param {string} key - Info key: 'title', 'composer', 'copyright', etc.
     * @returns {string}
     */
    getSongInfo(key) {
        if (!this._wasmModule || !this._songLoaded) return '';

        const infoTypes = {
            'title': 0,
            'composer': 1,
            'copyright': 2,
            'performer': 3,
            'publisher': 4
        };

        const type = infoTypes[key.toLowerCase()] ?? 0;
        const bufferSize = 256;
        const ptr = this._wasmModule._malloc(bufferSize);

        this._wasmModule._BAE_WASM_GetSongInfo(type, ptr, bufferSize);

        const result = this._wasmModule.UTF8ToString(ptr);
        this._wasmModule._free(ptr);

        return result;
    }

    /**
     * Load an RMF/MIDI as a sound effect (plays on top of main song)
     * @param {ArrayBuffer} data - RMF or MIDI file data
     * @returns {Promise<number>} 0 on success, error code on failure
     */
    async loadEffect(data) {
        if (!this._wasmModule) return -1;

        const uint8Array = new Uint8Array(data);
        const ptr = this._wasmModule._malloc(uint8Array.length);
        this._wasmModule.HEAPU8.set(uint8Array, ptr);

        const result = this._wasmModule._BAE_WASM_LoadEffect(ptr, uint8Array.length);
        this._wasmModule._free(ptr);

        return result;
    }

    /**
     * Play the loaded effect (overlaid on main song)
     * @returns {number} 0 on success, error code on failure
     */
    playEffect() {
        if (this._wasmModule) {
            return this._wasmModule._BAE_WASM_PlayEffect();
        }
        return -1;
    }

    /**
     * Stop the effect song
     * @returns {number} 0 on success
     */
    stopEffect() {
        if (this._wasmModule) {
            return this._wasmModule._BAE_WASM_StopEffect();
        }
        return 0;
    }

    /**
     * Check if effect is currently playing
     * @returns {boolean}
     */
    isEffectPlaying() {
        if (this._wasmModule) {
            return this._wasmModule._BAE_WASM_IsEffectPlaying() !== 0;
        }
        return false;
    }

    /**
     * Load an RMF as a sample bank (for voice/sample RMFs without MIDI)
     * This allows RMF files containing only samples (like voice files) to be triggered
     * @param {ArrayBuffer} data - RMF file data
     * @returns {Promise<number>} 0 on success, error code on failure
     */
    async loadSampleBank(data) {
        if (!this._wasmModule) return -1;

        const uint8Array = new Uint8Array(data);
        const ptr = this._wasmModule._malloc(uint8Array.length);
        this._wasmModule.HEAPU8.set(uint8Array, ptr);

        const result = this._wasmModule._BAE_WASM_LoadSampleBank(ptr, uint8Array.length);
        this._wasmModule._free(ptr);

        return result;
    }

    /**
     * Trigger a sample from a loaded sample bank
     * @param {number} bank - Bank number (usually 2 for RMF samples)
     * @param {number} program - Program number (usually 0)
     * @param {number} note - MIDI note number (60 = middle C is typical)
     * @param {number} velocity - Velocity 0-127
     * @returns {number} 0 on success, error code on failure
     */
    triggerSample(bank, program, note, velocity) {
        if (this._wasmModule) {
            return this._wasmModule._BAE_WASM_TriggerSample(bank, program, note, velocity);
        }
        return -1;
    }

    _startTimeUpdates() {
        if (this._timeUpdateInterval) return;

        this._timeUpdateInterval = setInterval(() => {
            if (!this._isPlaying) return;

            // Check if song ended
            if (this._wasmModule._BAE_WASM_IsPlaying() === 0) {
                this._isPlaying = false;
                this._stopTimeUpdates();
                this._dispatchEvent('end');
                return;
            }

            this._dispatchEvent('timeupdate', this.currentTime);
        }, 100);
    }

    _stopTimeUpdates() {
        if (this._timeUpdateInterval) {
            clearInterval(this._timeUpdateInterval);
            this._timeUpdateInterval = null;
        }
    }

    /**
     * Add event listener
     * @param {string} event - Event type: 'ready', 'play', 'pause', 'stop', 'end', 'error', 'timeupdate'
     * @param {function} listener - Event listener function
     */
    addEventListener(event, listener) {
        if (this._eventListeners[event]) {
            this._eventListeners[event].push(listener);
        }
    }

    /**
     * Remove event listener
     * @param {string} event - Event type
     * @param {function} listener - Event listener function to remove
     */
    removeEventListener(event, listener) {
        if (this._eventListeners[event]) {
            const index = this._eventListeners[event].indexOf(listener);
            if (index > -1) {
                this._eventListeners[event].splice(index, 1);
            }
        }
    }

    /**
     * Dispatch event to all listeners
     * @private
     * @param {string} event - Event type
     * @param {...any} args - Arguments to pass to listeners
     */
    _dispatchEvent(event, ...args) {
        if (this._eventListeners[event]) {
            this._eventListeners[event].forEach(listener => {
                try {
                    listener(...args);
                } catch (error) {
                    console.error(`Error in ${event} event listener:`, error);
                }
            });
        }
    }

    /**
     * Export the current song to raw audio data
     * @param {function} onProgress - Progress callback (0.0 to 1.0)
     * @param {number|null} duration - Duration in milliseconds to export (null = full song)
     * @returns {Promise<{leftChannel: Float32Array, rightChannel: Float32Array, sampleRate: number}>}
     */
    async exportToRawAudio(onProgress, duration = null) {
        if (!this._songLoaded) {
            throw new Error('No song loaded');
        }

        const wasPlaying = this._isPlaying;
        if (wasPlaying) {
            this.stop();
        }

        // Reload the song for a clean export
        const songData = this._songData;
        if (!songData) {
            throw new Error('Song data not available');
        }

        const ptr = this._wasmModule._malloc(songData.byteLength);
        const heapBytes = new Uint8Array(this._wasmModule.HEAPU8.buffer, ptr, songData.byteLength);
        heapBytes.set(songData);
        this._wasmModule._BAE_WASM_LoadSong(ptr, songData.byteLength);
        this._wasmModule._free(ptr);

        // Apply current settings
        this._wasmModule._BAE_WASM_SetVolume(Math.floor(this._volume * 100));
        this._wasmModule._BAE_WASM_SetTempo(Math.floor(this._tempo * 100));
        this._wasmModule._BAE_WASM_SetTranspose(this._transpose);
        this._wasmModule._BAE_WASM_SetReverbType(this._reverbType);
        this._wasmModule._BAE_WASM_SetOutputGain(Math.floor(this._outputGain * 256));

        // Start playback for rendering
        this._wasmModule._BAE_WASM_Play();

        // Warmup frames
        const warmupFrames = 16;
        this._wasmModule._BAE_WASM_GenerateAudio(warmupFrames);

        const sampleRate = this._audioContext.sampleRate;
        const durationValue = duration !== null ? duration : this._wasmModule._BAE_WASM_GetDuration();
        const totalFrames = Math.ceil(((durationValue / 1000) - 1) * sampleRate) + sampleRate;
        const chunkSize = 512;

        const leftChannel = new Float32Array(totalFrames);
        const rightChannel = new Float32Array(totalFrames);

        let framesRendered = 0;
        while (framesRendered < totalFrames) {
            const framesToRender = Math.min(chunkSize, totalFrames - framesRendered);

            const bufferPtr = this._wasmModule._BAE_WASM_GenerateAudio(framesToRender);
            const wasmBuffer = new Int16Array(
                this._wasmModule.HEAP16.buffer,
                bufferPtr,
                framesToRender * 2
            );

            for (let i = 0; i < framesToRender; i++) {
                leftChannel[framesRendered + i] = wasmBuffer[i * 2] / 32768.0;
                rightChannel[framesRendered + i] = wasmBuffer[i * 2 + 1] / 32768.0;
            }

            framesRendered += framesToRender;
            if (onProgress) {
                onProgress(framesRendered / totalFrames);
            }

            // Yield to event loop periodically
            if (framesRendered % (chunkSize * 20) === 0) {
                await new Promise(r => setTimeout(r, 0));
            }
        }

        this._wasmModule._BAE_WASM_Stop();

        return { leftChannel, rightChannel, sampleRate };
    }

    /**
     * Create a WAV file from audio data
     * @param {Float32Array} leftChannel - Left channel audio data
     * @param {Float32Array} rightChannel - Right channel audio data
     * @param {number} sampleRate - Sample rate in Hz
     * @returns {ArrayBuffer} WAV file data
     */
    createWavFile(leftChannel, rightChannel, sampleRate) {
        const numChannels = 2;
        const bitsPerSample = 16;
        const bytesPerSample = bitsPerSample / 8;
        const numSamples = leftChannel.length;
        const dataSize = numSamples * numChannels * bytesPerSample;
        const fileSize = 44 + dataSize;

        const buffer = new ArrayBuffer(fileSize);
        const view = new DataView(buffer);

        const writeString = (offset, string) => {
            for (let i = 0; i < string.length; i++) {
                view.setUint8(offset + i, string.charCodeAt(i));
            }
        };

        // RIFF header
        writeString(0, 'RIFF');
        view.setUint32(4, fileSize - 8, true);
        writeString(8, 'WAVE');
        
        // fmt chunk
        writeString(12, 'fmt ');
        view.setUint32(16, 16, true); // fmt chunk size
        view.setUint16(20, 1, true); // audio format (PCM)
        view.setUint16(22, numChannels, true);
        view.setUint32(24, sampleRate, true);
        view.setUint32(28, sampleRate * numChannels * bytesPerSample, true); // byte rate
        view.setUint16(32, numChannels * bytesPerSample, true); // block align
        view.setUint16(34, bitsPerSample, true);
        
        // data chunk
        writeString(36, 'data');
        view.setUint32(40, dataSize, true);

        // Write interleaved audio data
        let offset = 44;
        for (let i = 0; i < numSamples; i++) {
            // Clamp and convert to 16-bit PCM
            const leftSample = Math.max(-1, Math.min(1, leftChannel[i]));
            const rightSample = Math.max(-1, Math.min(1, rightChannel[i]));
            
            view.setInt16(offset, Math.floor(leftSample * 32767), true);
            view.setInt16(offset + 2, Math.floor(rightSample * 32767), true);
            offset += 4;
        }

        return buffer;
    }

    /**
     * Clean up resources
     */
    dispose() {
        this._stopTimeUpdates();        

        if (this._workletNode) {
            this._workletNode.disconnect();
            this._workletNode = null;
        }

        if (this._audioContext) {
            this._audioContext.close();
            this._audioContext = null;
        }

        if (this._wasmModule) {
            this.unload();
            this.unloadSoundbank();
            this._wasmModule._BAE_WASM_Shutdown();

            // Free soundbank memory after shutdown
            if (this._soundbankPtr) {
                this._wasmModule._free(this._soundbankPtr);
                this._soundbankPtr = null;
            }

            this._wasmModule = null;
        }

        this._isInitialized = false;
        this._isPlaying = false;
        this._songLoaded = false;
        this._soundbankLoaded = false;
        
        // Clear all event listeners
        Object.keys(this._eventListeners).forEach(event => {
            this._eventListeners[event] = [];
        });
    }
}

// Reverb type constants
BeatnikPlayer.REVERB_NONE = 0;
BeatnikPlayer.REVERB_CLOSET = 1;
BeatnikPlayer.REVERB_GARAGE = 2;
BeatnikPlayer.REVERB_ACOUSTIC_LAB = 3;
BeatnikPlayer.REVERB_CAVERN = 4;
BeatnikPlayer.REVERB_DUNGEON = 5;
BeatnikPlayer.REVERB_SMALL_REFLECTIONS = 6;
BeatnikPlayer.REVERB_EARLY_REFLECTIONS = 7;
BeatnikPlayer.REVERB_BASEMENT = 8;
BeatnikPlayer.REVERB_BANQUET_HALL = 9;
BeatnikPlayer.REVERB_CATACOMBS = 10;

// Export for module systems
if (typeof module !== 'undefined' && module.exports) {
    module.exports = BeatnikPlayer;
}

// Also attach to window for script tag usage
if (typeof window !== 'undefined') {
    window.BeatnikPlayer = BeatnikPlayer;
}
