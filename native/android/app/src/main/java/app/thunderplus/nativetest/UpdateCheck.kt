package app.thunderplus.nativetest

import android.content.Context
import org.json.JSONArray
import org.json.JSONException
import java.net.HttpURLConnection
import java.net.URL

/**
 * N2 H4: the in-app "new build" check against GitHub Releases of matthettich/thunder-plus.
 *
 * TAG PATTERN (what CI publishes, .github/workflows/native.yml job `android-release-build`): `native-android-<channel>-<N>`, <channel> one of stable | beta | test, <N> the CI run number, which is
 * also the APK's versionCode. Example: `native-android-beta-412`. A release is "newer" when its channel is THIS build's channel and its N is bigger than this build's BuildConfig.BUILD_NUMBER.
 * (The older N0 test releases `native-n0-android-<N>` do not match and are ignored.)
 *
 * NO NETWORK CALL unless the owner asks: [check] runs only from the menu button, or at start-up when the owner switched [checkOnStart] on (default OFF, kept in this app's own preferences). It makes ONE
 * HTTPS GET to the public releases list, sends no identifier, and installs nothing: the answer offers to OPEN the release page in the browser, where the owner downloads and installs the APK as usual
 * (same application id and signing key: it installs over this build and keeps songs and settings).
 */
object ReleaseTags {
    const val REPO = "matthettich/thunder-plus"
    const val API_URL = "https://api.github.com/repos/$REPO/releases?per_page=50"
    private val TAG = Regex("^native-android-(stable|beta|test)-([0-9]{1,9})$")

    data class Tag(val channel: String, val number: Int)
    data class Release(val tag: Tag, val tagName: String, val pageUrl: String, val apkUrl: String?, val name: String)

    fun parseTag(s: String): Tag? = TAG.matchEntire(s)?.let { Tag(it.groupValues[1], it.groupValues[2].toInt()) }

    /** The newest non-draft release of [channel] in a GitHub `releases` JSON array, or null. Pre-releases count (the CI makes them). */
    fun newest(json: String, channel: String): Release? {
        val a = try { JSONArray(json) } catch (e: JSONException) { return null }
        var best: Release? = null
        for (i in 0 until a.length()) {
            val r = a.optJSONObject(i) ?: continue
            if (r.optBoolean("draft", false)) continue
            val name = r.optString("tag_name", "")
            val tag = parseTag(name) ?: continue
            if (tag.channel != channel) continue
            if (best != null && tag.number <= best.tag.number) continue
            var apk: String? = null
            val assets = r.optJSONArray("assets")
            if (assets != null) for (k in 0 until assets.length()) { val s = assets.optJSONObject(k) ?: continue; if (s.optString("name").endsWith(".apk")) { apk = s.optString("browser_download_url").ifEmpty { null }; break } }
            best = Release(tag, name, r.optString("html_url", ""), apk, r.optString("name", name))
        }
        return best
    }

    sealed class Verdict {
        object UpToDate : Verdict()
        data class NewBuild(val release: Release) : Verdict()
        object NoneFound : Verdict()
    }

    fun verdict(json: String, channel: String, currentNumber: Int): Verdict {
        val r = newest(json, channel) ?: return Verdict.NoneFound
        return if (r.tag.number > currentNumber) Verdict.NewBuild(r) else Verdict.UpToDate
    }

    fun describe(v: Verdict, channel: String, currentNumber: Int): String = when (v) {
        is Verdict.UpToDate -> "This is the newest $channel build (#$currentNumber)."
        is Verdict.NoneFound -> "No $channel build is published yet."
        is Verdict.NewBuild -> "New $channel build #${v.release.tag.number} (this one is #$currentNumber)."
    }
}

object UpdateCheck {
    private const val PREFS = "thunder_update"
    private const val KEY_ON_START = "checkOnStart"

    /** Off until the owner turns it on. */
    fun checkOnStart(ctx: Context): Boolean = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_ON_START, false)
    fun setCheckOnStart(ctx: Context, on: Boolean) { ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_ON_START, on).apply() }

    /** One GET on a worker thread; [done] is called on that thread with the verdict, or with the error text. */
    fun check(done: (ReleaseTags.Verdict?, String) -> Unit) {
        Thread({
            try {
                val c = URL(ReleaseTags.API_URL).openConnection() as HttpURLConnection
                c.connectTimeout = 8000; c.readTimeout = 8000
                c.setRequestProperty("Accept", "application/vnd.github+json"); c.setRequestProperty("User-Agent", "thunder-plus-android")
                val code = c.responseCode
                if (code != 200) { done(null, "GitHub answered $code"); return@Thread }
                val body = c.inputStream.use { String(it.readBytes(), Charsets.UTF_8) }
                val v = ReleaseTags.verdict(body, BuildConfig.CHANNEL, BuildConfig.BUILD_NUMBER)
                done(v, ReleaseTags.describe(v, BuildConfig.CHANNEL, BuildConfig.BUILD_NUMBER))
            } catch (e: Exception) { done(null, "Could not check: ${e.javaClass.simpleName}") }
        }, "thunder-update-check").start()
    }
}
