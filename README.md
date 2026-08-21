# Wegert

Interactive Wegert phase portraits of complex polynomials on Android.

## First phone build

The first playable build is deliberately small:

- enter a real-coefficient polynomial in `z`, including factored forms such as `(z - 1)(z + 1)`;
- see the expanded polynomial and its numerical zeros;
- use named presets for parabola, shifted parabola, cubic, and a two-zero example;
- drag the portrait to move the visible complex domain;
- pinch, or use `+` / `−`, to change scale; `Center 0` restores the initial domain;
- render the portrait directly with an OpenGL ES 3 fragment shader, with no JavaScript layer.

The shader maps phase to HCL hue, uses chroma 45, and keeps the existing 66/4/3 lightness structure. The modulus band is logarithmic so multiplicative changes of `|f(z)|` repeat the lightness pattern.

### Polynomial syntax

Supported in the first build: real numbers, `z`, `+`, `-`, `*`, implicit multiplication, parentheses, and nonnegative integer powers. Maximum degree: 12.

Examples:

```text
z^2
(z - 1)^2
(z - 1)(z + 1)
z^3 - 1
```

## Android build

```sh
gradle :app:assembleDebug
```

The `initial-phone-build` branch publishes its debug APK as a GitHub prerelease after each successful push, so the exact branch commit can be installed directly on a phone for testing.
