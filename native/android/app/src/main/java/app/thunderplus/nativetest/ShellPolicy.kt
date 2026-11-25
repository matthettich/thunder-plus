package app.thunderplus.nativetest

/**
 * N2 H5: the shell's decisions about the window, as pure data so a JVM test can hold the manifest to them (ShellPolicyTest reads AndroidManifest.xml).
 *
 * WHY: the page lives in ONE WebView inside ONE Activity; the live engine, the MIDI host and the foreground service do not depend on the Activity at all (they are objects / a service). What WOULD hurt is the
 * Activity being re-created by a configuration change: the WebView would reload and the page would lose its state. So the activity declares every configuration change it handles itself (fold / unfold,
 * rotation, split screen, keyboard attach, dark mode, font size, language, display size): the WebView just resizes, nothing restarts, and the audio never notices.
 */
object ShellPolicy {
    /** android:configChanges of MainActivity: every token must be in the manifest (ShellPolicyTest). */
    val CONFIG_CHANGES = listOf("orientation", "screenSize", "smallestScreenSize", "screenLayout", "keyboardHidden", "keyboard", "navigation", "uiMode", "density", "fontScale", "locale", "layoutDirection", "colorMode", "touchscreen")

    enum class Back {
        /** the WebView has history (the page opened another page of the app, e.g. the bridge test): go back in it. */
        WEB_BACK,
        /** nothing to go back to: do NOT finish the app. Hand the screen back (home) and let the song play; the page and the engine stay as they are. */
        TO_BACKGROUND
    }

    /** The back gesture / button. The activity is never finished by Back: a producer swiping back by habit must not lose the page or stop the song (owner decision to change: N2-NOTES H5). */
    fun back(webCanGoBack: Boolean): Back = if (webCanGoBack) Back.WEB_BACK else Back.TO_BACKGROUND
}
