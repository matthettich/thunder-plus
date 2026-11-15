package app.thunderplus.nativetest

import android.content.Context
import android.content.res.AssetManager
import android.webkit.WebResourceResponse
import androidx.webkit.WebViewAssetLoader
import java.io.ByteArrayInputStream
import java.io.IOException

/** Serves the APK's assets with explicit content types (.wasm must be application/wasm for streaming compile). */
class MimeAssets(ctx: Context) : WebViewAssetLoader.PathHandler {
    private val assets: AssetManager = ctx.applicationContext.assets

    override fun handle(path: String): WebResourceResponse? {
        var p = path.trimStart('/')
        if (p.isEmpty()) p = "index.html"
        return try {
            val ext = p.substringAfterLast('.', "").lowercase()
            val mime = MIME[ext] ?: "application/octet-stream"
            val enc = if (mime.startsWith("text/") || mime.endsWith("json") || mime.endsWith("svg+xml")) "utf-8" else null
            WebResourceResponse(mime, enc, assets.open(p, AssetManager.ACCESS_STREAMING))
        } catch (e: IOException) {
            WebResourceResponse("text/plain", "utf-8", 404, "Not Found", emptyMap(), ByteArrayInputStream(ByteArray(0)))
        }
    }

    companion object {
        private val MIME = mapOf(
            "html" to "text/html", "js" to "text/javascript", "mjs" to "text/javascript", "css" to "text/css",
            "json" to "application/json", "thunder" to "application/json", "wasm" to "application/wasm",
            "webmanifest" to "application/manifest+json", "png" to "image/png", "jpg" to "image/jpeg", "jpeg" to "image/jpeg",
            "svg" to "image/svg+xml", "ico" to "image/x-icon", "woff" to "font/woff", "woff2" to "font/woff2", "ttf" to "font/ttf",
            "otf" to "font/otf", "wav" to "audio/wav", "txt" to "text/plain",
        )
    }
}
