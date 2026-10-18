cmake_minimum_required(VERSION 3.22)

project(MusicPlayer VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# For clangd. Only Ninja/Makefile generators emit it; see CMakePresets.json.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Sql Network Concurrent Widgets Test)
if(NOT WIN32)
    # Linux: MPRIS / file manager over D-Bus, AAC/M4A decoding with FFmpeg's libraries.
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    find_package(PkgConfig REQUIRED)
    # Order matters for the static FFmpeg CI builds: each library before the ones it uses.
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
    find_package(Threads REQUIRED)
endif()
qt_standard_project_setup(REQUIRES 6.8)

# Strict warnings for our own targets only; third-party code keeps its defaults.
add_library(musicplayer_warnings INTERFACE)
if(MSVC)
    target_compile_options(musicplayer_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(musicplayer_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# miniaudio: audio decoding (WAV/FLAC/MP3) and WASAPI output. Only miniaudio.c is
# compiled; upstream's CMakeLists (extra nodes, Vorbis/Opus probing) is skipped.
include(FetchContent)
FetchContent_Declare(miniaudio
    URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
    URL_HASH SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
    SOURCE_SUBDIR do-not-add-subdirectory
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(miniaudio)
add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})

# SoundTouch 2.3.3 (LGPL-2.1): pitch shifting without changing tempo. Like miniaudio, only the
# library sources are compiled (no BPM detection, no command-line tool); float samples, SSE.
FetchContent_Declare(soundtouch
    URL https://www.surina.net/soundtouch/soundtouch-2.3.3.tar.gz
    URL_HASH SHA256=43b23dfac2f64a3aff55d64be096ffc7b73842c3f5665caff44975633a975a99
    SOURCE_SUBDIR do-not-add-subdirectory
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(soundtouch)
set(soundtouch_src ${soundtouch_SOURCE_DIR}/source/SoundTouch)
add_library(soundtouch STATIC
    ${soundtouch_src}/AAFilter.cpp
    ${soundtouch_src}/FIFOSampleBuffer.cpp
    ${soundtouch_src}/FIRFilter.cpp
    ${soundtouch_src}/InterpolateCubic.cpp
    ${soundtouch_src}/InterpolateLinear.cpp
    ${soundtouch_src}/InterpolateShannon.cpp
    ${soundtouch_src}/RateTransposer.cpp
    ${soundtouch_src}/SoundTouch.cpp
    ${soundtouch_src}/TDStretch.cpp
    ${soundtouch_src}/cpu_detect_x86.cpp
    ${soundtouch_src}/sse_optimized.cpp
)
target_include_directories(soundtouch SYSTEM PUBLIC ${soundtouch_SOURCE_DIR}/include)
target_compile_definitions(soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES PRIVATE SOUNDTOUCH_ALLOW_SSE)
if(MSVC)
    target_compile_options(soundtouch PRIVATE /W0) # third-party code
endif()

# TagLib: title/artist/album/duration. Static; the release tarball bundles utfcpp.
# No zlib: only needed for rare compressed ID3v2 frames.
set(BUILD_SHARED_LIBS OFF)
set(BUILD_BINDINGS OFF)
set(WITH_ZLIB OFF)
set(BUILD_TESTING OFF) # TagLib's own CppUnit tests; ours use enable_testing() directly
FetchContent_Declare(taglib
    URL https://github.com/taglib/taglib/releases/download/v2.3.2/taglib-2.3.2.tar.gz
    URL_HASH SHA256=3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(taglib)
# The 'tag' target only exports include dirs for installed builds, not the build tree.
target_include_directories(tag SYSTEM INTERFACE
    $<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib>
    $<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/toolkit>
    $<BUILD_INTERFACE:${taglib_BINARY_DIR}>)

# Non-UI code (core, models, audio). Shared by the app and the tests.
add_library(musicplayer_core STATIC
    src/models/Track.h
    src/core/StatsDatabase.h
    src/core/StatsDatabase.cpp
    src/core/StringsTranslator.h
    src/core/StringsTranslator.cpp
    src/core/TagReader.h
    src/core/TagReader.cpp
    src/core/SlskdClient.h
    src/core/SlskdClient.cpp
    src/core/AudioConverter.h
    src/core/AudioConverter.cpp
    src/core/CoverCache.h
    src/core/CoverCache.cpp
    src/core/LibraryDatabase.h
    src/core/LibraryDatabase.cpp
    src/core/LibraryFiles.h
    src/core/LibraryFiles.cpp
    src/core/LibraryIndex.h
    src/core/LibraryIndex.cpp
    src/core/UserDataMigration.h
    src/core/UserDataMigration.cpp
    src/core/KeyFile.h
    src/core/KeyFile.cpp
    src/core/Lyrics.h
    src/core/Lyrics.cpp
    src/core/LyricsFetcher.h
    src/core/LyricsFetcher.cpp
    src/core/ArtistImageFetcher.h
    src/core/ArtistImageFetcher.cpp
    src/core/LastFmClient.h
    src/core/LastFmClient.cpp
    src/core/DiscordIpc.h
    src/core/DiscordIpc.cpp
    src/core/ImgurUploader.h
    src/core/ImgurUploader.cpp
    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
    src/audio/AacSource.h
    src/audio/Reverb.h
    src/audio/Reverb.cpp
    src/audio/PitchShifter.h
    src/audio/PitchShifter.cpp
)
target_include_directories(musicplayer_core PUBLIC src)
# windows.h (pulled in by Media Foundation) otherwise defines min/max macros.
target_compile_definitions(musicplayer_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(musicplayer_core
    PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network
    PUBLIC miniaudio # AacSource.h exposes ma_data_source
    PRIVATE tag soundtouch musicplayer_warnings)
# AAC/M4A decoding: Media Foundation on Windows, FFmpeg elsewhere (picked in AacSource.h).
if(WIN32)
    target_sources(musicplayer_core PRIVATE src/audio/MfAudioSource.h src/audio/MfAudioSource.cpp)
    target_link_libraries(musicplayer_core PRIVATE mfplat mfreadwrite mfuuid)
else()
    target_sources(musicplayer_core PRIVATE src/audio/FfmpegAudioSource.h src/audio/FfmpegAudioSource.cpp)
    target_link_libraries(musicplayer_core PRIVATE PkgConfig::FFMPEG Threads::Threads m)
endif()

qt_add_executable(MusicPlayer WIN32
    src/main.cpp
    src/controllers/TrayController.h
    src/controllers/TrayController.cpp
    src/providers/CoverImageProvider.h
    src/providers/CoverImageProvider.cpp
)
if(WIN32)
    target_sources(MusicPlayer PRIVATE
        resources/app.rc # exe icon (resources/app.ico, made by resources/make_icon.ps1)
        src/controllers/MediaKeys.h
        src/controllers/MediaKeys.cpp)
else()
    # Media keys and desktop playback controls (MPRIS); Selawik stands in for Segoe UI (see main.cpp).
    target_sources(MusicPlayer PRIVATE src/controllers/Mpris.h src/controllers/Mpris.cpp)
    qt_add_resources(MusicPlayer "fonts"
        PREFIX "/fonts"
        BASE resources/fonts
        FILES resources/fonts/selawk.ttf resources/fonts/selawksl.ttf resources/fonts/selawkl.ttf
              resources/fonts/selawksb.ttf resources/fonts/selawkb.ttf)
endif()

# Theme.qml is a QML singleton (pragma Singleton); qmldir needs to know.
set_source_files_properties(qml/Theme.qml qml/Effects.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# C++ types with QML_ELEMENT / QML_SINGLETON go in SOURCES below so they are
# registered automatically. Replaces a hand-written resources.qrc.
qt_add_qml_module(MusicPlayer
    URI MusicPlayer
    VERSION 1.0
    SOURCES
        src/controllers/LibraryController.h
        src/controllers/LibraryController.cpp
        src/controllers/PlayerController.h
        src/controllers/PlayerController.cpp
        src/controllers/PlaylistController.h
        src/controllers/PlaylistController.cpp
        src/controllers/SettingsController.h
        src/controllers/SettingsController.cpp
        src/controllers/LastFmController.h
        src/controllers/LastFmController.cpp
        src/controllers/DiscordController.h
        src/controllers/DiscordController.cpp
        src/controllers/SlskdController.h
        src/controllers/SlskdController.cpp
        src/controllers/StatsController.h
        src/controllers/StatsController.cpp
        src/providers/AlbumsModel.h
        src/providers/AlbumsModel.cpp
        src/providers/DownloadsModel.h
        src/providers/DownloadsModel.cpp
        src/providers/LibraryModel.h
        src/providers/LibraryModel.cpp
        src/providers/SearchGroupsModel.h
        src/providers/SearchGroupsModel.cpp
        src/providers/SearchResultsModel.h
        src/providers/SearchResultsModel.cpp
        src/providers/StatsListModel.h
        src/providers/StatsListModel.cpp
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Effects.qml
        qml/components/EffectsControls.qml
        qml/components/AccentButton.qml
        qml/components/Card.qml
        qml/components/CoverArt.qml
        qml/components/FastWheelScroll.qml
        qml/components/HeartButton.qml
        qml/components/NewPlaylistWindow.qml
        qml/components/StarRating.qml
        qml/components/TrackContextMenu.qml
        qml/components/IconButton.qml
        qml/components/NowPlayingPanel.qml
        qml/components/LyricsPanel.qml
        qml/components/PlaybackControls.qml
        qml/components/RankedList.qml
        qml/components/RatingCard.qml
        qml/components/ResizeEdge.qml
        qml/components/SeekBar.qml
        qml/components/SlimScrollBar.qml
        qml/components/EdgeFade.qml
        qml/components/PopupBackground.qml
        qml/components/CompactComboBox.qml
        qml/components/StatTile.qml
        qml/components/TopHighlight.qml
        qml/components/CompactMenu.qml
        qml/components/CompactMenuItem.qml
        qml/components/TitleBar.qml
        qml/components/VolumeControl.qml
        qml/pages/LibraryPage.qml
        qml/pages/PlaylistsPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/SoulseekPage.qml
        qml/pages/StatisticsPage.qml
    RESOURCES
        qml/icons/app.svg
        qml/icons/lyrics.svg
        qml/icons/close.svg
        qml/icons/add.svg
        qml/icons/search.svg
        qml/icons/refresh.svg
        qml/icons/tune.svg
        qml/icons/dashboard.svg
        qml/icons/library.svg
        qml/icons/music_note.svg
        qml/icons/heart.svg
        qml/icons/heart_outline.svg
        qml/icons/next.svg
        qml/icons/playlist.svg
        qml/icons/pause.svg
        qml/icons/play.svg
        qml/icons/previous.svg
        qml/icons/repeat.svg
        qml/icons/repeat_one.svg
        qml/icons/settings.svg
        qml/icons/shuffle.svg
        qml/icons/soulseek.svg
        qml/icons/statistics.svg
        qml/icons/volume.svg
        qml/icons/volume_off.svg
)

target_link_libraries(MusicPlayer PRIVATE musicplayer_core Qt6::Quick Qt6::Concurrent Qt6::Widgets musicplayer_warnings)
if(WIN32)
    target_link_libraries(MusicPlayer PRIVATE dwmapi)
else()
    target_link_libraries(MusicPlayer PRIVATE Qt6::DBus)
    install(TARGETS MusicPlayer RUNTIME DESTINATION bin)
endif()

# Every qsTr/tr text, collected at build time, embedded as :/ui_strings.txt. Settings writes it
# out as a translation template. Regenerated whenever a QML or C++ source changes.
file(GLOB_RECURSE MUSICPLAYER_UI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(JOIN MUSICPLAYER_UI_SOURCES "\n" MUSICPLAYER_UI_SOURCE_LIST)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui_sources.txt CONTENT "${MUSICPLAYER_UI_SOURCE_LIST}\n")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui_strings.txt
    COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/ui_strings.txt
            -DSOURCE_LIST=${CMAKE_CURRENT_BINARY_DIR}/ui_sources.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractUiStrings.cmake
    DEPENDS ${MUSICPLAYER_UI_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractUiStrings.cmake
    COMMENT "Collecting UI strings for translation templates")
qt_add_resources(MusicPlayer "ui_strings"
    PREFIX "/"
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${CMAKE_CURRENT_BINARY_DIR}/ui_strings.txt)
# windows.h (MediaKeys) otherwise defines min/max macros.
target_compile_definitions(MusicPlayer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    # C4702 (unreachable code) fires inside Qt headers for qmlcachegen-compiled QML;
    # it is a codegen warning, so /external cannot silence it.
    target_compile_options(MusicPlayer PRIVATE /wd4702)
endif()
# The generated QML type registration includes these headers by bare file name.
target_include_directories(MusicPlayer PRIVATE src/controllers src/providers)
# Shipped as GRIM.exe; the target, QML module and data folders keep the MusicPlayer name.
set_target_properties(MusicPlayer PROPERTIES OUTPUT_NAME GRIM)

# Copy Qt DLLs, plugins and QML modules next to the exe so it starts by double-click.
# windeployqt skips files that are already up to date.
if(WIN32)
    add_custom_command(TARGET MusicPlayer POST_BUILD
        COMMAND Qt6::windeployqt
            --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/qml
            --no-translations
            $<TARGET_FILE:MusicPlayer>
        COMMENT "Deploying Qt runtime next to GRIM.exe"
        VERBATIM)
endif()

enable_testing()

qt_add_executable(tst_statsdatabase tests/tst_statsdatabase.cpp)
target_link_libraries(tst_statsdatabase PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_statsdatabase COMMAND tst_statsdatabase)
# Qt DLLs are not deployed next to test binaries.
set_tests_properties(tst_statsdatabase PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_discord tests/tst_discord.cpp tests/FakeHttpServer.h)
target_link_libraries(tst_discord PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_discord COMMAND tst_discord)
set_tests_properties(tst_discord PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_lastfmclient tests/tst_lastfmclient.cpp tests/FakeHttpServer.h)
target_link_libraries(tst_lastfmclient PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_lastfmclient COMMAND tst_lastfmclient)
set_tests_properties(tst_lastfmclient PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_audioengine tests/tst_audioengine.cpp)
target_link_libraries(tst_audioengine PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_audioengine COMMAND tst_audioengine)
set_tests_properties(tst_audioengine PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_tagreader tests/tst_tagreader.cpp)
# Links TagLib directly to write tags into its fixture files.
target_link_libraries(tst_tagreader PRIVATE musicplayer_core tag Qt6::Test musicplayer_warnings)
add_test(NAME tst_tagreader COMMAND tst_tagreader)
set_tests_properties(tst_tagreader PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_slskdclient tests/tst_slskdclient.cpp tests/FakeHttpServer.h)
target_link_libraries(tst_slskdclient PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_slskdclient COMMAND tst_slskdclient)
set_tests_properties(tst_slskdclient PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_covercache tests/tst_covercache.cpp)
# Links TagLib directly to embed a cover into its fixture.
target_link_libraries(tst_covercache PRIVATE musicplayer_core tag Qt6::Test musicplayer_warnings)
add_test(NAME tst_covercache COMMAND tst_covercache)
set_tests_properties(tst_covercache PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_stringstranslator tests/tst_stringstranslator.cpp)
target_link_libraries(tst_stringstranslator PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_stringstranslator COMMAND tst_stringstranslator)
set_tests_properties(tst_stringstranslator PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_librarydatabase tests/tst_librarydatabase.cpp)
target_link_libraries(tst_librarydatabase PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_librarydatabase COMMAND tst_librarydatabase)
set_tests_properties(tst_librarydatabase PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_lyricsfetcher tests/tst_lyricsfetcher.cpp tests/FakeHttpServer.h)
target_link_libraries(tst_lyricsfetcher PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_lyricsfetcher COMMAND tst_lyricsfetcher)
set_tests_properties(tst_lyricsfetcher PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_artistimagefetcher tests/tst_artistimagefetcher.cpp tests/FakeHttpServer.h)
target_link_libraries(tst_artistimagefetcher PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_artistimagefetcher COMMAND tst_artistimagefetcher)
set_tests_properties(tst_artistimagefetcher PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_effects tests/tst_effects.cpp)
target_link_libraries(tst_effects PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_effects COMMAND tst_effects)
set_tests_properties(tst_effects PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_lyrics tests/tst_lyrics.cpp)
# Links TagLib directly to write a LYRICS tag into its fixture.
target_link_libraries(tst_lyrics PRIVATE musicplayer_core tag Qt6::Test musicplayer_warnings)
add_test(NAME tst_lyrics COMMAND tst_lyrics)
set_tests_properties(tst_lyrics PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_userdatamigration tests/tst_userdatamigration.cpp)
target_link_libraries(tst_userdatamigration PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_userdatamigration COMMAND tst_userdatamigration)
set_tests_properties(tst_userdatamigration PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_libraryindex tests/tst_libraryindex.cpp)
target_link_libraries(tst_libraryindex PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_libraryindex COMMAND tst_libraryindex)
set_tests_properties(tst_libraryindex PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_libraryfiles tests/tst_libraryfiles.cpp)
target_link_libraries(tst_libraryfiles PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_libraryfiles COMMAND tst_libraryfiles)
set_tests_properties(tst_libraryfiles PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

# Needs ffmpeg on PATH to build its .m4a fixture; skips otherwise.
qt_add_executable(tst_aacsource tests/tst_aacsource.cpp)
target_link_libraries(tst_aacsource PRIVATE musicplayer_core Qt6::Test musicplayer_warnings)
add_test(NAME tst_aacsource COMMAND tst_aacsource)
set_tests_properties(tst_aacsource PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

# Needs ffmpeg on PATH; skips otherwise.
qt_add_executable(tst_audioconverter tests/tst_audioconverter.cpp)
target_link_libraries(tst_audioconverter PRIVATE musicplayer_core tag Qt6::Test musicplayer_warnings)
add_test(NAME tst_audioconverter COMMAND tst_audioconverter)
set_tests_properties(tst_audioconverter PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
