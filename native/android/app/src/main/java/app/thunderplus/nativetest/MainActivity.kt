package app.thunderplus.nativetest

import android.annotation.SuppressLint
import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
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
    private lateinit var status: TextView
    private val ui = Handler(Looper.getMainLooper())
    private val base = "https://appassets.androidplatform.net/"

    private val refresh = object : Runnable {
        override fun run() {
            val running = ThunderHost.isRunning
            val head = if (running) "RUNNING  sr ${ThunderHost.sampleRate}  ${NativeEngine.nativeInfo()}" else "stopped"
            status.text = head + "\n" + ThunderHost.clock.lastTen()
            ui.postDelayed(this, 500)
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT >= 33) requestPermissions(arrayOf("android.permission.POST_NOTIFICATIONS"), 1)
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) WebView.setWebContentsDebuggingEnabled(true)

        val root = FrameLayout(this)
        webView = WebView(this)
        root.addView(webView, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        root.addView(buildOverlay(), FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM))
        setContentView(root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val b = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
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
        webView.loadUrl(base + "index.html")
    }

    private fun btn(label: String, onClick: () -> Unit) = Button(this).apply {
        text = label; isAllCaps = false; setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f); setPadding(12, 0, 12, 0); minHeight = 0; minimumHeight = 0
        setOnClickListener { onClick() }
        layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
    }

    private fun buildOverlay(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(Color.argb(210, 0, 0, 0)); setPadding(8, 4, 8, 4) }
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(btn("Play") { if (!ThunderHost.command(this, "{\"t\":\"play\"}")) Toast.makeText(this, "Audio did not start", Toast.LENGTH_LONG).show() })
        row.addView(btn("Stop") { ThunderHost.command(this, "{\"t\":\"stop\"}") })
        row.addView(btn("Copy") { copyLog() })
        row.addView(btn("Bridge test") { webView.loadUrl(base + "bridge-test.html") })
        row.addView(btn("App") { webView.loadUrl(base + "index.html") })
        status = TextView(this).apply { setTextColor(Color.WHITE); typeface = Typeface.MONOSPACE; setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f); text = "stopped" }
        row.addView(btn("Hide") { status.visibility = if (status.visibility == View.VISIBLE) View.GONE else View.VISIBLE })
        col.addView(row)
        col.addView(status)
        return col
    }

    private fun copyLog() {
        val wv = WebViewCompat.getCurrentWebViewPackage(this)
        val text = "Thunder Plus native N0 clock log\n" +
            "device: ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT}), WebView ${wv?.versionName}\n" +
            "audio: sr ${ThunderHost.sampleRate}  ${NativeEngine.nativeInfo()}\n" +
            "ratio = audio seconds / wall seconds, cumulative from the first second. PASS = 1.000 +-0.001 and 'after 1s' = 0.\n" +
            ThunderHost.clock.everything()
        (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("clock log", text))
        Toast.makeText(this, "Copied " + ThunderHost.clock.everything().lines().size + " lines", Toast.LENGTH_SHORT).show()
    }

    override fun onResume() { super.onResume(); ui.post(refresh) }
    override fun onPause() { ui.removeCallbacks(refresh); super.onPause() }
    override fun onDestroy() { bridge.close(); webView.destroy(); super.onDestroy() }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() { if (webView.canGoBack()) webView.goBack() else super.onBackPressed() }
}
