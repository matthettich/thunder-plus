package app.thunderplus.nativetest

import android.app.Activity
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.webkit.WebView
import androidx.webkit.JavaScriptReplyProxy
import androidx.webkit.WebMessageCompat
import androidx.webkit.WebMessagePortCompat
import androidx.webkit.WebViewCompat
import androidx.webkit.WebViewFeature
import org.json.JSONObject
import java.io.File
import java.io.RandomAccessFile

/**
 * A4: `window.thunderCore` for the page (CORE-BRIDGE section 2). The shim (res/raw/thunder_bridge.js) is injected at
 * document start. send / on / ask go over a WebMessagePort pair (strings, JSON). put has two candidate paths:
 *   (a) ArrayBuffer messages on the same port (needs WEB_MESSAGE_ARRAY_BUFFER), a header string then the bytes;
 *   (b) base64 chunks through addWebMessageListener ("thunderFile"), appended to a file in cacheDir, which is then read
 *       into the core in 1 MB pieces.
 * N0 test code: JSON strings, no versioning.
 */
class Bridge(private val activity: Activity, private val webView: WebView) {
    private val main = Handler(Looper.getMainLooper())
    private val origin = Uri.parse("https://appassets.androidplatform.net")
    private var port: WebMessagePortCompat? = null
    private var header: JSONObject? = null
    private var putStartNs = 0L
    private var file: RandomAccessFile? = null
    private var fileStartNs = 0L
    private val forward: (String) -> Unit = { s -> sendToPage(s) }

    val arrayBufferSupported: Boolean get() = WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_ARRAY_BUFFER)

    /** Before the first page load. */
    fun install() {
        val rules = setOf("https://appassets.androidplatform.net")
        if (WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT)) {
            val js = activity.resources.openRawResource(R.raw.thunder_bridge).bufferedReader().use { it.readText() }
            WebViewCompat.addDocumentStartJavaScript(webView, js, rules)
        }
        if (WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
            WebViewCompat.addWebMessageListener(webView, "thunderFile", rules) { _, message, _, _, reply ->
                onFileMessage(message.data ?: "", reply)
            }
        }
        ThunderHost.listeners.add(forward)
    }

    fun close() { ThunderHost.listeners.remove(forward); port?.close(); port = null }

    /** After each page load: a fresh channel; port 1 goes to the page. */
    fun attach() {
        if (!WebViewFeature.isFeatureSupported(WebViewFeature.CREATE_WEB_MESSAGE_CHANNEL) ||
            !WebViewFeature.isFeatureSupported(WebViewFeature.POST_WEB_MESSAGE)) return
        port?.close()
        SongFiles.pageAttached()
        val ports = WebViewCompat.createWebMessageChannel(webView)
        ports[0].setWebMessageCallback(object : WebMessagePortCompat.WebMessageCallbackCompat() {
            override fun onMessage(port: WebMessagePortCompat, message: WebMessageCompat?) {
                if (message != null) onPageMessage(message)
            }
        })
        WebViewCompat.postWebMessage(webView, WebMessageCompat("thunder-port", arrayOf(ports[1])), origin)
        port = ports[0]
        val wv = WebViewCompat.getCurrentWebViewPackage(activity)
        val caps = JSONObject().put("t", "caps").put("arrayBuffer", arrayBufferSupported)
            .put("model", Build.MANUFACTURER + " " + Build.MODEL).put("android", Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")")
            .put("webview", (wv?.packageName ?: "?") + " " + (wv?.versionName ?: "?"))
            .put("running", ThunderHost.isRunning)
        sendToPage(caps.toString())
    }

    private fun sendToPage(s: String) {
        main.post { try { port?.postMessage(WebMessageCompat(s)) } catch (e: Exception) { /* page gone */ } }
    }

    private fun onPageMessage(m: WebMessageCompat) {
        if (m.type == WebMessageCompat.TYPE_ARRAY_BUFFER) { if (LiveBridge.liveSession) LiveBridge.onBytes(m.arrayBuffer) else onBytes(m.arrayBuffer); return }
        val s = m.data ?: return
        // N2 H3: songs and samples (file.*) work in every mode: the live host, the N0 test core, no engine at all
        if (BridgeTranslator.typeOf(s)?.startsWith("file.") == true) { SongFiles.handle(activity, s); return }
        // N2 H1b: a page that said hello {live:true} talks to the LIVE host (LiveBridge, BridgeTranslator.kt); the N0 test core's messages below are unchanged for every other page.
        if (BridgeTranslator.typeOf(s) == "hello" && s.length < 1024 && (try { JSONObject(s).optBoolean("live", false) } catch (e: Exception) { false })) { LiveBridge.handle(activity, s); return }
        if (LiveBridge.liveSession) { LiveBridge.handle(activity, s); return }   // F16: also while the live engine is down: an error, never the N0 test core
        val j = try { JSONObject(s) } catch (e: Exception) { return }
        when (j.optString("t")) {
            "hello" -> { LiveBridge.endSession(); sendToPage(JSONObject().put("t", "reply").put("id", j.optInt("id")).put("v", 1).put("core", "n0-test")
                .put("caps", JSONObject().put("nativeAudio", true).put("midi", false).put("link", false).put("plugins", false)
                    .put("inputs", false).put("render", false).put("files", true)).toString()) }
            "put.header" -> { header = j; if (j.optInt("o") == 0) putStartNs = System.nanoTime() }
            "info" -> sendToPage(JSONObject().put("t", "reply").put("id", j.optInt("id")).put("info", NativeEngine.nativeInfo()).toString())
            else -> ThunderHost.command(activity, s)
        }
    }

    // ---- path (a): header string, then an ArrayBuffer (a chunk of floats) -------------------------------------------
    private fun onBytes(bytes: ByteArray) {
        val h = header ?: return
        header = null
        val id = h.optInt("id"); val o = h.optLong("o"); val total = h.optLong("n")
        val ok = NativeEngine.nativePutBytes(id, "{\"o\":$o,\"n\":$total}", bytes, 0, bytes.size)
        if (h.optBoolean("last")) {
            val info = NativeEngine.nativePutInfo(id)
            val ms = (System.nanoTime() - putStartNs) / 1e6
            sendToPage(JSONObject().put("t", "put.done").put("pid", h.optInt("pid")).put("ok", ok && info != null).put("info", info ?: "")
                .put("nativeMs", ms).toString())
        } else if (!ok) {
            sendToPage(JSONObject().put("t", "put.done").put("pid", h.optInt("pid")).put("ok", false).put("info", "chunk rejected").toString())
        }
    }

    // ---- path (b): "pid|id|offsetBytes|totalBytes|last|base64" strings through addWebMessageListener -----------------
    private fun onFileMessage(msg: String, reply: JavaScriptReplyProxy) {
        val parts = msg.split('|', limit = 6)
        if (parts.size < 6) return
        val pid = parts[0].toInt(); val id = parts[1].toInt(); val off = parts[2].toLong(); val total = parts[3].toLong()
        val last = parts[4] == "1"
        val data = Base64.decode(parts[5], Base64.DEFAULT)
        if (off == 0L) {
            file?.close()
            fileStartNs = System.nanoTime()
            file = RandomAccessFile(File(activity.cacheDir, "put-$id.f32"), "rw").also { it.setLength(0) }
        }
        val f = file ?: return
        f.seek(off); f.write(data)
        if (!last) return
        val writeMs = (System.nanoTime() - fileStartNs) / 1e6
        f.close(); file = null
        // the core "reads the file": 1 MB at a time into thunder_put
        val t1 = System.nanoTime()
        var ok = true
        RandomAccessFile(File(activity.cacheDir, "put-$id.f32"), "r").use { r ->
            val buf = ByteArray(1 shl 20)
            var pos = 0L
            while (pos < total && ok) {
                r.seek(pos)
                val n = r.read(buf, 0, minOf(buf.size.toLong(), total - pos).toInt())
                if (n <= 0) { ok = false; break }
                ok = NativeEngine.nativePutBytes(id, "{\"o\":${pos / 4},\"n\":${total / 4}}", buf, 0, n - (n and 3))
                pos += n
            }
        }
        val coreMs = (System.nanoTime() - t1) / 1e6
        val info = NativeEngine.nativePutInfo(id)
        reply.postMessage(JSONObject().put("t", "file.done").put("pid", pid).put("ok", ok && info != null).put("info", info ?: "")
            .put("writeMs", writeMs).put("coreMs", coreMs).toString())
    }
}
