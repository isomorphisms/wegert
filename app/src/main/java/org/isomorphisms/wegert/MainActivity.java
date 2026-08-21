package org.isomorphisms.wegert;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.text.InputType;
import android.view.GestureDetector;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.Locale;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public final class MainActivity extends Activity {
    private WegertView portrait;
    private EditText expression;
    private TextView expanded;
    private TextView zeros;
    private TextView domain;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(8), dp(8), dp(8), dp(8));
        root.setBackgroundColor(Color.rgb(246, 246, 246));

        TextView title = text("Wegert", 20f);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        root.addView(title, matchWrap());

        HorizontalScrollView presetScroll = new HorizontalScrollView(this);
        presetScroll.setHorizontalScrollBarEnabled(false);
        LinearLayout presets = new LinearLayout(this);
        presets.setOrientation(LinearLayout.HORIZONTAL);
        addPreset(presets, "Parabola", "z^2");
        addPreset(presets, "Shifted parabola", "(z - 1)^2");
        addPreset(presets, "Cubic", "z^3 - 1");
        addPreset(presets, "Two zeros", "(z - 1)(z + 1)");
        presetScroll.addView(presets, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(presetScroll, matchWrap());

        LinearLayout inputRow = new LinearLayout(this);
        inputRow.setOrientation(LinearLayout.HORIZONTAL);
        inputRow.setGravity(Gravity.CENTER_VERTICAL);
        expression = new EditText(this);
        expression.setSingleLine(true);
        expression.setText("(z - 1)(z + 1)");
        expression.setTextSize(18f);
        expression.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        expression.setImeOptions(EditorInfo.IME_ACTION_DONE);
        inputRow.addView(expression, new LinearLayout.LayoutParams(0, dp(52), 1f));
        Button apply = button("Apply");
        apply.setOnClickListener(v -> applyExpression());
        inputRow.addView(apply);
        root.addView(inputRow, matchWrap());

        expression.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                applyExpression();
                return true;
            }
            return false;
        });

        expanded = infoBox();
        zeros = infoBox();
        root.addView(expanded, matchWrap());
        root.addView(zeros, matchWrap());

        portrait = new WegertView();
        root.addView(portrait, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        domain = text("", 12f);
        domain.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(domain, matchWrap());
        portrait.setDomainListener((centerX, centerY, halfHeight, aspect) -> runOnUiThread(() ->
                domain.setText(String.format(Locale.US,
                        "center %.3f %+.3fi   visible x: [%.3f, %.3f]   y: [%.3f, %.3f]",
                        centerX, centerY,
                        centerX - halfHeight * aspect, centerX + halfHeight * aspect,
                        centerY - halfHeight, centerY + halfHeight))));

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        controls.setGravity(Gravity.CENTER);
        Button zoomOut = button("−");
        Button center = button("Center 0");
        Button zoomIn = button("+");
        zoomOut.setOnClickListener(v -> portrait.zoom(1.35));
        zoomIn.setOnClickListener(v -> portrait.zoom(1.0 / 1.35));
        center.setOnClickListener(v -> portrait.resetDomain());
        controls.addView(zoomOut);
        controls.addView(center);
        controls.addView(zoomIn);
        root.addView(controls, matchWrap());

        setContentView(root);
        applyExpression();
    }

    @Override
    protected void onPause() {
        super.onPause();
        portrait.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (portrait != null) portrait.onResume();
    }

    private void addPreset(LinearLayout row, String name, String polynomial) {
        Button button = button(name);
        button.setOnClickListener(v -> {
            expression.setText(polynomial);
            expression.setSelection(expression.length());
            applyExpression();
        });
        row.addView(button);
    }

    private void applyExpression() {
        try {
            Polynomial polynomial = Polynomial.parse(expression.getText().toString());
            expanded.setText("Expanded:  " + polynomial.expanded());
            zeros.setText("Zeros:  " + polynomial.zeros());
            expanded.setTextColor(Color.rgb(20, 20, 20));
            zeros.setTextColor(Color.rgb(20, 20, 20));
            portrait.setPolynomial(polynomial.coefficients());
        } catch (IllegalArgumentException error) {
            expanded.setText("Polynomial error: " + error.getMessage());
            zeros.setText("Syntax: z, real numbers, +, −, *, parentheses, and nonnegative integer powers");
            expanded.setTextColor(Color.rgb(150, 20, 20));
            zeros.setTextColor(Color.rgb(90, 90, 90));
        }
    }

    private TextView infoBox() {
        TextView view = text("", 15f);
        view.setPadding(dp(8), dp(5), dp(8), dp(5));
        view.setBackgroundColor(Color.rgb(232, 232, 232));
        return view;
    }

    private TextView text(String value, float size) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(Color.rgb(20, 20, 20));
        return view;
    }

    private Button button(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setMinHeight(dp(44));
        return button;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private interface DomainListener {
        void changed(double centerX, double centerY, double halfHeight, double aspect);
    }

    private final class WegertView extends GLSurfaceView implements GLSurfaceView.Renderer {
        private static final int MAX_DEGREE = Polynomial.MAX_DEGREE;
        private final float[] coefficients = new float[MAX_DEGREE + 1];
        private final GestureDetector dragDetector;
        private final ScaleGestureDetector scaleDetector;

        private volatile double centerX = 0.0;
        private volatile double centerY = 0.0;
        private volatile double halfHeight = 3.0;
        private volatile int polynomialDegree = 2;
        private volatile int viewWidth = 1;
        private volatile int viewHeight = 1;
        private DomainListener domainListener;
        private int program;
        private int uResolution;
        private int uCenter;
        private int uHalfHeight;
        private int uDegree;
        private int uCoefficient;

        WegertView() {
            super(MainActivity.this);
            setEGLContextClientVersion(3);
            setRenderer(this);
            setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
            setPreserveEGLContextOnPause(true);
            setBackgroundColor(Color.BLACK);

            dragDetector = new GestureDetector(MainActivity.this,
                    new GestureDetector.SimpleOnGestureListener() {
                        @Override
                        public boolean onDown(MotionEvent e) { return true; }

                        @Override
                        public boolean onScroll(MotionEvent e1, MotionEvent e2,
                                                float distanceX, float distanceY) {
                            if (scaleDetector.isInProgress()) return true;
                            double unitsPerPixel = (2.0 * halfHeight) / Math.max(1, viewHeight);
                            centerX += distanceX * unitsPerPixel;
                            centerY -= distanceY * unitsPerPixel;
                            requestRender();
                            announceDomain();
                            return true;
                        }
                    });

            scaleDetector = new ScaleGestureDetector(MainActivity.this,
                    new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                        @Override
                        public boolean onScale(ScaleGestureDetector detector) {
                            double newHalfHeight = halfHeight / detector.getScaleFactor();
                            halfHeight = Math.max(0.03, Math.min(1000.0, newHalfHeight));
                            requestRender();
                            announceDomain();
                            return true;
                        }
                    });
        }

        void setDomainListener(DomainListener listener) {
            domainListener = listener;
            announceDomain();
        }

        void setPolynomial(double[] values) {
            int last = Math.min(values.length - 1, MAX_DEGREE);
            for (int i = 0; i <= MAX_DEGREE; i++) coefficients[i] = 0f;
            for (int i = 0; i <= last; i++) coefficients[i] = (float) values[i];
            polynomialDegree = last;
            requestRender();
        }

        void zoom(double factor) {
            halfHeight = Math.max(0.03, Math.min(1000.0, halfHeight * factor));
            requestRender();
            announceDomain();
        }

        void resetDomain() {
            centerX = 0.0;
            centerY = 0.0;
            halfHeight = 3.0;
            requestRender();
            announceDomain();
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            scaleDetector.onTouchEvent(event);
            dragDetector.onTouchEvent(event);
            return true;
        }

        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            program = linkProgram(VERTEX_SHADER, FRAGMENT_SHADER);
            uResolution = GLES30.glGetUniformLocation(program, "uResolution");
            uCenter = GLES30.glGetUniformLocation(program, "uCenter");
            uHalfHeight = GLES30.glGetUniformLocation(program, "uHalfHeight");
            uDegree = GLES30.glGetUniformLocation(program, "uDegree");
            uCoefficient = GLES30.glGetUniformLocation(program, "uCoefficient[0]");
            GLES30.glClearColor(0f, 0f, 0f, 1f);
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            viewWidth = Math.max(1, width);
            viewHeight = Math.max(1, height);
            GLES30.glViewport(0, 0, width, height);
            announceDomain();
        }

        @Override
        public void onDrawFrame(GL10 gl) {
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT);
            GLES30.glUseProgram(program);
            GLES30.glUniform2f(uResolution, viewWidth, viewHeight);
            GLES30.glUniform2f(uCenter, (float) centerX, (float) centerY);
            GLES30.glUniform1f(uHalfHeight, (float) halfHeight);
            GLES30.glUniform1i(uDegree, polynomialDegree);
            GLES30.glUniform1fv(uCoefficient, MAX_DEGREE + 1, coefficients, 0);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3);
        }

        private void announceDomain() {
            DomainListener listener = domainListener;
            if (listener != null) {
                listener.changed(centerX, centerY, halfHeight,
                        (double) Math.max(1, viewWidth) / Math.max(1, viewHeight));
            }
        }

        private int linkProgram(String vertexSource, String fragmentSource) {
            int vertex = compileShader(GLES30.GL_VERTEX_SHADER, vertexSource);
            int fragment = compileShader(GLES30.GL_FRAGMENT_SHADER, fragmentSource);
            int result = GLES30.glCreateProgram();
            GLES30.glAttachShader(result, vertex);
            GLES30.glAttachShader(result, fragment);
            GLES30.glLinkProgram(result);
            int[] status = new int[1];
            GLES30.glGetProgramiv(result, GLES30.GL_LINK_STATUS, status, 0);
            if (status[0] == 0) {
                throw new IllegalStateException("OpenGL link failed: " + GLES30.glGetProgramInfoLog(result));
            }
            GLES30.glDeleteShader(vertex);
            GLES30.glDeleteShader(fragment);
            return result;
        }

        private int compileShader(int kind, String source) {
            int shader = GLES30.glCreateShader(kind);
            GLES30.glShaderSource(shader, source);
            GLES30.glCompileShader(shader);
            int[] status = new int[1];
            GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0);
            if (status[0] == 0) {
                throw new IllegalStateException("OpenGL shader failed: " + GLES30.glGetShaderInfoLog(shader));
            }
            return shader;
        }
    }

    private static final String VERTEX_SHADER =
            "#version 300 es\n" +
            "void main() {\n" +
            "  vec2 p;\n" +
            "  if (gl_VertexID == 0) p = vec2(-1.0, -1.0);\n" +
            "  else if (gl_VertexID == 1) p = vec2(3.0, -1.0);\n" +
            "  else p = vec2(-1.0, 3.0);\n" +
            "  gl_Position = vec4(p, 0.0, 1.0);\n" +
            "}\n";

    private static final String FRAGMENT_SHADER =
            "#version 300 es\n" +
            "precision highp float;\n" +
            "uniform vec2 uResolution;\n" +
            "uniform vec2 uCenter;\n" +
            "uniform float uHalfHeight;\n" +
            "uniform int uDegree;\n" +
            "uniform float uCoefficient[13];\n" +
            "out vec4 outColor;\n" +
            "const float PI = 3.14159265358979323846;\n" +
            "vec2 cmul(vec2 a, vec2 b) { return vec2(a.x*b.x-a.y*b.y, a.x*b.y+a.y*b.x); }\n" +
            "float srgb(float x) {\n" +
            "  x = max(0.0, x);\n" +
            "  return x <= 0.0031308 ? 12.92*x : 1.055*pow(x, 1.0/2.4)-0.055;\n" +
            "}\n" +
            "vec3 hcluvToSrgb(float hue, float chroma, float lightness) {\n" +
            "  float angle = radians(hue);\n" +
            "  float uStar = chroma*cos(angle);\n" +
            "  float vStar = chroma*sin(angle);\n" +
            "  const float Xn = 0.95047;\n" +
            "  const float Yn = 1.0;\n" +
            "  const float Zn = 1.08883;\n" +
            "  float denomN = Xn + 15.0*Yn + 3.0*Zn;\n" +
            "  float un = 4.0*Xn/denomN;\n" +
            "  float vn = 9.0*Yn/denomN;\n" +
            "  float L = clamp(lightness, 0.001, 100.0);\n" +
            "  float up = uStar/(13.0*L) + un;\n" +
            "  float vp = vStar/(13.0*L) + vn;\n" +
            "  float fy = (L + 16.0)/116.0;\n" +
            "  float Y = L > 8.0 ? fy*fy*fy : L/903.2962963;\n" +
            "  float safeV = max(vp, 1.0e-6);\n" +
            "  float X = 9.0*Y*up/(4.0*safeV);\n" +
            "  float Z = Y*(12.0 - 3.0*up - 20.0*vp)/(4.0*safeV);\n" +
            "  vec3 linearRgb = vec3(\n" +
            "      3.2404542*X - 1.5371385*Y - 0.4985314*Z,\n" +
            "     -0.9692660*X + 1.8760108*Y + 0.0415560*Z,\n" +
            "      0.0556434*X - 0.2040259*Y + 1.0572252*Z);\n" +
            "  return clamp(vec3(srgb(linearRgb.r), srgb(linearRgb.g), srgb(linearRgb.b)), 0.0, 1.0);\n" +
            "}\n" +
            "void main() {\n" +
            "  float aspect = uResolution.x/uResolution.y;\n" +
            "  vec2 normalized = gl_FragCoord.xy/uResolution - vec2(0.5);\n" +
            "  vec2 z = uCenter + vec2(normalized.x*2.0*uHalfHeight*aspect, normalized.y*2.0*uHalfHeight);\n" +
            "  vec2 value = vec2(0.0);\n" +
            "  for (int k = 12; k >= 0; --k) {\n" +
            "    if (k <= uDegree) value = cmul(value, z) + vec2(uCoefficient[k], 0.0);\n" +
            "  }\n" +
            "  float hue = mod(degrees(atan(value.y, value.x)) + 360.0, 360.0);\n" +
            "  float magnitude = max(length(value), 1.0e-30);\n" +
            "  float logarithmicRing = fract(log(magnitude)/log(10.0));\n" +
            "  float lightness = 66.0 + 4.0*logarithmicRing + 3.0*fract(hue/100.0);\n" +
            "  vec3 rgb = hcluvToSrgb(hue, 45.0, lightness);\n" +
            "  outColor = vec4(rgb, 1.0);\n" +
            "}\n";
}
