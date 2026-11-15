plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "app.thunderplus.nativetest"
    compileSdk = 35

    defaultConfig {
        applicationId = "app.thunderplus.nativetest"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "n0"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake { arguments += listOf("-DANDROID_STL=c++_static") }
        }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }

    // The real app's files, copied (never moved or edited) into the APK's assets.
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("webassets"))
    androidResources { noCompress += listOf("sf3", "wasm", "json") }

    packaging { jniLibs { useLegacyPackaging = false } }
}

dependencies {
    implementation("androidx.webkit:webkit:1.12.1")
    implementation("androidx.core:core-ktx:1.15.0")
}

// Copies the web app into build/webassets (the repo root is two folders above native/android).
val repoRoot = rootDir.resolve("../..").canonicalFile
val copyWebAssets = tasks.register<Copy>("copyWebAssets") {
    from(repoRoot) {
        include("index.html", "manifest.webmanifest", "*.js")
        include("chains/**", "waves/**", "core/**", "fonts/**", "icons/**", "sounds/**", "wavetables/**", "pump/**", "lxr/**")
        exclude("lxr/src/**", "**/*.md", "**/node_modules/**")
    }
    into(layout.buildDirectory.dir("webassets"))
}
tasks.configureEach {
    if (name.startsWith("merge") && name.endsWith("Assets")) dependsOn(copyWebAssets)
}
