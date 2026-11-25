plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// N2 H4: the build's CHANNEL and number. `-PthunderChannel=stable|beta` makes a release-channel app (its own application id and name; beta also gets the dogs-swapped icon, as the desktop beta does), no property is the
// native TEST build every CI run makes (application id app.thunderplus.nativetest). `-PbuildNumber=N` is the CI run number: versionCode, and what the in-app "new build" check compares with the release tags.
val channel: String = (project.findProperty("thunderChannel") as String?) ?: "test"
val buildNumber: Int = ((project.findProperty("buildNumber") as String?) ?: "1").toIntOrNull()?.coerceAtLeast(1) ?: 1

val repoRoot = rootDir.resolve("../..").canonicalFile   // the repo root is two folders above native/android
// The beta's launcher icon (the two dogs with swapped colours): a res directory made from electron/icons-beta, wired to the release source set below through the task, so Gradle knows who makes it.
val betaIcons = tasks.register<Copy>("betaIcons") {
    from(repoRoot.resolve("electron/icons-beta/plus-icon-192.png")) { rename { "ic_launcher.png" } }
    into(layout.buildDirectory.dir("betaRes/drawable-nodpi"))
}

android {
    namespace = "app.thunderplus.nativetest"
    compileSdk = 35

    defaultConfig {
        // stable and beta can sit side by side on one phone (different ids); every build of one channel has the same id and (when the CI secrets exist) the same key, so it installs OVER the last one and keeps its songs and settings
        applicationId = when (channel) { "stable" -> "app.thunderplus.android"; "beta" -> "app.thunderplus.android.beta"; else -> "app.thunderplus.nativetest" }
        minSdk = 29
        targetSdk = 35
        versionCode = buildNumber
        versionName = if (channel == "test") "n0-$buildNumber" else "0.1.$buildNumber" + (if (channel == "beta") "-beta" else "")
        resValue("string", "app_name", when (channel) { "stable" -> "Thunder Plus"; "beta" -> "Thunder Plus (beta)"; else -> "Thunder Plus (native TEST)" })
        buildConfigField("String", "CHANNEL", "\"$channel\"")
        buildConfigField("int", "BUILD_NUMBER", "$buildNumber")
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            // The optional LXR drum module (GPL-3.0-only) is ON; a closed build passes -PthunderWithLxr=false (docs/port/IF-SOLD.md).
            val withLxr = (project.findProperty("thunderWithLxr") as String?) != "false"
            cmake { arguments += listOf("-DANDROID_STL=c++_static", "-DTHUNDER_WITH_LXR=" + if (withLxr) "ON" else "OFF") }
        }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    buildFeatures { buildConfig = true }

    // N2 H4: the release key. CI hands over the owner's keystore (GitHub secrets ANDROID_KEYSTORE_B64 / _PASS, ANDROID_KEY_ALIAS / _PASS, decoded to a file by the workflow) through these four variables. Without them the
    // release build is signed with the repository's own DEBUG key (native/android/ci-debug.keystore: the well-known "android" password, a public throwaway; the workflow names the APK "...-DEBUGKEY.apk"): such a build
    // installs over the previous DEBUGKEY build but NOT over a build signed with the owner's key (Android refuses a different signature: uninstall first, which deletes the app's data).
    val ksPath = System.getenv("ANDROID_KEYSTORE_PATH")
    val ksPass = System.getenv("ANDROID_KEYSTORE_PASS")
    val ksAlias = System.getenv("ANDROID_KEY_ALIAS")
    val keyPass = System.getenv("ANDROID_KEY_PASS")
    val haveOwnerKey = !ksPath.isNullOrEmpty() && file(ksPath).exists() && !ksPass.isNullOrEmpty() && !ksAlias.isNullOrEmpty() && !keyPass.isNullOrEmpty()
    signingConfigs {
        create("release") {
            if (haveOwnerKey) { storeFile = file(ksPath!!); storePassword = ksPass; keyAlias = ksAlias; keyPassword = keyPass }
            else { storeFile = rootProject.file("ci-debug.keystore"); storePassword = "android"; keyAlias = "androiddebugkey"; keyPassword = "android" }
        }
    }
    buildTypes {
        release {
            signingConfig = signingConfigs.getByName("release")
            // R8 is OFF unless asked for (-PthunderMinify=true): nothing here can test a minified build on a device, and a stripped crash trace is worse than a big APK. The rules and the mapping.txt artifact are ready for the day it is turned on.
            isMinifyEnabled = (project.findProperty("thunderMinify") as String?) == "true"
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            ndk { debugSymbolLevel = "SYMBOL_TABLE" }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }

    // The real app's files, copied (never moved or edited) into the APK's assets.
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("webassets"))
    // the beta's launcher icon: the two dogs with swapped colours (electron/icons-beta, made by tools/make-beta-icon.py); the "release" source set overrides main's drawable-nodpi/ic_launcher.png
    if (channel == "beta") sourceSets["release"].res.srcDir(tasks.named<Copy>("betaIcons").map { it.destinationDir.parentFile })   // betaRes/ (the task writes betaRes/drawable-nodpi/ic_launcher.png)
    androidResources { noCompress += listOf("sf3", "wasm", "json") }

    packaging { jniLibs { useLegacyPackaging = false } }
}

dependencies {
    implementation("androidx.webkit:webkit:1.12.1")
    implementation("androidx.core:core-ktx:1.15.0")
    testImplementation("org.json:json:20240303")   // N2 H1b: the JVM tests of BridgeTranslator.kt use org.json (the Android stub jar only has empty methods)
    testImplementation("junit:junit:4.13.2")   // U36 T9: the JVM unit tests of MidiModel.kt (src/test), run by `gradle :app:testDebugUnitTest`
}

// Copies the web app into build/webassets (the repo root is two folders above native/android).
val copyWebAssets = tasks.register<Copy>("copyWebAssets") {
    from(repoRoot) {
        include("index.html", "manifest.webmanifest", "*.js")
        include("chains/**", "waves/**", "core/**", "fonts/**", "icons/**", "sounds/**", "wavetables/**", "pump/**", "lxr/**")
        exclude("lxr/src/**", "**/*.md", "**/node_modules/**")
    }
    // the beta's art replaces the stable icons the shell shows (notification, media session): the later `from` wins
    if (channel == "beta") from(repoRoot.resolve("electron/icons-beta")) { include("plus-icon-192.png", "plus-icon-512.png"); into("icons") }
    duplicatesStrategy = DuplicatesStrategy.INCLUDE
    into(layout.buildDirectory.dir("webassets"))
}
tasks.configureEach {
    if (name.startsWith("merge") && name.endsWith("Assets")) dependsOn(copyWebAssets)
}
