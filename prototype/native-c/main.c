#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "wegert", __VA_ARGS__)

#define MAX_POINTS 16
#define INITIAL_Y_HALF_EXTENT 2.5f
#define MIN_Y_HALF_EXTENT 0.05f
#define MAX_Y_HALF_EXTENT 100.0f
#define PHASE_CYCLE_SECONDS 6.0
#define PI_F 3.14159265358979323846f

typedef struct {
    struct android_app *android;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;

    GLuint program;
    GLuint vao;

    GLint zero_count_location;
    GLint pole_count_location;
    GLint zeros_location;
    GLint poles_location;
    GLint phase_offset_location;
    GLint viewport_location;
    GLint aspect_location;
    GLint view_center_location;
    GLint y_half_extent_location;

    int width;
    int height;

    /* Mathematical function: product(z-zero) / product(z-pole). */
    float zeros[MAX_POINTS][2];
    float poles[MAX_POINTS][2];
    int zero_count;
    int pole_count;

    /* Camera semantics match tablet-touch-game. */
    float center_re;
    float center_im;
    float y_half_extent;

    double phase_epoch_seconds;

    bool ready;
    bool dirty;

    bool touching;
    float last_x;
    float last_y;
    float last_pinch_distance;
} Wegert;

static const char *vertex_shader_source =
    "#version 300 es\n"
    "precision highp float;\n"
    "\n"
    "const vec2 vertices[3] = vec2[](\n"
    "    vec2(-1.0, -1.0),\n"
    "    vec2( 3.0, -1.0),\n"
    "    vec2(-1.0,  3.0)\n"
    ");\n"
    "\n"
    "void main() {\n"
    "    gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);\n"
    "}\n";

static const char *fragment_shader_source =
    "#version 300 es\n"
    "precision highp float;\n"
    "\n"
    "const int MAX_POINTS = 16;\n"
    "const float PI = 3.14159265358979323846;\n"
    "const float LN_10 = 2.302585092994046;\n"
    "\n"
    "uniform int zero_count;\n"
    "uniform int pole_count;\n"
    "uniform vec2 zeros[16];\n"
    "uniform vec2 poles[16];\n"
    "uniform float phase_offset;\n"
    "uniform vec2 viewport;\n"
    "uniform float aspect;\n"
    "uniform vec2 view_center;\n"
    "uniform float y_half_extent;\n"
    "\n"
    "out vec4 output_color;\n"
    "\n"
    "float linear_to_srgb(float x) {\n"
    "    x = max(x, 0.0);\n"
    "    if (x <= 0.0031308) {\n"
    "        return 12.92 * x;\n"
    "    }\n"
    "    return 1.055 * pow(x, 1.0 / 2.4) - 0.055;\n"
    "}\n"
    "\n"
    "vec3 hcl_to_srgb(float hue_degrees, float chroma, float lightness) {\n"
    "    /* R's hcl(): polar CIELUV, D65 white point. */\n"
    "    float h = hue_degrees * PI / 180.0;\n"
    "    float u_star = chroma * cos(h);\n"
    "    float v_star = chroma * sin(h);\n"
    "\n"
    "    const float u_n = 0.19783982482140777;\n"
    "    const float v_n = 0.4683363029324097;\n"
    "\n"
    "    if (lightness <= 0.0) {\n"
    "        return vec3(0.0);\n"
    "    }\n"
    "\n"
    "    float u_prime = u_star / (13.0 * lightness) + u_n;\n"
    "    float v_prime = v_star / (13.0 * lightness) + v_n;\n"
    "    v_prime = max(v_prime, 0.000001);\n"
    "\n"
    "    float Y;\n"
    "    if (lightness > 8.0) {\n"
    "        Y = pow((lightness + 16.0) / 116.0, 3.0);\n"
    "    } else {\n"
    "        Y = lightness / 903.2962962962963;\n"
    "    }\n"
    "\n"
    "    float X = 9.0 * Y * u_prime / (4.0 * v_prime);\n"
    "    float Z = Y * (12.0 - 3.0 * u_prime - 20.0 * v_prime) /\n"
    "        (4.0 * v_prime);\n"
    "\n"
    "    vec3 linear_rgb = vec3(\n"
    "         3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z,\n"
    "        -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z,\n"
    "         0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z\n"
    "    );\n"
    "\n"
    "    return clamp(vec3(\n"
    "        linear_to_srgb(linear_rgb.r),\n"
    "        linear_to_srgb(linear_rgb.g),\n"
    "        linear_to_srgb(linear_rgb.b)\n"
    "    ), 0.0, 1.0);\n"
    "}\n"
    "\n"
    "vec3 wegert_color(vec2 z) {\n"
    "    float phase = phase_offset;\n"
    "    float log_modulus = 0.0;\n"
    "\n"
    "    for (int index = 0; index < MAX_POINTS; index++) {\n"
    "        if (index < zero_count) {\n"
    "            vec2 delta = z - zeros[index];\n"
    "            float radius = max(length(delta), 0.000001);\n"
    "            phase += atan(delta.y, delta.x);\n"
    "            log_modulus += log(radius);\n"
    "        }\n"
    "        if (index < pole_count) {\n"
    "            vec2 delta = z - poles[index];\n"
    "            float radius = max(length(delta), 0.000001);\n"
    "            phase -= atan(delta.y, delta.x);\n"
    "            log_modulus -= log(radius);\n"
    "        }\n"
    "    }\n"
    "\n"
    "    float hue = mod(phase * 180.0 / PI, 360.0);\n"
    "    if (hue < 0.0) {\n"
    "        hue += 360.0;\n"
    "    }\n"
    "\n"
    "    float modulus_band = fract(log_modulus / LN_10);\n"
    "    float hue_band = fract(hue / 100.0);\n"
    "    float lightness = 66.0 + 4.0 * modulus_band + 3.0 * hue_band;\n"
    "\n"
    "    return hcl_to_srgb(hue, 45.0, lightness);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec2 normalized = vec2(\n"
    "        gl_FragCoord.x / viewport.x * 2.0 - 1.0,\n"
    "        gl_FragCoord.y / viewport.y * 2.0 - 1.0\n"
    "    );\n"
    "    vec2 z = view_center + vec2(\n"
    "        normalized.x * y_half_extent * aspect,\n"
    "        normalized.y * y_half_extent\n"
    "    );\n"
    "    output_color = vec4(wegert_color(z), 1.0);\n"
    "}\n";

static double monotonic_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double) now.tv_sec + (double) now.tv_nsec / 1000000000.0;
}

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        char message[4096];
        glGetShaderInfoLog(shader, sizeof message, NULL, message);
        LOG("shader error: %s", message);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint make_program(void)
{
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

    if (vertex == 0 || fragment == 0) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char message[4096];
        glGetProgramInfoLog(program, sizeof message, NULL, message);
        LOG("program link error: %s", message);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

static void stop_graphics(Wegert *wegert)
{
    if (wegert->display == EGL_NO_DISPLAY)
        return;

    if (wegert->program != 0)
        glDeleteProgram(wegert->program);
    if (wegert->vao != 0)
        glDeleteVertexArrays(1, &wegert->vao);

    eglMakeCurrent(
        wegert->display,
        EGL_NO_SURFACE,
        EGL_NO_SURFACE,
        EGL_NO_CONTEXT
    );

    if (wegert->context != EGL_NO_CONTEXT)
        eglDestroyContext(wegert->display, wegert->context);
    if (wegert->surface != EGL_NO_SURFACE)
        eglDestroySurface(wegert->display, wegert->surface);

    eglTerminate(wegert->display);

    wegert->display = EGL_NO_DISPLAY;
    wegert->surface = EGL_NO_SURFACE;
    wegert->context = EGL_NO_CONTEXT;
    wegert->program = 0;
    wegert->vao = 0;
    wegert->ready = false;
}

static bool start_graphics(Wegert *wegert)
{
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    wegert->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (wegert->display == EGL_NO_DISPLAY)
        return false;

    if (!eglInitialize(wegert->display, NULL, NULL)) {
        wegert->display = EGL_NO_DISPLAY;
        return false;
    }

    EGLConfig config;
    EGLint config_count = 0;

    if (!eglChooseConfig(
        wegert->display,
        config_attributes,
        &config,
        1,
        &config_count
    ) || config_count < 1) {
        stop_graphics(wegert);
        return false;
    }

    EGLint format;
    eglGetConfigAttrib(
        wegert->display,
        config,
        EGL_NATIVE_VISUAL_ID,
        &format
    );

    ANativeWindow_setBuffersGeometry(
        wegert->android->window,
        0,
        0,
        format
    );

    wegert->surface = eglCreateWindowSurface(
        wegert->display,
        config,
        wegert->android->window,
        NULL
    );

    wegert->context = eglCreateContext(
        wegert->display,
        config,
        EGL_NO_CONTEXT,
        context_attributes
    );

    if (wegert->surface == EGL_NO_SURFACE ||
        wegert->context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(
            wegert->display,
            wegert->surface,
            wegert->surface,
            wegert->context
        )) {
        stop_graphics(wegert);
        return false;
    }

    eglSwapInterval(wegert->display, 1);

    eglQuerySurface(
        wegert->display,
        wegert->surface,
        EGL_WIDTH,
        &wegert->width
    );
    eglQuerySurface(
        wegert->display,
        wegert->surface,
        EGL_HEIGHT,
        &wegert->height
    );

    wegert->program = make_program();
    if (wegert->program == 0) {
        stop_graphics(wegert);
        return false;
    }

    glGenVertexArrays(1, &wegert->vao);

    wegert->zero_count_location =
        glGetUniformLocation(wegert->program, "zero_count");
    wegert->pole_count_location =
        glGetUniformLocation(wegert->program, "pole_count");
    wegert->zeros_location =
        glGetUniformLocation(wegert->program, "zeros[0]");
    wegert->poles_location =
        glGetUniformLocation(wegert->program, "poles[0]");
    wegert->phase_offset_location =
        glGetUniformLocation(wegert->program, "phase_offset");
    wegert->viewport_location =
        glGetUniformLocation(wegert->program, "viewport");
    wegert->aspect_location =
        glGetUniformLocation(wegert->program, "aspect");
    wegert->view_center_location =
        glGetUniformLocation(wegert->program, "view_center");
    wegert->y_half_extent_location =
        glGetUniformLocation(wegert->program, "y_half_extent");

    wegert->ready = true;
    wegert->dirty = true;
    return true;
}

static float current_phase_offset(const Wegert *wegert)
{
    double elapsed = monotonic_seconds() - wegert->phase_epoch_seconds;
    double cycles = elapsed / PHASE_CYCLE_SECONDS;
    double fraction = cycles - floor(cycles);
    return (float) (fraction * 2.0 * PI_F);
}

static void draw(Wegert *wegert)
{
    if (!wegert->ready || wegert->width <= 0 || wegert->height <= 0)
        return;

    glViewport(0, 0, wegert->width, wegert->height);
    glUseProgram(wegert->program);
    glBindVertexArray(wegert->vao);

    glUniform1i(wegert->zero_count_location, wegert->zero_count);
    glUniform1i(wegert->pole_count_location, wegert->pole_count);

    if (wegert->zero_count > 0) {
        glUniform2fv(
            wegert->zeros_location,
            wegert->zero_count,
            &wegert->zeros[0][0]
        );
    }

    if (wegert->pole_count > 0) {
        glUniform2fv(
            wegert->poles_location,
            wegert->pole_count,
            &wegert->poles[0][0]
        );
    }

    glUniform1f(
        wegert->phase_offset_location,
        current_phase_offset(wegert)
    );
    glUniform2f(
        wegert->viewport_location,
        (float) wegert->width,
        (float) wegert->height
    );
    glUniform1f(
        wegert->aspect_location,
        (float) wegert->width / (float) wegert->height
    );
    glUniform2f(
        wegert->view_center_location,
        wegert->center_re,
        wegert->center_im
    );
    glUniform1f(
        wegert->y_half_extent_location,
        wegert->y_half_extent
    );

    /* One triangle covers the whole screen; the fragment shader does pixels. */
    glDrawArrays(GL_TRIANGLES, 0, 3);

    eglSwapBuffers(wegert->display, wegert->surface);
    wegert->dirty = false;
}

static float pointer_distance(AInputEvent *event)
{
    float x0 = AMotionEvent_getX(event, 0);
    float y0 = AMotionEvent_getY(event, 0);
    float x1 = AMotionEvent_getX(event, 1);
    float y1 = AMotionEvent_getY(event, 1);

    float dx = x1 - x0;
    float dy = y1 - y0;

    return sqrtf(dx * dx + dy * dy);
}

static int32_t handle_input(
    struct android_app *android,
    AInputEvent *event
)
{
    Wegert *wegert = android->userData;

    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
        return 0;

    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    size_t pointers = AMotionEvent_getPointerCount(event);

    if (action == AMOTION_EVENT_ACTION_DOWN) {
        wegert->touching = true;
        wegert->last_x = AMotionEvent_getX(event, 0);
        wegert->last_y = AMotionEvent_getY(event, 0);
        wegert->last_pinch_distance = 0.0f;
        return 1;
    }

    if (action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        if (pointers >= 2)
            wegert->last_pinch_distance = pointer_distance(event);

        return 1;
    }

    if (action == AMOTION_EVENT_ACTION_POINTER_UP) {
        size_t lifted = (size_t) (
            (AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
            >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT
        );

        /* Seed the next one-finger drag from a pointer that remains down. */
        for (size_t index = 0; index < pointers; index++) {
            if (index == lifted)
                continue;
            wegert->last_x = AMotionEvent_getX(event, index);
            wegert->last_y = AMotionEvent_getY(event, index);
            break;
        }

        wegert->last_pinch_distance = 0.0f;
        return 1;
    }

    if (action == AMOTION_EVENT_ACTION_MOVE) {
        if (pointers >= 2) {
            float distance = pointer_distance(event);

            if (wegert->last_pinch_distance > 0.0f && distance > 0.0f) {
                /* Same camera scale as tablet-touch-game, driven by pinch. */
                float scale = wegert->last_pinch_distance / distance;
                wegert->y_half_extent = clampf(
                    wegert->y_half_extent * scale,
                    MIN_Y_HALF_EXTENT,
                    MAX_Y_HALF_EXTENT
                );
                wegert->dirty = true;
            }

            wegert->last_pinch_distance = distance;
            return 1;
        }

        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);
        float dx = x - wegert->last_x;
        float dy = y - wegert->last_y;

        if (wegert->height <= 0)
            return 1;

        /* Android touch y grows downward; the complex imaginary axis grows up. */
        float units_per_pixel =
            2.0f * wegert->y_half_extent / (float) wegert->height;

        wegert->center_re -= dx * units_per_pixel;
        wegert->center_im += dy * units_per_pixel;

        wegert->last_x = x;
        wegert->last_y = y;
        wegert->dirty = true;

        return 1;
    }

    if (action == AMOTION_EVENT_ACTION_UP ||
        action == AMOTION_EVENT_ACTION_CANCEL) {
        wegert->touching = false;
        wegert->last_pinch_distance = 0.0f;
        return 1;
    }

    return 0;
}

static void refresh_surface_size(Wegert *wegert)
{
    if (!wegert->ready)
        return;

    eglQuerySurface(
        wegert->display,
        wegert->surface,
        EGL_WIDTH,
        &wegert->width
    );
    eglQuerySurface(
        wegert->display,
        wegert->surface,
        EGL_HEIGHT,
        &wegert->height
    );
    wegert->dirty = true;
}

static void handle_command(
    struct android_app *android,
    int32_t command
)
{
    Wegert *wegert = android->userData;

    switch (command) {
    case APP_CMD_INIT_WINDOW:
        if (android->window != NULL) {
            if (wegert->ready)
                stop_graphics(wegert);
            if (!start_graphics(wegert))
                LOG("failed to start OpenGL ES 3 graphics");
        }
        break;

    case APP_CMD_WINDOW_RESIZED:
        refresh_surface_size(wegert);
        break;

    case APP_CMD_TERM_WINDOW:
        stop_graphics(wegert);
        break;
    }
}

void android_main(struct android_app *android)
{
    app_dummy();

    Wegert wegert;
    memset(&wegert, 0, sizeof wegert);

    wegert.android = android;
    wegert.display = EGL_NO_DISPLAY;
    wegert.surface = EGL_NO_SURFACE;
    wegert.context = EGL_NO_CONTEXT;

    /* Camera matches tablet-touch-game: center 0, visible imaginary +/- 2.5. */
    wegert.center_re = 0.0f;
    wegert.center_im = 0.0f;
    wegert.y_half_extent = INITIAL_Y_HALF_EXTENT;

    /* Keep the native prototype's visible demonstration f(z) = z. */
    wegert.zero_count = 1;
    wegert.zeros[0][0] = 0.0f;
    wegert.zeros[0][1] = 0.0f;

    wegert.phase_epoch_seconds = monotonic_seconds();

    android->userData = &wegert;
    android->onAppCmd = handle_command;
    android->onInputEvent = handle_input;

    for (;;) {
        int events;
        struct android_poll_source *source;

        /* A live surface continuously redraws for the six-second phase cycle. */
        int timeout = wegert.ready ? 0 : -1;

        while (ALooper_pollOnce(
            timeout,
            NULL,
            &events,
            (void **) &source
        ) >= 0) {
            if (source != NULL)
                source->process(android, source);

            if (android->destroyRequested) {
                stop_graphics(&wegert);
                return;
            }

            timeout = 0;
        }

        if (wegert.ready)
            draw(&wegert);
    }
}
