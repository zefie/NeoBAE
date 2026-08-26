/* Session list multi-select / rename / cut-copy-paste + cross-process clipboard packages.
 * Included inside NbEditorApp (after nbeditor_dnd.inc). */

    static std::string ClipTempDir()
    {
#if defined(_WIN32)
        char buf[MAX_PATH];
        const DWORD n = GetTempPathA(static_cast<DWORD>(sizeof(buf)), buf);
        if (n > 0 && n < sizeof(buf))
        {
            std::string dir(buf);
            while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/'))
            {
                dir.pop_back();
            }
            return dir;
        }
#endif
        const char *tmpdir = std::getenv("TMPDIR");
        if (tmpdir && tmpdir[0])
        {
            return tmpdir;
        }
        tmpdir = std::getenv("TEMP");
        if (tmpdir && tmpdir[0])
        {
            return tmpdir;
        }
        tmpdir = std::getenv("TMP");
        if (tmpdir && tmpdir[0])
        {
            return tmpdir;
        }
#if defined(_WIN32)
        return ".";
#else
        return "/tmp";
#endif
    }

    static int ClipProcessId()
    {
#if defined(_WIN32)
        return static_cast<int>(_getpid());
#else
        return static_cast<int>(getpid());
#endif
    }

    static std::string ClipPackageLeaf(const std::string &path)
    {
        const size_t slash = path.find_last_of("/\\");
        return (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    static bool PathLooksLikeClipPackage(const std::string &path)
    {
        const std::string leaf = ClipPackageLeaf(path);
        if (leaf.rfind("neobae-clip-", 0) != 0)
        {
            return false;
        }
        return EndsWith(leaf, ".zsb") || EndsWith(leaf, ".hsb") || EndsWith(leaf, ".mid") ||
               EndsWith(leaf, ".midi") || EndsWith(leaf, ".rmf") || EndsWith(leaf, ".zmf");
    }

    ClipPackageKind ClipKindFromPackagePath(const std::string &path) const
    {
        if (!PathLooksLikeClipPackage(path))
        {
            return ClipPackageKind::None;
        }
        const std::string leaf = ToLower(ClipPackageLeaf(path));
        if (leaf.find("-song.") != std::string::npos || EndsWith(leaf, ".mid") ||
            EndsWith(leaf, ".midi") || EndsWith(leaf, ".rmf") || EndsWith(leaf, ".zmf"))
        {
            return ClipPackageKind::Song;
        }
        if (leaf.find("-snd.") != std::string::npos || leaf.find("-sample.") != std::string::npos)
        {
            return ClipPackageKind::Sample;
        }
        if (leaf.find("-inst.") != std::string::npos || leaf.find("-instrument.") != std::string::npos)
        {
            return ClipPackageKind::Instrument;
        }
        /* Legacy untagged .zsb/.hsb — classify by contents when possible. */
        if (EndsWith(leaf, ".zsb") || EndsWith(leaf, ".hsb"))
        {
            XFILENAME xname;
            XConvertPathToXFILENAME(const_cast<char *>(path.c_str()), &xname);
            XFILE donor = XFileOpenResource(&xname, TRUE);
            if (!donor)
            {
                return ClipPackageKind::None;
            }
            std::vector<XShortResourceID> inst_ids;
            std::vector<XShortResourceID> snd_ids;
            CollectAllInstAndSndIdsFromFile(donor, inst_ids, snd_ids);
            XFileClose(donor);
            if (!inst_ids.empty())
            {
                return ClipPackageKind::Instrument;
            }
            if (!snd_ids.empty())
            {
                return ClipPackageKind::Sample;
            }
        }
        return ClipPackageKind::None;
    }

    static std::string PercentDecodePath(const std::string &in)
    {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(in[i + 2])))
            {
                char hex[3] = {in[i + 1], in[i + 2], 0};
                out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
                i += 2;
            }
            else
            {
                out.push_back(in[i]);
            }
        }
        return out;
    }

    static std::string PathToFileUri(const std::string &path)
    {
        if (path.empty())
        {
            return {};
        }
        std::string normalized = path;
#if defined(_WIN32)
        for (char &c : normalized)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }
        /* file:///C:/Users/... */
        std::string uri = "file:///";
        if (normalized.size() >= 2 && normalized[1] == ':')
        {
            uri += normalized;
        }
        else if (!normalized.empty() && normalized[0] == '/')
        {
            uri += normalized.substr(1);
        }
        else
        {
            uri += normalized;
        }
#else
        std::string uri = "file://";
        if (normalized.empty() || normalized[0] != '/')
        {
            uri.push_back('/');
        }
        uri += normalized;
#endif
        return uri;
    }

    static std::string ClipboardPathFromText(const char *text)
    {
        if (!text || !text[0])
        {
            return {};
        }
        std::string s = text;
        /* Drop UTF-8 BOM if present. */
        if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
            static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
        {
            s.erase(0, 3);
        }
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        {
            s.pop_back();
        }
        if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        {
            s = s.substr(1, s.size() - 2);
        }
        /* text/uri-list may contain comments and multiple lines — take first path line. */
        {
            std::string line;
            size_t pos = 0;
            while (pos < s.size())
            {
                size_t end = s.find_first_of("\r\n", pos);
                if (end == std::string::npos)
                {
                    end = s.size();
                }
                line = s.substr(pos, end - pos);
                while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
                {
                    line.pop_back();
                }
                if (!line.empty() && line[0] != '#')
                {
                    s = line;
                    break;
                }
                pos = end;
                while (pos < s.size() && (s[pos] == '\r' || s[pos] == '\n'))
                {
                    ++pos;
                }
            }
        }
        if (s.rfind("file:", 0) == 0)
        {
            /* file:///path or file://localhost/path or file:/path */
            size_t i = 5;
            while (i < s.size() && s[i] == '/')
            {
                ++i;
            }
            /* Count leading slashes after "file:" */
            const size_t slash_count = i - 5;
            std::string rest = s.substr(i);
            if (rest.rfind("localhost/", 0) == 0)
            {
                rest = rest.substr(10);
            }
            else if (rest.rfind("localhost", 0) == 0 && rest.size() == 9)
            {
                rest.clear();
            }
            rest = PercentDecodePath(rest);
#if defined(_WIN32)
            /* file:///C:/Temp/x → rest is C:/Temp/x after eating three slashes.
             * file://C:/Temp (two slashes) may leave /C:/Temp — strip. */
            if (rest.size() >= 3 && rest[0] == '/' && std::isalpha(static_cast<unsigned char>(rest[1])) &&
                rest[2] == ':')
            {
                rest = rest.substr(1);
            }
            for (char &c : rest)
            {
                if (c == '/')
                {
                    c = '\\';
                }
            }
            (void)slash_count;
#else
            if (slash_count >= 2 && (rest.empty() || rest[0] != '/'))
            {
                rest.insert(rest.begin(), '/');
            }
            else if (slash_count >= 3 && rest.size() >= 2 && rest[0] != '/')
            {
                rest.insert(rest.begin(), '/');
            }
#endif
            return rest;
        }
        return PercentDecodePath(s);
    }

    static bool ClipFileExists(const std::string &path)
    {
        if (path.empty())
        {
            return false;
        }
        FILE *probe = std::fopen(path.c_str(), "rb");
        if (!probe)
        {
            return false;
        }
        std::fclose(probe);
        return true;
    }

    void CleanupStaleClipPackagesOnStartup()
    {
        const std::string dir = ClipTempDir();
        const std::time_t now = std::time(nullptr);
        const std::time_t ttl = 24 * 60 * 60;
#if !defined(_WIN32)
        DIR *d = opendir(dir.c_str());
        if (!d)
        {
            return;
        }
        while (dirent *ent = readdir(d))
        {
            if (!ent->d_name || std::strncmp(ent->d_name, "neobae-clip-", 12) != 0)
            {
                continue;
            }
            const std::string path = dir + "/" + ent->d_name;
            struct stat st;
            if (stat(path.c_str(), &st) != 0)
            {
                continue;
            }
            if (now > st.st_mtime && (now - st.st_mtime) > ttl)
            {
                std::remove(path.c_str());
            }
        }
        closedir(d);
#endif
    }

    void CleanupPublishedClipPackages()
    {
        (void)SDL_ClearClipboardData();
        for (const std::string &path : m_clip_published_paths)
        {
            std::remove(path.c_str());
        }
        m_clip_published_paths.clear();
        m_clip_active_package_path.clear();
        m_clip_offer_uri.clear();
        m_clip_offer_path.clear();
    }

    void DeleteClipPackageFile(const std::string &path)
    {
        if (path.empty())
        {
            return;
        }
        std::remove(path.c_str());
        m_clip_published_paths.erase(
            std::remove(m_clip_published_paths.begin(), m_clip_published_paths.end(), path),
            m_clip_published_paths.end());
        if (m_clip_active_package_path == path)
        {
            m_clip_active_package_path.clear();
        }
    }

    std::string NextClipPackagePath(ClipPackageKind kind, const char *ext)
    {
        const char *use_ext = (ext && ext[0]) ? ext : ".zsb";
        const char *tag = "pkg";
        switch (kind)
        {
        case ClipPackageKind::Song:
            tag = "song";
            break;
        case ClipPackageKind::Instrument:
            tag = "inst";
            break;
        case ClipPackageKind::Sample:
            tag = "snd";
            break;
        default:
            break;
        }
        char path[512];
#if defined(_WIN32)
        std::snprintf(path,
                      sizeof(path),
                      "%s\\neobae-clip-%d-%u-%s%s",
                      ClipTempDir().c_str(),
                      ClipProcessId(),
                      ++m_clip_seq,
                      tag,
                      use_ext);
#else
        std::snprintf(path,
                      sizeof(path),
                      "%s/neobae-clip-%d-%u-%s%s",
                      ClipTempDir().c_str(),
                      ClipProcessId(),
                      ++m_clip_seq,
                      tag,
                      use_ext);
#endif
        return path;
    }

    ClipPackageKind PeekOsClipboardKind() const
    {
        auto kind_from_text = [&](const char *text) -> ClipPackageKind {
            const std::string path = ClipboardPathFromText(text);
            if (path.empty() || !ClipFileExists(path))
            {
                return ClipPackageKind::None;
            }
            return ClipKindFromPackagePath(path);
        };

        const char *mime_try[] = {"text/uri-list", "text/plain", "TEXT", "text/plain;charset=utf-8"};
        for (const char *mime : mime_try)
        {
            if (!SDL_HasClipboardData(mime))
            {
                continue;
            }
            size_t size = 0;
            void *data = SDL_GetClipboardData(mime, &size);
            if (!data)
            {
                continue;
            }
            /* SDL may report a size that includes a trailing NUL. */
            if (size > 0 && static_cast<const char *>(data)[size - 1] == '\0')
            {
                --size;
            }
            std::string text(static_cast<const char *>(data), size);
            SDL_free(data);
            const ClipPackageKind kind = kind_from_text(text.c_str());
            if (kind != ClipPackageKind::None)
            {
                return kind;
            }
        }
        if (SDL_HasClipboardText())
        {
            char *text = SDL_GetClipboardText();
            if (text)
            {
                const ClipPackageKind kind = kind_from_text(text);
                SDL_free(text);
                if (kind != ClipPackageKind::None)
                {
                    return kind;
                }
            }
        }
        return ClipPackageKind::None;
    }

    bool CanPasteSongs() const
    {
        if (!m_song_clipboard.empty())
        {
            return true;
        }
        return PeekOsClipboardKind() == ClipPackageKind::Song;
    }

    bool CanPasteInstruments() const
    {
        if (!m_bank_token)
        {
            return false;
        }
        if (m_inst_clipboard_valid)
        {
            return true;
        }
        return PeekOsClipboardKind() == ClipPackageKind::Instrument;
    }

    bool CanPasteSamples() const
    {
        if (!m_bank_token)
        {
            return false;
        }
        if (!m_sample_clipboard_snd_ids.empty())
        {
            return true;
        }
        return PeekOsClipboardKind() == ClipPackageKind::Sample;
    }

    bool CanPasteForSessionTab(SessionTab tab) const
    {
        switch (tab)
        {
        case SessionTab::Songs:
            return CanPasteSongs();
        case SessionTab::Instruments:
            return CanPasteInstruments();
        case SessionTab::Samples:
            return CanPasteSamples();
        }
        return false;
    }

    static const void *SDLCALL ClipClipboardDataCallback(void *userdata,
                                                         const char *mime_type,
                                                         size_t *size)
    {
        auto *self = static_cast<NbEditorApp *>(userdata);
        if (!self || !mime_type || !size)
        {
            if (size)
            {
                *size = 0;
            }
            return nullptr;
        }
        if (std::strcmp(mime_type, "text/uri-list") == 0)
        {
            *size = self->m_clip_offer_uri.size();
            return self->m_clip_offer_uri.empty() ? nullptr : self->m_clip_offer_uri.data();
        }
        if (std::strcmp(mime_type, "text/plain") == 0 || std::strcmp(mime_type, "TEXT") == 0 ||
            std::strcmp(mime_type, "text/plain;charset=utf-8") == 0)
        {
            *size = self->m_clip_offer_path.size();
            return self->m_clip_offer_path.empty() ? nullptr : self->m_clip_offer_path.data();
        }
        *size = 0;
        return nullptr;
    }

    static void SDLCALL ClipClipboardCleanupCallback(void *userdata)
    {
        (void)userdata;
    }

    void PublishClipPackagePathToClipboard(const std::string &path)
    {
        if (path.empty())
        {
            return;
        }
        /* Replace prior clip from this process. */
        if (!m_clip_active_package_path.empty() && m_clip_active_package_path != path)
        {
            DeleteClipPackageFile(m_clip_active_package_path);
        }
        m_clip_active_package_path = path;
        if (std::find(m_clip_published_paths.begin(), m_clip_published_paths.end(), path) ==
            m_clip_published_paths.end())
        {
            m_clip_published_paths.push_back(path);
        }

        m_clip_offer_path = path;
        m_clip_offer_uri = PathToFileUri(path);
        if (!m_clip_offer_uri.empty() && m_clip_offer_uri.back() != '\n')
        {
            m_clip_offer_uri.push_back('\n');
        }

        /* Prefer eager plain-text path for cross-process paste. SDL delayed
         * mime callbacks often fail between two nbeditor processes on Win32. */
        if (!SDL_SetClipboardText(path.c_str()))
        {
            const char *mimes[] = {"text/uri-list", "text/plain"};
            (void)SDL_SetClipboardData(ClipClipboardDataCallback,
                                       ClipClipboardCleanupCallback,
                                       this,
                                       mimes,
                                       2);
        }
    }

    static bool IsUsableSndResourceId(uint16_t sid)
    {
        /* Beatnik uses 0xFFFF (-1) as "no sample". ID 0 is a valid SND id
         * (common in session/BSN banks with a single sample). */
        return sid != 0xFFFFu;
    }

    static XLongResourceID SndIdAsLong(XShortResourceID sid)
    {
        /* Zero-extend: sign-extending negative XShortResourceID breaks lookups. */
        return static_cast<XLongResourceID>(static_cast<uint16_t>(sid));
    }

    void AppendUniqueSndId(std::vector<XShortResourceID> &out_snd,
                           std::set<uint16_t> &seen,
                           uint16_t sid16)
    {
        if (!IsUsableSndResourceId(sid16) || !seen.insert(sid16).second)
        {
            return;
        }
        out_snd.push_back(static_cast<XShortResourceID>(sid16));
    }

    void CollectSndIdsFromInstData(InstrumentResource *inst,
                                   std::vector<XShortResourceID> &out_snd,
                                   std::set<uint16_t> &seen)
    {
        if (!inst)
        {
            return;
        }
        /* Prefer the classic collector; require maxArraySize > splitCount. */
        XShortResourceID buf[2048];
        const int16_t n = XCollectSoundsFromInstrument(inst, buf, 2048);
        for (int16_t i = 0; i < n; ++i)
        {
            AppendUniqueSndId(out_snd, seen, static_cast<uint16_t>(buf[i]));
        }
        /* Manual fallback if collector skipped (e.g. huge splitCount). */
        const int16_t split_count = static_cast<int16_t>(XGetShort(&inst->keySplitCount));
        if (split_count <= 0)
        {
            AppendUniqueSndId(out_snd,
                              seen,
                              static_cast<uint16_t>(XGetShort(&inst->sndResourceID)));
            return;
        }
        for (int16_t s = 0; s < split_count; ++s)
        {
            KeySplit split;
            std::memset(&split, 0, sizeof(split));
            XGetKeySplitFromPtr(inst, s, &split);
            AppendUniqueSndId(out_snd, seen, static_cast<uint16_t>(split.sndResourceID));
        }
    }

    void CollectSndIdsFromInstResourceId(XLongResourceID inst_id,
                                         std::vector<XShortResourceID> &out_snd)
    {
        /* INST id 0 is valid — only reject a missing bank. */
        if (!m_bank_token)
        {
            return;
        }
        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);
        int32_t size = 0;
        XPTR data = XGetFileResource(bank, ID_INST, inst_id, nullptr, &size);
        if (!data || size < 14)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            return;
        }
        std::set<uint16_t> seen;
        for (XShortResourceID existing : out_snd)
        {
            seen.insert(static_cast<uint16_t>(existing));
        }
        CollectSndIdsFromInstData(reinterpret_cast<InstrumentResource *>(data), out_snd, seen);
        XDisposePtr(data);
    }

    bool ProbeSndInFile(XFILE file, XShortResourceID sid) const
    {
        if (!file || !IsUsableSndResourceId(static_cast<uint16_t>(sid)))
        {
            return false;
        }
        static const XResourceType kSndTypes[] = {ID_ESND, ID_CSND, ID_SND, 0};
        for (int t = 0; kSndTypes[t] != 0; ++t)
        {
            int32_t size = 0;
            XPTR data = XGetFileResource(file,
                                         kSndTypes[t],
                                         SndIdAsLong(sid),
                                         nullptr,
                                         &size);
            if (data)
            {
                const bool ok = (size >= 4);
                XDisposePtr(data);
                if (ok)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool WriteBankFragmentPackage(const std::vector<XShortResourceID> &inst_ids,
                                  const std::vector<XShortResourceID> &snd_ids,
                                  const std::string &out_path,
                                  bool include_referenced_samples,
                                  unsigned *out_snd_copied)
    {
        if (out_snd_copied)
        {
            *out_snd_copied = 0;
        }
        if (!m_bank_token || out_path.empty())
        {
            return false;
        }
        if (inst_ids.empty() && snd_ids.empty())
        {
            return false;
        }
        XFILE src = reinterpret_cast<XFILE>(m_bank_token);
        uint32_t z_reason = 0;
        const bool want_zsb = (BAERmfEditorBank_RequiresZsb(m_bank_token, &z_reason) != FALSE) ||
                              EndsWith(out_path, ".zsb");
        XFILE dest = XFileOpenVirtualResource(want_zsb ? XFILERESOURCE_ZMF_ID : XFILERESOURCE_ID);
        if (!dest)
        {
            return false;
        }

        std::vector<XShortResourceID> inst_ok;
        if (!inst_ids.empty())
        {
            std::vector<XShortResourceID> inst_mut = inst_ids;
            (void)XCopyInstrumentResources(inst_mut.data(),
                                           static_cast<int16_t>(inst_mut.size()),
                                           src,
                                           dest,
                                           TRUE);
            for (XShortResourceID iid : inst_ids)
            {
                int32_t size = 0;
                XPTR data = XGetFileResource(dest,
                                             ID_INST,
                                             SndIdAsLong(iid),
                                             nullptr,
                                             &size);
                if (data && size >= 14)
                {
                    inst_ok.push_back(iid);
                }
                if (data)
                {
                    XDisposePtr(data);
                }
            }
        }

        /* Merge caller-provided SND ids with IDs referenced by copied INST data. */
        std::vector<XShortResourceID> snd_want = snd_ids;
        if (include_referenced_samples && !inst_ok.empty())
        {
            std::set<uint16_t> seen;
            for (XShortResourceID existing : snd_want)
            {
                seen.insert(static_cast<uint16_t>(existing));
            }
            for (XShortResourceID iid : inst_ok)
            {
                int32_t size = 0;
                XPTR data = XGetFileResource(dest,
                                             ID_INST,
                                             SndIdAsLong(iid),
                                             nullptr,
                                             &size);
                if (!data || size < 14)
                {
                    if (data)
                    {
                        XDisposePtr(data);
                    }
                    continue;
                }
                CollectSndIdsFromInstData(reinterpret_cast<InstrumentResource *>(data),
                                          snd_want,
                                          seen);
                XDisposePtr(data);
            }
        }

        std::vector<XShortResourceID> snd_ok;
        if (!snd_want.empty())
        {
            std::vector<XShortResourceID> snd_mut = snd_want;
            (void)XCopySndResources(snd_mut.data(),
                                    static_cast<int16_t>(snd_mut.size()),
                                    src,
                                    dest,
                                    FALSE,
                                    TRUE);
            /* Skip bad SNDs instead of aborting the whole package. */
            for (XShortResourceID sid : snd_want)
            {
                if (ProbeSndInFile(dest, sid))
                {
                    snd_ok.push_back(sid);
                }
            }
        }
        if (inst_ok.empty() && snd_ok.empty())
        {
            XFileClose(dest);
            return false;
        }
        if (out_snd_copied)
        {
            *out_snd_copied = static_cast<unsigned>(snd_ok.size());
        }
        /* Clip packages: flat finalize (no Pack/LZMA). Full ESND/SND payloads. */
        if (XFinalizeEditorResourceFile(dest) == FALSE)
        {
            (void)XCreateAccessCache(dest);
        }
        XPTR packed = nullptr;
        int32_t packed_size = 0;
        if (XFileGetMemoryFileAsData(dest, &packed, &packed_size) != 0 || !packed || packed_size <= 0)
        {
            XFileClose(dest);
            if (packed)
            {
                XDisposePtr(packed);
            }
            return false;
        }
        XFileClose(dest);
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            XDisposePtr(packed);
            return false;
        }
        out.write(reinterpret_cast<const char *>(packed), packed_size);
        XDisposePtr(packed);
        return out.good();
    }

    void CollectInstrumentSndIds(uint32_t instrument_index, std::vector<XShortResourceID> &out_snd)
    {
        std::set<uint16_t> seen;
        for (XShortResourceID existing : out_snd)
        {
            seen.insert(static_cast<uint16_t>(existing));
        }
        uint32_t split_count = 0;
        if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token, instrument_index, &split_count) ==
            BAE_NO_ERROR)
        {
            for (uint32_t s = 0; s < split_count; ++s)
            {
                BAERmfEditorBankSampleInfo info;
                std::memset(&info, 0, sizeof(info));
                if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token, instrument_index, s, &info) !=
                    BAE_NO_ERROR)
                {
                    continue;
                }
                AppendUniqueSndId(out_snd, seen, static_cast<uint16_t>(info.sndResourceID));
            }
        }
        /* Fallback: sample list rows for this instrument (covers edge cases where
         * split metadata is thin but the list already resolved the SND id). */
        for (const SampleRow &row : m_samples)
        {
            if (row.source != SampleSource::Bank || row.unassigned)
            {
                continue;
            }
            if (row.instrument_index != instrument_index)
            {
                continue;
            }
            AppendUniqueSndId(out_snd, seen, static_cast<uint16_t>(row.snd_resource_id));
        }
    }

    static void PatchInstResourceSndIds(unsigned char *inst_bytes,
                                        int32_t inst_size,
                                        const std::map<uint16_t, uint16_t> &snd_remap)
    {
        if (!inst_bytes || inst_size < 14 || snd_remap.empty())
        {
            return;
        }
        auto remap = [&](uint16_t sid) -> uint16_t {
            const auto it = snd_remap.find(sid);
            return (it != snd_remap.end()) ? it->second : sid;
        };

        const int16_t split_count = static_cast<int16_t>(XGetShort(inst_bytes + 12));
        if (split_count <= 0)
        {
            XPutShort(inst_bytes, remap(static_cast<uint16_t>(XGetShort(inst_bytes))));
            return;
        }
        for (int16_t s = 0; s < split_count; ++s)
        {
            const int32_t off = 14 + static_cast<int32_t>(s) * 8 + 2;
            if (off + 2 > inst_size)
            {
                break;
            }
            XPutShort(inst_bytes + off,
                      remap(static_cast<uint16_t>(XGetShort(inst_bytes + off))));
        }
        /* Keep header sndResourceID synchronized with split 0. */
        XPutShort(inst_bytes, static_cast<uint16_t>(XGetShort(inst_bytes + 14 + 2)));
    }

    /* Remap donor INST/SND IDs into free destination slots, then import.
     * Colliding SND IDs get new IDs (same allocator as Add Sample); key splits
     * are patched. First instrument → dest_first_inst_id; additional instruments
     * take the next free programs in the same bank/percussion group. */
    bool RemapAndImportInstrumentClip(const std::string &path,
                                      uint32_t dest_first_inst_id,
                                      bool delete_after_import)
    {
        if (path.empty() || !m_bank_token)
        {
            SetStatus("No bank for clip import");
            return false;
        }
        if (!EnsureActiveBankWritable())
        {
            return false;
        }

        XFILENAME xname;
        XConvertPathToXFILENAME(const_cast<char *>(path.c_str()), &xname);
        XFILE donor = XFileOpenResource(&xname, TRUE);
        if (!donor)
        {
            SetStatus("Failed to open clip package");
            return false;
        }

        std::vector<XShortResourceID> src_inst_ids;
        std::vector<XShortResourceID> src_snd_ids;
        CollectAllInstAndSndIdsFromFile(donor, src_inst_ids, src_snd_ids);
        if (src_inst_ids.empty())
        {
            XFileClose(donor);
            SetStatus("Clip package has no instruments");
            return false;
        }

        XFILE dest_bank = reinterpret_cast<XFILE>(m_bank_token);
        std::map<uint16_t, uint16_t> snd_remap;
        std::set<XLongResourceID> reserved_snd;
        unsigned remapped_snd = 0;

        for (XShortResourceID old_sid : src_snd_ids)
        {
            const uint16_t old16 = static_cast<uint16_t>(old_sid);
            if (!IsUsableSndResourceId(old16))
            {
                continue;
            }
            const XLongResourceID old_long = SndIdAsLong(old_sid);
            if (ProbeSndInFile(dest_bank, old_sid) || reserved_snd.count(old_long) != 0)
            {
                XLongResourceID neu = 0;
                if (!AllocateUniqueSndId(dest_bank, &neu, &reserved_snd))
                {
                    XFileClose(donor);
                    SetStatus("Failed to allocate free sample ID for paste");
                    return false;
                }
                reserved_snd.insert(neu);
                snd_remap[old16] = static_cast<uint16_t>(neu);
                ++remapped_snd;
            }
            else
            {
                reserved_snd.insert(old_long);
                snd_remap[old16] = old16;
            }
        }

        int dest_bank_num = 0;
        bool dest_perc = false;
        int dest_prog = 0;
        DecodeInstSlot(dest_first_inst_id, &dest_bank_num, &dest_perc, &dest_prog);

        bool used_programs[128];
        CollectUsedProgramsForGroup(dest_bank_num, dest_perc, used_programs);
        std::map<uint16_t, uint16_t> inst_remap;
        std::vector<XShortResourceID> new_inst_ids;
        new_inst_ids.reserve(src_inst_ids.size());

        int next_prog = dest_prog;
        for (size_t i = 0; i < src_inst_ids.size(); ++i)
        {
            const uint16_t old_inst16 = static_cast<uint16_t>(src_inst_ids[i]);
            uint32_t new_inst = 0;
            if (i == 0)
            {
                new_inst = dest_first_inst_id;
                used_programs[dest_prog] = true;
            }
            else
            {
                int found = -1;
                for (int p = next_prog + 1; p < 128; ++p)
                {
                    if (!used_programs[p])
                    {
                        found = p;
                        break;
                    }
                }
                if (found < 0)
                {
                    for (int p = 0; p <= next_prog; ++p)
                    {
                        if (!used_programs[p])
                        {
                            found = p;
                            break;
                        }
                    }
                }
                if (found < 0)
                {
                    XFileClose(donor);
                    SetStatus("Not enough free instrument slots for multi-instrument paste");
                    return false;
                }
                new_inst = BankInstIdFromSlot(dest_bank_num, dest_perc, found);
                used_programs[found] = true;
                next_prog = found;
            }
            inst_remap[old_inst16] = static_cast<uint16_t>(new_inst);
            new_inst_ids.push_back(static_cast<XShortResourceID>(new_inst));
        }

        std::vector<XShortResourceID> new_snd_ids;
        new_snd_ids.reserve(src_snd_ids.size());
        for (XShortResourceID old_sid : src_snd_ids)
        {
            const uint16_t old16 = static_cast<uint16_t>(old_sid);
            if (!IsUsableSndResourceId(old16))
            {
                continue;
            }
            const auto it = snd_remap.find(old16);
            new_snd_ids.push_back(static_cast<XShortResourceID>(
                it != snd_remap.end() ? it->second : old16));
        }

        XFILE staging = XFileOpenVirtualResource(XFILERESOURCE_ID);
        if (!staging)
        {
            XFileClose(donor);
            SetStatus("Failed to stage remapped clip package");
            return false;
        }

        static const XResourceType kSndTypes[] = {ID_ESND, ID_CSND, ID_SND, 0};
        for (XShortResourceID old_sid : src_snd_ids)
        {
            const uint16_t old16 = static_cast<uint16_t>(old_sid);
            if (!IsUsableSndResourceId(old16))
            {
                continue;
            }
            const uint16_t new16 = snd_remap.count(old16) ? snd_remap[old16] : old16;
            bool copied = false;
            for (int t = 0; kSndTypes[t] != 0; ++t)
            {
                char name[256];
                int32_t size = 0;
                name[0] = 0;
                XPTR data = XGetFileResource(donor,
                                             kSndTypes[t],
                                             SndIdAsLong(old_sid),
                                             name,
                                             &size);
                if (!data || size <= 0)
                {
                    if (data)
                    {
                        XDisposePtr(data);
                    }
                    continue;
                }
                if (XAddFileResource(staging,
                                     kSndTypes[t],
                                     static_cast<XLongResourceID>(new16),
                                     name,
                                     data,
                                     size) != 0)
                {
                    XDisposePtr(data);
                    XFileClose(staging);
                    XFileClose(donor);
                    SetStatus("Failed to stage remapped sample");
                    return false;
                }
                XDisposePtr(data);
                copied = true;
                break;
            }
            if (!copied)
            {
                XFileClose(staging);
                XFileClose(donor);
                SetStatus("Clip sample missing during remap");
                return false;
            }
        }

        for (XShortResourceID old_iid : src_inst_ids)
        {
            const uint16_t old16 = static_cast<uint16_t>(old_iid);
            const auto iit = inst_remap.find(old16);
            if (iit == inst_remap.end())
            {
                continue;
            }
            const uint16_t new16 = iit->second;
            char name[256];
            int32_t size = 0;
            name[0] = 0;
            XPTR data = XGetFileResource(donor,
                                         ID_INST,
                                         static_cast<XLongResourceID>(old16),
                                         name,
                                         &size);
            if (!data || size < 14)
            {
                if (data)
                {
                    XDisposePtr(data);
                }
                XFileClose(staging);
                XFileClose(donor);
                SetStatus("Clip instrument missing during remap");
                return false;
            }
            PatchInstResourceSndIds(static_cast<unsigned char *>(data), size, snd_remap);
            if (XAddFileResource(staging,
                                 ID_INST,
                                 static_cast<XLongResourceID>(new16),
                                 name,
                                 data,
                                 size) != 0)
            {
                XDisposePtr(data);
                XFileClose(staging);
                XFileClose(donor);
                SetStatus("Failed to stage remapped instrument");
                return false;
            }
            XDisposePtr(data);
        }

        XFileClose(donor);
        donor = nullptr;

        const BAEResult import_r = BAERmfEditorBank_ImportInstAndSndFromFile(
            m_bank_token,
            staging,
            new_inst_ids.empty() ? nullptr : new_inst_ids.data(),
            static_cast<uint32_t>(new_inst_ids.size()),
            new_snd_ids.empty() ? nullptr : new_snd_ids.data(),
            static_cast<uint32_t>(new_snd_ids.size()));
        XFileClose(staging);
        if (import_r != BAE_NO_ERROR)
        {
            SetStatus(std::string("Clip import failed: ") + FormatBAEError(import_r));
            return false;
        }

        for (XShortResourceID snd_id : new_snd_ids)
        {
            m_session_custom_snd_ids.insert(static_cast<uint32_t>(static_cast<uint16_t>(snd_id)));
        }
        for (XShortResourceID inst_id : new_inst_ids)
        {
            const uint32_t iid = static_cast<uint32_t>(static_cast<uint16_t>(inst_id));
            m_session_custom_inst_ids.insert(iid);
            if ((iid / 256u) < 2u)
            {
                m_bank01_touched = true;
            }
        }
        RefreshListsFromBank();
        if (delete_after_import && PathLooksLikeClipPackage(path))
        {
            DeleteClipPackageFile(path);
        }

        char status[256];
        if (remapped_snd > 0)
        {
            std::snprintf(status,
                          sizeof(status),
                          "Pasted %u instrument(s) → instID %u (%u sample ID(s) remapped)",
                          static_cast<unsigned>(new_inst_ids.size()),
                          dest_first_inst_id,
                          remapped_snd);
        }
        else
        {
            std::snprintf(status,
                          sizeof(status),
                          "Pasted %u instrument(s) → instID %u",
                          static_cast<unsigned>(new_inst_ids.size()),
                          dest_first_inst_id);
        }
        SetStatus(status);
        SelectInstrumentByInstId(dest_first_inst_id);
        return true;
    }

    bool ImportClipPackageFromPath(const char *path, bool delete_after_import)
    {
        if (!path || !path[0])
        {
            return false;
        }
        const std::string path_str = path;
        const bool is_song = EndsWith(path_str, ".mid") || EndsWith(path_str, ".midi") ||
                             EndsWith(path_str, ".rmf") || EndsWith(path_str, ".zmf");
        if (is_song)
        {
            const char *src = EndsWith(path_str, ".rmf")   ? "RMF"
                              : EndsWith(path_str, ".zmf") ? "ZMF"
                                                           : "MIDI";
            const bool ok = ImportSongDocumentAsNewSessionEntry(path_str, src);
            if (ok && delete_after_import && PathLooksLikeClipPackage(path_str))
            {
                DeleteClipPackageFile(path_str);
            }
            return ok;
        }

        if (!m_bank_token && !LoadBuiltinBankAsActive())
        {
            SetStatus("No bank for clip import");
            return false;
        }
        if (!EnsureActiveBankWritable())
        {
            return false;
        }
        XFILENAME xname;
        XConvertPathToXFILENAME(const_cast<char *>(path), &xname);
        XFILE donor = XFileOpenResource(&xname, TRUE);
        if (!donor)
        {
            SetStatus("Failed to open clip package");
            return false;
        }
        std::vector<XShortResourceID> inst_ids;
        std::vector<XShortResourceID> snd_ids;
        CollectAllInstAndSndIdsFromFile(donor, inst_ids, snd_ids);
        if (inst_ids.empty() && snd_ids.empty())
        {
            XFileClose(donor);
            SetStatus("Clip package has no instruments or samples");
            return false;
        }
        const BAEResult import_r = BAERmfEditorBank_ImportInstAndSndFromFile(
            m_bank_token,
            donor,
            inst_ids.empty() ? nullptr : inst_ids.data(),
            static_cast<uint32_t>(inst_ids.size()),
            snd_ids.empty() ? nullptr : snd_ids.data(),
            static_cast<uint32_t>(snd_ids.size()));
        XFileClose(donor);
        if (import_r != BAE_NO_ERROR)
        {
            SetStatus(std::string("Clip import failed: ") + FormatBAEError(import_r));
            return false;
        }
        for (XShortResourceID snd_id : snd_ids)
        {
            m_session_custom_snd_ids.insert(static_cast<uint32_t>(snd_id));
        }
        for (XShortResourceID inst_id : inst_ids)
        {
            m_session_custom_inst_ids.insert(static_cast<uint32_t>(inst_id));
            if ((static_cast<uint32_t>(inst_id) / 256u) < 2u)
            {
                m_bank01_touched = true;
            }
        }
        RefreshListsFromBank();
        if (delete_after_import && PathLooksLikeClipPackage(path_str))
        {
            DeleteClipPackageFile(path_str);
        }
        unsigned decode_ok = 0;
        for (XShortResourceID snd_id : snd_ids)
        {
            void *wave = nullptr;
            uint32_t frames = 0;
            uint16_t bits = 0;
            uint16_t ch = 0;
            BAE_UNSIGNED_FIXED rate = 0;
            if (BAERmfEditorBank_GetSndWaveformData(m_bank_token,
                                                    static_cast<uint32_t>(snd_id),
                                                    &wave,
                                                    &frames,
                                                    &bits,
                                                    &ch,
                                                    &rate) == BAE_NO_ERROR &&
                wave && frames > 0)
            {
                ++decode_ok;
            }
            if (wave)
            {
                BAERmfEditorBank_FreeWaveformData(wave);
            }
        }
        char status[224];
        if (!snd_ids.empty() && decode_ok == 0)
        {
            std::snprintf(status,
                          sizeof(status),
                          "Imported clip: %u instrument(s), %u sample(s) — WARNING: samples failed to decode",
                          static_cast<unsigned>(inst_ids.size()),
                          static_cast<unsigned>(snd_ids.size()));
        }
        else if (!snd_ids.empty() && decode_ok < snd_ids.size())
        {
            std::snprintf(status,
                          sizeof(status),
                          "Imported clip: %u instrument(s), %u sample(s) (%u decoded OK)",
                          static_cast<unsigned>(inst_ids.size()),
                          static_cast<unsigned>(snd_ids.size()),
                          decode_ok);
        }
        else
        {
            std::snprintf(status,
                          sizeof(status),
                          "Imported clip: %u instrument(s), %u sample(s)",
                          static_cast<unsigned>(inst_ids.size()),
                          static_cast<unsigned>(snd_ids.size()));
        }
        SetStatus(status);
        return true;
    }

    /* Peek an OS clipboard clip-package path without importing. */
    std::string PeekOsClipboardPackagePath(ClipPackageKind expect_kind = ClipPackageKind::None) const
    {
        auto try_text = [&](const char *text) -> std::string {
            const std::string path = ClipboardPathFromText(text);
            if (path.empty() || !PathLooksLikeClipPackage(path) || !ClipFileExists(path))
            {
                return {};
            }
            if (expect_kind != ClipPackageKind::None &&
                ClipKindFromPackagePath(path) != expect_kind)
            {
                return {};
            }
            return path;
        };

        const char *mime_try[] = {"text/uri-list", "text/plain", "TEXT", "text/plain;charset=utf-8"};
        for (const char *mime : mime_try)
        {
            if (!SDL_HasClipboardData(mime))
            {
                continue;
            }
            size_t size = 0;
            void *data = SDL_GetClipboardData(mime, &size);
            if (!data)
            {
                continue;
            }
            if (size > 0 && static_cast<const char *>(data)[size - 1] == '\0')
            {
                --size;
            }
            std::string text(static_cast<const char *>(data), size);
            SDL_free(data);
            const std::string path = try_text(text.c_str());
            if (!path.empty())
            {
                return path;
            }
        }

        if (SDL_HasClipboardText())
        {
            char *text = SDL_GetClipboardText();
            if (text)
            {
                const std::string path = try_text(text);
                SDL_free(text);
                if (!path.empty())
                {
                    return path;
                }
            }
        }
        return {};
    }

    /* Returns true if paste consumed an OS clip package (success or hard failure
     * already reported via SetStatus). False means no usable OS clip path. */
    bool TryPasteFromOsClipboard(ClipPackageKind expect_kind = ClipPackageKind::None)
    {
        auto try_text = [&](const char *text) -> bool {
            const std::string path = ClipboardPathFromText(text);
            if (path.empty() || !PathLooksLikeClipPackage(path) || !ClipFileExists(path))
            {
                return false;
            }
            if (expect_kind != ClipPackageKind::None &&
                ClipKindFromPackagePath(path) != expect_kind)
            {
                return false;
            }
            /* Import sets its own status on failure; always treat as handled so
             * callers do not replace it with "clipboard is empty". */
            (void)ImportClipPackageFromPath(path.c_str(), true);
            return true;
        };

        const char *mime_try[] = {"text/uri-list", "text/plain", "TEXT", "text/plain;charset=utf-8"};
        for (const char *mime : mime_try)
        {
            if (!SDL_HasClipboardData(mime))
            {
                continue;
            }
            size_t size = 0;
            void *data = SDL_GetClipboardData(mime, &size);
            if (!data)
            {
                continue;
            }
            if (size > 0 && static_cast<const char *>(data)[size - 1] == '\0')
            {
                --size;
            }
            std::string text(static_cast<const char *>(data), size);
            SDL_free(data);
            if (try_text(text.c_str()))
            {
                return true;
            }
        }

        if (SDL_HasClipboardText())
        {
            char *text = SDL_GetClipboardText();
            if (text)
            {
                const bool ok = try_text(text);
                SDL_free(text);
                if (ok)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void ApplyListSelection(std::set<int> &multi,
                            int &primary,
                            int index,
                            int last_anchor,
                            bool ctrl,
                            bool shift)
    {
        if (shift && last_anchor >= 0)
        {
            const int a = std::min(last_anchor, index);
            const int b = std::max(last_anchor, index);
            if (!ctrl)
            {
                multi.clear();
            }
            for (int i = a; i <= b; ++i)
            {
                multi.insert(i);
            }
            primary = index;
            return;
        }
        if (ctrl)
        {
            if (multi.count(index))
            {
                multi.erase(index);
            }
            else
            {
                multi.insert(index);
            }
            primary = index;
            return;
        }
        multi.clear();
        multi.insert(index);
        primary = index;
    }

    void OpenRenameDialog(const char *kind, int index, const std::string &current)
    {
        m_rename_kind = kind ? kind : "";
        m_rename_index = index;
        std::snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", current.c_str());
        m_rename_open = true;
    }

    void PerformRenameDialogOk()
    {
        const std::string name = m_rename_buf;
        if (name.empty())
        {
            SetStatus("Name cannot be empty");
            return;
        }
        if (m_rename_kind == "song")
        {
            if (m_rename_index < 0 || m_rename_index >= static_cast<int>(m_session_songs.size()))
            {
                m_rename_open = false;
                return;
            }
            if (m_rename_index == m_session_song_index && m_document)
            {
                (void)BAERmfEditorDocument_SetInfo(m_document, TITLE_INFO, name.c_str());
                m_document_dirty = true;
                m_song_row.name = name;
            }
            m_session_songs[static_cast<size_t>(m_rename_index)].name = name;
            SetStatus(std::string("Renamed song to \"") + name + "\"");
        }
        else if (m_rename_kind == "instrument")
        {
            if (!m_bank_token || m_rename_index < 0 ||
                m_rename_index >= static_cast<int>(m_instruments.size()))
            {
                m_rename_open = false;
                return;
            }
            const InstrumentRow &row = m_instruments[static_cast<size_t>(m_rename_index)];
            if (row.from_song_document || row.is_alias)
            {
                SetStatus("Cannot rename this instrument here");
                m_rename_open = false;
                return;
            }
            if (!EnsureActiveBankWritable())
            {
                return;
            }
            BAERmfEditorInstrumentExtInfo ext;
            std::memset(&ext, 0, sizeof(ext));
            if (BAERmfEditorBank_GetInstrumentExtInfo(m_bank_token, row.instrument_index, &ext) !=
                BAE_NO_ERROR)
            {
                SetStatus("Rename failed: could not load instrument");
                return;
            }
            /* displayName is borrowed; keep a stable buffer for the API call. */
            static char s_rename_display[256];
            std::snprintf(s_rename_display, sizeof(s_rename_display), "%s", name.c_str());
            ext.displayName = s_rename_display;
            const BAEResult r =
                BAERmfEditorBank_SetInstrumentExtInfo(m_bank_token, row.instrument_index, &ext);
            if (r != BAE_NO_ERROR)
            {
                SetStatus(std::string("Rename failed: ") + FormatBAEError(r));
                return;
            }
            MarkBank01TouchedIfNeeded(row.bank);
            RefreshListsFromBank();
            SetStatus(std::string("Renamed instrument to \"") + name + "\"");
        }
        else if (m_rename_kind == "sample")
        {
            if (m_rename_index < 0 || m_rename_index >= static_cast<int>(m_samples.size()))
            {
                m_rename_open = false;
                return;
            }
            const SampleRow sample = m_samples[static_cast<size_t>(m_rename_index)];
            if (sample.source == SampleSource::Song)
            {
                if (!m_document)
                {
                    SetStatus("Cannot rename song sample — no document");
                    m_rename_open = false;
                    return;
                }
                BAERmfEditorSampleInfo info;
                std::memset(&info, 0, sizeof(info));
                if (BAERmfEditorDocument_GetSampleInfo(m_document,
                                                       sample.document_sample_index,
                                                       &info) != BAE_NO_ERROR)
                {
                    SetStatus("Rename failed: could not load song sample");
                    return;
                }
                static char s_sample_rename[256];
                std::snprintf(s_sample_rename, sizeof(s_sample_rename), "%s", name.c_str());
                info.displayName = s_sample_rename;
                const BAEResult r = BAERmfEditorDocument_SetSampleInfo(
                    m_document, sample.document_sample_index, &info);
                if (r != BAE_NO_ERROR)
                {
                    SetStatus(std::string("Rename sample failed: ") + FormatBAEError(r));
                    return;
                }
                m_document_dirty = true;
                RefreshSongSamplesFromDocument();
                SetStatus(std::string("Renamed sample to \"") + name + "\"");
            }
            else
            {
                if (!m_bank_token || sample.snd_resource_id == kNoSndResourceId)
                {
                    SetStatus("Cannot rename this sample here");
                    m_rename_open = false;
                    return;
                }
                if (sample.snd_resource_id > 32767u)
                {
                    SetStatus("Cannot rename sample — SND id out of range");
                    m_rename_open = false;
                    return;
                }
                if (!EnsureActiveBankWritable())
                {
                    return;
                }
                const BAEResult r = BAERmfEditorBank_RenameSampleResource(
                    m_bank_token,
                    static_cast<XShortResourceID>(sample.snd_resource_id),
                    name.c_str());
                if (r != BAE_NO_ERROR)
                {
                    SetStatus(std::string("Rename sample failed: ") + FormatBAEError(r));
                    return;
                }
                RefreshListsFromBank();
                RefreshSongSamplesFromDocument();
                SetStatus(std::string("Renamed sample to \"") + name + "\"");
            }
        }
        m_rename_open = false;
    }

    void DrawRenameDialog()
    {
        if (!m_rename_open)
        {
            return;
        }
        ImGui::OpenPopup("Rename…");
        if (!ImGui::BeginPopupModal("Rename…", &m_rename_open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::Text("New name");
        ImGui::SetNextItemWidth(280.0f);
        const bool enter = ImGui::InputText("##rename",
                                            m_rename_buf,
                                            sizeof(m_rename_buf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("OK", ImVec2(80, 0)) || enter)
        {
            PerformRenameDialogOk();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            m_rename_open = false;
        }
        ImGui::EndPopup();
    }

    void DrawIncludeSamplesPrompt()
    {
        if (!m_clip_include_samples_prompt)
        {
            return;
        }
        ImGui::OpenPopup("Include samples?");
        if (!ImGui::BeginPopupModal("Include samples?",
                                    &m_clip_include_samples_prompt,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextWrapped(
            "Copy instrument(s) to the system clipboard for another nbeditor instance.\n"
            "Include referenced samples (SND) in the package?");
        if (ImGui::Button("Yes — include samples", ImVec2(200, 0)))
        {
            m_clip_include_samples_prompt = false;
            FinishPublishInstrumentClip(true);
        }
        if (ImGui::Button("No — instruments only", ImVec2(200, 0)))
        {
            m_clip_include_samples_prompt = false;
            FinishPublishInstrumentClip(false);
        }
        if (ImGui::Button("Cancel", ImVec2(200, 0)))
        {
            m_clip_include_samples_prompt = false;
            m_clip_pending_inst_list_indices.clear();
            m_clip_cut_instruments_after_publish = false;
        }
        ImGui::EndPopup();
    }

    std::vector<int> CollectTargetInstrumentIndices() const
    {
        std::vector<int> out;
        if (!m_multi_instruments.empty())
        {
            out.assign(m_multi_instruments.begin(), m_multi_instruments.end());
        }
        else if (m_selected_instrument >= 0)
        {
            out.push_back(m_selected_instrument);
        }
        return out;
    }

    std::vector<int> CollectTargetSampleIndices() const
    {
        std::vector<int> out;
        if (!m_multi_samples.empty())
        {
            out.assign(m_multi_samples.begin(), m_multi_samples.end());
        }
        else if (m_selected_sample >= 0)
        {
            out.push_back(m_selected_sample);
        }
        return out;
    }

    std::vector<int> CollectTargetSongIndices() const
    {
        std::vector<int> out;
        if (!m_multi_songs.empty())
        {
            out.assign(m_multi_songs.begin(), m_multi_songs.end());
        }
        else if (m_session_song_index >= 0 && m_groovoid_index < 0)
        {
            out.push_back(m_session_song_index);
        }
        return out;
    }

    bool WriteSongClipPackage(const SessionSongEntry &entry, std::string &out_path)
    {
        out_path.clear();
        if (!entry.rmf_bytes.empty())
        {
            const bool zmf = (entry.source_type == "ZMF") || EndsWith(entry.source_type, "ZMF");
            out_path = NextClipPackagePath(ClipPackageKind::Song, zmf ? ".zmf" : ".rmf");
            std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                out_path.clear();
                return false;
            }
            out.write(reinterpret_cast<const char *>(entry.rmf_bytes.data()),
                      static_cast<std::streamsize>(entry.rmf_bytes.size()));
            return out.good();
        }
        if (!entry.midi_bytes.empty())
        {
            out_path = NextClipPackagePath(ClipPackageKind::Song, ".mid");
            std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                out_path.clear();
                return false;
            }
            out.write(reinterpret_cast<const char *>(entry.midi_bytes.data()),
                      static_cast<std::streamsize>(entry.midi_bytes.size()));
            return out.good();
        }
        return false;
    }

    void CopySongsSelection(bool cut_after)
    {
        const std::vector<int> indices = CollectTargetSongIndices();
        if (indices.empty())
        {
            SetStatus("No song selected");
            return;
        }
        (void)CommitActiveSessionSong();
        m_song_clipboard.clear();
        for (int idx : indices)
        {
            if (idx < 0 || idx >= static_cast<int>(m_session_songs.size()))
            {
                continue;
            }
            m_song_clipboard.push_back(m_session_songs[static_cast<size_t>(idx)]);
        }
        if (m_song_clipboard.empty())
        {
            SetStatus("Nothing to copy");
            return;
        }
        /* Cross-process: write first song as MIDI/RMF temp. */
        std::string path;
        if (!WriteSongClipPackage(m_song_clipboard.front(), path) || path.empty())
        {
            SetStatus("Cannot copy song — no MIDI/RMF bytes available");
            if (!cut_after)
            {
                return;
            }
        }
        else
        {
            PublishClipPackagePathToClipboard(path);
            SetStatus(cut_after ? "Cut song(s) to clipboard package" : "Copied song(s) to clipboard package");
        }
        if (cut_after)
        {
            std::vector<int> sorted = indices;
            std::sort(sorted.begin(), sorted.end(), std::greater<int>());
            for (int idx : sorted)
            {
                RemoveSessionSongAt(idx);
            }
            m_multi_songs.clear();
        }
    }

    void PasteSongsClipboard()
    {
        if (TryPasteFromOsClipboard(ClipPackageKind::Song))
        {
            return;
        }
        if (m_song_clipboard.empty())
        {
            SetStatus("No song on clipboard (copy in this or another nbeditor first)");
            return;
        }
        (void)CommitActiveSessionSong();
        for (const SessionSongEntry &src : m_song_clipboard)
        {
            SessionSongEntry entry = src;
            entry.name = MakeUniqueSessionSongName(src.name + " copy");
            entry.song_resource_id = 0;
            entry.midi_resource_id = 0;
            m_session_songs.push_back(std::move(entry));
        }
        const int new_index = static_cast<int>(m_session_songs.size()) - 1;
        if (new_index >= 0)
        {
            (void)ActivateSessionSongAt(new_index);
        }
        SetStatus("Pasted song(s)");
    }

    void BeginCopyInstrumentsForClip()
    {
        m_clip_pending_inst_list_indices = CollectTargetInstrumentIndices();
        if (m_clip_pending_inst_list_indices.empty())
        {
            m_clip_cut_instruments_after_publish = false;
            SetStatus("No instrument selected");
            return;
        }
        /* In-process clipboard uses first selection (existing Paste dialog). */
        CopyInstrumentAt(m_clip_pending_inst_list_indices.front());
        m_clip_include_samples_prompt = true;
    }

    void FinishPublishInstrumentClip(bool include_samples)
    {
        if (!m_bank_token || m_clip_pending_inst_list_indices.empty())
        {
            m_clip_pending_inst_list_indices.clear();
            m_clip_cut_instruments_after_publish = false;
            return;
        }
        std::vector<int> list_indices = m_clip_pending_inst_list_indices;
        std::vector<XShortResourceID> inst_ids;
        std::vector<XShortResourceID> snd_ids;
        for (int list_index : list_indices)
        {
            if (list_index < 0 || list_index >= static_cast<int>(m_instruments.size()))
            {
                continue;
            }
            const InstrumentRow &row = m_instruments[static_cast<size_t>(list_index)];
            if (row.from_song_document)
            {
                continue;
            }
            const uint32_t iid = row.is_alias ? row.target_inst_id : row.inst_id;
            /* INST id 0 is valid; only skip unset / out-of-range. */
            if (iid != BAE_EDITOR_INST_ID_NONE && iid <= 0xFFFFu)
            {
                inst_ids.push_back(static_cast<XShortResourceID>(static_cast<uint16_t>(iid)));
            }
            if (include_samples)
            {
                CollectSndIdsFromInstResourceId(static_cast<XLongResourceID>(iid), snd_ids);
                uint32_t bank_index = row.instrument_index;
                if (row.is_alias)
                {
                    const int resolved = FindInstrumentIndexByInstId(iid);
                    if (resolved >= 0)
                    {
                        bank_index = static_cast<uint32_t>(resolved);
                    }
                }
                CollectInstrumentSndIds(bank_index, snd_ids);
            }
        }
        m_clip_pending_inst_list_indices.clear();
        if (inst_ids.empty())
        {
            m_clip_cut_instruments_after_publish = false;
            SetStatus("No bank instruments to package");
            return;
        }
        const bool wanted_samples = include_samples;
        const std::string path = NextClipPackagePath(ClipPackageKind::Instrument, ".zsb");
        unsigned snd_copied = 0;
        if (!WriteBankFragmentPackage(inst_ids, snd_ids, path, wanted_samples, &snd_copied))
        {
            m_clip_cut_instruments_after_publish = false;
            SetStatus("Failed to write instrument clip package");
            return;
        }
        PublishClipPackagePathToClipboard(path);
        /* Confirm the OS clipboard is readable from this process. */
        const bool peek_ok = (PeekOsClipboardKind() == ClipPackageKind::Instrument);
        char status[256];
        if (wanted_samples && snd_copied == 0)
        {
            std::snprintf(status,
                          sizeof(status),
                          "%s %u instrument(s) to clipboard (WARNING: no samples found)%s",
                          m_clip_cut_instruments_after_publish ? "Cut" : "Copied",
                          static_cast<unsigned>(inst_ids.size()),
                          peek_ok ? "" : " — clipboard publish FAILED");
        }
        else
        {
            std::snprintf(status,
                          sizeof(status),
                          "%s %u instrument(s)%s to clipboard package%s",
                          m_clip_cut_instruments_after_publish ? "Cut" : "Copied",
                          static_cast<unsigned>(inst_ids.size()),
                          (wanted_samples && snd_copied > 0) ? " + samples" : "",
                          peek_ok ? "" : " — clipboard publish FAILED");
        }
        SetStatus(status);
        if (m_clip_cut_instruments_after_publish)
        {
            m_clip_cut_instruments_after_publish = false;
            RequestDeleteInstruments(list_indices);
            m_multi_instruments.clear();
        }
    }

    void CutInstrumentsSelection()
    {
        m_clip_cut_instruments_after_publish = true;
        BeginCopyInstrumentsForClip();
    }

    void PasteInstrumentsSelection()
    {
        const std::string clip_path = PeekOsClipboardPackagePath(ClipPackageKind::Instrument);
        if (!clip_path.empty())
        {
            XFILENAME xname;
            XConvertPathToXFILENAME(const_cast<char *>(clip_path.c_str()), &xname);
            XFILE donor = XFileOpenResource(&xname, TRUE);
            if (!donor)
            {
                SetStatus("Failed to open instrument clipboard package");
                return;
            }
            std::vector<XShortResourceID> inst_ids;
            std::vector<XShortResourceID> snd_ids;
            CollectAllInstAndSndIdsFromFile(donor, inst_ids, snd_ids);
            XFileClose(donor);
            if (inst_ids.empty())
            {
                SetStatus("Instrument clipboard has no instruments");
                return;
            }
            OpenInstDestDialogForClipPackage(clip_path,
                                             true,
                                             static_cast<uint32_t>(
                                                 static_cast<uint16_t>(inst_ids.front())),
                                             static_cast<int>(inst_ids.size()));
            return;
        }
        if (m_inst_clipboard_valid)
        {
            OpenInstDestDialog(InstDestMode::Paste, -1);
            return;
        }
        SetStatus("Instrument clipboard is empty");
    }

    void CopySamplesSelection(bool cut_after)
    {
        const std::vector<int> indices = CollectTargetSampleIndices();
        if (indices.empty())
        {
            SetStatus("No sample selected");
            return;
        }
        m_sample_clipboard_snd_ids.clear();
        std::vector<XShortResourceID> snd_ids;
        for (int idx : indices)
        {
            if (idx < 0 || idx >= static_cast<int>(m_samples.size()))
            {
                continue;
            }
            const SampleRow &sample = m_samples[static_cast<size_t>(idx)];
            if (sample.source != SampleSource::Bank || sample.snd_resource_id > 0xFFFEu)
            {
                continue;
            }
            m_sample_clipboard_snd_ids.push_back(sample.snd_resource_id);
            snd_ids.push_back(
                static_cast<XShortResourceID>(static_cast<uint16_t>(sample.snd_resource_id)));
        }
        if (snd_ids.empty())
        {
            SetStatus("No bank samples to copy");
            return;
        }
        const std::string path = NextClipPackagePath(ClipPackageKind::Sample, ".zsb");
        if (!WriteBankFragmentPackage({}, snd_ids, path, false, nullptr))
        {
            SetStatus("Failed to write sample clip package");
            return;
        }
        PublishClipPackagePathToClipboard(path);
        SetStatus(cut_after ? "Cut sample(s)" : "Copied sample(s)");
        if (cut_after)
        {
            DeleteSamplesAtListIndices(indices);
        }
    }

    void PasteSamplesSelection()
    {
        if (TryPasteFromOsClipboard(ClipPackageKind::Sample))
        {
            return;
        }
        if (m_sample_clipboard_snd_ids.empty() || !m_bank_token)
        {
            SetStatus("Sample clipboard is empty");
            return;
        }
        /* In-process: re-import same IDs from active bank via a temp package. */
        std::vector<XShortResourceID> snd_ids;
        for (uint32_t id : m_sample_clipboard_snd_ids)
        {
            if (id <= 0xFFFEu)
            {
                snd_ids.push_back(static_cast<XShortResourceID>(static_cast<uint16_t>(id)));
            }
        }
        const std::string path = NextClipPackagePath(ClipPackageKind::Sample, ".zsb");
        if (!WriteBankFragmentPackage({}, snd_ids, path, false, nullptr))
        {
            SetStatus("Failed to stage sample paste");
            return;
        }
        (void)ImportClipPackageFromPath(path.c_str(), true);
    }

    void SessionSelectAllVisible()
    {
        switch (m_session_tab)
        {
        case SessionTab::Songs:
            /* Groovoids are not in m_multi_songs; only select session songs when shown. */
            m_multi_songs.clear();
            if (m_song_show_filter != SongShowFilter::Groovoids)
            {
                for (int i = 0; i < static_cast<int>(m_session_songs.size()); ++i)
                {
                    m_multi_songs.insert(i);
                }
            }
            break;
        case SessionTab::Instruments:
        {
            m_multi_instruments.clear();
            const InstrumentFilterOption *active_filter = nullptr;
            if (m_instrument_filter_index >= 0 &&
                m_instrument_filter_index < static_cast<int>(m_instrument_filters.size()))
            {
                active_filter =
                    &m_instrument_filters[static_cast<size_t>(m_instrument_filter_index)];
            }
            const bool used_by_song = active_filter && active_filter->bank == -3;
            if (used_by_song)
            {
                if (m_document_dirty)
                {
                    m_song_used_keys_dirty = true;
                }
                if (m_song_used_keys_dirty)
                {
                    CollectCurrentSongUsedInstrumentKeys(m_song_used_inst_ids,
                                                         m_song_used_instrument_indexes);
                    m_song_used_keys_dirty = false;
                }
            }
            for (int i = 0; i < static_cast<int>(m_instruments.size()); ++i)
            {
                const InstrumentRow &inst = m_instruments[static_cast<size_t>(i)];
                if (!InstrumentMatchesFilter(inst))
                {
                    continue;
                }
                if (used_by_song &&
                    !InstrumentUsedByCurrentSong(inst,
                                                 m_song_used_inst_ids,
                                                 m_song_used_instrument_indexes))
                {
                    continue;
                }
                if (!InstrumentMatchesTextFilter(inst))
                {
                    continue;
                }
                m_multi_instruments.insert(i);
            }
            break;
        }
        case SessionTab::Samples:
            m_multi_samples.clear();
            for (int i = 0; i < static_cast<int>(m_samples.size()); ++i)
            {
                const SampleRow &sample = m_samples[static_cast<size_t>(i)];
                if (!SampleMatchesShowFilter(sample) || !SampleMatchesTextFilter(sample))
                {
                    continue;
                }
                m_multi_samples.insert(i);
            }
            break;
        }
    }

    /* Session list Ctrl/Del shortcuts live in HandleGlobalAppShortcuts so they
     * work when Player (or other non-editor) windows are focused. */
