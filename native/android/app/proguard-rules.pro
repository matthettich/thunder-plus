# N2 H4: R8 rules, used only when the build is made with -PthunderMinify=true (off by default).
# JNI: the C++ side finds these by their Java names (Java_app_thunderplus_nativetest_<Class>_<method>) and the JNI layer may call back into fields: nothing in this package is renamed or removed.
-keep class app.thunderplus.nativetest.** { *; }
-keepclasseswithmembernames class * { native <methods>; }
# readable crash traces: file names and line numbers stay; mapping.txt is uploaded by CI
-keepattributes SourceFile,LineNumberTable,*Annotation*
-renamesourcefileattribute SourceFile
-dontwarn org.json.**
