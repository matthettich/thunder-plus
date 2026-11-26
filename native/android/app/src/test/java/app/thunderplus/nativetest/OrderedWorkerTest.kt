package app.thunderplus.nativetest

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.Collections
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** ANR fix (25 Nov): the worker LiveEngine runs nativeStart / nativeShutdown on. */
class OrderedWorkerTest {

    @Test fun postReturnsWhileTheWorkIsBlocked() {
        val w = OrderedWorker("t1")
        val gate = CountDownLatch(1); val ran = CountDownLatch(1)
        val t0 = System.nanoTime()
        w.post { gate.await(5, TimeUnit.SECONDS); ran.countDown() }   // a "shutdown" stuck behind a load
        val ms = (System.nanoTime() - t0) / 1_000_000
        assertTrue("post blocked for $ms ms", ms < 200)
        assertFalse(ran.await(50, TimeUnit.MILLISECONDS))
        gate.countDown()
        assertTrue(ran.await(2, TimeUnit.SECONDS))
    }

    @Test fun tasksRunInTheOrderPosted() {
        val w = OrderedWorker("t2")
        val log = Collections.synchronizedList(ArrayList<String>())
        w.post { Thread.sleep(50); log.add("shutdown") }
        w.post { log.add("shutdown2") }
        assertTrue(w.call { log.add("start"); true })   // like start() after shutdown() in engineReset
        assertEquals(listOf("shutdown", "shutdown2", "start"), log.toList())
    }

    @Test fun drainWaitsForEverythingBefore() {
        val w = OrderedWorker("t3")
        val log = Collections.synchronizedList(ArrayList<String>())
        w.post { Thread.sleep(80); log.add("a") }
        w.drain()
        assertEquals(listOf("a"), log.toList())
    }

    @Test fun aThrowingTaskDoesNotKillTheWorker() {
        val w = OrderedWorker("t4")
        w.post { throw IllegalStateException("x") }
        assertEquals(7, w.call { 7 })
    }

    @Test fun runsOffTheCallerThread() {
        val w = OrderedWorker("t5")
        assertTrue(w.call { Thread.currentThread().name == "t5" })
    }
}
