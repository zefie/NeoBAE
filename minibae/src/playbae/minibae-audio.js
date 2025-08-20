import PlayBAE from './playbae.js';

const TYPE_TO_CMD = {
    'wav': '-w',
    'aif': '-a',
    'rmf': '-r',
    'mid': '-m'
};

// Function to initialize and resume the AudioContext
function initAndResumeAudioContext() {
    const audioContext = new AudioContext();

    // Return a promise that resolves when the AudioContext is running (resumed).
    return new Promise((resolve, reject) => {
        // If already running, resolve immediately
        if (audioContext.state === 'running') {
            resolve(audioContext);
            return;
        }

        const cleanup = () => {
            document.removeEventListener('click', onInteraction);
            document.removeEventListener('keydown', onInteraction);
            document.removeEventListener('touchstart', onInteraction);
            audioContext.onstatechange = null;
        };

        const onInteraction = () => {
            if (audioContext.state === 'suspended') {
                audioContext.resume().then(() => {
                    console.log('AudioContext resumed');
                    cleanup();
                    resolve(audioContext);
                }).catch((err) => {
                    console.error('Failed to resume AudioContext:', err);
                    cleanup();
                    reject(err);
                });
            }
        };

        // Attach a single-use listener to user interaction events to attempt resume
        document.addEventListener('click', onInteraction, { once: true });
        document.addEventListener('keydown', onInteraction, { once: true });
        document.addEventListener('touchstart', onInteraction, { once: true });

        // Also watch for state changes (some browsers may change state without an explicit resume call)
        audioContext.onstatechange = () => {
            if (audioContext.state === 'running') {
                cleanup();
                resolve(audioContext);
            }
        };
    });
}


class AudioBufferManager {
    constructor(audioContext, sampleRate, bufferThreshold = 8192) {
        this.audioContext = audioContext;
        this.sampleRate = sampleRate;
        this.bufferThreshold = bufferThreshold;
        this.leftChannelBuffer = [];
        this.rightChannelBuffer = [];
        this.scheduledTime = this.audioContext.currentTime;
    }

    addAudioData(int16Data) {
        // Deinterleave and convert Int16 to Float32 for each chunk
        for (let i = 0; i < int16Data.length; i += 2) {
            this.leftChannelBuffer.push(int16Data[i] / 32768);
            this.rightChannelBuffer.push(int16Data[i + 1] / 32768);
        }

        // If we have enough samples, play the audio
        if (this.leftChannelBuffer.length >= this.bufferThreshold) {
            this.playBufferedAudio();
        }
    }

    playBufferedAudio() {
        const length = this.bufferThreshold;
        const audioBuffer = this.audioContext.createBuffer(2, length, this.sampleRate);

        // Prepare Float32Arrays for the AudioBuffer
        const leftChannel = new Float32Array(this.leftChannelBuffer.slice(0, length));
        const rightChannel = new Float32Array(this.rightChannelBuffer.slice(0, length));

        // Remove the used samples from the buffer
        this.leftChannelBuffer = this.leftChannelBuffer.slice(length);
        this.rightChannelBuffer = this.rightChannelBuffer.slice(length);

        // Copy the data to the AudioBuffer channels
        audioBuffer.copyToChannel(leftChannel, 0);
        audioBuffer.copyToChannel(rightChannel, 1);

        // Create a new source node for each playback
        const sourceNode = this.audioContext.createBufferSource();
        sourceNode.buffer = audioBuffer;
        sourceNode.connect(this.audioContext.destination);

        // Schedule playback slightly in the future to prevent gaps
        if (this.scheduledTime < this.audioContext.currentTime) {
            this.scheduledTime = this.audioContext.currentTime + 0.05; // Add a small buffer time
        }
        sourceNode.start(this.scheduledTime);

        // Update the scheduled time for the next buffer
        this.scheduledTime += audioBuffer.duration;
    }
}

class MiniBAEAudio extends HTMLAudioElement {
    constructor() {
        super();
        window.miniBAEInstance = this;
        // Defer async initialization to avoid making the constructor async
        this.audioContext = null;
        this.sampleRate = 44100; // default until we get the real context sampleRate
        this.bufferManager = null;
        this.postAudioData = null;

        // kick off async setup that will await AudioContext and then init BAE
        this._setup();
    }

    async _setup() {
        try {
                this.audioContext = await initAndResumeAudioContext();
                // Keep `this.sampleRate` as the source/sample rate produced by PlayBAE (default 44100).
                // Let the browser resample the buffer to the AudioContext's playback rate so pitch stays correct.
                this.bufferManager = new AudioBufferManager(this.audioContext, this.sampleRate);
            await this.initBAE();
        } catch (err) {
            console.error('Failed to initialize audio context or BAE:', err);
        }
    }

    async playAudioData(leftChannel, rightChannel) {
        
        // Ensure audioContext is available
        if (!this.audioContext) {
            console.warn('playAudioData called before audioContext was ready');
            return;
        }

        // Create a new AudioBuffer with 2 channels (stereo), matching sample rate and data length
        const audioBuffer = this.audioContext.createBuffer(2, leftChannel.length, this.audioContext.sampleRate);

        // Copy the left and right channels into the AudioBuffer
        audioBuffer.copyToChannel(leftChannel, 0); // Left channel
        audioBuffer.copyToChannel(rightChannel, 1); // Right channel

        // Create a new AudioBufferSourceNode
        const sourceNode = this.audioContext.createBufferSource();
        sourceNode.buffer = audioBuffer;

        // Connect the source node to the audio context's destination
        sourceNode.connect(this.audioContext.destination);

        // Start playback
        sourceNode.start();
        console.log('AudioContext state:', this.audioContext.state);
        console.log('AudioBuffer channels:', audioBuffer.numberOfChannels);
        console.log('AudioBuffer sampleRate:', audioBuffer.sampleRate);
    }

    async initBAE() {
        let playbaeOptions = {};
        playbaeOptions.arguments = ['-v', '255', '-t', '1200'];

        playbaeOptions.preRun = [(Module) => {
            this.postAudioData  = (pointer, length) => {
                const int16Data = new Int16Array(Module.HEAP16.buffer, pointer, length * 2); // Int16 data is half the byte length
                this.bufferManager.addAudioData(int16Data);
            };
            
            Module.FS.createPreloadedFile("/home/web_user/", "audio", this.currentSrc, true, true);
            if(this.dataset.minibaePatches) {
                playbaeOptions.arguments.push("-p", "/home/web_user/patches_custom.hsb");
                Module.FS.createPreloadedFile("/home/web_user/", "patches_custom.hsb", this.dataset.minibaePatches, true, true);
            } else {
                playbaeOptions.arguments.push("-p", "/home/web_user/patches.hsb");
            }
        }];

        if(this.dataset.minibaeType) {
            playbaeOptions.arguments.push(TYPE_TO_CMD[this.dataset.minibaeType]);
        } else {
            playbaeOptions.arguments.push('-f');
        }
        playbaeOptions.arguments.push("/home/web_user/audio");
        playbaeOptions.arguments.push("-o", "null");
        
        let playbae = await PlayBAE(playbaeOptions);
    }
}

customElements.define('minibae-audio', MiniBAEAudio, {extends: 'audio'});