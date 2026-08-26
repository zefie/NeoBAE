/* Trash (TRSH) browser — included inside NbEditorApp. */

    static const char *TrashTypeLabel(XResourceType t)
    {
        switch (t)
        {
        case ID_INST:
            return "INST";
        case ID_ALIAS:
            return "ALIS";
        case ID_SND:
            return "snd ";
        case ID_CSND:
            return "csnd";
        case ID_ESND:
            return "esnd";
        case ID_SONG:
            return "SONG";
        case ID_MIDI:
            return "Midi";
        case ID_MIDI_OLD:
            return "MIDI";
        case ID_EMID:
            return "emid";
        case ID_ECMI:
            return "ecmi";
        case ID_CMID:
            return "cmid";
        default:
            return t ? "other" : "unknown";
        }
    }

    static bool TrashTypeIsMidiObject(XResourceType t)
    {
        return t == ID_MIDI || t == ID_MIDI_OLD || t == ID_EMID ||
               t == ID_ECMI || t == ID_CMID;
    }

    bool PeekTrashSongObjectId(XFILE bank, int trash_index, uint16_t *out_object_id) const
    {
        if (out_object_id)
        {
            *out_object_id = 0;
        }
        if (!bank || trash_index < 0 || !out_object_id)
        {
            return false;
        }
        XLongResourceID trash_id = 0;
        int32_t size = 0;
        XPTR data = XGetIndexedFileResource(bank,
                                            XFILETRASH_ID,
                                            &trash_id,
                                            trash_index,
                                            nullptr,
                                            &size);
        if (!data || size < 2)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            return false;
        }
        const unsigned char *p = static_cast<const unsigned char *>(data);
        const unsigned char *payload = p;
        int32_t payload_size = size;
        if (size >= static_cast<int32_t>(XFILETRASH_META_SIZE) &&
            XGetLong(p) == static_cast<uint32_t>(XFILETRASH_META_MAGIC))
        {
            payload = p + XFILETRASH_META_SIZE;
            payload_size = size - static_cast<int32_t>(XFILETRASH_META_SIZE);
        }
        if (payload_size < 2)
        {
            XDisposePtr(data);
            return false;
        }
        *out_object_id = static_cast<uint16_t>(
            (static_cast<uint16_t>(payload[0]) << 8) | payload[1]);
        XDisposePtr(data);
        return true;
    }

    int FindPairedGroovoidTrashIndex(XFILE bank, int trash_index) const
    {
        if (!bank || trash_index < 0)
        {
            return -1;
        }
        XResourceType orig_type = 0;
        XLongResourceID orig_id = 0;
        if (XGetIndexedFileTrashInfo(bank,
                                     trash_index,
                                     nullptr,
                                     &orig_type,
                                     &orig_id,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr) == FALSE)
        {
            return -1;
        }

        const int count = static_cast<int>(XCountFileTrash(bank));
        if (orig_type == ID_SONG)
        {
            uint16_t object_id = 0;
            if (!PeekTrashSongObjectId(bank, trash_index, &object_id))
            {
                return -1;
            }
            for (int i = 0; i < count; ++i)
            {
                if (i == trash_index)
                {
                    continue;
                }
                XResourceType peer_type = 0;
                XLongResourceID peer_id = 0;
                if (XGetIndexedFileTrashInfo(bank,
                                             i,
                                             nullptr,
                                             &peer_type,
                                             &peer_id,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr) == FALSE)
                {
                    continue;
                }
                if (TrashTypeIsMidiObject(peer_type) &&
                    static_cast<uint32_t>(peer_id) == object_id)
                {
                    return i;
                }
            }
            return -1;
        }
        if (!TrashTypeIsMidiObject(orig_type))
        {
            return -1;
        }
        for (int i = 0; i < count; ++i)
        {
            if (i == trash_index)
            {
                continue;
            }
            XResourceType peer_type = 0;
            if (XGetIndexedFileTrashInfo(bank,
                                         i,
                                         nullptr,
                                         &peer_type,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr) == FALSE ||
                peer_type != ID_SONG)
            {
                continue;
            }
            uint16_t object_id = 0;
            if (PeekTrashSongObjectId(bank, i, &object_id) &&
                static_cast<uint32_t>(orig_id) == object_id)
            {
                return i;
            }
        }
        return -1;
    }

    int CountBankTrash() const
    {
        if (!m_bank_token)
        {
            return 0;
        }
        return static_cast<int>(
            XCountFileTrash(reinterpret_cast<XFILE>(m_bank_token)));
    }

    void CompactBankTrashDuplicates()
    {
        if (!m_bank_token)
        {
            return;
        }
        if (!EnsureActiveBankWritable())
        {
            return;
        }
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        const int before = CountBankTrash();
        int32_t removed = 0;
        if (XDedupeFileTrash(bank, &removed) == FALSE)
        {
            SetStatus("Compact Trash duplicates failed");
            return;
        }
        if (removed > 0)
        {
            RefreshListsFromBank();
            m_trash_selected = -1;
            char status[160];
            std::snprintf(status,
                          sizeof(status),
                          "Removed %d duplicate Trash entr%s (%d → %d)",
                          static_cast<int>(removed),
                          removed == 1 ? "y" : "ies",
                          before,
                          before - static_cast<int>(removed));
            SetStatus(status);
        }
    }

    void OpenTrashWindow()
    {
        m_trash_open = true;
        m_trash_selected = -1;
        /* Legacy soft-delete used id=0 for every TRSH; older pack paths then
         * cloned the first trash body into every slot. Collapse those on open. */
        if (CountBankTrash() > 1)
        {
            CompactBankTrashDuplicates();
        }
    }

    bool RestoreOneTrashItem(XFILE bank,
                             int trash_index,
                             char *name_before,
                             size_t name_before_size,
                             XResourceType *out_orig_type,
                             XLongResourceID *out_orig_id,
                             XLongResourceID *out_restored_id,
                             bool *out_has_meta)
    {
        XResourceType orig_type = 0;
        XLongResourceID orig_id = 0;
        XLongResourceID trash_id = 0;
        int32_t payload_size = 0;
        bool has_meta = false;
        bool guessed = false;
        if (name_before && name_before_size > 0)
        {
            name_before[0] = 0;
        }
        (void)XGetIndexedFileTrashInfo(bank,
                                       trash_index,
                                       &trash_id,
                                       &orig_type,
                                       &orig_id,
                                       name_before,
                                       &payload_size,
                                       &has_meta,
                                       &guessed);

        XLongResourceID restored_id = 0;
        if (XRestoreIndexedFileTrash(bank, trash_index, &restored_id) == FALSE)
        {
            return false;
        }
        if (orig_type == ID_INST ||
            XExistsFileResource(bank, ID_INST, restored_id) != FALSE)
        {
            m_session_custom_inst_ids.insert(static_cast<uint32_t>(restored_id));
        }
        else if (orig_type == ID_SND || orig_type == ID_CSND || orig_type == ID_ESND ||
                 XExistsFileResource(bank, ID_SND, restored_id) != FALSE ||
                 XExistsFileResource(bank, ID_CSND, restored_id) != FALSE ||
                 XExistsFileResource(bank, ID_ESND, restored_id) != FALSE)
        {
            m_session_custom_snd_ids.insert(static_cast<uint32_t>(restored_id));
        }
        if (out_orig_type)
        {
            *out_orig_type = orig_type;
        }
        if (out_orig_id)
        {
            *out_orig_id = orig_id;
        }
        if (out_restored_id)
        {
            *out_restored_id = restored_id;
        }
        if (out_has_meta)
        {
            *out_has_meta = has_meta;
        }
        (void)name_before_size;
        (void)guessed;
        return true;
    }

    void RestoreTrashAt(int trash_index)
    {
        if (!m_bank_token || trash_index < 0)
        {
            return;
        }
        if (!EnsureActiveBankWritable())
        {
            return;
        }
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        const int pair_index = FindPairedGroovoidTrashIndex(bank, trash_index);
        char name_before[256] = {};
        XResourceType orig_type = 0;
        XLongResourceID orig_id = 0;
        XLongResourceID restored_id = 0;
        bool has_meta = false;

        /* Restore the higher trash index first so the lower index stays valid. */
        const int first = (pair_index > trash_index) ? pair_index : trash_index;
        const int second = (pair_index >= 0 && pair_index != trash_index)
                               ? ((pair_index < trash_index) ? pair_index : trash_index)
                               : -1;
        const int primary_first = (first == trash_index);
        if (!RestoreOneTrashItem(bank,
                                 first,
                                 primary_first ? name_before : nullptr,
                                 primary_first ? sizeof(name_before) : 0,
                                 primary_first ? &orig_type : nullptr,
                                 primary_first ? &orig_id : nullptr,
                                 primary_first ? &restored_id : nullptr,
                                 primary_first ? &has_meta : nullptr))
        {
            SetStatus("Restore from Trash failed (unknown type or I/O error)");
            return;
        }
        bool pair_ok = true;
        if (second >= 0)
        {
            char pair_name[256] = {};
            XResourceType pair_type = 0;
            XLongResourceID pair_orig = 0;
            XLongResourceID pair_restored = 0;
            bool pair_meta = false;
            pair_ok = RestoreOneTrashItem(bank,
                                          second,
                                          primary_first ? pair_name : name_before,
                                          primary_first ? sizeof(pair_name) : sizeof(name_before),
                                          primary_first ? &pair_type : &orig_type,
                                          primary_first ? &pair_orig : &orig_id,
                                          primary_first ? &pair_restored : &restored_id,
                                          primary_first ? &pair_meta : &has_meta);
            if (!primary_first)
            {
                has_meta = pair_meta;
            }
            if (!pair_ok)
            {
                SetStatus("Restored one groovoid part; paired Trash item failed");
            }
        }

        const bool remapped =
            has_meta && orig_id != 0 && restored_id != orig_id;
        RefreshListsFromBank();
        m_trash_selected = -1;
        char status[320];
        if (pair_index >= 0 && pair_ok)
        {
            if (name_before[0])
            {
                std::snprintf(status,
                              sizeof(status),
                              "Restored groovoid \"%s\"",
                              name_before);
            }
            else
            {
                std::snprintf(status, sizeof(status), "Restored groovoid from Trash");
            }
            SetStatus(status);
            return;
        }
        if (remapped)
        {
            if (name_before[0])
            {
                std::snprintf(status,
                              sizeof(status),
                              "Restored \"%s\" as %s id %ld (id %ld in use — MIDI/INST refs may need update)",
                              name_before,
                              TrashTypeLabel(orig_type),
                              static_cast<long>(restored_id),
                              static_cast<long>(orig_id));
            }
            else
            {
                std::snprintf(status,
                              sizeof(status),
                              "Restored %s as id %ld (id %ld in use — MIDI/INST refs may need update)",
                              TrashTypeLabel(orig_type),
                              static_cast<long>(restored_id),
                              static_cast<long>(orig_id));
            }
        }
        else if (name_before[0])
        {
            std::snprintf(status,
                          sizeof(status),
                          "Restored \"%s\" as %s id %ld",
                          name_before,
                          TrashTypeLabel(orig_type),
                          static_cast<long>(restored_id));
        }
        else
        {
            std::snprintf(status,
                          sizeof(status),
                          "Restored %s id %ld from Trash",
                          TrashTypeLabel(orig_type),
                          static_cast<long>(restored_id));
        }
        SetStatus(status);
    }

    void PurgeTrashAt(int trash_index)
    {
        if (!m_bank_token || trash_index < 0)
        {
            return;
        }
        if (!EnsureActiveBankWritable())
        {
            return;
        }
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        const int pair_index = FindPairedGroovoidTrashIndex(bank, trash_index);
        const int first = (pair_index > trash_index) ? pair_index : trash_index;
        const int second = (pair_index >= 0 && pair_index != trash_index)
                               ? ((pair_index < trash_index) ? pair_index : trash_index)
                               : -1;
        if (XPurgeIndexedFileTrash(bank, first) == FALSE)
        {
            SetStatus("Permanently delete from Trash failed");
            return;
        }
        if (second >= 0 && XPurgeIndexedFileTrash(bank, second) == FALSE)
        {
            SetStatus("Permanently deleted one groovoid part; paired item failed");
            RefreshListsFromBank();
            m_trash_selected = -1;
            return;
        }
        RefreshListsFromBank();
        m_trash_selected = -1;
        SetStatus(pair_index >= 0 ? "Permanently deleted groovoid from Trash"
                                  : "Permanently deleted item from Trash");
    }

    void EmptyBankTrash()
    {
        if (!m_bank_token)
        {
            return;
        }
        if (!EnsureActiveBankWritable())
        {
            return;
        }
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        const int before = CountBankTrash();
        if (before <= 0)
        {
            SetStatus("Trash is empty");
            return;
        }
        if (XEmptyFileTrash(bank) == FALSE)
        {
            SetStatus("Empty Trash failed");
            return;
        }
        RefreshListsFromBank();
        m_trash_selected = -1;
        m_confirm_empty_trash_open = false;
        char status[128];
        std::snprintf(status, sizeof(status), "Emptied Trash (%d item%s)", before, before == 1 ? "" : "s");
        SetStatus(status);
    }

    void DrawTrashWindow()
    {
        if (!m_trash_open)
        {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Trash", &m_trash_open))
        {
            ImGui::End();
            return;
        }

        if (!m_bank_token)
        {
            ImGui::TextDisabled("No bank loaded.");
            ImGui::End();
            return;
        }

        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        const int count = CountBankTrash();
        ImGui::Text("%d item%s in Trash", count, count == 1 ? "" : "s");
        ImGui::SameLine();
        ImGui::TextDisabled("(survives Save; Empty Trash to purge)");
        ImGui::Separator();

        if (ImGui::BeginTable("##trash_table",
                              5,
                              ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f)))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Orig ID", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Meta", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < count; ++i)
            {
                XLongResourceID trash_id = 0;
                XResourceType orig_type = 0;
                XLongResourceID orig_id = 0;
                char name[256] = {};
                int32_t payload_size = 0;
                bool has_meta = false;
                bool guessed = false;
                if (XGetIndexedFileTrashInfo(bank,
                                             i,
                                             &trash_id,
                                             &orig_type,
                                             &orig_id,
                                             name,
                                             &payload_size,
                                             &has_meta,
                                             &guessed) == FALSE)
                {
                    continue;
                }
                const int pair_index = FindPairedGroovoidTrashIndex(bank, i);
                /* SONG row represents the groovoid; hide the paired MIDI body. */
                if (TrashTypeIsMidiObject(orig_type) && pair_index >= 0)
                {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool selected = (m_trash_selected == i);
                char label[288];
                if (name[0])
                {
                    std::snprintf(label, sizeof(label), "%s##trash%d", name, i);
                }
                else
                {
                    std::snprintf(label, sizeof(label), "(unnamed)##trash%d", i);
                }
                if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    m_trash_selected = i;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                {
                    RestoreTrashAt(i);
                }
                ImGui::TableNextColumn();
                if (orig_type == ID_SONG && pair_index >= 0)
                {
                    ImGui::TextUnformatted("Groovoid");
                }
                else
                {
                    ImGui::TextUnformatted(TrashTypeLabel(orig_type));
                }
                ImGui::TableNextColumn();
                if (has_meta)
                {
                    ImGui::Text("%ld", static_cast<long>(orig_id));
                }
                else
                {
                    ImGui::TextDisabled("—");
                }
                ImGui::TableNextColumn();
                ImGui::Text("%d", static_cast<int>(payload_size));
                ImGui::TableNextColumn();
                if (has_meta)
                {
                    ImGui::TextUnformatted("ZTRS");
                }
                else if (guessed)
                {
                    ImGui::TextDisabled("legacy*");
                }
                else
                {
                    ImGui::TextDisabled("legacy");
                }
            }
            ImGui::EndTable();
        }

        const bool has_sel = m_trash_selected >= 0 && m_trash_selected < count;
        if (ImGui::Button("Restore") && has_sel)
        {
            RestoreTrashAt(m_trash_selected);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Permanently...") && has_sel)
        {
            m_confirm_purge_trash_index = m_trash_selected;
            m_confirm_purge_trash_open = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Empty Trash...") && count > 0)
        {
            m_confirm_empty_trash_open = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Compact Duplicates") && count > 1)
        {
            CompactBankTrashDuplicates();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            m_trash_open = false;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip(
                "Legacy TRSH (from older saves) has no original type/id metadata.\n"
                "Restore guesses snd/INST from the body when possible.\n"
                "If the original ID is already taken, Restore assigns a free ID.");
        }

        ImGui::End();

        if (m_confirm_empty_trash_open)
        {
            ImGui::OpenPopup("Empty Trash?");
            m_confirm_empty_trash_open = false;
            m_confirm_empty_trash_popup = true;
        }
        if (m_confirm_empty_trash_popup)
        {
            if (ImGui::BeginPopupModal("Empty Trash?",
                                       &m_confirm_empty_trash_popup,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Permanently delete all %d Trash item%s?\nThis cannot be undone.",
                            count,
                            count == 1 ? "" : "s");
                if (ImGui::Button("Empty Trash", ImVec2(140, 0)))
                {
                    EmptyBankTrash();
                    m_confirm_empty_trash_popup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    m_confirm_empty_trash_popup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        if (m_confirm_purge_trash_open)
        {
            ImGui::OpenPopup("Delete Trash Item?");
            m_confirm_purge_trash_open = false;
            m_confirm_purge_trash_popup = true;
        }
        if (m_confirm_purge_trash_popup)
        {
            if (ImGui::BeginPopupModal("Delete Trash Item?",
                                       &m_confirm_purge_trash_popup,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Permanently delete the selected Trash item?\nThis cannot be undone.");
                if (m_bank_token && m_confirm_purge_trash_index >= 0 &&
                    FindPairedGroovoidTrashIndex(reinterpret_cast<XFILE>(m_bank_token),
                                                 m_confirm_purge_trash_index) >= 0)
                {
                    ImGui::TextDisabled("This also deletes the paired groovoid MIDI/emid.");
                }
                if (ImGui::Button("Delete", ImVec2(120, 0)))
                {
                    PurgeTrashAt(m_confirm_purge_trash_index);
                    m_confirm_purge_trash_popup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    m_confirm_purge_trash_popup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }
