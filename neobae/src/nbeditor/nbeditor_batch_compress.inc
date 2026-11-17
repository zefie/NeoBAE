    /* ---------------- Batch compress (Samples / Instruments lists) ----------------
     *
     * "Compress Selected..." / "Compress All..." — same semantics as Sample Editor
     * Apply with a codec, for many samples at once:
     *   - Bank samples: the editor bank stays PCM SND; each SND gets a session PCM
     *     master + deferred export target that Export (or "Delete uncompressed
     *     originals") encodes. Shared SNDs are handled once.
     *   - Song samples: the document sample's target compression is set directly.
     * Picking PCM clears any pending compression on the targets. */

#if NBEDITOR_MVP
    std::string BatchCompressTargetSummary() const
    {
        char buf[160];
        if (m_batch_compress_song_samples.empty())
        {
            std::snprintf(buf, sizeof(buf), "%zu bank sample(s)", m_batch_compress_snd_ids.size());
        }
        else if (m_batch_compress_snd_ids.empty())
        {
            std::snprintf(buf, sizeof(buf), "%zu song sample(s)", m_batch_compress_song_samples.size());
        }
        else
        {
            std::snprintf(buf,
                          sizeof(buf),
                          "%zu bank sample(s), %zu song sample(s)",
                          m_batch_compress_snd_ids.size(),
                          m_batch_compress_song_samples.size());
        }
        return buf;
    }

    /* Build the unique target set from Samples-list row indices and open the dialog. */
    void OpenBatchCompressDialogForSampleRows(const std::vector<int> &rows, const char *scope)
    {
        m_batch_compress_snd_ids.clear();
        m_batch_compress_song_samples.clear();
        m_batch_compress_unassigned_skipped = 0;
        std::set<uint32_t> seen_snd;
        std::set<uint32_t> seen_song;
        for (int r : rows)
        {
            if (r < 0 || r >= static_cast<int>(m_samples.size()))
            {
                continue;
            }
            const SampleRow &row = m_samples[static_cast<size_t>(r)];
            if (row.source == SampleSource::Song)
            {
                if (m_document && seen_song.insert(row.document_sample_index).second)
                {
                    m_batch_compress_song_samples.push_back(row.document_sample_index);
                }
                continue;
            }
            if (!m_bank_token || row.snd_resource_id == kNoSndResourceId)
            {
                continue;
            }
            if (row.unassigned)
            {
                /* Export only encodes SNDs an INST references. */
                ++m_batch_compress_unassigned_skipped;
                continue;
            }
            if (seen_snd.insert(row.snd_resource_id).second)
            {
                m_batch_compress_snd_ids.push_back(row.snd_resource_id);
            }
        }
        if (m_batch_compress_snd_ids.empty() && m_batch_compress_song_samples.empty())
        {
            SetStatus(m_batch_compress_unassigned_skipped > 0
                          ? "Compress: only unassigned samples selected (assign them to an instrument first)"
                          : "Compress: no samples to compress");
            return;
        }
        m_batch_compress_scope = scope ? scope : "Selected";
        m_batch_compress_open = true;
    }

    void OpenBatchCompressSelectedSamples()
    {
        OpenBatchCompressDialogForSampleRows(CollectTargetSampleIndices(), "Selected");
    }

    void OpenBatchCompressAllSamples()
    {
        std::vector<int> rows;
        rows.reserve(m_samples.size());
        for (int i = 0; i < static_cast<int>(m_samples.size()); ++i)
        {
            rows.push_back(i);
        }
        OpenBatchCompressDialogForSampleRows(rows, "All");
    }

    /* Instruments tab: every bank sample used by the selected instruments. */
    void OpenBatchCompressSelectedInstruments()
    {
        std::set<uint32_t> inst_indices;
        std::set<uint32_t> song_inst_keys; /* (bank << 8) | program for song instruments */
        for (int idx : CollectTargetInstrumentIndices())
        {
            if (idx < 0 || idx >= static_cast<int>(m_instruments.size()))
            {
                continue;
            }
            const InstrumentRow &inst = m_instruments[static_cast<size_t>(idx)];
            if (inst.from_song_document)
            {
                song_inst_keys.insert((static_cast<uint32_t>(inst.bank) << 8) |
                                      static_cast<uint32_t>(inst.program & 0xFF));
                continue;
            }
            uint32_t index = inst.instrument_index;
            if (inst.is_alias && m_bank_token)
            {
                /* Alias shares the target INST's samples. */
                uint32_t resolved = 0;
                if (BAERmfEditorBank_ResolveInstID(m_bank_token, inst.target_inst_id, &resolved, &index) !=
                    BAE_NO_ERROR)
                {
                    continue;
                }
            }
            inst_indices.insert(index);
        }
        std::vector<int> rows;
        for (int i = 0; i < static_cast<int>(m_samples.size()); ++i)
        {
            const SampleRow &row = m_samples[static_cast<size_t>(i)];
            if (row.unassigned)
            {
                continue;
            }
            if (row.source == SampleSource::Bank && inst_indices.count(row.instrument_index) != 0)
            {
                rows.push_back(i);
            }
            else if (row.source == SampleSource::Song &&
                     song_inst_keys.count((static_cast<uint32_t>(row.bank) << 8) |
                                          static_cast<uint32_t>(row.program & 0xFF)) != 0)
            {
                rows.push_back(i);
            }
        }
        OpenBatchCompressDialogForSampleRows(rows, "Selected Instruments");
    }

    /* Make sure a session PCM master exists for a bank SND (decode once). When
     * skip_lossy_source is set, SNDs already stored with a codec and no PCM
     * master are left alone (re-encoding lossy audio only degrades it). */
    enum class BatchMasterResult
    {
        Ok,
        SkippedLossy,
        Failed,
    };

    BatchMasterResult EnsureSessionPcmMasterForSnd(uint32_t snd_id, bool skip_lossy_source)
    {
        auto it = m_session_pcm_cache.find(snd_id);
        if (it != m_session_pcm_cache.end() && !it->second.pcm.empty() && it->second.frame_count > 0)
        {
            return BatchMasterResult::Ok;
        }
        if (HydrateSessionPcmEntryFromCasd(snd_id))
        {
            return BatchMasterResult::Ok;
        }
        uint32_t inst_index = 0;
        uint32_t split_index = 0;
        BAERmfEditorBankSampleInfo info;
        std::memset(&info, 0, sizeof(info));
        if (!ResolveBankSplitForSnd(snd_id, &inst_index, &split_index, &info))
        {
            return BatchMasterResult::Failed;
        }
        if (skip_lossy_source &&
            !FormatVisibleCodecLabel(static_cast<uint32_t>(info.compressionType),
                                     info.compressionSubType,
                                     info.opusRoundTripResample)
                 .empty())
        {
            return BatchMasterResult::SkippedLossy;
        }

        void *wave_data = nullptr;
        uint32_t frame_count = 0;
        uint16_t bit_size = 0;
        uint16_t channels = 0;
        BAE_UNSIGNED_FIXED sample_rate = 0;
        const BAEResult wr = BAERmfEditorBank_GetSndWaveformData(m_bank_token,
                                                                 snd_id,
                                                                 &wave_data,
                                                                 &frame_count,
                                                                 &bit_size,
                                                                 &channels,
                                                                 &sample_rate);
        if (wr != BAE_NO_ERROR || !wave_data || frame_count == 0 || bit_size == 0 || channels == 0)
        {
            if (wave_data)
            {
                BAERmfEditorBank_FreeWaveformData(wave_data);
            }
            return BatchMasterResult::Failed;
        }

        SessionPcmCacheEntry entry;
        if (it != m_session_pcm_cache.end())
        {
            /* Keep name / any pending prefs already recorded for this SND. */
            entry = it->second;
        }
        const size_t pcm_bytes = static_cast<size_t>(frame_count) *
                                 static_cast<size_t>(bit_size / 8u) * static_cast<size_t>(channels);
        entry.pcm.assign(static_cast<const unsigned char *>(wave_data),
                         static_cast<const unsigned char *>(wave_data) + pcm_bytes);
        BAERmfEditorBank_FreeWaveformData(wave_data);
        entry.frame_count = frame_count;
        entry.bit_size = bit_size;
        entry.channels = channels;
        entry.sample_rate = sample_rate ? sample_rate
                                        : UNSIGNED_LONG_TO_XFIXED(info.sampleRate ? info.sampleRate : 44100u);
        entry.loop_start = info.loopStart;
        entry.loop_end = info.loopEnd;
        entry.base_key = info.rootKey <= 127 ? static_cast<int>(info.rootKey) : 60;
        entry.snd_resource_id = snd_id;
        if (entry.name.empty())
        {
            for (const SampleRow &row : m_samples)
            {
                if (row.source == SampleSource::Bank && row.snd_resource_id == snd_id)
                {
                    entry.name = row.name;
                    break;
                }
            }
            if (entry.name.empty())
            {
                entry.name = "sample";
            }
        }
        m_session_pcm_cache[snd_id] = std::move(entry);
        return BatchMasterResult::Ok;
    }

    void PerformBatchCompress()
    {
        size_t option_count = 0;
        const SampleCodecOption *options = GetSampleCodecOptions(&option_count);
        if (m_batch_compress_codec_index < 0 ||
            m_batch_compress_codec_index >= static_cast<int>(option_count))
        {
            return;
        }
        const BAERmfEditorCompressionType codec =
            options[static_cast<size_t>(m_batch_compress_codec_index)].type;
        const bool clear_target = (codec == BAE_EDITOR_COMPRESSION_PCM ||
                                   codec == BAE_EDITOR_COMPRESSION_DONT_CHANGE);
        const BAERmfEditorSndStorageType storage = m_batch_compress_storage;
        const BAERmfEditorOpusMode opus_mode = m_batch_compress_opus_mode;
        const bool opus_roundtrip = IsOpusCompressionType(codec);
        const std::string label = clear_target ? std::string() : FormatVisibleCodecLabelFromEditorType(codec);

        const size_t total = m_batch_compress_snd_ids.size() + m_batch_compress_song_samples.size();
        size_t done = 0;
        size_t skipped_lossy = 0;
        size_t failed = 0;
        size_t step = 0;
        const bool show_busy = total > 4;
        if (show_busy)
        {
            BeginBusyProgress(clear_target ? "Clearing sample compression…" : "Compressing samples…");
        }
        auto progress = [&](const char *what, uint32_t id) {
            ++step;
            if (!show_busy)
            {
                return;
            }
            char detail[96];
            std::snprintf(detail, sizeof(detail), "%s %zu / %zu (%s %u)…",
                          clear_target ? "Clearing" : "Preparing",
                          step, total, what, id);
            UpdateBusyProgress(static_cast<float>(step) / static_cast<float>(total), detail);
        };

        for (uint32_t snd_id : m_batch_compress_snd_ids)
        {
            progress("SND", snd_id);
            if (clear_target)
            {
                ClearSessionExportTargetForSnd(snd_id);
                ++done;
                continue;
            }
            const BatchMasterResult mr = EnsureSessionPcmMasterForSnd(snd_id, m_batch_compress_skip_lossy);
            if (mr == BatchMasterResult::SkippedLossy)
            {
                ++skipped_lossy;
                continue;
            }
            if (mr != BatchMasterResult::Ok)
            {
                ++failed;
                continue;
            }
            SessionPcmCacheEntry &entry = m_session_pcm_cache[snd_id];
            entry.has_export_target = true;
            entry.export_compression = codec;
            entry.export_storage = storage;
            entry.export_opus_mode = opus_mode;
            entry.export_opus_roundtrip = opus_roundtrip;
            /* Stale compressed preview for the old codec. */
            m_app_compression_cache.erase(snd_id);
            for (SampleRow &row : m_samples)
            {
                if (row.source == SampleSource::Bank && row.snd_resource_id == snd_id)
                {
                    row.codec_label = label;
                }
            }
            ++done;
        }

        bool song_changed = false;
        for (uint32_t doc_index : m_batch_compress_song_samples)
        {
            progress("song sample", doc_index);
            if (!m_document)
            {
                ++failed;
                continue;
            }
            BAERmfEditorSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorDocument_GetSampleInfo(m_document, doc_index, &info) != BAE_NO_ERROR)
            {
                ++failed;
                continue;
            }
            /* SetSampleInfo may replace the strings it points at — pass copies. */
            const std::string display_name = info.displayName ? info.displayName : "";
            const std::string source_path = info.sourcePath ? info.sourcePath : "";
            info.displayName = display_name.c_str();
            info.sourcePath = info.sourcePath ? source_path.c_str() : nullptr;
            const BAERmfEditorCompressionType effective =
                clear_target ? BAE_EDITOR_COMPRESSION_PCM : codec;
            info.compressionType = effective;
            info.opusRoundTripResample = opus_roundtrip ? TRUE : FALSE;
            info.sndStorageType = storage;
            info.opusMode = opus_mode;
            if (BAERmfEditorDocument_SetSampleInfo(m_document, doc_index, &info) != BAE_NO_ERROR)
            {
                ++failed;
                continue;
            }
            uint32_t asset_id = 0;
            if (BAERmfEditorDocument_GetSampleAssetIDForSample(m_document, doc_index, &asset_id) ==
                BAE_NO_ERROR)
            {
                (void)BAERmfEditorDocument_SetSampleAssetCompression(m_document, asset_id, effective);
            }
            song_changed = true;
            ++done;
        }
        if (song_changed)
        {
            m_document_dirty = true;
            RefreshSongSamplesFromDocument();
        }
        if (show_busy)
        {
            EndBusyProgress();
        }

        /* Keep an open Sample Editor's codec combo in sync with what we just set. */
        if (m_sample_editor_open && !m_sample_editor_is_song_sample)
        {
            auto pref = m_session_pcm_cache.find(m_sample_editor_snd_resource_id);
            const bool targeted = std::find(m_batch_compress_snd_ids.begin(),
                                            m_batch_compress_snd_ids.end(),
                                            m_sample_editor_snd_resource_id) !=
                                  m_batch_compress_snd_ids.end();
            if (targeted && pref != m_session_pcm_cache.end())
            {
                const BAERmfEditorCompressionType want =
                    pref->second.has_export_target ? pref->second.export_compression
                                                   : BAE_EDITOR_COMPRESSION_DONT_CHANGE;
                for (size_t i = 0; i < option_count; ++i)
                {
                    if (options[i].type == want)
                    {
                        m_sample_editor_codec_index = static_cast<int>(i);
                        break;
                    }
                }
                m_sample_editor_codec_type = pref->second.has_export_target
                                                 ? pref->second.export_compression
                                                 : BAE_EDITOR_COMPRESSION_PCM;
            }
        }

        char status[256];
        if (clear_target)
        {
            std::snprintf(status, sizeof(status), "Cleared pending compression on %zu sample(s)", done);
        }
        else
        {
            int n = std::snprintf(status,
                                  sizeof(status),
                                  "Compression set for export: %s on %zu sample(s)",
                                  GetSampleCodecLabel(codec),
                                  done);
            if (skipped_lossy > 0 && n > 0 && static_cast<size_t>(n) < sizeof(status))
            {
                n += std::snprintf(status + n, sizeof(status) - static_cast<size_t>(n),
                                   ", %zu already compressed (skipped)", skipped_lossy);
            }
            if (m_batch_compress_unassigned_skipped > 0 && n > 0 && static_cast<size_t>(n) < sizeof(status))
            {
                n += std::snprintf(status + n, sizeof(status) - static_cast<size_t>(n),
                                   ", %d unassigned (skipped)", m_batch_compress_unassigned_skipped);
            }
        }
        if (failed > 0)
        {
            const size_t len = std::strlen(status);
            std::snprintf(status + len, sizeof(status) - len, ", %zu failed", failed);
        }
        SetStatus(status);
    }

    void DrawBatchCompressDialog()
    {
        if (m_batch_compress_open)
        {
            ImGui::OpenPopup("Compress Samples");
            m_batch_compress_open = false;
        }
        ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Compress Samples", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        size_t option_count = 0;
        const SampleCodecOption *options = GetSampleCodecOptions(&option_count);
        if (m_batch_compress_codec_index < 0 ||
            m_batch_compress_codec_index >= static_cast<int>(option_count) ||
            options[static_cast<size_t>(m_batch_compress_codec_index)].type ==
                BAE_EDITOR_COMPRESSION_DONT_CHANGE)
        {
            m_batch_compress_codec_index = 0;
            for (size_t i = 0; i < option_count; ++i)
            {
                if (options[i].type == BAE_EDITOR_COMPRESSION_ADPCM)
                {
                    m_batch_compress_codec_index = static_cast<int>(i);
                    break;
                }
            }
        }

        ImGui::Text("Compress %s: %s", m_batch_compress_scope.c_str(), BatchCompressTargetSummary().c_str());
        if (m_batch_compress_unassigned_skipped > 0)
        {
            ImGui::TextDisabled("%d unassigned sample(s) will be skipped.", m_batch_compress_unassigned_skipped);
        }
        ImGui::Spacing();

        if (ImGui::Checkbox("Player 2.0 compatible", &m_batch_compress_player20))
        {
            if (m_batch_compress_player20 &&
                !SeIsPlayer20Codec(options[static_cast<size_t>(m_batch_compress_codec_index)].type))
            {
                for (size_t i = 0; i < option_count; ++i)
                {
                    if (options[i].type == BAE_EDITOR_COMPRESSION_ADPCM)
                    {
                        m_batch_compress_codec_index = static_cast<int>(i);
                        break;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Restrict codecs to classic RMF-safe types (PCM / ADPCM / MP3)");
        }

        ImGui::SetNextItemWidth(240.0f);
        const BAERmfEditorCompressionType current =
            options[static_cast<size_t>(m_batch_compress_codec_index)].type;
        const char *preview = current == BAE_EDITOR_COMPRESSION_PCM
                                  ? "PCM (clear pending compression)"
                                  : options[static_cast<size_t>(m_batch_compress_codec_index)].label;
        if (ImGui::BeginCombo("Compression", preview))
        {
            for (size_t i = 0; i < option_count; ++i)
            {
                if (options[i].type == BAE_EDITOR_COMPRESSION_DONT_CHANGE)
                {
                    continue;
                }
                if (m_batch_compress_player20 && !SeIsPlayer20Codec(options[i].type))
                {
                    continue;
                }
                const bool selected = (m_batch_compress_codec_index == static_cast<int>(i));
                const char *label = options[i].type == BAE_EDITOR_COMPRESSION_PCM
                                        ? "PCM (clear pending compression)"
                                        : options[i].label;
                if (ImGui::Selectable(label, selected))
                {
                    m_batch_compress_codec_index = static_cast<int>(i);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const bool clearing = (current == BAE_EDITOR_COMPRESSION_PCM);
        ImGui::BeginDisabled(clearing);
        static const char *storage_labels[] = {"ESND", "CSND", "SND"};
        int storage_index = std::clamp(static_cast<int>(m_batch_compress_storage), 0, 2);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Storage", &storage_index, storage_labels, IM_ARRAYSIZE(storage_labels)))
        {
            m_batch_compress_storage = static_cast<BAERmfEditorSndStorageType>(storage_index);
        }
        if (IsOpusCompressionType(current))
        {
            ImGui::SameLine();
            static const char *opus_mode_labels[] = {"Opus Audio", "Opus Voice"};
            int opus_mode = std::clamp(static_cast<int>(m_batch_compress_opus_mode), 0, 1);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Opus Mode", &opus_mode, opus_mode_labels, IM_ARRAYSIZE(opus_mode_labels)))
            {
                m_batch_compress_opus_mode = static_cast<BAERmfEditorOpusMode>(opus_mode);
            }
        }
        ImGui::Checkbox("Skip samples already stored compressed", &m_batch_compress_skip_lossy);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Bank samples that are already ADPCM/MP3/etc. with no uncompressed\n"
                              "original are left as-is (re-encoding them only loses quality).");
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::TextDisabled("Like Sample Editor Apply: bank samples are encoded on Export.");
        ImGui::Separator();
        if (ImGui::Button(clearing ? "Clear" : "Compress", ImVec2(120.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            PerformBatchCompress();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    void OpenBatchCompressDialogForSampleRows(const std::vector<int> &, const char *) {}
    void OpenBatchCompressSelectedSamples() {}
    void OpenBatchCompressAllSamples() {}
    void OpenBatchCompressSelectedInstruments() {}
    void DrawBatchCompressDialog() {}
#endif
