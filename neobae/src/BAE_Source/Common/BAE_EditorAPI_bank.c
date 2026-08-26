/*
 * © 2021–2026 zefie
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
/*****************************************************************************/
/*
**  BAE_EditorAPI_bank.c
**
**  Bank enum/mutate/pack/batch/save/export. Hot paths: neobae/docs/BAE_EditorAPI_PERF.md
*/
/*****************************************************************************/

#include "BAE_EditorAPI_Internal.h"

/* miscParameter2 is split/header volume only when neither sample-offset nor
 * sound-modifier mode owns those words (see X_Formats.h). */
static bool PV_InstMiscParameter2IsVolume(unsigned char flags2)
{
    return !TEST_FLAG_VALUE(flags2, ZBF_enableSampleOffsetStart) &&
           !TEST_FLAG_VALUE(flags2, ZBF_enableSoundModifier);
}


BAE_BOOL BAERmfEditorBank_RequiresZsb(BAEBankToken bankToken, uint32_t *outReason)
{
    uint32_t instrumentCount;
    uint32_t instrumentIndex;
    uint32_t reason = 0;

    if (!bankToken)
    {
        return FALSE;
    }

    instrumentCount = 0;
    if (BAERmfEditorBank_GetInstrumentCount(bankToken, &instrumentCount) != BAE_NO_ERROR)
    {
        return FALSE;
    }

    for (instrumentIndex = 0; instrumentIndex < instrumentCount; ++instrumentIndex)
    {
        BAERmfEditorInstrumentExtInfo extInfo;
        BAERmfEditorBankSampleInfo firstSampleInfo;
        uint32_t sampleCount;
        uint32_t sampleIndex;
        bool usesLpf = FALSE;
        bool hasStereoSample = FALSE;

        if (BAERmfEditorBank_GetInstrumentExtInfo(bankToken, instrumentIndex, &extInfo) == BAE_NO_ERROR)
        {
            bool hasFilterLfo = FALSE;
            uint32_t lfoIdx;

            if ((reason & BAEZMF_REASON_EXTENDED_ADSR) == 0)
            {
                if (extInfo.volumeADSR.stageCount > BAE_RMF_MAX_ADSR_STAGES)
                {
                    reason |= BAEZMF_REASON_EXTENDED_ADSR;
                }
                else
                {
                    for (lfoIdx = 0; lfoIdx < extInfo.lfoCount && lfoIdx < BAE_EDITOR_MAX_LFOS; ++lfoIdx)
                    {
                        if (extInfo.lfos[lfoIdx].adsr.stageCount > BAE_RMF_MAX_ADSR_STAGES)
                        {
                            reason |= BAEZMF_REASON_EXTENDED_ADSR;
                            break;
                        }
                    }
                }
            }
            if (TEST_FLAG_VALUE(extInfo.flags2, ZBF_advancedInterpolation))
            {
#if defined(_DEBUG) && (_DEBUG != 0)
                if ((reason & BAEZMF_REASON_CUBIC_INTERPOLATION) == 0)
                {
                    uint32_t debugSndID = 0;
                    if (BAERmfEditorBank_GetInstrumentSampleInfo(bankToken,
                                                                 instrumentIndex,
                                                                 0,
                                                                 &firstSampleInfo) == BAE_NO_ERROR)
                    {
                        debugSndID = (uint32_t)(uint16_t)firstSampleInfo.sndResourceID;
                    }
                    debug_message("[BankRequiresZsb] TRIP reason=advanced-interpolation instIndex=%u sndID=%u flags2=0x%02X\n",
                               (unsigned)instrumentIndex,
                               (unsigned)debugSndID,
                               (unsigned)extInfo.flags2);
                }
#endif
                reason |= BAEZMF_REASON_CUBIC_INTERPOLATION;
            }
            for (lfoIdx = 0; lfoIdx < extInfo.lfoCount && lfoIdx < BAE_EDITOR_MAX_LFOS; ++lfoIdx)
            {
                if (PV_LfoDestinationIsLpf(extInfo.lfos[lfoIdx].destination))
                {
                    hasFilterLfo = TRUE;
                    break;
                }
            }
            /* Classic Beatnik supports mono LPF; stereo filter needs ZSB. */
            usesLpf = PV_InstrumentUsesLpfFilter(extInfo.LPF_frequency,
                                                 extInfo.LPF_resonance,
                                                 extInfo.LPF_lowpassAmount,
                                                 hasFilterLfo);
        }

        sampleCount = 0;
        if (BAERmfEditorBank_GetInstrumentSampleCount(bankToken, instrumentIndex, &sampleCount) != BAE_NO_ERROR)
        {
            continue;
        }

        for (sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
        {
            BAERmfEditorBankSampleInfo sampleInfo;
            uint32_t compressionType;

            if (BAERmfEditorBank_GetInstrumentSampleInfo(bankToken,
                                                         instrumentIndex,
                                                         sampleIndex,
                                                         &sampleInfo) != BAE_NO_ERROR)
            {
                continue;
            }

            if (sampleInfo.channels > 1)
            {
                hasStereoSample = TRUE;
            }

            if (sampleInfo.loopEnd > sampleInfo.loopStart)
            {
                uint32_t loopLength = sampleInfo.loopEnd - sampleInfo.loopStart;
                if (loopLength >= MIN_LOOP_SIZE_ZMF && loopLength < MIN_LOOP_SIZE_RMF)
                {
#if defined(_DEBUG) && (_DEBUG != 0)
                    if ((reason & BAEZMF_REASON_LOOP_TOO_SHORT) == 0)
                    {
                        debug_message("[BankRequiresZsb] TRIP reason=short-loop-rmf-window instIndex=%u sndID=%u loopStart=%u loopEnd=%u loopLen=%u minZmf=%u minRmf=%u\n",
                                   (unsigned)instrumentIndex,
                                   (unsigned)(uint16_t)sampleInfo.sndResourceID,
                                   (unsigned)sampleInfo.loopStart,
                                   (unsigned)sampleInfo.loopEnd,
                                   (unsigned)loopLength,
                                   (unsigned)MIN_LOOP_SIZE_ZMF,
                                   (unsigned)MIN_LOOP_SIZE_RMF);
                    }
#endif
                    reason |= BAEZMF_REASON_LOOP_TOO_SHORT;
                }
            }

            compressionType = (uint32_t)sampleInfo.compressionType;
            if (
#if USE_FLAC_DECODER == TRUE || USE_FLAC_ENCODER == TRUE
                compressionType == (uint32_t)C_FLAC ||
#endif                
#if USE_VORBIS_DECODER == TRUE || USE_VORBIS_ENCODER == TRUE
                compressionType == (uint32_t)C_VORBIS ||
#endif
#if USE_OPUS_DECODER == TRUE || USE_OPUS_ENCODER == TRUE
                compressionType == (uint32_t)C_OPUS ||
#endif
#if USE_QOA_SUPPORT == TRUE
                compressionType == (uint32_t)C_QOA ||
#endif
                compressionType == (uint32_t)C_IMA2 ||
            false)
            {
#if defined(_DEBUG) && (_DEBUG != 0)
                if ((reason & BAEZMF_REASON_MODERN_CODEC) == 0)
                {
                    debug_message("[BankRequiresZsb] TRIP reason=modern-codec instIndex=%u sndID=%u compressionType=0x%08X\n",
                               (unsigned)instrumentIndex,
                               (unsigned)(uint16_t)sampleInfo.sndResourceID,
                               (unsigned)compressionType);
                }
#endif
                reason |= BAEZMF_REASON_MODERN_CODEC;
                if (compressionType == (uint32_t)C_IMA2)
                {
                    reason |= BAEZMF_REASON_ADPCM_2BIT;
                }
            }
        }

        if (usesLpf && hasStereoSample && (reason & BAEZMF_REASON_LPF_FILTER) == 0)
        {
#if defined(_DEBUG) && (_DEBUG != 0)
            debug_message("[BankRequiresZsb] TRIP reason=stereo-lpf-filter instIndex=%u freq=%ld res=%ld amt=%ld\n",
                          (unsigned)instrumentIndex,
                          (long)extInfo.LPF_frequency,
                          (long)extInfo.LPF_resonance,
                          (long)extInfo.LPF_lowpassAmount);
#endif
            reason |= BAEZMF_REASON_LPF_FILTER;
        }
#if USE_ZMF_SUPPORT == TRUE
        if (extInfo.useOscillator)
        {
            reason |= BAEZMF_REASON_OSCILLATOR;
        }
#endif
        /* Boolean probes (outReason == NULL) only need any hit — zpatches-sized
         * banks were spending ~0.5–1s scanning every sample for save-path checks. */
        if (outReason == NULL && reason != BAEZMF_REASON_NONE)
        {
            break;
        }
    }
    if (outReason)
    {
        *outReason = reason;
    }
    return (reason != BAEZMF_REASON_NONE) ? TRUE : FALSE;
}


#if USE_ZMF_SUPPORT == TRUE
bool PV_BankNeedsZmfV6(BAEBankToken bankToken)
{
    uint32_t reason = 0;

    if (!bankToken)
    {
        return FALSE;
    }
    (void)BAERmfEditorBank_RequiresZsb(bankToken, &reason);
    return ((reason & (BAEZMF_REASON_OSCILLATOR | BAEZMF_REASON_ADPCM_2BIT)) != 0)
               ? TRUE
               : FALSE;
}


bool PV_BankHasOscillator(BAEBankToken bankToken)
{
    return PV_BankNeedsZmfV6(bankToken);
}
#endif /* USE_ZMF_SUPPORT */

/* ---------- Bank instrument enumeration and cloning ---------- */

BAEResult BAERmfEditorBank_GetInstrumentCount(BAEBankToken bankToken,
                                              uint32_t *outCount)
{
    XFILE bankFile;
    int32_t count;

    if (!outCount)
    {
        return BAE_PARAM_ERR;
    }
    *outCount = 0;
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;
    count = XCountFileResourcesOfType(bankFile, ID_INST);
    if (count < 0)
    {
        count = 0;
    }
    *outCount = (uint32_t)count;
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_GetInstrumentInfo(BAEBankToken bankToken,
                                             uint32_t instrumentIndex,
                                             BAERmfEditorBankInstrumentInfo *outInfo)
{
    enum
    {
        kInstHeaderMinSize = 14
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char rawName[256];
    InstrumentResource *inst;

    if (!outInfo)
    {
        return BAE_PARAM_ERR;
    }
    XSetMemory(outInfo, (int32_t)sizeof(*outInfo), 0);
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    rawName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, rawName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }
    inst = (InstrumentResource *)instData;

    outInfo->instID = (uint32_t)instID;
    PV_DecodeResourceName(rawName, outInfo->name);
    outInfo->program = (unsigned char)(instID % 128);
    outInfo->bank = (uint16_t)(instID / 256);
    outInfo->keySplitCount = (int16_t)XGetShort(&inst->keySplitCount);
    outInfo->flags1 = inst->flags1;
    outInfo->flags2 = inst->flags2;

    XDisposePtr(instData);
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_ResolveInstID(BAEBankToken bankToken,
                                         uint32_t instID,
                                         uint32_t *outResolvedInstID,
                                         uint32_t *outInstrumentIndex)
{
    XFILE bankFile;
    XFILENAME *pReference;
    int32_t totalInst;
    int32_t i;
    uint32_t resolvedID;
    int32_t flatCount;

    if (!outResolvedInstID || !outInstrumentIndex)
    {
        return BAE_PARAM_ERR;
    }
    *outResolvedInstID = instID;
    *outInstrumentIndex = 0;
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;
    pReference = (XFILENAME *)bankFile;
    if (pReference->pCache == NULL)
    {
        XCreateAccessCache(bankFile);
    }

    /* Try the requested ID first, then fall back to alias resolution. */
    resolvedID = instID;
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass == 1)
        {
            XAliasLinkResource *pAlias;
            XLongResourceID aliasTarget;

            pAlias = XGetAliasLinkFromFile(bankFile);
            if (!pAlias)
            {
                return BAE_BAD_FILE;
            }
            if (XLookupAlias(pAlias, (XLongResourceID)instID, &aliasTarget) != 0)
            {
                XDisposePtr((XPTR)pAlias);
                return BAE_BAD_FILE;
            }
            XDisposePtr((XPTR)pAlias);
            resolvedID = (uint32_t)aliasTarget;
        }

        /* Flat INST ids are already in the access cache — no body loads. */
        flatCount = 0;
        if (pReference->pCache)
        {
            int32_t found = 0;
            for (i = 0; i < pReference->pCache->totalResources; ++i)
            {
                if (pReference->pCache->cached[i].resourceType == ID_INST)
                {
                    if ((uint32_t)pReference->pCache->cached[i].resourceID == resolvedID)
                    {
                        *outResolvedInstID = resolvedID;
                        *outInstrumentIndex = (uint32_t)found;
                        return BAE_NO_ERROR;
                    }
                    found++;
                }
            }
            flatCount = found;
        }

        /* Fall back to indexed get (covers ZINS-packed INST). ZINS decode is
         * cached inside X_API so repeated resolves are cheap. */
        totalInst = XCountFileResourcesOfType(bankFile, ID_INST);
        for (i = flatCount; i < totalInst; ++i)
        {
            XLongResourceID thisID;
            int32_t size;
            XPTR data;

            data = XGetIndexedFileResource(bankFile, ID_INST, &thisID,
                                           i, NULL, &size);
            if (data)
            {
                XDisposePtr(data);
                if ((uint32_t)thisID == resolvedID)
                {
                    *outResolvedInstID = resolvedID;
                    *outInstrumentIndex = (uint32_t)i;
                    return BAE_NO_ERROR;
                }
            }
        }
    }
    return BAE_BAD_FILE;
}


/* ---------- Bank sample enumeration and editing ---------- */

BAEResult BAERmfEditorBank_GetInstrumentSampleCount(BAEBankToken bankToken,
                                                     uint32_t instrumentIndex,
                                                     uint32_t *outCount)
{
    enum
    {
        kInstHeaderMinSize = 14
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char rawName[256];
    InstrumentResource *inst;
    int16_t splitCount;

    if (!outCount)
    {
        return BAE_PARAM_ERR;
    }
    *outCount = 0;
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    rawName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, rawName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }
    inst = (InstrumentResource *)instData;
    splitCount = (int16_t)XGetShort(&inst->keySplitCount);
    if (splitCount < 0)
    {
        splitCount = 0;
    }

    /* Non-split instruments return 1 (the base sample), split instruments return the split count */
    *outCount = (splitCount > 0) ? (uint32_t)splitCount : 1;

    XDisposePtr(instData);
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_GetInstrumentSampleInfo(BAEBankToken bankToken,
                                                    uint32_t instrumentIndex,
                                                    uint32_t sampleIndex,
                                                    BAERmfEditorBankSampleInfo *outInfo)
{
    enum
    {
        kInstHeaderMinSize = 14,
        kInstKeySplitSize = 8
    };
    XFILE bankFile;
    XPTR instData;
    XPTR sndData;
    XLongResourceID instID;
    int32_t instSize;
    char rawName[256];
    InstrumentResource *inst;
    KeySplit split;
    XShortResourceID sndID;
    SampleDataInfo sampleInfo;
    int16_t splitCount;
    int16_t baseRootKey;
    int16_t baseVolume;
    bool useSoundModifierAsRootKey;
    int16_t miscParam1;

    if (!outInfo)
    {
        return BAE_PARAM_ERR;
    }
    XSetMemory(outInfo, (int32_t)sizeof(*outInfo), 0);
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    rawName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, rawName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }
    inst = (InstrumentResource *)instData;

    splitCount = (int16_t)XGetShort(&inst->keySplitCount);
    if (splitCount < 0)
    {
        splitCount = 0;
    }

    /* Validate sampleIndex */
    if (splitCount > 0)
    {
        if (sampleIndex >= (uint32_t)splitCount)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }
    }
    else
    {
        if (sampleIndex != 0)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }
    }

    outInfo->instID = (uint32_t)instID;
    outInfo->sampleIndex = sampleIndex;
    baseRootKey = (int16_t)XGetShort(&inst->midiRootKey);
    baseVolume = (int16_t)XGetShort(&inst->miscParameter2);
    useSoundModifierAsRootKey = TEST_FLAG_VALUE(inst->flags2, ZBF_useSoundModifierAsRootKey);
    miscParam1 = (int16_t)XGetShort(&inst->miscParameter1);

    if (splitCount > 0)
    {
        /* This is a split instrument - get info from the key split */
        XGetKeySplitFromPtr(inst, (int16_t)sampleIndex, &split);
        sndID = split.sndResourceID;

        outInfo->lowKey = (unsigned char)split.lowMidi;
        outInfo->highKey = (unsigned char)split.highMidi;
        /* Do not expose offset/smod words as "Vol" (e.g. 192 half-word). */
        outInfo->splitVolume = PV_InstMiscParameter2IsVolume(inst->flags2)
                                   ? split.miscParameter2
                                   : (int16_t)0;

        /* Sample rootKey for editor: when useSoundModifierAsRootKey, miscParameter1
         * is the root; otherwise the SND baseFrequency is
         * (INST midiRootKey is masterRootKey transpose, not the sample root). */
        if (useSoundModifierAsRootKey)
        {
            int16_t splitRoot = split.miscParameter1;
            if (split.lowMidi == split.highMidi && splitRoot == 0)
            {
                splitRoot = (int16_t)split.lowMidi;
            }
            if (splitRoot < 0 || splitRoot > 127)
            {
                splitRoot = baseRootKey;
            }
            outInfo->rootKey = (unsigned char)splitRoot;
        }
        else
        {
            outInfo->rootKey = 0; /* filled from SND baseKey below */
        }
    }
    else
    {
        /* Non-split instrument - use base sample */
        sndID = (XShortResourceID)XGetShort(&inst->sndResourceID);
        outInfo->lowKey = 0;
        outInfo->highKey = 127;
        outInfo->splitVolume = PV_InstMiscParameter2IsVolume(inst->flags2)
                                   ? baseVolume
                                   : (int16_t)0;

        if (useSoundModifierAsRootKey)
        {
            /* Root key 0 is valid (C-1). */
            if (miscParam1 >= 0 && miscParam1 <= 127)
            {
                outInfo->rootKey = (unsigned char)miscParam1;
            }
            else
            {
                outInfo->rootKey = (unsigned char)baseRootKey;
            }
        }
        else
        {
            outInfo->rootKey = 0; /* filled from SND baseKey below */
        }
    }

    outInfo->sndResourceID = sndID;

    /* Get SND resource info — prefer ESND/CSND over plain SND (promote collisions). */
    {
        int32_t sndSize;
        static const XResourceType sndTypes[] = { ID_ESND, ID_CSND, ID_SND, 0 };
        int32_t typeIdx;
        XResourceType foundSndType;
        bool rootFilled = useSoundModifierAsRootKey; /* INST/split misc already set */

        sndData = NULL;
        foundSndType = 0;
        for (typeIdx = 0; sndTypes[typeIdx] != 0; ++typeIdx)
        {
            sndData = XGetFileResource(bankFile, sndTypes[typeIdx], (XLongResourceID)sndID, NULL, &sndSize);
            if (sndData)
            {
                foundSndType = sndTypes[typeIdx];
                /* Decompress CSND (LZSS) or decrypt ESND before reading header */
                if (sndTypes[typeIdx] == ID_CSND)
                {
                    XPTR decompressed = XDecompressPtr(sndData, (uint32_t)sndSize, FALSE);
                    XDisposePtr(sndData);
                    sndData = decompressed;
                    if (sndData)
                    {
                        sndSize = XGetPtrSize(sndData);
                    }
                }
                else if (sndTypes[typeIdx] == ID_ESND)
                {
                    XDecryptData(sndData, (uint32_t)sndSize);
                }
                break;
            }
        }
        if (sndData && sndSize > 0)
        {
            /* Validate the SND resource has a recognizable sound header before
             * calling XGetSampleInfoFromSnd, which asserts on invalid data.
             * A valid SND resource starts with a format type at offset 0:
             * 1 = XFirstSoundFormat, 2 = XSecondSoundFormat, 3 = XThirdSoundFormat */
            int16_t soundFormat = (int16_t)XGetShort(sndData);
            if (soundFormat == 1 || soundFormat == 2 || soundFormat == 3)
            {
                XSetMemory(&sampleInfo, (int32_t)sizeof(sampleInfo), 0);
                if (XGetSampleInfoFromSnd(sndData, &sampleInfo) == 0)
                {
                    outInfo->sampleRate = (uint32_t)XFIXED_TO_UNSIGNED_LONG(sampleInfo.rate);
                    outInfo->frameCount = sampleInfo.frames;
                    outInfo->bitDepth = sampleInfo.bitSize;
                    outInfo->channels = sampleInfo.channels;
                    outInfo->loopStart = sampleInfo.loopStart;
                    outInfo->loopEnd = sampleInfo.loopEnd;
                    outInfo->compressionType = sampleInfo.compressionType;
                    outInfo->compressionSubType = PV_GetStoredCompressionSubTypeFromSnd(
                        sndData, sndSize, (uint32_t)sampleInfo.compressionType);
                    outInfo->opusRoundTripResample = XGetSoundOpusRoundTripFlag(sndData);
                    /* When !useSoundModifierAsRootKey, editor root is SND baseKey.
                     * Do not overwrite a legitimate INST/split root of 0. */
                    if (!useSoundModifierAsRootKey && outInfo->rootKey == 0)
                    {
                        if (sampleInfo.baseKey >= 0 && sampleInfo.baseKey <= 127)
                        {
                            outInfo->rootKey = (unsigned char)sampleInfo.baseKey;
                            rootFilled = TRUE;
                        }
                    }
                }
            }
            XDisposePtr(sndData);
        }

        if (!rootFilled)
        {
            if (outInfo->lowKey <= 127 && outInfo->highKey <= 127 &&
                outInfo->lowKey == outInfo->highKey)
            {
                outInfo->rootKey = outInfo->lowKey;
            }
            else if (!useSoundModifierAsRootKey && baseRootKey >= 0 && baseRootKey <= 127)
            {
                /* Last resort only — midiRootKey is often 60 ("no transpose"). */
                outInfo->rootKey = (unsigned char)baseRootKey;
            }
            else
            {
                outInfo->rootKey = 60;
            }
        }

        /* Report the container type so callers can reflect it in the UI */
        if (foundSndType == ID_CSND)
        {
            outInfo->sndStorageType = BAE_EDITOR_SND_STORAGE_CSND;
        }
        else if (foundSndType == ID_SND)
        {
            outInfo->sndStorageType = BAE_EDITOR_SND_STORAGE_SND;
        }
        else
        {
            outInfo->sndStorageType = BAE_EDITOR_SND_STORAGE_ESND; /* default / ESND */
        }
    }

    XDisposePtr(instData);
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_GetInstrumentExtInfo(BAEBankToken bankToken,
                                                 uint32_t instrumentIndex,
                                                 BAERmfEditorInstrumentExtInfo *outInfo)
{
    enum
    {
        kInstHeaderMinSize = 14
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char rawName[256];
    char instName[256];
    uint32_t i;

    if (!outInfo)
    {
        return BAE_PARAM_ERR;
    }
    XSetMemory(outInfo, (int32_t)sizeof(*outInfo), 0);
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    rawName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, rawName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    /* Get basic INST header info.
     * displayName must not point at this function's stack — that dangled and
     * corrupted nbeditor titles after keyboard preview reused the stack.
     * Static is valid until the next GetInstrumentExtInfo; callers that keep
     * the pointer across calls should copy (nbeditor does). */
    PV_DecodeResourceName(rawName, instName);
    outInfo->instID = (uint32_t)instID;
    {
        static char s_bankExtDisplayName[256];
        s_bankExtDisplayName[0] = 0;
        if (instName[0])
        {
            XBlockMove(instName, s_bankExtDisplayName, (int32_t)strlen(instName) + 1);
            outInfo->displayName = s_bankExtDisplayName;
        }
        else
        {
            outInfo->displayName = NULL;
        }
    }
    outInfo->flags1 = ((unsigned char *)instData)[5];
    /* Ghost is RMF/ZMF document-only; ignore if present on bank INST bytes. */
    outInfo->flags1 = (unsigned char)(outInfo->flags1 & (unsigned char)~ZBF_ghostInstrument);
    outInfo->flags2 = ((unsigned char *)instData)[6];
    outInfo->panPlacement = ((unsigned char *)instData)[4];
    outInfo->midiRootKey = (int16_t)XGetShort(((unsigned char *)instData) + 2);
    outInfo->miscParameter1 = (int16_t)XGetShort(((unsigned char *)instData) + 8);
    outInfo->miscParameter2 = (int16_t)XGetShort(((unsigned char *)instData) + 10);

    /* Parse extended data using existing function */
    {
        BAERmfEditorInstrumentExt extData;
        PV_ParseExtendedInstData(instData, instSize, &extData);
        outInfo->hasExtendedData = extData.hasExtendedData;
        outInfo->hasDefaultMod = extData.hasDefaultMod;
        outInfo->LPF_frequency = extData.LPF_frequency;
        outInfo->LPF_resonance = extData.LPF_resonance;
        outInfo->LPF_lowpassAmount = extData.LPF_lowpassAmount;
        outInfo->defaultReverbSend = extData.defaultReverbSend;
        outInfo->defaultChorusSend = extData.defaultChorusSend;
#if USE_ZMF_SUPPORT == TRUE
        outInfo->useOscillator = extData.useOscillator ? TRUE : FALSE;
        outInfo->oscWaveShape = extData.oscWaveShape ? extData.oscWaveShape : (int32_t)SINE_WAVE_LONG;
        outInfo->oscPulseWidth = extData.oscPulseWidth > 0 ? extData.oscPulseWidth : 32768;
        outInfo->oscVolume = extData.oscVolume;
        if (outInfo->oscVolume < 0)
            outInfo->oscVolume = 0;
        if (outInfo->oscVolume > 65536)
            outInfo->oscVolume = 65536;
#endif
        PV_CopyEditorADSRToInfo(&extData.volumeADSR, &outInfo->volumeADSR);
        outInfo->lfoCount = extData.lfoCount;
        for (i = 0; i < extData.lfoCount && i < BAE_EDITOR_MAX_LFOS; i++)
        {
            outInfo->lfos[i].destination = extData.lfos[i].destination;
            outInfo->lfos[i].period = extData.lfos[i].period;
            outInfo->lfos[i].waveShape = extData.lfos[i].waveShape;
            outInfo->lfos[i].DC_feed = extData.lfos[i].DC_feed;
            outInfo->lfos[i].level = extData.lfos[i].level;
            PV_CopyEditorADSRToInfo(&extData.lfos[i].adsr, &outInfo->lfos[i].adsr);
        }
        outInfo->curveCount = extData.curveCount;
        for (i = 0; i < extData.curveCount && i < BAE_EDITOR_MAX_CURVES; i++)
        {
            uint32_t j;
            outInfo->curves[i].tieFrom = extData.curves[i].tieFrom;
            outInfo->curves[i].tieTo = extData.curves[i].tieTo;
            outInfo->curves[i].curveCount = extData.curves[i].curveCount;
            for (j = 0; j < extData.curves[i].curveCount && j < BAE_EDITOR_MAX_ADSR_STAGES; j++)
            {
                outInfo->curves[i].from_Value[j] = extData.curves[i].from_Value[j];
                outInfo->curves[i].to_Scalar[j] = extData.curves[i].to_Scalar[j];
            }
        }
    }

    XDisposePtr(instData);
    return BAE_NO_ERROR;
}


/* INST-only replace. Prefer XReplaceFileResource (flat trash+append or ZINS
 * shadow); fall back to full replace only if that fails. The old "InPlace"
 * path rebuilt every INST+SND and made keymap Vol edits take seconds on
 * zpatches-sized ZSB. */
/* Prefer XReplace (flat trash+append / ZINS shadow). Fall back to full type-list
 * rebuild only when Replace cannot run — never PackSong/PackBank (see Replace). */
static BAEResult PV_BankCommitResource(XFILE bankFile,
                                       XResourceType resourceType,
                                       XLongResourceID resourceID,
                                       char const *pascalName,
                                       XPTR data,
                                       int32_t size)
{
    if (XReplaceFileResource(bankFile,
                             resourceType,
                             resourceID,
                             pascalName,
                             data,
                             size) != FALSE)
    {
        return BAE_NO_ERROR;
    }
    return PV_BankReplaceResource(bankFile,
                                  resourceType,
                                  resourceID,
                                  pascalName,
                                  data,
                                  size);
}

BAEResult PV_BankReplaceInstResourceInPlace(XFILE bankFile,
                                                   XLongResourceID instID,
                                                   char const *pascalName,
                                                   XPTR instData,
                                                   int32_t instSize)
{
    /* Metadata-only INST edits (keymap Vol/Low/High) must never fall back to a
     * full-bank rebuild: on read-only banks that path PackSong/PackBank LZMA'd
     * zpatches for seconds and could leave split volumes wrong after refresh.
     * Callers should EnsureWritable first so XReplace succeeds. */
    if (XReplaceFileResource(bankFile,
                             ID_INST,
                             instID,
                             pascalName,
                             instData,
                             instSize) != FALSE)
    {
        return BAE_NO_ERROR;
    }
    return BAE_FILE_IO_ERROR;
}

BAEResult PV_BankReplaceSndResourceInPlace(XFILE bankFile,
                                                  XResourceType oldSndType,
                                                  XResourceType newSndType,
                                                  XShortResourceID sndID,
                                                  char const *pascalName,
                                                  XPTR sndData,
                                                  int32_t sndSize)
{
    /* Batch mode: queue the replacement instead of rebuilding now */
    if (bankFile->pendingSndBatch != NULL)
    {
        struct XFilePendingSnd *entry;
        XPTR dataCopy;

        /* Grow the array if needed */
        if (bankFile->pendingSndCount >= bankFile->pendingSndCapacity)
        {
            int32_t newCap = bankFile->pendingSndCapacity ? bankFile->pendingSndCapacity * 2 : 64;
            struct XFilePendingSnd *newArr = (struct XFilePendingSnd *)XNewPtr(
                (int32_t)(newCap * (int32_t)sizeof(struct XFilePendingSnd)));
            if (!newArr)
            {
                return BAE_MEMORY_ERR;
            }
            if (bankFile->pendingSndBatch)
            {
                XBlockMove(bankFile->pendingSndBatch, newArr,
                           bankFile->pendingSndCount * (int32_t)sizeof(struct XFilePendingSnd));
                XDisposePtr(bankFile->pendingSndBatch);
            }
            bankFile->pendingSndBatch = newArr;
            bankFile->pendingSndCapacity = newCap;
        }

        /* Copy the SND data so the caller can free their copy */
        dataCopy = XNewPtr(sndSize);
        if (!dataCopy)
        {
            return BAE_MEMORY_ERR;
        }
        XBlockMove(sndData, dataCopy, sndSize);

        entry = &bankFile->pendingSndBatch[bankFile->pendingSndCount++];
        entry->oldType = (int32_t)oldSndType;
        entry->newType = (int32_t)newSndType;
        entry->sndID   = (int16_t)sndID;
        entry->data    = dataCopy;
        entry->size    = sndSize;
        entry->name[0] = 0;
        if (pascalName)
        {
            int32_t i;
            for (i = 0; i < 255 && pascalName[i]; ++i)
            {
                entry->name[i] = pascalName[i];
            }
            entry->name[i] = 0;
        }
        return BAE_NO_ERROR;
    }

    /* Same-type: O(1) flat trash+append when possible. */
    if (oldSndType == newSndType &&
        XReplaceFileResource(bankFile,
                             oldSndType,
                             (XLongResourceID)sndID,
                             pascalName,
                             sndData,
                             sndSize) != FALSE)
    {
        return BAE_NO_ERROR;
    }

    /* Type change on a writable bank: retire old container as GONE + append new
     * type. Avoids full SND-table rebuild (zpatches freeze) and does not pollute
     * Edit→Trash the way legacy XDeleteFileResource→TRSH did. */
    if (oldSndType != newSndType)
    {
        (void)XRetireFileResource(bankFile, oldSndType, (XLongResourceID)sndID);
        if (XAddFileResource(bankFile,
                             newSndType,
                             (XLongResourceID)sndID,
                             pascalName,
                             sndData,
                             sndSize) == 0)
        {
            return BAE_NO_ERROR;
        }
    }

    /* Replace unavailable (read-only / no flat entry): one opaque rebuild. */
    {
        PV_SndReplacement one;
        int32_t ni;

        XSetMemory(&one, (int32_t)sizeof(one), 0);
        one.oldType = oldSndType;
        one.newType = newSndType;
        one.sndID = sndID;
        one.data = sndData;
        one.size = sndSize;
        one.name[0] = 0;
        if (pascalName)
        {
            for (ni = 0; ni < 255 && pascalName[ni]; ++ni)
            {
                one.name[ni] = pascalName[ni];
            }
            one.name[ni] = 0;
        }
        return PV_BankReplaceMultipleSndResources(bankFile, &one, 1);
    }
}


BAEResult PV_BankCopyIndexedResourcesOfType(XFILE srcFile,
                                                    XFILE dstFile,
                                                    XResourceType resType)
{
    int32_t resCount;
    int32_t resIndex;

    resCount = XCountFileResourcesOfType(srcFile, resType);
    for (resIndex = 0; resIndex < resCount; ++resIndex)
    {
        XLongResourceID resID;
        int32_t resSize;
        XPTR resData;
        char resName[256];

        resName[0] = 0;
        resData = XGetIndexedFileResource(srcFile, resType, &resID, resIndex, resName, &resSize);
        if (!resData)
        {
            continue;
        }
        if (XAddFileResource(dstFile, resType, resID, resName, resData, resSize) != 0)
        {
            XDisposePtr(resData);
            return BAE_FILE_IO_ERROR;
        }
        XDisposePtr(resData);
    }
    return BAE_NO_ERROR;
}


BAEResult PV_BankReplaceMultipleSndResources(XFILE bankFile,
                                                     PV_SndReplacement const *replacements,
                                                     int32_t replacementCount)
{
    static const XResourceType sndTypes[] = { ID_SND, ID_CSND, ID_ESND, 0 };
    XFILERESOURCEMAP map;
    int32_t resourceID;
    XFILE outFile;
    XPTR packedData;
    int32_t packedSize;
    int typeIdx;
    int32_t ri;
    bool hasZins;
    bool hasZsng;
    bool hasZbnk;
    bool needClean;

    if (!bankFile || !replacements || replacementCount <= 0)
    {
        return BAE_PARAM_ERR;
    }

    if (XFileSetPosition(bankFile, 0L) != 0 ||
        XFileRead(bankFile, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        return BAE_BAD_FILE;
    }
    resourceID = (int32_t)XGetLong(&map.mapID);
    if (!XFILERESOURCE_ID_IS_VALID(resourceID))
    {
        return BAE_BAD_FILE;
    }

    outFile = XFileOpenVirtualResource(resourceID);
    if (!outFile)
    {
        return BAE_MEMORY_ERR;
    }

    /* Preserve packed ZMF blocks. Expanding every INST out of ZINS then
     * re-LZMA packing was the dominant cost of encrypt/Commit on large ZSB. */
    hasZins = (XCountFileResourcesOfType(bankFile, ID_ZINS) > 0) ? TRUE : FALSE;
    hasZsng = (XCountFileResourcesOfType(bankFile, ID_ZSNG) > 0) ? TRUE : FALSE;
    hasZbnk = (XCountFileResourcesOfType(bankFile, ID_ZBNK) > 0) ? TRUE : FALSE;
    needClean = (!hasZins) ? TRUE : FALSE;

    /* Walk SND resources; for each one check if it's in the replacement list */
    for (typeIdx = 0; sndTypes[typeIdx] != 0; ++typeIdx)
    {
        XResourceType resType = sndTypes[typeIdx];
        int32_t resCount = XCountFileResourcesOfType(bankFile, resType);
        int32_t resIndex;

        for (resIndex = 0; resIndex < resCount; ++resIndex)
        {
            XLongResourceID resID;
            int32_t resSize;
            XPTR resData;
            char resName[256];
            PV_SndReplacement const *match = NULL;

            resName[0] = 0;
            resData = XGetIndexedFileResource(bankFile, resType, &resID,
                                              resIndex, resName, &resSize);
            if (!resData)
            {
                continue;
            }

            for (ri = 0; ri < replacementCount; ++ri)
            {
                if (replacements[ri].oldType == resType &&
                    (XLongResourceID)replacements[ri].sndID == resID)
                {
                    match = &replacements[ri];
                    break;
                }
            }

            if (match)
            {
                char const *useName = match->name[0] ? match->name : resName;
                if (XAddFileResource(outFile, match->newType, resID,
                                     useName, match->data, match->size) != 0)
                {
                    XDisposePtr(resData);
                    XFileClose(outFile);
                    return BAE_FILE_IO_ERROR;
                }
            }
            else
            {
                if (XAddFileResource(outFile, resType, resID, resName,
                                     resData, resSize) != 0)
                {
                    XDisposePtr(resData);
                    XFileClose(outFile);
                    return BAE_FILE_IO_ERROR;
                }
            }
            XDisposePtr(resData);
        }
    }

    /* Opaque ZMF packs — copy as-is, never expand. Skip ZSHD: SND bodies above
     * are full rebuilt samples, so a stale header block would be wrong. */
    if (hasZins &&
        PV_BankCopyIndexedResourcesOfType(bankFile, outFile, ID_ZINS) != BAE_NO_ERROR)
    {
        XFileClose(outFile);
        return BAE_FILE_IO_ERROR;
    }
    if (hasZsng &&
        PV_BankCopyIndexedResourcesOfType(bankFile, outFile, ID_ZSNG) != BAE_NO_ERROR)
    {
        XFileClose(outFile);
        return BAE_FILE_IO_ERROR;
    }
    if (hasZbnk &&
        PV_BankCopyIndexedResourcesOfType(bankFile, outFile, ID_ZBNK) != BAE_NO_ERROR)
    {
        XFileClose(outFile);
        return BAE_FILE_IO_ERROR;
    }

    /* Loose non-SND resources that are not already inside a Z* pack. */
    {
        static const XResourceType looseTypes[] = {
            ID_INST, ID_ALIAS, ID_BANK, ID_SONG, ID_MIDI, ID_MIDI_OLD, ID_CMID,
            ID_EMID, ID_ECMI, ID_RMF, ID_TEXT, ID_VERS, 0
        };
        for (typeIdx = 0; looseTypes[typeIdx] != 0; ++typeIdx)
        {
            XResourceType resType = looseTypes[typeIdx];
            if (hasZins && (resType == ID_INST || resType == ID_ALIAS))
            {
                continue;
            }
            if (hasZsng && resType == ID_SONG)
            {
                continue;
            }
            if (hasZbnk &&
                (resType == ID_BANK || resType == ID_MIDI || resType == ID_MIDI_OLD))
            {
                continue;
            }
            if (PV_BankCopyIndexedResourcesOfType(bankFile, outFile, resType) != BAE_NO_ERROR)
            {
                XFileClose(outFile);
                return BAE_FILE_IO_ERROR;
            }
        }
    }

    if (needClean)
    {
        /* Editor path: never PackInst/Song/Bank/ZSHD — LZMA re-pack dominated
         * interactive edits. Flat resources in a ZREZ map are valid. */
        if (XRebuildResourceFileCache(outFile) == FALSE)
        {
            XFileClose(outFile);
            return BAE_FILE_IO_ERROR;
        }
    }

    packedData = NULL;
    packedSize = 0;
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 ||
        !packedData || packedSize <= 0)
    {
        XFileClose(outFile);
        if (packedData)
        {
            XDisposePtr(packedData);
        }
        return BAE_FILE_IO_ERROR;
    }
    XFileClose(outFile);

    if (bankFile->pCache)
    {
        XDisposePtr(bankFile->pCache);
        bankFile->pCache = NULL;
    }
    if (bankFile->pResourceData && bankFile->ownsResourceData)
    {
        XDisposePtr(bankFile->pResourceData);
    }

    bankFile->pResourceData = packedData;
    bankFile->resMemLength = packedSize;
    bankFile->resMemOffset = 0;
    bankFile->ownsResourceData = TRUE;
    bankFile->resizeResourceData = TRUE;
    bankFile->readOnly = FALSE;
    bankFile->allowMemCopy = TRUE;
    bankFile->pCache = XCreateAccessCache(bankFile);
    return BAE_NO_ERROR;
}


BAEResult PV_BankReplaceResource(XFILE bankFile,
                                        XResourceType type,
                                        XLongResourceID id,
                                        char const *pascalName,
                                        XPTR data,
                                        int32_t size)
{
    static const XResourceType bankResourceTypes[] = {
        ID_INST,
        ID_SND,
        ID_CSND,
        ID_ESND,
        ID_ALIAS,
        ID_BANK,
        ID_SONG,
        ID_MIDI,
        ID_MIDI_OLD,
        ID_CMID,
        ID_EMID,
        ID_ECMI,
        ID_RMF,
        ID_TEXT,
        ID_VERS,
        0
    };
    XFILERESOURCEMAP map;
    int32_t resourceID;
    XFILE outFile;
    int32_t typeIdx;
    XPTR packedData;
    int32_t packedSize;
    bool replaced;
    uint32_t type_u32 = (uint32_t)type;
    debug_message("[PV_BankReplaceResource] start: type=%c%c%c%c id=%d\n", (type_u32>>24)&0xFF, (type_u32>>16)&0xFF, (type_u32>>8)&0xFF, type_u32&0xFF, (int)id);

    debug_message("[PV_BankReplaceResource] step 1: Write map...\n");

    if (XFileSetPosition(bankFile, 0L) != 0 ||
        XFileRead(bankFile, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        return BAE_BAD_FILE;
    }
    resourceID = (int32_t)XGetLong(&map.mapID);
    if (!XFILERESOURCE_ID_IS_VALID(resourceID))
    {
        return BAE_BAD_FILE;
    }

    debug_message("[PV_BankReplaceResource] step 2: XFileOpenVirtualResource...\n");
    outFile = XFileOpenVirtualResource(resourceID);
    if (!outFile)
    {
        return BAE_MEMORY_ERR;
    }

    debug_message("[PV_BankReplaceResource] step 3: Loop bankResourceTypes...\n");
    replaced = FALSE;
    for (typeIdx = 0; bankResourceTypes[typeIdx] != 0; ++typeIdx)
    {
        XResourceType resType;
        int32_t resCount = 0;
        int32_t resIndex = 0;

        resType = bankResourceTypes[typeIdx];
        resCount = XCountFileResourcesOfType(bankFile, resType);
        debug_message("[PV_BankReplaceResource] Loop over resIndex %d of %d (type=%c%c%c%c)...\n", (int)resIndex, (int)resCount, ((uint32_t)resType>>24)&0xFF, ((uint32_t)resType>>16)&0xFF, ((uint32_t)resType>>8)&0xFF, (uint32_t)resType&0xFF);
        for (resIndex = 0; resIndex < resCount; ++resIndex)
        {
            XLongResourceID resID;
            int32_t resSize;
            XPTR resData;
            char resName[256];
            uint32_t startTicks = XMicroseconds();

            resName[0] = 0;
            resData = XGetIndexedFileResource(bankFile,
                                              resType,
                                              &resID,
                                              resIndex,
                                              resName,
                                              &resSize);
            uint32_t midTicks = XMicroseconds();
            if (!resData)
            {
                continue;
            }

            if (resType == type && resID == id)
            {
                if (!replaced)
                {
                    char const *replaceName;

                    replaceName = pascalName ? pascalName : resName;
                    if (XAddFileResource(outFile,
                                         type,
                                         id,
                                         replaceName,
                                         data,
                                         size) != 0)
                    {
                        XDisposePtr(resData);
                        XFileClose(outFile);
                        return BAE_FILE_IO_ERROR;
                    }
                    replaced = TRUE;
                }
                XDisposePtr(resData);
                continue;
            }

            if (XAddFileResource(outFile, resType, resID, resName, resData, resSize) != 0)
            {
                XDisposePtr(resData);
                XFileClose(outFile);
                return BAE_FILE_IO_ERROR;
            }
            uint32_t endTicks = XMicroseconds();
            if (endTicks - startTicks > 50000) {
                debug_message("[PV_BankReplaceResource] Slow resource copy: resIndex=%d resType=%c%c%c%c resID=%d size=%d XGetIndexedFileResource=%uus XAddFileResource=%uus\n", (int)resIndex, ((uint32_t)resType>>24)&0xFF, ((uint32_t)resType>>16)&0xFF, ((uint32_t)resType>>8)&0xFF, (uint32_t)resType&0xFF, (int)resID, (int)resSize, (unsigned)(midTicks - startTicks), (unsigned)(endTicks - midTicks));
            }
            XDisposePtr(resData);
        }
    }

    debug_message("[PV_BankReplaceResource] step 4: Out of loop. Replace if needed...\n");
    if (!replaced)
    {
        if (XAddFileResource(outFile,
                             type,
                             id,
                             pascalName,
                             data,
                             size) != 0)
        {
            XFileClose(outFile);
            return BAE_FILE_IO_ERROR;
        }
    }

    /* Editor mutations: never PackInst/Song/Bank. Historic Options(FALSE,FALSE)
     * still PackSong+PackBank (LZMA) — that made keymap/clone edits take seconds
     * on zpatches. Cache rebuild only. */
    debug_message("[PV_BankReplaceResource] step 5: XRebuildResourceFileCache...\n");
    if (XRebuildResourceFileCache(outFile) == FALSE)
    {
        XFileClose(outFile);
        return BAE_FILE_IO_ERROR;
    }

    debug_message("[PV_BankReplaceResource] step 6: XFileGetMemoryFileAsData...\n");
    packedData = NULL;
    packedSize = 0;
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 ||
        !packedData || packedSize <= 0)
    {
        XFileClose(outFile);
        if (packedData)
        {
            XDisposePtr(packedData);
        }
        return BAE_FILE_IO_ERROR;
    }
    XFileClose(outFile);

    if (bankFile->pCache)
    {
        XDisposePtr(bankFile->pCache);
        bankFile->pCache = NULL;
    }
    if (bankFile->pResourceData && bankFile->ownsResourceData)
    {
        XDisposePtr(bankFile->pResourceData);
    }

    bankFile->pResourceData = packedData;
    bankFile->resMemLength = packedSize;
    bankFile->resMemOffset = 0;
    bankFile->ownsResourceData = TRUE;
    bankFile->resizeResourceData = TRUE;
    bankFile->readOnly = FALSE;
    bankFile->allowMemCopy = TRUE;
    /* Match EnsureWritable — Exists/load paths need a live cache. */
    bankFile->pCache = XCreateAccessCache(bankFile);
    return BAE_NO_ERROR;
}

BAEResult PV_BankFindSndResource(XFILE bankFile,
                                        XShortResourceID sndID,
                                        XResourceType *outType,
                                        XPTR *outData,
                                        int32_t *outSize,
                                        char outName[256])
{
    static const XResourceType sndTypes[] = { ID_ESND, ID_CSND, ID_SND, 0 };
    int32_t typeIdx;

    if (!bankFile || !outType || !outData || !outSize)
    {
        return BAE_PARAM_ERR;
    }
    *outType = 0;
    *outData = NULL;
    *outSize = 0;
    if (outName)
    {
        outName[0] = 0;
    }

    for (typeIdx = 0; sndTypes[typeIdx] != 0; ++typeIdx)
    {
        XPTR data;
        int32_t size;
        char pascalName[256];

        pascalName[0] = 0;
        data = XGetFileResource(bankFile,
                                sndTypes[typeIdx],
                                (XLongResourceID)sndID,
                                pascalName,
                                &size);
        if (data)
        {
            if (outName)
            {
                XBlockMove(pascalName, outName, (int32_t)((unsigned char)pascalName[0]) + 1);
            }
            *outType = sndTypes[typeIdx];
            *outData = data;
            *outSize = size;
            return BAE_NO_ERROR;
        }
    }

    return BAE_BAD_FILE;
}


BAEResult PV_BankRewrapSndForType(XFILE bankFile,
                                         XResourceType sndType,
                                         XPTR plainSnd,
                                         int32_t plainSndSize,
                                         XPTR *outWrapped,
                                         int32_t *outWrappedSize)
{
    XPTR wrapped;
    int32_t wrappedSize;

    if (!bankFile || !plainSnd || plainSndSize <= 0 || !outWrapped || !outWrappedSize)
    {
        return BAE_PARAM_ERR;
    }
    *outWrapped = NULL;
    *outWrappedSize = 0;

    if (sndType == ID_SND)
    {
        wrapped = XNewPtr(plainSndSize);
        if (!wrapped)
        {
            return BAE_MEMORY_ERR;
        }
        XBlockMove(plainSnd, wrapped, plainSndSize);
        *outWrapped = wrapped;
        *outWrappedSize = plainSndSize;
        return BAE_NO_ERROR;
    }

    if (sndType == ID_ESND)
    {
        wrapped = XNewPtr(plainSndSize);
        if (!wrapped)
        {
            return BAE_MEMORY_ERR;
        }
        XBlockMove(plainSnd, wrapped, plainSndSize);
        XEncryptData(wrapped, (uint32_t)plainSndSize);
        *outWrapped = wrapped;
        *outWrappedSize = plainSndSize;
        return BAE_NO_ERROR;
    }

    if (sndType == ID_CSND)
    {
        XFILERESOURCEMAP map;
        int32_t mapID;
        XCOMPRESSION_TYPE compType;

        if (XFileSetPosition(bankFile, 0L) != 0 ||
            XFileRead(bankFile, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
        {
            return BAE_BAD_FILE;
        }
        mapID = (int32_t)XGetLong(&map.mapID);
#if USE_LZMA_COMPRESSION == TRUE
        compType = (mapID == XFILERESOURCE_ZMF_ID) ? X_LZMA_RAW : X_RAW;
#else
        compType = X_RAW;
#endif
        wrapped = NULL;
        wrappedSize = XCompressPtr(&wrapped,
                                   plainSnd,
                                   (uint32_t)plainSndSize,
                                   compType,
                                   NULL,
                                   NULL);
        if (wrappedSize <= 0 || !wrapped)
        {
            if (wrapped)
            {
                XDisposePtr(wrapped);
            }
            return BAE_COMPRESSION_INEFFECTIVE;
        }
        *outWrapped = wrapped;
        *outWrappedSize = wrappedSize;
        return BAE_NO_ERROR;
    }

    return BAE_PARAM_ERR;
}


/* Like PV_BankReplaceResource, but allows the container type to change.
 * Scans the bank and when a resource matching (oldType, id) is found it is
 * replaced with (newType, id, data).  When oldType == newType this is
 * identical to PV_BankReplaceResource. */
BAEResult PV_BankReplaceResourceEx(XFILE bankFile,
                                           XResourceType oldType,
                                           XResourceType newType,
                                           XLongResourceID id,
                                           char const *pascalName,
                                           XPTR data,
                                           int32_t size)
{
    static const XResourceType bankResourceTypes[] = {
        ID_INST,
        ID_SND,
        ID_CSND,
        ID_ESND,
        ID_ALIAS,
        ID_BANK,
        ID_SONG,
        ID_MIDI,
        ID_MIDI_OLD,
        ID_CMID,
        ID_EMID,
        ID_ECMI,
        ID_RMF,
        ID_TEXT,
        ID_VERS,
        0
    };
    XFILERESOURCEMAP map;
    int32_t resourceID;
    XFILE outFile;
    int32_t typeIdx;
    XPTR packedData;
    int32_t packedSize;
    bool replaced;

    if (!bankFile || !data || size <= 0)
    {
        return BAE_PARAM_ERR;
    }
    if (XFileSetPosition(bankFile, 0L) != 0 ||
        XFileRead(bankFile, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        return BAE_BAD_FILE;
    }
    resourceID = (int32_t)XGetLong(&map.mapID);
    if (!XFILERESOURCE_ID_IS_VALID(resourceID))
    {
        return BAE_BAD_FILE;
    }

    outFile = XFileOpenVirtualResource(resourceID);
    if (!outFile)
    {
        return BAE_MEMORY_ERR;
    }

    replaced = FALSE;
    for (typeIdx = 0; bankResourceTypes[typeIdx] != 0; ++typeIdx)
    {
        XResourceType resType;
        int32_t resCount;
        int32_t resIndex;

        resType = bankResourceTypes[typeIdx];
        resCount = XCountFileResourcesOfType(bankFile, resType);
        for (resIndex = 0; resIndex < resCount; ++resIndex)
        {
            XLongResourceID resID;
            int32_t resSize;
            XPTR resData;
            char resName[256];

            resName[0] = 0;
            resData = XGetIndexedFileResource(bankFile,
                                              resType,
                                              &resID,
                                              resIndex,
                                              resName,
                                              &resSize);
            if (!resData)
            {
                continue;
            }

            if (resType == oldType && resID == id)
            {
                if (!replaced)
                {
                    char const *replaceName;

                    replaceName = pascalName ? pascalName : resName;
                    /* Write with newType instead of oldType (handles container change) */
                    if (XAddFileResource(outFile,
                                         newType,
                                         id,
                                         replaceName,
                                         data,
                                         size) != 0)
                    {
                        XDisposePtr(resData);
                        XFileClose(outFile);
                        return BAE_FILE_IO_ERROR;
                    }
                    replaced = TRUE;
                }
                XDisposePtr(resData);
                continue;
            }

            if (XAddFileResource(outFile, resType, resID, resName, resData, resSize) != 0)
            {
                XDisposePtr(resData);
                XFileClose(outFile);
                return BAE_FILE_IO_ERROR;
            }
            XDisposePtr(resData);
        }
    }

    if (!replaced)
    {
        if (XAddFileResource(outFile, newType, id, pascalName, data, size) != 0)
        {
            XFileClose(outFile);
            return BAE_FILE_IO_ERROR;
        }
    }

    /* Same as PV_BankReplaceResource: cache only, no PackSong/Bank LZMA. */
    if (XRebuildResourceFileCache(outFile) == FALSE)
    {
        XFileClose(outFile);
        return BAE_FILE_IO_ERROR;
    }

    packedData = NULL;
    packedSize = 0;
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 ||
        !packedData || packedSize <= 0)
    {
        XFileClose(outFile);
        if (packedData)
        {
            XDisposePtr(packedData);
        }
        return BAE_FILE_IO_ERROR;
    }
    XFileClose(outFile);

    if (bankFile->pCache)
    {
        XDisposePtr(bankFile->pCache);
        bankFile->pCache = NULL;
    }
    if (bankFile->pResourceData && bankFile->ownsResourceData)
    {
        XDisposePtr(bankFile->pResourceData);
    }

    bankFile->pResourceData = packedData;
    bankFile->resMemLength = packedSize;
    bankFile->resMemOffset = 0;
    bankFile->ownsResourceData = TRUE;
    bankFile->resizeResourceData = TRUE;
    bankFile->readOnly = FALSE;
    bankFile->allowMemCopy = TRUE;
    bankFile->pCache = XCreateAccessCache(bankFile);
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_SetInstrumentExtInfo(BAEBankToken bankToken,
                                                uint32_t instrumentIndex,
                                                BAERmfEditorInstrumentExtInfo const *info)
{
    enum
    {
        kInstHeaderMinSize = 14,
        kInstKeySplitSize = 8,
        kInstTailSize = 10
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char rawName[256];
    unsigned char *instBytes;
    int16_t keySplitCount;
    int32_t baseSize;
    int32_t extTailSize;
    XPTR extTail;
    BAERmfEditorInstrumentExt ext;
    uint32_t i;
    BAEResult replaceResult;
    uint32_t resolvedID;
    uint32_t resolvedIndex;

    if (!bankToken || !info)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    /* XReplaceFileResource (flat trash+append) shifts GetIndexed INST order.
     * Callers often keep a stale instrumentIndex from open/Apply — resolve by
     * INST id so live IE patches cannot write B2P000 edits onto B2P001. */
    resolvedID = info->instID;
    resolvedIndex = instrumentIndex;
    if (BAERmfEditorBank_ResolveInstID(bankToken, info->instID, &resolvedID, &resolvedIndex) ==
        BAE_NO_ERROR)
    {
        instrumentIndex = resolvedIndex;
    }

    rawName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, rawName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if ((uint32_t)instID != info->instID && (uint32_t)instID != resolvedID)
    {
        /* Index still wrong after resolve — refuse rather than corrupt a neighbor. */
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    keySplitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
    if (keySplitCount < 0)
    {
        keySplitCount = 0;
    }
    baseSize = kInstHeaderMinSize + (keySplitCount * kInstKeySplitSize) + kInstTailSize;
    if (instSize < baseSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    XSetMemory(&ext, (int32_t)sizeof(ext), 0);
    ext.instID = (XLongResourceID)info->instID;
    ext.hasExtendedData = info->hasExtendedData;
    ext.flags1 = info->flags1;
    /* HSB/ZSB banks always embed their own SNDs — never persist ghost. */
    ext.flags1 = (unsigned char)(ext.flags1 & (unsigned char)~ZBF_ghostInstrument);
    ext.flags2 = info->flags2;
    ext.panPlacement = info->panPlacement;
    ext.midiRootKey = info->midiRootKey;
    ext.miscParameter1 = info->miscParameter1;
    ext.miscParameter2 = info->miscParameter2;
    ext.hasDefaultMod = info->hasDefaultMod;
    ext.LPF_frequency = info->LPF_frequency;
    ext.LPF_resonance = info->LPF_resonance;
    ext.LPF_lowpassAmount = info->LPF_lowpassAmount;
#if USE_ZMF_SUPPORT == TRUE
    ext.useOscillator = info->useOscillator ? TRUE : FALSE;
    ext.oscWaveShape = info->oscWaveShape ? info->oscWaveShape : (int32_t)SINE_WAVE_LONG;
    ext.oscPulseWidth = info->oscPulseWidth > 0 ? info->oscPulseWidth : 32768;
    ext.oscVolume = info->oscVolume;
    if (ext.oscVolume < 0)
        ext.oscVolume = 0;
    if (ext.oscVolume > 65536)
        ext.oscVolume = 65536;
    if (ext.useOscillator)
    {
        ext.flags1 |= ZBF_extendedFormat;
        ext.hasExtendedData = TRUE;
    }
#endif
    PV_CopyInfoToEditorADSR(&info->volumeADSR, &ext.volumeADSR);
    ext.lfoCount = info->lfoCount;
    if (ext.lfoCount > EDITOR_MAX_LFOS)
    {
        ext.lfoCount = EDITOR_MAX_LFOS;
    }
    for (i = 0; i < ext.lfoCount; ++i)
    {
        ext.lfos[i].destination = info->lfos[i].destination;
        ext.lfos[i].period = info->lfos[i].period;
        if (ext.lfos[i].period != 0 && ext.lfos[i].period <= 512)
        {
            ext.lfos[i].period = 513;
        }
        ext.lfos[i].waveShape = info->lfos[i].waveShape;
        ext.lfos[i].DC_feed = info->lfos[i].DC_feed;
        ext.lfos[i].level = info->lfos[i].level;
        PV_CopyInfoToEditorADSR(&info->lfos[i].adsr, &ext.lfos[i].adsr);
    }
    ext.curveCount = info->curveCount;
    if (ext.curveCount > EDITOR_MAX_CURVES)
    {
        ext.curveCount = EDITOR_MAX_CURVES;
    }
    for (i = 0; i < ext.curveCount; ++i)
    {
        uint32_t j;
        ext.curves[i].tieFrom = info->curves[i].tieFrom;
        ext.curves[i].tieTo = info->curves[i].tieTo;
        ext.curves[i].curveCount = info->curves[i].curveCount;
        if (ext.curves[i].curveCount > EDITOR_MAX_ADSR_STAGES)
        {
            ext.curves[i].curveCount = EDITOR_MAX_ADSR_STAGES;
        }
        for (j = 0; j < ext.curves[i].curveCount; ++j)
        {
            ext.curves[i].from_Value[j] = info->curves[i].from_Value[j];
            ext.curves[i].to_Scalar[j] = info->curves[i].to_Scalar[j];
        }
    }

    extTail = PV_SerializeExtendedInstTail(&ext, &extTailSize);

    instBytes = (unsigned char *)XNewPtr(baseSize + extTailSize);
    if (!instBytes)
    {
        if (extTail)
        {
            XDisposePtr(extTail);
        }
        XDisposePtr(instData);
        return BAE_MEMORY_ERR;
    }
    XSetMemory(instBytes, baseSize + extTailSize, 0);
    XBlockMove(instData, instBytes, baseSize);

    XPutShort(instBytes + 2, (uint16_t)info->midiRootKey);
    instBytes[4] = (unsigned char)info->panPlacement;
    instBytes[5] = ext.flags1;
    instBytes[6] = info->flags2;
    XPutShort(instBytes + 8, (uint16_t)info->miscParameter1);
    XPutShort(instBytes + 10, (uint16_t)info->miscParameter2);
    if (extTail && extTailSize > 0)
    {
        instBytes[5] |= ZBF_extendedFormat;
    }
    else
    {
        instBytes[5] &= (unsigned char)(~ZBF_extendedFormat);
    }

    if (extTail && extTailSize > 0)
    {
        XBlockMove(extTail, instBytes + baseSize, extTailSize);
    }

    {
        char pascalName[256];
        char const *nameForReplace = rawName;
        if (info->displayName && info->displayName[0] &&
            PV_CreatePascalName(info->displayName, pascalName) == BAE_NO_ERROR)
        {
            nameForReplace = pascalName;
        }
        replaceResult = PV_BankCommitResource(bankFile,
                                              ID_INST,
                                              instID,
                                              nameForReplace,
                                              instBytes,
                                              baseSize + extTailSize);
    }

    if (extTail)
    {
        XDisposePtr(extTail);
    }
    XDisposePtr(instBytes);
    XDisposePtr(instData);
    return replaceResult;
}


BAEResult BAERmfEditorBank_RenameSampleResource(BAEBankToken bankToken,
                                                XShortResourceID sndID,
                                                char const *displayName)
{
    XFILE bankFile;
    XResourceType sndType = 0;
    XPTR sndData = NULL;
    int32_t sndSize = 0;
    char pascalName[256];
    BAEResult r;

    /* sndID 0 is a valid resource id on some legacy banks — do not reject it. */
    if (!bankToken || !displayName || !displayName[0])
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;
    r = PV_BankFindSndResource(bankFile, sndID, &sndType, &sndData, &sndSize, NULL);
    if (r != BAE_NO_ERROR || !sndData)
    {
        return (r != BAE_NO_ERROR) ? r : BAE_BAD_FILE;
    }
    if (PV_CreatePascalName(displayName, pascalName) != BAE_NO_ERROR)
    {
        XDisposePtr(sndData);
        return BAE_PARAM_ERR;
    }
    r = PV_BankCommitResource(bankFile, sndType, (XLongResourceID)sndID, pascalName, sndData, sndSize);
    XDisposePtr(sndData);
    return r;
}


BAEResult BAERmfEditorBank_BeginBatchSnd(BAEBankToken bankToken)
{
    XFILE bankFile;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;
    if (bankFile->pendingSndBatch != NULL)
    {
        return BAE_PARAM_ERR; /* already in batch mode */
    }

    /* Allocate initial capacity - will grow as needed in PV_BankReplaceSndResourceInPlace */
    bankFile->pendingSndBatch = (struct XFilePendingSnd *)XNewPtr(
        64 * (int32_t)sizeof(struct XFilePendingSnd));
    if (!bankFile->pendingSndBatch)
    {
        return BAE_MEMORY_ERR;
    }
    bankFile->pendingSndCount = 0;
    bankFile->pendingSndCapacity = 64;
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_CommitBatchSnd(BAEBankToken bankToken)
{
    XFILE bankFile;
    PV_SndReplacement *replacements;
    int32_t i;
    BAEResult result;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;
    if (!bankFile->pendingSndBatch || bankFile->pendingSndCount == 0)
    {
        /* Nothing to flush; just clear batch mode */
        if (bankFile->pendingSndBatch)
        {
            XDisposePtr(bankFile->pendingSndBatch);
            bankFile->pendingSndBatch = NULL;
        }
        bankFile->pendingSndCount = 0;
        bankFile->pendingSndCapacity = 0;
        return BAE_NO_ERROR;
    }

    /* Build PV_SndReplacement array from pending list */
    replacements = (PV_SndReplacement *)XNewPtr(
        bankFile->pendingSndCount * (int32_t)sizeof(PV_SndReplacement));
    if (!replacements)
    {
        return BAE_MEMORY_ERR;
    }
    for (i = 0; i < bankFile->pendingSndCount; ++i)
    {
        struct XFilePendingSnd *p = &bankFile->pendingSndBatch[i];
        replacements[i].oldType = (XResourceType)p->oldType;
        replacements[i].newType = (XResourceType)p->newType;
        replacements[i].sndID   = (XShortResourceID)p->sndID;
        replacements[i].data    = p->data;
        replacements[i].size    = p->size;
        XBlockMove(p->name, replacements[i].name, 256);
    }

    result = PV_BankReplaceMultipleSndResources(bankFile, replacements,
                                                bankFile->pendingSndCount);

    /* Free pending data */
    for (i = 0; i < bankFile->pendingSndCount; ++i)
    {
        XDisposePtr(bankFile->pendingSndBatch[i].data);
    }
    XDisposePtr(bankFile->pendingSndBatch);
    bankFile->pendingSndBatch = NULL;
    bankFile->pendingSndCount = 0;
    bankFile->pendingSndCapacity = 0;
    XDisposePtr(replacements);
    return result;
}


void BAERmfEditorBank_AbortBatchSnd(BAEBankToken bankToken)
{
    XFILE bankFile;
    int32_t i;

    if (!bankToken)
    {
        return;
    }
    bankFile = (XFILE)bankToken;
    if (!bankFile->pendingSndBatch)
    {
        return;
    }
    for (i = 0; i < bankFile->pendingSndCount; ++i)
    {
        XDisposePtr(bankFile->pendingSndBatch[i].data);
    }
    XDisposePtr(bankFile->pendingSndBatch);
    bankFile->pendingSndBatch = NULL;
    bankFile->pendingSndCount = 0;
    bankFile->pendingSndCapacity = 0;
}


BAEResult BAERmfEditorBank_ScaleAllSplitVolumes(BAEBankToken bankToken,
                                                uint32_t instrumentIndex,
                                                double scalar)
{
    enum
    {
        kInstHeaderMinSize = 14,
        kInstKeySplitSize = 8
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char instName[256];
    int16_t splitCount;
    int32_t s;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    instName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, instName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    splitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
    if (splitCount < 0)
    {
        splitCount = 0;
    }

    if (!PV_InstMiscParameter2IsVolume(((unsigned char *)instData)[6]))
    {
        XDisposePtr(instData);
        return BAE_PARAM_ERR;
    }

    if (splitCount > 0)
    {
        /* Scale every split's miscParameter2 (volume) by scalar */
        for (s = 0; s < splitCount; ++s)
        {
            unsigned char *splitPtr = (unsigned char *)instData + 14 + (s * kInstKeySplitSize);
            int16_t curVol = (int16_t)XGetShort(splitPtr + 6);
            /* 0 means default (100) */
            if (curVol == 0) curVol = 100;
            {
                int32_t scaled = (int32_t)(curVol * scalar + 0.5);
                if (scaled < 0) scaled = 0;
                if (scaled > 800) scaled = 800;
                /* Store 0 to mean default only when exactly 100 */
                XPutShort(splitPtr + 6, (uint16_t)(scaled == 100 ? 0 : scaled));
            }
        }
    }
    else
    {
        /* Non-split instrument: scale header miscParameter2 */
        int16_t curVol = (int16_t)XGetShort((unsigned char *)instData + 10);
        if (curVol == 0) curVol = 100;
        {
            int32_t scaled = (int32_t)(curVol * scalar + 0.5);
            if (scaled < 0) scaled = 0;
            if (scaled > 800) scaled = 800;
            XPutShort((unsigned char *)instData + 10, (uint16_t)(scaled == 100 ? 0 : scaled));
        }
    }

    /* One rebuild for the whole instrument */
    {
        BAEResult r = PV_BankReplaceInstResourceInPlace(bankFile, instID, instName,
                                                        instData, instSize);
        XDisposePtr(instData);
        return r;
    }
}


BAEResult BAERmfEditorBank_SetInstrumentSampleInfo(BAEBankToken bankToken,
                                                    uint32_t instrumentIndex,
                                                    uint32_t sampleIndex,
                                                    BAERmfEditorBankSampleInfo const *info)
{
    enum
    {
        kInstHeaderMinSize = 14,
        kInstKeySplitSize = 8
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char instName[256];
    int16_t splitCount;
    XShortResourceID sndID;
    XResourceType sndType;
    XPTR sndRawData;
    int32_t sndRawSize;
    char sndName[256];
    XPTR sndPlain;
    int32_t sndPlainSize;
    XPTR sndWrapped;
    int32_t sndWrappedSize;
    BAEResult result;
    BAEResult replaceResult;
    bool useSoundModifierAsRootKey;

    if (!bankToken || !info)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    instName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, instName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    splitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
    if (splitCount < 0)
    {
        splitCount = 0;
    }

    useSoundModifierAsRootKey = TEST_FLAG_VALUE(((unsigned char *)instData)[6],
                                             ZBF_useSoundModifierAsRootKey);

    if (splitCount > 0)
    {
        unsigned char *splitPtr;

        if (sampleIndex >= (uint32_t)splitCount)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }

        splitPtr = (unsigned char *)instData + 14 + (sampleIndex * kInstKeySplitSize);
        splitPtr[0] = info->lowKey;
        splitPtr[1] = info->highKey;
        /* Never overwrite offset-start / sound-modifier words as "volume". */
        if (PV_InstMiscParameter2IsVolume(((unsigned char *)instData)[6]))
        {
            XPutShort(splitPtr + 6, (uint16_t)info->splitVolume);
        }
        /* sndResourceID 0 is a valid SND id — always write the caller's value. */
        XPutShort(splitPtr + 2, (uint16_t)info->sndResourceID);
        sndID = info->sndResourceID;

        /* Root key storage depends on ZBF_useSoundModifierAsRootKey.
         * When set, each split stores its own root in miscParameter1.
         * When clear, the sample root lives in the SND baseFrequency /
         * baseKey — never overwrite INST midiRootKey (master transpose). */
        if (useSoundModifierAsRootKey)
        {
            XPutShort(splitPtr + 4, (uint16_t)info->rootKey);
        }
    }
    else
    {
        if (sampleIndex != 0)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }
        if (PV_InstMiscParameter2IsVolume(((unsigned char *)instData)[6]))
        {
            XPutShort((unsigned char *)instData + 10, (uint16_t)info->splitVolume);
        }
        if (useSoundModifierAsRootKey)
        {
            /* Per-sample root in miscParameter1; keep midiRootKey as master transpose. */
            XPutShort((unsigned char *)instData + 8, (uint16_t)info->rootKey);
        }
        /* sndResourceID 0 is a valid SND id — always write the caller's value. */
        XPutShort((unsigned char *)instData + 0, (uint16_t)info->sndResourceID);
        sndID = info->sndResourceID;
    }

    /* Always commit INST via XReplace (never full-bank rebuild for keymap Vol/etc). */
    result = PV_BankReplaceInstResourceInPlace(bankFile,
                                               instID,
                                               instName,
                                               instData,
                                               instSize);
    XDisposePtr(instData);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    /* sampleRate == 0 is the editor sentinel for "INST fields only" (Vol/Low/High
     * and root-in-INST). Avoids opening/decompressing SND on every keymap tweak. */
    if (info->sampleRate == 0)
    {
        return BAE_NO_ERROR;
    }

    sndName[0] = 0;
    result = PV_BankFindSndResource(bankFile,
                                    sndID,
                                    &sndType,
                                    &sndRawData,
                                    &sndRawSize,
                                    sndName);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    sndPlain = sndRawData;
    sndPlainSize = sndRawSize;
    if (sndType == ID_CSND)
    {
        sndPlain = XDecompressPtr(sndRawData, (uint32_t)sndRawSize, FALSE);
        XDisposePtr(sndRawData);
        if (!sndPlain)
        {
            return BAE_BAD_FILE;
        }
        sndPlainSize = XGetPtrSize(sndPlain);
    }
    else if (sndType == ID_ESND)
    {
        XDecryptData(sndPlain, (uint32_t)sndPlainSize);
    }

    {
        uint32_t hz;
        BAE_UNSIGNED_FIXED sampleRate;
        SampleDataInfo sampleInfo;

        XSetMemory(&sampleInfo, (int32_t)sizeof(sampleInfo), 0);
        if (XGetSampleInfoFromSnd(sndPlain, &sampleInfo) == 0)
        {
            const uint32_t curHz = (uint32_t)XFIXED_TO_UNSIGNED_LONG(sampleInfo.rate);
            if (curHz == info->sampleRate &&
                sampleInfo.loopStart == info->loopStart &&
                sampleInfo.loopEnd == info->loopEnd &&
                (useSoundModifierAsRootKey ||
                 sampleInfo.baseKey == (int16_t)info->rootKey))
            {
                XDisposePtr(sndPlain);
                return BAE_NO_ERROR;
            }
        }

        hz = info->sampleRate;
        sampleRate = (BAE_UNSIGNED_FIXED)(hz << 16);
        XSetSoundSampleRate(sndPlain, sampleRate);
        XSetSoundLoopPoints(sndPlain, (int32_t)info->loopStart, (int32_t)info->loopEnd);

        /* When useSoundModifierAsRootKey is clear, the sample root is the SND
         * baseKey/baseFrequency.  When set, INST miscParameter1 owns the root
         * and the SND key stays as the sample's recorded pitch. */
        if (!useSoundModifierAsRootKey)
        {
            XSetSoundBaseKey(sndPlain, (int16_t)info->rootKey);
        }
    }

    sndWrapped = NULL;
    sndWrappedSize = 0;
    result = PV_BankRewrapSndForType(bankFile,
                                     sndType,
                                     sndPlain,
                                     sndPlainSize,
                                     &sndWrapped,
                                     &sndWrappedSize);
    XDisposePtr(sndPlain);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    /* Same-type SND: XReplace first; type-change falls through Commit→Replace. */
    replaceResult = PV_BankReplaceSndResourceInPlace(bankFile,
                                                     sndType,
                                                     sndType,
                                                     sndID,
                                                     sndName,
                                                     sndWrapped,
                                                     sndWrappedSize);
    XDisposePtr(sndWrapped);
    return replaceResult;
}


BAEResult BAERmfEditorBank_SetInstrumentSampleSndID(BAEBankToken bankToken,
                                                     uint32_t instrumentIndex,
                                                     uint32_t sampleIndex,
                                                     XShortResourceID sndResourceID)
{
    enum
    {
        kInstHeaderMinSize = 14,
        kInstKeySplitSize = 8
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char instName[256];
    int16_t splitCount;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    instName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, instName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    splitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
    if (splitCount < 0)
    {
        splitCount = 0;
    }

    if (splitCount > 0)
    {
        unsigned char *splitPtr;

        if (sampleIndex >= (uint32_t)splitCount)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }

        splitPtr = (unsigned char *)instData + 14 + (sampleIndex * kInstKeySplitSize);
        XPutShort(splitPtr + 2, (uint16_t)sndResourceID);

        /* Keep header sndResourceID synchronized with split 0 for compatibility. */
        XPutShort((unsigned char *)instData + 0,
                  (uint16_t)XGetShort((unsigned char *)instData + 14 + 2));
    }
    else
    {
        if (sampleIndex != 0)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }
        XPutShort((unsigned char *)instData + 0, (uint16_t)sndResourceID);
    }

    {
        BAEResult result = PV_BankCommitResource(bankFile,
                                                 ID_INST,
                                                 instID,
                                                 instName,
                                                 instData,
                                                 instSize);
        XDisposePtr(instData);
        return result;
    }
}



BAEResult BAERmfEditorBank_SetInstrumentSampleSndIDs(BAEBankToken bankToken,
                                                      uint32_t instrumentIndex,
                                                      XShortResourceID const *sndResourceIDs,
                                                      uint32_t sndResourceIDCount)
{
    enum
    {
        kInstHeaderMinSize = 14,
        kInstKeySplitSize = 8
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char instName[256];
    int16_t splitCount;
    uint32_t s;
    BAEResult result;

    if (!bankToken || !sndResourceIDs || sndResourceIDCount == 0)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    instName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, instName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    splitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
    if (splitCount < 0)
    {
        splitCount = 0;
    }

    if (splitCount > 0)
    {
        if (sndResourceIDCount < (uint32_t)splitCount)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }
        for (s = 0; s < (uint32_t)splitCount; ++s)
        {
            unsigned char *splitPtr =
                (unsigned char *)instData + 14 + (s * kInstKeySplitSize);
            XPutShort(splitPtr + 2, (uint16_t)sndResourceIDs[s]);
        }
        XPutShort((unsigned char *)instData + 0,
                  (uint16_t)XGetShort((unsigned char *)instData + 14 + 2));
    }
    else
    {
        XPutShort((unsigned char *)instData + 0, (uint16_t)sndResourceIDs[0]);
    }

    result = PV_BankCommitResource(bankFile,
                                   ID_INST,
                                   instID,
                                   instName,
                                   instData,
                                   instSize);
    XDisposePtr(instData);
    return result;
}


BAEResult BAERmfEditorBank_SetSampleSndStorageType(BAEBankToken bankToken,
                                                    uint32_t instrumentIndex,
                                                    uint32_t sampleIndex,
                                                    BAERmfEditorSndStorageType sndStorageType)
{
    XFILE bankFile;
    BAERmfEditorBankSampleInfo sampleInfo;
    XResourceType oldType;
    XResourceType newType;
    XPTR sndRawData;
    int32_t sndRawSize;
    XPTR sndPlain;
    int32_t sndPlainSize;
    XPTR sndWrapped;
    int32_t sndWrappedSize;
    char resName[256];
    BAEResult result;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }

    bankFile = (XFILE)bankToken;

    /* Resolve current sample metadata to locate the backing SND resource. */
    if (BAERmfEditorBank_GetInstrumentSampleInfo(bankToken,
                                                 instrumentIndex,
                                                 sampleIndex,
                                                 &sampleInfo) != BAE_NO_ERROR)
    {
        return BAE_BAD_FILE;
    }

    switch (sampleInfo.sndStorageType)
    {
        case BAE_EDITOR_SND_STORAGE_CSND:
            oldType = ID_CSND;
            break;
        case BAE_EDITOR_SND_STORAGE_SND:
            oldType = ID_SND;
            break;
        case BAE_EDITOR_SND_STORAGE_ESND:
        default:
            oldType = ID_ESND;
            break;
    }

    switch (sndStorageType)
    {
        case BAE_EDITOR_SND_STORAGE_CSND:
            newType = ID_CSND;
            break;
        case BAE_EDITOR_SND_STORAGE_SND:
            newType = ID_SND;
            break;
        case BAE_EDITOR_SND_STORAGE_ESND:
        default:
            newType = ID_ESND;
            break;
    }

    if (oldType == newType)
    {
        return BAE_NO_ERROR;
    }

    /* Unwrap to plain SND, then rewrap. ESND is XOR-encrypted; CSND is compressed
       plaintext. Never compress an ESND payload — decrypt to SND first. */
    resName[0] = 0;
    result = PV_BankFindSndResource(bankFile,
                                    (XShortResourceID)sampleInfo.sndResourceID,
                                    &oldType,
                                    &sndRawData,
                                    &sndRawSize,
                                    resName);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    sndPlain = sndRawData;
    sndPlainSize = sndRawSize;
    {
        int16_t soundFormat = 0;
        if (sndPlain && sndPlainSize >= 2)
        {
            soundFormat = (int16_t)XGetShort(sndPlain);
        }

        /* Some loaders return already-rebuilt plain SND data even for CSND/ESND
           entries. Only unwrap when data does not already look like SND. */
        if (!(soundFormat == 1 || soundFormat == 2 || soundFormat == 3))
        {
            if (oldType == ID_CSND)
            {
                XPTR decompressed = XDecompressPtr(sndRawData, (uint32_t)sndRawSize, FALSE);
                XDisposePtr(sndRawData);
                sndPlain = decompressed;
                if (!sndPlain)
                {
                    return BAE_BAD_FILE;
                }
                sndPlainSize = XGetPtrSize(sndPlain);
            }
            else if (oldType == ID_ESND)
            {
                XDecryptData(sndPlain, (uint32_t)sndPlainSize);
            }
        }
    }

    sndWrapped = NULL;
    sndWrappedSize = 0;
    result = PV_BankRewrapSndForType(bankFile,
                                     newType,
                                     sndPlain,
                                     sndPlainSize,
                                     &sndWrapped,
                                     &sndWrappedSize);
    XDisposePtr(sndPlain);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    result = PV_BankReplaceSndResourceInPlace(bankFile,
                                              oldType,
                                              newType,
                                              (XShortResourceID)sampleInfo.sndResourceID,
                                              resName,
                                              sndWrapped,
                                              sndWrappedSize);
    XDisposePtr(sndWrapped);
    return result;
}


BAEResult PV_BankDeleteResource(XFILE bankFile,
                                       XResourceType type,
                                       XLongResourceID id)
{
    /*
     * Hard-remove one resource while preserving ZSHD / TRSH / CaSd / ZINS peers.
     * The old type-walk rebuild only copied a fixed INST/SND/… list, so deleting
     * one INST (Unload Bank, etc.) silently dropped ZSHD — leaving payload-ref
     * SNDs unrestorable and subsequent soft-deletes as opaque TRSH junk.
     */
    if (!bankFile)
    {
        return BAE_PARAM_ERR;
    }
    if (XPurgeFileResource(bankFile, type, id) == FALSE)
    {
        return BAE_FILE_IO_ERROR;
    }
    return BAE_NO_ERROR;
}


/* Keep-callback may rewrite *to. Return TRUE to keep the entry. */
typedef bool (*PV_BankAliasKeepFn)(uint32_t from, uint32_t *to, void *user);

static BAEResult PV_BankFilterAliases(XFILE bankFile,
                                      PV_BankAliasKeepFn keep,
                                      void *user)
{
    XAliasLinkResource *oldAlias;
    XAliasLinkResource *newAlias;
    uint32_t oldCount;
    uint32_t newCount;
    uint32_t i;
    bool changed;
    int32_t newSize;
    char emptyName[1];
    BAEResult result;

    if (!bankFile || !keep)
    {
        return BAE_PARAM_ERR;
    }

    oldAlias = XGetAliasLinkFromFile(bankFile);
    if (!oldAlias)
    {
        return BAE_NO_ERROR;
    }

    oldCount = (uint32_t)XGetLong(&oldAlias->numberOfAliases);
    newCount = 0;
    changed = FALSE;
    for (i = 0; i < oldCount; ++i)
    {
        uint32_t from = (uint32_t)XGetLong(&oldAlias->list[i].aliasFrom);
        uint32_t to = (uint32_t)XGetLong(&oldAlias->list[i].aliasTo);
        uint32_t newTo = to;
        if (keep(from, &newTo, user))
        {
            ++newCount;
            if (newTo != to)
            {
                changed = TRUE;
            }
        }
        else
        {
            changed = TRUE;
        }
    }

    if (!changed)
    {
        XDisposePtr((XPTR)oldAlias);
        return BAE_NO_ERROR;
    }

    if (newCount == 0)
    {
        XDisposePtr((XPTR)oldAlias);
        return PV_BankDeleteResource(bankFile, ID_ALIAS, DEFAULT_RESOURCE_ALIAS_ID);
    }

    newSize = (int32_t)(sizeof(uint32_t) * 2 + newCount * sizeof(XAliasLink));
    newAlias = (XAliasLinkResource *)XNewPtr(newSize);
    if (!newAlias)
    {
        XDisposePtr((XPTR)oldAlias);
        return BAE_MEMORY_ERR;
    }

    XPutLong(&newAlias->version, ALIAS_ID_RESOURCE_VERSION);
    XPutLong(&newAlias->numberOfAliases, newCount);
    {
        uint32_t dst = 0;
        for (i = 0; i < oldCount; ++i)
        {
            uint32_t from = (uint32_t)XGetLong(&oldAlias->list[i].aliasFrom);
            uint32_t to = (uint32_t)XGetLong(&oldAlias->list[i].aliasTo);
            uint32_t newTo = to;
            if (keep(from, &newTo, user))
            {
                XPutLong(&newAlias->list[dst].aliasFrom, from);
                XPutLong(&newAlias->list[dst].aliasTo, newTo);
                ++dst;
            }
        }
    }
    XDisposePtr((XPTR)oldAlias);

    emptyName[0] = 0;
    result = PV_BankCommitResource(bankFile, ID_ALIAS,
                                   DEFAULT_RESOURCE_ALIAS_ID,
                                   emptyName, newAlias, newSize);
    XDisposePtr((XPTR)newAlias);
    return result;
}

static bool PV_BankAliasKeepIfNotTo(uint32_t from, uint32_t *to, void *user)
{
    (void)from;
    return *to != *(uint32_t *)user;
}

static bool PV_BankAliasKeepRetarget(uint32_t from, uint32_t *to, void *user)
{
    uint32_t const *ids = (uint32_t const *)user;
    (void)from;
    if (*to == ids[0])
    {
        *to = ids[1];
    }
    return TRUE;
}

static bool PV_BankAliasKeepIfTargetInstExists(uint32_t from, uint32_t *to, void *user)
{
    (void)from;
    return XExistsFileResource((XFILE)user, ID_INST, (XLongResourceID)*to) != FALSE;
}

static uint32_t PV_BankCountOrphanAliases(XFILE bankFile)
{
    XAliasLinkResource *alias;
    uint32_t count;
    uint32_t n;
    uint32_t i;

    alias = XGetAliasLinkFromFile(bankFile);
    if (!alias)
    {
        return 0;
    }
    n = (uint32_t)XGetLong(&alias->numberOfAliases);
    count = 0;
    for (i = 0; i < n; ++i)
    {
        uint32_t to = (uint32_t)XGetLong(&alias->list[i].aliasTo);
        if (XExistsFileResource(bankFile, ID_INST, (XLongResourceID)to) == FALSE)
        {
            ++count;
        }
    }
    XDisposePtr((XPTR)alias);
    return count;
}

static uint32_t PV_BankCountAliasesToInstID(XFILE bankFile, uint32_t instID)
{
    XAliasLinkResource *alias;
    uint32_t count;
    uint32_t n;
    uint32_t i;

    alias = XGetAliasLinkFromFile(bankFile);
    if (!alias)
    {
        return 0;
    }
    n = (uint32_t)XGetLong(&alias->numberOfAliases);
    count = 0;
    for (i = 0; i < n; ++i)
    {
        if ((uint32_t)XGetLong(&alias->list[i].aliasTo) == instID)
        {
            ++count;
        }
    }
    XDisposePtr((XPTR)alias);
    return count;
}

static BAEResult PV_BankPruneAliasesToInstID(XFILE bankFile, uint32_t instID)
{
    return PV_BankFilterAliases(bankFile, PV_BankAliasKeepIfNotTo, &instID);
}

BAEResult BAERmfEditorBank_PruneAliasesToInstID(BAEBankToken bankToken,
                                                uint32_t instID)
{
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    return PV_BankPruneAliasesToInstID((XFILE)bankToken, instID);
}

BAEResult BAERmfEditorBank_RetargetAliases(BAEBankToken bankToken,
                                           uint32_t oldToInstID,
                                           uint32_t newToInstID)
{
    uint32_t ids[2];

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    if (oldToInstID == newToInstID)
    {
        return BAE_NO_ERROR;
    }
    ids[0] = oldToInstID;
    ids[1] = newToInstID;
    return PV_BankFilterAliases((XFILE)bankToken, PV_BankAliasKeepRetarget, ids);
}

BAEResult BAERmfEditorBank_CountOrphanAliases(BAEBankToken bankToken,
                                              uint32_t *outCount)
{
    if (!bankToken || !outCount)
    {
        return BAE_PARAM_ERR;
    }
    *outCount = PV_BankCountOrphanAliases((XFILE)bankToken);
    return BAE_NO_ERROR;
}

BAEResult BAERmfEditorBank_CountAliasesToInstID(BAEBankToken bankToken,
                                                uint32_t instID,
                                                uint32_t *outCount)
{
    if (!bankToken || !outCount)
    {
        return BAE_PARAM_ERR;
    }
    *outCount = PV_BankCountAliasesToInstID((XFILE)bankToken, instID);
    return BAE_NO_ERROR;
}

BAEResult BAERmfEditorBank_PruneOrphanAliases(BAEBankToken bankToken,
                                              uint32_t *outRemoved)
{
    XFILE bankFile;
    uint32_t before;
    BAEResult result;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;
    before = PV_BankCountOrphanAliases(bankFile);
    result = PV_BankFilterAliases(bankFile, PV_BankAliasKeepIfTargetInstExists, bankFile);
    if (outRemoved)
    {
        *outRemoved = (result == BAE_NO_ERROR) ? before : 0;
    }
    return result;
}


// Public API to delete an instrument from a bank
BAEResult BAERmfEditorBank_DeleteInstrument(BAEBankToken bankToken,
                                            uint32_t instrumentIndex)
{
    XFILE bankFile;
    XLongResourceID instID;
    XPTR instData;
    int32_t instSize;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }

    bankFile = (XFILE)bankToken;

    // Get the instrument ID at this index
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, NULL, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }

    XDisposePtr(instData);

    // Use internal function to properly delete the resource from memory-based bank
    {
        BAEResult r = PV_BankDeleteResource(bankFile, ID_INST, instID);
        if (r != BAE_NO_ERROR)
            return r;
    }

    return PV_BankPruneAliasesToInstID(bankFile, (uint32_t)instID);
}


// Remove one alias entry (by aliasFrom ID) from the bank's ID_ALIAS resource.
// Does NOT delete the underlying INST resource.
BAEResult BAERmfEditorBank_DeleteAlias(BAEBankToken bankToken,
                                       uint32_t aliasFromInstID)
{
    XFILE bankFile;
    XAliasLinkResource *oldAlias;
    uint32_t oldCount;
    uint32_t newCount;
    int32_t newSize;
    XAliasLinkResource *newAlias;
    BAEResult result;

    if (!bankToken)
        return BAE_PARAM_ERR;

    bankFile = (XFILE)bankToken;
    oldAlias = XGetAliasLinkFromFile(bankFile);
    oldCount = oldAlias ? (uint32_t)XGetLong(&oldAlias->numberOfAliases) : 0u;

    if (oldCount == 0)
    {
        XDisposePtr((XPTR)oldAlias);
        return BAE_PARAM_ERR;  // nothing to remove
    }

    // Count entries that will survive
    newCount = 0;
    for (uint32_t i = 0; i < oldCount; ++i)
        if ((uint32_t)XGetLong(&oldAlias->list[i].aliasFrom) != aliasFromInstID)
            ++newCount;

    if (newCount == oldCount)
    {
        // Entry not found
        XDisposePtr((XPTR)oldAlias);
        return BAE_PARAM_ERR;
    }

    if (newCount == 0)
    {
        XDisposePtr((XPTR)oldAlias);
        return PV_BankDeleteResource(bankFile, ID_ALIAS, DEFAULT_RESOURCE_ALIAS_ID);
    }

    newSize  = (int32_t)(sizeof(uint32_t) * 2 + newCount * sizeof(XAliasLink));
    newAlias = (XAliasLinkResource *)XNewPtr(newSize);
    if (!newAlias)
    {
        XDisposePtr((XPTR)oldAlias);
        return BAE_MEMORY_ERR;
    }

    XPutLong(&newAlias->version, ALIAS_ID_RESOURCE_VERSION);
    XPutLong(&newAlias->numberOfAliases, newCount);
    {
        uint32_t dst = 0;
        for (uint32_t i = 0; i < oldCount; ++i)
        {
            if ((uint32_t)XGetLong(&oldAlias->list[i].aliasFrom) != aliasFromInstID)
            {
                XPutLong(&newAlias->list[dst].aliasFrom,
                         XGetLong(&oldAlias->list[i].aliasFrom));
                XPutLong(&newAlias->list[dst].aliasTo,
                         XGetLong(&oldAlias->list[i].aliasTo));
                ++dst;
            }
        }
    }
    XDisposePtr((XPTR)oldAlias);

    {
        char emptyName[1] = {0};
        result = PV_BankCommitResource(bankFile, ID_ALIAS,
                                       DEFAULT_RESOURCE_ALIAS_ID,
                                       emptyName, newAlias, newSize);
    }
    XDisposePtr((XPTR)newAlias);
    return result;
}


bool PV_BankIdInList(XShortResourceID const *ids, uint32_t count, XLongResourceID id)
{
    uint32_t i;

    if (!ids || count == 0 || id > 32767)
    {
        return FALSE;
    }
    for (i = 0; i < count; ++i)
    {
        if ((XLongResourceID)ids[i] == id)
        {
            return TRUE;
        }
    }
    return FALSE;
}


/* True if file has a decodable SND/CSND/ESND for sid (handles encrypted ESND).
 * ID 0 is a valid resource id; only 0xFFFF (-1) means "no sample". */
bool PV_BankFileHasValidSndResource(XFILE file, XShortResourceID sid)
{
    static const XResourceType sndTypes[] = {ID_ESND, ID_CSND, ID_SND, 0};
    int t;

    if (!file || PV_IsNoSampleSndID(sid))
    {
        return FALSE;
    }
    for (t = 0; sndTypes[t] != 0; ++t)
    {
        int32_t size = 0;
        /* Zero-extend: sign-extending negative shorts breaks ID lookups. */
        XPTR data = XGetFileResource(file,
                                     sndTypes[t],
                                     (XLongResourceID)(uint16_t)sid,
                                     NULL,
                                     &size);
        if (!data || size < 4)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            continue;
        }
        if (sndTypes[t] == ID_ESND)
        {
            XPTR copy = XNewPtr(size);
            int16_t fmt;
            bool ok;

            if (!copy)
            {
                XDisposePtr(data);
                return FALSE;
            }
            XBlockMove(data, copy, size);
            XDisposePtr(data);
            XDecryptData(copy, (uint32_t)size);
            fmt = (int16_t)XGetShort(copy);
            ok = (fmt == 1 || fmt == 2 || fmt == 3);
            XDisposePtr(copy);
            if (ok)
            {
                return TRUE;
            }
        }
        else if (sndTypes[t] == ID_CSND)
        {
            XPTR decoded = XDecompressPtr(data, (uint32_t)size, FALSE);
            int16_t fmt;
            bool ok;

            XDisposePtr(data);
            if (!decoded)
            {
                continue;
            }
            fmt = (int16_t)XGetShort(decoded);
            ok = (fmt == 1 || fmt == 2 || fmt == 3);
            XDisposePtr(decoded);
            if (ok)
            {
                return TRUE;
            }
        }
        else
        {
            int16_t fmt = (int16_t)XGetShort(data);
            XDisposePtr(data);
            if (fmt == 1 || fmt == 2 || fmt == 3)
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}


BAEResult BAERmfEditorBank_ImportInstAndSndFromFile(BAEBankToken bankToken,
                                                    void *sourceFile,
                                                    XShortResourceID const *instIds,
                                                    uint32_t instCount,
                                                    XShortResourceID const *sndIds,
                                                    uint32_t sndCount)
{
    static const XResourceType bankResourceTypes[] = {
        ID_INST,
        ID_SND,
        ID_CSND,
        ID_ESND,
        ID_ALIAS,
        ID_BANK,
        ID_SONG,
        ID_MIDI,
        ID_MIDI_OLD,
        ID_CMID,
        ID_EMID,
        ID_ECMI,
        ID_RMF,
        ID_TEXT,
        ID_VERS,
        0
    };
    static const XResourceType sndTypes[] = {ID_ESND, ID_CSND, ID_SND, 0};
    XFILE bankFile;
    XFILE donor;
    XFILE flatDonor;
    XFILERESOURCEMAP map;
    int32_t resourceID;
    XFILE outFile;
    int32_t typeIdx;
    XPTR packedData;
    int32_t packedSize;
    uint32_t i;
    XShortResourceID *sndCopy = NULL;
    XShortResourceID *instCopy = NULL;

    if (!bankToken || !sourceFile)
    {
        return BAE_PARAM_ERR;
    }
    if ((instCount > 0 && !instIds) || (sndCount > 0 && !sndIds))
    {
        return BAE_PARAM_ERR;
    }
    if (instCount == 0 && sndCount == 0)
    {
        return BAE_NO_ERROR;
    }

    bankFile = (XFILE)bankToken;
    donor = (XFILE)sourceFile;

    /* Expand ZREZ donor packages (ZSHD payload-refs / ZINS) into a flat IREZ
     * staging file before merge. XGetFileResource reassembles refs; writing
     * into IREZ + Clean does not re-pack, so overlays are always full SND/INST. */
    flatDonor = XFileOpenVirtualResource(XFILERESOURCE_ID);
    if (!flatDonor)
    {
        return BAE_MEMORY_ERR;
    }
    if (sndCount > 0)
    {
        sndCopy = (XShortResourceID *)XNewPtr((int32_t)(sndCount * sizeof(XShortResourceID)));
        if (!sndCopy)
        {
            XFileClose(flatDonor);
            return BAE_MEMORY_ERR;
        }
        XBlockMove((void *)sndIds, sndCopy, (int32_t)(sndCount * sizeof(XShortResourceID)));
        (void)XCopySndResources(sndCopy,
                                (int16_t)sndCount,
                                donor,
                                flatDonor,
                                FALSE,
                                TRUE);
        for (i = 0; i < sndCount; ++i)
        {
            if (!PV_BankFileHasValidSndResource(flatDonor, sndCopy[i]))
            {
                XDisposePtr(sndCopy);
                XFileClose(flatDonor);
                return BAE_BAD_FILE;
            }
        }
    }
    if (instCount > 0)
    {
        instCopy = (XShortResourceID *)XNewPtr((int32_t)(instCount * sizeof(XShortResourceID)));
        if (!instCopy)
        {
            if (sndCopy)
            {
                XDisposePtr(sndCopy);
            }
            XFileClose(flatDonor);
            return BAE_MEMORY_ERR;
        }
        XBlockMove((void *)instIds, instCopy, (int32_t)(instCount * sizeof(XShortResourceID)));
        (void)XCopyInstrumentResources(instCopy,
                                       (int16_t)instCount,
                                       donor,
                                       flatDonor,
                                       TRUE);
        for (i = 0; i < instCount; ++i)
        {
            int32_t size = 0;
            XPTR data = XGetFileResource(flatDonor,
                                         ID_INST,
                                         (XLongResourceID)instCopy[i],
                                         NULL,
                                         &size);
            if (!data || size < 14)
            {
                if (data)
                {
                    XDisposePtr(data);
                }
                XDisposePtr(instCopy);
                if (sndCopy)
                {
                    XDisposePtr(sndCopy);
                }
                XFileClose(flatDonor);
                return BAE_BAD_FILE;
            }
            XDisposePtr(data);
        }
    }
    /* Staging image: no Pack/LZMA — ZSHD refs are a paste footgun anyway. */
    if (XRebuildResourceFileCache(flatDonor) == FALSE)
    {
        if (instCopy)
        {
            XDisposePtr(instCopy);
        }
        if (sndCopy)
        {
            XDisposePtr(sndCopy);
        }
        XFileClose(flatDonor);
        return BAE_FILE_IO_ERROR;
    }
    donor = flatDonor;

    if (XFileSetPosition(bankFile, 0L) != 0 ||
        XFileRead(bankFile, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        if (instCopy)
        {
            XDisposePtr(instCopy);
        }
        if (sndCopy)
        {
            XDisposePtr(sndCopy);
        }
        XFileClose(flatDonor);
        return BAE_BAD_FILE;
    }
    resourceID = (int32_t)XGetLong(&map.mapID);
    if (!XFILERESOURCE_ID_IS_VALID(resourceID))
    {
        if (instCopy)
        {
            XDisposePtr(instCopy);
        }
        if (sndCopy)
        {
            XDisposePtr(sndCopy);
        }
        XFileClose(flatDonor);
        return BAE_BAD_FILE;
    }

    outFile = XFileOpenVirtualResource(resourceID);
    if (!outFile)
    {
        if (instCopy)
        {
            XDisposePtr(instCopy);
        }
        if (sndCopy)
        {
            XDisposePtr(sndCopy);
        }
        XFileClose(flatDonor);
        return BAE_MEMORY_ERR;
    }

    /* Copy dest resources, omitting ids that will be replaced. */
    for (typeIdx = 0; bankResourceTypes[typeIdx] != 0; ++typeIdx)
    {
        XResourceType resType = bankResourceTypes[typeIdx];
        int32_t resCount = XCountFileResourcesOfType(bankFile, resType);
        int32_t resIndex;

        for (resIndex = 0; resIndex < resCount; ++resIndex)
        {
            XLongResourceID resID = 0;
            int32_t resSize = 0;
            char resName[256];
            XPTR resData;

            resName[0] = 0;
            resData = XGetIndexedFileResource(bankFile,
                                              resType,
                                              &resID,
                                              resIndex,
                                              resName,
                                              &resSize);
            if (!resData)
            {
                continue;
            }

            if (resType == ID_INST && PV_BankIdInList(instIds, instCount, resID))
            {
                XDisposePtr(resData);
                continue;
            }
            if ((resType == ID_SND || resType == ID_CSND || resType == ID_ESND) &&
                PV_BankIdInList(sndIds, sndCount, resID))
            {
                XDisposePtr(resData);
                continue;
            }

            if (XAddFileResource(outFile, resType, resID, resName, resData, resSize) != 0)
            {
                XDisposePtr(resData);
                XFileClose(outFile);
                if (instCopy)
                {
                    XDisposePtr(instCopy);
                }
                if (sndCopy)
                {
                    XDisposePtr(sndCopy);
                }
                XFileClose(flatDonor);
                return BAE_FILE_IO_ERROR;
            }
            XDisposePtr(resData);
        }
    }

    /* Overlay SND containers from expanded donor (ESND/CSND/SND). */
    for (i = 0; i < sndCount; ++i)
    {
        XLongResourceID rid = (XLongResourceID)sndIds[i];
        int t;

        for (t = 0; sndTypes[t] != 0; ++t)
        {
            char name[256];
            int32_t size = 0;
            XPTR data;

            name[0] = 0;
            data = XGetFileResource(donor, sndTypes[t], rid, name, &size);
            if (!data || size <= 0)
            {
                if (data)
                {
                    XDisposePtr(data);
                }
                continue;
            }
            if (XAddFileResource(outFile, sndTypes[t], rid, name, data, size) != 0)
            {
                XDisposePtr(data);
                XFileClose(outFile);
                if (instCopy)
                {
                    XDisposePtr(instCopy);
                }
                if (sndCopy)
                {
                    XDisposePtr(sndCopy);
                }
                XFileClose(flatDonor);
                return BAE_FILE_IO_ERROR;
            }
            XDisposePtr(data);
        }
    }

    /* Overlay INST resources from expanded donor. */
    for (i = 0; i < instCount; ++i)
    {
        XLongResourceID rid = (XLongResourceID)instIds[i];
        char name[256];
        int32_t size = 0;
        XPTR data;

        name[0] = 0;
        data = XGetFileResource(donor, ID_INST, rid, name, &size);
        if (!data || size <= 0)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            continue;
        }
        if (XAddFileResource(outFile, ID_INST, rid, name, data, size) != 0)
        {
            XDisposePtr(data);
            XFileClose(outFile);
            if (instCopy)
            {
                XDisposePtr(instCopy);
            }
            if (sndCopy)
            {
                XDisposePtr(sndCopy);
            }
            XFileClose(flatDonor);
            return BAE_FILE_IO_ERROR;
        }
        XDisposePtr(data);
    }

    if (instCopy)
    {
        XDisposePtr(instCopy);
        instCopy = NULL;
    }
    if (sndCopy)
    {
        XDisposePtr(sndCopy);
        sndCopy = NULL;
    }
    XFileClose(flatDonor);
    flatDonor = NULL;

    /* Merge into open bank: no Pack/LZMA; keep complete ESND/SND payloads. */
    if (XRebuildResourceFileCache(outFile) == FALSE)
    {
        XFileClose(outFile);
        return BAE_FILE_IO_ERROR;
    }

    packedData = NULL;
    packedSize = 0;
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 ||
        !packedData || packedSize <= 0)
    {
        XFileClose(outFile);
        if (packedData)
        {
            XDisposePtr(packedData);
        }
        return BAE_FILE_IO_ERROR;
    }
    XFileClose(outFile);

    if (bankFile->pCache)
    {
        XDisposePtr(bankFile->pCache);
        bankFile->pCache = NULL;
    }
    if (bankFile->pResourceData && bankFile->ownsResourceData)
    {
        XDisposePtr(bankFile->pResourceData);
    }

    bankFile->pResourceData = packedData;
    bankFile->resMemLength = packedSize;
    bankFile->resMemOffset = 0;
    bankFile->ownsResourceData = TRUE;
    bankFile->resizeResourceData = TRUE;
    bankFile->readOnly = FALSE;
    bankFile->allowMemCopy = TRUE;
    /* Match EnsureWritable — Exists/name lookups need a live cache. */
    bankFile->pCache = XCreateAccessCache(bankFile);
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_EnsureWritable(BAEBankToken bankToken)
{
    XFILE bankFile;
    XFILERESOURCEMAP map;
    int32_t resourceID;
    int32_t imageSize;
    XPTR image;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;
    if (bankFile->pResourceData &&
        bankFile->readOnly == FALSE &&
        bankFile->resizeResourceData != FALSE)
    {
        return BAE_NO_ERROR;
    }

    /* Fast path: clone the whole IREZ/ZREZ image in one read/memcpy.
     * The old per-resource Get→Add→Clean path expands ZINS/ZSNG/ZBNK and
     * re-LZMAs them — dominant cost on large ZSB banks (e.g. zpatches). */
    imageSize = XFileGetLength(bankFile);
    if (imageSize <= (int32_t)sizeof(XFILERESOURCEMAP))
    {
        return BAE_BAD_FILE;
    }
    if (XFileSetPosition(bankFile, 0L) != 0 ||
        XFileRead(bankFile, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        return BAE_BAD_FILE;
    }
    resourceID = (int32_t)XGetLong(&map.mapID);
    if (!XFILERESOURCE_ID_IS_VALID(resourceID))
    {
        return BAE_BAD_FILE;
    }

    image = XNewPtr(imageSize);
    if (!image)
    {
        return BAE_MEMORY_ERR;
    }
    if (XFileSetPosition(bankFile, 0L) != 0 ||
        XFileRead(bankFile, image, imageSize) != 0)
    {
        XDisposePtr(image);
        return BAE_BAD_FILE;
    }

    if (bankFile->pCache)
    {
        XDisposePtr(bankFile->pCache);
        bankFile->pCache = NULL;
    }
    if (bankFile->pResourceData && bankFile->ownsResourceData)
    {
        XDisposePtr(bankFile->pResourceData);
    }
    else if (!bankFile->pResourceData && bankFile->fileReference)
    {
        /* Switching disk-backed → memory-backed; close the FD so XFileClose
         * (which skips BAE_FileClose when pResourceData is set) won't leak it. */
        BAE_FileClose(bankFile->fileReference);
        bankFile->fileReference = 0;
    }

    bankFile->pResourceData = image;
    bankFile->resMemLength = imageSize;
    bankFile->resMemOffset = 0;
    bankFile->ownsResourceData = TRUE;
    bankFile->resizeResourceData = TRUE;
    bankFile->readOnly = FALSE;
    bankFile->allowMemCopy = TRUE;

    /* Rebuild access cache so Exists/name lookups work (uncached path has a
     * not-found bug that reports success for missing IDs). */
    bankFile->pCache = XCreateAccessCache(bankFile);
    return BAE_NO_ERROR;
}


// Clone an INST resource to a new instID.  If deepClone is TRUE, all SND/CSND/ESND
// resources referenced by the instrument's key-splits are also duplicated with new IDs.
BAEResult BAERmfEditorBank_CloneInstrument(BAEBankToken bankToken,
                                           uint32_t instrumentIndex,
                                           uint32_t destInstID,
                                           bool deepClone)
{
    enum { kInstHeaderMinSize = 14, kInstKeySplitSize = 8 };
    XFILE bankFile;
    XLongResourceID srcInstID;
    XPTR instData;
    int32_t instSize;
    char instName[256];
    int16_t splitCount;
    BAEResult result;
    uint32_t resolvedInstID;
    uint32_t resolvedIndex;

    if (!bankToken)
        return BAE_PARAM_ERR;

    bankFile = (XFILE)bankToken;

    /* Duplicate guard: if destInstID already resolves to an existing INST
       (directly or through ID_ALIAS), report it explicitly. */
    result = BAERmfEditorBank_ResolveInstID(bankToken,
                                            destInstID,
                                            &resolvedInstID,
                                            &resolvedIndex);
    if (result == BAE_NO_ERROR)
    {
        return BAE_ALREADY_EXISTS;
    }

    instName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &srcInstID,
                                       (int32_t)instrumentIndex, instName, &instSize);
    if (!instData || instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    splitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
    if (splitCount < 0) splitCount = 0;

    if (deepClone && splitCount > 0)
    {
        /* One used-SND-id scan for the whole clone (was per-split).
         * sndRemap[old] = new: shared splits keep one copy. */
        unsigned char *cloneData = (unsigned char *)XNewPtr(instSize);
        bool usedIDs[65536];
        uint16_t sndRemap[65536];
        static const XResourceType sndTypes[] = { ID_SND, ID_CSND, ID_ESND, 0 };
        int nextFree = 1;
        int t;

        if (!cloneData)
        {
            XDisposePtr(instData);
            return BAE_MEMORY_ERR;
        }
        XBlockMove(instData, cloneData, instSize);
        XSetMemory(usedIDs, (int32_t)sizeof(usedIDs), 0);
        XSetMemory(sndRemap, (int32_t)sizeof(sndRemap), 0);
        for (t = 0; sndTypes[t] != 0; ++t)
        {
            int32_t cnt = XCountFileResourcesOfType(bankFile, sndTypes[t]);
            int k;
            for (k = 0; k < cnt; ++k)
            {
                XLongResourceID rid;
                XPTR tmp = XGetIndexedFileResource(bankFile, sndTypes[t], &rid, k, NULL, NULL);
                if (tmp)
                {
                    XDisposePtr(tmp);
                    if (rid >= 1 && rid < 65536)
                    {
                        usedIDs[rid] = TRUE;
                    }
                }
            }
        }

        for (int s = 0; s < splitCount; ++s)
        {
            unsigned char *splitPtr = cloneData + 14 + s * kInstKeySplitSize;
            XShortResourceID oldSndID = (XShortResourceID)XGetShort(splitPtr + 2);
            uint16_t oldSndU = (uint16_t)oldSndID;
            if (oldSndU != 0)
            {
                XShortResourceID newSndID;

                if (sndRemap[oldSndU] != 0)
                {
                    XPutShort(splitPtr + 2, sndRemap[oldSndU]);
                    continue;
                }

                newSndID = 0;
                for (; nextFree < 65536; ++nextFree)
                {
                    if (!usedIDs[nextFree])
                    {
                        newSndID = (XShortResourceID)nextFree;
                        usedIDs[nextFree] = TRUE;
                        ++nextFree;
                        break;
                    }
                }
                if (newSndID == 0)
                {
                    XDisposePtr(cloneData);
                    XDisposePtr(instData);
                    return BAE_FILE_IO_ERROR;
                }

                for (t = 0; sndTypes[t] != 0; ++t)
                {
                    int32_t sndSize;
                    XPTR sndData = XGetFileResource(bankFile, sndTypes[t],
                                                    (XLongResourceID)oldSndID, NULL, &sndSize);
                    if (sndData)
                    {
                        char emptyName[1] = {0};
                        if (XAddFileResource(bankFile, sndTypes[t],
                                             (XLongResourceID)newSndID,
                                             emptyName, sndData, sndSize) != 0)
                        {
                            XDisposePtr(sndData);
                            XDisposePtr(cloneData);
                            XDisposePtr(instData);
                            return BAE_FILE_IO_ERROR;
                        }
                        XDisposePtr(sndData);
                        sndRemap[oldSndU] = (uint16_t)newSndID;
                        XPutShort(splitPtr + 2, (uint16_t)newSndID);
                        break;
                    }
                }
            }
        }
        XPutShort(cloneData, (uint16_t)XGetShort(cloneData + 14 + 2));

        result = PV_BankCommitResource(bankFile, ID_INST,
                                       (XLongResourceID)destInstID,
                                       instName, cloneData, instSize);
        XDisposePtr(cloneData);
    }
    else
    {
        /* Shallow clone: INST under new ID, same SND references */
        result = PV_BankCommitResource(bankFile, ID_INST,
                                       (XLongResourceID)destInstID,
                                       instName, instData, instSize);
    }

    XDisposePtr(instData);
    return result;
}


// Add an alias entry (aliasFrom -> srcInstID) to the bank's ID_ALIAS resource.
BAEResult BAERmfEditorBank_AliasInstrument(BAEBankToken bankToken,
                                           uint32_t instrumentIndex,
                                           uint32_t aliasInstID)
{
    XFILE bankFile;
    XLongResourceID srcInstID;
    XPTR instData;
    int32_t instSize;
    XAliasLinkResource *oldAlias;
    uint32_t oldCount;
    uint32_t newCount;
    int32_t newSize;
    XAliasLinkResource *newAlias;
    BAEResult result;

    if (!bankToken)
        return BAE_PARAM_ERR;

    bankFile = (XFILE)bankToken;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &srcInstID,
                                       (int32_t)instrumentIndex, NULL, &instSize);
    if (!instData)
        return BAE_BAD_FILE;
    XDisposePtr(instData);

    oldAlias = XGetAliasLinkFromFile(bankFile);
    oldCount = oldAlias ? (uint32_t)XGetLong(&oldAlias->numberOfAliases) : 0u;

    // Check the alias doesn't already exist
    for (uint32_t i = 0; i < oldCount; ++i)
    {
        if ((uint32_t)XGetLong(&oldAlias->list[i].aliasFrom) == aliasInstID)
        {
            XDisposePtr((XPTR)oldAlias);
            return BAE_PARAM_ERR;  // slot already aliased
        }
    }

    newCount = oldCount + 1;
    newSize  = (int32_t)(sizeof(uint32_t) * 2 +
                         newCount * sizeof(XAliasLink));
    newAlias = (XAliasLinkResource *)XNewPtr(newSize);
    if (!newAlias)
    {
        XDisposePtr((XPTR)oldAlias);
        return BAE_MEMORY_ERR;
    }

    XPutLong(&newAlias->version, ALIAS_ID_RESOURCE_VERSION);
    XPutLong(&newAlias->numberOfAliases, newCount);

    for (uint32_t i = 0; i < oldCount; ++i)
    {
        XPutLong(&newAlias->list[i].aliasFrom, XGetLong(&oldAlias->list[i].aliasFrom));
        XPutLong(&newAlias->list[i].aliasTo,   XGetLong(&oldAlias->list[i].aliasTo));
    }
    XPutLong(&newAlias->list[oldCount].aliasFrom, aliasInstID);
    XPutLong(&newAlias->list[oldCount].aliasTo,   (uint32_t)srcInstID);

    XDisposePtr((XPTR)oldAlias);

    /* Write the updated alias resource back */
    {
        char emptyName[1] = {0};
        result = PV_BankCommitResource(bankFile, ID_ALIAS,
                                       DEFAULT_RESOURCE_ALIAS_ID,
                                       emptyName, newAlias, newSize);
    }
    XDisposePtr((XPTR)newAlias);
    return result;
}


uint32_t PV_BankCountSndReferences(XFILE bankFile, XShortResourceID sndID)
{
    enum
    {
        kInstHeaderMinSize = 14,
        kInstKeySplitSize = 8
    };
    uint32_t refs;
    int32_t instCount;
    int32_t instIndex;

    if (!bankFile)
    {
        return 0;
    }

    refs = 0;
    instCount = XCountFileResourcesOfType(bankFile, ID_INST);
    for (instIndex = 0; instIndex < instCount; ++instIndex)
    {
        XLongResourceID instID;
        int32_t instSize;
        XPTR instData;
        int16_t splitCount;

        instData = XGetIndexedFileResource(bankFile, ID_INST, &instID, instIndex, NULL, &instSize);
        if (!instData || instSize < kInstHeaderMinSize)
        {
            if (instData)
            {
                XDisposePtr(instData);
            }
            continue;
        }

        splitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
        if (splitCount < 0)
        {
            splitCount = 0;
        }

        if (splitCount > 0)
        {
            int16_t splitIndex;
            if (instSize >= (kInstHeaderMinSize + (splitCount * kInstKeySplitSize)))
            {
                for (splitIndex = 0; splitIndex < splitCount; ++splitIndex)
                {
                    unsigned char *splitPtr;
                    XShortResourceID splitSndID;

                    splitPtr = (unsigned char *)instData + 14 + (splitIndex * kInstKeySplitSize);
                    splitSndID = (XShortResourceID)XGetShort(splitPtr + 2);
                    if (splitSndID == sndID)
                    {
                        ++refs;
                    }
                }
            }
        }
        else
        {
            XShortResourceID baseSndID = (XShortResourceID)XGetShort((unsigned char *)instData + 0);
            if (baseSndID == sndID)
            {
                ++refs;
            }
        }

        XDisposePtr(instData);
    }

    return refs;
}


BAEResult BAERmfEditorBank_GrowInstrumentSampleSlots(BAEBankToken bankToken,
                                                      uint32_t instrumentIndex,
                                                      uint32_t desiredSampleCount)
{
    enum
    {
        kInstHeaderMinSize = 14
    };
    XFILE bankFile;
    XLongResourceID instID;
    XPTR instData;
    int32_t instSize;
    char instName[256];
    int16_t splitCount;
    uint32_t currentCount;
    InstrumentResource *grown;
    int16_t howMany;
    uint32_t startInit;
    uint32_t i;
    unsigned char defaultRoot;
    int16_t defaultSplitVolume;
    KeySplit newSplit;
    BAEResult replaceResult;

    if (!bankToken || desiredSampleCount == 0)
    {
        return BAE_PARAM_ERR;
    }
    if (desiredSampleCount > 32767u)
    {
        return BAE_PARAM_ERR;
    }

    bankFile = (XFILE)bankToken;
    instName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, instName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    splitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
    if (splitCount < 0)
    {
        splitCount = 0;
    }
    currentCount = (splitCount > 0) ? (uint32_t)splitCount : 1u;
    if (desiredSampleCount <= currentCount)
    {
        XDisposePtr(instData);
        return BAE_NO_ERROR;
    }

    if (splitCount > 0)
    {
        howMany = (int16_t)(desiredSampleCount - (uint32_t)splitCount);
        grown = XAddKeySplit((InstrumentResource *)instData, howMany);
        startInit = (uint32_t)splitCount;
    }
    else
    {
        /* Convert legacy non-split INST to split form, then grow to desired count. */
        howMany = (int16_t)desiredSampleCount;
        grown = XAddKeySplit((InstrumentResource *)instData, howMany);
        startInit = 0;
    }

    if (!grown)
    {
        XDisposePtr(instData);
        return BAE_MEMORY_ERR;
    }

    defaultRoot = (unsigned char)XGetShort((unsigned char *)grown + 2);
    defaultSplitVolume = (int16_t)XGetShort((unsigned char *)grown + 10);
    if (defaultSplitVolume == 0)
    {
        defaultSplitVolume = 100;
    }

    if (splitCount == 0)
    {
        /* Slot 0 preserves legacy header sample mapping. */
        XShortResourceID baseSnd = (XShortResourceID)XGetShort((unsigned char *)instData + 0);
        int16_t baseRoot = (int16_t)XGetShort((unsigned char *)instData + 2);
        int16_t miscParam1 = (int16_t)XGetShort((unsigned char *)instData + 8);
        int16_t baseVolume = (int16_t)XGetShort((unsigned char *)instData + 10);
        bool useSoundModifierAsRootKey = TEST_FLAG_VALUE(((unsigned char *)instData)[6], ZBF_useSoundModifierAsRootKey);
        int16_t splitRoot = baseRoot;

        if (useSoundModifierAsRootKey)
        {
            /* Root key 0 is valid (C-1). */
            if (miscParam1 >= 0 && miscParam1 <= 127)
            {
                splitRoot = miscParam1;
            }
        }

        XSetMemory(&newSplit, (int32_t)sizeof(newSplit), 0);
        newSplit.lowMidi = 0;
        newSplit.highMidi = 127;
        newSplit.sndResourceID = baseSnd;
        newSplit.miscParameter1 = splitRoot;
        newSplit.miscParameter2 = baseVolume;
        XSetKeySplitFromPtr(grown, 0, &newSplit);
        startInit = 1;
    }

    for (i = startInit; i < desiredSampleCount; ++i)
    {
        XSetMemory(&newSplit, (int32_t)sizeof(newSplit), 0);
        newSplit.lowMidi = 0;
        newSplit.highMidi = 127;
        /* Empty slot: 0xFFFF = no sample. SND id 0 is a real resource. */
        newSplit.sndResourceID = (XShortResourceID)0xFFFFu;
        newSplit.miscParameter1 = (int16_t)defaultRoot;
        newSplit.miscParameter2 = defaultSplitVolume;
        XSetKeySplitFromPtr(grown, (int16_t)i, &newSplit);
    }

    /* Keep header sndResourceID aligned with split 0 for compatibility. */
    {
        KeySplit split0;
        XGetKeySplitFromPtr(grown, 0, &split0);
        XPutShort((unsigned char *)grown + 0, (uint16_t)split0.sndResourceID);
    }

    replaceResult = PV_BankCommitResource(bankFile,
                                          ID_INST,
                                          instID,
                                          instName,
                                          grown,
                                          XGetPtrSize((XPTR)grown));

    XDisposePtr((XPTR)grown);
    XDisposePtr(instData);
    return replaceResult;
}


BAEResult BAERmfEditorBank_DeleteInstrumentSample(BAEBankToken bankToken,
                                                   uint32_t instrumentIndex,
                                                   uint32_t sampleIndex,
                                                   bool deleteSndIfUnreferenced)
{
    enum
    {
        kInstHeaderMinSize = 14,
        kInstKeySplitSize = 8
    };
    XFILE bankFile;
    XLongResourceID instID;
    XPTR instData;
    int32_t instSize;
    char instName[256];
    int16_t splitCount;
    XShortResourceID removedSndID;
    BAEResult result;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    instName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, instName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }

    splitCount = (int16_t)XGetShort((unsigned char *)instData + 12);
    if (splitCount < 0)
    {
        splitCount = 0;
    }

    removedSndID = 0;
    if (splitCount > 0)
    {
        InstrumentResource *shrunk;
        KeySplit removedSplit;
        int16_t newSplitCount;

        if (sampleIndex >= (uint32_t)splitCount)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }

        XGetKeySplitFromPtr((InstrumentResource *)instData, (int16_t)sampleIndex, &removedSplit);
        removedSndID = removedSplit.sndResourceID;

        shrunk = XRemoveThisKeySplit((InstrumentResource *)instData, (int16_t)sampleIndex);
        if (!shrunk)
        {
            XDisposePtr(instData);
            return BAE_MEMORY_ERR;
        }

        newSplitCount = (int16_t)XGetShort(&shrunk->keySplitCount);
        if (newSplitCount > 0)
        {
            KeySplit split0;
            XGetKeySplitFromPtr(shrunk, 0, &split0);
            XPutShort((unsigned char *)shrunk + 0, (uint16_t)split0.sndResourceID);
        }
        else
        {
            XPutShort((unsigned char *)shrunk + 0, 0);
        }

        result = PV_BankCommitResource(bankFile,
                                       ID_INST,
                                       instID,
                                       instName,
                                       shrunk,
                                       XGetPtrSize((XPTR)shrunk));
        XDisposePtr((XPTR)shrunk);
        XDisposePtr(instData);
        if (result != BAE_NO_ERROR)
        {
            return result;
        }
    }
    else
    {
        if (sampleIndex != 0)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }

        removedSndID = (XShortResourceID)XGetShort((unsigned char *)instData + 0);
        XPutShort((unsigned char *)instData + 0, 0);

        result = PV_BankCommitResource(bankFile,
                                       ID_INST,
                                       instID,
                                       instName,
                                       instData,
                                       instSize);
        XDisposePtr(instData);
        if (result != BAE_NO_ERROR)
        {
            return result;
        }
    }

    /* removedSndID 0 is a valid SND id — probe by resource existence, not != 0. */
    if (deleteSndIfUnreferenced)
    {
        if (PV_BankCountSndReferences(bankFile, removedSndID) == 0)
        {
            XResourceType sndType;
            XPTR sndData;
            int32_t sndSize;

            result = PV_BankFindSndResource(bankFile,
                                            removedSndID,
                                            &sndType,
                                            &sndData,
                                            &sndSize,
                                            NULL);
            if (result == BAE_NO_ERROR)
            {
                XDisposePtr(sndData);
                (void)PV_BankDeleteResource(bankFile, sndType, (XLongResourceID)removedSndID);
            }
        }
    }

    return BAE_NO_ERROR;
}


/* Serialize all resources from a loaded bank file into a new in-memory
 * resource image.  The output is a complete IREZ or ZREZ blob that can
 * be written to disk or loaded via BAEMixer_AddBankFromMemory.
 *
 * The format is preserved from the source bank (IREZ stays IREZ, ZREZ
 * stays ZREZ) unless overrideResourceID is non-zero, in which case that
 * format is used instead.  Same-format saves clone the image in one shot
 * (avoids ZINS expand/repack). Format conversion: packForShip packs LZMA;
 * editor/session use packForShip=FALSE (flat ZREZ stamp). */
BAEResult PV_BankSaveToMemory(BAEBankToken bankToken,
                                     int32_t overrideResourceID,
                                     bool packForShip,
                                     unsigned char **outData,
                                     uint32_t *outSize)
{
    XFILE bankFile;
    XFILE outFile;
    XFILERESOURCEMAP map;
    int32_t resourceID;
    int32_t sourceID;
    XPTR data;
    int32_t size;

    if (!outData || !outSize)
    {
        return BAE_PARAM_ERR;
    }
    *outData = NULL;
    *outSize = 0;
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    /* Drop XReplaceFileResource debris before cloning the image — same-format
     * save is a raw byte copy and would otherwise persist GONE forever. */
    (void)XCompactFileGoneResources(bankFile);

    /* Read the source bank's map header to determine IREZ vs ZREZ */
    if (XFileSetPosition(bankFile, 0L) != 0 ||
        XFileRead(bankFile, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        return BAE_BAD_FILE;
    }
    sourceID = (int32_t)XGetLong(&map.mapID);
    if (!XFILERESOURCE_ID_IS_VALID(sourceID))
    {
        return BAE_BAD_FILE;
    }

    /* Override format if requested (e.g. .zsb extension forces ZREZ) */
    resourceID = (overrideResourceID != 0) ? overrideResourceID : sourceID;

    /* Same container, or IREZ→ZREZ: clone the image (keeps sample payloads
     * in place). IREZ→ZREZ then patches the map header and runs Clean once to
     * pack ZINS — avoids re-copying every SND through Get/Add. */
    if (resourceID == sourceID ||
        (sourceID == XFILERESOURCE_ID && resourceID == XFILERESOURCE_ZMF_ID))
    {
        int32_t imageSize = XFileGetLength(bankFile);
        XFILE tmpFile;

        if (imageSize <= (int32_t)sizeof(XFILERESOURCEMAP))
        {
            return BAE_BAD_FILE;
        }
        data = XNewPtr(imageSize);
        if (!data)
        {
            return BAE_MEMORY_ERR;
        }
        if (XFileSetPosition(bankFile, 0L) != 0 ||
            XFileRead(bankFile, data, imageSize) != 0)
        {
            XDisposePtr(data);
            return BAE_BAD_FILE;
        }

        if (resourceID == sourceID)
        {
            *outData = (unsigned char *)data;
            *outSize = (uint32_t)imageSize;
#if USE_ZMF_SUPPORT == TRUE
            if (resourceID == XFILERESOURCE_ZMF_ID)
            {
                PV_StampZmfMapVersionForOscillator(*outData, *outSize,
                                                   PV_BankHasOscillator(bankToken));
            }
#endif
            return BAE_NO_ERROR;
        }

        /* IREZ → ZREZ: rewrite map ID/version. Ship export packs; editor/session
         * keep flat resources (valid ZREZ without ZINS LZMA). */
        {
            XFILERESOURCEMAP hdr;
            XBlockMove(data, &hdr, (int32_t)sizeof(hdr));
            XPutLong(&hdr.mapID, (uint32_t)resourceID);
            XPutLong(&hdr.version, (uint32_t)XFILERESOURCE_VERSION_FOR_ID(resourceID));
            XBlockMove(&hdr, data, (int32_t)sizeof(hdr));
        }
        tmpFile = XFileOpenWritableResourceFromMemory(data, (uint32_t)imageSize);
        if (!tmpFile)
        {
            XDisposePtr(data);
            return BAE_MEMORY_ERR;
        }
        if (packForShip)
        {
            if (XPackResourceFileForShip(tmpFile) == FALSE &&
                XCleanResourceFileEx(tmpFile, FALSE) == FALSE)
            {
                XFileClose(tmpFile);
                *outData = (unsigned char *)data;
                *outSize = (uint32_t)imageSize;
#if USE_ZMF_SUPPORT == TRUE
                PV_StampZmfMapVersionForOscillator(*outData, *outSize,
                                                   PV_BankHasOscillator(bankToken));
#endif
                return BAE_NO_ERROR;
            }
        }
        else if (XFinalizeEditorResourceFile(tmpFile) == FALSE)
        {
            XFileClose(tmpFile);
            *outData = (unsigned char *)data;
            *outSize = (uint32_t)imageSize;
#if USE_ZMF_SUPPORT == TRUE
            PV_StampZmfMapVersionForOscillator(*outData, *outSize,
                                               PV_BankHasOscillator(bankToken));
#endif
            return BAE_NO_ERROR;
        }
        XDisposePtr(data);
        data = NULL;
        if (XFileGetMemoryFileAsData(tmpFile, &data, &size) != 0 || !data || size <= 0)
        {
            XFileClose(tmpFile);
            if (data)
            {
                XDisposePtr(data);
            }
            return BAE_MEMORY_ERR;
        }
        XFileClose(tmpFile);
        *outData = (unsigned char *)data;
        *outSize = (uint32_t)size;
#if USE_ZMF_SUPPORT == TRUE
        PV_StampZmfMapVersionForOscillator(*outData, *outSize,
                                           PV_BankHasOscillator(bankToken));
#endif
        return BAE_NO_ERROR;
    }

    /* Create a virtual (in-memory) resource file with the target format */
    outFile = XFileOpenVirtualResource(resourceID);
    if (!outFile)
    {
        return BAE_MEMORY_ERR;
    }

    /* Enumerate all known bank resource types and copy each resource.
     * Bank files typically contain: INST, SND/CSND/ESND, ALIAS, and
     * possibly other types.  We iterate over a fixed list of known types
     * rather than using XCountTypes/XGetIndexedType (which are unimplemented). */
    {
        static const XResourceType bankResourceTypes[] = {
            ID_INST,
            ID_SND,
            ID_CSND,
            ID_ESND,
            ID_ALIAS,
            ID_BANK,
            ID_SONG,
            ID_MIDI,
            ID_MIDI_OLD,
            ID_CMID,
            ID_EMID,
            ID_ECMI,
            ID_RMF,
            ID_TEXT,
            ID_VERS,
            0  /* sentinel */
        };
        int32_t typeIdx;

        int32_t soundCount = 0;
        for (typeIdx = 0; bankResourceTypes[typeIdx] != 0; ++typeIdx)
        {
            XResourceType resType;
            int32_t resCount;
            int32_t resIndex;

            resType = bankResourceTypes[typeIdx];
            resCount = XCountFileResourcesOfType(bankFile, resType);
            for (resIndex = 0; resIndex < resCount; ++resIndex)
            {
                if (resType == ID_SND || resType == ID_CSND || resType == ID_ESND)
                {
                    ++soundCount;
                }
                if (soundCount > MAX_SAMPLES)
                {
                    XFileClose(outFile);
                    return BAE_TOO_MANY_SAMPLES;  // too many sound resources for 16-bit IDs
                }
                XLongResourceID resID;
                int32_t resSize;
                XPTR resData;
                char resName[256];

                resName[0] = 0;
                resData = XGetIndexedFileResource(bankFile, resType, &resID,
                                                  resIndex, resName, &resSize);
                if (!resData)
                {
                    continue;
                }
                int32_t result = XAddFileResource(outFile, resType, resID, resName, resData, resSize);
                if (result != 0)
                {
                    XDisposePtr(resData);
                    XFileClose(outFile);
                    return result;
                }
                XDisposePtr(resData);
            }
        }
    }

    /* Finalize: ship pack or editor flat finalize. */
    if (packForShip)
    {
        if (XPackResourceFileForShip(outFile) == FALSE)
        {
            XFileClose(outFile);
            return BAE_FILE_IO_ERROR;
        }
    }
    else if (XFinalizeEditorResourceFile(outFile) == FALSE)
    {
        XFileClose(outFile);
        return BAE_FILE_IO_ERROR;
    }

    /* Extract the serialized data */
    data = NULL;
    size = 0;
    if (XFileGetMemoryFileAsData(outFile, &data, &size) != 0 || !data || size <= 0)
    {
        XFileClose(outFile);
        if (data)
        {
            XDisposePtr(data);
        }
        return BAE_MEMORY_ERR;
    }
    XFileClose(outFile);

    *outData = (unsigned char *)data;
    *outSize = (uint32_t)size;
#if USE_ZMF_SUPPORT == TRUE
    if (resourceID == XFILERESOURCE_ZMF_ID)
    {
        PV_StampZmfMapVersionForOscillator(*outData, *outSize,
                                           PV_BankHasOscillator(bankToken));
    }
#endif
    return BAE_NO_ERROR;
}


/* Public wrapper - preserves source bank format (no override). */
BAEResult BAERmfEditorBank_SaveToMemory(BAEBankToken bankToken,
                                        unsigned char **outData,
                                        uint32_t *outSize)
{
    return PV_BankSaveToMemory(bankToken, 0, TRUE, outData, outSize);
}


BAEResult BAERmfEditorBank_SaveToMemoryExFlags(BAEBankToken bankToken,
                                               int32_t overrideResourceID,
                                               BAE_BOOL packForShip,
                                               unsigned char **outData,
                                               uint32_t *outSize)
{
    return PV_BankSaveToMemory(bankToken,
                               overrideResourceID,
                               packForShip ? TRUE : FALSE,
                               outData,
                               outSize);
}


BAEResult BAERmfEditorBank_SaveToMemoryEx(BAEBankToken bankToken,
                                          int32_t overrideResourceID,
                                          unsigned char **outData,
                                          uint32_t *outSize)
{
    /* Export default: pack on format convert. */
    return PV_BankSaveToMemory(bankToken, overrideResourceID, TRUE, outData, outSize);
}


BAEResult BAERmfEditorBank_SaveToFile(BAEBankToken bankToken,
                                      BAEPathName filePath)
{
    unsigned char *bankData;
    uint32_t bankSize;
    XFILENAME name;
    XFILE fileRef;
    BAEResult result;
    int32_t overrideResourceID;
    const char *ext;

    if (!bankToken || !filePath)
    {
        return BAE_PARAM_ERR;
    }

    /* Choose ZREZ format for .zsb files, like ZMF does for .zmf */
    overrideResourceID = 0;
    ext = strrchr(filePath, '.');
    if (ext && (strcmp(ext, ".zsb") == 0 || strcmp(ext, ".ZSB") == 0))
    {
#if USE_ZMF_SUPPORT == TRUE
        overrideResourceID = XFILERESOURCE_ZMF_ID;
#else
        return BAE_UNSUPPORTED_FORMAT;
#endif
    }
    else if (ext && (strcmp(ext, ".hsb") == 0 || strcmp(ext, ".HSB") == 0))
    {
        uint32_t zsbReason = 0;
        if (BAERmfEditorBank_RequiresZsb(bankToken, &zsbReason) != FALSE)
        {
            /* Modern codecs / extended ADSR / etc. cannot be written as classic HSB. */
            return BAE_UNSUPPORTED_FORMAT;
        }
        overrideResourceID = XFILERESOURCE_ID;
    }

    bankData = NULL;
    bankSize = 0;
    result = PV_BankSaveToMemory(bankToken, overrideResourceID, TRUE, &bankData, &bankSize);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    XConvertPathToXFILENAME(filePath, &name);
    fileRef = XFileOpenForWrite(&name, TRUE);
    if (!fileRef)
    {
        XDisposePtr((XPTR)bankData);
        return BAE_FILE_IO_ERROR;
    }

    if (XFileSetLength(fileRef, 0) != 0 ||
        XFileSetPosition(fileRef, 0L) != 0 ||
        XFileWrite(fileRef, bankData, (int32_t)bankSize) != 0)
    {
        XFileClose(fileRef);
        XDisposePtr((XPTR)bankData);
        return BAE_FILE_IO_ERROR;
    }

    XFileClose(fileRef);
    XDisposePtr((XPTR)bankData);
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_GetSampleWaveformData(BAEBankToken bankToken,
                                                  uint32_t instrumentIndex,
                                                  uint32_t sampleIndex,
                                                  void **outWaveData,
                                                  uint32_t *outFrameCount,
                                                  uint16_t *outBitSize,
                                                  uint16_t *outChannels,
                                                  BAE_UNSIGNED_FIXED *outSampleRate)
{
    enum
    {
        kInstHeaderMinSize = 14
    };
    XFILE bankFile;
    XPTR instData;
    XLongResourceID instID;
    int32_t instSize;
    char rawName[256];
    InstrumentResource *inst;
    KeySplit split;
    XShortResourceID sndID;
    int16_t splitCount;
    XPTR sndData;
    int32_t sndSize;
    SampleDataInfo sdi;
    XPTR pcmData;
    XPTR pcmOwner;
    int32_t pcmSize;
    XPTR ownedPcm;
    /* Prefer encrypted/compressed containers — session promote can leave a
     * colliding plain GM SND alongside the intended ESND/CSND. */
    static const XResourceType sndTypes[] = { ID_ESND, ID_CSND, ID_SND, 0 };
    int32_t typeIdx;

    if (!outWaveData || !outFrameCount || !outBitSize || !outChannels || !outSampleRate)
    {
        return BAE_PARAM_ERR;
    }
    *outWaveData = NULL;
    *outFrameCount = 0;
    *outBitSize = 0;
    *outChannels = 0;
    *outSampleRate = 0;
    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    rawName[0] = 0;
    instData = XGetIndexedFileResource(bankFile, ID_INST, &instID,
                                       (int32_t)instrumentIndex, rawName, &instSize);
    if (!instData)
    {
        return BAE_BAD_FILE;
    }
    if (instSize < kInstHeaderMinSize)
    {
        XDisposePtr(instData);
        return BAE_BAD_FILE;
    }
    inst = (InstrumentResource *)instData;
    splitCount = (int16_t)XGetShort(&inst->keySplitCount);
    if (splitCount < 0)
    {
        splitCount = 0;
    }

    /* Resolve the SND resource ID from the instrument or key split */
    if (splitCount > 0)
    {
        if (sampleIndex >= (uint32_t)splitCount)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }
        XGetKeySplitFromPtr(inst, (int16_t)sampleIndex, &split);
        sndID = split.sndResourceID;
    }
    else
    {
        if (sampleIndex != 0)
        {
            XDisposePtr(instData);
            return BAE_PARAM_ERR;
        }
        sndID = (XShortResourceID)XGetShort(&inst->sndResourceID);
    }
    XDisposePtr(instData);

    /* Load the SND resource from the bank file */
    sndData = NULL;
    sndSize = 0;
    for (typeIdx = 0; sndTypes[typeIdx] != 0; ++typeIdx)
    {
        sndData = XGetFileResource(bankFile, sndTypes[typeIdx], (XLongResourceID)sndID, NULL, &sndSize);
        if (sndData)
        {
            /* Match sample-info path: preprocess container payload before
             * parsing/decoding the SND structure. */
            if (sndTypes[typeIdx] == ID_CSND)
            {
                XPTR decompressed = XDecompressPtr(sndData, (uint32_t)sndSize, FALSE);
                XDisposePtr(sndData);
                sndData = decompressed;
                if (sndData)
                {
                    sndSize = XGetPtrSize(sndData);
                }
            }
            else if (sndTypes[typeIdx] == ID_ESND)
            {
                XDecryptData(sndData, (uint32_t)sndSize);
            }
            break;
        }
    }
    if (!sndData || sndSize <= 0)
    {
        return BAE_BAD_FILE;
    }

    /* Validate the SND format */
    {
        int16_t soundFormat = (int16_t)XGetShort(sndData);
        if (soundFormat != 1 && soundFormat != 2 && soundFormat != 3)
        {
            XDisposePtr(sndData);
            return BAE_BAD_FILE;
        }
    }

    /* Decode the SND to PCM */
    XSetMemory(&sdi, (int32_t)sizeof(sdi), 0);
    pcmData = XGetSamplePtrFromSnd(sndData, &sdi);
    pcmOwner = NULL;
    if (sdi.pMasterPtr && sdi.pMasterPtr != sndData)
    {
        pcmOwner = sdi.pMasterPtr;
    }
    if (!pcmData || sdi.bitSize == 0 || sdi.channels == 0)
    {
        if (pcmOwner)
        {
            XDisposePtr(pcmOwner);
        }
        XDisposePtr(sndData);
        return BAE_BAD_FILE;
    }

    /* Copy the PCM data so we can free the SND resource */
    pcmSize = (int32_t)(sdi.frames * (sdi.bitSize / 8) * sdi.channels);
    if (pcmSize <= 0)
    {
        if (pcmOwner)
        {
            XDisposePtr(pcmOwner);
        }
        XDisposePtr(sndData);
        return BAE_BAD_FILE;
    }
    ownedPcm = XNewPtr(pcmSize);
    if (!ownedPcm)
    {
        if (pcmOwner)
        {
            XDisposePtr(pcmOwner);
        }
        XDisposePtr(sndData);
        return BAE_MEMORY_ERR;
    }
    XBlockMove(pcmData, ownedPcm, pcmSize);

    if (pcmOwner)
    {
        XDisposePtr(pcmOwner);
    }
    XDisposePtr(sndData);

    *outWaveData = ownedPcm;
    *outFrameCount = sdi.frames;
    *outBitSize = sdi.bitSize;
    *outChannels = sdi.channels;
    *outSampleRate = sdi.rate;
    return BAE_NO_ERROR;
}


BAEResult BAERmfEditorBank_GetSndWaveformData(BAEBankToken bankToken,
                                               uint32_t sndResourceID,
                                               void **outWaveData,
                                               uint32_t *outFrameCount,
                                               uint16_t *outBitSize,
                                               uint16_t *outChannels,
                                               BAE_UNSIGNED_FIXED *outSampleRate)
{
    XFILE bankFile;
    XPTR sndData;
    int32_t sndSize;
    static const XResourceType sndTypes[] = { ID_ESND, ID_CSND, ID_SND, 0 };
    int32_t typeIdx;
    SampleDataInfo sdi;
    XPTR pcmData;
    XPTR pcmOwner;
    int32_t pcmSize;
    XPTR ownedPcm;

    if (!outWaveData || !outFrameCount || !outBitSize || !outChannels || !outSampleRate)
    {
        return BAE_PARAM_ERR;
    }
    *outWaveData = NULL;
    *outFrameCount = 0;
    *outBitSize = 0;
    *outChannels = 0;
    *outSampleRate = 0;
    if (!bankToken || sndResourceID > 32767u)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    sndData = NULL;
    sndSize = 0;
    for (typeIdx = 0; sndTypes[typeIdx] != 0; ++typeIdx)
    {
        sndData = XGetFileResource(bankFile,
                                   sndTypes[typeIdx],
                                   (XLongResourceID)sndResourceID,
                                   NULL,
                                   &sndSize);
        if (sndData)
        {
            if (sndTypes[typeIdx] == ID_CSND)
            {
                XPTR decompressed = XDecompressPtr(sndData, (uint32_t)sndSize, FALSE);
                XDisposePtr(sndData);
                sndData = decompressed;
                if (sndData)
                {
                    sndSize = XGetPtrSize(sndData);
                }
            }
            else if (sndTypes[typeIdx] == ID_ESND)
            {
                XDecryptData(sndData, (uint32_t)sndSize);
            }
            break;
        }
    }
    if (!sndData || sndSize <= 0)
    {
        return BAE_BAD_FILE;
    }
    {
        int16_t soundFormat = (int16_t)XGetShort(sndData);
        if (soundFormat != 1 && soundFormat != 2 && soundFormat != 3)
        {
            XDisposePtr(sndData);
            return BAE_BAD_FILE;
        }
    }

    XSetMemory(&sdi, (int32_t)sizeof(sdi), 0);
    pcmData = XGetSamplePtrFromSnd(sndData, &sdi);
    pcmOwner = NULL;
    if (sdi.pMasterPtr && sdi.pMasterPtr != sndData)
    {
        pcmOwner = sdi.pMasterPtr;
    }
    if (!pcmData || sdi.bitSize == 0 || sdi.channels == 0)
    {
        if (pcmOwner)
        {
            XDisposePtr(pcmOwner);
        }
        XDisposePtr(sndData);
        return BAE_BAD_FILE;
    }
    pcmSize = (int32_t)(sdi.frames * (sdi.bitSize / 8) * sdi.channels);
    if (pcmSize <= 0)
    {
        if (pcmOwner)
        {
            XDisposePtr(pcmOwner);
        }
        XDisposePtr(sndData);
        return BAE_BAD_FILE;
    }
    ownedPcm = XNewPtr(pcmSize);
    if (!ownedPcm)
    {
        if (pcmOwner)
        {
            XDisposePtr(pcmOwner);
        }
        XDisposePtr(sndData);
        return BAE_MEMORY_ERR;
    }
    XBlockMove(pcmData, ownedPcm, pcmSize);
    if (pcmOwner)
    {
        XDisposePtr(pcmOwner);
    }
    XDisposePtr(sndData);

    *outWaveData = ownedPcm;
    *outFrameCount = sdi.frames;
    *outBitSize = sdi.bitSize;
    *outChannels = sdi.channels;
    *outSampleRate = sdi.rate;
    return BAE_NO_ERROR;
}


void BAERmfEditorBank_FreeWaveformData(void *waveData)
{
    if (waveData)
    {
        XDisposePtr((XPTR)waveData);
    }
}


BAEResult BAERmfEditorBank_ExportSndResourceToFile(BAEBankToken bankToken,
                                                   uint32_t sndResourceID,
                                                   BAEPathName filePath)
{
    XFILE bankFile;
    XPTR sndData;
    int32_t sndSize;
    /* Prefer ESND/CSND over plain SND — session promote can leave a colliding
     * GM SND beside the intended custom container (same as waveform fetch). */
    static const XResourceType sndTypes[] = { ID_ESND, ID_CSND, ID_SND, 0 };
    int32_t typeIdx;
    SampleDataInfo sdi;
    BAEResult passResult;
    XPTR pcmData;
    XPTR pcmOwner;
    int32_t pcmSize;
    GM_Waveform wave;
    XFILENAME fileName;
    OPErr opErr;
    AudioFileType outType;
    char const *ext;

    if (!bankToken || !filePath)
    {
        return BAE_PARAM_ERR;
    }
    bankFile = (XFILE)bankToken;

    sndData = NULL;
    sndSize = 0;
    for (typeIdx = 0; sndTypes[typeIdx] != 0; ++typeIdx)
    {
        sndData = XGetFileResource(bankFile, sndTypes[typeIdx],
                                   (XLongResourceID)sndResourceID, NULL, &sndSize);
        if (sndData)
        {
            if (sndTypes[typeIdx] == ID_CSND)
            {
                XPTR decompressed = XDecompressPtr(sndData, (uint32_t)sndSize, FALSE);
                XDisposePtr(sndData);
                sndData = decompressed;
                if (sndData)
                {
                    sndSize = XGetPtrSize(sndData);
                }
            }
            else if (sndTypes[typeIdx] == ID_ESND)
            {
                /* Never decrypt in place — XGetFileResource may return a borrowed
                 * memory-bank pointer. */
                XPTR copy = XNewPtr(sndSize);
                if (!copy)
                {
                    XDisposePtr(sndData);
                    return BAE_MEMORY_ERR;
                }
                XBlockMove(sndData, copy, sndSize);
                XDisposePtr(sndData);
                sndData = copy;
                XDecryptData(sndData, (uint32_t)sndSize);
            }
            break;
        }
    }
    if (!sndData || sndSize <= 0)
    {
        return BAE_BAD_FILE;
    }

    {
        int16_t soundFormat = (int16_t)XGetShort(sndData);
        if (soundFormat != 1 && soundFormat != 2 && soundFormat != 3)
        {
            XDisposePtr(sndData);
            return BAE_BAD_FILE;
        }
    }

    XSetMemory(&sdi, (int32_t)sizeof(sdi), 0);
    if (XGetSampleInfoFromSnd(sndData, &sdi) == 0)
    {
        passResult = PV_ExportSndBitstreamToFile(sndData,
                                                 sndSize,
                                                 (SndCompressionType)sdi.compressionType,
                                                 filePath);
        if (passResult != BAE_NOT_SETUP)
        {
            XDisposePtr(sndData);
            return passResult;
        }
    }

    /* PCM / IMA / ADPCM (or failed bitstream extract): decode and write WAV/AIFF. */
    XSetMemory(&sdi, (int32_t)sizeof(sdi), 0);
    pcmData = XGetSamplePtrFromSnd(sndData, &sdi);
    pcmOwner = NULL;
    if (sdi.pMasterPtr && sdi.pMasterPtr != sndData)
    {
        pcmOwner = sdi.pMasterPtr;
    }
    if (!pcmData || sdi.bitSize == 0 || sdi.channels == 0 || sdi.frames == 0)
    {
        if (pcmOwner)
        {
            XDisposePtr(pcmOwner);
        }
        XDisposePtr(sndData);
        return BAE_BAD_FILE;
    }

    pcmSize = (int32_t)(sdi.frames * (sdi.bitSize / 8) * sdi.channels);
    if (pcmSize <= 0)
    {
        if (pcmOwner)
        {
            XDisposePtr(pcmOwner);
        }
        XDisposePtr(sndData);
        return BAE_BAD_FILE;
    }

    XSetMemory(&wave, (int32_t)sizeof(wave), 0);
    wave.theWaveform = (XPTR)pcmData;
    wave.waveSize = (uint32_t)pcmSize;
    wave.waveFrames = sdi.frames;
    wave.bitSize = (unsigned char)sdi.bitSize;
    wave.channels = (unsigned char)sdi.channels;
    wave.sampledRate = sdi.rate;
    wave.baseMidiPitch = (uint16_t)sdi.baseKey;
    wave.startLoop = sdi.loopStart;
    wave.endLoop = sdi.loopEnd;
    wave.compressionType = C_NONE; /* decoded PCM — required by GM_WriteFileFromMemory */

    ext = strrchr(filePath, '.');
    if (ext && (!XStrCmp(ext, ".aif") || !XStrCmp(ext, ".aiff") ||
               !XStrCmp(ext, ".AIF") || !XStrCmp(ext, ".AIFF")))
    {
        outType = FILE_AIFF_TYPE;
    }
    else
    {
        outType = FILE_WAVE_TYPE;
    }

    XConvertPathToXFILENAME(filePath, &fileName);
    opErr = GM_WriteFileFromMemory(&fileName, &wave, outType);

    if (pcmOwner)
    {
        XDisposePtr(pcmOwner);
    }
    XDisposePtr(sndData);
    if (opErr == NO_ERR)
    {
        return BAE_NO_ERROR;
    }
    if (opErr == PARAM_ERR || opErr == NOT_SETUP)
    {
        return BAE_PARAM_ERR;
    }
    if (opErr == MEMORY_ERR)
    {
        return BAE_MEMORY_ERR;
    }
    return BAE_FILE_IO_ERROR;
}


BAEResult BAERmfEditorBank_ExportSampleToFile(BAEBankToken bankToken,
                                              uint32_t instrumentIndex,
                                              uint32_t sampleIndex,
                                              BAEPathName filePath)
{
    BAERmfEditorBankSampleInfo info;
    BAEResult result;

    result = BAERmfEditorBank_GetInstrumentSampleInfo(bankToken, instrumentIndex,
                                                     sampleIndex, &info);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }
    return BAERmfEditorBank_ExportSndResourceToFile(bankToken,
                                                    (uint32_t)(uint16_t)info.sndResourceID,
                                                    filePath);
}

uint16_t PV_BankGroupFromInternalBank(uint16_t internalBank)
{
    uint16_t msb;

    if (internalBank < 128)
    {
        return internalBank;
    }
    msb = (uint16_t)((internalBank >> 7) & 0x7F);
    if (msb == 120u || msb == 121u)
    {
        return 0;
    }
    return msb;
}


/* Map editor/document bank to MIDI CC0/CC32. Beatnik uses CC0 as bank group. */
void PV_BankToMidiMsbLsb(uint16_t internalBank, uint16_t *outMsb, uint16_t *outLsb)
{
    if (!outMsb || !outLsb)
    {
        return;
    }
    if (internalBank < 128)
    {
        *outMsb = internalBank;
        *outLsb = 0;
        return;
    }
    *outMsb = (uint16_t)((internalBank >> 7) & 0x7F);
    *outLsb = (uint16_t)(internalBank & 0x7F);
}


/* True when two stored bank values select the same Beatnik/MIDI bank. */
bool PV_BanksEquivalent(uint16_t a, uint16_t b)
{
    uint16_t aMsb;
    uint16_t aLsb;
    uint16_t bMsb;
    uint16_t bLsb;

    if (a == b)
    {
        return TRUE;
    }
    PV_BankToMidiMsbLsb(a, &aMsb, &aLsb);
    PV_BankToMidiMsbLsb(b, &bMsb, &bLsb);
    return (aMsb == bMsb && aLsb == bLsb) ? TRUE : FALSE;
}


/* Core encode logic shared by bank re-encode and in-memory compression preview.
 * waveData is borrowed - caller owns it (may be modified in place by codecs).
 * When outPreviewPcm is non-NULL, encode+decode only (no bank I/O). */
BAEResult PV_BankReEncodeSampleCore(XFILE bankFile,
                                            BAERmfEditorBankSampleInfo *pSampleInfo,
                                            void *waveData,
                                            uint32_t frameCount,
                                            uint16_t bitSize,
                                            uint16_t channels,
                                            BAE_UNSIGNED_FIXED sampleRate,
                                            BAERmfEditorCompressionType compressionType,
                                            BAERmfEditorSndStorageType sndStorageType,
                                            BAERmfEditorOpusMode opusMode,
                                            bool opusRoundTripResample,
                                            void **outPreviewPcm,
                                            uint32_t *outPreviewFrames,
                                            uint16_t *outPreviewBitSize,
                                            uint16_t *outPreviewChannels,
                                            BAE_UNSIGNED_FIXED *outPreviewRate,
                                            uint32_t *outEncodedBytes)
{
    BAEResult result;
    SndCompressionType compType;
    SndCompressionSubType compSubType;
    SndCompressionSubType encodeCompSubType;
    GM_Waveform writeWaveform;
    XResourceType oldSndType;
    XResourceType newSndType;
    XPTR oldSndRawData;
    int32_t oldSndRawSize;
    XPTR oldSndPlain;
    int32_t oldSndPlainSize;
    bool oldSndPlainOwned;
    bool needInspectOldSndHeader;
    bool previewOnly;
    SampleDataInfo oldSndInfo;
    int16_t preservedBaseKey;
    char sndName[256];
    XPTR sndResource;
    XPTR encodeWaveDataOwner;
    XPTR wrappedSnd;
    int32_t wrappedSndSize;
    OPErr opErr;
    bool sampleWasEncodedMpeg;
    int32_t normalizedLoopStart;
    int32_t normalizedLoopEnd;
    uint32_t inputPcmRateHz;
    uint32_t decodedSampleRateForSnd;
    uint32_t decodedFramesForRate;
    int32_t roundTripSourceRate;
    int32_t loopStart;
    int32_t loopEnd;

    previewOnly = (outPreviewPcm != NULL);
    if (previewOnly)
    {
        *outPreviewPcm = NULL;
        if (outPreviewFrames) { *outPreviewFrames = 0; }
        if (outPreviewBitSize) { *outPreviewBitSize = 0; }
        if (outPreviewChannels) { *outPreviewChannels = 0; }
        if (outPreviewRate) { *outPreviewRate = 0; }
        if (outEncodedBytes) { *outEncodedBytes = 0; }
    }
    else if (!bankFile)
    {
        return BAE_PARAM_ERR;
    }

    /* Map editor compression type to SND compression type / sub-type */
    sampleWasEncodedMpeg = FALSE;
    normalizedLoopStart = (int32_t)pSampleInfo->loopStart;
    normalizedLoopEnd = (int32_t)pSampleInfo->loopEnd;
    inputPcmRateHz = 0;
    decodedSampleRateForSnd = 0;
    decodedFramesForRate = 0;
    roundTripSourceRate = 0;
    loopStart = 0;
    loopEnd = 0;
    oldSndPlain = NULL;
    oldSndPlainSize = 0;
    oldSndPlainOwned = FALSE;
    needInspectOldSndHeader = FALSE;
    oldSndType = ID_SND;
    XSetMemory(&oldSndInfo, (int32_t)sizeof(oldSndInfo), 0);
    /* Editor/INST rootKey is NOT the SND baseFrequency.  The engine always
     * loads baseMidiPitch from the SND header; when useSoundModifierAsRootKey
     * is clear that value is the playback root.  Prefer the existing SND key
     * and only fall back to the caller's rootKey (preview path / missing SND). */
    preservedBaseKey = -1;
    compType = C_NONE;
    compSubType = CS_DEFAULT;
    switch (compressionType)
    {
        case BAE_EDITOR_COMPRESSION_ADPCM:
            compType = C_IMA4;
            compSubType = CS_DEFAULT;
            break;
#if USE_ZMF_SUPPORT == TRUE
        case BAE_EDITOR_COMPRESSION_ADPCM_2BIT:
            compType = C_IMA2;
            compSubType = CS_DEFAULT;
            break;
#endif
        case BAE_EDITOR_COMPRESSION_ALAW:
            compType = C_ALAW;
            compSubType = CS_DEFAULT;
            break;
        case BAE_EDITOR_COMPRESSION_ULAW:
            compType = C_ULAW;
            compSubType = CS_DEFAULT;
            break;
        case BAE_EDITOR_COMPRESSION_MP3_32K:
            compType = C_MPEG_32;  compSubType = CS_MPEG2; sampleWasEncodedMpeg = TRUE; break;
        case BAE_EDITOR_COMPRESSION_MP3_48K:
            compType = C_MPEG_48;  compSubType = CS_MPEG2; sampleWasEncodedMpeg = TRUE; break;
        case BAE_EDITOR_COMPRESSION_MP3_64K:
            compType = C_MPEG_64;  compSubType = CS_MPEG2; sampleWasEncodedMpeg = TRUE; break;
        case BAE_EDITOR_COMPRESSION_MP3_96K:
            compType = C_MPEG_96;  compSubType = CS_MPEG2; sampleWasEncodedMpeg = TRUE; break;
        case BAE_EDITOR_COMPRESSION_MP3_128K:
            compType = C_MPEG_128; compSubType = CS_MPEG2; sampleWasEncodedMpeg = TRUE; break;
        case BAE_EDITOR_COMPRESSION_MP3_192K:
            compType = C_MPEG_192; compSubType = CS_MPEG2; sampleWasEncodedMpeg = TRUE; break;
        case BAE_EDITOR_COMPRESSION_MP3_256K:
            compType = C_MPEG_256; compSubType = CS_MPEG2; sampleWasEncodedMpeg = TRUE; break;
        case BAE_EDITOR_COMPRESSION_MP3_320K:
            compType = C_MPEG_320; compSubType = CS_MPEG2; sampleWasEncodedMpeg = TRUE; break;
#if USE_VORBIS_ENCODER == TRUE && USE_VORBIS_DECODER == TRUE
        case BAE_EDITOR_COMPRESSION_VORBIS_32K:
            compType = C_VORBIS; compSubType = CS_VORBIS_32K; break;
        case BAE_EDITOR_COMPRESSION_VORBIS_48K:
            compType = C_VORBIS; compSubType = CS_VORBIS_48K; break;
        case BAE_EDITOR_COMPRESSION_VORBIS_64K:
            compType = C_VORBIS; compSubType = CS_VORBIS_64K; break;
        case BAE_EDITOR_COMPRESSION_VORBIS_80K:
            compType = C_VORBIS; compSubType = CS_VORBIS_80K; break;
        case BAE_EDITOR_COMPRESSION_VORBIS_96K:
            compType = C_VORBIS; compSubType = CS_VORBIS_96K; break;
        case BAE_EDITOR_COMPRESSION_VORBIS_128K:
            compType = C_VORBIS; compSubType = CS_VORBIS_128K; break;
        case BAE_EDITOR_COMPRESSION_VORBIS_160K:
            compType = C_VORBIS; compSubType = CS_VORBIS_160K; break;
        case BAE_EDITOR_COMPRESSION_VORBIS_192K:
            compType = C_VORBIS; compSubType = CS_VORBIS_192K; break;
        case BAE_EDITOR_COMPRESSION_VORBIS_256K:
            compType = C_VORBIS; compSubType = CS_VORBIS_256K; break;
#endif
#if USE_FLAC_ENCODER == TRUE && USE_FLAC_DECODER == TRUE
        case BAE_EDITOR_COMPRESSION_FLAC:
            compType = C_FLAC; compSubType = CS_DEFAULT; break;
#endif
#if USE_OPUS_ENCODER == TRUE || USE_OPUS_DECODER == TRUE
        case BAE_EDITOR_COMPRESSION_OPUS_12K:
            compType = C_OPUS; compSubType = CS_OPUS_12K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_16K:
            compType = C_OPUS; compSubType = CS_OPUS_16K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_24K:
            compType = C_OPUS; compSubType = CS_OPUS_24K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_32K:
            compType = C_OPUS; compSubType = CS_OPUS_32K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_48K:
            compType = C_OPUS; compSubType = CS_OPUS_48K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_64K:
            compType = C_OPUS; compSubType = CS_OPUS_64K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_80K:
            compType = C_OPUS; compSubType = CS_OPUS_80K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_96K:
            compType = C_OPUS; compSubType = CS_OPUS_96K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_128K:
            compType = C_OPUS; compSubType = CS_OPUS_128K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_160K:
            compType = C_OPUS; compSubType = CS_OPUS_160K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_192K:
            compType = C_OPUS; compSubType = CS_OPUS_192K; break;
        case BAE_EDITOR_COMPRESSION_OPUS_256K:
            compType = C_OPUS; compSubType = CS_OPUS_256K; break;
#endif
#if USE_QOA_SUPPORT == TRUE
        case BAE_EDITOR_COMPRESSION_QOA:
            compType = C_QOA; compSubType = CS_DEFAULT; break;
#endif
        case BAE_EDITOR_COMPRESSION_PCM:
        default:
            compType = C_NONE;
            compSubType = CS_DEFAULT;
            break;
    }

    /* Find and inspect the existing SND resource so we can preserve
     * source playback metadata (notably base key) across bank re-encodes.
     * Preview path skips all bank I/O and uses the caller's rootKey. */
    sndName[0] = 0;
    oldSndRawData = NULL;
    oldSndRawSize = 0;
    if (!previewOnly)
    {
        result = PV_BankFindSndResource(bankFile, pSampleInfo->sndResourceID,
                                        &oldSndType, &oldSndRawData, &oldSndRawSize, sndName);
        if (result != BAE_NO_ERROR)
        {
            return result;
        }

        /* Always read the prior SND baseKey — do not stamp INST/editor rootKey
         * onto the sample header (that breaks pitch for !useSoundModifierAsRootKey). */
        needInspectOldSndHeader = TRUE;

        if (needInspectOldSndHeader)
        {
            oldSndPlain = oldSndRawData;
            oldSndPlainSize = oldSndRawSize;
            if (oldSndType == ID_CSND)
            {
                oldSndPlain = XDecompressPtr(oldSndRawData, (uint32_t)oldSndRawSize, FALSE);
                oldSndPlainSize = oldSndPlain ? XGetPtrSize(oldSndPlain) : 0;
                oldSndPlainOwned = TRUE;
            }
            else if (oldSndType == ID_ESND)
            {
                XDecryptData(oldSndPlain, (uint32_t)oldSndPlainSize);
            }

            if (oldSndPlain && oldSndPlainSize > 0)
            {
                if (XGetSampleInfoFromSnd(oldSndPlain, &oldSndInfo) == 0 &&
                    oldSndInfo.baseKey >= 0 && oldSndInfo.baseKey <= 127)
                {
                    preservedBaseKey = oldSndInfo.baseKey;
                }
            }
        }

        if (preservedBaseKey < 0 || preservedBaseKey > 127)
        {
            /* Missing/unreadable SND header: fall back to editor root, then middle C. */
            if (pSampleInfo->rootKey <= 127)
            {
                preservedBaseKey = (int16_t)pSampleInfo->rootKey;
            }
            else
            {
                preservedBaseKey = 60;
            }
        }
    }
    else
    {
        /* In-memory preview: caller supplies the intended base key via rootKey. */
        preservedBaseKey = (int16_t)pSampleInfo->rootKey;
        if (preservedBaseKey < 0 || preservedBaseKey > 127)
        {
            preservedBaseKey = 60;
        }
    }

    /* Build the source waveform descriptor */
    XSetMemory(&writeWaveform, (int32_t)sizeof(writeWaveform), 0);
    writeWaveform.theWaveform   = (signed char *)waveData;
    writeWaveform.waveFrames    = frameCount;
    writeWaveform.waveSize      = (int32_t)(frameCount * (uint32_t)(bitSize / 8u) * (uint32_t)channels);
    writeWaveform.bitSize       = bitSize;
    writeWaveform.channels      = channels;
    writeWaveform.sampledRate   = (int32_t)sampleRate;
    writeWaveform.baseMidiPitch = (uint16_t)preservedBaseKey;
    writeWaveform.compressionType = C_NONE;

    /* Bank sample metadata loop points are stored in sample-rate domain.
     * For non-RT Opus re-encode, incoming PCM may be decoded at 48kHz while
     * metadata/sampleRate remains at playback/header rate; remap loops to the
     * actual PCM frame domain before validation. */
#if USE_OPUS_ENCODER == TRUE || USE_OPUS_DECODER == TRUE
    if (compType == C_OPUS && !opusRoundTripResample)
    {
        if ((uint32_t)sampleRate >= (1000u << 16))
        {
            inputPcmRateHz = (uint32_t)sampleRate >> 16;
        }
        else if ((uint32_t)sampleRate >= 1000u && (uint32_t)sampleRate <= 384000u)
        {
            inputPcmRateHz = (uint32_t)sampleRate;
        }

        if (inputPcmRateHz > 0 && pSampleInfo->sampleRate > 0 && inputPcmRateHz != pSampleInfo->sampleRate)
        {
            uint32_t mappedStart;
            uint32_t mappedEnd;

            mappedStart = (uint32_t)(((uint64_t)(uint32_t)normalizedLoopStart * (uint64_t)inputPcmRateHz) /
                                     (uint64_t)pSampleInfo->sampleRate);
            mappedEnd = (uint32_t)((((uint64_t)(uint32_t)normalizedLoopEnd * (uint64_t)inputPcmRateHz) +
                                    ((uint64_t)pSampleInfo->sampleRate - 1ULL)) /
                                   (uint64_t)pSampleInfo->sampleRate);

            if (mappedStart > frameCount)
            {
                mappedStart = frameCount;
            }
            if (mappedEnd > frameCount)
            {
                mappedEnd = frameCount;
            }

            normalizedLoopStart = (int32_t)mappedStart;
            normalizedLoopEnd = (int32_t)mappedEnd;
        }
    }
#endif

    /* Validate/clamp loop points */
    if (normalizedLoopStart >= 0 &&
        normalizedLoopEnd > normalizedLoopStart &&
        (uint32_t)normalizedLoopEnd <= frameCount)
    {
        writeWaveform.startLoop = (uint32_t)normalizedLoopStart;
        writeWaveform.endLoop   = (uint32_t)normalizedLoopEnd;
    }

    /* Normalize sample rate to fixed-point */
    if ((uint32_t)writeWaveform.sampledRate == 0u)
    {
        writeWaveform.sampledRate = PV_IsOpusCompression(compressionType)
                                    ? (int32_t)(48000u << 16)
                                    : (int32_t)(44100u << 16);
    }
    else if ((uint32_t)writeWaveform.sampledRate < (1000u << 16))
    {
        if ((uint32_t)writeWaveform.sampledRate >= 1000u &&
            (uint32_t)writeWaveform.sampledRate <= 384000u)
        {
            writeWaveform.sampledRate <<= 16;
        }
        else
        {
            writeWaveform.sampledRate = PV_IsOpusCompression(compressionType)
                                        ? (int32_t)(48000u << 16)
                                        : (int32_t)(44100u << 16);
        }
    }

    encodeWaveDataOwner = NULL;

    /* MPEG: resample to a codec-compatible rate if needed */
    if (sampleWasEncodedMpeg)
    {
        BAE_UNSIGNED_FIXED sourceRate;
        BAE_UNSIGNED_FIXED targetRate;

        sourceRate = PV_NormalizeSampleRateForSave((BAE_UNSIGNED_FIXED)writeWaveform.sampledRate);
        targetRate = PV_ChooseCodecRateFromSourceHz(compressionType, (uint32_t)(sourceRate >> 16));
        if (targetRate == 0)
        {
            targetRate = (44100u << 16);
        }
        if (sourceRate != targetRate)
        {
            result = PV_ResampleWaveformLinear(&writeWaveform, targetRate, &encodeWaveDataOwner);
            if (result != BAE_NO_ERROR)
            {
                return result;
            }
        }
    }

#if USE_OPUS_ENCODER == TRUE || USE_OPUS_DECODER == TRUE
    /* Opus: resample to optimal rate for the chosen bitrate tier */
    if (compType == C_OPUS)
    {
        BAE_UNSIGNED_FIXED sourceRate;
        BAE_UNSIGNED_FIXED targetRate;

        sourceRate = PV_NormalizeSampleRateForSave((BAE_UNSIGNED_FIXED)writeWaveform.sampledRate);
        targetRate = sourceRate;
        if (!opusRoundTripResample)
        {
            targetRate = PV_ChooseCodecRateFromSourceHz(compressionType, (uint32_t)(sourceRate >> 16));
            if (targetRate == 0)
            {
                targetRate = (48000u << 16);
            }
        }
        if (sourceRate != targetRate)
        {
            result = PV_ResampleWaveformLinear(&writeWaveform, targetRate, &encodeWaveDataOwner);
            if (result != BAE_NO_ERROR)
            {
                if (encodeWaveDataOwner) { XDisposePtr(encodeWaveDataOwner); }
                return result;
            }
        }
        encodeCompSubType = PV_ComposeOpusEncodeSubType(compSubType, opusMode);
        if (opusRoundTripResample)
        {
            roundTripSourceRate = writeWaveform.sampledRate;
            writeWaveform.sampledRate = (int32_t)(48000u << 16);
        }
    }
    else
#endif
    {
        encodeCompSubType = compSubType;
    }

    /* Encode */
    sndResource = NULL;
    opErr = XCreateSoundObjectFromData(&sndResource, &writeWaveform, compType, encodeCompSubType, NULL, NULL);

    if (encodeWaveDataOwner)
    {
        XDisposePtr(encodeWaveDataOwner);
        encodeWaveDataOwner = NULL;
    }

    if (opErr != NO_ERR || !sndResource)
    {
        return BAE_BAD_FILE;
    }

    /* Store the compression sub-type in the SND header */
    PV_StoreCompressionSubTypeInSnd(sndResource, XGetPtrSize(sndResource), compType, compSubType);

    /* Update sample rate from the decoded stream (important for MPEG) */
    {
        int32_t sndSampleRate = writeWaveform.sampledRate;
        SampleDataInfo decodedInfo;
        XPTR decodedOwner;

        XSetMemory(&decodedInfo, (int32_t)sizeof(decodedInfo), 0);
        decodedOwner = NULL;
        (void)XGetSamplePtrFromSnd(sndResource, &decodedInfo);
        if (sampleWasEncodedMpeg && decodedInfo.rate != 0)
        {
            sndSampleRate = (int32_t)decodedInfo.rate;
            decodedSampleRateForSnd = (uint32_t)decodedInfo.rate;
        }

        decodedFramesForRate = decodedInfo.frames ? decodedInfo.frames : writeWaveform.waveFrames;
        loopStart = (int32_t)writeWaveform.startLoop;
        loopEnd   = (int32_t)writeWaveform.endLoop;

#if USE_OPUS_ENCODER == TRUE || USE_OPUS_DECODER == TRUE
        if (compType == C_OPUS)
        {
            if (opusRoundTripResample)
            {
                /* Match document save path: preserve source frame domain in RT mode,
                 * but clamp loop end to the actual encoded decode capacity. */
                PV_ForceSndDecodedFrameCount(sndResource, writeWaveform.waveFrames);
                decodedFramesForRate = writeWaveform.waveFrames;
                if (decodedInfo.frames > 0 && loopEnd > (int32_t)decodedInfo.frames)
                {
                    int32_t delta;

                    delta = loopEnd - (int32_t)decodedInfo.frames;
                    loopEnd = (int32_t)decodedInfo.frames;
                    if (delta > 0)
                    {
                        if (loopStart > delta)
                        {
                            loopStart -= delta;
                        }
                        else
                        {
                            loopStart = 0;
                        }
                    }
                    if (loopStart >= loopEnd)
                    {
                        loopStart = 0;
                        loopEnd = 0;
                    }
                }
            }
            else
            {
                /* Match document save non-RT path: keep decoder-probed frame domain
                 * and remap loops from writeWaveform domain into final decoded domain. */
                decodedFramesForRate = decodedInfo.frames ? decodedInfo.frames : writeWaveform.waveFrames;
                PV_ForceSndDecodedFrameCount(sndResource, decodedFramesForRate);
                PV_RemapLoopPointsToFrameCount(writeWaveform.waveFrames,
                                               decodedFramesForRate,
                                               &loopStart,
                                               &loopEnd);
            }
        }
        else
#endif
        {
            PV_ForceSndDecodedFrameCount(sndResource, decodedFramesForRate);
            if (compType != C_IMA4)
            {
                PV_RemapLoopPointsToFrameCount(writeWaveform.waveFrames,
                                               decodedFramesForRate,
                                               &loopStart,
                                               &loopEnd);
            }
        }

        writeWaveform.startLoop = (uint32_t)loopStart;
        writeWaveform.endLoop   = (uint32_t)loopEnd;

#if USE_OPUS_ENCODER == TRUE || USE_OPUS_DECODER == TRUE
        if (roundTripSourceRate != 0)
        {
            sndSampleRate = roundTripSourceRate;
        }
#endif

        XSetSoundBaseKey(sndResource, preservedBaseKey);
        XSetSoundSampleRate(sndResource, sndSampleRate);
        XSetSoundLoopPoints(sndResource,
                            (int32_t)writeWaveform.startLoop,
                            (int32_t)writeWaveform.endLoop);
        PV_ForceSndLoopPoints(sndResource,
                              (int32_t)writeWaveform.startLoop,
                              (int32_t)writeWaveform.endLoop);
#if USE_OPUS_ENCODER == TRUE || USE_OPUS_DECODER == TRUE
        if (roundTripSourceRate != 0)
        {
            XSetSoundOpusRoundTripFlag(sndResource, TRUE);
        }
#endif
        XSetSoundEmbeddedStatus(sndResource, TRUE);

        if (decodedInfo.pMasterPtr && decodedInfo.pMasterPtr != sndResource)
        {
            decodedOwner = decodedInfo.pMasterPtr;
        }
        if (decodedOwner)
        {
            XDisposePtr(decodedOwner);
        }
    }
    (void)decodedSampleRateForSnd; /* unused after rate-update block */

    if (oldSndPlainOwned && oldSndPlain)
    {
        XDisposePtr(oldSndPlain);
    }
    if (oldSndRawData)
    {
        XDisposePtr(oldSndRawData);
        oldSndRawData = NULL;
    }

    /* In-memory compression preview: decode the freshly encoded SND and return
     * PCM without touching the bank (avoids full-session SND rebuilds). */
    if (previewOnly)
    {
        SampleDataInfo previewInfo;
        XPTR previewPcm;
        XPTR previewOwner;
        XPTR ownedPcm;
        int32_t pcmSize;

        if (outEncodedBytes)
        {
            *outEncodedBytes = (uint32_t)XGetPtrSize(sndResource);
        }

        XSetMemory(&previewInfo, (int32_t)sizeof(previewInfo), 0);
        previewPcm = XGetSamplePtrFromSnd(sndResource, &previewInfo);
        previewOwner = NULL;
        if (previewInfo.pMasterPtr && previewInfo.pMasterPtr != sndResource)
        {
            previewOwner = previewInfo.pMasterPtr;
        }
        if (!previewPcm || previewInfo.bitSize == 0 || previewInfo.channels == 0 ||
            previewInfo.frames == 0)
        {
            if (previewOwner)
            {
                XDisposePtr(previewOwner);
            }
            XDisposePtr(sndResource);
            return BAE_BAD_FILE;
        }

        /* IMA4 decode reports ceil(frames/64)*64. Trim to the source PCM count so
         * preview/audition loops stay in the original frame domain. */
        if (compType == C_IMA4 &&
            writeWaveform.waveFrames > 0 &&
            previewInfo.frames > writeWaveform.waveFrames)
        {
            previewInfo.frames = writeWaveform.waveFrames;
        }

        pcmSize = (int32_t)(previewInfo.frames *
                            (previewInfo.bitSize / 8) *
                            previewInfo.channels);
        if (pcmSize <= 0)
        {
            if (previewOwner)
            {
                XDisposePtr(previewOwner);
            }
            XDisposePtr(sndResource);
            return BAE_BAD_FILE;
        }

        ownedPcm = XNewPtr(pcmSize);
        if (!ownedPcm)
        {
            if (previewOwner)
            {
                XDisposePtr(previewOwner);
            }
            XDisposePtr(sndResource);
            return BAE_MEMORY_ERR;
        }
        XBlockMove(previewPcm, ownedPcm, pcmSize);
        if (previewOwner)
        {
            XDisposePtr(previewOwner);
        }
        XDisposePtr(sndResource);

        *outPreviewPcm = ownedPcm;
        if (outPreviewFrames) { *outPreviewFrames = previewInfo.frames; }
        if (outPreviewBitSize) { *outPreviewBitSize = previewInfo.bitSize; }
        if (outPreviewChannels) { *outPreviewChannels = previewInfo.channels; }
        if (outPreviewRate) { *outPreviewRate = previewInfo.rate; }
        (void)sndStorageType;
        return BAE_NO_ERROR;
    }

    /* Map sndStorageType to XResourceType */
    switch (sndStorageType)
    {
        case BAE_EDITOR_SND_STORAGE_CSND: newSndType = ID_CSND; break;
        case BAE_EDITOR_SND_STORAGE_SND:  newSndType = ID_SND;  break;
        case BAE_EDITOR_SND_STORAGE_ESND:
        default:                          newSndType = ID_ESND; break;
    }

    /* Wrap the encoded SND in the target container */
    wrappedSnd = NULL;
    wrappedSndSize = 0;
    result = PV_BankRewrapSndForType(bankFile, newSndType, sndResource,
                                     XGetPtrSize(sndResource), &wrappedSnd, &wrappedSndSize);
    XDisposePtr(sndResource);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    /* Replace the SND resource in the bank using the fast SND-only path
       (handles container type change if oldSndType != newSndType) */
    result = PV_BankReplaceSndResourceInPlace(bankFile, oldSndType, newSndType,
                                              (XShortResourceID)pSampleInfo->sndResourceID,
                                              sndName, wrappedSnd, wrappedSndSize);
    XDisposePtr(wrappedSnd);
    return result;
}


BAEResult BAERmfEditorBank_ReEncodeSample(BAEBankToken bankToken,
                                           uint32_t instrumentIndex,
                                           uint32_t sampleIndex,
                                           BAERmfEditorCompressionType compressionType,
                                           BAERmfEditorSndStorageType sndStorageType,
                                           BAERmfEditorOpusMode opusMode)
{
    XFILE bankFile;
    BAERmfEditorBankSampleInfo sampleInfo;
    void *waveData;
    uint32_t frameCount;
    uint16_t bitSize;
    uint16_t channels;
    BAE_UNSIGNED_FIXED sampleRate;
    BAEResult result;

    if (!bankToken)
    {
        return BAE_PARAM_ERR;
    }
    if (compressionType == BAE_EDITOR_COMPRESSION_DONT_CHANGE)
    {
        return BAE_NO_ERROR;
    }

    bankFile = (XFILE)bankToken;

    /* Decode the sample to PCM */
    waveData = NULL;
    frameCount = 0;
    bitSize = 0;
    channels = 0;
    sampleRate = 0;
    result = BAERmfEditorBank_GetSampleWaveformData(bankToken, instrumentIndex, sampleIndex,
                                                    &waveData, &frameCount, &bitSize, &channels, &sampleRate);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }
    if (!waveData || frameCount == 0 || bitSize == 0 || channels == 0)
    {
        if (waveData) { XDisposePtr(waveData); }
        return BAE_BAD_FILE;
    }

    /* Get sample metadata (loop points, root key, current container type) */
    result = BAERmfEditorBank_GetInstrumentSampleInfo(bankToken, instrumentIndex, sampleIndex, &sampleInfo);
    if (result != BAE_NO_ERROR)
    {
        XDisposePtr(waveData);
        return result;
    }

    result = PV_BankReEncodeSampleCore(bankFile, &sampleInfo,
                                       waveData, frameCount, bitSize, channels, sampleRate,
                                       compressionType, sndStorageType, opusMode, FALSE,
                                       NULL, NULL, NULL, NULL, NULL, NULL);
    XDisposePtr(waveData);
    return result;
}


BAEResult BAERmfEditorBank_ReEncodeSampleFromPCM(BAEBankToken bankToken,
                                                  uint32_t instrumentIndex,
                                                  uint32_t sampleIndex,
                                                  BAERmfEditorCompressionType compressionType,
                                                  BAERmfEditorSndStorageType sndStorageType,
                                                  BAERmfEditorOpusMode opusMode,
                                                  const void *sourcePcm,
                                                  uint32_t frameCount,
                                                  uint16_t bitSize,
                                                  uint16_t channels,
                                                  BAE_UNSIGNED_FIXED sampleRate)
{
        return BAERmfEditorBank_ReEncodeSampleFromPCMEx(bankToken,
                                                                                                         instrumentIndex,
                                                                                                         sampleIndex,
                                                                                                         compressionType,
                                                                                                         sndStorageType,
                                                                                                         opusMode,
                                                                                                         FALSE,
                                                                                                         sourcePcm,
                                                                                                         frameCount,
                                                                                                         bitSize,
                                                                                                         channels,
                                                                                                         sampleRate);
}


BAEResult BAERmfEditorBank_ReEncodeSampleFromPCMEx(BAEBankToken bankToken,
                                                                                                        uint32_t instrumentIndex,
                                                                                                        uint32_t sampleIndex,
                                                                                                        BAERmfEditorCompressionType compressionType,
                                                                                                        BAERmfEditorSndStorageType sndStorageType,
                                                                                                        BAERmfEditorOpusMode opusMode,
                                                                                                        bool opusRoundTripResample,
                                                                                                        const void *sourcePcm,
                                                                                                        uint32_t frameCount,
                                                                                                        uint16_t bitSize,
                                                                                                        uint16_t channels,
                                                                                                        BAE_UNSIGNED_FIXED sampleRate)
{
    XFILE bankFile;
    BAERmfEditorBankSampleInfo sampleInfo;
    BAEResult result;
    void *pcmCopy;
    int32_t pcmBytes;

    if (!bankToken || !sourcePcm || frameCount == 0 || bitSize == 0 || channels == 0)
    {
        return BAE_PARAM_ERR;
    }
    if (compressionType == BAE_EDITOR_COMPRESSION_DONT_CHANGE)
    {
        return BAE_NO_ERROR;
    }

    bankFile = (XFILE)bankToken;

    result = BAERmfEditorBank_GetInstrumentSampleInfo(bankToken, instrumentIndex, sampleIndex, &sampleInfo);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    /* Make a writable copy of the caller's PCM so PV_BankReEncodeSampleCore can pass it to
       XCreateSoundObjectFromData (which may modify the buffer in place for some codecs). */
    pcmBytes = (int32_t)(frameCount * (uint32_t)(bitSize / 8u) * (uint32_t)channels);
    pcmCopy = XNewPtr(pcmBytes);
    if (!pcmCopy)
    {
        return BAE_MEMORY_ERR;
    }
    XBlockMove((XPTR)(uintptr_t)sourcePcm, pcmCopy, pcmBytes);

    result = PV_BankReEncodeSampleCore(bankFile, &sampleInfo,
                                       pcmCopy, frameCount, bitSize, channels, sampleRate,
                                       compressionType, sndStorageType, opusMode,
                                       opusRoundTripResample,
                                       NULL, NULL, NULL, NULL, NULL, NULL);
    XDisposePtr(pcmCopy);
    return result;
}


BAEResult BAERmfEditorBank_ReEncodeSampleFromMutablePCMEx(BAEBankToken bankToken,
                                                           uint32_t instrumentIndex,
                                                           uint32_t sampleIndex,
                                                           BAERmfEditorCompressionType compressionType,
                                                           BAERmfEditorSndStorageType sndStorageType,
                                                           BAERmfEditorOpusMode opusMode,
                                                           bool opusRoundTripResample,
                                                           void *mutablePcm,
                                                           uint32_t frameCount,
                                                           uint16_t bitSize,
                                                           uint16_t channels,
                                                           BAE_UNSIGNED_FIXED sampleRate)
{
    XFILE bankFile;
    BAERmfEditorBankSampleInfo sampleInfo;
    BAEResult result;

    if (!bankToken || !mutablePcm || frameCount == 0 || bitSize == 0 || channels == 0)
    {
        return BAE_PARAM_ERR;
    }
    if (compressionType == BAE_EDITOR_COMPRESSION_DONT_CHANGE)
    {
        return BAE_NO_ERROR;
    }

    bankFile = (XFILE)bankToken;

    result = BAERmfEditorBank_GetInstrumentSampleInfo(bankToken, instrumentIndex, sampleIndex, &sampleInfo);
    if (result != BAE_NO_ERROR)
    {
        return result;
    }

    return PV_BankReEncodeSampleCore(bankFile, &sampleInfo,
                                     mutablePcm, frameCount, bitSize, channels, sampleRate,
                                     compressionType, sndStorageType, opusMode,
                                     opusRoundTripResample,
                                     NULL, NULL, NULL, NULL, NULL, NULL);
}

