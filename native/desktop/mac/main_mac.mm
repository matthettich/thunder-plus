// H6: the Mac shell. A minimal Cocoa app: one NSWindow with a WKWebView that loads app://thunder/index.html (served by a WKURLSchemeHandler from the bundled web/ folder), the page bridges in
// shell_core.h (window.thunderCore, window.thunderDesktop), audio through the core's live host and miniaudio (Core Audio). UNSIGNED (the owner has no Apple Developer membership; H8 signs).
// Options: --selftest (run the page's selftest, print the verdict, exit 0 / 1), --null-audio (no sound device), --web <dir> (a web folder to serve instead of the bundled one).
// NOT HERE (H8): the real Mac menu, .thunder file association (application:openFile: is wired), single instance, updater. A minimal menu (Quit, Edit, Window) is here so the keys work.
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "app_scheme.h"
#include "audio_out.h"
#include "bootstrap_js.h"
#include "shell_core.h"
#include "text_util.h"

#ifndef THUNDER_APP_VERSION
#define THUNDER_APP_VERSION "0.0.0"
#endif

static std::string S(NSString* s) { return s ? std::string([s UTF8String]) : std::string(); }
static NSString* N(const std::string& s) { NSString* r = [NSString stringWithUTF8String:s.c_str()]; return r ? r : @""; }

@class TPDApp;

// ---- app:// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
@interface TPDScheme : NSObject <WKURLSchemeHandler>
@property(nonatomic, copy) NSString* root;
@end
@implementation TPDScheme
- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
  const std::string url = S(task.request.URL.absoluteString);
  tpd::AppResponse r = tpd::resolveAppUrl(url, S(self.root));
  NSMutableDictionary* h = [NSMutableDictionary dictionary];
  std::string body;
  if (r.status == 200 && tpd::readWholeFile(r.file, &body)) { for (auto& kv : r.headers) h[N(kv.first)] = N(kv.second); h[@"Content-Length"] = [NSString stringWithFormat:@"%lu", (unsigned long)body.size()]; }
  else { r.status = 404; body.clear(); h[@"Content-Type"] = @"text/plain"; h[@"Content-Length"] = @"0"; }
  NSHTTPURLResponse* resp = [[NSHTTPURLResponse alloc] initWithURL:task.request.URL statusCode:r.status HTTPVersion:@"HTTP/1.1" headerFields:h];
  [task didReceiveResponse:resp];
  if (!body.empty()) [task didReceiveData:[NSData dataWithBytes:body.data() length:body.size()]];
  [task didFinish];
}
- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {}
@end

// ---- the platform: dialogs, window, sleep guard -------------------------------------------------------------------------------------------------------------------------------------------
class MacPlatform : public tpd::IPlatform {
 public:
  bool selftest = false;
  __weak NSWindow* window = nil; __weak WKWebView* web = nil; id activity = nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  std::string pickOpenFile(const std::string& startDir) override {
    if (selftest) return "";
    NSOpenPanel* p = [NSOpenPanel openPanel]; p.canChooseFiles = YES; p.canChooseDirectories = NO; p.allowsMultipleSelection = NO; p.allowedFileTypes = @[ @"thunder", @"json" ];
    if (!startDir.empty()) p.directoryURL = [NSURL fileURLWithPath:N(startDir)];
    return [p runModal] == NSModalResponseOK && p.URL ? S(p.URL.path) : std::string();
  }
  std::string pickSaveFile(const std::string& dir, const std::string& name, const std::string&, const std::string& ext) override {
    if (selftest) return "";
    NSSavePanel* p = [NSSavePanel savePanel]; p.allowedFileTypes = @[ N(ext) ]; p.nameFieldStringValue = N(name); p.canCreateDirectories = YES;
    if (!dir.empty()) p.directoryURL = [NSURL fileURLWithPath:N(dir)];
    return [p runModal] == NSModalResponseOK && p.URL ? S(p.URL.path) : std::string();
  }
#pragma clang diagnostic pop
  void showInFolder(const std::string& path) override { [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:N(path)] ]]; }
  void reloadPage() override { [web reload]; }
  void setPlaying(bool on) override {   // App Nap guard (the page's `playing`): the song keeps its timers while it plays
    if (on && !activity) activity = [[NSProcessInfo processInfo] beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical) reason:@"Thunder Plus is playing"];
    else if (!on && activity) { [[NSProcessInfo processInfo] endActivity:activity]; activity = nil; }
  }
  bool naturalScroll() override { id v = [[NSUserDefaults standardUserDefaults] objectForKey:@"com.apple.swipescrolldirection"]; return v ? [v boolValue] : true; }
  void closeWindow(bool quit) override { if (quit) [NSApp terminate:nil]; else [window close]; }
  std::string knownDir(const std::string& which) override {
    NSArray* a = NSSearchPathForDirectoriesInDomains(which == "music" ? NSMusicDirectory : NSDocumentDirectory, NSUserDomainMask, YES);
    return a.count ? S(a[0]) : std::string();
  }
};

// ---- the app --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
@interface TPDApp : NSObject <NSApplicationDelegate, NSWindowDelegate, WKScriptMessageHandler, WKNavigationDelegate, WKUIDelegate>
@end
@implementation TPDApp {
  NSWindow* _win; WKWebView* _web; TPDScheme* _scheme; NSTimer* _timer;
  std::unique_ptr<MacPlatform> _plat; std::unique_ptr<tpd::ShellCore> _core;
  bool _selftest, _nullAudio, _selfTestStarted; std::string _webDir; std::string _pendingOpen;
}
- (instancetype)init {
  if ((self = [super init])) {
    NSArray<NSString*>* args = [[NSProcessInfo processInfo] arguments];
    for (NSUInteger i = 1; i < args.count; i++) {
      if ([args[i] isEqualToString:@"--selftest"]) _selftest = true;
      else if ([args[i] isEqualToString:@"--null-audio"]) _nullAudio = true;
      else if ([args[i] isEqualToString:@"--web"] && i + 1 < args.count) _webDir = S(args[++i]);
      else if ([args[i] hasSuffix:@".thunder"]) _pendingOpen = S(args[i]);
    }
    if (_webDir.empty()) _webDir = S([[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"web"]);
  }
  return self;
}
- (void)applicationDidFinishLaunching:(NSNotification*)n {
  _plat.reset(new MacPlatform()); _plat->selftest = _selftest;
  tpd::ShellCore::Options o; o.nullAudio = _nullAudio; o.version = THUNDER_APP_VERSION; o.audio = [] { return tpd::makeMiniaudioOut(); };
  TPDApp* me = self;
  o.requestFlush = [me] { dispatch_async(dispatch_get_main_queue(), ^{ [me flushToPage]; }); };
  o.deliver = [me](const std::string& text) { [me runInPage:"window.__thunderRecv(" + tpd::jsonQuote(text) + ")"]; };
  o.onSelfTest = [me](bool ok, const std::string& rep) { std::printf("SELFTEST %s\n%s", ok ? "PASS" : "FAIL", rep.c_str()); std::fflush(stdout); exit(ok ? 0 : 1); };
  _core.reset(new tpd::ShellCore(_plat.get(), o));

  WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
  _scheme = [[TPDScheme alloc] init]; _scheme.root = N(_webDir);
  [cfg setURLSchemeHandler:_scheme forURLScheme:@"app"];
  cfg.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;   // audio can start without a click first (as the Electron shell's autoplay switch)
  [cfg.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
  const std::string boot = _core->bootstrapScript((const char*)kBootstrapJs, (size_t)kBootstrapJs_len);
  WKUserScript* us = [[WKUserScript alloc] initWithSource:N(boot) injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES];
  [cfg.userContentController addUserScript:us];
  [cfg.userContentController addScriptMessageHandler:self name:@"thunder"];

  NSRect frame = NSMakeRect(0, 0, 1400, 900);
  _win = [[NSWindow alloc] initWithContentRect:frame styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:NO];
  _win.title = @"Thunder Plus"; _win.minSize = NSMakeSize(380, 500); _win.delegate = self; _win.releasedWhenClosed = NO; [_win center];
  _web = [[WKWebView alloc] initWithFrame:frame configuration:cfg];
  _web.navigationDelegate = self; _web.UIDelegate = self; _web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  if (@available(macOS 13.3, *)) _web.inspectable = YES;
  _win.contentView = _web;
  _plat->window = _win; _plat->web = _web;
  [self buildMenu];
  [_web loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"app://thunder/index.html"]]];
  [_win makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  _timer = [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer*) { self->_core->tick(); }];
  if (_selftest) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(120 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ std::printf("SELFTEST FAIL\nno result in 120 s\n"); std::fflush(stdout); exit(2); });
}
- (void)buildMenu {
  NSMenu* bar = [[NSMenu alloc] init];
  NSMenuItem* appItem = [[NSMenuItem alloc] init]; [bar addItem:appItem];
  NSMenu* app = [[NSMenu alloc] init];
  [app addItemWithTitle:@"About Thunder Plus" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
  [app addItem:[NSMenuItem separatorItem]];
  [app addItemWithTitle:@"Hide Thunder Plus" action:@selector(hide:) keyEquivalent:@"h"];
  [app addItem:[NSMenuItem separatorItem]];
  [app addItemWithTitle:@"Quit Thunder Plus" action:@selector(terminate:) keyEquivalent:@"q"];
  appItem.submenu = app;
  NSMenuItem* editItem = [[NSMenuItem alloc] init]; [bar addItem:editItem];
  NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
  [edit addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
  [edit addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"];
  [edit addItem:[NSMenuItem separatorItem]];
  [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
  [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
  [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
  [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
  editItem.submenu = edit;
  NSMenuItem* winItem = [[NSMenuItem alloc] init]; [bar addItem:winItem];
  NSMenu* win = [[NSMenu alloc] initWithTitle:@"Window"];
  [win addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
  winItem.submenu = win; [NSApp setWindowsMenu:win];
  [NSApp setMainMenu:bar];
}
- (void)flushToPage { if (_core) _core->flush(); }
- (void)runInPage:(const std::string&)js { if (_web) [_web evaluateJavaScript:N(js) completionHandler:nil]; }
// ---- page -> shell
- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m {
  if ([m.body isKindOfClass:[NSString class]]) _core->onPageMessage(S((NSString*)m.body));
}
- (void)webView:(WKWebView*)w didStartProvisionalNavigation:(WKNavigation*)nav { _core->api().onLoadStart(); }
- (void)webView:(WKWebView*)w didFinishNavigation:(WKNavigation*)nav {
  if (!_pendingOpen.empty()) { _core->api().openPath(_pendingOpen); _pendingOpen.clear(); }
  if (_selftest && !_selfTestStarted) {
    _selfTestStarted = true;   // after the page has had a moment to run its own start-up
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self runInPage:tpd::ShellCore::selfTestCall()]; });
  }
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)w { _core->api().onLoadStart(); [w reload]; }
- (void)webView:(WKWebView*)w decidePolicyForNavigationAction:(WKNavigationAction*)a decisionHandler:(void (^)(WKNavigationActionPolicy))h {
  NSString* s = a.request.URL.scheme;   // the window only ever shows the app; https links go to the browser
  if ([s isEqualToString:@"app"] || [s isEqualToString:@"about"] || [s isEqualToString:@"blob"] || [s isEqualToString:@"data"]) { h(WKNavigationActionPolicyAllow); return; }
  if ([s isEqualToString:@"http"] || [s isEqualToString:@"https"]) [[NSWorkspace sharedWorkspace] openURL:a.request.URL];
  h(WKNavigationActionPolicyCancel);
}
- (WKWebView*)webView:(WKWebView*)w createWebViewWithConfiguration:(WKWebViewConfiguration*)c forNavigationAction:(WKNavigationAction*)a windowFeatures:(WKWindowFeatures*)f {
  NSString* s = a.request.URL.scheme; if ([s isEqualToString:@"http"] || [s isEqualToString:@"https"]) [[NSWorkspace sharedWorkspace] openURL:a.request.URL];
  return nil;
}
// alert / confirm / prompt and <input type=file>
- (void)webView:(WKWebView*)w runJavaScriptAlertPanelWithMessage:(NSString*)msg initiatedByFrame:(WKFrameInfo*)f completionHandler:(void (^)(void))done {
  NSAlert* al = [[NSAlert alloc] init]; al.messageText = msg; [al runModal]; done();
}
- (void)webView:(WKWebView*)w runJavaScriptConfirmPanelWithMessage:(NSString*)msg initiatedByFrame:(WKFrameInfo*)f completionHandler:(void (^)(BOOL))done {
  NSAlert* al = [[NSAlert alloc] init]; al.messageText = msg; [al addButtonWithTitle:@"OK"]; [al addButtonWithTitle:@"Cancel"]; done([al runModal] == NSAlertFirstButtonReturn);
}
- (void)webView:(WKWebView*)w runJavaScriptTextInputPanelWithPrompt:(NSString*)p defaultText:(NSString*)d initiatedByFrame:(WKFrameInfo*)f completionHandler:(void (^)(NSString*))done {
  NSAlert* al = [[NSAlert alloc] init]; al.messageText = p; [al addButtonWithTitle:@"OK"]; [al addButtonWithTitle:@"Cancel"];
  NSTextField* t = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)]; t.stringValue = d ? d : @""; al.accessoryView = t;
  done([al runModal] == NSAlertFirstButtonReturn ? t.stringValue : nil);
}
- (void)webView:(WKWebView*)w runOpenPanelWithParameters:(WKOpenPanelParameters*)p initiatedByFrame:(WKFrameInfo*)f completionHandler:(void (^)(NSArray<NSURL*>*))done {
  if (_selftest) { done(nil); return; }
  NSOpenPanel* op = [NSOpenPanel openPanel]; op.allowsMultipleSelection = p.allowsMultipleSelection; op.canChooseDirectories = p.allowsDirectories;
  done([op runModal] == NSModalResponseOK ? op.URLs : nil);
}
// ---- window and app lifecycle: the close hand-off (page asked first), open-file
- (BOOL)windowShouldClose:(NSWindow*)s { return _core->api().requestClose(false) ? YES : NO; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)a { return _core->api().requestClose(true) ? NSTerminateNow : NSTerminateCancel; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a { return YES; }
- (BOOL)application:(NSApplication*)a openFile:(NSString*)f { if (_core) _core->api().openPath(S(f)); else _pendingOpen = S(f); return YES; }
- (void)applicationWillTerminate:(NSNotification*)n { if (_core) _core->shutdown(); }
@end

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    TPDApp* d = [[TPDApp alloc] init];
    app.delegate = d;
    [app run];
  }
  return 0;
}
