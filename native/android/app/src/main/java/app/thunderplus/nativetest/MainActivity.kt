package app.thunderplus.nativetest

import android.annotation.SuppressLint
import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.res.Configuration
import android.content.pm.ApplicationInfo
import android.graphics.Color
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.webkit.WebViewAssetLoader
import androidx.webkit.WebViewCompat

/** One activity: the real index.html in a WebView, and a native overlay with Play / Stop / Copy and the clock log. */
class MainActivity : Activity() {
    private lateinit var webView: WebView
    private lateinit var bridge: Bridge
    private val ui = Handler(Looper.getMainLooper())
    private val base = "https://appassets.androidplatform.net/"

    private lateinit var verdict: TextView
    private lateinit var readout: TextView
    private lateinit var runLines: TextView
    private lateinit var resultsBox: TextView
    private lateinit var midiLine: TextView   // U36 T9: debug MIDI out
    private lateinit var root: FrameLayout
    private lateinit var liveLine: TextView   // N2 H1a: the live engine's status
    private val green = Color.rgb(40, 200, 90)
    private val red = Color.rgb(235, 60, 60)

    private fun mmss(s: Double): String { val t = s.toInt(); return String.format(java.util.Locale.US, "%d:%02d", t / 60, t % 60) }

    private val refresh = object : Runnable {
        override fun run() {
            val running = ThunderHost.isRunning
            val rt = Rt.now()
            val active = ThunderHost.activeRun
            val done = ThunderHost.lastFinished?.let { ThunderHost.results[it] }
            // the big line: a finished run's verdict stays until the next run or RESET
            if (active != null) {
                val live = !rt.pending && rt.elapsedS >= 2
                verdict.setTextColor(if (!live) Color.WHITE else if (rt.pass) green else red)
                verdict.text = "RUNNING $active  ${mmss(if (rt.pending) 0.0 else rt.elapsedS)} / ${mmss(ThunderHost.RUN_SECONDS.toDouble())}" + if (live) (if (rt.pass) "  (so far OK)" else "  (so far FAIL)") else ""
            } else if (done != null) {
                verdict.setTextColor(if (done.pass) green else red)
                verdict.text = "${done.label}: ${if (done.pass) "PASS" else "FAIL"}"
            } else {
                verdict.setTextColor(Color.WHITE)
                verdict.text = if (running) "PLAYING (free run)" else "ready"
            }
            val shown = done?.numbers ?: if (!rt.pending && rt.frames > 0) rt.numbers() else "-"
            readout.text = shown + "\ncross-check (core clock events): " + (done?.crossCheck ?: ThunderHost.clock.lastLine()) +
                (if (running) "\naudio: sr ${ThunderHost.sampleRate}  ${NativeEngine.nativeInfo()}" else "")
            runLines.text = ThunderHost.RUN_LABELS.joinToString("\n") { l ->
                val r = ThunderHost.results[l]
                "$l: " + (if (r == null) (if (l == active) "running" else "not run yet") else (if (r.pass) "PASS" else "FAIL"))
            }
            liveLine.text = LiveEngine.statusText()
            if (midiLine.visibility == View.VISIBLE) midiLine.text = MidiPorts.get(this@MainActivity).statusText()
            ui.postDelayed(this, 500)
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT >= 33) requestPermissions(arrayOf("android.permission.POST_NOTIFICATIONS"), 1)
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) WebView.setWebContentsDebuggingEnabled(true)

        MidiPorts.get(this).start()   // U36 T9: hot-plug callback + first scan
        root = FrameLayout(this)
        webView = WebView(this)
        root.addView(webView, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        root.addView(buildOverlay(), FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM))
        androidx.core.view.WindowCompat.setDecorFitsSystemWindows(window, false)   // the insets above place the content (already so on Android 15)
        setContentView(root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            // the keyboard too (N2 H5): with it open the page's window shrinks above it instead of being covered; a fold / unfold changes the bars and the cutout the same way
            val b = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.ime())
            v.setPadding(b.left, b.top, b.right, b.bottom)
            WindowInsetsCompat.CONSUMED
        }

        webView.settings.javaScriptEnabled = true
        webView.settings.domStorageEnabled = true
        webView.settings.mediaPlaybackRequiresUserGesture = false
        webView.settings.allowFileAccess = false
        val loader = WebViewAssetLoader.Builder().setDomain("appassets.androidplatform.net").addPathHandler("/", MimeAssets(this)).build()
        bridge = Bridge(this, webView)
        webView.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? = loader.shouldInterceptRequest(request.url)
            override fun onPageFinished(view: WebView, url: String) { bridge.attach() }
        }
        bridge.install()
        SongFiles.attach(this)            // N2 H3: the folder pickers answer to this activity
        SongFiles.onIntent(this, intent)  // a project opened from a file manager (VIEW / SEND)
        if (Build.VERSION.SDK_INT >= 33) registerBack()
        webView.loadUrl(base + "index.html")
        if (UpdateCheck.checkOnStart(this)) runUpdateCheck(false)   // only when the owner switched it on
    }

    /** The "new build" check. [verbose]: say "up to date" too (the button); the start-up check speaks only when there IS a new build. A new build: a dialog that opens the release page in the browser. */
    private fun runUpdateCheck(verbose: Boolean) {
        UpdateCheck.check { v, text ->
            ui.post {
                if (isFinishing || isDestroyed) return@post   // H4-7: no dialog or toast on a finished activity (BadTokenException)
                if (v is ReleaseTags.Verdict.NewBuild) {
                    android.app.AlertDialog.Builder(this).setTitle("New build").setMessage(text + "\nSongs and settings stay when you install it over this one.")
                        .setPositiveButton("Open download page") { _, _ -> try { startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse(ReleaseTags.safePage(v.release.pageUrl)))) } catch (e: Exception) { Toast.makeText(this, "No browser found", Toast.LENGTH_LONG).show() } }
                        .setNegativeButton("Later", null).show()
                } else if (verbose) Toast.makeText(this, text, Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun btn(label: String, onClick: (View) -> Unit) = Button(this).apply {
        text = label; isAllCaps = false; setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f); setPadding(12, 0, 12, 0); minHeight = 0; minimumHeight = 0
        setOnClickListener { onClick(it) }
        layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
    }

    /** ANR fix (25 Nov): starting the N0 test core waits for the live engine's shutdown, which can sit behind a project load: [work] runs on a thread, [done] on the main thread. */
    private fun <T> off(work: () -> T, done: (T) -> Unit) {
        Thread({ val r = try { work() } catch (e: Exception) { null }; if (r != null) runOnUiThread { done(r) } }, "thunder-overlay-work").start()
    }

    private fun buildOverlay(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(Color.argb(235, 0, 0, 0)); setPadding(8, 4, 8, 4) }
        fun row(vararg v: View) = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; v.forEach { addView(it) } }
        val runRow = row(*ThunderHost.RUN_LABELS.map { l ->
            btn("$l 5 min") { off({ ThunderHost.startRun(this, l) }) { ok -> if (!ok) Toast.makeText(this, "Audio did not start", Toast.LENGTH_LONG).show() } }
        }.toTypedArray())
        val ctlRow = row(
            btn("RESET") { ThunderHost.resetRun(this); resultsBox.text = "" },
            btn("COPY RESULTS") { copyResults() },
            btn("Bridge test") { webView.loadUrl(base + "bridge-test.html") },
            btn("App") { webView.loadUrl(base + "index.html") },
            btn("Hide") { val v = if (readout.visibility == View.VISIBLE) View.GONE else View.VISIBLE; readout.visibility = v; runLines.visibility = v; midiLine.visibility = v; resultsBox.visibility = if (v == View.GONE) View.GONE else resultsBox.visibility })
        // U36 T9 DEBUG (no live song host yet): MIDI out test = two bars of notes + MIDI clock to port A through the real scheduler / pump / sink; "MIDI A: next" picks the device.
        val midiRow = row(
            btn("MIDI out test") { off({ MidiPorts.get(this).runTest(this) }) { midiLine.text = it } },
            btn("MIDI A: next device") { midiLine.text = MidiPorts.get(this).cycleA() },
            btn("COPY MIDI") {
                val t = MidiPorts.get(this).statusText()
                try { (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("MIDI out", t)); Toast.makeText(this, "MIDI status copied", Toast.LENGTH_SHORT).show() }
                catch (e: Exception) { Toast.makeText(this, "The clipboard refused", Toast.LENGTH_LONG).show() }
            })
        // N2 H1a: the LIVE engine (thunder_live_*): the bundled ELECTRO, its pads rendered by the core, played through the foreground service. The N0 runs above are untouched.
        val liveRow = row(
            btn("Play bundled song natively (test path)") { playBundledNatively() },
            btn("Stop (native)") { LiveEngine.async { LiveEngine.stop() } },
            btn("A5 gate 10 min (native)") { startNativeGate() },
            btn("Quit native") { LiveEngine.async { LiveEngine.quit(applicationContext) } })
        // N2 H4: the owner's "new build" check (UpdateCheck.kt): ONE request, only on this button, or at start-up when the owner switched it on here (default off)
        val updRow = row(
            btn("Check for new build") { runUpdateCheck(true) },
            btn(if (UpdateCheck.checkOnStart(this)) "Check at start: ON" else "Check at start: off") { v ->
                val on = !UpdateCheck.checkOnStart(this); UpdateCheck.setCheckOnStart(this, on); (v as Button).text = if (on) "Check at start: ON" else "Check at start: off" })
        liveLine = TextView(this).apply { setTextColor(Color.rgb(255, 200, 120)); typeface = Typeface.MONOSPACE; setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f); text = "native live engine: off" }
        midiLine = TextView(this).apply { setTextColor(Color.rgb(120, 220, 255)); typeface = Typeface.MONOSPACE; setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f); text = "MIDI out: -" }
        verdict = TextView(this).apply { setTextColor(Color.WHITE); setTextSize(TypedValue.COMPLEX_UNIT_SP, 26f); typeface = Typeface.DEFAULT_BOLD; text = "ready" }
        readout = TextView(this).apply { setTextColor(Color.WHITE); typeface = Typeface.MONOSPACE; setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f) }
        runLines = TextView(this).apply { setTextColor(Color.LTGRAY); typeface = Typeface.MONOSPACE; setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f) }
        resultsBox = TextView(this).apply { setTextColor(Color.YELLOW); typeface = Typeface.MONOSPACE; setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f); setTextIsSelectable(true); visibility = View.GONE }
        col.addView(verdict); col.addView(readout); col.addView(runLines); col.addView(runRow); col.addView(ctlRow); col.addView(midiRow); col.addView(midiLine); col.addView(liveRow); col.addView(liveLine); col.addView(updRow)
        col.addView(android.widget.ScrollView(this).apply { addView(resultsBox); layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, (120 * resources.displayMetrics.density).toInt()) })
        return col
    }

    /** N2 H1a: start the live engine, render ELECTRO's pads with the core, publish, play. All on a worker thread (the pad render takes seconds). */
    private fun playBundledNatively() {
        val started = LiveEngine.async {
            val r = LiveEngine.loadAsset(applicationContext, LiveEngine.BUNDLED_SONG)
            val rc = if (r.ok) LiveEngine.play() else r.rc
            ui.post { Toast.makeText(this, if (r.ok && rc == 0) r.summary() else "native play failed: " + r.summary() + " play rc $rc", Toast.LENGTH_LONG).show() }
        }
        if (!started) Toast.makeText(this, "The native engine is busy loading", Toast.LENGTH_SHORT).show()
    }

    /** N2 H1c: the A5 timing gate on the live callback: WORKPLAN H1 "1.00 +- 0.1 % for 10 minutes". Loads the bundled song when nothing is loaded, plays it (again when it ends), judges after 600 s of AUDIO frames. */
    private fun startNativeGate() {
        val started = LiveEngine.async {
            if (LiveEngine.loaded == null) { val r = LiveEngine.loadAsset(applicationContext, LiveEngine.BUNDLED_SONG); if (!r.ok) { ui.post { Toast.makeText(this, "gate: " + r.summary(), Toast.LENGTH_LONG).show() }; return@async } }
            val rc = LiveEngine.startGate()
            ui.post { Toast.makeText(this, if (rc == 0) "A5 gate started: 10 minutes, any screen state" else "gate did not start (rc $rc)", Toast.LENGTH_LONG).show() }
        }
        if (!started) Toast.makeText(this, "The native engine is busy loading", Toast.LENGTH_SHORT).show()
    }

    /** All three runs as text: on the clipboard, and in the selectable box under the buttons in case the clipboard is refused. */
    private fun copyResults() {
        val wv = WebViewCompat.getCurrentWebViewPackage(this)
        val sb = StringBuilder("Thunder Plus native N0: clock test results\n")
        sb.append("device: ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT}), WebView ${wv?.versionName}\n")
        sb.append("audio: sr ${ThunderHost.sampleRate}  ${NativeEngine.nativeInfo()}\n")
        sb.append("PASS = ratio within +-1000 ppm (1.0000 +- 0.001 s per second), 0 underruns, 0 late callbacks; the first second of each run is left out.\n")
        for (l in ThunderHost.RUN_LABELS) {
            val r = ThunderHost.results[l]
            sb.append("\n$l 5 min: ").append(if (r == null) "NOT RUN" else if (r.pass) "PASS" else "FAIL").append('\n')
            if (r != null) sb.append("  ").append(r.numbers).append("\n  cross-check (core clock events): ").append(r.crossCheck).append('\n')
        }
        sb.append("\nnative live engine (H1c): ").append(LiveEngine.gate.line()).append('\n')
        val text = sb.toString()
        resultsBox.text = text; resultsBox.visibility = View.VISIBLE
        try {
            (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("N0 results", text))
            Toast.makeText(this, "Copied. The same text is in the yellow box below.", Toast.LENGTH_LONG).show()
        } catch (e: Exception) {
            Toast.makeText(this, "The clipboard refused: select the yellow text and copy it by hand.", Toast.LENGTH_LONG).show()
        }
    }

    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); setIntent(intent); SongFiles.onIntent(this, intent) }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        SongFiles.onResult(applicationContext, requestCode, resultCode, data)
    }

    override fun onResume() { super.onResume(); ui.post(refresh) }
    override fun onPause() { ui.removeCallbacks(refresh); super.onPause() }
    override fun onDestroy() {
        if (Build.VERSION.SDK_INT >= 33) unregisterBack()
        bridge.close(); webView.destroy(); super.onDestroy()   // the engine and the service are NOT touched: a song keeps playing when the screen is closed
    }

    /** N2 H5: fold / unfold, rotation, split screen, a keyboard attached, dark mode, font size: the Activity is NOT re-created (android:configChanges), the same WebView just resizes, and nothing here touches the engine. */
    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        ViewCompat.requestApplyInsets(root)   // new bars / cutout / hinge area: place the content again
        ui.post(refresh)
    }

    /** N2 H5: Back (the gesture, the button, predictive back): the WebView's history first, otherwise the app goes to the background and KEEPS PLAYING; it never finishes the activity (ShellPolicy.back). */
    private var backCb: Any? = null   // an android.window.OnBackInvokedCallback (API 33): held as Any so that Android 10 to 12 never load the class
    @android.annotation.TargetApi(33) private fun registerBack() {
        val cb = android.window.OnBackInvokedCallback { handleBack() }; backCb = cb
        onBackInvokedDispatcher.registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, cb)
    }
    @android.annotation.TargetApi(33) private fun unregisterBack() { (backCb as? android.window.OnBackInvokedCallback)?.let { onBackInvokedDispatcher.unregisterOnBackInvokedCallback(it) }; backCb = null }
    private fun handleBack() { when (ShellPolicy.back(webView.canGoBack())) { ShellPolicy.Back.WEB_BACK -> webView.goBack(); ShellPolicy.Back.TO_BACKGROUND -> moveTaskToBack(true) } }
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() { handleBack() }   // Android 12 and older (and 13+ when the dispatcher is not used)
}
