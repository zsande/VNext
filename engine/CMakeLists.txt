cmake_minimum_required(VERSION 3.28)

# The oldest macOS the player runs on (the preset builds it for both Apple Silicon and Intel Macs).
# Without this it would only run on the macOS it was built on, or newer.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.3 CACHE STRING "Oldest macOS the player runs on" FORCE)
endif()

# One build: the release build.
set(CMAKE_BUILD_TYPE Release CACHE STRING "The one build there is" FORCE)
set(CMAKE_CONFIGURATION_TYPES Release CACHE STRING "The one build there is" FORCE)

project(VNext
    VERSION 0.3.0
    DESCRIPTION "A small 2D visual novel engine built on SDL3"
    LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX) # (src/Apple.mm)
endif()

# C++26; Visual C++ has no switch by that name yet: CMake gives it /std:c++latest for 23 (all of C++23 and
# what it has of C++26; what's C++26-only is used where a compiler has it, see VN_DELETED in System.hpp).
if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows: the C++ runtime goes inside the .exe (and SDL's libraries), so players don't need to
# install anything.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Dependencies (fetched from GitHub, statically linked)
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
    SYSTEM)

# SDL3_image: only formats that need no external libraries (PNG/JPG via stb or the OS).
set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG release-3.4.8
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    GIT_SUBMODULES ""   # its vendored codecs are not needed (and are several hundred MB)
    EXCLUDE_FROM_ALL
    SYSTEM)

# SDL3_ttf: vendored FreeType (draws the letters) and HarfBuzz (joins and orders them: Arabic,
# Hebrew, Hindi, Thai..., and kerning). No PlutoSVG (colour emoji fonts), to keep the download smaller.
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ ON CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG release-3.2.2
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    GIT_SUBMODULES "external/freetype;external/harfbuzz"
    EXCLUDE_FROM_ALL
    SYSTEM)

# SDL3_mixer: OGG (stb_vorbis), MP3 (dr_mp3), FLAC (dr_flac) and WAV are decoded by built-in
# single-header codecs, so none of its vendored libraries (or submodules) are needed.
set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_TREMOR OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MP3_DRMP3 ON CACHE BOOL "" FORCE)
set(SDLMIXER_MP3_MPG123 OFF CACHE BOOL "" FORCE)
set(SDLMIXER_FLAC_DRFLAC ON CACHE BOOL "" FORCE)
set(SDLMIXER_FLAC_LIBFLAC OFF CACHE BOOL "" FORCE)
set(SDLMIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_WAVPACK OFF CACHE BOOL "" FORCE)
set(SDLMIXER_GME OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MIDI OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG release-3.2.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    GIT_SUBMODULES ""
    EXCLUDE_FROM_ALL
    SYSTEM)

# pl_mpeg: plays MPEG-1 videos with MP2 sound (.mpg), one header of C.
FetchContent_Declare(pl_mpeg
    GIT_REPOSITORY https://github.com/phoboslab/pl_mpeg.git
    GIT_TAG c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
    SYSTEM)

# SheenBidi: Unicode's bidirectional algorithm (right-to-left text, with left-to-right in it), in C. Built
# below from its one unity source: its own CMake project would add install rules for its headers.
FetchContent_Declare(SheenBidi
    GIT_REPOSITORY https://github.com/Tehreer/SheenBidi.git
    GIT_TAG cfe430e7375a7845b679adae9d51dac6deaa8858 # v3.0.0
    GIT_PROGRESS TRUE
    SOURCE_SUBDIR none # (fetched only)
    EXCLUDE_FROM_ALL
    SYSTEM)

FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_ttf SDL3_mixer pl_mpeg SheenBidi)

add_library(vn_sheenbidi STATIC ${sheenbidi_SOURCE_DIR}/Source/SheenBidi.c)
target_compile_definitions(vn_sheenbidi PRIVATE SB_CONFIG_UNITY)
target_include_directories(vn_sheenbidi SYSTEM PUBLIC ${sheenbidi_SOURCE_DIR}/Headers PRIVATE ${sheenbidi_SOURCE_DIR}/Source)

# ---------------------------------------------------------------------------
# The player
# ---------------------------------------------------------------------------
# pl_mpeg is a single header of C: its code is compiled once, as C, from this file.
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pl_mpeg.c CONTENT [=[
#include <stddef.h>
#include <stdio.h>
#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>
]=])

add_executable(vnext
    src/Engine.cpp                # the player: window, menus, saving, settings, reload (and main)
    src/Developer.cpp             # developer mode: the developer tools (opening them, their bar and tabs)
    src/DeveloperStory.cpp        # ...the Story tab
    src/DeveloperPreview.cpp      # ...the preview (a row's, a screen's)
    src/DeveloperScreens.cpp      # ...the Screens tab
    src/DeveloperMap.cpp          # ...the Map tab
    src/DeveloperLibrary.cpp      # ...the Library tab
    src/DeveloperTranslations.cpp # ...the Translate tab
    src/DeveloperForms.cpp        # ...the forms, and the Settings tab
    src/DeveloperUi.cpp           # ...their widgets
    src/DeveloperShared.cpp       # ...the helpers the tabs share
    src/StoryEditor.cpp           # ...the editor: the scripts as forms
    src/Script.cpp                # the .vnscript language: lines and words, the parser, the built-in screens
    src/Expr.cpp                  # ...its expressions and values
    src/Presentation.cpp          # ...keys, colours and what the player shows
    src/Translation.cpp           # ...translations
    src/Game.cpp                  # the story as it plays: the game, the stage, the flowchart
    src/Screens.cpp               # fonts, text, shapes and the script's screens
    src/Assets.cpp                # pictures, sound, made-up voices and videos
    src/System.cpp                # helpers, the game's files (folder or pack), saves, the player's settings
    src/Licenses.cpp              # the licenses of what's built in, for About and next to packs
    ${CMAKE_CURRENT_BINARY_DIR}/pl_mpeg.c
)
set_target_properties(vnext PROPERTIES OUTPUT_NAME "VNext")
target_compile_definitions(vnext PRIVATE VN_VERSION="${PROJECT_VERSION}")

target_include_directories(vnext PRIVATE src)
target_include_directories(vnext SYSTEM PRIVATE ${pl_mpeg_SOURCE_DIR})
target_link_libraries(vnext PRIVATE SDL3_mixer::SDL3_mixer SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image SDL3::SDL3 vn_sheenbidi)
if(APPLE)
    # What macOS offers: spelling (the editor), reading text aloud and MP4 videos (for players), in Objective-C++.
    target_sources(vnext PRIVATE src/Apple.mm)
    set_source_files_properties(src/Apple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(vnext PRIVATE "-framework CoreFoundation" "-framework Foundation" # finding the real place of a downloaded app; the Trash
                                        "-framework AppKit" "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")
elseif(WIN32)
    target_link_libraries(vnext PRIVATE shell32) # the Recycle Bin
    # Spelling (its spell checker), reading aloud (SAPI) and MP4/MOV/M4V videos (Media Foundation): src/Windows.cpp
    target_sources(vnext PRIVATE src/Windows.cpp)
    target_link_libraries(vnext PRIVATE ole32 mfplat mfreadwrite mfuuid)
endif()
if(MSVC)
    target_compile_options(vnext PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/permissive-;/Zc:__cplusplus>")
else()
    target_compile_options(vnext PRIVATE "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion>") # (Apple.mm too; -Wconversion includes the sign changes)
    # GCC also warns about designated initializers that leave fields to their defaults (clang doesn't): they do on purpose.
    target_compile_options(vnext PRIVATE "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU>>:-Wno-missing-field-initializers>")
endif()

# Linux: spelling (Hunspell), reading aloud (speech-dispatcher) and MP4/MOV/M4V videos (GStreamer), where the
# player's system has them (looked for when needed: nothing more to install to build it): src/Linux.cpp.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(vnext PRIVATE src/Linux.cpp)
    target_link_libraries(vnext PRIVATE ${CMAKE_DL_LIBS})
endif()
# Linux: the C++ library goes inside the player too, so it also runs where an older one is installed.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(vnext PRIVATE -static-libstdc++ -static-libgcc)
endif()

# A GUI app: no console window on Windows (SDL supplies WinMain), an .app bundle on macOS. Logs go
# to log.txt in the player's data folder, and to the developer tools in developer mode.
set_target_properties(vnext PROPERTIES WIN32_EXECUTABLE TRUE)
# The player's icon: icon.png in the build folder (a square PNG), the window's when the game has none.
set(vn_icon ${CMAKE_CURRENT_BINARY_DIR}/icon.png)
if(NOT EXISTS ${vn_icon})
    message(FATAL_ERROR "The player's icon is missing: put it (a square PNG) at ${vn_icon}.")
endif()

if(APPLE)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Info.plist.in [=[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>${MACOSX_BUNDLE_ICON_FILE}</string>
    <key>CFBundleIdentifier</key>
    <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>${MACOSX_BUNDLE_BUNDLE_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${MACOSX_BUNDLE_SHORT_VERSION_STRING}</string>
    <key>CFBundleVersion</key>
    <string>${MACOSX_BUNDLE_BUNDLE_VERSION}</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
</dict>
</plist>
]=])
    set_target_properties(vnext PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "VNext"
        MACOSX_BUNDLE_ICON_FILE "VNext.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.vnext.engine"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})

    # The app's icon, from icon.png: the sizes macOS shows, as VNext.icns in the app (a packed game keeps
    # it when the game has no icon of its own), and the picture itself, for the window.
    set(vn_iconset ${CMAKE_CURRENT_BINARY_DIR}/VNext.iconset)
    set(vn_icns ${CMAKE_CURRENT_BINARY_DIR}/VNext.icns)
    set(vn_icon_sizes)
    foreach(size 16 32 128 256 512)
        math(EXPR double "${size} * 2")
        list(APPEND vn_icon_sizes COMMAND sips -z ${size} ${size} ${vn_icon} --out ${vn_iconset}/icon_${size}x${size}.png
                                  COMMAND sips -z ${double} ${double} ${vn_icon} --out ${vn_iconset}/icon_${size}x${size}@2x.png)
    endforeach()
    add_custom_command(OUTPUT ${vn_icns}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${vn_iconset}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${vn_iconset}
        ${vn_icon_sizes}
        COMMAND iconutil -c icns ${vn_iconset} -o ${vn_icns}
        DEPENDS ${vn_icon} COMMENT "Making the app icon" VERBATIM)
    target_sources(vnext PRIVATE ${vn_icns} ${vn_icon})
    set_source_files_properties(${vn_icns} ${vn_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Sign the whole app (ad hoc), not just the program in it.
    find_program(VN_CODESIGN codesign)
    if(VN_CODESIGN)
        # (a step of its own after the whole target, so it comes after the icon and the rest copied into the app)
        add_custom_target(vnext_signed ALL
            COMMAND ${VN_CODESIGN} --force --sign - $<TARGET_BUNDLE_DIR:vnext>
            COMMENT "Signing the app bundle (ad hoc)" VERBATIM)
        add_dependencies(vnext_signed vnext)
    endif()
else()
    # Elsewhere the window's icon is icon.png next to the program (copied there, and installed with it);
    # on Windows the .exe has it too, for Explorer and the taskbar: icon.ico in the build folder, made
    # from icon.png.
    add_custom_command(TARGET vnext POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${vn_icon} $<TARGET_FILE_DIR:vnext>/icon.png VERBATIM)
    install(FILES ${vn_icon} DESTINATION . COMPONENT player)
    if(WIN32)
        set(vn_ico ${CMAKE_CURRENT_BINARY_DIR}/icon.ico)
        if(NOT EXISTS ${vn_ico})
            message(FATAL_ERROR "The Windows icon is missing: put it (an .ico made from icon.png) at ${vn_ico}.")
        endif()
        enable_language(RC)
        file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon.rc CONTENT "1 ICON \"${vn_ico}\"\n")
        target_sources(vnext PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
    endif()
endif()

# "cmake --install build --prefix DIR" puts the player in DIR: with --prefix .. (from
# this folder) next to the game folder, where it plays the game being made.
install(TARGETS vnext BUNDLE DESTINATION . COMPONENT player RUNTIME DESTINATION . COMPONENT player)

# The web: the page plays the game folder next to this engine/ folder, if there is one.
if(EMSCRIPTEN)
    set_target_properties(vnext PROPERTIES SUFFIX ".html")
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game)
        target_link_options(vnext PRIVATE "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/../game@/game")
    endif()
endif()
