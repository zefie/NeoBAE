/* Phase 2 BE2-style Sample Editor — included inside NbEditorApp. */

    static const char *SeNoteName(int note, char *buffer, size_t buffer_size)
    {
        static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const int clamped = std::clamp(note, 0, 127);
        std::snprintf(buffer, buffer_size, "%s%d", names[clamped % 12], (clamped / 12) - 1);
        return buffer;
    }

    static const int *SeSampleRatePresets(size_t *out_count)
    {
        static const int presets[] = {48000, 44100, 36000, 22050, 11025};
        if (out_count)
        {
            *out_count = SDL_arraysize(presets);
        }
        return presets;
    }

    static bool SeIsPlayer20Codec(BAERmfEditorCompressionType type)
    {
        switch (type)
        {
        case BAE_EDITOR_COMPRESSION_DONT_CHANGE:
        case BAE_EDITOR_COMPRESSION_PCM:
        case BAE_EDITOR_COMPRESSION_ADPCM:
        case BAE_EDITOR_COMPRESSION_MP3_32K:
        case BAE_EDITOR_COMPRESSION_MP3_48K:
        case BAE_EDITOR_COMPRESSION_MP3_64K:
        case BAE_EDITOR_COMPRESSION_MP3_96K:
        case BAE_EDITOR_COMPRESSION_MP3_128K:
        case BAE_EDITOR_COMPRESSION_MP3_192K:
        case BAE_EDITOR_COMPRESSION_MP3_256K:
        case BAE_EDITOR_COMPRESSION_MP3_320K:
            return true;
        default:
            return false;
        }
    }

    static bool SeCodecIsUncompressedPcm(BAERmfEditorCompressionType type)
    {
        return type == BAE_EDITOR_COMPRESSION_PCM || type == BAE_EDITOR_COMPRESSION_DONT_CHANGE;
    }

    /* Map loop points from one frame-count domain into another (lossy encode /
     * MPEG resample). Matches PV_RemapLoopPointsToFrameCount half-open rules. */
    static void SeRemapLoopPoints(uint32_t source_frames,
                                  uint32_t target_frames,
                                  uint32_t *io_loop_start,
                                  uint32_t *io_loop_end)
    {
        if (!io_loop_start || !io_loop_end)
        {
            return;
        }
        uint32_t loop_start = *io_loop_start;
        uint32_t loop_end = *io_loop_end;
        if (source_frames == 0 || target_frames == 0 || loop_end <= loop_start ||
            loop_start >= source_frames)
        {
            *io_loop_start = 0;
            *io_loop_end = 0;
            return;
        }
        if (loop_end > source_frames)
        {
            loop_end = source_frames;
        }
        /* IMA4 decode pads to 64-frame blocks. Stretching loops into that pad
         * detunes the loop the same way save-time remap used to. Keep source
         * domain markers; they still fit in the padded buffer. */
        if (source_frames != target_frames)
        {
            const uint32_t ima4_padded =
                ((source_frames + 63u) / 64u) * 64u;
            if (target_frames == ima4_padded)
            {
                *io_loop_start = loop_start;
                *io_loop_end = loop_end;
                return;
            }
        }
        if (source_frames != target_frames)
        {
            const uint32_t mapped_start = static_cast<uint32_t>(
                (static_cast<uint64_t>(loop_start) * static_cast<uint64_t>(target_frames)) /
                static_cast<uint64_t>(source_frames));
            uint32_t mapped_end = static_cast<uint32_t>(
                ((static_cast<uint64_t>(loop_end) * static_cast<uint64_t>(target_frames)) +
                 (static_cast<uint64_t>(source_frames) - 1ull)) /
                static_cast<uint64_t>(source_frames));
            if (mapped_start > target_frames)
            {
                *io_loop_start = 0;
                *io_loop_end = 0;
                return;
            }
            if (mapped_end > target_frames)
            {
                mapped_end = target_frames;
            }
            if (mapped_end <= mapped_start)
            {
                if (mapped_start < target_frames)
                {
                    mapped_end = mapped_start + 1u;
                }
                else if (target_frames > 0)
                {
                    *io_loop_start = target_frames - 1u;
                    *io_loop_end = target_frames;
                    return;
                }
                else
                {
                    *io_loop_start = 0;
                    *io_loop_end = 0;
                    return;
                }
            }
            loop_start = mapped_start;
            loop_end = mapped_end;
        }
        *io_loop_start = loop_start;
        *io_loop_end = loop_end;
    }

    /* ---------------- PCM accessors ---------------- */

    uint32_t SeChannelCount() const
    {
        return std::max<uint32_t>(static_cast<uint32_t>(m_sample_editor_cached_channels), 1u);
    }

    uint32_t SeBytesPerFrame() const
    {
        const uint32_t bytes = (m_sample_editor_cached_bit_size == 8) ? 1u : 2u;
        return bytes * SeChannelCount();
    }

    float SeReadSample(uint32_t frame, uint32_t channel) const
    {
        const uint32_t channels = SeChannelCount();
        const size_t index = (static_cast<size_t>(frame) * channels) + channel;
        if (m_sample_editor_cached_bit_size == 8)
        {
            if (index >= m_sample_editor_cached_pcm.size())
            {
                return 0.0f;
            }
            return (static_cast<float>(m_sample_editor_cached_pcm[index]) - 128.0f) / 128.0f;
        }
        const size_t count = m_sample_editor_cached_pcm.size() / sizeof(int16_t);
        if (index >= count)
        {
            return 0.0f;
        }
        const int16_t *pcm16 = reinterpret_cast<const int16_t *>(m_sample_editor_cached_pcm.data());
        return static_cast<float>(pcm16[index]) / 32768.0f;
    }

    void SeWriteSample(uint32_t frame, uint32_t channel, float value)
    {
        const uint32_t channels = SeChannelCount();
        const size_t index = (static_cast<size_t>(frame) * channels) + channel;
        const float clamped = std::clamp(value, -1.0f, 1.0f);
        if (m_sample_editor_cached_bit_size == 8)
        {
            if (index >= m_sample_editor_cached_pcm.size())
            {
                return;
            }
            const int converted = static_cast<int>(std::lround(clamped * 127.0f)) + 128;
            m_sample_editor_cached_pcm[index] = static_cast<unsigned char>(std::clamp(converted, 0, 255));
            return;
        }
        const size_t count = m_sample_editor_cached_pcm.size() / sizeof(int16_t);
        if (index >= count)
        {
            return;
        }
        int16_t *pcm16 = reinterpret_cast<int16_t *>(m_sample_editor_cached_pcm.data());
        const int converted = static_cast<int>(std::lround(clamped * 32767.0f));
        pcm16[index] = static_cast<int16_t>(std::clamp(converted, -32768, 32767));
    }

    /* ---------------- View / bins ---------------- */

    void SeClampView()
    {
        if (m_sample_editor_frame_count == 0)
        {
            m_sample_editor_view_start = 0;
            m_sample_editor_view_end = 0;
            return;
        }
        if (m_sample_editor_view_end > m_sample_editor_frame_count)
        {
            m_sample_editor_view_end = m_sample_editor_frame_count;
        }
        if (m_sample_editor_view_end <= m_sample_editor_view_start)
        {
            m_sample_editor_view_start = 0;
            m_sample_editor_view_end = m_sample_editor_frame_count;
        }
        if (m_sample_editor_view_end - m_sample_editor_view_start < 8u)
        {
            const uint32_t start = std::min<uint32_t>(m_sample_editor_view_start,
                                                      (m_sample_editor_frame_count > 8u)
                                                          ? (m_sample_editor_frame_count - 8u)
                                                          : 0u);
            m_sample_editor_view_start = start;
            m_sample_editor_view_end = std::min<uint32_t>(start + 8u, m_sample_editor_frame_count);
        }
    }

    void RebuildSampleEditorWaveformBins()
    {
        m_sample_editor_wave_min.clear();
        m_sample_editor_wave_max.clear();
        if (m_sample_editor_cached_frame_count == 0 || m_sample_editor_cached_pcm.empty())
        {
            return;
        }

        SeClampView();
        const uint32_t view_start = m_sample_editor_view_start;
        const uint32_t view_end = std::max<uint32_t>(m_sample_editor_view_end, view_start + 1u);
        const uint32_t span = std::min<uint32_t>(view_end, m_sample_editor_cached_frame_count) - view_start;
        if (span == 0)
        {
            return;
        }

        const int bins = std::max(1, static_cast<int>(std::min<uint32_t>(span, 2048u)));
        m_sample_editor_wave_min.assign(static_cast<size_t>(bins), 0.0f);
        m_sample_editor_wave_max.assign(static_cast<size_t>(bins), 0.0f);
        const uint32_t channels = SeChannelCount();

        for (int bin = 0; bin < bins; ++bin)
        {
            const uint32_t begin = view_start +
                                   static_cast<uint32_t>((static_cast<uint64_t>(bin) * span) / static_cast<uint64_t>(bins));
            uint32_t end = view_start +
                           static_cast<uint32_t>((static_cast<uint64_t>(bin + 1) * span) / static_cast<uint64_t>(bins));
            if (end <= begin)
            {
                end = std::min<uint32_t>(view_start + span, begin + 1u);
            }

            float min_v = 1.0f;
            float max_v = -1.0f;
            for (uint32_t frame = begin; frame < end; ++frame)
            {
                float mono = 0.0f;
                for (uint32_t ch = 0; ch < channels; ++ch)
                {
                    mono += SeReadSample(frame, ch);
                }
                mono /= static_cast<float>(channels);
                min_v = std::min(min_v, mono);
                max_v = std::max(max_v, mono);
            }
            if (max_v < min_v)
            {
                min_v = 0.0f;
                max_v = 0.0f;
            }
            m_sample_editor_wave_min[static_cast<size_t>(bin)] = min_v;
            m_sample_editor_wave_max[static_cast<size_t>(bin)] = max_v;
        }
    }

    void SeSyncFromCache()
    {
        m_sample_editor_frame_count = m_sample_editor_cached_frame_count;
        m_sample_editor_channels = static_cast<int>(SeChannelCount());
        m_sample_editor_bit_depth = (m_sample_editor_cached_bit_size == 8) ? 8 : 16;

        if (m_sample_editor_frame_count == 0)
        {
            m_sample_editor_loop_start = 0;
            m_sample_editor_loop_end = 0;
        }
        else
        {
            m_sample_editor_loop_start = std::min<uint32_t>(m_sample_editor_loop_start,
                                                            m_sample_editor_frame_count - 1u);
            m_sample_editor_loop_end = std::min<uint32_t>(m_sample_editor_loop_end, m_sample_editor_frame_count);
            if (m_sample_editor_loop_end < m_sample_editor_loop_start)
            {
                m_sample_editor_loop_end = m_sample_editor_loop_start;
            }
        }

        if (m_sample_editor_sel_start >= static_cast<int>(m_sample_editor_frame_count) ||
            m_sample_editor_sel_end > static_cast<int>(m_sample_editor_frame_count))
        {
            m_sample_editor_sel_start = -1;
            m_sample_editor_sel_end = -1;
        }

        m_sample_editor_playhead_frame = std::min<uint32_t>(m_sample_editor_playhead_frame,
                                                            m_sample_editor_frame_count);
        SeClampView();
        RebuildSampleEditorWaveformBins();
    }

    void SeCommitMasterFromCache()
    {
        /* Waveform edits update the uncompressed master used for compression. */
        m_sample_editor_original_pcm = m_sample_editor_cached_pcm;
        m_sample_editor_original_frame_count = m_sample_editor_cached_frame_count;
        m_sample_editor_original_bit_size = m_sample_editor_cached_bit_size;
        m_sample_editor_original_channels = m_sample_editor_cached_channels;
        m_sample_editor_original_sample_rate = m_sample_editor_cached_sample_rate
                                                  ? m_sample_editor_cached_sample_rate
                                                  : LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz);
        m_sample_editor_audition_pcm.clear();
        m_sample_editor_audition_frame_count = 0;
    }

    void SeRestoreCacheFromMaster()
    {
        m_sample_editor_cached_pcm = m_sample_editor_original_pcm;
        m_sample_editor_cached_frame_count = m_sample_editor_original_frame_count;
        m_sample_editor_cached_bit_size = m_sample_editor_original_bit_size;
        m_sample_editor_cached_channels = m_sample_editor_original_channels;
        m_sample_editor_cached_sample_rate = m_sample_editor_original_sample_rate;
        SeSyncFromCache();
    }

    /* ---------------- Undo / redo ---------------- */

    SampleEditorUndoState CaptureSampleEditorState() const
    {
        SampleEditorUndoState state;
        state.pcm = m_sample_editor_cached_pcm;
        state.frame_count = m_sample_editor_cached_frame_count;
        state.bit_size = m_sample_editor_cached_bit_size;
        state.channels = m_sample_editor_cached_channels;
        state.sample_rate = m_sample_editor_cached_sample_rate;
        state.sample_rate_hz = m_sample_editor_sample_rate_hz;
        state.loop_start = m_sample_editor_loop_start;
        state.loop_end = m_sample_editor_loop_end;
        state.loop_enabled = m_sample_editor_loop_enabled;
        state.root_key = m_sample_editor_root_key;
        state.volume = m_sample_editor_volume;
        state.rate_preset = m_sample_editor_rate_preset;
        state.name = m_sample_editor_sample_name;
        return state;
    }

    void RestoreSampleEditorState(const SampleEditorUndoState &state)
    {
        m_sample_editor_cached_pcm = state.pcm;
        m_sample_editor_cached_frame_count = state.frame_count;
        m_sample_editor_cached_bit_size = state.bit_size;
        m_sample_editor_cached_channels = state.channels;
        m_sample_editor_cached_sample_rate = state.sample_rate;
        m_sample_editor_sample_rate_hz = state.sample_rate_hz;
        m_sample_editor_loop_start = state.loop_start;
        m_sample_editor_loop_end = state.loop_end;
        m_sample_editor_loop_enabled = state.loop_enabled;
        m_sample_editor_root_key = state.root_key;
        m_sample_editor_volume = state.volume;
        m_sample_editor_rate_preset = state.rate_preset;
        m_sample_editor_sample_name = state.name;
        SeSyncFromCache();
        SeCommitMasterFromCache();
    }

    void SePushUndoState(std::deque<SampleEditorUndoState> &target)
    {
        target.push_back(CaptureSampleEditorState());
        while (target.size() > 32u)
        {
            target.pop_front();
        }
    }

    void PushSampleEditorUndo()
    {
        SePushUndoState(m_sample_editor_undo);
        m_sample_editor_redo.clear();
        m_sample_editor_dirty = true;
    }

    void SampleEditorUndo()
    {
        if (m_sample_editor_undo.empty())
        {
            return;
        }
        SePushUndoState(m_sample_editor_redo);
        const SampleEditorUndoState state = m_sample_editor_undo.back();
        m_sample_editor_undo.pop_back();
        RestoreSampleEditorState(state);
        m_sample_editor_dirty = true;
        SetStatus("Sample editor: undo");
    }

    void SampleEditorRedo()
    {
        if (m_sample_editor_redo.empty())
        {
            return;
        }
        SePushUndoState(m_sample_editor_undo);
        const SampleEditorUndoState state = m_sample_editor_redo.back();
        m_sample_editor_redo.pop_back();
        RestoreSampleEditorState(state);
        m_sample_editor_dirty = true;
        SetStatus("Sample editor: redo");
    }

    void RevertSampleEditor()
    {
        if (m_sample_editor_pristine.pcm.empty())
        {
            return;
        }
        SePushUndoState(m_sample_editor_undo);
        m_sample_editor_redo.clear();
        RestoreSampleEditorState(m_sample_editor_pristine);
        m_sample_editor_dirty = true;
        SetStatus("Sample editor: reverted to opened state");
    }

    /* ---------------- Selection ---------------- */

    bool SeHasSelection() const
    {
        return m_sample_editor_sel_start >= 0 && m_sample_editor_sel_end > m_sample_editor_sel_start;
    }

    void SeSelectionRange(uint32_t *out_begin, uint32_t *out_end) const
    {
        uint32_t begin = 0;
        uint32_t end = m_sample_editor_frame_count;
        if (SeHasSelection())
        {
            begin = static_cast<uint32_t>(m_sample_editor_sel_start);
            end = std::min<uint32_t>(static_cast<uint32_t>(m_sample_editor_sel_end), m_sample_editor_frame_count);
        }
        if (out_begin)
        {
            *out_begin = begin;
        }
        if (out_end)
        {
            *out_end = end;
        }
    }

    void SeSelectAll()
    {
        if (m_sample_editor_frame_count == 0)
        {
            return;
        }
        m_sample_editor_sel_start = 0;
        m_sample_editor_sel_end = static_cast<int>(m_sample_editor_frame_count);
    }

    /* ---------------- Destructive edits ---------------- */

    void SeShiftLoopsForCut(uint32_t begin, uint32_t end)
    {
        const uint32_t removed = end - begin;
        auto adjust = [begin, end, removed](uint32_t value) -> uint32_t {
            if (value <= begin)
            {
                return value;
            }
            if (value >= end)
            {
                return value - removed;
            }
            return begin;
        };
        m_sample_editor_loop_start = adjust(m_sample_editor_loop_start);
        m_sample_editor_loop_end = adjust(m_sample_editor_loop_end);
    }

    void SeDeleteSelection()
    {
        uint32_t begin = 0;
        uint32_t end = 0;
        SeSelectionRange(&begin, &end);
        if (end <= begin || m_sample_editor_frame_count == 0 || (end - begin) >= m_sample_editor_frame_count)
        {
            SetStatus("Sample editor: nothing to delete");
            return;
        }

        PushSampleEditorUndo();
        const uint32_t bytes_per_frame = SeBytesPerFrame();
        const size_t byte_begin = static_cast<size_t>(begin) * bytes_per_frame;
        const size_t byte_end = std::min<size_t>(static_cast<size_t>(end) * bytes_per_frame,
                                                 m_sample_editor_cached_pcm.size());
        if (byte_end > byte_begin)
        {
            m_sample_editor_cached_pcm.erase(m_sample_editor_cached_pcm.begin() + static_cast<ptrdiff_t>(byte_begin),
                                             m_sample_editor_cached_pcm.begin() + static_cast<ptrdiff_t>(byte_end));
        }
        m_sample_editor_cached_frame_count -= (end - begin);
        SeShiftLoopsForCut(begin, end);
        m_sample_editor_sel_start = -1;
        m_sample_editor_sel_end = -1;
        m_sample_editor_view_start = 0;
        m_sample_editor_view_end = m_sample_editor_cached_frame_count;
        SeSyncFromCache();
        SeCommitMasterFromCache();
        SetStatus("Sample editor: deleted selection");
    }

    void SeCropToSelection()
    {
        if (!SeHasSelection())
        {
            SetStatus("Sample editor: no selection to crop to");
            return;
        }
        uint32_t begin = 0;
        uint32_t end = 0;
        SeSelectionRange(&begin, &end);
        if (end <= begin)
        {
            return;
        }

        PushSampleEditorUndo();
        const uint32_t bytes_per_frame = SeBytesPerFrame();
        const size_t byte_begin = std::min<size_t>(static_cast<size_t>(begin) * bytes_per_frame,
                                                   m_sample_editor_cached_pcm.size());
        const size_t byte_end = std::min<size_t>(static_cast<size_t>(end) * bytes_per_frame,
                                                 m_sample_editor_cached_pcm.size());
        std::vector<unsigned char> cropped(m_sample_editor_cached_pcm.begin() + static_cast<ptrdiff_t>(byte_begin),
                                           m_sample_editor_cached_pcm.begin() + static_cast<ptrdiff_t>(byte_end));
        m_sample_editor_cached_pcm.swap(cropped);
        m_sample_editor_cached_frame_count = end - begin;

        const uint32_t loop_start = (m_sample_editor_loop_start > begin) ? (m_sample_editor_loop_start - begin) : 0u;
        const uint32_t loop_end = (m_sample_editor_loop_end > begin) ? (m_sample_editor_loop_end - begin) : 0u;
        m_sample_editor_loop_start = std::min<uint32_t>(loop_start, m_sample_editor_cached_frame_count);
        m_sample_editor_loop_end = std::min<uint32_t>(loop_end, m_sample_editor_cached_frame_count);
        m_sample_editor_sel_start = -1;
        m_sample_editor_sel_end = -1;
        m_sample_editor_view_start = 0;
        m_sample_editor_view_end = m_sample_editor_cached_frame_count;
        SeSyncFromCache();
        SeCommitMasterFromCache();
        SetStatus("Sample editor: cropped to selection");
    }

    void SeNormalize()
    {
        uint32_t begin = 0;
        uint32_t end = 0;
        SeSelectionRange(&begin, &end);
        if (end <= begin)
        {
            return;
        }

        const uint32_t channels = SeChannelCount();
        float peak = 0.0f;
        for (uint32_t frame = begin; frame < end; ++frame)
        {
            for (uint32_t ch = 0; ch < channels; ++ch)
            {
                peak = std::max(peak, std::fabs(SeReadSample(frame, ch)));
            }
        }
        if (peak <= 0.0001f)
        {
            SetStatus("Sample editor: nothing to normalize (silent)");
            return;
        }

        PushSampleEditorUndo();
        const float scale = 0.99f / peak;
        for (uint32_t frame = begin; frame < end; ++frame)
        {
            for (uint32_t ch = 0; ch < channels; ++ch)
            {
                SeWriteSample(frame, ch, SeReadSample(frame, ch) * scale);
            }
        }
        RebuildSampleEditorWaveformBins();
        SeCommitMasterFromCache();
        SetStatus("Sample editor: normalized");
    }

    void SeFade(bool fade_in)
    {
        uint32_t begin = 0;
        uint32_t end = 0;
        SeSelectionRange(&begin, &end);
        if (end <= begin)
        {
            return;
        }

        PushSampleEditorUndo();
        const uint32_t channels = SeChannelCount();
        const float span = static_cast<float>(end - begin);
        for (uint32_t frame = begin; frame < end; ++frame)
        {
            const float t = static_cast<float>(frame - begin) / span;
            const float gain = fade_in ? t : (1.0f - t);
            for (uint32_t ch = 0; ch < channels; ++ch)
            {
                SeWriteSample(frame, ch, SeReadSample(frame, ch) * gain);
            }
        }
        RebuildSampleEditorWaveformBins();
        SeCommitMasterFromCache();
        SetStatus(fade_in ? "Sample editor: faded in" : "Sample editor: faded out");
    }

    void SeApplyVolumeHandles()
    {
        const bool has_fade_in = m_sample_editor_fade_in_frames > 0;
        const bool has_fade_out = m_sample_editor_fade_out_frames > 0;
        const bool has_gain = std::fabs(m_sample_editor_gain - 1.0f) > 0.001f;
        if ((!has_fade_in && !has_fade_out && !has_gain) || m_sample_editor_frame_count == 0)
        {
            m_sample_editor_fade_in_frames = 0;
            m_sample_editor_fade_out_frames = 0;
            m_sample_editor_gain = 1.0f;
            return;
        }

        PushSampleEditorUndo();
        const uint32_t channels = SeChannelCount();

        if (has_gain)
        {
            for (uint32_t frame = 0; frame < m_sample_editor_frame_count; ++frame)
            {
                for (uint32_t ch = 0; ch < channels; ++ch)
                {
                    SeWriteSample(frame, ch, SeReadSample(frame, ch) * m_sample_editor_gain);
                }
            }
        }
        if (has_fade_in)
        {
            const uint32_t span = std::min<uint32_t>(m_sample_editor_fade_in_frames, m_sample_editor_frame_count);
            for (uint32_t frame = 0; frame < span; ++frame)
            {
                const float gain = static_cast<float>(frame) / static_cast<float>(span);
                for (uint32_t ch = 0; ch < channels; ++ch)
                {
                    SeWriteSample(frame, ch, SeReadSample(frame, ch) * gain);
                }
            }
        }
        if (has_fade_out)
        {
            const uint32_t span = std::min<uint32_t>(m_sample_editor_fade_out_frames, m_sample_editor_frame_count);
            const uint32_t begin = m_sample_editor_frame_count - span;
            for (uint32_t frame = begin; frame < m_sample_editor_frame_count; ++frame)
            {
                const float gain = 1.0f - (static_cast<float>(frame - begin) / static_cast<float>(span));
                for (uint32_t ch = 0; ch < channels; ++ch)
                {
                    SeWriteSample(frame, ch, SeReadSample(frame, ch) * gain);
                }
            }
        }

        m_sample_editor_fade_in_frames = 0;
        m_sample_editor_fade_out_frames = 0;
        m_sample_editor_gain = 1.0f;
        RebuildSampleEditorWaveformBins();
        SeCommitMasterFromCache();
        SetStatus("Sample editor: applied volume handles");
    }

    /* ---------------- Zoom / view commands ---------------- */

    void SeZoomNormal()
    {
        m_sample_editor_view_start = 0;
        m_sample_editor_view_end = m_sample_editor_frame_count;
        m_sample_editor_zoom_y = 1.0f;
        SeClampView();
        RebuildSampleEditorWaveformBins();
    }

    void SeZoomAroundFrame(uint32_t frame, uint32_t span)
    {
        if (m_sample_editor_frame_count == 0)
        {
            return;
        }
        span = std::clamp<uint32_t>(span, 8u, m_sample_editor_frame_count);
        const uint32_t half = span / 2u;
        uint32_t start = (frame > half) ? (frame - half) : 0u;
        if (start + span > m_sample_editor_frame_count)
        {
            start = (m_sample_editor_frame_count > span) ? (m_sample_editor_frame_count - span) : 0u;
        }
        m_sample_editor_view_start = start;
        m_sample_editor_view_end = std::min<uint32_t>(start + span, m_sample_editor_frame_count);
        SeClampView();
        RebuildSampleEditorWaveformBins();
    }

    void SeZoomToLoopStart()
    {
        SeZoomAroundFrame(m_sample_editor_loop_start, std::max<uint32_t>(m_sample_editor_frame_count / 64u, 256u));
    }

    void SeZoomToLoopEnd()
    {
        SeZoomAroundFrame(m_sample_editor_loop_end, std::max<uint32_t>(m_sample_editor_frame_count / 64u, 256u));
    }

    void SeJumpToLoopInView()
    {
        if (m_sample_editor_loop_end <= m_sample_editor_loop_start)
        {
            return;
        }
        const uint32_t span = m_sample_editor_loop_end - m_sample_editor_loop_start;
        const uint32_t pad = std::max<uint32_t>(span / 8u, 16u);
        m_sample_editor_view_start = (m_sample_editor_loop_start > pad) ? (m_sample_editor_loop_start - pad) : 0u;
        m_sample_editor_view_end = std::min<uint32_t>(m_sample_editor_loop_end + pad, m_sample_editor_frame_count);
        SeClampView();
        RebuildSampleEditorWaveformBins();
    }

    void SeZoomBy(float factor, uint32_t focus_frame)
    {
        if (m_sample_editor_frame_count == 0)
        {
            return;
        }
        const uint32_t current_span = std::max<uint32_t>(m_sample_editor_view_end - m_sample_editor_view_start, 8u);
        float new_span = static_cast<float>(current_span) * factor;
        new_span = std::clamp(new_span, 8.0f, static_cast<float>(m_sample_editor_frame_count));
        const uint32_t span = std::max<uint32_t>(static_cast<uint32_t>(new_span), 8u);

        const float ratio = std::clamp((static_cast<float>(focus_frame) - static_cast<float>(m_sample_editor_view_start)) /
                                           static_cast<float>(current_span),
                                       0.0f,
                                       1.0f);
        const float raw_start = static_cast<float>(focus_frame) - (ratio * static_cast<float>(span));
        uint32_t start = (raw_start > 0.0f) ? static_cast<uint32_t>(raw_start) : 0u;
        if (start + span > m_sample_editor_frame_count)
        {
            start = (m_sample_editor_frame_count > span) ? (m_sample_editor_frame_count - span) : 0u;
        }
        m_sample_editor_view_start = start;
        m_sample_editor_view_end = std::min<uint32_t>(start + span, m_sample_editor_frame_count);
        SeClampView();
        RebuildSampleEditorWaveformBins();
    }

    /* ---------------- Open ---------------- */

    /* INST XReplace (sample apply / IE keymap) shifts GetIndexed order, so
     * SampleRow.instrument_index/split_index can point at a different INST's
     * split while snd_resource_id / the list label stay correct. Always key by SND. */
    bool ResolveBankSplitForSnd(uint32_t snd_id,
                                uint32_t *out_inst_index,
                                uint32_t *out_split_index,
                                BAERmfEditorBankSampleInfo *out_info) const
    {
        if (!m_bank_token || snd_id == kNoSndResourceId || !out_inst_index || !out_split_index)
        {
            return false;
        }
        uint32_t instrument_count = 0;
        if (BAERmfEditorBank_GetInstrumentCount(m_bank_token, &instrument_count) != BAE_NO_ERROR)
        {
            return false;
        }
        const uint16_t want = static_cast<uint16_t>(snd_id);
        for (uint32_t ii = 0; ii < instrument_count; ++ii)
        {
            uint32_t split_count = 0;
            if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token, ii, &split_count) !=
                BAE_NO_ERROR)
            {
                continue;
            }
            for (uint32_t si = 0; si < split_count; ++si)
            {
                BAERmfEditorBankSampleInfo info;
                std::memset(&info, 0, sizeof(info));
                if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token, ii, si, &info) !=
                    BAE_NO_ERROR)
                {
                    continue;
                }
                if (static_cast<uint16_t>(info.sndResourceID) != want)
                {
                    continue;
                }
                *out_inst_index = ii;
                *out_split_index = si;
                if (out_info)
                {
                    *out_info = info;
                }
                return true;
            }
        }
        return false;
    }

    void OpenSampleEditorForSelection()
    {
        if (m_selected_sample < 0 || m_selected_sample >= static_cast<int>(m_samples.size()))
        {
            return;
        }

        const SampleRow &sample = m_samples[static_cast<size_t>(m_selected_sample)];
        m_sample_editor_sample_row = m_selected_sample;
        m_sample_editor_is_song_sample = (sample.source == SampleSource::Song);
        m_sample_editor_document_sample_index = sample.document_sample_index;
        m_sample_editor_sample_name = sample.name;
        m_sample_editor_snd_resource_id = sample.snd_resource_id;
        m_sample_editor_root_key = sample.root_key;
        m_sample_editor_volume = 100;
        m_sample_editor_volume_editable = false;
        m_sample_editor_preview_needs_reencode = false;
        m_sample_editor_preview_note = sample.root_key;
        m_sample_editor_playing = false;
        m_sample_editor_play_started_ms = 0;
        m_sample_editor_playhead_frame = 0;
        m_sample_editor_loop_dirty = false;
        m_sample_editor_dirty = false;
        m_sample_editor_drag_mode = SeDragMode::None;
        m_sample_editor_sel_start = -1;
        m_sample_editor_sel_end = -1;
        m_sample_editor_fade_in_frames = 0;
        m_sample_editor_fade_out_frames = 0;
        m_sample_editor_gain = 1.0f;
        m_sample_editor_zoom_y = 1.0f;
        m_sample_editor_compressed_bytes = 0;
        m_sample_editor_choose_note_open = false;
        m_sample_editor_compression_open = false;
        m_sample_editor_undo.clear();
        m_sample_editor_redo.clear();
        m_sample_editor_wave_min.clear();
        m_sample_editor_wave_max.clear();
        m_sample_editor_cached_pcm.clear();
        m_sample_editor_original_pcm.clear();
        m_sample_editor_audition_pcm.clear();
        m_sample_editor_audition_frame_count = 0;
        m_sample_editor_has_uncompressed_master = false;

        void *wave_data = nullptr;
        uint32_t frame_count = 0;
        uint16_t bit_size = 0;
        uint16_t channels = 0;
        BAE_UNSIGNED_FIXED sample_rate = 0;
        BAERmfEditorCompressionType detected = BAE_EDITOR_COMPRESSION_PCM;
        m_sample_editor_opus_mode = BAE_EDITOR_OPUS_MODE_AUDIO;
        m_sample_editor_snd_storage_type = BAE_EDITOR_SND_STORAGE_ESND;

        if (m_sample_editor_is_song_sample)
        {
            if (!m_document)
            {
                SetStatus("Sample editor: no song document");
                return;
            }
            BAERmfEditorSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorDocument_GetSampleInfo(m_document, sample.document_sample_index, &info) != BAE_NO_ERROR)
            {
                SetStatus("Sample editor: failed to read song sample info");
                return;
            }
            m_sample_editor_loop_start = info.sampleInfo.startLoop;
            m_sample_editor_loop_end = info.sampleInfo.endLoop;
            m_sample_editor_frame_count = info.sampleInfo.waveFrames;
            m_sample_editor_sample_rate_hz = info.sampleInfo.sampledRate
                                                ? static_cast<uint32_t>(info.sampleInfo.sampledRate >> 16)
                                                : 44100;
            m_sample_editor_channels = info.sampleInfo.channels ? info.sampleInfo.channels : 1;
            m_sample_editor_bit_depth = info.sampleInfo.bitSize ? info.sampleInfo.bitSize : 16;
            m_sample_editor_snd_storage_type = info.sndStorageType;
            m_sample_editor_opus_mode = info.opusMode;
            m_sample_editor_root_key = static_cast<int>(info.rootKey);
            m_sample_editor_volume = DisplayInstVolume(info.splitVolume);
            m_sample_editor_volume_editable = true;
            /* sampleSize is the encoded byte size of the sample as stored in the
             * document, so it doubles as the "compressed bytes" readout. */
            m_sample_editor_compressed_bytes = info.sampleSize;
            detected = info.compressionType;
            const void *const_wave = nullptr;
            if (BAERmfEditorDocument_GetSampleWaveformData(m_document,
                                                           sample.document_sample_index,
                                                           &const_wave,
                                                           &frame_count,
                                                           &bit_size,
                                                           &channels,
                                                           &sample_rate) != BAE_NO_ERROR ||
                !const_wave || frame_count == 0)
            {
                SetStatus("Sample editor: song waveform decode failed");
                return;
            }
            wave_data = const_cast<void *>(const_wave);
        }
        else
        {
            if (!m_bank_token)
            {
                SetStatus("Sample editor: no bank loaded");
                return;
            }

            /* List identity is snd_resource_id — never overwrite from a stale split. */
            m_sample_editor_snd_resource_id = sample.snd_resource_id;
            m_sample_editor_root_key = sample.root_key;
            m_sample_editor_loop_start = sample.loop_start;
            m_sample_editor_loop_end = sample.loop_end;
            m_sample_editor_frame_count = sample.frame_count;
            m_sample_editor_sample_rate_hz = sample.sample_rate_hz;
            m_sample_editor_channels = sample.channels;
            m_sample_editor_bit_depth = sample.bit_depth;

            BAERmfEditorBankSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            uint32_t resolved_inst = sample.instrument_index;
            uint32_t resolved_split = sample.split_index;
            bool have_split_info = false;
            if (!sample.unassigned && sample.snd_resource_id != kNoSndResourceId)
            {
                /* Prefer a live bank scan by SND; fall back to row indices only
                 * when that split still points at the same SND. */
                if (ResolveBankSplitForSnd(sample.snd_resource_id,
                                           &resolved_inst,
                                           &resolved_split,
                                           &info))
                {
                    have_split_info = true;
                }
                else if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                                  sample.instrument_index,
                                                                  sample.split_index,
                                                                  &info) == BAE_NO_ERROR &&
                         static_cast<uint16_t>(info.sndResourceID) ==
                             static_cast<uint16_t>(sample.snd_resource_id))
                {
                    resolved_inst = sample.instrument_index;
                    resolved_split = sample.split_index;
                    have_split_info = true;
                }
            }
            if (have_split_info)
            {
                m_sample_editor_loop_start = info.loopStart;
                m_sample_editor_loop_end = info.loopEnd;
                m_sample_editor_frame_count = info.frameCount;
                m_sample_editor_sample_rate_hz = info.sampleRate;
                m_sample_editor_channels = info.channels;
                m_sample_editor_bit_depth = info.bitDepth;
                m_sample_editor_snd_storage_type = info.sndStorageType;
                m_sample_editor_root_key = static_cast<int>(info.rootKey);
                m_sample_editor_volume = DisplayInstVolume(info.splitVolume);
                {
                    BAERmfEditorBankInstrumentInfo vol_ii;
                    std::memset(&vol_ii, 0, sizeof(vol_ii));
                    m_sample_editor_volume_editable =
                        BAERmfEditorBank_GetInstrumentInfo(m_bank_token, resolved_inst, &vol_ii) ==
                            BAE_NO_ERROR &&
                        !TEST_FLAG_VALUE(vol_ii.flags2, ZBF_enableSampleOffsetStart) &&
                        !TEST_FLAG_VALUE(vol_ii.flags2, ZBF_enableSoundModifier);
                }
                detected = BankCompressionFromCodec(info.compressionType, info.compressionSubType);
                /* Keep list SND id — do not adopt info.sndResourceID from a mismatch. */
                m_sample_editor_snd_resource_id = sample.snd_resource_id;
                /* Refresh cached GetIndexed pointers on the list row. */
                m_samples[static_cast<size_t>(m_selected_sample)].instrument_index =
                    resolved_inst;
                m_samples[static_cast<size_t>(m_selected_sample)].split_index =
                    resolved_split;
            }

            /* Decode by SND id so a stale instrument_index cannot load the wrong PCM. */
            BAEResult wave_r = BAE_BAD_FILE;
            if (sample.snd_resource_id != kNoSndResourceId)
            {
                wave_r = BAERmfEditorBank_GetSndWaveformData(m_bank_token,
                                                            sample.snd_resource_id,
                                                            &wave_data,
                                                            &frame_count,
                                                            &bit_size,
                                                            &channels,
                                                            &sample_rate);
            }
            if ((wave_r != BAE_NO_ERROR || !wave_data || frame_count == 0) &&
                have_split_info)
            {
                if (wave_data)
                {
                    BAERmfEditorBank_FreeWaveformData(wave_data);
                    wave_data = nullptr;
                }
                wave_r = BAERmfEditorBank_GetSampleWaveformData(m_bank_token,
                                                               resolved_inst,
                                                               resolved_split,
                                                               &wave_data,
                                                               &frame_count,
                                                               &bit_size,
                                                               &channels,
                                                               &sample_rate);
            }
            if (wave_r != BAE_NO_ERROR || !wave_data || frame_count == 0)
            {
                if (wave_data)
                {
                    BAERmfEditorBank_FreeWaveformData(wave_data);
                }
                SetStatus("Sample editor: waveform decode failed");
                return;
            }

            /* Unassigned / no-INST path: list row may still have defaults; prefer
             * values from the decoded SND waveform. */
            if (!have_split_info && sample_rate != 0)
            {
                const uint32_t hz = (static_cast<uint32_t>(sample_rate) >= (1000u << 16))
                                        ? static_cast<uint32_t>(sample_rate >> 16)
                                        : static_cast<uint32_t>(sample_rate);
                if (hz > 0)
                {
                    m_sample_editor_sample_rate_hz = hz;
                }
            }
            if (!have_split_info)
            {
                if (frame_count > 0)
                {
                    m_sample_editor_frame_count = frame_count;
                }
                if (bit_size > 0)
                {
                    m_sample_editor_bit_depth = static_cast<int>(bit_size);
                }
                if (channels > 0)
                {
                    m_sample_editor_channels = static_cast<int>(channels);
                }
            }
        }

        m_sample_editor_codec_type = detected;
        /* Pending export encode prefs override bank PCM SND display. */
        if (!m_sample_editor_is_song_sample &&
            m_sample_editor_snd_resource_id != kNoSndResourceId)
        {
            auto pref = m_session_pcm_cache.find(m_sample_editor_snd_resource_id);
            if (pref != m_session_pcm_cache.end() && pref->second.has_export_target)
            {
                m_sample_editor_codec_type = pref->second.export_compression;
                m_sample_editor_snd_storage_type = pref->second.export_storage;
                m_sample_editor_opus_mode = pref->second.export_opus_mode;
                detected = pref->second.export_compression;
            }
        }
        m_sample_editor_loop_enabled = (m_sample_editor_loop_end > m_sample_editor_loop_start);

        size_t option_count = 0;
        const SampleCodecOption *options = GetSampleCodecOptions(&option_count);
        m_sample_editor_codec_index = 0; // No Recompression default
        for (size_t i = 0; i < option_count; ++i)
        {
            if (options[i].type == BAE_EDITOR_COMPRESSION_DONT_CHANGE)
            {
                m_sample_editor_codec_index = static_cast<int>(i);
                break;
            }
        }
        /* Prefer matching the pending/export codec in the combo when set. */
        if (detected != BAE_EDITOR_COMPRESSION_PCM &&
            detected != BAE_EDITOR_COMPRESSION_DONT_CHANGE)
        {
            for (size_t i = 0; i < option_count; ++i)
            {
                if (options[i].type == detected)
                {
                    m_sample_editor_codec_index = static_cast<int>(i);
                    break;
                }
            }
        }
        m_sample_editor_player20_compat = false;

        const uint32_t bytes_per_sample = static_cast<uint32_t>((bit_size / 8u) * channels);
        const uint32_t pcm_size = frame_count * bytes_per_sample;
        m_sample_editor_cached_pcm.resize(pcm_size);
        std::memcpy(m_sample_editor_cached_pcm.data(), wave_data, pcm_size);
        m_sample_editor_cached_frame_count = frame_count;
        m_sample_editor_cached_bit_size = bit_size;
        m_sample_editor_cached_channels = channels;
        m_sample_editor_cached_sample_rate = sample_rate;
        m_sample_editor_original_pcm = m_sample_editor_cached_pcm;
        m_sample_editor_original_frame_count = m_sample_editor_cached_frame_count;
        m_sample_editor_original_bit_size = m_sample_editor_cached_bit_size;
        m_sample_editor_original_channels = m_sample_editor_cached_channels;
        m_sample_editor_original_sample_rate = m_sample_editor_cached_sample_rate;
        m_sample_editor_original_codec_type = m_sample_editor_codec_type;
        m_sample_editor_original_snd_storage_type = m_sample_editor_snd_storage_type;
        m_sample_editor_original_opus_mode = m_sample_editor_opus_mode;

        size_t preset_count = 0;
        const int *presets = SeSampleRatePresets(&preset_count);
        m_sample_editor_rate_preset = static_cast<int>(preset_count); // "Other"
        for (size_t i = 0; i < preset_count; ++i)
        {
            if (static_cast<uint32_t>(presets[i]) == m_sample_editor_sample_rate_hz)
            {
                m_sample_editor_rate_preset = static_cast<int>(i);
                break;
            }
        }

        if (!m_sample_editor_is_song_sample)
        {
            BAERmfEditorBank_FreeWaveformData(wave_data);
            m_sample_editor_compressed_bytes = QueryBankSampleResourceBytes(sample);
        }

        m_sample_editor_view_start = 0;
        m_sample_editor_view_end = frame_count;
        SeSyncFromCache();
        m_sample_editor_pristine = CaptureSampleEditorState();
        m_sample_editor_has_uncompressed_master =
            (!m_sample_editor_is_song_sample &&
             HasUncompressedMasterForSnd(m_sample_editor_snd_resource_id));
        RefreshSampleEditorUsageText();
        m_sample_editor_open = true;
    }

    bool RestoreUncompressedMasterIntoEditor()
    {
        if (m_sample_editor_is_song_sample || m_sample_editor_snd_resource_id == kNoSndResourceId)
        {
            SetStatus("No session uncompressed master for this sample");
            return false;
        }
        if (!HydrateSessionPcmEntryFromCasd(m_sample_editor_snd_resource_id))
        {
            SetStatus("Uncompressed master not found (session PCM / CaSd)");
            m_sample_editor_has_uncompressed_master = false;
            return false;
        }
        const SessionPcmCacheEntry &entry = m_session_pcm_cache[m_sample_editor_snd_resource_id];
        if (entry.pcm.empty() || entry.frame_count == 0)
        {
            SetStatus("Uncompressed master is empty");
            return false;
        }

        m_sample_editor_original_pcm = entry.pcm;
        m_sample_editor_original_frame_count = entry.frame_count;
        m_sample_editor_original_bit_size = entry.bit_size ? entry.bit_size : 16;
        m_sample_editor_original_channels = entry.channels ? entry.channels : 1;
        m_sample_editor_original_sample_rate =
            entry.sample_rate ? entry.sample_rate : UNSIGNED_LONG_TO_XFIXED(44100);
        m_sample_editor_original_codec_type = BAE_EDITOR_COMPRESSION_PCM;
        m_sample_editor_codec_type = BAE_EDITOR_COMPRESSION_PCM;

        m_sample_editor_cached_pcm = m_sample_editor_original_pcm;
        m_sample_editor_cached_frame_count = m_sample_editor_original_frame_count;
        m_sample_editor_cached_bit_size = m_sample_editor_original_bit_size;
        m_sample_editor_cached_channels = m_sample_editor_original_channels;
        m_sample_editor_cached_sample_rate = m_sample_editor_original_sample_rate;
        m_sample_editor_frame_count = m_sample_editor_original_frame_count;
        m_sample_editor_bit_depth = m_sample_editor_original_bit_size;
        m_sample_editor_channels = m_sample_editor_original_channels;
        m_sample_editor_sample_rate_hz =
            m_sample_editor_original_sample_rate
                ? static_cast<uint32_t>(m_sample_editor_original_sample_rate >> 16)
                : 44100u;
        if (entry.loop_end > entry.loop_start)
        {
            m_sample_editor_loop_start = entry.loop_start;
            m_sample_editor_loop_end = entry.loop_end;
            m_sample_editor_loop_enabled = true;
        }
        if (entry.base_key >= 0 && entry.base_key <= 127)
        {
            m_sample_editor_root_key = entry.base_key;
        }

        size_t option_count = 0;
        const SampleCodecOption *options = GetSampleCodecOptions(&option_count);
        m_sample_editor_codec_index = 0;
        for (size_t i = 0; i < option_count; ++i)
        {
            if (options[i].type == BAE_EDITOR_COMPRESSION_PCM)
            {
                m_sample_editor_codec_index = static_cast<int>(i);
                break;
            }
        }

        m_sample_editor_view_start = 0;
        m_sample_editor_view_end = m_sample_editor_frame_count;
        SeSyncFromCache();
        /* Drop compression audition so SE Play / live remount hear master. */
        m_sample_editor_audition_pcm.clear();
        m_sample_editor_audition_frame_count = 0;
        m_app_compression_cache.erase(m_sample_editor_snd_resource_id);
        /* Deferred compress only sets export prefs — clear that tag now so the
         * sample list drops [MP3]/etc immediately. */
        ClearSessionExportTargetForSnd(m_sample_editor_snd_resource_id);
        m_sample_editor_dirty = true;
        m_sample_editor_has_uncompressed_master = true;
        /* Commit PCM + remount song/IE instruments immediately (same as Apply). */
        if (!ApplySampleEditorChanges(false))
        {
            SetStatus("Restored uncompressed master in editor (Apply failed — press Apply)");
            return false;
        }
        SetStatus("Restored uncompressed master (live preview updated)");
        return true;
    }

    /* ---------------- Apply ---------------- */

    bool ApplySampleEditorChanges(bool closing = false)
    {
        if (m_sample_editor_sample_row < 0 ||
            m_sample_editor_sample_row >= static_cast<int>(m_samples.size()) ||
            m_sample_editor_original_pcm.empty())
        {
            return false;
        }

        const SampleRow sample = m_samples[static_cast<size_t>(m_sample_editor_sample_row)];
        size_t option_count = 0;
        const SampleCodecOption *options = GetSampleCodecOptions(&option_count);
        if (m_sample_editor_codec_index < 0 || m_sample_editor_codec_index >= static_cast<int>(option_count))
        {
            return false;
        }

        const BAERmfEditorCompressionType target = options[static_cast<size_t>(m_sample_editor_codec_index)].type;
        /* "No Recompression" = restore in-memory master as PCM (undo compression trials). */
        const bool restore_master = (target == BAE_EDITOR_COMPRESSION_DONT_CHANGE);
        const BAERmfEditorCompressionType effective =
            restore_master ? BAE_EDITOR_COMPRESSION_PCM : target;
        const bool use_opus_roundtrip = IsOpusCompressionType(effective);

        const uint32_t loop_start = m_sample_editor_loop_enabled ? m_sample_editor_loop_start : 0u;
        const uint32_t loop_end = m_sample_editor_loop_enabled ? m_sample_editor_loop_end : 0u;
        /* Retag uses the editor rate (UI), not the load-time original — otherwise
         * Apply keeps rewriting the old Hz and sample-rate changes never stick. */
        const BAE_UNSIGNED_FIXED apply_rate =
            m_sample_editor_cached_sample_rate
                ? m_sample_editor_cached_sample_rate
                : LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz
                                             ? m_sample_editor_sample_rate_hz
                                             : 44100u);

        if (m_sample_editor_is_song_sample)
        {
            if (!m_document)
            {
                SetStatus("No song document for sample encode");
                return false;
            }

            BAESampleInfo replaced_info;
            std::memset(&replaced_info, 0, sizeof(replaced_info));
            const BAEResult replace_result = BAERmfEditorDocument_ReplaceSampleFromPCM(
                m_document,
                m_sample_editor_document_sample_index,
                m_sample_editor_original_pcm.data(),
                m_sample_editor_original_frame_count,
                m_sample_editor_original_bit_size,
                m_sample_editor_original_channels,
                apply_rate,
                loop_start,
                loop_end,
                &replaced_info);
            if (replace_result != BAE_NO_ERROR)
            {
                SetStatus(std::string("Song sample replace failed: ") + FormatBAEError(replace_result));
                return false;
            }

            BAERmfEditorSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorDocument_GetSampleInfo(m_document, m_sample_editor_document_sample_index, &info) != BAE_NO_ERROR)
            {
                SetStatus("Song sample info read failed");
                return false;
            }
            info.compressionType = effective;
            info.opusRoundTripResample = use_opus_roundtrip ? TRUE : FALSE;
            info.sndStorageType = m_sample_editor_snd_storage_type;
            info.opusMode = m_sample_editor_opus_mode;
            info.rootKey = static_cast<unsigned char>(std::clamp(m_sample_editor_root_key, 0, 127));
            if (m_sample_editor_volume_editable)
            {
                info.splitVolume = static_cast<int16_t>(m_sample_editor_volume);
            }
            info.sampleInfo.startLoop = loop_start;
            info.sampleInfo.endLoop = loop_end;
            info.sampleInfo.sampledRate = apply_rate;

            std::string display_name = m_sample_editor_sample_name;
            const std::string song_prefix = "[Song] ";
            if (display_name.size() >= song_prefix.size() &&
                display_name.compare(0, song_prefix.size(), song_prefix) == 0)
            {
                display_name.erase(0, song_prefix.size());
            }
            info.displayName = display_name.c_str();

            const BAEResult set_result = BAERmfEditorDocument_SetSampleInfo(
                m_document, m_sample_editor_document_sample_index, &info);
            if (set_result != BAE_NO_ERROR)
            {
                SetStatus(std::string("Song sample update failed: ") + FormatBAEError(set_result));
                return false;
            }

            uint32_t asset_id = 0;
            if (BAERmfEditorDocument_GetSampleAssetIDForSample(m_document,
                                                              m_sample_editor_document_sample_index,
                                                              &asset_id) == BAE_NO_ERROR)
            {
                (void)BAERmfEditorDocument_SetSampleAssetCompression(m_document, asset_id, effective);
            }
            m_sample_editor_codec_type = effective;
            m_sample_editor_original_sample_rate = apply_rate;
            m_sample_editor_sample_rate_hz =
                static_cast<uint32_t>(apply_rate >> 16);
            SeRestoreCacheFromMaster();

            BAERmfEditorSampleInfo refreshed;
            std::memset(&refreshed, 0, sizeof(refreshed));
            if (BAERmfEditorDocument_GetSampleInfo(m_document,
                                                   m_sample_editor_document_sample_index,
                                                   &refreshed) == BAE_NO_ERROR)
            {
                m_sample_editor_compressed_bytes = refreshed.sampleSize;
            }

            m_document_dirty = true;
            m_sample_editor_preview_needs_reencode = false;
            m_sample_editor_loop_dirty = false;
            m_sample_editor_dirty = false;
            RefreshSongSamplesFromDocument();
            SampleEditorSyncAuditionAfterApply(effective);
            SetStatus(closing ? "Song sample saved (editor closed)"
                              : (restore_master ? "Restored master PCM (no compression)"
                                                : "Song sample compression set for export"));
            return true;
        }

        if (!m_bank_token)
        {
            return false;
        }

        /* UI / export prefs keep the user's chosen codec+storage. */
        m_sample_editor_codec_type = effective;

        /* Codec/storage Apply is an export preference. Skip bank I/O unless the
         * working-set sample itself changed — forcing ESND/CSND→SND used to run
         * a full-bank SND rebuild (multi-second freeze on zpatches). */
        const uint32_t keep_snd = m_sample_editor_snd_resource_id;
        uint32_t work_inst_index = sample.instrument_index;
        uint32_t keep_split = sample.split_index;
        BAERmfEditorBankSampleInfo bank_info;
        std::memset(&bank_info, 0, sizeof(bank_info));
        bool have_bank_info = false;
        if (!sample.unassigned && keep_snd != kNoSndResourceId)
        {
            /* Re-find INST/split by SND — SampleRow indices go stale after XReplace. */
            if (ResolveBankSplitForSnd(keep_snd, &work_inst_index, &keep_split, &bank_info))
            {
                have_bank_info = true;
            }
            else if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                              sample.instrument_index,
                                                              sample.split_index,
                                                              &bank_info) == BAE_NO_ERROR &&
                     static_cast<uint16_t>(bank_info.sndResourceID) ==
                         static_cast<uint16_t>(keep_snd))
            {
                work_inst_index = sample.instrument_index;
                keep_split = sample.split_index;
                have_bank_info = true;
            }
        }
        const uint32_t want_root =
            static_cast<uint32_t>(std::clamp(m_sample_editor_root_key, 0, 127));
        const bool keymap_dirty =
            have_bank_info &&
            (bank_info.loopStart != loop_start || bank_info.loopEnd != loop_end ||
             static_cast<uint32_t>(bank_info.rootKey) != want_root ||
             (m_sample_editor_volume_editable &&
              DisplayInstVolume(bank_info.splitVolume) != m_sample_editor_volume) ||
             bank_info.sampleRate != m_sample_editor_sample_rate_hz);
        const bool name_dirty =
            !m_sample_editor_sample_name.empty() &&
            m_sample_editor_sample_name != sample.name;
        const bool bank_write_needed =
            restore_master || m_sample_editor_dirty || m_sample_editor_loop_dirty ||
            keymap_dirty || name_dirty;

        if (!bank_write_needed)
        {
            SeRestoreCacheFromMaster();
            RememberSessionPcmFromSampleEditor(true);
            /* Prefs-only Apply still creates a session PCM master — without this,
             * "Restore Uncompressed" stays hidden (common for SND id 0 with no CaSd). */
            m_sample_editor_has_uncompressed_master =
                HasUncompressedMasterForSnd(m_sample_editor_snd_resource_id);
            m_sample_editor_preview_needs_reencode = false;
            m_sample_editor_loop_dirty = false;
            m_sample_editor_dirty = false;
            {
                const std::string label = FormatVisibleCodecLabelFromEditorType(effective);
                const uint32_t snd_id = m_sample_editor_snd_resource_id;
                for (SampleRow &row : m_samples)
                {
                    if (row.source == SampleSource::Bank &&
                        (row.snd_resource_id == snd_id ||
                         (m_sample_editor_sample_row >= 0 &&
                          &row == &m_samples[static_cast<size_t>(m_sample_editor_sample_row)])))
                    {
                        row.codec_label = label;
                    }
                }
            }
            if (sample.bank >= 0 && sample.bank < 2)
            {
                m_bank01_touched = true;
            }
            SampleEditorSyncAuditionAfterApply(effective);
            if (have_bank_info)
            {
                uint32_t prefs_inst_id = BAE_EDITOR_INST_ID_NONE;
                BAERmfEditorBankInstrumentInfo prefs_iinfo{};
                if (BAERmfEditorBank_GetInstrumentInfo(m_bank_token,
                                                       work_inst_index,
                                                       &prefs_iinfo) == BAE_NO_ERROR)
                {
                    prefs_inst_id = prefs_iinfo.instID;
                }
                SampleEditorHotReloadLivePreview(work_inst_index,
                                                keep_split,
                                                m_sample_editor_snd_resource_id,
                                                prefs_inst_id,
                                                effective,
                                                /*bank_pcm_rewritten=*/false);
            }
            SetStatus(closing ? "Compression set for export (editor closed)"
                              : (SeCodecIsUncompressedPcm(effective)
                                     ? "Restored PCM for export (no deferred compression)"
                                     : (std::string("Compression set for export: ") +
                                        GetSampleCodecLabel(m_sample_editor_codec_type))));
            return true;
        }

        if (!EnsureActiveBankWritable())
        {
            SetStatus("Sample write failed: bank not writable");
            return false;
        }

        /* XReplace on INST is trash+append — GetIndexed order shifts. Pin by
         * INST id / SND id; never re-resolve the editor from a stale index. */
        uint32_t keep_inst_id = BAE_EDITOR_INST_ID_NONE;
        if (have_bank_info)
        {
            BAERmfEditorBankInstrumentInfo iinfo;
            std::memset(&iinfo, 0, sizeof(iinfo));
            if (BAERmfEditorBank_GetInstrumentInfo(m_bank_token, work_inst_index, &iinfo) ==
                BAE_NO_ERROR)
            {
                keep_inst_id = iinfo.instID;
            }
        }
        auto sync_work_inst_index = [&]() {
            if (keep_inst_id == BAE_EDITOR_INST_ID_NONE || !m_bank_token)
            {
                return;
            }
            uint32_t resolved = 0;
            uint32_t index = work_inst_index;
            if (BAERmfEditorBank_ResolveInstID(m_bank_token, keep_inst_id, &resolved, &index) ==
                BAE_NO_ERROR)
            {
                work_inst_index = index;
            }
        };

        /* CSND compresses a plain SND. Never encrypt (ESND) first — wrapping
         * ciphertext is pointless. ReEncode writes PCM as SND/ESND, or SND then
         * CSND compress, matching the requested type. */
        BAERmfEditorSndStorageType want_storage = m_sample_editor_snd_storage_type;

        if (sample.unassigned || !have_bank_info)
        {
            /* Orphan SNDs / unresolved splits: never ReEncode through a stale
             * instrument_index (that rewrites an unrelated sample). */
            SeRestoreCacheFromMaster();
            RememberSessionPcmFromSampleEditor(true);
            if (name_dirty && keep_snd <= 32767u)
            {
                (void)BAERmfEditorBank_RenameSampleResource(
                    m_bank_token,
                    static_cast<XShortResourceID>(keep_snd),
                    m_sample_editor_sample_name.c_str());
            }
            m_sample_editor_preview_needs_reencode = false;
            m_sample_editor_loop_dirty = false;
            m_sample_editor_dirty = false;
            RefreshListsFromBank();
            m_sample_editor_sample_row = -1;
            for (size_t i = 0; i < m_samples.size(); ++i)
            {
                if (m_samples[i].source == SampleSource::Bank &&
                    m_samples[i].snd_resource_id == keep_snd)
                {
                    m_sample_editor_sample_row = static_cast<int>(i);
                    break;
                }
            }
            RefreshSampleEditorUsageText();
            SampleEditorSyncAuditionAfterApply(effective);
            if (sample.unassigned)
            {
                SetStatus(closing ? "Unassigned sample prefs saved (editor closed)"
                                  : "Unassigned sample: export prefs saved (PCM bank write needs an instrument link)");
            }
            else
            {
                SetStatus("Sample prefs saved; could not resolve instrument split for bank PCM write");
            }
            return true;
        }

        const BAERmfEditorCompressionType bank_codec = BAE_EDITOR_COMPRESSION_PCM;
        const BAEResult result = BAERmfEditorBank_ReEncodeSampleFromPCMEx(
            m_bank_token,
            work_inst_index,
            keep_split,
            bank_codec,
            want_storage,
            BAE_EDITOR_OPUS_MODE_AUDIO,
            FALSE,
            m_sample_editor_original_pcm.data(),
            m_sample_editor_original_frame_count,
            m_sample_editor_original_bit_size,
            m_sample_editor_original_channels,
            apply_rate);

        if (result != BAE_NO_ERROR)
        {
            SetStatus(std::string("Sample write failed: ") + FormatBAEError(result));
            return false;
        }

        /* SND id is stable across ReEncode; keep the editor's id (uint16). */
        m_sample_editor_snd_resource_id = keep_snd;
        m_sample_editor_original_snd_storage_type = want_storage;

        if (keymap_dirty || m_sample_editor_loop_dirty)
        {
            BAERmfEditorBankSampleInfo updated_info;
            std::memset(&updated_info, 0, sizeof(updated_info));
            if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                         work_inst_index,
                                                         keep_split,
                                                         &updated_info) == BAE_NO_ERROR)
            {
                const uint32_t frames = (updated_info.frameCount > 0)
                                            ? updated_info.frameCount
                                            : m_sample_editor_original_frame_count;
                const uint32_t max_frame = (frames > 0) ? (frames - 1u) : 0u;
                const uint32_t applied_start = std::min<uint32_t>(loop_start, max_frame);
                uint32_t applied_end = std::min<uint32_t>(loop_end, frames);
                if (applied_end < applied_start)
                {
                    applied_end = applied_start;
                }

                updated_info.loopStart = applied_start;
                updated_info.loopEnd = applied_end;
                updated_info.rootKey = static_cast<unsigned char>(want_root);
                if (m_sample_editor_volume_editable)
                {
                    updated_info.splitVolume = static_cast<int16_t>(m_sample_editor_volume);
                }
                /* Preserve SND linkage — never let a zeroed/stale field remint. */
                updated_info.sndResourceID =
                    static_cast<XShortResourceID>(static_cast<uint16_t>(keep_snd));
                /* Root is always the SND baseKey (mirrored into misc when
                 * useSoundModifierAsRootKey is set). */
                const bool snd_meta_dirty =
                    !have_bank_info ||
                    bank_info.sampleRate != m_sample_editor_sample_rate_hz ||
                    bank_info.loopStart != applied_start ||
                    bank_info.loopEnd != applied_end ||
                    static_cast<uint32_t>(bank_info.rootKey) != want_root;
                updated_info.sampleRate =
                    snd_meta_dirty ? m_sample_editor_sample_rate_hz : 0u;
                const BAEResult info_result = BAERmfEditorBank_SetInstrumentSampleInfo(
                    m_bank_token, work_inst_index, keep_split, &updated_info);
                if (info_result != BAE_NO_ERROR)
                {
                    SetStatus(std::string("Sample info update failed: ") +
                              FormatBAEError(info_result));
                    return false;
                }
                /* INST XReplace shifts GetIndexed order. */
                sync_work_inst_index();
                /* Single-sample INST: root/volume are header misc words — keep an
                 * open Instrument Editor from writing its stale copy back. */
                MirrorFlatInstSampleFieldsToInstrumentEditor(
                    keep_inst_id,
                    static_cast<int>(want_root),
                    m_sample_editor_volume_editable ? m_sample_editor_volume : -1);

                if (m_sample_editor_loop_enabled)
                {
                    m_sample_editor_loop_start = applied_start;
                    m_sample_editor_loop_end = applied_end;
                }
            }
        }

        /* Commit retag rate into master before restoring the waveform cache. */
        m_sample_editor_original_sample_rate = apply_rate;
        m_sample_editor_sample_rate_hz = static_cast<uint32_t>(apply_rate >> 16);
        SeRestoreCacheFromMaster();
        {
            SampleRow refreshed = sample;
            refreshed.snd_resource_id = m_sample_editor_snd_resource_id;
            m_sample_editor_compressed_bytes = QueryBankSampleResourceBytes(refreshed);
        }

        /* Persist SND resource name only when it actually changed. */
        if (name_dirty && m_sample_editor_snd_resource_id <= 32767u)
        {
            const BAEResult rename_r = BAERmfEditorBank_RenameSampleResource(
                m_bank_token,
                static_cast<XShortResourceID>(m_sample_editor_snd_resource_id),
                m_sample_editor_sample_name.c_str());
            if (rename_r != BAE_NO_ERROR)
            {
                SetStatus(std::string("Sample saved but rename failed: ") + FormatBAEError(rename_r));
            }
        }

        m_sample_editor_preview_needs_reencode = false;
        m_sample_editor_loop_dirty = false;
        m_sample_editor_dirty = false;

        /* Capture export prefs before list rebuild — RefreshListsFromBank reads
         * m_session_pcm_cache for the visible [codec] tag. */
        RememberSessionPcmFromSampleEditor(true);

        sync_work_inst_index();
        RefreshListsFromBank();
        /* Rebind by SND id (stable). Prefer the row on the same INST/split. */
        m_sample_editor_sample_row = -1;
        int snd_fallback = -1;
        for (size_t i = 0; i < m_samples.size(); ++i)
        {
            const SampleRow &row = m_samples[i];
            if (row.source != SampleSource::Bank || row.snd_resource_id != keep_snd)
            {
                continue;
            }
            if (snd_fallback < 0)
            {
                snd_fallback = static_cast<int>(i);
            }
            if (!row.unassigned && row.instrument_index == work_inst_index &&
                row.split_index == keep_split)
            {
                m_sample_editor_sample_row = static_cast<int>(i);
                break;
            }
        }
        if (m_sample_editor_sample_row < 0)
        {
            m_sample_editor_sample_row = snd_fallback;
        }
        {
            const std::string label = FormatVisibleCodecLabelFromEditorType(effective);
            for (SampleRow &row : m_samples)
            {
                if (row.source == SampleSource::Bank && row.snd_resource_id == keep_snd)
                {
                    row.codec_label = label;
                }
            }
        }
        RefreshSampleEditorUsageText();
        SampleEditorSyncAuditionAfterApply(effective);
        SampleEditorHotReloadLivePreview(work_inst_index,
                                        keep_split,
                                        keep_snd,
                                        keep_inst_id,
                                        effective,
                                        /*bank_pcm_rewritten=*/true);
        m_sample_editor_has_uncompressed_master =
            HasUncompressedMasterForSnd(m_sample_editor_snd_resource_id);
        if (sample.bank >= 0 && sample.bank < 2)
        {
            m_bank01_touched = true;
        }
        SetStatus(closing ? "Sample saved to bank (editor closed)"
                          : (restore_master
                                 ? "Restored PCM master in bank (no compression)"
                                 : (std::string("Sample saved; ") +
                                    GetSampleCodecLabel(m_sample_editor_codec_type) +
                                    " / storage set for export")));
        return true;
    }

    /* Compatibility wrapper for call sites that predate Phase 2. */
    bool ApplySampleEditorEncodingToBank()
    {
        return ApplySampleEditorChanges(false);
    }

    /* ---------------- Preview (multi-voice, pitched like OpenMPT) ---------------- */

    void SeReleasePreviewVoice(SePreviewVoice &voice)
    {
        if (voice.sound)
        {
            /* Drain the mixer slice before returning — plain Stop clears
             * voiceRef and races FreeWaveform on the next Load. */
            (void)BAESound_StopAndWait(voice.sound);
        }
        voice.active = false;
        voice.midi_note = -1;
        voice.started_ms = 0;
        voice.playback_hz = 0.0;
        voice.buffer_frames = 0;
        voice.looping = false;
        voice.loop_start = 0;
        voice.loop_end = 0;
        voice.playhead_frame = 0;
    }

    void StopSampleEditorPreview()
    {
        for (SePreviewVoice &voice : m_se_preview_voices)
        {
            SeReleasePreviewVoice(voice);
        }
        m_sample_editor_playing = false;
        m_sample_editor_playhead_frame = 0;
    }

    void StopSampleEditorNote(int midi_note)
    {
        if (midi_note < 0 || midi_note > 127)
        {
            return;
        }
        bool any = false;
        for (SePreviewVoice &voice : m_se_preview_voices)
        {
            if (voice.active && voice.midi_note == midi_note)
            {
                SeReleasePreviewVoice(voice);
            }
            else if (voice.active)
            {
                any = true;
            }
        }
        m_sample_editor_playing = any;
        if (!any)
        {
            m_sample_editor_playhead_frame = 0;
        }
    }

    SePreviewVoice *SeAcquirePreviewVoice(int midi_note)
    {
        /* Retrigger same note on its existing voice. */
        for (SePreviewVoice &voice : m_se_preview_voices)
        {
            if (voice.active && voice.midi_note == midi_note)
            {
                return &voice;
            }
        }
        for (SePreviewVoice &voice : m_se_preview_voices)
        {
            if (voice.sound && !voice.active)
            {
                return &voice;
            }
        }
        /* Steal oldest. */
        SePreviewVoice *oldest = nullptr;
        for (SePreviewVoice &voice : m_se_preview_voices)
        {
            if (!voice.sound)
            {
                continue;
            }
            if (!oldest || voice.started_ms < oldest->started_ms)
            {
                oldest = &voice;
            }
        }
        if (oldest)
        {
            SeReleasePreviewVoice(*oldest);
        }
        return oldest;
    }

    uint32_t QueryBankSampleResourceBytes(const SampleRow &sample) const
    {
        if (!m_bank_token || sample.snd_resource_id == kNoSndResourceId)
        {
            return 0;
        }
        static const XResourceType snd_types[] = {ID_ESND, ID_CSND, ID_SND};
        for (XResourceType type : snd_types)
        {
            int32_t size = 0;
            XPTR data = XGetFileResource(reinterpret_cast<XFILE>(m_bank_token),
                                         type,
                                         static_cast<XLongResourceID>(sample.snd_resource_id),
                                         nullptr,
                                         &size);
            if (data)
            {
                XDisposePtr(data);
                if (size > 0)
                {
                    return static_cast<uint32_t>(size);
                }
            }
        }
        return 0;
    }

    void ClearSampleEditorSndSnapshot()
    {
        m_sample_editor_snd_snapshot_valid = false;
        m_sample_editor_snd_snapshot_data.clear();
        m_sample_editor_snd_snapshot_name.clear();
        m_sample_editor_snd_snapshot_id = static_cast<XLongResourceID>(kNoSndResourceId);
        m_sample_editor_snd_snapshot_type = ID_ESND;
    }

    bool CaptureSampleEditorSndSnapshot(uint32_t snd_resource_id)
    {
        ClearSampleEditorSndSnapshot();
        if (!m_bank_token || snd_resource_id == kNoSndResourceId)
        {
            return false;
        }
        static const XResourceType snd_types[] = {ID_ESND, ID_CSND, ID_SND};
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        for (XResourceType type : snd_types)
        {
            int32_t size = 0;
            char pascal_name[256];
            std::memset(pascal_name, 0, sizeof(pascal_name));
            XPTR data = XGetFileResource(bank,
                                         type,
                                         static_cast<XLongResourceID>(snd_resource_id),
                                         pascal_name,
                                         &size);
            if (!data || size <= 0)
            {
                if (data)
                {
                    XDisposePtr(data);
                }
                continue;
            }
            m_sample_editor_snd_snapshot_data.resize(static_cast<size_t>(size));
            std::memcpy(m_sample_editor_snd_snapshot_data.data(), data, static_cast<size_t>(size));
            XDisposePtr(data);
            m_sample_editor_snd_snapshot_type = type;
            m_sample_editor_snd_snapshot_id = static_cast<XLongResourceID>(snd_resource_id);
            if (pascal_name[0] > 0)
            {
                const unsigned char n = static_cast<unsigned char>(pascal_name[0]);
                m_sample_editor_snd_snapshot_name.assign(pascal_name + 1, pascal_name + 1 + n);
            }
            else
            {
                char c_name[256] = {0};
                if (XGetFileResourceName(bank,
                                         type,
                                         static_cast<XLongResourceID>(snd_resource_id),
                                         c_name))
                {
                    m_sample_editor_snd_snapshot_name = c_name;
                }
            }
            m_sample_editor_snd_snapshot_valid = true;
            return true;
        }
        return false;
    }

    bool RestoreSampleEditorSndSnapshot()
    {
        if (!m_sample_editor_snd_snapshot_valid || !m_bank_token ||
            m_sample_editor_snd_snapshot_data.empty())
        {
            return false;
        }
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        static const XResourceType snd_types[] = {ID_ESND, ID_CSND, ID_SND};
        for (XResourceType type : snd_types)
        {
            (void)XDeleteFileResource(bank, type, m_sample_editor_snd_snapshot_id, false);
        }
        char pascal_name[256];
        pascal_name[0] = 0;
        if (!m_sample_editor_snd_snapshot_name.empty())
        {
            const size_t n = std::min<size_t>(m_sample_editor_snd_snapshot_name.size(), 255);
            pascal_name[0] = static_cast<char>(n);
            std::memcpy(pascal_name + 1, m_sample_editor_snd_snapshot_name.data(), n);
        }
        const int32_t added = XAddFileResource(bank,
                                               m_sample_editor_snd_snapshot_type,
                                               m_sample_editor_snd_snapshot_id,
                                               pascal_name[0] ? pascal_name : nullptr,
                                               m_sample_editor_snd_snapshot_data.data(),
                                               static_cast<int32_t>(m_sample_editor_snd_snapshot_data.size()));
        if (added != 0)
        {
            SetStatus("Failed to restore sample after compression preview");
            return false;
        }
        return true;
    }

    bool ReloadSampleEditorPcmFromBank(const SampleRow &sample)
    {
        if (!m_bank_token)
        {
            return false;
        }
        void *wave_data = nullptr;
        uint32_t frame_count = 0;
        uint16_t bit_size = 0;
        uint16_t channels = 0;
        BAE_UNSIGNED_FIXED sample_rate = 0;
        if (BAERmfEditorBank_GetSampleWaveformData(m_bank_token,
                                                   sample.instrument_index,
                                                   sample.split_index,
                                                   &wave_data,
                                                   &frame_count,
                                                   &bit_size,
                                                   &channels,
                                                   &sample_rate) != BAE_NO_ERROR ||
            !wave_data || frame_count == 0)
        {
            if (wave_data)
            {
                BAERmfEditorBank_FreeWaveformData(wave_data);
            }
            return false;
        }

        const uint32_t bytes_per_sample = static_cast<uint32_t>((bit_size / 8u) * channels);
        const uint32_t pcm_size = frame_count * bytes_per_sample;
        m_sample_editor_cached_pcm.resize(pcm_size);
        std::memcpy(m_sample_editor_cached_pcm.data(), wave_data, pcm_size);
        BAERmfEditorBank_FreeWaveformData(wave_data);

        m_sample_editor_cached_frame_count = frame_count;
        m_sample_editor_cached_bit_size = bit_size;
        m_sample_editor_cached_channels = channels;
        m_sample_editor_cached_sample_rate = sample_rate;
        m_sample_editor_frame_count = frame_count;
        m_sample_editor_bit_depth = static_cast<int>(bit_size);
        m_sample_editor_channels = static_cast<int>(channels);
        if (m_sample_editor_view_end > frame_count || m_sample_editor_view_start >= frame_count)
        {
            m_sample_editor_view_start = 0;
            m_sample_editor_view_end = frame_count;
        }

        BAERmfEditorBankSampleInfo info;
        std::memset(&info, 0, sizeof(info));
        if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                     sample.instrument_index,
                                                     sample.split_index,
                                                     &info) == BAE_NO_ERROR)
        {
            m_sample_editor_codec_type = BankCompressionFromCodec(info.compressionType, info.compressionSubType);
            m_sample_editor_snd_storage_type = info.sndStorageType;
            m_sample_editor_snd_resource_id = static_cast<uint32_t>(info.sndResourceID);
            m_sample_editor_compressed_bytes = QueryBankSampleResourceBytes(sample);
            if (m_sample_editor_compressed_bytes == 0)
            {
                /* snd id may have changed — retry with refreshed id */
                SampleRow refreshed = sample;
                refreshed.snd_resource_id = m_sample_editor_snd_resource_id;
                m_sample_editor_compressed_bytes = QueryBankSampleResourceBytes(refreshed);
            }
        }

        RebuildSampleEditorWaveformBins();
        return true;
    }

    enum class SePreviewSource : int
    {
        Master = 0,   /* in-memory uncompressed master */
        Cache = 1,    /* current waveform cache */
        Audition = 2, /* compression round-trip buffer */
    };

    static bool SampleEditorCodecUsesAudition(BAERmfEditorCompressionType type)
    {
        return type != BAE_EDITOR_COMPRESSION_PCM &&
               type != BAE_EDITOR_COMPRESSION_DONT_CHANGE;
    }

    /* After Apply, bank stays PCM — live Play/keys use the decode buffer. */
    SePreviewSource SampleEditorLivePreviewSource() const
    {
        if (SampleEditorCodecUsesAudition(m_sample_editor_codec_type) &&
            !m_sample_editor_audition_pcm.empty() &&
            m_sample_editor_audition_frame_count > 0)
        {
            return SePreviewSource::Audition;
        }
        return SePreviewSource::Cache;
    }

    void StartSampleEditorNote(int midi_note,
                               unsigned char velocity = 100,
                               SePreviewSource source = SePreviewSource::Cache)
    {
        midi_note = std::clamp(midi_note, 0, 127);
        velocity = static_cast<unsigned char>(std::clamp<int>(velocity, 1, 127));
        if (source == SePreviewSource::Cache)
        {
            source = SampleEditorLivePreviewSource();
        }
        /* Export codec is set but bank stays PCM — encode into audition on first play. */
        if (source != SePreviewSource::Master &&
            source != SePreviewSource::Audition &&
            SampleEditorCodecUsesAudition(m_sample_editor_codec_type) &&
            (m_sample_editor_audition_pcm.empty() ||
             m_sample_editor_audition_frame_count == 0))
        {
            if (PrepareCompressionPreviewAudition())
            {
                source = SePreviewSource::Audition;
            }
        }

        const std::vector<unsigned char> *pcm = &m_sample_editor_cached_pcm;
        uint32_t frames = m_sample_editor_cached_frame_count;
        uint16_t bit_size = m_sample_editor_cached_bit_size;
        uint16_t channels = m_sample_editor_cached_channels;
        BAE_UNSIGNED_FIXED native_rate = m_sample_editor_cached_sample_rate;
        const char *status = "Auditioning sample";

        if (source == SePreviewSource::Master)
        {
            pcm = &m_sample_editor_original_pcm;
            frames = m_sample_editor_original_frame_count;
            bit_size = m_sample_editor_original_bit_size;
            channels = m_sample_editor_original_channels;
            /* Retag: honor the editor sample-rate, not the load-time original. */
            native_rate = m_sample_editor_cached_sample_rate
                              ? m_sample_editor_cached_sample_rate
                              : LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz);
            status = "Auditioning original (master)";
        }
        else if (source == SePreviewSource::Audition)
        {
            pcm = &m_sample_editor_audition_pcm;
            frames = m_sample_editor_audition_frame_count;
            bit_size = m_sample_editor_audition_bit_size;
            channels = m_sample_editor_audition_channels;
            native_rate = m_sample_editor_audition_sample_rate;
            status = "Previewing compressed decode";
        }

        SePreviewVoice *voice = SeAcquirePreviewVoice(midi_note);
        if (!voice || !voice->sound || !pcm || pcm->empty() || frames == 0)
        {
            SetStatus("Sample preview unavailable");
            return;
        }

        uint32_t loop_start = 0;
        uint32_t loop_end = 0;
        if (m_sample_editor_loop_enabled)
        {
            loop_start = m_sample_editor_loop_start;
            loop_end = m_sample_editor_loop_end;
            if (source == SePreviewSource::Audition &&
                m_sample_editor_original_frame_count > 0 &&
                frames != m_sample_editor_original_frame_count)
            {
                SeRemapLoopPoints(m_sample_editor_original_frame_count,
                                  frames,
                                  &loop_start,
                                  &loop_end);
            }
            loop_start = std::min<uint32_t>(loop_start, frames);
            loop_end = std::min<uint32_t>(loop_end, frames);
            if (loop_end <= loop_start)
            {
                loop_start = 0;
                loop_end = 0;
            }
        }

        /* Editor PCM cache is unsigned 8-bit (center 128). LoadCustomSample wants signed. */
        std::vector<unsigned char> signed_pcm;
        const unsigned char *load_ptr = pcm->data();
        if (bit_size == 8)
        {
            signed_pcm = *pcm;
            for (unsigned char &b : signed_pcm)
            {
                b = static_cast<unsigned char>(b - 128u);
            }
            load_ptr = signed_pcm.data();
        }

        if (!native_rate)
        {
            native_rate = LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz);
        }
        const double native_hz = std::max(1.0, UNSIGNED_FIXED_TO_FLOAT(native_rate));
        const int root = std::clamp(m_sample_editor_root_key, 0, 127);
        const double pitched_hz =
            native_hz * std::pow(2.0, static_cast<double>(midi_note - root) / 12.0);
        const double clamped_hz = std::clamp(pitched_hz, 100.0, 768000.0);

        /* Do not Stop() before Load — Unload drains correctly on its own. */
        BAEResult result = BAESound_LoadCustomSample(voice->sound,
                                                     const_cast<unsigned char *>(load_ptr),
                                                     frames,
                                                     bit_size,
                                                     channels,
                                                     native_rate,
                                                     loop_start,
                                                     loop_end);
        if (result == BAE_NO_ERROR)
        {
            (void)BAESound_SetRate(voice->sound, FLOAT_TO_UNSIGNED_FIXED(clamped_hz));
            (void)BAESound_SetLoopCount(voice->sound,
                                        (loop_end > loop_start) ? 0xFFFFFFFFu : 0u);
            const double vol = std::max(0.05, static_cast<double>(velocity) / 127.0);
            result = BAESound_Start(voice->sound, 0, FLOAT_TO_UNSIGNED_FIXED(vol), 0);
        }

        if (result == BAE_NO_ERROR)
        {
            voice->active = true;
            voice->midi_note = midi_note;
            voice->started_ms = SDL_GetTicks();
            voice->playback_hz = clamped_hz;
            voice->buffer_frames = frames;
            voice->looping = (loop_end > loop_start);
            voice->loop_start = loop_start;
            voice->loop_end = loop_end;
            voice->playhead_frame = 0;
            m_sample_editor_playing = true;
            m_sample_editor_preview_note = midi_note;
            m_sample_editor_play_started_ms = voice->started_ms;
            SetStatus(status);
        }
        else
        {
            SeReleasePreviewVoice(*voice);
            bool any = false;
            for (const SePreviewVoice &v : m_se_preview_voices)
            {
                if (v.active)
                {
                    any = true;
                    break;
                }
            }
            m_sample_editor_playing = any;
            SetStatus(std::string("Sample preview failed: ") + FormatBAEError(result));
        }
    }

    void StartSampleEditorPreview(SePreviewSource source)
    {
        /* Transport Play: root-key pitch on its own voice (polyphony still allowed). */
        StartSampleEditorNote(m_sample_editor_root_key, 100, source);
    }

    void StartSampleEditorPreview(bool use_original)
    {
        StartSampleEditorPreview(use_original ? SePreviewSource::Master
                                              : SampleEditorLivePreviewSource());
    }

    /* Bank Apply keeps PCM. Keep an existing compressed decode from Preview;
     * do not re-encode here (that froze Apply on large samples / SND 0). */
    void SampleEditorSyncAuditionAfterApply(BAERmfEditorCompressionType effective)
    {
        if (!SampleEditorCodecUsesAudition(effective))
        {
            m_sample_editor_audition_pcm.clear();
            m_sample_editor_audition_frame_count = 0;
            return;
        }
        if (!m_sample_editor_audition_pcm.empty() &&
            m_sample_editor_audition_frame_count > 0)
        {
            return;
        }
        /* No prior Preview — leave audition empty; Play uses master until Preview. */
    }

    /* Hot-reload song/IE instruments after sample Apply without Stop()'ing
     * playback. Bank working-set stays PCM master: when a compression audition
     * exists, briefly write the decode for remount, then restore master. */
    void SampleEditorHotReloadLivePreview(uint32_t work_inst_index,
                                          uint32_t keep_split,
                                          uint32_t keep_snd,
                                          uint32_t keep_inst_id,
                                          BAERmfEditorCompressionType effective,
                                          bool bank_pcm_rewritten)
    {
        if (!m_bank_token || keep_snd == kNoSndResourceId)
        {
            return;
        }

        const bool want_lossy_live =
            SampleEditorCodecUsesAudition(effective) &&
            !m_sample_editor_audition_pcm.empty() &&
            m_sample_editor_audition_frame_count > 0;
        if (!want_lossy_live && !bank_pcm_rewritten)
        {
            return;
        }

        auto sync_inst_index = [&]() {
            if (keep_inst_id == BAE_EDITOR_INST_ID_NONE || !m_bank_token)
            {
                return;
            }
            uint32_t resolved = 0;
            uint32_t index = work_inst_index;
            if (BAERmfEditorBank_ResolveInstID(m_bank_token, keep_inst_id, &resolved, &index) ==
                BAE_NO_ERROR)
            {
                work_inst_index = index;
            }
        };

        auto write_bank_pcm = [&](const void *pcm,
                                  uint32_t frames,
                                  uint16_t bit_size,
                                  uint16_t channels,
                                  BAE_UNSIGNED_FIXED rate) -> bool {
            if (!pcm || frames == 0 || !EnsureActiveBankWritable())
            {
                return false;
            }
            sync_inst_index();
            BAERmfEditorSndStorageType storage = BAE_EDITOR_SND_STORAGE_ESND;
            BAERmfEditorBankSampleInfo sinfo{};
            if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                         work_inst_index,
                                                         keep_split,
                                                         &sinfo) == BAE_NO_ERROR)
            {
                storage = sinfo.sndStorageType;
                if (storage == BAE_EDITOR_SND_STORAGE_CSND)
                {
                    storage = BAE_EDITOR_SND_STORAGE_ESND;
                }
            }
            const bool ok = BAERmfEditorBank_ReEncodeSampleFromPCMEx(
                                m_bank_token,
                                work_inst_index,
                                keep_split,
                                BAE_EDITOR_COMPRESSION_PCM,
                                storage,
                                BAE_EDITOR_OPUS_MODE_AUDIO,
                                FALSE,
                                pcm,
                                frames,
                                bit_size,
                                channels,
                                rate) == BAE_NO_ERROR;
            sync_inst_index();
            return ok;
        };

        if (want_lossy_live)
        {
            const BAE_UNSIGNED_FIXED aud_rate =
                m_sample_editor_audition_sample_rate
                    ? m_sample_editor_audition_sample_rate
                    : LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz
                                                 ? m_sample_editor_sample_rate_hz
                                                 : 44100u);
            if (write_bank_pcm(m_sample_editor_audition_pcm.data(),
                               m_sample_editor_audition_frame_count,
                               m_sample_editor_audition_bit_size,
                               m_sample_editor_audition_channels,
                               aud_rate))
            {
                LiveReloadInstrumentsUsingSnd(keep_snd);
                /* Restore working-set PCM at the editor (retag) rate, not the
                 * load-time original — otherwise Apply's rate change is undone. */
                const BAE_UNSIGNED_FIXED master_rate =
                    m_sample_editor_cached_sample_rate
                        ? m_sample_editor_cached_sample_rate
                        : LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz
                                                     ? m_sample_editor_sample_rate_hz
                                                     : 44100u);
                (void)write_bank_pcm(m_sample_editor_original_pcm.data(),
                                     m_sample_editor_original_frame_count,
                                     m_sample_editor_original_bit_size,
                                     m_sample_editor_original_channels,
                                     master_rate);
                return;
            }
        }

        LiveReloadInstrumentsUsingSnd(keep_snd);
    }

    bool PrepareCompressionPreviewAudition()
    {
        /* Encode from master only (never from a prior round-trip) into audition buffer.
         * Waveform cache stays on master — no progressive attenuation. */
        size_t option_count = 0;
        const SampleCodecOption *options = GetSampleCodecOptions(&option_count);
        if (m_sample_editor_codec_index < 0 || m_sample_editor_codec_index >= static_cast<int>(option_count))
        {
            return false;
        }
        if (m_sample_editor_sample_row < 0 ||
            m_sample_editor_sample_row >= static_cast<int>(m_samples.size()) ||
            m_sample_editor_original_pcm.empty())
        {
            return false;
        }

        const BAERmfEditorCompressionType target = options[static_cast<size_t>(m_sample_editor_codec_index)].type;

        /* Stop any current audition before a (possibly slow) re-encode so the
         * mixer is not still touching the previous waveform when we reload. */
        StopSampleEditorPreview();

        const BAE_UNSIGNED_FIXED editor_rate =
            m_sample_editor_cached_sample_rate
                ? m_sample_editor_cached_sample_rate
                : LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz
                                             ? m_sample_editor_sample_rate_hz
                                             : 44100u);

        if (target == BAE_EDITOR_COMPRESSION_DONT_CHANGE || target == BAE_EDITOR_COMPRESSION_PCM)
        {
            m_sample_editor_audition_pcm = m_sample_editor_original_pcm;
            m_sample_editor_audition_frame_count = m_sample_editor_original_frame_count;
            m_sample_editor_audition_bit_size = m_sample_editor_original_bit_size;
            m_sample_editor_audition_channels = m_sample_editor_original_channels;
            m_sample_editor_audition_sample_rate = editor_rate;
            SeRestoreCacheFromMaster();
            return true;
        }

        /* Song samples (module / RMF embeds) and bank samples share the same
         * PCM→encode→decode audition path; Apply still defers song encode to export. */
        const SampleRow sample = m_samples[static_cast<size_t>(m_sample_editor_sample_row)];
        const uint32_t snapshot_snd_id =
            m_sample_editor_snd_resource_id != kNoSndResourceId ? m_sample_editor_snd_resource_id
                                                               : sample.snd_resource_id;

        const bool use_opus_roundtrip = IsOpusCompressionType(target);
        const BAE_UNSIGNED_FIXED master_rate = editor_rate;

        void *wave_data = nullptr;
        uint32_t frame_count = 0;
        uint16_t bit_size = 0;
        uint16_t channels = 0;
        BAE_UNSIGNED_FIXED sample_rate = 0;
        uint32_t preview_bytes = 0;
        const BAEResult enc = BAERmfEditor_PreviewCompressPCM(
            m_sample_editor_original_pcm.data(),
            m_sample_editor_original_frame_count,
            m_sample_editor_original_bit_size,
            m_sample_editor_original_channels,
            master_rate,
            m_sample_editor_loop_enabled ? m_sample_editor_loop_start : 0u,
            m_sample_editor_loop_enabled ? m_sample_editor_loop_end : 0u,
            static_cast<int16_t>(m_sample_editor_root_key),
            m_sample_editor_sample_rate_hz,
            target,
            m_sample_editor_opus_mode,
            use_opus_roundtrip,
            &wave_data,
            &frame_count,
            &bit_size,
            &channels,
            &sample_rate,
            &preview_bytes);
        if (enc != BAE_NO_ERROR || !wave_data || frame_count == 0)
        {
            if (wave_data)
            {
                BAERmfEditorBank_FreeWaveformData(wave_data);
            }
            SetStatus(std::string("Compression preview encode failed: ") + FormatBAEError(enc));
            return false;
        }

        const uint32_t bytes_per_sample = static_cast<uint32_t>((bit_size / 8u) * channels);
        const uint32_t pcm_size = frame_count * bytes_per_sample;
        m_sample_editor_audition_pcm.resize(pcm_size);
        std::memcpy(m_sample_editor_audition_pcm.data(), wave_data, pcm_size);
        BAERmfEditorBank_FreeWaveformData(wave_data);
        m_sample_editor_audition_frame_count = frame_count;
        m_sample_editor_audition_bit_size = bit_size;
        m_sample_editor_audition_channels = channels;
        m_sample_editor_audition_sample_rate = sample_rate;
        m_sample_editor_compressed_bytes = preview_bytes;

        /* App-side compression preview cache (decoded audition only; not a bank commit).
         * SND ID 0 is valid. */
        m_app_compression_cache[snapshot_snd_id] = m_sample_editor_audition_pcm;

        /* Keep editor waveform on master — only audition buffer holds the decode.
         * Do not mutate m_sample_editor_codec_type / sample list tags from preview. */
        SeRestoreCacheFromMaster();
        return true;
    }

    void SeAdvancePlayhead()
    {
        if (!m_sample_editor_playing)
        {
            return;
        }

        const uint32_t now_ms = SDL_GetTicks();
        bool any = false;
        uint32_t primary_head = 0;
        uint32_t primary_started = 0;

        for (SePreviewVoice &voice : m_se_preview_voices)
        {
            if (!voice.active || !voice.sound)
            {
                continue;
            }

            BAE_BOOL is_done = FALSE;
            if (BAESound_IsDone(voice.sound, &is_done) == BAE_NO_ERROR && is_done)
            {
                SeReleasePreviewVoice(voice);
                continue;
            }

            const uint32_t elapsed_ms = now_ms - voice.started_ms;
            const double hz = std::max(1.0, voice.playback_hz);
            const uint64_t elapsed_frames =
                static_cast<uint64_t>((static_cast<double>(elapsed_ms) * hz) / 1000.0);
            const uint32_t buf = std::max<uint32_t>(voice.buffer_frames, 1);

            if (voice.looping && voice.loop_end > voice.loop_start)
            {
                if (elapsed_frames < voice.loop_end)
                {
                    voice.playhead_frame = static_cast<uint32_t>(elapsed_frames);
                }
                else
                {
                    const uint32_t loop_len = voice.loop_end - voice.loop_start;
                    const uint32_t loop_pos = static_cast<uint32_t>(
                        (elapsed_frames - voice.loop_start) % std::max<uint32_t>(loop_len, 1));
                    voice.playhead_frame = voice.loop_start + loop_pos;
                }
            }
            else
            {
                voice.playhead_frame =
                    static_cast<uint32_t>(std::min<uint64_t>(elapsed_frames, buf));
                if (elapsed_frames >= buf)
                {
                    SeReleasePreviewVoice(voice);
                    continue;
                }
            }

            any = true;
            if (!primary_started || voice.started_ms >= primary_started)
            {
                primary_started = voice.started_ms;
                primary_head = voice.playhead_frame;
            }
        }

        m_sample_editor_playing = any;
        m_sample_editor_playhead_frame = any ? primary_head : 0;
    }

    /* ---------------- Waveform ---------------- */

    float SeFrameToX(uint32_t frame, const ImVec2 &pos, const ImVec2 &size) const
    {
        const uint32_t view_start = m_sample_editor_view_start;
        const uint32_t view_end = std::max<uint32_t>(m_sample_editor_view_end, view_start + 1u);
        const float t = (static_cast<float>(frame) - static_cast<float>(view_start)) /
                        static_cast<float>(view_end - view_start);
        return pos.x + (t * size.x);
    }

    uint32_t SeXToFrame(float x, const ImVec2 &pos, const ImVec2 &size) const
    {
        const uint32_t view_start = m_sample_editor_view_start;
        const uint32_t view_end = std::max<uint32_t>(m_sample_editor_view_end, view_start + 1u);
        float t = (x - pos.x) / std::max(size.x, 1.0f);
        t = std::clamp(t, 0.0f, 1.0f);
        const float frame = static_cast<float>(view_start) + (t * static_cast<float>(view_end - view_start));
        return static_cast<uint32_t>(std::clamp(frame, 0.0f, static_cast<float>(m_sample_editor_frame_count)));
    }

    void SeHandleWaveformInput(const ImVec2 &pos, const ImVec2 &size, float strip_h)
    {
        ImGuiIO &io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;
        const bool hovered = ImGui::IsItemHovered();
        const float vol_strip_bottom = pos.y + strip_h;
        const float loop_strip_top = pos.y + size.y - strip_h;

        if (hovered && io.MouseWheel != 0.0f)
        {
            if (io.KeyCtrl)
            {
                m_sample_editor_zoom_y = std::clamp(m_sample_editor_zoom_y * ((io.MouseWheel > 0.0f) ? 1.25f : 0.8f),
                                                    0.25f,
                                                    32.0f);
            }
            else
            {
                SeZoomBy((io.MouseWheel > 0.0f) ? 0.8f : 1.25f, SeXToFrame(mouse.x, pos, size));
            }
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            m_sample_editor_drag_mode = SeDragMode::Pan;
            m_sample_editor_drag_anchor_frame = SeXToFrame(mouse.x, pos, size);
            m_sample_editor_pan_view_start_at_drag = m_sample_editor_view_start;
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_sample_editor_frame_count > 0)
        {
            const uint32_t frame = SeXToFrame(mouse.x, pos, size);
            auto begin_loop_drag = [&](SeDragMode mode) {
                m_sample_editor_drag_mode = mode;
                m_sample_editor_drag_anchor_frame = frame;
                m_sample_editor_drag_start_mouse_x = mouse.x;
                m_sample_editor_loop_start_at_drag = m_sample_editor_loop_start;
                m_sample_editor_loop_end_at_drag = m_sample_editor_loop_end;
            };

            if (mouse.y <= vol_strip_bottom)
            {
                const float fade_in_x = SeFrameToX(m_sample_editor_fade_in_frames, pos, size);
                const uint32_t fade_out_frame = (m_sample_editor_frame_count > m_sample_editor_fade_out_frames)
                                                    ? (m_sample_editor_frame_count - m_sample_editor_fade_out_frames)
                                                    : 0u;
                const float fade_out_x = SeFrameToX(fade_out_frame, pos, size);
                if (std::fabs(mouse.x - fade_in_x) <= 8.0f)
                {
                    m_sample_editor_drag_mode = SeDragMode::FadeIn;
                }
                else if (std::fabs(mouse.x - fade_out_x) <= 8.0f)
                {
                    m_sample_editor_drag_mode = SeDragMode::FadeOut;
                }
                else
                {
                    m_sample_editor_drag_mode = SeDragMode::Gain;
                }
            }
            else if (m_sample_editor_loop_enabled && m_sample_editor_loop_end > m_sample_editor_loop_start)
            {
                /* Grab loop markers anywhere on the waveform (lines + bottom handles). */
                const float loop_x0 = SeFrameToX(m_sample_editor_loop_start, pos, size);
                const float loop_x1 = SeFrameToX(m_sample_editor_loop_end, pos, size);
                const float loop_mid = (loop_x0 + loop_x1) * 0.5f;
                const float d0 = std::fabs(mouse.x - loop_x0);
                const float d1 = std::fabs(mouse.x - loop_x1);
                const float dm = std::fabs(mouse.x - loop_mid);
                const bool in_loop_strip = (mouse.y >= loop_strip_top);
                const float edge_hit = in_loop_strip ? 10.0f : 6.0f;
                const float mid_hit = in_loop_strip ? 10.0f : 5.0f;
                if (dm <= mid_hit && dm < d0 && dm < d1 && in_loop_strip)
                {
                    begin_loop_drag(SeDragMode::LoopMid);
                }
                else if (d0 <= edge_hit && d0 <= d1)
                {
                    begin_loop_drag(SeDragMode::LoopStart);
                }
                else if (d1 <= edge_hit)
                {
                    begin_loop_drag(SeDragMode::LoopEnd);
                }
                else if (in_loop_strip)
                {
                    if (d0 <= d1)
                    {
                        begin_loop_drag(SeDragMode::LoopStart);
                    }
                    else
                    {
                        begin_loop_drag(SeDragMode::LoopEnd);
                    }
                }
                else if (io.KeyShift && m_sample_editor_sel_start >= 0)
                {
                    m_sample_editor_drag_mode = SeDragMode::Selection;
                    m_sample_editor_drag_anchor_frame = static_cast<uint32_t>(m_sample_editor_sel_start);
                }
                else
                {
                    m_sample_editor_drag_mode = SeDragMode::Selection;
                    m_sample_editor_drag_anchor_frame = frame;
                    m_sample_editor_sel_start = static_cast<int>(frame);
                    m_sample_editor_sel_end = static_cast<int>(frame);
                }
            }
            else
            {
                if (io.KeyShift && m_sample_editor_sel_start >= 0)
                {
                    m_sample_editor_drag_mode = SeDragMode::Selection;
                    m_sample_editor_drag_anchor_frame = static_cast<uint32_t>(m_sample_editor_sel_start);
                }
                else
                {
                    m_sample_editor_drag_mode = SeDragMode::Selection;
                    m_sample_editor_drag_anchor_frame = frame;
                    m_sample_editor_sel_start = static_cast<int>(frame);
                    m_sample_editor_sel_end = static_cast<int>(frame);
                }
            }
        }

        if (m_sample_editor_drag_mode == SeDragMode::Pan)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                const uint32_t span = std::max<uint32_t>(m_sample_editor_view_end - m_sample_editor_view_start, 1u);
                const float delta_frames = -(mouse.x - ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Middle].x) /
                                           std::max(size.x, 1.0f) * static_cast<float>(span);
                float start = static_cast<float>(m_sample_editor_pan_view_start_at_drag) + delta_frames;
                start = std::clamp(start,
                                   0.0f,
                                   static_cast<float>((m_sample_editor_frame_count > span)
                                                          ? (m_sample_editor_frame_count - span)
                                                          : 0u));
                m_sample_editor_view_start = static_cast<uint32_t>(start);
                m_sample_editor_view_end = std::min<uint32_t>(m_sample_editor_view_start + span,
                                                              m_sample_editor_frame_count);
                RebuildSampleEditorWaveformBins();
            }
            else
            {
                m_sample_editor_drag_mode = SeDragMode::None;
            }
            return;
        }

        if (m_sample_editor_drag_mode != SeDragMode::None && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const uint32_t frame = SeXToFrame(mouse.x, pos, size);
            auto loop_drag_delta_frames = [&]() -> int64_t {
                const uint32_t view_start = m_sample_editor_view_start;
                const uint32_t view_end = std::max<uint32_t>(m_sample_editor_view_end, view_start + 1u);
                const float span = static_cast<float>(view_end - view_start);
                float scale = 1.0f;
                if (io.KeyCtrl && io.KeyShift)
                {
                    scale = 0.01f; /* finest */
                }
                else if (io.KeyCtrl)
                {
                    scale = 0.05f;
                }
                else if (io.KeyShift)
                {
                    scale = 0.25f;
                }
                const float dx = mouse.x - m_sample_editor_drag_start_mouse_x;
                return static_cast<int64_t>(
                    std::llround((dx / std::max(size.x, 1.0f)) * span * scale));
            };
            switch (m_sample_editor_drag_mode)
            {
            case SeDragMode::Selection:
            {
                const uint32_t anchor = m_sample_editor_drag_anchor_frame;
                m_sample_editor_sel_start = static_cast<int>(std::min(anchor, frame));
                m_sample_editor_sel_end = static_cast<int>(std::max(anchor, frame));
                break;
            }
            case SeDragMode::LoopStart:
            {
                int64_t new_start =
                    static_cast<int64_t>(m_sample_editor_loop_start_at_drag) + loop_drag_delta_frames();
                new_start = std::clamp<int64_t>(new_start,
                                                0,
                                                static_cast<int64_t>(m_sample_editor_loop_end_at_drag));
                if (static_cast<uint32_t>(new_start) != m_sample_editor_loop_start)
                {
                    m_sample_editor_loop_start = static_cast<uint32_t>(new_start);
                    m_sample_editor_loop_dirty = true;
                    m_sample_editor_dirty = true;
                }
                break;
            }
            case SeDragMode::LoopEnd:
            {
                int64_t new_end =
                    static_cast<int64_t>(m_sample_editor_loop_end_at_drag) + loop_drag_delta_frames();
                new_end = std::clamp<int64_t>(new_end,
                                              static_cast<int64_t>(m_sample_editor_loop_start_at_drag),
                                              static_cast<int64_t>(m_sample_editor_frame_count));
                if (static_cast<uint32_t>(new_end) != m_sample_editor_loop_end)
                {
                    m_sample_editor_loop_end = static_cast<uint32_t>(new_end);
                    m_sample_editor_loop_dirty = true;
                    m_sample_editor_dirty = true;
                }
                break;
            }
            case SeDragMode::LoopMid:
            {
                const int64_t delta = loop_drag_delta_frames();
                const int64_t length = static_cast<int64_t>(m_sample_editor_loop_end_at_drag) -
                                       static_cast<int64_t>(m_sample_editor_loop_start_at_drag);
                int64_t start = static_cast<int64_t>(m_sample_editor_loop_start_at_drag) + delta;
                const int64_t max_start =
                    std::max<int64_t>(0, static_cast<int64_t>(m_sample_editor_frame_count) - length);
                start = std::clamp<int64_t>(start, 0, max_start);
                m_sample_editor_loop_start = static_cast<uint32_t>(start);
                m_sample_editor_loop_end = static_cast<uint32_t>(std::min<int64_t>(
                    start + length, static_cast<int64_t>(m_sample_editor_frame_count)));
                m_sample_editor_loop_dirty = true;
                m_sample_editor_dirty = true;
                break;
            }
            case SeDragMode::FadeIn:
                m_sample_editor_fade_in_frames = std::min<uint32_t>(frame, m_sample_editor_frame_count);
                break;
            case SeDragMode::FadeOut:
                m_sample_editor_fade_out_frames = (m_sample_editor_frame_count > frame)
                                                      ? (m_sample_editor_frame_count - frame)
                                                      : 0u;
                break;
            case SeDragMode::Gain:
            {
                const float t = std::clamp((mouse.y - pos.y) / std::max(strip_h, 1.0f), 0.0f, 1.0f);
                m_sample_editor_gain = std::clamp(2.0f - (t * 2.0f), 0.0f, 2.0f);
                break;
            }
            default:
                break;
            }
        }
        else if (m_sample_editor_drag_mode != SeDragMode::None)
        {
            const SeDragMode finished = m_sample_editor_drag_mode;
            m_sample_editor_drag_mode = SeDragMode::None;
            if (finished == SeDragMode::FadeIn || finished == SeDragMode::FadeOut || finished == SeDragMode::Gain)
            {
                SeApplyVolumeHandles();
            }
        }
    }

    void DrawSampleEditorWaveform()
    {
        const float strip_h = 16.0f;
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        float avail_y = ImGui::GetContentRegionAvail().y - 8.0f;
        ImVec2 canvas_size = ImVec2(ImGui::GetContentRegionAvail().x, std::clamp(avail_y, 160.0f, 460.0f));
        if (canvas_size.x < 180.0f)
        {
            canvas_size.x = 180.0f;
        }

        ImDrawList *draw = ImGui::GetWindowDrawList();
        const ImVec2 canvas_end = ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);
        draw->AddRectFilled(canvas_pos, canvas_end, IM_COL32(18, 20, 30, 255), 4.0f);
        draw->AddRect(canvas_pos, canvas_end, IM_COL32(84, 104, 128, 255), 4.0f, 0, 1.2f);

        const float wave_top = canvas_pos.y + strip_h;
        const float wave_bottom = canvas_end.y - strip_h;
        const float wave_h = std::max(wave_bottom - wave_top, 8.0f);
        const float center_y = wave_top + (wave_h * 0.5f);

        if (SeHasSelection())
        {
            const float sel_x0 = SeFrameToX(static_cast<uint32_t>(m_sample_editor_sel_start), canvas_pos, canvas_size);
            const float sel_x1 = SeFrameToX(static_cast<uint32_t>(m_sample_editor_sel_end), canvas_pos, canvas_size);
            draw->AddRectFilled(ImVec2(std::min(sel_x0, sel_x1), wave_top),
                                ImVec2(std::max(sel_x0, sel_x1), wave_bottom),
                                IM_COL32(96, 128, 220, 64));
        }

        if (m_sample_editor_loop_enabled && m_sample_editor_loop_end > m_sample_editor_loop_start)
        {
            const float loop_x0 = SeFrameToX(m_sample_editor_loop_start, canvas_pos, canvas_size);
            const float loop_x1 = SeFrameToX(m_sample_editor_loop_end, canvas_pos, canvas_size);
            draw->AddRectFilled(ImVec2(loop_x0, wave_top), ImVec2(loop_x1, wave_bottom), IM_COL32(74, 180, 136, 48));
            draw->AddLine(ImVec2(loop_x0, wave_top), ImVec2(loop_x0, canvas_end.y), IM_COL32(124, 244, 194, 200), 1.5f);
            draw->AddLine(ImVec2(loop_x1, wave_top), ImVec2(loop_x1, canvas_end.y), IM_COL32(124, 244, 194, 200), 1.5f);

            const float handle_y = canvas_end.y - (strip_h * 0.5f);
            draw->AddCircleFilled(ImVec2(loop_x0, handle_y), 5.0f, IM_COL32(124, 244, 194, 255));
            draw->AddCircleFilled(ImVec2(loop_x1, handle_y), 5.0f, IM_COL32(124, 244, 194, 255));
            const float mid_x = (loop_x0 + loop_x1) * 0.5f;
            draw->AddRectFilled(ImVec2(mid_x - 6.0f, handle_y - 4.0f),
                                ImVec2(mid_x + 6.0f, handle_y + 4.0f),
                                IM_COL32(96, 200, 160, 255));
        }

        /* Pixel-column envelope from PCM in the current view. Stereo samples
         * get stacked L/R lanes; mono stays a single centered waveform. */
        const uint32_t view_start = m_sample_editor_view_start;
        const uint32_t view_end = std::max(m_sample_editor_view_end, view_start + 1u);
        const uint32_t view_span = view_end - view_start;
        const int columns = std::max(1, static_cast<int>(canvas_size.x));
        const uint16_t bit_size = m_sample_editor_cached_bit_size;
        const uint16_t channels = std::max<uint16_t>(m_sample_editor_cached_channels, 1);
        const bool stereo_lanes = (channels >= 2);
        const float lane_gap = stereo_lanes ? 3.0f : 0.0f;
        const float lane_h =
            stereo_lanes ? std::max(8.0f, (wave_h - lane_gap) * 0.5f) : wave_h;

        if (!m_sample_editor_cached_pcm.empty() && m_sample_editor_cached_frame_count > 0 && columns > 0)
        {
            auto fill_columns_for_channel = [&](int channel_or_mono,
                                               std::vector<float> &col_min,
                                               std::vector<float> &col_max) {
                col_min.assign(static_cast<size_t>(columns), 0.0f);
                col_max.assign(static_cast<size_t>(columns), 0.0f);
                for (int col = 0; col < columns; ++col)
                {
                    const uint32_t begin =
                        view_start + static_cast<uint32_t>((static_cast<uint64_t>(col) * view_span) /
                                                          static_cast<uint64_t>(columns));
                    uint32_t end =
                        view_start + static_cast<uint32_t>((static_cast<uint64_t>(col + 1) * view_span) /
                                                          static_cast<uint64_t>(columns));
                    if (end <= begin)
                    {
                        end = std::min(view_end, begin + 1u);
                    }
                    end = std::min(end, m_sample_editor_cached_frame_count);

                    float min_v = 1.0f;
                    float max_v = -1.0f;
                    const uint32_t span = (end > begin) ? (end - begin) : 0u;
                    const uint32_t step = std::max(1u, span / 96u);
                    if (bit_size == 16)
                    {
                        const int16_t *pcm16 =
                            reinterpret_cast<const int16_t *>(m_sample_editor_cached_pcm.data());
                        for (uint32_t frame = begin; frame < end; frame += step)
                        {
                            float sample = 0.0f;
                            if (channel_or_mono >= 0)
                            {
                                sample = static_cast<float>(
                                             pcm16[(frame * channels) + static_cast<uint16_t>(channel_or_mono)]) /
                                         32768.0f;
                            }
                            else
                            {
                                for (uint16_t ch = 0; ch < channels; ++ch)
                                {
                                    sample += static_cast<float>(pcm16[(frame * channels) + ch]) / 32768.0f;
                                }
                                sample /= static_cast<float>(channels);
                            }
                            min_v = std::min(min_v, sample);
                            max_v = std::max(max_v, sample);
                        }
                    }
                    else
                    {
                        const uint8_t *pcm8 = m_sample_editor_cached_pcm.data();
                        for (uint32_t frame = begin; frame < end; frame += step)
                        {
                            float sample = 0.0f;
                            if (channel_or_mono >= 0)
                            {
                                sample = (static_cast<float>(
                                              pcm8[(frame * channels) + static_cast<uint16_t>(channel_or_mono)]) -
                                          128.0f) /
                                         128.0f;
                            }
                            else
                            {
                                for (uint16_t ch = 0; ch < channels; ++ch)
                                {
                                    sample +=
                                        (static_cast<float>(pcm8[(frame * channels) + ch]) - 128.0f) / 128.0f;
                                }
                                sample /= static_cast<float>(channels);
                            }
                            min_v = std::min(min_v, sample);
                            max_v = std::max(max_v, sample);
                        }
                    }
                    if (span == 0)
                    {
                        min_v = max_v = 0.0f;
                    }
                    col_min[static_cast<size_t>(col)] = min_v;
                    col_max[static_cast<size_t>(col)] = max_v;
                }

                if (columns >= 3)
                {
                    std::vector<float> smin = col_min;
                    std::vector<float> smax = col_max;
                    for (int col = 1; col < columns - 1; ++col)
                    {
                        smin[static_cast<size_t>(col)] =
                            (col_min[static_cast<size_t>(col - 1)] * 0.25f) +
                            (col_min[static_cast<size_t>(col)] * 0.50f) +
                            (col_min[static_cast<size_t>(col + 1)] * 0.25f);
                        smax[static_cast<size_t>(col)] =
                            (col_max[static_cast<size_t>(col - 1)] * 0.25f) +
                            (col_max[static_cast<size_t>(col)] * 0.50f) +
                            (col_max[static_cast<size_t>(col + 1)] * 0.25f);
                    }
                    col_min.swap(smin);
                    col_max.swap(smax);
                }
            };

            auto draw_lane = [&](float lane_top,
                                 float lane_bottom,
                                 const std::vector<float> &col_min,
                                 const std::vector<float> &col_max,
                                 ImU32 stem_col,
                                 ImU32 outline_col,
                                 ImU32 zero_col,
                                 const char *label) {
                const float lane_center = lane_top + ((lane_bottom - lane_top) * 0.5f);
                const float amplitude =
                    ((lane_bottom - lane_top) * 0.46f) * m_sample_editor_zoom_y;
                draw->AddLine(ImVec2(canvas_pos.x, lane_center),
                              ImVec2(canvas_end.x, lane_center),
                              zero_col,
                              1.0f);
                if (label && label[0])
                {
                    draw->AddText(ImVec2(canvas_pos.x + 4.0f, lane_top + 2.0f),
                                  IM_COL32(160, 170, 185, 200),
                                  label);
                }
                for (int col = 0; col < columns; ++col)
                {
                    const float x = canvas_pos.x + static_cast<float>(col) + 0.5f;
                    float y0 = lane_center - (col_max[static_cast<size_t>(col)] * amplitude);
                    float y1 = lane_center - (col_min[static_cast<size_t>(col)] * amplitude);
                    y0 = std::clamp(y0, lane_top, lane_bottom);
                    y1 = std::clamp(y1, lane_top, lane_bottom);
                    if (std::fabs(y1 - y0) < 1.0f)
                    {
                        y1 = y0 + 1.0f;
                    }
                    draw->AddLine(ImVec2(x, y0), ImVec2(x, y1), stem_col, 1.0f);
                }
                draw->PathClear();
                for (int col = 0; col < columns; ++col)
                {
                    const float x = canvas_pos.x + static_cast<float>(col) + 0.5f;
                    float y = lane_center - (col_max[static_cast<size_t>(col)] * amplitude);
                    draw->PathLineTo(ImVec2(x, std::clamp(y, lane_top, lane_bottom)));
                }
                draw->PathStroke(outline_col, 0, 1.0f);
                draw->PathClear();
                for (int col = 0; col < columns; ++col)
                {
                    const float x = canvas_pos.x + static_cast<float>(col) + 0.5f;
                    float y = lane_center - (col_min[static_cast<size_t>(col)] * amplitude);
                    draw->PathLineTo(ImVec2(x, std::clamp(y, lane_top, lane_bottom)));
                }
                draw->PathStroke(outline_col, 0, 1.0f);
            };

            std::vector<float> col_min;
            std::vector<float> col_max;
            if (stereo_lanes)
            {
                const float l_top = wave_top;
                const float l_bottom = wave_top + lane_h;
                const float r_top = l_bottom + lane_gap;
                const float r_bottom = wave_bottom;
                draw->AddLine(ImVec2(canvas_pos.x, l_bottom + (lane_gap * 0.5f)),
                              ImVec2(canvas_end.x, l_bottom + (lane_gap * 0.5f)),
                              IM_COL32(50, 58, 72, 220),
                              1.0f);

                fill_columns_for_channel(0, col_min, col_max);
                draw_lane(l_top,
                          l_bottom,
                          col_min,
                          col_max,
                          IM_COL32(118, 196, 248, 210),
                          IM_COL32(186, 228, 255, 160),
                          IM_COL32(66, 84, 105, 180),
                          "L");

                fill_columns_for_channel(1, col_min, col_max);
                draw_lane(r_top,
                          r_bottom,
                          col_min,
                          col_max,
                          IM_COL32(248, 176, 118, 210),
                          IM_COL32(255, 214, 176, 160),
                          IM_COL32(84, 72, 66, 180),
                          "R");
            }
            else
            {
                fill_columns_for_channel(-1, col_min, col_max);
                draw_lane(wave_top,
                          wave_bottom,
                          col_min,
                          col_max,
                          IM_COL32(118, 196, 248, 210),
                          IM_COL32(186, 228, 255, 160),
                          IM_COL32(66, 84, 105, 180),
                          nullptr);
            }
        }
        else
        {
            draw->AddLine(ImVec2(canvas_pos.x, center_y),
                          ImVec2(canvas_end.x, center_y),
                          IM_COL32(66, 84, 105, 180),
                          1.0f);
        }

        /* Volume envelope strip (fade in / gain / fade out handles) */
        {
            const float gain_t = std::clamp(1.0f - (m_sample_editor_gain * 0.5f), 0.0f, 1.0f);
            const float gain_y = canvas_pos.y + (gain_t * strip_h);
            const float fade_in_x = SeFrameToX(m_sample_editor_fade_in_frames, canvas_pos, canvas_size);
            const uint32_t fade_out_frame = (m_sample_editor_frame_count > m_sample_editor_fade_out_frames)
                                                ? (m_sample_editor_frame_count - m_sample_editor_fade_out_frames)
                                                : 0u;
            const float fade_out_x = SeFrameToX(fade_out_frame, canvas_pos, canvas_size);

            draw->AddRectFilled(canvas_pos,
                                ImVec2(canvas_end.x, canvas_pos.y + strip_h),
                                IM_COL32(28, 32, 44, 200));
            draw->AddLine(ImVec2(canvas_pos.x, canvas_pos.y + strip_h),
                          ImVec2(fade_in_x, gain_y),
                          IM_COL32(248, 200, 120, 220),
                          1.6f);
            draw->AddLine(ImVec2(fade_in_x, gain_y), ImVec2(fade_out_x, gain_y), IM_COL32(248, 200, 120, 220), 1.6f);
            draw->AddLine(ImVec2(fade_out_x, gain_y),
                          ImVec2(canvas_end.x, canvas_pos.y + strip_h),
                          IM_COL32(248, 200, 120, 220),
                          1.6f);
            draw->AddCircleFilled(ImVec2(fade_in_x, gain_y), 4.0f, IM_COL32(255, 226, 160, 255));
            draw->AddCircleFilled(ImVec2(fade_out_x, gain_y), 4.0f, IM_COL32(255, 226, 160, 255));
        }

        if (m_sample_editor_frame_count > 0)
        {
            static const ImU32 kPlayheadColors[] = {
                IM_COL32(255, 238, 138, 255),
                IM_COL32(138, 220, 255, 255),
                IM_COL32(255, 170, 200, 255),
                IM_COL32(180, 255, 170, 255),
                IM_COL32(210, 180, 255, 255),
                IM_COL32(255, 200, 140, 255),
                IM_COL32(160, 255, 230, 255),
                IM_COL32(255, 150, 150, 255),
            };
            bool drew_any = false;
            for (size_t i = 0; i < m_se_preview_voices.size(); ++i)
            {
                const SePreviewVoice &voice = m_se_preview_voices[i];
                if (!voice.active)
                {
                    continue;
                }
                const float play_x = SeFrameToX(voice.playhead_frame, canvas_pos, canvas_size);
                if (play_x >= canvas_pos.x && play_x <= canvas_end.x)
                {
                    draw->AddLine(ImVec2(play_x, canvas_pos.y),
                                  ImVec2(play_x, canvas_end.y),
                                  kPlayheadColors[i % (sizeof(kPlayheadColors) / sizeof(kPlayheadColors[0]))],
                                  2.0f);
                    drew_any = true;
                }
            }
            if (!drew_any && m_sample_editor_playing)
            {
                const float play_x = SeFrameToX(m_sample_editor_playhead_frame, canvas_pos, canvas_size);
                if (play_x >= canvas_pos.x && play_x <= canvas_end.x)
                {
                    draw->AddLine(ImVec2(play_x, canvas_pos.y),
                                  ImVec2(play_x, canvas_end.y),
                                  IM_COL32(255, 238, 138, 255),
                                  2.0f);
                }
            }
        }

        ImGui::SetCursorScreenPos(canvas_pos);
        ImGui::InvisibleButton("##SampleWaveformCanvas",
                               canvas_size,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);

        SeHandleWaveformInput(canvas_pos, canvas_size, strip_h);

        if (ImGui::BeginPopupContextItem("##SampleWaveformContext"))
        {
            DrawSampleEditorEditMenuItems();
            ImGui::EndPopup();
        }
    }

    /* ---------------- Menus ---------------- */

    void DrawSampleEditorEditMenuItems()
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_sample_editor_undo.empty()))
        {
            SampleEditorUndo();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y / Ctrl+Shift+Z", false, !m_sample_editor_redo.empty()))
        {
            SampleEditorRedo();
        }
        if (ImGui::MenuItem("Revert to Opened", nullptr, false, !m_sample_editor_pristine.pcm.empty()))
        {
            RevertSampleEditor();
        }
        if (ImGui::MenuItem("Restore Uncompressed Original",
                            nullptr,
                            false,
                            m_sample_editor_has_uncompressed_master))
        {
            (void)RestoreUncompressedMasterIntoEditor();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", "Ctrl+A", false, m_sample_editor_frame_count > 0))
        {
            SeSelectAll();
        }
        if (ImGui::MenuItem("Clear Selection", nullptr, false, SeHasSelection()))
        {
            m_sample_editor_sel_start = -1;
            m_sample_editor_sel_end = -1;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, SeHasSelection()))
        {
            SeDeleteSelection();
        }
        if (ImGui::MenuItem("Crop to Selection", nullptr, false, SeHasSelection()))
        {
            SeCropToSelection();
        }
        if (ImGui::MenuItem("Normalize", nullptr, false, m_sample_editor_frame_count > 0))
        {
            SeNormalize();
        }
        if (ImGui::MenuItem("Fade In", nullptr, false, m_sample_editor_frame_count > 0))
        {
            SeFade(true);
        }
        if (ImGui::MenuItem("Fade Out", nullptr, false, m_sample_editor_frame_count > 0))
        {
            SeFade(false);
        }
    }

    void DrawSampleEditorMenuBar(bool *open)
    {
        if (!ImGui::BeginMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Apply Changes", "Ctrl+S"))
            {
                ApplySampleEditorChanges(false);
            }
            if (ImGui::MenuItem("Compression..."))
            {
                BeginSampleEditorCompressionModal();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close"))
            {
                *open = false;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            DrawSampleEditorEditMenuItems();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Zoom Normal"))
            {
                SeZoomNormal();
            }
            if (ImGui::MenuItem("Zoom Loop Start", nullptr, false, m_sample_editor_loop_enabled))
            {
                SeZoomToLoopStart();
            }
            if (ImGui::MenuItem("Zoom Loop End", nullptr, false, m_sample_editor_loop_enabled))
            {
                SeZoomToLoopEnd();
            }
            if (ImGui::MenuItem("Jump to Loop", nullptr, false, m_sample_editor_loop_enabled))
            {
                SeJumpToLoopInView();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Zoom In"))
            {
                SeZoomBy(0.5f, (m_sample_editor_view_start + m_sample_editor_view_end) / 2u);
            }
            if (ImGui::MenuItem("Zoom Out"))
            {
                SeZoomBy(2.0f, (m_sample_editor_view_start + m_sample_editor_view_end) / 2u);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Vertical Zoom In"))
            {
                m_sample_editor_zoom_y = std::clamp(m_sample_editor_zoom_y * 1.25f, 0.25f, 32.0f);
            }
            if (ImGui::MenuItem("Vertical Zoom Out"))
            {
                m_sample_editor_zoom_y = std::clamp(m_sample_editor_zoom_y * 0.8f, 0.25f, 32.0f);
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    /* ---------------- Property strip ---------------- */

    void DrawSampleEditorProperties()
    {
        char name_buffer[256];
        std::snprintf(name_buffer, sizeof(name_buffer), "%s", m_sample_editor_sample_name.c_str());
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::InputText("Name", name_buffer, sizeof(name_buffer)))
        {
            m_sample_editor_sample_name = name_buffer;
            m_sample_editor_dirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(m_sample_editor_is_song_sample
                                 ? "Song sample display name"
                                 : "SND resource name in the bank");
        }

        ImGui::SameLine();
        if (m_sample_editor_is_song_sample)
        {
            ImGui::Text("Doc Sample: %u", m_sample_editor_document_sample_index);
        }
        else
        {
            ImGui::Text("Resource ID: %u", m_sample_editor_snd_resource_id);
        }
        if (!m_sample_editor_usage_text.empty())
        {
            ImGui::TextWrapped("%s", m_sample_editor_usage_text.c_str());
        }

        const uint32_t rate_hz = std::max<uint32_t>(m_sample_editor_sample_rate_hz, 1u);
        const double seconds = static_cast<double>(m_sample_editor_frame_count) / static_cast<double>(rate_hz);
        const uint32_t uncompressed_bytes = m_sample_editor_frame_count *
                                            static_cast<uint32_t>(SeChannelCount()) *
                                            static_cast<uint32_t>((m_sample_editor_bit_depth == 8) ? 1 : 2);

        ImGui::Text("Length: %u frames (%.3f s)   Channels: %d   Bits: %d",
                    m_sample_editor_frame_count,
                    seconds,
                    m_sample_editor_channels,
                    m_sample_editor_bit_depth);
        ImGui::Text("Codec: %s   Uncompressed: %u bytes",
                    GetSampleCodecLabel(m_sample_editor_codec_type),
                    uncompressed_bytes);
        ImGui::SameLine();
        if (m_sample_editor_compressed_bytes > 0)
        {
            /* PCM SND resources include a header, so resource size is often a
             * few dozen bytes above raw PCM — that is not "compression". */
            if (SeCodecIsUncompressedPcm(m_sample_editor_codec_type))
            {
                ImGui::Text("   Stored: %u bytes", m_sample_editor_compressed_bytes);
            }
            else
            {
                ImGui::Text("   Compressed: %u bytes", m_sample_editor_compressed_bytes);
            }
        }
        else if (SeCodecIsUncompressedPcm(m_sample_editor_codec_type))
        {
            ImGui::TextUnformatted("   Stored: unknown");
        }
        else
        {
            ImGui::TextUnformatted("   Compressed: unknown");
        }

        /* Sample rate presets (retag only, no resampling) */
        size_t preset_count = 0;
        const int *presets = SeSampleRatePresets(&preset_count);
        std::string preview;
        if (m_sample_editor_rate_preset >= 0 && m_sample_editor_rate_preset < static_cast<int>(preset_count))
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d Hz", presets[m_sample_editor_rate_preset]);
            preview = buf;
        }
        else
        {
            preview = "Other";
        }

        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::BeginCombo("Sample Rate", preview.c_str()))
        {
            for (size_t i = 0; i < preset_count; ++i)
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d Hz", presets[i]);
                if (ImGui::Selectable(buf, m_sample_editor_rate_preset == static_cast<int>(i)))
                {
                    PushSampleEditorUndo();
                    m_sample_editor_rate_preset = static_cast<int>(i);
                    m_sample_editor_sample_rate_hz = static_cast<uint32_t>(presets[i]);
                    m_sample_editor_cached_sample_rate =
                        LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz);
                    /* Rate retag is metadata — mark dirty so Apply is obvious. */
                    m_sample_editor_dirty = true;
                }
            }
            if (ImGui::Selectable("Other", m_sample_editor_rate_preset >= static_cast<int>(preset_count)))
            {
                m_sample_editor_rate_preset = static_cast<int>(preset_count);
            }
            ImGui::EndCombo();
        }

        if (m_sample_editor_rate_preset >= static_cast<int>(preset_count))
        {
            ImGui::SameLine();
            int custom_hz = static_cast<int>(m_sample_editor_sample_rate_hz);
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt("Hz", &custom_hz, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
            {
                custom_hz = std::clamp(custom_hz, 1000, 192000);
                PushSampleEditorUndo();
                m_sample_editor_sample_rate_hz = static_cast<uint32_t>(custom_hz);
                m_sample_editor_cached_sample_rate =
                    LONG_TO_UNSIGNED_FIXED(m_sample_editor_sample_rate_hz);
                m_sample_editor_dirty = true;
            }
        }

        ImGui::SameLine();
        int root_key = m_sample_editor_root_key;
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputInt("Root Key", &root_key))
        {
            root_key = std::clamp(root_key, 0, 127);
            if (root_key != m_sample_editor_root_key)
            {
                PushSampleEditorUndo();
                m_sample_editor_root_key = root_key;
                m_sample_editor_preview_note = root_key;
            }
        }
        ImGui::SameLine();
        char note_name[16];
        SeNoteName(m_sample_editor_root_key, note_name, sizeof(note_name));
        if (ImGui::Button(note_name))
        {
            m_sample_editor_choose_note_open = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Choose Note...");
        }
        ImGui::SameLine();
        {
            int volume = m_sample_editor_volume;
            ImGui::SetNextItemWidth(90.0f);
            ImGui::BeginDisabled(!m_sample_editor_volume_editable);
            ImGui::InputInt("Volume", &volume, 0, 0);
            const bool vol_committed = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            if (vol_committed)
            {
                volume = std::clamp(volume, 1, 32767);
                if (volume != m_sample_editor_volume)
                {
                    PushSampleEditorUndo();
                    m_sample_editor_volume = volume;
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip(m_sample_editor_volume_editable
                                      ? "Instrument volume for this sample (keysplit Vol, or the\n"
                                        "instrument volume for single-sample instruments). 100 = default."
                                      : "Volume unavailable: this instrument uses sample-offset or\n"
                                        "sound-modifier mode (miscParameter2 is not volume).");
            }
        }

        /* Loop controls — markers are draggable on the waveform; Shift/Ctrl slow the drag. */
        bool loop_enabled = m_sample_editor_loop_enabled;
        if (ImGui::Checkbox("Loop", &loop_enabled))
        {
            PushSampleEditorUndo();
            m_sample_editor_loop_enabled = loop_enabled;
            m_sample_editor_loop_dirty = true;
            if (loop_enabled && m_sample_editor_loop_end <= m_sample_editor_loop_start)
            {
                m_sample_editor_loop_start = 0;
                m_sample_editor_loop_end = m_sample_editor_frame_count;
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Drag green markers on the waveform. Shift = fine, Ctrl = finer, Shift+Ctrl = finest.");
        }

        ImGui::SameLine();
        uint32_t loop_start_input = m_sample_editor_loop_start;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputScalar("Loop Start", ImGuiDataType_U32, &loop_start_input))
        {
            const uint32_t max_frame = (m_sample_editor_frame_count > 0) ? (m_sample_editor_frame_count - 1u) : 0u;
            loop_start_input = std::min<uint32_t>(loop_start_input, max_frame);
            if (loop_start_input != m_sample_editor_loop_start)
            {
                m_sample_editor_loop_start = loop_start_input;
                if (m_sample_editor_loop_end < loop_start_input)
                {
                    m_sample_editor_loop_end = loop_start_input;
                }
                m_sample_editor_loop_dirty = true;
                m_sample_editor_dirty = true;
            }
        }

        ImGui::SameLine();
        uint32_t loop_end_input = m_sample_editor_loop_end;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputScalar("Loop End", ImGuiDataType_U32, &loop_end_input))
        {
            loop_end_input = std::min<uint32_t>(loop_end_input, m_sample_editor_frame_count);
            if (loop_end_input != m_sample_editor_loop_end)
            {
                m_sample_editor_loop_end = loop_end_input;
                if (m_sample_editor_loop_start > loop_end_input)
                {
                    m_sample_editor_loop_start = loop_end_input;
                }
                m_sample_editor_loop_dirty = true;
                m_sample_editor_dirty = true;
            }
        }
    }

    /* ---------------- Modals ---------------- */

    void DrawSampleEditorChooseNoteModal()
    {
        if (m_sample_editor_choose_note_open && !ImGui::IsPopupOpen("Choose Note"))
        {
            ImGui::OpenPopup("Choose Note");
        }
        if (!ImGui::BeginPopupModal("Choose Note", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        char current[16];
        SeNoteName(m_sample_editor_root_key, current, sizeof(current));
        ImGui::Text("Root Key: %d (%s)", m_sample_editor_root_key, current);
        ImGui::Separator();

        static const int white_offsets[7] = {0, 2, 4, 5, 7, 9, 11};
        for (int octave = 0; octave < 11; ++octave)
        {
            ImGui::PushID(octave);
            ImGui::Text("C%d", octave - 1);
            ImGui::SameLine();
            for (int key = 0; key < 7; ++key)
            {
                const int note = (octave * 12) + white_offsets[key];
                if (note > 127)
                {
                    break;
                }
                char label[16];
                SeNoteName(note, label, sizeof(label));
                ImGui::PushID(key);
                const bool selected = (note == m_sample_editor_root_key);
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.62f, 0.45f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(46.0f, 0.0f)))
                {
                    if (note != m_sample_editor_root_key)
                    {
                        PushSampleEditorUndo();
                        m_sample_editor_root_key = note;
                        m_sample_editor_preview_note = note;
                    }
                }
                if (selected)
                {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
                if (key < 6)
                {
                    ImGui::SameLine();
                }
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
        {
            m_sample_editor_choose_note_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void BeginSampleEditorCompressionModal()
    {
        m_sample_editor_comp_codec_index_on_open = m_sample_editor_codec_index;
        m_sample_editor_comp_storage_on_open = m_sample_editor_snd_storage_type;
        m_sample_editor_comp_opus_on_open = m_sample_editor_opus_mode;
        m_sample_editor_comp_dirty_on_open = m_sample_editor_dirty;
        m_sample_editor_comp_preview_needs_on_open = m_sample_editor_preview_needs_reencode;
        m_sample_editor_compression_open = true;
    }

    void CancelSampleEditorCompressionModal()
    {
        StopSampleEditorPreview();
        if (m_sample_editor_snd_snapshot_valid)
        {
            (void)RestoreSampleEditorSndSnapshot();
            ClearSampleEditorSndSnapshot();
        }
        m_sample_editor_codec_index = m_sample_editor_comp_codec_index_on_open;
        m_sample_editor_snd_storage_type = m_sample_editor_comp_storage_on_open;
        m_sample_editor_opus_mode = m_sample_editor_comp_opus_on_open;
        m_sample_editor_dirty = m_sample_editor_comp_dirty_on_open;
        m_sample_editor_preview_needs_reencode = m_sample_editor_comp_preview_needs_on_open;
        m_sample_editor_audition_pcm.clear();
        m_sample_editor_audition_frame_count = 0;
        if (m_sample_editor_snd_resource_id != kNoSndResourceId)
        {
            m_app_compression_cache.erase(m_sample_editor_snd_resource_id);
        }
        if (m_sample_editor_sample_row >= 0 &&
            m_sample_editor_sample_row < static_cast<int>(m_samples.size()))
        {
            m_sample_editor_compressed_bytes =
                QueryBankSampleResourceBytes(m_samples[static_cast<size_t>(m_sample_editor_sample_row)]);
        }
        m_sample_editor_compression_open = false;
    }

    void DrawSampleEditorCompressionModal()
    {
        if (m_sample_editor_compression_open && !ImGui::IsPopupOpen("Sample Compression"))
        {
            ImGui::OpenPopup("Sample Compression");
        }
        bool compression_open = m_sample_editor_compression_open;
        if (!ImGui::BeginPopupModal("Sample Compression",
                                    &compression_open,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (m_sample_editor_compression_open && !compression_open)
            {
                CancelSampleEditorCompressionModal();
            }
            return;
        }
        if (!compression_open)
        {
            CancelSampleEditorCompressionModal();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        size_t option_count = 0;
        const SampleCodecOption *options = GetSampleCodecOptions(&option_count);
        if (m_sample_editor_codec_index < 0 || m_sample_editor_codec_index >= static_cast<int>(option_count))
        {
            m_sample_editor_codec_index = 0;
        }

        if (ImGui::Checkbox("Limit to RMF Compatibility", &m_sample_editor_player20_compat))
        {
            if (m_sample_editor_player20_compat &&
                !SeIsPlayer20Codec(options[static_cast<size_t>(m_sample_editor_codec_index)].type))
            {
                for (size_t i = 0; i < option_count; ++i)
                {
                    if (options[i].type == BAE_EDITOR_COMPRESSION_DONT_CHANGE)
                    {
                        m_sample_editor_codec_index = static_cast<int>(i);
                        break;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Restrict codecs to classic RMF-safe types (PCM / ADPCM / MP3 / No Recompression)");
        }

        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::BeginCombo("Compression", options[static_cast<size_t>(m_sample_editor_codec_index)].label))
        {
            for (size_t i = 0; i < option_count; ++i)
            {
                if (m_sample_editor_player20_compat && !SeIsPlayer20Codec(options[i].type))
                {
                    continue;
                }
                const bool selected = (m_sample_editor_codec_index == static_cast<int>(i));
                if (ImGui::Selectable(options[i].label, selected))
                {
                    /* Tentative only — Apply commits; Cancel restores. */
                    m_sample_editor_codec_index = static_cast<int>(i);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        static const char *storage_labels[] = {"ESND", "CSND", "SND"};
        int storage_index = std::clamp(static_cast<int>(m_sample_editor_snd_storage_type), 0, 2);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Storage", &storage_index, storage_labels, IM_ARRAYSIZE(storage_labels)))
        {
            m_sample_editor_snd_storage_type = static_cast<BAERmfEditorSndStorageType>(storage_index);
            if (m_sample_editor_snd_storage_type != m_sample_editor_original_snd_storage_type)
            {
                m_sample_editor_dirty = true;
            }
        }

        const BAERmfEditorCompressionType selected_codec =
            options[static_cast<size_t>(m_sample_editor_codec_index)].type;
        if (IsOpusCompressionType(selected_codec))
        {
            ImGui::SameLine();
            static const char *opus_mode_labels[] = {"Opus Audio", "Opus Voice"};
            int opus_mode = std::clamp(static_cast<int>(m_sample_editor_opus_mode), 0, 1);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Opus Mode", &opus_mode, opus_mode_labels, IM_ARRAYSIZE(opus_mode_labels)))
            {
                m_sample_editor_opus_mode = static_cast<BAERmfEditorOpusMode>(opus_mode);
            }
        }

        ImGui::Separator();
        const uint32_t uncompressed_bytes = m_sample_editor_cached_frame_count *
                                            static_cast<uint32_t>(SeChannelCount()) *
                                            static_cast<uint32_t>((m_sample_editor_cached_bit_size == 8) ? 1 : 2);
        ImGui::Text("Uncompressed: %u bytes", uncompressed_bytes);
        if (SeCodecIsUncompressedPcm(selected_codec))
        {
            /* Selecting PCM: show raw PCM as the payload size; resource/header
             * overhead is not compression. */
            if (m_sample_editor_compressed_bytes > 0 &&
                m_sample_editor_compressed_bytes != uncompressed_bytes)
            {
                ImGui::Text("Stored:       %u bytes (SND resource)", m_sample_editor_compressed_bytes);
            }
            ImGui::TextDisabled("PCM is uncompressed — resource size includes SND header.");
        }
        else if (m_sample_editor_compressed_bytes > 0)
        {
            ImGui::Text("Compressed:   %u bytes", m_sample_editor_compressed_bytes);
        }
        else
        {
            ImGui::TextUnformatted("Compressed:   unknown until applied / previewed");
        }

        ImGui::Separator();
        if (ImGui::Button("Audition Original"))
        {
            StartSampleEditorPreview(SePreviewSource::Master);
        }
        ImGui::SameLine();
        if (ImGui::Button("Preview"))
        {
            /* Encode from master → audition buffer, then restore bank SND. */
            if (PrepareCompressionPreviewAudition())
            {
                StartSampleEditorPreview(SePreviewSource::Audition);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
        {
            StopSampleEditorPreview();
        }

        ImGui::Separator();
        if (ImGui::Button("Apply", ImVec2(120.0f, 0.0f)))
        {
            ClearSampleEditorSndSnapshot();
            if (ApplySampleEditorChanges(false))
            {
                m_sample_editor_compression_open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            CancelSampleEditorCompressionModal();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    /* ---------------- Dialog ---------------- */

    void DrawSampleEditorDialog()
    {
        if (!m_sample_editor_open)
        {
            m_sample_editor_focused = false;
            return;
        }

        char title[384];
        std::snprintf(title,
                      sizeof(title),
                      "Sample Editor - %s%s###SampleEditorWindow",
                      m_sample_editor_sample_name.empty() ? "(none)" : m_sample_editor_sample_name.c_str(),
                      m_sample_editor_dirty ? " *" : "");

        bool open = m_sample_editor_open;
        /* Default roughly matches a typical first-run layout (~screenshot size). */
        ImGui::SetNextWindowSize(ImVec2(780.0f, 430.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar))
        {
            m_sample_editor_focused = false;
            ImGui::End();
            if (!open)
            {
                CloseSampleEditor();
            }
            return;
        }
        m_sample_editor_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        DrawSampleEditorMenuBar(&open);

        const bool window_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        DrawSampleEditorProperties();
        ImGui::Separator();

        ImGui::TextDisabled("Type A–K / MIDI keys to pitch-preview (poly playheads). ,/. octave.");
        if (ImGui::Button(m_sample_editor_playing ? "Stop" : "Play"))
        {
            if (m_sample_editor_playing)
            {
                StopSampleEditorPreview();
                m_sample_editor_playhead_frame = 0;
            }
            else
            {
                StartSampleEditorPreview(false);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply"))
        {
            ApplySampleEditorChanges(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Compression..."))
        {
            BeginSampleEditorCompressionModal();
        }
        if (m_sample_editor_has_uncompressed_master)
        {
            ImGui::SameLine();
            if (ImGui::Button("Restore Uncompressed…"))
            {
                (void)RestoreUncompressedMasterIntoEditor();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Reload session PCM / CaSd master, clear export compression, "
                    "and update live song/IE preview");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Zoom Normal"))
        {
            SeZoomNormal();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            open = false;
        }
        ImGui::SameLine();
        if (SeHasSelection())
        {
            ImGui::Text("Selection: %d - %d", m_sample_editor_sel_start, m_sample_editor_sel_end);
        }
        else
        {
            ImGui::Text("View: %u - %u", m_sample_editor_view_start, m_sample_editor_view_end);
        }

        SeAdvancePlayhead();

        if (window_focused && !ImGui::GetIO().WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
            {
                if (m_sample_editor_playing)
                {
                    StopSampleEditorPreview();
                }
                else
                {
                    StartSampleEditorPreview(false);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && SeHasSelection())
            {
                SeDeleteSelection();
            }
            if (ImGui::GetIO().KeyCtrl)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_A, false))
                {
                    SeSelectAll();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    if (ImGui::GetIO().KeyShift)
                    {
                        SampleEditorRedo();
                    }
                    else
                    {
                        SampleEditorUndo();
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                {
                    SampleEditorRedo();
                }
                /* Ctrl+S Apply: HandleGlobalAppShortcuts when sample editor focused. */
            }
        }

        DrawSampleEditorWaveform();
        DrawSampleEditorChooseNoteModal();
        DrawSampleEditorCompressionModal();

        ImGui::End();

        if (!open)
        {
            CloseSampleEditor();
        }
    }

    void CloseSampleEditor()
    {
        StopSampleEditorPreview();
        if (m_sample_editor_compression_open)
        {
            CancelSampleEditorCompressionModal();
        }
        if (m_sample_editor_snd_snapshot_valid)
        {
            (void)RestoreSampleEditorSndSnapshot();
            ClearSampleEditorSndSnapshot();
        }
        if (m_sample_editor_dirty || m_sample_editor_loop_dirty)
        {
            (void)ApplySampleEditorChanges(true);
        }
        m_sample_editor_choose_note_open = false;
        m_sample_editor_compression_open = false;
        m_sample_editor_preview_needs_reencode = false;
        m_sample_editor_drag_mode = SeDragMode::None;
        m_sample_editor_open = false;
        m_sample_editor_focused = false;
        m_sample_editor_audition_pcm.clear();
        m_sample_editor_audition_frame_count = 0;
    }





