#!/bin/bash
export NOAUTO=1
runtest() {
    echo "Testing ${@} ..."
    make clean > /dev/null
    ${@} -j16 > /dev/null
    if [ $? -ne 0 ]; then
        echo "Test failed: ${@}"
        exit 1
    fi
}

for f in Makefile.gui-mingw Makefile.mingw Makefile.gui Makefile Makefile.clang; do
    # basic
    runtest make -f ${f} all
    # mp3dec
    runtest make -f ${f} MP3_DEC=1 all
    # mp3enc
    runtest make -f ${f} MP3_ENC=1 all
    # full mp3
    runtest make -f ${f} MP3_ENC=1 MP3_DEC=1 all
    # flac dec
    runtest make -f ${f} FLAC_DEC=1 all
    # flac enc
    runtest make -f ${f} FLAC_ENC=1 all
    # flac full
    runtest make -f ${f} FLAC_ENC=1 FLAC_DEC=1 all
    # midi hw only
    runtest make -f ${f} ENABLE_MIDI_HW=1 all
    # karaoke support
    runtest make -f ${f} KARAOKE=1 all
    # full build
    runtest make -f ${f} ENABLE_MIDI_HW=1 MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 KARAOKE=1 all
    if [ "${f}" == "Makefile.gui" ]; then
        # Linux HW Midi Drivers
        runtest make -f ${f} ENABLE_MIDI_HW=1 MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 ENABLE_ALSA=1 all
        runtest make -f ${f} ENABLE_MIDI_HW=1 MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 ENABLE_JACK=1 all
        runtest make -f ${f} ENABLE_MIDI_HW=1 MP3_ENC=1 MP3_DEC=1 FLAC_ENC=1 FLAC_DEC=1 ENABLE_ALSA=1 ENABLE_JACK=1 all
    fi
    if [ "${f}" == "Makefile.mingw" ]; then
        # Windows playbae SDL
        runtest make -f ${f} USE_SDL=1 all
    fi
done
