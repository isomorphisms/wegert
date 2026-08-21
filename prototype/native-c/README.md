# Native C touch prototype

This is the C-first Android shell for Wegert. It now carries the phase-portrait math and color mapping from `tablet-touch-game` directly in an OpenGL ES 3 fragment shader; Godot is not part of this path.

```text
touchscreen
    -> Android native input
    -> C camera/function state
    -> OpenGL ES uniforms
    -> fragment shader
    -> screen
```

## Preserved rendering semantics

The fragment shader matches the working tablet shader rather than the earlier placeholder:

- rational functions are represented as `product(z - zero) / product(z - pole)` with up to 16 zeros and 16 poles;
- phase is accumulated from the arguments of those factors and modulus is accumulated in log space;
- hue is the complex argument in degrees;
- color is R-style HCL, i.e. polar CIELUV with a D65 white point;
- chroma is fixed at `45`;
- lightness is `66 + 4 * frac(log10(|f(z)|)) + 3 * frac(hue / 100)`;
- codomain phase rotates once every six seconds;
- the shader evaluates the portrait per fragment; there is no CPU pixel loop.

The native prototype still seeds one zero at the origin, so its initial demonstration is `f(z) = z`, matching what the old placeholder visualized. The zero/pole arrays are now real shader inputs; a native factor-editing UI can mutate them without changing the renderer.

## Preserved camera semantics

The camera now uses the same state as `tablet-touch-game`:

- `view_center` starts at ordinary complex zero;
- `y_half_extent` starts at `2.5`;
- the real half-extent is `y_half_extent * surface_width / surface_height`;
- one-finger drag pans in complex coordinates;
- two-finger pinch changes `y_half_extent` continuously and clamps it to `[0.05, 100]`;
- positive imaginary values remain upward even though Android touch y coordinates grow downward.

The whole EGL surface is currently the portrait. There is no native toolbar yet, so the Godot buttons for zero/pole mode, undo, clear, center, and pause are intentionally not reproduced here.

## Android / graphics assumptions

- Android device with OpenGL ES 3.0 support; the manifest declares ES 3.0 as required.
- GLSL ES 3.00 fragment shaders with high-precision floating point.
- Android NDK containing `android_native_app_glue`; application code enters through `NativeActivity` and `android_main`.
- Touch input is consumed from Android native input events. This does not open Linux `/dev/input/eventN` directly.
- Android motion coordinates use a top-left origin. OpenGL `gl_FragCoord` uses a bottom-left origin; the C pan sign and shader mapping account for that explicitly.
- Surface width and height come from EGL and define the portrait aspect ratio after creation or resize.
- Rendering uses one full-screen triangle, `eglSwapInterval(..., 1)`, and `eglSwapBuffers`; there is no Choreographer integration in this prototype.
- Shader sources are embedded in `main.c` so this shell does not need Android asset-loading code yet.

## Packaging status

`CMakeLists.txt` builds the native shared library and `AndroidManifest.xml` describes the `NativeActivity`, but this directory is still not a complete Gradle-packaged APK project. The rendering transplant is complete; the next separate step is packaging it into an installable Android build and then wiring a native factor-editing control surface to the existing zero/pole arrays.
