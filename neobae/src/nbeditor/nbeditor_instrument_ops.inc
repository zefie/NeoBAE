/* Instrument alias / clone / move / copy-paste — included inside NbEditorApp
 * (after nbeditor_bank_add.inc). */

    enum class InstDestMode : int
    {
        Alias = 0,
        Clone = 1,
        Move = 2,
        Paste = 3,
    };

    int FindInstrumentIndexByInstId(uint32_t inst_id) const
    {
        uint32_t resolved = 0;
        uint32_t index = 0;
        if (m_bank_token &&
            BAERmfEditorBank_ResolveInstID(m_bank_token, inst_id, &resolved, &index) == BAE_NO_ERROR)
        {
            return static_cast<int>(index);
        }
        for (const InstrumentRow &row : m_instruments)
        {
            if (!row.is_alias && row.target_inst_id == inst_id)
            {
                return static_cast<int>(row.instrument_index);
            }
        }
        return -1;
    }

    bool IsSessionCustomInstId(uint32_t inst_id) const
    {
        if (m_session_custom_inst_ids.count(inst_id) != 0)
        {
            return true;
        }
        /* Bank 2+ slots are always treated as custom instruments. */
        return static_cast<int>(inst_id / 256u) >= 2;
    }

    void TrackCustomInstIdAfterDestOp(uint32_t source_id, uint32_t dest_id, bool is_move)
    {
        const bool source_custom = IsSessionCustomInstId(source_id);
        if (is_move)
        {
            m_session_custom_inst_ids.erase(source_id);
        }
        if (source_custom || static_cast<int>(dest_id / 256u) >= 2)
        {
            m_session_custom_inst_ids.insert(dest_id);
        }
    }

    void OpenInstDestDialog(InstDestMode mode, int list_index)
    {
        if (!m_bank_token)
        {
            SetStatus("No bank loaded");
            return;
        }
        m_inst_dest_clip_path.clear();
        m_inst_dest_clip_delete = false;
        m_inst_dest_clip_inst_count = 0;
        m_inst_dest_is_clip_package = false;

        if (mode != InstDestMode::Paste)
        {
            if (list_index < 0 || list_index >= static_cast<int>(m_instruments.size()))
            {
                return;
            }
            const InstrumentRow &src = m_instruments[static_cast<size_t>(list_index)];
            if (src.from_song_document)
            {
                SetStatus("Song-document instruments cannot be aliased/cloned in the bank");
                return;
            }
            if (mode == InstDestMode::Move && src.is_alias)
            {
                SetStatus("Cannot move an alias — remove it or move the source instrument");
                return;
            }
            m_inst_dest_source_index = src.instrument_index;
            m_inst_dest_source_inst_id = src.target_inst_id;
            m_inst_dest_source_display_id = src.inst_id;
            /* Move/Clone from GM banks default to Custom (Bank 2). If the source
             * is already custom (Bank 2+), keep its bank for in-place remaps. */
            if (mode == InstDestMode::Move || mode == InstDestMode::Clone)
            {
                m_inst_dest_bank = (src.bank >= 2) ? src.bank : 2;
            }
            else
            {
                m_inst_dest_bank = src.bank;
            }
            m_inst_dest_percussion = src.percussion;
        }
        else
        {
            if (!m_inst_clipboard_valid)
            {
                SetStatus("Instrument clipboard is empty");
                return;
            }
            m_inst_dest_source_index = 0;
            m_inst_dest_source_inst_id = m_inst_clipboard_inst_id;
            m_inst_dest_source_display_id = m_inst_clipboard_inst_id;
            /* Default to the source bank:program (user may remap to another free slot). */
            DecodeInstSlot(m_inst_clipboard_inst_id,
                           &m_inst_dest_bank,
                           &m_inst_dest_percussion,
                           &m_inst_dest_program);
        }

        if (mode != InstDestMode::Paste)
        {
            const int free_prog =
                FirstFreeProgramForGroup(m_inst_dest_bank, m_inst_dest_percussion);
            m_inst_dest_program = free_prog >= 0 ? free_prog : 0;
        }
        m_inst_dest_deep_clone = false;
        m_inst_dest_mode = mode;
        m_inst_dest_open = true;
    }

    void OpenInstDestDialogForClipPackage(const std::string &path,
                                          bool delete_after,
                                          uint32_t source_inst_id,
                                          int inst_count)
    {
        if (!m_bank_token && !LoadBuiltinBankAsActive())
        {
            SetStatus("No bank loaded");
            return;
        }
        if (path.empty())
        {
            SetStatus("Instrument clipboard is empty");
            return;
        }
        m_inst_dest_clip_path = path;
        m_inst_dest_clip_delete = delete_after;
        m_inst_dest_clip_inst_count = inst_count > 0 ? inst_count : 1;
        m_inst_dest_is_clip_package = true;
        m_inst_dest_source_index = 0;
        m_inst_dest_source_inst_id = source_inst_id;
        m_inst_dest_source_display_id = source_inst_id;
        DecodeInstSlot(source_inst_id,
                       &m_inst_dest_bank,
                       &m_inst_dest_percussion,
                       &m_inst_dest_program);
        m_inst_dest_deep_clone = false;
        m_inst_dest_mode = InstDestMode::Paste;
        m_inst_dest_open = true;
    }

    void CopyInstrumentAt(int list_index)
    {
        if (list_index < 0 || list_index >= static_cast<int>(m_instruments.size()))
        {
            return;
        }
        const InstrumentRow &src = m_instruments[static_cast<size_t>(list_index)];
        if (src.from_song_document)
        {
            SetStatus("Cannot copy a song-document instrument to the bank clipboard");
            return;
        }
        m_inst_clipboard_valid = true;
        m_inst_clipboard_inst_id = src.target_inst_id;
        m_inst_clipboard_name = src.name;
        char status[192];
        std::snprintf(status,
                      sizeof(status),
                      "Copied instrument %s (instID %u)",
                      src.name.c_str(),
                      src.target_inst_id);
        SetStatus(status);
    }

    void PerformInstDestAction()
    {
        if (!EnsureActiveBankWritable())
        {
            return;
        }

        const uint32_t dest_id =
            BankInstIdFromSlot(m_inst_dest_bank, m_inst_dest_percussion, m_inst_dest_program);
        bool used_programs[128];
        CollectUsedProgramsForGroup(m_inst_dest_bank, m_inst_dest_percussion, used_programs);
        if (used_programs[m_inst_dest_program])
        {
            SetStatus("Destination instrument slot is already in use");
            return;
        }

        /* Cross-process / OS clip package paste: remap INST + colliding SNDs. */
        if (m_inst_dest_mode == InstDestMode::Paste && m_inst_dest_is_clip_package)
        {
            const std::string path = m_inst_dest_clip_path;
            const bool delete_after = m_inst_dest_clip_delete;
            if (!RemapAndImportInstrumentClip(path, dest_id, delete_after))
            {
                return;
            }
            m_inst_dest_open = false;
            m_inst_dest_clip_path.clear();
            m_inst_dest_is_clip_package = false;
            m_inst_dest_clip_delete = false;
            m_inst_dest_clip_inst_count = 0;
            MarkBank01TouchedIfNeeded(m_inst_dest_bank);
            return;
        }

        int src_index = FindInstrumentIndexByInstId(m_inst_dest_source_inst_id);
        if (src_index < 0)
        {
            SetStatus("Source instrument no longer found in bank");
            m_inst_dest_open = false;
            return;
        }

        BAEResult r = BAE_NO_ERROR;
        char status[256];
        const uint32_t source_id = m_inst_dest_source_inst_id;
        switch (m_inst_dest_mode)
        {
        case InstDestMode::Alias:
            r = BAERmfEditorBank_AliasInstrument(m_bank_token,
                                                 static_cast<uint32_t>(src_index),
                                                 dest_id);
            if (r == BAE_NO_ERROR)
            {
                std::snprintf(status, sizeof(status), "Created alias instID %u", dest_id);
                SetStatus(status);
            }
            break;
        case InstDestMode::Clone:
        case InstDestMode::Paste:
            r = BAERmfEditorBank_CloneInstrument(m_bank_token,
                                                 static_cast<uint32_t>(src_index),
                                                 dest_id,
                                                 m_inst_dest_deep_clone ? TRUE : FALSE);
            if (r == BAE_NO_ERROR)
            {
                TrackCustomInstIdAfterDestOp(source_id, dest_id, false);
                std::snprintf(status,
                              sizeof(status),
                              "%s instrument to instID %u (%s)",
                              m_inst_dest_mode == InstDestMode::Paste ? "Pasted" : "Cloned",
                              dest_id,
                              m_inst_dest_deep_clone ? "deep" : "shared samples");
                SetStatus(status);
            }
            break;
        case InstDestMode::Move:
        {
            /* Clone first; finish source slot after song-remap is decided.
             * Bank 0/1 sources are deep-cloned so restoring the pristine GM
             * slot cannot clobber samples still shared with the new custom. */
            const bool deep_for_bank01 = (static_cast<int>(source_id / 256u) < 2);
            r = BAERmfEditorBank_CloneInstrument(m_bank_token,
                                                 static_cast<uint32_t>(src_index),
                                                 dest_id,
                                                 deep_for_bank01 ? TRUE : FALSE);
            if (r == BAE_NO_ERROR)
            {
                TrackCustomInstIdAfterDestOp(source_id, dest_id, false);
                m_inst_move_pending_delete_source_id = source_id;
                m_inst_move_pending_dest_id = dest_id;
                std::snprintf(status, sizeof(status), "Moved instrument to instID %u", dest_id);
                SetStatus(status);
            }
            break;
        }
        }

        if (r != BAE_NO_ERROR)
        {
            SetStatus(std::string("Instrument operation failed: ") + FormatBAEError(r));
            return;
        }

        const InstDestMode completed_mode = m_inst_dest_mode;
        int src_bank = 0;
        bool src_perc = false;
        int src_prog = 0;
        DecodeInstSlot(source_id, &src_bank, &src_perc, &src_prog);

        m_inst_dest_open = false;
        MarkBank01TouchedIfNeeded(m_inst_dest_bank);
        MarkBank01TouchedIfNeeded(src_bank);
        RefreshListsFromBank();
        EnsureInstrumentFilterShows(m_inst_dest_bank, m_inst_dest_percussion);
        SelectInstrumentByInstId(dest_id);
        if (completed_mode == InstDestMode::Alias)
        {
            MarkPlaybackStale();
            (void)ReloadPlaybackPreservingTransport();
        }
        if (completed_mode == InstDestMode::Move ||
            completed_mode == InstDestMode::Clone ||
            completed_mode == InstDestMode::Paste)
        {
            RefreshAuditionInstrumentLoad(dest_id, dest_id);
        }

        /* Move always; Clone when landing in custom (Bank 2+) — typical
         * "promote GM patch to custom and retarget the song" workflow. */
        const bool offer_song_remap =
            (completed_mode == InstDestMode::Move) ||
            (completed_mode == InstDestMode::Clone && m_inst_dest_bank >= 2);
        if (offer_song_remap)
        {
            const bool dialog_opened =
                OfferSongRemapAfterInstrumentMove(src_bank,
                                                  src_prog,
                                                  src_perc,
                                                  m_inst_dest_bank,
                                                  m_inst_dest_program,
                                                  m_inst_dest_percussion);
            if (completed_mode == InstDestMode::Move && !dialog_opened)
            {
                /* No song notes use the old slot — finish the move now. */
                FinishPendingInstrumentMove(true);
            }
        }
        else if (completed_mode == InstDestMode::Move)
        {
            FinishPendingInstrumentMove(true);
        }
    }

    /* Re-import one Bank 0/1 INST (+ its SNDs) from the loaded bank file or
     * built-in patches — used when moving an override off a GM program slot. */
    bool RestoreBank01SlotFromPristine(uint32_t inst_id)
    {
        if (!m_bank_token || static_cast<int>(inst_id / 256u) >= 2)
        {
            return false;
        }
        if (!EnsureActiveBankWritable())
        {
            return false;
        }

        XFILE donor = nullptr;
        BAEBankToken builtin_token = 0;
        bool close_file = false;
        bool unload_builtin = false;

        if (!m_loaded_bank_path.empty())
        {
            XFILENAME xname;
            XConvertPathToXFILENAME(const_cast<char *>(m_loaded_bank_path.c_str()), &xname);
            donor = XFileOpenResource(&xname, TRUE);
            close_file = (donor != nullptr);
        }
        if (!donor)
        {
            if (!LoadBuiltinBankToMixer(m_mixer, &builtin_token) || builtin_token == 0)
            {
                return false;
            }
            donor = reinterpret_cast<XFILE>(builtin_token);
            unload_builtin = true;
        }

        std::vector<XShortResourceID> snd_ids;
        if (!CollectSndIdsForInstInFile(donor, inst_id, snd_ids))
        {
            if (close_file)
            {
                XFileClose(donor);
            }
            if (unload_builtin)
            {
                (void)BAEMixer_UnloadBank(m_mixer, builtin_token);
            }
            return false;
        }

        const XShortResourceID inst16 = static_cast<XShortResourceID>(inst_id);
        const BAEResult import_r = BAERmfEditorBank_ImportInstAndSndFromFile(
            m_bank_token,
            donor,
            &inst16,
            1,
            snd_ids.empty() ? nullptr : snd_ids.data(),
            static_cast<uint32_t>(snd_ids.size()));

        if (close_file)
        {
            XFileClose(donor);
        }
        if (unload_builtin)
        {
            (void)BAEMixer_UnloadBank(m_mixer, builtin_token);
        }

        if (import_r != BAE_NO_ERROR)
        {
            return false;
        }
        m_session_custom_inst_ids.erase(inst_id);
        /* Restored pristine GM content for this slot; SNDs from the donor are
         * not session-custom. */
        for (XShortResourceID snd : snd_ids)
        {
            m_session_custom_snd_ids.erase(static_cast<uint32_t>(
                static_cast<uint16_t>(snd)));
        }
        return true;
    }

    bool FinishPendingInstrumentMove(bool delete_source)
    {
        const uint32_t source_id = m_inst_move_pending_delete_source_id;
        const uint32_t dest_id = m_inst_move_pending_dest_id;
        if (source_id == 0)
        {
            return true;
        }
        m_inst_move_pending_delete_source_id = 0;
        m_inst_move_pending_dest_id = 0;

        if (!delete_source)
        {
            SetStatus("Cloned to new slot; original kept (song notes not updated)");
            return true;
        }

        if (!m_bank_token || !EnsureActiveBankWritable())
        {
            SetStatus("Move incomplete: could not remove original instrument");
            return false;
        }

        /* Drop stale song embeds so Session save cannot resurrect this INST. */
        StripInstrumentFromSessionSongs(source_id);

        const int source_bank = static_cast<int>(source_id / 256u);
        bool restored_or_deleted = false;
        if (source_bank < 2)
        {
            /* Moving an override/custom off Bank 0/1: put the bank's own
             * program back instead of leaving a hole. */
            if (RestoreBank01SlotFromPristine(source_id))
            {
                restored_or_deleted = true;
            }
        }

        if (!restored_or_deleted)
        {
            const int del_index = FindInstrumentIndexByInstId(source_id);
            if (del_index < 0)
            {
                m_session_custom_inst_ids.erase(source_id);
            }
            else
            {
                const BAEResult r = BAERmfEditorBank_DeleteInstrument(
                    m_bank_token, static_cast<uint32_t>(del_index));
                if (r != BAE_NO_ERROR)
                {
                    SetStatus(std::string("Move incomplete: failed to remove original: ") +
                              FormatBAEError(r));
                    return false;
                }
                m_session_custom_inst_ids.erase(source_id);
                MarkBank01TouchedIfNeeded(source_bank);
            }
        }

        /* Rebuild bank list, then re-scan song embeds so restored GM slots are
         * not still tagged (Override)/custom from the pre-strip override cache. */
        m_song_override_instruments.clear();
        RefreshListsFromBank();
        RefreshSongOverridesFromDocument();
        RefreshAuditionInstrumentLoad(source_id, dest_id != 0 ? dest_id : source_id);
        return true;
    }

    /* Remap declined: still finish the move. Notes keep the old slot. */
    void FinishPendingInstrumentMoveLeavingNotes(void)
    {
        if (m_inst_move_pending_delete_source_id == 0)
        {
            return;
        }
        if (FinishPendingInstrumentMove(true))
        {
            SetStatus("Moved instrument; song notes left on original slot");
        }
    }

    void StripInstrumentFromSessionSongs(uint32_t inst_id)
    {
        /* INST id 0 is valid (B0P000); never treat as unset. */
        (void)CommitActiveSessionSong();

        auto strip_doc = [inst_id](BAERmfEditorDocument *doc) {
            if (!doc)
            {
                return;
            }
            (void)BAERmfEditorDocument_DeleteInstrument(doc, inst_id);
        };

        if (!m_session_songs.empty())
        {
            for (size_t i = 0; i < m_session_songs.size(); ++i)
            {
                SessionSongEntry &entry = m_session_songs[i];
                const bool is_active =
                    m_document && m_session_song_index == static_cast<int>(i);
                if (is_active)
                {
                    strip_doc(m_document);
                    m_document_dirty = true;
                    (void)CommitActiveSessionSong();
                    continue;
                }
                BAERmfEditorDocument *doc = LoadSessionSongDocumentTemp(entry);
                if (!doc)
                {
                    continue;
                }
                strip_doc(doc);
                (void)PersistDocumentToSessionSongEntry(doc, entry);
                BAERmfEditorDocument_Delete(doc);
            }
        }
        else if (m_document)
        {
            strip_doc(m_document);
            m_document_dirty = true;
            (void)CommitActiveSessionSong();
        }
    }

    /* Match CollectCurrentSongUsedInstrumentKeys: melodic = bank+program on
     * non-CH10; percussion kit = bank + note number on CH10. */
    static bool NoteUsesInstSlot(const BAERmfEditorNoteInfo &note,
                                 int bank_group,
                                 int program,
                                 bool percussion)
    {
        if (ExportBankGroupFromNoteBank(note.bank) != bank_group)
        {
            return false;
        }
        if (percussion)
        {
            return note.channel == 9 && static_cast<int>(note.note) == program;
        }
        return note.channel != 9 && static_cast<int>(note.program) == program;
    }

    static uint16_t RemapNoteBankPreserveForm(uint16_t old_bank, int new_bank_group)
    {
        if (old_bank < 128)
        {
            return static_cast<uint16_t>(std::clamp(new_bank_group, 0, 127));
        }
        return InternalMidiBankFromGroup(new_bank_group);
    }

    static uint32_t CountNotesUsingInstSlot(BAERmfEditorDocument *doc,
                                            int bank_group,
                                            int program,
                                            bool percussion)
    {
        if (!doc)
        {
            return 0;
        }
        uint32_t count = 0;
        uint16_t track_count = 0;
        if (BAERmfEditorDocument_GetTrackCount(doc, &track_count) != BAE_NO_ERROR)
        {
            return 0;
        }
        for (uint16_t t = 0; t < track_count; ++t)
        {
            uint32_t note_count = 0;
            if (BAERmfEditorDocument_GetNoteCount(doc, t, &note_count) != BAE_NO_ERROR)
            {
                continue;
            }
            for (uint32_t n = 0; n < note_count; ++n)
            {
                BAERmfEditorNoteInfo note;
                std::memset(&note, 0, sizeof(note));
                if (BAERmfEditorDocument_GetNoteInfo(doc, t, n, &note) != BAE_NO_ERROR)
                {
                    continue;
                }
                if (NoteUsesInstSlot(note, bank_group, program, percussion))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    BAERmfEditorDocument *LoadSessionSongDocumentTemp(const SessionSongEntry &entry) const
    {
        if (!entry.rmf_bytes.empty())
        {
            BAERmfEditorDocument *doc = BAERmfEditorDocument_LoadFromMemory(
                entry.rmf_bytes.data(),
                static_cast<uint32_t>(entry.rmf_bytes.size()),
                BAE_INVALID_TYPE);
            if (doc)
            {
                return doc;
            }
        }
        if (!entry.midi_bytes.empty())
        {
            return BAERmfEditorDocument_LoadFromMemory(
                entry.midi_bytes.data(),
                static_cast<uint32_t>(entry.midi_bytes.size()),
                BAE_MIDI_TYPE);
        }
        return nullptr;
    }

    bool PersistDocumentToSessionSongEntry(BAERmfEditorDocument *doc, SessionSongEntry &entry)
    {
        if (!doc)
        {
            return false;
        }
        unsigned char *midi_data = nullptr;
        uint32_t midi_size = 0;
        if (BAERmfEditorDocument_SaveAsMidiToMemory(doc, &midi_data, &midi_size) != BAE_NO_ERROR ||
            !midi_data || midi_size == 0)
        {
            if (midi_data)
            {
                XDisposePtr(midi_data);
            }
            return false;
        }
        entry.midi_bytes.assign(midi_data, midi_data + midi_size);
        XDisposePtr(midi_data);

        unsigned char *rmf_data = nullptr;
        uint32_t rmf_size = 0;
        uint32_t zmf_reason = 0;
        const bool use_zmf = BAERmfEditorDocument_RequiresZmf(doc, &zmf_reason) != FALSE;
        if (BAERmfEditorDocument_SaveAsRmfToMemoryEx(doc, use_zmf, FALSE, &rmf_data, &rmf_size) ==
                BAE_NO_ERROR &&
            rmf_data && rmf_size > 0)
        {
            entry.rmf_bytes.assign(rmf_data, rmf_data + rmf_size);
        }
        if (rmf_data)
        {
            XDisposePtr(rmf_data);
        }
        return true;
    }

    uint32_t CountSessionNotesUsingInstSlot(int bank_group, int program, bool percussion)
    {
        (void)CommitActiveSessionSong();

        uint32_t total = 0;
        if (!m_session_songs.empty())
        {
            for (const SessionSongEntry &entry : m_session_songs)
            {
                BAERmfEditorDocument *doc = nullptr;
                bool owned = false;
                if (m_document &&
                    m_session_song_index >= 0 &&
                    m_session_song_index < static_cast<int>(m_session_songs.size()) &&
                    &entry == &m_session_songs[static_cast<size_t>(m_session_song_index)])
                {
                    doc = m_document;
                }
                else
                {
                    doc = LoadSessionSongDocumentTemp(entry);
                    owned = (doc != nullptr);
                }
                if (!doc)
                {
                    continue;
                }
                total += CountNotesUsingInstSlot(doc, bank_group, program, percussion);
                if (owned)
                {
                    BAERmfEditorDocument_Delete(doc);
                }
            }
            return total;
        }

        /* Song open but not yet committed into the session list. */
        return CountNotesUsingInstSlot(m_document, bank_group, program, percussion);
    }

    bool RemapDocumentInstSlot(BAERmfEditorDocument *doc,
                               int old_bank,
                               int old_program,
                               bool old_percussion,
                               int new_bank,
                               int new_program,
                               bool new_percussion)
    {
        if (!doc)
        {
            return false;
        }

        /* Same-type melodic: engine remap covers notes, track defaults, aux PC/bank. */
        if (!old_percussion && !new_percussion)
        {
            const BAEResult r = BAERmfEditorDocument_RemapInstrumentReferences(
                doc,
                InternalMidiBankFromGroup(old_bank),
                static_cast<unsigned char>(std::clamp(old_program, 0, 127)),
                InternalMidiBankFromGroup(new_bank),
                static_cast<unsigned char>(std::clamp(new_program, 0, 127)));
            return r == BAE_NO_ERROR;
        }

        /* Percussion kit (and cross-type): rewrite matching note-ons by note number. */
        uint16_t track_count = 0;
        if (BAERmfEditorDocument_GetTrackCount(doc, &track_count) != BAE_NO_ERROR)
        {
            return false;
        }
        for (uint16_t t = 0; t < track_count; ++t)
        {
            /* Defaults only — SetTrackInfo would rewrite note bank/program and
             * desync kit hits that key off note number. */
            BAERmfEditorTrackInfo track{};
            if (BAERmfEditorDocument_GetTrackInfo(doc, t, &track) == BAE_NO_ERROR &&
                old_percussion && track.channel == 9 &&
                ExportBankGroupFromNoteBank(track.bank) == old_bank &&
                static_cast<int>(track.program) == old_program)
            {
                (void)BAERmfEditorDocument_SetTrackDefaultInstrument(
                    doc,
                    t,
                    RemapNoteBankPreserveForm(track.bank, new_bank),
                    static_cast<unsigned char>(std::clamp(new_program, 0, 127)));
            }

            uint32_t note_count = 0;
            if (BAERmfEditorDocument_GetNoteCount(doc, t, &note_count) != BAE_NO_ERROR)
            {
                continue;
            }
            for (uint32_t n = 0; n < note_count; ++n)
            {
                BAERmfEditorNoteInfo note;
                std::memset(&note, 0, sizeof(note));
                if (BAERmfEditorDocument_GetNoteInfo(doc, t, n, &note) != BAE_NO_ERROR)
                {
                    continue;
                }
                if (!NoteUsesInstSlot(note, old_bank, old_program, old_percussion))
                {
                    continue;
                }
                note.bank = RemapNoteBankPreserveForm(note.bank, new_bank);
                if (new_percussion)
                {
                    note.note = static_cast<unsigned char>(std::clamp(new_program, 0, 127));
                    note.channel = 9;
                }
                else
                {
                    note.program = static_cast<unsigned char>(std::clamp(new_program, 0, 127));
                    if (note.channel == 9)
                    {
                        note.channel = 0;
                    }
                }
                (void)BAERmfEditorDocument_SetNoteInfo(doc, t, n, &note);
            }
        }
        return true;
    }

    bool ApplySongRemapAfterInstrumentMove()
    {
        (void)CommitActiveSessionSong();

        bool any = false;
        if (!m_session_songs.empty())
        {
            for (size_t i = 0; i < m_session_songs.size(); ++i)
            {
                SessionSongEntry &entry = m_session_songs[i];
                const bool is_active =
                    m_document &&
                    m_session_song_index == static_cast<int>(i);

                BAERmfEditorDocument *doc = is_active ? m_document
                                                     : LoadSessionSongDocumentTemp(entry);
                if (!doc)
                {
                    continue;
                }
                if (RemapDocumentInstSlot(doc,
                                          m_inst_move_remap_old_bank,
                                          m_inst_move_remap_old_program,
                                          m_inst_move_remap_old_percussion,
                                          m_inst_move_remap_new_bank,
                                          m_inst_move_remap_new_program,
                                          m_inst_move_remap_new_percussion))
                {
                    any = true;
                }
                if (is_active)
                {
                    m_document_dirty = true;
                    MeInvalidateMidiDocCaches();
                    (void)CommitActiveSessionSong();
                }
                else
                {
                    (void)PersistDocumentToSessionSongEntry(doc, entry);
                    BAERmfEditorDocument_Delete(doc);
                }
            }
        }
        else if (m_document)
        {
            if (RemapDocumentInstSlot(m_document,
                                      m_inst_move_remap_old_bank,
                                      m_inst_move_remap_old_program,
                                      m_inst_move_remap_old_percussion,
                                      m_inst_move_remap_new_bank,
                                      m_inst_move_remap_new_program,
                                      m_inst_move_remap_new_percussion))
            {
                any = true;
                m_document_dirty = true;
                MeInvalidateMidiDocCaches();
                (void)CommitActiveSessionSong();
            }
        }

        if (any && m_document)
        {
            (void)ReloadPlaybackFromDocument(true);
        }
        return any;
    }

    /* Returns true if the confirm dialog was opened (caller should wait). */
    bool OfferSongRemapAfterInstrumentMove(int old_bank,
                                           int old_program,
                                           bool old_percussion,
                                           int new_bank,
                                           int new_program,
                                           bool new_percussion)
    {
        if (old_bank == new_bank && old_program == new_program &&
            old_percussion == new_percussion)
        {
            return false;
        }
        if (!m_document && m_session_songs.empty())
        {
            return false;
        }

        const uint32_t note_count =
            CountSessionNotesUsingInstSlot(old_bank, old_program, old_percussion);
        if (note_count == 0)
        {
            return false;
        }

        m_inst_move_remap_note_count = note_count;
        m_inst_move_remap_old_bank = old_bank;
        m_inst_move_remap_old_program = old_program;
        m_inst_move_remap_old_percussion = old_percussion;
        m_inst_move_remap_new_bank = new_bank;
        m_inst_move_remap_new_program = new_program;
        m_inst_move_remap_new_percussion = new_percussion;
        m_inst_move_remap_confirm_open = true;
        return true;
    }

    void DrawInstMoveSongRemapConfirmDialog()
    {
        if (m_inst_move_remap_confirm_open)
        {
            ImGui::OpenPopup("Update Song Notes?");
        }
        bool popup_open = m_inst_move_remap_confirm_open;
        if (!ImGui::BeginPopupModal("Update Song Notes?",
                                    &popup_open,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            /* Closed via title-bar X: still finish the move; notes stay. */
            if (m_inst_move_remap_confirm_open && !popup_open)
            {
                m_inst_move_remap_confirm_open = false;
                FinishPendingInstrumentMoveLeavingNotes();
            }
            return;
        }
        if (!popup_open)
        {
            m_inst_move_remap_confirm_open = false;
            FinishPendingInstrumentMoveLeavingNotes();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextWrapped("%u note%s use %d:%d, would you like to update them to %d:%d?",
                           m_inst_move_remap_note_count,
                           m_inst_move_remap_note_count == 1 ? "" : "s",
                           m_inst_move_remap_old_bank,
                           m_inst_move_remap_old_program,
                           m_inst_move_remap_new_bank,
                           m_inst_move_remap_new_program);
        if (m_inst_move_remap_old_percussion || m_inst_move_remap_new_percussion)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Percussion slots match by drum note number on channel 10.");
        }
        ImGui::Separator();
        if (ImGui::Button("Yes", ImVec2(120.0f, 0.0f)))
        {
            /* Defer heavy remap/reload until after the modal closes. */
            m_inst_move_remap_pending_apply = true;
            m_inst_move_remap_confirm_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120.0f, 0.0f)))
        {
            FinishPendingInstrumentMoveLeavingNotes();
            m_inst_move_remap_confirm_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void PollInstMoveSongRemapPendingApply()
    {
        if (!m_inst_move_remap_pending_apply)
        {
            return;
        }
        m_inst_move_remap_pending_apply = false;

        const uint32_t n = m_inst_move_remap_note_count;
        const int ob = m_inst_move_remap_old_bank;
        const int op = m_inst_move_remap_old_program;
        const int nb = m_inst_move_remap_new_bank;
        const int np = m_inst_move_remap_new_program;
        if (ApplySongRemapAfterInstrumentMove())
        {
            (void)FinishPendingInstrumentMove(true);
            char status[192];
            std::snprintf(status,
                          sizeof(status),
                          "Updated %u note%s from %d:%d to %d:%d",
                          n,
                          n == 1 ? "" : "s",
                          ob,
                          op,
                          nb,
                          np);
            SetStatus(status);
        }
        else
        {
            SetStatus("Song note update failed");
            /* Leave original in place if remap failed. */
            if (m_inst_move_pending_delete_source_id != 0)
            {
                (void)FinishPendingInstrumentMove(false);
            }
        }
    }

    void RemoveAliasAt(int list_index)
    {
        if (list_index < 0 || list_index >= static_cast<int>(m_instruments.size()))
        {
            return;
        }
        const InstrumentRow &row = m_instruments[static_cast<size_t>(list_index)];
        if (!row.is_alias)
        {
            SetStatus("Selected instrument is not an alias");
            return;
        }
        if (!EnsureActiveBankWritable())
        {
            return;
        }
        const BAEResult r = BAERmfEditorBank_DeleteAlias(m_bank_token, row.inst_id);
        if (r != BAE_NO_ERROR)
        {
            SetStatus(std::string("Remove alias failed: ") + FormatBAEError(r));
            return;
        }
        char status[128];
        std::snprintf(status, sizeof(status), "Removed alias instID %u", row.inst_id);
        SetStatus(status);
        RefreshListsFromBank();
        MarkPlaybackStale();
        (void)ReloadPlaybackPreservingTransport();
    }

    void DeleteInstrumentAt(int list_index)
    {
        if (list_index < 0 || list_index >= static_cast<int>(m_instruments.size()))
        {
            return;
        }
        const InstrumentRow &row = m_instruments[static_cast<size_t>(list_index)];
        if (row.from_song_document || row.has_song_override)
        {
            if (m_document)
            {
                (void)BAERmfEditorDocument_DeleteInstrument(m_document, row.inst_id);
                (void)BAERmfEditorDocument_DeleteInstrument(m_document, row.target_inst_id);
                m_session_custom_inst_ids.erase(row.inst_id);
                m_session_custom_inst_ids.erase(row.target_inst_id);
                RefreshListsFromBank();
                SetStatus(std::string("Deleted song instrument ") + row.name);
                return;
            }
            SetStatus("Cannot delete a song-document instrument");
            return;
        }
        if (row.is_alias)
        {
            RemoveAliasAt(list_index);
            return;
        }
        if (!EnsureActiveBankWritable())
        {
            return;
        }
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        const XLongResourceID iid = static_cast<XLongResourceID>(row.inst_id);
        if (XTrashFileResource(bank, ID_INST, iid) == FALSE)
        {
            /* Fallback for edge cases where ZTRS trash rebuild fails. */
            const BAEResult r =
                BAERmfEditorBank_DeleteInstrument(m_bank_token, row.instrument_index);
            if (r != BAE_NO_ERROR)
            {
                SetStatus(std::string("Move instrument to Trash failed: ") + FormatBAEError(r));
                return;
            }
            char status[128];
            std::snprintf(status, sizeof(status), "Deleted instrument instID %u", row.inst_id);
            SetStatus(status);
            RefreshListsFromBank();
            return;
        }
        m_session_custom_inst_ids.erase(row.inst_id);
        char status[128];
        std::snprintf(status, sizeof(status), "Moved instrument instID %u to Trash", row.inst_id);
        SetStatus(status);
        RefreshListsFromBank();
    }

    void DrawInstDestDialog()
    {
        if (!m_inst_dest_open)
        {
            return;
        }
        const char *title = "Instrument Destination";
        switch (m_inst_dest_mode)
        {
        case InstDestMode::Alias:
            title = "Create Alias…";
            break;
        case InstDestMode::Clone:
            title = "Clone Instrument…";
            break;
        case InstDestMode::Move:
            title = "Move Instrument…";
            break;
        case InstDestMode::Paste:
            title = "Paste Instrument…";
            break;
        }
        ImGui::OpenPopup(title);
        bool popup_open = m_inst_dest_open;
        if (!ImGui::BeginPopupModal(title, &popup_open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!popup_open)
            {
                m_inst_dest_open = false;
                m_inst_dest_clip_path.clear();
                m_inst_dest_is_clip_package = false;
            }
            return;
        }
        if (!popup_open)
        {
            m_inst_dest_open = false;
            m_inst_dest_clip_path.clear();
            m_inst_dest_is_clip_package = false;
            ImGui::EndPopup();
            return;
        }

        if (m_inst_dest_mode == InstDestMode::Paste)
        {
            int src_bank = 0;
            bool src_perc = false;
            int src_prog = 0;
            DecodeInstSlot(m_inst_dest_source_display_id, &src_bank, &src_perc, &src_prog);
            ImGui::Text("Source slot: B%d%s P%03d (instID %u)",
                        src_bank,
                        src_perc ? " Perc" : "",
                        src_prog,
                        m_inst_dest_source_display_id);
            if (m_inst_dest_is_clip_package && m_inst_dest_clip_inst_count > 1)
            {
                ImGui::TextDisabled(
                    "%d instruments in clipboard — first → selected slot; others → next free.",
                    m_inst_dest_clip_inst_count);
            }
            ImGui::Separator();
        }

        ImGui::Text("Destination slot");
        const int prev_bank = m_inst_dest_bank;
        const bool prev_perc = m_inst_dest_percussion;
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("Bank", &m_inst_dest_bank);
        m_inst_dest_bank = std::clamp(m_inst_dest_bank, 0, 127);
        if ((m_inst_dest_mode == InstDestMode::Move ||
             m_inst_dest_mode == InstDestMode::Clone) &&
            m_inst_dest_bank != 2)
        {
            ImGui::TextDisabled("Defaults to Custom (Bank 2); other banks allowed here.");
        }
        ImGui::Checkbox("Percussion", &m_inst_dest_percussion);
        if (m_inst_dest_bank != prev_bank || m_inst_dest_percussion != prev_perc)
        {
            const int free_prog = FirstFreeProgramForGroup(m_inst_dest_bank, m_inst_dest_percussion);
            if (free_prog >= 0)
            {
                m_inst_dest_program = free_prog;
            }
        }
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("Program", &m_inst_dest_program);
        m_inst_dest_program = std::clamp(m_inst_dest_program, 0, 127);

        bool used[128];
        CollectUsedProgramsForGroup(m_inst_dest_bank, m_inst_dest_percussion, used);
        const uint32_t dest_id =
            BankInstIdFromSlot(m_inst_dest_bank, m_inst_dest_percussion, m_inst_dest_program);
        if (used[m_inst_dest_program])
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "in use");
            ImGui::TextDisabled("Choose a free program slot — Apply is disabled while occupied.");
        }
        else
        {
            ImGui::SameLine();
            ImGui::TextDisabled("instID %u", dest_id);
        }

        if ((m_inst_dest_mode == InstDestMode::Clone || m_inst_dest_mode == InstDestMode::Paste) &&
            !m_inst_dest_is_clip_package)
        {
            ImGui::Separator();
            ImGui::Checkbox("Deep clone (duplicate samples)", &m_inst_dest_deep_clone);
            if (!m_inst_dest_deep_clone)
            {
                ImGui::TextDisabled("Shares sample resources with the source.");
            }
        }
        else if (m_inst_dest_is_clip_package)
        {
            ImGui::Separator();
            ImGui::TextDisabled("Samples with IDs already in use are remapped to free SND IDs.");
        }

        ImGui::Separator();
        const char *ok_label = "OK";
        switch (m_inst_dest_mode)
        {
        case InstDestMode::Alias:
            ok_label = "Create Alias";
            break;
        case InstDestMode::Clone:
            ok_label = "Clone";
            break;
        case InstDestMode::Move:
            ok_label = "Move";
            break;
        case InstDestMode::Paste:
            ok_label = "Paste";
            break;
        }
        const bool can_apply = !used[m_inst_dest_program] && m_bank_token != 0;
        if (!can_apply)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(ok_label, ImVec2(140.0f, 0.0f)))
        {
            PerformInstDestAction();
            if (!m_inst_dest_open)
            {
                ImGui::CloseCurrentPopup();
            }
        }
        if (!can_apply)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_inst_dest_open = false;
            m_inst_dest_clip_path.clear();
            m_inst_dest_is_clip_package = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
