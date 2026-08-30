# Wegert Edric -> GLSL hosted x86_64 experiment

This is a disposable integration probe. It does not replace Wegert's Android build, does not cross-compile for the phone, and does not modify the existing renderer path.

The question is narrower: on an ordinary hosted x86_64 build machine, how far can the current Wegert mathematics travel through

```text
Wegert source
  -> Edric bootstrap/compiler API
  -> idris-shader-backend
  -> GLSL ES 3.00
  -> Khronos GLSL validation
```

The probe deliberately separates failures so one red boundary cannot be mistaken for another.

## Stages

1. `host` — record x86_64/compiler/GLSL-validator availability.
2. `handwritten_glsl` — assemble and validate the existing Wegert GLSL before involving Edric.
3. `host_c_fallback` — compile and run Wegert's ordinary x86 complex-math fallback. This proves the host-side test does not depend on the AArch64 ICK object.
4. `edric_bootstrap` — build the selected Edric revision with its pinned Chez toolchain.
5. `edric_compiler_api` — install that exact compiler's API into its private bootstrap prefix.
6. `glsles_backend_build` — build the existing registered GLSL backend with the Edric executable rather than stock Idris.
7. `wegert_idr_compile` — compile the backend's ordinary `.idr` `SharedFactorPortrait`, exercising the real shared 64-zero/64-pole Wegert mathematics through Edric + GLSL without the Idric source-profile restrictions.
8. `generated_idr_glsl_validate` — validate and link that generated fragment.
9. `wegert_idric_compile` — compile `WegertProbe.idric`, exercising the same portrait through the Idric source profile.
10. `generated_idric_glsl_validate` — validate and link the `.idric`-generated fragment.

The `.idr`/`.idric` split is deliberate. If the ordinary `.idr` portrait compiles but the `.idric` portrait fails specifically because `Double` is forbidden, then the compiler API and GLSL backend hookup have already passed and the remaining problem is the shader source scalar vocabulary.

Every stage gets its own log and receipt row. The script reports failures as warnings by default and still writes the evidence directory. `WEGERT_MOCK_STRICT=1` turns the same probe into a hard gate when that becomes useful.

## Precision lanes

The workflow intentionally compares three moving integration lanes:

- current `Idriç` + GLSL backend `main`;
- current `Idriç` + the Mali-G57 backend branch with `float-precision=mediump`;
- the no-wide-float Idric branch + that same Mali branch.

The last lane is expected to tell us whether the remaining boundary is the compiler hookup or the shader source scalar vocabulary. At present `Shader.Source` still uses inherited `Double` names even though the emitted GLSL value is `float`. If the ordinary `.idr` route passes and the no-wide-float `.idric` route rejects that spelling, the receipt classifies it as `SOURCE_FLOAT_PROFILE_BLOCKS_SHADER_API` rather than as a generic GLSL failure.

## ICK boundary

ICK has no role in this hosted shader-compiler path. Wegert's ICK object is an isolated AArch64 CPU-side complex-math implementation. On x86_64, Wegert deliberately uses `complex_math_fallback.c`; the probe compiles and runs that fallback separately so an ICK problem cannot be confused with an Edric/GLSL problem.

## Output

`build/edric-glsles-host-mock/` contains:

- `components.tsv` — exact Wegert, Idric, and shader-backend revisions;
- `receipt.tsv` — stage-by-stage PASS/FAIL/SKIP diagnostics;
- `diagnosis.txt` — first failure and coarse classification;
- `logs/*.log` — one witness per stage;
- `generated/` — handwritten baseline and compiler-generated fragments when available.

This layout is intentionally simple for AICI to ingest as compatibility evidence later. It is diagnostic evidence, not a claim that a GPU executed the shader.
