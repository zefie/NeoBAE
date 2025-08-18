/****************************************************************************
*
* simple.c
*
* a command line audiofile player that supports RMF
*
* � Copyright 1999 Beatnik, Inc, All Rights Reserved.
* Written by Mark Deggeller (mark@beatnik.com)
*
* Legal Notice:
*   Beatnik products contain certain trade secrets and confidential and
*   proprietary information of Beatnik.  Use, reproduction, disclosure
*   and distribution by any means are prohibited, except pursuant to
*   a written license from Beatnik. Use of copyright notice is
*   precautionary and does not imply publication or disclosure.
*
* Restricted Rights Legend:
*   Use, duplication, or disclosure by the Government is subject to
*   restrictions as set forth in subparagraph (c)(1)(ii) of The
*   Rights in Technical Data and Computer Software clause in DFARS
*   252.227-7013 or subparagraphs (c)(1) and (2) of the Commercial
*   Computer Software--Restricted Rights at 48 CFR 52.227-19, as
*   applicable.
*
* Confidential - Internal use only
*
* History:
*   7/30/99     Created
*  8/11/99      Added PV_PrintRMFFields
*  9/21/99      Added support for wav, au, aiff, mpeg files.
*               Added BeatnikPlay(), PlaySound(), playbae()
*   10/26/99    Added PlayMIDI()
*
****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <MiniBAE.h>
#include <X_Assert.h>
#include <BAE_API.h>
#include <GenSnd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>

#ifdef main
   #undef main
#endif

#ifdef _BUILT_IN_PATCHES
	#include <BAEPatches.h>
#endif

static volatile int interruptPlayBack = FALSE;
static volatile int verboseMode = FALSE;
static volatile int silentMode = FALSE;
static volatile int fadeOut = TRUE;
static int16_t positionDisplayMultiplier = 10; // 100 = 1 second
static int16_t positionDisplayMultiplierCounter = 0;

#ifdef _WIN32
    #define stricmp _stricmp
#else
    #define stricmp strcasecmp
#endif

void intHandler(int dummy) {
    interruptPlayBack = TRUE;
}

const char* BAE_GetErrorString(BAEResult err) {
    switch(err) {
        case BAE_NO_ERROR: return "No error";
        case BAE_PARAM_ERR: return "Parameter error";
        case BAE_MEMORY_ERR: return "Memory error";
        case BAE_BAD_INSTRUMENT: return "Bad instrument";
        case BAE_BAD_MIDI_DATA: return "Bad MIDI data";
        case BAE_ALREADY_PAUSED: return "Already paused";
        case BAE_ALREADY_RESUMED: return "Already resumed";
        case BAE_DEVICE_UNAVAILABLE: return "Device unavailable";
        case BAE_NO_SONG_PLAYING: return "No song playing";
        case BAE_STILL_PLAYING: return "Still playing";
        case BAE_TOO_MANY_SONGS_PLAYING: return "Too many songs playing";
        case BAE_NO_VOLUME: return "No volume";
        case BAE_GENERAL_ERR: return "General error";
        case BAE_NOT_SETUP: return "Not setup";
        case BAE_NO_FREE_VOICES: return "No free voices";
        case BAE_STREAM_STOP_PLAY: return "Stream stop play";
        case BAE_BAD_FILE_TYPE: return "Bad file type";
        case BAE_GENERAL_BAD: return "General bad";
        case BAE_BAD_FILE: return "Bad file";
        case BAE_NOT_REENTERANT: return "Not reentrant";
        case BAE_BAD_SAMPLE: return "Bad sample";
        case BAE_BUFFER_TOO_SMALL: return "Buffer too small";
        case BAE_BAD_BANK: return "Bad bank";
        case BAE_BAD_SAMPLE_RATE: return "Bad sample rate";
        case BAE_TOO_MANY_SAMPLES: return "Too many samples";
        case BAE_UNSUPPORTED_FORMAT: return "Unsupported format";
        case BAE_FILE_IO_ERROR: return "File I/O error";
        case BAE_SAMPLE_TOO_LARGE: return "Sample too large";
        case BAE_UNSUPPORTED_HARDWARE: return "Unsupported hardware";
        case BAE_ABORTED: return "Aborted";
        case BAE_FILE_NOT_FOUND: return "File not found";
        case BAE_RESOURCE_NOT_FOUND: return "Resource not found";
        case BAE_NULL_OBJECT: return "Null object";
        case BAE_ALREADY_EXISTS: return "Already exists";
        default: return "Unknown error";
    }
}

void playbae_dprintf(const char *fmt, ...) {
        if (verboseMode) {
            va_list args;
            va_start(args, fmt);
            vfprintf(stdout, fmt, args);
            va_end(args);
        }
}

void playbae_printf(const char *fmt, ...) {
	if (!silentMode) {
	    va_list args;
	    va_start(args, fmt);
	    vfprintf(stdout, fmt, args);
	    va_end(args);
	}
}


// prototypes

char const copyrightInfo[] =
{
   "Copyright (C) 2009 Beatnik, Inc and Copyright (C) 2021 Zefie Networks. All rights reserved.\n"
};

char const usageString[] =
{
   "USAGE:  playbae  -p  {patches.hsb}\n"
   "                 -f  {Play a file (MIDI, RMF, WAV, AIFF, MPEG audio: MP2/MP3)}\n"
   "                 -o  {write output to file}\n"
   "                 -mr {mixer sample rate ie. 11025}\n"
   "                 -l  {# of times to loop}\n"
   "                 -v  {max volume (in percent, overdrive allowed) (default: 100)}\n"
   "                 -t  {max length in seconds to play midi (0 = forever)}\n"
   "		     -mc {MIDI/RMF Channels to mute, 1-16, comma seperated (example: 1,10,16)}\n"
   "                 -rv {set default reverb type}\n"
   "                 -rl {display reverb definitions}\n"
   "                 -nf {disable fade-out when stopping via time limit or CTRL-C}\n"
   "                 -q  {quiet mode}\n"
   "                 -d  {verbose (debug) mode}\n"
   "                 -h  {displays this message then exits}\n"
   "                 -x  {displays additional lesser-used options}\n"
};

char const usageStringExtra[] =
{
   " Additional flags:\n"
   "                 -2p {use 2-point Interpolation rather than default of Linear}\n"
   "                 -mv {max voices (default: 64)}\n"
   "                 -sw {Stream a WAV file}\n"
   "                 -sa {Stream a AIF file}\n"
   "                 -a  {Play a AIF file}\n"
   "                 -r  {Play a RMF file}\n"
   "                 -m  {Play a MID file}\n"
   "                 -mp {Play an MPEG audio file (MP2/MP3)}\n"
};

char const reverbTypeList[] =
{
  "Valid Reverb Types for -rv command:\n"
  "   0               Default\n"
  "   1               None\n"
  "   2               Igor's Closet\n"
  "   3               Igor's Garage\n"
  "   4               Igor's Acoustic Lab\n"
  "   5               Igor's Cavern\n"
  "   6               Igor's Dungeon\n"
  "   7               Small reflections (Reverb used for WebTV)\n"
  "   8               Early reflections (variable verb)\n"
  "   9               Basement (variable verb)\n"
  "   10              Banquet hall (variable verb)\n"
  "   11              Catacombs (variable verb)\n"
};


static int gWriteToFile = FALSE;

static void PV_Task(void *reference)
{
   if (reference)
   {
      BAEMixer_ServiceStreams(reference);
   }
}

static void PV_Idle(BAEMixer theMixer, uint32_t time)
{
   uint32_t count;
   uint32_t max;

   if (gWriteToFile)
   {
      BAEMixer_ServiceAudioOutputToFile(theMixer);
   }
#ifndef WASM
   else
   {
#endif
      max = time / 12000;
      for (count = 0; count < max; count++)
      {
         BAE_WaitMicroseconds(12000);
      }
#ifndef WASM
   }
#endif
}

#if _DEBUG
static void PV_StreamCallback(BAEStream stream, uint32_t reference)
{
   playbae_dprintf("Stream %p reference %lx done\n", stream, reference);
}

static void PV_SongCallback(BAESong song, void *reference)
{
   playbae_dprintf("Song %p reference %lx done\n", song, reference);
}

static void PV_SongMetaCallback(BAESong song, void *reference, char markerType, void *pText, int32_t textLength, int16_t currentTrack)
{
   playbae_dprintf("Song meta: reference %lx, markerType: %lx, txtlen: %lx, trk: %d, txt: %s\n", reference, markerType, textLength, currentTrack, (char *)pText);
}
#endif

static int PV_ParseCommands(int argc, char *argv[], char *command, int getResult, char *result)
{
   int count = 0;

   while (argc--)
   {
      if (strcmp(argv[count], command) == 0)
      {
         if (getResult)
         {
            strcpy(result, argv[count + 1]);
         }
         return(1);
      }
      count++;
   }
   return(0);
}


BAE_UNSIGNED_FIXED calculateVolume(BAE_UNSIGNED_FIXED volume, BAE_BOOL multiply) {
	BAE_UNSIGNED_FIXED temp = 0;
	if (multiply) {
		temp = (volume / 100) *  MAX_SONG_VOLUME;
	} else {
		temp = volume / MAX_SONG_VOLUME;
	}
	return temp;
}


static BAEResult MuteCommaSeperatedChannels(BAESong theSong, char* channelsToMute) {
	BAEResult err = BAE_NO_ERROR;
	char *token = strtok(channelsToMute, ",");
	int tokenInt = 0;
	while (token != NULL && err == 0)
        {
	     tokenInt = atoi(token);
	     if (tokenInt > 0 && tokenInt <= 16) {
		err = BAESong_MuteChannel(theSong,tokenInt - 1);
	        playbae_printf("Muting midi channel %i\n", tokenInt);
		token = strtok(NULL, ",");
	     } else {
	        playbae_printf("Invalid MIDI channel specified: %s\n", token);
		token = strtok(NULL, ",");
             }
        }
	return err;
}


static void displayCurrentPosition(uint32_t currentPosition, uint32_t totalPlayedTime) {
	int m, s, ms = 0;
	positionDisplayMultiplierCounter = positionDisplayMultiplierCounter + 1;
	if (positionDisplayMultiplierCounter == positionDisplayMultiplier) {
		positionDisplayMultiplierCounter = 0;
		m = (currentPosition / 60000);
		s = (currentPosition - (m*60000)) / 1000;
		ms = (currentPosition - (60000*m) - (s*1000));
		if (ms > 1 || s > 0 || m > 0) {
         if (totalPlayedTime > currentPosition) {
            int tm, ts, tms = 0;
            tm = (totalPlayedTime / 60000);
		      ts = (totalPlayedTime - (m*60000)) / 1000;
		      tms = (totalPlayedTime - (60000*m) - (s*1000));
            playbae_printf("Playback position: %02d:%02d.%03d (Total: %02d:%02d.%03d)\r",m,s,ms,tm,ts,tms);
         } else {
            playbae_printf("Playback position: %02d:%02d.%03d\r",m,s,ms);
         }			
#ifdef WASM
   playbae_printf("\n");
#endif
		}
	}
}

// PlayPCM()
// ---------------------------------------------------------------------
//
//
static BAEResult PlayPCM(BAEMixer theMixer, char *fileName, BAEFileType type, BAE_UNSIGNED_FIXED volume, unsigned int timeLimit)
{
   BAEResult err;
   BAESound  sound = BAESound_New(theMixer);
   BAESampleInfo songInfo;
   uint32_t currentPosition;
   int m, s, rate;
   BAE_BOOL  done;

   if (sound)
   {
      err = BAESound_LoadFileSample(sound, (BAEPathName)fileName, type);
      if (err == BAE_NO_ERROR)
      {
	 BAESound_SetVolume(sound, calculateVolume(volume, TRUE));
         err = BAESound_Start(sound, 0, BAE_FIXED_1, 0);
         if (err == BAE_NO_ERROR)
         {
	    BAESound_GetInfo(sound, &songInfo);
	    rate = songInfo.sampledRate / 65536;
            playbae_dprintf("BAE memory used for everything %ld bytes\n\n", BAE_GetSizeOfMemoryUsed()); 
	    playbae_printf("Master sound volume set to %lu%%\n", calculateVolume(volume, FALSE));
            done = FALSE;
            while (done == FALSE)
            {
	       if (interruptPlayBack) {
		  playbae_printf("Stop requested... please wait for data flush...\n");
		  interruptPlayBack = FALSE;
		  BAESound_Stop(sound, fadeOut);
	       }
               BAESound_IsDone(sound, &done);
	       BAESound_GetSamplePlaybackPosition(sound, &currentPosition);
	       currentPosition = (currentPosition / rate);
               m = (currentPosition / 60);
               s = (currentPosition - (m*60));
               if (s > 0 || m > 0) {
                  playbae_printf("Playback position: %02d:%02d\r",m,s);
               }

               if (timeLimit > 0) {
 		  if (currentPosition >= timeLimit) {
			BAESound_Stop(sound, fadeOut);
		  }
	       }
               if (done == FALSE)
               {
                  PV_Idle(theMixer, 15000);
               }
            }
            PV_Idle(theMixer, 900000);
         }
         else
         {
            playbae_printf("playbae:  Couldn't start sound (BAE Error #%d)\n", err);
         }
      }
      else
      {
         playbae_printf("playbae:  Couldn't open sound file '%s' (BAE Error #%d)\n", fileName, err);
      }
   }
   else
   {
      err = BAE_MEMORY_ERR;
   }
   BAESound_Delete(sound);
   return(err);
}

// PlayPCMStreamed()
// ---------------------------------------------------------------------
//
//
static BAEResult PlayPCMStreamed(BAEMixer theMixer, char *fileName, BAEFileType type, BAE_UNSIGNED_FIXED volume)
{
   BAEResult err;
   BAEStream stream = BAEStream_New(theMixer);
   BAE_BOOL  done;

   if (stream)
   {
      err = BAEStream_SetupFile(stream, (BAEPathName)fileName,
                                type,
                                BAE_MIN_STREAM_BUFFER_SIZE,
                                FALSE);

      if (err == BAE_NO_ERROR)
      {
	 BAEStream_SetVolume(stream, calculateVolume(volume, TRUE));
#if _DEBUG
         BAEStream_SetCallback(stream, PV_StreamCallback, 0x1234);
#endif
         err = BAEStream_Start(stream);
         if (err == BAE_NO_ERROR)
         {
 	    playbae_printf("Master stream volume set to %lu%%\n", calculateVolume(volume, FALSE));
            playbae_dprintf("BAE memory used for everything %ld bytes\n\n", BAE_GetSizeOfMemoryUsed());
            done = FALSE;
            while (done == FALSE)
            {
	       if (interruptPlayBack) {
		  playbae_printf("Stop requested... please wait for data flush...\n");
		  interruptPlayBack = 0;
		  BAEStream_Stop(stream, fadeOut);
	       }
               BAEStream_IsDone(stream, &done);
               if (done == FALSE)
               {
                  PV_Idle(theMixer, 15000);
               }
            }
            PV_Idle(theMixer, 900000);
         }
         else
         {
            playbae_printf("playbae:  Couldn't start sound (BAE Error #%d)\n", err);
         }
      }
      else
      {
         playbae_printf("playbae:  Couldn't open sound file '%s' (BAE Error #%d)\n", fileName, err);
      }
   }
   else
   {
      err = BAE_MEMORY_ERR;
   }
   BAEStream_Delete(stream);
   return(err);
}


// PlayMidi()
// ---------------------------------------------------------------------
//
//
static BAEResult PlayMidi(BAEMixer theMixer, char *fileName, BAE_UNSIGNED_FIXED volume, unsigned int timeLimit, unsigned int loopCount, BAEReverbType reverbType, char *midiMuteChannels)
{
   BAEResult err;
   BAESong   theSong = BAESong_New(theMixer);
   uint32_t currentPosition;
   uint32_t lastPosition = 0;
   uint32_t cumulativeTime = 0;
   BAE_BOOL  done;
   if (theSong)
   {
#if 0
      err = BAESong_ControlChange(theSong, 0, 1, 1, 0);

#else
      err = BAESong_LoadMidiFromFile(theSong, (BAEPathName)fileName, TRUE);
      if (err == BAE_NO_ERROR)
      {
         err = BAESong_Start(theSong, 0);
         if (err == BAE_NO_ERROR)
         {
	    BAESong_SetVolume(theSong, calculateVolume(volume, TRUE));
#if _DEBUG
            BAESong_SetCallback(theSong, (BAE_SongCallbackPtr)PV_SongCallback, (void *)0x1234);
	    BAESong_SetMetaEventCallback(theSong, (GM_SongMetaCallbackProcPtr)PV_SongMetaCallback, (void *)0x1235);
#endif
	    if (verboseMode) {
	            BAESong_DisplayInfo(theSong);
	    }

            BAEMixer_SetDefaultReverb(theMixer, (BAEReverbType)reverbType);
            playbae_printf("Reverb Type set to %d\n", reverbType);

	    if (strlen(midiMuteChannels) > 0) {
		MuteCommaSeperatedChannels(theSong, midiMuteChannels);
	    }

            BAESong_SetLoops(theSong, loopCount);
	    playbae_printf("Master song volume set to %lu%%\n", calculateVolume(volume, FALSE));
	    if (loopCount > 0) {
		playbae_printf("Will loop song %u times\n", loopCount);
	    }
            if (timeLimit > 0) {
		playbae_printf("Max Play Duration: %d seconds\n", timeLimit);
            }

            playbae_dprintf("BAE memory used for everything %ld bytes\n\n", BAE_GetSizeOfMemoryUsed());
            done = FALSE;
            while (done == FALSE)
            {
	       if (interruptPlayBack) {
		  playbae_printf("Stop requested... please wait for data flush...\n");
		  interruptPlayBack = 0;
		  BAESong_Stop(theSong, fadeOut);
	       }
               BAESong_IsDone(theSong, &done);
	       BAESong_GetMicrosecondPosition(theSong, &currentPosition);
               currentPosition = currentPosition / 1000;
               
               // Detect loop reset - if current position is significantly less than last position
               if (currentPosition < lastPosition && (lastPosition - currentPosition) > 1000) {
                   // Position reset detected, add the last position to cumulative time
                   cumulativeTime += lastPosition;
                   playbae_dprintf("Loop detected: added %u ms to cumulative time, now %u ms\n", lastPosition, cumulativeTime);
               }
               lastPosition = currentPosition;
               
               // Use cumulative time + current position for display and time limit check
               uint32_t totalPlayedTime = cumulativeTime + currentPosition;
               displayCurrentPosition(currentPosition, totalPlayedTime);
               
               if (timeLimit > 0) {
                  if (totalPlayedTime > (timeLimit * 1000) - 750) {
                        BAESong_Stop(theSong, fadeOut);
                  }
               }
               if (done == FALSE)
               {
                  PV_Idle(theMixer, 15000);
               }
            }
            PV_Idle(theMixer, 900000);
         }
         else
         {
            playbae_printf("playbae:  Couldn't start song (BAE Error #%d)\n", err);
         }
      }
      else
      {
         playbae_printf("playbae:  Couldn't open Midi file '%s' (BAE Error #%d)\n", fileName, err);
      }
#endif
   }
   else
   {
      err = BAE_MEMORY_ERR;
   }
   playbae_printf("\n");
   BAESong_Delete(theSong);
   return(err);
}

// PlayRMF()
// ---------------------------------------------------------------------
//
//
static BAEResult PlayRMF(BAEMixer theMixer, char *fileName, BAE_UNSIGNED_FIXED volume,  unsigned int timeLimit, unsigned int loopCount, BAEReverbType reverbType, char *midiMuteChannels)
{
   BAEResult err;
   BAESong   theSong = BAESong_New(theMixer);
   uint32_t currentPosition;
   uint32_t lastPosition = 0;
   uint32_t cumulativeTime = 0;
   BAE_BOOL  done;

   if (theSong)
   {
      err = BAESong_LoadRmfFromFile(theSong, (BAEPathName)fileName, 0, TRUE);
      if (err == BAE_NO_ERROR)
      {
	 BAESong_SetVolume(theSong, calculateVolume(volume, TRUE));
#if _DEBUG
         BAESong_SetCallback(theSong, (BAE_SongCallbackPtr)PV_SongCallback, (void *)0x1234);
#endif
         err = BAESong_Start(theSong, 0);
         if (err == BAE_NO_ERROR)
         {
	    if (verboseMode) {
	            BAESong_DisplayInfo(theSong);
	    }

            BAEMixer_SetDefaultReverb(theMixer, (BAEReverbType)reverbType);
            playbae_printf("Reverb Type set to %d\n", reverbType);

	    if (strlen(midiMuteChannels) > 0) {
		MuteCommaSeperatedChannels(theSong, midiMuteChannels);
	    }

            BAESong_SetLoops(theSong, loopCount);
	    playbae_printf("Master song volume set to %lu%%\n", calculateVolume(volume, FALSE));
	    if (loopCount > 0) {
		playbae_printf("Will loop song %u times\n", loopCount);
	    }
	    if (timeLimit > 0) {
		playbae_printf("Max Play Duration: %d seconds\n", timeLimit);
            }
            playbae_dprintf("BAE memory used for everything %ld bytes\n\n", BAE_GetSizeOfMemoryUsed());
            done = FALSE;
	    while (done == FALSE)
            {
	       if (interruptPlayBack) {
		  playbae_printf("Stop requested... please wait for data flush...\n");
		  interruptPlayBack = 0;
		  BAESong_Stop(theSong, fadeOut);
	       }
               BAESong_IsDone(theSong, &done);
	       BAESong_GetMicrosecondPosition(theSong, &currentPosition);
               currentPosition = currentPosition / 1000;
               
               // Detect loop reset - if current position is significantly less than last position
               if (currentPosition < lastPosition && (lastPosition - currentPosition) > 1000) {
                   // Position reset detected, add the last position to cumulative time
                   cumulativeTime += lastPosition;
                   playbae_dprintf("Loop detected: added %u ms to cumulative time, now %u ms\n", lastPosition, cumulativeTime);
               }
               lastPosition = currentPosition;
               
               // Use cumulative time + current position for display and time limit check
               uint32_t totalPlayedTime = cumulativeTime + currentPosition;
               displayCurrentPosition(currentPosition, totalPlayedTime);
               
               if (timeLimit > 0) {
                  if (totalPlayedTime > (timeLimit * 1000) - 750) {
                        BAESong_Stop(theSong, fadeOut);
                  }
               }
               if (done == FALSE)
               {
                  PV_Idle(theMixer, 15000);
               }
            }
            PV_Idle(theMixer, 900000);
         }
         else
         {
            playbae_printf("playbae:  Couldn't start song (BAE Error #%d)\n", err);
         }
      }
      else
      {
         playbae_printf("playbae:  Couldn't open RMF file '%s' (BAE Error #%d)\n", fileName, err);
      }
   }
   else
   {
      err = BAE_MEMORY_ERR;
   }
   playbae_printf("\n");
   BAESong_Delete(theSong);
   return(err);
}

static int PV_IsFileExtension(const char *path, const char *ext) {
   size_t lp, le;
   if (!path || !ext) return 0;
   lp = strlen(path);
   le = strlen(ext);
   if (le > lp) return 0;
   return stricmp(path + lp - le, ext) == 0;
}

static int PV_IsLikelyMP3Header(const unsigned char header[4]) {
   /* ID3 tag */
   if (header[0]=='I' && header[1]=='D' && header[2]=='3') return 1;
   /* Frame sync 11 bits 0xFFE, we check first 2 bytes */
   if (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) return 1;
   return 0;
}

BAEResult playFile(BAEMixer theMixer, char *parmFile, BAE_UNSIGNED_FIXED volume, unsigned int timeLimit, unsigned int loopCount, BAEReverbType reverbType, char *midiMuteChannels) {
	BAEResult err = BAE_NO_ERROR;
	char fileHeader[5] = {0}; // 4 char + 1 null byte
	int32_t filePtr;
	filePtr = BAE_FileOpenForRead(parmFile);
	if (filePtr > 0) {
		BAE_ReadFile(filePtr, &fileHeader, 4);
		BAE_FileClose(filePtr);
		if (strcmp(fileHeader,X_FILETYPE_MIDI) == 0) {
       		    playbae_printf("Playing MIDI %s\n", parmFile);
	            err = PlayMidi(theMixer, parmFile, volume, timeLimit, loopCount, reverbType, midiMuteChannels);
		} else if (strcmp(fileHeader,X_FILETYPE_RMF) == 0) {
       		    playbae_printf("Playing RMF %s\n", parmFile);
	            err = PlayRMF(theMixer, parmFile, volume, timeLimit, loopCount, reverbType, midiMuteChannels);
		} else if (strcmp(fileHeader,X_FILETYPE_AIFF) == 0) {
		    playbae_printf("Playing AIFF %s\n", parmFile);
		    err = PlayPCM(theMixer, parmFile, BAE_AIFF_TYPE, volume, timeLimit);
		} else if (strcmp(fileHeader,X_FILETYPE_WAVE) == 0) {
       		    playbae_printf("Playing WAVE %s\n", parmFile);
	            err = PlayPCM(theMixer, parmFile, BAE_AIFF_TYPE, volume, timeLimit);
		} else if (PV_IsLikelyMP3Header((unsigned char*)fileHeader) ||
		           PV_IsFileExtension(parmFile, ".mp3") || PV_IsFileExtension(parmFile, ".mp2") || PV_IsFileExtension(parmFile, ".mpg")) {
          playbae_printf("Playing MPEG audio (MP2/MP3) %s\n", parmFile);
		    err = PlayPCM(theMixer, parmFile, BAE_MPEG_TYPE, volume, timeLimit);
		} else {
		    err = (BAEResult)10069;
		}
	} else {
		err = (BAEResult)filePtr;
	}
	return err;
}

// main()
// ---------------------------------------------------------------------
int main(int argc, char *argv[])
{
   // Initialize err so we don't report garbage if early allocation fails
   BAEResult    err = BAE_NO_ERROR;
   BAEMixer     theMixer;
   int16_t    rmf, pcm, level;
   unsigned int loopCount = 0;
   unsigned int timeLimit = 0;
   int fileSpecified = FALSE;
   BAE_UNSIGNED_FIXED volume = 100 * BAE_MAX_MIDI_VOLUME;
   BAETerpMode interpol = BAE_LINEAR_INTERPOLATION;
   int maxVoices = BAE_MAX_VOICES;
   BAEBankToken bank;
   int doneCommand = 0;
   BAEReverbType reverbType = BAE_REVERB_TYPE_8; // early reflections
   char parmFile[1024];
   char midiMuteChannels[512];
   const char *libMiniBAECPUArch;
   const char *libMiniBAEVersion;
   const char *libMiniBAECompInfo;
   BAERate rate = BAE_RATE_44K;
   memset(parmFile, '\0', 1024);
   memset(midiMuteChannels, '\0', 512);


   if (PV_ParseCommands(argc, argv, "-q", FALSE, NULL))
   {
        silentMode = TRUE;
 	verboseMode = FALSE;
   }

   if (PV_ParseCommands(argc, argv, "-d", FALSE, NULL))
   {
        silentMode = FALSE;
	verboseMode = TRUE;
   }

   if (!silentMode) {
      libMiniBAEVersion = BAE_GetVersion();
      libMiniBAECompInfo = BAE_GetCompileInfo();
      libMiniBAECPUArch = BAE_GetCurrentCPUArchitecture();
      playbae_printf("playbae %s built with %s, libminiBAE %s\n", libMiniBAECPUArch, libMiniBAECompInfo, libMiniBAEVersion);
      playbae_printf(copyrightInfo);
   }

   signal(SIGINT, intHandler);
   theMixer = BAEMixer_New();
   if (theMixer)
   {

       if (PV_ParseCommands(argc, argv, "-mv", TRUE, parmFile))
       {
           maxVoices = atoi(parmFile);
	   if (maxVoices < BAE_MIN_VOICES) {
		playbae_printf("Invalid value for max voices: %d, expected 4-64. Set to %d.\n", maxVoices, BAE_MIN_VOICES);
		maxVoices = BAE_MIN_VOICES;
	   }
	   if (maxVoices > BAE_MAX_VOICES) {
		playbae_printf("Invalid value for max voices: %d, expected 4-64. Set to %d.\n", maxVoices, BAE_MAX_VOICES);
		maxVoices = BAE_MAX_VOICES;
	   }
       }

       pcm   = 1;
       rmf   = maxVoices - pcm;
       level = rmf / 3;
       if (PV_ParseCommands(argc, argv, "-rl", FALSE, NULL))
       {
           playbae_printf(reverbTypeList);
           return 0;
       }
       if (PV_ParseCommands(argc, argv, "-h", FALSE, NULL))
       {
           playbae_printf(usageString);
           return 0;
       }
       if (PV_ParseCommands(argc, argv, "-x", FALSE, NULL))
       {
           playbae_printf(usageStringExtra);
           return 0;
       }
       if (PV_ParseCommands(argc, argv, "-mr", TRUE, parmFile))
       {
          rate = (BAERate)atoi(parmFile);
       }

       if (PV_ParseCommands(argc, argv, "-l", TRUE, parmFile))
       {
          loopCount = (unsigned int)atoi(parmFile);
       }

       if (PV_ParseCommands(argc, argv, "-mc", TRUE, parmFile))
       {
	  strcpy(midiMuteChannels,parmFile);
       }

       if (PV_ParseCommands(argc, argv, "-v", TRUE, parmFile))
       {
          volume = (unsigned int)(atoi(parmFile) * BAE_MAX_MIDI_VOLUME);
          if (volume > (BAE_MAX_OVERDRIVE_PCT * BAE_MAX_MIDI_VOLUME)) {
		playbae_printf("Volume Overdrive limit reached: Setting volume to %u%%\n", BAE_MAX_OVERDRIVE_PCT);
		volume = (BAE_MAX_OVERDRIVE_PCT * BAE_MAX_MIDI_VOLUME);
          }
       }

       if (PV_ParseCommands(argc, argv, "-t", TRUE, parmFile))
       {
          timeLimit = (unsigned int)atoi(parmFile);
       }

       if (PV_ParseCommands(argc, argv, "-nf", FALSE, NULL))
       {
          fadeOut = FALSE;
       }

       if (PV_ParseCommands(argc, argv, "-2p", FALSE, NULL))
       {
          interpol = BAE_2_POINT_INTERPOLATION;
       }


      playbae_dprintf("Allocating mixer with %d voices for RMF/Midi playback\n"
             "and %d voices for PCM playback at %d sample rate\n",
             rmf, pcm,
             rate);

      playbae_dprintf("About to call BAEMixer_Open...\n");
      err = BAEMixer_Open(theMixer,
                          rate,
                          interpol,
                          BAE_USE_STEREO | BAE_USE_16,
                          rmf,                                          // midi voices
                          pcm,                                          // pcm voices
                          level,
                          TRUE);
      playbae_dprintf("BAEMixer_Open returned error code: %d (%s)\n", err, BAE_GetErrorString(err));
      if (err == BAE_NO_ERROR)
      {
         BAEMixer_SetAudioTask(theMixer, PV_Task, (void *)theMixer);

         // turn on nice verb
         if (PV_ParseCommands(argc, argv, "-rv", TRUE, parmFile))
         {
	    // user selected reverb
            reverbType = (int16_t)atoi(parmFile);
	    if (reverbType > 11) {
		playbae_printf("Invalid reverbType %d, expected 1-11. Ignored.\n", reverbType);
		reverbType = 8;
  	    }
         }
         playbae_dprintf("BAE memory used during idle prior to SetBankToFile: %ld bytes\n\n", BAE_GetSizeOfMemoryUsed());

         if (PV_ParseCommands(argc, argv, "-p", TRUE, parmFile))
         {
            playbae_printf("Using bank '%s'\n", parmFile);
            err = BAEMixer_AddBankFromFile(theMixer, (BAEPathName)parmFile, &bank);
            if (err > 0) {
		playbae_printf("Error %d loading patch bank %s",err,parmFile);
		return(1);
	    }
            playbae_dprintf("BAE memory used during idle after SetBankToFile: %ld bytes\n\n", BAE_GetSizeOfMemoryUsed());
         } else {
#ifdef _BUILT_IN_PATCHES
            playbae_printf("Using built-in bank\n");
	    err = BAEMixer_AddBankFromMemory(theMixer, BAE_PATCHES,(unsigned int)BAE_PATCHES_size,&bank);
            if (err > 0) {
		playbae_printf("Error %d loading patch bank", err);
		return(1);
	    }
            playbae_dprintf("BAE memory used during idle after SetBankToFile: %ld bytes\n\n", BAE_GetSizeOfMemoryUsed());
#else
            playbae_printf("ERR: Built-in patches were disabled at compile-time. -p flag is required.\n");
            playbae_printf(usageString);
            return 0;
#endif
	 }
         if (PV_ParseCommands(argc, argv, "-o", TRUE, parmFile))
         {
	    // do not update position timer as often since it will be much faster
	    positionDisplayMultiplier = 100; // 1 update per second of media

            err = BAEMixer_StartOutputToFile(theMixer,
                                             (BAEPathName)parmFile,
                                             BAE_WAVE_TYPE,
                                             BAE_COMPRESSION_NONE);
            if (err)
            {
               playbae_printf("Error %d accessing file for write: %s\n", err, parmFile);
            }
            else
            {
               gWriteToFile = TRUE;
               playbae_printf("Writing to file %s\n", parmFile);
            }
         }

         if (argc > 1 && argv[1][0] != (char)'-') {
		err = playFile(theMixer, argv[1], volume, timeLimit, loopCount, reverbType, midiMuteChannels);
		fileSpecified = TRUE;
                doneCommand = 1;
         }

         if (PV_ParseCommands(argc, argv, "-f", TRUE, parmFile) && !fileSpecified)
         {
		err = playFile(theMixer, parmFile, volume, timeLimit, loopCount, reverbType, midiMuteChannels);
		fileSpecified = TRUE;
                doneCommand = 1;
	 }

         if (PV_ParseCommands(argc, argv, "-a", TRUE, parmFile) && !fileSpecified)
         {
	    fileSpecified = TRUE;
            playbae_printf("Playing AIFF %s\n", parmFile);
            err         = PlayPCM(theMixer, parmFile, BAE_AIFF_TYPE, volume, timeLimit);
            doneCommand = 1;
         }
         if (PV_ParseCommands(argc, argv, "-sa", TRUE, parmFile) && !fileSpecified)
         {
	    fileSpecified = TRUE;
            playbae_printf("Streaming AIFF %s\n", parmFile);
            err         = PlayPCMStreamed(theMixer, parmFile, BAE_AIFF_TYPE, volume);
            doneCommand = 1;
         }
         if (PV_ParseCommands(argc, argv, "-w", TRUE, parmFile) && !fileSpecified)
         {
	    fileSpecified = TRUE;
            playbae_printf("Playing WAVE %s\n", parmFile);
            err         = PlayPCM(theMixer, parmFile, BAE_WAVE_TYPE, volume, timeLimit);
            doneCommand = 1;
         }
         if (PV_ParseCommands(argc, argv, "-sw", TRUE, parmFile) && !fileSpecified)
         {
	    fileSpecified = TRUE;
            playbae_dprintf("Streaming WAVE %s\n", parmFile);
            err         = PlayPCMStreamed(theMixer, parmFile, BAE_WAVE_TYPE, volume);
            doneCommand = 1;
         }
         if (PV_ParseCommands(argc, argv, "-r", TRUE, parmFile) && !fileSpecified)
         {
	    fileSpecified = TRUE;
            playbae_printf("Playing RMF %s\n", parmFile);
            err         = PlayRMF(theMixer, parmFile, volume, timeLimit, loopCount, reverbType, midiMuteChannels);
            doneCommand = 1;
         }
         if (PV_ParseCommands(argc, argv, "-m", TRUE, parmFile) && !fileSpecified)
         {
	    fileSpecified = TRUE;
            playbae_printf("Playing MIDI %s\n", parmFile);
            err         = PlayMidi(theMixer, parmFile, volume, timeLimit, loopCount, reverbType, midiMuteChannels);
            doneCommand = 1;
         }

         if (gWriteToFile)
         {
            BAEMixer_StopOutputToFile();
         }
      }
      else
      {
         playbae_printf("playbae:  Couldn't open mixer (BAE Error #%d: %s)\n", err, BAE_GetErrorString(err));
      }
   }
   else
   {
      playbae_printf("playbae:  Memory error.\n");
      err = BAE_MEMORY_ERR; // ensure err is a defined code
   }

   if (err > 0) {
        playbae_printf("playbae:  BAE Error #%d: %s\n", err, BAE_GetErrorString(err));
	return(1);
   }

   if (doneCommand == 0)
   {
      playbae_printf(usageString);
   }

   BAE_WaitMicroseconds(160000);
   BAEMixer_Delete(theMixer);
   return(0);
}


// EOF
