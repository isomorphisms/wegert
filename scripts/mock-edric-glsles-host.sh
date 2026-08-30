#!/usr/bin/env bash
set -uo pipefail

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
edric_root=${EDRIC_ROOT:-"$repo_root/deps/Idric"}
backend_root=${IDRIS_SHADER_BACKEND:-"$repo_root/deps/idris-shader-backend"}
out=${WEGERT_MOCK_OUT:-"$repo_root/build/edric-glsles-host-mock"}
float_precision=${SHADER_FLOAT_PRECISION:-}
strict=${WEGERT_MOCK_STRICT:-0}

rm -rf "$out"
mkdir -p "$out/logs" "$out/generated" "$out/source"
receipt="$out/receipt.tsv"
components="$out/components.tsv"
summary="$out/diagnosis.txt"
printf 'stage\tstatus\tcode\twitness\n' > "$receipt"
printf 'component\trepository\trevision\n' > "$components"

warn() {
    if [ "${GITHUB_ACTIONS:-}" = true ]; then
        printf '::warning title=Wegert Edric GLSL host probe::%s\n' "$*"
    else
        printf 'WARNING: %s\n' "$*" >&2
    fi
}

record() {
    printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >> "$receipt"
}

run_stage() {
    local stage=$1
    local code=$2
    shift 2
    local log="$out/logs/$stage.log"
    printf '== %s ==\n' "$stage"
    if "$@" >"$log" 2>&1; then
        record "$stage" PASS "$code" "logs/$stage.log"
        printf 'PASS %s\n' "$stage"
        return 0
    else
        local rc=$?
        record "$stage" FAIL "$code" "logs/$stage.log"
        warn "$stage failed with exit $rc; see $log"
        printf 'FAIL %s (%d)\n' "$stage" "$rc"
        return "$rc"
    fi
}

skip_stage() {
    local stage=$1
    local code=$2
    local reason=$3
    printf '%s\n' "$reason" > "$out/logs/$stage.log"
    record "$stage" SKIP "$code" "logs/$stage.log"
    printf 'SKIP %s: %s\n' "$stage" "$reason"
}

stage_status() {
    local wanted=$1
    awk -F '\t' -v wanted="$wanted" '$1 == wanted { print $2; exit }' "$receipt"
}

revision() {
    local label=$1
    local repository=$2
    local path=$3
    if [ -d "$path/.git" ]; then
        printf '%s\t%s\t%s\n' "$label" "$repository" "$(git -C "$path" rev-parse HEAD)" >> "$components"
    else
        printf '%s\t%s\t%s\n' "$label" "$repository" MISSING >> "$components"
    fi
}

stage_host() {
    uname -a
    printf 'architecture: '; uname -m
    command -v cc
    cc --version | head -n 1
    command -v glslangValidator
    glslangValidator --version | head -n 2
}

stage_handwritten_glsl() {
    local assembled="$out/generated/wegert-handwritten.frag"
    awk -v core="$repo_root/wegert_color.glsl" '
        /\/\*__WEGERT_COLOR_CORE__\*\// {
            while ((getline line < core) > 0) print line
            close(core)
            next
        }
        { print }
    ' "$repo_root/wegert.frag.in" > "$assembled"
    glslangValidator -S frag "$assembled"
    grep -q 'uniform vec2 u_zeros\[MAX_FACTORS\];' "$assembled"
    grep -q 'wegert_color_from_phase_log_modulus' "$assembled"
}

stage_host_c() {
    local exe="$out/wegert-complex-fallback-test"
    cc -std=c11 -Wall -Wextra -Werror -pedantic \
        "$repo_root/tests/complex_math_test.c" \
        "$repo_root/complex_math_fallback.c" \
        -lm -o "$exe"
    "$exe"
}

stage_edric_bootstrap() {
    test -x "$edric_root/edric"
    PATH="$edric_root/.tools/bin:$PATH" "$edric_root/edric" bootstrap
    test -x "$edric_root/bootstrap-build/bin/idris2"
}

stage_edric_api() {
    local edric="$edric_root/bootstrap-build/bin/idris2"
    PATH="$edric_root/.tools/bin:$PATH" \
        make -C "$edric_root" install-api \
        IDRIS2_BOOT="$edric" \
        PREFIX="$edric_root/bootstrap-build"
}

stage_glsles_backend() {
    local edric="$edric_root/bootstrap-build/bin/idris2"
    PATH="$edric_root/.tools/bin:$PATH" \
        make -C "$backend_root" backend IDRIS2="$edric"
    test -x "$backend_root/build/exec/idris2-glsles"
}

stage_wegert_idr_compile() {
    local compiler="$backend_root/build/exec/idris2-glsles"
    local args=(
        "$compiler"
        --cg glsles
        --source-dir src
        --output-dir "$out/generated"
    )
    if [ -n "$float_precision" ]; then
        args+=(--directive "float-precision=$float_precision")
    fi
    args+=(src/Example/SharedFactorPortrait.idr -o wegert-edric-idr)
    (
        cd "$backend_root"
        PATH="$edric_root/.tools/bin:$PATH" LC_ALL=C "${args[@]}"
    )
    test -s "$out/generated/wegert-edric-idr.frag"
}

stage_wegert_idric_compile() {
    local compiler="$backend_root/build/exec/idris2-glsles"
    local source_root="$out/source"
    rm -rf "$source_root"
    mkdir -p "$source_root"
    cp -R "$backend_root/src/Shader" "$source_root/Shader"
    cp "$repo_root/experiments/edric-glsles-host/WegertProbe.idric" \
       "$source_root/WegertProbe.idric"

    local args=(
        "$compiler"
        --cg glsles
        --source-dir .
        --output-dir "$out/generated"
    )
    if [ -n "$float_precision" ]; then
        args+=(--directive "float-precision=$float_precision")
    fi
    args+=(WegertProbe.idric -o wegert-edric-idric)

    (
        cd "$source_root"
        PATH="$edric_root/.tools/bin:$PATH" LC_ALL=C "${args[@]}"
    )
    test -s "$out/generated/wegert-edric-idric.frag"
}

stage_generated_glsl() {
    local fragment=$1
    test -s "$fragment"
    glslangValidator -S frag "$fragment"
    glslangValidator -l "$backend_root/fixtures/wegert-fullscreen.vert" "$fragment"
    grep -q 'uniform vec2 u_zeros\[64\];' "$fragment"
    grep -q 'uniform vec2 u_poles\[64\];' "$fragment"
    grep -q 'atan(' "$fragment"
    grep -q 'log(' "$fragment"
    grep -q 'pow(' "$fragment"
    if [ -n "$float_precision" ]; then
        grep -q "precision $float_precision float;" "$fragment"
    fi
}

revision wegert isomorphismes/wegert "$repo_root"
revision idric isomorphisms/Idric "$edric_root"
revision shader_backend isomorphisms/idris-shader-backend "$backend_root"

failures=0
run_stage host HOST_ENV stage_host || failures=$((failures + 1))
run_stage handwritten_glsl WEGERT_GLSL_BASELINE stage_handwritten_glsl || failures=$((failures + 1))
run_stage host_c_fallback WEGERT_X86_C_FALLBACK stage_host_c || failures=$((failures + 1))

if run_stage edric_bootstrap EDRIC_BOOTSTRAP stage_edric_bootstrap; then
    if run_stage edric_compiler_api EDRIC_COMPILER_API stage_edric_api; then
        if run_stage glsles_backend_build GLSLES_BACKEND_BUILD stage_glsles_backend; then
            if run_stage wegert_idr_compile WEGERT_IDR_TO_GLSL stage_wegert_idr_compile; then
                run_stage generated_idr_glsl_validate GENERATED_IDR_GLSL_VALIDATE \
                    stage_generated_glsl "$out/generated/wegert-edric-idr.frag" || failures=$((failures + 1))
            else
                failures=$((failures + 1))
                skip_stage generated_idr_glsl_validate GENERATED_IDR_GLSL_VALIDATE \
                    'ordinary .idr Wegert fragment unavailable'
            fi

            if run_stage wegert_idric_compile WEGERT_IDRIC_TO_GLSL stage_wegert_idric_compile; then
                run_stage generated_idric_glsl_validate GENERATED_IDRIC_GLSL_VALIDATE \
                    stage_generated_glsl "$out/generated/wegert-edric-idric.frag" || failures=$((failures + 1))
            else
                failures=$((failures + 1))
                skip_stage generated_idric_glsl_validate GENERATED_IDRIC_GLSL_VALIDATE \
                    'Wegert .idric fragment unavailable'
            fi
        else
            failures=$((failures + 1))
            skip_stage wegert_idr_compile WEGERT_IDR_TO_GLSL 'GLSL backend executable unavailable'
            skip_stage generated_idr_glsl_validate GENERATED_IDR_GLSL_VALIDATE 'ordinary .idr fragment unavailable'
            skip_stage wegert_idric_compile WEGERT_IDRIC_TO_GLSL 'GLSL backend executable unavailable'
            skip_stage generated_idric_glsl_validate GENERATED_IDRIC_GLSL_VALIDATE 'Wegert .idric fragment unavailable'
        fi
    else
        failures=$((failures + 1))
        skip_stage glsles_backend_build GLSLES_BACKEND_BUILD 'Edric compiler API installation failed'
        skip_stage wegert_idr_compile WEGERT_IDR_TO_GLSL 'GLSL backend was not built'
        skip_stage generated_idr_glsl_validate GENERATED_IDR_GLSL_VALIDATE 'ordinary .idr fragment unavailable'
        skip_stage wegert_idric_compile WEGERT_IDRIC_TO_GLSL 'GLSL backend was not built'
        skip_stage generated_idric_glsl_validate GENERATED_IDRIC_GLSL_VALIDATE 'Wegert .idric fragment unavailable'
    fi
else
    failures=$((failures + 1))
    skip_stage edric_compiler_api EDRIC_COMPILER_API 'Edric bootstrap failed'
    skip_stage glsles_backend_build GLSLES_BACKEND_BUILD 'Edric compiler unavailable'
    skip_stage wegert_idr_compile WEGERT_IDR_TO_GLSL 'GLSL backend was not built'
    skip_stage generated_idr_glsl_validate GENERATED_IDR_GLSL_VALIDATE 'ordinary .idr fragment unavailable'
    skip_stage wegert_idric_compile WEGERT_IDRIC_TO_GLSL 'GLSL backend was not built'
    skip_stage generated_idric_glsl_validate GENERATED_IDRIC_GLSL_VALIDATE 'Wegert .idric fragment unavailable'
fi

{
    printf 'Wegert Edric -> GLSL host diagnostic\n'
    printf 'float_precision=%s\n' "${float_precision:-default}"
    printf 'failures=%d\n' "$failures"
    first_fail=$(awk -F '\t' '$2 == "FAIL" { print $1; exit }' "$receipt")
    printf 'first_failure=%s\n' "${first_fail:-none}"

    compile_log="$out/logs/wegert_idric_compile.log"
    if [ "$(stage_status wegert_idr_compile)" = PASS ] && \
       [ "$(stage_status wegert_idric_compile)" = FAIL ] && \
       [ -f "$compile_log" ] && \
       grep -Eiq 'wider floating precision|does not provide Double|Double.*disabled|decimal floating literals.*disabled|Float16.*Double|Double.*Float16|unsupported shader entry type Float16' "$compile_log"; then
        printf 'classification=SOURCE_FLOAT_PROFILE_BLOCKS_SHADER_API\n'
        printf 'detail=Edric and the GLSL backend compiled the ordinary Idris Wegert path, but the .idric path reached a Float16/Double shader-source mismatch. Shader.Source and the GLSL signature/lowering boundary still need an explicit Float16 scalar contract.\n'
    elif [ -n "${first_fail:-}" ]; then
        printf 'classification=FAILED_AT_%s\n' "$first_fail"
    else
        printf 'classification=HOST_EDRIC_GLSL_PATH_PASS\n'
    fi

    printf '\nICK boundary: NOT_USED. The hosted x86_64 probe compiles complex_math_fallback.c; ICK remains the separate AArch64 object path.\n'
} > "$summary"

cat "$summary"
printf '\nreceipt: %s\ncomponents: %s\n' "$receipt" "$components"

if [ "$strict" = 1 ] && [ "$failures" -ne 0 ]; then
    exit 1
fi
exit 0
