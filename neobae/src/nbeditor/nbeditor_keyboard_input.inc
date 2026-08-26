/* Shared keyboard / hardware-MIDI dispatch + PC musical typing.
 * Included inside NbEditorApp (before midi_hw / player note paths). */

    enum class KeyboardInputTarget : uint8_t
    {
        None = 0,
        SampleEditor,
        InstrumentEditor,
        MidiEditorRecord, /* piano roll armed — §2 recording sink */
        MainPreview,
    };

    KeyboardInputTarget ResolveKeyboardTarget() const
    {
        /* Blocking modal: drop new notes (Note Off still uses per-note memory). */
        if (ImGui::GetTopMostPopupModal() != nullptr)
        {
            return KeyboardInputTarget::None;
        }
        if (m_sample_editor_open)
        {
            return KeyboardInputTarget::SampleEditor;
        }
        if (m_instrument_editor_open)
        {
            return KeyboardInputTarget::InstrumentEditor;
        }
        if (m_midi_editor_open && m_midi_editor_focused)
        {
            if (m_midi_record_armed)
            {
                return KeyboardInputTarget::MidiEditorRecord;
            }
            return KeyboardInputTarget::MainPreview;
        }
        return KeyboardInputTarget::MainPreview;
    }

    static int PcKeyboardScancodeToOffset(SDL_Scancode sc)
    {
        /* zefidi map: a w s e d f t g y h u j k o → C..C# (+octave). */
        switch (sc)
        {
        case SDL_SCANCODE_A: return 0;
        case SDL_SCANCODE_W: return 1;
        case SDL_SCANCODE_S: return 2;
        case SDL_SCANCODE_E: return 3;
        case SDL_SCANCODE_D: return 4;
        case SDL_SCANCODE_F: return 5;
        case SDL_SCANCODE_T: return 6;
        case SDL_SCANCODE_G: return 7;
        case SDL_SCANCODE_Y: return 8;
        case SDL_SCANCODE_H: return 9;
        case SDL_SCANCODE_U: return 10;
        case SDL_SCANCODE_J: return 11;
        case SDL_SCANCODE_K: return 12;
        case SDL_SCANCODE_O: return 13;
        default: return -1;
        }
    }

    bool PcKeyboardTypingAllowed() const
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (io.WantTextInput)
        {
            return false;
        }
        if (io.KeyCtrl || io.KeyAlt || io.KeySuper)
        {
            return false;
        }
        return ResolveKeyboardTarget() != KeyboardInputTarget::None;
    }

    void DispatchKeyboardNoteOn(int midi_note, unsigned char velocity = 127)
    {
        if (midi_note < 0 || midi_note > 127)
        {
            return;
        }
        if (velocity == 0)
        {
            DispatchKeyboardNoteOff(midi_note);
            return;
        }

        const KeyboardInputTarget target = ResolveKeyboardTarget();
        m_keyboard_note_target[static_cast<size_t>(midi_note)] = target;

        switch (target)
        {
        case KeyboardInputTarget::None:
            m_keyboard_note_target[static_cast<size_t>(midi_note)] = KeyboardInputTarget::None;
            return;
        case KeyboardInputTarget::SampleEditor:
#if NBEDITOR_MVP
            StartSampleEditorNote(midi_note, velocity);
#endif
            return;
        case KeyboardInputTarget::MidiEditorRecord:
            MeRecordNoteOn(midi_note, velocity);
            return;
        case KeyboardInputTarget::InstrumentEditor:
        case KeyboardInputTarget::MainPreview:
            NoteOn(midi_note, velocity);
            return;
        }
    }

    void DispatchKeyboardNoteOff(int midi_note)
    {
        if (midi_note < 0 || midi_note > 127)
        {
            return;
        }
        const KeyboardInputTarget target = m_keyboard_note_target[static_cast<size_t>(midi_note)];
        m_keyboard_note_target[static_cast<size_t>(midi_note)] = KeyboardInputTarget::None;

        switch (target)
        {
        case KeyboardInputTarget::None:
            NoteOff(midi_note);
#if NBEDITOR_MVP
            StopSampleEditorNote(midi_note);
#endif
            return;
        case KeyboardInputTarget::SampleEditor:
#if NBEDITOR_MVP
            StopSampleEditorNote(midi_note);
#endif
            return;
        case KeyboardInputTarget::MidiEditorRecord:
            MeRecordNoteOff(midi_note);
            return;
        case KeyboardInputTarget::InstrumentEditor:
        case KeyboardInputTarget::MainPreview:
            NoteOff(midi_note);
            return;
        }
    }

    void RefreshHwNoteHeldDisplay(int midi_note)
    {
        if (midi_note < 0 || midi_note > 127)
        {
            return;
        }
        bool any = false;
        for (int c = 0; c < 16; ++c)
        {
            if (m_hw_full_ch_note_held[static_cast<size_t>(c)][static_cast<size_t>(midi_note)])
            {
                any = true;
                break;
            }
        }
        m_hw_note_held[static_cast<size_t>(midi_note)] = any;
    }

    /* Main preview only: zefidi-style full MIDI (all 16 channels, native CC/PC/PB).
     * Does not remap through the selected instrument / AuditionMidiChannel(). */
    void DispatchMainPreviewFullMidi(unsigned char status,
                                     unsigned char data1,
                                     unsigned char data2)
    {
        BAESong song = GetAuditionSong();
        if (!song)
        {
            return;
        }
        const unsigned char mtype = status & 0xF0u;
        const unsigned char ch = status & 0x0Fu;
        const unsigned char d1 = data1 & 0x7fu;
        const unsigned char d2 = data2 & 0x7fu;

        switch (mtype)
        {
        case 0x80u: /* Note Off — always deliver (avoid stuck notes after focus change) */
            BAESong_NoteOff(song, ch, d1, 0, 0);
            m_hw_full_ch_note_held[ch][d1] = false;
            RefreshHwNoteHeldDisplay(static_cast<int>(d1));
            return;
        case 0x90u: /* Note On / vel0 off */
            if (d2 == 0)
            {
                BAESong_NoteOff(song, ch, d1, 0, 0);
                m_hw_full_ch_note_held[ch][d1] = false;
                RefreshHwNoteHeldDisplay(static_cast<int>(d1));
            }
            else
            {
                BAESong_NoteOnWithLoad(song, ch, d1, d2, 0);
                m_hw_full_ch_note_held[ch][d1] = true;
                RefreshHwNoteHeldDisplay(static_cast<int>(d1));
            }
            return;
        case 0xA0u:
            BAESong_KeyPressure(song, ch, d1, d2, 0);
            return;
        case 0xB0u:
            BAESong_ControlChange(song, ch, d1, d2, 0);
            return;
        case 0xC0u:
            BAESong_ProgramChange(song, ch, d1, 0);
            return;
        case 0xD0u:
            BAESong_ChannelPressure(song, ch, d1, 0);
            return;
        case 0xE0u:
            BAESong_PitchBend(song, ch, d1, d2, 0);
            return;
        default:
            return;
        }
    }

    /* Instrument editor / sample editor: remapped single-channel audition.
     * Main preview uses DispatchMainPreviewFullMidi instead. */
    void DispatchAuditionMidiMessage(unsigned char status,
                                     unsigned char data1,
                                     unsigned char data2)
    {
        const unsigned char mtype = status & 0xF0u;
        if (mtype == 0x80u || mtype == 0x90u)
        {
            return; /* notes use DispatchKeyboardNote* */
        }

        const KeyboardInputTarget target = ResolveKeyboardTarget();
        if (target == KeyboardInputTarget::MainPreview)
        {
            DispatchMainPreviewFullMidi(status, data1, data2);
            return;
        }

        if (target == KeyboardInputTarget::MidiEditorRecord)
        {
            MeRecordController(status, data1, data2);
            return;
        }

        if (target == KeyboardInputTarget::None)
        {
            if (mtype == 0xB0u && (data1 == 120 || data1 == 123))
            {
                PanicAuditionNotes();
#if NBEDITOR_MVP
                StopSampleEditorPreview();
#endif
            }
            return;
        }

        if (target == KeyboardInputTarget::SampleEditor)
        {
            if (mtype == 0xB0u && (data1 == 120 || data1 == 123))
            {
#if NBEDITOR_MVP
                StopSampleEditorPreview();
#endif
                PanicAuditionNotes();
            }
            return;
        }

        /* InstrumentEditor — keep patch; controllers on audition channel only. */
        if (m_instrument_editor_open)
        {
            EnsureBankAuditionSong();
        }
        BAESong song = m_ie_note_song ? m_ie_note_song : GetAuditionSong();
        if (!song)
        {
            return;
        }
        const unsigned char ch = AuditionMidiChannel();

        switch (mtype)
        {
        case 0xA0u:
            BAESong_KeyPressure(song, ch, data1 & 0x7fu, data2 & 0x7fu, 0);
            return;
        case 0xB0u:
        {
            const unsigned char cc = data1 & 0x7fu;
            const unsigned char val = data2 & 0x7fu;
            if (cc == 120 || cc == 123)
            {
                PanicAuditionNotes();
#if NBEDITOR_MVP
                StopSampleEditorPreview();
#endif
                return;
            }
            BAESong_ControlChange(song, ch, cc, val, 0);
            return;
        }
        case 0xC0u:
            /* Keep IE patch; PC would fight the instrument being edited. */
            return;
        case 0xD0u:
            BAESong_ChannelPressure(song, ch, data1 & 0x7fu, 0);
            return;
        case 0xE0u:
            BAESong_PitchBend(song, ch, data1 & 0x7fu, data2 & 0x7fu, 0);
            return;
        default:
            return;
        }
    }

    void HandlePcKeyboardEventInternal(const SDL_Event &event)
    {
        if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
        {
            return;
        }
        if (event.key.repeat)
        {
            return;
        }

        const SDL_Scancode sc = event.key.scancode;
        const int sc_i = static_cast<int>(sc);
        if (sc_i < 0 || sc_i >= static_cast<int>(m_pc_key_note.size()))
        {
            return;
        }

        const bool is_down = (event.type == SDL_EVENT_KEY_DOWN);

        /* Always release tracked notes (avoids stuck notes when focus enters a text field). */
        if (!is_down)
        {
            const int held = m_pc_key_note[static_cast<size_t>(sc_i)];
            if (held >= 0)
            {
                m_pc_key_note[static_cast<size_t>(sc_i)] = -1;
                m_hw_note_held[static_cast<size_t>(held)] = false;
                DispatchKeyboardNoteOff(held);
            }
            return;
        }

        if (!PcKeyboardTypingAllowed())
        {
            return;
        }

        if (sc == SDL_SCANCODE_COMMA)
        {
            m_pc_keyboard_octave = std::max(0, m_pc_keyboard_octave - 1);
            SetStatus(std::string("Keyboard octave: ") + std::to_string(m_pc_keyboard_octave));
            return;
        }
        if (sc == SDL_SCANCODE_PERIOD)
        {
            m_pc_keyboard_octave = std::min(8, m_pc_keyboard_octave + 1);
            SetStatus(std::string("Keyboard octave: ") + std::to_string(m_pc_keyboard_octave));
            return;
        }

        const int offset = PcKeyboardScancodeToOffset(sc);
        if (offset < 0)
        {
            return;
        }

        int midi = 60 + (m_pc_keyboard_octave - 4) * 12 + offset;
        midi = std::clamp(midi, 0, 127);
        if (m_pc_key_note[static_cast<size_t>(sc_i)] == midi)
        {
            return;
        }
        if (m_pc_key_note[static_cast<size_t>(sc_i)] >= 0)
        {
            DispatchKeyboardNoteOff(m_pc_key_note[static_cast<size_t>(sc_i)]);
        }
        m_pc_key_note[static_cast<size_t>(sc_i)] = midi;
        m_hw_note_held[static_cast<size_t>(midi)] = true;
        DispatchKeyboardNoteOn(midi, 100);
    }

    void PanicPcKeyboardNotes()
    {
        for (int &n : m_pc_key_note)
        {
            if (n >= 0)
            {
                DispatchKeyboardNoteOff(n);
                n = -1;
            }
        }
    }

    bool CanSaveSessionNow() const
    {
        return m_document != nullptr || m_bank_token != 0 || !m_session_songs.empty();
    }

    void RequestSaveSession()
    {
        if (!CanSaveSessionNow())
        {
            SetStatus("Nothing to save");
            return;
        }
        if (m_loaded_session_path.empty())
        {
            OpenSaveSessionDialog();
            return;
        }
        m_long_op = LongOp::SaveSession;
        m_long_op_path = m_loaded_session_path;
        ArmBusyProgress("Saving Session…");
    }

    void RequestSaveSessionAs()
    {
        if (!CanSaveSessionNow())
        {
            SetStatus("Nothing to save");
            return;
        }
        OpenSaveSessionDialog();
    }

    /* App-wide shortcuts (File Ctrl+* + session Edit). Sub-editors handle their
     * own undo/clipboard when focused; call after windows update focus flags. */
    void HandleGlobalAppShortcuts()
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (io.WantTextInput)
        {
            return;
        }
        if (ImGui::GetTopMostPopupModal() != nullptr)
        {
            return;
        }
        if (m_long_op != LongOp::None)
        {
            return;
        }

        const bool shift = io.KeyShift;
#if NBEDITOR_MVP
        const bool se_focused = m_sample_editor_open && m_sample_editor_focused;
#else
        const bool se_focused = false;
#endif
        const bool ie_focused = m_instrument_editor_open && m_instrument_editor_focused;
        const bool me_focused = m_midi_editor_open && m_midi_editor_focused;
        const bool editor_owns_keys = se_focused || ie_focused || me_focused;

        if (io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            {
#if NBEDITOR_MVP
                if (se_focused)
                {
                    ApplySampleEditorChanges(false);
                    return;
                }
#endif
                if (shift)
                {
                    RequestSaveSessionAs();
                }
                else
                {
                    RequestSaveSession();
                }
                return;
            }

            if (!shift && ImGui::IsKeyPressed(ImGuiKey_O, false))
            {
                OpenLoadDialog();
                return;
            }
            if (!shift && ImGui::IsKeyPressed(ImGuiKey_N, false))
            {
                RequestNewSessionConfirm();
                return;
            }

            /* Session list clipboard — skip when a sub-editor owns the keys. */
            if (!editor_owns_keys)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_A, false))
                {
                    SessionSelectAllVisible();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_C, false))
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
                if (ImGui::IsKeyPressed(ImGuiKey_X, false))
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
                if (ImGui::IsKeyPressed(ImGuiKey_V, false) &&
                    CanPasteForSessionTab(m_session_tab))
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
            }
        }

        /* Del/Backspace only while Session is focused (avoid nuking selection
         * from Player/Status). Clipboard Ctrl+* matches Edit menu (global). */
        if (!editor_owns_keys && m_session_window_focused &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
             ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
        {
            switch (m_session_tab)
            {
            case SessionTab::Songs:
                if (m_groovoid_index >= 0 && m_multi_songs.empty())
                {
                    DeleteGroovoidAt(m_groovoid_index);
                }
                else
                {
                    RequestDeleteSessionSongs(CollectTargetSongIndices());
                }
                break;
            case SessionTab::Instruments:
            {
                std::vector<int> indices = CollectTargetInstrumentIndices();
                RequestDeleteInstruments(indices);
                m_multi_instruments.clear();
                break;
            }
            case SessionTab::Samples:
                DeleteSamplesAtListIndices(CollectTargetSampleIndices());
                break;
            }
        }
    }
