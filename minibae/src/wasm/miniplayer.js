/**
 * miniBAE Mini Player - Self-contained popup player
 * 
 * Usage:
 *   playSong(songUrl, bankUrl) - Opens a popup player and plays the song
 * 
 * Requires:
 *   - player-class.js (BeatnikPlayer)
 *   - engine.js (BeatnikModule)
 */

(function() {
    'use strict';

    // Global player instance (singleton)
    let globalPlayer = null;
    let currentModal = null;
    let currentSoundbank = null;
    let default_bank_mid = "/content/PatchBanks/SF3/GeneralUser-GS.sf3";
    let default_bank_rmf = "/content/PatchBanks/HSB/patchesp.hsb";

    // Inject CSS styles
    const styles = `
        .miniplayer-overlay {
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            z-index: 10000;
            pointer-events: none;
        }

        @keyframes fadeIn {
            from { opacity: 0; }
            to { opacity: 1; }
        }

        @keyframes slideIn {
            from { 
                transform: translateY(-20px);
                opacity: 0;
            }
            to { 
                transform: translateY(0);
                opacity: 1;
            }
        }

        .miniplayer-modal {
            background: #3c3c3c;
            border: 1px solid #555;
            border-radius: 8px;
            width: 420px;
            max-width: 90vw;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.8);
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
            color: #d4d4d4;
            animation: slideIn 0.3s ease-out;
            pointer-events: auto;
            position: relative;
        }
        
        @media (max-width: 640px) {
            .miniplayer-overlay {
                top: 0;
                left: 0;
                transform: none;
                width: 100%;
                height: 100%;
                display: flex;
                align-items: center;
                justify-content: center;
                padding: 10px;
            }
            
            .miniplayer-modal {
                width: 100%;
                max-width: 100%;
                margin: 0 auto;
            }
            
            .miniplayer-body {
                padding: 15px !important;
            }
            
            .miniplayer-controls {
                flex-wrap: wrap;
            }
            
            .miniplayer-btn {
                flex: 1;
                min-width: calc(50% - 5px);
            }
            
            .miniplayer-volume {
                flex-direction: column;
                align-items: stretch;
                gap: 8px;
            }
            
            .miniplayer-volume-label {
                min-width: auto;
                text-align: center;
            }
            
            .miniplayer-volume-value {
                text-align: center;
            }
            
            .miniplayer-statusbar {
                flex-direction: column;
                gap: 6px;
            }
            
            .miniplayer-statusbar-item {
                justify-content: center;
            }
        }

        .miniplayer-header {
            background: #2d2d30;
            padding: 10px 12px;
            border-bottom: 1px solid #555;
            border-radius: 8px 8px 0 0;
            display: flex;
            align-items: center;
            justify-content: space-between;
            cursor: move;
            user-select: none;
        }

        .miniplayer-title {
            font-size: 13px;
            font-weight: 600;
            color: #9cdcfe;
            flex: 1;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .miniplayer-close {
            background: transparent;
            border: none;
            color: #cccccc;
            font-size: 18px;
            width: 24px;
            height: 24px;
            border-radius: 4px;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: background 0.2s;
        }

        .miniplayer-close:hover {
            background: #e81123;
            color: #fff;
        }

        .miniplayer-body {
            padding: 20px;
        }

        .miniplayer-progress {
            margin-bottom: 15px;
        }

        .miniplayer-times {
            display: flex;
            justify-content: space-between;
            font-size: 11px;
            color: #9cdcfe;
            margin-bottom: 6px;
            font-family: 'Courier New', monospace;
        }

        .miniplayer-seekbar {
            width: 100%;
            height: 6px;
            background: #1e1e1e;
            border-radius: 3px;
            position: relative;
            cursor: pointer;
            border: 1px solid #555;
        }

        .miniplayer-seekbar-fill {
            height: 100%;
            background: linear-gradient(to right, #0e639c, #1177bb);
            border-radius: 3px;
            transition: width 0.1s linear;
            min-width: 2px;
        }

        .miniplayer-seekbar:hover .miniplayer-seekbar-fill {
            background: linear-gradient(to right, #1177bb, #1e8ad6);
        }

        .miniplayer-controls {
            display: flex;
            gap: 10px;
            margin-bottom: 15px;
            justify-content: center;
        }

        .miniplayer-btn {
            background: #007acc;
            border: 1px solid #0e639c;
            color: #fff;
            padding: 8px 16px;
            border-radius: 4px;
            cursor: pointer;
            font-size: 12px;
            font-weight: 500;
            transition: all 0.2s;
            min-width: 70px;
        }

        .miniplayer-btn:hover {
            background: #1e8ad6;
            border-color: #1177bb;
        }

        .miniplayer-btn:active {
            transform: translateY(1px);
        }

        .miniplayer-btn:disabled {
            background: #3c3c3c;
            border-color: #555;
            color: #666;
            cursor: not-allowed;
        }

        .miniplayer-btn-stop {
            background: #c72e0f;
            border-color: #a52a0f;
        }

        .miniplayer-btn-stop:hover:not(:disabled) {
            background: #e03e1e;
            border-color: #c72e0f;
        }

        .miniplayer-volume {
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .miniplayer-volume-label {
            font-size: 12px;
            color: #cccccc;
            min-width: 60px;
        }

        .miniplayer-slider {
            flex: 1;
            height: 6px;
            border-radius: 3px;
            background: #1e1e1e;
            border: 1px solid #555;
            outline: none;
            -webkit-appearance: none;
            appearance: none;
        }

        .miniplayer-slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 14px;
            height: 14px;
            border-radius: 50%;
            background: #007acc;
            cursor: pointer;
            border: 2px solid #0e639c;
        }

        .miniplayer-slider::-webkit-slider-thumb:hover {
            background: #1e8ad6;
        }

        .miniplayer-slider::-moz-range-thumb {
            width: 14px;
            height: 14px;
            border-radius: 50%;
            background: #007acc;
            cursor: pointer;
            border: 2px solid #0e639c;
        }

        .miniplayer-slider::-moz-range-thumb:hover {
            background: #1e8ad6;
        }

        .miniplayer-slider::-webkit-slider-runnable-track {
            background: transparent;
        }

        .miniplayer-slider::-moz-range-track {
            background: transparent;
        }

        .miniplayer-volume-value {
            font-size: 11px;
            color: #9cdcfe;
            min-width: 40px;
            text-align: right;
            font-family: 'Courier New', monospace;
        }

        .miniplayer-status {
            text-align: center;
            font-size: 11px;
            color: #858585;
            padding: 8px;
            background: #2d2d30;
            border-radius: 4px;
        }

        .miniplayer-status.playing {
            color: #4ec9b0;
        }

        .miniplayer-status.paused {
            color: #ce9178;
        }

        .miniplayer-status.loading {
            color: #569cd6;
        }

        .miniplayer-status.error {
            color: #f48771;
        }

        .miniplayer-statusbar {
            background: #2d2d30;
            border-top: 1px solid #555;
            padding: 8px 12px;
            display: flex;
            gap: 12px;
            font-size: 10px;
            color: #858585;
            border-radius: 0 0 8px 8px;
        }

        .miniplayer-statusbar-item {
            display: flex;
            align-items: center;
            gap: 4px;
        }

        .miniplayer-statusbar-label {
            color: #9cdcfe;
            font-weight: 500;
        }

        .miniplayer-statusbar-value {
            color: #cccccc;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
            max-width: 150px;
        }

        .miniplayer-statusbar-separator {
            width: 1px;
            background: #555;
            margin: 0 4px;
        }
    `;

    // Inject styles into document
    function injectStyles() {
        if (!document.getElementById('miniplayer-styles')) {
            const styleEl = document.createElement('style');
            styleEl.id = 'miniplayer-styles';
            styleEl.textContent = styles;
            document.head.appendChild(styleEl);
        }
    }

    // Format time as MM:SS.mmm
    function formatTime(ms) {
        const totalSeconds = Math.floor(ms / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        const milliseconds = Math.floor(ms % 1000);
        return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}.${String(milliseconds).padStart(3, '0')}`;
    }

    // Extract filename from URL or return placeholder
    function getFileName(source) {
        if (typeof source === 'string') {
            const filename = source.split('/').pop().split('?')[0];
            return filename || '-';
        }
        return 'Loading...';
    }

    // Create modal HTML
    function createModal(title = 'miniBAE Player') {
        const overlay = document.createElement('div');
        overlay.className = 'miniplayer-overlay';
        
        overlay.innerHTML = `
            <div class="miniplayer-modal">
                <div class="miniplayer-header">
                    <div class="miniplayer-title">${title}</div>
                    <button class="miniplayer-close" title="Close">×</button>
                </div>
                <div class="miniplayer-body">
                    <div class="miniplayer-progress">
                        <div class="miniplayer-times">
                            <span class="miniplayer-current-time">00:00.000</span>
                            <span class="miniplayer-duration">00:00.000</span>
                        </div>
                        <div class="miniplayer-seekbar">
                            <div class="miniplayer-seekbar-fill" style="width: 0%"></div>
                        </div>
                    </div>
                    <div class="miniplayer-controls">
                        <button class="miniplayer-btn miniplayer-play-pause">Play</button>
                        <button class="miniplayer-btn miniplayer-btn-stop miniplayer-stop">Stop</button>
                    </div>
                    <div class="miniplayer-volume">
                        <span class="miniplayer-volume-label">Volume:</span>
                        <input type="range" class="miniplayer-slider miniplayer-volume-slider" 
                               min="0" max="100" value="100" step="1">
                        <span class="miniplayer-volume-value">100%</span>
                    </div>
                    <div class="miniplayer-status loading">Initializing...</div>
                </div>
                <div class="miniplayer-statusbar">
                    <div class="miniplayer-statusbar-item">
                        <span class="miniplayer-statusbar-label">File:</span>
                        <span class="miniplayer-statusbar-value miniplayer-file-status">-</span>
                    </div>
                    <div class="miniplayer-statusbar-separator"></div>
                    <div class="miniplayer-statusbar-item">
                        <span class="miniplayer-statusbar-label">Bank:</span>
                        <span class="miniplayer-statusbar-value miniplayer-bank-status">-</span>
                    </div>
                </div>
            </div>
        `;

        // Close button
        const closeBtn = overlay.querySelector('.miniplayer-close');
        closeBtn.addEventListener('click', closePlayer);

        // Make modal draggable
        const header = overlay.querySelector('.miniplayer-header');
        const modal = overlay.querySelector('.miniplayer-modal');
        let isDragging = false;
        let currentX = 0;
        let currentY = 0;
        let initialX = 0;
        let initialY = 0;

        header.addEventListener('mousedown', (e) => {
            if (e.target === closeBtn || closeBtn.contains(e.target)) return;
            isDragging = true;
            initialX = e.clientX - currentX;
            initialY = e.clientY - currentY;
            header.style.cursor = 'grabbing';
        });

        document.addEventListener('mousemove', (e) => {
            if (!isDragging) return;
            e.preventDefault();
            currentX = e.clientX - initialX;
            currentY = e.clientY - initialY;
            modal.style.transform = `translate(${currentX}px, ${currentY}px)`;
        });

        document.addEventListener('mouseup', () => {
            if (isDragging) {
                isDragging = false;
                header.style.cursor = 'move';
            }
        });

        // ESC key to close
        const escHandler = (e) => {
            if (e.key === 'Escape') {
                closePlayer();
            }
        };
        document.addEventListener('keydown', escHandler);
        overlay._escHandler = escHandler;

        return overlay;
    }

    // Update UI elements
    function updateUI(modal, player) {
        const currentTimeEl = modal.querySelector('.miniplayer-current-time');
        const durationEl = modal.querySelector('.miniplayer-duration');
        const seekbarFill = modal.querySelector('.miniplayer-seekbar-fill');
        const playPauseBtn = modal.querySelector('.miniplayer-play-pause');
        const stopBtn = modal.querySelector('.miniplayer-stop');
        const statusEl = modal.querySelector('.miniplayer-status');

        // Update time display
        function updateTime() {
            const current = player.currentTime;
            const duration = player.duration;

            currentTimeEl.textContent = formatTime(current);
            durationEl.textContent = formatTime(duration);

            // Update seekbar
            const percent = duration > 0 ? (current / duration) * 100 : 0;
            seekbarFill.style.width = percent + '%';
        }

        // Seekbar click
        const seekbar = modal.querySelector('.miniplayer-seekbar');
        seekbar.addEventListener('click', (e) => {
            const rect = seekbar.getBoundingClientRect();
            const percent = (e.clientX - rect.left) / rect.width;
            const newTime = percent * player.duration;
            player.currentTime = newTime;
            updateTime();
        });

        // Play/Pause button
        playPauseBtn.addEventListener('click', () => {
            if (player.isPlaying) {
                player.pause();
                playPauseBtn.textContent = 'Resume';
                statusEl.textContent = 'Paused';
                statusEl.className = 'miniplayer-status paused';
            } else {
                if (playPauseBtn.textContent === 'Play') {
                    player.play();
                } else {
                    player.resume();
                }
                playPauseBtn.textContent = 'Pause';
                statusEl.textContent = 'Playing';
                statusEl.className = 'miniplayer-status playing';
            }
        });

        // Stop button
        stopBtn.addEventListener('click', () => {
            player.stop();
            playPauseBtn.textContent = 'Play';
            statusEl.textContent = 'Stopped';
            statusEl.className = 'miniplayer-status';
            updateTime();
        });

        // Volume control
        const volumeSlider = modal.querySelector('.miniplayer-volume-slider');
        const volumeValue = modal.querySelector('.miniplayer-volume-value');
        
        volumeSlider.addEventListener('input', (e) => {
            const volume = parseInt(e.target.value) / 100;
            player.volume = volume;
            volumeValue.textContent = e.target.value + '%';
        });

        // Time update interval
        const interval = setInterval(() => {
            if (player.isPlaying) {
                updateTime();
            }
        }, 50);

        // Store event handlers for removal (define BEFORE addEventListener)
        modal._stopHandler = () => {
            playPauseBtn.textContent = 'Play';
            statusEl.textContent = 'Stopped';
            statusEl.className = 'miniplayer-status';
        };

        modal._endHandler = () => {
            playPauseBtn.textContent = 'Play';
            statusEl.textContent = 'Finished';
            statusEl.className = 'miniplayer-status';
        };

        modal._errorHandler = (error) => {
            statusEl.textContent = 'Error: ' + error.message;
            statusEl.className = 'miniplayer-status error';
            playPauseBtn.disabled = true;
            stopBtn.disabled = true;
        };

        // Player event listeners
        player.addEventListener('stop', modal._stopHandler);
        player.addEventListener('end', modal._endHandler);
        player.addEventListener('error', modal._errorHandler);

        // Initial update
        updateTime();

        // Store cleanup function
        modal._cleanup = () => {
            clearInterval(interval);
            if (modal._escHandler) {
                document.removeEventListener('keydown', modal._escHandler);
            }
        };

        return { updateTime };
    }

    // Reset global player
    function resetGlobalPlayer() {
        if (globalPlayer) {
            globalPlayer.stop();
            globalPlayer.unload();
        }
    }

    // Close player
    function closePlayer() {
        if (currentModal) {
            if (globalPlayer) {
                resetGlobalPlayer();
                // Remove event listeners
                if (currentModal._stopHandler) {
                    globalPlayer.removeEventListener('stop', currentModal._stopHandler);
                }
                if (currentModal._endHandler) {
                    globalPlayer.removeEventListener('end', currentModal._endHandler);
                }
                if (currentModal._errorHandler) {
                    globalPlayer.removeEventListener('error', currentModal._errorHandler);
                }
            }
            
            if (currentModal._cleanup) {
                currentModal._cleanup();
            }
            currentModal.remove();
            currentModal = null;
            currentSoundbank = null;
        }
    }

    // Initialize player if needed
    async function initPlayer() {
        if (!globalPlayer) {
            if (typeof BeatnikPlayer === 'undefined') {
                throw new Error('BeatnikPlayer not found. Please include player-class.js');
            }
            
            globalPlayer = await BeatnikPlayer.init({
                sampleRate: 44100,
                maxVoices: 64
            });
        }
        return globalPlayer;
    }

    // Main playSong function
    async function playSong(songSource, bankSource, options = {}) {
        try {
            // Inject styles
            injectStyles();

            // Extract title from options or source
            let title = options.title || 'miniBAE Player';
            if (typeof songSource === 'string' && !options.title) {
                const filename = songSource.split('/').pop().split('?')[0];
                title = filename || 'miniBAE Player';
            }

            // Create or reuse modal
            if (!currentModal) {
                currentModal = createModal(title);
                document.body.appendChild(currentModal);
            } else {
                // Update title of existing modal
                const titleEl = currentModal.querySelector('.miniplayer-title');
                if (titleEl) titleEl.textContent = title;
            }

            const statusEl = currentModal.querySelector('.miniplayer-status');
            const playPauseBtn = currentModal.querySelector('.miniplayer-play-pause');
            const stopBtn = currentModal.querySelector('.miniplayer-stop');
            const fileStatus = currentModal.querySelector('.miniplayer-file-status');
            const bankStatus = currentModal.querySelector('.miniplayer-bank-status');

            // Update status bar with file names
            fileStatus.textContent = getFileName(songSource);
            fileStatus.title = fileStatus.textContent; // Full name on hover
            bankStatus.textContent = getFileName(bankSource);
            bankStatus.title = bankStatus.textContent; // Full name on hover

            // Initialize player
            statusEl.textContent = 'Initializing player...';
            const player = await initPlayer();

            // Unload previous song if player already has content
            if (player.isLoaded) {
                player.unload();
            }

            if (player.isPlaying) {
                player.stop();
            }

            // Get file extension from songSource
            let songExt = '';
            if (typeof songSource === 'string') {
                const filename = songSource.split('/').pop().split('?')[0];
                const lastDot = filename.lastIndexOf('.');
                if (lastDot !== -1) {
                    songExt = filename.substring(lastDot + 1).toLowerCase();
                }
            }

            if (!bankSource) {
                if (songExt === 'rmf') {
                    // Use default RMF bank if none provided
                    bankSource = default_bank_rmf;
                    bankStatus.textContent = getFileName(bankSource);
                    bankStatus.title = bankStatus.textContent;
                } else {
                    // Use default MIDI bank if none provided
                    bankSource = default_bank_mid;
                    bankStatus.textContent = getFileName(bankSource);
                    bankStatus.title = bankStatus.textContent;
                }
            }

            // Load soundbank only if different or not loaded
            const bankKey = typeof bankSource === 'string' ? bankSource : 'buffer';
            if (currentSoundbank !== bankKey) {
                if (currentSoundbank) {
                    await player.unloadSoundbank();
                }
                statusEl.textContent = 'Loading soundbank...';
                await player.loadSoundbank(bankSource);
                currentSoundbank = bankKey;
            }

            // Load song
            statusEl.textContent = 'Loading song...';
            await player.load(songSource);

            // Setup UI (only if new modal or first time)
            if (!currentModal._uiSetup) {
                const { updateTime } = updateUI(currentModal, player);
                currentModal._updateTime = updateTime;
                currentModal._uiSetup = true;
            } else {
                // Just update the time display for reused modal
                if (currentModal._updateTime) {
                    currentModal._updateTime();
                }
            }

            // Enable controls
            playPauseBtn.disabled = false;
            stopBtn.disabled = false;

            // Auto-play if requested
            if (options.autoplay !== false) {
                player.play();
                playPauseBtn.textContent = 'Pause';
                statusEl.textContent = 'Playing';
                statusEl.className = 'miniplayer-status playing';
            } else {
                statusEl.textContent = 'Ready';
                statusEl.className = 'miniplayer-status';
            }

            if (currentModal._updateTime) {
                currentModal._updateTime();
            }

        } catch (error) {
            console.error('miniBAE Player Error:', error);
            if (currentModal) {
                const statusEl = currentModal.querySelector('.miniplayer-status');
                statusEl.textContent = 'Error: ' + error.message;
                statusEl.className = 'miniplayer-status error';
            } else {
                alert('Failed to load player: ' + error.message);
            }
        }
    }

    // Export to global scope
    window.playSong = playSong;
    window.closeMiniPlayer = closePlayer;

})();
