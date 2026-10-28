#!/bin/sh
# Builds a check program ($1: a .cpp here, default harness.cpp) from the release build's objects
# (arm64 only, so build the player first). The program goes in build.nosync/tests/. Engine.cpp is
# compiled here without SDL's main().
# SANITIZE=1: the engine's own files are compiled again, with AddressSanitizer and UBSan (a read out of
# range, a use after free, undefined behaviour: the program stops at the first one and says where), into
# build.nosync/tests/sanitized/.
set -e
H=$(cd "$(dirname "$0")" && pwd)
SRCFILE=${1:-$H/harness.cpp}
B=$H/../build.nosync
SRC=$H/../src
T=$B/tests
FLAGS="-O1 -std=c++26 -DVN_VERSION=\"0.3.0\" -arch arm64 -mmacosx-version-min=13.3"
LINK="-arch arm64 -mmacosx-version-min=13.3"
if [ "$SANITIZE" = 1 ]; then
  T=$B/tests/sanitized
  FLAGS="$FLAGS -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined"
  LINK="$LINK -fsanitize=address,undefined"
fi
mkdir -p "$T"
OUT=$T/$(basename "${SRCFILE%.cpp}")
INC="-I$SRC -isystem $B/_deps/pl_mpeg-src -isystem $B/_deps/sdl3_mixer-src/include -isystem $B/_deps/sdl3-build/include-revision -isystem $B/_deps/sdl3-src/include -isystem $B/_deps/sdl3_ttf-src/include -isystem $B/_deps/sdl3_image-src/include -isystem $B/_deps/sheenbidi-src/Headers"
c++ $INC $FLAGS -c "$SRCFILE" -o "$OUT.o"
# (an object is made again when its source, any header, or this script (its flags), is newer than it)
stale() {
  [ -f "$2" ] || return 0
  [ "$1" -nt "$2" ] && return 0
  [ "$H/build.sh" -nt "$2" ] && return 0
  for h in "$SRC"/*.hpp; do [ "$h" -nt "$2" ] && return 0; done
  return 1
}
if stale "$SRC/Engine.cpp" "$T/Engine_nomain.o"; then c++ $INC $FLAGS -DSDL_MAIN_NOIMPL -c "$SRC/Engine.cpp" -o "$T/Engine_nomain.o"; fi
O=$B/CMakeFiles/vnext.dir
DEV="Developer DeveloperForms DeveloperLibrary DeveloperMap DeveloperPreview DeveloperScreens DeveloperShared DeveloperStory DeveloperTranslations DeveloperUi StoryEditor"
OWN="Assets Game Licenses Screens Script Expr Presentation Translation System"
# (a program that includes the developer files, to look inside them, has them already)
grep -q '#include "Developer.cpp"' "$SRCFILE" || OWN="$OWN $DEV"
OBJS="$T/Engine_nomain.o $O/pl_mpeg.c.o"
for f in $OWN; do
  if [ "$SANITIZE" = 1 ]; then
    if stale "$SRC/$f.cpp" "$T/$f.o"; then c++ $INC $FLAGS -c "$SRC/$f.cpp" -o "$T/$f.o"; fi
    OBJS="$OBJS $T/$f.o"
  else
    OBJS="$OBJS $O/src/$f.cpp.o"
  fi
done
if [ "$SANITIZE" = 1 ]; then
  if stale "$SRC/Apple.mm" "$T/Apple.o"; then c++ -x objective-c++ -fobjc-arc $INC $FLAGS -c "$SRC/Apple.mm" -o "$T/Apple.o"; fi
  OBJS="$OBJS $T/Apple.o"
else
  OBJS="$OBJS $O/src/Apple.mm.o"
fi
c++ $LINK "$OUT.o" $OBJS -o "$OUT" \
  $B/_deps/sdl3_mixer-build/libSDL3_mixer.a $B/_deps/sdl3_ttf-build/libSDL3_ttf.a $B/_deps/sdl3_image-build/libSDL3_image.a $B/_deps/sdl3-build/libSDL3.a $B/libvn_sheenbidi.a \
  -framework CoreFoundation -framework CoreText -framework CoreGraphics -framework ApplicationServices -lobjc -lm -framework CoreMedia -framework CoreVideo -framework Cocoa -Xlinker -weak_framework -Xlinker UniformTypeIdentifiers -framework IOKit -framework ForceFeedback -framework Carbon -framework CoreAudio -framework AudioToolbox -framework AVFoundation -framework Foundation -framework GameController -framework Metal -framework QuartzCore -Xlinker -weak_framework -Xlinker CoreHaptics -framework AppKit
echo "built $OUT"
