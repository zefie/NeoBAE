/*
    Copyright (c) 2009 Beatnik, Inc All rights reserved.
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    
    Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    
    Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    
    Neither the name of the Beatnik, Inc nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.
    
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
    TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*
    Additional modifications © 2021-2026 zefie
    Licensed under the GNU Lesser General Public License v3.0 or later.
*/
/*****************************************************************************/
/*
**  X_API.c
**
**      This provides platform specfic functions
**
**  © Copyright 1995-2001 Beatnik, Inc, All Rights Reserved.
**  Written by Steve Hales
**
**  Beatnik products contain certain trade secrets and confidential and
**  proprietary information of Beatnik.  Use, reproduction, disclosure
**  and distribution by any means are prohibited, except pursuant to
**  a written license from Beatnik. Use of copyright notice is
**  precautionary and does not imply publication or disclosure.
**
**  Restricted Rights Legend:
**  Use, duplication, or disclosure by the Government is subject to
**  restrictions as set forth in subparagraph (c)(1)(ii) of The
**  Rights in Technical Data and Computer Software clause in DFARS
**  252.227-7013 or subparagraphs (c)(1) and (2) of the Commercial
**  Computer Software--Restricted Rights at 48 CFR 52.227-19, as
**  applicable.
**
**  Confidential-- Internal use only
**
**  History -
**  9/25/95     Created
**  12/14/95    Added XGetAndDetachResource
**  12/19/95    Added XSetBit & XTestBit
**  1/18/96     Spruced up for C++ extra error checking
**  1/28/96     Changed XGetAndDetachResource
**  2/3/96      Removed extra includes
**  2/11/96     Added XIsStereoSupported & XIs16BitSupported
**              Added XFixedDivide & XFixedMultiply
**              Started putting in platform defines around code functions
**  2/21/96     Changed XGetAndDetachResource to return a size
**  3/25/96     Modifed XExpandMace1to6 & XExpandMace1to3 to create silence if 
**              not supported
**  3/29/96     Added XPutLong & XPutShort
**  4/20/96     Moved myFixedMultiply from GenSynth.c and moved all references 
**              to XFixedMultiply
**              Moved myFixedDivide from GenSynth.c and moved all references 
**              to XFixedDivide
**  4/21/96     Removed register usage in parameters
**  5/14/96     Fixed Odd address error in XIsOurMemoryPtr
**              Replaced XFixedMultiply and XFixedDivide with integer versions rather 
**              than floating point
**  6/1/96      Added some resource management code
**  6/5/96      Added some Win95 stuff.
**  6/28/96     Added BeBox stuff
**  6/30/96     Changed font and re tabbed
**              Moved DecompressSampleFormatPtr from MacSpecificSMS
**              Moved XSetVolume & XGetVolume from MacSpecificSMS
**              Moved DecompressPtr from MacSpecificSMS
**  7/1/96      Added XCreateAccessCache
**              Changed XFILERESOURCEID to XFILERESOURCE_ID
**  7/2/96      Changed XFileOpenResource to support read only access
**  7/3/96      Added XStrCmp
**  7/4/96      Added error checking for opening read only resources
**  7/7/96      Added XStrnCmp
**  7/18/96     Modifed XFileOpenResource to read Cache resource
**              Fixed a NULL reference in PV_XFileValid
**              Fixed problem with cached resources and byte ordering
**  7/21/96     Fixed dumb bug in XGetFileResource that caused extra file scanning
**  7/23/96     Added XMicroseconds
**  8/6/96      Added XGetIndexedType & XCountTypes
**  8/12/96     Moved XExpandMace1to3 & XExpandMace1to6 to X_Compress.c
**  9/22/96     Added XRandom & XSeedRandom & XRandomRange
**  10/8/96     Added XStrCpy
**  10/11/96    Added XConvertNativeFileToXFILENAME
**              Added XFileSetPositionRelative
**              Added XDetermineByteOrder
**              Added XSwapLong & XSwapShort
**  10/15/96    Added XFileOpenResourceFromMemory
**  10/23/96    Changed XGetAndDetachResource to support our resource manager
**              if a file has been opened
**              Changed XFileClose to NULL out currentResourceFile when closing
**              file
**  11/7/96     Fixed a bug in XMicroseconds
**  11/8/96     Fixed bug in XStrCpy. Forgot to copy zero terminator
**  11/14/96    Added XGetResourceName & XGetNamedResource
**              Removed dependancy on MacSpecificSMS.h
**  11/26/96    Changed file read status native to MacOS from read/write
**              to read only
**  12/3/96     Removed C++ warnings on void * to char * conversion
**  12/4/96     Added XStrLen
**  12/15/96    Added X_SOLARIS
**  12/17/96    Fixed a bug in XBlockMove for WinOS.
**  12/18/96    Added all Solaris APIs
**  12/19/96    Added create flag in XFileOpenForWrite
**  12/30/96    Changed copyright
**  1/2/97      Added XCtoPstr & XPtoCstr
**  1/12/97     Renamed DecompressPtr to XDecompressPtr
**  1/13/97     Added XDuplicateStr
**  1/16/97     Fixed bug with XStrLen that added extra byte
**  1/20/97     Added XLStrCmp
**              Put in more support for MacOS native resource management
**  1/21/97     Added XLStrnCmp
**  1/23/97     Special cased XFixedMultiply to handle zero faster
**  1/24/97     Fixed XWaitMicroseocnds on WinOS to handle threads better
**  1/28/97     Changed XGetFileResource to not duplicate a resource if its
**              being loaded from memory
**              Added some Liberate Code
**              Fixed a bug with XIsOurMemoryPtr (Thanks Jeff!). Forgot to
**              get the data blocks in a platform way.
**              Added XGetIndexedResource
**  1/29/97     Fixed some platform bugs with XGetIndexedFileResource
**              Added XCompressPtr
**  2/4/97      Fixed XBlockMove for Liberate case. It was backwards
**  2/5/97      Added XFileOpenForReadFromMemory
**  2/6/97      Fixed a bug with XFileOpenResourceFromMemory that creates a cache,
**              its not needed for memory based resources
**  2/11/97     Modified XGetIndexedFileResource to support MacOS native resource manager
**  2/18/97     Fixed XFileOpenResource & XFileOpenResourceFromMemory to fail if file
**              is not a XFile resource
**  2/19/97     Added XStrStr
**  4/22/97     Fixed a bug with XGetPtrSize that referenced a NULL pointer
**
**  6/5/97      (Beatnik Editor Windows)
**              bvk - Changed the _creat call (in XFileOpenResource) to use the right 
**              flags based on this goofy Microsoft C++ 5.0 (_S_IREAD | _S_IWRITE 
**              instead of _O_RDWR).  Added an include (sys/stat.h).
**
**  6/12/97     bvk - Added XGetResourceNameOnly().  Copied/modified from other code.
**              Doesn't load the resource data, just checks resource headers.
**  6/13/97     bvk - Added XCountFileResourcesOfType(XFILE fileRef, long int theType);
**
**  6/19/97     bvk - Fixed bug in XAddFileResource.  Wasn't flipping the 'next' value,
**                  Fixed bug getting name in XGetResourceNameOnly().
**
**  6/22/97     bvk - Changed XGetPtrSize(XPTR data).  It used to innocently assume that
**                  it could get the handle for any data pointer to get the size.  That
**                  actually leaves a lot of room for error...
**
**  6/27/97     bvk - Added XDeleteFileResource.  This functions in two ways, either to delete
**                  the resource immediately, or to just mark it as a 'TRSH' resource.
**                  Then, trash collection occurs when XCleanResourceFile() is called.
**                  For either case, resource deletion fails if the file has been opened as
**                  read only.
**
**  7/02/97     bvk - Fixed bug in XGetResourceNameOnly().
**
**  7/03/97     bvk - Fixed bug in XCleanResourceFile() that was writing the # of resources wrong.
**
**  7/05/97     bvk - Modified the resource info caching system.  Read carefully:
**                  Originally, the resource cache was only created if the res
**                  file was opened ReadOnly.  Ostensibly, this was because the
**                  resource map cache would be out of date should a resource add
**                  or delete take place.
**
**                  Now, the cache is used for all types of files.  It is updated
**                  correctly should the res file have added or deleted resources.
**
**                  I also rewrote some frequently called routines to use the cache.
**
**                  I also disabled loading the pre-generated cache from the resource file.
**                  Too many of my files didn't have 'em (and none of these routines write it out!)
**
**  5/3/97      Fixed a few potential problems that the MOT compiler found
**  5/13/97     Fixed extra byte swap with XGetIndexedFileResource. Thanks Moe!
**  6/20/97     Changed XGetHardwareVolume & XSetHardwareVolume for MacOS
**              to support news names for CW 2.0
**  6/30/97     Fixed a bug in XCompressPtr that forgot the extra four bytes! Thanks Moe!
**  7/14/97     Put in special case to XFixedDivide to handle zero values better and faster.
**              Put in support for new X_PLATFORM type, called X_WIN_BAE
**  7/17/97     Removed XIsVirtualMemoryAvailable & XLockMemory & XUnlockMemory. Because
**              its assumed that all memory is locked.
**              Removed XSetHardwareSampleRate & XGetHardwareSampleRate
**  7/18/97     Added XFileSetLength
**  7/21/97     Changed XGetResourceName to call XGetResourceNameOnly.
**  7/30/97     Removed references to creat because its not ANSI, now using the ANSI way which
**              is to call open with O_CREAT flag.
**  8/13/97     Fixed bug in XDuplicateAndStripStr that didn't increment counter the right way
**  8/18/97     Changed X_WIN_BAE to USE_BAE_EXTERNAL_API
**  9/3/97      Wired XGetHardwareVolume & XSetHardwareVolume to BAE_EXTERNAL_API
**              Wrapped a few more functions around USE_CREATION_API
**  9/29/97     Changed XSetBit & XClearBit & XTestBit to be uint32_t rather than long
**  10/2/97     Fixed a bug with XGetFileResource when trying to load a memory based resource
**              it still continued searching after it found what it was looking for.
**              Fixed XGetIndexedFileResource to support memory based resource files without
**              duplicating memory
**  10/14/97    Fixed bug with XFileOpenForWrite that didn't allow creation of files.
**  10/18/97    Modified XDisposePtr to mark blocks deallocated
**  11/19/97    Modified XNewPtr to allocate memory for performance
**  12/16/97    Moe: removed compiler warnings
**  12/17/97    bvk - Modified PV_XGetNamedCacheEntry().  Here's the problem:
**              Memory resource files don't have caches.  So how does a function 
**              like XGetNamedResource(), that relies on PV_XGetNamedCacheEntry() 
**              work?  IT DOESN'T.
**              PV_XGetNamedCacheEntry() now will search the whole file if for the
**              named resource if it is a memory file!
**  12/18/97    bvk - Modified XGetAndDetachResource so that in the case of memory files,
**              it truly creates a new buffer (so that calls to dispose of the resource
**              don't fail/nuke the res file).
**  1/8/98      MOE: added XSwapLongsInAccessCache()
**  1/9/98      Added XFileDelete
**  1/21/98     Changed the functions XGetShort & XGetLong & XPutShort & XPutLong
**              into macros that fall out for Motorola order hardware
**  1/26/98     Added int32_t to various functions
**  1/31/98     Moved XPI_Memblock structures to X_API.h, and moved the function
**              XIsOurMemoryPtr to X_API.h
**  2/7/98      Changed XFIXED back to an uint32_t to fix broken content
**  2/9/98      Fixed bug with memory files and XFileClose in which the memory
**              block allocated outside of the resource file system was deallocated!
**  3/18/98     Added XIs8BitSupported
**  3/23/98     MOE: Changed _XGetShort() and _XGetLong() to accept const* pointers
**  3/23/98     MOE: Added _XGetShortIntel() and _XGetLongIntel()
**  3/23/98     MOE: Fixed big memory leak in XCompressPtr()
**  4/15/98     Removed the conditional compile for XGet/XPutXXX functions
**  4/20/98     Fixed a bug with XGetIndexedFileResource that return the wrong resource length
**              Fixed a alignment bug with Solaris only that caused memory to not be freed in
**              XIsOurMemoryPtr and XNewPtr. Did this by changing the size of the XPI_Memblock
**              structure to 16 bytes rather than 12.
**  4/27/98     Changed XCompressPtr to handle XCOMPRESSION_TYPE
**  4/27/98     MOE:  Changed XDecompressPtr() and XCompressPtr(),
**              eliminated XDecompressSampleFormatPtr()
**  5/7/98      Added XGetTempXFILENAME
**  5/12/98     Added some missing MacOS header files
**
**  6/5/98      Jim Nitchals RIP    1/15/62 - 6/5/98
**              I'm going to miss your irreverent humor. Your coding style and desire
**              to make things as fast as possible. Your collaboration behind this entire
**              codebase. Your absolute belief in creating the best possible relationships 
**              from honesty and integrity. Your ability to enjoy conversation. Your business 
**              savvy in understanding the big picture. Your gentleness. Your willingness 
**              to understand someone else's way of thinking. Your debates on the latest 
**              political issues. Your generosity. Your great mimicking of cartoon voices. 
**              Your friendship. - Steve Hales
**
**  6/18/98     Added XFileFreeResourceCache
**  7/1/98      Changed various API to use the new XResourceType and XLongResourceID
**  7/6/98      Fixed some type problems with XCompressPtr
**              Changed _XPutShort to pass a uint16_t rather than an uint32_t
**  7/10/98     Added XGetUniqueFileResourceID & XGetUniqueResourceID & XAddResource
**              Added XDeleteResource
**              Added XCountResourcesOfType
**              Added XCleanResource
**              Added XFileGetCurrentResourceFile
**  7/13/98     Fixed bug with XGetUniqueFileResourceID & XRandom
**  7/15/98     Fixed bug with XGetResourceName inwhich the name being returned was a
**              pascal string, rather than a C string
**  7/17/98     Fixed bug with XCleanResourceFile inwhich a errant buffer might be
**              deallocated twice
**  7/20/98     Fixed a problem with XConvertNativeFileToXFILENAME that didn't clear
**              the XFILENAME if there was a problem
**  9/10/98     Put XGetFileAsData into the common build code because BAE.cpp uses it,
**              and the new exporter API uses it.
**              Fixed XGetIndexedType & XCountTypes to handle a NULL file reference being passed
**              in meaning use from most recently opened file.
**  9/30/98     Added test to XNewPtr to fail if size passed in is zero.
**  11/11/98    Cleaned up some left over bark in XGetAndDetachResource. Renamed pName to pResourceName
**              because of a MacOS macro conflict.
**              Added XReadPartialFileResource
**              Removed unused macros XGet/Put and placed them back into
**              real functions
**  11/18/98    Fixed a bug with XFileClose in which a file that was closed
**              twice would crash rather than get ignored.
**  12/21/98    Fixed a memory leak in XCleanResourceFile
**  12/22/98    Added USE_FILE_CACHE to control cache usage
**              Modified XCleanResourceFile to delete the cache resource if there is one, because
**              it can become invalid after a clean or delete. Also modified XAddFileResource to
**              delete the file cache
**  1/5/98      Fixed a warning in XFileClose and changed copyrights
**  2/8/99      Added XLStrStr & XStrCat
**  2/21/99     Added XStripStr & XFileCreateResourceCache
**  3/1/99      Removed some warnings for BeOS compiles
**  3/16/99     MOE:  Changed XCompressPtr() to use new LZSS... parameters
**  3/25/99     MOE:  Added procData parameter to functions using XCompressStatusProc
**  5/13/99     Fixed bug with XDuplicateAndStripStr in which it stripped characters
**              outside the 0-32 range.
**  5/20/99     Changed XGetTempXFILENAME on Mac to create temp files in the trash.
**  5/26/99     MOE:  Added XResizePtr()
**  5/26/99     MOE:  Made XStrLen() more efficient and straightforward
**  5/28/99     MOE:  Changed XStrLen() & XEncryptedStrLen() to return long,
**              and accept const pointers
**  5/28/99     MOE:  Changed the string/memory manipulation functions
**              to accept const pointers
**  6/8/99      Removed references to USE_BAE_EXTERNAL_API. We now require BAE_API
**  7/13/99     Renamed HAE to BAE. Renamed BAEAudioMixer to BAEAudioOutput. Renamed BAEMidiDirect
**              to BAEMidiSynth. Renamed BAEMidiFile to BAEMidiSong. Renamed BAERMFFile to BAERmfSong.
**              Renamed BAErr to BAEResult. Renamed BAEMidiExternal to BAEMidiInput. Renamed BAEMod
**              to BAEModSong. Renamed BAEGroup to BAENoiseGroup. Renamed BAEReverbMode to BAEReverbType.
**              Renamed BAEAudioNoise to BAENoise.
**  7/27/99     Fixed misspelling in XWaitMicroseocnds
**  8/10/99     Changed XFileSetPositionRelative & XFileSetLength to return an int32_t
**  8/11/99     MOE: Changed XAddFileResource() to fix two problems:
**                  writing name was failing but XFileWrite()'s return value was ignored
**                  cacheItem.fileOffsetData was not being set right
**                      (this problem was masked by the fact that the built cache was never used)
**  8/11/99     MOE: Added XCtoPascalString()
**  8/11/99     MOE: Made XCtoPstr() safely handle strings longer than 255 characters
**  8/11/99     MOE:  Added XLongToStr()
**  8/17/99     Fixed bug in XDeleteFileResource in which if a cache is used and the resource is
**              not in the cache, it now will search the file and delete it.
**  8/19/99     Fixed XCleanResourceFile to not ignore an error returned from changing the length of
**              a file. Also fixed XDeleteFileResource to not ignore the error returned from calling
**              XCleanResourceFile.
**  8/28/99     Renamed resourceFileCount & openResourceFiles to g_resourceFileCount & g_openResourceFiles
**              to emphisize their global nature. This is not thread safe. If you need thread safe
**              resource access, only access the resource API's that you pass in an XFILE.
**              This is going to change.
**              Increased MAX_OPEN_XFILES to 10 open files at once. Removed obsolete macro USE_WIN32_FILE_IO.
**  9/1/99      Changed PV_CopyWithinFile to return an int32_t rather than a int.
**  9/8/99      MOE: Changed XResizePtr() to check for same-size reallocation
**  9/8/99      MOE: Changed XResizePtr() to not always attempt to reallocate large blocks
**              This is a hack to be taken out when BAE_ResizePointer() exists
**  9/8/99      MOE: Added XGetFileResourceName(),
**              XExistsResource(), XExistsFileResource(),
**              XMakeUniqueResourceID(), XMakeUniqueFileResourceID()
**              Changed XGetResourceName() to return whether it found a resource,
**              and changed it so that searching would stop if it finds a
**              resource with a NULL name.  (It had been falling through to
**              match a resource in a lower-level resource file if the higher
**              level resource had a NULL name.)
**  10/7/99     Fixed bugs in XGetResourceName in which pascal strings were not being
**              converted to C strings prior to returning. Also failed to handle
**              the Mac native resource case
**  10/9/99     Modified PV_AddResourceFileToOpenFiles & PV_RemoveResourceFileFromOpenFiles
**              to fail if called from different threads
**              Fixed bug in XFileOpenResource in which an attempt to open a resource left it
**              open and in the search change even though it failed the validation test
**              Fixed XGetResourceNameOnly to search through Mac resources if non found in XFILE
**              resources
**  10/20/99    MSD: moved X_PLATFORM dependent #includes to BAEBuildOptions_*.h
**  12/13/99    Modified XFileClose to call XFileFreeResourceCache to clear the RAM based cached.
**              Fixed bug in XAddFileResource in which the RAM based cache that is maintained when
**              adding new resources with PV_AddToAccessCache was not storing into the structure
**              in the native endian mode, so cache entries would become invalid unless you closed
**              the file and reopened it.
**  1/21/00     Fixed a few cast issues with XFILE and MacOS.
**              MSD: added XConvertPathToXFILENAME()
**  1/25/00     Modified XConvertPathToXFILENAME to not convert a path if its larger than 255
**              bytes. Pascal string limit for MacOS
**  2/4/2000    Changed copyright. We're Y2K compliant!
**  3/14/2000   Fixed XGetNamedResource to search through the file for a named resource
**              if the file cache system is not enabled.
**  3/15/2000   Fixed XGetNamedResource to work correctly under MacOS.
**  3/29/2000   jsc: XFileOpenResource was leaving file open on failure.
**  4/14/2000   jsc: moved MAX_OPEN_XFILES to X_API.h and increased from 10 to 100
**  5/2/2000    sh: Added XFileIsValidResourceFromName & XFileIsValidResource
**  7/07/2000   DS: Created Mutex "object" for future use -- currently ifdef'd out.
**  2000.07.14 AER  Due to collisions between XBankTokens discovered in the
**                  field, I've made XBankToken a struct with some added data.
**                  It should collide less now (although without a better
**                  GUID-style system, there's still the potential)
**                  Added functions to more easily create and read them
**  7/25/2000   sh  Removed PV_IsVirtualMemoryAvailable. It was MacOS specific
**                  and not used here anymore.
**  10/1/2000   sh  Changed XConvertPathToXFILENAME to call the internal c to
**                  pascal string function. Conflicts with MacOS X Carbon.
**  12/21/2000  sh  Forced a setting in XFileOpenResourceFromMemory. There's a bug
**                  when passing FALSE to allowMemCopy into this function. NOT FIXED.
**                  Needs to be looked into.
**  1/24/2002   sh  Removed XGetHardwareVolume/XSetHardwareVolume, they were
**                  duplicated by BAE_GetHardwareVolume/BAE_SetHardwareVolume
**                  Fixed declaration of XDuplicateMemory
**  5/2/2001    sh  Added cool Mac trick to XConvertPathToXFILENAME to convert unix/dos
**                  paths to Mac FSSpec. Also handles relative paths.
**  5/29/2001   sh  Added new debugging system with debug_message
**  7-16-2001   dcm Added uint32_t XSwapShortInLong(uint32_t value) to swap shorts in long
**  8/28/2001       Fixed massive memory leak in XGetAndDetachResource when using memory files.
**                  Was duplicating resource when it was not required.
**  1/24/2002   sh  Removed XGetHardwareVolume/XSetHardwareVolume, they were
**                  duplicated by BAE_GetHardwareVolume/BAE_SetHardwareVolume
**                  Fixed declaration of XDuplicateMemory
*/
/*****************************************************************************/

#include "X_API.h"
#include <stdint.h>
#include <limits.h>
#include "X_Formats.h"
#include "X_Assert.h"
#include "BAE_API.h"
#include <stdint.h>


#define DEBUG_PRINT_RESOURCE        0
#define USE_FILE_CACHE              0   // if 1, then file cache is enabled

// Buffer size used for intra-file copy/compaction operations when cleaning
// resource files. Original code referenced XFER_BUFFER_SIZE without a local
// definition (lost during prior refactors). 8 KB is a reasonable chunk size.
#ifndef XFER_BUFFER_SIZE
#define XFER_BUFFER_SIZE 8192
#endif

// Structures

// Variables
static int16_t    g_resourceFileCount = 0;                // number of open resource files
static XFILE        g_openResourceFiles[MAX_OPEN_XFILES];
/* Protects g_openResourceFiles[] / g_resourceFileCount. Readers snapshot under
 * the lock then scan unlocked so file I/O is not serialized. Mutators wait
 * (unlike the old static inUse flag, which silently skipped updates). */
static BAE_Mutex    g_openResourceFilesMutex = NULL;

static void PV_EnsureOpenResourceFilesMutex(void)
{
    if (g_openResourceFilesMutex == NULL)
    {
        BAE_Mutex created = NULL;

        if (BAE_NewMutex(&created, "openResourceFiles", (char *)__FILE__, __LINE__) != 0 &&
            created != NULL)
        {
            if (g_openResourceFilesMutex == NULL)
            {
                g_openResourceFilesMutex = created;
            }
            else
            {
                BAE_DestroyMutex(created);
            }
        }
    }
}

static void PV_LockOpenResourceFiles(void)
{
    PV_EnsureOpenResourceFilesMutex();
    if (g_openResourceFilesMutex)
    {
        BAE_AcquireMutex(g_openResourceFilesMutex);
    }
}

static void PV_UnlockOpenResourceFiles(void)
{
    if (g_openResourceFilesMutex)
    {
        BAE_ReleaseMutex(g_openResourceFilesMutex);
    }
}

/* Copy the open list under the lock. Caller may scan outFiles[] after unlock. */
static int16_t PV_SnapshotOpenResourceFiles(XFILE *outFiles, int16_t maxFiles)
{
    int16_t count;
    int16_t i;

    if (!outFiles || maxFiles <= 0)
    {
        return 0;
    }
    PV_LockOpenResourceFiles();
    count = g_resourceFileCount;
    if (count > maxFiles)
    {
        count = maxFiles;
    }
    for (i = 0; i < count; i++)
    {
        outFiles[i] = g_openResourceFiles[i];
    }
    PV_UnlockOpenResourceFiles();
    return count;
}

static XFILE PV_GetFrontOpenResourceFile(void)
{
    XFILE front = NULL;

    PV_LockOpenResourceFiles();
    if (g_resourceFileCount > 0)
    {
        front = g_openResourceFiles[0];
    }
    PV_UnlockOpenResourceFiles();
    return front;
}

#define ZMF_INST_BLOCK_MAGIC       FOUR_CHAR('Z','I','N','S')
#define ZMF_SONG_BLOCK_MAGIC       FOUR_CHAR('Z','S','N','G')
#define ZMF_BANK_BLOCK_MAGIC       FOUR_CHAR('Z','B','N','K')
#define ZMF_SND_HEADER_BLOCK_MAGIC FOUR_CHAR('Z','S','H','D')
#define ZMF_SND_PAYLOAD_REF_MAGIC  FOUR_CHAR('Z','S','R','F')
#define ZMF_INST_BLOCK_VERSION_V1  1u
#define ZMF_INST_BLOCK_VERSION_V2  2u
#define ZMF_SND_HEADER_BLOCK_VERSION 1u
#define ZMF_SND_PAYLOAD_REF_VERSION 1u

static bool PV_XFileValid(XFILE fileRef);
//#define ZMF_SND_PAYLOAD_REF16_VERSION 2u
static bool PV_IsSndResourceType(XResourceType resourceType)
{
    return (resourceType == ID_SND || resourceType == ID_CSND || resourceType == ID_ESND) ? TRUE : FALSE;
}

uint16_t PV_ReadBE16(unsigned char const *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t PV_ReadBE32(unsigned char const *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

uint16_t PV_ReadLE16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t PV_ReadLE32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void PV_WriteBE16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)((value >> 8) & 0xFFu);
    p[1] = (unsigned char)(value & 0xFFu);
}

static void PV_WriteBE32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)((value >> 24) & 0xFFu);
    p[1] = (unsigned char)((value >> 16) & 0xFFu);
    p[2] = (unsigned char)((value >> 8) & 0xFFu);
    p[3] = (unsigned char)(value & 0xFFu);
}

static bool PV_AppendBytes(XPTR *buffer, int32_t *size, int32_t *capacity, void const *src, int32_t srcSize)
{
    int32_t required;
    int32_t newCapacity;
    XPTR grown;

    if (!buffer || !size || !capacity || !src || srcSize < 0)
    {
        return FALSE;
    }
    required = *size + srcSize;
    if (required < 0)
    {
        return FALSE;
    }
    if (required > *capacity)
    {
        newCapacity = (*capacity > 0) ? *capacity : 256;
        while (newCapacity < required)
        {
            if (newCapacity > 0x3FFFFFFF)
            {
                return FALSE;
            }
            newCapacity *= 2;
        }
        grown = XResizePtr(*buffer, newCapacity);
        if (!grown)
        {
            return FALSE;
        }
        *buffer = grown;
        *capacity = newCapacity;
    }
    XBlockMove((void *)src, (unsigned char *)(*buffer) + *size, srcSize);
    *size = required;
    return TRUE;
}

static bool PV_ReadCachedResourceRaw(XFILE fileRef,
                                     XFILE_CACHED_ITEM const *item,
                                     char *outPascalName,
                                     XPTR *outData,
                                     int32_t *outDataSize)
{
    if (!PV_XFileValid(fileRef) || !item || !outData || !outDataSize)
    {
        return FALSE;
    }

    if (outPascalName)
    {
        outPascalName[0] = 0;
        if (item->fileOffsetName >= 0)
        {
            if (XFileSetPosition(fileRef, item->fileOffsetName) != 0 ||
                XFileRead(fileRef, &outPascalName[0], 1L) != 0)
            {
                return FALSE;
            }
            if ((unsigned char)outPascalName[0] > 0)
            {
                if (XFileRead(fileRef, &outPascalName[1], (int32_t)(unsigned char)outPascalName[0]) != 0)
                {
                    return FALSE;
                }
            }
        }
    }

    *outData = NULL;
    *outDataSize = item->resourceLength;
    if (item->resourceLength <= 0)
    {
        return TRUE;
    }

    *outData = XNewPtr(item->resourceLength);
    if (!*outData)
    {
        return FALSE;
    }
    if (XFileSetPosition(fileRef, item->fileOffsetData) != 0 ||
        XFileRead(fileRef, *outData, item->resourceLength) != 0)
    {
        XDisposePtr(*outData);
        *outData = NULL;
        return FALSE;
    }
    return TRUE;
}

static bool PV_IsZmfSndPayloadRef(XPTR data,
                                  int32_t dataSize,
                                  XLongResourceID resourceID,
                                  unsigned char **outPayload,
                                  int32_t *outPayloadSize)
{
    unsigned char *p;

    if (outPayload)
    {
        *outPayload = NULL;
    }
    if (outPayloadSize)
    {
        *outPayloadSize = 0;
    }
    if (!data || dataSize < 8)
    {
        if (data && dataSize >= 2 && (uint16_t)PV_ReadBE16((unsigned char *)data) == (uint16_t)resourceID)
        {
            if (outPayload)
            {
                *outPayload = (unsigned char *)data + 2;
            }
            if (outPayloadSize)
            {
                *outPayloadSize = dataSize - 2;
            }
            return TRUE;
        }
        return FALSE;
    }

    p = (unsigned char *)data;
    if (PV_ReadBE32(p) != (uint32_t)ZMF_SND_PAYLOAD_REF_MAGIC ||
        PV_ReadBE32(p + 4) != ZMF_SND_PAYLOAD_REF_VERSION)
    {
        if ((uint16_t)PV_ReadBE16(p) == (uint16_t)resourceID)
        {
            if (outPayload)
            {
                *outPayload = p + 2;
            }
            if (outPayloadSize)
            {
                *outPayloadSize = dataSize - 2;
            }
            return TRUE;
        }
        return FALSE;
    }

    if (outPayload)
    {
        *outPayload = p + 8;
    }
    if (outPayloadSize)
    {
        *outPayloadSize = dataSize - 8;
    }
    return TRUE;
}

static bool PV_ParseSndHeaderAndPayload(XPTR sndData,
                                        int32_t sndSize,
                                        int32_t *outHeaderBytes,
                                        int32_t *outPayloadOffset)
{
    unsigned char *p;
    int16_t soundFormat;
    int32_t headerOffset;
    int32_t headerSpan;
    int16_t numSynths;
    int16_t numCmds;
    int32_t numCmdPos;
    XSoundHeader *sndHeader;
    int32_t samplePtr;

    if (outHeaderBytes)
    {
        *outHeaderBytes = 0;
    }
    if (outPayloadOffset)
    {
        *outPayloadOffset = 0;
    }
    if (!sndData || sndSize < 4 || !outHeaderBytes || !outPayloadOffset)
    {
        return FALSE;
    }

    p = (unsigned char *)sndData;
    soundFormat = XGetShort((char *)p);
    if (soundFormat == XThirdSoundFormat)
    {
        headerSpan = 2 + ((int32_t)sizeof(XSoundHeader3) - 1);
        if (headerSpan < 0 || headerSpan > sndSize)
        {
            return FALSE;
        }
        *outHeaderBytes = headerSpan;
        *outPayloadOffset = headerSpan;
        return TRUE;
    }

    if (soundFormat != XFirstSoundFormat && soundFormat != XSecondSoundFormat)
    {
        return FALSE;
    }

    if (sndSize < 6)
    {
        return FALSE;
    }

    numSynths = 0;
    if (soundFormat == XFirstSoundFormat)
    {
        numSynths = XGetShort((char *)p + 2);
        if (numSynths < 0)
        {
            return FALSE;
        }
    }

    numCmdPos = 4 + (int32_t)numSynths * 6;
    if (numCmdPos < 0 || numCmdPos + 2 > sndSize)
    {
        return FALSE;
    }
    numCmds = XGetShort((char *)p + numCmdPos);
    if (numCmds < 0)
    {
        return FALSE;
    }

    headerOffset = 6 + (int32_t)numSynths * 6 + (int32_t)numCmds * 8;
    if (headerOffset < 0 || headerOffset + ((int32_t)sizeof(XSoundHeader) - 1) > sndSize)
    {
        return FALSE;
    }

    sndHeader = (XSoundHeader *)(p + headerOffset);
    samplePtr = (int32_t)XGetLong(&sndHeader->samplePtr);
    if (samplePtr != 0)
    {
        return FALSE;
    }

    switch ((int16_t)(sndHeader->encode & 0xFF))
    {
        case XStandardHeader:
            headerSpan = headerOffset + ((int32_t)sizeof(XSoundHeader) - 1);
            break;
        case XExtendedHeader:
            if (headerOffset + ((int32_t)sizeof(XExtSoundHeader) - 1) > sndSize)
            {
                return FALSE;
            }
            headerSpan = headerOffset + ((int32_t)sizeof(XExtSoundHeader) - 1);
            break;
        case XCompressedHeader:
            if (headerOffset + ((int32_t)sizeof(XCmpSoundHeader) - 1) > sndSize)
            {
                return FALSE;
            }
            headerSpan = headerOffset + ((int32_t)sizeof(XCmpSoundHeader) - 1);
            break;
        default:
            return FALSE;
    }

    if (headerSpan < 0 || headerSpan > sndSize)
    {
        return FALSE;
    }

    *outHeaderBytes = headerSpan;
    *outPayloadOffset = headerSpan;
    return TRUE;
}

static bool PV_GetResourceMapInfo(XFILE fileRef, int32_t *outMapID, int32_t *outVersion)
{
    XFILERESOURCEMAP map;
    int32_t savedPos;

    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }

    savedPos = XFileGetPosition(fileRef);
    if (XFileSetPosition(fileRef, 0L) != 0 ||
        XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        (void)XFileSetPosition(fileRef, savedPos);
        return FALSE;
    }
    (void)XFileSetPosition(fileRef, savedPos);

    if (outMapID)
    {
        *outMapID = (int32_t)XGetLong(&map.mapID);
    }
    if (outVersion)
    {
        *outVersion = (int32_t)XGetLong(&map.version);
    }
    return TRUE;
}

/* Virtual/Pack rebuilds open a fresh map with VERSION_FOR_ID (ZMF default v5).
 * Copy the source file's version so a prior content-based v5/v6 stamp survives. */
static void PV_ApplySourceMapVersionToPacked(XFILE srcFile, XPTR packedData, int32_t packedSize)
{
    int32_t srcMapID = 0;
    int32_t srcVersion = 0;
    XFILERESOURCEMAP dst;

    if (!packedData || packedSize < (int32_t)sizeof(dst))
    {
        return;
    }
    if (!PV_GetResourceMapInfo(srcFile, &srcMapID, &srcVersion))
    {
        return;
    }
    if (!XFILERESOURCE_VERSION_IS_VALID(srcVersion))
    {
        return;
    }
    XBlockMove(packedData, &dst, (int32_t)sizeof(dst));
    if ((int32_t)XGetLong(&dst.mapID) != srcMapID)
    {
        return;
    }
    XPutLong(&dst.version, (uint32_t)srcVersion);
    XBlockMove(&dst, packedData, (int32_t)sizeof(dst));
}

/* Decoded ZINS cache.
 *
 * Why: editor ResolveInstID / GetIndexed INST on zpatches was
 * O(N × ZINS_size) — each extract LZMA-decompressed once (cached) but still
 * XNewPtr+XBlockMove'd the *entire* decoded block, then copied one entry.
 * What: retain one decode; extractors parse in place and allocate only the
 * single INST/ALIAS payload (+ name). Flat INST/ALIAS peers still shadow
 * packed entries (XGetFileResource / GetIndexed prefer flat first).
 * Safe: XGetIndexedFileResource callers dispose the small entry pointer, not
 * the packed block. PackInst / Purge own a clone via PV_LoadZmfInstBlock. */
static XFILE s_zinsDecodedFile = NULL;
static XPTR s_zinsDecodedBlock = NULL;
static int32_t s_zinsDecodedSize = 0;

static void PV_ClearZmfInstBlockCache(XFILE fileRef)
{
    if (fileRef != NULL && s_zinsDecodedFile != fileRef)
    {
        return;
    }
    if (s_zinsDecodedBlock)
    {
        XDisposePtr(s_zinsDecodedBlock);
        s_zinsDecodedBlock = NULL;
    }
    s_zinsDecodedFile = NULL;
    s_zinsDecodedSize = 0;
}

/* Borrowed pointer into s_zinsDecodedBlock — do NOT XDisposePtr. */
static XPTR PV_EnsureZmfInstBlockCached(XFILE fileRef, int32_t *outSize)
{
    XLongResourceID blockID;
    int32_t blockSize;
    XPTR blockData;
    XPTR decoded;

    if (!outSize)
    {
        return NULL;
    }
    *outSize = 0;

    if (s_zinsDecodedFile == fileRef && s_zinsDecodedBlock && s_zinsDecodedSize > 0)
    {
        *outSize = s_zinsDecodedSize;
        return s_zinsDecodedBlock;
    }

    blockData = XGetIndexedFileResource(fileRef, ID_ZINS, &blockID, 0, NULL, &blockSize);
    if (!blockData || blockSize <= 0)
    {
        if (blockData)
        {
            XDisposePtr(blockData);
        }
        return NULL;
    }

    decoded = XDecompressPtr(blockData, (uint32_t)blockSize, TRUE);
    XDisposePtr(blockData);
    if (!decoded)
    {
        return NULL;
    }

    PV_ClearZmfInstBlockCache(NULL);
    s_zinsDecodedFile = fileRef;
    s_zinsDecodedBlock = decoded;
    s_zinsDecodedSize = XGetPtrSize(decoded);
    *outSize = s_zinsDecodedSize;
    return s_zinsDecodedBlock;
}

/* Owned full-block clone for PackInst / Purge (caller XDisposePtr). */
static XPTR PV_LoadZmfInstBlock(XFILE fileRef, int32_t *outSize)
{
    XPTR borrowed;
    XPTR copy;
    int32_t size;

    if (!outSize)
    {
        return NULL;
    }
    *outSize = 0;
    borrowed = PV_EnsureZmfInstBlockCached(fileRef, &size);
    if (!borrowed || size <= 0)
    {
        return NULL;
    }
    copy = XNewPtr(size);
    if (!copy)
    {
        return NULL;
    }
    XBlockMove(borrowed, copy, size);
    *outSize = size;
    return copy;
}

static bool PV_ParseZmfInstBlockHeader(XPTR block,
                                       int32_t blockSize,
                                       uint32_t *outVersion,
                                       uint32_t *outCount,
                                       unsigned char **outEntries,
                                       unsigned char **outEnd)
{
    unsigned char *p;

    if (!block || blockSize < 12 || !outVersion || !outCount || !outEntries || !outEnd)
    {
        return FALSE;
    }

    p = (unsigned char *)block;
    if (PV_ReadBE32(p) != (uint32_t)ZMF_INST_BLOCK_MAGIC)
    {
        return FALSE;
    }

    *outVersion = PV_ReadBE32(p + 4);
    if (*outVersion != ZMF_INST_BLOCK_VERSION_V1 && *outVersion != ZMF_INST_BLOCK_VERSION_V2)
    {
        return FALSE;
    }

    *outCount = PV_ReadBE32(p + 8);
    *outEntries = p + 12;
    *outEnd = p + blockSize;
    return TRUE;
}

static XPTR PV_GetResourceFromZmfInstBlockByTypeAndID(XFILE fileRef,
                                                      XResourceType resourceType,
                                                      XLongResourceID resourceID,
                                                      void *pResourceName,
                                                      int32_t *pReturnedResourceSize)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p;
    unsigned char *end;
    uint32_t count;
    uint32_t version;
    uint32_t i;

    /* Borrow retained decode — copy only the matching entry. */
    block = PV_EnsureZmfInstBlockCached(fileRef, &blockSize);
    if (!block)
    {
        return NULL;
    }
    if (!PV_ParseZmfInstBlockHeader(block, blockSize, &version, &count, &p, &end))
    {
        return NULL;
    }

    for (i = 0; i < count; ++i)
    {
        uint32_t type = (uint32_t)ID_INST;
        uint32_t id;
        uint16_t nameLen;
        uint32_t dataLen;
        unsigned char *namePtr;
        unsigned char *dataPtr;
        XPTR copy;
        uint16_t pNameLen;

        if (version >= ZMF_INST_BLOCK_VERSION_V2)
        {
            if (p + 4 > end)
            {
                return NULL;
            }
            type = PV_ReadBE32(p);
            p += 4;
        }

        if (p + 10 > end)
        {
            return NULL;
        }
        id = PV_ReadBE32(p);
        p += 4;
        nameLen = PV_ReadBE16(p);
        p += 2;
        dataLen = PV_ReadBE32(p);
        p += 4;
        if (p + nameLen > end)
        {
            return NULL;
        }
        namePtr = p;
        p += nameLen;
        if (p + dataLen > end)
        {
            return NULL;
        }
        dataPtr = p;

        if ((XResourceType)type == resourceType && (XLongResourceID)id == resourceID)
        {
            copy = XNewPtr((int32_t)dataLen);
            if (!copy)
            {
                return NULL;
            }
            XBlockMove(dataPtr, copy, (int32_t)dataLen);
            if (pReturnedResourceSize)
            {
                *pReturnedResourceSize = (int32_t)dataLen;
            }
            if (pResourceName)
            {
                pNameLen = (nameLen > 255u) ? 255u : nameLen;
                ((unsigned char *)pResourceName)[0] = (unsigned char)pNameLen;
                if (pNameLen > 0)
                {
                    XBlockMove(namePtr, (unsigned char *)pResourceName + 1, pNameLen);
                }
            }
            return copy;
        }

        p += dataLen;
    }

    return NULL;
}

static XPTR PV_GetIndexedResourceFromZmfInstBlockByType(XFILE fileRef,
                                                         XResourceType resourceType,
                                                         int32_t resourceIndex,
                                                         XLongResourceID *pReturnedID,
                                                         void *pResourceName,
                                                         int32_t *pReturnedResourceSize)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p;
    unsigned char *end;
    uint32_t count;
    uint32_t version;
    uint32_t i;
    int32_t found;

    block = PV_EnsureZmfInstBlockCached(fileRef, &blockSize);
    if (!block || resourceIndex < 0)
    {
        return NULL;
    }
    if (!PV_ParseZmfInstBlockHeader(block, blockSize, &version, &count, &p, &end))
    {
        return NULL;
    }

    found = 0;
    for (i = 0; i < count; ++i)
    {
        uint32_t type = (uint32_t)ID_INST;
        uint32_t id;
        uint16_t nameLen;
        uint32_t dataLen;
        unsigned char *namePtr;
        unsigned char *dataPtr;
        XPTR copy;
        uint16_t pNameLen;

        if (version >= ZMF_INST_BLOCK_VERSION_V2)
        {
            if (p + 4 > end)
            {
                return NULL;
            }
            type = PV_ReadBE32(p);
            p += 4;
        }

        if (p + 10 > end)
        {
            return NULL;
        }
        id = PV_ReadBE32(p);
        p += 4;
        nameLen = PV_ReadBE16(p);
        p += 2;
        dataLen = PV_ReadBE32(p);
        p += 4;
        if (p + nameLen > end)
        {
            return NULL;
        }
        namePtr = p;
        p += nameLen;
        if (p + dataLen > end)
        {
            return NULL;
        }
        dataPtr = p;

        if ((XResourceType)type == resourceType)
        {
            if (found == resourceIndex)
            {
                copy = XNewPtr((int32_t)dataLen);
                if (!copy)
                {
                    return NULL;
                }
                XBlockMove(dataPtr, copy, (int32_t)dataLen);
                if (pReturnedID)
                {
                    *pReturnedID = (XLongResourceID)id;
                }
                if (pReturnedResourceSize)
                {
                    *pReturnedResourceSize = (int32_t)dataLen;
                }
                if (pResourceName)
                {
                    pNameLen = (nameLen > 255u) ? 255u : nameLen;
                    ((unsigned char *)pResourceName)[0] = (unsigned char)pNameLen;
                    if (pNameLen > 0)
                    {
                        XBlockMove(namePtr, (unsigned char *)pResourceName + 1, pNameLen);
                    }
                }
                return copy;
            }
            found++;
        }

        p += dataLen;
    }

    return NULL;
}

static int32_t PV_CountResourcesInZmfInstBlockByType(XFILE fileRef, XResourceType resourceType)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p;
    unsigned char *end;
    uint32_t count;
    uint32_t version;
    uint32_t i;
    int32_t found;

    block = PV_EnsureZmfInstBlockCached(fileRef, &blockSize);
    if (!block)
    {
        return 0;
    }
    if (!PV_ParseZmfInstBlockHeader(block, blockSize, &version, &count, &p, &end))
    {
        return 0;
    }

    if (version == ZMF_INST_BLOCK_VERSION_V1)
    {
        int32_t v1Count = (resourceType == ID_INST) ? (int32_t)count : 0;
        return (v1Count < 0) ? 0 : v1Count;
    }

    found = 0;
    for (i = 0; i < count; ++i)
    {
        uint32_t type;
        uint16_t nameLen;
        uint32_t dataLen;

        if (p + 14 > end)
        {
            return 0;
        }
        type = PV_ReadBE32(p);
        p += 4;
        p += 4; /* id */
        nameLen = PV_ReadBE16(p);
        p += 2;
        dataLen = PV_ReadBE32(p);
        p += 4;

        if ((XResourceType)type == resourceType)
        {
            found++;
        }
        if (p + nameLen > end)
        {
            return 0;
        }
        p += nameLen;
        if (p + dataLen > end)
        {
            return 0;
        }
        p += dataLen;
    }

    return found;
}

static XPTR PV_GetInstFromZmfBlockByID(XFILE fileRef,
                                       XLongResourceID resourceID,
                                       void *pResourceName,
                                       int32_t *pReturnedResourceSize)
{
    return PV_GetResourceFromZmfInstBlockByTypeAndID(fileRef,
                                                     ID_INST,
                                                     resourceID,
                                                     pResourceName,
                                                     pReturnedResourceSize);
}

static XPTR PV_GetIndexedInstFromZmfBlock(XFILE fileRef,
                                          int32_t resourceIndex,
                                          XLongResourceID *pReturnedID,
                                          void *pResourceName,
                                          int32_t *pReturnedResourceSize)
{
    return PV_GetIndexedResourceFromZmfInstBlockByType(fileRef,
                                                       ID_INST,
                                                       resourceIndex,
                                                       pReturnedID,
                                                       pResourceName,
                                                       pReturnedResourceSize);
}

static int32_t PV_CountInstsInZmfBlock(XFILE fileRef)
{
    return PV_CountResourcesInZmfInstBlockByType(fileRef, ID_INST);
}

static XPTR PV_GetAliasFromZmfInstBlockByID(XFILE fileRef,
                                            XLongResourceID resourceID,
                                            void *pResourceName,
                                            int32_t *pReturnedResourceSize)
{
    return PV_GetResourceFromZmfInstBlockByTypeAndID(fileRef,
                                                     ID_ALIAS,
                                                     resourceID,
                                                     pResourceName,
                                                     pReturnedResourceSize);
}

static XPTR PV_GetIndexedAliasFromZmfInstBlock(XFILE fileRef,
                                                int32_t resourceIndex,
                                                XLongResourceID *pReturnedID,
                                                void *pResourceName,
                                                int32_t *pReturnedResourceSize)
{
    return PV_GetIndexedResourceFromZmfInstBlockByType(fileRef,
                                                       ID_ALIAS,
                                                       resourceIndex,
                                                       pReturnedID,
                                                       pResourceName,
                                                       pReturnedResourceSize);
}

static int32_t PV_CountAliasesInZmfBlock(XFILE fileRef)
{
    return PV_CountResourcesInZmfInstBlockByType(fileRef, ID_ALIAS);
}

/* Flat (cache) INST/ALIAS only — does not look inside ZINS. */
static bool PV_CacheHasResourceTypeID(XFILE fileRef,
                                      XResourceType resourceType,
                                      XLongResourceID resourceID)
{
    XFILENAME *srcRef;
    int32_t i;

    srcRef = (XFILENAME *)fileRef;
    if (!srcRef || !srcRef->pCache)
    {
        return FALSE;
    }
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        if ((XResourceType)srcRef->pCache->cached[i].resourceType == resourceType &&
            (XLongResourceID)srcRef->pCache->cached[i].resourceID == resourceID)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static bool PV_AppendInstEntryToZmfPlain(XPTR *plainBlock,
                                         int32_t *plainSize,
                                         int32_t *plainCapacity,
                                         int64_t *originalStoredBytes,
                                         XResourceType rtype,
                                         XLongResourceID resourceID,
                                         const char *pName,
                                         XPTR resourceData,
                                         int32_t resourceSize)
{
    uint16_t nameLen;
    unsigned char eh[14];

    if (!plainBlock || !plainSize || !plainCapacity || !resourceData || resourceSize <= 0)
    {
        return FALSE;
    }
    nameLen = pName ? (uint16_t)(uint8_t)pName[0] : 0;
    if (originalStoredBytes)
    {
        *originalStoredBytes += (int64_t)17 + (int64_t)nameLen + (int64_t)resourceSize;
    }
    PV_WriteBE32(eh + 0, (uint32_t)rtype);
    PV_WriteBE32(eh + 4, (uint32_t)resourceID);
    PV_WriteBE16(eh + 8, nameLen);
    PV_WriteBE32(eh + 10, (uint32_t)resourceSize);
    if (!PV_AppendBytes(plainBlock, plainSize, plainCapacity, eh, 14) ||
        (nameLen > 0 && !PV_AppendBytes(plainBlock, plainSize, plainCapacity, pName + 1, nameLen)) ||
        !PV_AppendBytes(plainBlock, plainSize, plainCapacity, resourceData, resourceSize))
    {
        return FALSE;
    }
    return TRUE;
}

static bool PV_PackInstResourcesIntoZmfBlock(XFILE fileRef)
{
    int32_t mapID;
    int32_t packedCount;
    int32_t flatCount;
    int32_t i;
    XPTR plainBlock;
    int32_t plainSize;
    int32_t plainCapacity;
    XPTR compressedBlock;
    int32_t compressedSize;
    XFILE outFile;
    XPTR packedData;
    int32_t packedSize;
    XFILENAME *srcRef;
    int64_t originalStoredBytes;
    XPTR existingZins;
    int32_t existingZinsSize;
    uint32_t existingVersion;
    uint32_t existingCount;
    unsigned char *existingEntries;
    unsigned char *existingEnd;

    if (!PV_GetResourceMapInfo(fileRef, &mapID, NULL))
    {
        return FALSE;
    }
    if (mapID != XFILERESOURCE_ZMF_ID)
    {
        return TRUE;
    }

    srcRef = (XFILENAME *)fileRef;
    if (srcRef->pCache == NULL)
    {
        XCreateAccessCache(fileRef);
    }
    if (!srcRef->pCache)
    {
        return FALSE;
    }
    flatCount = 0;
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XResourceType rtype = (XResourceType)srcRef->pCache->cached[i].resourceType;
        if (rtype == ID_INST || rtype == ID_ALIAS)
        {
            flatCount++;
        }
    }

    /* Merge existing ZINS with flat INST/ALIAS. Flat wins on id conflict.
     * Without this, adding one custom INST then Clean replaces ZINS and
     * permanently drops every packed instrument. */
    existingZins = PV_LoadZmfInstBlock(fileRef, &existingZinsSize);
    existingVersion = 0;
    existingCount = 0;
    existingEntries = NULL;
    existingEnd = NULL;
    if (existingZins &&
        !PV_ParseZmfInstBlockHeader(existingZins,
                                    existingZinsSize,
                                    &existingVersion,
                                    &existingCount,
                                    &existingEntries,
                                    &existingEnd))
    {
        XDisposePtr(existingZins);
        existingZins = NULL;
        existingCount = 0;
    }

    packedCount = flatCount;
    if (existingZins && existingCount > 0)
    {
        unsigned char *p = existingEntries;
        for (i = 0; i < (int32_t)existingCount; ++i)
        {
            uint32_t type = (uint32_t)ID_INST;
            uint32_t id;
            uint16_t nameLen;
            uint32_t dataLen;

            if (existingVersion >= ZMF_INST_BLOCK_VERSION_V2)
            {
                if (p + 4 > existingEnd)
                {
                    break;
                }
                type = PV_ReadBE32(p);
                p += 4;
            }
            if (p + 10 > existingEnd)
            {
                break;
            }
            id = PV_ReadBE32(p);
            p += 4;
            nameLen = PV_ReadBE16(p);
            p += 2;
            dataLen = PV_ReadBE32(p);
            p += 4;
            if (p + nameLen + dataLen > existingEnd)
            {
                break;
            }
            p += nameLen + dataLen;
            if ((type == (uint32_t)ID_INST || type == (uint32_t)ID_ALIAS) &&
                !PV_CacheHasResourceTypeID(fileRef, (XResourceType)type, (XLongResourceID)id))
            {
                packedCount++;
            }
        }
    }

    if (packedCount <= 0)
    {
        if (existingZins)
        {
            XDisposePtr(existingZins);
        }
        return TRUE;
    }

    plainBlock = NULL;
    plainSize = 0;
    plainCapacity = 0;
    originalStoredBytes = 0;
    {
        unsigned char header[12];
        PV_WriteBE32(header + 0, (uint32_t)ZMF_INST_BLOCK_MAGIC);
        PV_WriteBE32(header + 4, ZMF_INST_BLOCK_VERSION_V2);
        PV_WriteBE32(header + 8, (uint32_t)packedCount);
        if (!PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, header, 12))
        {
            if (existingZins)
            {
                XDisposePtr(existingZins);
            }
            return FALSE;
        }
    }

    /* 1) Carry forward ZINS entries not overridden by a flat resource. */
    if (existingZins && existingCount > 0)
    {
        unsigned char *p = existingEntries;
        for (i = 0; i < (int32_t)existingCount; ++i)
        {
            uint32_t type = (uint32_t)ID_INST;
            uint32_t id;
            uint16_t nameLen;
            uint32_t dataLen;
            unsigned char *namePtr;
            unsigned char *dataPtr;
            char pName[256];

            if (existingVersion >= ZMF_INST_BLOCK_VERSION_V2)
            {
                if (p + 4 > existingEnd)
                {
                    break;
                }
                type = PV_ReadBE32(p);
                p += 4;
            }
            if (p + 10 > existingEnd)
            {
                break;
            }
            id = PV_ReadBE32(p);
            p += 4;
            nameLen = PV_ReadBE16(p);
            p += 2;
            dataLen = PV_ReadBE32(p);
            p += 4;
            if (p + nameLen + dataLen > existingEnd)
            {
                break;
            }
            namePtr = p;
            p += nameLen;
            dataPtr = p;
            p += dataLen;

            if (type != (uint32_t)ID_INST && type != (uint32_t)ID_ALIAS)
            {
                continue;
            }
            if (PV_CacheHasResourceTypeID(fileRef, (XResourceType)type, (XLongResourceID)id))
            {
                continue; /* flat wins */
            }
            pName[0] = (char)((nameLen > 255u) ? 255u : nameLen);
            if (pName[0] > 0)
            {
                XBlockMove(namePtr, pName + 1, (int32_t)(uint8_t)pName[0]);
            }
            if (!PV_AppendInstEntryToZmfPlain(&plainBlock,
                                              &plainSize,
                                              &plainCapacity,
                                              &originalStoredBytes,
                                              (XResourceType)type,
                                              (XLongResourceID)id,
                                              pName,
                                              dataPtr,
                                              (int32_t)dataLen))
            {
                XDisposePtr(plainBlock);
                XDisposePtr(existingZins);
                return FALSE;
            }
        }
    }
    if (existingZins)
    {
        XDisposePtr(existingZins);
    }

    /* 2) Append flat INST/ALIAS (overrides). Raw cache read — XGetFileResource
     * has failed for some flat custom INST and skipping them produced a ZINS
     * with only Bank 0/1 (session save lost Bank 2+/customs). */
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item;
        XResourceType rtype;
        XLongResourceID resourceID;
        int32_t resourceSize;
        XPTR resourceData;
        char pName[256];

        item = &srcRef->pCache->cached[i];
        rtype = (XResourceType)item->resourceType;
        if (rtype != ID_INST && rtype != ID_ALIAS)
        {
            continue;
        }

        resourceID = (XLongResourceID)item->resourceID;
        pName[0] = 0;
        resourceData = NULL;
        resourceSize = 0;
        if (!PV_ReadCachedResourceRaw(fileRef, item, pName, &resourceData, &resourceSize) ||
            !resourceData || resourceSize <= 0)
        {
            /* Abort pack — leave flat customs + existing ZINS untouched. */
            if (resourceData)
            {
                XDisposePtr(resourceData);
            }
            XDisposePtr(plainBlock);
            return TRUE;
        }

        if (!PV_AppendInstEntryToZmfPlain(&plainBlock,
                                          &plainSize,
                                          &plainCapacity,
                                          &originalStoredBytes,
                                          rtype,
                                          resourceID,
                                          pName,
                                          resourceData,
                                          resourceSize))
        {
            XDisposePtr(resourceData);
            XDisposePtr(plainBlock);
            return FALSE;
        }
        XDisposePtr(resourceData);
    }

#if USE_LZMA_COMPRESSION == TRUE
    compressedSize = XCompressPtr(&compressedBlock,
                                  plainBlock,
                                  (uint32_t)plainSize,
                                  X_LZMA_RAW,
                                  NULL,
                                  NULL);
#else
    compressedBlock = XNewPtr(plainSize);
    compressedSize = 0;
    if (compressedBlock)
    {
        XBlockMove(plainBlock, compressedBlock, plainSize);
        compressedSize = plainSize;
    }
#endif
    XDisposePtr(plainBlock);
    if (!compressedBlock || compressedSize <= 0)
    {
        if (compressedBlock) XDisposePtr(compressedBlock);
        return FALSE;
    }

    if (((int64_t)17 + (int64_t)compressedSize) >= originalStoredBytes)
    {
        XDisposePtr(compressedBlock);
        return TRUE;
    }

    outFile = XFileOpenVirtualResource(XFILERESOURCE_ZMF_ID);
    if (!outFile)
    {
        XDisposePtr(compressedBlock);
        return FALSE;
    }

    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item;
        XPTR data;
        int32_t size;
        char name[256];

        item = &srcRef->pCache->cached[i];
        if (item->resourceType == ID_INST || item->resourceType == ID_ALIAS ||
            item->resourceType == ID_ZINS ||
            item->resourceType == XFILEGONE_ID)
        {
            continue;
        }
        /* Raw copy — don't expand ZSHD payload-refs via XGetFileResource.
         * TRSH entries are preserved so editor trash survives Clean/pack.
         * GONE (replace debris) is dropped. */
        name[0] = 0;
        data = NULL;
        size = 0;
        if (!PV_ReadCachedResourceRaw(fileRef, item, name, &data, &size) || !data)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            continue;
        }
        if (XAddFileResource(outFile,
                             (XResourceType)item->resourceType,
                             (XLongResourceID)item->resourceID,
                             name,
                             data,
                             size) != 0)
        {
            XDisposePtr(data);
            XDisposePtr(compressedBlock);
            XFileClose(outFile);
            return FALSE;
        }
        XDisposePtr(data);
    }

    PV_ClearZmfInstBlockCache(outFile);
    PV_ClearZmfInstBlockCache(fileRef);
    if (XAddFileResource(outFile, ID_ZINS, 1, NULL, compressedBlock, compressedSize) != 0)
    {
        XDisposePtr(compressedBlock);
        XFileClose(outFile);
        return FALSE;
    }
    XDisposePtr(compressedBlock);

    /* Temp already has new ZINS; do not re-PackInst/PackSnd. */
    if (XCleanResourceFileOptions(outFile, FALSE, FALSE) == FALSE)
    {
        XFileClose(outFile);
        return FALSE;
    }
    packedData = NULL;
    packedSize = 0;
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 || !packedData || packedSize <= 0)
    {
        if (packedData) XDisposePtr(packedData);
        XFileClose(outFile);
        return FALSE;
    }
    XFileClose(outFile);

    PV_ApplySourceMapVersionToPacked(fileRef, packedData, packedSize);
    XFileFreeResourceCache(fileRef);
    {
        XFILENAME *pReference = (XFILENAME *)fileRef;
        if (pReference->pResourceData)
        {
            if (pReference->ownsResourceData)
            {
                XDisposePtr(pReference->pResourceData);
            }
            pReference->pResourceData = packedData;
            pReference->resMemLength = packedSize;
            pReference->resMemCapacity = packedSize;
            pReference->resMemOffset = 0;
            pReference->ownsResourceData = TRUE;
        }
        else
        {
            if (XFileSetLength(fileRef, 0) != 0 ||
                XFileSetPosition(fileRef, 0L) != 0 ||
                XFileWrite(fileRef, packedData, packedSize) != 0)
            {
                XDisposePtr(packedData);
                return FALSE;
            }
            XDisposePtr(packedData);
        }
    }

    return TRUE;
}

static XPTR PV_LoadZmfSongBlock(XFILE fileRef, int32_t *outSize)
{
    XLongResourceID blockID;
    int32_t blockSize;
    XPTR blockData;
    XPTR decoded;

    if (!outSize)
    {
        return NULL;
    }
    *outSize = 0;

    blockData = XGetIndexedFileResource(fileRef, ID_ZSNG, &blockID, 0, NULL, &blockSize);
    if (!blockData || blockSize <= 0)
    {
        if (blockData)
        {
            XDisposePtr(blockData);
        }
        return NULL;
    }

    decoded = XDecompressPtr(blockData, (uint32_t)blockSize, TRUE);
    XDisposePtr(blockData);
    if (!decoded)
    {
        return NULL;
    }
    *outSize = XGetPtrSize(decoded);
    return decoded;
}

static XPTR PV_GetSongFromZmfBlockByID(XFILE fileRef,
                                       XLongResourceID resourceID,
                                       void *pResourceName,
                                       int32_t *pReturnedResourceSize)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p;
    unsigned char *end;
    uint32_t count;
    uint32_t i;

    block = PV_LoadZmfSongBlock(fileRef, &blockSize);
    if (!block || blockSize < 12)
    {
        if (block) XDisposePtr(block);
        return NULL;
    }

    p = (unsigned char *)block;
    end = p + blockSize;
    if (PV_ReadBE32(p) != (uint32_t)ZMF_SONG_BLOCK_MAGIC || PV_ReadBE32(p + 4) != 1u)
    {
        XDisposePtr(block);
        return NULL;
    }
    count = PV_ReadBE32(p + 8);
    p += 12;

    for (i = 0; i < count; ++i)
    {
        uint32_t id;
        uint16_t nameLen;
        uint32_t dataLen;
        unsigned char *namePtr;
        unsigned char *dataPtr;
        XPTR copy;
        uint16_t pNameLen;

        if (p + 10 > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        id = PV_ReadBE32(p);
        p += 4;
        nameLen = PV_ReadBE16(p);
        p += 2;
        dataLen = PV_ReadBE32(p);
        p += 4;
        if (p + nameLen > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        namePtr = p;
        p += nameLen;
        if (p + dataLen > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        dataPtr = p;

        if ((XLongResourceID)id == resourceID)
        {
            copy = XNewPtr((int32_t)dataLen);
            if (!copy)
            {
                XDisposePtr(block);
                return NULL;
            }
            XBlockMove(dataPtr, copy, (int32_t)dataLen);
            if (pReturnedResourceSize)
            {
                *pReturnedResourceSize = (int32_t)dataLen;
            }
            if (pResourceName)
            {
                pNameLen = (nameLen > 255u) ? 255u : nameLen;
                ((unsigned char *)pResourceName)[0] = (unsigned char)pNameLen;
                if (pNameLen > 0)
                {
                    XBlockMove(namePtr, (unsigned char *)pResourceName + 1, pNameLen);
                }
            }
            XDisposePtr(block);
            return copy;
        }

        p += dataLen;
    }

    XDisposePtr(block);
    return NULL;
}

static XPTR PV_GetIndexedSongFromZmfBlock(XFILE fileRef,
                                          int32_t resourceIndex,
                                          XLongResourceID *pReturnedID,
                                          void *pResourceName,
                                          int32_t *pReturnedResourceSize)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p;
    unsigned char *end;
    uint32_t count;
    uint32_t i;

    block = PV_LoadZmfSongBlock(fileRef, &blockSize);
    if (!block || blockSize < 12)
    {
        if (block) XDisposePtr(block);
        return NULL;
    }

    p = (unsigned char *)block;
    end = p + blockSize;
    if (PV_ReadBE32(p) != (uint32_t)ZMF_SONG_BLOCK_MAGIC || PV_ReadBE32(p + 4) != 1u)
    {
        XDisposePtr(block);
        return NULL;
    }
    count = PV_ReadBE32(p + 8);
    if (resourceIndex < 0 || (uint32_t)resourceIndex >= count)
    {
        XDisposePtr(block);
        return NULL;
    }
    p += 12;

    for (i = 0; i < count; ++i)
    {
        uint32_t id;
        uint16_t nameLen;
        uint32_t dataLen;
        unsigned char *namePtr;
        unsigned char *dataPtr;
        XPTR copy;
        uint16_t pNameLen;

        if (p + 10 > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        id = PV_ReadBE32(p);
        p += 4;
        nameLen = PV_ReadBE16(p);
        p += 2;
        dataLen = PV_ReadBE32(p);
        p += 4;
        if (p + nameLen > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        namePtr = p;
        p += nameLen;
        if (p + dataLen > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        dataPtr = p;

        if ((int32_t)i == resourceIndex)
        {
            copy = XNewPtr((int32_t)dataLen);
            if (!copy)
            {
                XDisposePtr(block);
                return NULL;
            }
            XBlockMove(dataPtr, copy, (int32_t)dataLen);
            if (pReturnedID)
            {
                *pReturnedID = (XLongResourceID)id;
            }
            if (pReturnedResourceSize)
            {
                *pReturnedResourceSize = (int32_t)dataLen;
            }
            if (pResourceName)
            {
                pNameLen = (nameLen > 255u) ? 255u : nameLen;
                ((unsigned char *)pResourceName)[0] = (unsigned char)pNameLen;
                if (pNameLen > 0)
                {
                    XBlockMove(namePtr, (unsigned char *)pResourceName + 1, pNameLen);
                }
            }
            XDisposePtr(block);
            return copy;
        }

        p += dataLen;
    }

    XDisposePtr(block);
    return NULL;
}

static int32_t PV_CountSongsInZmfBlock(XFILE fileRef)
{
    XPTR block;
    int32_t blockSize;

    block = PV_LoadZmfSongBlock(fileRef, &blockSize);
    if (!block || blockSize < 12)
    {
        if (block) XDisposePtr(block);
        return 0;
    }
    if (PV_ReadBE32((unsigned char *)block) != (uint32_t)ZMF_SONG_BLOCK_MAGIC ||
        PV_ReadBE32((unsigned char *)block + 4) != 1u)
    {
        XDisposePtr(block);
        return 0;
    }
    {
        int32_t count = (int32_t)PV_ReadBE32((unsigned char *)block + 8);
        XDisposePtr(block);
        return (count < 0) ? 0 : count;
    }
}

static bool PV_PackSongResourcesIntoZmfBlock(XFILE fileRef)
{
    int32_t mapID;
    int32_t songCount;
    int32_t i;
    XPTR plainBlock;
    int32_t plainSize;
    int32_t plainCapacity;
    XPTR compressedBlock;
    int32_t compressedSize;
    XFILE outFile;
    XPTR packedData;
    int32_t packedSize;
    XFILENAME *srcRef;
    int64_t originalStoredBytes;

    if (!PV_GetResourceMapInfo(fileRef, &mapID, NULL))
    {
        return FALSE;
    }
    if (mapID != XFILERESOURCE_ZMF_ID)
    {
        return TRUE;
    }

    srcRef = (XFILENAME *)fileRef;
    if (srcRef->pCache == NULL)
    {
        XCreateAccessCache(fileRef);
    }
    if (!srcRef->pCache)
    {
        return FALSE;
    }
    songCount = 0;
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        if (srcRef->pCache->cached[i].resourceType == ID_SONG)
        {
            songCount++;
        }
    }
    if (songCount <= 0)
    {
        return TRUE;
    }

    plainBlock = NULL;
    plainSize = 0;
    plainCapacity = 0;
    originalStoredBytes = 0;
    {
        unsigned char header[12];
        PV_WriteBE32(header + 0, (uint32_t)ZMF_SONG_BLOCK_MAGIC);
        PV_WriteBE32(header + 4, 1u);
        PV_WriteBE32(header + 8, (uint32_t)songCount);
        if (!PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, header, 12))
        {
            return FALSE;
        }
    }

    for (i = 0; i < songCount; ++i)
    {
        XLongResourceID songID;
        int32_t songSize;
        XPTR songData;
        char pName[256];
        uint16_t nameLen;
        unsigned char eh[10];

        pName[0] = 0;
        songData = XGetIndexedFileResource(fileRef, ID_SONG, &songID, i, pName, &songSize);
        if (!songData || songSize <= 0)
        {
            if (songData) XDisposePtr(songData);
            XDisposePtr(plainBlock);
            return FALSE;
        }

        nameLen = (uint8_t)pName[0];
        originalStoredBytes += (int64_t)17 + (int64_t)nameLen + (int64_t)songSize;
        PV_WriteBE32(eh + 0, (uint32_t)songID);
        PV_WriteBE16(eh + 4, nameLen);
        PV_WriteBE32(eh + 6, (uint32_t)songSize);
        if (!PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, eh, 10) ||
            (nameLen > 0 && !PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, pName + 1, nameLen)) ||
            !PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, songData, songSize))
        {
            XDisposePtr(songData);
            XDisposePtr(plainBlock);
            return FALSE;
        }
        XDisposePtr(songData);
    }

#if USE_LZMA_COMPRESSION == TRUE
    compressedSize = XCompressPtr(&compressedBlock,
                                  plainBlock,
                                  (uint32_t)plainSize,
                                  X_LZMA_RAW,
                                  NULL,
                                  NULL);
#else
    compressedBlock = XNewPtr(plainSize);
    compressedSize = 0;
    if (compressedBlock)
    {
        XBlockMove(plainBlock, compressedBlock, plainSize);
        compressedSize = plainSize;
    }
#endif
    XDisposePtr(plainBlock);
    if (!compressedBlock || compressedSize <= 0)
    {
        if (compressedBlock) XDisposePtr(compressedBlock);
        return FALSE;
    }

    if (((int64_t)17 + (int64_t)compressedSize) >= originalStoredBytes)
    {
        XDisposePtr(compressedBlock);
        return TRUE;
    }

    outFile = XFileOpenVirtualResource(XFILERESOURCE_ZMF_ID);
    if (!outFile)
    {
        XDisposePtr(compressedBlock);
        return FALSE;
    }

    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item;
        XPTR data;
        int32_t size;
        char name[256];

        item = &srcRef->pCache->cached[i];
        if (item->resourceType == ID_SONG || item->resourceType == ID_ZSNG ||
            item->resourceType == XFILEGONE_ID)
        {
            continue;
        }
        name[0] = 0;
        data = NULL;
        size = 0;
        if (!PV_ReadCachedResourceRaw(fileRef, item, name, &data, &size) || !data)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            continue;
        }
        if (XAddFileResource(outFile,
                             (XResourceType)item->resourceType,
                             (XLongResourceID)item->resourceID,
                             name,
                             data,
                             size) != 0)
        {
            XDisposePtr(data);
            XDisposePtr(compressedBlock);
            XFileClose(outFile);
            return FALSE;
        }
        XDisposePtr(data);
    }

    if (XAddFileResource(outFile, ID_ZSNG, 1, NULL, compressedBlock, compressedSize) != 0)
    {
        XDisposePtr(compressedBlock);
        XFileClose(outFile);
        return FALSE;
    }
    XDisposePtr(compressedBlock);

    /* Keep flat INST/ZINS as copied — do not PackInst on this temp. */
    if (XCleanResourceFileOptions(outFile, FALSE, FALSE) == FALSE)
    {
        XFileClose(outFile);
        return FALSE;
    }
    packedData = NULL;
    packedSize = 0;
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 || !packedData || packedSize <= 0)
    {
        if (packedData) XDisposePtr(packedData);
        XFileClose(outFile);
        return FALSE;
    }
    XFileClose(outFile);

    PV_ApplySourceMapVersionToPacked(fileRef, packedData, packedSize);
    XFileFreeResourceCache(fileRef);
    {
        XFILENAME *pReference = (XFILENAME *)fileRef;
        if (pReference->pResourceData)
        {
            if (pReference->ownsResourceData)
            {
                XDisposePtr(pReference->pResourceData);
            }
            pReference->pResourceData = packedData;
            pReference->resMemLength = packedSize;
            pReference->resMemCapacity = packedSize;
            pReference->resMemOffset = 0;
            pReference->ownsResourceData = TRUE;
        }
        else
        {
            if (XFileSetLength(fileRef, 0) != 0 ||
                XFileSetPosition(fileRef, 0L) != 0 ||
                XFileWrite(fileRef, packedData, packedSize) != 0)
            {
                XDisposePtr(packedData);
                return FALSE;
            }
            XDisposePtr(packedData);
        }
    }

    return TRUE;
}

// ---- ZBNK block: typed LZMA-compressed container for BANK and MIDI resources ----

static XPTR PV_LoadZmfBankBlock(XFILE fileRef, int32_t *outSize)
{
    XLongResourceID blockID;
    int32_t blockSize;
    XPTR blockData;
    XPTR decoded;

    if (!outSize)
        return NULL;
    *outSize = 0;

    blockData = XGetIndexedFileResource(fileRef, ID_ZBNK, &blockID, 0, NULL, &blockSize);
    if (!blockData || blockSize <= 0)
    {
        if (blockData) XDisposePtr(blockData);
        return NULL;
    }

    decoded = XDecompressPtr(blockData, (uint32_t)blockSize, TRUE);
    XDisposePtr(blockData);
    if (!decoded)
        return NULL;
    *outSize = XGetPtrSize(decoded);
    return decoded;
}

static XPTR PV_GetResourceFromZmfBankBlockByTypeAndID(XFILE fileRef,
                                                       XResourceType resourceType,
                                                       XLongResourceID resourceID,
                                                       void *pResourceName,
                                                       int32_t *pReturnedResourceSize)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p, *end;
    uint32_t count, i;

    block = PV_LoadZmfBankBlock(fileRef, &blockSize);
    if (!block || blockSize < 12)
    {
        if (block) XDisposePtr(block);
        return NULL;
    }

    p = (unsigned char *)block;
    end = p + blockSize;
    if (PV_ReadBE32(p) != (uint32_t)ZMF_BANK_BLOCK_MAGIC || PV_ReadBE32(p + 4) != 1u)
    {
        XDisposePtr(block);
        return NULL;
    }
    count = PV_ReadBE32(p + 8);
    p += 12;

    for (i = 0; i < count; ++i)
    {
        uint32_t etype, eid;
        uint16_t nameLen;
        uint32_t dataLen;
        unsigned char *namePtr, *dataPtr;

        if (p + 14 > end) { XDisposePtr(block); return NULL; }
        etype   = PV_ReadBE32(p); p += 4;
        eid     = PV_ReadBE32(p); p += 4;
        nameLen = PV_ReadBE16(p); p += 2;
        dataLen = PV_ReadBE32(p); p += 4;
        if (p + nameLen > end) { XDisposePtr(block); return NULL; }
        namePtr = p; p += nameLen;
        if (p + dataLen > end) { XDisposePtr(block); return NULL; }
        dataPtr = p;

        if ((XResourceType)etype == resourceType && (XLongResourceID)eid == resourceID)
        {
            XPTR copy = XNewPtr((int32_t)dataLen);
            if (!copy) { XDisposePtr(block); return NULL; }
            XBlockMove(dataPtr, copy, (int32_t)dataLen);
            if (pReturnedResourceSize) *pReturnedResourceSize = (int32_t)dataLen;
            if (pResourceName)
            {
                uint16_t pNameLen = (nameLen > 255u) ? 255u : nameLen;
                ((unsigned char *)pResourceName)[0] = (unsigned char)pNameLen;
                if (pNameLen > 0) XBlockMove(namePtr, (unsigned char *)pResourceName + 1, pNameLen);
            }
            XDisposePtr(block);
            return copy;
        }
        p += dataLen;
    }
    XDisposePtr(block);
    return NULL;
}

static XPTR PV_GetIndexedResourceFromZmfBankBlockByType(XFILE fileRef,
                                                         XResourceType resourceType,
                                                         int32_t resourceIndex,
                                                         XLongResourceID *pReturnedID,
                                                         void *pResourceName,
                                                         int32_t *pReturnedResourceSize)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p, *end;
    uint32_t count, i;
    int32_t found;

    block = PV_LoadZmfBankBlock(fileRef, &blockSize);
    if (!block || blockSize < 12)
    {
        if (block) XDisposePtr(block);
        return NULL;
    }

    p = (unsigned char *)block;
    end = p + blockSize;
    if (PV_ReadBE32(p) != (uint32_t)ZMF_BANK_BLOCK_MAGIC || PV_ReadBE32(p + 4) != 1u)
    {
        XDisposePtr(block);
        return NULL;
    }
    count = PV_ReadBE32(p + 8);
    p += 12;
    found = 0;

    for (i = 0; i < count; ++i)
    {
        uint32_t etype, eid;
        uint16_t nameLen;
        uint32_t dataLen;
        unsigned char *namePtr, *dataPtr;

        if (p + 14 > end) { XDisposePtr(block); return NULL; }
        etype   = PV_ReadBE32(p); p += 4;
        eid     = PV_ReadBE32(p); p += 4;
        nameLen = PV_ReadBE16(p); p += 2;
        dataLen = PV_ReadBE32(p); p += 4;
        if (p + nameLen > end) { XDisposePtr(block); return NULL; }
        namePtr = p; p += nameLen;
        if (p + dataLen > end) { XDisposePtr(block); return NULL; }
        dataPtr = p;

        if ((XResourceType)etype == resourceType)
        {
            if (found == resourceIndex)
            {
                XPTR copy = XNewPtr((int32_t)dataLen);
                if (!copy) { XDisposePtr(block); return NULL; }
                XBlockMove(dataPtr, copy, (int32_t)dataLen);
                if (pReturnedID) *pReturnedID = (XLongResourceID)eid;
                if (pReturnedResourceSize) *pReturnedResourceSize = (int32_t)dataLen;
                if (pResourceName)
                {
                    uint16_t pNameLen = (nameLen > 255u) ? 255u : nameLen;
                    ((unsigned char *)pResourceName)[0] = (unsigned char)pNameLen;
                    if (pNameLen > 0) XBlockMove(namePtr, (unsigned char *)pResourceName + 1, pNameLen);
                }
                XDisposePtr(block);
                return copy;
            }
            found++;
        }
        p += dataLen;
    }
    XDisposePtr(block);
    return NULL;
}

static int32_t PV_CountResourcesInZmfBankBlockByType(XFILE fileRef, XResourceType resourceType)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p, *end;
    uint32_t count, i;
    int32_t found;

    block = PV_LoadZmfBankBlock(fileRef, &blockSize);
    if (!block || blockSize < 12)
    {
        if (block) XDisposePtr(block);
        return 0;
    }

    p = (unsigned char *)block;
    end = p + blockSize;
    if (PV_ReadBE32(p) != (uint32_t)ZMF_BANK_BLOCK_MAGIC || PV_ReadBE32(p + 4) != 1u)
    {
        XDisposePtr(block);
        return 0;
    }
    count = PV_ReadBE32(p + 8);
    p += 12;
    found = 0;

    for (i = 0; i < count; ++i)
    {
        uint32_t etype;
        uint16_t nameLen;
        uint32_t dataLen;

        if (p + 14 > end) break;
        etype = PV_ReadBE32(p); p += 4;
        p += 4; /* id */
        nameLen = PV_ReadBE16(p); p += 2;
        dataLen = PV_ReadBE32(p); p += 4;
        if (p + nameLen + dataLen > end) break;
        p += nameLen + dataLen;
        if ((XResourceType)etype == resourceType)
            found++;
    }
    XDisposePtr(block);
    return found;
}

static bool PV_PackBankResourcesIntoZmfBlock(XFILE fileRef)
{
    int32_t mapID;
    int32_t i;
    XPTR plainBlock;
    int32_t plainSize;
    int32_t plainCapacity;
    XPTR compressedBlock;
    int32_t compressedSize;
    XFILE outFile;
    XPTR packedData;
    int32_t packedSize;
    XFILENAME *srcRef;
    int64_t originalStoredBytes;
    int32_t resCount;

    if (!PV_GetResourceMapInfo(fileRef, &mapID, NULL))
        return FALSE;
    if (mapID != XFILERESOURCE_ZMF_ID)
        return TRUE;

    srcRef = (XFILENAME *)fileRef;
    if (srcRef->pCache == NULL)
        XCreateAccessCache(fileRef);
    if (!srcRef->pCache)
        return FALSE;

    resCount = 0;
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XResourceType rtype = (XResourceType)srcRef->pCache->cached[i].resourceType;
        if (rtype == ID_BANK || rtype == ID_MIDI || rtype == ID_MIDI_OLD)
            resCount++;
    }
    if (resCount <= 0)
        return TRUE;

    plainBlock = NULL;
    plainSize = 0;
    plainCapacity = 0;
    originalStoredBytes = 0;
    {
        unsigned char header[12];
        PV_WriteBE32(header + 0, (uint32_t)ZMF_BANK_BLOCK_MAGIC);
        PV_WriteBE32(header + 4, 1u);
        PV_WriteBE32(header + 8, (uint32_t)resCount);
        if (!PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, header, 12))
            return FALSE;
    }

    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XResourceType rtype = (XResourceType)srcRef->pCache->cached[i].resourceType;
        XLongResourceID resID;
        int32_t resSize;
        char pName[256];
        uint16_t nameLen;
        unsigned char eh[14];
        XPTR resData;

        if (rtype != ID_BANK && rtype != ID_MIDI && rtype != ID_MIDI_OLD)
            continue;

        resID = (XLongResourceID)srcRef->pCache->cached[i].resourceID;
        pName[0] = 0;
        resData = XGetFileResource(fileRef, rtype, resID, pName, &resSize);
        if (!resData || resSize <= 0)
        {
            if (resData) XDisposePtr(resData);
            XDisposePtr(plainBlock);
            return FALSE;
        }

        nameLen = (uint16_t)(uint8_t)pName[0];
        originalStoredBytes += (int64_t)17 + (int64_t)nameLen + (int64_t)resSize;
        PV_WriteBE32(eh + 0,  (uint32_t)rtype);
        PV_WriteBE32(eh + 4,  (uint32_t)resID);
        PV_WriteBE16(eh + 8,  nameLen);
        PV_WriteBE32(eh + 10, (uint32_t)resSize);
        if (!PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, eh, 14) ||
            (nameLen > 0 && !PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, pName + 1, nameLen)) ||
            !PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, resData, resSize))
        {
            XDisposePtr(resData);
            XDisposePtr(plainBlock);
            return FALSE;
        }
        XDisposePtr(resData);
    }

#if USE_LZMA_COMPRESSION == TRUE
    compressedSize = XCompressPtr(&compressedBlock,
                                  plainBlock,
                                  (uint32_t)plainSize,
                                  X_LZMA_RAW,
                                  NULL,
                                  NULL);
#else
    compressedBlock = XNewPtr(plainSize);
    compressedSize = 0;
    if (compressedBlock)
    {
        XBlockMove(plainBlock, compressedBlock, plainSize);
        compressedSize = plainSize;
    }
#endif
    XDisposePtr(plainBlock);
    if (!compressedBlock || compressedSize <= 0)
    {
        if (compressedBlock) XDisposePtr(compressedBlock);
        return FALSE;
    }

    if (((int64_t)17 + (int64_t)compressedSize) >= originalStoredBytes)
    {
        XDisposePtr(compressedBlock);
        return TRUE;
    }

    outFile = XFileOpenVirtualResource(XFILERESOURCE_ZMF_ID);
    if (!outFile)
    {
        XDisposePtr(compressedBlock);
        return FALSE;
    }

    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item = &srcRef->pCache->cached[i];
        XPTR data;
        int32_t size;
        char name[256];

        if (item->resourceType == ID_BANK || item->resourceType == ID_MIDI ||
            item->resourceType == ID_MIDI_OLD || item->resourceType == ID_ZBNK ||
            item->resourceType == XFILEGONE_ID)
        {
            continue;
        }

        name[0] = 0;
        data = NULL;
        size = 0;
        if (!PV_ReadCachedResourceRaw(fileRef, item, name, &data, &size) || !data)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            continue;
        }
        if (XAddFileResource(outFile,
                             (XResourceType)item->resourceType,
                             (XLongResourceID)item->resourceID,
                             name,
                             data,
                             size) != 0)
        {
            XDisposePtr(data);
            XDisposePtr(compressedBlock);
            XFileClose(outFile);
            return FALSE;
        }
        XDisposePtr(data);
    }

    if (XAddFileResource(outFile, ID_ZBNK, 1, NULL, compressedBlock, compressedSize) != 0)
    {
        XDisposePtr(compressedBlock);
        XFileClose(outFile);
        return FALSE;
    }
    XDisposePtr(compressedBlock);

    /* Keep flat INST/ZINS as copied — do not PackInst on this temp. */
    if (XCleanResourceFileOptions(outFile, FALSE, FALSE) == FALSE)
    {
        XFileClose(outFile);
        return FALSE;
    }
    packedData = NULL;
    packedSize = 0;
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 || !packedData || packedSize <= 0)
    {
        if (packedData) XDisposePtr(packedData);
        XFileClose(outFile);
        return FALSE;
    }
    XFileClose(outFile);

    PV_ApplySourceMapVersionToPacked(fileRef, packedData, packedSize);
    XFileFreeResourceCache(fileRef);
    {
        XFILENAME *pReference = (XFILENAME *)fileRef;
        if (pReference->pResourceData)
        {
            if (pReference->ownsResourceData)
                XDisposePtr(pReference->pResourceData);
            pReference->pResourceData = packedData;
            pReference->resMemLength = packedSize;
            pReference->resMemCapacity = packedSize;
            pReference->resMemOffset = 0;
            pReference->ownsResourceData = TRUE;
        }
        else
        {
            if (XFileSetLength(fileRef, 0) != 0 ||
                XFileSetPosition(fileRef, 0L) != 0 ||
                XFileWrite(fileRef, packedData, packedSize) != 0)
            {
                XDisposePtr(packedData);
                return FALSE;
            }
            XDisposePtr(packedData);
        }
    }
    return TRUE;
}

// ---- ZSHD block: typed LZMA-compressed container for SND/CSND/ESND headers ----

static XPTR PV_LoadZmfSndHeaderBlock(XFILE fileRef, int32_t *outSize)
{
    XLongResourceID blockID;
    int32_t blockSize;
    XPTR blockData;
    XPTR decoded;

    if (!outSize)
    {
        return NULL;
    }
    *outSize = 0;

    blockData = XGetIndexedFileResource(fileRef, ID_ZSHD, &blockID, 0, NULL, &blockSize);
    if (!blockData || blockSize <= 0)
    {
        if (blockData)
        {
            XDisposePtr(blockData);
        }
        return NULL;
    }

    decoded = XDecompressPtr(blockData, (uint32_t)blockSize, TRUE);
    XDisposePtr(blockData);
    if (!decoded)
    {
        return NULL;
    }
    *outSize = XGetPtrSize(decoded);
    return decoded;
}

static XPTR PV_GetSndHeaderFromZmfBlockByTypeAndID(XFILE fileRef,
                                                    XResourceType resourceType,
                                                    XLongResourceID resourceID,
                                                    void *pResourceName,
                                                    int32_t *pReturnedHeaderSize)
{
    XPTR block;
    int32_t blockSize;
    unsigned char *p;
    unsigned char *end;
    uint32_t count;
    uint32_t i;

    if (pReturnedHeaderSize)
    {
        *pReturnedHeaderSize = 0;
    }
    block = PV_LoadZmfSndHeaderBlock(fileRef, &blockSize);
    if (!block || blockSize < 12)
    {
        if (block)
        {
            XDisposePtr(block);
        }
        return NULL;
    }

    p = (unsigned char *)block;
    end = p + blockSize;
    if (PV_ReadBE32(p) != (uint32_t)ZMF_SND_HEADER_BLOCK_MAGIC ||
        PV_ReadBE32(p + 4) != ZMF_SND_HEADER_BLOCK_VERSION)
    {
        XDisposePtr(block);
        return NULL;
    }
    count = PV_ReadBE32(p + 8);
    p += 12;

    for (i = 0; i < count; ++i)
    {
        uint32_t etype;
        uint32_t eid;
        uint16_t nameLen;
        uint32_t headerLen;
        unsigned char *headerPtr;
        XPTR copy;

        if (p + 14 > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        etype = PV_ReadBE32(p);
        p += 4;
        eid = PV_ReadBE32(p);
        p += 4;
        nameLen = PV_ReadBE16(p);
        p += 2;
        headerLen = PV_ReadBE32(p);
        p += 4;
        if (p + nameLen > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        p += nameLen;
        if (p + headerLen > end)
        {
            XDisposePtr(block);
            return NULL;
        }
        headerPtr = p;

        if ((XResourceType)etype == resourceType && (XLongResourceID)eid == resourceID)
        {
            copy = XNewPtr((int32_t)headerLen);
            if (!copy)
            {
                XDisposePtr(block);
                return NULL;
            }
            XBlockMove(headerPtr, copy, (int32_t)headerLen);
            if (pResourceName)
            {
                uint16_t pNameLen = (nameLen > 255u) ? 255u : nameLen;
                ((unsigned char *)pResourceName)[0] = (unsigned char)pNameLen;
                if (pNameLen > 0)
                {
                    XBlockMove(p - nameLen, (unsigned char *)pResourceName + 1, pNameLen);
                }
            }
            if (pReturnedHeaderSize)
            {
                *pReturnedHeaderSize = (int32_t)headerLen;
            }
            XDisposePtr(block);
            return copy;
        }

        p += headerLen;
    }

    XDisposePtr(block);
    return NULL;
}

static bool PV_HasZmfSndHeader(XFILE fileRef,
                               XResourceType resourceType,
                               XLongResourceID resourceID)
{
    int32_t headerSize = 0;
    XPTR headerData = PV_GetSndHeaderFromZmfBlockByTypeAndID(fileRef,
                                                             resourceType,
                                                             resourceID,
                                                             NULL,
                                                             &headerSize);
    if (headerData)
    {
        XDisposePtr(headerData);
        return TRUE;
    }
    return FALSE;
}

static XPTR PV_RebuildSndResourceFromZmfHeaderRef(XFILE fileRef,
                                                  XResourceType resourceType,
                                                  XLongResourceID resourceID,
                                                  void *pResourceName,
                                                  XPTR resourceData,
                                                  int32_t resourceDataSize,
                                                  int32_t *pReturnedResourceSize)
{
    unsigned char *payload;
    int32_t payloadSize;
    XPTR headerData;
    int32_t headerSize;
    XPTR rebuilt;

    if (!PV_IsSndResourceType(resourceType))
    {
        return NULL;
    }
    if (!PV_IsZmfSndPayloadRef(resourceData, resourceDataSize, resourceID, &payload, &payloadSize))
    {
        return NULL;
    }

    headerData = PV_GetSndHeaderFromZmfBlockByTypeAndID(fileRef,
                                                        resourceType,
                                                        resourceID,
                                                        pResourceName,
                                                        &headerSize);
    if (!headerData || headerSize <= 0)
    {
        if (headerData)
        {
            XDisposePtr(headerData);
        }
        return NULL;
    }

    rebuilt = XNewPtr(headerSize + payloadSize);
    if (!rebuilt)
    {
        XDisposePtr(headerData);
        return NULL;
    }
    XBlockMove(headerData, rebuilt, headerSize);
    if (payloadSize > 0)
    {
        XBlockMove(payload, (unsigned char *)rebuilt + headerSize, payloadSize);
    }
    XDisposePtr(headerData);

    if (pReturnedResourceSize)
    {
        *pReturnedResourceSize = headerSize + payloadSize;
    }
    return rebuilt;
}

/* Drop ZSHD when no flat SND/CSND/ESND peers remain (payload-refs or full). */
static bool PV_StripOrphanZmfSndHeaderBlock(XFILE fileRef)
{
    XFILENAME *srcRef;
    int32_t i;
    int32_t sndCount;
    int32_t zshdCount;
    XResourceType *omitTypes;
    XLongResourceID *omitIds;
    int32_t omitCount;
    int32_t omitCap;

    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    srcRef = (XFILENAME *)fileRef;
    if (srcRef->pCache == NULL)
    {
        XCreateAccessCache(fileRef);
    }
    if (!srcRef->pCache)
    {
        return FALSE;
    }

    sndCount = 0;
    zshdCount = 0;
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XResourceType rtype = (XResourceType)srcRef->pCache->cached[i].resourceType;
        if (PV_IsSndResourceType(rtype))
        {
            sndCount++;
        }
        else if (rtype == ID_ZSHD)
        {
            zshdCount++;
        }
    }
    if (sndCount > 0 || zshdCount <= 0)
    {
        return TRUE;
    }

    omitCap = zshdCount;
    omitTypes = (XResourceType *)XNewPtr((int32_t)(sizeof(XResourceType) * (uint32_t)omitCap));
    omitIds = (XLongResourceID *)XNewPtr((int32_t)(sizeof(XLongResourceID) * (uint32_t)omitCap));
    if (!omitTypes || !omitIds)
    {
        if (omitTypes)
        {
            XDisposePtr(omitTypes);
        }
        if (omitIds)
        {
            XDisposePtr(omitIds);
        }
        return FALSE;
    }
    omitCount = 0;
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item = &srcRef->pCache->cached[i];
        if ((XResourceType)item->resourceType != ID_ZSHD)
        {
            continue;
        }
        if (omitCount >= omitCap)
        {
            break;
        }
        omitTypes[omitCount] = ID_ZSHD;
        omitIds[omitCount] = (XLongResourceID)item->resourceID;
        omitCount++;
    }
    if (omitCount <= 0)
    {
        XDisposePtr(omitTypes);
        XDisposePtr(omitIds);
        return TRUE;
    }
    /* XOmitFileResources rebuilds without Clean — safe from PackSnd re-entry. */
    if (XOmitFileResources(fileRef, omitTypes, omitIds, omitCount) == FALSE)
    {
        XDisposePtr(omitTypes);
        XDisposePtr(omitIds);
        return FALSE;
    }
    XDisposePtr(omitTypes);
    XDisposePtr(omitIds);
    return TRUE;
}

static bool PV_PackSndHeaderResourcesIntoZmfBlock(XFILE fileRef)
{
    int32_t mapID;
    int32_t i;
    int32_t packableCount;
    int64_t originalStoredBytes;
    int64_t wrappedStoredBytes;
    XPTR plainBlock;
    int32_t plainSize;
    int32_t plainCapacity;
    XPTR compressedBlock;
    int32_t compressedSize;
    XFILE outFile;
    XPTR packedData;
    int32_t packedSize;
    XFILENAME *srcRef;

    if (!PV_GetResourceMapInfo(fileRef, &mapID, NULL))
    {
        return FALSE;
    }
    if (mapID != XFILERESOURCE_ZMF_ID)
    {
        return TRUE;
    }

    srcRef = (XFILENAME *)fileRef;
    if (srcRef->pCache == NULL)
    {
        XCreateAccessCache(fileRef);
    }
    if (!srcRef->pCache)
    {
        return FALSE;
    }

    packableCount = 0;
    originalStoredBytes = 0;
    wrappedStoredBytes = 0;
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item;
        XResourceType rtype;
        XPTR rawData;
        int32_t rawSize;
        char pName[256];
        unsigned char *unusedPayload;
        int32_t unusedPayloadSize;
        int32_t headerBytes;
        int32_t payloadOffset;
        uint16_t nameLen;

        item = &srcRef->pCache->cached[i];
        rtype = (XResourceType)item->resourceType;
        if (!PV_IsSndResourceType(rtype))
        {
            continue;
        }
        if (!PV_ReadCachedResourceRaw(fileRef, item, pName, &rawData, &rawSize))
        {
            /* Skip unreadable SND entries — don't fail the whole Clean/pack. */
            if (rawData)
            {
                XDisposePtr(rawData);
            }
            continue;
        }
        if (!rawData || rawSize <= 0)
        {
            if (rawData)
            {
                XDisposePtr(rawData);
            }
            continue;
        }

        if (PV_IsZmfSndPayloadRef(rawData, rawSize, (XLongResourceID)item->resourceID, &unusedPayload, &unusedPayloadSize))
        {
            XDisposePtr(rawData);
            continue;
        }
        if (!PV_ParseSndHeaderAndPayload(rawData, rawSize, &headerBytes, &payloadOffset))
        {
            XDisposePtr(rawData);
            continue;
        }
        if (headerBytes <= 0 || payloadOffset < 0 || payloadOffset > rawSize)
        {
            XDisposePtr(rawData);
            continue;
        }

        nameLen = (uint16_t)(uint8_t)pName[0];
        packableCount++;
        originalStoredBytes += (int64_t)17 + (int64_t)nameLen + (int64_t)rawSize;
        wrappedStoredBytes += (int64_t)17 + (int64_t)2 + (int64_t)(rawSize - payloadOffset);
        XDisposePtr(rawData);
    }

    if (packableCount <= 0)
    {
        /* Already packed (payload-refs + ZSHD) keeps ZSHD; orphan ZSHD goes. */
        (void)PV_StripOrphanZmfSndHeaderBlock(fileRef);
        return TRUE;
    }

    plainBlock = NULL;
    plainSize = 0;
    plainCapacity = 0;
    {
        unsigned char header[12];
        PV_WriteBE32(header + 0, (uint32_t)ZMF_SND_HEADER_BLOCK_MAGIC);
        PV_WriteBE32(header + 4, ZMF_SND_HEADER_BLOCK_VERSION);
        PV_WriteBE32(header + 8, (uint32_t)packableCount);
        if (!PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, header, 12))
        {
            return FALSE;
        }
    }

    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item;
        XResourceType rtype;
        XPTR rawData;
        int32_t rawSize;
        char pName[256];
        unsigned char *unusedPayload;
        int32_t unusedPayloadSize;
        int32_t headerBytes;
        int32_t payloadOffset;
        uint16_t nameLen;
        unsigned char eh[14];

        item = &srcRef->pCache->cached[i];
        rtype = (XResourceType)item->resourceType;
        if (!PV_IsSndResourceType(rtype))
        {
            continue;
        }
        if (!PV_ReadCachedResourceRaw(fileRef, item, pName, &rawData, &rawSize))
        {
            /* Skip unreadable SND — don't fail whole Clean (IREZ↔ZREZ clone). */
            if (rawData)
            {
                XDisposePtr(rawData);
            }
            continue;
        }
        if (!rawData || rawSize <= 0)
        {
            if (rawData)
            {
                XDisposePtr(rawData);
            }
            continue;
        }
        if (PV_IsZmfSndPayloadRef(rawData, rawSize, (XLongResourceID)item->resourceID, &unusedPayload, &unusedPayloadSize) ||
            !PV_ParseSndHeaderAndPayload(rawData, rawSize, &headerBytes, &payloadOffset) ||
            headerBytes <= 0 || payloadOffset < 0 || payloadOffset > rawSize)
        {
            XDisposePtr(rawData);
            continue;
        }

        nameLen = (uint16_t)(uint8_t)pName[0];
        PV_WriteBE32(eh + 0, (uint32_t)rtype);
        PV_WriteBE32(eh + 4, (uint32_t)item->resourceID);
        PV_WriteBE16(eh + 8, nameLen);
        PV_WriteBE32(eh + 10, (uint32_t)headerBytes);
        if (!PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, eh, 14) ||
            (nameLen > 0 && !PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, pName + 1, nameLen)) ||
            !PV_AppendBytes(&plainBlock, &plainSize, &plainCapacity, rawData, headerBytes))
        {
            XDisposePtr(rawData);
            XDisposePtr(plainBlock);
            return FALSE;
        }
        XDisposePtr(rawData);
    }

#if USE_LZMA_COMPRESSION == TRUE
    compressedSize = XCompressPtr(&compressedBlock,
                                  plainBlock,
                                  (uint32_t)plainSize,
                                  X_LZMA_RAW,
                                  NULL,
                                  NULL);
#else
    compressedBlock = XNewPtr(plainSize);
    compressedSize = 0;
    if (compressedBlock)
    {
        XBlockMove(plainBlock, compressedBlock, plainSize);
        compressedSize = plainSize;
    }
#endif
    XDisposePtr(plainBlock);
    if (!compressedBlock || compressedSize <= 0)
    {
        if (compressedBlock)
        {
            XDisposePtr(compressedBlock);
        }
        /* Leave SNDs unpacked rather than failing Clean / clone bank. */
        return TRUE;
    }

    if ((wrappedStoredBytes + (int64_t)17 + (int64_t)compressedSize) >= originalStoredBytes)
    {
        XDisposePtr(compressedBlock);
        return TRUE;
    }

    outFile = XFileOpenVirtualResource(XFILERESOURCE_ZMF_ID);
    if (!outFile)
    {
        XDisposePtr(compressedBlock);
        return TRUE;
    }

    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item;
        XResourceType rtype;
        XPTR rawData;
        int32_t rawSize;
        char pName[256];
        unsigned char *unusedPayload;
        int32_t unusedPayloadSize;
        int32_t headerBytes;
        int32_t payloadOffset;
        XPTR payloadRef;

        item = &srcRef->pCache->cached[i];
        rtype = (XResourceType)item->resourceType;
        if (rtype == ID_ZSHD || rtype == XFILEGONE_ID)
        {
            continue;
        }
        if (!PV_ReadCachedResourceRaw(fileRef, item, pName, &rawData, &rawSize))
        {
            if (rawData)
            {
                XDisposePtr(rawData);
            }
            /* Abort ZSHD pack; keep source bank as-is. */
            XDisposePtr(compressedBlock);
            XFileClose(outFile);
            return TRUE;
        }

        if (PV_IsSndResourceType(rtype) &&
            rawData && rawSize > 0 &&
            !PV_IsZmfSndPayloadRef(rawData, rawSize, (XLongResourceID)item->resourceID, &unusedPayload, &unusedPayloadSize) &&
            PV_ParseSndHeaderAndPayload(rawData, rawSize, &headerBytes, &payloadOffset) &&
            headerBytes > 0 && payloadOffset >= 0 && payloadOffset <= rawSize)
        {
            int32_t payloadSize = rawSize - payloadOffset;

            payloadRef = XNewPtr(2 + payloadSize);
            if (!payloadRef)
            {
                XDisposePtr(rawData);
                XDisposePtr(compressedBlock);
                XFileClose(outFile);
                return FALSE;
            }
            PV_WriteBE16((unsigned char *)payloadRef + 0, (uint16_t)item->resourceID);
            if (payloadSize > 0)
            {
                XBlockMove((unsigned char *)rawData + payloadOffset,
                           (unsigned char *)payloadRef + 2,
                           payloadSize);
            }
            /* Keep the SND resource name on the payload-ref entry so
             * XGetFileResourceName still works if ZSHD lookup is skipped. */
            if (XAddFileResource(outFile,
                                 rtype,
                                 (XLongResourceID)item->resourceID,
                                 pName,
                                 payloadRef,
                                 2 + payloadSize) != 0)
            {
                XDisposePtr(payloadRef);
                XDisposePtr(rawData);
                XDisposePtr(compressedBlock);
                XFileClose(outFile);
                return FALSE;
            }
            XDisposePtr(payloadRef);
            XDisposePtr(rawData);
            continue;
        }

        if (rawData)
        {
            if (XAddFileResource(outFile,
                                 rtype,
                                 (XLongResourceID)item->resourceID,
                                 pName,
                                 rawData,
                                 rawSize) != 0)
            {
                XDisposePtr(rawData);
                XDisposePtr(compressedBlock);
                XFileClose(outFile);
                return FALSE;
            }
            XDisposePtr(rawData);
        }
    }

    if (XAddFileResource(outFile, ID_ZSHD, 1, NULL, compressedBlock, compressedSize) != 0)
    {
        XDisposePtr(compressedBlock);
        XFileClose(outFile);
        return FALSE;
    }
    XDisposePtr(compressedBlock);

    /* outFile already has payload-refs + ZSHD; don't re-enter pack. */
    if (XCleanResourceFileEx(outFile, FALSE) == FALSE)
    {
        XFileClose(outFile);
        return FALSE;
    }
    packedData = NULL;
    packedSize = 0;
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 || !packedData || packedSize <= 0)
    {
        if (packedData)
        {
            XDisposePtr(packedData);
        }
        XFileClose(outFile);
        return FALSE;
    }
    XFileClose(outFile);

    PV_ApplySourceMapVersionToPacked(fileRef, packedData, packedSize);
    XFileFreeResourceCache(fileRef);
    {
        XFILENAME *pReference = (XFILENAME *)fileRef;
        if (pReference->pResourceData)
        {
            if (pReference->ownsResourceData)
            {
                XDisposePtr(pReference->pResourceData);
            }
            pReference->pResourceData = packedData;
            pReference->resMemLength = packedSize;
            pReference->resMemCapacity = packedSize;
            pReference->resMemOffset = 0;
            pReference->ownsResourceData = TRUE;
        }
        else
        {
            if (XFileSetLength(fileRef, 0) != 0 ||
                XFileSetPosition(fileRef, 0L) != 0 ||
                XFileWrite(fileRef, packedData, packedSize) != 0)
            {
                XDisposePtr(packedData);
                return FALSE;
            }
            XDisposePtr(packedData);
        }
    }

    return TRUE;
}

// Private functions

// Check for a valid file reference
static bool PV_XFileValid(XFILE fileRef)
{
    XFILENAME   *pReference;
    bool       valid;

    valid = FALSE;
    pReference = fileRef;
    if (pReference)
    {
        int         code;
        code = BAE_IsBadReadPointer(&pReference->fileValidID, sizeof(int32_t));
        if (code == 0 || code == 2)
        {
            if (pReference->fileValidID == XPI_BLOCK_3_ID)
            {
                valid = TRUE;
            }
        }
    }
    return valid;
}

// Return TRUE if file is locked (read only) – helper referenced by deletion code
static bool PV_IsXFileLocked(XFILE fileRef)
{
    XFILENAME *pReference = (XFILENAME *)fileRef;
    if (!PV_XFileValid(fileRef))
    {
        return TRUE; // treat invalid as locked to force early failure
    }
    return pReference->readOnly ? TRUE : FALSE;
}

// Copy data within the same file (used when compacting resource files)
// Returns 0 on success, non–zero on error.
int32_t PV_CopyWithinFile(XFILE fileRef,
                              int32_t srcPos,
                              int32_t dstPos,
                              int32_t size,
                              void *tempBuffer,
                              int32_t tempBufferSize)
{
    int32_t err = 0;
    int32_t remaining = size;
    int32_t localSrc = srcPos;
    int32_t localDst = dstPos;
    if (!PV_XFileValid(fileRef) || size < 0 || tempBufferSize <= 0 || tempBuffer == NULL)
    {
        return -1;
    }
    while (remaining > 0 && err == 0)
    {
        int32_t chunk = (remaining > tempBufferSize) ? tempBufferSize : remaining;
        err = XFileSetPosition(fileRef, localSrc);
        if (err != 0) { break; }
        err = XFileRead(fileRef, tempBuffer, chunk);
        if (err != 0) { break; }
        err = XFileSetPosition(fileRef, localDst);
        if (err != 0) { break; }
        err = XFileWrite(fileRef, tempBuffer, chunk);
        if (err != 0) { break; }
        remaining -= chunk;
        localSrc += chunk;
        localDst += chunk;
    }
    return err;
}

// Given a valid XFILE, this will return the open index. Will return -1 if not valid.
// Caller must hold PV_LockOpenResourceFiles().
static int16_t PV_FindResourceFileReferenceIndex(XFILE fileRef)
{
    int16_t   count;

    for (count = 0; count < g_resourceFileCount; count++)
    {
        if (g_openResourceFiles[count] == fileRef)
        {
            return count;
        }
    }
    return -1;
}

// Add a newly opened resource file to the front of the search path.
// Returns TRUE if the list is full (file was not added).
static bool PV_AddResourceFileToOpenFiles(XFILE fileRef)
{
    bool           full;
    int16_t       count;

    full = TRUE;
    PV_LockOpenResourceFiles();
    if (g_resourceFileCount < MAX_OPEN_XFILES)
    {
        for (count = MAX_OPEN_XFILES-2; count >= 0; count--)
        {
            g_openResourceFiles[count+1] = g_openResourceFiles[count];
        }
        g_openResourceFiles[0] = fileRef;
        g_resourceFileCount++;
        full = FALSE;
    }
    PV_UnlockOpenResourceFiles();
    return full;
}

// Remove an open resource file from the resource search path.
static void PV_RemoveResourceFileFromOpenFiles(XFILE fileRef)
{
    int16_t       count;
    int16_t       found;

    found = -1;
    PV_LockOpenResourceFiles();
    for (count = 0; count < g_resourceFileCount; count++)
    {
        if (g_openResourceFiles[count] == fileRef)
        {
            found = count;
            break;
        }
    }
    if (found != -1)
    {
        for (count = found; count < g_resourceFileCount-1; count++)
        {
            g_openResourceFiles[count] = g_openResourceFiles[count+1];
        }
        g_openResourceFiles[count] = 0;
        g_resourceFileCount--;
    }
    PV_UnlockOpenResourceFiles();
}

static INLINE bool PV_IsAnyOpenResourceFiles(void)
{
    bool any;

    PV_LockOpenResourceFiles();
    any = (g_resourceFileCount > 0) ? TRUE : FALSE;
    PV_UnlockOpenResourceFiles();
    return any;
}

// Functions

// convert a c string to a pascal string
void XCtoPascalString(char const* cString, char pascalString[256])
{
    int32_t        length;

    if (cString && pascalString)
    {
        length = XStrLen(cString);
        if (length > 255) length = 255;
        XBlockMove(cString, pascalString + 1, length);
        pascalString[0] = (char)length;
    }
}

// convert a c string to a pascal string
void* XCtoPstr(void* cstr)
{
    XCtoPascalString((char const *)cstr, (char *)cstr);
    return cstr;
}

// convert a pascal string to a c string
void * XPtoCstr(register void *pstr)
{
    register char * source;
    register char * dest;
    register int32_t   length;
    char            data[256];

    if (pstr)
    {
        length = *((unsigned char *)pstr);
        source = (char *)pstr;
        source++;
        dest = data;

        while (length--)
        {
            *dest++ = *source++;
        }
        *dest = 0;
        length = *((unsigned char *)pstr) + 1;
        XBlockMove(data, pstr, length);
    }
    return pstr;
}


// Debug helper: verify header integrity and print details if corrupted
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static bool XValidateMemblockHeader(XPTR data, const char *context)
{
    if (!data) return true;
    
    XPI_Memblock *mb = (XPI_Memblock *)XIsOurMemoryPtr(data);
    if (!mb) {
        debug_message("[MEM CORRUPTION] %s: XIsOurMemoryPtr returned NULL for data=%p\n", context, data);
        BAE_ASSERT(FALSE);
        return false;
    }
    
    uint32_t id1 = XGetLong(&mb->blockID_one);
    uint32_t id2 = XGetLong(&mb->blockID_two);
    if (id1 != XPI_BLOCK_1_ID || id2 != XPI_BLOCK_2_ID) {
        debug_message("[MEM CORRUPTION] %s: header corrupted at data=%p mb=%p\n", context, data, mb);
        debug_message("  expected id1=0x%08X got=0x%08X\n", XPI_BLOCK_1_ID, id1);
        debug_message("  expected id2=0x%08X got=0x%08X\n", XPI_BLOCK_2_ID, id2);
        debug_message("  blockSize=%d\n", mb->blockSize);
        BAE_ASSERT(FALSE);
        return false;
    }
    return true;
}

// Determine if a data block was allocated with our memory allocation API
XPI_Memblock * XIsOurMemoryPtr(XPTR data)
{
    char            *pData;
    XPI_Memblock    *pBlock, *pBlockReturn;
    int16_t       code;

    pBlockReturn = NULL;
    if (data)
    {
        pData = (char *)data;
        pData -= sizeof(XPI_Memblock);
        pBlock = (XPI_Memblock *)pData;
        code = BAE_IsBadReadPointer(pBlock, sizeof(XPI_Memblock));
        if (code == 0 || code == 2)
        {
            if ( (XGetLong(&pBlock->blockID_one) == XPI_BLOCK_1_ID) &&
                (XGetLong(&pBlock->blockID_two) == XPI_BLOCK_2_ID) )
            {
                pBlockReturn = pBlock;
            } else {
                // Debug: print details about the failed pointer check
                debug_message("[MEM CHECK FAILED] XIsOurMemoryPtr: pointer %p is not a valid block header\n", data);
                BAE_ASSERT(FALSE);
            }
        }
    }
    return pBlockReturn;
}

XPI_Memblock *XGetHeaderIfOurs(void *memoryBlock)
{
    if (!memoryBlock) {
        return NULL;
    }

    XPI_Memblock *mb = (XPI_Memblock *)((char*)memoryBlock - sizeof(XPI_Memblock));

    if (XGetLong(&mb->blockID_one) != XPI_BLOCK_1_ID ||
        XGetLong(&mb->blockID_two) != XPI_BLOCK_2_ID) {
        return NULL; // not one of our blocks
    }

    return mb;
}


// This function re-allocates a memory block
// ptr may be NULL, in which case the functionality is the same as XNewPtr()
// If allocation fails, ptr is unaffected (It's still allocated.)
// Like with XNewPtr(), any newly allocated memory is zeroed.
XPTR XResizePtr(XPTR ptr, int32_t size)
{
int32_t        const currentSize = ptr ? XGetPtrSize(ptr) : 0;

    //MOE:  If we are allocating to a certain quantum (typically 16 bytes),
    //      We should apply that quantum before this test.
    if (ptr == NULL)
    {
        return XNewPtr(size);
    }
    if (size != currentSize && ptr)
    {
        
        XPI_Memblock *mb = (XPI_Memblock*)XIsOurMemoryPtr(ptr);
        void *newReal = realloc(mb->realPtr, sizeof(XPI_Memblock) + size);
        if (newReal) {
            XPI_Memblock *newMb = (XPI_Memblock *)newReal;
            newMb->realPtr = newReal;
            newMb->blockSize = size;
            return (XPTR)(newMb + 1);
        } else {
            debug_message("[MEM REALLOC FAILED] XResizePtr: realloc failed for ptr=%p size=%d\n", ptr, size);
            // fall back to the old way
            
            XPTR        newPtr;

            // disable when these is an BAE_ResizePointer()
            if ((size < currentSize) &&
                (size >= 0x100000) &&               // if over 1Mb would have to be allocated
                (currentSize - size <= 0x10000) &&  // if less than 64Kb would be saved
                XIsOurMemoryPtr(ptr))
            {
                newPtr = NULL;  // force hacky but quick pseudo resizing
            }
            else
            {
                newPtr = XNewPtr(size);
            }

            if (newPtr && ptr)
            {
            int32_t        copyLength;
            
                copyLength = currentSize;
                if (copyLength > size)
                {
                    copyLength = size;
                }
                XBlockMove(ptr, newPtr, copyLength);
                XDisposePtr(ptr);
            }
            else if (ptr && (size < currentSize))
            {
            XPI_Memblock*       odata;

                odata = (XPI_Memblock*)XIsOurMemoryPtr(ptr);
                if (odata)
                {
                    odata->blockSize = size;
                    return ptr;
                }
            }
            return newPtr;
        }
    }
    return ptr;
}

// Allocates a block of ZEROED!!!! memory and locks it down
XPTR XNewPtr(int32_t size)
{
    char            *data;
    XPI_Memblock    *pBlock;

    if (size == 0)
    {
        return NULL;
    }
    size += sizeof(XPI_Memblock);
    data = (char *)BAE_Allocate(size);
    if (data)
    {
        pBlock = (XPI_Memblock *)data;
        XPutLong(&pBlock->blockID_one, XPI_BLOCK_1_ID);         // set our ID for this block
        XPutLong(&pBlock->blockID_two, XPI_BLOCK_2_ID);
        pBlock->realPtr = data;
        data += sizeof(XPI_Memblock);
        pBlock->blockSize = size - sizeof(XPI_Memblock);
    }
    return (XPTR)data;
}

void XDisposePtr(XPTR data)
{
    XPTR            osAllocatedData;
    XPI_Memblock    *pBlock;
    
    osAllocatedData = (XPTR)XIsOurMemoryPtr(data);
    if (osAllocatedData)
    {
        XGetPtrSize(data);   // need to get the size before we translate the pointer

        pBlock = (XPI_Memblock *)osAllocatedData;
        XPutLong(&pBlock->blockID_one, (uint32_t)XPI_DEAD_ID);         // set our ID for this block
        XPutLong(&pBlock->blockID_two, (uint32_t)XPI_DEAD_ID);         // to be dead. Used for tracking
        BAE_Deallocate(osAllocatedData);
    }
}

int32_t XGetPtrSize(XPTR data)
{
    int32_t        size;
    XPTR        odata;

    size = 0;
    if (data)
    {
        odata = (XPTR)XIsOurMemoryPtr(data);
        if (odata)
        {
            size = ((XPI_Memblock *)odata)->blockSize;
        }
        else
        {
            // then this block is not ours, so use the system to determine its real size
            printf("bae memory mismatch\n");
            BAE_ASSERT(TRUE);
        }
    }
    return size;
}

// Typical block move, expect it must be able to handle overlapping source and
// destination pointers
void XBlockMove(XPTRC source, XPTR dest, int32_t size)
{
    if (source && dest && size)
    {
        BAE_BlockMove((void *)source, dest, size);
    }
}

// set memory range with value
void XSetMemory(void *pAdr, int32_t len, char value)
{
    if (pAdr && (len > 0)) {
        memset(pAdr, value, len);
    }
}

// Given a pointer, and a bit number; this will set that bit to 1
void XSetBit(void *pBitArray, uint32_t whichbit)
{
    uint32_t   byteindex, bitindex;
    unsigned char *byte;

    if (pBitArray)
    {
        byteindex = whichbit / 8;
        bitindex = whichbit % 8;
        byte = &((unsigned char *)pBitArray)[byteindex];
        *byte |= (1L << (7-bitindex));
    }
}

// Given a pointer, and a bit number; this will set that bit to 0
void XClearBit(void *pBitArray, uint32_t whichbit)
{
    uint32_t   byteindex, bitindex;
    unsigned char *byte;

    if (pBitArray)
    {
        byteindex = whichbit / 8;
        bitindex = whichbit % 8;
        byte = &((unsigned char *)pBitArray)[byteindex];
        *byte &= ~(1L << (7-bitindex));
    }
}

// Given a pointer, and a bit number; this return the value of that bit
bool XTestBit(void *pBitArray, uint32_t whichbit)
{
    register uint32_t  byteindex, byte, bitindex;
        
    if (pBitArray)
    {
        byteindex = whichbit / 8;
        bitindex = whichbit % 8;
        byte = ((unsigned char *)pBitArray)[byteindex];
        return (byte & (1L << (7-bitindex))) ? TRUE : FALSE;
    }
    else
    {
        return FALSE;
    }
}

/* Sort an integer array from the lowest to the highest
*/
#if USE_CREATION_API == TRUE
void XBubbleSortArray(int16_t theArray[], int16_t theCount)
{
    register int16_t i, j, swapValue;
    
    for (i = 0; i < (theCount - 1); i++)
    {
        for (j = i + 1; j < theCount; j++)
        {
            if (theArray[i] > theArray[j])
            {
                swapValue = theArray[i];
                theArray[i] = theArray[j];
                theArray[j] = swapValue;
            }
        }
    }
}
#endif  // USE_CREATION_API


// Does sound hardware support stereo output
bool XIsStereoSupported(void)
{
    return BAE_IsStereoSupported();
}

// wait for a waitAmount of microseconds to pass
// CLS??: If this function is called from within the frame thread and
// JAVA_THREAD is non-zero, we'll probably crash.
void XWaitMicroseconds(uint32_t waitAmount)
{
    BAE_WaitMicroseconds(waitAmount);
}


// Returns microseconds since boot
// 1 second = 1000 milliseconds = 100000 microseconds
uint32_t XMicroseconds(void)
{
    return BAE_Microseconds();
}

// Does sound hardware support 16 bit output
bool XIs16BitSupported(void)
{
    return BAE_Is16BitSupported();
}

// Does sound hardware support 8 bit output
bool XIs8BitSupported(void)
{
    return BAE_Is8BitSupported();
}

// do a 16.16 fixed point multiply
XFIXED XFixedMultiply(XFIXED factor1, XFIXED factor2)
{
    XFIXED  result;
    
    if (factor1 && factor2)
    {
        result = (((factor1 >> 16L) & 0xFFFFL) * ((factor2 >> 16L) & 0xFFFFL)) << 16L;
        result += ((factor1 >> 16L) & 0xFFFFL) * ( factor2 & 0xFFFFL);
        result += (factor1 & 0xFFFFL) * ((factor2 >> 16L) & 0xFFFFL);
        result += (((factor1 & 0xFFFFL) * (factor2 & 0xFFFFL)) >> 16L) & 0xFFFFL;
    }
    else
    {
        result = 0;
    }
    return result;
}

// do a 16.16 fixed point divide
XFIXED XFixedDivide(XFIXED dividend, XFIXED divisor)
{
    XFIXED  result;
    
    if (divisor && dividend)
    {
        XFIXED      temp, rfactor, f2;
        int         i;
        
        f2 = divisor;
        temp = dividend;
        result = 0;
        rfactor = 0x10000L;
        for (i = 0; i < 16; i++)
        {
            while ((temp >= f2) && (rfactor != 0) && (temp != 0))
            {
                temp -= f2;
                result += rfactor;
            }
            f2 = f2 >> 1L;  
            rfactor = rfactor >> 1L;
        }
    }
    else
    {
        result = 0;
    }
    return result;
}

// 1/4 of cos/sin tables in 16.16 fixed point math
// 65536 = 1.0, 1144 = .01745605469
static const XFIXED cosSinTable90[] = 
{
    // 0-44
    65536, 65526, 65496, 65446, 65376, 65287, 65177, 65048, 64898, 64729, 64540, 64332, 
    64104, 63856, 63589, 63303, 62997, 62672, 62328, 61966, 61584, 61183, 60764, 60326, 
    59870, 59396, 58903, 58393, 57865, 57319, 56756, 56175, 55578, 54963, 54332, 53684, 
    53020, 52339, 51643, 50931, 50203, 49461, 48703, 47930, 47143, 
    // 45-89
    46341, 45525, 44695, 43852, 42995, 42126, 41243, 40348, 39441, 38521, 37590, 36647, 
    35693, 34729, 33754, 32768, 31772, 30767, 29753, 28729, 27697, 26656, 25607, 24550, 
    23486, 22415, 21336, 20252, 19161, 18064, 16962, 15855, 14742, 13626, 12505, 11380, 
    10252, 9121, 7987, 6850, 5712, 4572, 3430, 2287, 1144, 0
};


// angle = 0-359. returns 16.16 value
XFIXED XFixedSin(int angle)
{
    XFIXED  sin = 0;
    
    angle = angle % 360;
    
    if ( (angle >= 0) && (angle < 90) )
    {
        sin = cosSinTable90[90 - angle];    // 0-89
    }
    else if ( (angle >= 90) && (angle < 180) )
    {
        sin = cosSinTable90[angle - 90];    // 90-179
    }
    else if ( (angle >= 180) && (angle < 270) )
    {
        sin = -cosSinTable90[270 - angle];  // 180-269
    }
    else if ( (angle >= 270) && (angle <= 359) )
    {
        sin = -cosSinTable90[angle - 270];  // 270-359
    }
    return sin;
}

// angle = 0-359. returns 16.16 value
XFIXED XFixedCos(int angle)
{
    XFIXED  cos = 0;
    
    angle = angle % 360;
    
    if ( (angle >= 0) && (angle < 90) )
    {
        cos = cosSinTable90[angle]; // 0-89
    }
    else if ( (angle >= 90) && (angle < 180) )
    {
        cos = -cosSinTable90[180 - angle];  // 90-179
    }
    else if ( (angle >= 180) && (angle < 270) )
    {
        cos = -cosSinTable90[angle - 180];  // 180-269
    }
    else if ( (angle >= 270) && (angle <= 359) )
    {
        cos = cosSinTable90[90 - (angle - 270)];    // 270-359
    }
    
    return cos;
}

// given a fixed point value, do a floor and return the closest integer
uint32_t XFixedFloor(XFIXED value)
{
    return ((value + XFIXED_ONEHALF) >> 16L);
}

// given a pointer, get a int16_t ordered in an Intel way
uint16_t XGetShortIntel(void const* pData)
{
    register const unsigned char *pByte;
    register uint16_t data;

    pByte = (const unsigned char *)pData;
    data = ((uint16_t)pByte[1] << 8) |
           (uint16_t)pByte[0];
    return data;
}
// given a pointer, get a long ordered in an Intel way
uint32_t XGetLongIntel(void const* pData)
{
    register const unsigned char *pByte;
    register uint32_t  data;

    pByte = (const unsigned char *)pData;
    data = ((uint32_t)pByte[3] << 24L) |
           ((uint32_t)pByte[2] << 16L) |
           ((uint32_t)pByte[1] << 8L) |
           (uint32_t)pByte[0];
    return data;
}

// given a pointer, get a int16_t ordered in a Motorola way
uint16_t XGetShort(void const* pData)
{
    register const unsigned char *pByte;
    register uint16_t data;

    pByte = (const unsigned char *)pData;
    data = ((uint16_t)pByte[0] << 8) |
           (uint16_t)pByte[1];
    return data;
}
// given a pointer, get a long ordered in a Motorola way
uint32_t XGetLong(void const* pData)
{
    register const unsigned char *pByte;
    register uint32_t  data;

    pByte = (const unsigned char *)pData;
    data = ((uint32_t)pByte[0] << 24L) |
           ((uint32_t)pByte[1] << 16L) | 
           ((uint32_t)pByte[2] << 8L) |
           (uint32_t)pByte[3];
    return data;
}
// given a pointer and a value, this with put a short in a ordered 68k way
void XPutShort(void *pData, uint16_t data)
{
    register unsigned char  *pByte;

    pByte = (unsigned char *)pData;
    pByte[0] = (unsigned char)((data >> 8) & 0xFF);
    pByte[1] = (unsigned char)(data & 0xFF);
}

// given a pointer and a value, this with put a long in a ordered 68k way
void XPutLong(void *pData, uint32_t data)
{
    register unsigned char  *pByte;

    pByte = (unsigned char *)pData;
    pByte[0] = (unsigned char)((data >> 24) & 0xFF);
    pByte[1] = (unsigned char)((data >> 16) & 0xFF);
    pByte[2] = (unsigned char)((data >> 8) & 0xFF);
    pByte[3] = (unsigned char)(data & 0xFF);
}

uint32_t XSwapLong(uint32_t value)
{
    uint32_t   newValue;
    unsigned char   *pOld;
    unsigned char   *pNew;
    unsigned char   temp;

    pOld = (unsigned char *)&value;
    pNew = (unsigned char *)&newValue;

    temp = pOld[0];
    pNew[3] = temp;
    temp = pOld[1];
    pNew[2] = temp;
    temp = pOld[2];
    pNew[1] = temp;
    temp = pOld[3];
    pNew[0] = temp;

    return newValue;
}

uint16_t XSwapShort(uint16_t value)
{
    uint16_t  newValue;
    unsigned char   *pOld;
    unsigned char   *pNew;
    unsigned char   temp;

    pOld = (unsigned char *)&value;
    pNew = (unsigned char *)&newValue;

    temp = pOld[0];
    pNew[1] = temp;
    temp = pOld[1];
    pNew[0] = temp;

    return newValue;
}
// Swap shorts within a long, but not the bytes in each each short.

uint32_t XSwapShortInLong(uint32_t value)
{
    uint32_t   newValue = 0;
    uint16_t  *pOld;
    uint16_t  *pNew;
    uint16_t  temp;

    pOld = (uint16_t *)&value;
    pNew = (uint16_t *)&newValue;

    temp = pOld[0];
    pNew[1] = temp;
    temp = pOld[1];
    pNew[0] = temp;

    return newValue;
}
// if TRUE, then motorola; if FALSE then intel
bool XDetermineByteOrder(void)
{
    static int32_t value = 0x12345678;
    bool       order;

    order = FALSE;
    if (XGetLong(&value) == 0x12345678)
    {
        order = TRUE;
    }
    return order;
}

// given a path (MacHD:Folder:Path for MacOS, c:\folder\path for WinOS), fill in a XFILENAME
// If Mac, this will convert '/', '\' to ':'
// Also if Mac, this will convert relative paths, ie. ..\..\folder\file to mac equals.
void XConvertPathToXFILENAME(void *path, XFILENAME *xfile)
{
    void *file;

#if X_PLATFORM == X_MACINTOSH_9
    FSSpec  spec;
    char    tmp[256];
    char    *src, *dst;
    OSErr   err;
    bool   firstChar;

    firstChar = TRUE;
    tmp[0] = 0;
    src = (char *)path;
    dst = tmp;
    while (*src)
    {
        if ((*src == '/') || (*src == '\\'))
        {
            *dst++ = ':';
            src++;
            if (firstChar)
            {
                *dst++ = ':';   // first char, need an extra :
                firstChar = FALSE;
            }           
        }
        else
        {
            if ((src[0] == '.') && (src[1] == '.') && ((src[2] == '/') || (src[2] == '\\')))
            {
                if ( firstChar || (dst[-1] != ':') && (dst[0] != ':') ) // already going up directories?
                {   // no
                    *dst++ = ':';
                }
                *dst++ = ':';   // yes, only need to add single colon
                src += 3;
                firstChar = FALSE;
            }
            else
            {
                *dst++ = *src++;
                firstChar = FALSE;
            }
        }
    }
    *dst = 0;
    err = FSMakeFSSpec(0, 0, (unsigned char *)XCtoPstr(tmp), &spec);
    file = (void *)&spec;
#else
    file = path;
#endif

    XConvertNativeFileToXFILENAME(file, xfile); 
}

// given a native file spec (FSSpec for MacOS, and 'C' string for WinOS, fill in a XFILENAME
void XConvertNativeFileToXFILENAME(void *file, XFILENAME *xfile)
{
    if (xfile)
    {
        XSetMemory(xfile, (int32_t)sizeof(XFILENAME), 0);
    }
    if (file)
    {
        void    *dest;

        dest = &xfile->theFile;
        BAE_CopyFileNameNative(file, dest);
    }
}

// Read a file into memory and return an allocated pointer.
// 0 is ok, -1 failed to open, -2 failed to read, -3 failed memory
// if 0, then *pData is valid
int32_t XGetFileAsData(XFILENAME *pResourceName, XPTR *ppData, int32_t *pSize)
{
    XFILE   ref;
    int32_t    size;
    int32_t    error;
    XPTR    pData;

    error = -3; // failed memory
    pData = NULL;
    if (pResourceName && pSize && ppData)
    {
        *pSize = 0;
        *ppData = NULL;
        ref = XFileOpenForRead(pResourceName);
        if (!ref) {
        } else {
        }
        if (ref)
        {
            size = XFileGetLength(ref);
            pData = XNewPtr(size);
            if (pData)
            {
                if (XFileRead(ref, pData, size) != 0)
                {
                    error = -2; // failed to read
                    XDisposePtr(pData);
                    pData = NULL;
                }
                else
                {
                    error = 0;  // ok
                }
            }
            else
            {
                error = -3; // failed memory
            }
            XFileClose(ref);
            *pSize = size;
        }
        else
        {
            error = -1; // failed open
        }
        *ppData = pData;
    }
    return error;
}

#if USE_CREATION_API == TRUE
// Create a temporary file name and fill an XFILENAME structure. Return -1 for failure, or 0 for sucess.
int32_t XGetTempXFILENAME(XFILENAME* xfilename)
{
#if X_PLATFORM == X_IOS
    return -1;
#endif
#if X_PLATFORM == X_ANDROID
	return -1;
#endif
#if X_PLATFORM == X_MACINTOSH
    char sfn[30] = "/tmp/bae_tmp.XXXXXX";
    char* result = NULL;

    result = mkstemp(sfn);
    XConvertNativeFileToXFILENAME(result, xfilename);
#endif

#if X_PLATFORM == X_MACINTOSH_9
    int16_t           vRefNum;
    int32_t            theDirID;
    OSErr           err;
    FSSpec          fileSpec;
    char            name[64], name2[32];
    int             length;

    // point to the prefs folder
    err = FindFolder(kOnSystemDisk, kTrashFolderType, kCreateFolder, &vRefNum, &theDirID);
    if (err == noErr) 
    {
        XStrCpy(name, "BAE_TEMP_");
        length = XStrLen(name);

        NumToString(XMicroseconds() & 0x3FFF, (unsigned char *)name2);  // create a text plot based upon time
        XStrCpy(&name[length], (char *)XPtoCstr(name2));                // concatinate it to name

        // Create FSSpec structure from file name
        XSetMemory(&fileSpec, (int32_t)sizeof(FSSpec), 0);
        err = FSMakeFSSpec(vRefNum, theDirID, (unsigned char *)XCtoPstr(name), &fileSpec);
        if ((err == noErr) || (err == fnfErr))
        {
            XConvertNativeFileToXFILENAME(&fileSpec, xfilename);
            return 0;   // success
        }
    }
#endif
#if (X_PLATFORM == X_WIN95) || (X_PLATFORM == X_WIN_HARDWARE)
    TCHAR           directory[MAX_PATH];
    TCHAR           path[MAX_PATH];
    unsigned int    length;

    length = GetTempPath(MAX_PATH, directory);
    if ((length != 0) && (length < MAX_PATH))
    {
        if (GetTempFileName(directory, "BAE", 0, path) != 0)
        {
            XConvertNativeFileToXFILENAME(path, xfilename);
            return 0;   // success
        }
    }
#endif
    return -1;  // failure
}
#endif

// Given an open file, return TRUE if this is a valid resource file.
bool XFileIsValidResource(XFILE file)
{
    bool               valid;
    XFILERESOURCEMAP    map;

    valid = FALSE;
    if (file)
    {
        // validate resource file
        XFileSetPosition(file, 0L);     // at start
        if (XFileRead(file, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
        {
            if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
            {
                if (XFILERESOURCE_VERSION_IS_VALID(XGetLong(&map.version)))
                {
                    valid = TRUE;
                }
            }
        }
    }
    return valid;
}

// Given a filename, return TRUE if this is a valid resource file.
bool XFileIsValidResourceFromName(XFILENAME *file)
{
    bool               valid;
    XFILE               reference;

    valid = FALSE;
    if (file)
    {
        reference = XFileOpenForRead(file);
        valid = XFileIsValidResource(reference);
        XFileClose(reference);
    }
    return valid;
}


XFILE XFileOpenResourceFromMemory(XPTR pResource, uint32_t resourceLength, bool allowCopy)
{
    XFILENAME           *pReference;
    XFILERESOURCEMAP    map;
    int16_t           err;

    err = 0;
    pReference = (XFILENAME *)XNewPtr((int32_t)sizeof(XFILENAME));
    if (pReference)
    {
        pReference->pResourceData = pResource;
        pReference->resMemLength = resourceLength;
        pReference->resMemCapacity = resourceLength;
        pReference->resMemOffset = 0;
        pReference->ownsResourceData = FALSE;
        pReference->resizeResourceData = FALSE;
        pReference->resourceFile = TRUE;

        pReference->allowMemCopy = TRUE;

        // NOTE: There's a bug when allowMemCopy is set to FALSE. Needs to be looked into.
        //pReference->allowMemCopy = allowCopy;

        pReference->fileValidID = XPI_BLOCK_3_ID;
        pReference->fileReference = 0;  // Initialize file reference for memory-based resources
        pReference->pCache = NULL;      // Initialize cache pointer
        pReference->readOnly = TRUE;    // Memory-based resources are read-only
        XSetMemory(&pReference->memoryCacheEntry, sizeof(XFILE_CACHED_ITEM), 0);  // Zero cache entry
        if (pReference)
        {
            if (PV_AddResourceFileToOpenFiles(pReference))
            {   // can't open any more files
                err = 1;
            }
            else
            {
                // since we are pointer based, we don't care about caching.

                // validate resource file
                XFileSetPosition(pReference, 0L);        // at start
                if (XFileRead(pReference, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
                {
                    if (!XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)) ||
                        !XFILERESOURCE_VERSION_IS_VALID(XGetLong(&map.version)))
                    {
                        err = 2;
                    }
                }
                else
                {
                    err = 3;
                }
            }
        }
        if (err)
        {
            PV_RemoveResourceFileFromOpenFiles(pReference);  // make sure we remove it from the list
            XDisposePtr(pReference);
            pReference = NULL;
        }
    }
    return pReference;
}

XFILE XFileOpenResource(XFILENAME *file, bool readOnly)
{
    XFILENAME           *pReference;
    XFILERESOURCEMAP    map;
    int32_t                err;

    err = 0;
    pReference = (XFILENAME *)XNewPtr((int32_t)sizeof(XFILENAME));
    if (pReference)
    {
        *pReference = *file;
        pReference->resourceFile = TRUE;
        pReference->fileValidID = XPI_BLOCK_3_ID;
        pReference->pResourceData = NULL;
        pReference->ownsResourceData = FALSE;
        pReference->resizeResourceData = FALSE;
        pReference->allowMemCopy = TRUE;
        pReference->readOnly = readOnly;

        if (readOnly)
        {
            pReference->fileReference = BAE_FileOpenForRead((void *)&pReference->theFile);
            if (pReference->fileReference == (intptr_t)-1)
            {
                XDisposePtr(pReference);
                pReference = NULL;
            }
        }
        else
        {
            pReference->fileReference = BAE_FileOpenForReadWrite((void *)&pReference->theFile);
            if (pReference->fileReference == (intptr_t)-1)
            {
                // must not be there, so create it and prepare it as a resource file
                BAE_FileCreate((void *)&pReference->theFile);
                pReference->fileReference = BAE_FileOpenForReadWrite((void *)&pReference->theFile);
                if (pReference->fileReference == (intptr_t)-1)
                {
                    XDisposePtr(pReference);
                    pReference = NULL;
                }
                else
                {
                    // prepare it as a resource file
                    XFileSetPosition(pReference, 0L);        // at start
                    XPutLong(&map.mapID, XFILERESOURCE_ID);
                    XPutLong(&map.version, XFILERESOURCE_VERSION_RMF);
                    XPutLong(&map.totalResources, 0);
                    XFileWrite(pReference, &map, (int32_t)sizeof(XFILERESOURCEMAP));
                }
            }
        }

        if (pReference)
        {
            // success
            if (PV_AddResourceFileToOpenFiles(pReference))
            {   // can't open any more files
                BAE_FileClose(pReference->fileReference);       // jsc 3/29/00
                XDisposePtr(pReference);
                pReference = NULL;
            }
            else
            {
                pReference->pCache = NULL;
#if USE_FILE_CACHE != 0
//              if (readOnly)
                {
                    // Try to open XFILECACHE_ID cache block.
                    pReference->pCache = (XFILERESOURCECACHE *)XGetFileResource(pReference,
                                                                XFILECACHE_ID, 0, NULL, NULL);
                    if (pReference->pCache != NULL)
                    {
                        XSwapLongsInAccessCache(pReference->pCache, TRUE);
                    }
                    else
                    {
                        pReference->pCache = XCreateAccessCache(pReference);
                    }
                }
#endif
                // validate resource file
                XFileSetPosition(pReference, 0L);        // at start
                if (XFileRead(pReference, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
                {
                    if (!XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)) ||
                        !XFILERESOURCE_VERSION_IS_VALID(XGetLong(&map.version)))
                    {
                        err = -1;
                    }
                }
                else
                {
                    err = -2;
                }
            }
            if (err)
            {
                XDisposePtr(pReference->pCache);
                PV_RemoveResourceFileFromOpenFiles(pReference);  // make sure we remove it from the list
                BAE_FileClose(pReference->fileReference);               // jsc 3/29/00
                XDisposePtr(pReference);
                pReference = NULL;
            }
        }
    }
    return pReference;
}

XFILE XFileOpenVirtualResource(int32_t resourceID)
{
    XFILENAME *pReference;
    XFILERESOURCEMAP map;
    int16_t err;

    err = 0;
    pReference = (XFILENAME *)XNewPtr((int32_t)sizeof(XFILENAME));
    if (!pReference)
    {
        return NULL;
    }

    pReference->fileValidID = XPI_BLOCK_3_ID;
    pReference->fileReference = 0;
    pReference->resourceFile = TRUE;
    pReference->readOnly = FALSE;
    pReference->allowMemCopy = TRUE;
    pReference->pCache = NULL;
    XSetMemory(&pReference->memoryCacheEntry, sizeof(XFILE_CACHED_ITEM), 0);
    pReference->pResourceData = XNewPtr((int32_t)sizeof(XFILERESOURCEMAP));
    pReference->resMemLength = (pReference->pResourceData != NULL) ? (int32_t)sizeof(XFILERESOURCEMAP) : 0;
    pReference->resMemCapacity = pReference->resMemLength;
    pReference->resMemOffset = 0;
    pReference->ownsResourceData = TRUE;
    pReference->resizeResourceData = TRUE;

    if (!pReference->pResourceData)
    {
        XDisposePtr(pReference);
        return NULL;
    }

    if (PV_AddResourceFileToOpenFiles(pReference))
    {
        XDisposePtr(pReference->pResourceData);
        XDisposePtr(pReference);
        return NULL;
    }

    XPutLong(&map.mapID, resourceID);
    XPutLong(&map.version, XFILERESOURCE_VERSION_FOR_ID(resourceID));
    XPutLong(&map.totalResources, 0);

    if (XFileSetPosition(pReference, 0L) != 0 ||
        XFileWrite(pReference, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        err = 1;
    }

    if (err)
    {
        PV_RemoveResourceFileFromOpenFiles(pReference);
        if (pReference->pResourceData)
        {
            XDisposePtr(pReference->pResourceData);
        }
        XDisposePtr(pReference);
        return NULL;
    }

    return pReference;
}

static XFILE PV_OpenWritableResourceMemory(XPTR image,
                                           uint32_t resourceLength,
                                           bool adoptImage)
{
    XFILENAME *pReference;
    XFILERESOURCEMAP map;
    XPTR owned;

    if (!image || resourceLength < (uint32_t)sizeof(XFILERESOURCEMAP))
    {
        return NULL;
    }

    XBlockMove(image, &map, (int32_t)sizeof(XFILERESOURCEMAP));
    if (!XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)) ||
        !XFILERESOURCE_VERSION_IS_VALID(XGetLong(&map.version)))
    {
        return NULL;
    }

    if (adoptImage)
    {
        owned = image;
    }
    else
    {
        owned = XNewPtr((int32_t)resourceLength);
        if (!owned)
        {
            return NULL;
        }
        XBlockMove(image, owned, (int32_t)resourceLength);
    }

    pReference = (XFILENAME *)XNewPtr((int32_t)sizeof(XFILENAME));
    if (!pReference)
    {
        if (!adoptImage)
        {
            XDisposePtr(owned);
        }
        return NULL;
    }

    XSetMemory(pReference, (int32_t)sizeof(XFILENAME), 0);
    pReference->fileValidID = XPI_BLOCK_3_ID;
    pReference->fileReference = 0;
    pReference->resourceFile = TRUE;
    pReference->readOnly = FALSE;
    pReference->allowMemCopy = TRUE;
    pReference->pResourceData = owned;
    pReference->resMemLength = (int32_t)resourceLength;
    pReference->resMemCapacity = (int32_t)resourceLength;
    pReference->resMemOffset = 0;
    pReference->ownsResourceData = TRUE;
    pReference->resizeResourceData = TRUE;
    pReference->pCache = NULL;

    if (PV_AddResourceFileToOpenFiles(pReference))
    {
        if (!adoptImage)
        {
            XDisposePtr(owned);
        }
        /* Adopt failure: caller still owns image — detach before free. */
        pReference->pResourceData = NULL;
        pReference->ownsResourceData = FALSE;
        XDisposePtr(pReference);
        return NULL;
    }

    pReference->pCache = XCreateAccessCache(pReference);
    return pReference;
}

XFILE XFileOpenWritableResourceFromMemory(XPTR pResource, uint32_t resourceLength)
{
    return PV_OpenWritableResourceMemory(pResource, resourceLength, FALSE);
}

XFILE XFileOpenWritableResourceAdoptMemory(XPTR pResource, uint32_t resourceLength)
{
    return PV_OpenWritableResourceMemory(pResource, resourceLength, TRUE);
}

XFILE XFileOpenForReadFromMemory(XPTR pMemoryBlock, uint32_t memoryBlockSize)
{
    XFILENAME   *pReference;

    pReference = (XFILENAME *)XNewPtr((int32_t)sizeof(XFILENAME));
    if (pReference)
    {
        pReference->pResourceData = pMemoryBlock;
        pReference->resMemLength = memoryBlockSize;
        pReference->resMemCapacity = memoryBlockSize;
        pReference->resMemOffset = 0;
        pReference->ownsResourceData = FALSE;
        pReference->resizeResourceData = FALSE;
        pReference->resourceFile = FALSE;
        pReference->allowMemCopy = TRUE;
        pReference->fileValidID = XPI_BLOCK_3_ID;
        pReference->pCache = NULL;
        pReference->fileReference = 0;
    }
    return pReference;
}

XFILE XFileOpenForRead(XFILENAME *file)
{
    XFILENAME   *pReference;

    pReference = (XFILENAME *)XNewPtr((int32_t)sizeof(XFILENAME));
    if (pReference)
    {
        *pReference = *file;
        pReference->resourceFile = FALSE;
        pReference->fileValidID = XPI_BLOCK_3_ID;
        pReference->pResourceData = NULL;
        pReference->ownsResourceData = FALSE;
        pReference->resizeResourceData = FALSE;
        pReference->allowMemCopy = TRUE;
        pReference->pCache = NULL;

        pReference->fileReference = BAE_FileOpenForRead((void *)&pReference->theFile);
    if (pReference->fileReference == (intptr_t)-1)
        {
            XDisposePtr(pReference);
            pReference = NULL;
        }
    }
    return pReference;
}

#if USE_CREATION_API == TRUE
XFILE XFileOpenForWrite(XFILENAME *file, bool create)
{
    XFILENAME   *pReference;

    pReference = (XFILENAME *)XNewPtr((int32_t)sizeof(XFILENAME));
    if (pReference)
    {
        *pReference = *file;
        pReference->resourceFile = FALSE;
        pReference->fileValidID = XPI_BLOCK_3_ID;
        pReference->pResourceData = NULL;
        pReference->ownsResourceData = FALSE;
        pReference->resizeResourceData = FALSE;
        pReference->allowMemCopy = TRUE;
        pReference->pCache = NULL;

        if (create)
        {
            pReference->fileReference = BAE_FileCreate((void *)&pReference->theFile);
        }
        pReference->fileReference = BAE_FileOpenForReadWrite((void *)&pReference->theFile);
    if (pReference->fileReference == (intptr_t)-1)
        {
            XDisposePtr(pReference);
            pReference = NULL;
        }
    }
    return pReference;
}
#endif

// delete file. 0 is ok, -1 for failure
int32_t XFileDelete(XFILENAME *file)
{
    void    *dest;

    dest = &file->theFile;
    return BAE_FileDelete(dest);
}

void XFileClose(XFILE fileRef)
{
    XFILENAME   *pReference;

    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        PV_ClearZmfInstBlockCache(fileRef);
        XFileFreeResourceCache(fileRef);
        pReference->fileValidID = (int32_t)XPI_DEAD_ID;
        if (pReference->pResourceData)
        {
            if (pReference->ownsResourceData)
            {
                XDisposePtr(pReference->pResourceData);
            }
            pReference->pResourceData = NULL;   // clear memory file access
        }
        else
        {
            BAE_FileClose(pReference->fileReference);
        }
        PV_RemoveResourceFileFromOpenFiles(fileRef);
        XDisposePtr(pReference);
    }
}

int32_t XFileRead(XFILE fileRef, XPTR buffer, int32_t bufferLength)
{
    XFILENAME   *pReference;
    int32_t        newLength;
    int32_t        err;

    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        if (pReference->pResourceData)
        {
            err = 0;
            newLength = pReference->resMemOffset + bufferLength;
            if (newLength > pReference->resMemLength)
            {
                bufferLength -= (newLength - pReference->resMemLength);
                err = -1;   // eof
            }
            XBlockMove((char *)pReference->pResourceData + pReference->resMemOffset, 
                        buffer, bufferLength);
            pReference->resMemOffset += bufferLength;
            return err;
        }
        else
        {
            return (BAE_ReadFile(pReference->fileReference, buffer, bufferLength) == bufferLength) ? 0 : -1;
        }
    }
    return -1;
}

int32_t XFileWrite(XFILE fileRef, XPTRC buffer, int32_t bufferLength)
{
    XFILENAME   *pReference;

    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        if (pReference->pResourceData)
        {
            int32_t requiredLength;

            if (pReference->readOnly || !pReference->resizeResourceData)
            {
                return -1;
            }
            if (bufferLength < 0)
            {
                return -1;
            }
            requiredLength = pReference->resMemOffset + bufferLength;
            if (requiredLength < pReference->resMemOffset)
            {
                return -1;
            }
            /* Geometric capacity — exact-grow rebuilds of large banks were O(n²). */
            {
                int32_t capacity = pReference->resMemCapacity;
                if (capacity < pReference->resMemLength)
                {
                    capacity = pReference->resMemLength;
                }
                if (requiredLength > capacity)
                {
                    int32_t newCapacity = (capacity > 0) ? capacity : 256;
                    XPTR grown;

                    while (newCapacity < requiredLength)
                    {
                        if (newCapacity > 0x3FFFFFFF)
                        {
                            return -1;
                        }
                        newCapacity *= 2;
                    }
                    grown = XResizePtr(pReference->pResourceData, newCapacity);
                    if (!grown)
                    {
                        return -1;
                    }
                    pReference->pResourceData = grown;
                    pReference->resMemCapacity = newCapacity;
                }
            }
            if (requiredLength > pReference->resMemLength)
            {
                pReference->resMemLength = requiredLength;
            }
            if (bufferLength > 0)
            {
                XBlockMove((XPTR)buffer,
                           (char *)pReference->pResourceData + pReference->resMemOffset,
                           bufferLength);
            }
            pReference->resMemOffset += bufferLength;
            return 0;
        }
        else
        {
            return (BAE_WriteFile(pReference->fileReference, (XPTR)buffer, bufferLength) == bufferLength) ? 0 : -1;
        }
    }
    return -1;
}

int32_t XFileSetPosition(XFILE fileRef, int32_t filePosition)
{
    XFILENAME   *pReference;
    int32_t        err;

    err = -1;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        if (pReference->pResourceData)
        {
            // Match typical file semantics: seeking to EOF (position == length) is valid.
            if ((filePosition >= 0) && (filePosition <= pReference->resMemLength))
            {
                pReference->resMemOffset = filePosition;
                err = 0;
            }
        }
        else
        {
            err = BAE_SetFilePosition(pReference->fileReference, filePosition);
        }
    }
    return err;
}

int32_t XFileSetPositionRelative(XFILE fileRef, int32_t relativeOffset)
{
    int32_t        pos;
    int32_t        err;

    err = -1;
    pos = XFileGetPosition(fileRef);
    if (pos != -1)
    {
        err = XFileSetPosition(fileRef, pos + relativeOffset);
    }
    return err;
}

static unsigned char * PV_GetFilePositionFromMemoryResource(XFILE fileRef)
{
    XFILENAME   *pReference;
    unsigned char       *pos;

    pos = NULL;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        if (pReference->pResourceData)
        {
            pos = ((unsigned char *)pReference->pResourceData) + pReference->resMemOffset;
        }
    }
    return pos;
}

int32_t XFileGetPosition(XFILE fileRef)
{
    XFILENAME   *pReference;
    int32_t        pos;

    pos = -1;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        if (pReference->pResourceData)
        {
            pos = pReference->resMemOffset;
        }
        else
        {
            pos = BAE_GetFilePosition(pReference->fileReference);
        }
    }
    return pos;
}

#if USE_CREATION_API == TRUE
int32_t XFileSetLength(XFILE fileRef, uint32_t newSize)
{
    XFILENAME   *pReference;
    int32_t        error;

    error = 0;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        if (pReference->pResourceData)
        {
            if (pReference->readOnly || !pReference->resizeResourceData)
            {
                error = -1;
            }
            else
            {
                int32_t capacity = pReference->resMemCapacity;
                if (capacity < pReference->resMemLength)
                {
                    capacity = pReference->resMemLength;
                }
                if ((int32_t)newSize > capacity)
                {
                    int32_t newCapacity = (capacity > 0) ? capacity : 256;
                    XPTR grown;

                    while (newCapacity < (int32_t)newSize)
                    {
                        if (newCapacity > 0x3FFFFFFF)
                        {
                            return -1;
                        }
                        newCapacity *= 2;
                    }
                    grown = XResizePtr(pReference->pResourceData, newCapacity);
                    if (!grown && newSize > 0)
                    {
                        error = -1;
                    }
                    else
                    {
                        pReference->pResourceData = grown;
                        pReference->resMemCapacity = newCapacity;
                    }
                }
                if (error == 0)
                {
                    pReference->resMemLength = (int32_t)newSize;
                    if (pReference->resMemOffset > pReference->resMemLength)
                    {
                        pReference->resMemOffset = pReference->resMemLength;
                    }
                }
            }
        }
        else
        {
            error = BAE_SetFileLength(pReference->fileReference, newSize);
        }
    }
    return (error) ? -1 : 0;
}
#endif

int32_t XFileGetMemoryFileAsData(XFILE fileRef, XPTR *ppData, int32_t *pSize)
{
    XFILENAME *pReference;
    XPTR copy;

    if (!ppData || !pSize)
    {
        return -1;
    }
    *ppData = NULL;
    *pSize = 0;

    pReference = fileRef;
    if (!PV_XFileValid(fileRef) || !pReference->pResourceData)
    {
        return -1;
    }
    if (pReference->resMemLength < 0)
    {
        return -1;
    }
    if (pReference->resMemLength == 0)
    {
        return 0;
    }

    copy = XNewPtr(pReference->resMemLength);
    if (!copy)
    {
        return -1;
    }
    XBlockMove(pReference->pResourceData, copy, pReference->resMemLength);
    *ppData = copy;
    *pSize = pReference->resMemLength;
    return 0;
}

int32_t XFileGetLength(XFILE fileRef)
{
    XFILENAME   *pReference;
    int32_t        pos;

    pos = -1;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        if (pReference->pResourceData)
        {
            pos = pReference->resMemLength;
        }
        else
        {
            pos = BAE_GetFileLength(pReference->fileReference);
        }
    }
    return pos;
}

// search the cache for a particular item
static XFILE_CACHED_ITEM * PV_XGetCacheEntry(XFILE fileRef, XResourceType resourceType, XLongResourceID resourceID)
{
    XFILENAME           *pReference;
    int32_t                count, total;
    XFILERESOURCECACHE  *pCache;
    XFILE_CACHED_ITEM   *pItem;

    pItem = NULL;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        pCache = pReference->pCache;
        if (pCache)
        {
            total = pCache->totalResources;
            for (count = 0; count < total; count++)
            {
                if (pCache->cached[count].resourceType == resourceType)
                {
                    if (pCache->cached[count].resourceID == resourceID)
                    {
                        pItem = &pCache->cached[count];
                        break;
                    }
                }
            }
        }
    }
    return pItem;
}

// search the cache for a particular item with a name
static XFILE_CACHED_ITEM * PV_XGetNamedCacheEntry(XFILE fileRef, XResourceType resourceType, void *cName)
{
    XFILENAME           *pReference;
    int32_t                count, total;
    XFILERESOURCECACHE  *pCache;
    XFILE_CACHED_ITEM   *pItem;
    int32_t                err=0;
    char                tempPascalName[256];
    int32_t                savePos;
    XFILERESOURCEMAP    map;
    int32_t                data, next;

    pItem = NULL;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        if (pReference->pResourceData && (pReference->allowMemCopy == FALSE) )
        {
            //Yes sir!  We have a memory file!
            //Gotta search the whole bleedin' thing!

            XFileSetPosition(fileRef, 0L);      // at start
            if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
            {
                if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
                {
                    next = sizeof(XFILERESOURCEMAP);
                    total = XGetLong(&map.totalResources);
                    for (count = 0; (count < total) && (err == 0); count++)
                    {
                        err = XFileSetPosition(fileRef, next);      // at start
                        if (err == 0)
                        {
                            err = XFileRead(fileRef, &next, (int32_t)sizeof(int32_t));        // get next pointer
                            next = XGetLong(&next);
                            if (next != -1L)
                            {   
                                err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get type

                                if ((XResourceType)XGetLong(&data) == resourceType)
                                {
                                    pReference->memoryCacheEntry.resourceType = (XResourceType)XGetLong(&data);
                                        
                                    err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get ID

                                    pReference->memoryCacheEntry.resourceID = (XLongResourceID)XGetLong(&data);

                                    err = XFileRead(fileRef, &tempPascalName[0], 1L);       // get name
                                    if (tempPascalName[0])
                                    {
                                        err = XFileRead(fileRef, &tempPascalName[1], (int32_t)tempPascalName[0]);

                                        XPtoCstr(tempPascalName);
                                        if (!XStrCmp((char *)tempPascalName, (char *)cName))
                                        {
                                            pItem = &pReference->memoryCacheEntry;
                                            break;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                err = -4;
                                debug_message("Next offset is bad\n");
                                break;
                            }
                        }
                        else
                        {
                            err = -3;
                            debug_message("Can't set next position\n");
                            break;
                        }
                    }
                }
            }
        
        }
        else
        {
            savePos = XFileGetPosition(fileRef);
            pCache = pReference->pCache;
            if (pCache)
            {
                total = pCache->totalResources;
                for (count = 0; count < total; count++)
                {
                    if (pCache->cached[count].resourceType == resourceType)
                    {
                        XFileSetPosition(fileRef, pCache->cached[count].fileOffsetName);
                        err = XFileRead(fileRef, &tempPascalName[0], 1L);       // get name
                        if (tempPascalName[0])
                        {
                            err = XFileRead(fileRef, &tempPascalName[1], (int32_t)tempPascalName[0]);
                            if (XStrCmp((char *)cName, (char *)XPtoCstr(tempPascalName)) == 0)
                            {
                                pItem = &pCache->cached[count];
                                break;
                            }
                        }
                    }
                }
            }
            XFileSetPosition(fileRef, savePos);
        }
    }
    return pItem;
}


// bvk 6/12/97
//  This scans the resource file for the header data and returns the name associated
//      with a type and id.  Taken pretty much verbatim from XGetFileResource (just
//      without the data loading).
// pResourceName is a pascal string
char *  XGetResourceNameOnly(XFILE fileRef, XResourceType resourceType, XLongResourceID resourceID, char *pResourceName)
{
    XFILENAME           *pReference;
    int32_t                err;
    XFILERESOURCEMAP    map;
    int32_t                data, next;
    int32_t                count, total;
    char                tempPascalName[256];
    XFILE_CACHED_ITEM   *pCacheItem;

    BAE_ASSERT(pResourceName);  //MOE: why would anyone call this with NULL?
    if (pResourceName)
    {
        *pResourceName = '\0';
    }
    
    err = 0;
    tempPascalName[0] = 0;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        // first do we have a cache?
        if (pReference->pCache)
        {
            // second check to see if its in the cache
            pCacheItem = PV_XGetCacheEntry(fileRef, resourceType, resourceID);
            if (pCacheItem)
            {
                // get name
                if (pResourceName)
                {
                    XFileSetPosition(fileRef, pCacheItem->fileOffsetName);
                    err = XFileRead(fileRef, &tempPascalName[0], 1L);       // get name
                    if (tempPascalName[0])
                    {
                        err = XFileRead(fileRef, &tempPascalName[1], (int32_t)tempPascalName[0]);
                        XBlockMove(tempPascalName, pResourceName, (int32_t)tempPascalName[0] + 1);
                    }
                }
            }
            else
            {
                err = -1;   // can't find it
            }
        }
        else
        {
            XFileSetPosition(fileRef, 0L);      // at start
            if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
            {
                if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
                {
                    next = sizeof(XFILERESOURCEMAP);
                    total = XGetLong(&map.totalResources);
                    for (count = 0; (count < total) && (err == 0); count++)
                    {
                        err = XFileSetPosition(fileRef, next);      // at start
                        if (err == 0)
                        {
                            err = XFileRead(fileRef, &next, (int32_t)sizeof(int32_t));        // get next pointer
                            next = XGetLong(&next);
                            if (next != -1L)
                            {   
                                err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get type
                                if ((XResourceType)XGetLong(&data) == resourceType)
                                {
                                    err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get ID
                                    if ((XLongResourceID)XGetLong(&data) == resourceID)
                                    {
                                        err = XFileRead(fileRef, &tempPascalName[0], 1L);       // get name
                                        if (tempPascalName[0])
                                        {
                                            err = XFileRead(fileRef, &tempPascalName[1], (int32_t)tempPascalName[0]);
                                            if (pResourceName)
                                            {
                                                XBlockMove(tempPascalName, pResourceName, (int32_t)tempPascalName[0] + 1 );
                                                break;
                                            }
                                        }
                                        err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get length
                                        data = XGetLong(&data);     // get resource size
                                    }
                                }
                            }
                            else
                            {
                                err = -4;
                                debug_message("Next offset is bad\n");
                                break;
                            }
                        }
                        else
                        {
                            err = -3;
                            debug_message("Can't set next position\n");
                            break;
                        }
                    }
                }
            }
        }
    }
#if X_PLATFORM == X_MACINTOSH_9
    else
    {   // use native resource manager
        int16_t       theID;
        uint32_t   theType;
        Handle          theData;
        int16_t       currentResourceFile;

        currentResourceFile = CurResFile();
        UseResFile((int16_t)fileRef);

        SetResLoad(FALSE);
        theData = Get1Resource(resourceType, (int16_t)resourceID);
        SetResLoad(TRUE);
        err = -1;
        if (theData)
        {
            GetResInfo(theData, &theID, &theType, (unsigned char *)pResourceName);
            DetachResource(theData);
            DisposeHandle(theData);
            err = 0;
        }
        UseResFile(currentResourceFile);
    }
#endif
    return (err ? NULL : pResourceName);
}

int32_t XReadPartialFileResource(XFILE fileRef, XResourceType resourceType, XLongResourceID resourceID,
                                char *pResourceName,
                                XPTR *pReturnedBuffer, int32_t bytesToReadAndAllocate)
{
    XFILENAME           *pReference;
    int32_t                err;
    XFILERESOURCEMAP    map;
    int32_t                data, next;
    int32_t                count, total;
    XPTR                pData;
    char                tempPascalName[256];
    XFILE_CACHED_ITEM   *pCacheItem;

    err = 0;
    pData = NULL;
    tempPascalName[0] = 0;
    pReference = fileRef;

    if (PV_XFileValid(fileRef) && pReturnedBuffer && bytesToReadAndAllocate)
    {
        // first do we have a cache?
        if (pReference->pCache)
        {
            // second check to see if its in the cache
            pCacheItem = PV_XGetCacheEntry(fileRef, resourceType, resourceID);
            if (pCacheItem)
            {
                // get name
                if (pResourceName)
                {
                    XFileSetPosition(fileRef, pCacheItem->fileOffsetName);
                    err = XFileRead(fileRef, &tempPascalName[0], 1L);       // get name
                    if (tempPascalName[0])
                    {
                        err = XFileRead(fileRef, &tempPascalName[1], (int32_t)tempPascalName[0]);
                        if (pResourceName)
                        {
                            XBlockMove(tempPascalName, pResourceName, (int32_t)tempPascalName[0] + 1);
                        }
                    }
                }
                // get data
                XFileSetPosition(fileRef, pCacheItem->fileOffsetData);

                // is data memory based?
                if (pReference->pResourceData && (pReference->allowMemCopy == FALSE) )
                {   // don't bother coping data again, since its already in memory
                    pData = PV_GetFilePositionFromMemoryResource(fileRef);
                    if (pData == NULL)
                    {
                        err = -2;
                        debug_message("Out of memory; can't allocate resource\n");
                    }
                }
                else
                {
                    pData = XNewPtr(bytesToReadAndAllocate);
                    if (pData)
                    {
                        err = XFileRead(fileRef, pData, bytesToReadAndAllocate);
                    }
                    else
                    {
                        err = -2;
                        debug_message("Out of memory; can't allocate resource\n");
                    }
                }
            }
            else
            {
                err = -1;   // can't find it
            }
        }
        else
        {
            XFileSetPosition(fileRef, 0L);      // at start
            if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
            {
                if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
                {
                    next = sizeof(XFILERESOURCEMAP);
                    total = XGetLong(&map.totalResources);
                    for (count = 0; (count < total) && (err == 0); count++)
                    {
                        err = XFileSetPosition(fileRef, next);      // at start
                        if (err == 0)
                        {
                            err = XFileRead(fileRef, &next, (int32_t)sizeof(int32_t));        // get next pointer
                            next = XGetLong(&next);
                            if (next != -1L)
                            {   
                                err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get type
                                if ((XResourceType)XGetLong(&data) == resourceType)
                                {
                                    err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get ID
                                    if ((XLongResourceID)XGetLong(&data) == resourceID)
                                    {
                                        err = XFileRead(fileRef, &tempPascalName[0], 1L);       // get name
                                        if (tempPascalName[0])
                                        {
                                            err = XFileRead(fileRef, &tempPascalName[1], (int32_t)tempPascalName[0]);
                                            if (pResourceName)
                                            {
                                                XBlockMove(tempPascalName, pResourceName, (int32_t)tempPascalName[0] + 1);
                                            }
                                        }
                                        err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get length
                                        data = XGetLong(&data);     // get resource size

                                        // get data
                                        // is data memory based?
                                        if (pReference->pResourceData && (pReference->allowMemCopy == FALSE) )
                                        {   // don't bother coping data again, since its already in memory
                                            pData = PV_GetFilePositionFromMemoryResource(fileRef);
                                            if (pData)
                                            {
                                                err = 0;
                                                break;
                                            }
                                            else
                                            {
                                                err = -2;
                                                debug_message("Out of memory; can't allocate resource\n");
                                            }
                                        }
                                        else
                                        {
                                            pData = XNewPtr(bytesToReadAndAllocate);
                                            if (pData)
                                            {
                                                err = XFileRead(fileRef, pData, bytesToReadAndAllocate);
                                                break;
                                            }
                                            else
                                            {
                                                err = -2;
                                                debug_message("Out of memory; can't allocate resource\n");
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            else
                            {
                                err = -4;
                                debug_message("Next offset is bad\n");
                                break;
                            }
                        }
                        else
                        {
                            err = -3;
                            debug_message("Can't set next position\n");
                            break;
                        }
                    }
                }
            }
        }
    }
    else
    {
        err = -1;
    }
    return err;
}


// Read from a resource a particular type and ID. Return the size and name and data block.
// fileRef is an open resource file
// resourceType is a vaild resource type
// resourceID is an ID
// pResourceName is a pascal string. pResourceName can be NULL.
// pReturnedResourceSize be filled with the size of the resource. pReturnedResourceSize can be NULL.
XPTR XGetFileResource(XFILE fileRef, XResourceType resourceType, XLongResourceID resourceID, void *pResourceName, int32_t *pReturnedResourceSize)
{
    XFILENAME           *pReference;
    int32_t                err;
    XFILERESOURCEMAP    map;
    int32_t                data, next;
    int32_t                count, total;
    XPTR                pData;
    char                tempPascalName[256];
    XFILE_CACHED_ITEM   *pCacheItem;

    err = 0;
    pData = NULL;
    if (pReturnedResourceSize)
    {
        *pReturnedResourceSize = 0;
    }
#if DEBUG_PRINT_RESOURCE
    XPutLong(tempPascalName, resourceType);
    tempPascalName[4] = 0;
    debug_message("GetResource %s %ld is ", tempPascalName, resourceID);
#endif
    tempPascalName[0] = 0;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        // first do we have a cache?
        if (pReference->pCache)
        {
            // second check to see if its in the cache
            pCacheItem = PV_XGetCacheEntry(fileRef, resourceType, resourceID);
            if (pCacheItem)
            {
                // get name
                if (pResourceName)
                {
                    XFileSetPosition(fileRef, pCacheItem->fileOffsetName);
                    err = XFileRead(fileRef, &tempPascalName[0], 1L);       // get name
                    if (tempPascalName[0])
                    {
                        err = XFileRead(fileRef, &tempPascalName[1], (int32_t)tempPascalName[0]);
                        if (pResourceName)
                        {
                            XBlockMove(tempPascalName, pResourceName, (int32_t)tempPascalName[0] + 1);
                        }
                    }
                }
                // get data
                XFileSetPosition(fileRef, pCacheItem->fileOffsetData);

                // is data memory based?
                if (pReference->pResourceData && (pReference->allowMemCopy == FALSE) )
                {   // don't bother coping data again, since its already in memory
                    pData = PV_GetFilePositionFromMemoryResource(fileRef);
                    if (pData)
                    {
                        if (pReturnedResourceSize)
                        {
                            *pReturnedResourceSize = pCacheItem->resourceLength;
                        }
                    }
                    else
                    {
                        err = -2;
                        debug_message("Out of memory; can't allocate resource\n");
                    }
                }
                else
                {
                    pData = XNewPtr(pCacheItem->resourceLength);
                    if (pData)
                    {
                        err = XFileRead(fileRef, pData, pCacheItem->resourceLength);
                        if (pReturnedResourceSize)
                        {
                            *pReturnedResourceSize = pCacheItem->resourceLength;
                        }
                    }
                    else
                    {
                        err = -2;
                        debug_message("Out of memory; can't allocate resource\n");
                    }
                }
            }
            else
            {
                err = -1;   // can't find it
#if DEBUG_PRINT_RESOURCE                
                debug_message("Can't find it\n");
#endif                
            }
        }
        else
        {
            XFileSetPosition(fileRef, 0L);      // at start
            if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
            {
                if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
                {
                    next = sizeof(XFILERESOURCEMAP);
                    total = XGetLong(&map.totalResources);
                    for (count = 0; (count < total) && (err == 0); count++)
                    {
                        err = XFileSetPosition(fileRef, next);      // at start
                        if (err == 0)
                        {
                            err = XFileRead(fileRef, &next, (int32_t)sizeof(int32_t));        // get next pointer
                            next = XGetLong(&next);
                            if (next != -1L)
                            {   
                                err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get type
                                if ((XResourceType)XGetLong(&data) == resourceType)
                                {
                                    err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get ID
                                    if ((XLongResourceID)XGetLong(&data) == resourceID)
                                    {
                                        err = XFileRead(fileRef, &tempPascalName[0], 1L);       // get name
                                        if (tempPascalName[0])
                                        {
                                            err = XFileRead(fileRef, &tempPascalName[1], (int32_t)tempPascalName[0]);
                                            if (pResourceName)
                                            {
                                                XBlockMove(tempPascalName, pResourceName, (int32_t)tempPascalName[0] + 1);
                                            }
                                        }
                                        err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get length
                                        data = XGetLong(&data);     // get resource size

                                        // get data
                                        // is data memory based?
                                        if (pReference->pResourceData && (pReference->allowMemCopy == FALSE) )
                                        {   // don't bother coping data again, since its already in memory
                                            pData = PV_GetFilePositionFromMemoryResource(fileRef);
                                            if (pData)
                                            {
                                                if (pReturnedResourceSize)
                                                {
                                                    *pReturnedResourceSize = data;
                                                }
                                                err = 0;
                                                break;
                                            }
                                            else
                                            {
                                                err = -2;
                                                debug_message("Out of memory; can't allocate resource\n");
                                            }
                                        }
                                        else
                                        {
                                            pData = XNewPtr(data);
                                            if (pData)
                                            {
                                                err = XFileRead(fileRef, pData, data);
                                                if (pReturnedResourceSize)
                                                {
                                                    *pReturnedResourceSize = data;
                                                }
                                                break;
                                            }
                                            else
                                            {
                                                err = -2;
                                                debug_message("Out of memory; can't allocate resource\n");
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            else
                            {
                                err = -4;
                                debug_message("Next offset is bad\n");
                                break;
                            }
                        }
                        else
                        {
                            err = -3;
                            debug_message("Can't set next position\n");
                            break;
                        }
                    }
                }
            }
        }
    }
    if (!pData && resourceType == ID_INST)
    {
        pData = PV_GetInstFromZmfBlockByID(fileRef,
                                           resourceID,
                                           pResourceName,
                                           pReturnedResourceSize);
    }
    else if (!pData && resourceType == ID_ALIAS)
    {
        pData = PV_GetAliasFromZmfInstBlockByID(fileRef,
                                                resourceID,
                                                pResourceName,
                                                pReturnedResourceSize);
    }
    else if (!pData && resourceType == ID_SONG)
    {
        pData = PV_GetSongFromZmfBlockByID(fileRef,
                                               resourceID,
                                               pResourceName,
                                               pReturnedResourceSize);
    }
    else if (!pData && (resourceType == ID_BANK || resourceType == ID_MIDI || resourceType == ID_MIDI_OLD))
    {
        pData = PV_GetResourceFromZmfBankBlockByTypeAndID(fileRef,
                                                          resourceType,
                                                          resourceID,
                                                          pResourceName,
                                                          pReturnedResourceSize);
    }
    if (pData && PV_IsSndResourceType(resourceType))
    {
        int32_t currentSize = pReturnedResourceSize ? *pReturnedResourceSize : XGetPtrSize(pData);
        unsigned char *payloadIgnore = NULL;
        int32_t payloadIgnoreSize = 0;
        bool isPayloadRef = PV_IsZmfSndPayloadRef(pData,
                                                  currentSize,
                                                  resourceID,
                                                  &payloadIgnore,
                                                  &payloadIgnoreSize);
        /* Explicit ZSRF magic is unambiguous. Short-form BE16(id)+payload also
         * matches encrypted ESND whose first two bytes equal the resource id —
         * only treat that as a ref when ZSHD has a header for this id. */
        bool isMagicRef = (currentSize >= 8 &&
                           PV_ReadBE32((unsigned char *)pData) == (uint32_t)ZMF_SND_PAYLOAD_REF_MAGIC &&
                           PV_ReadBE32((unsigned char *)pData + 4) == ZMF_SND_PAYLOAD_REF_VERSION);
        bool shouldRebuild = isPayloadRef &&
                             (isMagicRef || PV_HasZmfSndHeader(fileRef, resourceType, resourceID));

        if (shouldRebuild)
        {
            XPTR rebuilt = PV_RebuildSndResourceFromZmfHeaderRef(fileRef,
                                                                 resourceType,
                                                                 resourceID,
                                                                 pResourceName,
                                                                 pData,
                                                                 currentSize,
                                                                 pReturnedResourceSize);
            XDisposePtr(pData);
            pData = rebuilt;
            if (!pData && pReturnedResourceSize)
            {
                *pReturnedResourceSize = 0;
            }
        }
    }
#if DEBUG_PRINT_RESOURCE
    debug_message((pData) ? "OK\n" : "BAD\n");
#endif
    return pData;
}

// Free cache of a resource file
void XFileFreeResourceCache(XFILE fileRef)
{
    XFILENAME *pReference = (XFILENAME *)fileRef;
    if (PV_XFileValid(fileRef) && pReference->pCache)
    {
        XDisposePtr(pReference->pCache);
        pReference->pCache = NULL;
    }
}

// Force a clean/update of the resource file. Simplified: rebuild in‑memory cache.
bool XCleanResourceFileOptionsEx(XFILE fileRef,
                                 bool packInst,
                                 bool packSong,
                                 bool packBank,
                                 bool packSndHeaders)
{
    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    /* Packers are best-effort. Session save must use packInst=FALSE so flat
     * Bank 2+/custom INST are not rebuilt into a Bank-0/1-only ZINS. */
    if (packInst)
    {
        (void)PV_PackInstResourcesIntoZmfBlock(fileRef);
    }
    if (packSong)
    {
        (void)PV_PackSongResourcesIntoZmfBlock(fileRef);
    }
    if (packBank)
    {
        (void)PV_PackBankResourcesIntoZmfBlock(fileRef);
    }
    if (packSndHeaders)
    {
        (void)PV_PackSndHeaderResourcesIntoZmfBlock(fileRef);
    }
    XFileFreeResourceCache(fileRef);
    return (XCreateAccessCache(fileRef) != NULL) ? TRUE : FALSE;
}

bool XCleanResourceFileOptions(XFILE fileRef, bool packInst, bool packSndHeaders)
{
    /* Historic behaviour: always PackSong + PackBank on ZREZ. */
    return XCleanResourceFileOptionsEx(fileRef, packInst, TRUE, TRUE, packSndHeaders);
}

bool XRebuildResourceFileCache(XFILE fileRef)
{
    return XCleanResourceFileOptionsEx(fileRef, FALSE, FALSE, FALSE, FALSE);
}

bool XFinalizeEditorResourceFile(XFILE fileRef)
{
    /* Flat INST/SND/ALIAS/SONG in a ZREZ map are valid; do not Pack or LZMA. */
    return XRebuildResourceFileCache(fileRef);
}

bool XPackResourceFileForShip(XFILE fileRef)
{
    return XCleanResourceFile(fileRef);
}

bool XCleanResourceFileEx(XFILE fileRef, bool packSndHeaders)
{
    return XCleanResourceFileOptions(fileRef, TRUE, packSndHeaders);
}

bool XCleanResourceFile(XFILE fileRef)
{
    return XCleanResourceFileOptions(fileRef, TRUE, TRUE);
}

// Return the Nth resource of a given type from a specific file.
XPTR XGetIndexedFileResource(XFILE fileRef, XResourceType resourceType, XLongResourceID *pReturnedID, int32_t resourceIndex,
                             void *pResourceName, int32_t *pReturnedResourceSize)
{
    if (pReturnedResourceSize) { *pReturnedResourceSize = 0; }
    if (pReturnedID) { *pReturnedID = 0; }
    if (!PV_XFileValid(fileRef) || resourceIndex < 0)
    {
        return NULL;
    }
    XFILENAME *pReference = (XFILENAME *)fileRef;
    if (pReference->pCache == NULL)
    {
        // build cache lazily
        XCreateAccessCache(fileRef);
    }
    if (pReference->pCache)
    {
        int32_t found = 0;
        int32_t flatCount = 0;
        for (int32_t i = 0; i < pReference->pCache->totalResources; i++)
        {
            if (pReference->pCache->cached[i].resourceType == resourceType)
            {
                flatCount++;
            }
        }
        for (int32_t i = 0; i < pReference->pCache->totalResources; i++)
        {
            XFILE_CACHED_ITEM *item = &pReference->pCache->cached[i];
            if (item->resourceType == resourceType)
            {
                if (found == resourceIndex)
                {
                    /* Read THIS cache slot. XGetFileResource(type,id) is wrong when
                     * multiple entries share an id (legacy TRSH all use id 0). */
                    char localName[256];
                    char *nameOut = pResourceName ? (char *)pResourceName : localName;
                    XPTR data = NULL;
                    int32_t size = 0;
                    XLongResourceID rid = (XLongResourceID)item->resourceID;

                    nameOut[0] = 0;
                    if (pReturnedID)
                    {
                        *pReturnedID = rid;
                    }
                    if (!PV_ReadCachedResourceRaw(fileRef, item, nameOut, &data, &size) || !data)
                    {
                        if (data)
                        {
                            XDisposePtr(data);
                        }
                        if (pReturnedResourceSize)
                        {
                            *pReturnedResourceSize = 0;
                        }
                        return NULL;
                    }
                    if (PV_IsSndResourceType(resourceType))
                    {
                        unsigned char *payloadIgnore = NULL;
                        int32_t payloadIgnoreSize = 0;
                        bool isPayloadRef = PV_IsZmfSndPayloadRef(data,
                                                                  size,
                                                                  rid,
                                                                  &payloadIgnore,
                                                                  &payloadIgnoreSize);
                        bool isMagicRef = (size >= 8 &&
                                           PV_ReadBE32((unsigned char *)data) == (uint32_t)ZMF_SND_PAYLOAD_REF_MAGIC &&
                                           PV_ReadBE32((unsigned char *)data + 4) == ZMF_SND_PAYLOAD_REF_VERSION);
                        bool shouldRebuild = isPayloadRef &&
                                             (isMagicRef || PV_HasZmfSndHeader(fileRef, resourceType, rid));
                        if (shouldRebuild)
                        {
                            int32_t rebuiltSize = 0;
                            XPTR rebuilt = PV_RebuildSndResourceFromZmfHeaderRef(fileRef,
                                                                                 resourceType,
                                                                                 rid,
                                                                                 nameOut,
                                                                                 data,
                                                                                 size,
                                                                                 &rebuiltSize);
                            XDisposePtr(data);
                            data = rebuilt;
                            size = rebuiltSize;
                            if (!data)
                            {
                                if (pReturnedResourceSize)
                                {
                                    *pReturnedResourceSize = 0;
                                }
                                return NULL;
                            }
                        }
                    }
                    if (pReturnedResourceSize)
                    {
                        *pReturnedResourceSize = size;
                    }
                    return data;
                }
                found++;
            }
        }
        /* Flat exhausted — for INST/ALIAS continue into ZINS with an index
         * that skips ids already present as flat (same union as Count). */
        if ((resourceType == ID_INST || resourceType == ID_ALIAS) && flatCount > 0)
        {
            int32_t packedCount = (resourceType == ID_INST)
                                      ? PV_CountInstsInZmfBlock(fileRef)
                                      : PV_CountAliasesInZmfBlock(fileRef);
            int32_t need = resourceIndex - flatCount;
            int32_t seen = 0;
            int32_t i;
            for (i = 0; i < packedCount; ++i)
            {
                XLongResourceID id = 0;
                int32_t sz = 0;
                char name[256];
                XPTR data;

                name[0] = 0;
                data = (resourceType == ID_INST)
                           ? PV_GetIndexedInstFromZmfBlock(fileRef, i, &id, name, &sz)
                           : PV_GetIndexedAliasFromZmfInstBlock(fileRef, i, &id, name, &sz);
                if (!data)
                {
                    continue;
                }
                if (PV_CacheHasResourceTypeID(fileRef, resourceType, id))
                {
                    XDisposePtr(data);
                    continue;
                }
                if (seen == need)
                {
                    if (pReturnedID)
                    {
                        *pReturnedID = id;
                    }
                    if (pReturnedResourceSize)
                    {
                        *pReturnedResourceSize = sz;
                    }
                    if (pResourceName)
                    {
                        XBlockMove(name, pResourceName, (int32_t)((unsigned char)name[0]) + 1);
                    }
                    return data;
                }
                XDisposePtr(data);
                seen++;
            }
            return NULL;
        }
    }
    if (resourceType == ID_INST)
    {
        return PV_GetIndexedInstFromZmfBlock(fileRef,
                                             resourceIndex,
                                             pReturnedID,
                                             pResourceName,
                                             pReturnedResourceSize);
    }
    if (resourceType == ID_ALIAS)
    {
        return PV_GetIndexedAliasFromZmfInstBlock(fileRef,
                                                  resourceIndex,
                                                  pReturnedID,
                                                  pResourceName,
                                                  pReturnedResourceSize);
    }
    if (resourceType == ID_SONG)
    {
        return PV_GetIndexedSongFromZmfBlock(fileRef,
                                             resourceIndex,
                                             pReturnedID,
                                             pResourceName,
                                             pReturnedResourceSize);
    }
    if (resourceType == ID_BANK || resourceType == ID_MIDI || resourceType == ID_MIDI_OLD)
    {
        return PV_GetIndexedResourceFromZmfBankBlockByType(fileRef,
                                                           resourceType,
                                                           resourceIndex,
                                                           pReturnedID,
                                                           pResourceName,
                                                           pReturnedResourceSize);
    }
    return NULL;
}



/******************************************************************************
*******************************************************************************
*******************************************************************************
**
**  Functions to manage bank tokens
**
*******************************************************************************
*******************************************************************************
******************************************************************************/


bool AreBankTokensIdentical(XBankToken tok1, XBankToken tok2)
{
    if (tok1.fileLen == tok2.fileLen && tok1.xFile == tok2.xFile)
    {
        return TRUE;
    }
    return FALSE;
}

XBankToken CreateBankToken(void)
{
    XBankToken          retVal;

    retVal.xFile = XFileGetCurrentResourceFile();
    retVal.fileLen = (retVal.xFile) ? XFileGetLength(retVal.xFile) : 0;

    return retVal;
}

XBankToken CreateBankTokenFromInputs(uint32_t tok1, uint32_t tok2)
{
    XBankToken          retVal;
    // Legacy API passed two uint32_t values; interpret tok2 as XFILE pointer value
    // and tok1 as file length. If tok2 was produced by casting a pointer to uint32_t
    // previously, this will require callers to be updated to pass a real XFILE.
    retVal.fileLen = (int32_t)tok1;
    retVal.xFile = (XFILE)(uintptr_t)tok2; // tolerate older callers until migrated
    return retVal;
}



//bvk New!
//  Adds another cache entry to end of cache.

#if USE_CREATION_API == TRUE
static bool PV_AddToAccessCache(XFILE fileRef, XFILE_CACHED_ITEM *cacheItemPtr )
{
    XFILENAME           *pReference;
    XFILERESOURCECACHE  *pCache;
    XFILERESOURCECACHE  *newCache;
    int32_t             oldCount;
    int32_t             newSize;
    int32_t             itemSize;
    XFILE_CACHED_ITEM   *pItem;

    pReference = fileRef;
    if (!PV_XFileValid(fileRef) || !cacheItemPtr)
    {
        return FALSE;
    }
    pCache = pReference->pCache;
    if (!pCache)
    {
        return FALSE;
    }
    oldCount = pCache->totalResources;
    if (oldCount < 0)
    {
        return FALSE;
    }
    /* cached[1] is already in sizeof(XFILERESOURCECACHE); N items need (N-1)
     * extra slots. Growing to oldCount+1 items => extra == oldCount. */
    itemSize = (int32_t)sizeof(XFILE_CACHED_ITEM);
    if (itemSize > 0 &&
        oldCount > (INT32_MAX - (int32_t)sizeof(XFILERESOURCECACHE)) / itemSize)
    {
        return FALSE;
    }
    newSize = (int32_t)sizeof(XFILERESOURCECACHE) + itemSize * oldCount;
    /* XResizePtr copies from the stored block size, so it cannot over-read
     * the FAM allocation used by XCreateAccessCache. */
    newCache = (XFILERESOURCECACHE *)XResizePtr(pCache, newSize);
    if (!newCache)
    {
        return FALSE;
    }
    pReference->pCache = newCache;
    newCache->totalResources = oldCount + 1;
    pItem = &newCache->cached[oldCount];
    *pItem = *cacheItemPtr;
    return TRUE;
}
#endif  // USE_CREATION_API == TRUE

// Build an in‑memory cache of resource headers for faster lookups.
// The original implementation also performed file compaction; that logic was
// badly corrupted during previous refactors and is replaced here with a
// straightforward reader. Compaction is handled elsewhere (XCleanResourceFile).
XFILERESOURCECACHE * XCreateAccessCache(XFILE fileRef)
{
    XFILENAME *pReference = (XFILENAME *)fileRef;
    XFILERESOURCEMAP map;
    int32_t next;
    int32_t total;
    int32_t err;
    XFILERESOURCECACHE *newCache = NULL;

    if (!PV_XFileValid(fileRef))
    {
        return NULL;
    }
    err = XFileSetPosition(fileRef, 0L);
    if (err != 0) { return NULL; }
    if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        return NULL;
    }
    if (!XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)) ||
        !XFILERESOURCE_VERSION_IS_VALID(XGetLong(&map.version)))
    {
        return NULL;
    }
    total = (int32_t)XGetLong(&map.totalResources);
    if (total <= 0)
    {
        return NULL; // nothing to cache
    }
    newCache = (XFILERESOURCECACHE *)XNewPtr((int32_t)sizeof(XFILERESOURCECACHE) +
                                             (int32_t)sizeof(XFILE_CACHED_ITEM) * (total - 1));
    if (!newCache)
    {
        return NULL;
    }
    newCache->totalResources = total;
    next = sizeof(XFILERESOURCEMAP);
    for (int32_t count = 0; count < total; count++)
    {
        XFILE_CACHED_ITEM *item = &newCache->cached[count];
        int32_t headerNext;
        err = XFileSetPosition(fileRef, next);
        if (err != 0) { XDisposePtr(newCache); return NULL; }
        err = XFileRead(fileRef, &headerNext, (int32_t)sizeof(int32_t)); // next pointer
        if (err != 0) { XDisposePtr(newCache); return NULL; }
        headerNext = (int32_t)XGetLong(&headerNext);
        /* -1 is the normal end-of-list marker on the final resource. */
        if (headerNext == -1L && count != total - 1)
        {
            debug_message("[XCreateAccessCache] FAIL: unexpected next=-1 at resource %d of %d\n",
                          (int)count + 1, (int)total);
            XDisposePtr(newCache);
            return NULL;
        }
        int32_t data;
        // type
        if (XFileRead(fileRef, &data, (int32_t)sizeof(int32_t)) != 0) { XDisposePtr(newCache); return NULL; }
        item->resourceType = (int32_t)XGetLong(&data);
        // id
        if (XFileRead(fileRef, &data, (int32_t)sizeof(int32_t)) != 0) { XDisposePtr(newCache); return NULL; }
        item->resourceID = (int32_t)XGetLong(&data);
        // name length (pascal)
        int32_t namePos = XFileGetPosition(fileRef);
        unsigned char nameLen = 0;
        if (XFileRead(fileRef, &nameLen, 1) != 0) { 
            debug_message("[XCreateAccessCache] FAIL: XFileRead(nameLen) failed at resource %d\n", count+1);
            XDisposePtr(newCache); return NULL; 
        }
        if (nameLen > 0)
        {
            // skip remainder of name
            if (XFileSetPositionRelative(fileRef, nameLen) != 0) { 
                debug_message("[XCreateAccessCache] FAIL: XFileSetPositionRelative(nameLen=%d) failed at resource %d\n", nameLen, count+1);
                XDisposePtr(newCache); return NULL; 
            }
        }
        // length
        if (XFileRead(fileRef, &data, (int32_t)sizeof(int32_t)) != 0) { 
            debug_message("[XCreateAccessCache] FAIL: XFileRead(resourceLength) failed at resource %d\n", count+1);
            XDisposePtr(newCache); return NULL; 
        }
        item->resourceLength = (int32_t)XGetLong(&data);
        item->fileOffsetName = namePos;
        item->fileOffsetData = XFileGetPosition(fileRef); // start of data
        // skip data block (but not for the last resource, as it may extend past file end)
        if (count < total - 1)
        {
            if (XFileSetPositionRelative(fileRef, item->resourceLength) != 0) { 
                debug_message("[XCreateAccessCache] FAIL: XFileSetPositionRelative(resourceLength=%d) failed at resource %d\n", item->resourceLength, count+1);
                XDisposePtr(newCache); return NULL; 
            }
        }
        next = headerNext;
    }
    // Replace any existing cache
    if (pReference->pCache)
    {
        XDisposePtr(pReference->pCache);
    }
    pReference->pCache = newCache;
    /* SUCCESS spam was drowning export logs (Clean/pack rebuild caches often). */
    return newCache;
}


/* In-place type/id rewrite for a flat resource (TRSH retire, GONE supersede). */
static bool PV_MarkFileResourceTypeID(XFILE fileRef,
                                      XResourceType resourceType,
                                      XLongResourceID resourceID,
                                      XResourceType newType,
                                      XLongResourceID newID)
{
    XFILENAME           *pReference;
    int32_t                err = 0;
    XFILERESOURCEMAP    map;
    int32_t                data, next;
    int32_t                count, total;
    int32_t                whereType;
    XFILE_CACHED_ITEM   *pCachedItem;

    pReference = fileRef;
    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    /* Writable memory files support in-place marking via XFileWrite.
     * (Older code rejected all pResourceData handles, which broke deletes
     * after EnsureWritable cloned a bank into RAM.) */
    if (PV_IsXFileLocked(fileRef) ||
        (pReference->pResourceData && pReference->resizeResourceData == FALSE))
    {
        return FALSE;
    }

    if (pReference->pCache)
    {
        pCachedItem = PV_XGetCacheEntry(fileRef, resourceType, resourceID);
        if (pCachedItem)
        {
            pCachedItem->resourceType = newType;
            pCachedItem->resourceID = newID;
            whereType = pCachedItem->fileOffsetName;
            whereType -= (int32_t)(sizeof(resourceType) + sizeof(resourceID));
            err = XFileSetPosition(fileRef, whereType);
            if (err != -1)
            {
                XPutLong(&data, (int32_t)newType);
                err = XFileWrite(fileRef, &data, (int32_t)sizeof(int32_t));
                if (err == 0)
                {
                    XPutLong(&data, (int32_t)newID);
                    err = XFileWrite(fileRef, &data, (int32_t)sizeof(int32_t));
                }
            }
            else
            {
                err = -7;
            }
            return (err == 0);
        }
    }

    XFileSetPosition(fileRef, 0L);
    if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        return FALSE;
    }
    if (!XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
    {
        return FALSE;
    }
    next = sizeof(XFILERESOURCEMAP);
    total = XGetLong(&map.totalResources);
    for (count = 0; (count < total) && (err == 0); count++)
    {
        err = XFileSetPosition(fileRef, next);
        if (err != 0)
        {
            err = -3;
            break;
        }
        err = XFileRead(fileRef, &next, (int32_t)sizeof(int32_t));
        next = XGetLong(&next);
        if (next == -1L)
        {
            err = -4;
            break;
        }
        whereType = XFileGetPosition(fileRef);
        err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));
        if ((XResourceType)XGetLong(&data) != resourceType)
        {
            continue;
        }
        err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));
        if ((XLongResourceID)XGetLong(&data) != resourceID)
        {
            continue;
        }
        err = XFileSetPosition(fileRef, whereType);
        XPutLong(&data, (int32_t)newType);
        err = XFileWrite(fileRef, &data, (int32_t)sizeof(int32_t));
        if (err == 0)
        {
            XPutLong(&data, (int32_t)newID);
            err = XFileWrite(fileRef, &data, (int32_t)sizeof(int32_t));
        }
        else
        {
            err = -5;
        }
        break;
    }
    return (err == 0);
}

//  Marks the selected resource as a 'TRSH' type w/ID of 0 (legacy soft-delete:
//  original type/id are lost; body + Pascal name remain). Prefer XTrashFileResource
//  for recoverable deletes. collectTrash=TRUE calls XEmptyFileTrash.
//
//  Not the fastest thing in the world, but none of the resource functions are!
//
#if USE_CREATION_API == TRUE
bool XEmptyFileTrash(XFILE fileRef); /* used by XDeleteFileResource(collectTrash) */

bool XDeleteFileResource(XFILE fileRef, XResourceType resourceType, XLongResourceID resourceID, bool collectTrash )
{
    int32_t err = 0;

    if (PV_MarkFileResourceTypeID(fileRef, resourceType, resourceID,
                                  XFILETRASH_ID, 0) == FALSE)
    {
        err = -1;
    }

    if (collectTrash)
    {
        if (XEmptyFileTrash(fileRef) == FALSE)
        {
            err = -8;
        }
    }
    return (err == 0);
}

bool XRetireFileResource(XFILE fileRef,
                         XResourceType resourceType,
                         XLongResourceID resourceID)
{
    /* Keep original id so multiple supersedes of the same INST stay distinct
     * from TRSH/0 clutter; lookups are by (type,id) so GONE never shadows INST. */
    return PV_MarkFileResourceTypeID(fileRef, resourceType, resourceID,
                                     XFILEGONE_ID, resourceID);
}

/* ---- NeoBAE recoverable trash (ZTRS) ------------------------------------ */

static bool PV_ParseZtrsMeta(XPTR data,
                             int32_t dataSize,
                             XResourceType *outOrigType,
                             XLongResourceID *outOrigID,
                             unsigned char **outPayload,
                             int32_t *outPayloadSize)
{
    unsigned char *p;

    if (outOrigType)
    {
        *outOrigType = 0;
    }
    if (outOrigID)
    {
        *outOrigID = 0;
    }
    if (outPayload)
    {
        *outPayload = NULL;
    }
    if (outPayloadSize)
    {
        *outPayloadSize = 0;
    }
    if (!data || dataSize < XFILETRASH_META_SIZE)
    {
        return FALSE;
    }
    p = (unsigned char *)data;
    if (PV_ReadBE32(p) != (uint32_t)XFILETRASH_META_MAGIC ||
        PV_ReadBE32(p + 4) != XFILETRASH_META_VERSION)
    {
        return FALSE;
    }
    if (outOrigType)
    {
        *outOrigType = (XResourceType)PV_ReadBE32(p + 8);
    }
    if (outOrigID)
    {
        *outOrigID = (XLongResourceID)PV_ReadBE32(p + 12);
    }
    if (outPayload)
    {
        *outPayload = p + XFILETRASH_META_SIZE;
    }
    if (outPayloadSize)
    {
        *outPayloadSize = dataSize - XFILETRASH_META_SIZE;
    }
    return TRUE;
}

static XPTR PV_MakeZtrsWrapped(XResourceType origType,
                               XLongResourceID origID,
                               XPTR payload,
                               int32_t payloadSize,
                               int32_t *outWrappedSize)
{
    XPTR wrapped;
    unsigned char *p;

    if (outWrappedSize)
    {
        *outWrappedSize = 0;
    }
    if (!payload || payloadSize < 0)
    {
        return NULL;
    }
    wrapped = XNewPtr(XFILETRASH_META_SIZE + payloadSize);
    if (!wrapped)
    {
        return NULL;
    }
    p = (unsigned char *)wrapped;
    PV_WriteBE32(p + 0, (uint32_t)XFILETRASH_META_MAGIC);
    PV_WriteBE32(p + 4, XFILETRASH_META_VERSION);
    PV_WriteBE32(p + 8, (uint32_t)origType);
    PV_WriteBE32(p + 12, (uint32_t)origID);
    if (payloadSize > 0)
    {
        XBlockMove(payload, p + XFILETRASH_META_SIZE, payloadSize);
    }
    if (outWrappedSize)
    {
        *outWrappedSize = XFILETRASH_META_SIZE + payloadSize;
    }
    return wrapped;
}

/* Guess original type for legacy TRSH (no ZTRS). Best-effort for recovery. */
static XResourceType PV_GuessTrashedResourceType(XPTR data, int32_t dataSize)
{
    int32_t headerBytes = 0;
    int32_t payloadOffset = 0;

    if (!data || dataSize <= 0)
    {
        return 0;
    }
    if (PV_ParseSndHeaderAndPayload(data, dataSize, &headerBytes, &payloadOffset))
    {
        return ID_SND;
    }
    /* InstrumentResource on disk: keySplitCount @14, then KeySplit[], then
     * tremoloCount + tremoloEnd (typically 0x8000). */
    if (dataSize >= 20)
    {
        unsigned char *p = (unsigned char *)data;
        int16_t keySplits = (int16_t)PV_ReadBE16(p + 12);
        if (keySplits >= 0 && keySplits < 128)
        {
            int32_t tremoloEndOff = 14 + (int32_t)keySplits * (int32_t)sizeof(KeySplit) + 2;
            if (tremoloEndOff + 2 <= dataSize)
            {
                int16_t tremoloEnd = (int16_t)PV_ReadBE16(p + tremoloEndOff);
                if (tremoloEnd == (int16_t)0x8000)
                {
                    return ID_INST;
                }
            }
        }
    }
    if (dataSize >= 4 && PV_ReadBE32((unsigned char *)data) == (uint32_t)FOUR_CHAR('M', 'T', 'h', 'd'))
    {
        return ID_MIDI;
    }
    return 0;
}

/* packForShip: TRUE = full Pack+LZMA Clean; FALSE = editor finalize (cache only). */
static bool PV_CommitVirtualOverFileEx(XFILE fileRef, XFILE outFile, bool packForShip)
{
    XPTR packedData = NULL;
    int32_t packedSize = 0;
    XFILENAME *pReference;

    if (packForShip)
    {
        if (XPackResourceFileForShip(outFile) == FALSE)
        {
            return FALSE;
        }
    }
    else if (XFinalizeEditorResourceFile(outFile) == FALSE)
    {
        return FALSE;
    }
    if (XFileGetMemoryFileAsData(outFile, &packedData, &packedSize) != 0 ||
        !packedData || packedSize <= 0)
    {
        if (packedData)
        {
            XDisposePtr(packedData);
        }
        return FALSE;
    }
    PV_ApplySourceMapVersionToPacked(fileRef, packedData, packedSize);

    XFileFreeResourceCache(fileRef);
    pReference = (XFILENAME *)fileRef;
    if (pReference->pResourceData)
    {
        if (pReference->ownsResourceData)
        {
            XDisposePtr(pReference->pResourceData);
        }
        pReference->pResourceData = packedData;
        pReference->resMemLength = packedSize;
        pReference->resMemCapacity = packedSize;
        pReference->resMemOffset = 0;
        pReference->ownsResourceData = TRUE;
        pReference->resizeResourceData = TRUE;
        pReference->readOnly = FALSE;
        pReference->allowMemCopy = TRUE;
    }
    else
    {
        if (XFileSetLength(fileRef, 0) != 0 ||
            XFileSetPosition(fileRef, 0L) != 0 ||
            XFileWrite(fileRef, packedData, packedSize) != 0)
        {
            XDisposePtr(packedData);
            return FALSE;
        }
        XDisposePtr(packedData);
    }
    return (XCreateAccessCache(fileRef) != NULL) ? TRUE : FALSE;
}

static bool PV_CommitVirtualOverFile(XFILE fileRef, XFILE outFile)
{
    /* Purge/omit/trash: never Pack or LZMA — editor working set stays flat ZREZ. */
    return PV_CommitVirtualOverFileEx(fileRef, outFile, FALSE);
}

static bool PV_LongIdListContains(const XLongResourceID *ids, int32_t count, XLongResourceID id)
{
    int32_t i;
    if (!ids || count <= 0)
    {
        return FALSE;
    }
    for (i = 0; i < count; ++i)
    {
        if (ids[i] == id)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * Rebuild fileRef into a fresh image.
 * - emptyTrash: drop every TRSH
 * - omitTrashIndex >= 0: drop the Nth TRSH only
 * - omitTrashBitmap: if non-NULL, length omitTrashBitmapLen; drop TRSH indices
 *   where the byte is non-zero
 * - omitType/omitID: drop one logical resource (expands ZINS/ZSNG/ZBNK when needed)
 * - omitInstIds/omitSndIds: bulk omit (Unload Bank); expands ZINS once
 */
static bool PV_RebuildFileForTrashOps(XFILE fileRef,
                                      XResourceType omitType,
                                      XLongResourceID omitID,
                                      bool emptyTrash,
                                      int32_t omitTrashIndex,
                                      const unsigned char *omitTrashBitmap,
                                      int32_t omitTrashBitmapLen,
                                      const XLongResourceID *omitInstIds,
                                      int32_t omitInstCount,
                                      const XLongResourceID *omitSndIds,
                                      int32_t omitSndCount)
{
    int32_t mapID = 0;
    XFILE outFile;
    XFILENAME *srcRef;
    int32_t i;
    bool expandInst;
    bool expandSong;
    bool expandBank;
    int32_t trashSeen;

    if (!PV_XFileValid(fileRef) || !PV_GetResourceMapInfo(fileRef, &mapID, NULL))
    {
        return FALSE;
    }
    if (PV_IsXFileLocked(fileRef))
    {
        return FALSE;
    }

    srcRef = (XFILENAME *)fileRef;
    if (srcRef->pCache == NULL)
    {
        XCreateAccessCache(fileRef);
    }
    if (!srcRef->pCache)
    {
        return FALSE;
    }

    expandInst = (omitType == ID_INST || omitType == ID_ALIAS || omitInstCount > 0) ? TRUE : FALSE;
    expandSong = (omitType == ID_SONG) ? TRUE : FALSE;
    expandBank = (omitType == ID_BANK || omitType == ID_MIDI || omitType == ID_MIDI_OLD)
                     ? TRUE
                     : FALSE;
    if (expandBank || expandSong)
    {
        bool hasZbnk = FALSE;
        bool hasZsng = FALSE;
        for (i = 0; i < srcRef->pCache->totalResources; ++i)
        {
            XResourceType rt = (XResourceType)srcRef->pCache->cached[i].resourceType;
            if (rt == ID_ZBNK)
            {
                hasZbnk = TRUE;
            }
            else if (rt == ID_ZSNG)
            {
                hasZsng = TRUE;
            }
        }
        if (!hasZbnk)
        {
            expandBank = FALSE;
        }
        if (!hasZsng)
        {
            expandSong = FALSE;
        }
    }

    outFile = XFileOpenVirtualResource(mapID);
    if (!outFile)
    {
        return FALSE;
    }
    if (srcRef->resMemLength > 0)
    {
        int32_t mapLen = XFileGetLength(outFile);
        if (mapLen > 0 &&
            XFileSetLength(outFile, (uint32_t)srcRef->resMemLength) == 0)
        {
            (void)XFileSetLength(outFile, (uint32_t)mapLen);
        }
    }

    trashSeen = 0;
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item = &srcRef->pCache->cached[i];
        XResourceType rtype = (XResourceType)item->resourceType;
        XLongResourceID rid = (XLongResourceID)item->resourceID;
        XPTR data = NULL;
        int32_t size = 0;
        char name[256];

        /* Replace debris — never carry GONE through rebuilds. */
        if (rtype == XFILEGONE_ID)
        {
            continue;
        }
        if (rtype == XFILETRASH_ID)
        {
            int32_t thisTrash = trashSeen;
            trashSeen++;
            if (emptyTrash)
            {
                continue;
            }
            if (omitTrashIndex >= 0 && thisTrash == omitTrashIndex)
            {
                continue;
            }
            if (omitTrashBitmap && thisTrash >= 0 && thisTrash < omitTrashBitmapLen &&
                omitTrashBitmap[thisTrash] != 0)
            {
                continue;
            }
        }
        if (omitType != 0 && rtype == omitType && rid == omitID)
        {
            continue;
        }
        if (omitSndCount > 0 && PV_IsSndResourceType(rtype) &&
            PV_LongIdListContains(omitSndIds, omitSndCount, rid))
        {
            continue;
        }
        if (expandInst && rtype == ID_ZINS)
        {
            continue;
        }
        if (expandSong && rtype == ID_ZSNG)
        {
            continue;
        }
        if (expandBank && rtype == ID_ZBNK)
        {
            continue;
        }
        /* When omitting a logical INST/SONG/… skip flat peers of the same
         * family — they are re-added from GetIndexed below. */
        if (expandInst && (rtype == ID_INST || rtype == ID_ALIAS))
        {
            continue;
        }
        if (expandSong && rtype == ID_SONG)
        {
            continue;
        }
        if (expandBank &&
            (rtype == ID_BANK || rtype == ID_MIDI || rtype == ID_MIDI_OLD))
        {
            continue;
        }

        name[0] = 0;
        if (!PV_ReadCachedResourceRaw(fileRef, item, name, &data, &size) || !data)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            /* Fail closed — silent skip used to drop kept SONG/Midi/INST. */
            XFileClose(outFile);
            return FALSE;
        }
        if (XAddFileResource(outFile, rtype, rid, name, data, size) != 0)
        {
            XDisposePtr(data);
            XFileClose(outFile);
            return FALSE;
        }
        XDisposePtr(data);
    }

    if (expandInst)
    {
        int32_t count = XCountFileResourcesOfType(fileRef, ID_INST);
        int32_t expectedKeep = 0;
        int32_t keptInst = 0;
        /* Fail closed on any unreadable INST index — silently skipping used to
         * drop Bank 2+ / custom INST while still committing the rebuild. */
        for (i = 0; i < count; ++i)
        {
            XLongResourceID id = 0;
            int32_t sz = 0;
            char name[256];
            XPTR data;
            name[0] = 0;
            data = XGetIndexedFileResource(fileRef, ID_INST, &id, i, name, &sz);
            if (!data)
            {
                XFileClose(outFile);
                return FALSE;
            }
            if ((omitType == ID_INST && id == omitID) ||
                PV_LongIdListContains(omitInstIds, omitInstCount, id))
            {
                XDisposePtr(data);
                continue;
            }
            ++expectedKeep;
            if (XAddFileResource(outFile, ID_INST, id, name, data, sz) != 0)
            {
                XDisposePtr(data);
                XFileClose(outFile);
                return FALSE;
            }
            XDisposePtr(data);
            ++keptInst;
        }
        if (keptInst != expectedKeep ||
            (omitInstCount > 0 && expectedKeep == 0 && count > omitInstCount))
        {
            XFileClose(outFile);
            return FALSE;
        }
        count = XCountFileResourcesOfType(fileRef, ID_ALIAS);
        for (i = 0; i < count; ++i)
        {
            XLongResourceID id = 0;
            int32_t sz = 0;
            char name[256];
            XPTR data;
            name[0] = 0;
            data = XGetIndexedFileResource(fileRef, ID_ALIAS, &id, i, name, &sz);
            if (!data)
            {
                XFileClose(outFile);
                return FALSE;
            }
            if (omitType == ID_ALIAS && id == omitID)
            {
                XDisposePtr(data);
                continue;
            }
            if (XAddFileResource(outFile, ID_ALIAS, id, name, data, sz) != 0)
            {
                XDisposePtr(data);
                XFileClose(outFile);
                return FALSE;
            }
            XDisposePtr(data);
        }
    }
    if (expandSong)
    {
        int32_t count = XCountFileResourcesOfType(fileRef, ID_SONG);
        for (i = 0; i < count; ++i)
        {
            XLongResourceID id = 0;
            int32_t sz = 0;
            char name[256];
            XPTR data;
            name[0] = 0;
            data = XGetIndexedFileResource(fileRef, ID_SONG, &id, i, name, &sz);
            if (!data)
            {
                XFileClose(outFile);
                return FALSE;
            }
            if (id == omitID)
            {
                XDisposePtr(data);
                continue;
            }
            if (XAddFileResource(outFile, ID_SONG, id, name, data, sz) != 0)
            {
                XDisposePtr(data);
                XFileClose(outFile);
                return FALSE;
            }
            XDisposePtr(data);
        }
    }
    if (expandBank)
    {
        static const XResourceType kBankTypes[] = {ID_BANK, ID_MIDI, ID_MIDI_OLD, 0};
        int t;
        for (t = 0; kBankTypes[t] != 0; ++t)
        {
            int32_t count = XCountFileResourcesOfType(fileRef, kBankTypes[t]);
            for (i = 0; i < count; ++i)
            {
                XLongResourceID id = 0;
                int32_t sz = 0;
                char name[256];
                XPTR data;
                name[0] = 0;
                data = XGetIndexedFileResource(fileRef, kBankTypes[t], &id, i, name, &sz);
                if (!data)
                {
                    XFileClose(outFile);
                    return FALSE;
                }
                if (kBankTypes[t] == omitType && id == omitID)
                {
                    XDisposePtr(data);
                    continue;
                }
                if (XAddFileResource(outFile, kBankTypes[t], id, name, data, sz) != 0)
                {
                    XDisposePtr(data);
                    XFileClose(outFile);
                    return FALSE;
                }
                XDisposePtr(data);
            }
        }
    }

    if (PV_CommitVirtualOverFile(fileRef, outFile) == FALSE)
    {
        XFileClose(outFile);
        return FALSE;
    }
    XFileClose(outFile);
    return TRUE;
}

bool XPurgeFileResource(XFILE fileRef, XResourceType theType, XLongResourceID resourceID)
{
    if (!PV_XFileValid(fileRef) || theType == 0)
    {
        return FALSE;
    }
    return PV_RebuildFileForTrashOps(fileRef, theType, resourceID, FALSE, -1, NULL, 0,
                                     NULL, 0, NULL, 0);
}

bool XOmitFileResources(XFILE fileRef,
                        const XResourceType *omitTypes,
                        const XLongResourceID *omitIds,
                        int32_t omitCount)
{
    int32_t mapID = 0;
    XFILE outFile;
    XFILENAME *srcRef;
    int32_t i;
    int32_t j;
    bool keepZshd;
    bool expandBank;
    bool expandSong;

    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    if (omitCount < 0)
    {
        return FALSE;
    }
    if (omitCount == 0)
    {
        return TRUE;
    }
    if (!omitTypes || !omitIds)
    {
        return FALSE;
    }
    if (!PV_GetResourceMapInfo(fileRef, &mapID, NULL) || PV_IsXFileLocked(fileRef))
    {
        return FALSE;
    }

    srcRef = (XFILENAME *)fileRef;
    if (srcRef->pCache == NULL)
    {
        XCreateAccessCache(fileRef);
    }
    if (!srcRef->pCache)
    {
        return FALSE;
    }

    /* Logical Midi/BANK live in ZBNK; SONG in ZSNG — flat omit alone can't
     * remove them (export song-strip was leaving a fat ZBNK behind).
     * Only expand when those packed containers exist; on flat banks, expand
     * would skip every SONG/Midi in the copy loop and re-add via GetIndexed. */
    expandBank = FALSE;
    expandSong = FALSE;
    for (j = 0; j < omitCount; ++j)
    {
        if (omitTypes[j] == ID_BANK || omitTypes[j] == ID_MIDI ||
            omitTypes[j] == ID_MIDI_OLD || omitTypes[j] == ID_ZBNK)
        {
            expandBank = TRUE;
        }
        if (omitTypes[j] == ID_SONG || omitTypes[j] == ID_ZSNG)
        {
            expandSong = TRUE;
        }
    }
    if (expandBank || expandSong)
    {
        bool hasZbnk = FALSE;
        bool hasZsng = FALSE;
        for (i = 0; i < srcRef->pCache->totalResources; ++i)
        {
            XResourceType rt = (XResourceType)srcRef->pCache->cached[i].resourceType;
            if (rt == ID_ZBNK)
            {
                hasZbnk = TRUE;
            }
            else if (rt == ID_ZSNG)
            {
                hasZsng = TRUE;
            }
        }
        if (!hasZbnk)
        {
            expandBank = FALSE;
        }
        if (!hasZsng)
        {
            expandSong = FALSE;
        }
    }

    outFile = XFileOpenVirtualResource(mapID);
    if (!outFile)
    {
        return FALSE;
    }
    /* Pre-size capacity to source length so the copy loop barely reallocs. */
    if (srcRef->resMemLength > 0)
    {
        int32_t mapLen = XFileGetLength(outFile);
        if (mapLen > 0 &&
            XFileSetLength(outFile, (uint32_t)srcRef->resMemLength) == 0)
        {
            (void)XFileSetLength(outFile, (uint32_t)mapLen);
        }
    }

    /* If this omit removes the last SND/CSND/ESND, also drop orphan ZSHD. */
    keepZshd = FALSE;
    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item = &srcRef->pCache->cached[i];
        XResourceType rtype = (XResourceType)item->resourceType;
        XLongResourceID rid = (XLongResourceID)item->resourceID;
        bool omit = FALSE;

        if (!PV_IsSndResourceType(rtype))
        {
            continue;
        }
        for (j = 0; j < omitCount; ++j)
        {
            if (omitTypes[j] == rtype && omitIds[j] == rid)
            {
                omit = TRUE;
                break;
            }
        }
        if (!omit)
        {
            keepZshd = TRUE;
            break;
        }
    }

    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item = &srcRef->pCache->cached[i];
        XResourceType rtype = (XResourceType)item->resourceType;
        XLongResourceID rid = (XLongResourceID)item->resourceID;
        XPTR data = NULL;
        int32_t size = 0;
        char name[256];
        bool omit = FALSE;

        for (j = 0; j < omitCount; ++j)
        {
            if (omitTypes[j] == rtype && omitIds[j] == rid)
            {
                omit = TRUE;
                break;
            }
        }
        if (omit)
        {
            continue;
        }
        if (rtype == XFILEGONE_ID)
        {
            continue;
        }
        if (!keepZshd && rtype == ID_ZSHD)
        {
            continue;
        }
        /* Drop packed containers; re-add kept logical peers below. */
        if (expandBank && rtype == ID_ZBNK)
        {
            continue;
        }
        if (expandSong && rtype == ID_ZSNG)
        {
            continue;
        }
        if (expandBank &&
            (rtype == ID_BANK || rtype == ID_MIDI || rtype == ID_MIDI_OLD))
        {
            continue;
        }
        if (expandSong && rtype == ID_SONG)
        {
            continue;
        }

        name[0] = 0;
        if (!PV_ReadCachedResourceRaw(fileRef, item, name, &data, &size) || !data)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            /* Fail closed on unreadable entries — silent skip used to drop ZINS. */
            XFileClose(outFile);
            return FALSE;
        }
        if (XAddFileResource(outFile, rtype, rid, name, data, size) != 0)
        {
            XDisposePtr(data);
            XFileClose(outFile);
            return FALSE;
        }
        XDisposePtr(data);
    }

    if (expandBank)
    {
        static const XResourceType kBankTypes[] = {ID_BANK, ID_MIDI, ID_MIDI_OLD, 0};
        int t;
        for (t = 0; kBankTypes[t] != 0; ++t)
        {
            int32_t count = XCountFileResourcesOfType(fileRef, kBankTypes[t]);
            for (i = 0; i < count; ++i)
            {
                XLongResourceID id = 0;
                int32_t sz = 0;
                char name[256];
                XPTR data;
                bool omit = FALSE;

                name[0] = 0;
                data = XGetIndexedFileResource(fileRef, kBankTypes[t], &id, i, name, &sz);
                if (!data)
                {
                    XFileClose(outFile);
                    return FALSE;
                }
                for (j = 0; j < omitCount; ++j)
                {
                    if (omitTypes[j] == kBankTypes[t] && omitIds[j] == id)
                    {
                        omit = TRUE;
                        break;
                    }
                }
                if (omit)
                {
                    XDisposePtr(data);
                    continue;
                }
                if (XAddFileResource(outFile, kBankTypes[t], id, name, data, sz) != 0)
                {
                    XDisposePtr(data);
                    XFileClose(outFile);
                    return FALSE;
                }
                XDisposePtr(data);
            }
        }
    }

    if (expandSong)
    {
        int32_t count = XCountFileResourcesOfType(fileRef, ID_SONG);
        for (i = 0; i < count; ++i)
        {
            XLongResourceID id = 0;
            int32_t sz = 0;
            char name[256];
            XPTR data;
            bool omit = FALSE;

            name[0] = 0;
            data = XGetIndexedFileResource(fileRef, ID_SONG, &id, i, name, &sz);
            if (!data)
            {
                XFileClose(outFile);
                return FALSE;
            }
            for (j = 0; j < omitCount; ++j)
            {
                if (omitTypes[j] == ID_SONG && omitIds[j] == id)
                {
                    omit = TRUE;
                    break;
                }
            }
            if (omit)
            {
                XDisposePtr(data);
                continue;
            }
            if (XAddFileResource(outFile, ID_SONG, id, name, data, sz) != 0)
            {
                XDisposePtr(data);
                XFileClose(outFile);
                return FALSE;
            }
            XDisposePtr(data);
        }
    }

    /* No Clean/PackInst here — preserve flat custom INST + ZINS byte-for-byte.
     * Session save runs one Clean at the end. */
    if (PV_CommitVirtualOverFileEx(fileRef, outFile, FALSE) == FALSE)
    {
        XFileClose(outFile);
        return FALSE;
    }
    XFileClose(outFile);
    return TRUE;
}

bool XReplaceFileResource(XFILE fileRef,
                          XResourceType resourceType,
                          XLongResourceID resourceID,
                          void const *pResourceName,
                          void *pData,
                          int32_t length)
{
    XFILENAME *pReference;

    if (!PV_XFileValid(fileRef) || !pData || length <= 0)
    {
        return FALSE;
    }
    pReference = (XFILENAME *)fileRef;
    if (PV_IsXFileLocked(fileRef) ||
        (pReference->pResourceData && pReference->resizeResourceData == FALSE))
    {
        return FALSE;
    }
    if (pReference->pCache == NULL)
    {
        XCreateAccessCache(fileRef);
    }

    /* Fast path: flat resource — mark GONE + append (no SND walk, not Trash).
     * Legacy used XDeleteFileResource→TRSH which filled Edit→Trash with a
     * "*legacy" INST on every instrument-editor write. */
    if (pReference->pCache && PV_XGetCacheEntry(fileRef, resourceType, resourceID))
    {
        if (XRetireFileResource(fileRef, resourceType, resourceID) == FALSE)
        {
            return FALSE;
        }
        return (XAddFileResource(fileRef,
                                 resourceType,
                                 resourceID,
                                 pResourceName,
                                 pData,
                                 length) == 0)
                   ? TRUE
                   : FALSE;
    }

    /* ZINS-packed INST/ALIAS: append a flat resource that shadows the packed
     * entry. Get-by-id prefers flat; avoids recompressing ZINS and rewriting
     * every SND on each instrument-editor Apply (zpatches-sized banks).
     * Do NOT clear the ZINS decode cache — packed bytes are unchanged; clearing
     * forced a full LZMA decompress on the next INST read. */
    if (resourceType == ID_INST || resourceType == ID_ALIAS)
    {
        return (XAddFileResource(fileRef,
                                 resourceType,
                                 resourceID,
                                 pResourceName,
                                 pData,
                                 length) == 0)
                   ? TRUE
                   : FALSE;
    }

    return FALSE;
}

bool XPurgeFileInstrumentAndSoundLists(XFILE fileRef,
                                       const XLongResourceID *omitInstIds,
                                       int32_t omitInstCount,
                                       const XLongResourceID *omitSndIds,
                                       int32_t omitSndCount)
{
    int32_t mapID = 0;
    XFILE outFile;
    XFILENAME *srcRef;
    int32_t i;
    XPTR existingZins = NULL;
    int32_t existingZinsSize = 0;
    XPTR newZinsPlain = NULL;
    int32_t newZinsPlainSize = 0;
    int32_t newZinsCap = 0;
    int32_t keptFromZins = 0;
    int32_t keptFlat = 0;
    uint32_t existingVersion = 0;
    uint32_t existingCount = 0;
    unsigned char *existingEntries = NULL;
    unsigned char *existingEnd = NULL;

    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    if (omitInstCount < 0 || omitSndCount < 0)
    {
        return FALSE;
    }
    if (omitInstCount == 0 && omitSndCount == 0)
    {
        return TRUE;
    }
    if ((omitInstCount > 0 && !omitInstIds) || (omitSndCount > 0 && !omitSndIds))
    {
        return FALSE;
    }
    if (!PV_GetResourceMapInfo(fileRef, &mapID, NULL) || PV_IsXFileLocked(fileRef))
    {
        return FALSE;
    }

    srcRef = (XFILENAME *)fileRef;
    if (srcRef->pCache == NULL)
    {
        XCreateAccessCache(fileRef);
    }
    if (!srcRef->pCache)
    {
        return FALSE;
    }

    /* Filter ZINS in place (no GetIndexed expand). Expanding used to drop
     * Bank 2+ when any INST index failed to read. */
    if (omitInstCount > 0)
    {
        bool hasZinsResource = FALSE;
        for (i = 0; i < srcRef->pCache->totalResources; ++i)
        {
            if ((XResourceType)srcRef->pCache->cached[i].resourceType == ID_ZINS)
            {
                hasZinsResource = TRUE;
                break;
            }
        }
        existingZins = PV_LoadZmfInstBlock(fileRef, &existingZinsSize);
        if (hasZinsResource &&
            (!existingZins ||
             !PV_ParseZmfInstBlockHeader(existingZins,
                                         existingZinsSize,
                                         &existingVersion,
                                         &existingCount,
                                         &existingEntries,
                                         &existingEnd)))
        {
            /* Fail closed — do not rebuild without being able to read ZINS. */
            if (existingZins)
            {
                XDisposePtr(existingZins);
            }
            return FALSE;
        }
        if (existingZins &&
            PV_ParseZmfInstBlockHeader(existingZins,
                                       existingZinsSize,
                                       &existingVersion,
                                       &existingCount,
                                       &existingEntries,
                                       &existingEnd))
        {
            unsigned char header[12];
            unsigned char *p = existingEntries;

            PV_WriteBE32(header + 0, (uint32_t)ZMF_INST_BLOCK_MAGIC);
            PV_WriteBE32(header + 4, ZMF_INST_BLOCK_VERSION_V2);
            PV_WriteBE32(header + 8, 0); /* filled after count */
            if (!PV_AppendBytes(&newZinsPlain, &newZinsPlainSize, &newZinsCap, header, 12))
            {
                XDisposePtr(existingZins);
                return FALSE;
            }
            for (i = 0; i < (int32_t)existingCount; ++i)
            {
                uint32_t type = (uint32_t)ID_INST;
                uint32_t id;
                uint16_t nameLen;
                uint32_t dataLen;
                unsigned char *namePtr;
                unsigned char *dataPtr;
                char pName[256];

                if (existingVersion >= ZMF_INST_BLOCK_VERSION_V2)
                {
                    if (p + 4 > existingEnd)
                    {
                        break;
                    }
                    type = PV_ReadBE32(p);
                    p += 4;
                }
                if (p + 10 > existingEnd)
                {
                    break;
                }
                id = PV_ReadBE32(p);
                p += 4;
                nameLen = PV_ReadBE16(p);
                p += 2;
                dataLen = PV_ReadBE32(p);
                p += 4;
                if (p + nameLen + dataLen > existingEnd)
                {
                    break;
                }
                namePtr = p;
                p += nameLen;
                dataPtr = p;
                p += dataLen;
                if (type != (uint32_t)ID_INST && type != (uint32_t)ID_ALIAS)
                {
                    continue;
                }
                if (PV_LongIdListContains(omitInstIds, omitInstCount, (XLongResourceID)id))
                {
                    continue;
                }
                pName[0] = (char)((nameLen > 255u) ? 255u : nameLen);
                if (pName[0] > 0)
                {
                    XBlockMove(namePtr, pName + 1, (int32_t)(uint8_t)pName[0]);
                }
                if (!PV_AppendInstEntryToZmfPlain(&newZinsPlain,
                                                  &newZinsPlainSize,
                                                  &newZinsCap,
                                                  NULL,
                                                  (XResourceType)type,
                                                  (XLongResourceID)id,
                                                  pName,
                                                  dataPtr,
                                                  (int32_t)dataLen))
                {
                    XDisposePtr(newZinsPlain);
                    XDisposePtr(existingZins);
                    return FALSE;
                }
                ++keptFromZins;
            }
            PV_WriteBE32((unsigned char *)newZinsPlain + 8, (uint32_t)keptFromZins);
        }
        if (existingZins)
        {
            XDisposePtr(existingZins);
            existingZins = NULL;
        }
    }

    outFile = XFileOpenVirtualResource(mapID);
    if (!outFile)
    {
        if (newZinsPlain)
        {
            XDisposePtr(newZinsPlain);
        }
        return FALSE;
    }

    for (i = 0; i < srcRef->pCache->totalResources; ++i)
    {
        XFILE_CACHED_ITEM *item = &srcRef->pCache->cached[i];
        XResourceType rtype = (XResourceType)item->resourceType;
        XLongResourceID rid = (XLongResourceID)item->resourceID;
        XPTR data = NULL;
        int32_t size = 0;
        char name[256];

        if (rtype == ID_ZINS && omitInstCount > 0)
        {
            continue; /* replaced below when filtering INST */
        }
        if (omitInstCount > 0 && (rtype == ID_INST || rtype == ID_ALIAS))
        {
            if (PV_LongIdListContains(omitInstIds, omitInstCount, rid))
            {
                continue;
            }
            /* Flat keeper: append into new ZINS plain (flat wins over packed). */
            name[0] = 0;
            if (!PV_ReadCachedResourceRaw(fileRef, item, name, &data, &size) || !data)
            {
                if (data)
                {
                    XDisposePtr(data);
                }
                XFileClose(outFile);
                if (newZinsPlain)
                {
                    XDisposePtr(newZinsPlain);
                }
                return FALSE;
            }
            if (!newZinsPlain)
            {
                unsigned char header[12];
                PV_WriteBE32(header + 0, (uint32_t)ZMF_INST_BLOCK_MAGIC);
                PV_WriteBE32(header + 4, ZMF_INST_BLOCK_VERSION_V2);
                PV_WriteBE32(header + 8, 0);
                if (!PV_AppendBytes(&newZinsPlain, &newZinsPlainSize, &newZinsCap, header, 12))
                {
                    XDisposePtr(data);
                    XFileClose(outFile);
                    return FALSE;
                }
            }
            /* Drop any prior packed entry with same id (flat wins). */
            /* Simpler: append flat; PackInst-style duplicate ids: readers prefer flat
             * but we have only ZINS — skip append if id already in plain. */
            {
                unsigned char *scan = (unsigned char *)newZinsPlain + 12;
                unsigned char *end = (unsigned char *)newZinsPlain + newZinsPlainSize;
                bool dup = FALSE;
                while (scan + 14 <= end)
                {
                    uint32_t t = PV_ReadBE32(scan);
                    uint32_t sid = PV_ReadBE32(scan + 4);
                    uint16_t nlen = PV_ReadBE16(scan + 8);
                    uint32_t dlen = PV_ReadBE32(scan + 10);
                    scan += 14;
                    if (scan + nlen + dlen > end)
                    {
                        break;
                    }
                    if ((t == (uint32_t)ID_INST || t == (uint32_t)ID_ALIAS) &&
                        sid == (uint32_t)rid)
                    {
                        dup = TRUE;
                        break;
                    }
                    scan += nlen + dlen;
                }
                if (!dup)
                {
                    if (!PV_AppendInstEntryToZmfPlain(&newZinsPlain,
                                                      &newZinsPlainSize,
                                                      &newZinsCap,
                                                      NULL,
                                                      rtype,
                                                      rid,
                                                      name,
                                                      data,
                                                      size))
                    {
                        XDisposePtr(data);
                        XDisposePtr(newZinsPlain);
                        XFileClose(outFile);
                        return FALSE;
                    }
                    ++keptFlat;
                    PV_WriteBE32((unsigned char *)newZinsPlain + 8,
                                 (uint32_t)(keptFromZins + keptFlat));
                }
            }
            XDisposePtr(data);
            continue;
        }
        if (omitSndCount > 0 && PV_IsSndResourceType(rtype) &&
            PV_LongIdListContains(omitSndIds, omitSndCount, rid))
        {
            continue;
        }

        name[0] = 0;
        if (!PV_ReadCachedResourceRaw(fileRef, item, name, &data, &size) || !data)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            continue;
        }
        if (XAddFileResource(outFile, rtype, rid, name, data, size) != 0)
        {
            XDisposePtr(data);
            if (newZinsPlain)
            {
                XDisposePtr(newZinsPlain);
            }
            XFileClose(outFile);
            return FALSE;
        }
        XDisposePtr(data);
    }

    if (omitInstCount > 0)
    {
        int32_t totalKept = keptFromZins + keptFlat;
        /* Before omit, if the bank had instruments, we must keep some when
         * omit list is a proper subset. */
        {
            int32_t before = XCountFileResourcesOfType(fileRef, ID_INST);
            if (before > omitInstCount && totalKept <= 0)
            {
                if (newZinsPlain)
                {
                    XDisposePtr(newZinsPlain);
                }
                XFileClose(outFile);
                return FALSE;
            }
        }
        if (newZinsPlain && totalKept > 0)
        {
            /* Editor policy: emit kept INST/ALIAS as flat resources — do not
             * re-LZMA a ZINS block (Unload Bank used to stall on large ZSB). */
            unsigned char *p = (unsigned char *)newZinsPlain + 12;
            unsigned char *end = (unsigned char *)newZinsPlain + newZinsPlainSize;
            int32_t emitted = 0;

            PV_WriteBE32((unsigned char *)newZinsPlain + 8, (uint32_t)totalKept);
            while (p + 14 <= end && emitted < totalKept)
            {
                uint32_t type = PV_ReadBE32(p);
                uint32_t id = PV_ReadBE32(p + 4);
                uint16_t nameLen = PV_ReadBE16(p + 8);
                uint32_t dataLen = PV_ReadBE32(p + 10);
                unsigned char *namePtr;
                unsigned char *dataPtr;
                char pName[256];

                p += 14;
                if (p + nameLen + dataLen > end)
                {
                    break;
                }
                namePtr = p;
                p += nameLen;
                dataPtr = p;
                p += dataLen;
                if (type != (uint32_t)ID_INST && type != (uint32_t)ID_ALIAS)
                {
                    continue;
                }
                pName[0] = (char)((nameLen > 255u) ? 255u : nameLen);
                if (pName[0] > 0)
                {
                    XBlockMove(namePtr, pName + 1, (int32_t)(uint8_t)pName[0]);
                }
                if (XAddFileResource(outFile,
                                     (XResourceType)type,
                                     (XLongResourceID)id,
                                     pName,
                                     dataPtr,
                                     (int32_t)dataLen) != 0)
                {
                    XDisposePtr(newZinsPlain);
                    XFileClose(outFile);
                    return FALSE;
                }
                ++emitted;
            }
            XDisposePtr(newZinsPlain);
            newZinsPlain = NULL;
        }
        else if (newZinsPlain)
        {
            XDisposePtr(newZinsPlain);
        }
    }
    else if (newZinsPlain)
    {
        XDisposePtr(newZinsPlain);
    }

    if (PV_CommitVirtualOverFile(fileRef, outFile) == FALSE)
    {
        XFileClose(outFile);
        return FALSE;
    }
    XFileClose(outFile);
    return TRUE;
}

bool XCompactFileGoneResources(XFILE fileRef)
{
    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    if (XCountFileResourcesOfType(fileRef, XFILEGONE_ID) <= 0)
    {
        return TRUE;
    }
    /* Rebuild skips GONE unconditionally. */
    return PV_RebuildFileForTrashOps(fileRef, 0, 0, FALSE, -1, NULL, 0,
                                     NULL, 0, NULL, 0);
}

bool XEmptyFileTrash(XFILE fileRef)
{
    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    if (XCountFileResourcesOfType(fileRef, XFILETRASH_ID) <= 0)
    {
        return TRUE;
    }
    return PV_RebuildFileForTrashOps(fileRef, 0, 0, TRUE, -1, NULL, 0,
                                     NULL, 0, NULL, 0);
}

bool XPurgeIndexedFileTrash(XFILE fileRef, int32_t trashIndex)
{
    if (!PV_XFileValid(fileRef) || trashIndex < 0)
    {
        return FALSE;
    }
    if (trashIndex >= XCountFileResourcesOfType(fileRef, XFILETRASH_ID))
    {
        return FALSE;
    }
    return PV_RebuildFileForTrashOps(fileRef, 0, 0, FALSE, trashIndex, NULL, 0,
                                     NULL, 0, NULL, 0);
}

bool XDedupeFileTrash(XFILE fileRef, int32_t *pRemovedCount)
{
    int32_t count;
    int32_t i;
    int32_t j;
    unsigned char *omit = NULL;
    int32_t removed = 0;
    XPTR *bodies = NULL;
    int32_t *sizes = NULL;
    char *names = NULL;

    if (pRemovedCount)
    {
        *pRemovedCount = 0;
    }
    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    count = XCountFileResourcesOfType(fileRef, XFILETRASH_ID);
    if (count <= 1)
    {
        return TRUE;
    }

    omit = (unsigned char *)XNewPtr(count);
    bodies = (XPTR *)XNewPtr((int32_t)sizeof(XPTR) * count);
    sizes = (int32_t *)XNewPtr((int32_t)sizeof(int32_t) * count);
    names = (char *)XNewPtr(count * 256);
    if (!omit || !bodies || !sizes || !names)
    {
        if (omit) XDisposePtr(omit);
        if (bodies) XDisposePtr(bodies);
        if (sizes) XDisposePtr(sizes);
        if (names) XDisposePtr(names);
        return FALSE;
    }
    XSetMemory(omit, count, 0);
    XSetMemory(bodies, (int32_t)sizeof(XPTR) * count, 0);
    XSetMemory(sizes, (int32_t)sizeof(int32_t) * count, 0);
    XSetMemory(names, count * 256, 0);

    for (i = 0; i < count; ++i)
    {
        XLongResourceID tid = 0;
        char *name_i = names + (i * 256);
        name_i[0] = 0;
        bodies[i] = XGetIndexedFileResource(fileRef, XFILETRASH_ID, &tid, i, name_i, &sizes[i]);
    }

    for (i = 0; i < count; ++i)
    {
        char *name_i;
        if (omit[i] || !bodies[i])
        {
            continue;
        }
        name_i = names + (i * 256);
        for (j = i + 1; j < count; ++j)
        {
            char *name_j;
            if (omit[j] || !bodies[j])
            {
                continue;
            }
            name_j = names + (j * 256);
            if (sizes[j] != sizes[i])
            {
                continue;
            }
            if (name_i[0] != name_j[0])
            {
                continue;
            }
            if (name_i[0] > 0 &&
                XMemCmp(name_i + 1, name_j + 1, (int32_t)(unsigned char)name_i[0]) != 0)
            {
                continue;
            }
            if (sizes[i] > 0 && XMemCmp(bodies[i], bodies[j], sizes[i]) != 0)
            {
                continue;
            }
            omit[j] = 1;
            removed++;
        }
    }

    for (i = 0; i < count; ++i)
    {
        if (bodies[i])
        {
            XDisposePtr(bodies[i]);
        }
    }
    XDisposePtr(bodies);
    XDisposePtr(sizes);
    XDisposePtr(names);

    if (removed <= 0)
    {
        XDisposePtr(omit);
        return TRUE;
    }

    if (PV_RebuildFileForTrashOps(fileRef, 0, 0, FALSE, -1, omit, count,
                                  NULL, 0, NULL, 0) == FALSE)
    {
        XDisposePtr(omit);
        return FALSE;
    }
    XDisposePtr(omit);
    if (pRemovedCount)
    {
        *pRemovedCount = removed;
    }
    return TRUE;
}

int32_t XCountFileTrash(XFILE fileRef)
{
    return XCountFileResourcesOfType(fileRef, XFILETRASH_ID);
}

bool XTrashFileResources(XFILE fileRef,
                         const XResourceType *types,
                         const XLongResourceID *ids,
                         int32_t count)
{
    XResourceType *omitTypes = NULL;
    XLongResourceID *omitIds = NULL;
    int32_t omitCount = 0;
    int32_t i;

    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    if (count < 0)
    {
        return FALSE;
    }
    if (count == 0)
    {
        return TRUE;
    }
    if (!types || !ids)
    {
        return FALSE;
    }

    omitTypes = (XResourceType *)XNewPtr(sizeof(XResourceType) * (uint32_t)count);
    omitIds = (XLongResourceID *)XNewPtr(sizeof(XLongResourceID) * (uint32_t)count);
    if (!omitTypes || !omitIds)
    {
        if (omitTypes)
        {
            XDisposePtr(omitTypes);
        }
        if (omitIds)
        {
            XDisposePtr(omitIds);
        }
        return FALSE;
    }

    for (i = 0; i < count; ++i)
    {
        XPTR data = NULL;
        int32_t dataSize = 0;
        char pName[256];
        XPTR wrapped = NULL;
        int32_t wrappedSize = 0;
        XLongResourceID trashID = 1;
        const XResourceType theType = types[i];
        const XLongResourceID resourceID = ids[i];
        int32_t o;

        if (theType == 0 || theType == XFILETRASH_ID)
        {
            continue;
        }

        /* Skip duplicates in the batch list. */
        for (o = 0; o < omitCount; ++o)
        {
            if (omitTypes[o] == theType && omitIds[o] == resourceID)
            {
                break;
            }
        }
        if (o < omitCount)
        {
            continue;
        }

        pName[0] = 0;
        data = XGetFileResource(fileRef, theType, resourceID, pName, &dataSize);
        if (!data || dataSize <= 0)
        {
            if (data)
            {
                XDisposePtr(data);
            }
            continue;
        }

        wrapped = PV_MakeZtrsWrapped(theType, resourceID, data, dataSize, &wrappedSize);
        XDisposePtr(data);
        if (!wrapped || wrappedSize <= 0)
        {
            if (wrapped)
            {
                XDisposePtr(wrapped);
            }
            continue;
        }

        if (XGetUniqueFileResourceID(fileRef, XFILETRASH_ID, &trashID) != 0)
        {
            /* Trash entry ids are arbitrary; resourceID 0 is a valid original id
             * but may already be taken as a trash id — prefer a unique mint. */
            trashID = 1;
            (void)XMakeUniqueFileResourceID(fileRef, XFILETRASH_ID, &trashID);
        }

        if (XAddFileResource(fileRef, XFILETRASH_ID, trashID, pName, wrapped, wrappedSize) != 0)
        {
            XDisposePtr(wrapped);
            continue;
        }
        XDisposePtr(wrapped);

        omitTypes[omitCount] = theType;
        omitIds[omitCount] = resourceID;
        omitCount++;
    }

    if (omitCount == 0)
    {
        XDisposePtr(omitTypes);
        XDisposePtr(omitIds);
        return FALSE;
    }

    /* One omit rebuild — no per-item Clean/ZSHD pack (same as XOmitFileResources). */
    if (XOmitFileResources(fileRef, omitTypes, omitIds, omitCount) == FALSE)
    {
        XDisposePtr(omitTypes);
        XDisposePtr(omitIds);
        return FALSE;
    }
    XDisposePtr(omitTypes);
    XDisposePtr(omitIds);
    return TRUE;
}

bool XTrashFileResource(XFILE fileRef, XResourceType theType, XLongResourceID resourceID)
{
    return XTrashFileResources(fileRef, &theType, &resourceID, 1);
}

bool XGetIndexedFileTrashInfo(XFILE fileRef,
                              int32_t trashIndex,
                              XLongResourceID *pTrashResourceID,
                              XResourceType *pOriginalType,
                              XLongResourceID *pOriginalID,
                              char *cName,
                              int32_t *pPayloadSize,
                              bool *pHasMeta,
                              bool *pTypeGuessed)
{
    XLongResourceID trashID = 0;
    int32_t size = 0;
    char pName[256];
    XPTR data;
    XResourceType origType = 0;
    XLongResourceID origID = 0;
    unsigned char *payload = NULL;
    int32_t payloadSize = 0;
    bool hasMeta;
    bool guessed = FALSE;

    if (pTrashResourceID)
    {
        *pTrashResourceID = 0;
    }
    if (pOriginalType)
    {
        *pOriginalType = 0;
    }
    if (pOriginalID)
    {
        *pOriginalID = 0;
    }
    if (cName)
    {
        cName[0] = 0;
    }
    if (pPayloadSize)
    {
        *pPayloadSize = 0;
    }
    if (pHasMeta)
    {
        *pHasMeta = FALSE;
    }
    if (pTypeGuessed)
    {
        *pTypeGuessed = FALSE;
    }
    if (!PV_XFileValid(fileRef) || trashIndex < 0)
    {
        return FALSE;
    }

    pName[0] = 0;
    data = XGetIndexedFileResource(fileRef, XFILETRASH_ID, &trashID, trashIndex, pName, &size);
    if (!data)
    {
        return FALSE;
    }

    hasMeta = PV_ParseZtrsMeta(data, size, &origType, &origID, &payload, &payloadSize);
    if (!hasMeta)
    {
        payload = (unsigned char *)data;
        payloadSize = size;
        origType = PV_GuessTrashedResourceType(data, size);
        origID = trashID; /* often 0 for legacy */
        guessed = (origType != 0) ? TRUE : FALSE;
    }

    if (pTrashResourceID)
    {
        *pTrashResourceID = trashID;
    }
    if (pOriginalType)
    {
        *pOriginalType = origType;
    }
    if (pOriginalID)
    {
        *pOriginalID = origID;
    }
    if (pPayloadSize)
    {
        *pPayloadSize = payloadSize;
    }
    if (pHasMeta)
    {
        *pHasMeta = hasMeta;
    }
    if (pTypeGuessed)
    {
        *pTypeGuessed = guessed;
    }
    if (cName)
    {
        if (pName[0] != 0)
        {
            XBlockMove(pName, cName, (int32_t)((unsigned char)pName[0]) + 1);
            XPtoCstr(cName);
        }
        else
        {
            cName[0] = 0;
        }
    }
    XDisposePtr(data);
    return TRUE;
}

bool XRestoreIndexedFileTrash(XFILE fileRef,
                              int32_t trashIndex,
                              XLongResourceID *pRestoredID)
{
    XLongResourceID trashID = 0;
    int32_t size = 0;
    char pName[256];
    XPTR data;
    XResourceType origType = 0;
    XLongResourceID origID = 0;
    unsigned char *payload = NULL;
    int32_t payloadSize = 0;
    XPTR payloadCopy = NULL;
    bool hasMeta;

    if (pRestoredID)
    {
        *pRestoredID = 0;
    }
    if (!PV_XFileValid(fileRef) || trashIndex < 0)
    {
        return FALSE;
    }

    pName[0] = 0;
    data = XGetIndexedFileResource(fileRef, XFILETRASH_ID, &trashID, trashIndex, pName, &size);
    if (!data || size <= 0)
    {
        if (data)
        {
            XDisposePtr(data);
        }
        return FALSE;
    }

    hasMeta = PV_ParseZtrsMeta(data, size, &origType, &origID, &payload, &payloadSize);
    if (!hasMeta)
    {
        payload = (unsigned char *)data;
        payloadSize = size;
        origType = PV_GuessTrashedResourceType(data, size);
        origID = 0;
    }
    if (origType == 0 || !payload || payloadSize <= 0)
    {
        XDisposePtr(data);
        return FALSE;
    }

    payloadCopy = XNewPtr(payloadSize);
    if (!payloadCopy)
    {
        XDisposePtr(data);
        return FALSE;
    }
    XBlockMove(payload, payloadCopy, payloadSize);
    XDisposePtr(data);

    /* Resource id 0 is valid. Only mint a new id when the trash entry had no
     * original id metadata, or the original id is already taken. */
    if (!hasMeta || XExistsFileResource(fileRef, origType, origID) != FALSE)
    {
        if (XGetUniqueFileResourceID(fileRef, origType, &origID) != 0)
        {
            XDisposePtr(payloadCopy);
            return FALSE;
        }
    }

    if (XAddFileResource(fileRef, origType, origID, pName, payloadCopy, payloadSize) != 0)
    {
        XDisposePtr(payloadCopy);
        return FALSE;
    }
    XDisposePtr(payloadCopy);

    if (XPurgeIndexedFileTrash(fileRef, trashIndex) == FALSE)
    {
        return FALSE;
    }
    if (pRestoredID)
    {
        *pRestoredID = origID;
    }
    return TRUE;
}
#endif  // USE_CREATION_API == TRUE


// return the number of resources of a particular type.
int32_t XCountResourcesOfType(XResourceType resourceType)
{
    int32_t    err;
    XFILE      front;

    err = -1;

#if X_PLATFORM == X_MACINTOSH_9
    if (PV_IsAnyOpenResourceFiles() == FALSE)
    {
        return Count1Resources((ResType)resourceType);
    }
#endif
    front = PV_GetFrontOpenResourceFile();
    if (front)
    {   // count from the most recent open file
        err = XCountFileResourcesOfType(front, resourceType);
    }
    return err;
}


// bvk - NEW for Beatnik Windows
//  For a given resource type, counts the number of resources of a type.
//  Does not check for duplicate IDs.
int32_t XCountFileResourcesOfType(XFILE fileRef, XResourceType theType)
{
    int32_t                resCount;
    XResourceType       resourceType;
    XFILENAME           *pReference;
    int32_t                err;
    XFILERESOURCEMAP    map;
    int32_t                data, next;
    int32_t                count, total;
    XFILERESOURCECACHE  *pCache;
    XFILE_CACHED_ITEM   *pCacheItem;

    err = 0;
    resCount = 0;
    resourceType = 0;
    pReference = fileRef;
    if (PV_IsAnyOpenResourceFiles())
    {
        if (PV_XFileValid(fileRef))
        {
            if (pReference->pCache)
            {
                pCache = pReference->pCache;
                
                for (count = 0; count < pCache->totalResources; count++)
                {
                    pCacheItem = &pCache->cached[count];

                    if (pCacheItem->resourceType == theType)
                    {
                        resCount++;
                    }
                }
            }
            else
            {
                XFileSetPosition(fileRef, 0L);      // at start
                if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
                {
                    if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
                    {
                        next = sizeof(XFILERESOURCEMAP);
                        total = XGetLong(&map.totalResources);
                        for (count = 0; (count < total) && (err == 0); count++)
                        {
                            err = XFileSetPosition(fileRef, next);      // at start
                            if (err == 0)
                            {
                                err = XFileRead(fileRef, &next, (int32_t)sizeof(int32_t));        // get next pointer
                                next = XGetLong(&next);
                                if (next != -1L)
                                {   
                                    err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get type
                                    resourceType = (XResourceType)XGetLong(&data);
                                    if (resourceType == theType )
                                    {
                                        resCount++;
                                    }
                                }
                                else
                                {
                                    err = -4;
                                    debug_message("Next offset is bad\n");
                                    break;
                                }
                            }
                            else
                            {
                                err = -3;
                                debug_message("Can't set next position\n");
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    if (theType == ID_INST || theType == ID_ALIAS)
    {
        int32_t packedCount;
        int32_t i;
        int32_t extra;

        packedCount = (theType == ID_INST) ? PV_CountInstsInZmfBlock(fileRef)
                                           : PV_CountAliasesInZmfBlock(fileRef);
        if (resCount == 0)
        {
            return packedCount;
        }
        /* Union: flat + ZINS entries not overridden by a flat id. */
        extra = 0;
        for (i = 0; i < packedCount; ++i)
        {
            XLongResourceID id = 0;
            int32_t sz = 0;
            XPTR data = (theType == ID_INST)
                            ? PV_GetIndexedInstFromZmfBlock(fileRef, i, &id, NULL, &sz)
                            : PV_GetIndexedAliasFromZmfInstBlock(fileRef, i, &id, NULL, &sz);
            if (!data)
            {
                continue;
            }
            XDisposePtr(data);
            if (!PV_CacheHasResourceTypeID(fileRef, theType, id))
            {
                extra++;
            }
        }
        return resCount + extra;
    }
    if (resCount == 0 && theType == ID_SONG)
    {
        resCount = PV_CountSongsInZmfBlock(fileRef);
    }
    if (resCount == 0 && (theType == ID_BANK || theType == ID_MIDI || theType == ID_MIDI_OLD))
    {
        resCount = PV_CountResourcesInZmfBankBlockByType(fileRef, theType);
    }
    return resCount;
}


// search through open resource files until the resourceType is found based upon the resource index
// pResourceName is a pascal string
XPTR XGetIndexedResource(XResourceType resourceType, XLongResourceID *pReturnedID, int32_t resourceIndex, 
                                void *pResourceName, int32_t *pReturnedResourceSize)
{
    int32_t    count;
    int16_t    openCount;
    XFILE      openFiles[MAX_OPEN_XFILES];
    XPTR    pData;

    pData = NULL;
    openCount = PV_SnapshotOpenResourceFiles(openFiles, MAX_OPEN_XFILES);
    for (count = 0; count < openCount; count++)
    {
        pData = XGetIndexedFileResource(openFiles[count], resourceType, 
                                pReturnedID, resourceIndex, pResourceName, pReturnedResourceSize);
        if (pData)
        {
            break;
        }
    }
#if X_PLATFORM == X_MACINTOSH_9
    if ((pData == NULL) && (PV_IsAnyOpenResourceFiles() == FALSE))
    {
        // use native resource manager
        ResType     type;
        Handle      data;
        int16_t       shortID;
        char        pPName[256];
        int32_t        total;

        data = Get1IndResource(resourceType, resourceIndex + 1);
        if (data)
        {
            total = GetHandleSize(data);
            if (pReturnedResourceSize)
            {
                *pReturnedResourceSize = total;
            }
            pData = XNewPtr(total);
            if (pData)
            {
                HLock(data);
                XBlockMove(*data, pData, total);
                HUnlock(data);

                GetResInfo(data, &shortID, &type, (unsigned char *)pPName);
                if (pReturnedID)
                {
                    *pReturnedID = shortID;
                }
                if (pResourceName)
                {
                    XBlockMove(pPName, pResourceName, pPName[0]+1);
                }
                ReleaseResource(data);
            }
        }
    }
#endif
    return pData;
}

// get unique ID from most recent open resource file
int32_t XGetUniqueResourceID(XResourceType resourceType, XLongResourceID *pReturnedID)
{
    int32_t    err;
    XFILE      front;

    err = -1;
#if X_PLATFORM == X_MACINTOSH_9
    if (PV_IsAnyOpenResourceFiles() == FALSE)
    {
        *pReturnedID = UniqueID(resourceType);
        return 0;
    }
#endif
    front = PV_GetFrontOpenResourceFile();
    if (front)
    {   // pull from the most recent open file
        err = XGetUniqueFileResourceID(front, resourceType, pReturnedID);
    }
    return err;
}

// Given a resource file, and a type, scan through the file and return a unique and unused XLongResourceID. Will
// return 0 if ok, -1 if failure
int32_t XGetUniqueFileResourceID(XFILE fileRef, XResourceType resourceType, XLongResourceID *pReturnedID)
{
    XFILENAME           *pReference;
    int32_t                err;
    XFILERESOURCECACHE  *pCache;
    int32_t                count, total, idCount, next, data;
    XLongResourceID     *pIDs;
    XFILERESOURCEMAP    map;

    err = -1;
    pReference = fileRef;
    if (PV_XFileValid(fileRef) && pReturnedID)
    {
        // theory is we are going to collect all the IDs from either the cache or the file, then walk
        // through them and figure out the best new ID
        *pReturnedID = 0;
        idCount = 0;
        pIDs = NULL;

        // scan from cache and collect them pesky IDs
        pCache = pReference->pCache;
        if (pCache)
        {
            total = pCache->totalResources;
            pIDs = (XLongResourceID *)XNewPtr(sizeof(XLongResourceID) * total);
            if (pIDs)
            {
                err = 0;
                for (count = 0; count < total; count++)
                {
                    // same type
                    if (pCache->cached[count].resourceType == resourceType)
                    {
                        pIDs[idCount] = pCache->cached[count].resourceID;
                        idCount++;
                    }
                }
            }
        }
        else
        {
            err = XFileSetPosition(fileRef, 0L);        // at start
            if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
            {
                if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
                {
                    next = sizeof(XFILERESOURCEMAP);
                    total = XGetLong(&map.totalResources);
                    pIDs = (XLongResourceID *)XNewPtr(sizeof(XLongResourceID) * total);
                    if (pIDs)
                    {
                        for (count = 0; (count < total) && (err == 0); count++)
                        {
                            err = XFileSetPosition(fileRef, next);      // at start
                            if (err == 0)
                            {
                                err = XFileRead(fileRef, &next, (int32_t)sizeof(int32_t));        // get next pointer
                                next = XGetLong(&next);
                                if (next != -1L)
                                {   
                                    err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get type
                                    if ((XResourceType)XGetLong(&data) == resourceType)
                                    {
                                        err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get ID
                                        pIDs[idCount] = (XLongResourceID)XGetLong(&data);
                                        idCount++;
                                    }
                                }
                                else
                                {
                                    err = -4;
                                    debug_message("Next offset is bad\n");
                                    break;
                                }
                            }
                            else
                            {
                                err = -3;
                                debug_message("Can't set next position\n");
                                break;
                            }
                        }
                    }
                }
                else
                {
                    err = -2;
                }
            }
            else
            {
                err = -1;
            }
        }
        // ok, we've now got a list of ID's. Pick a random number and search for a match, if there's
        // a match, try again.
        if (pIDs && (err == 0))
        {
            bool               good, failureCount;
            XLongResourceID     newID;

            good = FALSE;
            failureCount = 0;
            while (good == FALSE)
            {
                newID = (XLongResourceID)XRandom(); // we only pick numbers within 0-32767. This is on purpose.
                                                    // numbers outside of this range are reserved
                good = TRUE;
                for (count = 0; count < idCount; count++)
                {
                    if (pIDs[count] == newID)
                    {
                        good = FALSE;
                        break;
                    }
                }
                failureCount++;
                if (good == FALSE)
                {
                    if (failureCount > idCount) // we've looked through all possible IDs, time to bail
                    {
                        err = -1;
                        good = TRUE;
                    }
                }
                else
                {
                    *pReturnedID = newID;
                }
            }
        }
        else
        {
            err = -1;   // I guess not
        }
        XDisposePtr(pIDs);
    }
#if X_PLATFORM == X_MACINTOSH_9
    else
    {
        int16_t   currentResourceFile;

        currentResourceFile = CurResFile();
        UseResFile((int16_t)fileRef);
        *pReturnedID = UniqueID(resourceType);
        UseResFile(currentResourceFile);
        err = 0;
    }
#endif
    return err;
}

int32_t XMakeUniqueResourceID(XResourceType resourceType,
                                XLongResourceID* id)
{
    if (XExistsResource(resourceType, *id))
    {
    XLongResourceID     newId;
    
        if (XGetUniqueResourceID(resourceType, &newId) == 0)
        {
            *id = newId;
            return 1;
        }
        return -1;
    }
    return 0;
}
int32_t XMakeUniqueFileResourceID(XFILE fileRef, XResourceType resourceType,
                                XLongResourceID* id)
{
    if (XExistsResource(resourceType, *id))
    {
    XLongResourceID     newId;
    
        if (XGetUniqueFileResourceID(fileRef, resourceType, &newId) == 0)
        {
            *id = newId;
            return 1;
        }
        return -1;
    }
    return 0;
}

// Add a resource to the most recently open resource file.
//      resourceType is a type
//      resourceID is an ID
//      pResourceName is a pascal string
//      pData is the data block to add
//      length is the length of the data block
int32_t    XAddResource(XResourceType resourceType, XLongResourceID resourceID, void *pResourceName, void *pData, int32_t length)
{
    int32_t    err;

    err = -1;
#if X_PLATFORM == X_MACINTOSH_9
    if (PV_IsAnyOpenResourceFiles() == FALSE)
    {
        Handle  dataBlock;
        
        dataBlock = NewHandle(length);
        if (dataBlock)
        {
            HLock(dataBlock);
            BlockMove(pData, *dataBlock, length);
            HUnlock(dataBlock);
            AddResource(dataBlock, (ResType)resourceType, resourceID, (unsigned char *)pResourceName);
        }
        return (ResError() == noErr) ? 0 : -1;
    }
#endif
    {
        XFILE front = PV_GetFrontOpenResourceFile();
        if (front)
        {   // add to the most recent open file
            err = XAddFileResource(front, resourceType, resourceID, pResourceName, pData, length);
        }
    }
    return err;
}


// Delete a resource from the most recently open resource file.
//      resourceType is a type
//      resourceID is an ID
//      collectTrash if TRUE will force an update, otherwise it will happen when the file is closed
bool XDeleteResource(XResourceType resourceType, XLongResourceID resourceID, bool collectTrash)
{
#if X_PLATFORM == X_MACINTOSH_9
    if (PV_IsAnyOpenResourceFiles() == FALSE)
    {
        Handle  dataBlock;
        
        dataBlock = Get1Resource(resourceType, resourceID);
        if (dataBlock)
        {
            RemoveResource(dataBlock);
            if (collectTrash)
            {
                UpdateResFile(CurResFile());
            }
        }
        return (ResError() == noErr) ? TRUE : FALSE;
    }
#endif
    {
        XFILE front = PV_GetFrontOpenResourceFile();
        if (front)
        {   // delete from the most recent open file
            return XDeleteFileResource(front, resourceType, resourceID, collectTrash);
        }
    }
    return FALSE;
}


// Add a resource to a particular file
//      fileRef is the open file
//      resourceType is a type
//      resourceID is an ID
//      pResourceName is a pascal string
//      pData is the data block to add
//      length is the length of the data block
int32_t XAddFileResource(XFILE fileRef, XResourceType resourceType,
                        XLongResourceID resourceID, void const* pResourceName,
                        void *pData, int32_t length)
{
    XFILENAME           *pReference;
    int32_t                err;
    XFILERESOURCEMAP    map;
    int32_t                data;
    char                fakeName[2];
    int32_t                next, nextsave;
    XFILE_CACHED_ITEM   cacheItem;

#if X_PLATFORM == X_MACINTOSH_9
    if (PV_IsAnyOpenResourceFiles() == FALSE)
    {
        // use native resource manager
        Handle  theData;

        theData = NewHandle(length);
        if (theData)
        {
            HLock(theData);
            XBlockMove(pData, *theData, length);
            HUnlock(theData);
            AddResource(theData, resourceType, (int16_t)resourceID, (unsigned char *)pResourceName);
        }
        return 0;
    }
#endif
    err = -1;
    pReference = fileRef;
    if (PV_XFileValid(fileRef))
    {
        // the cache will updated, and the file based cache will be deleted
        if (pData && length)
        {
            // if we have a cache resource, delete it
            XDeleteFileResource(fileRef, XFILECACHE_ID, 0, FALSE);

            XFileSetPosition(fileRef, 0L);      // at start
            if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
            {
                if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
                {
                    XFileSetPosition(fileRef, XFileGetLength(fileRef));                     // at end
    
                    nextsave = XFileGetPosition(fileRef);   // rewrite this later
                    next = -1;                              // store all Fs for now
                    err = XFileWrite(fileRef, &next, (int32_t)sizeof(int32_t));

                    XPutLong(&data, (uint32_t)resourceType);
                    err = XFileWrite(fileRef, &data, (int32_t)sizeof(int32_t));                       // put type
                    cacheItem.resourceType = (uint32_t)resourceType;
    
                    if (err == 0)
                    {
                        XPutLong(&data, (uint32_t)resourceID);
                        err = XFileWrite(fileRef, &data, (int32_t)sizeof(int32_t));                   // put ID
                        cacheItem.resourceID = (uint32_t)resourceID;

                        if (err == 0)
                        {
                            data = XFileGetPosition(fileRef);   // get name pos
                            cacheItem.fileOffsetName = data;

                            if (pResourceName)
                            {
                                err = XFileWrite(fileRef, pResourceName, (int32_t)(((char *)pResourceName)[0])+1L);        // put name
                            }
                            else
                            {
                                fakeName[0] = 0;
                                err = XFileWrite(fileRef, fakeName, 1L);        // put name
                            }
                            
                            if (err == 0)
                            {
                                XPutLong(&data, length);
                                err = XFileWrite(fileRef, &data, (int32_t)sizeof(int32_t));               // put length
                                cacheItem.resourceLength = length;
        
                                if (err == 0)
                                {
                                    data = XFileGetPosition(fileRef);   // get data pos
                                    cacheItem.fileOffsetData = data;
                                    err = XFileWrite(fileRef, pData, length);                   // put data block
        
                                    if (err == 0)
                                    {
                                        next = XFileGetPosition(fileRef);   // get pos of next resource

                                        XFileSetPosition(fileRef, 0L);      // back to start
                                        data = XGetLong(&map.totalResources) + 1;
                                        XPutLong(&map.totalResources, data);
                                        err = XFileWrite(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP));

                                        if (err == 0)
                                        {
                                            // write real offset-of-next-resource value
                                            XFileSetPosition(fileRef, nextsave);
                                            XPutLong(&data, next);
                                            err = XFileWrite(fileRef, &data, (int32_t)sizeof(int32_t));

                                            if (err == 0)
                                            {
                                                // Now we add this to the native RAM cache
                                                if (pReference->pCache)
                                                {
#if USE_CREATION_API == TRUE
                                                    if (PV_AddToAccessCache(fileRef, &cacheItem ) == FALSE)
                                                    {
                                                        /* Grow failed; rebuild so RAM cache matches the map. */
                                                        (void)XCreateAccessCache(fileRef);
                                                    }
#endif
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return err;
}



// get a resource type from the name. cName is a C string
XPTR XGetNamedResource(XResourceType resourceType, void *cName, int32_t *pReturnedResourceSize)
{
    XPTR                pData;
    XFILE_CACHED_ITEM   *pCacheItem;
    char                pResourceName[256];
    int32_t                count, total;
    int16_t           fileCount;
    int32_t                err;
    XFILE               fileRef;
    XFILERESOURCEMAP    map;
    int32_t                next, data;
    XLongResourceID     resourceID;

    pData = NULL;
    if (pReturnedResourceSize)
    {
        *pReturnedResourceSize = 0;
    }
#if X_PLATFORM == X_MACINTOSH_9
    {
        Handle  theData;
        int32_t    size;
        XFILE      openFiles[MAX_OPEN_XFILES];
        int16_t    openCount;

        // first look inside any open resource files
        openCount = PV_SnapshotOpenResourceFiles(openFiles, MAX_OPEN_XFILES);
        for (count = 0; count < openCount; count++)
        {
            pCacheItem = PV_XGetNamedCacheEntry(openFiles[count], resourceType, cName);
            if (pCacheItem)
            {
                pData = XGetFileResource(openFiles[count], pCacheItem->resourceType, 
                                                                pCacheItem->resourceID, 
                                                                pResourceName, pReturnedResourceSize);
                // we found our resource
                break;
            }
        }
        if (pData == NULL)
//      if ((pData == NULL) && (PV_IsAnyOpenResourceFiles() == FALSE))
        {
            // use native resource manager
            XStrCpy(pResourceName, (char *)cName);
            XCtoPstr(pResourceName);
            theData = Get1NamedResource(resourceType, (unsigned char *)pResourceName);
            if (theData)
            {
                size = GetHandleSize(theData);
                pData = XNewPtr(size);
                if (pData)
                {
                    HLock(theData);
                    XBlockMove(*theData, pData, size);
                    HUnlock(theData);
                    ReleaseResource(theData);
                    if (pReturnedResourceSize)
                    {
                        *pReturnedResourceSize = size;
                    }
                }
            }
        }
    }
#endif
    if (pData == NULL)
    {
        XFILE      openFiles[MAX_OPEN_XFILES];
        int16_t    openCount;

        openCount = PV_SnapshotOpenResourceFiles(openFiles, MAX_OPEN_XFILES);
        for (fileCount = 0; fileCount < openCount; fileCount++)
        {
            pCacheItem = PV_XGetNamedCacheEntry(openFiles[fileCount], resourceType, cName);
            if (pCacheItem)
            {
                pData = XGetFileResource(openFiles[fileCount], pCacheItem->resourceType, 
                                                                pCacheItem->resourceID, 
                                                                pResourceName, pReturnedResourceSize);
                // we found our resource
                break;
            }
            else
            {
                err = 0;
                // search through without cache.
                fileRef = openFiles[fileCount];
                XFileSetPosition(fileRef, 0L);      // at start
                if (XFileRead(fileRef, &map, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
                {
                    if (XFILERESOURCE_ID_IS_VALID(XGetLong(&map.mapID)))
                    {
                        next = sizeof(XFILERESOURCEMAP);
                        total = XGetLong(&map.totalResources);
                        for (count = 0; (count < total) && (err == 0); count++)
                        {
                            err = XFileSetPosition(fileRef, next);      // at start
                            if (err == 0)
                            {
                                err = XFileRead(fileRef, &next, (int32_t)sizeof(int32_t));        // get next pointer
                                next = XGetLong(&next);
                                if (next != -1L)
                                {   
                                    err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get type
                                    if ((XResourceType)XGetLong(&data) == resourceType)
                                    {
                                        err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get ID
                                        resourceID = (XLongResourceID)XGetLong(&data);
                                        err = XFileRead(fileRef, &pResourceName[0], 1L);        // get name
                                        if (pResourceName[0])
                                        {
                                            err = XFileRead(fileRef, &pResourceName[1], (int32_t)pResourceName[0]);
                                            XPtoCstr(pResourceName);
                                            if (XStrCmp(pResourceName, (char *)cName) == 0)
                                            {   // match?
                                                return XGetFileResource(fileRef, 
                                                                resourceType, resourceID, 
                                                                pResourceName, pReturnedResourceSize);
                                            }
                                            err = XFileRead(fileRef, &data, (int32_t)sizeof(int32_t));        // get length
                                            data = XGetLong(&data);     // get resource size
                                            if (XFileSetPositionRelative(fileRef, data))
                                            {
                                                err = -2;
                                            }
                                        }
                                    }
                                }
                            }
                            else
                            {
                                err = -4;
                                debug_message("Next offset is bad\n");
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    // (Removed invalid code referencing undefined pPName.)
    return pData;
}

bool XExistsResource(XResourceType resourceType, XLongResourceID resourceID)
{
    char        name[256];

    return XGetResourceName(resourceType, resourceID, name);
}
bool XExistsFileResource(XFILE fileRef,
                            XResourceType resourceType, XLongResourceID resourceID)
{
    char        name[256];
    int32_t     size;
    XPTR        data;

    name[0] = 0;
    /* Flat cache/scan hit — including empty Pascal names. Classic bank emid
     * entries are often unnamed; XGetFileResourceName treats "" as missing so
     * it can fall through to ZSHD/ZINS name tables, which wrongly made Exists
     * fail for those resources (nbeditor groovoid strip skipped all of them). */
    if (XGetResourceNameOnly(fileRef, resourceType, resourceID, name) != NULL)
    {
        return TRUE;
    }
    /* Named packed ZREZ logical resources (ZINS/ZSNG/ZBNK/ZSHD). */
    if (XGetFileResourceName(fileRef, resourceType, resourceID, name))
    {
        return TRUE;
    }
    /* Unnamed packed resources: probe load. */
    size = 0;
    data = XGetFileResource(fileRef, resourceType, resourceID, NULL, &size);
    if (data)
    {
        XDisposePtr(data);
        return TRUE;
    }
    return FALSE;
}

// Get just the resource name from resourceType and resourceID.
// The cName is a 'C' string which is returned
bool XGetResourceName(XResourceType resourceType, XLongResourceID resourceID,
                        char *cName)
{
    int         count;
    XFILE       openFiles[MAX_OPEN_XFILES];
    int16_t     openCount;

    if (cName)
    {
        cName[0] = 0;
        openCount = PV_SnapshotOpenResourceFiles(openFiles, MAX_OPEN_XFILES);
#if X_PLATFORM == X_MACINTOSH_9
        // first look in any open resource files
        for (count = 0; count < openCount; count++)
        {
            if (XGetFileResourceName(openFiles[count],
                                        resourceType, resourceID, cName))
            {
                // we found data
                break;
            }
        }
        if (cName[0] == 0)
        {   // use native resource manager
            int16_t       theID;
            uint32_t   theType;
            Handle          theData;

            SetResLoad(FALSE);
            theData = Get1Resource(resourceType, (int16_t)resourceID);
            SetResLoad(TRUE);
            if (theData)
            {
                GetResInfo(theData, &theID, &theType, (unsigned char *)cName);
                XPtoCstr(cName);
                DetachResource(theData);
                DisposeHandle(theData);
                return TRUE;
            }
        }
        else
        {
            return TRUE;
        }
#else
        for (count = 0; count < openCount; count++)
        {
            if (XGetFileResourceName(openFiles[count],
                                        resourceType, resourceID, cName))
            {
                return TRUE;
            }
        }
#endif
    }
    return FALSE;
}
bool XGetFileResourceName(XFILE fileRef, XResourceType resourceType,
                            XLongResourceID resourceID, char *cName)
{
    char            pascalName[256];
    XPTR            pData;
    int32_t         size;
    if (cName)
    {
        cName[0] = 0;
    }
    if (XGetResourceNameOnly(fileRef, resourceType, resourceID, cName))
    {
        /* Empty Pascal name still "succeeds" from the cache — treat as missing
         * so ZSHD / ZINS / ZBNK name tables can supply the real name. */
        if (cName && cName[0] != 0)
        {
            XPtoCstr(cName);
            return TRUE;
        }
        if (cName)
        {
            cName[0] = 0;
        }
    }

    /*
     * ZREZ packs logical INST/SONG/Midi/BANK into ZINS/ZSNG/ZBNK, and SND
     * headers/names into ZSHD. The flat resource cache only lists packed
     * containers or payload-refs, so name lookups must fall through.
     */
    pascalName[0] = 0;
    pData = NULL;
    size = 0;
    if (resourceType == ID_INST)
    {
        pData = PV_GetInstFromZmfBlockByID(fileRef, resourceID, pascalName, &size);
    }
    else if (resourceType == ID_ALIAS)
    {
        pData = PV_GetAliasFromZmfInstBlockByID(fileRef, resourceID, pascalName, &size);
    }
    else if (resourceType == ID_SONG)
    {
        pData = PV_GetSongFromZmfBlockByID(fileRef, resourceID, pascalName, &size);
    }
    else if (resourceType == ID_BANK || resourceType == ID_MIDI || resourceType == ID_MIDI_OLD)
    {
        pData = PV_GetResourceFromZmfBankBlockByTypeAndID(fileRef,
                                                          resourceType,
                                                          resourceID,
                                                          pascalName,
                                                          &size);
    }
    else if (resourceType == ID_SND || resourceType == ID_CSND || resourceType == ID_ESND)
    {
        pData = PV_GetSndHeaderFromZmfBlockByTypeAndID(fileRef,
                                                       resourceType,
                                                       resourceID,
                                                       pascalName,
                                                       &size);
    }
    if (pData)
    {
        XDisposePtr(pData);
        if (cName && pascalName[0] != 0)
        {
            XBlockMove(pascalName, cName, (int32_t)((unsigned char)pascalName[0]) + 1);
            XPtoCstr(cName);
            return TRUE;
        }
    }
    return FALSE;
}

// Get a resource and detach it from the resource manager. Which means that you'll need to call XDisposePtr
// to free the memory
XPTR XGetAndDetachResource(XResourceType resourceType, XLongResourceID resourceID, int32_t *pReturnedResourceSize)
{
#if X_PLATFORM == X_MACINTOSH_9
    Handle      theData;
    XPTR        pData;
    XPTR        pNewData;
    int32_t        size;
    char        szPName[256];
    int16_t   count;
    int16_t   openCount;
    XFILE       openFiles[MAX_OPEN_XFILES];
    XFILE       fileRef;
    XFILENAME   *pReference;

    pData = NULL;
    if (pReturnedResourceSize)
    {
        *pReturnedResourceSize = 0;
    }
    // first look in any open resource files
    openCount = PV_SnapshotOpenResourceFiles(openFiles, MAX_OPEN_XFILES);
    for (count = 0; count < openCount; count++)
    {
        pData = XGetFileResource(openFiles[count], resourceType, resourceID, szPName, &size);
        if (pData)
        {
            fileRef = openFiles[count];
            pReference = fileRef;
            if (pReference->pResourceData && (pReference->allowMemCopy) )
            {
                //In the case of a memory file, we have to create a new block to return.
                pNewData = XNewPtr(size);
                if (pNewData)
                {
                    XBlockMove(pData, pNewData, size);
                    pData = pNewData;
                }
                else
                {
                    pData = NULL;   //not detaching is NO SUCCESS!
                }
            }
            if (pReturnedResourceSize)
            {
                *pReturnedResourceSize = size;
            }
            // we found data
            break;
        }
    }
    if (pData == NULL)
//  if ((pData == NULL) && (PV_IsAnyOpenResourceFiles() == FALSE))
    {   // use native resource manager
        theData = Get1Resource(resourceType, (int16_t)resourceID);
        if (theData)
        {
            size = GetHandleSize(theData);
            pData = XNewPtr(size);
            if (pData)
            {
                HLock(theData);
                XBlockMove(*theData, pData, size);
                HUnlock(theData);
                ReleaseResource(theData);
                if (pReturnedResourceSize)
                {
                    *pReturnedResourceSize = size;
                }
            }
        }
    }
    return pData;
#else   // X_PLATFORM == X_MACINTOSH_9
    char        szPName[256];
    int32_t        lSize = 0;
    XPTR        pData = NULL;
    XPTR        pNewData;
    int16_t   count;
    int16_t   openCount;
    XFILE       openFiles[MAX_OPEN_XFILES];
    XFILE       fileRef;
    XFILENAME   *pReference;

    openCount = PV_SnapshotOpenResourceFiles(openFiles, MAX_OPEN_XFILES);
    for (count = 0; count < openCount; count++)
    {
        pData = XGetFileResource(openFiles[count], resourceType, resourceID, szPName, &lSize);

        if (pData)
        {
            fileRef = openFiles[count];
            pReference = fileRef;
            if (pReference->pResourceData && (pReference->allowMemCopy) )
            {
                //In the case of a memory file, we have to create a new block to return.
                pNewData = XNewPtr( lSize );
                if (pNewData)
                {
                    XBlockMove(pData, pNewData, lSize );
                    pData = pNewData;
                }
                else
                {
                    pData = NULL;   //not detaching is NO SUCCESS!
                }
            }

            if (pReturnedResourceSize)
            {
                *pReturnedResourceSize = lSize;
            }
            // we found data
            break;
        }
    }
    return pData;
#endif  //  X_PLATFORM == X_MACINTOSH_9
}

// get current most recently opened resource file, or NULL if nothing is open
XFILE XFileGetCurrentResourceFile(void)
{
    return PV_GetFrontOpenResourceFile();
}

int16_t XFileGetOpenResourceFiles(XFILE *outFiles, int16_t maxFiles)
{
    return PV_SnapshotOpenResourceFiles(outFiles, maxFiles);
}

// make sure this resource file is first in the scan list
void XFileUseThisResourceFile(XFILE fileRef)
{
    int16_t   fileCount;
    XFILE       currentFirst;

    if (PV_XFileValid(fileRef))
    {
        PV_LockOpenResourceFiles();
        fileCount = PV_FindResourceFileReferenceIndex(fileRef);
        if (fileCount != -1)
        {
            currentFirst = g_openResourceFiles[0];
            g_openResourceFiles[0] = fileRef;
            g_openResourceFiles[fileCount] = currentFirst;
        }
        PV_UnlockOpenResourceFiles();
    }
}

void XFileSetForceZsbFeatures(XFILE fileRef, bool enable)
{
    if (PV_XFileValid(fileRef))
    {
        ((XFILENAME *)fileRef)->forceZsbFeatures = enable ? TRUE : FALSE;
    }
}

bool XFileGetForceZsbFeatures(XFILE fileRef)
{
    if (!PV_XFileValid(fileRef))
    {
        return FALSE;
    }
    return ((XFILENAME *)fileRef)->forceZsbFeatures ? TRUE : FALSE;
}


// Decompress a 'csnd' format sound.
// First byte is a type.
// Next three bytes are a length.
// Type 0 is Delta LZSS compression
// Type 0x80+ is Delta LZMA compression (ZMF containers)
#if USE_LZMA_COMPRESSION == TRUE
static bool PV_IsLZMACompressionType(XCOMPRESSION_TYPE t)
{
    return (t == X_LZMA_RAW ||
            (t >= X_LZMA_MONO_8 && t <= X_LZMA_STEREO_16)) ? TRUE : FALSE;
}
#endif

void * XDecompressPtr(void* pData, uint32_t dataSize, bool ignoreType)
{
    uint32_t       theTotalSize;
    XCOMPRESSION_TYPE   theType;
    XPTR                theNewData;

    theNewData = NULL;
    if (pData && dataSize)
    {
        theTotalSize = XGetLong(pData);
        if (ignoreType)
        {
#if USE_LZMA_COMPRESSION == TRUE
            /* When ignoring the delta sub-type (MIDI data), we still need
             * to detect whether the data was compressed with LZMA vs LZSS. */
            XCOMPRESSION_TYPE rawType = (XCOMPRESSION_TYPE)((uint8_t)(theTotalSize >> 24L));
            theType = PV_IsLZMACompressionType(rawType) ? X_LZMA_RAW : X_RAW;
#else
            theType = X_RAW;
#endif
        }
        else
        {
            theType = (XCOMPRESSION_TYPE)(theTotalSize >> 24L);
        }
        theTotalSize &= 0x00FFFFFFL;
        theNewData = XNewPtr(theTotalSize);
        if (theNewData)
        {
            switch (theType)
            {
                case X_RAW:
                    LZSSUncompress((unsigned char*)pData + sizeof(int32_t),
                                    dataSize - sizeof(int32_t), 
                                    (unsigned char*)theNewData, 
                                    theTotalSize);
                    break;
                case X_MONO_8:
                    LZSSUncompressDeltaMono8((unsigned char*)pData + sizeof(int32_t),
                                                dataSize - sizeof(int32_t), 
                                                (unsigned char*)theNewData, 
                                                theTotalSize);
                    break;
                case X_STEREO_8:
                    LZSSUncompressDeltaStereo8((unsigned char*)pData + sizeof(int32_t),
                                                dataSize - sizeof(int32_t), 
                                                (unsigned char*)theNewData, 
                                                theTotalSize);
                    break;
                case X_MONO_16:
                    LZSSUncompressDeltaMono16((unsigned char*)pData + sizeof(int32_t),
                                                dataSize - sizeof(int32_t), 
                                                (int16_t*)theNewData, 
                                                theTotalSize);
                    break;
                case X_STEREO_16:
                    LZSSUncompressDeltaStereo16((unsigned char*)pData + sizeof(int32_t),
                                                dataSize - sizeof(int32_t), 
                                                (int16_t*)theNewData, 
                                                theTotalSize);
                    break;
#if USE_LZMA_COMPRESSION == TRUE
                case X_LZMA_RAW:
                    LZMAUncompress((unsigned char*)pData + sizeof(int32_t),
                                    dataSize - sizeof(int32_t),
                                    (unsigned char*)theNewData,
                                    theTotalSize);
                    break;
                case X_LZMA_MONO_8:
                    LZMAUncompressDeltaMono8((unsigned char*)pData + sizeof(int32_t),
                                                dataSize - sizeof(int32_t),
                                                (unsigned char*)theNewData,
                                                theTotalSize);
                    break;
                case X_LZMA_STEREO_8:
                    LZMAUncompressDeltaStereo8((unsigned char*)pData + sizeof(int32_t),
                                                dataSize - sizeof(int32_t),
                                                (unsigned char*)theNewData,
                                                theTotalSize);
                    break;
                case X_LZMA_MONO_16:
                    LZMAUncompressDeltaMono16((unsigned char*)pData + sizeof(int32_t),
                                                dataSize - sizeof(int32_t),
                                                (int16_t*)theNewData,
                                                theTotalSize);
                    break;
                case X_LZMA_STEREO_16:
                    LZMAUncompressDeltaStereo16((unsigned char*)pData + sizeof(int32_t),
                                                dataSize - sizeof(int32_t),
                                                (int16_t*)theNewData,
                                                theTotalSize);
                    break;
#endif
                default:
                    XDisposePtr(theNewData);
                    theNewData = NULL;
                    break;
            }
        }
    }
    return theNewData;
}

#if USE_CREATION_API == TRUE

// Given a block of data and a size, this will compress it into a newly-allocated.
// block of data.  The original pointer is not deallocated.  The new pointer must
// be deallocated when no longer needed.  The first byte of the compressed data
// is the XCOMPRESSION_TYPE used and the following 3 bytes are the length of the 
// uncompressed data.  (Original data cannot be larger than than 16 MB.)
// A pointer to the compressed block is stored at compressedDataTarget.
// The length of the compressed data is returned if compression succeeds
//  If -1 is returned, the compression failed
//  If 0 is returned, compression was aborted by proc.
int32_t XCompressPtr(XPTR* compressedDataTarget,
                    XPTR pData, uint32_t dataSize,
                    XCOMPRESSION_TYPE type,
                    XCompressStatusProc proc, void* procData)
{
XPTR            compressedData;
int32_t            compressedSize = 0;
unsigned char           *realData;
uint32_t        allocSize;

    if (!compressedDataTarget)
    {
        return -1;  // bad parameters
    }
    *compressedDataTarget = NULL;
    if (!pData || (dataSize == 0))
    {
        return -1;  // bad parameters
    }
    if (dataSize > 0x00FFFFFF)
    {
        return -1;  // too big
    }

#if USE_LZMA_COMPRESSION == TRUE
    if (PV_IsLZMACompressionType(type))
    {
        allocSize = LZMACompressBound(dataSize);
    }
    else
#endif
    {
        allocSize = dataSize;
    }

    compressedData = XNewPtr(allocSize);
    if (!compressedData)
    {
        return -1;  // out of memory
    }

    switch (type)
    {
    case X_RAW:
        compressedSize = LZSSCompress((unsigned char*)pData, dataSize,
                                        (unsigned char*)compressedData,
                                        proc, procData);
        break;
    case X_MONO_8:
        compressedSize = LZSSCompressDeltaMono8((unsigned char*)pData, dataSize,
                                                (unsigned char*)compressedData,
                                                proc, procData);
        break;
    case X_STEREO_8:
        compressedSize = LZSSCompressDeltaStereo8((unsigned char*)pData, dataSize,
                                                    (unsigned char*)compressedData,
                                                    proc, procData);
        break;
    case X_MONO_16:
        compressedSize = LZSSCompressDeltaMono16((int16_t*)pData, dataSize,
                                                    (unsigned char*)compressedData,
                                                    proc, procData);
        break;
    case X_STEREO_16:
        compressedSize = LZSSCompressDeltaStereo16((int16_t*)pData, dataSize,
                                                    (unsigned char*)compressedData,
                                                    proc, procData);
        break;
#if USE_LZMA_COMPRESSION == TRUE
    case X_LZMA_RAW:
        compressedSize = LZMACompress((unsigned char*)pData, dataSize,
                                        (unsigned char*)compressedData,
                                        proc, procData);
        break;
    case X_LZMA_MONO_8:
        compressedSize = LZMACompressDeltaMono8((unsigned char*)pData, dataSize,
                                                (unsigned char*)compressedData,
                                                proc, procData);
        break;
    case X_LZMA_STEREO_8:
        compressedSize = LZMACompressDeltaStereo8((unsigned char*)pData, dataSize,
                                                    (unsigned char*)compressedData,
                                                    proc, procData);
        break;
    case X_LZMA_MONO_16:
        compressedSize = LZMACompressDeltaMono16((int16_t*)pData, dataSize,
                                                    (unsigned char*)compressedData,
                                                    proc, procData);
        break;
    case X_LZMA_STEREO_16:
        compressedSize = LZMACompressDeltaStereo16((int16_t*)pData, dataSize,
                                                    (unsigned char*)compressedData,
                                                    proc, procData);
        break;
#endif
    }
    
    if (compressedSize > 0)
    {
        compressedSize += sizeof(int32_t);
        realData = (unsigned char*)XNewPtr(compressedSize);
        if (realData)
        {
            XPutLong(realData, dataSize);
            realData[0] = (unsigned char)type;
            XBlockMove(compressedData, realData + sizeof(int32_t),
                        compressedSize - sizeof(int32_t));
        }
        *compressedDataTarget = realData;
    }
    XDisposePtr(compressedData);

    return compressedSize;
}

#endif  // USE_CREATION_API == TRUE

XPTR XDuplicateMemory(XPTRC src, uint32_t len)
{
    XPTR dup;

    dup = NULL;
    if (src)
    {
        dup = XNewPtr((int32_t)len);
        if (dup)
        {
            XBlockMove(src, dup, (int32_t)len);
        }
    }
    return dup;
}

char * XDuplicateStr(char const* src)
{
    char *dup;

    dup = NULL;
    if (src)
    {
        dup = (char *)XNewPtr(XStrLen(src)+1);
        if (dup)
        {
            XStrCpy(dup, src);
        }
    }
    return dup;
}

// Strip characters between 0-32 in place
void XStripStr(char *pString)
{
    char *pNew;
    
    pNew = XDuplicateAndStripStr(pString);
    if (pNew)
    {
        XStrCpy(pString, pNew);
    }
    XDisposePtr(pNew);
}
    

// Duplicate and string characters between 0-32
char * XDuplicateAndStripStr(char const* src)
{
    int32_t            length;         // must be signed
    char*           cStrippedString;
    char*           cDest;
    char const*     cSource;

    cStrippedString = NULL;
    // strip out undesirable characters, give them some walking money
    length = XStrLen(src);
    if (length)
    {
        cStrippedString = (char *)XNewPtr(length+1);
        if (cStrippedString)
        {
            cDest = cStrippedString;
            cSource = src;
            while (*cSource)
            {
                // we only want to strip 0-32 nothing else
                if ((*cSource >= 32) || (*cSource < 0))
                {
                    *cDest++ = *cSource;
                }
                cSource++;
            }
            *cDest = 0;
        }
    }
    return cStrippedString;
}

// standard strcpy
// Copies 'C' string src into dest
char * XStrCpy(char *dest, char const* src)
{
    char *sav;

    sav = dest;
    if (src == NULL)
    {
        src = "";
    }
    if (dest)
    {
        while (*src)
        {
            *dest++ = *src++;
        }
        *dest = 0;
    }
    return sav;
}

bool XIsDigit(int16_t c)
{
    if (c >= '0' && c <= '9')
    {
        return TRUE;
    }   
    return FALSE;
}

int16_t XLowerCase(int16_t c)
{
    return( ( ((c >= 'A') && (c <= 'Z')) ? c | 0x20 : c) );
}

// string search.
char * XStrStr(char *source, char const* pattern)
{
    unsigned char*          s1;
    unsigned char const*    p1;
    unsigned char           firstc, c1, c2;

    if (source == NULL)
    {
        source = "";
    }
    if (pattern == NULL)
    {
        pattern = "";
    }
    s1 = (unsigned char *)source;
    p1 = (unsigned char const*)pattern;

    if (!(firstc = *p1++))
    {
        return((char *) s1);
    }
    while((c1 = *s1) != 0)
    {
        s1++;
        if (c1 == firstc)
        {
            const unsigned char * s2 = s1;
            const unsigned char * p2 = p1;
            
            while ((c1 = *s2++) == (c2 = *p2++) && c1) {};
            
            if (!c2)
            {
                return((char *) s1 - 1);
            }
        }
    }
    return(NULL);
}

// string search, but ignore case
char * XLStrStr(char *source, char const* pattern)
{
    unsigned char* s1;
    unsigned char const* p1;
    unsigned char firstc, c1, c2;

    if (source == NULL)
    {
        source = "";
    }
    if (pattern == NULL)
    {
        pattern = "";
    }
    s1 = (unsigned char*)source;
    p1 = (unsigned char const*)pattern;

    if (!(firstc = *p1++))
    {
        return((char *) s1);
    }
    while((c1 = *s1) != 0)
    {
        s1++;
        if (XLowerCase(c1) == XLowerCase(firstc))
        {
            const unsigned char * s2 = s1;
            const unsigned char * p2 = p1;
            
            while (XLowerCase(c1 = *s2++) == XLowerCase(c2 = *p2++) && XLowerCase(c1)) {};
            
            if (!c2)
            {
                return((char *) s1 - 1);
            }
        }
    }
    return(NULL);
}

char* XStrCatChar(char *dest, char c)
{
    char    cc[2];

    cc[0] = c;
    cc[1] = 0;
    return XStrCat(dest, (char const*)cc);
}

char * XStrCat(char * dest, char const* source)
{
    char const* p = source;
    char*       q = dest;
    if (dest)
    {
        if (source == NULL)
        {
            source = "";
        }
        while ((*q++) != 0) {};
        
        q--;
        
        while ((*q++ = *p++) != 0) {};
    }   
    return dest;
}

int32_t XStrLen(char const* src)
{
#if 1
    int16_t len;

    len = -1;
    if (src == NULL)
    {
        src = "";
    }
    do
    {
        len++;
    } while  (*src++);

    return len;
#else
char    const* s;

    s = src;
    if (s)
    {
        while (*s) s++;
    }
    return s - src;
#endif
}

// standard strcmp
int16_t XStrCmp(char const* s1, char const* s2)
{
    if (s1 == NULL)
    {
        s1 = "";
    }
    if (s2 == NULL)
    {
        s2 = "";
    }
    
    while (1) 
    {
        if (*s1 == *s2) 
        {
            if (*s1 == 0) 
            {
                return 0;
            } 
            else 
            {
                s1++;
                s2++;
            }
        } 
        else if (*s1 > *s2) 
        {
            return 1;
        } 
        else 
        {
            return -1;
        }
    }
}

// standard strcmp, but ignore case
int16_t XLStrCmp(char const* s1, char const* s2)
{
    if (s1 == NULL)
    {
        s1 = "";
    }
    if (s2 == NULL)
    {
        s2 = "";
    }
    
    while (1) 
    {
        if (XLowerCase(*s1) == XLowerCase(*s2)) 
        {
            if (*s1 == 0) 
            {
                return 0;
            } 
            else 
            {
                s1++;
                s2++;
            }
        } 
        else if (XLowerCase(*s1) > XLowerCase(*s2)) 
        {
            return 1;
        } 
        else 
        {
            return -1;
        }
    }
}

// Standard strncmp, but ignore case. Compares zero terminated s1 with non zero terminated s2 with s2 having
// a length of n
int16_t XLStrnCmp(char const* s1, char const* s2, int32_t n)
{
    if (s1 == NULL)
    {
        s1 = "";
    }
    if (s2 == NULL)
    {
        s2 = "";
    }

    if (n)
    {
        do 
        {
            if (XLowerCase(*s1) != XLowerCase(*s2++))
            {
                return (*(unsigned char *)s1 - *(unsigned char *)--s2);
            }
            if (*s1++ == 0)
            {
                break;
            }
        } while (--n != 0);
    }
    return 0;
}
// Standard strncmp. Compares zero terminated s1 with non zero terminated s2 with s2 having
// a length of n
int16_t XStrnCmp(char const* s1, char const* s2, register int32_t n)
{
    if (s1 == NULL)
    {
        s1 = "";
    }
    if (s2 == NULL)
    {
        s2 = "";
    }

    if (n)
    {
        do 
        {
            if (*s1 != *s2++)
            {
                return (*(unsigned char *)s1 - *(unsigned char *)--s2);
            }
            if (*s1++ == 0)
            {
                break;
            }
        } while (--n != 0);
    }
    return 0;
}

// Converts a long value to a base 10 string,
// Returns pointer to the '\0' at the end of the string
char* XLongToStr(char* dest, int32_t value)
{
char*           t;

    t = dest;
    if (t)
    {
        if (value == 0)
        {
            *t++ = '0';
        }
        else
        {
        uint32_t   v;
        uint32_t   power;
        bool           nonzeroFound;

            v = value;
            if (value < 0)
            {
                *t++ = '-';
                v = -value;
            }
            
            nonzeroFound = FALSE;
            power = 1000000000;
            while (power > 0)
            {
            unsigned int    digit;
            
                digit = v / power;
                if ((digit > 0) || nonzeroFound)
                {
                    BAE_ASSERT(digit <= 9);
                    *t++ = (char)(digit + '0');
                    v -= digit * power;
                    nonzeroFound = TRUE;
                }
                power /= 10;
            }
        }
        *t = '\0';
    }
    return t;
}

// This will convert a string to a base 10 long value
int32_t XStrnToLong(char const* pData, int32_t length)
{
    int32_t    result, num, count;
    char    data[12];
    
    result = 0;
    num = 0;
    for (count = 0; count < length; count++)
    {
        if (*pData != 0x20)
        {
            if ( ((*pData) >= '0') && ((*pData) <= '9') )
            {
                data[num++] = *pData;
                if (num > 11)
                {
                    break;
                }
            }
            else
            {
                break;
            }
        }
        pData++;
    }
    if (num)
    {
        for (count = 0; count < num; count++)
        {
            result *= 10;
            result += data[count] - '0';
        }
    }
    return result;
}


#if 1
int16_t XMemCmp(void const* src1, void const * src2, int32_t n)
{
    unsigned const char * p1;
    unsigned const char * p2;

    
    for (p1 = (unsigned const char *) src1, p2 = (unsigned const char *) src2, n++; --n;)
    {
        if (*p1++ != *p2++)
        {
            return((*--p1 < *--p2) ? -1 : +1);
        }
    }
    return 0;
}
#else
int16_t XMemCmp(void const* src1, void const* src2, int32_t n)
{
unsigned char const*    p1;
unsigned char const*    p2;

    p1 = (unsigned char const*)src1;
    p2 = (unsigned char const*)src2;
    while (--n >= 0)
    {
        if (*p1++ != *p2++)
        {
            return((*--p1 < *--p2) ? -1 : +1);
        }
    }
    return 0;
}
#endif


#if USE_CREATION_API == TRUE
/*
 *  pseudo-random number generator
 *
 */
static uint32_t seed = 1;

// return a pseudo-random number in the range of 0-32767
int16_t XRandom(void)
{
    seed = seed * 1103515245 + 12345;

    return (int16_t)((seed >> 16L) & 0x7FFFL);        // high word of long, remove high bit
}


/*
 *  seed pseudo-random number generator
 *
 */

void XSeedRandom(uint32_t n)
{
    seed = n;
}

int16_t XRandomRange(int16_t max)
{
    static char setup = 0;

    if (setup == 0)
    {
        XSeedRandom(XMicroseconds());
        setup = 1;
    }
    return XRandom() % max;
}
#endif  // USE_CREATION_API

#if USE_FULL_RMF_SUPPORT == TRUE
/****************************************************************************
**
** XIsWinInMac() determines whether a Macintosh character has an
** equivalent in the Windows character set.
**
** XIsMacInWin() determines whether a Windows character has an
** equivalent in the Macintosh character set.
**
** XTranslateWinToMac() provides the Macintosh-equivalent of a Windows
** character code.
** 
** XTranslateMacToWin() provides the Windows-equivalent of a Macintosh
** character code.
**
*****************************************************************************/
////////////////////////////////////////////////// DATA:

static const unsigned char macToWinTable[128] =
{
    0xC4,   // A-umlaut
    0xC5,   // A-circle
    0xC7,   // C-cedire
    0xC9,   // E-acute
    0xD1,   // N-tilde
    0xD6,   // O-umlaut
    0xDC,   // U-umlaut
    0xE1,   // a-acute
    0xE0,   // a-grave
    0xE2,   // a-circumflex
    0xE4,   // a-umlaut
    0xE3,   // a-tilde
    0xE5,   // a-circle
    0xE7,   // c-cedire
    0xE9,   // e-acute
    0xE8,   // e-grave

    0xEA,   // e-circumflex
    0xEB,   // e-umlaut
    0xED,   // i-acute
    0xEC,   // i-grave
    0xEE,   // i-circumflex
    0xEF,   // i-umlaut
    0xF1,   // n-tilde
    0xF3,   // o-acute
    0xF2,   // o-grave
    0xF4,   // o-circumflex
    0xF6,   // o-umlaut
    0xF5,   // o-tilde
    0xFA,   // u-acute
    0xF9,   // u-grave
    0xFB,   // u-circumflex
    0xFC,   // u-umlaut

    0x86,   // little up arrow (dagger substituted)
    0xB0,   // degrees
    0xA2,   // cents
    0xA3,   // pounds
    0xA7,   // section
    0x95,   // bullet
    0xB6,   // paragraph
    0xDF,   // german double-s
    0xAE,   // registered
    0xA9,   // copyright (same in both sets)
    0x99,   // tm
    0xB4,   // acute
    0xA8,   // umlaut
    0xB1,   // not equals (plus-minus substituted)
    0xC6,   // AE
    0xD8,   // O-slash

    0xBF,   // infinity (upsidedown-? substituted)
    0xB1,   // plus-minus
    '<',    // less-than or equal-to
    '>',    // greater-than or equal-to
    0xA5,   // yen
    0xB5,   // mu
    'd',    // delta
    'S',    // SIGMA
    'P',    // PI
    'p',    // pi
    0x83,   // f without bar (fi substituted)
    0xAA,   // small a with bar
    0xBA,   // small o with bar
    'O',    // omega
    0xE6,   // ae
    0xF8,   // o-slash

    0xBF,   // upsidedown-?
    0xA1,   // upsidedown-!
    0xAC,   // rho(?)
    '/',    // integral
    0x83,   // fi(?)
    '~',    // approx=
    'D',    // DELTA
    0xAB,   // euro double-open-quote
    0xBB,   // euro double-close-quote
    0x85,   // ellipsis
    0xA0,   // non-breaking space
    0xC0,   // A-grave
    0xC3,   // A-tilde
    0xD5,   // O-tilde
    0x8C,   // OE
    0x9C,   // oe

    0x96,   // dash
    0x97,   // long dash
    0x93,   // open "
    0x94,   // close "
    0x91,   // open '
    0x92,   // close '
    0xF7,   // divide by
    '$',    // wordstar diamond
    0xFF,   // y-umlaut
    0x9F,   // Y-umlaut
    '/',    // some other kinda slash?
    0xA4,   // can't remember what this is called
    0x8B,   // euro single-open-quote
    0x9B,   // euro single-close-quote
    'f',    // fi
    'f',    // fl

    0x87,   // little up/down arrow (double dagger substituted)
    0xB7,   // center dot
    0x82,   // baseline single-quote
    0x84,   // baseline double-quote
    0x89,   // per-thousand
    0xC2,   // A-circumflex 
    0xCA,   // E-circumflex
    0xC1,   // A-acute
    0xCB,   // E-umlaut
    0xC8,   // E-grave
    0xCD,   // I-acute
    0xCE,   // I-circumflex
    0xCF,   // I-umlaut
    0xCC,   // I-grave
    0xD3,   // O-acute
    0xD4,   // O-circumflex

    0xDA,   // U-acute
    0xDB,   // U-circumflex
    0xD9,   // U-grave
    'i',    // dotless i
    0x88,   // circumflex
    0x98,   // tilde
    0x8F,   // bar
    '\'',   // scoop accent
    0xB0,   // single dot accent (circle accent substituted)
    0xB0,   // circle accent
    0xB8,   // cedire
    0x98,   // another tilde(?)
    '.',    // backwards cedire
    '\'',   // another scoop accent
};


////////////////////////////////////////////////// FUNCTIONS:

bool XIsWinInMac(char ansiChar)
{
    return (XTranslateWinToMac(ansiChar) == (char)0xF0) ? (bool)FALSE
                                                 : (bool)TRUE;
}

bool XIsMacInWin(char macChar)
{
char        ansiChar = XTranslateMacToWin(macChar);

    return (XTranslateWinToMac(ansiChar) != macChar) ? (bool)FALSE
                                                    : (bool)TRUE;
}

char XTranslateWinToMac(char ansiChar)
{
    int iAnsiChar = (int)ansiChar;

    if (iAnsiChar < 0x80)
    {
        return ansiChar;
    }
    else
    {
    int         macChar;

        macChar = 0x80;
        while (--macChar >= 0)
        {
            if ((char)macToWinTable[macChar] == ansiChar)
            {
                return (char)(macChar + 0x80);
            }
        }
        return (char)0xF0;  // apple character
    }
}

char XTranslateMacToWin(char macChar)
{
    int iMacChar = (int)macChar;

    if (iMacChar < 0x80)
    {
        return macChar;
    }
    else
    {
        return (char)macToWinTable[macChar - 0x80];
    }
}
#endif  // USE_FULL_RMF_SUPPORT

// EOF of X_API.c
