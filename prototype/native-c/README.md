# Native C touch prototype

This is the first C-first Android sketch for Wegert.

The intended path is:

```text
touchscreen
    -> Android native input
    -> C state (center, scale)
    -> OpenGL ES uniforms
    -> fragment shader
    -> screen
```

The prototype deliberately keeps the CPU side small:

- one-finger drag changes the complex-plane center;
- two-finger pinch changes `units_per_pixel`;
- the center starts at ordinary complex zero;
- one full-screen triangle invokes the fragment shader for the visible pixels;
- there is no CPU pixel loop;
- there is no JavaScript layer;
- application code is C through `NativeActivity` / `android_native_app_glue`.

## Important placeholder

`wegert_color()` in `main.c` is only a temporary visualization. It is not the chosen Wegert coloring. Replace that function with the existing Lab-color implementation, preserving the selected saturation and logarithmic-lightness behavior.

## Hardware/software assumptions

- Android device with OpenGL ES 3.0 support.
- Android NDK containing `android_native_app_glue`.
- Touch input arrives through Android's native input event path; this prototype does not attempt to open Linux `/dev/input/eventN` directly.

## Current status

This directory is an architecture/build skeleton, not yet a complete Gradle-packaged APK. The immediate next integration step is to transplant the existing domain-coloring shader into this native shell, then package and test it on the target tablet.
