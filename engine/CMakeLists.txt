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
    VERSION 1.0.3
    DESCRIPTION "A small 2D visual novel engine built on SDL3"
    LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX) # (src/Apple.mm)
endif()

# C++26 for VNext's own code (the libraries keep their own: see the player below). Visual C++ has no switch
# by that name yet: CMake gives it /std:c++latest for 23 (all of C++23 and what it has of C++26); and Apple's
# compilers before clang 21 stop at Apple's own headers in C++26 (they mix enum types), so they get 23 too.
# What's C++26-only is used where a compiler has it (VN_DELETED in Basics.hpp).
if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 21))
    set(vn_cxx 23)
else()
    set(vn_cxx 26)
endif()

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

# pl_mpeg: plays MPEG-1 movies with MP2 sound (.mpg), one header of C.
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
    # The player
    src/Main.cpp                # where SDL starts the player, and its events and frames go to it
    src/Engine.cpp              # the player: starting, the game found and read, the title screen, the story's end, packing
    src/Frame.cpp               # ...each frame: the game going on and drawn, the window's view, the mistakes screen
    src/Input.cpp               # ...events: mouse, keys, controllers, touch; who's in charge; the keys on the screens
    src/Actions.cpp             # ...what buttons and keys do, and the questions they ask
    src/OpenScreens.cpp         # ...the screens open over the game
    src/PlayerSettings.cpp      # ...the player's settings
    src/Controls.cpp            # ...controls: keys and buttons, and changing them
    src/Saves.cpp               # ...saves: the game saved and loaded, rollback, the save slots
    src/Progress.cpp            # ...the player's progress, kept across plays
    src/Extras.cpp              # ...the extras: what's unlocked, the music room
    src/ReadAloud.cpp           # ...Read aloud
    src/BuiltInValues.cpp       # ...the values texts and conditions can use
    src/Translations.cpp        # ...a game's translations, and the language picked
    # The story as it plays
    src/Story.cpp               # the story going on: its lines run, its values, what it waits for
    src/StoryLines.cpp          # ...what's said: textboxes, words appearing, voices and mouths
    src/Choices.cpp             # ...choices
    src/SoundsAndMovies.cpp     # ...its music, sounds, voices and movies, captions and subtitles
    src/GameTime.cpp            # ...the game's time
    src/Stage.cpp               # ...the stage: characters, images, backgrounds, the camera, weather, effects
    src/Phone.cpp               # ...the phone: chats, calls, the people in it, its screen
    src/Flowchart.cpp           # ...the flowchart
    # The game's screens
    src/Screens.cpp             # a screen as it's open: its parts' state
    src/ScreenLayout.cpp        # ...laid out
    src/ScreenDrawing.cpp       # ...drawn
    src/ScreenLists.cpp         # ...its lists (list=)
    src/ScreenInput.cpp         # ...the mouse, keys and controller on it
    src/Typing.cpp              # ...typing into a box, and the on-screen keyboard
    src/Text.cpp                # fonts and text
    src/Shapes.cpp              # shapes and wipes
    # Images, sound and movies
    src/Assets.cpp              # the game's images (and where its sounds and movies are)
    src/Sound.cpp               # sound: music, sounds, voices
    src/MadeUpVoice.cpp         # made-up voices
    src/Movie.cpp               # movies
    # The .vnscript language
    src/Script.cpp              # the scripts as read, looked up by name
    src/ScriptWords.cpp         # ...how their lines are written: notes, quotes, words
    src/ScriptParser.cpp        # ...reading them: files, lines, mistakes, names
    src/ScriptStory.cpp         # ...the story's lines
    src/ScriptLibrary.cpp       # ...what the Library lists, and the game's settings
    src/ScriptScreens.cpp       # ...screens and their parts
    src/BuiltInScreens.cpp      # ...the player's built-in screens and keys
    src/Expressions.cpp         # ...expressions, values and random numbers
    src/Animations.cpp          # ...animations: easing, and where one is at a moment
    # The developer tools
    src/Developer.cpp           # developer mode: the developer tools opened and closed, each frame of them
    src/DeveloperButtons.cpp    # ...their own part: the buttons at the window's foot
    src/Export.cpp              # ...Export the game: a zip for each system, named after the game
    src/DeveloperInput.cpp      # ...their input: keys, a controller, moving between stops
    src/DeveloperKeyboard.cpp   # ...their on-screen keyboard
    src/DeveloperWindows.cpp    # ...their messages and questions, and choosing files
    src/DeveloperLanguage.cpp   # ...their own words, and translating them
    src/DeveloperUi.cpp         # ...their widgets
    src/PageParts.cpp           # ...the pages' page parts (Layout)
    src/Forms.cpp               # ...the forms
    src/Popups.cpp              # ...a box's list or colour wheel, and tips
    src/StoryPage.cpp           # ...the Story page
    src/StoryMap.cpp            # ...its map
    src/PlayFromLine.cpp        # ...Play from this line, Stop here, the lines reached
    src/ScreensPage.cpp         # ...the Screens page
    src/ReadyMadeScreens.cpp    # ...the ready-made screens it starts from
    src/LibraryPage.cpp         # ...the Library page
    src/LibraryAnimations.cpp   # ...its animations
    src/LibraryImages.cpp       # ...its atlases and sub-images
    src/LibraryTranslations.cpp # ...its translations, and the languages
    src/NoGamePage.cpp          # ...the page without a game: making one, or finding it
    src/About.cpp               # ...About
    src/StoryEditor.cpp         # the editor: the scripts as the pages show them, read
    src/StoryEditorForms.cpp    # ...its forms: each kind of box, each line's fields and back
    src/StoryEditorChanges.cpp  # ...its changes: undo, scenes, lines and characters
    src/StoryEditorScreens.cpp  # ...the Screens page's screens and parts in the scripts
    src/StoryEditorLibrary.cpp  # ...what the Library lists, and the game's settings
    src/ScriptEdits.cpp         # ...the scripts' text as it changes: lines, names renamed everywhere
    # Basics
    src/Basics.cpp              # helpers for words, numbers and names
    src/Colours.cpp             # colours read and said
    src/Files.cpp               # the game's files (its folder or its pack), packing, the Trash
    src/Sample.cpp              # the samples built in: New game and --sample make a game folder with them
    src/Platform.cpp            # what the player does without the system's help (Apple.mm, Windows.cpp, Linux.cpp: with it)
    src/Licences.cpp            # the licences of what's built in (licences.txt), for About and next to packs
    ${CMAKE_CURRENT_BINARY_DIR}/pl_mpeg.c
)
set_target_properties(vnext PROPERTIES OUTPUT_NAME "VNext"
    CXX_STANDARD ${vn_cxx} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    OBJCXX_STANDARD ${vn_cxx} OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
target_compile_definitions(vnext PRIVATE VN_VERSION="${PROJECT_VERSION}")

target_include_directories(vnext PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(vnext SYSTEM PRIVATE ${pl_mpeg_SOURCE_DIR})
target_link_libraries(vnext PRIVATE SDL3_mixer::SDL3_mixer SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image SDL3::SDL3 vn_sheenbidi)
if(APPLE)
    # What macOS offers: spelling (the editor), reading text aloud, VoiceOver and MP4 movies (for players), in Objective-C++.
    target_sources(vnext PRIVATE src/Apple.mm)
    set_source_files_properties(src/Apple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(vnext PRIVATE "-framework CoreFoundation" "-framework Foundation" # finding the real place of a downloaded app; the Trash
                                        "-framework AppKit" "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")
elseif(WIN32)
    target_link_libraries(vnext PRIVATE shell32) # the Recycle Bin
    # Spelling (its spell checker), reading aloud (SAPI), screen readers (UI Automation) and MP4/MOV/M4V movies (Media Foundation): src/Windows.cpp
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

# Linux: spelling (Hunspell), reading aloud (speech-dispatcher, Orca's voice too) and MP4/MOV/M4V movies (GStreamer), where the
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

# Files built into the player as bytes, so it needs nothing next to it: generated/<inc> has the array
# `name`. (A build configures again when one of them changes.)
function(vn_embed file name inc)
    file(READ ${file} hex HEX)
    string(REPEAT "[0-9a-f][0-9a-f]" 32 row)
    string(REGEX REPLACE "(${row})" "\\1\n" hex "${hex}") # (32 bytes a line)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/${inc} CONTENT "constexpr unsigned char ${name}[] = {\n${bytes}};\n" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file})
endfunction()
vn_embed(${vn_icon} kIconPng icon_png.inc)                                   # the window's icon
vn_embed(${CMAKE_CURRENT_SOURCE_DIR}/licences.txt kLicencesTxt licences_txt.inc) # About, packs

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

    # The app's icon in the Finder and the Dock, from icon.png: the sizes macOS shows, as VNext.icns in the app.
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
    target_sources(vnext PRIVATE ${vn_icns})
    set_source_files_properties(${vn_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Sign the whole app (ad hoc), not just the program in it.
    find_program(VN_CODESIGN codesign)
    if(VN_CODESIGN)
        # (a step of its own after the whole target, so it comes after the icon and the rest copied into the app)
        add_custom_target(vnext_signed ALL
            COMMAND ${VN_CODESIGN} --force --sign - $<TARGET_BUNDLE_DIR:vnext>
            COMMENT "Signing the app bundle (ad hoc)" VERBATIM)
        add_dependencies(vnext_signed vnext vnext_sample)
    endif()
elseif(WIN32)
    # The .exe's own icon, for Explorer and the taskbar: icon.ico in the build folder, made from icon.png.
    set(vn_ico ${CMAKE_CURRENT_BINARY_DIR}/icon.ico)
    if(NOT EXISTS ${vn_ico})
        message(FATAL_ERROR "The Windows icon is missing: put it (an .ico made from icon.png) at ${vn_ico}.")
    endif()
    enable_language(RC)
    # ...and its details (Explorer's Properties; signing checks the name and version): the project's version,
    # also as four numbers.
    string(REPLACE "." "," vn_version_numbers "${PROJECT_VERSION}.0")
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon.rc @ONLY CONTENT [=[#pragma code_page(65001)
1 ICON "@vn_ico@"
1 VERSIONINFO
FILEVERSION @vn_version_numbers@
PRODUCTVERSION @vn_version_numbers@
FILEOS 0x40004
FILETYPE 0x1
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "FileDescription", "VNext"
            VALUE "FileVersion", "@PROJECT_VERSION@"
            VALUE "InternalName", "VNext"
            VALUE "LegalCopyright", "Copyright (C) 2026 András Horesta"
            VALUE "OriginalFilename", "VNext.exe"
            VALUE "ProductName", "VNext"
            VALUE "ProductVersion", "@PROJECT_VERSION@"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
]=])
    target_sources(vnext PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
endif()

# The samples built into VNext (Sample.hpp): sample/, packed by the player just built (which checks its scripts) as
# sample.vnext, in the app (a Mac's Resources) or next to the player. A release puts it at the end of the .exe or the
# AppImage instead (the build workflow's Package step), so the player is all there is. And Lanterns, the demo: the samples
# with lanterns/ over them (its story, its main and extras, its gallery), packed as lanterns.vnext for the release page.
file(GLOB_RECURSE vn_sample_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sample/*)
set(vn_sample ${CMAKE_CURRENT_BINARY_DIR}/sample.vnext)
if(APPLE)
    set(vn_sample_in $<TARGET_BUNDLE_CONTENT_DIR:vnext>/Resources/sample.vnext)
else()
    set(vn_sample_in $<TARGET_FILE_DIR:vnext>/sample.vnext)
endif()
add_custom_command(OUTPUT ${vn_sample}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${vn_sample}
    COMMAND $<TARGET_FILE:vnext> --game ${CMAKE_CURRENT_SOURCE_DIR}/sample --data ${CMAKE_CURRENT_BINARY_DIR}/sample-data --pack ${vn_sample}
    COMMAND ${CMAKE_COMMAND} -E copy ${vn_sample} ${vn_sample_in}
    DEPENDS vnext ${vn_sample_files} COMMENT "Packing the samples" VERBATIM)
add_custom_target(vnext_sample ALL DEPENDS ${vn_sample})
file(GLOB_RECURSE vn_lanterns_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lanterns/*)
set(vn_lanterns ${CMAKE_CURRENT_BINARY_DIR}/lanterns.vnext)
add_custom_command(OUTPUT ${vn_lanterns}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/lanterns ${vn_lanterns}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/sample ${CMAKE_CURRENT_BINARY_DIR}/lanterns
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/lanterns ${CMAKE_CURRENT_BINARY_DIR}/lanterns
    COMMAND $<TARGET_FILE:vnext> --game ${CMAKE_CURRENT_BINARY_DIR}/lanterns --data ${CMAKE_CURRENT_BINARY_DIR}/lanterns-data --pack ${vn_lanterns}
    DEPENDS vnext ${vn_sample_files} ${vn_lanterns_files} COMMENT "Packing Lanterns, the demo" VERBATIM)
add_custom_target(vnext_lanterns ALL DEPENDS ${vn_lanterns})

# "cmake --install build --prefix DIR" puts the player in DIR: with --prefix .. (from
# this folder) next to the game folder, where it plays the game being made. (Not on a Mac, the sample next to it.)
install(TARGETS vnext BUNDLE DESTINATION . COMPONENT player RUNTIME DESTINATION . COMPONENT player)
if(NOT APPLE)
    install(FILES ${vn_sample} DESTINATION . COMPONENT player)
endif()
