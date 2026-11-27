package app.thunderplus.nativetest

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HelloDiagTest {
    @Test fun portHandedOncePerLoad() {
        val g = PortGate()
        g.onPageStarted()
        assertTrue(g.claim("ask", 100))
        assertFalse(g.claim("pageFinished", 200))
        assertEquals("ask", g.path)
        g.onPageStarted()
        assertEquals("", g.path)
        assertTrue(g.claim("pageFinished", 300))
        assertEquals("pageFinished", g.path)
    }

    @Test fun releaseLetsTheFallbackTry() {
        val g = PortGate()
        g.onPageStarted()
        assertTrue(g.claim("ask", 0)); g.release()
        assertTrue(g.claim("pageFinished", 1))
    }

    @Test fun helloLineTellsTheStory() {
        val h = HelloLog()
        assertTrue(h.line(0).contains("0"))
        h.began(1000); h.failed("live engine did not start: nativeStart returned -1")
        val l = h.line(13000)
        assertTrue(l, l.contains("1,") && l.contains("12 s ago") && l.contains("FAILED: live engine did not start"))
        h.began(20000); h.ok()
        assertTrue(h.line(20000).contains("2, last 0 s ago: ok"))
    }

    @Test fun textIncludesAllFour() {
        val t = HelloDiag.text(5000, "x went wrong")
        assertTrue(t.contains("hellos from the page") && t.contains("x went wrong") && t.contains("port to page"))
    }
}
