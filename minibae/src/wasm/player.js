class MiniBAEPlayer {
    constructor() {
        this.player = null;
        this.isLooping = true;
        this.isPaused = false;
        this.channelMutes = new Array(16).fill(false);
        this.transpose = 0;
        this.tempo = 100;
        this.default_media_file = "/content/RMF/Other/trance.rmf";
        this.default_bank_mid = "/content/PatchBanks/SF3/GeneralUser-GS.sf3";
        this.default_bank_rmf = "/content/PatchBanks/HSB/patchesp.hsb";
        this.default_reverb = 7; // Early Reflections
        this.userMediaFile = null;
        this.userBankFile = null;

        // Get file parameter from URL
        const urlParams = new URLSearchParams(window.location.search);
        const fileName = urlParams.get('file') !== null ? urlParams.get('file') : this.default_media_file;
        const customBank = urlParams.get('bank');
        
        // Parse and sanity check reverb (0-11 based on available types)
        const customReverb = urlParams.get('reverb') !== null ? parseInt(urlParams.get('reverb')) : this.default_reverb;
        
        // Parse and sanity check transpose (-12 to 12 semitones)
        let customTranspose = urlParams.get('transpose') !== null ? parseInt(urlParams.get('transpose')) : 0;
        customTranspose = Math.max(-12, Math.min(12, customTranspose));
        
        // Parse and sanity check tempo (50 to 200%)
        let customTempo = urlParams.get('tempo') !== null ? parseInt(urlParams.get('tempo')) : 100;
        customTempo = Math.max(50, Math.min(200, customTempo));
        
        // Parse and sanity check volume (0 to 100%)
        let customVolume = urlParams.get('volume') !== null ? parseInt(urlParams.get('volume')) : 100;
        customVolume = Math.max(0, Math.min(100, customVolume));
        
        // Parse muted channels (1-indexed, comma-separated or single value)
        const mutedParam = urlParams.get('muted');
        const mutedChannels = new Set();
        if (mutedParam) {
            const channels = mutedParam.includes(',') ? mutedParam.split(',') : [mutedParam];
            channels.forEach(ch => {
                const channelNum = parseInt(ch.trim());
                // Convert 1-indexed to 0-indexed and validate range (1-16)
                if (channelNum >= 1 && channelNum <= 16) {
                    mutedChannels.add(channelNum - 1);
                }
            });
        }
        
        // Parse loop parameter (default: enabled)
        const customLoop = urlParams.get('loop') !== null ? urlParams.get('loop') !== '0' : true;
        
        // Parse seek parameter (in seconds, will be validated after load)
        const customSeek = urlParams.get('seek') !== null ? parseFloat(urlParams.get('seek')) : null;
        
        // Parse virtual keyboard channel (1-16, default to 1)
        let customVkbdChannel = urlParams.get('vkbd') !== null ? parseInt(urlParams.get('vkbd')) : 1;
        // Validate and convert to 0-indexed (0-15)
        if (isNaN(customVkbdChannel) || customVkbdChannel < 1 || customVkbdChannel > 16) {
            customVkbdChannel = 1; // default to channel 1
        }
        customVkbdChannel = customVkbdChannel - 1; // Convert to 0-indexed
        
        this.autostart = urlParams.get('autostart') === '1';

        // Detect file type and set appropriate bank
        if (fileName && fileName.toLowerCase().endsWith('.rmf')) {
            this.bank = customBank || this.default_bank_rmf;
        } else {
            this.bank = customBank || this.default_bank_mid;
        }

        this.initElements();
        this.initChannels();
        this.initKeyboard();
        this.bindEvents();
        this.setupFileLoading();


        
        // Start loading immediately
        if (fileName) {
            this.initPlayer().then(() => {
                if (this.player && this.player._wasmModule) {
                    const versionPtr = this.player._wasmModule._BAE_WASM_GetVersionString();
                    if (versionPtr) {
                        const version = this.player._wasmModule.UTF8ToString(versionPtr);
                        document.querySelector('.title-bar-text').textContent = `zefidi web player - ${version}`;
                        // Add click handler for title bar to show features
                        document.querySelector('.title-bar-text').style.cursor = 'pointer';
                        document.querySelector('.title-bar-text').addEventListener('click', () => {
                            const featuresPtr = this.player._wasmModule._BAE_WASM_GetFeatureString();
                            if (featuresPtr) {
                                const features = this.player._wasmModule.UTF8ToString(featuresPtr);
                                this.showFeaturesModal(version, features);
                            }
                        });
                        
                        // Add click handler for help icon
                        document.getElementById('helpIcon').addEventListener('click', (e) => {
                            e.stopPropagation();
                            this.showHelpModal();
                        });
                    }                            
                }                        
                this.load(fileName, this.bank).then(() => {
                    // After loading, wait for audio context if needed, then optionally autostart
                    this.waitForAudioContext().then(() => {
                        this.player._wasmModule._BAE_WASM_SetReverbType(customReverb);
                        // Update dropdown to match the custom reverb setting
                        this.elements.reverbSelect.value = customReverb.toString();
                        
                        // Apply custom transpose
                        this.elements.transposeSlider.value = customTranspose;
                        this.elements.transposeSlider.dispatchEvent(new Event('input'));
                        
                        // Apply custom tempo
                        this.elements.tempoSlider.value = customTempo;
                        this.elements.tempoSlider.dispatchEvent(new Event('input'));
                        
                        // Apply custom volume
                        this.elements.volumeSlider.value = customVolume;
                        this.elements.volumeSlider.dispatchEvent(new Event('input'));
                        
                        // Apply muted channels
                        if (mutedChannels.size > 0) {
                            const checkboxes = this.elements.channelsGrid.querySelectorAll('input[type="checkbox"]');
                            mutedChannels.forEach(channelIndex => {
                                if (checkboxes[channelIndex]) {
                                    checkboxes[channelIndex].checked = false;
                                    this.channelMutes[channelIndex] = true;
                                    if (this.player && this.player._songLoaded) {
                                        this.player.muteChannel(channelIndex, true);
                                    }
                                }
                            });
                        }
                        
                        // Apply loop setting
                        this.isLooping = customLoop;
                        this.elements.loopCheckbox.checked = customLoop;
                        if (this.player && this.player._songLoaded) {
                            this.player._wasmModule._BAE_WASM_SetLoops(this.isLooping ? 32767 : 0);
                        }
                        
                        // Apply seek position with sanity checks
                        if (customSeek !== null && this.player && this.player.duration > 0) {
                            // Convert seconds to milliseconds and validate
                            const seekMs = customSeek * 1000;
                            if (!isNaN(seekMs) && seekMs >= 0 && seekMs <= this.player.duration) {
                                this.player.currentTime = seekMs;
                            }
                        }
                        
                        // Apply virtual keyboard channel
                        this.elements.channelSelect.value = customVkbdChannel.toString();
                        
                        if (this.autostart) {
                            this.play();
                        }
                    });
                });
            });
        } else {
            this.updateStatus('No file specified in URL. Please provide a "file" parameter.');
            // Still need to wait for context even if no file
            if (this.player._audioContext.state === 'suspended')
                this.waitForAudioContext();
        }
    }
    
    async waitForAudioContext() {
        const overlay = document.createElement('div');
        overlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.95);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 9999;
            cursor: pointer;
        `;

        const message = document.createElement('div');
        message.style.cssText = `
            text-align: center;
            padding: 40px;
            background: #2d2d30;
            border: 2px solid #007acc;
            border-radius: 8px;
            color: #cccccc;
        `;
        message.innerHTML = `
            <h2 style="font-size: 24px; margin-bottom: 16px; color: #9cdcfe;">
                Click to Start Audio
            </h2>
            <p style="font-size: 14px;">
                Click anywhere to enable audio playback
            </p>
        `;

        overlay.appendChild(message);
        document.body.appendChild(overlay);

        return new Promise((resolve) => {
            // Check if audio context is already available and not suspended
            const checkAudioContext = () => {
                if (this.player && this.player._audioContext) {
                    if (this.player._audioContext.state === 'running') {
                        overlay.remove();
                        resolve();
                        return true;
                    }
                }
                return false;
            };
            
            // Check immediately
            if (checkAudioContext()) return;
            
            // Also check periodically in case context becomes available
            const interval = setInterval(() => {
                if (checkAudioContext()) {
                    clearInterval(interval);
                }
            }, 100);
            
            const handleClick = async () => {
                clearInterval(interval);
                
                // Try to resume audio context if it exists but is suspended
                if (this.player && this.player._audioContext && this.player._audioContext.state === 'suspended') {
                    try {
                        await this.player._audioContext.resume();
                    } catch (e) {
                        console.warn('Failed to resume audio context:', e);
                    }
                }
                
                overlay.remove();
                resolve();
            };
            overlay.addEventListener('click', handleClick, { once: true });
        });
    }

    async load(fileName, bank) {
        const displayFileName = typeof fileName === 'string' ? fileName.split('/').pop() : fileName.name;
        const displayBank = typeof bank === 'string' ? bank.split('/').pop() : bank.name;
        
        this.elements.fileStatus.textContent = displayFileName;
        this.elements.bankStatus.textContent = 'Loading...';
        this.updateStatus('Loading bank...');

        try {
            await this.player.loadSoundbank(bank);
            this.elements.bankStatus.textContent = displayBank;
            this.updateStatus('Loading file...');
            
            await this.player.load(fileName);
            this.elements.playBtn.disabled = false;
            this.elements.exportBtn.disabled = false;
            
            // Apply current channel mute states
            for (let i = 0; i < 16; i++) {
                if (this.channelMutes[i]) {
                    this.player.muteChannel(i, true);
                }
            }
            
            // Apply current loop setting
            if (this.player._wasmModule) {
                this.player._wasmModule._BAE_WASM_SetLoops(this.isLooping ? 32767 : 0);
            }
            
            this.updateStatus('Ready');
        } catch (error) {
            this.updateStatus(`Failed to load: ${error.message}`);
            console.error('Load error:', error);
            throw error;
        }
    }            initChannels() {
        const grid = this.elements.channelsGrid;
        for (let i = 0; i < 16; i++) {
            const channel = document.createElement('div');
            channel.className = 'channel';
            
            const checkbox = document.createElement('input');
            checkbox.type = 'checkbox';
            checkbox.className = 'channel-checkbox';
            checkbox.checked = true;
            checkbox.dataset.channel = i;
            checkbox.addEventListener('change', (e) => {
                this.channelMutes[i] = !e.target.checked;
                if (this.player && this.player._songLoaded) {
                    this.player.muteChannel(i, this.channelMutes[i]);
                }
            });
            
            const label = document.createElement('div');
            label.className = 'channel-number';
            label.textContent = i + 1;
            
            // VU Meter
            const vuMeter = document.createElement('div');
            vuMeter.className = 'vu-meter';
            const vuFill = document.createElement('div');
            vuFill.className = 'vu-meter-fill';
            vuFill.id = `vu-${i}`;
            vuMeter.appendChild(vuFill);
            
            channel.appendChild(checkbox);
            channel.appendChild(label);
            channel.appendChild(vuMeter);
            grid.appendChild(channel);
        }
        
        // Start VU meter animation
        this.updateVUMeters();
    }

    initKeyboard() {
        const container = this.elements.keysContainer;
        const octaves = 7;
        const startOctave = 0;
        
        // Populate channel selector
        for (let i = 1; i < 16; i++) {
            const option = document.createElement('option');
            option.value = i;
            option.textContent = `Ch ${i + 1}`;
            this.elements.channelSelect.appendChild(option);
        }
        
        for (let octave = startOctave; octave < startOctave + octaves; octave++) {
            const notes = ['C', 'D', 'E', 'F', 'G', 'A', 'B'];
            
            for (let i = 0; i < notes.length; i++) {
                const note = notes[i];
                const midiNote = (octave + 1) * 12 + [0, 2, 4, 5, 7, 9, 11][i];
                
                const whiteKey = document.createElement('div');
                whiteKey.className = 'key white-key';
                whiteKey.dataset.note = midiNote;
                
                if (i === 0) {
                    const label = document.createElement('div');
                    label.className = 'octave-label';
                    label.textContent = `C${octave + 1}`;
                    whiteKey.appendChild(label);
                }
                
                container.appendChild(whiteKey);
                
                // Add black keys
                if ([0, 1, 3, 4, 5].includes(i)) {
                    const blackKey = document.createElement('div');
                    blackKey.className = 'key black-key';
                    const blackMidiNote = midiNote + 1;
                    blackKey.dataset.note = blackMidiNote;
                    
                    whiteKey.appendChild(blackKey);
                }
            }
        }
    }

    updateVUMeters() {
        if (this.player && this.player._wasmModule && this.player._songLoaded) {
            // Update VU meters
            for (let i = 0; i < 16; i++) {
                const activity = this.player._wasmModule._BAE_WASM_GetChannelActivity(i);
                const percent = (activity / 255) * 100;
                const vuFill = document.getElementById(`vu-${i}`);
                if (vuFill) {
                    vuFill.style.width = `${percent}%`;
                }
            }
            
            // Update keyboard highlighting from playback only when playing
            if (this.player._isPlaying) {
                this.updateKeyboardHighlighting();
            }
        }
        requestAnimationFrame(() => this.updateVUMeters());
    }

    updateKeyboardHighlighting() {
        if (!this.player || !this.player._wasmModule || !this.player._songLoaded) return;
        
        const selectedChannel = parseInt(this.elements.channelSelect.value);
        const showAllChannels = this.elements.allChannelsCheckbox.checked;
        
        // Create a buffer for note data (128 bytes for 128 MIDI notes)
        const Module = this.player._wasmModule;
        const notesBuffer = Module._malloc(128);
        if (!notesBuffer) return;
        
        try {
            // Collect active notes from selected channel(s)
            const activeNotes = new Set();
            
            if (showAllChannels) {
                // Show notes from all channels
                for (let ch = 0; ch < 16; ch++) {
                    const result = Module._BAE_WASM_GetActiveNotesForChannel(ch, notesBuffer);
                    if (result === 0) {
                        const notes = new Uint8Array(Module.HEAPU8.buffer, notesBuffer, 128);
                        for (let n = 0; n < 128; n++) {
                            if (notes[n] > 0) activeNotes.add(n);
                        }
                    }
                }
            } else {
                // Show notes from selected channel only
                const result = Module._BAE_WASM_GetActiveNotesForChannel(selectedChannel, notesBuffer);
                if (result === 0) {
                    const notes = new Uint8Array(Module.HEAPU8.buffer, notesBuffer, 128);
                    for (let n = 0; n < 128; n++) {
                        if (notes[n] > 0) activeNotes.add(n);
                    }
                }
            }
            
            // Update visual state of all keys
            const allKeys = document.querySelectorAll('.key');
            allKeys.forEach(key => {
                const note = parseInt(key.dataset.note);
                const isActive = activeNotes.has(note);
                
                // Show only playback highlighting (read-only)
                if (isActive) {
                    key.classList.add('from-playback');
                } else {
                    key.classList.remove('from-playback');
                }
            });
        } finally {
            // Always free the buffer
            Module._free(notesBuffer);
        }
    }
    

    initElements() {
        this.elements = {
            // Transport
            playBtn: document.getElementById('playBtn'),
            stopBtn: document.getElementById('stopBtn'),
            exportBtn: document.getElementById('exportBtn'),
            loopCheckbox: document.getElementById('loopCheckbox'),
            
            // Export Progress
            exportProgress: document.getElementById('exportProgress'),
            exportStatus: document.getElementById('exportStatus'),
            exportProgressFill: document.getElementById('exportProgressFill'),
            
            // File Loading
            mediaFileInput: document.getElementById('mediaFileInput'),
            loadMediaBtn: document.getElementById('loadMediaBtn'),
            mediaLoadStatus: document.getElementById('mediaLoadStatus'),
            bankFileInput: document.getElementById('bankFileInput'),
            loadBankBtn: document.getElementById('loadBankBtn'),
            bankLoadStatus: document.getElementById('bankLoadStatus'),
            
            // Progress
            progressBar: document.getElementById('progressBar'),
            progressFill: document.getElementById('progressFill'),
            currentTime: document.getElementById('currentTime'),
            duration: document.getElementById('duration'),
            
            // Controls
            transposeSlider: document.getElementById('transposeSlider'),
            transposeValue: document.getElementById('transposeValue'),
            transposeReset: document.getElementById('transposeReset'),
            tempoSlider: document.getElementById('tempoSlider'),
            tempoValue: document.getElementById('tempoValue'),
            tempoReset: document.getElementById('tempoReset'),
            reverbSelect: document.getElementById('reverbSelect'),
            volumeSlider: document.getElementById('volumeSlider'),
            volumeValue: document.getElementById('volumeValue'),
            
            // Channels
            channelsGrid: document.getElementById('channelsGrid'),
            invertBtn: document.getElementById('invertBtn'),
            muteAllBtn: document.getElementById('muteAllBtn'),
            unmuteAllBtn: document.getElementById('unmuteAllBtn'),
            
            // Keyboard
            keysContainer: document.getElementById('keysContainer'),
            channelSelect: document.getElementById('channelSelect'),
            velocityDisplay: document.getElementById('velocityDisplay'),
            allChannelsCheckbox: document.getElementById('allChannelsCheckbox'),
            
            // Karaoke
            karaokePanel: document.getElementById('karaokePanel'),
            karaokePrevious: document.getElementById('karaokePrevious'),
            karaokeCurrent: document.getElementById('karaokeCurrent'),
            
            // Status
            fileStatus: document.getElementById('fileStatus'),
            bankStatus: document.getElementById('bankStatus'),
            statusIndicator: document.getElementById('statusIndicator'),
            statusText: document.getElementById('statusText')
        };
    }

    bindEvents() {
        // Transport controls
        this.elements.playBtn.addEventListener('click', () => this.togglePlayPause());
        this.elements.stopBtn.addEventListener('click', () => this.stop());
        
        // Export WAV
        this.elements.exportBtn.addEventListener('click', async () => {
            if (!this.player || !this.player._songLoaded) return;
            
            this.elements.exportBtn.disabled = true;
            this.elements.exportProgress.style.display = 'block';
            this.elements.exportStatus.textContent = 'Rendering audio...';
            this.elements.exportProgressFill.style.width = '0%';

            try {
                const currentFile = this.elements.fileStatus.textContent;
                const baseName = currentFile !== '-' ? currentFile.replace(/\.(mid|midi|rmf|kar|smf|xmf|mxmf)$/i, '') : 'exported';
                var duration = null;
                if (baseName == "DialingWebTV") {
                    duration = 240000; // milliseconds
                }

                const audioData = await this.player.exportToRawAudio((progress) => {
                    this.elements.exportProgressFill.style.width = (progress * 70) + '%';
                    this.elements.exportStatus.textContent = `Rendering... ${Math.round(progress * 70)}%`;
                }, duration);
                
                this.elements.exportStatus.textContent = 'Creating WAV file...';
                this.elements.exportProgressFill.style.width = '90%';
                await new Promise(r => setTimeout(r, 10));
                
                const wavData = this.player.createWavFile(audioData.leftChannel, audioData.rightChannel, audioData.sampleRate);
                
                this.elements.exportProgressFill.style.width = '100%';
                this.elements.exportStatus.textContent = 'Creating download...';
                
                const blob = new Blob([wavData], { type: 'audio/wav' });
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                
                a.download = `${baseName}.wav`;
                
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                URL.revokeObjectURL(url);
                
                this.elements.exportStatus.textContent = 'Export complete!';
                setTimeout(() => {
                    this.elements.exportProgress.style.display = 'none';
                }, 2000);
            } catch (error) {
                console.error('Export error:', error);
                this.elements.exportStatus.textContent = 'Error: ' + error.message;
            } finally {
                this.elements.exportBtn.disabled = false;
            }
        });
        
        this.elements.loopCheckbox.addEventListener('change', (e) => {
            this.isLooping = e.target.checked;
            // Set BAESong loop count like the GUI does
            if (this.player && this.player._wasmModule && this.player._songLoaded) {
                this.player._wasmModule._BAE_WASM_SetLoops(this.isLooping ? 32767 : 0);
            }
        });

        // Progress bar
        this.elements.progressBar.addEventListener('click', (e) => {
            if (this.player && this.player.duration > 0) {
                const rect = this.elements.progressBar.getBoundingClientRect();
                const percent = (e.clientX - rect.left) / rect.width;
                const time = percent * this.player.duration;
                this.player.currentTime = time;
            }
        });

        // Current time click to seek to start
        this.elements.currentTime.addEventListener('click', () => {
            if (this.player) {
                this.player.currentTime = 0;
            }
        });

        // Transpose
        this.elements.transposeSlider.addEventListener('input', (e) => {
            this.transpose = parseInt(e.target.value);
            this.elements.transposeValue.textContent = this.transpose >= 0 ? `+${this.transpose}` : this.transpose;
            if (this.player) {
                this.player.transpose = this.transpose;
            }
        });
        this.elements.transposeReset.addEventListener('click', () => {
            this.elements.transposeSlider.value = 0;
            this.elements.transposeSlider.dispatchEvent(new Event('input'));
        });

        // Tempo
        this.elements.tempoSlider.addEventListener('input', (e) => {
            this.tempo = parseInt(e.target.value);
            this.elements.tempoValue.textContent = `${this.tempo}%`;
            if (this.player) {
                this.player.tempo = this.tempo / 100;
            }
        });
        this.elements.tempoReset.addEventListener('click', () => {
            this.elements.tempoSlider.value = 100;
            this.elements.tempoSlider.dispatchEvent(new Event('input'));
        });

        // Reverb
        this.elements.reverbSelect.addEventListener('change', (e) => {
            const reverbType = parseInt(e.target.value);
            if (this.player) {
                this.player.reverbType = reverbType;
            }
        });

        // Volume
        this.elements.volumeSlider.addEventListener('input', (e) => {
            const volume = parseInt(e.target.value);
            this.elements.volumeValue.textContent = `${volume}%`;
            if (this.player) {
                this.player.volume = volume / 100;
            }
        });

        // Channel controls
        this.elements.invertBtn.addEventListener('click', () => {
            const checkboxes = this.elements.channelsGrid.querySelectorAll('input[type="checkbox"]');
            checkboxes.forEach((cb, i) => {
                cb.checked = !cb.checked;
                this.channelMutes[i] = !cb.checked;
                if (this.player && this.player._songLoaded) {
                    this.player.muteChannel(i, this.channelMutes[i]);
                }
            });
        });

        this.elements.muteAllBtn.addEventListener('click', () => {
            const checkboxes = this.elements.channelsGrid.querySelectorAll('input[type="checkbox"]');
            checkboxes.forEach((cb, i) => {
                cb.checked = false;
                this.channelMutes[i] = true;
                if (this.player && this.player._songLoaded) {
                    this.player.muteChannel(i, true);
                }
            });
        });

        this.elements.unmuteAllBtn.addEventListener('click', () => {
            const checkboxes = this.elements.channelsGrid.querySelectorAll('input[type="checkbox"]');
            checkboxes.forEach((cb, i) => {
                cb.checked = true;
                this.channelMutes[i] = false;
                if (this.player && this.player._songLoaded) {
                    this.player.muteChannel(i, false);
                }
            });
        });
    }

    setupFileLoading() {
        // Media file loading
        this.elements.loadMediaBtn.addEventListener('click', () => {
            this.elements.mediaFileInput.click();
        });

        this.elements.mediaFileInput.addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (!file) return;

            this.userMediaFile = file;
            this.elements.mediaLoadStatus.textContent = file.name;

            // If we have the audio system initialized, load immediately
            if (this.player && this.player._wasmModule) {
                await this.loadUserFile();
            }
        });

        // Soundbank file loading
        this.elements.loadBankBtn.addEventListener('click', () => {
            this.elements.bankFileInput.click();
        });

        this.elements.bankFileInput.addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (!file) return;

            this.userBankFile = file;
            this.elements.bankLoadStatus.textContent = file.name;

            // If we have the audio system initialized, load immediately
            if (this.player && this.player._wasmModule) {
                await this.loadUserBank();
            }
        });
    }

    async loadUserFile() {
        if (!this.userMediaFile) return;

        try {
            // Track if we were playing so we can resume after loading
            const wasPlaying = this.player && this.player._isPlaying;
            
            // Stop current playback
            if (wasPlaying) {
                this.stop();
            }

            this.updateStatus('Loading user file...');
            
            // Convert File to ArrayBuffer
            const fileData = await this.userMediaFile.arrayBuffer();
            
            // Use current bank (user bank if loaded, otherwise default)
            const bank = this.userBankFile ? await this.userBankFile.arrayBuffer() : this.bank;
            
            // Load with the file data
            await this.player.loadSoundbank(bank);
            this.elements.bankStatus.textContent = this.userBankFile ? this.userBankFile.name : this.bank.split('/').pop();
            
            await this.player.load(fileData);
            this.elements.fileStatus.textContent = this.userMediaFile.name;
            this.elements.playBtn.disabled = false;
            this.elements.exportBtn.disabled = false;
            
            // Apply current channel mute states
            for (let i = 0; i < 16; i++) {
                if (this.channelMutes[i]) {
                    this.player.muteChannel(i, true);
                }
            }
            
            // Apply current loop setting
            if (this.player._wasmModule) {
                this.player._wasmModule._BAE_WASM_SetLoops(this.isLooping ? 32767 : 0);
            }
            
            this.updateStatus('User file loaded successfully');
            
            // Resume playback if we were playing before
            if (wasPlaying) {
                this.play();
            }
        } catch (error) {
            this.updateStatus(`Failed to load user file: ${error.message}`);
            console.error('User file load error:', error);
        }
    }

    async loadUserBank() {
        if (!this.userBankFile) return;

        try {
            // Track if we were playing so we can resume after loading
            const wasPlaying = this.player && this.player._isPlaying;
            
            // Stop current playback
            if (wasPlaying) {
                this.stop();
            }

            this.updateStatus('Loading user soundbank...');
            
            // Convert File to ArrayBuffer
            const bankData = await this.userBankFile.arrayBuffer();
            
            // Reload current file with new bank
            let mediaData;
            if (this.userMediaFile) {
                mediaData = await this.userMediaFile.arrayBuffer();
            } else if (this.fileName) {
                const response = await fetch(this.fileName);
                mediaData = await response.arrayBuffer();
            } else {
                throw new Error('No media file to reload');
            }
            
            // Load with the new bank
            await this.player.loadSoundbank(bankData);
            this.elements.bankStatus.textContent = this.userBankFile.name;
            
            await this.player.load(mediaData);
            this.elements.playBtn.disabled = false;
            this.elements.exportBtn.disabled = false;
            
            // Apply current channel mute states
            for (let i = 0; i < 16; i++) {
                if (this.channelMutes[i]) {
                    this.player.muteChannel(i, true);
                }
            }
            
            // Apply current loop setting
            if (this.player._wasmModule) {
                this.player._wasmModule._BAE_WASM_SetLoops(this.isLooping ? 32767 : 0);
            }
            
            this.updateStatus('User soundbank loaded successfully');
            
            // Resume playback if we were playing before
            if (wasPlaying) {
                this.play();
            }
        } catch (error) {
            this.updateStatus(`Failed to load user soundbank: ${error.message}`);
            console.error('User soundbank load error:', error);
        }
    }

    async initPlayer() {
        try {
            this.updateStatus('Initializing audio engine...');
            
            this.player = new BeatnikPlayer();

            this.player.addEventListener('ready', () => {
                this.updateStatus('Ready');
            });

            this.player.addEventListener('play', () => {
                this.updateButtonStates(true);
                this.updateStatus('Playing');
            });

            this.player.addEventListener('pause', () => {
                this.updateButtonStates(false);
                this.updateStatus('Paused');
            });

            this.player.addEventListener('stop', () => {
                this.updateButtonStates(false);
                this.updateStatus('Stopped');
            });

            this.player.addEventListener('end', () => {
                // BAESong_SetLoops handles looping internally, so just update status when truly finished
                if (!this.isLooping) {
                    this.updateButtonStates(false);
                    this.updateStatus('Finished');
                }
            });

            this.player.addEventListener('error', (error) => {
                this.updateStatus(`Error: ${error.message}`);
                console.error('Player error:', error);
            });

            this.player.addEventListener('timeupdate', (time) => {
                this.updateTimeDisplay(time);
            });

            this.player.addEventListener('lyric', (lyricData) => {
                this.updateKaraokeDisplay(lyricData);
            });

            await this.player._init({
                sampleRate: 44100,
                maxVoices: 64
            });

            this.player.volume = this.elements.volumeSlider.value / 100;

        } catch (error) {
            this.updateStatus(`Failed to initialize: ${error.message}`);
            console.error('Init error:', error);
        }
    }

    togglePlayPause() {
        if (!this.player || !this.player._songLoaded) return;
        
        if (this.player._isPlaying) {
            this.pause();
        } else {
            // Use resume if we're paused, otherwise play from start
            if (this.isPaused) {
                this.resume();
            } else {
                this.play();
            }
        }
    }

    play() {
        if (this.player) {
            try {
                this.player.play();
                this.isPaused = false;
            } catch (error) {
                this.updateStatus(`Play error: ${error.message}`);
            }
        }
    }

    pause() {
        if (this.player) {
            try {
                this.player.pause();
                this.isPaused = true;
                this.clearActiveKeyboardNotes();
            } catch (error) {
                this.updateStatus(`Pause error: ${error.message}`);
            }
        }
    }

    resume() {
        if (this.player) {
            try {
                this.player.resume();
                this.isPaused = false;
            } catch (error) {
                this.updateStatus(`Resume error: ${error.message}`);
            }
        }
    }

    stop() {
        if (this.player) {
            this.player.stop();
            this.isPaused = false;
            this.clearActiveKeyboardNotes();
            this.updateTimeDisplay(0);
        }
    }

    clearActiveKeyboardNotes() {
        // Clear all active keyboard highlights
        const allKeys = document.querySelectorAll('.key');
        allKeys.forEach(key => {
            key.classList.remove('active', 'from-playback');
        });
    }

    updateButtonStates(playing) {
        this.elements.playBtn.textContent = playing ? 'Pause' : 'Play';
        this.elements.playBtn.disabled = false;
        this.elements.stopBtn.disabled = !playing;
        this.elements.exportBtn.disabled = !this.player || !this.player._songLoaded;
        
        this.elements.statusIndicator.className = playing ? 'status-indicator playing' : 'status-indicator stopped';
    }

    updateTimeDisplay(currentTime) {
        const duration = this.player ? this.player.duration : 0;
        
        this.elements.currentTime.textContent = this.formatTime(currentTime);
        this.elements.duration.innerHTML = this.formatTime(duration);
        
        if (duration > 0) {
            const percent = (currentTime / duration) * 100;
            this.elements.progressFill.style.width = `${percent}%`;
        }
    }

    formatTime(ms, includeMs = true) {
        const totalSeconds = Math.floor(ms / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        const milliseconds = Math.floor((ms % 1000));
        
        if (includeMs) {
            return `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}.${milliseconds.toString().padStart(3, '0')}`;
        } else {
            return `${minutes}:${seconds.toString().padStart(2, '0')}`;
        }
    }

    updateStatus(message) {
        this.elements.statusText.textContent = message;
    }

    updateKaraokeDisplay(lyricData) {
        // Show panel when lyrics are detected
        if (lyricData && (lyricData.current || lyricData.previous)) {
            this.elements.karaokePanel.style.display = 'flex';
            
            // Update previous line (dimmed)
            this.elements.karaokePrevious.textContent = lyricData.previous || '';
            
            // Update current line with highlighting logic
            const current = lyricData.current || '';
            const fragment = lyricData.fragment || '';
            
            if (fragment && current.endsWith(fragment) && current.length > fragment.length) {
                // Split into prefix and highlighted fragment
                const prefixLen = current.length - fragment.length;
                const prefix = current.substring(0, prefixLen);
                
                // Clear and rebuild with styled elements
                this.elements.karaokeCurrent.innerHTML = '';
                
                const prefixSpan = document.createElement('span');
                prefixSpan.textContent = prefix;
                prefixSpan.style.color = '#cccccc';
                
                const fragmentSpan = document.createElement('span');
                fragmentSpan.textContent = fragment;
                fragmentSpan.style.color = '#4ec9b0';
                fragmentSpan.style.fontWeight = '600';
                
                this.elements.karaokeCurrent.appendChild(prefixSpan);
                this.elements.karaokeCurrent.appendChild(fragmentSpan);
            } else {
                // No fragment highlighting, just show current line
                this.elements.karaokeCurrent.textContent = current;
            }
        }
    }

    showHelpModal() {
        // Create modal overlay
        const overlay = document.createElement('div');
        overlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.8);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 10000;
            padding: 20px;
        `;

        // Create modal content
        const modal = document.createElement('div');
        modal.style.cssText = `
            background: #2d2d30;
            border: 2px solid #007acc;
            border-radius: 8px;
            max-width: 700px;
            max-height: 80vh;
            overflow-y: auto;
            color: #cccccc;
        `;

        modal.innerHTML = `
            <div style="padding: 20px;">
                <h2 style="font-size: 20px; margin-bottom: 16px; color: #9cdcfe; border-bottom: 1px solid #555; padding-bottom: 8px;">
                    URL Parameters
                </h2>
                <div style="font-size: 13px; line-height: 1.6;">
                    <p style="margin-bottom: 16px;">You can control the player using URL parameters:</p>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">file</strong>
                        <div style="margin-left: 16px; color: #aaa;">Path to the MIDI/RMF file to load</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?file=${this.default_media_file}</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">bank</strong>
                        <div style="margin-left: 16px; color: #aaa;">Path to custom sound bank (.hsb, .sf2, .sf3, .sfo, .dls)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?bank=${this.default_bank_rmf}</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">reverb</strong>
                        <div style="margin-left: 16px; color: #aaa;">Reverb type (0-11)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?reverb=${this.default_reverb}</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">transpose</strong>
                        <div style="margin-left: 16px; color: #aaa;">Transpose in semitones (-12 to 12)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?transpose=2</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">tempo</strong>
                        <div style="margin-left: 16px; color: #aaa;">Tempo percentage (50-200)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?tempo=120</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">volume</strong>
                        <div style="margin-left: 16px; color: #aaa;">Volume percentage (0-100)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?volume=80</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">muted</strong>
                        <div style="margin-left: 16px; color: #aaa;">Comma-separated list of muted channels (1-16)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?muted=1,10,16</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">loop</strong>
                        <div style="margin-left: 16px; color: #aaa;">Enable BAE Loop (0 = disabled, any other value = enabled)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?loop=0</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">seek</strong>
                        <div style="margin-left: 16px; color: #aaa;">Start position in seconds (validated against song duration)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?seek=30.5</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">vkbd</strong>
                        <div style="margin-left: 16px; color: #aaa;">Virtual keyboard channel (1-16, default: 1)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?vkbd=10</div>
                    </div>
                    
                    <div style="margin-bottom: 12px;">
                        <strong style="color: #dcdcaa;">autostart</strong>
                        <div style="margin-left: 16px; color: #aaa;">Automatically start playback (1 = enabled)</div>
                        <div style="margin-left: 16px; font-family: monospace; font-size: 11px; color: #ce9178;">?autostart=1</div>
                    </div>
                    
                    <div style="margin-top: 20px; padding: 12px; background: #1e1e1e; border-radius: 4px; border-left: 3px solid #007acc;">
                        <strong style="color: #dcdcaa;">Example:</strong>
                        <div style="font-family: monospace; font-size: 11px; margin-top: 8px; color: #ce9178; word-break: break-all;">
                            ?file=${this.default_media_file}&bank=${this.default_bank_rmf}&reverb=${this.default_reverb}&transpose=2&tempo=120&volume=80&muted=10,16&loop=0&seek=30&vkbd=10&autostart=1
                        </div>
                    </div>
                </div>
                <div style="margin-top: 20px; text-align: right;">
                    <button id="closeHelpModal" style="
                        padding: 6px 16px;
                        border: 1px solid #555;
                        border-radius: 3px;
                        background: #3c3c3c;
                        color: #cccccc;
                        font-size: 12px;
                        cursor: pointer;
                    ">Close</button>
                </div>
            </div>
        `;

        overlay.appendChild(modal);
        document.body.appendChild(overlay);

        // Close handlers
        const closeModal = () => overlay.remove();
        modal.querySelector('#closeHelpModal').addEventListener('click', closeModal);
        overlay.addEventListener('click', (e) => {
            if (e.target === overlay) closeModal();
        });

        // Escape key to close
        const handleEscape = (e) => {
            if (e.key === 'Escape') {
                closeModal();
                document.removeEventListener('keydown', handleEscape);
            }
        };
        document.addEventListener('keydown', handleEscape);
    }

    showFeaturesModal(version, features) {
        // Create modal overlay
        const overlay = document.createElement('div');
        overlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.8);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 10000;
            padding: 20px;
        `;

        // Create modal content
        const modal = document.createElement('div');
        modal.style.cssText = `
            background: #2d2d30;
            border: 2px solid #007acc;
            border-radius: 8px;
            max-width: 600px;
            max-height: 80vh;
            overflow-y: auto;
            color: #cccccc;
        `;

        // Parse features into readable format
        const featureLines = features.split(',').map(f => f.trim());
        const featureList = featureLines.map(f => `<li style="margin: 4px 0;">${f}</li>`).join('');

        modal.innerHTML = `
            <div style="padding: 20px;">
                <h2 style="font-size: 20px; margin-bottom: 16px; color: #9cdcfe; border-bottom: 1px solid #555; padding-bottom: 8px;">
                    miniBAE Build Information
                </h2>
                <div style="margin-bottom: 16px;">
                    <strong style="color: #dcdcaa;">Version:</strong>
                    <span style="margin-left: 8px;">${version}</span>
                </div>
                <div>
                    <strong style="color: #dcdcaa;">Compiled Features:</strong>
                    <ul style="margin: 8px 0 0 20px; padding: 0; list-style-type: disc;">
                        ${featureList}
                    </ul>
                </div>
                <div style="margin-top: 20px; text-align: right;">
                    <button id="closeModal" style="
                        padding: 6px 16px;
                        border: 1px solid #555;
                        border-radius: 3px;
                        background: #3c3c3c;
                        color: #cccccc;
                        font-size: 12px;
                        cursor: pointer;
                    ">Close</button>
                </div>
            </div>
        `;

        overlay.appendChild(modal);
        document.body.appendChild(overlay);

        // Close handlers
        const closeModal = () => overlay.remove();
        modal.querySelector('#closeModal').addEventListener('click', closeModal);
        overlay.addEventListener('click', (e) => {
            if (e.target === overlay) closeModal();
        });

        // Escape key to close
        const handleEscape = (e) => {
            if (e.key === 'Escape') {
                closeModal();
                document.removeEventListener('keydown', handleEscape);
            }
        };
        document.addEventListener('keydown', handleEscape);
    }
}

// Initialize the player when the page loads
document.addEventListener('DOMContentLoaded', () => {
    new MiniBAEPlayer();
});