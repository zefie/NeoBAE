/*
 * GenXMF.c
 *
 * Minimal XMF/MXMF loader: extract embedded SMF and optional bank (SF2/DLS)
 */

#include "MiniBAE.h"
#include "X_API.h"
#include "BAE_API.h"
#include "GenXMF.h"
#include <string.h>
// zlib for MXMF packed content
#if USE_XMF_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
#include <zlib.h>
#endif
#include "X_Assert.h" // BAE_PRINTF
// LZSS API (declared in NewNewLZSS.c)
void LZSSUncompress(unsigned char* src, uint32_t srcBytes,
                    unsigned char* dst, uint32_t dstBytes);

#if USE_XMF_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
#include "GenSF2_FluidSynth.h"
#endif

// Control verbose inflate failure logging during MXMF packed scans
#ifndef MXMF_LOG_INFLATE_FAILURES
#define MXMF_LOG_INFLATE_FAILURES 0
#endif

// Local helpers copied from MiniBAE.c XMF section
static int PV_FindSignature(const unsigned char *buf, uint32_t len, const char sig[4])
{
    for (uint32_t i = 0; i + 4 <= len; ++i)
    {
        if (buf[i] == (unsigned char)sig[0] && buf[i+1] == (unsigned char)sig[1] && buf[i+2] == (unsigned char)sig[2] && buf[i+3] == (unsigned char)sig[3])
        {
            return (int)i;
        }
    }
    return -1;
}

// Find an arbitrary byte pattern inside a buffer
static int PV_FindBytes(const unsigned char *buf, uint32_t len, const char *pat, uint32_t patLen)
{
    if (!buf || !pat || patLen == 0 || len < patLen)
        return -1;
    for (uint32_t i = 0; i + patLen <= len; ++i)
    {
        if (buf[i] == (unsigned char)pat[0])
        {
            uint32_t j = 1;
            while (j < patLen && buf[i + j] == (unsigned char)pat[j])
                ++j;
            if (j == patLen)
                return (int)i;
        }
    }
    return -1;
}

// Quick zlib header validation to reduce false-positive inflate attempts
static XBOOL PV_IsLikelyZlibHeader(const unsigned char *buf, uint32_t len, uint32_t offset)
{
    if (!buf || offset + 2 > len) return FALSE;
    unsigned cmf = buf[offset];
    unsigned flg = buf[offset + 1];
    // CMF lower 4 bits must be 8 (deflate)
    if ((cmf & 0x0F) != 8) return FALSE;
    // Check bits: (CMF*256 + FLG) mod 31 == 0
    unsigned hdr = (cmf << 8) | flg;
    if ((hdr % 31) != 0) return FALSE;
    return TRUE;
}

// If region holds a RIFF 'RMID' container, locate 'data' chunk and return SMF bytes
static XBOOL PV_ExtractRMIDToSMF(const unsigned char *buf, uint32_t len, const unsigned char **outSmf, uint32_t *outSmfLen)
{
    if (!buf || len < 12) return FALSE;
    if (!(buf[0]=='R'&&buf[1]=='I'&&buf[2]=='F'&&buf[3]=='F')) return FALSE;
    uint32_t riffSize = (uint32_t)buf[4] | ((uint32_t)buf[5]<<8) | ((uint32_t)buf[6]<<16) | ((uint32_t)buf[7]<<24);
    if (riffSize+8 > len) return FALSE;
    if (!(buf[8]=='R'&&buf[9]=='M'&&buf[10]=='I'&&buf[11]=='D')) return FALSE;
    uint32_t i = 12;
    while (i + 8 <= len)
    {
        const unsigned char *ch = buf + i;
        uint32_t csize = (uint32_t)ch[4] | ((uint32_t)ch[5]<<8) | ((uint32_t)ch[6]<<16) | ((uint32_t)ch[7]<<24);
        if (i + 8 + csize > len) break;
        if (ch[0]=='d'&&ch[1]=='a'&&ch[2]=='t'&&ch[3]=='a')
        {
            if (outSmf) *outSmf = ch + 8;
            if (outSmfLen) *outSmfLen = csize;
            return TRUE;
        }
        // chunks are word-aligned
        i += 8 + csize;
        if (i & 1) i++;
    }
    return FALSE;
}

static XPTR PV_GetFileAsData(XFILENAME *pFile, int32_t *pSize)
{
    XPTR data;
    if (XGetFileAsData(pFile, &data, pSize))
    {
        data = NULL;
    }
    return data;
}

// Forward declaration used by packed extractor
// Returns TRUE if a bank was successfully loaded from the blob
static XBOOL PV_TryLoadBankFromBlob(const unsigned char *buf, uint32_t len);

// Forward prototypes for inflate helpers (defined later)
static XBOOL PV_InflateFromOffset(const unsigned char *buf, uint32_t len, uint32_t offset,
                                  unsigned char **outBuf, uint32_t *outLen);
static XBOOL PV_InflateRawFromOffset(const unsigned char *buf, uint32_t len, uint32_t offset,
                                     unsigned char **outBuf, uint32_t *outLen);

// Forward prototype for LZSS probe (defined later)
static XBOOL PV_ProbeLZSS(const unsigned char *bytes, uint32_t ulen, uint32_t offset,
                          const unsigned char **outMidi, uint32_t *outMidiLen,
                          const unsigned char **outRmf, uint32_t *outRmfLen);

// -------------------- XMF v1 (1.00) minimal parser --------------------

// Read a 7-bit VLQ; updates *pos, returns TRUE on success
static XBOOL PV_ReadVLQ(const unsigned char *buf, uint32_t len, uint32_t *pos, uint32_t *out)
{
    if (!buf || !pos || !out) return FALSE;
    uint32_t p = *pos;
    uint32_t v = 0;
    int bytes = 0;
    while (p < len && bytes < 5)
    {
        unsigned char c = buf[p++];
        v = (v << 7) | (uint32_t)(c & 0x7F);
        bytes++;
        if ((c & 0x80) == 0)
            break;
    }
    if (bytes == 0) return FALSE;
    *pos = p;
    *out = v;
    return TRUE;
}

// Read a VLQ within a bounded slice [start,end)
static XBOOL PV_ReadVLQSlice(const unsigned char *buf, uint32_t start, uint32_t end, uint32_t *pos, uint32_t *out)
{
    if (!buf || !pos || !out || *pos < start || *pos >= end) return FALSE;
    uint32_t p = *pos;
    uint32_t v = 0; int bytes = 0;
    while (p < end && bytes < 5)
    {
        unsigned char c = buf[p++];
        v = (v << 7) | (uint32_t)(c & 0x7F);
        bytes++;
        if ((c & 0x80) == 0) break;
    }
    if (bytes == 0) return FALSE;
    *pos = p;
    *out = v;
    return TRUE;
}

// Parse node metadata to extract resourceFormat (formatType, formatId); -1 if unknown
static void PV_ParseXMF1Metadata(const unsigned char *bytes, uint32_t len, uint32_t metaStart, uint32_t metaLen,
                                 int *outFmtType, int *outFmtId)
{
    if (outFmtType) { *outFmtType = -1; }
    if (outFmtId) { *outFmtId = -1; }
    if (!bytes || metaLen == 0) return;
    uint32_t p = metaStart;
    uint32_t end = metaStart + metaLen;
    if (end > len) return;
    while (p < end)
    {
        if (bytes[p] == 0)
        {
            // Typed field
            p++;
            uint32_t typeId = 0;
            if (!PV_ReadVLQSlice(bytes, metaStart, end, &p, &typeId)) break;
            uint32_t numVersions = 0;
            if (!PV_ReadVLQSlice(bytes, metaStart, end, &p, &numVersions)) break;
            if (numVersions == 0)
            {
                uint32_t dataLen = 0;
                if (!PV_ReadVLQSlice(bytes, metaStart, end, &p, &dataLen)) break;
                if (p + dataLen > end) break;
                if (dataLen >= 1 && typeId == 3) // resourceFormat
                {
                    uint32_t q = p;
                    unsigned char fmt = bytes[q++];
                    if (fmt >= 4) // binary
                    {
                        int ftype = -1, fid = -1;
                        if (PV_ReadVLQSlice(bytes, p, p + dataLen, &q, (uint32_t *)&ftype) &&
                            PV_ReadVLQSlice(bytes, p, p + dataLen, &q, (uint32_t *)&fid))
                        {
                            if (outFmtType) *outFmtType = ftype;
                            if (outFmtId) *outFmtId = fid;
                        }
                    }
                }
                p += dataLen;
            }
            else
            {
                // Internationalized metadata not handled; bail out of metadata parse
                break;
            }
        }
        else
        {
            // Custom key: VLQ length + string
            uint32_t keyLen = 0;
            if (!PV_ReadVLQSlice(bytes, metaStart, end, &p, &keyLen)) break;
            if (p + keyLen > end) break;
            p += keyLen;
            uint32_t numVersions = 0;
            if (!PV_ReadVLQSlice(bytes, metaStart, end, &p, &numVersions)) break;
            if (numVersions == 0)
            {
                uint32_t dataLen = 0;
                if (!PV_ReadVLQSlice(bytes, metaStart, end, &p, &dataLen)) break;
                if (p + dataLen > end) break;
                // Skip contents
                p += dataLen;
            }
            else
            {
                break;
            }
        }
    }
}

// Parse a node recursively, extracting inline resources (SMF/RMF/RMID) and trying to load SF2/DLS banks
static XBOOL PV_ParseXMF1Node(const unsigned char *bytes, uint32_t len, uint32_t *pos,
                              const unsigned char **outMidi, uint32_t *outMidiLen,
                              const unsigned char **outRmf, uint32_t *outRmfLen,
                              XBOOL *bankLoaded)
{
    if (!bytes || !pos || *pos >= len) return FALSE;

    uint32_t start = *pos;
    uint32_t nodeLen = 0, itemCount = 0, headerLen = 0;
    if (!PV_ReadVLQ(bytes, len, pos, &nodeLen)) return FALSE;
    if (!PV_ReadVLQ(bytes, len, pos, &itemCount)) return FALSE;
    if (!PV_ReadVLQ(bytes, len, pos, &headerLen)) return FALSE;
    if (nodeLen == 0) return FALSE;
    uint32_t nodeEnd = start + nodeLen;
    if (nodeEnd > len) return FALSE;
    BAE_PRINTF("[XMF1] node@%u len=%u items=%u headerLen=%u\n", start, nodeLen, itemCount, headerLen);

    // Header: metadataLen + metadata, unpackersLen + unpackers
    uint32_t headerStart = *pos;
    uint32_t headerEnd = headerStart + headerLen;
    if (headerEnd > len) return FALSE;
    uint32_t metadataLen = 0; uint32_t metaStart = 0;
    int rfType = -1, rfId = -1;
    // Metadata LEN (if any bytes remain in header)
    if (*pos < headerEnd)
    {
        if (!PV_ReadVLQSlice(bytes, headerStart, headerEnd, pos, &metadataLen)) metadataLen = 0;
        if (metadataLen > 0 && *pos + metadataLen <= headerEnd)
        {
            metaStart = *pos;
            PV_ParseXMF1Metadata(bytes, len, metaStart, metadataLen, &rfType, &rfId);
            *pos += metadataLen;
        }
        else
        {
            // No metadata or malformed; jump to header end
            *pos = headerEnd;
        }
    }
    // Unpackers LEN (if any header bytes remain)
    uint32_t unpackersLen = 0;
    if (*pos < headerEnd)
    {
        if (!PV_ReadVLQSlice(bytes, headerStart, headerEnd, pos, &unpackersLen)) unpackersLen = 0;
        if (*pos + unpackersLen <= headerEnd)
        {
            *pos += unpackersLen;
        }
        // else malformed; fall through to headerEnd
    }
    // Land exactly at end of header
    *pos = headerEnd;
    // We don't parse unpackers in detail; presence means packed content
    XBOOL isPacked = (unpackersLen > 0) ? TRUE : FALSE;
    BAE_PRINTF("[XMF1] header %u..%u metaLen=%u unpackersLen=%u isPacked=%d rfTypeHint=%d rfIdHint=%d\n",
               headerStart, headerEnd, metadataLen, unpackersLen, (int)isPacked, rfType, rfId);

    if (itemCount == 0)
    {
        // File node
        // Reference Type ID (VLQ):
        // 1 = inLineResource (content follows)
        // 2 = inFileResource (content elsewhere in file: [offsetVLQ][lengthVLQ])
        // 3 = inFileNode (another node at [offsetVLQ])
        uint32_t refType = 0;
        if (!PV_ReadVLQ(bytes, len, pos, &refType)) return FALSE;
        if (refType == 0) refType = 1; // Some files omit and imply inline; be permissive
        BAE_PRINTF("[XMF1] refType=%u\n", refType);
        const unsigned char *content = NULL;
        uint32_t contentLen = 0;
        if (refType == 1)
        {
            // inLineResource: content from current pos to nodeEnd
            if (*pos > nodeEnd) return FALSE;
            content = bytes + *pos;
            contentLen = nodeEnd - *pos;
            BAE_PRINTF("[XMF1] file: inline contentLen=%u rfType=%d rfId=%d first4=%02X %02X %02X %02X\n",
                       contentLen, rfType, rfId,
                       (contentLen>=1?content[0]:0),(contentLen>=2?content[1]:0),(contentLen>=3?content[2]:0),(contentLen>=4?content[3]:0));
        }
        else if (refType == 2)
        {
            // inFileResource: next two VLQs are [offset][length]
            uint32_t off = 0, blen = 0;
            if (!PV_ReadVLQ(bytes, len, pos, &off)) return FALSE;
            if (!PV_ReadVLQ(bytes, len, pos, &blen)) return FALSE;
            if (off > len || blen > len || off + blen > len) return FALSE;
            content = bytes + off;
            contentLen = blen;
            BAE_PRINTF("[XMF1] file: inFileResource off=%u len=%u rfType=%d rfId=%d first4=%02X %02X %02X %02X\n",
                       off, blen, rfType, rfId,
                       (blen>=1?content[0]:0),(blen>=2?content[1]:0),(blen>=3?content[2]:0),(blen>=4?content[3]:0));
        }
        else if (refType == 3)
        {
            // inFileNode: next VLQ is [offset] of another node – recurse there
            uint32_t nOff = 0;
            if (!PV_ReadVLQ(bytes, len, pos, &nOff)) return FALSE;
            if (nOff >= len) return FALSE;
            uint32_t cpos = nOff;
            BAE_PRINTF("[XMF1] file: inFileNode -> recurse at off=%u\n", nOff);
            XBOOL ok = PV_ParseXMF1Node(bytes, len, &cpos, outMidi, outMidiLen, outRmf, outRmfLen, bankLoaded);
            // Move to end of current node content regardless
            *pos = nodeEnd;
            return ok;
        }
        else
        {
            // Unsupported ref types (external). Skip node.
            *pos = nodeEnd;
            return TRUE;
        }

        // If packed, try to inflate content (zlib/gzip first, then raw)
        const unsigned char *payload = content;
        uint32_t payloadLen = contentLen;
        unsigned char *inflated = NULL; uint32_t inflatedLen = 0;
        if (isPacked && contentLen >= 4)
        {
            BAE_PRINTF("[XMF1] content marked packed; trying inflate...\n");
            if (!PV_InflateFromOffset(content, contentLen, 0, &inflated, &inflatedLen))
            {
                (void)PV_InflateRawFromOffset(content, contentLen, 0, &inflated, &inflatedLen);
            }
            // Some XMF v1 packers prepend 2 bytes before deflate stream
            if (!inflated)
            {
                if (!PV_InflateFromOffset(content, contentLen, 2, &inflated, &inflatedLen))
                {
                    (void)PV_InflateRawFromOffset(content, contentLen, 2, &inflated, &inflatedLen);
                }
            }
            if (inflated)
            {
                payload = inflated; payloadLen = inflatedLen;
                BAE_PRINTF("[XMF1] inflate -> %u bytes\n", payloadLen);
            }
            else
            {
                // Try decrypt+inflate as last resort
                unsigned char *cpy = (unsigned char *)XNewPtr(contentLen);
                if (cpy)
                {
                    XBlockMove(content, cpy, contentLen);
                    XDecryptData(cpy, contentLen);
                    if (PV_InflateFromOffset(cpy, contentLen, 0, &inflated, &inflatedLen) ||
                        PV_InflateRawFromOffset(cpy, contentLen, 0, &inflated, &inflatedLen) ||
                        PV_InflateFromOffset(cpy, contentLen, 2, &inflated, &inflatedLen) ||
                        PV_InflateRawFromOffset(cpy, contentLen, 2, &inflated, &inflatedLen))
                    {
                        payload = inflated; payloadLen = inflatedLen;
                        BAE_PRINTF("[XMF1] decrypt+inflate -> %u bytes\n", payloadLen);
                    }
                    XDisposePtr(cpy);
                }
            }
        }
        else if (contentLen >= 4)
        {
            // Even if not marked packed, try to inflate at common offsets just in case
            if (!inflated)
            {
                (void)PV_InflateFromOffset(content, contentLen, 0, &inflated, &inflatedLen);
                if (!inflated) (void)PV_InflateRawFromOffset(content, contentLen, 0, &inflated, &inflatedLen);
                if (!inflated) (void)PV_InflateFromOffset(content, contentLen, 2, &inflated, &inflatedLen);
                if (!inflated) (void)PV_InflateRawFromOffset(content, contentLen, 2, &inflated, &inflatedLen);
                if (inflated) { payload = inflated; payloadLen = inflatedLen; }
            }
        }
        // If still opaque, try LZSS probe on payload
        if (payload == content)
        {
            const unsigned char *pm=NULL; uint32_t pmLen=0; const unsigned char *pr=NULL; uint32_t prLen=0;
            if (PV_ProbeLZSS(content, contentLen, 0, &pm, &pmLen, &pr, &prLen))
            {
                if (pm && pmLen && outMidi && outMidiLen && !*outMidi)
                {
                    *outMidi = pm; *outMidiLen = pmLen; // take ownership
                }
                else if (pr && prLen && outRmf && outRmfLen && !*outRmf)
                {
                    *outRmf = pr; *outRmfLen = prLen; // take ownership
                }
                else
                {
                    if (pm) { XDisposePtr((XPTR)pm); }
                    if (pr) { XDisposePtr((XPTR)pr); }
                }
            }
        }

        // Try to load a bank first if we don't have one yet
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
        if (bankLoaded && *bankLoaded == FALSE)
        {
            if (PV_TryLoadBankFromBlob(payload, payloadLen) == TRUE)
            {
                *bankLoaded = TRUE;
            }
        }
#endif

        // Try based on resourceFormat when available, else heuristic
        const unsigned char *smfRMID=NULL; uint32_t smfRMIDLen=0;
    XBOOL preferBank = FALSE;
        if (rfType == 0) { // standard
            if (rfId == 2 || rfId == 3 || rfId == 4 || rfId == 5) preferBank = TRUE; // DLS
        }

        if (preferBank)
        {
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
            if (bankLoaded && *bankLoaded == FALSE)
            {
                if (PV_TryLoadBankFromBlob(payload, payloadLen) == TRUE) { *bankLoaded = TRUE; }
            }
#endif
        }

        if (PV_ExtractRMIDToSMF(payload, payloadLen, &smfRMID, &smfRMIDLen))
        {
            if (outMidi && outMidiLen && !*outMidi)
            {
                unsigned char *cpy = (unsigned char *)XNewPtr(smfRMIDLen);
                if (cpy) { XBlockMove(smfRMID, cpy, smfRMIDLen); *outMidi = cpy; *outMidiLen = smfRMIDLen; }
            }
        }
        else
        {
            int moff = PV_FindSignature(payload, payloadLen, "MThd");
            if (moff >= 0 && outMidi && outMidiLen && !*outMidi)
            {
                uint32_t copyLen = payloadLen - (uint32_t)moff;
                unsigned char *cpy = (unsigned char *)XNewPtr(copyLen);
                if (cpy) { XBlockMove(payload + moff, cpy, copyLen); *outMidi = cpy; *outMidiLen = copyLen; }
            }
            else
            {
                int roff = PV_FindSignature(payload, payloadLen, "IREZ");
                if (roff >= 0 && outRmf && outRmfLen && !*outRmf)
                {
                    uint32_t copyLen = payloadLen - (uint32_t)roff;
                    unsigned char *cpy = (unsigned char *)XNewPtr(copyLen);
                    if (cpy) { XBlockMove(payload + roff, cpy, copyLen); *outRmf = cpy; *outRmfLen = copyLen; }
                }
            }
        }

        if (inflated) { XDisposePtr(inflated); }
        *pos = nodeEnd;
        return TRUE;
    }
    else
    {
        // Folder node: recursively parse children in content area until nodeEnd
        while (*pos < nodeEnd)
        {
            if (!PV_ParseXMF1Node(bytes, len, pos, outMidi, outMidiLen, outRmf, outRmfLen, bankLoaded))
                break;
            // Early out if we have song and bank
            if ((outMidi && *outMidi) || (outRmf && *outRmf))
            {
                if (!bankLoaded || (bankLoaded && *bankLoaded == TRUE))
                    break;
            }
        }
        // Ensure position ends at nodeEnd
        if (*pos < nodeEnd) *pos = nodeEnd;
        return TRUE;
    }
}

// Entry for XMF_1.00 parsing: returns TRUE if any content was found
static XBOOL PV_TryParseXMF1(const unsigned char *bytes, uint32_t len,
                             const unsigned char **outMidi, uint32_t *outMidiLen,
                             const unsigned char **outRmf, uint32_t *outRmfLen,
                             XBOOL *bankLoaded)
{
    if (outMidi) { *outMidi = NULL; }
    if (outMidiLen) { *outMidiLen = 0; }
    if (outRmf) { *outRmf = NULL; }
    if (outRmfLen) { *outRmfLen = 0; }
    if (bankLoaded) { *bankLoaded = FALSE; }
    if (!bytes || len < 8) return FALSE;
    if (memcmp(bytes, "XMF_1.00", 8) != 0) return FALSE;

    uint32_t pos = 8;
    uint32_t fileLen = 0, metaTableLen = 0, rootOffset = 0;
    if (!PV_ReadVLQ(bytes, len, &pos, &fileLen)) return FALSE;
    if (!PV_ReadVLQ(bytes, len, &pos, &metaTableLen)) return FALSE;
    if (!PV_ReadVLQ(bytes, len, &pos, &rootOffset)) return FALSE;
    if (rootOffset >= len) return FALSE;

    pos = rootOffset;
    BAE_PRINTF("[XMF] Parsing XMF_1.00, root node @%u, fileLen(VLQ)=%u, metaTableLen=%u\n", rootOffset, fileLen, metaTableLen);
    XBOOL ok = PV_ParseXMF1Node(bytes, len, &pos, outMidi, outMidiLen, outRmf, outRmfLen, bankLoaded);
    return ok && ((outMidi && *outMidi) || (outRmf && *outRmf));
}

// Try to inflate a zlib stream starting at a given offset. Returns newly allocated buffer on success.
static XBOOL PV_InflateFromOffset(const unsigned char *buf, uint32_t len, uint32_t offset,
                                  unsigned char **outBuf, uint32_t *outLen)
{
#if USE_XMF_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
    if (!buf || offset >= len || !outBuf || !outLen) return FALSE;
    // Quick header sanity: accept zlib (0x78 ..), or gzip (0x1f 0x8b)
    if (offset + 2 > len) return FALSE;
    unsigned char b0 = buf[offset];
    unsigned char b1 = buf[offset+1];
    XBOOL is_zlib = (b0 == 0x78);
    XBOOL is_gzip = (b0 == 0x1f && b1 == 0x8b);
    if (!is_zlib && !is_gzip) return FALSE;

    // Inflate using streaming API so we don't need exact output size.
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    // 15+32 enables zlib or gzip auto header detection
    if (inflateInit2(&zs, 15 + 32) != Z_OK)
        return FALSE;

    const uint32_t kChunk = 64 * 1024;
    uint32_t cap = kChunk;
    unsigned char *dst = (unsigned char *)XNewPtr(cap);
    if (!dst)
    {
        inflateEnd(&zs);
        return FALSE;
    }

    zs.next_in = (Bytef *)(buf + offset);
    zs.avail_in = (uInt)(len - offset);
    zs.next_out = (Bytef *)dst;
    zs.avail_out = (uInt)cap;

    XBOOL ok = FALSE;
    int zret;
    do {
    zret = inflate(&zs, Z_NO_FLUSH);
        if (zret == Z_STREAM_END)
        {
            ok = TRUE;
            break;
        }
        else if (zret == Z_OK)
        {
            // Need more output space
            if (zs.avail_out == 0)
            {
                uint32_t used = (uint32_t)((char *)zs.next_out - (char *)dst);
                uint32_t ncap = cap + kChunk;
                unsigned char *ndst = (unsigned char *)XNewPtr(ncap);
                if (!ndst)
                {
                    ok = FALSE;
                    break;
                }
                XBlockMove(dst, ndst, used);
                XDisposePtr(dst);
                dst = ndst;
                cap = ncap;
                zs.next_out = (Bytef *)(dst + used);
                zs.avail_out = (uInt)(cap - used);
            }
            // else keep inflating
        }
    else
    {
#if MXMF_LOG_INFLATE_FAILURES
        BAE_PRINTF("[MXMF] inflate failed at input offset=%u (zret=%d)\n", offset, zret);
#endif
        ok = FALSE;
        break;
    }
    } while (1);

    if (ok)
    {
        uint32_t used = (uint32_t)((char *)zs.next_out - (char *)dst);
        *outBuf = dst;
        *outLen = used;
    }
    else
    {
        XDisposePtr(dst);
        dst = NULL;
    }
    inflateEnd(&zs);
    if (ok) {
        BAE_PRINTF("[MXMF] inflated stream at offset=%u -> %u bytes\n", offset, *outLen);
    }
    return ok;
#else
    (void)buf; (void)len; (void)offset; (void)outBuf; (void)outLen;
    return FALSE;
#endif
}

// Try to inflate a raw DEFLATE stream (no zlib/gzip header) starting at offset.
static XBOOL PV_InflateRawFromOffset(const unsigned char *buf, uint32_t len, uint32_t offset,
                                     unsigned char **outBuf, uint32_t *outLen)
{
#if USE_XMF_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
    if (!buf || offset >= len || !outBuf || !outLen) return FALSE;
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) // raw deflate
        return FALSE;

    const uint32_t kChunk = 64 * 1024;
    uint32_t cap = kChunk;
    unsigned char *dst = (unsigned char *)XNewPtr(cap);
    if (!dst)
    {
        inflateEnd(&zs);
        return FALSE;
    }

    zs.next_in = (Bytef *)(buf + offset);
    zs.avail_in = (uInt)(len - offset);
    zs.next_out = (Bytef *)dst;
    zs.avail_out = (uInt)cap;

    XBOOL ok = FALSE;
    int zret;
    do {
        zret = inflate(&zs, Z_NO_FLUSH);
        if (zret == Z_STREAM_END)
        {
            ok = TRUE;
            break;
        }
        else if (zret == Z_OK)
        {
            if (zs.avail_out == 0)
            {
                uint32_t used = (uint32_t)((char *)zs.next_out - (char *)dst);
                uint32_t ncap = cap + kChunk;
                unsigned char *ndst = (unsigned char *)XNewPtr(ncap);
                if (!ndst)
                {
                    ok = FALSE;
                    break;
                }
                XBlockMove(dst, ndst, used);
                XDisposePtr(dst);
                dst = ndst;
                cap = ncap;
                zs.next_out = (Bytef *)(dst + used);
                zs.avail_out = (uInt)(cap - used);
            }
        }
    else
    {
#if MXMF_LOG_INFLATE_FAILURES
        BAE_PRINTF("[MXMF] inflate RAW failed at input offset=%u (zret=%d)\n", offset, zret);
#endif
        ok = FALSE;
        break;
    }
    } while (1);

    if (ok)
    {
        uint32_t used = (uint32_t)((char *)zs.next_out - (char *)dst);
        *outBuf = dst;
        *outLen = used;
    }
    else
    {
        XDisposePtr(dst);
        dst = NULL;
    }
    inflateEnd(&zs);
    if (ok) {
        BAE_PRINTF("[MXMF] inflated RAW stream at offset=%u -> %u bytes\n", offset, *outLen);
    }
    return ok;
#else
    (void)buf; (void)len; (void)offset; (void)outBuf; (void)outLen;
    return FALSE;
#endif
}

// Compute SMF total length from an in-memory buffer starting at 'MThd'
static uint32_t PV_ComputeSMFLen(const unsigned char *p, uint32_t len)
{
    if (!p || len < 14) return 0;
    if (!(p[0]=='M'&&p[1]=='T'&&p[2]=='h'&&p[3]=='d')) return 0;
    uint32_t hdrLen = (p[4]<<24)|(p[5]<<16)|(p[6]<<8)|p[7];
    if (hdrLen != 6 || len < 14) return 0;
    uint16_t ntrks = ((uint16_t)p[10]<<8)|p[11];
    uint32_t pos = 8 + 6; // after header content
    uint32_t need = pos;
    for (uint16_t t=0; t<ntrks; ++t)
    {
        if (need + 8 > len) return 0;
        const unsigned char *trk = p + need;
        if (!(trk[0]=='M'&&trk[1]=='T'&&trk[2]=='r'&&trk[3]=='k')) return 0;
        uint32_t tlen = (trk[4]<<24)|(trk[5]<<16)|(trk[6]<<8)|trk[7];
        need += 8 + tlen;
        if (need > len) return 0;
    }
    return need;
}

// Try LZSS decompress at offset and extract SMF/RMF
static XBOOL PV_ProbeLZSS(const unsigned char *bytes, uint32_t ulen, uint32_t offset,
                          const unsigned char **outMidi, uint32_t *outMidiLen,
                          const unsigned char **outRmf, uint32_t *outRmfLen)
{
    if (outMidi) { *outMidi = NULL; }
    if (outMidiLen) { *outMidiLen = 0; }
    if (outRmf) { *outRmf = NULL; }
    if (outRmfLen) { *outRmfLen = 0; }
    if (!bytes || offset >= ulen) return FALSE;
    uint32_t inLen = ulen - offset;
    // Cap output to 8MB or 8x input, whichever smaller
    uint32_t cap = inLen * 8u;
    if (cap < 256*1024u) cap = 256*1024u;
    if (cap > (8u<<20)) cap = (8u<<20);
    unsigned char *dst = (unsigned char *)XNewPtr(cap);
    if (!dst) return FALSE;
    // Decompress; LZSSUncompress doesn't report size; we scan the buffer
    LZSSUncompress((unsigned char *)(bytes + offset), inLen, dst, cap);
    // Try RMID first
    const unsigned char *rmidSmf=NULL; uint32_t rmidLen=0;
    if (PV_ExtractRMIDToSMF(dst, cap, &rmidSmf, &rmidLen))
    {
        unsigned char *cpy = (unsigned char *)XNewPtr(rmidLen);
        if (cpy) { XBlockMove(rmidSmf, cpy, rmidLen); if (outMidi) *outMidi = cpy; if (outMidiLen) *outMidiLen = rmidLen; }
        XDisposePtr(dst);
        return TRUE;
    }
    // Try SMF
    int off = PV_FindSignature(dst, cap, "MThd");
    if (off >= 0)
    {
        uint32_t need = PV_ComputeSMFLen(dst + off, cap - (uint32_t)off);
        if (need == 0) need = cap - (uint32_t)off;
        unsigned char *cpy = (unsigned char *)XNewPtr(need);
        if (cpy)
        {
            XBlockMove(dst + off, cpy, need);
            if (outMidi) { *outMidi = cpy; }
            if (outMidiLen) { *outMidiLen = need; }
            XDisposePtr(dst);
            return TRUE;
        }
    }
    // Try RMF
    int roff = PV_FindSignature(dst, cap, "IREZ");
    if (roff >= 0)
    {
        uint32_t copyLen = cap - (uint32_t)roff;
        unsigned char *cpy = (unsigned char *)XNewPtr(copyLen);
        if (cpy) { XBlockMove(dst + roff, cpy, copyLen); if (outRmf) *outRmf = cpy; if (outRmfLen) *outRmfLen = copyLen; }
        XDisposePtr(dst);
        return TRUE;
    }
    XDisposePtr(dst);
    return FALSE;
}
// Scan a buffer for zlib streams; try raw and decrypted windows; for each inflated blob, try to extract bank and midi/rmf.
static XBOOL PV_TryExtractFromPackedMXMF(const unsigned char *bytes, uint32_t ulen,
                                         const unsigned char **outMidi, uint32_t *outMidiLen,
                                         const unsigned char **outRmf, uint32_t *outRmfLen,
                                         XBOOL *bankLoaded)
{
    if (outMidi) {
        *outMidi = NULL;
    }
    if (outMidiLen) {
        *outMidiLen = 0;
    }
    if (outRmf) {
        *outRmf = NULL;
    }
    if (outRmfLen) {
        *outRmfLen = 0;
    }
    if (bankLoaded) *bankLoaded = FALSE;
    if (!bytes || ulen < 4) return FALSE;

    const char smf_sig[4] = {'M','T','h','d'};
    const char rmf_sig[4] = {'I','R','E','Z'};

    // Heuristic cap to avoid runaway
    const uint32_t kMaxStreams = 64;
    uint32_t foundStreams = 0;

    // Track discovered song data while we continue to look for a bank in later streams
    const unsigned char *foundMidi = NULL; uint32_t foundMidiLen = 0;
    const unsigned char *foundRmf  = NULL; uint32_t foundRmfLen  = 0;

    for (uint32_t i = 0; i + 2 < ulen && foundStreams < kMaxStreams; ++i)
    {
        // Look for zlib (0x78 ..) or gzip (0x1f 0x8b)
        XBOOL isZ = (bytes[i] == 0x78) ? TRUE : FALSE;
        XBOOL isGz = (!isZ && i + 1 < ulen && bytes[i] == 0x1f && bytes[i+1] == 0x8b) ? TRUE : FALSE;
        if (!isZ && !isGz) continue;
        if (isZ && !PV_IsLikelyZlibHeader(bytes, ulen, i)) continue;

        unsigned char *out = NULL; uint32_t outLen = 0;
        if (PV_InflateFromOffset(bytes, ulen, i, &out, &outLen))
        {
            foundStreams++;
            BAE_PRINTF("[MXMF] zlib stream #%u at file+%u, inflated=%u bytes\n", foundStreams, i, outLen);
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
            // Try bank from inflated blob
            if (bankLoaded && *bankLoaded == FALSE)
            {
                if (PV_TryLoadBankFromBlob(out, outLen) == TRUE)
                {
                    BAE_PRINTF("[MXMF] bank loaded from inflated stream #%u\n", foundStreams);
                    *bankLoaded = TRUE;
                }
            }
#endif
            // Try MIDI or RMF from inflated blob
            int off = PV_FindSignature(out, outLen, smf_sig);
            if (off >= 0 && !foundMidi)
            {
                uint32_t copyLen = outLen - (uint32_t)off;
                unsigned char *copy = (unsigned char *)XNewPtr(copyLen);
                if (!copy) { XDisposePtr(out); return FALSE; }
                XBlockMove(out + off, copy, copyLen);
                XDisposePtr(out);
                foundMidi = copy;
                foundMidiLen = copyLen;
                BAE_PRINTF("[MXMF] found SMF in inflated stream (offset %d, len %u)\n", off, copyLen);
                // If we already loaded a bank, we can stop scanning now
                if (bankLoaded && *bankLoaded == TRUE) { goto done_scan; }
                // Otherwise keep scanning for a bank
                continue;
            }
            int roff = PV_FindSignature(out, outLen, rmf_sig);
            if (roff >= 0 && !foundRmf)
            {
                uint32_t copyLen = outLen - (uint32_t)roff;
                unsigned char *copy = (unsigned char *)XNewPtr(copyLen);
                if (!copy) { XDisposePtr(out); return FALSE; }
                XBlockMove(out + roff, copy, copyLen);
                XDisposePtr(out);
                foundRmf = copy;
                foundRmfLen = copyLen;
                BAE_PRINTF("[MXMF] found RMF in inflated stream (offset %d, len %u)\n", roff, copyLen);
                if (bankLoaded && *bankLoaded == TRUE) { goto done_scan; }
                continue;
            }
            // Also check for RMID (RIFF MIDI)
            const unsigned char *rmidSmf=NULL; uint32_t rmidLen=0;
            if (PV_ExtractRMIDToSMF(out, outLen, &rmidSmf, &rmidLen))
            {
                if (!foundMidi)
                {
                    unsigned char *copy = (unsigned char *)XNewPtr(rmidLen);
                    if (!copy) { XDisposePtr(out); return FALSE; }
                    XBlockMove(rmidSmf, copy, rmidLen);
                    XDisposePtr(out);
                    foundMidi = copy;
                    foundMidiLen = rmidLen;
                    BAE_PRINTF("[MXMF] found RMID->SMF in inflated stream (len %u)\n", rmidLen);
                    if (bankLoaded && *bankLoaded == TRUE) { goto done_scan; }
                    continue;
                }
            }
            // No direct hit; free and continue
            XDisposePtr(out);
            out = NULL;
        }
        else
        {
            // Try decrypting a window then inflating
            uint32_t win = (ulen - i);
            if (win > (8u<<20)) win = (8u<<20); // limit to 8MB window
            unsigned char *cpy = (unsigned char *)XNewPtr(win);
            if (!cpy) continue;
            XBlockMove(bytes + i, cpy, win);
            XDecryptData(cpy, win);
            unsigned char *dout=NULL; uint32_t dlen=0;
            if (PV_InflateFromOffset(cpy, win, 0, &dout, &dlen))
            {
                foundStreams++;
                BAE_PRINTF("[MXMF] zlib(decrypted) stream #%u at file+%u, inflated=%u bytes\n", foundStreams, i, dlen);
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
                if (bankLoaded && *bankLoaded == FALSE)
                {
                    if (PV_TryLoadBankFromBlob(dout, dlen) == TRUE)
                    {
                        BAE_PRINTF("[MXMF] bank loaded from decrypted inflated stream #%u\n", foundStreams);
                        *bankLoaded = TRUE;
                    }
                }
#endif
                int off = PV_FindSignature(dout, dlen, smf_sig);
                if (off >= 0 && !foundMidi)
                {
                    uint32_t copyLen = dlen - (uint32_t)off;
                    unsigned char *copy = (unsigned char *)XNewPtr(copyLen);
                    if (!copy) { XDisposePtr(dout); XDisposePtr(cpy); return FALSE; }
                    XBlockMove(dout + off, copy, copyLen);
                    XDisposePtr(dout);
                    XDisposePtr(cpy);
                    foundMidi = copy;
                    foundMidiLen = copyLen;
                    BAE_PRINTF("[MXMF] found SMF in decrypted inflated stream (offset %d, len %u)\n", off, copyLen);
                    if (bankLoaded && *bankLoaded == TRUE) { goto done_scan; }
                    continue;
                }
                int roff = PV_FindSignature(dout, dlen, rmf_sig);
                if (roff >= 0 && !foundRmf)
                {
                    uint32_t copyLen = dlen - (uint32_t)roff;
                    unsigned char *copy = (unsigned char *)XNewPtr(copyLen);
                    if (!copy) { XDisposePtr(dout); XDisposePtr(cpy); return FALSE; }
                    XBlockMove(dout + roff, copy, copyLen);
                    XDisposePtr(dout);
                    XDisposePtr(cpy);
                    foundRmf = copy;
                    foundRmfLen = copyLen;
                    BAE_PRINTF("[MXMF] found RMF in decrypted inflated stream (offset %d, len %u)\n", roff, copyLen);
                    if (bankLoaded && *bankLoaded == TRUE) { goto done_scan; }
                    continue;
                }
                const unsigned char *rmidSmf=NULL; uint32_t rmidLen=0;
                if (PV_ExtractRMIDToSMF(dout, dlen, &rmidSmf, &rmidLen))
                {
                    if (!foundMidi)
                    {
                        unsigned char *copy = (unsigned char *)XNewPtr(rmidLen);
                        if (!copy) { XDisposePtr(dout); XDisposePtr(cpy); return FALSE; }
                        XBlockMove(rmidSmf, copy, rmidLen);
                        XDisposePtr(dout);
                        XDisposePtr(cpy);
                        foundMidi = copy;
                        foundMidiLen = rmidLen;
                        BAE_PRINTF("[MXMF] found RMID->SMF in decrypted inflated stream (len %u)\n", rmidLen);
                        if (bankLoaded && *bankLoaded == TRUE) { goto done_scan; }
                        continue;
                    }
                }
                XDisposePtr(dout);
            }
            XDisposePtr(cpy);
        }
        // Early out if we have both a song and loaded a bank
        if ((foundMidi || foundRmf) && bankLoaded && *bankLoaded == TRUE)
            break;
    }
done_scan:
    // If we didn't find any headered stream, try a bounded RAW deflate probe at coarse offsets
    if (!foundMidi && !foundRmf && (!bankLoaded || *bankLoaded == FALSE))
    {
        const uint32_t step = (ulen < 65536) ? 256u : 1024u;
        uint32_t tries = 0, maxTries = 32;
        for (uint32_t roff = 0; roff + 8 < ulen && tries < maxTries; roff += step, ++tries)
        {
            unsigned char *rdout = NULL; uint32_t rdlen = 0;
            if (PV_InflateRawFromOffset(bytes, ulen, roff, &rdout, &rdlen))
            {
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
                if (bankLoaded && *bankLoaded == FALSE)
                {
                    if (PV_TryLoadBankFromBlob(rdout, rdlen) == TRUE)
                    {
                        BAE_PRINTF("[MXMF] bank loaded from RAW inflated stream\n");
                        *bankLoaded = TRUE;
                    }
                }
#endif
                int off = PV_FindSignature(rdout, rdlen, smf_sig);
                if (off >= 0 && !foundMidi)
                {
                    uint32_t copyLen = rdlen - (uint32_t)off;
                    unsigned char *copy = (unsigned char *)XNewPtr(copyLen);
                    if (!copy) { XDisposePtr(rdout); break; }
                    XBlockMove(rdout + off, copy, copyLen);
                    XDisposePtr(rdout);
                    foundMidi = copy; foundMidiLen = copyLen;
                    BAE_PRINTF("[MXMF] found SMF in RAW inflated stream (offset %d, len %u)\n", off, copyLen);
                    if (!bankLoaded || *bankLoaded == FALSE) continue;
                    break;
                }
                int roff2 = PV_FindSignature(rdout, rdlen, rmf_sig);
                if (roff2 >= 0 && !foundRmf)
                {
                    uint32_t copyLen = rdlen - (uint32_t)roff2;
                    unsigned char *copy = (unsigned char *)XNewPtr(copyLen);
                    if (!copy) { XDisposePtr(rdout); break; }
                    XBlockMove(rdout + roff2, copy, copyLen);
                    XDisposePtr(rdout);
                    foundRmf = copy; foundRmfLen = copyLen;
                    BAE_PRINTF("[MXMF] found RMF in RAW inflated stream (offset %d, len %u)\n", roff2, copyLen);
                    if (!bankLoaded || *bankLoaded == FALSE) continue;
                    break;
                }
                XDisposePtr(rdout);
            }
        }
    }
    // Provide any discovered song buffers to caller
    if (foundMidi && outMidi && outMidiLen)
    {
        *outMidi = foundMidi;
        *outMidiLen = foundMidiLen;
    }
    if (foundRmf && outRmf && outRmfLen)
    {
        *outRmf = foundRmf;
        *outRmfLen = foundRmfLen;
    }
    return (foundMidi != NULL) || (foundRmf != NULL);
}

static XBOOL PV_TryLoadBankFromBlob(const unsigned char *buf, uint32_t len)
{
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
    // Collect RIFF SF2/DLS candidates and try them in priority order
    typedef struct { uint32_t off, bytes; XBOOL isDLS, hasWvpl; uint64_t score; } cand_t;
    cand_t cands[16];
    int candCount = 0;
    for (uint32_t i = 0; i + 12 <= len; ++i)
    {
        if (buf[i] == 'R' && buf[i+1] == 'I' && buf[i+2] == 'F' && buf[i+3] == 'F')
        {
            uint32_t sz = (uint32_t)buf[i+4] | ((uint32_t)buf[i+5] << 8) | ((uint32_t)buf[i+6] << 16) | ((uint32_t)buf[i+7] << 24);
            if (i + 8 + sz > len) { continue; }
            const unsigned char *type = &buf[i+8];
            XBOOL isDLS = (type[0] == 'D' && type[1] == 'L' && type[2] == 'S' && type[3] == ' ');
            XBOOL isSF2 = (type[0] == 's' && type[1] == 'f' && type[2] == 'b' && type[3] == 'k');
            if (!(isDLS || isSF2)) { i += (8 + sz) - 1; continue; }
            BAE_PRINTF("[XMF] RIFF at +%u type=%.4s size=%u (isDLS=%d isSF2=%d)\n", i, type, sz, (int)isDLS, (int)isSF2);
            XBOOL hasWvpl = FALSE; uint32_t waveCount = 0;
            if (isDLS) {
                const unsigned char *dls = buf + i + 12;
                uint32_t dlsLen = (sz >= 4 ? sz - 4 : 0);
                for (uint32_t k = 0; k + 4 <= dlsLen; ++k) {
                    if (!hasWvpl && dls[k]=='w' && dls[k+1]=='v' && dls[k+2]=='p' && dls[k+3]=='l') hasWvpl = TRUE;
                    if (dls[k]=='w' && dls[k+1]=='a' && dls[k+2]=='v' && dls[k+3]=='e') waveCount++;
                }
            }
            const uint32_t totalBytes = 8 + sz;
            if (isDLS && (!hasWvpl && totalBytes < 32u * 1024u)) {
                BAE_PRINTF("[XMF] skipping tiny DLS (bytes=%u, hasWvpl=%d, wave tags=%u)\n", totalBytes, (int)hasWvpl, waveCount);
                i += (8 + sz) - 1; continue;
            }
            if (candCount < (int)(sizeof(cands)/sizeof(cands[0]))) {
                cand_t *c = &cands[candCount++];
                c->off = i; c->bytes = totalBytes; c->isDLS = isDLS; c->hasWvpl = hasWvpl;
                c->score = (uint64_t)totalBytes + ((isDLS && hasWvpl) ? ((uint64_t)1 << 40) : 0);
            }
            i += (8 + sz) - 1;
        }
    }
    if (candCount == 0) {
        BAE_PRINTF("[XMF] no RIFF bank found in blob of %u bytes\n", len);
        return FALSE;
    }
    // Sort by score desc
    for (int a = 0; a < candCount - 1; ++a) {
        int best = a;
        for (int b = a + 1; b < candCount; ++b) {
            if (cands[b].score > cands[best].score) best = b;
        }
        if (best != a) { cand_t tmp = cands[a]; cands[a] = cands[best]; cands[best] = tmp; }
    }
    BAE_PRINTF("[XMF] trying %d bank candidate(s)\n", candCount);
    for (int idx = 0; idx < candCount; ++idx) {
        cand_t *c = &cands[idx];
        BAE_PRINTF("[XMF] attempting load #%d @+%u bytes=%u%s\n", idx+1, c->off, c->bytes,
                   (c->isDLS? (c->hasWvpl? ", DLS wvpl=YES" : ", DLS wvpl=NO") : ", SF2"));
        OPErr r = GM_LoadSF2SoundfontFromMemory(buf + c->off, (size_t)c->bytes);
        if (r == NO_ERR) {
            int presetCount = 0;
            XBOOL okPresets = GM_SF2_CurrentFontHasAnyPreset(&presetCount);
            if (okPresets) {
                BAE_PRINTF("[XMF] bank load succeeded on candidate #%d (presets>0)\n", idx+1);
                return TRUE;
            } else {
                BAE_PRINTF("[XMF] bank candidate #%d loaded but no presets found (count=%d) — trying next...\n", idx+1, presetCount);
                GM_UnloadSF2Soundfont();
            }
        } else {
            BAE_PRINTF("[XMF] bank load failed on candidate #%d (result=%d), trying next...\n", idx+1, r);
        }
    }
    BAE_PRINTF("[XMF] all bank candidates failed to load\n");
    return FALSE;
#else
    (void)buf; (void)len; return FALSE;
#endif
}

BAEResult BAESong_LoadXmfFromFile(BAESong song, BAEPathName filePath, BAE_BOOL ignoreBadInstruments)
{
    if (!(song))
        return BAE_NULL_OBJECT;

    XFILENAME name;
    XConvertPathToXFILENAME(filePath, &name);

    int32_t size = 0;
    XPTR data = PV_GetFileAsData(&name, &size);
    if (!data || size <= 0)
    {
        if (data) XDisposePtr(data);
        return BAE_BAD_FILE;
    }

    unsigned char *bytes = (unsigned char *)data;
    uint32_t ulen = (uint32_t)size;

    const char smf_sig[4] = {'M','T','h','d'};
    const char rmf_sig[4] = {'I','R','E','Z'};

    // MXMF (XMF 2.00) fast path: detect and try to extract packed content
    if (ulen >= 8 && memcmp(bytes, "XMF_2.00", 8) == 0)
    {
        BAE_PRINTF("[XMF] Detected XMF_2.00 (MXMF), size=%u\n", ulen);
        const unsigned char *mid=NULL; uint32_t midLen=0;
        const unsigned char *rmf=NULL; uint32_t rmfLen=0;
        XBOOL bankLoaded = FALSE;
        if (PV_TryExtractFromPackedMXMF(bytes, ulen, &mid, &midLen, &rmf, &rmfLen, &bankLoaded))
        {
            BAEResult lerr;
            if (mid && midLen)
            {
                BAE_PRINTF("[XMF] Packed scan yielded SMF len=%u (bankLoaded=%d)\n", midLen, (int)bankLoaded);
                lerr = BAESong_LoadMidiFromMemory(song, (void const *)mid, (uint32_t)midLen, ignoreBadInstruments);
            }
            else if (rmf && rmfLen)
            {
                BAE_PRINTF("[XMF] Packed scan yielded RMF len=%u (bankLoaded=%d)\n", rmfLen, (int)bankLoaded);
                lerr = BAESong_LoadRmfFromMemory(song, (void *)rmf, (uint32_t)rmfLen, 0, ignoreBadInstruments);
            }
            else
            {
                lerr = BAE_BAD_FILE;
            }
            // If we didn't find/load a bank during the packed scan, try scanning the raw file as a fallback
            if (bankLoaded == FALSE)
            {
                BAE_PRINTF("[XMF] No bank loaded from packed content; scanning raw container for RIFF bank...\n");
                PV_TryLoadBankFromBlob(bytes, ulen);
            }
            // Free inflated buffers (loaders make their own copy)
            if (mid) { XDisposePtr((XPTR)mid); }
            if (rmf) { XDisposePtr((XPTR)rmf); }
            XDisposePtr(data);
            return lerr;
        }
        // If packed extraction failed, fall back to heuristic scanning below
    }

    // XMF v1.00 structured parser path
    if (ulen >= 8 && memcmp(bytes, "XMF_1.00", 8) == 0)
    {
        const unsigned char *mid=NULL; uint32_t midLen=0;
        const unsigned char *rmf=NULL; uint32_t rmfLen=0;
        XBOOL bankLoaded = FALSE;
        if (PV_TryParseXMF1(bytes, ulen, &mid, &midLen, &rmf, &rmfLen, &bankLoaded))
        {
            BAEResult lerr = BAE_BAD_FILE;
            if (mid && midLen)
            {
                BAE_PRINTF("[XMF] Parsed XMF_1.00 -> SMF len=%u (bankLoaded=%d)\n", midLen, (int)bankLoaded);
                lerr = BAESong_LoadMidiFromMemory(song, (void const *)mid, (uint32_t)midLen, ignoreBadInstruments);
            }
            else if (rmf && rmfLen)
            {
                BAE_PRINTF("[XMF] Parsed XMF_1.00 -> RMF len=%u (bankLoaded=%d)\n", rmfLen, (int)bankLoaded);
                lerr = BAESong_LoadRmfFromMemory(song, (void *)rmf, (uint32_t)rmfLen, 0, ignoreBadInstruments);
            }
            if (mid) XDisposePtr((XPTR)mid);
            if (rmf) XDisposePtr((XPTR)rmf);
            // If no bank was loaded during parse, scan the raw file for a RIFF bank as fallback
            if (bankLoaded == FALSE)
            {
                BAE_PRINTF("[XMF] No bank loaded during XMF_1.00 parse; scanning raw container for RIFF bank...\n");
                PV_TryLoadBankFromBlob(bytes, ulen);
            }
            XDisposePtr(data);
            return lerr;
        }
        // If parsing didn't yield content, try a packed-stream scan across the whole file
        if (!(mid && midLen) && !(rmf && rmfLen))
        {
            const unsigned char *pm=NULL; uint32_t pmLen=0; const unsigned char *pr=NULL; uint32_t prLen=0; XBOOL bank2 = bankLoaded;
            if (PV_TryExtractFromPackedMXMF(bytes, ulen, &pm, &pmLen, &pr, &prLen, &bank2))
            {
                BAEResult lerr2 = BAE_BAD_FILE;
                if (pm && pmLen) { lerr2 = BAESong_LoadMidiFromMemory(song, (void const *)pm, pmLen, ignoreBadInstruments); }
                else if (pr && prLen) { lerr2 = BAESong_LoadRmfFromMemory(song, (void *)pr, prLen, 0, ignoreBadInstruments); }
                if (pm) { XDisposePtr((XPTR)pm); }
                if (pr) { XDisposePtr((XPTR)pr); }
                if (bank2 == FALSE) { PV_TryLoadBankFromBlob(bytes, ulen); }
                XDisposePtr(data);
                return lerr2;
            }
            // Try on decrypted copy as well
            unsigned char *decAll = (unsigned char *)XNewPtr(ulen);
            if (decAll)
            {
                XBlockMove(bytes, decAll, ulen);
                XDecryptData(decAll, ulen);
                pm=NULL; pmLen=0; pr=NULL; prLen=0; bank2 = bankLoaded;
                if (PV_TryExtractFromPackedMXMF(decAll, ulen, &pm, &pmLen, &pr, &prLen, &bank2))
                {
                    BAEResult lerr2 = BAE_BAD_FILE;
                    if (pm && pmLen) { lerr2 = BAESong_LoadMidiFromMemory(song, (void const *)pm, pmLen, ignoreBadInstruments); }
                    else if (pr && prLen) { lerr2 = BAESong_LoadRmfFromMemory(song, (void *)pr, prLen, 0, ignoreBadInstruments); }
                    if (pm) { XDisposePtr((XPTR)pm); }
                    if (pr) { XDisposePtr((XPTR)pr); }
                    if (bank2 == FALSE) { PV_TryLoadBankFromBlob(decAll, ulen); }
                    XDisposePtr(decAll);
                    XDisposePtr(data);
                    return lerr2;
                }
                XDisposePtr(decAll);
            }
        }
        // Fall through to heuristics if still no content
        BAE_PRINTF("[XMF] XMF_1.00 parse didn't yield content; falling back to heuristics\n");
    }

    int smf_off = PV_FindSignature(bytes, ulen, smf_sig);
    if (smf_off >= 0)
    {
        BAE_PRINTF("[XMF] Found SMF header at +%d in container (size=%u)\n", smf_off, ulen);
        const char *bankHdr = "Bank Files";
        int bankHdrOff = PV_FindBytes(bytes, ulen, bankHdr, (uint32_t)strlen(bankHdr));
        if (bankHdrOff >= 0)
        {
            uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
            BAE_PRINTF("[XMF] 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
            if (bankStart < ulen)
                PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
        }
        else
        {
            BAE_PRINTF("[XMF] No 'Bank Files' header; scanning entire container for RIFF bank\n");
            PV_TryLoadBankFromBlob(bytes, ulen);
        }
        BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)(bytes + smf_off), (uint32_t)(ulen - (uint32_t)smf_off), ignoreBadInstruments);
        XDisposePtr(data);
        return lerr;
    }

    int rmf_off = PV_FindSignature(bytes, ulen, rmf_sig);
    if (rmf_off >= 0)
    {
        BAE_PRINTF("[XMF] Found RMF header at +%d in container (size=%u)\n", rmf_off, ulen);
        const char *bankHdr = "Bank Files";
        int bankHdrOff = PV_FindBytes(bytes, ulen, bankHdr, (uint32_t)strlen(bankHdr));
        if (bankHdrOff >= 0)
        {
            uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
            BAE_PRINTF("[XMF] 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
            if (bankStart < ulen)
                PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
        }
        else
        {
            BAE_PRINTF("[XMF] No 'Bank Files' header; scanning entire container for RIFF bank\n");
            PV_TryLoadBankFromBlob(bytes, ulen);
        }
        BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)(bytes + rmf_off), (uint32_t)(ulen - (uint32_t)rmf_off), 0, ignoreBadInstruments);
        XDisposePtr(data);
        return lerr;
    }

    const unsigned char *rmidSmf = NULL; uint32_t rmidLen = 0;
    if (PV_ExtractRMIDToSMF(bytes, ulen, &rmidSmf, &rmidLen))
    {
        BAE_PRINTF("[XMF] Found RIFF/RMID -> SMF len=%u in container\n", rmidLen);
        const char *bankHdr = "Bank Files";
        int bankHdrOff = PV_FindBytes(bytes, ulen, bankHdr, (uint32_t)strlen(bankHdr));
        if (bankHdrOff >= 0)
        {
            uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
            BAE_PRINTF("[XMF] 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
            if (bankStart < ulen)
                PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
        }
        else
        {
            BAE_PRINTF("[XMF] No 'Bank Files' header; scanning entire container for RIFF bank\n");
            PV_TryLoadBankFromBlob(bytes, ulen);
        }
        BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)rmidSmf, rmidLen, ignoreBadInstruments);
        XDisposePtr(data);
        return lerr;
    }

    const char *midiHdr = "MIDI Files";
    const char *bankHdr = "Bank Files";
    int midiHdrOff = PV_FindBytes(bytes, ulen, midiHdr, (uint32_t)strlen(midiHdr));
    int bankHdrOff = PV_FindBytes(bytes, ulen, bankHdr, (uint32_t)strlen(bankHdr));
    if (midiHdrOff >= 0)
    {
        uint32_t midiStart = (uint32_t)midiHdrOff + (uint32_t)strlen(midiHdr);
        uint32_t midiEnd = (bankHdrOff > midiHdrOff && bankHdrOff >= 0) ? (uint32_t)bankHdrOff : ulen;
        BAE_PRINTF("[XMF] 'MIDI Files' header at +%d -> region %u..%u\n", midiHdrOff, midiStart, midiEnd);
        if (midiStart < midiEnd && midiEnd <= ulen)
        {
            const unsigned char *m = bytes + midiStart;
            uint32_t mlen = midiEnd - midiStart;
            int roff = PV_FindSignature(m, mlen, smf_sig);
            if (roff >= 0)
            {
                BAE_PRINTF("[XMF] Found SMF at +%d in MIDI region (len=%u)\n", roff, (uint32_t)(mlen - (uint32_t)roff));
                if (bankHdrOff >= 0)
                {
                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                    BAE_PRINTF("[XMF] 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
                    if (bankStart < ulen)
                        PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                }
                BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)(m + roff), (uint32_t)(mlen - (uint32_t)roff), ignoreBadInstruments);
                XDisposePtr(data);
                return lerr;
            }
            const unsigned char *smf2=NULL; uint32_t slen2=0;
            if (PV_ExtractRMIDToSMF(m, mlen, &smf2, &slen2))
            {
                BAE_PRINTF("[XMF] Found RMID->SMF in MIDI region (len=%u)\n", slen2);
                if (bankHdrOff >= 0)
                {
                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                    BAE_PRINTF("[XMF] 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
                    if (bankStart < ulen)
                        PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                }
                BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)smf2, slen2, ignoreBadInstruments);
                XDisposePtr(data);
                return lerr;
            }
            int irez = PV_FindSignature(m, mlen, rmf_sig);
            if (irez >= 0)
            {
                BAE_PRINTF("[XMF] Found RMF at +%d in MIDI region (len=%u)\n", irez, (uint32_t)(mlen - (uint32_t)irez));
                if (bankHdrOff >= 0)
                {
                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                    BAE_PRINTF("[XMF] 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
                    if (bankStart < ulen)
                        PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                }
                BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)(m + irez), (uint32_t)(mlen - (uint32_t)irez), 0, ignoreBadInstruments);
                XDisposePtr(data);
                return lerr;
            }

            // Region-only decrypt fallback: some XMF v1 encrypt only the payloads
            unsigned char *mcpy = (unsigned char *)XNewPtr(mlen);
            if (mcpy)
            {
                XBlockMove(m, mcpy, mlen);
                XDecryptData(mcpy, mlen);
                int droff = PV_FindSignature(mcpy, mlen, smf_sig);
                if (droff >= 0)
                {
                    BAE_PRINTF("[XMF] (dec-region) Found SMF at +%d in MIDI region (len=%u)\n", droff, (uint32_t)(mlen - (uint32_t)droff));
                    if (bankHdrOff >= 0)
                    {
                        uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                        BAE_PRINTF("[XMF] (dec-region) 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
                        if (bankStart < ulen)
                            PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                    }
                    BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)(mcpy + droff), (uint32_t)(mlen - (uint32_t)droff), ignoreBadInstruments);
                    XDisposePtr(mcpy);
                    XDisposePtr(data);
                    return lerr;
                }
                const unsigned char *dsmf2=NULL; uint32_t dslen2=0;
                if (PV_ExtractRMIDToSMF(mcpy, mlen, &dsmf2, &dslen2))
                {
                    BAE_PRINTF("[XMF] (dec-region) Found RMID->SMF in MIDI region (len=%u)\n", dslen2);
                    if (bankHdrOff >= 0)
                    {
                        uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                        BAE_PRINTF("[XMF] (dec-region) 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
                        if (bankStart < ulen)
                            PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                    }
                    BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)dsmf2, dslen2, ignoreBadInstruments);
                    XDisposePtr(mcpy);
                    XDisposePtr(data);
                    return lerr;
                }
                int direz = PV_FindSignature(mcpy, mlen, rmf_sig);
                if (direz >= 0)
                {
                    BAE_PRINTF("[XMF] (dec-region) Found RMF at +%d in MIDI region (len=%u)\n", direz, (uint32_t)(mlen - (uint32_t)direz));
                    if (bankHdrOff >= 0)
                    {
                        uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                        BAE_PRINTF("[XMF] (dec-region) 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
                        if (bankStart < ulen)
                            PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                    }
                    BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)(mcpy + direz), (uint32_t)(mlen - (uint32_t)direz), 0, ignoreBadInstruments);
                    XDisposePtr(mcpy);
                    XDisposePtr(data);
                    return lerr;
                }
                // Sliding-window decrypt: some files encode subranges only. Try decrypt starting at each offset.
                {
                    const uint32_t maxTry = mlen > 0 ? (mlen - 1) : 0;
                    for (uint32_t so = 0; so < maxTry; ++so)
                    {
                        uint32_t dlen = mlen - so;
                        unsigned char *dwin = (unsigned char *)XNewPtr(dlen);
                        if (!dwin) break;
                        XBlockMove(m + so, dwin, dlen);
                        XDecryptData(dwin, dlen);
                        int woff = PV_FindSignature(dwin, dlen, smf_sig);
                        if (woff >= 0)
                        {
                            BAE_PRINTF("[XMF] (dec-scan) Found SMF with decrypt start @+%u (woff=%d, outLen=%u)\n", so, woff, dlen - (uint32_t)woff);
                            if (bankHdrOff >= 0)
                            {
                                uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                BAE_PRINTF("[XMF] (dec-scan) 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
                                if (bankStart < ulen)
                                    PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                            }
                            BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)(dwin + woff), (uint32_t)(dlen - (uint32_t)woff), ignoreBadInstruments);
                            XDisposePtr(dwin);
                            XDisposePtr(mcpy);
                            XDisposePtr(data);
                            return lerr;
                        }
                        int wrez = PV_FindSignature(dwin, dlen, rmf_sig);
                        if (wrez >= 0)
                        {
                            BAE_PRINTF("[XMF] (dec-scan) Found RMF with decrypt start @+%u (wrez=%d, outLen=%u)\n", so, wrez, dlen - (uint32_t)wrez);
                            if (bankHdrOff >= 0)
                            {
                                uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                BAE_PRINTF("[XMF] (dec-scan) 'Bank Files' header at +%d -> scanning bank region start=%u\n", bankHdrOff, bankStart);
                                if (bankStart < ulen)
                                    PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                            }
                            BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)(dwin + wrez), (uint32_t)(dlen - (uint32_t)wrez), 0, ignoreBadInstruments);
                            XDisposePtr(dwin);
                            XDisposePtr(mcpy);
                            XDisposePtr(data);
                            return lerr;
                        }
                        XDisposePtr(dwin);
                    }
                    XDisposePtr(mcpy);
                }

                // Packed content fallback: try deflate (zlib/gzip) and raw-deflate inside region
                {
                    // First pass: zlib/gzip headered inflates
                    for (uint32_t i = 0; i + 2 < mlen; ++i)
                    {
                        XBOOL z = (m[i] == 0x78) ? TRUE : FALSE;
                        XBOOL gz = (!z && i + 1 < mlen && m[i] == 0x1f && m[i+1] == 0x8b) ? TRUE : FALSE;
                        if (!z && !gz) continue;
                        if (z && !PV_IsLikelyZlibHeader(m, mlen, i)) continue;
                        unsigned char *out = NULL; uint32_t outLen = 0;
                        if (PV_InflateFromOffset(m, mlen, i, &out, &outLen))
                        {
                            int off = PV_FindSignature(out, outLen, smf_sig);
                            if (off >= 0)
                            {
                                BAE_PRINTF("[XMF] (inflate) Found SMF in region stream (roff=%u, len=%u)\n", i, outLen - (uint32_t)off);
                                if (bankHdrOff >= 0)
                                {
                                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                    if (bankStart < ulen) PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                                }
                                BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)(out + off), (uint32_t)(outLen - (uint32_t)off), ignoreBadInstruments);
                                XDisposePtr(out);
                                XDisposePtr(data);
                                return lerr;
                            }
                            int roff = PV_FindSignature(out, outLen, rmf_sig);
                            if (roff >= 0)
                            {
                                BAE_PRINTF("[XMF] (inflate) Found RMF in region stream (roff=%u, len=%u)\n", i, outLen - (uint32_t)roff);
                                if (bankHdrOff >= 0)
                                {
                                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                    if (bankStart < ulen) PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                                }
                                BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)(out + roff), (uint32_t)(outLen - (uint32_t)roff), 0, ignoreBadInstruments);
                                XDisposePtr(out);
                                XDisposePtr(data);
                                return lerr;
                            }
                            const unsigned char *smfRMID=NULL; uint32_t smfRMIDLen=0;
                            if (PV_ExtractRMIDToSMF(out, outLen, &smfRMID, &smfRMIDLen))
                            {
                                BAE_PRINTF("[XMF] (inflate) Found RMID->SMF in region stream (len=%u)\n", smfRMIDLen);
                                if (bankHdrOff >= 0)
                                {
                                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                    if (bankStart < ulen) PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                                }
                                BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)smfRMID, smfRMIDLen, ignoreBadInstruments);
                                XDisposePtr(out);
                                XDisposePtr(data);
                                return lerr;
                            }
                            XDisposePtr(out);
                        }
                    }
                    // Second pass: coarse raw-deflate attempts
                    const uint32_t step = (mlen <= 4096) ? 1u : ((mlen < 65536) ? 128u : 512u);
                    uint32_t tries = 0, maxTries = 128;
                    for (uint32_t roff = 0; roff + 8 < mlen && tries < maxTries; roff += step, ++tries)
                    {
                        unsigned char *rdout = NULL; uint32_t rdlen = 0;
                        if (PV_InflateRawFromOffset(m, mlen, roff, &rdout, &rdlen))
                        {
                            int off = PV_FindSignature(rdout, rdlen, smf_sig);
                            if (off >= 0)
                            {
                                BAE_PRINTF("[XMF] (inflate-raw) Found SMF in region (start=%u, len=%u)\n", roff, rdlen - (uint32_t)off);
                                if (bankHdrOff >= 0)
                                {
                                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                    if (bankStart < ulen) PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                                }
                                BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)(rdout + off), (uint32_t)(rdlen - (uint32_t)off), ignoreBadInstruments);
                                XDisposePtr(rdout);
                                XDisposePtr(data);
                                return lerr;
                            }
                            int rmr = PV_FindSignature(rdout, rdlen, rmf_sig);
                            if (rmr >= 0)
                            {
                                BAE_PRINTF("[XMF] (inflate-raw) Found RMF in region (start=%u, len=%u)\n", roff, rdlen - (uint32_t)rmr);
                                if (bankHdrOff >= 0)
                                {
                                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                    if (bankStart < ulen) PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                                }
                                BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)(rdout + rmr), (uint32_t)(rdlen - (uint32_t)rmr), 0, ignoreBadInstruments);
                                XDisposePtr(rdout);
                                XDisposePtr(data);
                                return lerr;
                            }
                            const unsigned char *rmidSmf2=NULL; uint32_t rmidLen2=0;
                            if (PV_ExtractRMIDToSMF(rdout, rdlen, &rmidSmf2, &rmidLen2))
                            {
                                BAE_PRINTF("[XMF] (inflate-raw) Found RMID->SMF in region (len=%u)\n", rmidLen2);
                                if (bankHdrOff >= 0)
                                {
                                    uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                    if (bankStart < ulen) PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                                }
                                BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)rmidSmf2, rmidLen2, ignoreBadInstruments);
                                XDisposePtr(rdout);
                                XDisposePtr(data);
                                return lerr;
                            }
                            XDisposePtr(rdout);
                        }
                    }
                }
                // Third pass: LZSS probe at coarse offsets
                for (uint32_t loff = 0; loff + 16 < mlen; loff += (mlen <= 4096 ? 64u : 512u))
                {
                    const unsigned char *pm=NULL; uint32_t pmLen=0; const unsigned char *pr=NULL; uint32_t prLen=0;
                    if (PV_ProbeLZSS(m, mlen, loff, &pm, &pmLen, &pr, &prLen))
                    {
                        if (pm && pmLen)
                        {
                            BAE_PRINTF("[XMF] (lzss) Found SMF in region at loff=%u len=%u\n", loff, pmLen);
                            if (bankHdrOff >= 0)
                            {
                                uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                if (bankStart < ulen) PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                            }
                            BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)pm, pmLen, ignoreBadInstruments);
                            XDisposePtr((XPTR)pm); if (pr) XDisposePtr((XPTR)pr);
                            XDisposePtr(data);
                            return lerr;
                        }
                        if (pr && prLen)
                        {
                            BAE_PRINTF("[XMF] (lzss) Found RMF in region at loff=%u len=%u\n", loff, prLen);
                            if (bankHdrOff >= 0)
                            {
                                uint32_t bankStart = (uint32_t)bankHdrOff + (uint32_t)strlen(bankHdr);
                                if (bankStart < ulen) PV_TryLoadBankFromBlob(bytes + bankStart, ulen - bankStart);
                            }
                            BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)pr, prLen, 0, ignoreBadInstruments);
                            XDisposePtr((XPTR)pr); if (pm) XDisposePtr((XPTR)pm);
                            XDisposePtr(data);
                            return lerr;
                        }
                        if (pm) { XDisposePtr((XPTR)pm); }
                        if (pr) { XDisposePtr((XPTR)pr); }
                    }
                }
            }
        }
    }

    // If we fall through to here, try a whole-file decrypt fallback (XMF v1 encrypted files)
    // We'll decrypt a copy of the container and re-run the same heuristics.
    BAE_PRINTF("[XMF] Plain scan failed; attempting XMF v1 decrypt fallback...\n");
    unsigned char *dec = (unsigned char *)XNewPtr(ulen);
    if (dec)
    {
        XBlockMove(bytes, dec, ulen);
        XDecryptData(dec, ulen);

        // Re-scan decrypted buffer for SMF header
        int d_smf_off = PV_FindSignature(dec, ulen, smf_sig);
        if (d_smf_off >= 0)
        {
            BAE_PRINTF("[XMF] Decrypt yielded SMF header at +%d (size=%u)\n", d_smf_off, ulen);
            int d_bankHdrOff = PV_FindBytes(dec, ulen, bankHdr, (uint32_t)strlen(bankHdr));
            if (d_bankHdrOff >= 0)
            {
                uint32_t bankStart = (uint32_t)d_bankHdrOff + (uint32_t)strlen(bankHdr);
                BAE_PRINTF("[XMF] (dec) 'Bank Files' header at +%d -> scanning bank region start=%u\n", d_bankHdrOff, bankStart);
                if (bankStart < ulen)
                    PV_TryLoadBankFromBlob(dec + bankStart, ulen - bankStart);
            }
            else
            {
                BAE_PRINTF("[XMF] (dec) No 'Bank Files' header; scanning entire container for RIFF bank\n");
                PV_TryLoadBankFromBlob(dec, ulen);
            }
            BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)(dec + d_smf_off), (uint32_t)(ulen - (uint32_t)d_smf_off), ignoreBadInstruments);
            XDisposePtr(dec);
            XDisposePtr(data);
            return lerr;
        }

        // RMF after decrypt
        int d_rmf_off = PV_FindSignature(dec, ulen, rmf_sig);
        if (d_rmf_off >= 0)
        {
            BAE_PRINTF("[XMF] Decrypt yielded RMF header at +%d (size=%u)\n", d_rmf_off, ulen);
            int d_bankHdrOff = PV_FindBytes(dec, ulen, bankHdr, (uint32_t)strlen(bankHdr));
            if (d_bankHdrOff >= 0)
            {
                uint32_t bankStart = (uint32_t)d_bankHdrOff + (uint32_t)strlen(bankHdr);
                BAE_PRINTF("[XMF] (dec) 'Bank Files' header at +%d -> scanning bank region start=%u\n", d_bankHdrOff, bankStart);
                if (bankStart < ulen)
                    PV_TryLoadBankFromBlob(dec + bankStart, ulen - bankStart);
            }
            else
            {
                BAE_PRINTF("[XMF] (dec) No 'Bank Files' header; scanning entire container for RIFF bank\n");
                PV_TryLoadBankFromBlob(dec, ulen);
            }
            BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)(dec + d_rmf_off), (uint32_t)(ulen - (uint32_t)d_rmf_off), 0, ignoreBadInstruments);
            XDisposePtr(dec);
            XDisposePtr(data);
            return lerr;
        }

        // RMID->SMF after decrypt
        const unsigned char *d_rmidSmf = NULL; uint32_t d_rmidLen = 0;
        if (PV_ExtractRMIDToSMF(dec, ulen, &d_rmidSmf, &d_rmidLen))
        {
            BAE_PRINTF("[XMF] Decrypt yielded RMID->SMF len=%u in container\n", d_rmidLen);
            int d_bankHdrOff = PV_FindBytes(dec, ulen, bankHdr, (uint32_t)strlen(bankHdr));
            if (d_bankHdrOff >= 0)
            {
                uint32_t bankStart = (uint32_t)d_bankHdrOff + (uint32_t)strlen(bankHdr);
                BAE_PRINTF("[XMF] (dec) 'Bank Files' header at +%d -> scanning bank region start=%u\n", d_bankHdrOff, bankStart);
                if (bankStart < ulen)
                    PV_TryLoadBankFromBlob(dec + bankStart, ulen - bankStart);
            }
            else
            {
                BAE_PRINTF("[XMF] (dec) No 'Bank Files' header; scanning entire container for RIFF bank\n");
                PV_TryLoadBankFromBlob(dec, ulen);
            }
            BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)d_rmidSmf, d_rmidLen, ignoreBadInstruments);
            XDisposePtr(dec);
            XDisposePtr(data);
            return lerr;
        }

        // Lastly, try the explicit 'MIDI Files' region after decrypt
        int d_midiHdrOff = PV_FindBytes(dec, ulen, midiHdr, (uint32_t)strlen(midiHdr));
        int d_bankHdrOff2 = PV_FindBytes(dec, ulen, bankHdr, (uint32_t)strlen(bankHdr));
        if (d_midiHdrOff >= 0)
        {
            uint32_t midiStart = (uint32_t)d_midiHdrOff + (uint32_t)strlen(midiHdr);
            uint32_t midiEnd = (d_bankHdrOff2 > d_midiHdrOff && d_bankHdrOff2 >= 0) ? (uint32_t)d_bankHdrOff2 : ulen;
            BAE_PRINTF("[XMF] (dec) 'MIDI Files' header at +%d -> region %u..%u\n", d_midiHdrOff, midiStart, midiEnd);
            if (midiStart < midiEnd && midiEnd <= ulen)
            {
                const unsigned char *m = dec + midiStart;
                uint32_t mlen = midiEnd - midiStart;
                int roff2 = PV_FindSignature(m, mlen, smf_sig);
                if (roff2 >= 0)
                {
                    BAE_PRINTF("[XMF] (dec) Found SMF at +%d in MIDI region (len=%u)\n", roff2, (uint32_t)(mlen - (uint32_t)roff2));
                    if (d_bankHdrOff2 >= 0)
                    {
                        uint32_t bankStart = (uint32_t)d_bankHdrOff2 + (uint32_t)strlen(bankHdr);
                        BAE_PRINTF("[XMF] (dec) 'Bank Files' header at +%d -> scanning bank region start=%u\n", d_bankHdrOff2, bankStart);
                        if (bankStart < ulen)
                            PV_TryLoadBankFromBlob(dec + bankStart, ulen - bankStart);
                    }
                    BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)(m + roff2), (uint32_t)(mlen - (uint32_t)roff2), ignoreBadInstruments);
                    XDisposePtr(dec);
                    XDisposePtr(data);
                    return lerr;
                }
                const unsigned char *smf2=NULL; uint32_t slen2=0;
                if (PV_ExtractRMIDToSMF(m, mlen, &smf2, &slen2))
                {
                    BAE_PRINTF("[XMF] (dec) Found RMID->SMF in MIDI region (len=%u)\n", slen2);
                    if (d_bankHdrOff2 >= 0)
                    {
                        uint32_t bankStart = (uint32_t)d_bankHdrOff2 + (uint32_t)strlen(bankHdr);
                        BAE_PRINTF("[XMF] (dec) 'Bank Files' header at +%d -> scanning bank region start=%u\n", d_bankHdrOff2, bankStart);
                        if (bankStart < ulen)
                            PV_TryLoadBankFromBlob(dec + bankStart, ulen - bankStart);
                    }
                    BAEResult lerr = BAESong_LoadMidiFromMemory(song, (void const *)smf2, slen2, ignoreBadInstruments);
                    XDisposePtr(dec);
                    XDisposePtr(data);
                    return lerr;
                }
                int irez2 = PV_FindSignature(m, mlen, rmf_sig);
                if (irez2 >= 0)
                {
                    BAE_PRINTF("[XMF] (dec) Found RMF at +%d in MIDI region (len=%u)\n", irez2, (uint32_t)(mlen - (uint32_t)irez2));
                    if (d_bankHdrOff2 >= 0)
                    {
                        uint32_t bankStart = (uint32_t)d_bankHdrOff2 + (uint32_t)strlen(bankHdr);
                        BAE_PRINTF("[XMF] (dec) 'Bank Files' header at +%d -> scanning bank region start=%u\n", d_bankHdrOff2, bankStart);
                        if (bankStart < ulen)
                            PV_TryLoadBankFromBlob(dec + bankStart, ulen - bankStart);
                    }
                    BAEResult lerr = BAESong_LoadRmfFromMemory(song, (void *)(m + irez2), (uint32_t)(mlen - (uint32_t)irez2), 0, ignoreBadInstruments);
                    XDisposePtr(dec);
                    XDisposePtr(data);
                    return lerr;
                }
            }
        }
        // No luck after decrypt
        XDisposePtr(dec);
    }
    BAE_PRINTF("[XMF] Decrypt fallback failed to locate content; unsupported XMF variant\n");
    XDisposePtr(data);
    return BAE_BAD_FILE;
}

#endif // USE_XMF_SUPPORT && _USING_FLUIDSYNTH
