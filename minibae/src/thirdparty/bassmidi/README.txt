Due to copyright and licensing, I cannot include BASSMIDI sources or binaries here.
Get BASS and BASSMIDI from un4seen:
- https://www.un4seen.com/

You need both "BASS" and the "BASSMIDI" Plugin for your platform.
Then, set up the directories like so:

All files are relative to (projectDir)/minibae/src/thirdparty/bassmidi/

For Linux x64:
./libs/x86_64/libbassmidi.so
./libs/x86_64/bassmidi.dll
./libs/x86_64/libbass.so

For Linux x86:
./libs/x86/libbassmidi.so
./libs/x86/libbass.so

For Linux armhf:
./libs/armhf/libbassmidi.so

For Linux aarch64:
./libs/aarch64/libbassmidi.so

For Windowx x86:
./libs/x86/bass.lib
./libs/x86/bass.dll
./libs/x86/bassmidi.lib
./libs/x86/bassmidi.dll

for Windows x64:
./libs/x86_64/bass.lib
./libs/x86_64/bass.dll
./libs/x86_64/bassmidi.lib
./libs/x86_64/bassmidi.dll

Common (needed by all):
./bassmidi.h
./bass.h
