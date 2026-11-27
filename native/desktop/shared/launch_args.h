// H8: how a file to open reaches the app: a command-line argument (a double-click on a .thunder file, a second launch), an OS "open file" event (Mac), or the second-instance hand-off (Windows WM_COPYDATA).
// Platform-neutral and tested on Linux (tests/test_launch_args.cpp). The Electron rules it ports (electron/main.js): `argPath` = the first argument that names an EXISTING `.thunder` file; an OS open-file
// event passes any path on (the page's readProj then refuses what is not a project). Here the shell refuses anything that is not an existing .thunder / .json file before it reaches DesktopApi::openPath,
// and S-1 then allows exactly that path (nothing else the page names).
#ifndef TPD_LAUNCH_ARGS_H
#define TPD_LAUNCH_ARGS_H

#include <string>
#include <vector>

namespace tpd {

// The first of `args` (argv WITHOUT argv[0]) that ends in .thunder (any case) and names an existing regular file. A relative path is made absolute against `cwd` ("" = the process's own). "" when none.
std::string projectFromArgs(const std::vector<std::string>& args, const std::string& cwd = "");
// A path from an OS open-file event or a hand-off: it must end in .thunder or .json and be an existing regular file. Returns the absolute path, "" when it is neither.
std::string openablePath(const std::string& path, const std::string& cwd = "");

// The second-instance hand-off message (Windows WM_COPYDATA payload): "TPD1\n" + the absolute UTF-8 path, or just "TPD1\n" = "only bring the window forward". At most kHandoffMax bytes.
constexpr size_t kHandoffMax = 32 * 1024;
constexpr unsigned long kHandoffMagic = 0x54504431UL;   // 'TPD1', the COPYDATASTRUCT dwData
std::string handoffEncode(const std::string& path);
// false: not ours (wrong magic text, too long, a NUL inside, or a path that is not openable). On true *path is the openable absolute path, or "" for "only bring the window forward".
bool handoffDecode(const char* data, size_t size, std::string* path);

}  // namespace tpd
#endif
