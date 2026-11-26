# H6: the desktop shells (WORKPLAN N4). Added from native/CMakeLists.txt after the core and the tests.
#   thunder_desktop_shared  the platform-neutral library: the app:// scheme mapping, the thunderCore message translator (a port of the Android BridgeTranslator.kt), the put store, the live host over
#                           the core's live.h (reuses the Android shell's plain-C++ LiveEngine / SongFit / PadFiller / event JSON sources by path), the 31 window.thunderDesktop channels, the audio-out
#                           interface and its null output. Builds and tests on every platform (the Linux CI jobs run the tests).
#   thunder_desktop_win     Windows: WebView2 in a Win32 window (needs the WebView2 SDK: -DTHUNDER_WEBVIEW2_DIR=<the Microsoft.Web.WebView2 NuGet package folder>, pinned in the workflow)
#   ThunderPlus (mac)       macOS: WKWebView in a minimal Cocoa app bundle (Objective-C++), unsigned
# The shells are built only with -DTHUNDER_DESKTOP_SHELL=ON (the native-desktop workflow); the existing core jobs build the library and its tests only.
option(THUNDER_DESKTOP_SHELL "Build the WebView2 (Windows) / WKWebView (Mac) shell executable" OFF)
set(THUNDER_WEBVIEW2_DIR "" CACHE PATH "The extracted Microsoft.Web.WebView2 NuGet package (the folder holding build/native)")
set(THUNDER_APP_VERSION "0.0.0-native" CACHE STRING "The version the shells report to the page (window.thunderDesktop.updates.state().version)")

get_filename_component(TPD_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(TPA_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../android/app/src/main/cpp")   # the Android shell's plain-C++ files (no android.h in them) are compiled in place, not copied

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_js.cmake)
set(TPD_GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${TPD_GEN}")
tpd_embed("${CMAKE_CURRENT_SOURCE_DIR}/js/desktop_bridge.js" "${TPD_GEN}/bootstrap_js.h" kBootstrapJs)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/js/desktop_bridge.js")

find_package(Threads REQUIRED)
add_library(thunder_desktop_shared STATIC
  shared/app_scheme.cpp shared/put_store.cpp shared/bridge_translator.cpp shared/live_host.cpp shared/bridge_session.cpp shared/desktop_api.cpp shared/text_util.cpp shared/audio_null.cpp shared/shell_core.cpp shared/recovery_store.cpp
  ${TPA_CPP}/live_engine.cpp ${TPA_CPP}/live_events.cpp ${TPA_CPP}/songfit.cpp ${TPA_CPP}/pad_filler.cpp)
# UBSan's vptr check cannot work across the -fno-rtti core: the shared_ptr control blocks the core makes (Song, SeqSession) are released in code built WITH rtti (this library, its tests), and the check calls them
# "not a _Sp_counted_base" (a false positive: the Core (Linux, sanitizers) job, desktop_live_host and desktop_session). Off for this library and its tests, as for test_seq_takeover and the other tests that hold a Seq.
if(THUNDER_SANITIZE)
  target_compile_options(thunder_desktop_shared PRIVATE -fno-sanitize=vptr)
endif()
target_include_directories(thunder_desktop_shared PUBLIC shared ${TPA_CPP} ${CMAKE_CURRENT_SOURCE_DIR}/../core/src)
target_link_libraries(thunder_desktop_shared PUBLIC thunder_core Threads::Threads)
target_compile_features(thunder_desktop_shared PUBLIC cxx_std_17)
if(TARGET thunder_lxr)
  target_link_libraries(thunder_desktop_shared PUBLIC thunder_lxr)
endif()
if(MSVC)
  # live_engine.cpp (the Android shell's file) uses clock_gettime: a force-included shim gives MSVC one (QueryPerformanceCounter); the Android file is not edited.
  set_source_files_properties(${TPA_CPP}/live_engine.cpp ${TPA_CPP}/live_events.cpp ${TPA_CPP}/songfit.cpp ${TPA_CPP}/pad_filler.cpp PROPERTIES COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/shared/posix_shim.h")
  target_compile_definitions(thunder_desktop_shared PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  target_compile_options(thunder_desktop_shared PRIVATE /EHsc /utf-8)
endif()

if(THUNDER_BUILD_DESKTOP_TESTS)
  foreach(t app_scheme translator put_store desktop_api live_host session shell_core)
    add_executable(test_desktop_${t} tests/test_${t}.cpp)
    target_link_libraries(test_desktop_${t} PRIVATE thunder_desktop_shared)
    target_compile_definitions(test_desktop_${t} PRIVATE TPD_REPO="${TPD_REPO}")
    if(THUNDER_SANITIZE)
      target_compile_options(test_desktop_${t} PRIVATE -fno-sanitize=vptr)
    endif()
    add_test(NAME desktop_${t} COMMAND test_desktop_${t})
  endforeach()
endif()

# ---- the shells -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
if(THUNDER_DESKTOP_SHELL)
  set(MA_DIR "${TPD_REPO}/io/vendor/miniaudio")
  add_library(thunder_desktop_audio STATIC shared/audio_miniaudio.cpp shared/miniaudio_impl.c)
  target_include_directories(thunder_desktop_audio PUBLIC shared ${MA_DIR})
  target_link_libraries(thunder_desktop_audio PUBLIC thunder_desktop_shared)
  target_compile_definitions(thunder_desktop_audio PUBLIC MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_ENABLE_ONLY_SPECIFIC_BACKENDS)
  if(WIN32)
    target_compile_definitions(thunder_desktop_audio PUBLIC MA_ENABLE_WASAPI)
  elseif(APPLE)
    target_compile_definitions(thunder_desktop_audio PUBLIC MA_ENABLE_COREAUDIO)
    target_link_libraries(thunder_desktop_audio PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
  else()
    target_compile_definitions(thunder_desktop_audio PUBLIC MA_ENABLE_NULL)
  endif()

  # The app's files next to / inside the shell: the build step of docs/port/H6-NOTES.md ("bundled files from the repo root")
  function(tpd_bundle_web target dest_expr)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -DREPO=${TPD_REPO} -DDEST=${dest_expr} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_web.cmake
      COMMENT "Bundling the web app into ${target}")
  endfunction()

  if(WIN32)
    if(NOT THUNDER_WEBVIEW2_DIR OR NOT EXISTS "${THUNDER_WEBVIEW2_DIR}/build/native/include/WebView2.h")
      message(FATAL_ERROR "THUNDER_DESKTOP_SHELL on Windows needs -DTHUNDER_WEBVIEW2_DIR=<Microsoft.Web.WebView2 package folder> (build/native/include/WebView2.h)")
    endif()
    # the loader as a DLL (WebView2Loader.dll + its import library), copied next to the exe: the static loader is built /MT and the core libraries are /MD
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(WV2_ARCH x64)
    else()
      set(WV2_ARCH x86)
    endif()
    set(WV2_LIB "${THUNDER_WEBVIEW2_DIR}/build/native/${WV2_ARCH}/WebView2Loader.dll.lib")
    set(WV2_DLL "${THUNDER_WEBVIEW2_DIR}/build/native/${WV2_ARCH}/WebView2Loader.dll")
    add_executable(thunder_desktop_win WIN32 win/main_win.cpp)
    target_include_directories(thunder_desktop_win PRIVATE "${THUNDER_WEBVIEW2_DIR}/build/native/include" ${TPD_GEN})
    target_link_libraries(thunder_desktop_win PRIVATE thunder_desktop_audio "${WV2_LIB}" user32 ole32 shell32 comdlg32 shlwapi advapi32 oleaut32 runtimeobject)
    target_compile_definitions(thunder_desktop_win PRIVATE THUNDER_APP_VERSION="${THUNDER_APP_VERSION}" UNICODE _UNICODE)
    target_compile_options(thunder_desktop_win PRIVATE /EHsc /utf-8 /W3)
    set_target_properties(thunder_desktop_win PROPERTIES OUTPUT_NAME "Thunder Plus")
    add_custom_command(TARGET thunder_desktop_win POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WV2_DLL}" "$<TARGET_FILE_DIR:thunder_desktop_win>" COMMENT "WebView2Loader.dll")
    tpd_bundle_web(thunder_desktop_win "$<TARGET_FILE_DIR:thunder_desktop_win>/web")
  elseif(APPLE)
    enable_language(OBJCXX)
    add_executable(ThunderPlus MACOSX_BUNDLE mac/main_mac.mm)
    target_include_directories(ThunderPlus PRIVATE ${TPD_GEN})
    target_link_libraries(ThunderPlus PRIVATE thunder_desktop_audio "-framework Cocoa" "-framework WebKit")
    target_compile_definitions(ThunderPlus PRIVATE THUNDER_APP_VERSION="${THUNDER_APP_VERSION}")
    target_compile_options(ThunderPlus PRIVATE -fobjc-arc)
    set_target_properties(ThunderPlus PROPERTIES OUTPUT_NAME "Thunder Plus" MACOSX_BUNDLE_BUNDLE_NAME "Thunder Plus" MACOSX_BUNDLE_GUI_IDENTIFIER "com.matthettich.thunderplus.native"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${THUNDER_APP_VERSION}" MACOSX_BUNDLE_BUNDLE_VERSION "${THUNDER_APP_VERSION}" MACOSX_BUNDLE_INFO_STRING "Thunder Plus (native shell, unsigned)")
    tpd_bundle_web(ThunderPlus "$<TARGET_BUNDLE_CONTENT_DIR:ThunderPlus>/Resources/web")
  endif()
endif()
