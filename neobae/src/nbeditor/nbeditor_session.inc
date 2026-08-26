/* Session shell, Song Info, Instrument Editor, and export helpers — included inside NbEditorApp. */

    void DrawMainMenuBar()
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Session", "Ctrl+N"))
            {
                RequestNewSessionConfirm();
            }
            if (ImGui::MenuItem("Open / Import...", "Ctrl+O"))
            {
                OpenLoadDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Session", "Ctrl+S", false, CanSaveSessionNow()))
            {
                RequestSaveSession();
            }
            if (ImGui::MenuItem("Save Session As...",
                                "Ctrl+Shift+S",
                                false,
                                CanSaveSessionNow()))
            {
                RequestSaveSessionAs();
            }
            if (ImGui::MenuItem("Revert Session to Saved",
                                nullptr,
                                false,
                                !m_loaded_session_path.empty()))
            {
                RequestRevertSessionConfirm();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Reload the session file from disk, discarding unsaved changes.");
            }
            if (ImGui::MenuItem("Session Info..."))
            {
                OpenSessionInfoDialog();
            }
            if (ImGui::BeginMenu("Recent Sessions", !m_recent_sessions.empty()))
            {
                for (size_t i = 0; i < m_recent_sessions.size(); ++i)
                {
                    const std::string &path = m_recent_sessions[i];
                    const std::string label = FileNameFromPath(path).empty() ? path : FileNameFromPath(path);
                    char item[512];
                    std::snprintf(item, sizeof(item), "%zu  %s", i + 1, label.c_str());
                    if (ImGui::MenuItem(item))
                    {
                        ImportPathIntoSession(path);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", path.c_str());
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clear Recent Sessions"))
                {
                    m_recent_sessions.clear();
                    SaveRecentSessionsToDisk();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Song Info...", nullptr, false, m_document != nullptr))
            {
                OpenSongInfoDialog();
            }
            if (ImGui::MenuItem("Export RMF/ZMF...", nullptr, false, m_document != nullptr))
            {
                OpenExportRmfInstrumentDialog();
            }
            if (ImGui::MenuItem("Export MIDI...", nullptr, false, m_document != nullptr))
            {
                RequestExportMidi();
            }
            if (ImGui::MenuItem("Export as Audio...", nullptr, false, m_document != nullptr || m_song != nullptr))
            {
                OpenExportAudioDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export Bank...", nullptr, false, m_bank_token != 0))
            {
                OpenExportBankDialog();
            }
            ImGui::Separator();
#if _BUILT_IN_PATCHES == TRUE
            if (ImGui::MenuItem("Load Built-in Bank", nullptr, false, m_mixer != nullptr))
            {
                RequestLoadBuiltinBankConfirm();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                     ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Replace the current bank with the built-in patch bank.");
            }
#endif
            if (ImGui::MenuItem("Unload Bank",
                                nullptr,
                                false,
                                m_bank_token != 0 && Bank01HasInstruments()))
            {
                RequestUnloadBankConfirm();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                     ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip(
                    "Remove Bank 0/1 instruments and unused built-in samples. Bank 2+ is kept.");
            }
            if (ImGui::MenuItem("Clean Unused Samples",
                                nullptr,
                                false,
                                m_bank_token != 0 && CountUnusedBankSamples() > 0))
            {
                CleanUnusedSamplesToTrash();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                     ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip(
                    "Move all unassigned bank samples to Trash (not referenced by any instrument).");
            }
            if (ImGui::MenuItem("Remove Aliases for Non-existent Instruments",
                                nullptr,
                                false,
                                m_bank_token != 0 && CountOrphanAliases() > 0))
            {
                RequestRemoveOrphanAliases();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                     ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip(
                    "Drop bank aliases whose target instrument is gone.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                RequestQuitConfirm();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Song"))
        {
            if (ImGui::MenuItem("Play", nullptr, false, m_document != nullptr || m_song != nullptr))
            {
                PlaySessionSong();
            }
            if (ImGui::MenuItem("Stop", nullptr, false, m_song != nullptr))
            {
                StopSessionSong();
            }
            if (ImGui::MenuItem("Song Info...", nullptr, false, m_document != nullptr))
            {
                OpenSongInfoDialog();
            }
            if (ImGui::MenuItem("Song Settings...", nullptr, false, m_document != nullptr || m_song != nullptr))
            {
                OpenSongSettingsDialog();
            }
            if (ImGui::MenuItem("MIDI Editor...", nullptr, false, m_document != nullptr))
            {
                OpenMidiEditor();
            }
            if (ImGui::MenuItem("MIDI Note Remapper...", nullptr, false, m_document != nullptr))
            {
                OpenMidiNoteRemapperDialog();
            }
            if (ImGui::MenuItem("Lyrics...", nullptr, false, m_document != nullptr))
            {
                OpenLyricsEditor();
            }
            if (ImGui::MenuItem("Export MIDI...", nullptr, false, m_document != nullptr))
            {
                RequestExportMidi();
            }
            if (ImGui::MenuItem("Export as Audio...", nullptr, false, m_document != nullptr || m_song != nullptr))
            {
                OpenExportAudioDialog();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::BeginMenu("Preferences"))
            {
                if (ImGui::MenuItem("General..."))
                {
                    OpenEditorPreferencesDialog();
                }
                if (ImGui::MenuItem("Module Import Settings..."))
                {
                    OpenModuleImportSettingsDialog();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                {
                    ImGui::SetTooltip(
                        "Options applied when opening tracker modules\n"
                        "(MOD/S3M/XM/IT/…). Saved to app preferences.");
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Undocked layout",
                                nullptr,
                                m_session_undocked_layout))
            {
                m_session_undocked_layout = !m_session_undocked_layout;
                ApplyWorkspaceLayoutMode();
                SetStatus(m_session_undocked_layout
                              ? "Undocked layout (floating Player / Session / Status)"
                              : "Docked layout (Player / Session / Status)");
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip(
                    "BE2-style floating dashboard for Player / Session / Status.\n"
                    "Off = docked workspace (default). Per-session; app default in Preferences.");
            }
            if (ImGui::MenuItem("Reset Layout"))
            {
                ResetSessionLayoutToDefault();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip(
                    "Restore default Player / Session / Status layout for the current mode\n"
                    "(docked or undocked). Does not change Undocked layout on/off.\n"
                    "Closes floating editor windows (IE, Sample Editor, etc.).");
            }
            ImGui::Separator();
            {
                const int trash_n = CountBankTrash();
                char trash_label[64];
                if (trash_n > 0)
                {
                    std::snprintf(trash_label, sizeof(trash_label), "Trash... (%d)", trash_n);
                }
                else
                {
                    std::snprintf(trash_label, sizeof(trash_label), "Trash...");
                }
                if (ImGui::MenuItem(trash_label, nullptr, false, m_bank_token != 0))
                {
                    OpenTrashWindow();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                         ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip(
                        "Browse soft-deleted samples/instruments.\n"
                        "Restore, delete permanently, or Empty Trash.\n"
                        "Also recovers legacy TRSH left by older ZSN saves.");
                }
                if (ImGui::MenuItem("Empty Trash...",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && trash_n > 0))
                {
                    m_confirm_empty_trash_open = true;
                    m_trash_open = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A"))
            {
                SessionSelectAllVisible();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cut",
                               "Ctrl+X",
                               false,
                               m_session_tab == SessionTab::Songs
                                   ? (m_session_song_index >= 0 || !m_multi_songs.empty())
                                   : m_session_tab == SessionTab::Instruments
                                         ? (m_selected_instrument >= 0 || !m_multi_instruments.empty())
                                         : (m_selected_sample >= 0 || !m_multi_samples.empty())))
            {
                switch (m_session_tab)
                {
                case SessionTab::Songs:
                    CopySongsSelection(true);
                    break;
                case SessionTab::Instruments:
                    CutInstrumentsSelection();
                    break;
                case SessionTab::Samples:
                    CopySamplesSelection(true);
                    break;
                }
            }
            if (ImGui::MenuItem("Copy",
                               "Ctrl+C",
                               false,
                               m_session_tab == SessionTab::Songs
                                   ? (m_session_song_index >= 0 || !m_multi_songs.empty())
                                   : m_session_tab == SessionTab::Instruments
                                         ? (m_selected_instrument >= 0 || !m_multi_instruments.empty())
                                         : (m_selected_sample >= 0 || !m_multi_samples.empty())))
            {
                switch (m_session_tab)
                {
                case SessionTab::Songs:
                    CopySongsSelection(false);
                    break;
                case SessionTab::Instruments:
                    BeginCopyInstrumentsForClip();
                    break;
                case SessionTab::Samples:
                    CopySamplesSelection(false);
                    break;
                }
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, CanPasteForSessionTab(m_session_tab)))
            {
                switch (m_session_tab)
                {
                case SessionTab::Songs:
                    PasteSongsClipboard();
                    break;
                case SessionTab::Instruments:
                    PasteInstrumentsSelection();
                    break;
                case SessionTab::Samples:
                    PasteSamplesSelection();
                    break;
                }
            }
            if (ImGui::MenuItem("Copy Instrument",
                               nullptr,
                               false,
                               m_selected_instrument >= 0 &&
                                   m_selected_instrument < static_cast<int>(m_instruments.size()) &&
                                   !m_selected_empty_slot))
            {
                BeginCopyInstrumentsForClip();
            }
            if (ImGui::MenuItem("Paste Instrument...",
                               nullptr,
                               false,
                               CanPasteInstruments()))
            {
                PasteInstrumentsSelection();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear compression cache..."))
            {
                RequestClearCompressionCacheConfirm();
            }
            if (ImGui::MenuItem("Delete uncompressed originals..."))
            {
                RequestDeleteUncompressedConfirm();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Tips..."))
            {
                OpenTipsDialog(false);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip(
                    "Browse editor tips.\n"
                    "Startup tips can be toggled in Preferences → General.");
            }
            if (ImGui::MenuItem("About NeoBAE Editor..."))
            {
                OpenAboutDialog();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    void ClearSessionSongState(bool clear_bank_custom_state = true)
    {
        m_session_songs.clear();
        m_session_song_index = -1;
        m_session_casd_count = 0;
        m_loaded_session_path.clear();
        /* Stale Song Info title otherwise sticks into Save Session As / export
         * suggested filenames after New Session or Open as Session. */
        m_song_info_title[0] = 0;
        if (clear_bank_custom_state)
        {
            m_session_pcm_cache.clear();
            m_app_compression_cache.clear();
            m_session_custom_snd_ids.clear();
            m_session_custom_inst_ids.clear();
            m_bank01_touched = false;
        }
    }

    bool HasSessionContentWorthKeeping() const
    {
        if (!m_loaded_session_path.empty() || !m_session_songs.empty() || m_document != nullptr)
        {
            return true;
        }
        if (!m_session_custom_snd_ids.empty() || !m_session_custom_inst_ids.empty() ||
            !m_session_pcm_cache.empty())
        {
            return true;
        }
        return false;
    }

    bool SessionHasUnsavedChanges() const
    {
        if (!HasSessionContentWorthKeeping())
        {
            return false;
        }
        if (m_document_dirty || m_ie_dirty || m_bank01_touched)
        {
            return true;
        }
#if NBEDITOR_MVP
        if (m_sample_editor_dirty)
        {
            return true;
        }
#endif
        /* Content exists but was never saved to a session file. */
        if (m_loaded_session_path.empty())
        {
            return true;
        }
        return false;
    }

    bool PathWouldReplaceSession(const std::string &selected_path) const
    {
        std::string lower = ToLower(selected_path);
        std::string ext;
        const size_t dot = lower.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < lower.size())
        {
            ext = lower.substr(dot + 1);
        }
        /* Session / bank loads replace current session content.
         * RMF/ZMF use a dedicated Open-as-Session / Add-to-Session chooser. */
        if (ext == "bsn" || ext == "zsn")
        {
            return true;
        }
        if (IsBankExtension(ext) || ext == "zsb")
        {
            return true;
        }
        return false;
    }

    void RequestQuitConfirm()
    {
        /* Drop tips first so a stuck modal cannot block Quit / the confirm. */
        DismissTipsDialog();
        if (SessionHasUnsavedChanges())
        {
            m_confirm_quit_open = true;
            return;
        }
        m_request_quit = true;
    }

    void RequestRevertSessionConfirm()
    {
        if (m_loaded_session_path.empty())
        {
            SetStatus("No saved session to revert");
            return;
        }
        m_pending_replace_session_path = m_loaded_session_path;
        m_confirm_replace_session_open = true;
    }

    void DrawReplaceSessionConfirmDialog()
    {
        if (m_confirm_replace_session_open)
        {
            ImGui::OpenPopup("Replace Session / Bank");
        }
        if (!ImGui::BeginPopupModal("Replace Session / Bank",
                                    &m_confirm_replace_session_open,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        const bool is_revert =
            !m_loaded_session_path.empty() &&
            m_pending_replace_session_path == m_loaded_session_path;
        if (is_revert)
        {
            ImGui::TextWrapped(
                "Revert the current session to the last saved file on disk?\n%s",
                m_pending_replace_session_path.c_str());
        }
        else
        {
            ImGui::TextWrapped(
                "Opening this file will replace the current session and/or bank:\n%s",
                m_pending_replace_session_path.c_str());
        }
        ImGui::Spacing();
        if (SessionHasUnsavedChanges())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "The current session has unsaved changes.");
        }
        if (!is_revert && ActiveBankHasContentToOverwrite())
        {
            const bool session_doc =
                EndsWith(m_pending_replace_session_path, ".bsn") ||
                EndsWith(m_pending_replace_session_path, ".zsn");
            if (session_doc)
            {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                    "Opening a Session file replaces the active bank token.");
            }
            else
            {
                ImGui::TextDisabled(
                    "Bank 0/1 (including prior session overrides) are replaced. "
                    "Bank 2+ instruments are kept when present.");
            }
        }
        if (!is_revert)
        {
            ImGui::TextDisabled("MIDI/modules that only append to the session do not ask.");
        }
        ImGui::Separator();
        if (ImGui::Button(is_revert ? "Discard and Revert" : "Discard and Open", ImVec2(160.0f, 0.0f)))
        {
            const std::string path = m_pending_replace_session_path;
            m_confirm_replace_session_open = false;
            m_pending_replace_session_path.clear();
            ImGui::CloseCurrentPopup();
            ImportPathIntoSession(path, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_confirm_replace_session_open = false;
            m_pending_replace_session_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawRmfOpenChoiceDialog()
    {
        /* Shared chooser for RMF/ZMF and tracker modules. */
        std::string pending_ext;
        {
            const std::string lower = ToLower(m_pending_rmf_open_path);
            const size_t dot = lower.find_last_of('.');
            if (dot != std::string::npos && dot + 1 < lower.size())
            {
                pending_ext = lower.substr(dot + 1);
            }
        }
        const bool pending_module = IsModuleExtension(pending_ext);
        const char *popup_title = pending_module ? "Open Module" : "Open RMF / ZMF";

        if (m_confirm_rmf_open_open)
        {
            ImGui::OpenPopup(popup_title);
        }
        if (!ImGui::BeginPopupModal(popup_title,
                                    &m_confirm_rmf_open_open,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextWrapped("%s", m_pending_rmf_open_path.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Open as Session replaces the current session (embeds become custom bank instruments).\n"
            "Add to Session keeps the current session and appends this song.");
        if (SessionHasUnsavedChanges())
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "The current session has unsaved changes.");
        }
        ImGui::Separator();
        if (ImGui::Button("Open as Session", ImVec2(150.0f, 0.0f)))
        {
            const std::string path = m_pending_rmf_open_path;
            m_confirm_rmf_open_open = false;
            m_pending_rmf_open_path.clear();
            ImGui::CloseCurrentPopup();
            if (pending_module)
            {
                (void)OpenModuleAsSession(path);
            }
            else
            {
                const char *stype = EndsWith(path, ".zmf") ? "ZMF" : "RMF";
                (void)OpenRmfAsSession(path, stype);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Add to Session", ImVec2(140.0f, 0.0f)))
        {
            const std::string path = m_pending_rmf_open_path;
            m_confirm_rmf_open_open = false;
            m_pending_rmf_open_path.clear();
            ImGui::CloseCurrentPopup();
            if (pending_module)
            {
                (void)AddModuleToSession(path);
            }
            else
            {
                const char *stype = EndsWith(path, ".zmf") ? "ZMF" : "RMF";
                (void)ImportSongDocumentAsNewSessionEntry(path, stype);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_confirm_rmf_open_open = false;
            m_pending_rmf_open_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawQuitConfirmDialog()
    {
        if (m_confirm_quit_open)
        {
            ImGui::OpenPopup("Quit");
        }
        if (!ImGui::BeginPopupModal("Quit", &m_confirm_quit_open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextWrapped("Quit nbeditor?");
        ImGui::Spacing();
        if (SessionHasUnsavedChanges())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "The current session has unsaved changes.");
        }
        ImGui::Separator();
        if (ImGui::Button("Discard and Quit", ImVec2(150.0f, 0.0f)))
        {
            m_confirm_quit_open = false;
            m_request_quit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_confirm_quit_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void RequestUnloadBankConfirm()
    {
        if (!m_bank_token || !Bank01HasInstruments())
        {
            SetStatus("Bank 0/1 already empty");
            return;
        }
        m_confirm_unload_bank_open = true;
    }

    void RequestLoadBuiltinBankConfirm()
    {
        if (!m_mixer)
        {
            return;
        }
        if (ActiveBankHasContentToOverwrite() || SessionHasUnsavedChanges())
        {
            m_confirm_load_builtin_open = true;
            return;
        }
        LoadBuiltinBankAsActive();
    }

    void RequestNewSessionConfirm()
    {
        if (HasSessionContentWorthKeeping() || ActiveBankHasContentToOverwrite() ||
            SessionHasUnsavedChanges() || m_document != nullptr || m_song != nullptr)
        {
            m_confirm_new_session_open = true;
            return;
        }
        NewSession();
    }

#if USE_NEO_EFFECTS == TRUE
    void SyncCustomReverbDialogFromEngine()
    {
        m_custom_reverb_comb_count = GetNeoCustomReverbCombCount();
        if (m_custom_reverb_comb_count < 1)
        {
            m_custom_reverb_comb_count = 1;
        }
        if (m_custom_reverb_comb_count > NEO_CUSTOM_MAX_COMBS)
        {
            m_custom_reverb_comb_count = NEO_CUSTOM_MAX_COMBS;
        }
        for (int i = 0; i < NEO_CUSTOM_MAX_COMBS; ++i)
        {
            m_custom_reverb_delays[i] = GetNeoCustomReverbCombDelay(i);
            m_custom_reverb_feedback[i] = GetNeoCustomReverbCombFeedback(i);
            m_custom_reverb_gain[i] = GetNeoCustomReverbCombGain(i);
        }
        m_custom_reverb_lowpass = GetNeoCustomReverbLowpass();
        m_custom_reverb_mix = GetNeoReverbMix();
    }

    void OpenCustomReverbDialog()
    {
        SyncCustomReverbDialogFromEngine();
        m_custom_reverb_open = true;
    }

    void DrawCustomReverbDialog()
    {
        if (!m_custom_reverb_open)
        {
            return;
        }
        bool open = m_custom_reverb_open;
        if (ImGui::Begin("Custom Reverb Settings", &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (ImGui::SliderInt("Comb Count",
                                &m_custom_reverb_comb_count,
                                1,
                                NEO_CUSTOM_MAX_COMBS))
            {
                SetNeoCustomReverbCombCount(m_custom_reverb_comb_count);
            }

            const int display_count = std::min(m_custom_reverb_comb_count, NEO_CUSTOM_MAX_COMBS);
            for (int i = 0; i < display_count; ++i)
            {
                ImGui::PushID(i);
                char comb_header[32];
                std::snprintf(comb_header, sizeof(comb_header), "Comb %d", i + 1);
                ImGui::SeparatorText(comb_header);
                if (ImGui::SliderInt("Delay (ms)",
                                    &m_custom_reverb_delays[i],
                                    1,
                                    NEO_CUSTOM_MAX_DELAY_MS))
                {
                    SetNeoCustomReverbCombDelay(i, m_custom_reverb_delays[i]);
                }
                if (ImGui::SliderInt("Feedback",
                                    &m_custom_reverb_feedback[i],
                                    0,
                                    NEO_CUSTOM_MAX_FEEDBACK))
                {
                    SetNeoCustomReverbCombFeedback(i, m_custom_reverb_feedback[i]);
                }
                if (ImGui::SliderInt("Gain",
                                    &m_custom_reverb_gain[i],
                                    0,
                                    NEO_CUSTOM_MAX_GAIN))
                {
                    SetNeoCustomReverbCombGain(i, m_custom_reverb_gain[i]);
                }
                ImGui::PopID();
            }

            ImGui::Separator();
            if (ImGui::SliderInt("Low-pass",
                                &m_custom_reverb_lowpass,
                                0,
                                NEO_CUSTOM_MAX_LOWPASS))
            {
                SetNeoCustomReverbLowpass(m_custom_reverb_lowpass);
            }
            if (ImGui::SliderInt("Wet/Dry Mix", &m_custom_reverb_mix, 0, 255))
            {
                SetNeoReverbMix(m_custom_reverb_mix);
            }
            ImGui::TextDisabled("Changes apply in real time.");
            if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
            {
                open = false;
            }
        }
        ImGui::End();
        m_custom_reverb_open = open;
    }
#else
    void OpenCustomReverbDialog()
    {
        SetStatus("Neo reverb editing is not available in this build");
    }
#endif

    void DrawBankDestructiveConfirmDialogs()
    {
        if (m_confirm_unload_bank_open)
        {
            ImGui::OpenPopup("Unload Bank 0/1");
        }
        if (ImGui::BeginPopupModal("Unload Bank 0/1",
                                   &m_confirm_unload_bank_open,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(
                "Unload Bank 0 and Bank 1 instruments and permanently remove their\n"
                "built-in samples? Bank 2+ and custom samples are kept.\n"
                "(This does not use Trash — it is not undoable.)");
            ImGui::Spacing();
            ImGui::TextDisabled(
                "Samples still used by Bank 2+, and custom/session samples, are kept.");
            if (m_bank01_touched)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "Bank 0/1 have been edited in this session.");
            }
            ImGui::Separator();
            if (ImGui::Button("Unload Bank", ImVec2(140.0f, 0.0f)))
            {
                UnloadBank01KeepingBank2();
                m_confirm_unload_bank_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            {
                m_confirm_unload_bank_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (m_confirm_load_builtin_open)
        {
            ImGui::OpenPopup("Load Built-in Bank");
        }
        if (ImGui::BeginPopupModal("Load Built-in Bank",
                                   &m_confirm_load_builtin_open,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(
                "Replace Bank 0/1 with the built-in patch bank?");
            ImGui::Spacing();
            ImGui::TextDisabled(
                "Same as Unload Bank, then load built-in Bank 0/1. Bank 2+ and custom samples are kept.");
            if (m_bank01_touched)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "Bank 0/1 have been edited in this session.");
            }
            ImGui::Separator();
            if (ImGui::Button("Load Built-in Bank", ImVec2(170.0f, 0.0f)))
            {
                LoadBuiltinBankAsActive();
                m_confirm_load_builtin_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            {
                m_confirm_load_builtin_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (m_confirm_new_session_open)
        {
            ImGui::OpenPopup("New Session");
        }
        if (ImGui::BeginPopupModal("New Session",
                                   &m_confirm_new_session_open,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(
                "Start a new session? This clears songs/document and unloads the current bank "
                "(including Bank 0/1 and custom samples), then loads the built-in bank.");
            if (SessionHasUnsavedChanges())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "Unsaved session changes will be discarded.");
            }
            ImGui::Separator();
            if (ImGui::Button("New Session", ImVec2(140.0f, 0.0f)))
            {
                NewSession();
                m_confirm_new_session_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            {
                m_confirm_new_session_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void DrawDeleteSongConfirmDialog()
    {
        if (m_confirm_delete_song_open)
        {
            ImGui::OpenPopup("Delete Song");
        }
        if (!ImGui::BeginPopupModal("Delete Song",
                                    &m_confirm_delete_song_open,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        const size_t n = m_pending_delete_song_indices.size();
        if (n == 1 && m_pending_delete_song_indices[0] >= 0 &&
            m_pending_delete_song_indices[0] < static_cast<int>(m_session_songs.size()))
        {
            const SessionSongEntry &entry =
                m_session_songs[static_cast<size_t>(m_pending_delete_song_indices[0])];
            ImGui::TextWrapped("Delete song \"%s\" from the session?", entry.name.c_str());
        }
        else
        {
            ImGui::TextWrapped("Delete %zu song%s from the session?", n, n == 1 ? "" : "s");
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Songs are removed from the session (not moved to Trash).");

        const bool has_unused = (m_pending_delete_song_unused_inst +
                                 m_pending_delete_song_unused_snd) > 0;
        if (has_unused)
        {
            ImGui::Spacing();
            ImGui::Checkbox("Also move unused custom instruments/samples to Trash",
                            &m_pending_delete_song_trash_unused);
            ImGui::TextDisabled("%zu instrument%s, %zu sample%s unused by remaining songs",
                                m_pending_delete_song_unused_inst,
                                m_pending_delete_song_unused_inst == 1 ? "" : "s",
                                m_pending_delete_song_unused_snd,
                                m_pending_delete_song_unused_snd == 1 ? "" : "s");
            ImGui::TextDisabled(
                "Restore from Trash remaps to a free ID if the original is taken.");
        }
        else
        {
            m_pending_delete_song_trash_unused = false;
            ImGui::Spacing();
            ImGui::TextDisabled("No unused custom instruments/samples found for these songs.");
        }

        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
        {
            ConfirmDeletePendingSessionSongs();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_confirm_delete_song_open = false;
            m_pending_delete_song_indices.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void NewSession()
    {
        ClearSessionSongState(true);
        if (m_song)
        {
            BAESong_Stop(m_song, FALSE);
            BAESong_Delete(m_song);
            m_song = nullptr;
        }
        if (m_document)
        {
            BAERmfEditorDocument_Delete(m_document);
            m_document = nullptr;
        }
        m_loaded_song_path.clear();
        m_loaded_doc_path.clear();
        m_song_row = SongRow{};
        m_document_dirty = false;
        m_song_started = false;
        ClearSongOverrides();
        /* Full bank wipe (any user/file bank), then built-in Bank 0/1. */
        if (m_mixer)
        {
            BAEMixer_UnloadBanks(m_mixer);
        }
        m_bank_token = 0;
        m_loaded_bank_path.clear();
        m_loaded_bank_display_name.clear();
        m_instruments.clear();
        m_bank_instruments.clear();
        m_samples.clear();
        (void)LoadBuiltinBankAsActive();
        RefreshSongSamplesFromDocument();
        m_instrument_editor_open = false;
        m_song_info_open = false;
        /* Player mix + list UI are session-scoped (nBeT); reset on new session. */
        m_volume_percent = 100;
        m_song_settings_tempo_percent = 100;
        m_transpose_semitones = 0;
        m_reverb_type = static_cast<int>(BAE_REVERB_TYPE_1);
        m_velocity_curve = 1;
        m_song_info_apply_player_reverb = false;
        m_song_info_apply_player_velocity_curve = false;
        m_instrument_list_sort = InstrumentListSort::ByProgram;
        m_sample_list_sort = SampleListSort::ByName;
        m_sample_show_filter = SampleShowFilter::Custom;
        m_song_show_filter = SongShowFilter::Custom;
        m_show_empty_instrument_slots = false;
        m_instrument_text_filter[0] = '\0';
        m_sample_text_filter[0] = '\0';
        m_session_tab = SessionTab::Songs;
        m_nbet_restore_has_list_ui = false;
        m_nbet_restore_session_song_index = -1;
        RebuildInstrumentFilters();
        SelectInstrumentFilterByBankCategory(-2, 0); /* Custom Melodic */
        ApplyPlayerMixSettings();
        m_session_undocked_layout = m_pref_default_undocked_layout;
        ApplyWorkspaceLayoutMode();
        SetStatus("New Session");
    }

    void ImportPathIntoSession(const std::string &selected_path, bool skip_replace_confirm = false)
    {
        std::string lower = ToLower(selected_path);
        std::string ext;
        const size_t dot = lower.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < lower.size())
        {
            ext = lower.substr(dot + 1);
        }

        /* RMF/ZMF / modules: choose Open as Session vs Add to Session. */
        if (ext == "rmf" || ext == "zmf" || IsModuleExtension(ext))
        {
            if (!skip_replace_confirm)
            {
                m_pending_rmf_open_path = selected_path;
                m_confirm_rmf_open_open = true;
                return;
            }
            /* argv / "Open with": default to Open as Session. */
            if (IsModuleExtension(ext))
            {
                (void)OpenModuleAsSession(selected_path);
            }
            else
            {
                (void)OpenRmfAsSession(selected_path, ext == "zmf" ? "ZMF" : "RMF");
            }
            return;
        }

        if (!skip_replace_confirm && PathWouldReplaceSession(selected_path) &&
            (SessionHasUnsavedChanges() || ActiveBankHasContentToOverwrite()))
        {
            m_pending_replace_session_path = selected_path;
            m_confirm_replace_session_open = true;
            return;
        }

        if (ext == "nbs")
        {
            SetStatus("Session files (.nbs) are not supported in nbeditor yet");
            return;
        }
        if (ext == "bsn" || ext == "zsn")
        {
            if (ProbeBe2SessionBsn(selected_path.c_str(), nullptr, nullptr))
            {
                LoadSessionBsnFromPath(selected_path.c_str());
                return;
            }
            if (ext == "zsn")
            {
                SetStatus("Not a NeoBAE Session .zsn (missing session layout)");
                return;
            }
            /* Bank-shaped .bsn: full replace unless real Bank 2+ must stay.
             * Clear ZMF/RMF Bank 0/1 promote flags first so they don't force a
             * merge that keeps orphan custom samples from the prior session. */
            ClearSessionSongState(true);
            LoadBankFromPath(selected_path.c_str(), ShouldPreserveBank2OnBankLoad());
            return;
        }
        if (ext == "mid" || ext == "midi" || ext == "kar" || ext == "rmi")
        {
            (void)ImportSongDocumentAsNewSessionEntry(selected_path, "MIDI");
            return;
        }
        if (IsBankExtension(ext))
        {
            /* Pure bank (.zsb/.hsb): drop prior session custom INST/SND tracking
             * so ZMF promotes don't survive into the new bank (merge only when
             * Bank 2+ instruments are actually present). */
            ClearSessionSongState(true);
            LoadBankFromPath(selected_path.c_str(), ShouldPreserveBank2OnBankLoad());
            return;
        }

        if (LoadSongFromPath(selected_path.c_str()))
        {
            SetStatus("Song loaded (no editable document)");
            return;
        }
        SetStatus("Selected file could not be loaded");
    }

    void UpdateSongRowFromDocument(const std::string &path, const char *source_type)
    {
        m_song_row.path = path;
        m_song_row.source_type = source_type ? source_type : "";
        m_song_row.name = FileNameFromPath(path);
        if (m_document)
        {
            const char *title = BAERmfEditorDocument_GetInfo(m_document, TITLE_INFO);
            if (title && title[0])
            {
                m_song_row.name = title;
            }
        }
        m_loaded_song_path = path;
        m_loaded_doc_path = path;
        m_document_dirty = false;
        RefreshSongSamplesFromDocument();
    }

    bool ReloadPlaybackFromDocument(bool pause_after_start)
    {
        if (!m_mixer || !m_document)
        {
            return false;
        }

        unsigned char *rmf_data = nullptr;
        uint32_t rmf_size = 0;
        /* Always serialize as ZMF for live editor playback so stereo LPF and
         * other zs* features audition correctly. Export still probes RequiresZmf. */
        const BAEResult save_result = BAERmfEditorDocument_SaveAsRmfToMemoryEx(m_document,
                                                                              TRUE,
                                                                              FALSE,
                                                                              &rmf_data,
                                                                              &rmf_size);
        if (save_result != BAE_NO_ERROR || !rmf_data || rmf_size == 0)
        {
            if (rmf_data)
            {
                XDisposePtr((XPTR)rmf_data);
            }
            SetStatus(std::string("Serialize song failed: ") + FormatBAEError(save_result));
            return false;
        }

        if (m_song)
        {
            BAESong_Stop(m_song, FALSE);
            BAESong_Delete(m_song);
            m_song = nullptr;
        }

        BAESong song = BAESong_New(m_mixer);
        if (!song)
        {
            XDisposePtr((XPTR)rmf_data);
            SetStatus("BAESong_New failed");
            return false;
        }

        const BAEResult load_result = BAESong_LoadRmfFromMemory(song, rmf_data, rmf_size, 0, TRUE);
        XDisposePtr((XPTR)rmf_data);
        if (load_result != BAE_NO_ERROR)
        {
            BAESong_Delete(song);
            SetStatus(std::string("Playback load failed: ") + FormatBAEError(load_result));
            return false;
        }
        EnableEditorZsCompatibility(song);

        BAESong_Preroll(song);
        BAESong_SetLoops(song, m_loop_enabled ? 32767 : 0);
        BAEMixer_SetOutputGain(m_mixer, m_volume_percent);
        {
            const float ratio =
                static_cast<float>(std::max(1, m_song_settings_tempo_percent)) / 100.0f;
            (void)BAESong_SetMasterTempo(song, FLOAT_TO_UNSIGNED_FIXED(ratio));
            (void)BAESong_SetTranspose(song, std::clamp(m_transpose_semitones, -24, 24));
        }
        /* Arm with Preroll only when paused: Start+Pause would dispatch and then
         * kill tick-0 notes, so the first note never returns on Resume. */
        if (pause_after_start)
        {
            m_song_started = false;
        }
        else
        {
            /* Priority 1 > bank preview song (0) so audition NoteOns don't steal. */
            BAESong_Start(song, 1);
            m_song_started = true;
        }
        m_song = song;
        m_document_dirty = false;
        m_playback_stale = false;
        ApplyChannelMutes();
        MeApplyTrackMutes();
        RebuildPreviewSongFromDocument();
        ApplyVelocityCurveToSongs();
        LyResetPreview();
        LyWirePreviewCallback();
        return true;
    }

    /* Rebuild the live song so bank ALIS / INST edits reach remapArray.
     * Keeps pause vs play and song position when possible. */
    bool ReloadPlaybackPreservingTransport()
    {
        uint32_t restore_us = 0;
        bool was_paused = true;

        if (!m_document)
        {
            return false;
        }
        if (m_song)
        {
            BAE_BOOL paused = TRUE;
            (void)BAESong_GetMicrosecondPosition(m_song, &restore_us);
            (void)BAESong_IsPaused(m_song, &paused);
            was_paused = (paused != FALSE) || !m_song_started;
        }
        if (!ReloadPlaybackFromDocument(was_paused))
        {
            return false;
        }
        if (m_song && restore_us > 0)
        {
            (void)BAESong_SetMicrosecondPosition(m_song, restore_us);
        }
        return true;
    }

    void RebuildPreviewSongFromDocument()
    {
        if (!m_mixer || !m_document)
        {
            return;
        }
        if (m_preview_song)
        {
            BAESong_Stop(m_preview_song, FALSE);
            BAESong_Delete(m_preview_song);
            m_preview_song = nullptr;
        }
        BAESong preview = BAESong_New(m_mixer);
        if (!preview)
        {
            return;
        }
        unsigned char *rmf_data = nullptr;
        uint32_t rmf_size = 0;
        if (BAERmfEditorDocument_SaveAsRmfToMemoryEx(m_document, TRUE, FALSE, &rmf_data, &rmf_size) !=
                BAE_NO_ERROR ||
            !rmf_data)
        {
            BAESong_Delete(preview);
            return;
        }
        const BAEResult lr = BAESong_LoadRmfFromMemory(preview, rmf_data, rmf_size, 0, TRUE);
        XDisposePtr((XPTR)rmf_data);
        if (lr != BAE_NO_ERROR)
        {
            BAESong_Delete(preview);
            return;
        }
        EnableEditorZsCompatibility(preview);
        BAESong_Preroll(preview);
        /* Start+Pause so IE/audition NoteOn shares the same live-mixer path as
         * the main playback song (Preroll alone leaves some NoteOn paths silent). */
        BAESong_Start(preview, 0);
        BAESong_Pause(preview);
        m_preview_song = preview;
        ApplyVelocityCurveToSongs();
    }

    bool DecodeGroovoidMidiBytes(int index, std::vector<unsigned char> &out_midi) const
    {
        out_midi.clear();
        if (!m_bank_token || index < 0 || index >= static_cast<int>(m_groovoids.size()))
        {
            return false;
        }
        const GroovoidEntry &entry = m_groovoids[static_cast<size_t>(index)];
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        int32_t size = 0;
        const XResourceType obj_type = entry.object_is_midi ? ID_MIDI : ID_EMID;
        XPTR raw = XGetFileResource(bank,
                                    obj_type,
                                    static_cast<XLongResourceID>(entry.emid_resource_id),
                                    nullptr,
                                    &size);
        if (!raw || size <= 0)
        {
            if (raw)
            {
                XDisposePtr(raw);
            }
            return false;
        }
        out_midi.assign(static_cast<const unsigned char *>(raw),
                        static_cast<const unsigned char *>(raw) + static_cast<size_t>(size));
        XDisposePtr(raw);
        if (!entry.object_is_midi)
        {
            /* Decrypt a copy — XDecryptData works in place. */
            XDecryptData(out_midi.data(), static_cast<uint32_t>(out_midi.size()));
        }
        if (out_midi.size() < 4 ||
            !(out_midi[0] == 'M' && out_midi[1] == 'T' && out_midi[2] == 'h' && out_midi[3] == 'd'))
        {
            out_midi.clear();
            return false;
        }
        return true;
    }

    void BeginGroovoidRmfExport(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_groovoids.size()))
        {
            SetStatus("No groovoid selected");
            return;
        }
        m_pending_groovoid_export_index = index;
        m_pending_dialog_action = DialogAction::ExportGroovoidRmf;
        const std::string stem = SanitizeExportFileStem(m_groovoids[static_cast<size_t>(index)].name);
        const std::string default_name = stem + ".rmf";
        static const SDL_DialogFileFilter filters[] = {
            {"RMF / ZMF (*.rmf;*.zmf)", "rmf;zmf"},
            {"All files (*.*)", "*"},
        };
        SDL_ShowSaveFileDialog(OnFileDialogResult,
                               this,
                               m_main_window,
                               filters,
                               static_cast<int>(SDL_arraysize(filters)),
                               default_name.c_str());
    }

    void ExportGroovoidRmfToPath(const std::string &path)
    {
        const int index = m_pending_groovoid_export_index;
        m_pending_groovoid_export_index = -1;
        if (index < 0 || index >= static_cast<int>(m_groovoids.size()))
        {
            SetStatus("No groovoid selected for export");
            return;
        }

        std::vector<unsigned char> midi;
        if (!DecodeGroovoidMidiBytes(index, midi))
        {
            SetStatus("Export failed: could not decode groovoid MIDI");
            return;
        }

        BAERmfEditorDocument *export_doc = BAERmfEditorDocument_LoadFromMemory(
            midi.data(),
            static_cast<uint32_t>(midi.size()),
            BAE_MIDI_TYPE);
        if (!export_doc)
        {
            SetStatus("Export failed: could not build RMF document from groovoid");
            return;
        }

        const GroovoidEntry &entry = m_groovoids[static_cast<size_t>(index)];
        if (!entry.name.empty())
        {
            (void)BAERmfEditorDocument_SetInfo(export_doc, TITLE_INFO, entry.name.c_str());
        }

        std::string out_path = path;
        uint32_t reason = 0;
        const bool requires_zmf = BAERmfEditorDocument_RequiresZmf(export_doc, &reason) != FALSE;
        if (requires_zmf && !EndsWith(out_path, ".zmf"))
        {
            const size_t dot = out_path.find_last_of('.');
            if (dot != std::string::npos)
            {
                out_path = out_path.substr(0, dot) + ".zmf";
            }
            else
            {
                out_path += ".zmf";
            }
        }

        const BAEResult save_r =
            BAERmfEditorDocument_SaveAsRmf(export_doc, const_cast<char *>(out_path.c_str()));
        BAERmfEditorDocument_Delete(export_doc);
        if (save_r == BAE_NO_ERROR)
        {
            char status[256];
            std::snprintf(status,
                          sizeof(status),
                          "Exported groovoid \"%s\" → %s",
                          entry.name.c_str(),
                          FileNameFromPath(out_path).c_str());
            SetStatus(status);
        }
        else
        {
            SetStatus(std::string("Groovoid export failed: ") + FormatBAEError(save_r));
        }
    }

    bool PlayGroovoidAt(int index)
    {
        if (!m_mixer)
        {
            SetStatus("Mixer not ready");
            return false;
        }
        if (index < 0 || index >= static_cast<int>(m_groovoids.size()))
        {
            SetStatus("No groovoid selected");
            return false;
        }

        const GroovoidEntry &entry = m_groovoids[static_cast<size_t>(index)];
        char name_buf[256];
        std::snprintf(name_buf, sizeof(name_buf), "%s", entry.name.c_str());

        if (m_song)
        {
            BAESong_Stop(m_song, FALSE);
            BAESong_Delete(m_song);
            m_song = nullptr;
            m_song_started = false;
        }

        BAESong song = BAESong_New(m_mixer);
        if (!song)
        {
            SetStatus("BAESong_New failed");
            return false;
        }

        const BAEResult load_r = BAESong_LoadGroovoid(song, name_buf, TRUE);
        if (load_r != BAE_NO_ERROR)
        {
            BAESong_Delete(song);
            SetStatus(std::string("Groovoid load failed: ") + FormatBAEError(load_r));
            return false;
        }

        EnableEditorZsCompatibility(song);
        BAESong_Preroll(song);
        BAESong_SetLoops(song, m_loop_enabled ? 32767 : 0);
        BAEMixer_SetOutputGain(m_mixer, m_volume_percent);
        ResetPeakVoices();
        BAESong_Start(song, 1);
        m_song = song;
        m_song_started = true;
        m_groovoid_index = index;
        ApplyChannelMutes();
        SetStatus(std::string("Playing Groovoid ") + entry.name);
        return true;
    }

    void RestartSessionSongFromStart()
    {
        if (!m_song)
        {
            return;
        }
        BAESong_Stop(m_song, FALSE);
        BAESong_SetMicrosecondPosition(m_song, 0);
        BAESong_SetLoops(m_song, m_loop_enabled ? 32767 : 0);
        BAESong_Preroll(m_song);
        ResetPeakVoices();
        BAESong_Start(m_song, 1);
        m_song_started = true;
    }

    void PlaySessionSong()
    {
        /* Always play the selected Session song only (not a playlist). */
        if (!m_session_songs.empty() &&
            (m_session_song_index < 0 ||
             m_session_song_index >= static_cast<int>(m_session_songs.size())))
        {
            if (!ActivateSessionSongAt(0))
            {
                SetStatus("No song selected");
                return;
            }
        }
        /* Count-in owns the transport; Play waits until capture starts. */
        if (m_midi_count_in_active)
        {
            return;
        }
        const bool attach_record = m_midi_record_free_run;
        uint32_t attach_tick = m_midi_playhead_tick;
        if (attach_record)
        {
            attach_tick = MeComputeFreeRunTick();
            MeAttachRecordToSongClock();
            attach_tick = m_midi_playhead_tick;
        }
        if (m_document && (m_document_dirty || m_playback_stale || !m_song))
        {
            if (!ReloadPlaybackFromDocument(false))
            {
                if (attach_record && m_midi_record_armed)
                {
                    MeStartRecordFreeRun();
                }
                return;
            }
            if (attach_record || m_midi_record_armed)
            {
                MeSeekToTick(attach_tick);
                m_midi_metro_last_song_beat = -1;
                m_midi_record_was_playing = true;
                SetStatus(m_midi_record_armed ? "Recording — synced to Play"
                                              : (m_song_row.name.empty()
                                                     ? "Playing Session song"
                                                     : (std::string("Playing ") + m_song_row.name)));
                return;
            }
            SetStatus(m_song_row.name.empty() ? "Playing Session song"
                                             : (std::string("Playing ") + m_song_row.name));
            return;
        }
        if (!m_song)
        {
            SetStatus("No song to play");
            return;
        }
        BAE_BOOL paused_flag = FALSE;
        BAE_BOOL done_flag = FALSE;
        BAESong_IsPaused(m_song, &paused_flag);
        BAESong_IsDone(m_song, &done_flag);
        if (!m_song_started)
        {
            if (attach_record)
            {
                MeSeekToTick(attach_tick);
            }
            BAESong_SetLoops(m_song, m_loop_enabled ? 32767 : 0);
            BAESong_Preroll(m_song);
            ResetPeakVoices();
            /* Priority > preview song so keyboard/SE audition NoteOns don't steal. */
            BAESong_Start(m_song, 1);
            m_song_started = true;
            m_midi_metro_last_song_beat = -1;
            if (m_midi_record_armed)
            {
                m_midi_record_was_playing = true;
                SetStatus("Recording — synced to Play");
            }
        }
        else if (done_flag)
        {
            /* Natural end (loop off): Resume is a no-op on finished tracks. */
            RestartSessionSongFromStart();
            m_midi_metro_last_song_beat = -1;
        }
        else if (paused_flag)
        {
            /* If we're still at the top of the song, Resume cannot re-fire notes
             * already consumed by a prior Start+Pause arming — restart instead. */
            uint32_t pos_us = 0;
            if (BAESong_GetMicrosecondPosition(m_song, &pos_us) == BAE_NO_ERROR &&
                pos_us < 50000u)
            {
                RestartSessionSongFromStart();
                m_midi_metro_last_song_beat = -1;
            }
            else
            {
                BAESong_Resume(m_song);
            }
        }
        else
        {
            BAESong_Pause(m_song);
        }
    }

    void StopSessionSong()
    {
        MeCancelCountIn();
        if (!m_song)
        {
            m_song_started = false;
            return;
        }
        BAESong_Stop(m_song, FALSE);
        BAESong_SetMicrosecondPosition(m_song, 0);
        BAESong_SetLoops(m_song, m_loop_enabled ? 32767 : 0);
        BAESong_Preroll(m_song);
        /* Leave stopped/armed — Start+Pause here would drop tick-0 notes on Play. */
        m_song_started = false;
        m_midi_metro_last_song_beat = -1;
    }

    /* When loop is off and the engine finishes, park like Stop so Play returns. */
    void PollSessionSongFinished()
    {
        if (!m_song || !m_song_started || m_loop_enabled)
        {
            return;
        }
        BAE_BOOL paused_flag = FALSE;
        BAE_BOOL done_flag = FALSE;
        BAESong_IsPaused(m_song, &paused_flag);
        BAESong_IsDone(m_song, &done_flag);
        if (paused_flag)
        {
            return;
        }
        if (done_flag)
        {
            StopSessionSong();
            return;
        }
        /* Fallback if track status never reports done after the timeline ends. */
        uint32_t pos_us = 0;
        uint32_t len_us = 0;
        if (BAESong_GetMicrosecondPosition(m_song, &pos_us) != BAE_NO_ERROR ||
            BAESong_GetMicrosecondLength(m_song, &len_us) != BAE_NO_ERROR ||
            len_us == 0)
        {
            return;
        }
        if (pos_us + 20000u < len_us)
        {
            return;
        }
        BAE_BOOL pending = TRUE;
        if (BAESong_AreMidiEventsPending(m_song, &pending) == BAE_NO_ERROR && !pending)
        {
            StopSessionSong();
        }
    }

    /* Matches CloneUsed / rmfutil create bank-group decoding.
     * Note bank is MIDI (MSB<<7)|LSB; GM MSB 120/121 → HSB group 0. */
    static int ExportBankGroupFromNoteBank(uint16_t internal_bank)
    {
        if (internal_bank < 128)
        {
            return static_cast<int>(internal_bank);
        }
        const int msb = static_cast<int>((internal_bank >> 7) & 0x7F);
        if (msb == 120 || msb == 121)
        {
            return 0;
        }
        return msb;
    }

    enum class ExportSelectionMode
    {
        All = 0,
        Custom, /* bank 2+ / custom only */
        None,
    };

    /* Always list every instrument the song uses; selection is via checkboxes. */
    void RebuildExportInstrumentList()
    {
        m_export_items.clear();
        m_export_embed_inst_ids.clear();
        if (!m_document || !m_bank_token)
        {
            return;
        }

        struct UsedInst
        {
            uint32_t requested_inst_id = 0;
            uint32_t resolved_inst_id = 0;
            uint32_t instrument_index = 0;
            int bank = 0;
            int program = 0;
            bool percussion = false;
            std::string name;
        };
        std::map<uint32_t, UsedInst> used_by_inst;
        std::set<uint32_t> requested_seen;
        std::set<uint32_t> resolve_cache_success;
        std::set<uint32_t> resolve_cache_failed;

        uint16_t track_count = 0;
        if (BAERmfEditorDocument_GetTrackCount(m_document, &track_count) != BAE_NO_ERROR)
        {
            return;
        }
        for (uint16_t t = 0; t < track_count; ++t)
        {
            uint32_t note_count = 0;
            if (BAERmfEditorDocument_GetNoteCount(m_document, t, &note_count) != BAE_NO_ERROR)
            {
                continue;
            }
            for (uint32_t n = 0; n < note_count; ++n)
            {
                BAERmfEditorNoteInfo note;
                std::memset(&note, 0, sizeof(note));
                if (BAERmfEditorDocument_GetNoteInfo(m_document, t, n, &note) != BAE_NO_ERROR)
                {
                    continue;
                }
                const int bank_group = ExportBankGroupFromNoteBank(note.bank);
                bool percussion = (note.channel == 9);
                uint32_t requested_inst_id =
                    percussion ? (static_cast<uint32_t>(bank_group) * 256u) + 128u + note.note
                               : (static_cast<uint32_t>(bank_group) * 256u) + note.program;
                if (!requested_seen.insert(requested_inst_id).second)
                {
                    continue;
                }

                uint32_t resolved_inst_id = requested_inst_id;
                uint32_t instrument_index = 0;
                if (BAERmfEditorBank_ResolveInstID(m_bank_token,
                                                   requested_inst_id,
                                                   &resolved_inst_id,
                                                   &instrument_index) != BAE_NO_ERROR)
                {
                    continue;
                }
                if (percussion && resolved_inst_id < 128)
                {
                    /* Drum note must not resolve to a melodic instrument (e.g. 118 Synth Drum). */
                    continue;
                }
                if (used_by_inst.count(resolved_inst_id) != 0)
                {
                    if (!percussion)
                    {
                        used_by_inst[resolved_inst_id].percussion = false;
                        used_by_inst[resolved_inst_id].program = static_cast<int>(note.program);
                    }
                    continue;
                }

                UsedInst entry;
                entry.requested_inst_id = requested_inst_id;
                entry.resolved_inst_id = resolved_inst_id;
                entry.instrument_index = instrument_index;
                entry.bank = bank_group;
                entry.program = percussion ? static_cast<int>(note.note) : static_cast<int>(note.program);
                entry.percussion = (resolved_inst_id < 128) ? false : percussion;

                BAERmfEditorBankInstrumentInfo info;
                std::memset(&info, 0, sizeof(info));
                if (BAERmfEditorBank_GetInstrumentInfo(m_bank_token, instrument_index, &info) ==
                        BAE_NO_ERROR &&
                    info.name[0])
                {
                    entry.name = info.name;
                    entry.bank = static_cast<int>(info.bank);
                    entry.program = static_cast<int>(info.program);
                }
                else
                {
                    char fallback[64];
                    std::snprintf(fallback,
                                  sizeof(fallback),
                                  percussion ? "Drum B%d N%03d" : "Inst B%d P%03d",
                                  bank_group,
                                  entry.program);
                    entry.name = fallback;
                }
                used_by_inst[resolved_inst_id] = std::move(entry);
            }
        }

        std::vector<UsedInst> ordered;
        ordered.reserve(used_by_inst.size());
        for (auto &kv : used_by_inst)
        {
            ordered.push_back(std::move(kv.second));
        }
        std::sort(ordered.begin(), ordered.end(), [](const UsedInst &a, const UsedInst &b) {
            if (a.bank != b.bank)
            {
                return a.bank < b.bank;
            }
            if (a.percussion != b.percussion)
            {
                return static_cast<int>(a.percussion) < static_cast<int>(b.percussion);
            }
            return a.program < b.program;
        });

        int custom_index = 0;
        for (const UsedInst &used : ordered)
        {
            const bool is_custom =
                (used.bank >= 2) ||
                (m_session_custom_inst_ids.count(used.resolved_inst_id) != 0);
            ExportInstrumentItem item;
            item.inst_id = used.resolved_inst_id;
            item.requested_inst_id = used.requested_inst_id;
            item.instrument_index = used.instrument_index;
            item.name = used.name;
            item.is_custom = is_custom;
            item.percussion = used.percussion;
            item.custom_index = is_custom ? custom_index++ : -1;
            /* BE2 default: only custom instruments are embedded. */
            item.mode = is_custom ? ExportInstMode::Embed : ExportInstMode::Off;
            item.selected = (item.mode == ExportInstMode::Embed);
            item.bank = used.bank;
            item.program = used.program;
            m_export_items.push_back(std::move(item));
        }
    }

    void ApplyExportInstrumentSelection(ExportSelectionMode mode)
    {
        for (ExportInstrumentItem &item : m_export_items)
        {
            switch (mode)
            {
            case ExportSelectionMode::All:
                item.mode = ExportInstMode::Embed;
                break;
            case ExportSelectionMode::None:
                item.mode = ExportInstMode::Off;
                break;
            case ExportSelectionMode::Custom:
                item.mode = item.is_custom ? ExportInstMode::Embed : ExportInstMode::Off;
                break;
            }
            item.selected = (item.mode == ExportInstMode::Embed);
        }
    }

    void SyncExportEmbedSetFromItems()
    {
        m_export_embed_inst_ids.clear();
        m_export_ghost_inst_ids.clear();
        for (const ExportInstrumentItem &item : m_export_items)
        {
            /* INST id 0 is valid (B0P000) — do not skip. */
            if (item.mode == ExportInstMode::Embed)
            {
                m_export_embed_inst_ids.insert(item.inst_id);
                if (item.requested_inst_id != item.inst_id)
                {
                    m_export_embed_inst_ids.insert(item.requested_inst_id);
                }
            }
            else if (item.mode == ExportInstMode::Ghost)
            {
                m_export_ghost_inst_ids.insert(item.inst_id);
                if (item.requested_inst_id != item.inst_id)
                {
                    m_export_ghost_inst_ids.insert(item.requested_inst_id);
                }
            }
        }
    }

    void OpenExportRmfInstrumentDialog()
    {
        if (!m_document)
        {
            SetStatus("No song document to export");
            return;
        }
        if (!m_bank_token)
        {
            SetStatus("Load a bank before exporting RMF/ZMF");
            return;
        }
        /* Open immediately; heavy list/ZMF scan runs on first dialog frame. */
        m_export_items.clear();
        m_export_embed_inst_ids.clear();
        m_export_ghost_inst_ids.clear();
        m_export_rmf_needs_zmf = false;
        m_export_rmf_z_reason = 0;
        m_export_snd_storage_index = static_cast<int>(BAE_EDITOR_SND_STORAGE_SND);
        m_export_rmf_prepare_pending = true;
        m_export_rmf_open = true;
    }

    void FinishExportRmfDialogPrepare()
    {
        RebuildExportInstrumentList();
        ApplyExportInstrumentSelection(ExportSelectionMode::All);
        uint32_t reason = 0;
        bool needs_z = false;
        if (m_document && BAERmfEditorDocument_RequiresZmf(m_document, &reason) != FALSE)
        {
            reason &= ~(uint32_t)BAEZMF_ALREADY_ZMF;
            needs_z = (reason != 0);
        }
        if (!needs_z && BankRequiresZsb(&reason))
        {
            needs_z = true;
        }
        m_export_rmf_needs_zmf = needs_z;
        m_export_rmf_z_reason = reason;
        m_export_rmf_prepare_pending = false;
    }

    void DrawExportRmfDialog()
    {
        if (m_export_rmf_open)
        {
            ImGui::OpenPopup("Export RMF File");
        }
        ImGui::SetNextWindowSize(ImVec2(480, 400), ImGuiCond_FirstUseEver);
        bool open = m_export_rmf_open;
        if (!ImGui::BeginPopupModal("Export RMF File", &open, ImGuiWindowFlags_None))
        {
            m_export_rmf_open = open;
            return;
        }
        m_export_rmf_open = open;

        if (m_export_rmf_prepare_pending)
        {
            ImGui::TextWrapped("Preparing export list…");
            ImGui::EndPopup();
            FinishExportRmfDialogPrepare();
            return;
        }

        ImGui::TextWrapped(
            "Per instrument: Off (bank ref), Embed (INST+samples), or Ghost "
            "(INST only; samples resolve from the host bank). Oscillator "
            "('Don't use sample') instruments embed sample-free in either "
            "Embed or Ghost. Usually embed only instruments you created yourself.");
        if (m_export_rmf_needs_zmf)
        {
            char reason_buf[256] = {0};
            BAEZMFReasonCodeToString(m_export_rmf_z_reason, reason_buf, sizeof(reason_buf));
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "Export will use .zmf: %s",
                               reason_buf);
        }
        ImGui::Spacing();

        const float side_w = 88.0f;
        const float list_w = ImGui::GetContentRegionAvail().x - side_w - ImGui::GetStyle().ItemSpacing.x;
        const float list_h = ImGui::GetContentRegionAvail().y - 88.0f;

        if (ImGui::BeginChild("##export_inst_list", ImVec2(list_w, list_h), true))
        {
            if (m_export_items.empty())
            {
                ImGui::TextDisabled("(no instruments used by this song)");
                ImGui::TextDisabled("OK exports with bank references only.");
            }
            static const char *kModeLabels[] = {"Off", "Embed", "Ghost"};
            for (size_t i = 0; i < m_export_items.size(); ++i)
            {
                ExportInstrumentItem &item = m_export_items[i];
                char label[320];
                if (item.custom_index >= 0)
                {
                    std::snprintf(label,
                                  sizeof(label),
                                  "%s (Custom: %d%s)",
                                  item.name.c_str(),
                                  item.custom_index,
                                  item.percussion ? " Perc" : "");
                }
                else
                {
                    std::snprintf(label,
                                  sizeof(label),
                                  "%s (B%d%s P%03d)",
                                  item.name.c_str(),
                                  item.bank,
                                  item.percussion ? " Perc" : "",
                                  item.program);
                }
                ImGui::PushID(static_cast<int>(item.inst_id));
                int mode_i = static_cast<int>(item.mode);
                if (mode_i < 0 || mode_i > 2)
                {
                    mode_i = 0;
                }
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::Combo("##mode", &mode_i, kModeLabels, IM_ARRAYSIZE(kModeLabels)))
                {
                    item.mode = static_cast<ExportInstMode>(mode_i);
                    item.selected = (item.mode == ExportInstMode::Embed);
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(label);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && item.mode == ExportInstMode::Ghost)
                {
                    ImGui::SetTooltip(
                        "Ghost: include instrument definition; samples stay bank aliases.");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##export_side", ImVec2(side_w, list_h), false))
        {
            if (ImGui::Button("All", ImVec2(-1, 0)))
            {
                ApplyExportInstrumentSelection(ExportSelectionMode::All);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip("Set every instrument to Embed.");
            }
            if (ImGui::Button("Custom", ImVec2(-1, 0)))
            {
                ApplyExportInstrumentSelection(ExportSelectionMode::Custom);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip(
                    "Embed custom (Bank 2+) instruments; Off for bank 0/1.");
            }
            if (ImGui::Button("None", ImVec2(-1, 0)))
            {
                ApplyExportInstrumentSelection(ExportSelectionMode::None);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip(
                    "Set all to Off (bank references only; list stays).");
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        {
            static const char *kStorageLabels[] = {"ESND", "CSND", "SND"};
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Sound Storage Type:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (m_export_snd_storage_index < 0 || m_export_snd_storage_index > 2)
            {
                m_export_snd_storage_index = static_cast<int>(BAE_EDITOR_SND_STORAGE_ESND);
            }
            ImGui::Combo("##export_snd_storage",
                         &m_export_snd_storage_index,
                         kStorageLabels,
                         IM_ARRAYSIZE(kStorageLabels));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip(
                    "Container for embedded samples in the exported RMF/ZMF.\n"
                    "ESND = encrypted (default), CSND = compressed, SND = plain.");
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(80, 0)))
        {
            SyncExportEmbedSetFromItems();
            m_export_rmf_open = false;
            ImGui::CloseCurrentPopup();
            OpenExportSongDialog();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            m_export_rmf_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    static uint16_t InternalMidiBankFromGroup(int bank_group)
    {
        if (bank_group < 0)
        {
            bank_group = 0;
        }
        return static_cast<uint16_t>((bank_group & 0x7F) << 7);
    }

    /* BE2 Make Song Using: append a one-note Session song that bank-refs the instrument. */
    bool MakeSongUsingInstrumentAt(int list_index)
    {
        if (!m_bank_token)
        {
            SetStatus("Load a bank before Make Song Using");
            return false;
        }
        if (list_index < 0 || list_index >= static_cast<int>(m_instruments.size()))
        {
            SetStatus("No instrument selected");
            return false;
        }
        const InstrumentRow &inst = m_instruments[static_cast<size_t>(list_index)];
        if (inst.from_song_document)
        {
            SetStatus("Cannot Make Song Using a song-only instrument");
            return false;
        }
        if (inst.is_alias)
        {
            SetStatus("Select the source instrument (not an alias) for Make Song Using");
            return false;
        }
        if (inst.bank < 0 || inst.program < 0)
        {
            SetStatus("Instrument has no bank/program slot");
            return false;
        }

        (void)CommitActiveSessionSong();

        BAERmfEditorDocument *doc = BAERmfEditorDocument_New();
        if (!doc)
        {
            SetStatus("Make Song Using failed: could not create document");
            return false;
        }

        std::string title = inst.name;
        const size_t alias_tag = title.find(" (Alias)");
        if (alias_tag != std::string::npos)
        {
            title = title.substr(0, alias_tag);
        }
        if (title.empty())
        {
            title = "Untitled Song";
        }

        const uint32_t bpm = 120;
        const uint16_t tpq = 480;
        (void)BAERmfEditorDocument_SetTempoBPM(doc, bpm);
        (void)BAERmfEditorDocument_SetTicksPerQuarter(doc, tpq);
        (void)BAERmfEditorDocument_AddTempoEvent(doc, 0, 60000000u / bpm);
        (void)BAERmfEditorDocument_SetInfo(doc, TITLE_INFO, title.c_str());

        char track_name_buf[256];
        std::snprintf(track_name_buf, sizeof(track_name_buf), "%s", title.c_str());

        BAERmfEditorTrackSetup setup;
        std::memset(&setup, 0, sizeof(setup));
        setup.channel = inst.percussion ? 9 : 0;
        setup.bank = InternalMidiBankFromGroup(inst.bank);
        setup.program = static_cast<unsigned char>(std::clamp(inst.program, 0, 127));
        setup.name = track_name_buf;

        uint16_t track_index = 0;
        if (BAERmfEditorDocument_AddTrack(doc, &setup, &track_index) != BAE_NO_ERROR)
        {
            BAERmfEditorDocument_Delete(doc);
            SetStatus("Make Song Using failed: add track");
            return false;
        }
        (void)BAERmfEditorDocument_SetTrackDefaultInstrument(doc,
                                                             track_index,
                                                             setup.bank,
                                                             setup.program);

        const uint32_t note_duration = tpq * 2u; /* 1 second at 120 BPM */
        const uint32_t song_length = DurationTicksFromSeconds(1.5, bpm, tpq);
        if (BAERmfEditorDocument_AddNote(doc, track_index, 0, note_duration, 60, 127) !=
            BAE_NO_ERROR)
        {
            BAERmfEditorDocument_Delete(doc);
            SetStatus("Make Song Using failed: add note");
            return false;
        }
        (void)BAERmfEditorDocument_SetTrackEndOfTrackTick(doc, track_index, song_length);

        unsigned char *midi_data = nullptr;
        uint32_t midi_size = 0;
        const BAEResult midi_r =
            BAERmfEditorDocument_SaveAsMidiToMemory(doc, &midi_data, &midi_size);
        if (midi_r != BAE_NO_ERROR || !midi_data || midi_size == 0)
        {
            if (midi_data)
            {
                XDisposePtr(midi_data);
            }
            BAERmfEditorDocument_Delete(doc);
            SetStatus(std::string("Make Song Using failed: ") + FormatBAEError(midi_r));
            return false;
        }

        SessionSongEntry entry;
        entry.song_resource_id = 0;
        entry.midi_resource_id = 0;
        entry.name = title;
        entry.source_type = "Song";
        entry.midi_bytes.assign(midi_data, midi_data + midi_size);
        XDisposePtr(midi_data);

        if (m_document)
        {
            BAERmfEditorDocument_Delete(m_document);
        }
        m_document = doc;
        m_session_songs.push_back(std::move(entry));
        m_session_song_index = static_cast<int>(m_session_songs.size()) - 1;
        m_document_dirty = false;
        m_song_row.name = title;
        m_song_row.path.clear();
        m_song_row.source_type = "Song";
        m_loaded_song_path.clear();
        m_loaded_doc_path.clear();
        m_song_show_filter = SongShowFilter::Custom;
        m_session_tab = SessionTab::Songs;
        RefreshSongOverridesFromDocument();
        RefreshSongSamplesFromDocument();
        (void)ReloadPlaybackFromDocument(true);

        char status[256];
        std::snprintf(status,
                      sizeof(status),
                      "Made Session song \"%s\" using B%d%s P%03d",
                      title.c_str(),
                      inst.bank,
                      inst.percussion ? " Perc" : "",
                      inst.program);
        SetStatus(status);
        return true;
    }

    /* Append an empty Session song (one silent track, no notes). */
    bool CreateEmptySong()
    {
        (void)CommitActiveSessionSong();

        BAERmfEditorDocument *doc = BAERmfEditorDocument_New();
        if (!doc)
        {
            SetStatus("Create empty song failed: could not create document");
            return false;
        }

        const std::string title = MakeUniqueSessionSongName("Untitled Song");
        const uint32_t bpm = 120;
        const uint16_t tpq = 480;
        (void)BAERmfEditorDocument_SetTempoBPM(doc, bpm);
        (void)BAERmfEditorDocument_SetTicksPerQuarter(doc, tpq);
        (void)BAERmfEditorDocument_AddTempoEvent(doc, 0, 60000000u / bpm);
        (void)BAERmfEditorDocument_SetInfo(doc, TITLE_INFO, title.c_str());

        char track_name_buf[64];
        std::snprintf(track_name_buf, sizeof(track_name_buf), "Track 1");

        BAERmfEditorTrackSetup setup;
        std::memset(&setup, 0, sizeof(setup));
        setup.channel = 0;
        setup.bank = 0;
        setup.program = 0;
        setup.name = track_name_buf;

        uint16_t track_index = 0;
        if (BAERmfEditorDocument_AddTrack(doc, &setup, &track_index) != BAE_NO_ERROR)
        {
            BAERmfEditorDocument_Delete(doc);
            SetStatus("Create empty song failed: add track");
            return false;
        }
        (void)BAERmfEditorDocument_SetTrackDefaultInstrument(doc, track_index, 0, 0);
        (void)BAERmfEditorDocument_SetTrackEndOfTrackTick(doc, track_index, tpq * 4u);

        unsigned char *midi_data = nullptr;
        uint32_t midi_size = 0;
        const BAEResult midi_r =
            BAERmfEditorDocument_SaveAsMidiToMemory(doc, &midi_data, &midi_size);
        if (midi_r != BAE_NO_ERROR || !midi_data || midi_size == 0)
        {
            if (midi_data)
            {
                XDisposePtr(midi_data);
            }
            BAERmfEditorDocument_Delete(doc);
            SetStatus(std::string("Create empty song failed: ") + FormatBAEError(midi_r));
            return false;
        }

        SessionSongEntry entry;
        entry.song_resource_id = 0;
        entry.midi_resource_id = 0;
        entry.name = title;
        entry.source_type = "Song";
        entry.midi_bytes.assign(midi_data, midi_data + midi_size);
        XDisposePtr(midi_data);

        if (m_document)
        {
            BAERmfEditorDocument_Delete(m_document);
        }
        m_document = doc;
        m_session_songs.push_back(std::move(entry));
        m_session_song_index = static_cast<int>(m_session_songs.size()) - 1;
        m_document_dirty = false;
        m_song_row.name = title;
        m_song_row.path.clear();
        m_song_row.source_type = "Song";
        m_loaded_song_path.clear();
        m_loaded_doc_path.clear();
        m_song_show_filter = SongShowFilter::Custom;
        m_session_tab = SessionTab::Songs;
        m_groovoid_index = -1;
        RefreshSongOverridesFromDocument();
        RefreshSongSamplesFromDocument();
        (void)ReloadPlaybackFromDocument(true);
        OpenMidiEditor();
        {
            char status[160];
            std::snprintf(status, sizeof(status), "Created empty song \"%s\"", title.c_str());
            SetStatus(status);
        }
        return true;
    }

    static uint32_t DurationTicksFromSeconds(double seconds, uint32_t bpm, uint16_t tpq)
    {
        if (seconds <= 0.0 || bpm == 0 || tpq == 0)
        {
            return tpq > 0 ? tpq : 480u;
        }
        double ticks = seconds * (static_cast<double>(bpm) / 60.0) * static_cast<double>(tpq);
        if (ticks < 1.0)
        {
            ticks = 1.0;
        }
        if (ticks > 2147483647.0)
        {
            ticks = 2147483647.0;
        }
        return static_cast<uint32_t>(ticks + 0.5);
    }

    static std::string SanitizeExportFileStem(const std::string &name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
        {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
                c == '<' || c == '>' || c == '|')
            {
                out.push_back('_');
            }
            else
            {
                out.push_back(c);
            }
        }
        while (!out.empty() && (out.front() == ' ' || out.front() == '.'))
        {
            out.erase(out.begin());
        }
        while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        {
            out.pop_back();
        }
        if (out.empty())
        {
            out = "export";
        }
        return out;
    }

    /* Prefer live song row / document title; session basename only when set. */
    std::string SuggestedExportFileStem() const
    {
        std::string raw;
        if (!m_song_row.name.empty())
        {
            raw = m_song_row.name;
        }
        else if (m_document)
        {
            const char *title = BAERmfEditorDocument_GetInfo(m_document, TITLE_INFO);
            if (title && title[0])
            {
                raw = title;
            }
        }
        if (raw.empty() && m_song_info_title[0] != '\0')
        {
            raw = m_song_info_title;
        }
        if (raw.empty() && !m_loaded_session_path.empty())
        {
            raw = FileNameFromPath(m_loaded_session_path);
            const size_t dot = raw.find_last_of('.');
            if (dot != std::string::npos && dot > 0)
            {
                raw = raw.substr(0, dot);
            }
        }
        return SanitizeExportFileStem(raw);
    }

    /* Bank export: session basename, else "export" (not the active song name). */
    std::string SuggestedBankExportFileStem() const
    {
        std::string raw;
        if (!m_loaded_session_path.empty())
        {
            raw = FileNameFromPath(m_loaded_session_path);
            const size_t dot = raw.find_last_of('.');
            if (dot != std::string::npos && dot > 0)
            {
                raw = raw.substr(0, dot);
            }
        }
        if (raw.empty())
        {
            raw = "export";
        }
        return SanitizeExportFileStem(raw);
    }

    void BeginOneShotRmfSaveDialog(const std::string &suggested_name)
    {
        m_pending_dialog_action = DialogAction::ExportOneShotRmf;
        const std::string stem = SanitizeExportFileStem(suggested_name);
        const std::string default_name = stem + ".rmf";
        static const SDL_DialogFileFilter filters[] = {
            {"RMF / ZMF (*.rmf;*.zmf)", "rmf;zmf"},
            {"All files (*.*)", "*"},
        };
        SDL_ShowSaveFileDialog(OnFileDialogResult,
                               this,
                               m_main_window,
                               filters,
                               static_cast<int>(SDL_arraysize(filters)),
                               default_name.c_str());
    }

    /* Pick native container extension from the sample's visible codec label. */
    static const char *NativeExportExtensionForCodecLabel(const std::string &codec_label)
    {
        std::string lower = codec_label;
        for (char &c : lower)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (lower.find("flac") != std::string::npos)
        {
            return "flac";
        }
        if (lower.find("vorbis") != std::string::npos || lower.find("opus") != std::string::npos)
        {
            return "ogg";
        }
        if (lower.find("mp3") != std::string::npos || lower.find("mpeg") != std::string::npos)
        {
            return "mp3";
        }
        if (lower.find("qoa") != std::string::npos)
        {
            return "qoa";
        }
        /* PCM / IMA / ADPCM / unknown → WAV */
        return "wav";
    }

    void OpenExportSampleDialog(int sample_row)
    {
        if (sample_row < 0 || sample_row >= static_cast<int>(m_samples.size()))
        {
            SetStatus("No sample selected");
            return;
        }
        const SampleRow &sample = m_samples[static_cast<size_t>(sample_row)];
        if (sample.source == SampleSource::Bank)
        {
            if (!m_bank_token || sample.snd_resource_id == kNoSndResourceId)
            {
                SetStatus("Export Sample requires a bank SND");
                return;
            }
        }
        else if (sample.source == SampleSource::Song)
        {
            if (!m_document)
            {
                SetStatus("Export Sample requires a song document");
                return;
            }
        }
        else
        {
            SetStatus("Cannot export this sample");
            return;
        }

        m_export_native_sample_row = sample_row;
        m_export_native_sample_ext = NativeExportExtensionForCodecLabel(sample.codec_label);
        m_pending_dialog_action = DialogAction::ExportSample;

        const std::string stem = SanitizeExportFileStem(sample.name);
        const std::string default_name = stem + "." + m_export_native_sample_ext;

        /* Filters must outlive the async dialog (static). */
        static const SDL_DialogFileFilter kFlac[] = {
            {"FLAC (*.flac)", "flac"}, {"All files (*.*)", "*"}};
        static const SDL_DialogFileFilter kOgg[] = {
            {"Ogg (*.ogg)", "ogg"}, {"All files (*.*)", "*"}};
        static const SDL_DialogFileFilter kMp3[] = {
            {"MP3 (*.mp3)", "mp3"}, {"All files (*.*)", "*"}};
        static const SDL_DialogFileFilter kQoa[] = {
            {"QOA (*.qoa)", "qoa"}, {"All files (*.*)", "*"}};
        static const SDL_DialogFileFilter kWav[] = {
            {"WAV (*.wav)", "wav"}, {"All files (*.*)", "*"}};

        const SDL_DialogFileFilter *filters = kWav;
        if (m_export_native_sample_ext == "flac")
        {
            filters = kFlac;
        }
        else if (m_export_native_sample_ext == "ogg")
        {
            filters = kOgg;
        }
        else if (m_export_native_sample_ext == "mp3")
        {
            filters = kMp3;
        }
        else if (m_export_native_sample_ext == "qoa")
        {
            filters = kQoa;
        }

        SDL_ShowSaveFileDialog(OnFileDialogResult,
                               this,
                               m_main_window,
                               filters,
                               2,
                               default_name.c_str());
    }

    void ExportSampleToPath(const std::string &path_in)
    {
        if (m_export_native_sample_row < 0 ||
            m_export_native_sample_row >= static_cast<int>(m_samples.size()))
        {
            SetStatus("No sample selected for export");
            return;
        }
        const SampleRow &sample = m_samples[static_cast<size_t>(m_export_native_sample_row)];
        /* SDL/clipboard may hand back file:// URIs — strip before fopen. */
        std::string path = ClipboardPathFromText(path_in.c_str());
        if (path.empty())
        {
            path = path_in;
        }
        const std::string want_ext = "." + (m_export_native_sample_ext.empty()
                                                ? std::string("wav")
                                                : m_export_native_sample_ext);
        {
            const size_t slash = path.find_last_of("/\\");
            const size_t dot = path.find_last_of('.');
            const bool has_ext = (dot != std::string::npos &&
                                  (slash == std::string::npos || dot > slash));
            if (!has_ext)
            {
                path += want_ext;
            }
        }

        BAEResult result = BAE_PARAM_ERR;
        if (sample.source == SampleSource::Bank)
        {
            if (!m_bank_token || sample.snd_resource_id == kNoSndResourceId)
            {
                SetStatus("Export Sample requires a bank SND");
                return;
            }
            result = BAERmfEditorBank_ExportSndResourceToFile(
                m_bank_token,
                sample.snd_resource_id,
                const_cast<char *>(path.c_str()));
        }
        else if (sample.source == SampleSource::Song)
        {
            if (!m_document)
            {
                SetStatus("Export Sample requires a song document");
                return;
            }
            result = BAERmfEditorDocument_ExportSampleToFile(
                m_document,
                sample.document_sample_index,
                const_cast<char *>(path.c_str()));
        }

        if (result == BAE_NO_ERROR)
        {
            SetStatus(std::string("Exported sample to ") + FileNameFromPath(path));
        }
        else
        {
            SetStatus(std::string("Sample export failed: ") + FormatBAEError(result));
        }
        m_export_native_sample_row = -1;
        m_export_native_sample_ext.clear();
    }

    void OpenExportInstrumentAsRmf(int list_index)
    {
        if (!m_bank_token)
        {
            SetStatus("Load a bank before exporting RMF");
            return;
        }
        if (list_index < 0 || list_index >= static_cast<int>(m_instruments.size()))
        {
            SetStatus("No instrument selected");
            return;
        }
        const InstrumentRow &inst = m_instruments[static_cast<size_t>(list_index)];
        if (inst.from_song_document)
        {
            SetStatus("Cannot export a song-only instrument as RMF from the bank list");
            return;
        }
        m_oneshot_export_pending = true;
        m_oneshot_bank_instrument_index = inst.instrument_index;
        m_oneshot_sample_split_index = 0;
        m_oneshot_snd_id = kNoSndResourceId;
        m_oneshot_sample_unassigned = false;
        m_oneshot_percussion = inst.percussion;
        m_oneshot_from_sample = false;
        m_oneshot_target_program = inst.program;
        m_oneshot_sample_frames = 0;
        m_oneshot_sample_rate_hz = 0;
        m_oneshot_note = 60;
        m_oneshot_title = inst.name;
        const size_t alias_tag = m_oneshot_title.find(" (Alias)");
        if (alias_tag != std::string::npos)
        {
            m_oneshot_title = m_oneshot_title.substr(0, alias_tag);
        }
        BeginOneShotRmfSaveDialog(m_oneshot_title);
    }

    void OpenExportSampleRmfDialog(int sample_row)
    {
        if (!m_bank_token)
        {
            SetStatus("Load a bank before exporting RMF");
            return;
        }
        if (sample_row < 0 || sample_row >= static_cast<int>(m_samples.size()))
        {
            SetStatus("No sample selected");
            return;
        }
        const SampleRow &sample = m_samples[static_cast<size_t>(sample_row)];
        if (sample.source != SampleSource::Bank || sample.snd_resource_id == kNoSndResourceId)
        {
            SetStatus("Export as RMF requires a bank sample");
            return;
        }
        m_export_sample_row = sample_row;
        m_export_sample_bank = 2;
        m_export_sample_percussion = false;
        if (m_instrument_filter_index >= 0 &&
            m_instrument_filter_index < static_cast<int>(m_instrument_filters.size()) &&
            m_instrument_filters[static_cast<size_t>(m_instrument_filter_index)].category == 1)
        {
            m_export_sample_percussion = true;
        }
        const int free_prog =
            FirstFreeProgramForGroup(m_export_sample_bank, m_export_sample_percussion);
        m_export_sample_program = free_prog >= 0 ? free_prog : 0;
        m_export_sample_rmf_open = true;
    }

    bool PrepareSampleOneShotExportFromDialog()
    {
        if (!m_bank_token)
        {
            SetStatus("No bank loaded");
            return false;
        }
        if (m_export_sample_row < 0 ||
            m_export_sample_row >= static_cast<int>(m_samples.size()))
        {
            SetStatus("No sample selected");
            return false;
        }
        const SampleRow &sample = m_samples[static_cast<size_t>(m_export_sample_row)];
        if (sample.snd_resource_id == kNoSndResourceId)
        {
            SetStatus("Sample has no SND resource");
            return false;
        }

        /* Export builds a temp RMF document only — never mutates the session bank. */
        m_export_sample_bank = 2;
        const bool percussion = m_export_sample_percussion;
        const int program = std::clamp(m_export_sample_program, 0, 127);
        uint32_t frames = sample.frame_count;
        uint32_t rate_hz = sample.sample_rate_hz;

        if (!sample.unassigned)
        {
            BAERmfEditorBankSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                         sample.instrument_index,
                                                         sample.split_index,
                                                         &info) == BAE_NO_ERROR)
            {
                if (info.frameCount > 0)
                {
                    frames = info.frameCount;
                }
                if (info.sampleRate > 0)
                {
                    rate_hz = info.sampleRate;
                }
            }
        }

        std::string title = sample.name;
        const size_t bracket = title.rfind(" [SND:");
        if (bracket != std::string::npos)
        {
            title = title.substr(0, bracket);
        }
        if (title.empty())
        {
            title = "Sample";
        }

        m_oneshot_export_pending = true;
        m_oneshot_bank_instrument_index = sample.instrument_index;
        m_oneshot_sample_split_index = sample.split_index;
        m_oneshot_snd_id = sample.snd_resource_id;
        m_oneshot_sample_unassigned = sample.unassigned;
        m_oneshot_percussion = percussion;
        m_oneshot_from_sample = true;
        m_oneshot_target_program = program;
        m_oneshot_sample_frames = frames;
        m_oneshot_sample_rate_hz = rate_hz;
        m_oneshot_note = 60;
        m_oneshot_title = title;
        return true;
    }

    void DrawExportSampleRmfDialog()
    {
        if (m_export_sample_rmf_open)
        {
            ImGui::OpenPopup("Export RMF from Sample");
        }
        if (!ImGui::BeginPopupModal("Export RMF from Sample",
                                    &m_export_sample_rmf_open,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextUnformatted("Place instrument in RMF as:");
        ImGui::TextDisabled("Builds a temporary one-note song; the session bank is not changed.");
        ImGui::SetNextItemWidth(320.0f);
        {
            /* BE2 Export RMF from Sample: Bank 2 Melodic / Percussion only. */
            struct ExportSampleBankChoice
            {
                bool percussion;
                const char *label;
            };
            static const ExportSampleBankChoice kChoices[] = {
                {false, "Bank 2 Melodic"},
                {true, "Bank 2 Percussion"},
            };
            m_export_sample_bank = 2;
            const char *preview =
                m_export_sample_percussion ? kChoices[1].label : kChoices[0].label;
            if (ImGui::BeginCombo("##export_sample_bank", preview))
            {
                for (const ExportSampleBankChoice &choice : kChoices)
                {
                    const bool selected = (choice.percussion == m_export_sample_percussion);
                    if (ImGui::Selectable(choice.label, selected))
                    {
                        m_export_sample_percussion = choice.percussion;
                        const int free_prog =
                            FirstFreeProgramForGroup(2, m_export_sample_percussion);
                        if (free_prog >= 0)
                        {
                            m_export_sample_program = free_prog;
                        }
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("Instrument Number:", &m_export_sample_program);
        m_export_sample_program = std::clamp(m_export_sample_program, 0, 127);

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(80, 0)))
        {
            if (PrepareSampleOneShotExportFromDialog())
            {
                m_export_sample_rmf_open = false;
                ImGui::CloseCurrentPopup();
                BeginOneShotRmfSaveDialog(m_oneshot_title);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            m_export_sample_rmf_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool DecodeBankSndToPCM(uint32_t snd_id,
                            std::vector<unsigned char> &out_pcm,
                            uint32_t *out_frames,
                            uint16_t *out_bit_size,
                            uint16_t *out_channels,
                            BAE_UNSIGNED_FIXED *out_rate) const
    {
        if (!m_bank_token || snd_id == kNoSndResourceId || !out_frames || !out_bit_size ||
            !out_channels || !out_rate)
        {
            return false;
        }
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        static const XResourceType kSndTypes[] = {ID_ESND, ID_CSND, ID_SND, 0};
        XPTR snd_data = nullptr;
        int32_t snd_size = 0;
        for (int t = 0; kSndTypes[t] != 0; ++t)
        {
            snd_data = XGetFileResource(bank,
                                        kSndTypes[t],
                                        static_cast<XLongResourceID>(snd_id),
                                        nullptr,
                                        &snd_size);
            if (!snd_data)
            {
                continue;
            }
            if (kSndTypes[t] == ID_CSND)
            {
                XPTR decompressed = XDecompressPtr(snd_data, static_cast<uint32_t>(snd_size), FALSE);
                XDisposePtr(snd_data);
                snd_data = decompressed;
                if (snd_data)
                {
                    snd_size = XGetPtrSize(snd_data);
                }
            }
            else if (kSndTypes[t] == ID_ESND)
            {
                XDecryptData(snd_data, static_cast<uint32_t>(snd_size));
            }
            break;
        }
        if (!snd_data || snd_size <= 0)
        {
            return false;
        }

        SampleDataInfo sdi;
        std::memset(&sdi, 0, sizeof(sdi));
        XPTR pcm_ptr = XGetSamplePtrFromSnd(snd_data, &sdi);
        XPTR pcm_owner = (sdi.pMasterPtr && sdi.pMasterPtr != snd_data) ? sdi.pMasterPtr : nullptr;
        if (!pcm_ptr || sdi.frames == 0 || sdi.bitSize == 0 || sdi.channels == 0)
        {
            if (pcm_owner)
            {
                XDisposePtr(pcm_owner);
            }
            XDisposePtr(snd_data);
            return false;
        }
        const int32_t pcm_size =
            static_cast<int32_t>(sdi.frames * (sdi.bitSize / 8) * sdi.channels);
        if (pcm_size <= 0)
        {
            if (pcm_owner)
            {
                XDisposePtr(pcm_owner);
            }
            XDisposePtr(snd_data);
            return false;
        }
        out_pcm.resize(static_cast<size_t>(pcm_size));
        std::memcpy(out_pcm.data(), pcm_ptr, static_cast<size_t>(pcm_size));
        *out_frames = sdi.frames;
        *out_bit_size = sdi.bitSize;
        *out_channels = sdi.channels;
        *out_rate = sdi.rate ? sdi.rate : LONG_TO_UNSIGNED_FIXED(44100);
        if (pcm_owner)
        {
            XDisposePtr(pcm_owner);
        }
        XDisposePtr(snd_data);
        return true;
    }

    void ApplyOneShotSustainAdsr(BAERmfEditorDocument *export_doc, uint32_t inst_id)
    {
        if (!export_doc)
        {
            return;
        }
        /* Sustain at full level until note-off, then immediate release.
         * A single LAST stage (engine default) sounds like a blip. */
        BAERmfEditorInstrumentExtInfo ext;
        std::memset(&ext, 0, sizeof(ext));
        if (BAERmfEditorDocument_GetInstrumentExtInfo(export_doc, inst_id, &ext) != BAE_NO_ERROR)
        {
            ext.instID = inst_id;
            ext.hasExtendedData = TRUE;
            ext.flags1 = ZBF_useSampleRate;
            ext.flags2 = ZBF_useSoundModifierAsRootKey;
            ext.midiRootKey = static_cast<int16_t>(std::clamp(m_oneshot_note, 0, 127));
            ext.miscParameter1 = ext.midiRootKey;
            ext.miscParameter2 = 100;
        }
        ext.instID = inst_id;
        ext.hasExtendedData = TRUE;
        ext.volumeADSR.stageCount = 2;
        ext.volumeADSR.stages[0].level = 4096; /* VOLUME_RANGE */
        ext.volumeADSR.stages[0].time = 0;
        ext.volumeADSR.stages[0].flags = static_cast<int32_t>(FOUR_CHAR('S', 'U', 'S', 'T'));
        ext.volumeADSR.stages[1].level = 0;
        ext.volumeADSR.stages[1].time = 0;
        ext.volumeADSR.stages[1].flags = static_cast<int32_t>(FOUR_CHAR('L', 'A', 'S', 'T'));
        ext.lfoCount = 0;
        ext.curveCount = 0;
        (void)BAERmfEditorDocument_SetInstrumentExtInfo(export_doc, inst_id, &ext);
    }

    void ExportOneShotRmfToPath(const std::string &path)
    {
        if (!m_oneshot_export_pending)
        {
            SetStatus("No one-shot RMF export pending");
            return;
        }
        m_oneshot_export_pending = false;
        if (!m_bank_token)
        {
            SetStatus("Load a bank before exporting RMF");
            return;
        }

        BAERmfEditorDocument *export_doc = BAERmfEditorDocument_New();
        if (!export_doc)
        {
            SetStatus("Export failed: could not create document");
            return;
        }

        const uint32_t bpm = 120;
        const uint16_t tpq = 480;
        (void)BAERmfEditorDocument_SetTempoBPM(export_doc, bpm);
        (void)BAERmfEditorDocument_SetTicksPerQuarter(export_doc, tpq);
        (void)BAERmfEditorDocument_AddTempoEvent(export_doc, 0, 60000000u / bpm);
        if (!m_oneshot_title.empty())
        {
            (void)BAERmfEditorDocument_SetInfo(export_doc, TITLE_INFO, m_oneshot_title.c_str());
        }

        const unsigned char target_program =
            static_cast<unsigned char>(std::clamp(m_oneshot_target_program, 0, 127));
        /* Always land at INST 512+program (bank 2 melodic) so ch0 + bank 2<<7 resolves. */
        (void)m_oneshot_percussion;
        const uint32_t target_inst_id = 512u + static_cast<uint32_t>(target_program);

        if (m_oneshot_from_sample)
        {
            std::vector<unsigned char> pcm;
            uint32_t frames = 0;
            uint16_t bit_size = 16;
            uint16_t channels = 1;
            BAE_UNSIGNED_FIXED rate = LONG_TO_UNSIGNED_FIXED(44100);
            bool got_pcm = false;

            if (!m_oneshot_sample_unassigned)
            {
                void *wave = nullptr;
                if (BAERmfEditorBank_GetSampleWaveformData(m_bank_token,
                                                           m_oneshot_bank_instrument_index,
                                                           m_oneshot_sample_split_index,
                                                           &wave,
                                                           &frames,
                                                           &bit_size,
                                                           &channels,
                                                           &rate) == BAE_NO_ERROR &&
                    wave && frames > 0)
                {
                    const size_t nbytes =
                        static_cast<size_t>(frames) * (bit_size / 8u) * channels;
                    pcm.resize(nbytes);
                    std::memcpy(pcm.data(), wave, nbytes);
                    BAERmfEditorBank_FreeWaveformData(wave);
                    got_pcm = true;
                }
                else if (wave)
                {
                    BAERmfEditorBank_FreeWaveformData(wave);
                }
            }
            if (!got_pcm)
            {
                got_pcm = DecodeBankSndToPCM(m_oneshot_snd_id, pcm, &frames, &bit_size, &channels, &rate);
            }
            if (!got_pcm || pcm.empty() || frames == 0)
            {
                BAERmfEditorDocument_Delete(export_doc);
                SetStatus("Export failed: could not decode sample PCM");
                return;
            }

            m_oneshot_sample_frames = frames;
            m_oneshot_sample_rate_hz =
                rate ? static_cast<uint32_t>(rate >> 16) : m_oneshot_sample_rate_hz;

            char name_buf[256];
            std::snprintf(name_buf,
                          sizeof(name_buf),
                          "%s",
                          m_oneshot_title.empty() ? "Sample" : m_oneshot_title.c_str());
            BAERmfEditorSampleSetup sample_setup;
            std::memset(&sample_setup, 0, sizeof(sample_setup));
            sample_setup.program = target_program;
            sample_setup.rootKey =
                static_cast<unsigned char>(std::clamp(m_oneshot_note, 0, 127));
            sample_setup.lowKey = 0;
            sample_setup.highKey = 127;
            sample_setup.displayName = name_buf;

            uint32_t sample_index = 0;
            BAESampleInfo sample_info;
            std::memset(&sample_info, 0, sizeof(sample_info));
            if (BAERmfEditorDocument_AddEmptySample(export_doc,
                                                    &sample_setup,
                                                    &sample_index,
                                                    &sample_info) != BAE_NO_ERROR)
            {
                BAERmfEditorDocument_Delete(export_doc);
                SetStatus("Export failed: could not create sample");
                return;
            }
            if (BAERmfEditorDocument_ReplaceSampleFromPCM(export_doc,
                                                          sample_index,
                                                          pcm.data(),
                                                          frames,
                                                          bit_size,
                                                          channels,
                                                          rate,
                                                          0,
                                                          0,
                                                          &sample_info) != BAE_NO_ERROR)
            {
                BAERmfEditorDocument_Delete(export_doc);
                SetStatus("Export failed: could not embed sample PCM");
                return;
            }
            (void)BAERmfEditorDocument_SetSampleInstID(export_doc, sample_index, target_inst_id);
            ApplyOneShotSustainAdsr(export_doc, target_inst_id);
        }
        else
        {
            BAEResult clone = BAERmfEditorDocument_CloneInstrumentFromBankToInstID(
                export_doc,
                m_bank_token,
                m_oneshot_bank_instrument_index,
                target_inst_id,
                target_program);
            if (clone != BAE_NO_ERROR)
            {
                /* Fallback: classic clone into INST 512+program. */
                clone = BAERmfEditorDocument_CloneInstrumentFromBank(
                    export_doc,
                    m_bank_token,
                    m_oneshot_bank_instrument_index,
                    target_program);
                if (clone != BAE_NO_ERROR)
                {
                    BAERmfEditorDocument_Delete(export_doc);
                    SetStatus(std::string("Export clone failed: ") + FormatBAEError(clone));
                    return;
                }
                ApplyOneShotSustainAdsr(export_doc, 512u + static_cast<uint32_t>(target_program));
            }
            else
            {
                ApplyOneShotSustainAdsr(export_doc, target_inst_id);
            }
        }

        char track_name_buf[256];
        std::snprintf(track_name_buf,
                      sizeof(track_name_buf),
                      "%s",
                      m_oneshot_title.empty() ? "One Shot" : m_oneshot_title.c_str());

        BAERmfEditorTrackSetup setup;
        std::memset(&setup, 0, sizeof(setup));
        setup.channel = 0;
        setup.bank = InternalMidiBankFromGroup(2);
        setup.program = target_program;
        setup.name = track_name_buf;

        uint16_t track_index = 0;
        BAEResult track_r = BAERmfEditorDocument_AddTrack(export_doc, &setup, &track_index);
        if (track_r != BAE_NO_ERROR)
        {
            BAERmfEditorDocument_Delete(export_doc);
            SetStatus(std::string("Export track failed: ") + FormatBAEError(track_r));
            return;
        }
        (void)BAERmfEditorDocument_SetTrackDefaultInstrument(export_doc,
                                                             track_index,
                                                             setup.bank,
                                                             setup.program);

        uint32_t note_duration = tpq * 2u; /* instrument default: 1 second */
        uint32_t song_length = tpq * 4u;   /* instrument default: 2 seconds */
        if (m_oneshot_from_sample)
        {
            double sample_sec = 1.0;
            if (m_oneshot_sample_frames > 0 && m_oneshot_sample_rate_hz > 0)
            {
                sample_sec = static_cast<double>(m_oneshot_sample_frames) /
                             static_cast<double>(m_oneshot_sample_rate_hz);
            }
            note_duration = DurationTicksFromSeconds(sample_sec, bpm, tpq);
            song_length = DurationTicksFromSeconds(sample_sec + 0.5, bpm, tpq);
            if (song_length < note_duration + 1u)
            {
                song_length = note_duration + 1u;
            }
        }
        const unsigned char note =
            static_cast<unsigned char>(std::clamp(m_oneshot_note, 0, 127));
        BAEResult note_r =
            BAERmfEditorDocument_AddNote(export_doc, track_index, 0, note_duration, note, 127);
        if (note_r != BAE_NO_ERROR)
        {
            BAERmfEditorDocument_Delete(export_doc);
            SetStatus(std::string("Export note failed: ") + FormatBAEError(note_r));
            return;
        }
        (void)BAERmfEditorDocument_SetTrackEndOfTrackTick(export_doc, track_index, song_length);

        std::string out_path = path;
        uint32_t reason = 0;
        const bool requires_zmf = BAERmfEditorDocument_RequiresZmf(export_doc, &reason) != FALSE;
        if (requires_zmf && !EndsWith(out_path, ".zmf"))
        {
            const size_t dot = out_path.find_last_of('.');
            if (dot != std::string::npos)
            {
                out_path = out_path.substr(0, dot) + ".zmf";
            }
            else
            {
                out_path += ".zmf";
            }
        }

        const BAEResult save_r =
            BAERmfEditorDocument_SaveAsRmf(export_doc, const_cast<char *>(out_path.c_str()));
        BAERmfEditorDocument_Delete(export_doc);
        if (save_r == BAE_NO_ERROR)
        {
            char status[256];
            std::snprintf(status,
                          sizeof(status),
                          "Exported one-shot RMF \"%s\"",
                          FileNameFromPath(out_path).c_str());
            SetStatus(status);
        }
        else
        {
            SetStatus(std::string("Export failed: ") + FormatBAEError(save_r));
        }
    }

    bool SongUsesCustomInstrumentsForMidiWarn() const
    {
        if (!m_document)
        {
            return false;
        }
        /* Embedded INST/SND in the song document cannot be represented in SMF. */
        if (BAERmfEditorDocument_CanSaveAsMidi(m_document) == FALSE)
        {
            return true;
        }

        uint16_t track_count = 0;
        if (BAERmfEditorDocument_GetTrackCount(m_document, &track_count) != BAE_NO_ERROR)
        {
            return false;
        }
        for (uint16_t t = 0; t < track_count; ++t)
        {
            BAERmfEditorTrackInfo ti{};
            if (BAERmfEditorDocument_GetTrackInfo(m_document, t, &ti) == BAE_NO_ERROR)
            {
                if (ExportBankGroupFromNoteBank(ti.bank) >= 2)
                {
                    return true;
                }
                if (m_bank_token && !m_session_custom_inst_ids.empty())
                {
                    const bool percussion = (ti.channel == 9);
                    const uint32_t requested_inst_id =
                        percussion ? (static_cast<uint32_t>(ExportBankGroupFromNoteBank(ti.bank)) * 256u) +
                                         128u + ti.program
                                   : (static_cast<uint32_t>(ExportBankGroupFromNoteBank(ti.bank)) * 256u) +
                                         ti.program;
                    uint32_t resolved = requested_inst_id;
                    uint32_t index = 0;
                    if (BAERmfEditorBank_ResolveInstID(m_bank_token,
                                                       requested_inst_id,
                                                       &resolved,
                                                       &index) == BAE_NO_ERROR &&
                        m_session_custom_inst_ids.count(resolved) != 0)
                    {
                        return true;
                    }
                }
            }

            uint32_t note_count = 0;
            if (BAERmfEditorDocument_GetNoteCount(m_document, t, &note_count) != BAE_NO_ERROR)
            {
                continue;
            }
            for (uint32_t n = 0; n < note_count; ++n)
            {
                BAERmfEditorNoteInfo note{};
                if (BAERmfEditorDocument_GetNoteInfo(m_document, t, n, &note) != BAE_NO_ERROR)
                {
                    continue;
                }
                const int bank_group = ExportBankGroupFromNoteBank(note.bank);
                if (bank_group >= 2)
                {
                    return true;
                }
                if (m_bank_token && !m_session_custom_inst_ids.empty())
                {
                    const bool percussion = (note.channel == 9);
                    const uint32_t requested_inst_id =
                        percussion ? (static_cast<uint32_t>(bank_group) * 256u) + 128u + note.note
                                   : (static_cast<uint32_t>(bank_group) * 256u) + note.program;
                    uint32_t resolved = requested_inst_id;
                    uint32_t index = 0;
                    if (BAERmfEditorBank_ResolveInstID(m_bank_token,
                                                       requested_inst_id,
                                                       &resolved,
                                                       &index) == BAE_NO_ERROR &&
                        m_session_custom_inst_ids.count(resolved) != 0)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void RequestExportMidi()
    {
        if (!m_document)
        {
            SetStatus("No song document to export");
            return;
        }
        if (SongUsesCustomInstrumentsForMidiWarn())
        {
            m_confirm_export_midi_open = true;
            return;
        }
        OpenExportMidiDialog();
    }

    void DrawExportMidiConfirmDialog()
    {
        if (m_confirm_export_midi_open)
        {
            ImGui::OpenPopup("Export MIDI");
        }
        if (!ImGui::BeginPopupModal("Export MIDI",
                                    &m_confirm_export_midi_open,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextWrapped(
            "This song uses custom instruments. Standard MIDI (.mid) cannot embed "
            "those instruments, so they will not play correctly in other players.");
        ImGui::Spacing();
        ImGui::TextDisabled("Bank/program numbers are still written; use Export RMF/ZMF to keep samples.");
        ImGui::Separator();
        if (ImGui::Button("Export MIDI anyway", ImVec2(160.0f, 0.0f)))
        {
            m_confirm_export_midi_open = false;
            ImGui::CloseCurrentPopup();
            OpenExportMidiDialog();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_confirm_export_midi_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ExportSessionSongMidiToPath(const std::string &path)
    {
        if (!m_document)
        {
            SetStatus("No song document to export");
            return;
        }

        std::string out_path = path;
        if (!EndsWith(out_path, ".mid") && !EndsWith(out_path, ".midi"))
        {
            out_path += ".mid";
        }

        unsigned char *midi_data = nullptr;
        uint32_t midi_size = 0;
        const BAEResult build_r =
            BAERmfEditorDocument_SaveAsMidiToMemory(m_document, &midi_data, &midi_size);
        if (build_r != BAE_NO_ERROR || !midi_data || midi_size == 0)
        {
            if (midi_data)
            {
                XDisposePtr((XPTR)midi_data);
            }
            SetStatus(std::string("MIDI export failed: ") + FormatBAEError(build_r));
            return;
        }

        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            XDisposePtr((XPTR)midi_data);
            SetStatus("MIDI export failed: could not write file");
            return;
        }
        out.write(reinterpret_cast<const char *>(midi_data),
                  static_cast<std::streamsize>(midi_size));
        XDisposePtr((XPTR)midi_data);
        if (!out)
        {
            SetStatus("MIDI export failed: write error");
            return;
        }

        char status[256];
        std::snprintf(status,
                      sizeof(status),
                      "Exported MIDI \"%s\"",
                      FileNameFromPath(out_path).c_str());
        SetStatus(status);
    }

    void ExportSessionSongToPath(const std::string &path)
    {
        if (!m_document)
        {
            SetStatus("No song document to export");
            return;
        }

        std::string out_path = path;
        const bool want_midi = EndsWith(out_path, ".mid") || EndsWith(out_path, ".midi");
        if (want_midi)
        {
            ExportSessionSongMidiToPath(out_path);
            return;
        }

        if (!m_bank_token)
        {
            SetStatus("Load a bank before exporting RMF/ZMF");
            return;
        }

        UpdateBusyProgress(0.10f, "Snapshotting song document…");
        unsigned char *rmf_snapshot_data = nullptr;
        uint32_t rmf_snapshot_size = 0;
        uint32_t zmf_reason = 0;
        const bool use_zmf = BAERmfEditorDocument_RequiresZmf(m_document, &zmf_reason) != FALSE;
        if (BAERmfEditorDocument_SaveAsRmfToMemory(m_document, use_zmf, &rmf_snapshot_data, &rmf_snapshot_size) != BAE_NO_ERROR ||
            !rmf_snapshot_data || rmf_snapshot_size == 0)
        {
            if (rmf_snapshot_data)
            {
                XDisposePtr(rmf_snapshot_data);
            }
            SetStatus("Export failed: could not snapshot song document");
            return;
        }

        BAERmfEditorDocument *export_doc =
            BAERmfEditorDocument_LoadFromMemory(rmf_snapshot_data, rmf_snapshot_size, BAE_RMF);
        XDisposePtr(rmf_snapshot_data);
        if (!export_doc)
        {
            SetStatus("Export failed: could not build export document");
            return;
        }

        UpdateBusyProgress(0.30f, "Resolving used instruments…");
        if (m_export_items.empty())
        {
            RebuildExportInstrumentList();
            ApplyExportInstrumentSelection(ExportSelectionMode::All);
        }
        SyncExportEmbedSetFromItems();

        /* Embed + Ghost both need INST on the export doc first; Ghost then
         * strips sample payloads via SetInstrumentGhost. */
        std::vector<uint32_t> prepare_ids;
        prepare_ids.reserve(m_export_embed_inst_ids.size() + m_export_ghost_inst_ids.size());
        for (uint32_t id : m_export_embed_inst_ids)
        {
            prepare_ids.push_back(id);
        }
        for (uint32_t id : m_export_ghost_inst_ids)
        {
            prepare_ids.push_back(id);
        }

        UpdateBusyProgress(0.50f, "Cloning instruments and samples…");
        BAERmfEditorCloneUsedResult clone_result;
        std::memset(&clone_result, 0, sizeof(clone_result));
        BAEResult prep = BAERmfEditorDocument_ExportUsedInstrumentsFromBank(
            export_doc,
            m_bank_token,
            prepare_ids.empty() ? nullptr : prepare_ids.data(),
            static_cast<uint32_t>(prepare_ids.size()),
            &clone_result);
        if (prep != BAE_NO_ERROR)
        {
            BAERmfEditorDocument_Delete(export_doc);
            SetStatus(std::string("Export prepare failed: ") + FormatBAEError(prep));
            return;
        }

        /* Refuse to write a silent song: every Embed/Ghost INST must land on the
         * export document (ExtInfo and/or samples). ALIS-without-INST was the
         * miami-vice failure mode. */
        for (uint32_t id : prepare_ids)
        {
            BAERmfEditorInstrumentExtInfo ext;
            std::memset(&ext, 0, sizeof(ext));
            const bool has_ext =
                BAERmfEditorDocument_GetInstrumentExtInfo(export_doc, id, &ext) == BAE_NO_ERROR;
#if USE_ZMF_SUPPORT == TRUE
            const bool has_osc = has_ext && ext.useOscillator != FALSE;
#else
            const bool has_osc = false;
#endif
            bool has_sample = false;
            uint32_t sc = 0;
            if (BAERmfEditorDocument_GetSampleCount(export_doc, &sc) == BAE_NO_ERROR)
            {
                for (uint32_t si = 0; si < sc; ++si)
                {
                    uint32_t sample_inst = BAE_EDITOR_INST_ID_NONE;
                    if (BAERmfEditorDocument_GetInstIDForSample(export_doc, si, &sample_inst) ==
                            BAE_NO_ERROR &&
                        sample_inst == id)
                    {
                        has_sample = true;
                        break;
                    }
                }
            }
            if (!has_osc && !has_sample)
            {
                BAERmfEditorDocument_Delete(export_doc);
                char buf[128];
                std::snprintf(buf,
                              sizeof(buf),
                              "Export prepare failed: instrument %u was not embedded",
                              static_cast<unsigned>(id));
                SetStatus(buf);
                return;
            }
        }

        for (const ExportInstrumentItem &item : m_export_items)
        {
            uint32_t target_inst = item.inst_id;
            if (item.percussion)
            {
                for (uint32_t mi = 0; mi < clone_result.mappingCount; ++mi)
                {
                    if (clone_result.mappings[mi].resolvedInstID == item.inst_id &&
                        clone_result.mappings[mi].isPercussion)
                    {
                        target_inst = clone_result.mappings[mi].targetInstID;
                        break;
                    }
                }
            }
            if (item.mode == ExportInstMode::Off)
            {
                (void)BAERmfEditorDocument_DeleteInstrument(export_doc, item.inst_id);
                if (item.percussion && target_inst != item.inst_id)
                {
                    (void)BAERmfEditorDocument_DeleteInstrument(export_doc, target_inst);
                }
            }
            else if (item.mode == ExportInstMode::Ghost)
            {
                (void)BAERmfEditorDocument_SetInstrumentGhost(export_doc,
                                                               target_inst,
                                                               m_bank_token,
                                                               TRUE);
            }
        }

#if USE_ZMF_SUPPORT == TRUE
        /* Generators are sample-free. Re-apply song-doc OSCL (export reclones
         * from the bank and can drop song-only Apply), and strip any leftover
         * sample rows so Ghost/Embed both write a playable OSCL INST. */
        for (const ExportInstrumentItem &item : m_export_items)
        {
            if (item.mode == ExportInstMode::Off)
            {
                continue;
            }
            uint32_t target_inst = item.inst_id;
            if (item.percussion)
            {
                for (uint32_t mi = 0; mi < clone_result.mappingCount; ++mi)
                {
                    if (clone_result.mappings[mi].resolvedInstID == item.inst_id &&
                        clone_result.mappings[mi].isPercussion)
                    {
                        target_inst = clone_result.mappings[mi].targetInstID;
                        break;
                    }
                }
            }
            BAERmfEditorInstrumentExtInfo ext;
            std::memset(&ext, 0, sizeof(ext));
            bool have_osc =
                BAERmfEditorDocument_GetInstrumentExtInfo(export_doc, target_inst, &ext) ==
                    BAE_NO_ERROR &&
                ext.useOscillator != FALSE;
            if (!have_osc && m_document)
            {
                BAERmfEditorInstrumentExtInfo song_ext;
                std::memset(&song_ext, 0, sizeof(song_ext));
                if (BAERmfEditorDocument_GetInstrumentExtInfo(m_document, item.inst_id, &song_ext) ==
                        BAE_NO_ERROR &&
                    song_ext.useOscillator != FALSE)
                {
                    char name_buf[256];
                    name_buf[0] = 0;
                    if (song_ext.displayName && song_ext.displayName[0])
                    {
                        std::snprintf(name_buf, sizeof(name_buf), "%s", song_ext.displayName);
                        song_ext.displayName = name_buf;
                    }
                    else
                    {
                        song_ext.displayName = nullptr;
                    }
                    song_ext.instID = target_inst;
                    song_ext.flags1 = static_cast<unsigned char>(
                        song_ext.flags1 & static_cast<unsigned char>(~ZBF_ghostInstrument));
                    song_ext.flags1 =
                        static_cast<unsigned char>(song_ext.flags1 | ZBF_extendedFormat);
                    song_ext.hasExtendedData = TRUE;
                    if (BAERmfEditorDocument_SetInstrumentExtInfo(export_doc, target_inst, &song_ext) ==
                        BAE_NO_ERROR)
                    {
                        ext = song_ext;
                        have_osc = true;
                    }
                }
            }
            if (!have_osc)
            {
                continue;
            }
            uint32_t sample_count = 0;
            if (BAERmfEditorDocument_GetSampleCount(export_doc, &sample_count) == BAE_NO_ERROR)
            {
                for (uint32_t si = sample_count; si > 0; --si)
                {
                    uint32_t sample_inst = BAE_EDITOR_INST_ID_NONE;
                    if (BAERmfEditorDocument_GetInstIDForSample(export_doc, si - 1, &sample_inst) ==
                            BAE_NO_ERROR &&
                        sample_inst == target_inst)
                    {
                        (void)BAERmfEditorDocument_DeleteSample(export_doc, si - 1);
                    }
                }
            }
            if (TEST_FLAG_VALUE(ext.flags1, ZBF_ghostInstrument))
            {
                ext.flags1 = static_cast<unsigned char>(ext.flags1 &
                                                        static_cast<unsigned char>(~ZBF_ghostInstrument));
                (void)BAERmfEditorDocument_SetInstrumentExtInfo(export_doc, target_inst, &ext);
            }
        }
#endif

        /* Apply chosen sound storage type to embedded (non-alias) samples. */
        {
            BAERmfEditorSndStorageType storage = BAE_EDITOR_SND_STORAGE_ESND;
            if (m_export_snd_storage_index == static_cast<int>(BAE_EDITOR_SND_STORAGE_CSND))
            {
                storage = BAE_EDITOR_SND_STORAGE_CSND;
            }
            else if (m_export_snd_storage_index == static_cast<int>(BAE_EDITOR_SND_STORAGE_SND))
            {
                storage = BAE_EDITOR_SND_STORAGE_SND;
            }
            uint32_t sample_count = 0;
            if (BAERmfEditorDocument_GetSampleCount(export_doc, &sample_count) == BAE_NO_ERROR)
            {
                for (uint32_t si = 0; si < sample_count; ++si)
                {
                    bool is_alias = false;
                    if (BAERmfEditorDocument_IsSampleBankAlias(export_doc, si, &is_alias) ==
                            BAE_NO_ERROR &&
                        is_alias)
                    {
                        continue;
                    }
                    BAERmfEditorSampleInfo info;
                    std::memset(&info, 0, sizeof(info));
                    if (BAERmfEditorDocument_GetSampleInfo(export_doc, si, &info) != BAE_NO_ERROR)
                    {
                        continue;
                    }
                    info.sndStorageType = storage;
                    (void)BAERmfEditorDocument_SetSampleInfo(export_doc, si, &info);
                }
            }
        }

        uint32_t reason = 0;
        const bool requires_zmf = BAERmfEditorDocument_RequiresZmf(export_doc, &reason) != FALSE;
        if (requires_zmf && !EndsWith(out_path, ".zmf"))
        {
            const size_t dot = out_path.find_last_of('.');
            if (dot != std::string::npos)
            {
                out_path = out_path.substr(0, dot) + ".zmf";
            }
            else
            {
                out_path += ".zmf";
            }
            char reason_buf[256] = {0};
            BAEZMFReasonCodeToString(reason, reason_buf, sizeof(reason_buf));
            SetStatus(std::string("ZMF required (") + reason_buf + "); exporting as .zmf");
        }

        UpdateBusyProgress(0.80f, "Dropping unused samples…");
        (void)DropUnreferencedSamplesFromDocument(export_doc);

        UpdateBusyProgress(0.85f, "Writing exported song file…");
        BAEResult r = BAERmfEditorDocument_SaveAsRmf(export_doc, const_cast<char *>(out_path.c_str()));
        if (r == BAE_NO_ERROR && !prepare_ids.empty())
        {
            /* Safety net: refuse ALIS-without-INST (silent ZMF). Rebuild OSCL
             * INST from the export doc and/or copy from the session bank. */
            const BAEResult patch = BAERmfEditorDocument_PatchMissingInstIntoSongFile(
                export_doc,
                const_cast<char *>(out_path.c_str()),
                m_bank_token,
                prepare_ids.data(),
                static_cast<uint32_t>(prepare_ids.size()));
            if (patch != BAE_NO_ERROR)
            {
                r = patch;
            }
        }
        BAERmfEditorDocument_Delete(export_doc);
        UpdateBusyProgress(1.00f, "Export Complete");
        if (r == BAE_NO_ERROR)
        {
            uint32_t embedded = 0;
            uint32_t ghosted = 0;
            uint32_t bank_ref = 0;
            for (uint32_t i = 0; i < clone_result.mappingCount; ++i)
            {
                const uint32_t rid = clone_result.mappings[i].resolvedInstID;
                if (m_export_embed_inst_ids.count(rid) != 0)
                {
                    ++embedded;
                }
                else if (m_export_ghost_inst_ids.count(rid) != 0)
                {
                    ++ghosted;
                }
                else
                {
                    ++bank_ref;
                }
            }
            char status[256];
            std::snprintf(status,
                          sizeof(status),
                          "Exported %s (%u embedded, %u ghost, %u bank-ref)",
                          FileNameFromPath(out_path).c_str(),
                          static_cast<unsigned>(embedded),
                          static_cast<unsigned>(ghosted),
                          static_cast<unsigned>(bank_ref));
            SetStatus(status);
        }
        else
        {
            SetStatus(std::string("Export failed: ") + FormatBAEError(r));
        }
    }

    void RefreshSongSamplesFromDocument()
    {
        // Drop previous song-sourced sample rows, keep bank rows.
        m_samples.erase(std::remove_if(m_samples.begin(),
                                       m_samples.end(),
                                       [](const SampleRow &s) { return s.source == SampleSource::Song; }),
                        m_samples.end());

        if (!m_document)
        {
            return;
        }

        uint32_t sample_count = 0;
        if (BAERmfEditorDocument_GetSampleCount(m_document, &sample_count) != BAE_NO_ERROR)
        {
            return;
        }

        uint32_t row_index = static_cast<uint32_t>(m_samples.size());
        for (uint32_t i = 0; i < sample_count; ++i)
        {
            BAERmfEditorSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorDocument_GetSampleInfo(m_document, i, &info) != BAE_NO_ERROR)
            {
                continue;
            }

            uint32_t asset_id = 0;
            if (BAERmfEditorDocument_GetSampleAssetIDForSample(m_document, i, &asset_id) !=
                BAE_NO_ERROR)
            {
                asset_id = 0;
            }

            /* After RMF promote the same SND is listed as a bank custom row —
             * keep the bank copy (editable) and skip the song duplicate. */
            if (asset_id != 0)
            {
                const bool bank_has_same_snd = std::any_of(
                    m_samples.begin(),
                    m_samples.end(),
                    [asset_id](const SampleRow &s) {
                        return s.source == SampleSource::Bank && s.snd_resource_id == asset_id;
                    });
                if (bank_has_same_snd)
                {
                    continue;
                }
            }

            SampleRow row;
            row.index = row_index++;
            row.document_sample_index = i;
            row.source = SampleSource::Song;
            row.is_custom = true;
            row.program = static_cast<int>(info.program);
            row.root_key = static_cast<int>(info.rootKey);
            row.low_key = static_cast<int>(info.lowKey);
            row.high_key = static_cast<int>(info.highKey);
            row.frame_count = info.sampleInfo.waveFrames;
            row.loop_start = info.sampleInfo.startLoop;
            row.loop_end = info.sampleInfo.endLoop;
            row.bit_depth = info.sampleInfo.bitSize ? info.sampleInfo.bitSize : 16;
            row.channels = info.sampleInfo.channels ? info.sampleInfo.channels : 1;
            row.sample_rate_hz = info.sampleInfo.sampledRate
                                     ? static_cast<uint32_t>(info.sampleInfo.sampledRate >> 16)
                                     : 44100;
            row.snd_resource_id = asset_id;
            row.codec_label = FormatVisibleCodecLabelFromEditorType(info.compressionType);
            /* List UI already tags Song rows with [Song]; don't prefix the name. */
            if (info.displayName && info.displayName[0])
            {
                row.name = info.displayName;
            }
            else
            {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "Sample %u", i);
                row.name = buf;
            }
            m_samples.push_back(row);
        }
        SortSampleListAlphabetical();
    }

    void DrawSessionWindow()
    {
        ApplyPendingFloatingWindowPlacement("Session");
        ImGuiWindowFlags session_flags = 0;
        if (m_session_undocked_layout)
        {
            session_flags |= ImGuiWindowFlags_NoDocking;
        }
        if (!ImGui::Begin("Session", nullptr, session_flags))
        {
            m_session_window_focused = false;
            ImGui::End();
            return;
        }
        m_session_window_focused =
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (ImGui::BeginTabBar("##SessionTabs"))
        {
            const ImGuiTabItemFlags songs_flags =
                (m_pending_session_tab == static_cast<int>(SessionTab::Songs))
                    ? ImGuiTabItemFlags_SetSelected
                    : ImGuiTabItemFlags_None;
            const ImGuiTabItemFlags instruments_flags =
                (m_pending_session_tab == static_cast<int>(SessionTab::Instruments))
                    ? ImGuiTabItemFlags_SetSelected
                    : ImGuiTabItemFlags_None;
            const ImGuiTabItemFlags samples_flags =
                (m_pending_session_tab == static_cast<int>(SessionTab::Samples))
                    ? ImGuiTabItemFlags_SetSelected
                    : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem("Songs", nullptr, songs_flags))
            {
                m_session_tab = SessionTab::Songs;
                DrawSongsListWindow();
                ImGui::TextDisabled("Drop MIDI / RMF / ZMF / Session files here");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Instruments", nullptr, instruments_flags))
            {
                m_session_tab = SessionTab::Instruments;
                DrawInstrumentListWindow();
                ImGui::TextDisabled("Drop bank (.hsb/.zsb/.bsn/.zsn) to replace Bank 0/1 — Bank 2+ kept");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Samples", nullptr, samples_flags))
            {
                m_session_tab = SessionTab::Samples;
                DrawSampleListWindow();
                ImGui::TextDisabled("Drop WAV / AIFF / MP3 / OGG / FLAC / Opus / QOA here");
                ImGui::EndTabItem();
            }
            m_pending_session_tab = -1;
            ImGui::EndTabBar();
        }
        /* Session list Ctrl/Del shortcuts: HandleGlobalAppShortcuts (end of DrawUI). */
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
        {
            switch (m_session_tab)
            {
            case SessionTab::Songs:
                m_dnd_hover_target = SessionDropTarget::Songs;
                break;
            case SessionTab::Instruments:
                m_dnd_hover_target = SessionDropTarget::Instruments;
                break;
            case SessionTab::Samples:
                m_dnd_hover_target = SessionDropTarget::Samples;
                break;
            }
        }
        ImGui::End();
    }

    void DrawSongsListWindow()
    {
        static const char *kSongShowLabels[] = {
            "All",
            "Custom Songs",
            "Groovoids",
        };
        const int song_filter = static_cast<int>(m_song_show_filter);
        ImGui::Text("Show:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("##SongShowFilter",
                              kSongShowLabels[song_filter >= 0 && song_filter < 3 ? song_filter : 0]))
        {
            for (int i = 0; i < 3; ++i)
            {
                const bool selected = (song_filter == i);
                if (ImGui::Selectable(kSongShowLabels[i], selected))
                {
                    m_song_show_filter = static_cast<SongShowFilter>(i);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (!m_loaded_session_path.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("| Session .bsn · %d CaSd", m_session_casd_count);
        }
        ImGui::Separator();

        const bool show_custom = (m_song_show_filter != SongShowFilter::Groovoids);
        const bool show_groovoids = (m_song_show_filter != SongShowFilter::Custom);

        if (show_custom && ImGui::BeginPopupContextWindow("song_list_bg_ctx",
                                                          ImGuiPopupFlags_MouseButtonRight |
                                                              ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Paste", nullptr, false, CanPasteSongs()))
            {
                PasteSongsClipboard();
            }
            if (ImGui::MenuItem("Select All", nullptr, false, !m_session_songs.empty()))
            {
                SessionSelectAllVisible();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Create Empty Song"))
            {
                CreateEmptySong();
            }
            if (ImGui::MenuItem("Add Song..."))
            {
                OpenAddSongFileDialog();
            }
            ImGui::EndPopup();
        }

        bool drew_any = false;

        if (show_custom)
        {
            if (m_session_songs.empty() && m_document)
            {
                (void)CommitActiveSessionSong();
            }

            for (int i = 0; i < static_cast<int>(m_session_songs.size()); ++i)
            {
                ImGui::PushID(i);
                const SessionSongEntry &entry = m_session_songs[static_cast<size_t>(i)];
                const char *tag = entry.source_type.empty() ? "Song" : entry.source_type.c_str();
                if (entry.source_type.empty() && !m_loaded_session_path.empty())
                {
                    tag = EndsWith(m_loaded_session_path, ".zsn") ? "ZSN" : "BSN";
                }
                char line[512];
                std::snprintf(line,
                              sizeof(line),
                              "%s  [%s]%s",
                              entry.name.c_str(),
                              tag,
                              (i == m_session_song_index && m_document_dirty) ? " *" : "");
                const bool selected =
                    (m_multi_songs.count(i) != 0) || (i == m_session_song_index && m_groovoid_index < 0);
                if (ImGui::Selectable(line, selected))
                {
                    const ImGuiIO &io = ImGui::GetIO();
                    int primary = m_session_song_index;
                    ApplyListSelection(m_multi_songs,
                                       primary,
                                       i,
                                       m_multi_song_anchor,
                                       io.KeyCtrl,
                                       io.KeyShift);
                    m_multi_song_anchor = i;
                    m_groovoid_index = -1;
                    if (primary != m_session_song_index)
                    {
                        ActivateSessionSongAt(primary);
                    }
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    m_groovoid_index = -1;
                    if (i != m_session_song_index)
                    {
                        ActivateSessionSongAt(i);
                    }
                    PlaySessionSong();
                }
                if (ImGui::BeginPopupContextItem("song_ctx"))
                {
                    if (ImGui::MenuItem("Play"))
                    {
                        m_groovoid_index = -1;
                        if (i != m_session_song_index)
                        {
                            ActivateSessionSongAt(i);
                        }
                        PlaySessionSong();
                    }
                    if (ImGui::MenuItem("Rename..."))
                    {
                        OpenRenameDialog("song", i, entry.name);
                    }
                    if (ImGui::MenuItem("Song Info...",
                                        nullptr,
                                        false,
                                        m_document != nullptr && i == m_session_song_index))
                    {
                        OpenSongInfoDialog();
                    }
                    if (ImGui::MenuItem("MIDI Editor..."))
                    {
                        m_groovoid_index = -1;
                        if (i != m_session_song_index)
                        {
                            ActivateSessionSongAt(i);
                        }
                        OpenMidiEditor();
                    }
                    if (ImGui::MenuItem("Lyrics..."))
                    {
                        m_groovoid_index = -1;
                        if (i != m_session_song_index)
                        {
                            ActivateSessionSongAt(i);
                        }
                        OpenLyricsEditor();
                    }
                    if (ImGui::MenuItem("Export RMF/ZMF...",
                                        nullptr,
                                        false,
                                        m_document != nullptr && i == m_session_song_index))
                    {
                        OpenExportRmfInstrumentDialog();
                    }
                    if (ImGui::MenuItem("Export MIDI...",
                                        nullptr,
                                        false,
                                        m_document != nullptr && i == m_session_song_index))
                    {
                        RequestExportMidi();
                    }
                    if (ImGui::MenuItem("Get Resource Usage..."))
                    {
                        OpenResourceUsageForSong(i);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Cut"))
                    {
                        m_session_song_index = i;
                        m_multi_songs.insert(i);
                        CopySongsSelection(true);
                    }
                    if (ImGui::MenuItem("Copy"))
                    {
                        m_session_song_index = i;
                        m_multi_songs.insert(i);
                        CopySongsSelection(false);
                    }
                    if (ImGui::MenuItem("Paste", nullptr, false, CanPasteSongs()))
                    {
                        PasteSongsClipboard();
                    }
                    if (ImGui::MenuItem("Select All"))
                    {
                        SessionSelectAllVisible();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Create Empty Song"))
                    {
                        CreateEmptySong();
                    }
                    if (ImGui::MenuItem("Add Song..."))
                    {
                        OpenAddSongFileDialog();
                    }
                    if (ImGui::MenuItem("Delete Song"))
                    {
                        m_session_song_index = i;
                        if (m_multi_songs.count(i) == 0)
                        {
                            m_multi_songs.clear();
                            m_multi_songs.insert(i);
                        }
                        RequestDeleteSessionSongs(CollectTargetSongIndices());
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                drew_any = true;
            }
        }

        if (show_groovoids)
        {
            for (int i = 0; i < static_cast<int>(m_groovoids.size()); ++i)
            {
                const GroovoidEntry &entry = m_groovoids[static_cast<size_t>(i)];
                char line[512];
                std::snprintf(line,
                              sizeof(line),
                              "%s  [Groovoid]",
                              entry.name.c_str());
                const bool selected = (i == m_groovoid_index);
                ImGui::PushID(i + 100000);
                if (ImGui::Selectable(line, selected))
                {
                    m_groovoid_index = i;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    (void)PlayGroovoidAt(i);
                }
                if (ImGui::BeginPopupContextItem("groovoid_ctx"))
                {
                    if (ImGui::MenuItem("Play"))
                    {
                        (void)PlayGroovoidAt(i);
                    }
                    if (ImGui::MenuItem("Export as RMF..."))
                    {
                        BeginGroovoidRmfExport(i);
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                drew_any = true;
            }
        }

        if (drew_any)
        {
            if (show_custom && m_document_dirty && m_groovoid_index < 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                                   "Document modified — play/export will re-serialize");
            }
            return;
        }

        if (m_song_show_filter == SongShowFilter::Groovoids)
        {
            ImGui::TextDisabled(m_bank_token ? "(no groovoids in bank)"
                                            : "(load a bank to list groovoids)");
            return;
        }

        ImGui::TextDisabled(
            "(no song in Session — drop MIDI/RMF/ZMF, right-click Add Song…, or File → Open)");
    }

    void OpenSongInfoDialog()
    {
        if (!m_document)
        {
            return;
        }
        auto copy_info = [this](BAEInfoType t, char *buf, size_t n) {
            const char *v = BAERmfEditorDocument_GetInfo(m_document, t);
            std::snprintf(buf, n, "%s", v ? v : "");
        };
        copy_info(TITLE_INFO, m_song_info_title, sizeof(m_song_info_title));
        copy_info(COMPOSER_INFO, m_song_info_composer, sizeof(m_song_info_composer));
        copy_info(COPYRIGHT_INFO, m_song_info_copyright, sizeof(m_song_info_copyright));
        copy_info(PERFORMED_BY_INFO, m_song_info_performed, sizeof(m_song_info_performed));
        copy_info(PUBLISHER_CONTACT_INFO, m_song_info_publisher, sizeof(m_song_info_publisher));
        copy_info(LICENSED_TO_URL_INFO, m_song_info_license_url, sizeof(m_song_info_license_url));
        copy_info(GENRE_INFO, m_song_info_genre, sizeof(m_song_info_genre));
        copy_info(SUB_GENRE_INFO, m_song_info_subgenre, sizeof(m_song_info_subgenre));
        copy_info(COMPOSER_NOTES_INFO, m_song_info_notes, sizeof(m_song_info_notes));
        uint32_t bpm = 120;
        uint16_t tpq = 480;
        BAERmfEditorDocument_GetTempoBPM(m_document, &bpm);
        BAERmfEditorDocument_GetTicksPerQuarter(m_document, &tpq);
        m_song_info_bpm = static_cast<int>(bpm);
        m_song_info_tpq = static_cast<int>(tpq);
        bool loop_enabled = false;
        uint32_t loop_start = 0, loop_end = 0;
        int32_t loop_count = 0;
        BAERmfEditorDocument_GetMidiLoopMarkers(m_document, &loop_enabled, &loop_start, &loop_end, &loop_count);
        m_song_info_loop_enabled = loop_enabled;
        m_song_info_loop_start = static_cast<int>(loop_start);
        m_song_info_loop_end = static_cast<int>(loop_end);
        m_song_info_loop_count = static_cast<int>(loop_count);
        BAERmfEditorMidiStorageType storage = BAE_EDITOR_MIDI_STORAGE_ECMI;
        BAERmfEditorDocument_GetMidiStorageType(m_document, &storage);
        m_song_info_storage = static_cast<int>(storage);
        int32_t engine_flags = 0;
        (void)BAERmfEditorDocument_GetEngineConfig(m_document, &engine_flags);
        m_song_info_classic_chorus = (engine_flags & SONG_CONFIG_CLASSIC_CHORUS_ON) != 0;
        m_song_info_pan_fix = (engine_flags & SONG_CONFIG_PANFIX_ON) != 0;
        /* Opt-in stamps of current Player controls; not persisted song state. */
        m_song_info_apply_player_reverb = false;
        m_song_info_apply_player_velocity_curve = false;
        m_song_info_open = true;
    }

    void ApplySongInfoFieldsToDocument()
    {
        if (!m_document)
        {
            return;
        }
        BAERmfEditorDocument_SetInfo(m_document, TITLE_INFO, m_song_info_title);
        BAERmfEditorDocument_SetInfo(m_document, COMPOSER_INFO, m_song_info_composer);
        BAERmfEditorDocument_SetInfo(m_document, COPYRIGHT_INFO, m_song_info_copyright);
        BAERmfEditorDocument_SetInfo(m_document, PERFORMED_BY_INFO, m_song_info_performed);
        BAERmfEditorDocument_SetInfo(m_document, PUBLISHER_CONTACT_INFO, m_song_info_publisher);
        BAERmfEditorDocument_SetInfo(m_document, LICENSED_TO_URL_INFO, m_song_info_license_url);
        BAERmfEditorDocument_SetInfo(m_document, GENRE_INFO, m_song_info_genre);
        BAERmfEditorDocument_SetInfo(m_document, SUB_GENRE_INFO, m_song_info_subgenre);
        BAERmfEditorDocument_SetInfo(m_document, COMPOSER_NOTES_INFO, m_song_info_notes);
        BAERmfEditorDocument_SetTempoBPM(m_document, static_cast<uint32_t>(std::max(1, m_song_info_bpm)));
        BAERmfEditorDocument_SetTicksPerQuarter(m_document, static_cast<uint16_t>(std::max(1, m_song_info_tpq)));
        BAERmfEditorDocument_SetMidiLoopMarkers(m_document,
                                                 m_song_info_loop_enabled,
                                                 static_cast<uint32_t>(std::max(0, m_song_info_loop_start)),
                                                 static_cast<uint32_t>(std::max(0, m_song_info_loop_end)),
                                                 static_cast<int32_t>(m_song_info_loop_count));
        BAERmfEditorDocument_SetMidiStorageType(
            m_document, static_cast<BAERmfEditorMidiStorageType>(m_song_info_storage));

        int32_t engine_flags = 0;
        (void)BAERmfEditorDocument_GetEngineConfig(m_document, &engine_flags);
        engine_flags &= ~(SONG_CONFIG_HAS_CLASSIC_CHORUS | SONG_CONFIG_CLASSIC_CHORUS_ON |
                          SONG_CONFIG_HAS_PANFIX | SONG_CONFIG_PANFIX_ON);
        if (m_song_info_classic_chorus)
        {
            engine_flags |= SONG_CONFIG_HAS_CLASSIC_CHORUS | SONG_CONFIG_CLASSIC_CHORUS_ON;
        }
        if (m_song_info_pan_fix)
        {
            engine_flags |= SONG_CONFIG_HAS_PANFIX | SONG_CONFIG_PANFIX_ON;
        }
        (void)BAERmfEditorDocument_SetEngineConfig(m_document, engine_flags);
        (void)BAE_SetClassicChorus(m_song_info_classic_chorus ? TRUE : FALSE);
        (void)BAE_SetSpanDCFix(m_song_info_pan_fix ? TRUE : FALSE);

        m_document_dirty = true;
        UpdateSongRowFromDocument(m_loaded_doc_path.empty() ? m_song_row.path : m_loaded_doc_path,
                                  m_song_row.source_type.c_str());
    }

    void DrawSongInfoDialog()
    {
        if (!m_song_info_open)
        {
            return;
        }
        bool open = m_song_info_open;
        if (ImGui::Begin("Song Info", &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Title", m_song_info_title, sizeof(m_song_info_title));
            ImGui::InputText("Composer", m_song_info_composer, sizeof(m_song_info_composer));
            ImGui::InputText("Copyright", m_song_info_copyright, sizeof(m_song_info_copyright));
            ImGui::InputText("Performed by", m_song_info_performed, sizeof(m_song_info_performed));
            ImGui::InputText("Publisher", m_song_info_publisher, sizeof(m_song_info_publisher));
            ImGui::InputText("License URL", m_song_info_license_url, sizeof(m_song_info_license_url));
            ImGui::InputText("Genre", m_song_info_genre, sizeof(m_song_info_genre));
            ImGui::InputText("Sub-genre", m_song_info_subgenre, sizeof(m_song_info_subgenre));
            ImGui::InputTextMultiline("Composer notes",
                                      m_song_info_notes,
                                      sizeof(m_song_info_notes),
                                      ImVec2(420, 60));
            ImGui::InputInt("Tempo BPM", &m_song_info_bpm);
            ImGui::InputInt("Ticks/Quarter", &m_song_info_tpq);
            ImGui::Checkbox("Loop markers", &m_song_info_loop_enabled);
            ImGui::InputInt("Loop start tick", &m_song_info_loop_start);
            ImGui::InputInt("Loop end tick", &m_song_info_loop_end);
            ImGui::InputInt("Loop count", &m_song_info_loop_count);
            static const char *storage_labels[] = {"CMID", "ECMI", "EMID", "MIDI"};
            ImGui::Combo("MIDI storage", &m_song_info_storage, storage_labels, IM_ARRAYSIZE(storage_labels));
            ImGui::SeparatorText("Engine (requires ZMF)");
            ImGui::Checkbox("Classic Chorus order", &m_song_info_classic_chorus);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip("Per-song classic chorus ordering. Forces ZMF on export.");
            }
            ImGui::Checkbox("Stereo Pan LFO DC Fix", &m_song_info_pan_fix);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip("Per-song STEREO_PAN LFO DC fix. Forces ZMF on export.");
            }
            if (m_song_info_classic_chorus || m_song_info_pan_fix)
            {
                ImGui::TextDisabled("Enabled options require .zmf / .zsn export.");
            }
            ImGui::SeparatorText("Player mix (tick 0 NRPN)");
            ImGui::Checkbox("Apply Player Reverb to Song", &m_song_info_apply_player_reverb);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip(
                    "On Apply, write the Player panel reverb as NRPN 7,0 at tick 0.");
            }
            ImGui::Checkbox("Apply Player Velocity Curve to Song",
                            &m_song_info_apply_player_velocity_curve);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip(
                    "On Apply, write the Player panel velocity curve as NRPN 4,0 at tick 0.");
            }
            if (ImGui::Button("Apply"))
            {
                const bool stamp_reverb = m_song_info_apply_player_reverb;
                const bool stamp_velocity = m_song_info_apply_player_velocity_curve;
                ApplySongInfoFieldsToDocument();
                bool nrpn_ok = true;
                if (stamp_reverb || stamp_velocity)
                {
                    nrpn_ok = WritePlayerMixNrpnsToSong(stamp_velocity, stamp_reverb);
                    /* One-shot; leave unchecked so a later Apply does not re-stamp. */
                    m_song_info_apply_player_reverb = false;
                    m_song_info_apply_player_velocity_curve = false;
                }
                if (stamp_reverb || stamp_velocity)
                {
                    /* WritePlayerMixNrpnsToSong already set a detailed status. */
                    if (!nrpn_ok)
                    {
                        SetStatus("Song Info applied; player mix NRPN write failed");
                    }
                }
                else
                {
                    SetStatus("Song Info applied");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Defaults"))
            {
                std::snprintf(m_pref_song_title, sizeof(m_pref_song_title), "%s", m_song_info_title);
                std::snprintf(m_pref_song_composer, sizeof(m_pref_song_composer), "%s", m_song_info_composer);
                std::snprintf(m_pref_song_copyright, sizeof(m_pref_song_copyright), "%s", m_song_info_copyright);
                std::snprintf(m_pref_song_performed, sizeof(m_pref_song_performed), "%s", m_song_info_performed);
                std::snprintf(m_pref_song_publisher, sizeof(m_pref_song_publisher), "%s", m_song_info_publisher);
                std::snprintf(m_pref_song_license_url, sizeof(m_pref_song_license_url), "%s", m_song_info_license_url);
                std::snprintf(m_pref_song_genre, sizeof(m_pref_song_genre), "%s", m_song_info_genre);
                std::snprintf(m_pref_song_subgenre, sizeof(m_pref_song_subgenre), "%s", m_song_info_subgenre);
                std::snprintf(m_pref_song_notes, sizeof(m_pref_song_notes), "%s", m_song_info_notes);
                SaveEditorPrefsToDisk();
                SetStatus("Song Info defaults saved");
            }
            ImGui::SameLine();
            if (ImGui::Button("Use Defaults"))
            {
                std::snprintf(m_song_info_title, sizeof(m_song_info_title), "%s", m_pref_song_title);
                std::snprintf(m_song_info_composer, sizeof(m_song_info_composer), "%s", m_pref_song_composer);
                std::snprintf(m_song_info_copyright, sizeof(m_song_info_copyright), "%s", m_pref_song_copyright);
                std::snprintf(m_song_info_performed, sizeof(m_song_info_performed), "%s", m_pref_song_performed);
                std::snprintf(m_song_info_publisher, sizeof(m_song_info_publisher), "%s", m_pref_song_publisher);
                std::snprintf(m_song_info_license_url, sizeof(m_song_info_license_url), "%s", m_pref_song_license_url);
                std::snprintf(m_song_info_genre, sizeof(m_song_info_genre), "%s", m_pref_song_genre);
                std::snprintf(m_song_info_subgenre, sizeof(m_song_info_subgenre), "%s", m_pref_song_subgenre);
                std::snprintf(m_song_info_notes, sizeof(m_song_info_notes), "%s", m_pref_song_notes);
                SetStatus("Song Info defaults loaded into dialog");
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                open = false;
            }
        }
        ImGui::End();
        m_song_info_open = open;
    }

    static const char *AdsrStageFlagName(int32_t flags)
    {
        if (flags == static_cast<int32_t>(FOUR_CHAR('L', 'I', 'N', 'E')))
        {
            return "LINE";
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('S', 'U', 'S', 'T')))
        {
            return "SUST";
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('L', 'A', 'S', 'T')))
        {
            return "LAST";
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('G', 'O', 'T', 'O')))
        {
            return "GOTO";
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('G', 'O', 'S', 'T')))
        {
            return "GOTO_COND";
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('R', 'E', 'L', 'S')))
        {
            return "RELS";
        }
        return "OFF";
    }

    static ImU32 AdsrStageFlagColor(int32_t flags)
    {
        if (flags == static_cast<int32_t>(FOUR_CHAR('S', 'U', 'S', 'T')))
        {
            return IM_COL32(80, 200, 80, 255);
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('R', 'E', 'L', 'S')))
        {
            return IM_COL32(200, 80, 80, 255);
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('L', 'A', 'S', 'T')))
        {
            return IM_COL32(200, 200, 60, 255);
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('G', 'O', 'T', 'O')))
        {
            return IM_COL32(140, 100, 220, 255);
        }
        if (flags == static_cast<int32_t>(FOUR_CHAR('G', 'O', 'S', 'T')))
        {
            return IM_COL32(100, 140, 220, 255);
        }
        return IM_COL32(98, 215, 255, 255);
    }

    static int32_t AdsrCycleStageFlags(int32_t flags)
    {
        static const int32_t kCycle[] = {
            static_cast<int32_t>(FOUR_CHAR('L', 'I', 'N', 'E')),
            static_cast<int32_t>(FOUR_CHAR('S', 'U', 'S', 'T')),
            static_cast<int32_t>(FOUR_CHAR('R', 'E', 'L', 'S')),
            static_cast<int32_t>(FOUR_CHAR('L', 'A', 'S', 'T')),
            0,
        };
        for (size_t i = 0; i < sizeof(kCycle) / sizeof(kCycle[0]); ++i)
        {
            if (flags == kCycle[i])
            {
                return kCycle[(i + 1) % (sizeof(kCycle) / sizeof(kCycle[0]))];
            }
        }
        return kCycle[0];
    }

    /* Beatnik Editor units: VOLUME_RANGE (4096) → 100, FULL_RANGE (8192) → 200.
     * Stored engine values stay raw; UI shows/edits the scaled units. */
    static constexpr int32_t kAdsrVolumeRange = 4096;
    static constexpr int kAdsrBeatnikLevelMin = 0;
    static constexpr int kAdsrBeatnikLevelMax = 200; /* FULL_RANGE — BE2 envelope limit */
    /* LFO/filter ADSR may be bipolar (−200…200); volume ADSR stays unipolar (0…200). */
    static constexpr int kAdsrBeatnikLevelMinSigned = -kAdsrBeatnikLevelMax;
    /* UI times are milliseconds; engine stores microseconds. Cap avoids int32 overflow. */
    static constexpr int kAdsrTimeMsMax =
        static_cast<int>((std::numeric_limits<int32_t>::max)() / 1000);
    static int AdsrLevelToBeatnik(int32_t raw)
    {
        const int64_t n = static_cast<int64_t>(raw) * 100;
        const int64_t bias = (raw >= 0) ? (kAdsrVolumeRange / 2) : -(kAdsrVolumeRange / 2);
        return static_cast<int>((n + bias) / kAdsrVolumeRange);
    }
    static int32_t AdsrLevelFromBeatnik(int display, int min_disp = kAdsrBeatnikLevelMin)
    {
        display = std::clamp(display, min_disp, kAdsrBeatnikLevelMax);
        return static_cast<int32_t>((static_cast<int64_t>(display) * kAdsrVolumeRange) / 100);
    }
    /* BE2 TIME (MS): µs↔ms. SUST stored as 0 shows as 12000 (classic scale/5 display). */
    static constexpr int kBe2SustainDisplayMs = 12000;
    static bool AdsrIsSustainFlag(int32_t flags)
    {
        return flags == static_cast<int32_t>(FOUR_CHAR('S', 'U', 'S', 'T'));
    }
    static int AdsrTimeUsToMs(int32_t us)
    {
        if (us >= 0)
        {
            return static_cast<int>((us + 500) / 1000);
        }
        return static_cast<int>((us - 500) / 1000);
    }
    static int32_t AdsrTimeMsToUs(int ms)
    {
        ms = std::clamp(ms, 0, kAdsrTimeMsMax);
        return static_cast<int32_t>(static_cast<int64_t>(ms) * 1000);
    }
    static int AdsrTimeToDisplayMs(int32_t flags, int32_t time_us)
    {
        if (AdsrIsSustainFlag(flags) && time_us == 0)
        {
            return kBe2SustainDisplayMs;
        }
        return std::clamp(AdsrTimeUsToMs(time_us), 0, kAdsrTimeMsMax);
    }
    static int32_t AdsrTimeFromDisplayMs(int32_t flags, int display_ms)
    {
        display_ms = std::clamp(display_ms, 0, kAdsrTimeMsMax);
        if (AdsrIsSustainFlag(flags) && display_ms == kBe2SustainDisplayMs)
        {
            return 0;
        }
        return AdsrTimeMsToUs(display_ms);
    }
    static bool LfoDestUsesBeatnikLevel(int32_t destination)
    {
        return destination == static_cast<int32_t>(FOUR_CHAR('V', 'O', 'L', 'U')) ||
               destination == static_cast<int32_t>(FOUR_CHAR('L', 'P', 'F', 'R')) ||
               destination == static_cast<int32_t>(FOUR_CHAR('L', 'P', 'R', 'E')) ||
               destination == static_cast<int32_t>(FOUR_CHAR('L', 'P', 'A', 'M')) ||
               /* WIDX: Beatnik 100 (=4096) × ADSR 100 ≈ full 0..5 wave table. */
               destination == static_cast<int32_t>(FOUR_CHAR('W', 'I', 'D', 'X')) ||
               /* PWID: same Beatnik 100 → engine ×128 ≈ ±50% pulse width. */
               destination == static_cast<int32_t>(FOUR_CHAR('P', 'W', 'I', 'D'));
    }

    /* BE "Envelope Only": hide LFO wave UI; sine + level 0 + DC 3072 + period 0. */
    static constexpr int32_t kLfoEnvelopeOnlyDcFeed = 3072;
    /* BE waveform-only ADSR (raw engine units; display may use Beatnik ÷4096). */
    static constexpr int32_t kLfoWaveformOnlyAdsrLevel = 65536;
    static constexpr int32_t kLfoWaveformOnlyAdsrHoldUs = 90000;

    struct FourCharLabel
    {
        int32_t value;
        const char *label;
    };

    enum LfoControlMode : int
    {
        kLfoModeAdsrOnly = 0,  /* hide waveform UI; BE sine/0/3072/period0 */
        kLfoModeWaveOnly = 1,  /* hide ADSR UI; BE flat ADSR */
        kLfoModeBoth = 2,      /* show both (explicit) */
    };

    static bool LfoMatchesEnvelopeOnlyDefaults(const BAERmfEditorLFOInfo &lfo)
    {
        return lfo.period == 0 &&
               lfo.level == 0 &&
               lfo.DC_feed == kLfoEnvelopeOnlyDcFeed &&
               lfo.waveShape == static_cast<int32_t>(FOUR_CHAR('S', 'I', 'N', 'E'));
    }

    static bool LfoIsWaveformOnlyAdsr(const BAERmfEditorADSRInfo &adsr)
    {
        return adsr.stageCount == 2 &&
               adsr.stages[0].level == kLfoWaveformOnlyAdsrLevel &&
               adsr.stages[0].time == 0 &&
               adsr.stages[0].flags == static_cast<int32_t>(FOUR_CHAR('L', 'I', 'N', 'E')) &&
               adsr.stages[1].level == kLfoWaveformOnlyAdsrLevel &&
               adsr.stages[1].time == kLfoWaveformOnlyAdsrHoldUs &&
               adsr.stages[1].flags == static_cast<int32_t>(FOUR_CHAR('L', 'A', 'S', 'T'));
    }

    static void LfoApplyEnvelopeOnlyDefaults(BAERmfEditorLFOInfo &lfo)
    {
        lfo.waveShape = static_cast<int32_t>(FOUR_CHAR('S', 'I', 'N', 'E'));
        lfo.level = 0;
        lfo.DC_feed = kLfoEnvelopeOnlyDcFeed;
        lfo.period = 0;
    }

    static void LfoApplyWaveformOnlyAdsrDefault(BAERmfEditorADSRInfo &adsr)
    {
        std::memset(&adsr, 0, sizeof(adsr));
        adsr.stageCount = 2;
        adsr.stages[0].level = kLfoWaveformOnlyAdsrLevel;
        adsr.stages[0].time = 0;
        adsr.stages[0].flags = static_cast<int32_t>(FOUR_CHAR('L', 'I', 'N', 'E'));
        adsr.stages[1].level = kLfoWaveformOnlyAdsrLevel;
        adsr.stages[1].time = kLfoWaveformOnlyAdsrHoldUs;
        adsr.stages[1].flags = static_cast<int32_t>(FOUR_CHAR('L', 'A', 'S', 'T'));
    }

    static int InferLfoControlMode(const BAERmfEditorLFOInfo &lfo)
    {
        /* Prefer ADSR-only when BE envelope-only waveform stub is present. */
        if (LfoMatchesEnvelopeOnlyDefaults(lfo))
        {
            return kLfoModeAdsrOnly;
        }
        if (LfoIsWaveformOnlyAdsr(lfo.adsr))
        {
            return kLfoModeWaveOnly;
        }
        return kLfoModeBoth;
    }

    void SyncLfoControlModesFromExt()
    {
        for (uint32_t i = 0; i < BAE_EDITOR_MAX_LFOS; ++i)
        {
            if (i < m_ie_ext.lfoCount)
            {
                m_ie_lfo_control_mode[i] = InferLfoControlMode(m_ie_ext.lfos[i]);
            }
            else
            {
                m_ie_lfo_control_mode[i] = kLfoModeBoth;
            }
        }
    }

    void RemoveLfoControlModeAt(uint32_t index)
    {
        if (index >= BAE_EDITOR_MAX_LFOS)
        {
            return;
        }
        for (uint32_t j = index + 1; j < BAE_EDITOR_MAX_LFOS; ++j)
        {
            m_ie_lfo_control_mode[j - 1] = m_ie_lfo_control_mode[j];
        }
        m_ie_lfo_control_mode[BAE_EDITOR_MAX_LFOS - 1] = kLfoModeBoth;
    }

    /* Shared LFO editor for Modulation + Filter pages (BE envelope/waveform-only modes). */
    void DrawInstrumentLfoEditor(BAERmfEditorLFOInfo &lfo,
                                 uint32_t lfo_index,
                                 const char *id,
                                 const FourCharLabel *dest_labels,
                                 int dest_label_count,
                                 const FourCharLabel *shape_labels,
                                 int shape_label_count)
    {
        ImGui::PushID(id);
        auto draw_four_char_combo = [this](const char *label,
                                           int32_t &value,
                                           const FourCharLabel *labels,
                                           int label_count) {
            int current = 0;
            for (int i = 0; i < label_count; ++i)
            {
                if (labels[i].value == value)
                {
                    current = i;
                    break;
                }
            }
            if (ImGui::BeginCombo(label, labels[current].label))
            {
                for (int i = 0; i < label_count; ++i)
                {
                    const bool selected = (i == current);
                    if (ImGui::Selectable(labels[i].label, selected))
                    {
                        value = labels[i].value;
                        MarkInstrumentEditorDirty();
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        };

        int mode = (lfo_index < BAE_EDITOR_MAX_LFOS) ? m_ie_lfo_control_mode[lfo_index]
                                                     : kLfoModeBoth;
        ImGui::TextUnformatted("Control with");
        ImGui::SameLine();
        if (ImGui::RadioButton("ADSR only", mode == kLfoModeAdsrOnly))
        {
            MarkInstrumentEditorDirty(false);
            LfoApplyEnvelopeOnlyDefaults(lfo);
            mode = kLfoModeAdsrOnly;
            if (lfo_index < BAE_EDITOR_MAX_LFOS)
            {
                m_ie_lfo_control_mode[lfo_index] = mode;
            }
            LivePatchInstrumentEditor();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip(
                "Hide waveform controls. Sets sine, level 0, DC feed 3072, period 0 "
                "(BE). Destination is driven by the ADSR only.");
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Waveform only", mode == kLfoModeWaveOnly))
        {
            MarkInstrumentEditorDirty(false);
            LfoApplyWaveformOnlyAdsrDefault(lfo.adsr);
            if (lfo.period == 0)
            {
                lfo.period = 513; /* engine: 0 or > 512 */
            }
            mode = kLfoModeWaveOnly;
            if (lfo_index < BAE_EDITOR_MAX_LFOS)
            {
                m_ie_lfo_control_mode[lfo_index] = mode;
            }
            LivePatchInstrumentEditor();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip(
                "Hide ADSR editor. Installs BE flat ADSR "
                "(LINE@65536/0ms + LAST@65536/90ms); destination follows the LFO wave.");
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("ADSR + Waveform", mode == kLfoModeBoth))
        {
            MarkInstrumentEditorDirty(false);
            if (lfo.period == 0)
            {
                lfo.period = 513;
            }
            mode = kLfoModeBoth;
            if (lfo_index < BAE_EDITOR_MAX_LFOS)
            {
                m_ie_lfo_control_mode[lfo_index] = mode;
            }
            LivePatchInstrumentEditor();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip("Show both waveform and ADSR editors.");
        }

        if (dest_labels && dest_label_count > 0)
        {
            const int32_t prev_dest = lfo.destination;
            draw_four_char_combo("Destination", lfo.destination, dest_labels, dest_label_count);
#if USE_ZMF_SUPPORT == TRUE
            /* Fresh WIDX/PWID: Beatnik 100 (=4096) is a useful unity depth. */
            if (lfo.destination != prev_dest &&
                (lfo.destination == static_cast<int32_t>(FOUR_CHAR('W', 'I', 'D', 'X')) ||
                 lfo.destination == static_cast<int32_t>(FOUR_CHAR('P', 'W', 'I', 'D'))) &&
                lfo.level == 0 && lfo.DC_feed == 0)
            {
                lfo.DC_feed = 4096;
                if (lfo.destination == static_cast<int32_t>(FOUR_CHAR('P', 'W', 'I', 'D')))
                {
                    /* Oscillating PWM: put depth in level, not DC. */
                    lfo.level = 4096;
                    lfo.DC_feed = 0;
                }
                if (lfo.adsr.stageCount == 0)
                {
                    LfoApplyWaveformOnlyAdsrDefault(lfo.adsr);
                }
                LivePatchInstrumentEditor();
            }
#endif
        }

        const bool beatnik_levels = LfoDestUsesBeatnikLevel(lfo.destination);
        const bool show_wave = (mode != kLfoModeAdsrOnly);
        const bool show_adsr = (mode != kLfoModeWaveOnly);

        if (show_wave)
        {
            if (shape_labels && shape_label_count > 0)
            {
                draw_four_char_combo("Wave shape",
                                     lfo.waveShape,
                                     shape_labels,
                                     shape_label_count);
            }
            /* Engine stores period in µs; UI edits milliseconds. */
            int period_ms = (lfo.period <= 0) ? 0 : AdsrTimeUsToMs(lfo.period);
            if (ImGui::InputInt("period (ms)", &period_ms))
            {
                if (period_ms <= 0)
                {
                    lfo.period = 513; /* wave mode: engine needs 0 or > 512 */
                }
                else
                {
                    lfo.period = AdsrTimeMsToUs(period_ms);
                    if (lfo.period != 0 && lfo.period <= 512)
                    {
                        lfo.period = 513;
                    }
                }
                MarkInstrumentEditorDirty();
            }
            if (beatnik_levels)
            {
                int dc = AdsrLevelToBeatnik(lfo.DC_feed);
                int lvl = AdsrLevelToBeatnik(lfo.level);
                if (ImGui::InputInt("DC_feed", &dc))
                {
                    dc = std::clamp(dc, kAdsrBeatnikLevelMin, kAdsrBeatnikLevelMax);
                    lfo.DC_feed = AdsrLevelFromBeatnik(dc);
                    MarkInstrumentEditorDirty();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                {
                    ImGui::SetTooltip("%d–%d (100 = unity)",
                                      kAdsrBeatnikLevelMin,
                                      kAdsrBeatnikLevelMax);
                }
                if (ImGui::InputInt("level", &lvl))
                {
                    lvl = std::clamp(lvl, kAdsrBeatnikLevelMin, kAdsrBeatnikLevelMax);
                    lfo.level = AdsrLevelFromBeatnik(lvl);
                    MarkInstrumentEditorDirty();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                {
                    ImGui::SetTooltip("%d–%d (100 = unity)",
                                      kAdsrBeatnikLevelMin,
                                      kAdsrBeatnikLevelMax);
                }
            }
            else
            {
                if (ImGui::InputInt("DC_feed", &lfo.DC_feed))
                {
                    MarkInstrumentEditorDirty();
                }
                if (ImGui::InputInt("level", &lfo.level))
                {
                    MarkInstrumentEditorDirty();
                }
            }
            DrawLfoWaveformGraph(lfo, "lfo_wave", 72.0f, beatnik_levels);
        }

        if (show_adsr)
        {
            /* Negatives OK for LFO/filter envelopes; volume ADSR stays ≥0. */
            DrawAdsrStageEditor(lfo.adsr, "lfo_adsr", beatnik_levels, true);
        }
        ImGui::PopID();
    }

    /* LPF UI clamps — match engine CLIP / BE TMPL (freq 0 = off). */
    static constexpr int kLpfFreqMin = 0;
    static constexpr int kLpfFreqMax = 127 * 256; /* MAXRESONANCE*256 */
    static constexpr int kLpfResMin = 0;
    static constexpr int kLpfResMax = 255;
    static constexpr int kLpfAmountMin = -256;
    static constexpr int kLpfAmountMax = 256;

    /* 2D pad: X = LPF frequency, Y = resonance. InputInts stay authoritative. */
    void DrawFilterFreqResPad()
    {
        constexpr int kFreqMin = kLpfFreqMin;
        constexpr int kFreqMax = kLpfFreqMax;
        constexpr int kResMin = kLpfResMin;
        constexpr int kResMax = kLpfResMax;
        const float height = 140.0f;
        const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##lpf_pad",
                               ImVec2(width, height),
                               ImGuiButtonFlags_MouseButtonLeft);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = canvas_pos;
        const ImVec2 p1(canvas_pos.x + width, canvas_pos.y + height);
        dl->AddRectFilled(p0, p1, IM_COL32(18, 20, 24, 255));
        dl->AddRect(p0, p1, IM_COL32(70, 70, 70, 255));

        const float margin = 8.0f;
        const float graph_l = p0.x + margin + 36.0f;
        const float graph_r = p1.x - margin - 8.0f;
        const float graph_t = p0.y + margin + 4.0f;
        const float graph_b = p1.y - margin - 14.0f;
        const float graph_w = graph_r - graph_l;
        const float graph_h = graph_b - graph_t;
        if (graph_w < 20.0f || graph_h < 20.0f)
        {
            return;
        }

        dl->AddLine(ImVec2(graph_l, graph_t), ImVec2(graph_l, graph_b), IM_COL32(50, 50, 50, 255));
        dl->AddLine(ImVec2(graph_l, graph_b), ImVec2(graph_r, graph_b), IM_COL32(50, 50, 50, 255));
        dl->AddText(ImVec2(p0.x + 4.0f, graph_t), IM_COL32(140, 140, 140, 255), "Res");
        dl->AddText(ImVec2(graph_r - 28.0f, graph_b + 2.0f), IM_COL32(140, 140, 140, 255), "Freq");

        auto freq_to_x = [&](int f) -> float {
            const float t = static_cast<float>(std::clamp(f, kFreqMin, kFreqMax) - kFreqMin) /
                            static_cast<float>(kFreqMax - kFreqMin);
            return graph_l + t * graph_w;
        };
        auto res_to_y = [&](int r) -> float {
            const float t = static_cast<float>(std::clamp(r, kResMin, kResMax) - kResMin) /
                            static_cast<float>(kResMax - kResMin);
            return graph_b - t * graph_h;
        };
        auto x_to_freq = [&](float x) -> int {
            const float t = std::clamp((x - graph_l) / graph_w, 0.0f, 1.0f);
            return kFreqMin + static_cast<int>(std::lround(t * (kFreqMax - kFreqMin)));
        };
        auto y_to_res = [&](float y) -> int {
            const float t = std::clamp((graph_b - y) / graph_h, 0.0f, 1.0f);
            return kResMin + static_cast<int>(std::lround(t * (kResMax - kResMin)));
        };

        static bool s_dragging = false;
        static bool s_pushed_undo = false;
        if (active && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            s_dragging = true;
            s_pushed_undo = false;
        }
        if (s_dragging && active)
        {
            if (!s_pushed_undo)
            {
                InstrumentEditorPushUndoFromPreEdit();
                s_pushed_undo = true;
            }
            const ImVec2 mp = ImGui::GetIO().MousePos;
            m_ie_ext.LPF_frequency = x_to_freq(mp.x);
            m_ie_ext.LPF_resonance = y_to_res(mp.y);
            m_ie_dirty = true;
        }
        if (s_dragging && !active)
        {
            s_dragging = false;
            s_pushed_undo = false;
            m_ie_dirty = true;
            LivePatchInstrumentEditor();
        }

        const float hx = freq_to_x(m_ie_ext.LPF_frequency);
        const float hy = res_to_y(m_ie_ext.LPF_resonance);
        dl->AddLine(ImVec2(hx, graph_t), ImVec2(hx, graph_b), IM_COL32(60, 90, 110, 180));
        dl->AddLine(ImVec2(graph_l, hy), ImVec2(graph_r, hy), IM_COL32(60, 90, 110, 180));
        dl->AddCircleFilled(ImVec2(hx, hy), 7.0f, IM_COL32(98, 215, 255, 255));
        dl->AddCircle(ImVec2(hx, hy), 7.0f, IM_COL32(20, 20, 20, 255), 0, 1.5f);
        char meta[96];
        std::snprintf(meta,
                      sizeof(meta),
                      "freq=%d  res=%d",
                      m_ie_ext.LPF_frequency,
                      m_ie_ext.LPF_resonance);
        dl->AddText(ImVec2(graph_l + 4.0f, graph_t + 2.0f), IM_COL32(150, 150, 150, 255), meta);
        ImGui::TextDisabled("Drag handle: X = frequency · Y = resonance (live-patches on release)");
    }

    void DrawKeymapZoneDiagram(uint32_t split_count)
    {
        const float height = 56.0f;
        const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##keymap_zones",
                               ImVec2(width, height),
                               ImGuiButtonFlags_MouseButtonLeft);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = canvas_pos;
        const ImVec2 p1(canvas_pos.x + width, canvas_pos.y + height);
        dl->AddRectFilled(p0, p1, IM_COL32(18, 20, 24, 255));
        dl->AddRect(p0, p1, IM_COL32(70, 70, 70, 255));

        const float margin = 6.0f;
        const float graph_l = p0.x + margin;
        const float graph_r = p1.x - margin;
        const float graph_t = p0.y + margin + 10.0f;
        const float graph_b = p1.y - margin - 10.0f;
        const float graph_w = graph_r - graph_l;
        if (graph_w < 40.0f)
        {
            return;
        }

        auto key_to_x = [&](int key) -> float {
            return graph_l + (static_cast<float>(std::clamp(key, 0, 127)) / 127.0f) * graph_w;
        };
        auto x_to_key = [&](float x) -> int {
            const float t = std::clamp((x - graph_l) / graph_w, 0.0f, 1.0f);
            return static_cast<int>(std::lround(t * 127.0f));
        };

        static const ImU32 kZoneCols[] = {
            IM_COL32(70, 140, 200, 160),
            IM_COL32(90, 180, 110, 160),
            IM_COL32(200, 150, 70, 160),
            IM_COL32(180, 90, 160, 160),
            IM_COL32(90, 170, 170, 160),
            IM_COL32(160, 120, 80, 160),
        };

        struct ZoneGeom
        {
            uint32_t split = 0;
            int low = 0;
            int high = 0;
            float x0 = 0.0f;
            float x1 = 0.0f;
        };
        std::vector<ZoneGeom> zones;
        zones.reserve(split_count);
        for (uint32_t s = 0; s < split_count; ++s)
        {
            BAERmfEditorBankSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token, m_ie_instrument_index, s, &info) !=
                BAE_NO_ERROR)
            {
                continue;
            }
            ZoneGeom z;
            z.split = s;
            z.low = std::clamp(static_cast<int>(info.lowKey), 0, 127);
            z.high = std::clamp(static_cast<int>(info.highKey), 0, 127);
            if (z.high < z.low)
            {
                std::swap(z.low, z.high);
            }
            z.x0 = key_to_x(z.low);
            z.x1 = key_to_x(z.high + 1);
            zones.push_back(z);
        }

        for (int k = 0; k <= 127; k += 12)
        {
            const float x = key_to_x(k);
            dl->AddLine(ImVec2(x, graph_t), ImVec2(x, graph_b), IM_COL32(45, 45, 50, 255));
            char lab[8];
            std::snprintf(lab, sizeof(lab), "%d", k);
            dl->AddText(ImVec2(x + 1.0f, p0.y + 1.0f), IM_COL32(110, 110, 110, 255), lab);
        }

        for (const ZoneGeom &z : zones)
        {
            const bool selected = (m_ie_selected_split == static_cast<int>(z.split));
            const ImU32 fill = kZoneCols[z.split % (sizeof(kZoneCols) / sizeof(kZoneCols[0]))];
            dl->AddRectFilled(ImVec2(z.x0, graph_t),
                              ImVec2(std::max(z.x0 + 2.0f, z.x1), graph_b),
                              fill);
            if (selected)
            {
                dl->AddRect(ImVec2(z.x0, graph_t),
                            ImVec2(std::max(z.x0 + 2.0f, z.x1), graph_b),
                            IM_COL32(255, 255, 255, 220),
                            0.0f,
                            0,
                            2.0f);
            }
            char idx[8];
            std::snprintf(idx, sizeof(idx), "%u", z.split);
            dl->AddText(ImVec2(z.x0 + 3.0f, graph_t + 2.0f), IM_COL32(240, 240, 240, 255), idx);
        }

        enum class EdgeDrag : int
        {
            None = 0,
            Low,
            High,
        };
        static EdgeDrag s_edge = EdgeDrag::None;
        static int s_edge_split = -1;
        const float edge_hit = 5.0f;
        const ImVec2 mp = ImGui::GetIO().MousePos;

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            s_edge = EdgeDrag::None;
            s_edge_split = -1;
            int hit_split = -1;
            for (const ZoneGeom &z : zones)
            {
                if (std::fabs(mp.x - z.x0) <= edge_hit)
                {
                    s_edge = EdgeDrag::Low;
                    s_edge_split = static_cast<int>(z.split);
                    hit_split = s_edge_split;
                    break;
                }
                if (std::fabs(mp.x - z.x1) <= edge_hit)
                {
                    s_edge = EdgeDrag::High;
                    s_edge_split = static_cast<int>(z.split);
                    hit_split = s_edge_split;
                    break;
                }
                if (mp.x >= z.x0 && mp.x <= z.x1)
                {
                    hit_split = static_cast<int>(z.split);
                }
            }
            if (hit_split >= 0)
            {
                m_ie_selected_split = hit_split;
            }
        }

        if (active && s_edge != EdgeDrag::None && s_edge_split >= 0 && m_bank_token)
        {
            BAERmfEditorBankSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                         m_ie_instrument_index,
                                                         static_cast<uint32_t>(s_edge_split),
                                                         &info) == BAE_NO_ERROR)
            {
                const int key = x_to_key(mp.x);
                if (s_edge == EdgeDrag::Low)
                {
                    info.lowKey = static_cast<unsigned char>(
                        std::clamp(key, 0, static_cast<int>(info.highKey)));
                }
                else
                {
                    info.highKey = static_cast<unsigned char>(
                        std::clamp(key, static_cast<int>(info.lowKey), 127));
                }
                /* INST-only: zone edges never touch SND headers. */
                info.sampleRate = 0;
                if (EnsureActiveBankWritable() &&
                    BAERmfEditorBank_SetInstrumentSampleInfo(m_bank_token,
                                                             m_ie_instrument_index,
                                                             static_cast<uint32_t>(s_edge_split),
                                                             &info) == BAE_NO_ERROR)
                {
                    SyncInstrumentEditorBankIndex();
                }
            }
        }
        if (!active)
        {
            s_edge = EdgeDrag::None;
            s_edge_split = -1;
        }
        ImGui::TextDisabled("Zones 0–127 · click to select · drag edges to set Low/High");
    }

    void DrawAdsrEnvelopeGraph(BAERmfEditorADSRInfo &adsr,
                               const char *id,
                               float height = 180.0f,
                               bool editable = false,
                               bool beatnik_level_scale = false,
                               bool allow_negative_levels = false)
    {
        const int beatnik_min =
            (beatnik_level_scale && allow_negative_levels) ? kAdsrBeatnikLevelMinSigned
                                                          : kAdsrBeatnikLevelMin;
        /* BE2-style envelope: Y in % (100=unity), X in ms with split timeline
         * (attack | gray sustain band | release axis resets to 0). */
        ImGui::PushID(id);
        const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##adsr_canvas",
                               ImVec2(width, height),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = canvas_pos;
        const ImVec2 p1(canvas_pos.x + width, canvas_pos.y + height);
        dl->AddRectFilled(p0, p1, IM_COL32(32, 34, 38, 255));
        dl->AddRect(p0, p1, IM_COL32(90, 90, 90, 255));

        const int stages = static_cast<int>(adsr.stageCount);
        if (stages <= 0)
        {
            dl->AddText(ImVec2(p0.x + 8.0f, p0.y + height * 0.5f - 6.0f),
                        IM_COL32(120, 120, 120, 255),
                        editable ? "No ADSR stages — use Add Stage" : "No ADSR stages");
            ImGui::PopID();
            return;
        }

        const float margin_l = 36.0f;
        const float margin_r = 8.0f;
        const float margin_t = 8.0f;
        const float margin_b = 22.0f;
        const float graph_l = p0.x + margin_l;
        const float graph_r = p1.x - margin_r;
        const float graph_t = p0.y + margin_t;
        const float graph_b = p1.y - margin_b;
        const float graph_w = graph_r - graph_l;
        const float graph_h = graph_b - graph_t;
        if (graph_w < 20.0f || graph_h < 20.0f)
        {
            ImGui::PopID();
            return;
        }

        int sust_i = -1;
        int max_disp_level = 0;
        int min_disp_level = 0;
        for (int i = 0; i < stages; ++i)
        {
            if (sust_i < 0 && AdsrIsSustainFlag(adsr.stages[i].flags))
            {
                sust_i = i;
            }
            if (!beatnik_level_scale)
            {
                const int disp = adsr.stages[i].level;
                max_disp_level = std::max(max_disp_level, disp);
                min_disp_level = std::min(min_disp_level, disp);
            }
        }
        if (beatnik_level_scale)
        {
            /* Fixed BE2 envelope range: 0%…200% (volume) or −200%…200% (LFO/filter). */
            max_disp_level = kAdsrBeatnikLevelMax;
            min_disp_level = beatnik_min;
        }
        else
        {
            max_disp_level = std::max(max_disp_level, 1);
            if (max_disp_level <= min_disp_level)
            {
                max_disp_level = min_disp_level + 1;
            }
        }
        const int level_span = max_disp_level - min_disp_level;

        auto disp_level_of = [&](int32_t raw) -> int {
            if (!beatnik_level_scale)
            {
                return raw;
            }
            return std::clamp(AdsrLevelToBeatnik(raw), beatnik_min, kAdsrBeatnikLevelMax);
        };
        auto level_to_y = [&](int disp) -> float {
            if (beatnik_level_scale)
            {
                disp = std::clamp(disp, beatnik_min, kAdsrBeatnikLevelMax);
            }
            const double norm =
                static_cast<double>(disp - min_disp_level) / static_cast<double>(level_span);
            return graph_b - static_cast<float>(norm * graph_h);
        };

        /* Layout widths: attack | sustain band | release (BE2 split timeline). */
        const bool split = (sust_i >= 0);
        const float left_w = split ? graph_w * 0.58f : graph_w;
        const float sust_w = split ? graph_w * 0.14f : 0.0f;
        const float right_w = split ? graph_w * 0.28f : 0.0f;
        const float left_l = graph_l;
        const float left_r = graph_l + left_w;
        const float sust_l = left_r;
        const float sust_r = left_r + sust_w;
        const float right_l = sust_r;
        const float right_r = graph_r;

        int64_t pre_ms = 0;
        int64_t post_ms = 0;
        for (int i = 0; i < stages; ++i)
        {
            if (i == sust_i)
            {
                continue;
            }
            const int64_t ms = std::llabs(static_cast<int64_t>(AdsrTimeUsToMs(adsr.stages[i].time)));
            if (split && i > sust_i)
            {
                post_ms += ms;
            }
            else if (!split || i < sust_i)
            {
                pre_ms += ms;
            }
        }
        if (pre_ms <= 0)
        {
            pre_ms = 1;
        }
        if (split && post_ms <= 0)
        {
            post_ms = 1;
        }

        /* Grid + axis labels */
        dl->AddLine(ImVec2(graph_l, graph_t), ImVec2(graph_l, graph_b), IM_COL32(70, 70, 70, 255));
        dl->AddLine(ImVec2(graph_l, graph_b), ImVec2(graph_r, graph_b), IM_COL32(70, 70, 70, 255));
        if (beatnik_level_scale)
        {
            for (int pct : {-200, 0, 100, 200})
            {
                if (pct < min_disp_level || pct > max_disp_level)
                {
                    continue;
                }
                const float y = level_to_y(pct);
                const ImU32 col = (pct == 100 || pct == 0) ? IM_COL32(110, 110, 110, 255)
                                                           : IM_COL32(55, 55, 55, 255);
                dl->AddLine(ImVec2(graph_l, y), ImVec2(graph_r, y), col);
                char lab[16];
                std::snprintf(lab, sizeof(lab), "%d%%", pct);
                dl->AddText(ImVec2(p0.x + 4.0f, y - 7.0f), IM_COL32(160, 160, 160, 255), lab);
            }
        }
        else
        {
            char lo[32], hi[32];
            std::snprintf(hi, sizeof(hi), "%d", max_disp_level);
            std::snprintf(lo, sizeof(lo), "%d", min_disp_level);
            dl->AddText(ImVec2(p0.x + 4.0f, graph_t - 2.0f), IM_COL32(160, 160, 160, 255), hi);
            dl->AddText(ImVec2(p0.x + 4.0f, graph_b - 8.0f), IM_COL32(160, 160, 160, 255), lo);
            if (min_disp_level < 0 && max_disp_level > 0)
            {
                dl->AddLine(ImVec2(graph_l, level_to_y(0)),
                            ImVec2(graph_r, level_to_y(0)),
                            IM_COL32(70, 70, 70, 255));
            }
        }

        auto draw_ms_ticks = [&](float x0, float x1, int64_t total_ms, bool) {
            if (total_ms <= 0 || x1 <= x0)
            {
                return;
            }
            int64_t step = 200;
            if (total_ms > 2000)
            {
                step = 500;
            }
            if (total_ms > 5000)
            {
                step = 1000;
            }
            for (int64_t t = 0; t <= total_ms; t += step)
            {
                const float x = x0 + static_cast<float>(static_cast<double>(t) / total_ms * (x1 - x0));
                dl->AddLine(ImVec2(x, graph_b), ImVec2(x, graph_b + 4.0f), IM_COL32(90, 90, 90, 255));
                char lab[24];
                std::snprintf(lab, sizeof(lab), "%lld ms", static_cast<long long>(t));
                dl->AddText(ImVec2(x - 10.0f, graph_b + 5.0f), IM_COL32(140, 140, 140, 255), lab);
            }
        };
        draw_ms_ticks(left_l, left_r, pre_ms, true);
        if (split)
        {
            draw_ms_ticks(right_l, right_r, post_ms, true);
        }

        if (split)
        {
            dl->AddRectFilled(ImVec2(sust_l, graph_t),
                              ImVec2(sust_r, graph_b),
                              IM_COL32(70, 70, 70, 70));
            dl->AddLine(ImVec2(sust_l, graph_t), ImVec2(sust_l, graph_b), IM_COL32(100, 100, 100, 180));
            dl->AddLine(ImVec2(sust_r, graph_t), ImVec2(sust_r, graph_b), IM_COL32(100, 100, 100, 180));
        }

        enum class AdsrDragKind : int
        {
            None = 0,
            Level,
            TimeEdge,
        };
        struct StageGeom
        {
            float x1 = 0.0f;
            float x2 = 0.0f;
            float y2 = 0.0f;
            bool in_release = false;
        };
        std::vector<StageGeom> geoms(static_cast<size_t>(stages));
        std::vector<ImVec2> polyline;
        polyline.reserve(static_cast<size_t>(stages) + 2);
        polyline.push_back(ImVec2(left_l, level_to_y(0)));

        int64_t cum_pre = 0;
        int64_t cum_post = 0;
        int prev_disp = 0;
        for (int i = 0; i < stages; ++i)
        {
            const int disp = disp_level_of(adsr.stages[i].level);
            const float y2 = level_to_y(disp);
            float x1 = 0.0f;
            float x2 = 0.0f;
            bool in_release = false;
            if (i == sust_i)
            {
                x1 = sust_l;
                x2 = sust_r;
                geoms[static_cast<size_t>(i)] = {x1, x2, y2, false};
                /* Enter sustain: vertical then hold across band */
                polyline.push_back(ImVec2(x1, level_to_y(prev_disp)));
                polyline.push_back(ImVec2(x1, y2));
                polyline.push_back(ImVec2(x2, y2));
            }
            else if (split && i > sust_i)
            {
                in_release = true;
                const int64_t ms =
                    std::max<int64_t>(0, std::llabs(static_cast<int64_t>(AdsrTimeUsToMs(adsr.stages[i].time))));
                x1 = right_l + static_cast<float>(static_cast<double>(cum_post) / post_ms * right_w);
                cum_post += ms;
                x2 = right_l + static_cast<float>(static_cast<double>(cum_post) / post_ms * right_w);
                if (ms == 0)
                {
                    x2 = std::max(x2, x1 + 1.0f);
                }
                geoms[static_cast<size_t>(i)] = {x1, x2, y2, true};
                polyline.push_back(ImVec2(x2, y2));
            }
            else
            {
                const int64_t ms =
                    std::max<int64_t>(0, std::llabs(static_cast<int64_t>(AdsrTimeUsToMs(adsr.stages[i].time))));
                x1 = left_l + static_cast<float>(static_cast<double>(cum_pre) / pre_ms * left_w);
                cum_pre += ms;
                x2 = left_l + static_cast<float>(static_cast<double>(cum_pre) / pre_ms * left_w);
                if (ms == 0)
                {
                    x2 = std::max(x2, x1 + 1.0f);
                }
                geoms[static_cast<size_t>(i)] = {x1, x2, y2, false};
                polyline.push_back(ImVec2(x2, y2));
            }
            prev_disp = disp;
        }

        /* Curve */
        const ImU32 curve_col = IM_COL32(220, 220, 220, 255);
        for (size_t i = 1; i < polyline.size(); ++i)
        {
            dl->AddLine(polyline[i - 1], polyline[i], curve_col, 1.75f);
        }
        for (int i = 0; i < stages; ++i)
        {
            if (i == sust_i)
            {
                continue;
            }
            const StageGeom &g = geoms[static_cast<size_t>(i)];
            const bool selected = (editable && m_ie_adsr_selected_stage == i &&
                                   m_ie_adsr_selected_owner == ImGui::GetID("adsr_select"));
            const float hx = g.x2;
            const float hy = g.y2;
            if (editable)
            {
                dl->AddRectFilled(ImVec2(hx - 4.0f, hy - 4.0f),
                                  ImVec2(hx + 4.0f, hy + 4.0f),
                                  selected ? IM_COL32(255, 220, 80, 255) : IM_COL32(240, 240, 240, 255));
                dl->AddRect(ImVec2(hx - 4.0f, hy - 4.0f),
                            ImVec2(hx + 4.0f, hy + 4.0f),
                            IM_COL32(20, 20, 20, 220));
            }
            else
            {
                dl->AddRectFilled(ImVec2(hx - 3.0f, hy - 3.0f),
                                  ImVec2(hx + 3.0f, hy + 3.0f),
                                  IM_COL32(240, 240, 240, 255));
            }
        }

        if (editable)
        {
            static int s_drag_stage = -1;
            static ImGuiID s_drag_owner = 0;
            static AdsrDragKind s_drag_kind = AdsrDragKind::None;
            static float s_drag_start_x = 0.0f;
            static float s_drag_start_y = 0.0f;
            static int32_t s_drag_start_level = 0;
            static int32_t s_drag_start_time = 0;
            static bool s_drag_mutated = false;
            const ImGuiID owner = ImGui::GetID("adsr_drag");
            const ImGuiID select_owner = ImGui::GetID("adsr_select");
            const ImVec2 mouse = ImGui::GetIO().MousePos;

            auto hit_handle = [&](int i) -> bool {
                if (i == sust_i)
                {
                    return false;
                }
                const float dx = mouse.x - geoms[static_cast<size_t>(i)].x2;
                const float dy = mouse.y - geoms[static_cast<size_t>(i)].y2;
                return (dx * dx + dy * dy) <= 10.0f * 10.0f;
            };
            auto hit_band = [&](int i) -> bool {
                const StageGeom &g = geoms[static_cast<size_t>(i)];
                return mouse.x >= g.x1 && mouse.x <= std::max(g.x1 + 2.0f, g.x2) &&
                       mouse.y >= graph_t && mouse.y <= graph_b;
            };

            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                for (int i = stages - 1; i >= 0; --i)
                {
                    if (hit_band(i) || hit_handle(i))
                    {
                        m_ie_adsr_selected_stage = i;
                        m_ie_adsr_selected_owner = select_owner;
                        MarkInstrumentEditorDirty(false);
                        adsr.stages[i].flags = AdsrCycleStageFlags(adsr.stages[i].flags);
                        LivePatchInstrumentEditor();
                        break;
                    }
                }
            }
            else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                int hit = -1;
                for (int i = stages - 1; i >= 0; --i)
                {
                    if (hit_handle(i))
                    {
                        hit = i;
                        break;
                    }
                }
                if (hit < 0)
                {
                    for (int i = stages - 1; i >= 0; --i)
                    {
                        if (hit_band(i))
                        {
                            hit = i;
                            break;
                        }
                    }
                }
                if (hit >= 0)
                {
                    m_ie_adsr_selected_stage = hit;
                    m_ie_adsr_selected_owner = select_owner;
                    s_drag_stage = hit;
                    s_drag_owner = owner;
                    s_drag_kind = (hit == sust_i) ? AdsrDragKind::Level : AdsrDragKind::Level;
                    s_drag_start_x = mouse.x;
                    s_drag_start_y = mouse.y;
                    s_drag_start_level = adsr.stages[hit].level;
                    s_drag_start_time = adsr.stages[hit].time;
                    s_drag_mutated = false;
                }
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                for (int i = stages - 1; i >= 0; --i)
                {
                    if (hit_band(i) || hit_handle(i))
                    {
                        m_ie_adsr_selected_stage = i;
                        m_ie_adsr_selected_owner = select_owner;
                        ImGui::OpenPopup("##adsr_stage_type");
                        break;
                    }
                }
            }

            if (ImGui::BeginPopup("##adsr_stage_type"))
            {
                static const struct
                {
                    int32_t flags;
                    const char *label;
                } kPopupTypes[] = {
                    {static_cast<int32_t>(FOUR_CHAR('L', 'I', 'N', 'E')), "LINE"},
                    {static_cast<int32_t>(FOUR_CHAR('S', 'U', 'S', 'T')), "SUST"},
                    {static_cast<int32_t>(FOUR_CHAR('R', 'E', 'L', 'S')), "RELS"},
                    {static_cast<int32_t>(FOUR_CHAR('L', 'A', 'S', 'T')), "LAST"},
                    {0, "OFF"},
                    {static_cast<int32_t>(FOUR_CHAR('G', 'O', 'T', 'O')), "GOTO"},
                    {static_cast<int32_t>(FOUR_CHAR('G', 'O', 'S', 'T')), "GOTO_COND"},
                };
                if (m_ie_adsr_selected_stage >= 0 && m_ie_adsr_selected_stage < stages &&
                    m_ie_adsr_selected_owner == select_owner)
                {
                    const int si = m_ie_adsr_selected_stage;
                    ImGui::Text("Stage %d type", si);
                    ImGui::Separator();
                    for (const auto &choice : kPopupTypes)
                    {
                        const bool selected = (adsr.stages[si].flags == choice.flags);
                        if (ImGui::MenuItem(choice.label, nullptr, selected))
                        {
                            MarkInstrumentEditorDirty(false);
                            adsr.stages[si].flags = choice.flags;
                            LivePatchInstrumentEditor();
                        }
                    }
                }
                ImGui::EndPopup();
            }

            if (s_drag_owner == owner && s_drag_stage >= 0 && s_drag_stage < stages &&
                (active || ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            {
                const int i = s_drag_stage;
                bool changed = false;
                const float dy = mouse.y - s_drag_start_y;
                const int disp_delta = static_cast<int>(
                    std::lround((-dy / std::max(1.0f, graph_h)) * static_cast<float>(level_span)));
                if (beatnik_level_scale)
                {
                    const int start_disp = AdsrLevelToBeatnik(s_drag_start_level);
                    const int new_disp =
                        std::clamp(start_disp + disp_delta, beatnik_min, kAdsrBeatnikLevelMax);
                    const int32_t new_raw = AdsrLevelFromBeatnik(new_disp, beatnik_min);
                    if (new_raw != adsr.stages[i].level)
                    {
                        adsr.stages[i].level = new_raw;
                        changed = true;
                    }
                }
                else
                {
                    const int32_t new_level = s_drag_start_level + disp_delta;
                    if (new_level != adsr.stages[i].level)
                    {
                        adsr.stages[i].level = new_level;
                        changed = true;
                    }
                }
                /* Horizontal drag adjusts time in ms (not for SUST — display-only band). */
                if (i != sust_i && std::fabs(mouse.x - s_drag_start_x) >= 6.0f)
                {
                    const StageGeom &g = geoms[static_cast<size_t>(i)];
                    const float region_w = g.in_release ? right_w : left_w;
                    const int64_t region_ms = g.in_release ? post_ms : pre_ms;
                    if (region_w > 1.0f)
                    {
                        const float dx = mouse.x - s_drag_start_x;
                        const int64_t delta_ms = static_cast<int64_t>(
                            std::llround((dx / region_w) * static_cast<double>(region_ms)));
                        const int start_ms = AdsrTimeUsToMs(s_drag_start_time);
                        const int new_ms = std::clamp(start_ms + static_cast<int>(delta_ms),
                                                     0,
                                                     kAdsrTimeMsMax);
                        const int32_t new_us = AdsrTimeMsToUs(new_ms);
                        if (new_us != adsr.stages[i].time)
                        {
                            adsr.stages[i].time = new_us;
                            changed = true;
                        }
                    }
                }
                if (changed)
                {
                    if (!s_drag_mutated)
                    {
                        InstrumentEditorPushUndoFromPreEdit();
                        s_drag_mutated = true;
                    }
                    m_ie_dirty = true;
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s_drag_owner == owner)
            {
                if (s_drag_mutated)
                {
                    LivePatchInstrumentEditor();
                }
                s_drag_stage = -1;
                s_drag_owner = 0;
                s_drag_kind = AdsrDragKind::None;
                s_drag_mutated = false;
            }

            ImGui::TextDisabled(
                beatnik_level_scale
                    ? "BE2 view · Y: %% (100=unity) · X: ms (sustain band) · drag node · "
                      "right-click type"
                    : "BE2 view · X: ms (sustain band) · drag node · right-click type");
        }
        ImGui::PopID();
    }

    void DrawLfoWaveformGraph(const BAERmfEditorLFOInfo &lfo,
                              const char *id,
                              float height = 72.0f,
                              bool beatnik_level_scale = false)
    {
        ImGui::PushID(id);
        const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##lfo_wave", ImVec2(width, height));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = canvas_pos;
        const ImVec2 p1(canvas_pos.x + width, canvas_pos.y + height);
        dl->AddRectFilled(p0, p1, IM_COL32(22, 22, 22, 255));
        dl->AddRect(p0, p1, IM_COL32(70, 70, 70, 255));

        const float margin = 6.0f;
        const float left = p0.x + margin;
        const float right = p1.x - margin;
        const float top = p0.y + margin;
        const float bottom = p1.y - margin;
        const float w = right - left;
        const float h = bottom - top;
        const float center_y = top + h * 0.5f;
        dl->AddLine(ImVec2(left, center_y), ImVec2(right, center_y), IM_COL32(55, 55, 55, 255));

        auto evaluate = [](int32_t shape, double phase) -> double {
            phase -= std::floor(phase);
            if (phase < 0.0)
            {
                phase += 1.0;
            }
            switch (shape)
            {
            case static_cast<int32_t>(FOUR_CHAR('T', 'R', 'I', 'A')):
                return -(4.0 * std::fabs(phase - 0.5) - 1.0);
            case static_cast<int32_t>(FOUR_CHAR('S', 'Q', 'U', 'A')):
                return (phase < 0.5) ? 1.0 : -1.0;
            case static_cast<int32_t>(FOUR_CHAR('S', 'Q', 'U', '2')):
                return (phase < 0.25) ? 1.0 : -1.0;
            case static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', 'T')):
                return (2.0 * phase) - 1.0;
            case static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', '2')):
                return 1.0 - (2.0 * phase);
            case static_cast<int32_t>(FOUR_CHAR('S', 'I', 'N', 'E')):
            default:
                return std::sin(phase * 2.0 * 3.14159265358979323846);
            }
        };

        if (lfo.period == 0 && lfo.level == 0 && lfo.DC_feed == 0)
        {
            dl->AddText(ImVec2(left + 8.0f, center_y - 6.0f), IM_COL32(120, 120, 120, 255), "LFO preview");
            ImGui::PopID();
            return;
        }

        const int64_t span =
            std::max<int64_t>(1, std::llabs(static_cast<int64_t>(lfo.level)) +
                                     std::llabs(static_cast<int64_t>(lfo.DC_feed)));
        ImVec2 prev;
        bool has_prev = false;
        const int pixels = std::max(2, static_cast<int>(w));
        for (int x = 0; x < pixels; ++x)
        {
            const double phase = (pixels > 1) ? (static_cast<double>(x) / static_cast<double>(pixels - 1)) : 0.0;
            const double wave = evaluate(lfo.waveShape, phase);
            const double value = wave * static_cast<double>(lfo.level) + static_cast<double>(lfo.DC_feed);
            const double normalized = std::clamp(value / static_cast<double>(span), -1.0, 1.0);
            const float y = center_y - static_cast<float>(normalized * (h * 0.5f - 2.0f));
            const ImVec2 pt(left + static_cast<float>(x), y);
            if (has_prev)
            {
                dl->AddLine(prev, pt, IM_COL32(98, 215, 255, 255), 2.0f);
            }
            prev = pt;
            has_prev = true;
        }
        char meta[96];
        const int period_ms = (lfo.period <= 0) ? 0 : AdsrTimeUsToMs(lfo.period);
        if (beatnik_level_scale)
        {
            std::snprintf(meta,
                          sizeof(meta),
                          "period=%dms  level=%d  dc=%d",
                          period_ms,
                          AdsrLevelToBeatnik(lfo.level),
                          AdsrLevelToBeatnik(lfo.DC_feed));
        }
        else
        {
            std::snprintf(meta,
                          sizeof(meta),
                          "period=%dms  level=%d  dc=%d",
                          period_ms,
                          lfo.level,
                          lfo.DC_feed);
        }
        dl->AddText(ImVec2(left + 6.0f, top + 2.0f), IM_COL32(150, 150, 150, 255), meta);
        ImGui::PopID();
    }

    void DrawAdsrStageEditor(BAERmfEditorADSRInfo &adsr,
                             const char *id,
                             bool beatnik_level_scale = false,
                             bool allow_negative_levels = false)
    {
        ImGui::PushID(id);
        m_ie_adsr_default_focus_id = id ? id : "";
        const int beatnik_min =
            (beatnik_level_scale && allow_negative_levels) ? kAdsrBeatnikLevelMinSigned
                                                          : kAdsrBeatnikLevelMin;
        DrawAdsrEnvelopeGraph(
            adsr, "graph", 188.0f, true, beatnik_level_scale, allow_negative_levels);
        int stages = static_cast<int>(adsr.stageCount);
        auto init_stage = [&](int index) {
            if (index < 0 || index >= BAE_EDITOR_MAX_ADSR_STAGES)
            {
                return;
            }
            const int32_t prev_level =
                (index > 0) ? adsr.stages[index - 1].level : 0;
            adsr.stages[index].level = prev_level;
            adsr.stages[index].time = 0;
            adsr.stages[index].flags = static_cast<int32_t>(FOUR_CHAR('L', 'I', 'N', 'E'));
        };
        if (ImGui::Button("Add Stage") && stages < BAE_EDITOR_MAX_ADSR_STAGES)
        {
            MarkInstrumentEditorDirty(false);
            init_stage(stages);
            adsr.stageCount = static_cast<uint32_t>(stages + 1);
            stages = static_cast<int>(adsr.stageCount);
            LivePatchInstrumentEditor();
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Last") && stages > 0)
        {
            MarkInstrumentEditorDirty(false);
            adsr.stageCount = static_cast<uint32_t>(stages - 1);
            stages = static_cast<int>(adsr.stageCount);
            LivePatchInstrumentEditor();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##stage_count", &stages))
        {
            stages = std::clamp(stages, 0, BAE_EDITOR_MAX_ADSR_STAGES);
            MarkInstrumentEditorDirty(false);
            if (stages > static_cast<int>(adsr.stageCount))
            {
                for (int i = static_cast<int>(adsr.stageCount); i < stages; ++i)
                {
                    init_stage(i);
                }
            }
            adsr.stageCount = static_cast<uint32_t>(stages);
            LivePatchInstrumentEditor();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("stages");
        ImGui::SameLine();
        if (ImGui::Button("Use As Default"))
        {
            m_pref_adsr_default = adsr;
            m_pref_adsr_default_valid = true;
            SaveEditorPrefsToDisk();
            SetStatus("Saved ADSR as editor default");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_pref_adsr_default_valid);
        if (ImGui::Button("Paste Default"))
        {
            MarkInstrumentEditorDirty(false);
            adsr = m_pref_adsr_default;
            stages = static_cast<int>(adsr.stageCount);
            LivePatchInstrumentEditor();
            SetStatus("Pasted default ADSR");
        }
        ImGui::EndDisabled();

        static const struct
        {
            int32_t flags;
            const char *label;
        } kFlagChoices[] = {
            {0, "OFF"},
            {static_cast<int32_t>(FOUR_CHAR('L', 'I', 'N', 'E')), "LINE"},
            {static_cast<int32_t>(FOUR_CHAR('S', 'U', 'S', 'T')), "SUST"},
            {static_cast<int32_t>(FOUR_CHAR('R', 'E', 'L', 'S')), "RELS"},
            {static_cast<int32_t>(FOUR_CHAR('L', 'A', 'S', 'T')), "LAST"},
            {static_cast<int32_t>(FOUR_CHAR('G', 'O', 'T', 'O')), "GOTO"},
            {static_cast<int32_t>(FOUR_CHAR('G', 'O', 'S', 'T')), "GOTO_COND"},
        };
        auto is_goto_stage = [](int32_t flags) {
            return flags == static_cast<int32_t>(FOUR_CHAR('G', 'O', 'T', 'O')) ||
                   flags == static_cast<int32_t>(FOUR_CHAR('G', 'O', 'S', 'T'));
        };

        /* BE2-style compact LEVEL / TIME rows — wrap into groups so 32 stages fit. */
        if (stages > 0)
        {
            constexpr float kLabelCol = 72.0f;
            constexpr float kMinColW = 56.0f;
            const float avail = std::max(120.0f, ImGui::GetContentRegionAvail().x - kLabelCol);
            int per_row = static_cast<int>(avail / kMinColW);
            per_row = std::clamp(per_row, 4, 8); /* 4–8 cols/group; 32 stages → ≥4 groups */
            const int group_count = (stages + per_row - 1) / per_row;

            ImGui::Spacing();
            for (int g = 0; g < group_count; ++g)
            {
                const int begin = g * per_row;
                const int end = std::min(stages, begin + per_row);
                const int count = end - begin;
                const float col_w = std::max(kMinColW, avail / static_cast<float>(count));

                if (group_count > 1)
                {
                    char group_title[48];
                    std::snprintf(group_title,
                                  sizeof(group_title),
                                  "Stages %d–%d",
                                  begin,
                                  end - 1);
                    ImGui::SeparatorText(group_title);
                }

                ImGui::PushID(5000 + g);

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(beatnik_level_scale ? "LEVEL (%)" : "LEVEL");
                ImGui::SameLine(kLabelCol);
                for (int i = begin; i < end; ++i)
                {
                    ImGui::PushID(1000 + i);
                    ImGui::SetNextItemWidth(col_w - 4.0f);
                    if (is_goto_stage(adsr.stages[i].flags))
                    {
                        int target = adsr.stages[i].level;
                        if (target < 1)
                        {
                            target = 1;
                        }
                        if (ImGui::InputInt("##lvl", &target, 0, 0))
                        {
                            MarkInstrumentEditorDirty(false);
                            adsr.stages[i].level = std::clamp(target, 1, std::max(1, stages));
                            LivePatchInstrumentEditor();
                        }
                    }
                    else if (beatnik_level_scale)
                    {
                        int level = AdsrLevelToBeatnik(adsr.stages[i].level);
                        if (ImGui::InputInt("##lvl", &level, 0, 0))
                        {
                            MarkInstrumentEditorDirty(false);
                            level = std::clamp(level, beatnik_min, kAdsrBeatnikLevelMax);
                            adsr.stages[i].level = AdsrLevelFromBeatnik(level, beatnik_min);
                            LivePatchInstrumentEditor();
                        }
                    }
                    else
                    {
                        int level = adsr.stages[i].level;
                        if (ImGui::InputInt("##lvl", &level, 0, 0))
                        {
                            MarkInstrumentEditorDirty(false);
                            adsr.stages[i].level = level;
                            LivePatchInstrumentEditor();
                        }
                    }
                    if (i + 1 < end)
                    {
                        ImGui::SameLine(0.0f, 4.0f);
                    }
                    ImGui::PopID();
                }

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("TIME (MS)");
                ImGui::SameLine(kLabelCol);
                for (int i = begin; i < end; ++i)
                {
                    ImGui::PushID(2000 + i);
                    ImGui::SetNextItemWidth(col_w - 4.0f);
                    int ms = AdsrTimeToDisplayMs(adsr.stages[i].flags, adsr.stages[i].time);
                    if (ImGui::InputInt("##tm", &ms, 0, 0))
                    {
                        MarkInstrumentEditorDirty(false);
                        adsr.stages[i].time =
                            AdsrTimeFromDisplayMs(adsr.stages[i].flags, ms);
                        LivePatchInstrumentEditor();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) &&
                        AdsrIsSustainFlag(adsr.stages[i].flags) && adsr.stages[i].time == 0)
                    {
                        ImGui::SetTooltip(
                            "BE2 shows %d ms for SUST when stored time is 0",
                            kBe2SustainDisplayMs);
                    }
                    if (i + 1 < end)
                    {
                        ImGui::SameLine(0.0f, 4.0f);
                    }
                    ImGui::PopID();
                }

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("TYPE");
                ImGui::SameLine(kLabelCol);
                for (int i = begin; i < end; ++i)
                {
                    ImGui::PushID(3000 + i);
                    ImGui::SetNextItemWidth(col_w - 4.0f);
                    const char *preview = AdsrStageFlagName(adsr.stages[i].flags);
                    if (ImGui::BeginCombo("##ty", preview))
                    {
                        for (const auto &choice : kFlagChoices)
                        {
                            const bool selected = (adsr.stages[i].flags == choice.flags);
                            if (ImGui::Selectable(choice.label, selected))
                            {
                                MarkInstrumentEditorDirty(false);
                                const bool became_goto = is_goto_stage(choice.flags);
                                const bool was_goto = is_goto_stage(adsr.stages[i].flags);
                                adsr.stages[i].flags = choice.flags;
                                if (became_goto && !was_goto)
                                {
                                    if (adsr.stages[i].level < 1 ||
                                        adsr.stages[i].level > stages)
                                    {
                                        adsr.stages[i].level = 1;
                                    }
                                }
                                LivePatchInstrumentEditor();
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (i + 1 < end)
                    {
                        ImGui::SameLine(0.0f, 4.0f);
                    }
                    ImGui::PopID();
                }

                ImGui::PopID();
            }
        }

        if (ImGui::CollapsingHeader("Advanced (raw µs / engine levels)"))
        {
            if (beatnik_level_scale)
            {
                ImGui::TextDisabled("Levels still Beatnik %% here; times are raw microseconds");
            }
            if (ImGui::BeginTable("adsr_adv", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn(beatnik_level_scale ? "Level %" : "Level");
                ImGui::TableSetupColumn("Time (µs)");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("#");
                ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableHeadersRow();
                for (int i = 0; i < stages; ++i)
                {
                    ImGui::TableNextRow();
                    ImGui::PushID(4000 + i);
                    ImGui::TableSetColumnIndex(0);
                    if (is_goto_stage(adsr.stages[i].flags))
                    {
                        int target = adsr.stages[i].level;
                        if (ImGui::InputInt("##goto_tgt", &target))
                        {
                            MarkInstrumentEditorDirty(false);
                            adsr.stages[i].level = std::clamp(target, 1, std::max(1, stages));
                            LivePatchInstrumentEditor();
                        }
                    }
                    else if (beatnik_level_scale)
                    {
                        int level = AdsrLevelToBeatnik(adsr.stages[i].level);
                        if (ImGui::InputInt("##lvl", &level))
                        {
                            MarkInstrumentEditorDirty(false);
                            level = std::clamp(level, beatnik_min, kAdsrBeatnikLevelMax);
                            adsr.stages[i].level = AdsrLevelFromBeatnik(level, beatnik_min);
                            LivePatchInstrumentEditor();
                        }
                    }
                    else
                    {
                        int level = adsr.stages[i].level;
                        if (ImGui::InputInt("##lvl", &level))
                        {
                            MarkInstrumentEditorDirty(false);
                            adsr.stages[i].level = level;
                            LivePatchInstrumentEditor();
                        }
                    }
                    ImGui::TableSetColumnIndex(1);
                    int time_us = adsr.stages[i].time;
                    if (ImGui::InputInt("##tm", &time_us))
                    {
                        MarkInstrumentEditorDirty(false);
                        adsr.stages[i].time = std::max(0, time_us);
                        LivePatchInstrumentEditor();
                    }
                    ImGui::TableSetColumnIndex(2);
                    const char *preview = AdsrStageFlagName(adsr.stages[i].flags);
                    if (ImGui::BeginCombo("##fl", preview))
                    {
                        for (const auto &choice : kFlagChoices)
                        {
                            const bool selected = (adsr.stages[i].flags == choice.flags);
                            if (ImGui::Selectable(choice.label, selected))
                            {
                                MarkInstrumentEditorDirty(false);
                                adsr.stages[i].flags = choice.flags;
                                LivePatchInstrumentEditor();
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", i);
                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::SmallButton("Del"))
                    {
                        MarkInstrumentEditorDirty(false);
                        for (int j = i; j < stages - 1; ++j)
                        {
                            adsr.stages[j] = adsr.stages[j + 1];
                        }
                        adsr.stageCount = static_cast<uint32_t>(stages - 1);
                        stages = static_cast<int>(adsr.stageCount);
                        LivePatchInstrumentEditor();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }

    void CollectBankSndChoices(std::vector<std::pair<uint32_t, std::string>> &out) const
    {
        out.clear();
        std::set<uint32_t> seen;
        for (const SampleRow &sample : m_samples)
        {
            /* ID 0 is valid (BSN/session banks often use it); only skip unset 0xFFFF. */
            if (sample.source != SampleSource::Bank || sample.snd_resource_id == 0xFFFFu)
            {
                continue;
            }
            if (!seen.insert(sample.snd_resource_id).second)
            {
                continue;
            }
            char label[320];
            const char *nm = sample.name.empty() ? "Sample" : sample.name.c_str();
            if (sample.unassigned)
            {
                if (sample.codec_label.empty())
                {
                    std::snprintf(label,
                                  sizeof(label),
                                  "SND %u  %s",
                                  sample.snd_resource_id,
                                  nm);
                }
                else
                {
                    std::snprintf(label,
                                  sizeof(label),
                                  "SND %u  %s [%s]",
                                  sample.snd_resource_id,
                                  nm,
                                  sample.codec_label.c_str());
                }
            }
            else if (sample.codec_label.empty())
            {
                std::snprintf(label,
                              sizeof(label),
                              "SND %u  %s (B%dP%03d)",
                              sample.snd_resource_id,
                              nm,
                              sample.bank,
                              sample.program);
            }
            else
            {
                std::snprintf(label,
                              sizeof(label),
                              "SND %u  %s (B%dP%03d) [%s]",
                              sample.snd_resource_id,
                              nm,
                              sample.bank,
                              sample.program,
                              sample.codec_label.c_str());
            }
            out.push_back({sample.snd_resource_id, label});
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    }

    void OpenInstrumentEditorForSelection()
    {
        if (m_selected_instrument < 0 || m_selected_instrument >= static_cast<int>(m_instruments.size()))
        {
            return;
        }
        const InstrumentRow &inst = m_instruments[static_cast<size_t>(m_selected_instrument)];
        m_ie_inst_id = inst.inst_id;
        m_ie_instrument_index = inst.instrument_index;
        /* Document path only when the INST truly lives in the song document. */
        m_ie_from_song = inst.from_song_document;
        m_ie_bank = inst.bank;
        m_ie_program = inst.program;
        std::memset(&m_ie_ext, 0, sizeof(m_ie_ext));
        BAEResult r = BAE_PARAM_ERR;
        bool loaded_from_doc = false;
        /* INST id 0 is valid (e.g. B0P000) — do not treat 0 as unset. */
        if (inst.from_song_document && m_document)
        {
            r = BAERmfEditorDocument_GetInstrumentExtInfo(m_document, m_ie_inst_id, &m_ie_ext);
            loaded_from_doc = (r == BAE_NO_ERROR);
            /* Silent defaults (no real ADSR/LFO) → fall back to bank. */
            if (loaded_from_doc && !m_ie_ext.hasExtendedData && m_ie_ext.lfoCount == 0 &&
                m_ie_ext.volumeADSR.stageCount <= 1 && m_bank_token)
            {
                loaded_from_doc = false;
                r = BAE_PARAM_ERR;
            }
        }
        if (!loaded_from_doc && m_bank_token)
        {
            m_ie_from_song = false;
            /* Resolve by INST id — flat promote after Apply can shift indexes. */
            {
                uint32_t resolved = 0;
                uint32_t index = m_ie_instrument_index;
                if (BAERmfEditorBank_ResolveInstID(m_bank_token, m_ie_inst_id, &resolved, &index) ==
                    BAE_NO_ERROR)
                {
                    m_ie_instrument_index = index;
                }
            }
            r = BAERmfEditorBank_GetInstrumentExtInfo(m_bank_token, m_ie_instrument_index, &m_ie_ext);
            if (r == BAE_NO_ERROR)
            {
                /* Prefer bank Ext instID; 0 is valid — do not fall through on falsy. */
                m_ie_inst_id = m_ie_ext.instID;
            }
        }
        if (r != BAE_NO_ERROR)
        {
            SetStatus(std::string("Instrument Ext load failed: ") + FormatBAEError(r));
            return;
        }
        m_ie_ext.flags1 = static_cast<unsigned char>(m_ie_ext.flags1 &
                                                     static_cast<unsigned char>(~ZBF_ghostInstrument));
        /* Own the title — bank API displayName is only valid until the Get returns. */
        if (m_ie_ext.displayName && m_ie_ext.displayName[0])
        {
            m_ie_display_name = m_ie_ext.displayName;
        }
        else if (!inst.name.empty())
        {
            m_ie_display_name = inst.name;
        }
        else
        {
            m_ie_display_name.clear();
        }
        m_ie_ext.displayName = m_ie_display_name.empty() ? nullptr : m_ie_display_name.c_str();
        m_ie_dirty = false;
        m_ie_live_osc_persisted = (m_ie_ext.useOscillator != FALSE);
        m_ie_page = 0;
        m_ie_selected_split = -1;
        m_ie_ext_pristine = m_ie_ext;
        m_ie_ext_pre_edit = m_ie_ext;
        m_ie_undo.clear();
        m_ie_redo.clear();
        m_ie_undo_pushed_for_gesture = false;
        m_ie_adsr_selected_stage = -1;
        m_ie_adsr_selected_owner = 0;
        SyncLfoControlModesFromExt();
        m_ie_sample_splits_backup.clear();
        CaptureInstrumentEditorPristineSplits();
        m_instrument_editor_open = true;
        LivePatchInstrumentEditor(true);
        SetStatus(std::string("Editing instrument ") +
                  (m_ie_display_name.empty() ? inst.name : m_ie_display_name));
    }

    void BindInstrumentEditorDisplayName()
    {
        m_ie_ext.displayName = m_ie_display_name.empty() ? nullptr : m_ie_display_name.c_str();
    }

    static bool IeSplitsHaveSample(const std::vector<IePristineSplit> &splits)
    {
        for (const IePristineSplit &snap : splits)
        {
            if (static_cast<uint32_t>(snap.sndResourceID) != kNoSndResourceId)
            {
                return true;
            }
        }
        return false;
    }

    void CaptureInstrumentEditorSplitsFromBank(std::vector<IePristineSplit> &out)
    {
        out.clear();
        if (!m_bank_token || m_ie_from_song)
        {
            return;
        }
        uint32_t split_count = 0;
        if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token,
                                                      m_ie_instrument_index,
                                                      &split_count) != BAE_NO_ERROR)
        {
            return;
        }
        out.reserve(split_count);
        for (uint32_t s = 0; s < split_count; ++s)
        {
            IePristineSplit snap;
            BAERmfEditorBankSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                         m_ie_instrument_index,
                                                         s,
                                                         &info) != BAE_NO_ERROR)
            {
                snap.sndResourceID = static_cast<uint16_t>(kNoSndResourceId);
                out.push_back(snap);
                continue;
            }
            snap.lowKey = info.lowKey;
            snap.highKey = info.highKey;
            snap.rootKey = info.rootKey;
            snap.splitVolume = info.splitVolume;
            snap.sndResourceID = static_cast<uint16_t>(info.sndResourceID);
            out.push_back(snap);
        }
    }

    void CaptureInstrumentEditorPristineSplits()
    {
        CaptureInstrumentEditorSplitsFromBank(m_ie_pristine_splits);
        /* Refresh sample backup when the bank still has real SND links.
         * Do not clear the backup here — Apply+generator captures SND=none into
         * pristine afterward and must keep the pre-clear keymap. */
        if (IeSplitsHaveSample(m_ie_pristine_splits))
        {
            m_ie_sample_splits_backup = m_ie_pristine_splits;
        }
    }

    /* Snapshot current bank keymap into the sample backup if it still has SNDs
     * (call before clearing links for oscillator mode). */
    void CaptureInstrumentEditorSampleBackupIfPresent()
    {
        std::vector<IePristineSplit> cur;
        CaptureInstrumentEditorSplitsFromBank(cur);
        if (IeSplitsHaveSample(cur))
        {
            m_ie_sample_splits_backup = std::move(cur);
        }
    }

    void RestoreInstrumentEditorSplits(const std::vector<IePristineSplit> &splits)
    {
        if (!m_bank_token || m_ie_from_song || splits.empty())
        {
            return;
        }
        if (!EnsureActiveBankWritable())
        {
            return;
        }
        /* Flat INST shadows shift GetIndexed order; INST id 0 is valid. */
        SyncInstrumentEditorBankIndex();
        uint32_t split_count = 0;
        if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token,
                                                      m_ie_instrument_index,
                                                      &split_count) != BAE_NO_ERROR ||
            split_count == 0)
        {
            return;
        }
        const uint32_t want = static_cast<uint32_t>(splits.size());
        /* Align split count so Vol/Low/High/Root/SND can all be restored. */
        while (split_count > want)
        {
            if (BAERmfEditorBank_DeleteInstrumentSample(
                    m_bank_token, m_ie_instrument_index, split_count - 1, false) !=
                BAE_NO_ERROR)
            {
                break;
            }
            SyncInstrumentEditorBankIndex();
            if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token,
                                                          m_ie_instrument_index,
                                                          &split_count) != BAE_NO_ERROR)
            {
                return;
            }
        }
        if (split_count < want)
        {
            if (BAERmfEditorBank_GrowInstrumentSampleSlots(
                    m_bank_token, m_ie_instrument_index, want) == BAE_NO_ERROR)
            {
                SyncInstrumentEditorBankIndex();
                if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token,
                                                              m_ie_instrument_index,
                                                              &split_count) !=
                    BAE_NO_ERROR)
                {
                    return;
                }
            }
        }
        const bool root_in_snd =
            !TEST_FLAG_VALUE(m_ie_ext.flags2, ZBF_useSoundModifierAsRootKey);
        const uint32_t n = std::min(split_count, want);
        for (uint32_t s = 0; s < n; ++s)
        {
            const IePristineSplit &snap = splits[s];
            BAERmfEditorBankSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            /* Prefer current SND metadata so root-in-SND restore can rewrite
             * baseKey (sampleRate==0 only updates INST keymap fields). */
            if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                         m_ie_instrument_index,
                                                         s,
                                                         &info) != BAE_NO_ERROR)
            {
                std::memset(&info, 0, sizeof(info));
            }
            info.lowKey = snap.lowKey;
            info.highKey = snap.highKey;
            info.rootKey = snap.rootKey;
            info.splitVolume = snap.splitVolume;
            info.sndResourceID = static_cast<XShortResourceID>(snap.sndResourceID);
            if (!root_in_snd || info.sampleRate == 0)
            {
                info.sampleRate = 0;
            }
            if (BAERmfEditorBank_SetInstrumentSampleInfo(
                    m_bank_token, m_ie_instrument_index, s, &info) != BAE_NO_ERROR)
            {
                break;
            }
            SyncInstrumentEditorBankIndex();
        }
    }

    void RestoreInstrumentEditorPristineSplits()
    {
        RestoreInstrumentEditorSplits(m_ie_pristine_splits);
    }

    /* Prefer pre-oscillator sample keymap; fall back to pristine. */
    void RestoreInstrumentEditorSampleSplits()
    {
        if (IeSplitsHaveSample(m_ie_sample_splits_backup))
        {
            RestoreInstrumentEditorSplits(m_ie_sample_splits_backup);
            return;
        }
        RestoreInstrumentEditorPristineSplits();
    }

    bool InstrumentEditorBankSplitsAreNone()
    {
        std::vector<IePristineSplit> cur;
        CaptureInstrumentEditorSplitsFromBank(cur);
        return !IeSplitsHaveSample(cur);
    }

    uint32_t InstrumentEditorPatchId() const
    {
        uint32_t patch_id = m_ie_inst_id;
        if (m_selected_instrument >= 0 &&
            m_selected_instrument < static_cast<int>(m_instruments.size()))
        {
            const InstrumentRow &row = m_instruments[static_cast<size_t>(m_selected_instrument)];
            /* Aliases: target_inst_id is the concrete INST (0 is valid). */
            if (row.is_alias)
            {
                patch_id = row.target_inst_id;
            }
        }
        return patch_id;
    }

    /* XReplace appends a flat INST shadow; GetIndexed lists flats first, so
     * instrument_index shifts. Always re-resolve by INST id after bank mutates.
     * INST id 0 is valid (e.g. B0P000 Piano) — do not treat 0 as unset. */
    void SyncInstrumentEditorBankIndex()
    {
        if (!m_bank_token || m_ie_from_song)
        {
            return;
        }
        uint32_t resolved = 0;
        uint32_t index = m_ie_instrument_index;
        if (BAERmfEditorBank_ResolveInstID(m_bank_token, m_ie_inst_id, &resolved, &index) !=
            BAE_NO_ERROR)
        {
            return;
        }
        m_ie_instrument_index = index;
        if (m_selected_instrument >= 0 &&
            m_selected_instrument < static_cast<int>(m_instruments.size()) &&
            m_instruments[static_cast<size_t>(m_selected_instrument)].inst_id == m_ie_inst_id)
        {
            m_instruments[static_cast<size_t>(m_selected_instrument)].instrument_index = index;
        }
    }

    void LivePatchInstrumentOnSong(BAESong song,
                                   BAE_INSTRUMENT inst,
                                   bool reload_samples_from_bank,
                                   bool always_load)
    {
        if (!song)
        {
            return;
        }
        if (reload_samples_from_bank)
        {
            /* GM_LoadInstrument caches INST; unload so keysplit/SND/oscillator
             * bank edits are picked up. */
            BAE_BOOL loaded = FALSE;
            int guard = 0;
            while (guard++ < 64 &&
                   BAESong_IsInstrumentLoaded(song, inst, &loaded) == BAE_NO_ERROR &&
                   loaded)
            {
                if (BAESong_UnloadInstrument(song, inst) != BAE_NO_ERROR)
                {
                    break;
                }
            }
            (void)BAESong_LoadInstrument(song, inst);
        }
        else if (always_load)
        {
            (void)BAESong_LoadInstrument(song, inst);
        }
        else
        {
            /* Playback song: only touch patches already cached so we do not
             * pull unused programs into the playing song's instrument table. */
            BAE_BOOL loaded = FALSE;
            if (BAESong_IsInstrumentLoaded(song, inst, &loaded) != BAE_NO_ERROR || !loaded)
            {
                return;
            }
        }
        (void)BAESong_PatchLoadedInstrumentExtInfo(song, inst, &m_ie_ext);
    }

    /* reload_samples_from_bank: unload/reload so keysplit/SND edits are heard.
     * reload_playback_song: also unload/reload m_song (live song preview). Soft
     * PatchLoadedInstrumentExtInfo alone does not pick up SND remaps.
     * Keep reload_playback_song false when Apply has cleared SND links for a
     * generator — hard-reload of audition would drop shared sample-cache refs
     * still used by m_song. Unload on m_song itself is safe (NeoBAE no longer
     * Stop()'s when mInstrumentsLoadedCount is 0). */
    void LivePatchInstrumentEditor(bool reload_samples_from_bank = false,
                                   bool reload_playback_song = false)
    {
        EnsureBankAuditionSong();
        BAESong audition = GetAuditionSong();
        if (!audition && !m_song)
        {
            return;
        }
#if USE_ZMF_SUPPORT == TRUE
        /* Persist OSCL + PWID/WIDX so NoteOn from bank/doc hears generators
         * before Apply. Also rewrite when clearing a previously live-written
         * OSCL (toggle off / Revert / Close) — otherwise bank stays stuck on. */
        if (m_ie_ext.useOscillator || m_ie_live_osc_persisted)
        {
            BindInstrumentEditorDisplayName();
            m_ie_ext.flags1 = static_cast<unsigned char>(
                m_ie_ext.flags1 & static_cast<unsigned char>(~ZBF_ghostInstrument));
            if (m_ie_ext.useOscillator)
            {
                m_ie_ext.flags1 = static_cast<unsigned char>(m_ie_ext.flags1 | ZBF_extendedFormat);
                m_ie_ext.hasExtendedData = TRUE;
            }
            if (m_ie_from_song && m_document)
            {
                if (BAERmfEditorDocument_SetInstrumentExtInfo(m_document, m_ie_inst_id, &m_ie_ext) ==
                    BAE_NO_ERROR)
                {
                    m_document_dirty = true;
                }
            }
            else if (m_bank_token && EnsureActiveBankWritable())
            {
                /* Re-resolve before write — prior live patches XReplace and shift
                 * GetIndexed INST order. */
                SyncInstrumentEditorBankIndex();
                if (BAERmfEditorBank_SetInstrumentExtInfo(m_bank_token,
                                                          m_ie_instrument_index,
                                                          &m_ie_ext) == BAE_NO_ERROR)
                {
                    SyncInstrumentEditorBankIndex();
                }
                MarkBank01TouchedIfNeeded(m_ie_bank);
            }
            m_ie_live_osc_persisted = (m_ie_ext.useOscillator != FALSE);
        }
#endif
        /* Inst ID 0 is valid (bank 0 program 0). */
        const uint32_t patch_id = InstrumentEditorPatchId();
        const BAE_INSTRUMENT inst = static_cast<BAE_INSTRUMENT>(patch_id);

        if (audition)
        {
            LivePatchInstrumentOnSong(audition, inst, reload_samples_from_bank, true);
        }
        if (m_song && m_song != audition)
        {
            if (reload_samples_from_bank && reload_playback_song)
            {
                /* Only refresh instruments already cached in the playing song —
                 * do not pull unused programs into its instrument table. */
                BAE_BOOL loaded = FALSE;
                if (BAESong_IsInstrumentLoaded(m_song, inst, &loaded) == BAE_NO_ERROR && loaded)
                {
                    LivePatchInstrumentOnSong(m_song, inst, true, false);
                }
            }
            else
            {
                LivePatchInstrumentOnSong(m_song, inst, /*reload_samples=*/false, false);
            }
        }
    }

    /* Unload/reload loaded patches that reference a bank SND so song/IE hear
     * waveform changes. Does not Stop() the song (UnloadInstrument no longer
     * treats mInstrumentsLoadedCount==0 as end-of-playback). */
    void LiveReloadInstrumentsUsingSnd(uint32_t snd_id)
    {
        if (!m_bank_token || snd_id == kNoSndResourceId)
        {
            return;
        }

        std::unordered_set<uint32_t> patch_ids;
        for (const SampleRow &row : m_samples)
        {
            if (row.source != SampleSource::Bank || row.unassigned ||
                row.snd_resource_id != snd_id)
            {
                continue;
            }
            BAERmfEditorBankInstrumentInfo info{};
            if (BAERmfEditorBank_GetInstrumentInfo(m_bank_token, row.instrument_index, &info) !=
                BAE_NO_ERROR)
            {
                continue;
            }
            patch_ids.insert(info.instID);
        }
        for (const InstrumentRow &row : m_instruments)
        {
            if (row.is_alias && patch_ids.count(row.target_inst_id) != 0)
            {
                patch_ids.insert(row.inst_id);
            }
        }
        if (patch_ids.empty())
        {
            return;
        }

        EnsureBankAuditionSong();
        auto remount = [&](BAESong song, bool load_if_missing) {
            if (!song)
            {
                return;
            }
            for (uint32_t id : patch_ids)
            {
                const BAE_INSTRUMENT inst = static_cast<BAE_INSTRUMENT>(id);
                BAE_BOOL loaded = FALSE;
                const bool was_loaded =
                    (BAESong_IsInstrumentLoaded(song, inst, &loaded) == BAE_NO_ERROR && loaded);
                if (!was_loaded && !load_if_missing)
                {
                    continue;
                }
                /* Unload/reload only — do not PatchLoadedInstrumentExtInfo with
                 * m_ie_ext (that belongs to the open IE instrument, not every SND user). */
                int guard = 0;
                while (guard++ < 64 &&
                       BAESong_IsInstrumentLoaded(song, inst, &loaded) == BAE_NO_ERROR &&
                       loaded)
                {
                    if (BAESong_UnloadInstrument(song, inst) != BAE_NO_ERROR)
                    {
                        break;
                    }
                }
                (void)BAESong_LoadInstrument(song, inst);
            }
        };

        BAESong audition = GetAuditionSong();
        /* Audition keyboard: only remount patches already loaded there. */
        remount(audition, false);
        if (m_song && m_song != audition)
        {
            remount(m_song, false);
        }

        /* Reloaded INST drops soft ExtInfo; re-apply if IE is open on one of them. */
        if (m_instrument_editor_open && !m_ie_from_song)
        {
            const uint32_t ie_id = InstrumentEditorPatchId();
            if (patch_ids.count(ie_id) != 0 || patch_ids.count(m_ie_inst_id) != 0)
            {
                LivePatchInstrumentEditor(false);
            }
        }
    }

    void InstrumentEditorRefreshPreEditSnapshot()
    {
        if (!ImGui::IsAnyItemActive())
        {
            m_ie_ext_pre_edit = m_ie_ext;
            m_ie_undo_pushed_for_gesture = false;
        }
    }

    void InstrumentEditorPushUndoFromPreEdit()
    {
        if (m_ie_undo_pushed_for_gesture)
        {
            return;
        }
        m_ie_undo.push_back(m_ie_ext_pre_edit);
        while (m_ie_undo.size() > 32u)
        {
            m_ie_undo.pop_front();
        }
        m_ie_redo.clear();
        m_ie_undo_pushed_for_gesture = true;
    }

    void MarkInstrumentEditorDirty(bool live_patch = true)
    {
        InstrumentEditorPushUndoFromPreEdit();
        m_ie_dirty = true;
        if (live_patch)
        {
            LivePatchInstrumentEditor();
        }
    }

    void InstrumentEditorUndo()
    {
        if (m_ie_undo.empty())
        {
            return;
        }
        m_ie_redo.push_back(m_ie_ext);
        while (m_ie_redo.size() > 32u)
        {
            m_ie_redo.pop_front();
        }
        m_ie_ext = m_ie_undo.back();
        m_ie_undo.pop_back();
        BindInstrumentEditorDisplayName();
        SyncLfoControlModesFromExt();
        m_ie_dirty = true;
        m_ie_undo_pushed_for_gesture = false;
        m_ie_ext_pre_edit = m_ie_ext;
        LivePatchInstrumentEditor();
        SetStatus("Instrument editor: undo");
    }

    void InstrumentEditorRedo()
    {
        if (m_ie_redo.empty())
        {
            return;
        }
        m_ie_undo.push_back(m_ie_ext);
        while (m_ie_undo.size() > 32u)
        {
            m_ie_undo.pop_front();
        }
        m_ie_ext = m_ie_redo.back();
        m_ie_redo.pop_back();
        BindInstrumentEditorDisplayName();
        SyncLfoControlModesFromExt();
        m_ie_dirty = true;
        m_ie_undo_pushed_for_gesture = false;
        m_ie_ext_pre_edit = m_ie_ext;
        LivePatchInstrumentEditor();
        SetStatus("Instrument editor: redo");
    }

    void InstrumentEditorRevert(bool announce = true)
    {
        m_ie_ext = m_ie_ext_pristine;
        BindInstrumentEditorDisplayName();
        SyncLfoControlModesFromExt();
        /* Re-resolve before bank writes — index may already be stale from prior
         * XReplace (esp. INST id 0 where Sync used to no-op). */
        SyncInstrumentEditorBankIndex();
        RestoreInstrumentEditorPristineSplits();
        m_ie_undo.clear();
        m_ie_redo.clear();
        m_ie_dirty = false;
        m_ie_undo_pushed_for_gesture = false;
        m_ie_ext_pre_edit = m_ie_ext;
#if USE_ZMF_SUPPORT == TRUE
        /* Oscillator toggle may have written OSCL into the bank for audition;
         * push pristine ext back so Revert truly restores opened state. */
        SyncInstrumentEditorOscillatorPreview();
#else
        LivePatchInstrumentEditor(true);
#endif
        if (announce)
        {
            SetStatus("Instrument editor: reverted to opened state");
        }
    }

    void ClearInstrumentEditorHistory()
    {
        m_ie_undo.clear();
        m_ie_redo.clear();
        m_ie_ext_pristine = m_ie_ext;
        m_ie_ext_pre_edit = m_ie_ext;
        m_ie_undo_pushed_for_gesture = false;
        m_ie_live_osc_persisted = (m_ie_ext.useOscillator != FALSE);
        CaptureInstrumentEditorPristineSplits();
    }

    void ApplyInstrumentEditor(bool live_patch)
    {
        BAEResult r = BAE_PARAM_ERR;
        bool splits_changed = false;
        m_ie_ext.flags1 = static_cast<unsigned char>(m_ie_ext.flags1 &
                                                     static_cast<unsigned char>(~ZBF_ghostInstrument));
#if USE_ZMF_SUPPORT == TRUE
        /* Only rewrite split SND ids when oscillator mode actually toggled.
         * ADSR/LPF/LFO Apply used to restore every split (N full bank rebuilds).
         * Export strips leftover samples for OSCL instruments as a safety net. */
        {
            const bool osc_now = m_ie_ext.useOscillator != FALSE;
            const bool osc_was = m_ie_ext_pristine.useOscillator != FALSE;
            if (m_bank_token && !m_ie_from_song && osc_now != osc_was)
            {
                splits_changed = true;
                if (osc_now)
                {
                    CaptureInstrumentEditorSampleBackupIfPresent();
                    SetInstrumentEditorSplitSndIds(static_cast<uint16_t>(kNoSndResourceId));
                }
                else if (InstrumentEditorBankSplitsAreNone())
                {
                    /* User already picked a sample after unchecking generator —
                     * do not wipe it with a pristine snapshot that may be SND=none
                     * from the prior generator Apply. */
                    RestoreInstrumentEditorSampleSplits();
                }
                RefreshListsFromBank();
            }
            /* Song instrument → generator: drop its document sample rows so they
             * don't linger as undeletable [Song] orphans. */
            if (m_ie_from_song && m_document && osc_now)
            {
                (void)DeleteDocumentSamplesForInstrument(m_ie_inst_id);
                RefreshSongSamplesFromDocument();
            }
        }
#endif
        if (m_ie_from_song && m_document)
        {
            r = BAERmfEditorDocument_SetInstrumentExtInfo(m_document, m_ie_inst_id, &m_ie_ext);
            if (r == BAE_NO_ERROR)
            {
                m_document_dirty = true;
            }
        }
        else if (m_bank_token)
        {
            if (!EnsureActiveBankWritable())
            {
                SetStatus("Instrument apply failed: bank not writable");
                return;
            }
            SyncInstrumentEditorBankIndex();
            r = BAERmfEditorBank_SetInstrumentExtInfo(m_bank_token, m_ie_instrument_index, &m_ie_ext);
            if (r == BAE_NO_ERROR)
            {
                SyncInstrumentEditorBankIndex();
            }
        }
        if (r != BAE_NO_ERROR)
        {
            SetStatus(std::string("Instrument apply failed: ") + FormatBAEError(r));
            return;
        }
        m_ie_dirty = false;
        ClearInstrumentEditorHistory();
        MarkBank01TouchedIfNeeded(m_ie_bank);
        if (live_patch)
        {
            /* Soft-patch only. Hard-reloading the audition song after Apply has
             * cleared INST→SND links drops shared sample-cache refs still used
             * by m_song. Live edits already patched both songs. */
            LivePatchInstrumentEditor(false);
        }
        (void)splits_changed;
        SetStatus("Instrument applied");
    }

    void DrawInstrumentEditorDialog()
    {
        if (!m_instrument_editor_open)
        {
            m_instrument_editor_focused = false;
            return;
        }

        static const FourCharLabel kLfoDestLabels[] = {
            {static_cast<int32_t>(FOUR_CHAR('V', 'O', 'L', 'U')), "VOLUME"},
            {static_cast<int32_t>(FOUR_CHAR('P', 'I', 'T', 'C')), "PITCH"},
            {static_cast<int32_t>(FOUR_CHAR('S', 'P', 'A', 'N')), "STEREO_PAN"},
            {static_cast<int32_t>(FOUR_CHAR('P', 'A', 'N', ' ')), "STEREO_PAN_2"},
            {static_cast<int32_t>(FOUR_CHAR('L', 'P', 'F', 'R')), "LPF_FREQUENCY"},
            {static_cast<int32_t>(FOUR_CHAR('L', 'P', 'R', 'E')), "LPF_DEPTH"},
            {static_cast<int32_t>(FOUR_CHAR('L', 'P', 'A', 'M')), "LOW_PASS_AMOUNT"},
#if USE_ZMF_SUPPORT == TRUE
            {static_cast<int32_t>(FOUR_CHAR('P', 'W', 'I', 'D')), "PULSE_WIDTH/SHAPE"},
            {static_cast<int32_t>(FOUR_CHAR('W', 'I', 'D', 'X')), "WAVE_INDEX"},
#endif
        };
        static const FourCharLabel kLfoShapeLabels[] = {
            {0, "None"},
            {static_cast<int32_t>(FOUR_CHAR('S', 'I', 'N', 'E')), "SINE"},
            {static_cast<int32_t>(FOUR_CHAR('T', 'R', 'I', 'A')), "TRIANGLE"},
            {static_cast<int32_t>(FOUR_CHAR('S', 'Q', 'U', 'A')), "SQUARE"},
            {static_cast<int32_t>(FOUR_CHAR('S', 'Q', 'U', '2')), "SQUARE_SYNC"},
            {static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', 'T')), "SAWTOOTH"},
            {static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', '2')), "SAWTOOTH_SYNC"},
        };
        auto label_for = [](const FourCharLabel *table, int count, int32_t value) -> const char * {
            for (int i = 0; i < count; ++i)
            {
                if (table[i].value == value)
                {
                    return table[i].label;
                }
            }
            return "Custom";
        };
        auto draw_four_char_combo = [&](const char *label,
                                        int32_t &value,
                                        const FourCharLabel *table,
                                        int count) {
            const char *preview = label_for(table, count, value);
            if (ImGui::BeginCombo(label, preview))
            {
                for (int i = 0; i < count; ++i)
                {
                    const bool selected = (table[i].value == value);
                    if (ImGui::Selectable(table[i].label, selected))
                    {
                        value = table[i].value;
                        MarkInstrumentEditorDirty();
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        };
        auto is_filter_lfo_dest = [](int32_t dest) {
            return dest == static_cast<int32_t>(FOUR_CHAR('L', 'P', 'F', 'R')) ||
                   dest == static_cast<int32_t>(FOUR_CHAR('L', 'P', 'R', 'E')) ||
                   dest == static_cast<int32_t>(FOUR_CHAR('L', 'P', 'A', 'M'));
        };

        bool open = m_instrument_editor_open;
        ImGui::SetNextWindowSize(ImVec2(900, 690), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Instrument Editor", &open))
        {
            m_instrument_editor_focused =
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            InstrumentEditorRefreshPreEditSnapshot();

            ImGui::Text("%s  instID=%u  B%dP%03d  %s",
                        m_ie_ext.displayName ? m_ie_ext.displayName : "(instrument)",
                        static_cast<unsigned>(m_ie_inst_id),
                        m_ie_bank,
                        m_ie_program,
                        m_ie_from_song ? "[Song]" : "[Bank]");
            if (m_ie_dirty)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "modified");
            }

            ImGui::BeginDisabled(m_ie_undo.empty());
            if (ImGui::Button("Undo"))
            {
                InstrumentEditorUndo();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Ctrl+Z");
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_ie_redo.empty());
            if (ImGui::Button("Redo"))
            {
                InstrumentEditorRedo();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Ctrl+Y / Ctrl+Shift+Z");
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert"))
            {
                InstrumentEditorRevert();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Restore instrument as opened");
            }

            if (m_instrument_editor_focused && !ImGui::GetIO().WantTextInput &&
                ImGui::GetIO().KeyCtrl)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    if (ImGui::GetIO().KeyShift)
                    {
                        InstrumentEditorRedo();
                    }
                    else
                    {
                        InstrumentEditorUndo();
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                {
                    InstrumentEditorRedo();
                }
            }

            ImGui::SeparatorText("Preview");
            DrawKeyboard(true);

            if (ImGui::BeginTabBar("##IEPages"))
            {
                if (ImGui::BeginTabItem("Keymap"))
                {
                    m_ie_page = 0;
                    DrawInstrumentEditorKeymapPage();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Filter"))
                {
                    m_ie_page = 1;
                    bool ie_has_stereo_sample = false;
                    {
                        uint32_t split_count = 0;
                        if (m_bank_token &&
                            BAERmfEditorBank_GetInstrumentSampleCount(
                                m_bank_token, m_ie_instrument_index, &split_count) == BAE_NO_ERROR)
                        {
                            for (uint32_t s = 0; s < split_count; ++s)
                            {
                                BAERmfEditorBankSampleInfo sinfo;
                                std::memset(&sinfo, 0, sizeof(sinfo));
                                if (BAERmfEditorBank_GetInstrumentSampleInfo(
                                        m_bank_token, m_ie_instrument_index, s, &sinfo) == BAE_NO_ERROR &&
                                    sinfo.channels > 1)
                                {
                                    ie_has_stereo_sample = true;
                                    break;
                                }
                            }
                        }
                    }
                    ImGui::TextDisabled(
                        "Mono LPF is classic HSB/RMF-compatible. "
                        "Stereo samples with an active filter need .zsb / .zmf.");
                    int freq = m_ie_ext.LPF_frequency;
                    int res = m_ie_ext.LPF_resonance;
                    int amt = m_ie_ext.LPF_lowpassAmount;
                    if (ImGui::InputInt("LPF frequency", &freq))
                    {
                        m_ie_ext.LPF_frequency =
                            std::clamp(freq, kLpfFreqMin, kLpfFreqMax);
                        MarkInstrumentEditorDirty();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip("0 = off · engine active range ≈ 512–%d",
                                          kLpfFreqMax);
                    }
                    if (ImGui::InputInt("LPF resonance", &res))
                    {
                        m_ie_ext.LPF_resonance =
                            std::clamp(res, kLpfResMin, kLpfResMax);
                        MarkInstrumentEditorDirty();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip("Range %d–%d", kLpfResMin, kLpfResMax);
                    }
                    if (ImGui::InputInt("LPF lowpass amount", &amt))
                    {
                        m_ie_ext.LPF_lowpassAmount =
                            std::clamp(amt, kLpfAmountMin, kLpfAmountMax);
                        MarkInstrumentEditorDirty();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip("Range %d–%d",
                                          kLpfAmountMin,
                                          kLpfAmountMax);
                    }
                    {
                        bool has_filter_lfo = false;
                        for (uint32_t i = 0; i < m_ie_ext.lfoCount && i < BAE_EDITOR_MAX_LFOS; ++i)
                        {
                            if (is_filter_lfo_dest(m_ie_ext.lfos[i].destination))
                            {
                                has_filter_lfo = true;
                                break;
                            }
                        }
                        const bool filter_active =
                            (freq != 0 || res != 0 || amt != 0 || has_filter_lfo);
                        if (filter_active && ie_has_stereo_sample)
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                               "Active filter on stereo sample → requires .zsb / .zmf");
                        }
                    }
                    DrawFilterFreqResPad();
                    ImGui::SeparatorText("Filter envelopes (LFO → LPFR / LPRE / LPAM)");
                    {
                        uint32_t filter_lfo_count = 0;
                        for (uint32_t i = 0; i < m_ie_ext.lfoCount && i < BAE_EDITOR_MAX_LFOS; ++i)
                        {
                            if (is_filter_lfo_dest(m_ie_ext.lfos[i].destination))
                            {
                                filter_lfo_count++;
                            }
                        }
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Filter LFOs: %u", static_cast<unsigned>(filter_lfo_count));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("+##add_filter_lfo") &&
                            m_ie_ext.lfoCount < BAE_EDITOR_MAX_LFOS)
                        {
                            MarkInstrumentEditorDirty(false);
                            BAERmfEditorLFOInfo &lfo = m_ie_ext.lfos[m_ie_ext.lfoCount];
                            std::memset(&lfo, 0, sizeof(lfo));
                            lfo.destination = static_cast<int32_t>(FOUR_CHAR('L', 'P', 'F', 'R'));
                            lfo.waveShape = static_cast<int32_t>(FOUR_CHAR('S', 'I', 'N', 'E'));
                            lfo.period = 1000;
                            lfo.DC_feed = 0;
                            lfo.level = 0;
                            lfo.adsr.stageCount = 0;
                            m_ie_lfo_control_mode[m_ie_ext.lfoCount] = kLfoModeBoth;
                            m_ie_ext.lfoCount++;
                            LivePatchInstrumentEditor();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("-##remove_filter_lfo") && filter_lfo_count > 0)
                        {
                            MarkInstrumentEditorDirty(false);
                            for (int i = static_cast<int>(m_ie_ext.lfoCount) - 1; i >= 0; --i)
                            {
                                if (!is_filter_lfo_dest(m_ie_ext.lfos[static_cast<uint32_t>(i)].destination))
                                {
                                    continue;
                                }
                                for (uint32_t j = static_cast<uint32_t>(i) + 1; j < m_ie_ext.lfoCount; ++j)
                                {
                                    m_ie_ext.lfos[j - 1] = m_ie_ext.lfos[j];
                                }
                                m_ie_ext.lfoCount--;
                                std::memset(&m_ie_ext.lfos[m_ie_ext.lfoCount],
                                            0,
                                            sizeof(m_ie_ext.lfos[0]));
                                RemoveLfoControlModeAt(static_cast<uint32_t>(i));
                                break;
                            }
                            LivePatchInstrumentEditor();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        {
                            ImGui::SetTooltip("Remove last filter-destination LFO");
                        }
                    }
                    bool any_filter_lfo = false;
                    for (uint32_t i = 0; i < m_ie_ext.lfoCount && i < BAE_EDITOR_MAX_LFOS; ++i)
                    {
                        if (!is_filter_lfo_dest(m_ie_ext.lfos[i].destination))
                        {
                            continue;
                        }
                        any_filter_lfo = true;
                        ImGui::PushID(static_cast<int>(i) + 3000);
                        ImGui::Text("LFO %u → %s",
                                    static_cast<unsigned>(i),
                                    label_for(kLfoDestLabels,
                                              static_cast<int>(sizeof(kLfoDestLabels) / sizeof(kLfoDestLabels[0])),
                                              m_ie_ext.lfos[i].destination));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove"))
                        {
                            MarkInstrumentEditorDirty(false);
                            for (uint32_t j = i + 1; j < m_ie_ext.lfoCount; ++j)
                            {
                                m_ie_ext.lfos[j - 1] = m_ie_ext.lfos[j];
                            }
                            if (m_ie_ext.lfoCount > 0)
                            {
                                m_ie_ext.lfoCount--;
                                std::memset(&m_ie_ext.lfos[m_ie_ext.lfoCount],
                                            0,
                                            sizeof(m_ie_ext.lfos[0]));
                            }
                            RemoveLfoControlModeAt(i);
                            LivePatchInstrumentEditor();
                            ImGui::PopID();
                            break;
                        }
                        char lfo_id[32];
                        std::snprintf(lfo_id, sizeof(lfo_id), "filter_lfo_%u", i);
                        DrawInstrumentLfoEditor(
                            m_ie_ext.lfos[i],
                            i,
                            lfo_id,
                            kLfoDestLabels,
                            static_cast<int>(sizeof(kLfoDestLabels) / sizeof(kLfoDestLabels[0])),
                            kLfoShapeLabels,
                            static_cast<int>(sizeof(kLfoShapeLabels) / sizeof(kLfoShapeLabels[0])));
                        ImGui::PopID();
                    }
                    if (!any_filter_lfo)
                    {
                        ImGui::TextDisabled(
                            "No filter-destination LFOs. Use + to add LPFR/LPRE/LPAM, "
                            "or configure them on the Modulation page.");
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Volume"))
                {
                    m_ie_page = 2;
                    int pan = m_ie_ext.panPlacement;
                    int vol = m_ie_ext.miscParameter2;
                    int rev = m_ie_ext.defaultReverbSend;
                    int cho = m_ie_ext.defaultChorusSend;
                    if (ImGui::SliderInt("Pan", &pan, -63, 63))
                    {
                        m_ie_ext.panPlacement = static_cast<char>(pan);
                        MarkInstrumentEditorDirty();
                    }
                    if (ImGui::InputInt("Volume (misc2)", &vol))
                    {
                        m_ie_ext.miscParameter2 = static_cast<int16_t>(vol);
                        MarkInstrumentEditorDirty();
                    }
                    if (ImGui::InputInt("Default reverb send", &rev))
                    {
                        m_ie_ext.defaultReverbSend = static_cast<int16_t>(rev);
                        MarkInstrumentEditorDirty();
                    }
                    if (ImGui::InputInt("Default chorus send", &cho))
                    {
                        m_ie_ext.defaultChorusSend = static_cast<int16_t>(cho);
                        MarkInstrumentEditorDirty();
                    }
                    ImGui::SeparatorText("Volume ADSR");
                    ImGui::TextDisabled(
                        "BE2 units · LEVEL %% (100 = unity / 4096 raw) · TIME (MS)");
                    DrawAdsrStageEditor(m_ie_ext.volumeADSR, "vol_adsr", true);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Modulation"))
                {
                    m_ie_page = 3;
                    bool def_mod = m_ie_ext.hasDefaultMod != FALSE;
                    if (ImGui::Checkbox("Has DEFAULT_MOD", &def_mod))
                    {
                        m_ie_ext.hasDefaultMod = def_mod ? TRUE : FALSE;
                        MarkInstrumentEditorDirty();
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("LFOs: %u / %d",
                                static_cast<unsigned>(m_ie_ext.lfoCount),
                                BAE_EDITOR_MAX_LFOS);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+##add_lfo") &&
                        m_ie_ext.lfoCount < BAE_EDITOR_MAX_LFOS)
                    {
                        MarkInstrumentEditorDirty(false);
                        BAERmfEditorLFOInfo &lfo = m_ie_ext.lfos[m_ie_ext.lfoCount];
                        std::memset(&lfo, 0, sizeof(lfo));
                        lfo.destination = static_cast<int32_t>(FOUR_CHAR('V', 'O', 'L', 'U'));
                        lfo.waveShape = static_cast<int32_t>(FOUR_CHAR('S', 'I', 'N', 'E'));
                        lfo.period = 1000;
                        lfo.DC_feed = 0;
                        lfo.level = 0;
                        lfo.adsr.stageCount = 0;
                        m_ie_lfo_control_mode[m_ie_ext.lfoCount] = kLfoModeBoth;
                        m_ie_ext.lfoCount++;
                        LivePatchInstrumentEditor();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-##remove_lfo") && m_ie_ext.lfoCount > 0)
                    {
                        MarkInstrumentEditorDirty(false);
                        m_ie_ext.lfoCount--;
                        std::memset(&m_ie_ext.lfos[m_ie_ext.lfoCount], 0, sizeof(m_ie_ext.lfos[0]));
                        RemoveLfoControlModeAt(m_ie_ext.lfoCount);
                        LivePatchInstrumentEditor();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip("Remove last LFO");
                    }
                    for (uint32_t i = 0; i < m_ie_ext.lfoCount && i < BAE_EDITOR_MAX_LFOS; ++i)
                    {
                        ImGui::PushID(static_cast<int>(i) + 1000);
                        ImGui::Separator();
                        ImGui::Text("LFO %u", static_cast<unsigned>(i));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove"))
                        {
                            MarkInstrumentEditorDirty(false);
                            for (uint32_t j = i + 1; j < m_ie_ext.lfoCount; ++j)
                            {
                                m_ie_ext.lfos[j - 1] = m_ie_ext.lfos[j];
                            }
                            if (m_ie_ext.lfoCount > 0)
                            {
                                m_ie_ext.lfoCount--;
                                std::memset(&m_ie_ext.lfos[m_ie_ext.lfoCount],
                                            0,
                                            sizeof(m_ie_ext.lfos[0]));
                            }
                            RemoveLfoControlModeAt(i);
                            LivePatchInstrumentEditor();
                            ImGui::PopID();
                            break;
                        }
                        char lfo_id[32];
                        std::snprintf(lfo_id, sizeof(lfo_id), "mod_lfo_%u", i);
                        DrawInstrumentLfoEditor(
                            m_ie_ext.lfos[i],
                            i,
                            lfo_id,
                            kLfoDestLabels,
                            static_cast<int>(sizeof(kLfoDestLabels) / sizeof(kLfoDestLabels[0])),
                            kLfoShapeLabels,
                            static_cast<int>(sizeof(kLfoShapeLabels) / sizeof(kLfoShapeLabels[0])));
                        ImGui::PopID();
                    }
                    ImGui::Text("Curves: %u", static_cast<unsigned>(m_ie_ext.curveCount));
                    for (uint32_t i = 0; i < m_ie_ext.curveCount && i < BAE_EDITOR_MAX_CURVES; ++i)
                    {
                        ImGui::PushID(static_cast<int>(i) + 2000);
                        if (ImGui::InputInt("tieFrom", &m_ie_ext.curves[i].tieFrom))
                        {
                            MarkInstrumentEditorDirty();
                        }
                        if (ImGui::InputInt("tieTo", &m_ie_ext.curves[i].tieTo))
                        {
                            MarkInstrumentEditorDirty();
                        }
                        int cc = m_ie_ext.curves[i].curveCount;
                        if (ImGui::InputInt("points", &cc))
                        {
                            m_ie_ext.curves[i].curveCount = static_cast<int16_t>(
                                std::clamp(cc, 0, BAE_EDITOR_MAX_ADSR_STAGES));
                            MarkInstrumentEditorDirty();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Output"))
                {
                    m_ie_page = 4;
                    int root = m_ie_ext.midiRootKey;
                    int misc1 = m_ie_ext.miscParameter1;
                    if (ImGui::InputInt("MIDI root key", &root))
                    {
                        m_ie_ext.midiRootKey = static_cast<int16_t>(root);
                        MarkInstrumentEditorDirty();
                    }
                    if (ImGui::InputInt("miscParameter1", &misc1))
                    {
                        m_ie_ext.miscParameter1 = static_cast<int16_t>(misc1);
                        MarkInstrumentEditorDirty();
                    }
                    auto flag_bit = [this](const char *label, unsigned char &flags, unsigned char bit) {
                        bool on = (flags & bit) != 0;
                        if (ImGui::Checkbox(label, &on))
                        {
                            if (on)
                            {
                                flags = static_cast<unsigned char>(flags | bit);
                            }
                            else
                            {
                                flags = static_cast<unsigned char>(flags & ~bit);
                            }
                            MarkInstrumentEditorDirty();
                        }
                    };
                    flag_bit("Use sample rate", m_ie_ext.flags1, ZBF_useSampleRate);
                    flag_bit("Sample and hold", m_ie_ext.flags1, ZBF_sampleAndHold);
                    flag_bit("Avoid reverb", m_ie_ext.flags1, ZBF_avoidReverb);
                    flag_bit("Disable SND looping", m_ie_ext.flags1, ZBF_disableSndLooping);
                    flag_bit("Play at sampled freq", m_ie_ext.flags2, ZBF_playAtSampledFreq);
#if USE_ZMF_SUPPORT == TRUE
                    flag_bit("Advanced interpolation (ZMF)", m_ie_ext.flags2, ZBF_advancedInterpolation);
                    flag_bit("Sample offset start (ZMF)", m_ie_ext.flags2, ZBF_enableSampleOffsetStart);
                    ImGui::Spacing();
                    DrawInstrumentEditorOscillatorControls();
#endif
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            if (ImGui::Button("Apply"))
            {
                /* Defer off the ImGui frame so busy UI can paint before
                 * EnsureWritable / ZINS work on large banks. */
                m_ie_close_after_apply = false;
                m_long_op = LongOp::ApplyInstrument;
                ArmBusyProgress("Applying instrument…");
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                RequestCloseInstrumentEditor();
            }
        }
        else
        {
            m_instrument_editor_focused = false;
        }
        ImGui::End();
        /* Title-bar X sets open=false — same confirm path as Close. */
        if (m_instrument_editor_open && !open)
        {
            RequestCloseInstrumentEditor();
        }
        DrawInstrumentEditorCloseConfirm();
    }

    void FinishCloseInstrumentEditor(bool discard_changes)
    {
        if (m_mouse_active_note >= 0)
        {
            NoteOff(m_mouse_active_note);
            m_key_mouse_held[static_cast<size_t>(m_mouse_active_note)] = false;
            m_mouse_active_note = -1;
        }
        if (m_ie_note_song)
        {
            BAESong_KillActiveNotes(m_ie_note_song);
            m_ie_note_song = nullptr;
        }
        m_instrument_editor_focused = false;
        m_confirm_ie_close_open = false;
        m_ie_close_after_apply = false;
        if (discard_changes && m_ie_dirty)
        {
            /* Generator (and other) live writes can hit bank/doc before Apply. */
            InstrumentEditorRevert(false);
        }
        m_instrument_editor_open = false;
    }

    void RequestCloseInstrumentEditor()
    {
        if (!m_instrument_editor_open)
        {
            return;
        }
        if (m_ie_dirty)
        {
            m_confirm_ie_close_open = true;
            return;
        }
        FinishCloseInstrumentEditor(false);
    }

    void DrawInstrumentEditorCloseConfirm()
    {
        if (m_confirm_ie_close_open)
        {
            ImGui::OpenPopup("Unsaved Instrument Changes");
        }
        if (!ImGui::BeginPopupModal("Unsaved Instrument Changes",
                                    &m_confirm_ie_close_open,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextWrapped(
            "This instrument has unapplied changes.\n"
            "Apply them to the bank/song, or discard and close?");
        ImGui::Spacing();
        if (ImGui::Button("Apply and Close", ImVec2(150.0f, 0.0f)))
        {
            m_confirm_ie_close_open = false;
            m_ie_close_after_apply = true;
            m_long_op = LongOp::ApplyInstrument;
            ArmBusyProgress("Applying instrument…");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f)))
        {
            m_confirm_ie_close_open = false;
            FinishCloseInstrumentEditor(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_confirm_ie_close_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    int FindSampleRowForInstrumentSplit(uint32_t instrument_index,
                                        uint32_t split_index,
                                        uint32_t snd_resource_id) const
    {
        for (size_t i = 0; i < m_samples.size(); ++i)
        {
            const SampleRow &row = m_samples[i];
            if (row.source == SampleSource::Bank && row.instrument_index == instrument_index &&
                row.split_index == split_index)
            {
                return static_cast<int>(i);
            }
        }
        if (snd_resource_id == kNoSndResourceId)
        {
            return -1;
        }
        for (size_t i = 0; i < m_samples.size(); ++i)
        {
            const SampleRow &row = m_samples[i];
            if (row.source == SampleSource::Bank && row.snd_resource_id == snd_resource_id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void DeleteInstrumentSplitAt(uint32_t split_index)
    {
        if (!m_bank_token || !EnsureActiveBankWritable())
        {
            return;
        }
        if (BAERmfEditorBank_DeleteInstrumentSample(m_bank_token,
                                                    m_ie_instrument_index,
                                                    split_index,
                                                    false) == BAE_NO_ERROR)
        {
            SyncInstrumentEditorBankIndex();
        }
        if (m_ie_selected_split == static_cast<int>(split_index))
        {
            m_ie_selected_split = -1;
        }
        else if (m_ie_selected_split > static_cast<int>(split_index))
        {
            --m_ie_selected_split;
        }
        RefreshListsFromBank();
        RefreshSongSamplesFromDocument();
    }

#if USE_ZMF_SUPPORT == TRUE
    void SetInstrumentEditorSplitSndIds(uint16_t snd_id)
    {
        if (!m_bank_token || m_ie_from_song || !EnsureActiveBankWritable())
        {
            return;
        }
        SyncInstrumentEditorBankIndex();
        uint32_t split_count = 0;
        if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token,
                                                      m_ie_instrument_index,
                                                      &split_count) != BAE_NO_ERROR ||
            split_count == 0)
        {
            return;
        }
        std::vector<XShortResourceID> ids(split_count, static_cast<XShortResourceID>(snd_id));
        if (BAERmfEditorBank_SetInstrumentSampleSndIDs(
                m_bank_token, m_ie_instrument_index, ids.data(), split_count) == BAE_NO_ERROR)
        {
            SyncInstrumentEditorBankIndex();
        }
    }

    /* Oscillator shape/PW UI entry point; LivePatch persists OSCL+modulators. */
    void SyncInstrumentEditorOscillatorPreview()
    {
        LivePatchInstrumentEditor(true);
    }

    void DrawInstrumentEditorOscillatorControls()
    {
        ImGui::SeparatorText("Oscillator (ZMF)");
        bool use_osc = m_ie_ext.useOscillator != FALSE;
        if (ImGui::Checkbox("Don't use sample", &use_osc))
        {
            m_ie_ext.useOscillator = use_osc ? TRUE : FALSE;
            if (use_osc)
            {
                if (m_ie_ext.oscWaveShape == 0)
                {
                    m_ie_ext.oscWaveShape = static_cast<int32_t>(FOUR_CHAR('S', 'I', 'N', 'E'));
                }
                if (m_ie_ext.oscPulseWidth <= 0)
                {
                    m_ie_ext.oscPulseWidth = 32768;
                }
                if (m_ie_ext.oscVolume <= 0)
                {
                    m_ie_ext.oscVolume = 16384; /* 25% default */
                }
                MarkInstrumentEditorDirty(false);
                SyncInstrumentEditorOscillatorPreview();
            }
            else
            {
                /* Restore sample links so reload can succeed; clearing useOscillator
                 * on a NULL-waveform voice crashes on note-on. Prefer the
                 * pre-generator sample backup — pristine may be SND=none after a
                 * prior Apply with the generator on. */
                RestoreInstrumentEditorSampleSplits();
                RefreshListsFromBank();
                MarkInstrumentEditorDirty(false);
                SyncInstrumentEditorOscillatorPreview();
            }
        }
        if (!use_osc)
        {
            return;
        }

        struct WaveChoice
        {
            const char *label;
            int32_t fourcc;
        };
        /* SINE = real sine; TRIA/SAWT/SQUA match LFO. Osc-only: PLSE/NOIS.
         * PLSE is variable-duty (high first); SQUA is fixed-polarity square
         * with PW via phase remap — not the same waveform. */
        static const WaveChoice kWaves[] = {
            {"Sine", static_cast<int32_t>(FOUR_CHAR('S', 'I', 'N', 'E'))},
            {"Triangle", static_cast<int32_t>(FOUR_CHAR('T', 'R', 'I', 'A'))},
            {"Saw", static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', 'T'))},
            {"Square", static_cast<int32_t>(FOUR_CHAR('S', 'Q', 'U', 'A'))},
            {"Pulse", static_cast<int32_t>(FOUR_CHAR('P', 'L', 'S', 'E'))},
            {"Noise", static_cast<int32_t>(FOUR_CHAR('N', 'O', 'I', 'S'))},
        };
        const int wave_count = static_cast<int>(sizeof(kWaves) / sizeof(kWaves[0]));
        int current = 0;
        for (int i = 0; i < wave_count; ++i)
        {
            int32_t shape = m_ie_ext.oscWaveShape;
            if (shape == static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', 'W')))
            {
                shape = static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', 'T'));
            }
            if (shape == kWaves[i].fourcc)
            {
                current = i;
                break;
            }
        }
        ImGui::BeginGroup();
        for (int i = 0; i < wave_count; ++i)
        {
            if (i)
            {
                ImGui::SameLine();
            }
            const bool selected = (current == i);
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(kWaves[i].label, ImVec2(64.0f, 48.0f)))
            {
                m_ie_ext.oscWaveShape = kWaves[i].fourcc;
                MarkInstrumentEditorDirty(false);
                SyncInstrumentEditorOscillatorPreview();
            }
            if (selected)
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndGroup();

        /* Same OSCL/PWID field; label depends on wave (Pulse = duty, others = shape). */
        {
            int32_t shape = m_ie_ext.oscWaveShape;
            if (shape == static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', 'W')))
            {
                shape = static_cast<int32_t>(FOUR_CHAR('S', 'A', 'W', 'T'));
            }
            const bool is_pulse =
                shape == static_cast<int32_t>(FOUR_CHAR('P', 'L', 'S', 'E'));
            const bool is_noise =
                shape == static_cast<int32_t>(FOUR_CHAR('N', 'O', 'I', 'S'));
            /* ### keeps ImGui ID stable when the visible label swaps. */
            const char *pw_label =
                is_pulse ? "Pulse Width###osc_pw" : "Shape###osc_pw";
            float pw_pct = static_cast<float>(m_ie_ext.oscPulseWidth) * (100.0f / 65536.0f);
            if (is_noise)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::SliderFloat(pw_label, &pw_pct, 1.0f, 99.0f, "%.0f%%"))
            {
                m_ie_ext.oscPulseWidth = static_cast<int32_t>(pw_pct * (65536.0f / 100.0f) + 0.5f);
                if (m_ie_ext.oscPulseWidth < 1)
                {
                    m_ie_ext.oscPulseWidth = 1;
                }
                if (m_ie_ext.oscPulseWidth > 65535)
                {
                    m_ie_ext.oscPulseWidth = 65535;
                }
                MarkInstrumentEditorDirty(false);
                SyncInstrumentEditorOscillatorPreview();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                if (is_noise)
                {
                    ImGui::SetTooltip("Noise ignores Shape / Pulse Width (PWID).");
                }
                else if (is_pulse)
                {
                    ImGui::SetTooltip("Duty cycle: %% of the cycle that is high.");
                }
                else
                {
                    ImGui::SetTooltip(
                        "Phase morph / asymmetry (50%% = natural wave).\n"
                        "Same PWID field as Pulse Width. Subtle on Saw.");
                }
            }
            if (is_noise)
            {
                ImGui::EndDisabled();
            }
        }
        if (m_ie_ext.oscVolume < 0)
        {
            m_ie_ext.oscVolume = 0;
        }
        if (m_ie_ext.oscVolume > 65536)
        {
            m_ie_ext.oscVolume = 65536;
        }
        float osc_vol_pct = static_cast<float>(m_ie_ext.oscVolume) * (100.0f / 65536.0f);
        if (ImGui::SliderFloat("Oscillator Volume", &osc_vol_pct, 0.0f, 100.0f, "%.0f%%"))
        {
            m_ie_ext.oscVolume = static_cast<int32_t>(osc_vol_pct * (65536.0f / 100.0f) + 0.5f);
            if (m_ie_ext.oscVolume < 0)
            {
                m_ie_ext.oscVolume = 0;
            }
            if (m_ie_ext.oscVolume > 65536)
            {
                m_ie_ext.oscVolume = 65536;
            }
            MarkInstrumentEditorDirty(false);
            SyncInstrumentEditorOscillatorPreview();
        }
        ImGui::TextDisabled(
            "A4=440 Hz. Noise updates at note rate. Modulators (Modulation tab): "
            "PWID = Pulse Width (Pulse) or Shape (other waves; ignored by Noise); "
            "WIDX = wave index 0..5 (SINE TRIA SAWT SQUA PLSE NOIS). "
            "Beatnik levels: WIDX 100 ≈ full table; PWID 100 ≈ ±50%%.");
    }
#endif

    void DrawInstrumentEditorKeymapPage()
    {
#if USE_ZMF_SUPPORT == TRUE
        DrawInstrumentEditorOscillatorControls();
        ImGui::Spacing();
#endif
        if (!m_bank_token || m_ie_from_song)
        {
            ImGui::TextWrapped("Keymap split editing is available for bank instruments. Song-embedded instruments: use Volume/Filter/Modulation/Output pages.");
            return;
        }
        uint32_t split_count = 0;
        if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token, m_ie_instrument_index, &split_count) != BAE_NO_ERROR)
        {
            ImGui::TextDisabled("No splits");
            return;
        }
        if (ImGui::Button("Add split"))
        {
            if (EnsureActiveBankWritable())
            {
                if (BAERmfEditorBank_GrowInstrumentSampleSlots(m_bank_token,
                                                               m_ie_instrument_index,
                                                               split_count + 1) == BAE_NO_ERROR)
                {
                    SyncInstrumentEditorBankIndex();
                }
                RefreshListsFromBank();
                RefreshSongSamplesFromDocument();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete selected split") && m_ie_selected_split >= 0)
        {
            DeleteInstrumentSplitAt(static_cast<uint32_t>(m_ie_selected_split));
        }

        std::vector<std::pair<uint32_t, std::string>> snd_choices;
        CollectBankSndChoices(snd_choices);

        DrawKeymapZoneDiagram(split_count);

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
        if (ImGui::BeginTable("keymap",
                              6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit,
                              ImVec2(0, 320)))
        {
            ImGui::TableSetupColumn("Sel", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Low", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("High", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("Root", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Vol", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            const float split_row_h = ImGui::GetFrameHeightWithSpacing() + 4.0f;
            for (uint32_t s = 0; s < split_count; ++s)
            {
                BAERmfEditorBankSampleInfo info;
                std::memset(&info, 0, sizeof(info));
                if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token, m_ie_instrument_index, s, &info) != BAE_NO_ERROR)
                {
                    continue;
                }
                ImGui::TableNextRow(ImGuiTableRowFlags_None, split_row_h);
                ImGui::PushID(static_cast<int>(s));
                ImGui::TableSetColumnIndex(0);
                bool sel = (m_ie_selected_split == static_cast<int>(s));
                char sel_label[16];
                std::snprintf(sel_label, sizeof(sel_label), "%u", s);
                if (ImGui::Selectable(sel_label, sel, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    m_ie_selected_split = static_cast<int>(s);
                }
                const int sample_row = FindSampleRowForInstrumentSplit(
                    m_ie_instrument_index, s, static_cast<uint32_t>(info.sndResourceID));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && sample_row >= 0)
                {
                    m_selected_sample = sample_row;
#if NBEDITOR_MVP
                    OpenSampleEditorForSelection();
#endif
                }
                bool deleted_split = false;
                if (ImGui::BeginPopupContextItem("##split_sel_ctx"))
                {
                    m_ie_selected_split = static_cast<int>(s);
#if NBEDITOR_MVP
                    if (ImGui::MenuItem("Edit Sample", nullptr, false, sample_row >= 0))
                    {
                        m_selected_sample = sample_row;
                        OpenSampleEditorForSelection();
                    }
#endif
                    if (ImGui::MenuItem("Show in Samples", nullptr, false, sample_row >= 0))
                    {
                        RevealSampleInSamplesTab(sample_row);
                    }
                    if (ImGui::MenuItem("Export Sample...",
                                        nullptr,
                                        false,
                                        sample_row >= 0))
                    {
                        OpenExportSampleDialog(sample_row);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete Split"))
                    {
                        deleted_split = true;
                    }
                    ImGui::EndPopup();
                }
                if (deleted_split)
                {
                    ImGui::PopID();
                    DeleteInstrumentSplitAt(s);
                    break;
                }
                int low = info.lowKey, high = info.highKey, root = info.rootKey, vol = info.splitVolume;
                /* step=0 hides +/- (they crush 72px cells). Commit on deactivate so
                 * typing does not rebuild INST on every keystroke. */
                /* sampleRate==0 → INST-only replace (skip SND open/decompress).
                 * Root-in-SND still needs a real sampleRate so the SND baseKey updates. */
                auto live_patch_split = [this, s](const BAERmfEditorBankSampleInfo &split) {
                    m_ie_ext.hasSampleOverride = TRUE;
                    m_ie_ext.sampleOverrideIndex = s;
                    m_ie_ext.sampleRootKey = split.rootKey;
                    m_ie_ext.sampleLowKey = split.lowKey;
                    m_ie_ext.sampleHighKey = split.highKey;
                    m_ie_ext.sampleSplitVolume = split.splitVolume;
                    m_ie_ext.sampleRate = split.sampleRate;
                    m_ie_ext.sampleLoopStart = split.loopStart;
                    m_ie_ext.sampleLoopEnd = split.loopEnd;
                    LivePatchInstrumentEditor(false);
                    /* Ephemeral preview fields — do not bake into Apply/pristine. */
                    m_ie_ext.hasSampleOverride = FALSE;
                };
                auto commit_split = [&](bool may_touch_snd_root) {
                    /* XReplace fails on read-only banks; EnsureWritable first so
                     * INST Vol never falls into a multi-second full Replace. */
                    if (!EnsureActiveBankWritable())
                    {
                        return;
                    }
                    const uint32_t saved_rate = info.sampleRate;
                    if (!may_touch_snd_root)
                    {
                        info.sampleRate = 0;
                    }
                    if (BAERmfEditorBank_SetInstrumentSampleInfo(
                            m_bank_token, m_ie_instrument_index, s, &info) == BAE_NO_ERROR)
                    {
                        /* Flat INST shadow shifts GetIndexed order — without this,
                         * the next frame reads a different INST (often all Vol=192). */
                        SyncInstrumentEditorBankIndex();
                        m_ie_dirty = true;
                        info.sampleRate = saved_rate;
                        /* Soft-patch audition/playback so Root/Low/High/Vol are heard
                         * without waiting for Apply / instrument reload. */
                        live_patch_split(info);
                    }
                    else
                    {
                        info.sampleRate = saved_rate;
                    }
                };
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputInt("##low", &low, 0, 0);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    info.lowKey = static_cast<unsigned char>(std::clamp(low, 0, 127));
                    commit_split(false);
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputInt("##high", &high, 0, 0);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    info.highKey = static_cast<unsigned char>(std::clamp(high, 0, 127));
                    commit_split(false);
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputInt("##root", &root, 0, 0);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    info.rootKey = static_cast<unsigned char>(std::clamp(root, 0, 127));
                    /* Root lives in SND only when useSoundModifierAsRootKey is clear. */
                    const bool root_in_snd =
                        !TEST_FLAG_VALUE(m_ie_ext.flags2, ZBF_useSoundModifierAsRootKey);
                    commit_split(root_in_snd);
                }
                ImGui::TableSetColumnIndex(4);
                {
                    const uint32_t snd_id = static_cast<uint32_t>(
                        static_cast<uint16_t>(info.sndResourceID));
                    /* 0xFFFF = no sample (empty INST). ID 0 is valid; missing = blob gone. */
                    const bool snd_none = (snd_id == kNoSndResourceId);
                    const bool snd_missing = !snd_none && !BankSndResourceExists(snd_id);
                    char preview[96];
                    bool found_choice = false;
                    if (!snd_none && !snd_missing)
                    {
                        for (const auto &choice : snd_choices)
                        {
                            if (choice.first == snd_id)
                            {
                                std::snprintf(preview, sizeof(preview), "%s", choice.second.c_str());
                                found_choice = true;
                                break;
                            }
                        }
                    }
                    if (!found_choice)
                    {
                        if (snd_none)
                        {
                            std::snprintf(preview, sizeof(preview), "(none)");
                        }
                        else if (snd_missing)
                        {
                            std::snprintf(preview, sizeof(preview), "SND %u [missing]", snd_id);
                        }
                        else
                        {
                            std::snprintf(preview, sizeof(preview), "SND %u", snd_id);
                        }
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##snd", preview))
                    {
                        if (snd_missing)
                        {
                            ImGui::TextDisabled("Current: SND %u [missing]", snd_id);
                            ImGui::Separator();
                        }
                        for (const auto &choice : snd_choices)
                        {
                            const bool selected = (choice.first == snd_id);
                            if (ImGui::Selectable(choice.second.c_str(), selected))
                            {
                                if (EnsureActiveBankWritable())
                                {
                                    const BAEResult r =
                                        BAERmfEditorBank_SetInstrumentSampleSndID(
                                            m_bank_token,
                                            m_ie_instrument_index,
                                            s,
                                            static_cast<XShortResourceID>(choice.first));
                                    if (r == BAE_NO_ERROR)
                                    {
                                        SyncInstrumentEditorBankIndex();
                                        m_session_custom_snd_ids.insert(choice.first);
                                        m_ie_dirty = true;
                                        /* Keep sample backup in sync with the new pick. */
                                        CaptureInstrumentEditorSampleBackupIfPresent();
                                        RefreshListsFromBank();
                                        /* SND remap needs unload/reload on both the
                                         * audition keyboard and the live song preview. */
                                        LivePatchInstrumentEditor(true, true);
                                        SetStatus(std::string("Keysplit sample → SND ") +
                                                  std::to_string(choice.first));
                                    }
                                    else
                                    {
                                        SetStatus(std::string("Set sample failed: ") + FormatBAEError(r));
                                    }
                                }
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (snd_missing && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip(
                            "Sample SND %u is missing (deleted). Instrument still "
                            "references it — pick another sample to reassign.",
                            snd_id);
                    }
                }
                ImGui::TableSetColumnIndex(5);
                ImGui::SetNextItemWidth(-FLT_MIN);
                /* miscParameter2 is volume only when offset-start / sound-modifier
                 * are clear — otherwise editing "Vol" corrupts those fields. */
                const bool vol_editable = !TEST_FLAG_VALUE(m_ie_ext.flags2,
                                                           ZBF_enableSampleOffsetStart) &&
                                          !TEST_FLAG_VALUE(m_ie_ext.flags2,
                                                           ZBF_enableSoundModifier);
                if (vol_editable)
                {
                    ImGui::InputInt("##vol", &vol, 0, 0);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        info.splitVolume = static_cast<int16_t>(vol);
                        commit_split(false);
                    }
                }
                else
                {
                    ImGui::TextDisabled("—");
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::SetTooltip(
                            "Volume unavailable: this instrument uses sample-offset "
                            "or sound-modifier mode (miscParameter2 is not volume).");
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(2);
        ImGui::TextDisabled(
            "Sample column reassigns the keysplit. Right-click Sel for Edit Sample / Delete Split; double-click edits.");
    }
