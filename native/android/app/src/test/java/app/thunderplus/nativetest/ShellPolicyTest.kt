package app.thunderplus.nativetest

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

// N2 H5: the manifest keeps the window promises of ShellPolicy.kt (fold / unfold and the keyboard do not re-create the Activity; Back does not end the app; the screen can rotate and resize).
class ShellPolicyTest {
    private val manifest: String = listOf("src/main/AndroidManifest.xml", "app/src/main/AndroidManifest.xml").map { File(it) }.first { it.exists() }.readText()
    private val activity: String = manifest.substringAfter("<activity").substringBefore("</activity>")
    private fun attr(name: String): String? = Regex("android:$name=\"([^\"]*)\"").find(activity.substringBefore(">"))?.groupValues?.get(1)

    @Test fun theActivityHandlesEveryConfigurationChangeItself() {
        val have = attr("configChanges")!!.split('|').toSet()
        for (t in ShellPolicy.CONFIG_CHANGES) assertTrue("android:configChanges lacks $t (the Activity would be re-created and the WebView reloaded)", t in have)
    }

    @Test fun resizableAndNotOrientationLocked() {
        assertEquals("true", attr("resizeableActivity"))   // a foldable's inner screen, split screen, a desktop window
        assertEquals(null, attr("screenOrientation"))
    }

    @Test fun theKeyboardResizesTheWindowAndBackIsPredictive() {
        assertTrue(attr("windowSoftInputMode")!!.contains("adjustResize"))
        assertEquals("true", attr("enableOnBackInvokedCallback"))
    }

    @Test fun backNeverFinishesTheApp() {
        assertEquals(ShellPolicy.Back.WEB_BACK, ShellPolicy.back(true))
        assertEquals(ShellPolicy.Back.TO_BACKGROUND, ShellPolicy.back(false))
    }

    @Test fun theServiceStaysAMediaPlaybackForegroundService() {
        val svc = manifest.substringAfter("<service").substringBefore("/>")
        assertTrue(svc.contains("foregroundServiceType=\"mediaPlayback\""))
        assertFalse("no WAKE_LOCK permission (WORKPLAN A3: a mediaPlayback foreground service needs none)", Regex("uses-permission[^>]*WAKE_LOCK").containsMatchIn(manifest))
    }
}
