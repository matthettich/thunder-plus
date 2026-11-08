# thunder_io.node (see NOTICE.md). Built by io/build.js with node-gyp. Plain N-API: built once against Node's headers,
# it loads in Electron too. Vendored, single-file or header-only libraries, no system SDK: miniaudio (audio) and
# libremidi (MIDI, header-only, phase B: src/midi.cc). C++20: libremidi needs it.
{
  'targets': [{
    'target_name': 'thunder_io',
    'sources': ['src/thunder_io.cc', 'src/midi.cc'],
    'include_dirs': ['vendor/miniaudio', 'vendor/libremidi/include'],
    'cflags_cc': ['-std=c++20', '-fexceptions'], 'cflags_cc!': ['-fno-exceptions'],   # (libremidi needs exceptions; midi.cc catches them all)
    'defines': ['NAPI_DISABLE_CPP_EXCEPTIONS', 'MA_NO_DECODING', 'MA_NO_ENCODING', 'MA_NO_GENERATION',
                'MA_NO_RESOURCE_MANAGER', 'MA_NO_NODE_GRAPH', 'MA_NO_ENGINE'],
    'conditions': [
      ['OS=="mac"', {
        'xcode_settings': { 'CLANG_CXX_LANGUAGE_STANDARD': 'c++20', 'GCC_ENABLE_CPP_EXCEPTIONS': 'YES', 'MACOSX_DEPLOYMENT_TARGET': '11.0' },
        'libraries': ['-framework CoreAudio', '-framework AudioToolbox', '-framework CoreFoundation', '-framework CoreMIDI']
      }],
      ['OS=="linux"', {
        # miniaudio loads ALSA and PulseAudio at run time with dlopen, so nothing else is linked
        'libraries': ['-lpthread', '-ldl', '-lm']
      }],
      ['OS=="win"', {
        'defines': ['NOMINMAX', 'WIN32_LEAN_AND_MEAN'],
        'libraries': ['ole32.lib', 'winmm.lib'],
        'msvs_settings': { 'VCCLCompilerTool': { 'ExceptionHandling': 1, 'AdditionalOptions': ['/std:c++20'] } }
      }]
    ]
  }]
}
