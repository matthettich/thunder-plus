// H6: the Windows shell. One Win32 window with a WebView2 control that loads app://thunder/index.html (served from the bundled web\ folder next to the exe through a registered custom scheme and
// WebResourceRequested), the page bridges in shell_core.h (window.thunderCore, window.thunderDesktop), audio through the core's live host and miniaudio (WASAPI). UNSIGNED.
// Options: --selftest (run the page's selftest, write the verdict, exit 0 / 1 / 2), --null-audio (no sound device), --web <dir> (serve another web folder), --report <file> (the selftest verdict as a file:
// a GUI-subsystem exe has no console on a CI runner). UNVERIFIED on a PC: this box cannot build it; the Windows runner of .github/workflows/native-desktop.yml is the compile check and runs --selftest.
// NOT HERE (H8): single instance, .thunder association, updater, signing. The WebView2 loader is the DLL (WebView2Loader.dll, copied next to the exe), the Evergreen runtime is on Windows 11 and most Windows 10.
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
#include <wrl.h>

#include <cstdio>
#include <memory>
#include <string>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "app_scheme.h"
#include "audio_out.h"
#include "bootstrap_js.h"
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
bool g_selftest = false, g_nullAudio = false, g_selfTestStarted = false;
std::string g_pendingOpen;

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
  if (st) { st->put_IsStatusBarEnabled(FALSE); st->put_AreDevToolsEnabled(TRUE); st->put_IsZoomControlEnabled(FALSE); }
  resizeWeb();
  EventRegistrationToken tok;
  g_web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
    LPWSTR s = nullptr;
    if (SUCCEEDED(a->TryGetWebMessageAsString(&s)) && s) { const std::string m = U(s); CoTaskMemFree(s); if (g_core) g_core->onPageMessage(m); }
    return S_OK;
  }).Get(), &tok);
  g_web->AddWebResourceRequestedFilter(L"app://thunder/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
  g_web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* a) -> HRESULT { onResourceRequested(a); return S_OK; }).Get(), &tok);
  // the window only ever shows the app: https links go to the browser, anything else is refused
  g_web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
    LPWSTR u = nullptr; a->get_Uri(&u); const std::wstring url = u ? u : L""; CoTaskMemFree(u);
    if (url.rfind(L"app://", 0) == 0 || url.rfind(L"about:", 0) == 0 || url.rfind(L"blob:", 0) == 0 || url.rfind(L"data:", 0) == 0) { if (g_core) g_core->api().onLoadStart(); return S_OK; }
    if (isHttp(url)) openExternal(url);
    a->put_Cancel(TRUE);
    return S_OK;
  }).Get(), &tok);
  g_web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
    LPWSTR u = nullptr; a->get_Uri(&u); const std::wstring url = u ? u : L""; CoTaskMemFree(u);
    if (isHttp(url)) openExternal(url);
    a->put_Handled(TRUE);
    return S_OK;
  }).Get(), &tok);
  g_web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
    if (!g_pendingOpen.empty() && g_core) { g_core->api().openPath(g_pendingOpen); g_pendingOpen.clear(); }
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
  opts->put_AdditionalBrowserArguments(L"--autoplay-policy=no-user-gesture-required --disable-renderer-backgrounding --disable-background-timer-throttling --disable-backgrounding-occluded-windows");
  ComPtr<ICoreWebView2EnvironmentOptions4> o4;
  if (SUCCEEDED(opts.As(&o4))) {
    const WCHAR* allowed[1] = {L"*"};
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

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_SIZE: resizeWeb(); return 0;
    case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = 380; mm->ptMinTrackSize.y = 500; return 0; }
    case WM_FLUSH: if (g_core) g_core->flush(); return 0;
    case WM_TIMER:
      if (w == TIMER_TICK) { if (g_core) g_core->tick(); }
      else if (w == TIMER_SELFTEST) { KillTimer(h, TIMER_SELFTEST); if (g_web) g_web->ExecuteScript(W(tpd::ShellCore::selfTestCall()).c_str(), nullptr); }
      else if (w == TIMER_WATCHDOG) finishSelfTest(false, "no result in 120 s\n"), ExitProcess(2);
      return 0;
    case WM_CLOSE:
      if (g_core && !g_core->api().requestClose(false)) return 0;   // the page was asked (close-ask); it answers, or 3 s pass
      DestroyWindow(h); return 0;
    case WM_DESTROY: KillTimer(h, TIMER_TICK); if (g_core) g_core->shutdown(); PostQuitMessage(0); return 0;
    case WM_POWERBROADCAST: if (w == PBT_APMRESUMEAUTOMATIC && g_core) g_core->api().push("thunder:power", "\"resume\""); return TRUE;   // DEVICE RECOVERY: the computer woke
  }
  return DefWindowProcW(h, m, w, l);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; i++) {
    const std::wstring a = argv[i];
    if (a == L"--selftest") g_selftest = true;
    else if (a == L"--null-audio") g_nullAudio = true;
    else if (a == L"--web" && i + 1 < argc) g_webDir = U(argv[++i]);
    else if (a == L"--report" && i + 1 < argc) g_report = U(argv[++i]);
    else if (a.size() > 8 && a.compare(a.size() - 8, 8, L".thunder") == 0) g_pendingOpen = U(a);
  }
  if (g_webDir.empty()) { wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); PathRemoveFileSpecW(exe); g_webDir = U(std::wstring(exe) + L"\\web"); }

  WNDCLASSEXW wc{}; wc.cbSize = sizeof wc; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(RGB(0xf0, 0xe6, 0xf4)); wc.lpszClassName = L"ThunderPlusNative";
  RegisterClassExW(&wc);
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Thunder Plus", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1400, 900, nullptr, nullptr, inst, nullptr);

  tpd::ShellCore::Options o; o.nullAudio = g_nullAudio; o.version = THUNDER_APP_VERSION; o.audio = [] { return tpd::makeMiniaudioOut(); };
  o.requestFlush = [] { PostMessageW(g_hwnd, WM_FLUSH, 0, 0); };
  o.deliver = [](const std::string& text) { if (g_web) g_web->PostWebMessageAsString(W(text).c_str()); };
  o.onSelfTest = [](bool ok, const std::string& rep) { finishSelfTest(ok, rep); };
  g_core.reset(new tpd::ShellCore(&g_plat, o));

  ShowWindow(g_hwnd, show == SW_HIDE ? SW_SHOWNORMAL : show);
  SetTimer(g_hwnd, TIMER_TICK, 100, nullptr);
  if (g_selftest) SetTimer(g_hwnd, TIMER_WATCHDOG, 120000, nullptr);
  createWebView();
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  g_core.reset();
  CoUninitialize();
  return (int)msg.wParam;
}
