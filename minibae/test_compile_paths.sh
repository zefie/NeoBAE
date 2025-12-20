#!/bin/bash
export NOAUTO=1
export START=0
if [ -n "${1}" ]; then
	if [ "${1}" -gt 0 ]; then
	    export START=${1}
	fi
fi

CURRENT=1

runtest() {
    echo "${CURRENT}) Testing ${@} ..."
    if [ ${CURRENT} -lt ${START} ]; then
        echo "Skipping test ${CURRENT}"
        CURRENT=$((CURRENT + 1))
        return
    fi
    make clean > /dev/null
    ${@} -j16 > /dev/null
    CURRENT=$((CURRENT + 1))
    if [ $? -ne 0 ]; then
        echo "Test failed: ${@}"
        exit 1
    fi
}

for f in Makefile.gui-mingw Makefile.mingw Makefile.gui Makefile Makefile.clang; do
    # basic
    runtest make -f ${f}
    # XMF
    runtest make -f ${f} SF2_SUPPORT=1 XMF_SUPPORT=1
    # mp3dec
    runtest make -f ${f} MP3_DEC=1
    # mp3enc
    runtest make -f ${f} MP3_ENC=1
    # full mp3
    runtest make -f ${f} MP3_ENC=1 MP3_DEC=1
    # flac dec
    runtest make -f ${f} FLAC_DEC=1
    # flac enc
    runtest make -f ${f} FLAC_ENC=1
    # flac full
    runtest make -f ${f} FLAC_ENC=1 FLAC_DEC=1
    # karaoke support
    runtest make -f ${f} KARAOKE=1
    # ogg support, no vorbis
    runtest make -f ${f} OGG_SUPPORT=1
    # vorbis dec
    runtest make -f ${f} VORBIS_DEC=1
    # vorbis enc
    runtest make -f ${f} VORBIS_ENC=1
    # vorbis full
    runtest make -f ${f} VORBIS_ENC=1 VORBIS_DEC=1
    # SF2 Support (via FluidSynth)
    runtest make -f ${f} SF2_SUPPORT=1
    if [ "$(echo "${f}" | grep "gui" -c)" -eq 1 ]; then
        # midi hw only
        runtest make -f ${f} ENABLE_MIDI_HW=1
        # playlist support only
        runtest make -f ${f} PLAYLIST=1
        # full build (gui)
        runtest make -f ${f} ENABLE_MIDI_HW=1 MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 KARAOKE=1 VORBIS_ENC=1 VORBIS_DEC=1 PLAYLIST=1 SF2_SUPPORT=1
    else
        # full build (cli)
        runtest make -f ${f} MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 KARAOKE=1 VORBIS_ENC=1 VORBIS_DEC=1 SF2_SUPPORT=1
        # SDL3
        runtest make -f ${f} USE_SDL3=1
        # SDL3 MP3_ENC=1
        runtest make -f ${f} USE_SDL3=1 MP3_ENC=1
        # SDL3 FLAC_ENC=1
        runtest make -f ${f} USE_SDL3=1 FLAC_ENC=1
        # SDL3 VORBIS_ENC=1
        runtest make -f ${f} USE_SDL3=1 VORBIS_ENC=1
        # SDL3 ALL
        runtest make -f ${f} USE_SDL3=1 MP3_ENC=1 FLAC_ENC=1 VORBIS_ENC=1
    fi
    if [ "${f}" == "Makefile.gui" ]; then
        # Linux HW Midi Drivers
        runtest make -f ${f} ENABLE_MIDI_HW=1 MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 ENABLE_ALSA=1
        runtest make -f ${f} ENABLE_MIDI_HW=1 MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 ENABLE_JACK=1
        runtest make -f ${f} ENABLE_MIDI_HW=1 MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 ENABLE_ALSA=1 ENABLE_JACK=1
    fi
    if [ "${f}" == "Makefile.mingw" ]; then
        # Windows playbae SDL
        runtest make -f ${f} USE_SDL=1
    fi
done
