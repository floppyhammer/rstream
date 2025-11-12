# Add project specific ProGuard rules here.
# By default, the shrinker retains all classes that are specified as
# entry points in your app's manifest file. The shrinker also retains
# any classes that are referenced by those classes, and so on.

# GStreamer JNI / Android Media Classes
# These classes are accessed by name from native code (JNI) and MUST NOT be obfuscated or removed.

-keep public class org.freedesktop.gstreamer.GStreamer {
    *;
}
-keep public class org.freedesktop.gstreamer.GStreamer$* {
    *;
}
-keep public class org.freedesktop.gstreamer.androidmedia.GstAmc* {
    *;
}

# The specific class mentioned in your error:
-keep public class org.freedesktop.gstreamer.androidmedia.GstAmcOnFrameAvailableListener {
    <init>(...);
    public *;
}

# If you are using any custom GStreamer Java plugins, you might need to keep them too.
# Example for a custom element:
# -keep public class com.yourcompany.gstreamer.MyCustomElement {
#     *;
# }

# The following rules are used to strip debug and verbose logs from release builds.
-assumenosideeffects class android.util.Log {
    public static int v(...);
    public static int d(...);
}
