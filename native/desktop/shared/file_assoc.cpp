#include "file_assoc.h"

namespace tpd {

std::vector<RegValue> fileAssocEntries(const std::string& exe) {
  if (exe.empty() || exe.find('"') != std::string::npos) return {};
  const std::string cls = "Software\\Classes\\", prog = cls + kProgId;
  return {
    {cls + ".thunder", "", kProgId},
    {cls + ".thunder", "Content Type", kProjectMime},
    {cls + ".thunder\\OpenWithProgids", kProgId, ""},
    {prog, "", "Thunder Plus project"},
    {prog + "\\DefaultIcon", "", "\"" + exe + "\",0"},
    {prog + "\\shell\\open\\command", "", "\"" + exe + "\" \"%1\""},
  };
}

std::vector<std::string> fileAssocKeysToRemove() {
  const std::string cls = "Software\\Classes\\", prog = cls + kProgId;
  return {prog + "\\shell\\open\\command", prog + "\\shell\\open", prog + "\\shell", prog + "\\DefaultIcon", prog, cls + ".thunder\\OpenWithProgids", cls + ".thunder"};
}

}  // namespace tpd
