# thunder_link.node (GPL-2.0-or-later; see NOTICE.md). Built by link/build.js with node-gyp against the Link SDK in
# link/sdk (fetched by link/fetch-sdk.sh at a pinned tag). Plain N-API: built once against Node's headers, it loads in Electron.
{
  'variables': { 'link%': 'sdk' },
  'targets': [{
    'target_name': 'thunder_link',
    'sources': ['src/thunder_link.cc'],
    'include_dirs': ['<(link)/include', '<(link)/modules/asio-standalone/asio/include'],
    # What Link's cmake (ConfigureAbletonLink / ConfigureAsioStandalone) sets, for a build without cmake
    'defines': ['ASIO_STANDALONE=1', 'ASIO_NO_TYPEID=1', 'ASIO_VERSION_NAMESPACE=link_asio_thunder', 'NAPI_DISABLE_CPP_EXCEPTIONS'],
    'cflags_cc!': ['-fno-exceptions', '-fno-rtti', '-std=gnu++17'],
    'cflags_cc': ['-std=c++17', '-fexceptions'],
    'conditions': [
      ['OS=="mac"', {
        'defines': ['LINK_PLATFORM_MACOSX=1', 'LINK_PLATFORM_UNIX=1'],
        'xcode_settings': { 'GCC_ENABLE_CPP_EXCEPTIONS': 'YES', 'GCC_ENABLE_CPP_RTTI': 'YES', 'CLANG_CXX_LANGUAGE_STANDARD': 'c++17',
                            'MACOSX_DEPLOYMENT_TARGET': '11.0' }
      }],
      ['OS=="linux"', {
        'defines': ['LINK_PLATFORM_LINUX=1', 'LINK_PLATFORM_UNIX=1'],
        'libraries': ['-latomic', '-lpthread']
      }],
      ['OS=="win"', {
        'defines': ['LINK_PLATFORM_WINDOWS=1', 'INCL_EXTRA_HTON_FUNCTIONS=1', '_WIN32_WINNT=0x0601', 'NOMINMAX', 'WIN32_LEAN_AND_MEAN'],
        'libraries': ['ws2_32.lib', 'iphlpapi.lib', 'winmm.lib'],
        'msvs_settings': { 'VCCLCompilerTool': { 'ExceptionHandling': 1, 'RuntimeTypeInfo': 'true', 'AdditionalOptions': ['/std:c++17', '/bigobj'] } }
      }]
    ]
  }]
}
