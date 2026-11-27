// H6: the Windows shell. One Win32 window with a WebView2 control that loads app://thunder/index.html (served from the bundled web\ folder next to the exe through a registered custom scheme and
// WebResourceRequested), the page bridges in shell_core.h (window.thunderCore, window.thunderDesktop), audio through the core's live host and miniaudio (WASAPI). UNSIGNED.
// Options: --selftest (run the page's selftest, write the verdict, exit 0 / 1 / 2), --null-audio (no sound device), --web <dir> (serve another web folder), --cdp-port <n> (DevTools Protocol for the UI sweeps), --report <file> (the selftest verdict as a file:
// a GUI-subsystem exe has no console on a CI runner). UNVERIFIED on a PC: this box cannot build it; the Windows runner of .github/workflows/native-desktop.yml is the compile check and runs --selftest.
// H8: single instance (a named mutex; a second launch sends its file to the running window with WM_COPYDATA and exits), the .thunder association (`--register-file-type` / `--unregister-file-type`, per user), the check-only
// updater (WinHTTP). NOT HERE: signing. The WebView2 loader is the DLL (WebView2Loader.dll, copied next to the exe), the Evergreen runtime is on Windows 11 and most Windows 10.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <wrl.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "app_scheme.h"
#include "audio_out.h"
#include "bootstrap_js.h"
#include "file_assoc.h"
#include "launch_args.h"
#include "shell_core.h"
#include "text_util.h"

#ifndef THUNDER_APP_VERSION
#define THUNDER_APP_VERSION "0.0.0"
#endif

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;

namespace {

constexpr UINT WM_FLUSH = WM_APP + 1;
constexpr UINT_PTR TIMER_TICK = 1, TIMER_SELFTEST = 2, TIMER_WATCHDOG = 3;

std::wstring W(const std::string& s) {
  if (s.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}
std::string U(const wchar_t* w) {
  if (!w || !*w) return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}
std::string U(const std::wstring& w) { return U(w.c_str()); }

HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Environment> g_env;
ComPtr<ICoreWebView2Controller> g_ctrl;
ComPtr<ICoreWebView2> g_web;
std::unique_ptr<tpd::ShellCore> g_core;
std::string g_webDir, g_report;
int g_cdpPort = 0;   // --cdp-port N: the DevTools Protocol on 127.0.0.1:N, for the UI sweeps (tests/sweep/ui-lib-shell.mjs)
bool g_selftest = false, g_nullAudio = false, g_selfTestStarted = false, g_devtools = false;
std::string g_pendingOpen, g_openLog;   // g_openLog (--open-log <file>, a test hook): every path handed to the page is appended to this file
constexpr wchar_t kClassName[] = L"ThunderPlusNative";
constexpr wchar_t kMutexName[] = L"Local\\ThunderPlusNative.SingleInstance";

// A file the OS or a second launch asked us to open (H8): S-1 allows exactly that path (DesktopApi::openPath), the page gets it when it is ready.
void openFromOs(const std::string& p) {
  if (!g_openLog.empty()) { FILE* f = _wfopen(W(g_openLog).c_str(), L"ab"); if (f) { const std::string l = p + "\n"; fwrite(l.data(), 1, l.size(), f); fclose(f); } }
  if (g_core) g_core->api().openPath(p); else g_pendingOpen = p;
}
void bringForward(HWND h) { if (IsIconic(h)) ShowWindow(h, SW_RESTORE); SetForegroundWindow(h); }

void finishSelfTest(bool ok, const std::string& rep) {
  const std::string text = std::string("SELFTEST ") + (ok ? "PASS" : "FAIL") + "\n" + rep;
  if (!g_report.empty()) { FILE* f = _wfopen(W(g_report).c_str(), L"wb"); if (f) { fwrite(text.data(), 1, text.size(), f); fclose(f); } }
  if (AttachConsole(ATTACH_PARENT_PROCESS)) { FILE* o; freopen_s(&o, "CONOUT$", "w", stdout); std::fputs(text.c_str(), stdout); std::fflush(stdout); }
  ExitProcess(ok ? 0 : 1);
}

class WinPlatform : public tpd::IPlatform {
 public:
  std::string pickOpenFile(const std::string& startDir) override {
    if (g_selftest) return "";
    std::wstring buf(32768, L'\0'), dir = W(startDir);
    OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.hwndOwner = g_hwnd; o.lpstrFile = &buf[0]; o.nMaxFile = (DWORD)buf.size();
    o.lpstrFilter = L"Thunder Plus project (*.thunder, *.json)\0*.thunder;*.json\0All files\0*.*\0"; o.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&o) ? U(buf.c_str()) : std::string();
  }
  std::string pickSaveFile(const std::string& dir, const std::string& name, const std::string& filterName, const std::string& ext) override {
    if (g_selftest) return "";
    std::wstring buf = W(name); buf.resize(32768, L'\0');
    std::wstring d = W(dir), e = W(ext), filter = W(filterName) + L" (*." + e + L")";
    filter.push_back(L'\0'); filter += L"*." + e; filter.push_back(L'\0'); filter += L"All files"; filter.push_back(L'\0'); filter += L"*.*"; filter.push_back(L'\0');
    OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.hwndOwner = g_hwnd; o.lpstrFile = &buf[0]; o.nMaxFile = (DWORD)buf.size();
    o.lpstrFilter = filter.c_str(); o.lpstrInitialDir = d.empty() ? nullptr : d.c_str(); o.lpstrDefExt = e.c_str();
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetSaveFileNameW(&o) ? U(buf.c_str()) : std::string();
  }
  void showInFolder(const std::string& path) override {
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(W(path).c_str());
    if (pidl) { SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0); ILFree(pidl); }
  }
  void reloadPage() override { if (g_web) g_web->Reload(); }
  void setPlaying(bool on) override { SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS); }   // no sleep while the song plays
  bool naturalScroll() override { return false; }
  void closeWindow(bool) override { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }   // asked from inside a handler: closed after it returns; DesktopApi lets this close through
  std::string knownDir(const std::string& which) override {
    PWSTR p = nullptr; std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(which == "music" ? FOLDERID_Music : FOLDERID_Documents, 0, nullptr, &p))) { r = U(p); CoTaskMemFree(p); }
    return r;
  }
  // ---- H8: the check-only updater (update_check.h). WinHTTP: HTTPS only, redirects followed, short timeouts, at most 4 MiB.
  bool canHttp() override { return !g_selftest; }
  bool httpGet(const std::string& url, std::string* body) override {
    const std::wstring wu = W(url);
    URL_COMPONENTSW uc{}; uc.dwStructSize = sizeof uc; uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wu.c_str(), (DWORD)wu.size(), 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength), path(uc.lpszUrlPath, uc.dwUrlPathLength + uc.dwExtraInfoLength);
    bool ok = false;
    HINTERNET ses = WinHttpOpen(L"thunder-plus-desktop", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = WinHttpOpen(L"thunder-plus-desktop", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = nullptr, req = nullptr;
    if (ses) {
      WinHttpSetTimeouts(ses, 5000, 5000, 6000, 8000);
      con = WinHttpConnect(ses, host.c_str(), uc.nPort, 0);
      if (con) req = WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }
    if (req && WinHttpSendRequest(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
      DWORD code = 0, len = sizeof code;
      if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX) && code == 200) {
        std::string out; DWORD avail = 0; ok = true;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
          if (out.size() + avail > ((size_t)4 << 20)) { ok = false; break; }
          std::string chunk(avail, '\0'); DWORD got = 0;
          if (!WinHttpReadData(req, &chunk[0], avail, &got) || got == 0) { ok = false; break; }
          out.append(chunk, 0, got);
        }
        if (ok) *body = std::move(out);
      }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return ok;
  }
  void openUrl(const std::string& url) override { if (url.rfind(tpd::kReleasesPrefix, 0) == 0) ShellExecuteW(nullptr, L"open", W(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
  void notify(const std::string& title, const std::string& message) override { MessageBoxW(nullptr, W(message).c_str(), W(title).c_str(), MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND); }
};
WinPlatform g_plat;

void openExternal(const std::wstring& url) { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
bool isHttp(const std::wstring& u) { return u.rfind(L"http://", 0) == 0 || u.rfind(L"https://", 0) == 0; }

void resizeWeb() { if (g_ctrl) { RECT r; GetClientRect(g_hwnd, &r); g_ctrl->put_Bounds(r); } }

void navigate() { if (g_web) g_web->Navigate(L"app://thunder/index.html"); }

// app://thunder/* from the bundle: the whole file in a memory stream, with the headers app_scheme.h gives
void onResourceRequested(ICoreWebView2WebResourceRequestedEventArgs* args) {
  ComPtr<ICoreWebView2WebResourceRequest> req; args->get_Request(&req);
  LPWSTR uri = nullptr; req->get_Uri(&uri);
  const std::string url = U(uri); CoTaskMemFree(uri);
  tpd::AppResponse r = tpd::resolveAppUrl(url, g_webDir);
  std::string body; std::wstring headers;
  const bool ok = r.status == 200 && tpd::readWholeFile(r.file, &body);
  if (ok) for (auto& kv : r.headers) headers += W(kv.first) + L": " + W(kv.second) + L"\r\n";
  else { r.status = 404; headers = L"Content-Type: text/plain\r\n"; }
  ComPtr<IStream> stream; stream.Attach(SHCreateMemStream(body.empty() ? nullptr : (const BYTE*)body.data(), (UINT)body.size()));
  ComPtr<ICoreWebView2WebResourceResponse> resp;
  if (g_env && SUCCEEDED(g_env->CreateWebResourceResponse(stream.Get(), r.status, ok ? L"OK" : L"Not Found", headers.c_str(), &resp))) args->put_Response(resp.Get());
}

void onControllerReady() {
  g_ctrl->get_CoreWebView2(&g_web);
  ComPtr<ICoreWebView2Settings> st; g_web->get_Settings(&st);
  // (L-3) DevTools only on request: --devtools or --cdp-port
  if (st) { st->put_IsStatusBarEnabled(FALSE); st->put_AreDevToolsEnabled(g_devtools || g_cdpPort > 0 ? TRUE : FALSE); st->put_IsZoomControlEnabled(FALSE); }
  resizeWeb();
  EventRegistrationToken tok;
  g_web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
    // S-2: only the app's own page talks to the shell: a message from any other origin (a frame, a page the window was somehow sent to) is dropped
    LPWSTR src = nullptr; a->get_Source(&src); const std::string from = U(src); CoTaskMemFree(src);
    if (!tpd::isAppOrigin(from)) return S_OK;
    try {
      LPWSTR s = nullptr;
      if (SUCCEEDED(a->TryGetWebMessageAsString(&s)) && s) { const std::string m = U(s); CoTaskMemFree(s); if (g_core) g_core->onPageMessage(m); }
    } catch (...) {}   // R-2: nothing escapes into WebView2
    return S_OK;
  }).Get(), &tok);
  g_web->AddWebResourceRequestedFilter(L"app://thunder/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
  g_web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* a) -> HRESULT { try { onResourceRequested(a); } catch (...) {} return S_OK; }).Get(), &tok);
  // the window only ever shows the app: https links go to the browser, anything else is refused
  g_web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
    LPWSTR u = nullptr; a->get_Uri(&u); const std::wstring url = u ? u : L""; CoTaskMemFree(u);
    // S-4: the window only ever shows app://thunder (a data: or blob: page would still get the bridge); http(s) links open in the browser, anything else is refused
    if (tpd::isAppOrigin(U(url))) { if (g_core) g_core->api().onLoadStart(); return S_OK; }
    if (isHttp(url)) openExternal(url);
    a->put_Cancel(TRUE);
    return S_OK;
  }).Get(), &tok);
  // S-2: a frame inside the page may load the app or about:blank only (the bridge is injected into the main frame only, see bootstrapScript)
  g_web->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
    LPWSTR u = nullptr; a->get_Uri(&u); const std::wstring url = u ? u : L""; CoTaskMemFree(u);
    if (!tpd::isAppOrigin(U(url)) && url.rfind(L"about:", 0) != 0) a->put_Cancel(TRUE);
    return S_OK;
  }).Get(), &tok);
  g_web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
    LPWSTR u = nullptr; a->get_Uri(&u); const std::wstring url = u ? u : L""; CoTaskMemFree(u);
    if (isHttp(url)) openExternal(url);
    a->put_Handled(TRUE);
    return S_OK;
  }).Get(), &tok);
  g_web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
    if (!g_pendingOpen.empty() && g_core) { const std::string p = g_pendingOpen; g_pendingOpen.clear(); openFromOs(p); }
    if (g_selftest && !g_selfTestStarted) { g_selfTestStarted = true; SetTimer(g_hwnd, TIMER_SELFTEST, 3000, nullptr); }   // after the page has had a moment to run its own start-up
    return S_OK;
  }).Get(), &tok);
  g_web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT { if (g_core) g_core->api().onLoadStart(); if (g_web) g_web->Reload(); return S_OK; }).Get(), &tok);
  // the bootstrap goes in first; the page loads when it is in
  const std::string boot = g_core->bootstrapScript((const char*)kBootstrapJs, (size_t)kBootstrapJs_len);
  g_web->AddScriptToExecuteOnDocumentCreated(W(boot).c_str(), Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>([](HRESULT, LPCWSTR) -> HRESULT { navigate(); return S_OK; }).Get());
}

void createWebView() {
  auto opts = Make<CoreWebView2EnvironmentOptions>();
  // what Electron's switches did (electron/main.js): audio without a first click; no throttling of a window in the back, so the sequencer keeps feeding on time
  std::wstring browserArgs = L"--autoplay-policy=no-user-gesture-required --disable-renderer-backgrounding --disable-background-timer-throttling --disable-backgrounding-occluded-windows";
  if (g_cdpPort > 0) browserArgs += L" --remote-debugging-port=" + std::to_wstring(g_cdpPort) + L" --remote-allow-origins=*";
  opts->put_AdditionalBrowserArguments(browserArgs.c_str());
  ComPtr<ICoreWebView2EnvironmentOptions4> o4;
  if (SUCCEEDED(opts.As(&o4))) {
    const WCHAR* allowed[1] = {L"app://thunder"};   // S-2: only the app's own origin may request app:// resources from another origin
    auto reg = Make<CoreWebView2CustomSchemeRegistration>(L"app");
    reg->put_TreatAsSecure(TRUE);
    reg->put_HasAuthorityComponent(TRUE);
    reg->SetAllowedOrigins(1, allowed);
    ICoreWebView2CustomSchemeRegistration* regs[1] = {reg.Get()};
    o4->SetCustomSchemeRegistrations(1, regs);
  }
  PWSTR local = nullptr; std::wstring dataDir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) { dataDir = std::wstring(local) + L"\\Thunder Plus Native\\WebView2"; CoTaskMemFree(local); }
  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.empty() ? nullptr : dataDir.c_str(), opts.Get(),
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([](HRESULT r, ICoreWebView2Environment* env) -> HRESULT {
      if (FAILED(r) || !env) { if (g_selftest) finishSelfTest(false, "WebView2 environment failed: " + std::to_string((long)r) + "\n"); MessageBoxW(g_hwnd, L"WebView2 could not start. Install the Microsoft Edge WebView2 Runtime.", L"Thunder Plus", MB_ICONERROR); return r; }
      g_env = env;
      env->CreateCoreWebView2Controller(g_hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([](HRESULT r2, ICoreWebView2Controller* c) -> HRESULT {
        if (FAILED(r2) || !c) { if (g_selftest) finishSelfTest(false, "WebView2 controller failed: " + std::to_string((long)r2) + "\n"); return r2; }
        g_ctrl = c; onControllerReady(); return S_OK;
      }).Get());
      return S_OK;
    }).Get());
  if (FAILED(hr)) { if (g_selftest) finishSelfTest(false, "CreateCoreWebView2EnvironmentWithOptions failed: " + std::to_string((long)hr) + "\n"); MessageBoxW(g_hwnd, L"WebView2 could not start. Install the Microsoft Edge WebView2 Runtime.", L"Thunder Plus", MB_ICONERROR); }
}

// ---- H8: the .thunder file association, per user (HKCU\\Software\\Classes): `Thunder Plus.exe --register-file-type` / `--unregister-file-type`. An installer runs the first one (or writes the same keys, see file_assoc.h).
bool setReg(const tpd::RegValue& v) {
  HKEY k = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, W(v.key).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
  const std::wstring val = W(v.value), name = W(v.name);
  const LSTATUS r = RegSetValueExW(k, v.name.empty() ? nullptr : name.c_str(), 0, REG_SZ, (const BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(k);
  return r == ERROR_SUCCESS;
}
std::wstring selfPath() { wchar_t exe[MAX_PATH * 2]; const DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0])); return std::wstring(exe, n); }
int registerFileType(bool on) {
  bool ok = true;
  if (on) {
    const auto entries = tpd::fileAssocEntries(U(selfPath()));
    if (entries.empty()) return 1;
    for (const auto& v : entries) ok = setReg(v) && ok;
  } else {
    // our ProgID tree always goes; the extension key only when it still points at us (another app may own .thunder by now)
    const std::wstring cls = L"Software\\Classes\\", prog = cls + W(tpd::kProgId);
    RegDeleteTreeW(HKEY_CURRENT_USER, prog.c_str());
    wchar_t cur[256] = L""; DWORD len = sizeof cur;
    const std::wstring ext = cls + L".thunder";
    if (RegGetValueW(HKEY_CURRENT_USER, ext.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, cur, &len) == ERROR_SUCCESS && W(tpd::kProgId) == cur) RegDeleteTreeW(HKEY_CURRENT_USER, ext.c_str());
    else RegDeleteKeyValueW(HKEY_CURRENT_USER, (ext + L"\\OpenWithProgids").c_str(), W(tpd::kProgId).c_str());
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return ok ? 0 : 1;
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_SIZE: resizeWeb(); return 0;
    case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = 380; mm->ptMinTrackSize.y = 500; return 0; }
    case WM_FLUSH: if (g_core) g_core->flush(); return 0;
    case WM_COPYDATA: {   // H8: a second launch hands over its file (single instance). Only our own message (magic + "TPD1\n" + an existing .thunder / .json path) is accepted.
      const COPYDATASTRUCT* cds = (const COPYDATASTRUCT*)l;
      std::string p;
      if (cds && cds->dwData == tpd::kHandoffMagic && tpd::handoffDecode((const char*)cds->lpData, cds->cbData, &p)) { bringForward(h); if (!p.empty()) openFromOs(p); return TRUE; }
      return FALSE;
    }
    case WM_TIMER:
      if (w == TIMER_TICK) { if (g_core) g_core->tick(); }
      else if (w == TIMER_SELFTEST) { KillTimer(h, TIMER_SELFTEST); if (g_web) g_web->ExecuteScript(W(tpd::ShellCore::selfTestCall()).c_str(), nullptr); }
      else if (w == TIMER_WATCHDOG) finishSelfTest(false, "no result in 120 s\n"), ExitProcess(2);
      return 0;
    case WM_CLOSE:
      if (g_core && !g_core->api().requestClose(false)) return 0;   // the page was asked (close-ask); it answers, or 3 s pass
      DestroyWindow(h); return 0;
    case WM_DESTROY:
      KillTimer(h, TIMER_TICK);
      // L-2: the window is gone first; the worker may be inside a long song load: wait for it at most 3 s, then leave the process (the objects it uses are never destroyed under it)
      if (g_core && !g_core->shutdown(3000)) ExitProcess(0);
      PostQuitMessage(0); return 0;
    case WM_POWERBROADCAST: if (w == PBT_APMRESUMEAUTOMATIC && g_core) g_core->api().push("thunder:power", "\"resume\""); return TRUE;   // DEVICE RECOVERY: the computer woke
  }
  return DefWindowProcW(h, m, w, l);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool regType = false, unregType = false, anyInstance = false; std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    const std::wstring a = argv[i];
    if (a == L"--selftest") g_selftest = true;
    else if (a == L"--null-audio") g_nullAudio = true;
    else if (a == L"--web" && i + 1 < argc) g_webDir = U(argv[++i]);
    else if (a == L"--report" && i + 1 < argc) g_report = U(argv[++i]);
    else if (a == L"--devtools") g_devtools = true;
    else if (a == L"--cdp-port" && i + 1 < argc) g_cdpPort = _wtoi(argv[++i]);
    else if (a == L"--open-log" && i + 1 < argc) g_openLog = U(argv[++i]);   // test hook: log every path handed to the page
    else if (a == L"--register-file-type") regType = true;
    else if (a == L"--unregister-file-type") unregType = true;
    else if (a == L"--allow-multiple") anyInstance = true;
    else positional.push_back(U(a));
  }
  if (regType || unregType) {   // H8: the association (no window)
    const int rc = registerFileType(regType);
    if (AttachConsole(ATTACH_PARENT_PROCESS)) { FILE* o; freopen_s(&o, "CONOUT$", "w", stdout); std::fputs(rc == 0 ? (regType ? "file type registered\n" : "file type removed\n") : "file type change failed\n", stdout); std::fflush(stdout); }
    return rc;
  }
  g_pendingOpen = tpd::projectFromArgs(positional);   // a double-clicked .thunder file arrives as an argument (the association's "%1")
  // H8: ONE COPY AT A TIME (Electron: requestSingleInstanceLock): a named mutex per login session; a second launch hands its file to the running window (WM_COPYDATA) and leaves. Not for --selftest / --cdp-port / --allow-multiple (CI and dev runs).
  static HANDLE s_mutex = nullptr;
  if (!g_selftest && g_cdpPort == 0 && !anyInstance) {
    s_mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (s_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
      const std::string msg = tpd::handoffEncode(g_pendingOpen);
      for (int i = 0; i < 60; i++) {   // the first copy may still be opening its window: wait up to 6 s for it
        HWND other = FindWindowW(kClassName, nullptr);
        if (other) {
          AllowSetForegroundWindow(ASFW_ANY);
          COPYDATASTRUCT cds{}; cds.dwData = tpd::kHandoffMagic; cds.cbData = (DWORD)msg.size(); cds.lpData = (PVOID)msg.data();
          DWORD_PTR res = 0;
          if (SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 5000, &res) && res) return 0;
        }
        Sleep(100);
      }
      // no window answered (the first copy is stuck or closing): carry on as a copy of our own rather than do nothing
    }
  }
  if (g_webDir.empty()) { wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); PathRemoveFileSpecW(exe); g_webDir = U(std::wstring(exe) + L"\\web"); }

  WNDCLASSEXW wc{}; wc.cbSize = sizeof wc; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(RGB(0xf0, 0xe6, 0xf4)); wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Thunder Plus", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1400, 900, nullptr, nullptr, inst, nullptr);

  tpd::ShellCore::Options o; o.nullAudio = g_nullAudio; o.version = THUNDER_APP_VERSION; o.audio = [] { return tpd::makeMiniaudioOut(); };
  o.requestFlush = [] { PostMessageW(g_hwnd, WM_FLUSH, 0, 0); };
  o.deliver = [](const std::string& text) { if (g_web) g_web->PostWebMessageAsString(W(text).c_str()); };
  o.selfTest = g_selftest;   // S-3: the page's "S:" verdict ends the process only under --selftest
  o.onSelfTest = [](bool ok, const std::string& rep) { finishSelfTest(ok, rep); };
  o.autoUpdateCheck = !g_selftest && !g_nullAudio && std::string(THUNDER_APP_VERSION).find('-') == std::string::npos;   // H8: a release build looks for a newer release by itself; dev builds (0.0.0-native.N) and CI runs do not (the Update menu item still works)
  { PWSTR local = nullptr; if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) { o.dataDir = U(std::wstring(local) + L"\\Thunder Plus Native\\data"); CoTaskMemFree(local); } }   // recovery copy, picked-paths list
  g_core.reset(new tpd::ShellCore(&g_plat, o));

  ShowWindow(g_hwnd, show == SW_HIDE ? SW_SHOWNORMAL : show);
  SetTimer(g_hwnd, TIMER_TICK, 100, nullptr);
  if (g_selftest) SetTimer(g_hwnd, TIMER_WATCHDOG, 120000, nullptr);
  createWebView();
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  g_core.reset();
  // L-2: release the WebView2 objects while COM is still up (their static destructors would run after CoUninitialize)
  if (g_ctrl) g_ctrl->Close();
  g_web.Reset(); g_ctrl.Reset(); g_env.Reset();
  CoUninitialize();
  return (int)msg.wParam;
}
