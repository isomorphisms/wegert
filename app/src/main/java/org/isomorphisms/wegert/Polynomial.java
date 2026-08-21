package org.isomorphisms.wegert;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

final class Polynomial {
    static final int MAX_DEGREE = 12;
    private static final double EPS = 1e-10;

    private final double[] coefficient; // ascending powers of z

    private Polynomial(double[] values) {
        coefficient = trim(values);
    }

    static Polynomial parse(String source) {
        Parser parser = new Parser(source);
        Polynomial result = parser.parseSum();
        parser.expect(TokenType.END);
        return result;
    }

    double[] coefficients() {
        return Arrays.copyOf(coefficient, coefficient.length);
    }

    int degree() {
        if (coefficient.length == 1 && Math.abs(coefficient[0]) < EPS) return 0;
        return coefficient.length - 1;
    }

    String expanded() {
        StringBuilder out = new StringBuilder();
        for (int power = coefficient.length - 1; power >= 0; power--) {
            double value = coefficient[power];
            if (Math.abs(value) < EPS) continue;

            boolean first = out.length() == 0;
            if (!first) out.append(value < 0 ? " - " : " + ");
            else if (value < 0) out.append('-');

            double magnitude = Math.abs(value);
            if (power == 0 || Math.abs(magnitude - 1.0) > EPS) {
                out.append(number(magnitude));
                if (power > 0) out.append(' ');
            }
            if (power > 0) {
                out.append('z');
                if (power > 1) out.append('^').append(power);
            }
        }
        return out.length() == 0 ? "0" : out.toString();
    }

    String zeros() {
        int n = degree();
        if (n == 0) return "none";

        boolean monomial = true;
        for (int i = 0; i < n; i++) {
            if (Math.abs(coefficient[i]) > EPS) {
                monomial = false;
                break;
            }
        }
        if (monomial) {
            if (n == 1) return "0";
            return "0 (multiplicity " + n + ")";
        }

        Complex[] roots = durandKerner();
        Arrays.sort(roots, Comparator
                .comparingDouble((Complex z) -> z.re)
                .thenComparingDouble(z -> z.im));

        List<String> rendered = new ArrayList<>();
        boolean[] used = new boolean[roots.length];
        for (int i = 0; i < roots.length; i++) {
            if (used[i]) continue;
            int multiplicity = 1;
            used[i] = true;
            for (int j = i + 1; j < roots.length; j++) {
                if (!used[j] && roots[i].distance(roots[j]) < 2e-4) {
                    used[j] = true;
                    multiplicity++;
                }
            }
            String root = roots[i].render();
            rendered.add(multiplicity == 1 ? root : root + " (multiplicity " + multiplicity + ")");
        }
        return String.join(",  ", rendered);
    }

    private Complex[] durandKerner() {
        int n = degree();
        double lead = coefficient[n];
        double radius = 1.0;
        for (int i = 0; i < n; i++) {
            radius = Math.max(radius, 1.0 + Math.abs(coefficient[i] / lead));
        }

        Complex[] roots = new Complex[n];
        double phaseOffset = 0.271828;
        for (int k = 0; k < n; k++) {
            double angle = phaseOffset + 2.0 * Math.PI * k / n;
            roots[k] = Complex.polar(radius, angle);
        }

        for (int iteration = 0; iteration < 160; iteration++) {
            double maxChange = 0.0;
            Complex[] next = new Complex[n];
            for (int i = 0; i < n; i++) {
                Complex denominator = Complex.ONE;
                for (int j = 0; j < n; j++) {
                    if (i != j) denominator = denominator.mul(roots[i].sub(roots[j]));
                }
                if (denominator.abs() < 1e-18) {
                    roots[i] = roots[i].add(new Complex(1e-6 * (i + 1), -1e-6 * (i + 2)));
                    denominator = new Complex(1e-18, 0.0);
                }
                Complex delta = evaluate(roots[i]).div(denominator).div(new Complex(lead, 0.0));
                next[i] = roots[i].sub(delta);
                maxChange = Math.max(maxChange, delta.abs());
            }
            roots = next;
            if (maxChange < 1e-12) break;
        }
        return roots;
    }

    private Complex evaluate(Complex z) {
        Complex value = new Complex(coefficient[coefficient.length - 1], 0.0);
        for (int i = coefficient.length - 2; i >= 0; i--) {
            value = value.mul(z).add(new Complex(coefficient[i], 0.0));
        }
        return value;
    }

    private static Polynomial constant(double value) {
        return new Polynomial(new double[]{value});
    }

    private static Polynomial variable() {
        return new Polynomial(new double[]{0.0, 1.0});
    }

    private Polynomial add(Polynomial other) {
        int size = Math.max(coefficient.length, other.coefficient.length);
        double[] result = new double[size];
        for (int i = 0; i < size; i++) {
            result[i] = valueAt(i) + other.valueAt(i);
        }
        return new Polynomial(result);
    }

    private Polynomial sub(Polynomial other) {
        int size = Math.max(coefficient.length, other.coefficient.length);
        double[] result = new double[size];
        for (int i = 0; i < size; i++) {
            result[i] = valueAt(i) - other.valueAt(i);
        }
        return new Polynomial(result);
    }

    private Polynomial negate() {
        double[] result = new double[coefficient.length];
        for (int i = 0; i < coefficient.length; i++) result[i] = -coefficient[i];
        return new Polynomial(result);
    }

    private Polynomial mul(Polynomial other) {
        int degree = degree() + other.degree();
        if (degree > MAX_DEGREE) throw new IllegalArgumentException("degree exceeds " + MAX_DEGREE);
        double[] result = new double[degree + 1];
        for (int i = 0; i < coefficient.length; i++) {
            for (int j = 0; j < other.coefficient.length; j++) {
                result[i + j] += coefficient[i] * other.coefficient[j];
            }
        }
        return new Polynomial(result);
    }

    private Polynomial pow(int exponent) {
        if (exponent < 0) throw new IllegalArgumentException("negative exponents are not polynomials");
        Polynomial result = constant(1.0);
        Polynomial base = this;
        int e = exponent;
        while (e > 0) {
            if ((e & 1) == 1) result = result.mul(base);
            e >>= 1;
            if (e > 0) base = base.mul(base);
        }
        return result;
    }

    private double valueAt(int power) {
        return power < coefficient.length ? coefficient[power] : 0.0;
    }

    private static double[] trim(double[] input) {
        int last = input.length - 1;
        while (last > 0 && Math.abs(input[last]) < EPS) last--;
        return Arrays.copyOf(input, last + 1);
    }

    private static String number(double value) {
        double rounded = Math.rint(value);
        if (Math.abs(value - rounded) < 1e-9) return Long.toString((long) rounded);
        String text = String.format(Locale.US, "%.6f", value);
        return text.replaceFirst("0+$", "").replaceFirst("\\.$", "");
    }

    private enum TokenType { NUMBER, Z, PLUS, MINUS, STAR, CARET, LPAREN, RPAREN, END }

    private static final class Token {
        final TokenType type;
        final double number;
        Token(TokenType type) { this(type, Double.NaN); }
        Token(TokenType type, double number) { this.type = type; this.number = number; }
    }

    private static final class Lexer {
        private final String text;
        private int at;

        Lexer(String text) {
            this.text = text.replace('−', '-').replace('×', '*');
        }

        Token next() {
            while (at < text.length() && Character.isWhitespace(text.charAt(at))) at++;
            if (at >= text.length()) return new Token(TokenType.END);
            char c = text.charAt(at++);
            switch (c) {
                case 'z': case 'Z': return new Token(TokenType.Z);
                case '+': return new Token(TokenType.PLUS);
                case '-': return new Token(TokenType.MINUS);
                case '*': return new Token(TokenType.STAR);
                case '^': return new Token(TokenType.CARET);
                case '(': return new Token(TokenType.LPAREN);
                case ')': return new Token(TokenType.RPAREN);
                default:
                    if (Character.isDigit(c) || c == '.') {
                        int start = at - 1;
                        while (at < text.length()) {
                            char d = text.charAt(at);
                            if (!(Character.isDigit(d) || d == '.')) break;
                            at++;
                        }
                        try {
                            return new Token(TokenType.NUMBER, Double.parseDouble(text.substring(start, at)));
                        } catch (NumberFormatException bad) {
                            throw new IllegalArgumentException("bad number near position " + start);
                        }
                    }
                    throw new IllegalArgumentException("unexpected '" + c + "'");
            }
        }
    }

    private static final class Parser {
        private final Lexer lexer;
        private Token token;

        Parser(String source) {
            lexer = new Lexer(source);
            token = lexer.next();
        }

        Polynomial parseSum() {
            Polynomial value = parseProduct();
            while (token.type == TokenType.PLUS || token.type == TokenType.MINUS) {
                TokenType operation = token.type;
                advance();
                Polynomial right = parseProduct();
                value = operation == TokenType.PLUS ? value.add(right) : value.sub(right);
            }
            return value;
        }

        private Polynomial parseProduct() {
            Polynomial value = parseUnary();
            while (token.type == TokenType.STAR || beginsImplicitFactor(token.type)) {
                if (token.type == TokenType.STAR) advance();
                value = value.mul(parseUnary());
            }
            return value;
        }

        private Polynomial parseUnary() {
            if (token.type == TokenType.PLUS) {
                advance();
                return parseUnary();
            }
            if (token.type == TokenType.MINUS) {
                advance();
                return parseUnary().negate();
            }
            return parsePower();
        }

        private Polynomial parsePower() {
            Polynomial base = parsePrimary();
            if (token.type == TokenType.CARET) {
                advance();
                if (token.type != TokenType.NUMBER || token.number < 0 || Math.rint(token.number) != token.number) {
                    throw new IllegalArgumentException("exponent must be a nonnegative integer");
                }
                int exponent = (int) token.number;
                advance();
                return base.pow(exponent);
            }
            return base;
        }

        private Polynomial parsePrimary() {
            if (token.type == TokenType.NUMBER) {
                double value = token.number;
                advance();
                return constant(value);
            }
            if (token.type == TokenType.Z) {
                advance();
                return variable();
            }
            if (token.type == TokenType.LPAREN) {
                advance();
                Polynomial value = parseSum();
                expect(TokenType.RPAREN);
                return value;
            }
            throw new IllegalArgumentException("expected a number, z, or parenthesized polynomial");
        }

        void expect(TokenType expected) {
            if (token.type != expected) throw new IllegalArgumentException("unexpected input");
            advance();
        }

        private void advance() { token = lexer.next(); }

        private static boolean beginsImplicitFactor(TokenType type) {
            return type == TokenType.NUMBER || type == TokenType.Z || type == TokenType.LPAREN;
        }
    }

    private static final class Complex {
        static final Complex ONE = new Complex(1.0, 0.0);
        final double re;
        final double im;

        Complex(double re, double im) { this.re = re; this.im = im; }
        static Complex polar(double radius, double angle) {
            return new Complex(radius * Math.cos(angle), radius * Math.sin(angle));
        }
        Complex add(Complex other) { return new Complex(re + other.re, im + other.im); }
        Complex sub(Complex other) { return new Complex(re - other.re, im - other.im); }
        Complex mul(Complex other) {
            return new Complex(re * other.re - im * other.im, re * other.im + im * other.re);
        }
        Complex div(Complex other) {
            double d = other.re * other.re + other.im * other.im;
            return new Complex((re * other.re + im * other.im) / d,
                    (im * other.re - re * other.im) / d);
        }
        double abs() { return Math.hypot(re, im); }
        double distance(Complex other) { return Math.hypot(re - other.re, im - other.im); }

        String render() {
            double r = Math.abs(re) < 5e-7 ? 0.0 : re;
            double i = Math.abs(im) < 5e-7 ? 0.0 : im;
            if (i == 0.0) return number(r);
            if (r == 0.0) {
                if (Math.abs(i - 1.0) < 1e-7) return "i";
                if (Math.abs(i + 1.0) < 1e-7) return "-i";
                return number(i) + "i";
            }
            return number(r) + (i < 0 ? " - " : " + ") + number(Math.abs(i)) + "i";
        }
    }
}
