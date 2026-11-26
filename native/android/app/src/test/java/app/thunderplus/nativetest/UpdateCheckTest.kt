package app.thunderplus.nativetest

import app.thunderplus.nativetest.ReleaseTags.Verdict
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

// N2 H4: the tag pattern and the "is there a new build" decision (UpdateCheck.kt). No network, no Android.
class UpdateCheckTest {
    private fun rel(tag: String, draft: Boolean = false, apk: Boolean = true) =
        "{\"tag_name\":\"$tag\",\"draft\":$draft,\"prerelease\":true,\"name\":\"$tag\",\"html_url\":\"https://github.com/matthettich/thunder-plus/releases/tag/$tag\"," +
        "\"assets\":[" + (if (apk) "{\"name\":\"thunder-plus-$tag.apk\",\"browser_download_url\":\"https://example.invalid/$tag.apk\"}," else "") + "{\"name\":\"mapping.txt\",\"browser_download_url\":\"https://example.invalid/m\"}]}"
    private fun list(vararg r: String) = "[" + r.joinToString(",") + "]"

    @Test fun tagPattern() {
        assertEquals(ReleaseTags.Tag("beta", 412), ReleaseTags.parseTag("native-android-beta-412"))
        assertEquals(ReleaseTags.Tag("stable", 7), ReleaseTags.parseTag("native-android-stable-7"))
        assertEquals(ReleaseTags.Tag("test", 1), ReleaseTags.parseTag("native-android-test-1"))
        for (bad in listOf("native-n0-android-12", "native-android-beta-", "native-android-beta-x", "native-android-nightly-3", "v1.3.14", "", "native-android-beta-12-extra", "xnative-android-beta-12"))
            assertNull(bad, ReleaseTags.parseTag(bad))
    }

    @Test fun newerBuildOfMyChannelIsOffered() {
        val j = list(rel("native-android-beta-410"), rel("native-android-beta-412"), rel("native-android-stable-500"), rel("native-n0-android-900"), rel("native-android-beta-411"))
        val v = ReleaseTags.verdict(j, "beta", 410)
        assertTrue(v is Verdict.NewBuild); v as Verdict.NewBuild
        assertEquals(412, v.release.tag.number); assertEquals("https://example.invalid/native-android-beta-412.apk", v.release.apkUrl)
        assertTrue(v.release.pageUrl.endsWith("native-android-beta-412"))
        assertEquals("New beta build #412 (this one is #410).", ReleaseTags.describe(v, "beta", 410))
    }

    @Test fun sameOrOlderIsUpToDate() {
        val j = list(rel("native-android-beta-410"), rel("native-android-beta-409"))
        assertEquals(Verdict.UpToDate, ReleaseTags.verdict(j, "beta", 410))
        assertEquals(Verdict.UpToDate, ReleaseTags.verdict(j, "beta", 999))
    }

    @Test fun otherChannelsDraftsAndGarbageAreIgnored() {
        assertEquals(Verdict.NoneFound, ReleaseTags.verdict(list(rel("native-android-stable-5")), "beta", 1))
        assertEquals(Verdict.NoneFound, ReleaseTags.verdict(list(rel("native-android-beta-9", draft = true)), "beta", 1))
        assertEquals(Verdict.NoneFound, ReleaseTags.verdict("not json", "beta", 1))
        assertEquals(Verdict.NoneFound, ReleaseTags.verdict("[]", "beta", 1))
        assertEquals(Verdict.NoneFound, ReleaseTags.verdict("{}", "beta", 1))
    }

    @Test fun aReleaseWithoutAnApkStillTellsThePage() {
        val v = ReleaseTags.verdict(list(rel("native-android-test-8", apk = false)), "test", 3) as Verdict.NewBuild
        assertNull(v.release.apkUrl); assertTrue(v.release.pageUrl.isNotEmpty())
    }

    @Test fun paging() {
        assertTrue(ReleaseTags.apiUrl(2).contains("per_page=100&page=2"))
        assertEquals("No beta build found in the newest 500 releases (none published yet, or it is older than that).", ReleaseTags.describe(Verdict.NoneFound, "beta", 1))
    }

    @Test fun releasePageIsChecked() {
        val ok = "https://github.com/matthettich/thunder-plus/releases/tag/native-android-beta-5"
        assertEquals(ok, ReleaseTags.safePage(ok))
        assertEquals(ReleaseTags.RELEASES_PAGE, ReleaseTags.safePage("https://evil.example/releases/tag/x"))
        assertEquals(ReleaseTags.RELEASES_PAGE, ReleaseTags.safePage("https://github.com/matthettich/thunder-plus/releases.evil"))
        assertEquals(ReleaseTags.RELEASES_PAGE, ReleaseTags.safePage(""))
    }
}
