# Summary of XMF and MXMF Formats

## Overview
The Extensible Music Format (XMF) is a container format designed to encapsulate Standard MIDI Files (SMF) and Downloadable Sounds (DLS) resources, along with metadata, into a single file for efficient distribution and playback of music content. It supports a hierarchical tree structure of nodes, allowing for organized bundling of multiple resources. XMF is defined in the Recommended Practice RP-029 (XMF v1.0) from the MIDI Manufacturers Association (MMA).

MXMF (Mobile XMF) is an extension of XMF tailored for mobile devices, introduced in RP-043 (XMF 2.00). It maintains the core structure of XMF but adds enhancements for mobile environments, such as improved metadata handling and file type identifiers. The provided TypeScript code (`xmf_loader.ts`) implements a parser for XMF files, with partial support for XMF 2.00 (MXMF) features like additional header fields. It focuses on inline resources (embedded content) and throws errors for external references or unsupported unpackers. The parser extracts embedded MIDI data and DLS sound banks, along with metadata, for use in a MIDI synthesizer.

Key features from the code:
- **Supported Content**: Inline Standard MIDI Files (Type 0 or Type 1) and DLS variants (DLS1, DLS2, DLS2.2, Mobile DLS).
- **Packing/Unpacking**: Supports deflation (zlib-based decompression) for packed node content using the `fflate` library's `inflateSync`. Only the first unpacker is handled; others (e.g., MMA-specific or registered unpackers) are unsupported and cause errors.
- **Decryption**: No decryption or encryption handling is implemented or mentioned.
- **Layout**: Binary format using Variable Length Quantities (VLQ) for most lengths and IDs to optimize space. The file consists of a global header, a metadata table (skipped in parsing), and a root node defining a tree of file/folder nodes.
- **Limitations in Code**: Only inline resources (`inLineResource`) are supported; external files, in-file nodes, or XMF URIs throw errors. Internationalized metadata (multiple language versions) is skipped with a warning. Folder nodes are parsed recursively, but only MIDI and DLS files are extracted—other formats are ignored.

## File Header Structure
The XMF/MXMF file begins with a fixed header, followed by metadata and the node tree. All multi-byte values use big-endian byte order where specified; most lengths use VLQ encoding (7-bit continuation, similar to MIDI delta-time).

1. **Magic Identifier** (4 bytes): Fixed string `"XMF_"` (ASCII). Invalid headers throw a syntax error.
2. **Version String** (4 bytes): ASCII string indicating the version, e.g., `"1.00"` for XMF v1.0 or `"2.00"` for MXMF (XMF 2.00).
   - For version `"2.00"` (MXMF):
     - **File Type ID** (4 bytes, big-endian uint32): Identifies the file type (logged but not used further in code).
     - **File Type Revision ID** (4 bytes, big-endian uint32): Revision of the file type (logged but not used further).
3. **File Length** (VLQ): Total length of the file in bytes (including header). Skipped in parsing.
4. **Metadata Table Length** (VLQ): Length of the global metadata table. The parser skips this section entirely.
5. **Root Node Offset** (VLQ): Byte offset from the start of the file to the root node. The parser jumps directly to this offset to begin node parsing.

The header ensures file integrity and version compatibility. MXMF (v2.00) extends the header for better mobile device identification but does not alter the core node structure.

## Node Structure (XMFNode)
The core of XMF/MXMF is a tree of nodes, starting from the root node. Each node represents either a **file** (leaf with embedded content) or a **folder** (container for child nodes). Nodes are self-contained binary chunks with a header describing metadata, unpackers, reference type, and content.

### Node Header
- **Node Length** (VLQ): Total length of the node in bytes (including header and content).
- **Item Count** (VLQ): Number of items in the node.
  - `0`: File node (contains actual resource data).
  - `>0`: Folder node (contains child nodes; exact count not enforced in code).
- **Header Length** (VLQ): Length of the node's header section (after the initial length and item count).

The remaining header bytes (headerLength - bytes already read) contain:
- **Metadata Length** (VLQ): Length of the metadata chunk.
- **Metadata Chunk** (variable): See [Metadata Section](#metadata-section) below.
- **Unpackers Length** (VLQ): Length of the unpackers description.
- **Unpackers Chunk** (variable): See [Unpackers Section](#unpackers-section) below.

After the header:
- **Reference Type ID** (VLQ): Specifies how the node's content is referenced (from `referenceTypeIds` enum).
  - `1 (inLineResource)`: Inline embedded data (only supported type; content follows immediately).
  - Other types (e.g., `2` inFileResource, `3` inFileNode, `4` externalFile, `5` externalXMF, `6` XMFFileURIandNodeID): Unsupported; throw errors.
- **Node Data** (remaining bytes up to node length): The actual content, which may be packed.

### Metadata Section
Metadata is a key-value store for descriptive information, stored in the metadata chunk. It supports both predefined types and custom string keys. Internationalization (multiple language versions) is skipped in the code with a log message.

Loop until metadata chunk end:
- **Field Specifier**:
  - If first byte `== 0`: Followed by VLQ ID from `metadataTypes` enum (predefined keys like `title`, `copyrightNotice`, `comment`, `resourceFormat`, etc.). Invalid IDs log a warning and use `unknown_${id}` as key.
  - Else: VLQ string length + UTF-8 string (custom key).
- **Number of Versions** (VLQ): Language variants.
  - `0`: Single version. Followed by **Data Length** (VLQ) + **Contents**:
    - **Format ID** (VLQ, first byte of contents): `0-3` = text (UTF-8 string, length = dataLength - 1). `>=4` = binary data (raw bytes).
  - `>0`: International content (skipped entirely; logs number of versions).
- **Supported Metadata Types** (from enum):
  - `0`: XMFFileType
  - `1`: nodeName
  - `2`: nodeIDNumber
  - `3`: resourceFormat (critical for content identification; array [formatTypeID, resourceFormatID])
  - `4`: filenameOnDisk
  - `5`: filenameExtensionOnDisk
  - `6`: macOSFileTypeAndCreator
  - `7`: mimeType
  - `8`: title
  - `9`: copyrightNotice
  - `10`: comment
  - `11`: autoStart
  - `12`: preload
  - `13`: contentDescription (RP-42a)
  - `14`: ID3Metadata (RP-47)

In parsing, metadata like `title`, `copyrightNotice`, and `comment` is extracted as UTF-8 bytes for RMIDInfo (RIFF MIDI) compatibility. `resourceFormat` is an array: `[formatTypeID (0=standard,1=MMA,2=registered,3=nonRegistered), resourceFormatID (0=StandardMIDIFile,1=StandardMIDIFileType1,2=DLS1,3=DLS2,4=DLS22,5=mobileDLS)]`. Unrecognized formats default to "unknown".

### Unpackers Section
Describes decompression/packing schemes for the node data. If length `>0`, `packedContent = true`.

Loop until unpackers chunk end:
- **Unpacker ID** (VLQ, from `unpackerIDs` enum):
  - `0 (none)`: Followed by **Standard ID** (VLQ). No special handling.
  - `1 (MMAUnpacker)`: **Manufacturer ID** (1 or 3 bytes: if first byte `0`, read two more for 24-bit ID). + **Manufacturer Internal ID** (VLQ).
  - `2 (registered)` or `3 (nonRegistered)`: Unsupported; throw error.
  - Unknown IDs: Throw error.
- **Decoded Size** (VLQ): Expected uncompressed size (used for verification).

Only the first unpacker is processed for decompression. In the code, for file nodes:
- Slice node data from byte 2 (skipping possible zlib header?).
- Decompress using `inflateSync` (zlib deflate).
- Errors in decompression throw a custom error.
- MXMF may use additional unpackers for mobile-optimized compression, but the code does not support them beyond basic deflate.

### Node Content (Node Data)
- For **File Nodes** (`itemCount === 0`):
  - If packed: Decompress as above, resulting in raw resource bytes.
  - Interpret via `resourceFormat` metadata:
    - MIDI: `StandardMIDIFile` or `StandardMIDIFileType1` → Extract as MIDI data.
    - DLS: `DLS1`, `DLS2`, `DLS22`, or `mobileDLS` → Extract as embedded sound bank buffer.
    - Unknown/Other: Ignored or logged as unrecognized.
  - Non-standard `formatTypeID` (>0) logs a warning but proceeds.
- For **Folder Nodes** (`itemCount > 0`):
  - Resource format defaults to `"folder"`.
  - Recursively parse child nodes: Loop reading VLQ lengths and instantiating `XMFNode` for each until end.
  - No direct content; used for organization (e.g., grouping MIDI + DLS).

The tree is traversed post-parsing to find and extract MIDI data (required; throws if missing) and optional DLS sound banks. Metadata from nodes (e.g., title as nodeName or title) overrides into a unified RMIDInfo structure.

## Overall Parsing Flow
1. Validate header and version; handle MXMF extras.
2. Skip file length, metadata table, and jump to root node offset.
3. Parse root `XMFNode` recursively.
4. Traverse tree: Extract metadata, decompress file nodes if needed, identify MIDI/DLS.
5. Return raw MIDI bytes; set sound bank if DLS found.

## Additional Notes
- **VLQ Encoding**: All lengths/IDs use 7-bit VLQ (MSB=1 continues). Big-endian for fixed 4-byte fields (e.g., MXMF IDs).
- **Endianness**: Big-endian for explicit reads (e.g., version 2.00 fields); VLQ is inherently big-endian.
- **Error Handling**: Strict—unsupported features (e.g., external refs, unknown unpackers) throw errors. Logs via `SpessaSynthInfo/Warn` for debugging.
- **References**:
  - XMF v1.0: [RP-029](https://amei.or.jp/midistandardcommittee/Recommended_Practice/e/xmf-v1a.pdf)
  - MXMF (XMF 2.00): [RP-043](https://amei.or.jp/midistandardcommittee/Recommended_Practice/e/rp43.pdf)
  - Additional: RP-42a (content description), RP-47 (ID3 metadata).
- **Code-Specific**: Implements extraction for a `BasicMIDI` object, setting `bankOffset=0`, `embeddedSoundBank`, and `rmidiInfo`. No support for auto-start/preload or external loading.

Also see [xmf_loader.ts](xmf_loader.ts) for a working TypeScript implementation from SpessaSynth