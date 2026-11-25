package app.thunderplus.nativetest

import java.util.concurrent.Callable
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.Future

/**
 * ANR fix (25 Nov): ONE background thread that runs tasks in the order they were posted. [LiveEngine] runs nativeStart / nativeShutdown on it, so the main thread never waits for the native
 * life lock (a shutdown can sit behind a load for seconds) and start / shutdown / reset keep their order. Pure JVM: unit-tested.
 */
class OrderedWorker(name: String) {
    private val exec: ExecutorService = Executors.newSingleThreadExecutor { r -> Thread(r, name).also { it.isDaemon = true } }

    /** Queues [work] and returns at once. */
    fun post(work: () -> Unit) { exec.execute { try { work() } catch (e: Throwable) { /* the caller logs inside its work */ } } }

    /** Queues [work] and waits for its result (NOT on the main thread). */
    fun <T> call(work: () -> T): T { val f: Future<T> = exec.submit(Callable { work() }); return f.get() }

    /** Waits until everything posted before this call has run (NOT on the main thread). */
    fun drain() { call { } }
}
