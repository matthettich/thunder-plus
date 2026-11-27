// H8: the Windows registry entries that make `.thunder` open in the native app (the same ones electron-builder's NSIS installer writes for the Electron app: extension, a ProgID, the open command, an icon).
// Per-user (HKEY_CURRENT_USER\Software\Classes), so no administrator rights. The shell applies them with `--register-file-type` and removes them with `--unregister-file-type`; an installer runs the first
// (or writes the same keys under HKLM\Software\Classes). The list is plain data so it is tested on every platform; only the registry calls are Windows-only (win/main_win.cpp).
#ifndef TPD_FILE_ASSOC_H
#define TPD_FILE_ASSOC_H

#include <string>
#include <vector>

namespace tpd {

struct RegValue {
  std::string key;     // under HKEY_CURRENT_USER, backslash separated
  std::string name;    // "" = the key's (Default) value
  std::string value;
};
constexpr const char* kProgId = "ThunderPlus.Project";
constexpr const char* kProjectMime = "application/x-thunder-plus";   // as electron/package.json's fileAssociations
// Every value to set for an app at exePath (UTF-8, absolute). Empty when exePath is empty or contains a double quote (it goes inside quotes in the open command).
std::vector<RegValue> fileAssocEntries(const std::string& exePath);
// The keys to delete again (children before parents).
std::vector<std::string> fileAssocKeysToRemove();

}  // namespace tpd
#endif
