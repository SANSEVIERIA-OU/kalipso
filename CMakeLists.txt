cmake_minimum_required(VERSION 3.22)
# Must precede project(): the oldest macOS the app and its helper run on.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum macOS version (Qt 6.12 requires 14.4)")
project(Kalipso VERSION 2.4 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network Svg)
find_package(Qt6 QUIET COMPONENTS LinguistTools)
set(KALIPSO_WARNINGS $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)

# --- Script tracking: Qt Core only ---------------------------------------------
add_library(prompter_core STATIC src/tracker.cpp src/spoken_words.cpp)
target_include_directories(prompter_core PUBLIC src)
target_link_libraries(prompter_core PUBLIC Qt6::Core)
target_compile_options(prompter_core PRIVATE ${KALIPSO_WARNINGS})

# --- Speech recognition ---------------------------------------------------------
add_library(kalipso_speech STATIC src/speech.cpp src/microphone.cpp src/moonshine.cpp src/system_speech/client.cpp)
target_link_libraries(kalipso_speech PUBLIC prompter_core Qt6::Core)
target_compile_options(kalipso_speech PRIVATE ${KALIPSO_WARNINGS})
if(APPLE OR WIN32)
  # Linux captures through PipeWire's pw-record; macOS and Windows use Qt Multimedia.
  if(APPLE)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Multimedia)
  else()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Multimedia)
  endif()
  target_link_libraries(kalipso_speech PUBLIC Qt6::Multimedia)
  target_compile_definitions(kalipso_speech PUBLIC KALIPSO_QT_AUDIO=1)
endif()

# --- Speech model downloads: the built-in catalog, fingerprint-checked -----------
add_library(kalipso_vendor STATIC third_party/puff/puff.c third_party/qrcodegen/qrcodegen.cpp)
add_library(kalipso_downloads STATIC src/speech_download.cpp)
target_link_libraries(kalipso_downloads PUBLIC kalipso_speech Qt6::Network PRIVATE kalipso_vendor)
target_compile_options(kalipso_downloads PRIVATE ${KALIPSO_WARNINGS})
qt_add_resources(kalipso_downloads "speech_catalog" PREFIX "/speech" BASE ${CMAKE_CURRENT_SOURCE_DIR}/tools/data
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/tools/data/speech-catalog.json)
file(GLOB_RECURSE KALIPSO_NATIVE_NOTICES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/native-notices/*/*)
qt_add_resources(kalipso_downloads "speech_notices" PREFIX "/speech/notices"
  BASE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/native-notices FILES ${KALIPSO_NATIVE_NOTICES})

# --- Linux companion for the Plasma widget and GNOME extension ------------------
if(UNIX AND NOT APPLE)
  find_package(Qt6 REQUIRED COMPONENTS DBus Gui)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
  add_library(kalipso_colors STATIC src/screen_colors/portal.cpp)
  target_include_directories(kalipso_colors PUBLIC src)
  target_link_libraries(kalipso_colors PUBLIC Qt6::Gui Qt6::DBus PRIVATE PkgConfig::PIPEWIRE)
  target_compile_definitions(kalipso_colors PUBLIC KALIPSO_PORTAL_COLORS=1)

  # The companion reads the app's script library (src/library.cpp) for the widget and the extension.
  add_executable(kalipso-engine src/engine/main.cpp src/library.cpp)
  target_link_libraries(kalipso-engine PRIVATE kalipso_downloads kalipso_colors prompter_core Qt6::DBus)
  target_compile_options(kalipso-engine PRIVATE ${KALIPSO_WARNINGS})
  # Its messages in the session's language: the frontends show them as they are.
  file(GLOB KALIPSO_ENGINE_TRANSLATIONS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/kalipso-engine_*.ts)
  if(Qt6LinguistTools_FOUND AND KALIPSO_ENGINE_TRANSLATIONS)
    # The .ts file also covers the speech, download and color sources the companion links. Refresh it with
    # lupdate -no-obsolete -locations none src/engine src/library.cpp src/speech.cpp src/speech_download.cpp
    #   src/microphone.cpp src/moonshine.cpp src/system_speech/client.cpp src/screen_colors/portal.cpp
    #   -ts translations/kalipso-engine_it.ts
    qt_add_translations(kalipso-engine TS_FILES ${KALIPSO_ENGINE_TRANSLATIONS} RESOURCE_PREFIX /i18n)
  endif()
  install(TARGETS kalipso-engine RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Engine)
  configure_file(packaging/io.github.sansevieria_ou.Kalipso.Engine.service.in
    io.github.sansevieria_ou.Kalipso.Engine.service @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.sansevieria_ou.Kalipso.Engine.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services COMPONENT Engine)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/kalipso-engine COMPONENT Engine)
  # kalipso-voice-setup: downloads one language from a terminal, as Settings does.
  install(PROGRAMS packaging/kalipso-voice-setup DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Engine)

  # The Plasma widget, the GNOME Shell extension and the KWin script, installed where each desktop looks
  # for add-ons installed system-wide: every Linux package (.deb, .rpm, Arch) carries them with the app.
  # desktop/package.py writes the same files as its archives, unpacked.
  option(KALIPSO_DESKTOP_ADDONS "Install the Plasma widget, the GNOME Shell extension and the KWin script" ON)
  if(KALIPSO_DESKTOP_ADDONS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(addons ${CMAKE_CURRENT_BINARY_DIR}/desktop-addons)
    file(GLOB_RECURSE addon_sources CONFIGURE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/desktop/plasma/* ${CMAKE_CURRENT_SOURCE_DIR}/desktop/gnome/*
      ${CMAKE_CURRENT_SOURCE_DIR}/desktop/*.mjs ${CMAKE_CURRENT_SOURCE_DIR}/desktop/po/*.po
      ${CMAKE_CURRENT_SOURCE_DIR}/packaging/kwin/* ${CMAKE_CURRENT_SOURCE_DIR}/src/fonts/*)
    add_custom_command(OUTPUT ${addons}/stamp
      COMMAND ${CMAKE_COMMAND} -E rm -rf ${addons}/plasma ${addons}/gnome ${addons}/kwin
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/desktop/package.py --tree ${addons}
      COMMAND ${CMAKE_COMMAND} -E touch ${addons}/stamp
      DEPENDS ${addon_sources} ${CMAKE_CURRENT_SOURCE_DIR}/desktop/package.py
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.sansevieria_ou.kalipso.svg
      COMMENT "Preparing the Plasma widget, the GNOME Shell extension and the KWin script" VERBATIM)
    add_custom_target(desktop_addons ALL DEPENDS ${addons}/stamp)
    install(DIRECTORY ${addons}/plasma/
      DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/io.github.sansevieria_ou.kalipso)
    install(DIRECTORY ${addons}/gnome/
      DESTINATION ${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions/kalipso@sansevieria-ou.github.io)
    install(DIRECTORY ${addons}/kwin/ DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts/kalipso-companion)
  endif()
endif()

# --- Standalone application -----------------------------------------------------
add_library(kalipso_app STATIC
  src/window.cpp src/reader.cpp src/backdrop.cpp src/library.cpp src/import.cpp src/remote.cpp
  src/app_bridge.cpp src/script_highlighter.cpp src/session_stats.cpp src/impress.cpp src/icons.cpp src/motion.cpp
  src/platform/capture_exclusion.cpp src/platform/kwin_placement.cpp)
target_link_libraries(kalipso_app PUBLIC kalipso_downloads Qt6::Widgets Qt6::Network Qt6::Svg PRIVATE kalipso_vendor)
file(GLOB KALIPSO_ICONS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/icons/*.svg ${CMAKE_CURRENT_SOURCE_DIR}/src/icons/generated/*.svg)
qt_add_resources(kalipso_app "icons" PREFIX "/icons" BASE ${CMAKE_CURRENT_SOURCE_DIR}/src/icons FILES ${KALIPSO_ICONS})
file(GLOB KALIPSO_FONTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/fonts/*.ttf)
qt_add_resources(kalipso_app "fonts" PREFIX "/fonts" BASE ${CMAKE_CURRENT_SOURCE_DIR}/src/fonts FILES ${KALIPSO_FONTS})
qt_add_resources(kalipso_app "impress" PREFIX "/impress" BASE ${CMAKE_CURRENT_SOURCE_DIR}/src/impress
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/impress/impress_follow.py)
# The KWin script that places the reader on Plasma with Wayland (src/platform/kwin_placement.h).
qt_add_resources(kalipso_app "kwin" PREFIX "/kwin" BASE ${CMAKE_CURRENT_SOURCE_DIR}/packaging/kwin/contents/code
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/kwin/contents/code/main.js)
target_compile_definitions(kalipso_app PUBLIC KALIPSO_VERSION="${PROJECT_VERSION}")
target_compile_options(kalipso_app PRIVATE ${KALIPSO_WARNINGS})
if(TARGET kalipso_colors)
  target_link_libraries(kalipso_app PUBLIC kalipso_colors)
endif()
if(APPLE)
  target_link_libraries(kalipso_app PUBLIC "-framework CoreFoundation")
endif()
if(WIN32)
  target_link_libraries(kalipso_app PUBLIC gdi32 user32)
endif()

add_executable(kalipso src/main.cpp)
target_link_libraries(kalipso PRIVATE kalipso_app)
target_compile_options(kalipso PRIVATE ${KALIPSO_WARNINGS})
file(GLOB KALIPSO_TRANSLATIONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/translations/kalipso_*.ts)
if(Qt6LinguistTools_FOUND AND KALIPSO_TRANSLATIONS)
  qt_add_translations(kalipso TS_FILES ${KALIPSO_TRANSLATIONS} RESOURCE_PREFIX /i18n)
endif()

if(APPLE)
  set_target_properties(kalipso PROPERTIES MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in")
  find_program(SWIFTC swiftc REQUIRED)
  if(CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 KALIPSO_SWIFT_ARCH)
  else()
    set(KALIPSO_SWIFT_ARCH ${CMAKE_HOST_SYSTEM_PROCESSOR})
  endif()
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech"
    COMMAND ${SWIFTC} -parse-as-library -swift-version 5 -O
      -target ${KALIPSO_SWIFT_ARCH}-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/system_speech/macos.swift" -o "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech"
    DEPENDS src/system_speech/macos.swift VERBATIM)
  add_custom_target(system_speech_helper ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech")
  add_dependencies(kalipso system_speech_helper)
  add_custom_command(TARGET kalipso POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:kalipso>/Contents/Helpers"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech" "$<TARGET_BUNDLE_DIR:kalipso>/Contents/Helpers/")
  install(TARGETS kalipso BUNDLE DESTINATION .)
elseif(WIN32)
  set_target_properties(kalipso PROPERTIES WIN32_EXECUTABLE TRUE)
  add_executable(kalipso-system-speech src/system_speech/windows.cpp)
  target_link_libraries(kalipso-system-speech PRIVATE Qt6::Core sapi ole32)
  target_compile_definitions(kalipso-system-speech PRIVATE NOMINMAX UNICODE _UNICODE)
  install(TARGETS kalipso kalipso-system-speech RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  install(TARGETS kalipso RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/io.github.sansevieria_ou.kalipso.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/io.github.sansevieria_ou.kalipso.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(FILES packaging/io.github.sansevieria_ou.kalipso.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/kalipso)

# --- Tests ------------------------------------------------------------------------
if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  find_package(Python3 QUIET COMPONENTS Interpreter)
  find_program(DBUS_RUN_SESSION dbus-run-session)
  find_program(GDBUS_EXECUTABLE gdbus)

  add_executable(core_tests tests/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE prompter_core Qt6::Test)
  add_test(NAME core COMMAND core_tests)

  add_executable(library_tests tests/library_tests.cpp)
  target_link_libraries(library_tests PRIVATE kalipso_app Qt6::Test)
  add_test(NAME library COMMAND library_tests)
  set_tests_properties(library PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_library(fake_moonshine SHARED tests/fake_moonshine.cpp)
  set_target_properties(fake_moonshine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  add_library(fake_vosk SHARED tests/fake_vosk.cpp)
  set_target_properties(fake_vosk PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  add_executable(download_tests tests/download_tests.cpp)
  target_link_libraries(download_tests PRIVATE kalipso_downloads Qt6::Test)
  add_test(NAME downloads COMMAND download_tests)

  add_executable(speech_tests tests/speech_tests.cpp)
  target_link_libraries(speech_tests PRIVATE kalipso_speech Qt6::Test)
  target_compile_definitions(speech_tests PRIVATE FAKE_MOONSHINE_LIBRARY="$<TARGET_FILE:fake_moonshine>"
    FAKE_VOSK_LIBRARY="$<TARGET_FILE:fake_vosk>")
  add_dependencies(speech_tests fake_moonshine fake_vosk)
  add_test(NAME speech COMMAND speech_tests)

  add_executable(fake_system_speech tests/fake_system_speech.cpp)
  target_link_libraries(fake_system_speech PRIVATE Qt6::Core)
  add_executable(system_speech_tests tests/system_speech_tests.cpp)
  target_link_libraries(system_speech_tests PRIVATE kalipso_speech Qt6::Test)
  target_compile_definitions(system_speech_tests PRIVATE FAKE_SYSTEM_SPEECH="$<TARGET_FILE:fake_system_speech>")
  add_dependencies(system_speech_tests fake_system_speech)
  add_test(NAME system_speech COMMAND system_speech_tests)

  if(WIN32)
    set(KALIPSO_TEST_QPA windows)
  else()
    set(KALIPSO_TEST_QPA offscreen)
  endif()
  # Every screen in every combination, checked (and, given a directory, photographed). Run by hand
  # under each desktop look too: Breeze light and dark, the drawn style.
  add_executable(layout_matrix tests/layout_matrix.cpp)
  target_link_libraries(layout_matrix PRIVATE kalipso_app Qt6::Test)
  add_test(NAME layout_matrix COMMAND layout_matrix)
  set_tests_properties(layout_matrix PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${KALIPSO_TEST_QPA}")
  add_executable(ui_tests tests/ui_tests.cpp)
  target_link_libraries(ui_tests PRIVATE kalipso_app Qt6::Test)
  target_compile_definitions(ui_tests PRIVATE FAKE_MOONSHINE_LIBRARY="$<TARGET_FILE:fake_moonshine>")
  add_dependencies(ui_tests fake_moonshine)
  add_test(NAME ui COMMAND ui_tests -o ui-results.txt,txt -o -,txt)
  # Two monitors, one of which goes away (the offscreen platform with a configuration file; its
  # option syntax takes no drive letters, so not on Windows).
  if(NOT WIN32)
    add_executable(migration_tests tests/migration_tests.cpp)
    target_include_directories(migration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(migration_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME migration COMMAND migration_tests)
    add_executable(screens_tests tests/screens_tests.cpp)
    target_link_libraries(screens_tests PRIVATE kalipso_app Qt6::Test)
    add_test(NAME screens COMMAND screens_tests)
    set_tests_properties(screens PROPERTIES ENVIRONMENT
      "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/two-screens.json")
  endif()
  # The same check of icon motion with the system asking for reduced motion.
  add_test(NAME ui_reduced_motion COMMAND ui_tests iconMotionFollowsTheSystemChoice)
  if(WIN32)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    set_tests_properties(ui_reduced_motion PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;KALIPSO_REDUCED_MOTION=1")
  else()
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(ui_reduced_motion PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;KALIPSO_REDUCED_MOTION=1")
  endif()

  # Frontend JavaScript: Node for logic, and the real QML (Plasma) and GJS (GNOME) engines.
  find_program(NODE_EXECUTABLE node)
  if(NODE_EXECUTABLE)
    add_test(NAME tracker_js COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/desktop/tracker.test.mjs)
    add_test(NAME contrast_js COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/contrast.test.mjs)
    add_test(NAME kwin_script_js COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/kwin_script.test.mjs)
  endif()
  find_program(QMLTESTRUNNER qmltestrunner
    HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBEXECS}" /usr/lib/qt6/bin)
  find_program(GJS_EXECUTABLE gjs)
  if(Python3_Interpreter_FOUND AND QMLTESTRUNNER AND GJS_EXECUTABLE)
    add_test(NAME frontend_runtimes COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/script_runtimes.py
      ${QMLTESTRUNNER} ${GJS_EXECUTABLE})
  endif()
  find_program(DBUS_RUN_SESSION dbus-run-session)
  if(Python3_Interpreter_FOUND AND GJS_EXECUTABLE AND DBUS_RUN_SESSION)
    # The extension finds a Kalipso 2.0 companion by the name it had before the move to SANSEVIERIA-OU.
    add_test(NAME companion_names COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/companion_names_test.py
      ${GJS_EXECUTABLE})
  endif()
  if(Python3_Interpreter_FOUND)
    add_test(NAME shared_modules COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/sync-shared.py --check)
  endif()

  add_executable(speech_probe benchmarks/speech_probe.cpp)
  target_link_libraries(speech_probe PRIVATE kalipso_speech)
  add_executable(follow_probe benchmarks/follow_probe.cpp)
  target_link_libraries(follow_probe PRIVATE kalipso_speech prompter_core)
  add_executable(tracker_benchmark benchmarks/tracker_benchmark.cpp)
  target_link_libraries(tracker_benchmark PRIVATE prompter_core)

  if(Python3_Interpreter_FOUND)
    add_test(NAME package_translations COMMAND ${Python3_EXECUTABLE}
                                               ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_translations_test.py)
    add_test(NAME version_scheme COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/version_test.py)
    # Real LibreOffice Impress slideshow; skipped (77) without LibreOffice, uno or a display.
    add_test(NAME impress_follow COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/impress_follow_test.py)
    set_tests_properties(impress_follow PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
    if(APPLE)
      add_test(NAME system_helper_probe COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_helper_smoke.py "${CMAKE_CURRENT_BINARY_DIR}/kalipso-system-speech")
    elseif(WIN32)
      add_test(NAME system_helper_probe COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_helper_smoke.py $<TARGET_FILE:kalipso-system-speech>)
    endif()
  endif()

  if(TARGET kalipso-engine AND Python3_Interpreter_FOUND AND DBUS_RUN_SESSION AND GDBUS_EXECUTABLE)
    add_test(NAME engine_dependencies COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/engine_dependencies.py $<TARGET_FILE:kalipso-engine>)
  endif()

  if(TARGET kalipso_colors AND DBUS_RUN_SESSION)
    add_executable(portal_colors_tests tests/portal_colors_tests.cpp)
    target_link_libraries(portal_colors_tests PRIVATE kalipso_colors Qt6::Test)
    target_compile_definitions(portal_colors_tests PRIVATE ENGINE_BINARY="$<TARGET_FILE:kalipso-engine>")
    add_dependencies(portal_colors_tests kalipso-engine)
    add_test(NAME portal_colors COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:portal_colors_tests>)
    # Real PipeWire frames through a simulated consent portal, when the tools exist.
    find_program(GST_LAUNCH gst-launch-1.0)
    find_program(GST_INSPECT gst-inspect-1.0)
    find_program(PIPEWIRE_SERVER pipewire)
    find_program(WIREPLUMBER wireplumber)
    if(GST_INSPECT)
      execute_process(COMMAND ${GST_INSPECT} pipewiresink RESULT_VARIABLE HAVE_PIPEWIRE_SINK OUTPUT_QUIET ERROR_QUIET)
      execute_process(COMMAND ${GST_INSPECT} videotestsrc RESULT_VARIABLE HAVE_VIDEO_TEST_SOURCE OUTPUT_QUIET ERROR_QUIET)
    endif()
    if(GST_LAUNCH AND HAVE_PIPEWIRE_SINK EQUAL 0 AND HAVE_VIDEO_TEST_SOURCE EQUAL 0 AND PIPEWIRE_SERVER
       AND WIREPLUMBER AND Python3_Interpreter_FOUND)
      add_test(NAME portal_colors_stream COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/portal_colors_stream.py $<TARGET_FILE:portal_colors_tests>)
      set_tests_properties(portal_colors_stream PROPERTIES TIMEOUT 60)
    endif()
  endif()
endif()

option(BUILD_RENDER_BENCHMARKS "Build equivalent Widgets/Quick render probes" OFF)
if(BUILD_RENDER_BENCHMARKS)
  find_package(Qt6 REQUIRED COMPONENTS Quick)
  add_executable(render_probe benchmarks/render_probe.cpp)
  target_link_libraries(render_probe PRIVATE Qt6::Widgets Qt6::Quick)
endif()

# --- Packages: cpack -G DEB|RPM|TGZ (Linux), ZIP (Windows), DragNDrop (macOS) -------
set(CPACK_PACKAGE_NAME kalipso)
set(CPACK_PACKAGE_VENDOR "Kalipso contributors")
set(CPACK_PACKAGE_CONTACT "Kalipso contributors <https://github.com/SANSEVIERIA-OU/kalipso/issues>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Teleprompter that follows your voice, on your device")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://sansevieria-ou.github.io/kalipso/")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Qt's SVG image plugin, which the computed dependencies miss: the drawn style (GNOME, any desktop without
# its own Qt style) draws its check marks and arrows from SVG images. Debian 13 ships the plugin apart
# from the library (qt6-svg-plugins); Ubuntu 24.04, with Qt 6.4, has no such package and keeps it in
# libqt6svg6. Fedora's qt6-qtsvg and Arch's qt6-svg include it.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-svg-plugins | libqt6svg6 (<< 6.5~)")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire-bin, xdg-desktop-portal")
# Fedora and openSUSE: rpmbuild finds the libraries the binaries need; PipeWire's tools are named, since
# the microphone is recorded with pw-record.
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_URL "https://sansevieria-ou.github.io/kalipso/")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_SUGGESTS "xdg-desktop-portal")
set(CPACK_RPM_PACKAGE_REQUIRES "/usr/bin/pw-record")
# Directories the system already owns.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/applications /usr/share/dbus-1 /usr/share/dbus-1/services
  /usr/share/icons /usr/share/icons/hicolor /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps
  /usr/share/licenses /usr/share/metainfo /usr/share/plasma /usr/share/plasma/plasmoids /usr/share/gnome-shell
  /usr/share/gnome-shell/extensions /usr/share/kwin /usr/share/kwin/scripts)
set(CPACK_RPM_PACKAGE_DESCRIPTION "A teleprompter that follows your voice with speech recognition on your device, \
scrolls at a steady pace or waits for your foot pedal.")
if(APPLE)
  set(CPACK_GENERATOR DragNDrop)
elseif(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR "DEB;TGZ") # RPM too with -G "DEB;RPM;TGZ" where rpmbuild is installed.
endif()
include(CPack)
