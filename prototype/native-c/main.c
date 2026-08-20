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

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "wegert", __VA_ARGS__)

typedef struct {
    struct android_app *android;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;

    GLuint program;
    GLuint vao;

    int width;
    int height;

    /* Mathematical domain. */
    float center_re;
    float center_im;
    float units_per_pixel;

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
    "uniform vec2  center;\n"
    "uniform vec2  viewport;\n"
    "uniform float units_per_pixel;\n"
    "\n"
    "out vec4 output_color;\n"
    "\n"
    "/* Temporary visualization only. Replace this function with\n"
    " * the existing Wegert Lab/saturation/log-lightness shader. */\n"
    "vec3 wegert_color(vec2 z) {\n"
    "    float argument = atan(z.y, z.x);\n"
    "    float radius = max(length(z), 0.000001);\n"
    "\n"
    "    vec3 c = 0.5 + 0.5 * cos(\n"
    "        argument + vec3(0.0, 2.094395, 4.188790)\n"
    "    );\n"
    "\n"
    "    float rings = 0.75 + 0.25 *\n"
    "        cos(6.283185 * log2(radius));\n"
    "\n"
    "    return c * rings;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec2 pixel = gl_FragCoord.xy - viewport * 0.5;\n"
    "    vec2 z = center + pixel * units_per_pixel;\n"
    "    output_color = vec4(wegert_color(z), 1.0);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        char message[2048];
        glGetShaderInfoLog(shader, sizeof message, NULL, message);
        LOG("shader error: %s", message);
    }

    return shader;
}

static GLuint make_program(void)
{
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    GLuint program = glCreateProgram();

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

static void start_graphics(Wegert *wegert)
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
    eglInitialize(wegert->display, NULL, NULL);

    EGLConfig config;
    EGLint config_count;

    eglChooseConfig(
        wegert->display,
        config_attributes,
        &config,
        1,
        &config_count
    );

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

    eglMakeCurrent(
        wegert->display,
        wegert->surface,
        wegert->surface,
        wegert->context
    );

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
    glGenVertexArrays(1, &wegert->vao);

    wegert->ready = true;
    wegert->dirty = true;
}

static void draw(Wegert *wegert)
{
    if (!wegert->ready)
        return;

    glViewport(0, 0, wegert->width, wegert->height);

    glUseProgram(wegert->program);
    glBindVertexArray(wegert->vao);

    GLint center_location = glGetUniformLocation(wegert->program, "center");
    GLint viewport_location = glGetUniformLocation(wegert->program, "viewport");
    GLint scale_location = glGetUniformLocation(
        wegert->program,
        "units_per_pixel"
    );

    glUniform2f(
        center_location,
        wegert->center_re,
        wegert->center_im
    );

    glUniform2f(
        viewport_location,
        (float) wegert->width,
        (float) wegert->height
    );

    glUniform1f(scale_location, wegert->units_per_pixel);

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

    if (action == AMOTION_EVENT_ACTION_MOVE) {
        if (pointers >= 2) {
            float distance = pointer_distance(event);

            if (wegert->last_pinch_distance > 0.0f && distance > 0.0f) {
                /* Spread fingers -> smaller units/pixel -> zoom in. */
                wegert->units_per_pixel *=
                    wegert->last_pinch_distance / distance;
                wegert->dirty = true;
            }

            wegert->last_pinch_distance = distance;
            return 1;
        }

        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);
        float dx = x - wegert->last_x;
        float dy = y - wegert->last_y;

        wegert->center_re -= dx * wegert->units_per_pixel;
        wegert->center_im += dy * wegert->units_per_pixel;

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

static void handle_command(
    struct android_app *android,
    int32_t command
)
{
    Wegert *wegert = android->userData;

    switch (command) {
    case APP_CMD_INIT_WINDOW:
        if (android->window != NULL)
            start_graphics(wegert);
        break;

    case APP_CMD_WINDOW_RESIZED:
        if (wegert->ready) {
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
        break;
    }
}

void android_main(struct android_app *android)
{
    app_dummy();

    Wegert wegert;
    memset(&wegert, 0, sizeof wegert);

    wegert.android = android;

    /* Start centered on ordinary complex zero. */
    wegert.center_re = 0.0f;
    wegert.center_im = 0.0f;
    wegert.units_per_pixel = 0.004f;

    android->userData = &wegert;
    android->onAppCmd = handle_command;
    android->onInputEvent = handle_input;

    for (;;) {
        int events;
        struct android_poll_source *source;
        int timeout = wegert.dirty ? 0 : -1;

        while (ALooper_pollOnce(
            timeout,
            NULL,
            &events,
            (void **) &source
        ) >= 0) {
            if (source != NULL)
                source->process(android, source);

            if (android->destroyRequested)
                return;

            timeout = 0;
        }

        if (wegert.dirty)
            draw(&wegert);
    }
}
