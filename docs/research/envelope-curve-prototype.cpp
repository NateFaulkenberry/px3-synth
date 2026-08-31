// Envelope segment curve prototype.
//
// Settles the pivotal question the brief leaves open (§12: "Do not assume
// Bezier is automatically superior") by measuring the three candidates on the
// properties that actually matter for an audio envelope: does it stay bounded
// and monotone at extreme settings, is it symmetric, and what does it cost per
// sample on the audio thread.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static constexpr double kPi = 3.14159265358979323846;

// --------------------------------------------------------------- candidates --

// 1. POWER. y = x^p. The obvious first idea and what many editors ship.
static inline double power(double x, double curve)
{
    // curve -1..1 -> exponent 1/8 .. 8
    const double p = std::pow(8.0, -curve);
    return std::pow(x, p);
}

// 2. RATIONAL (Mobius). y = x / (x + r(1-x)).
// One divide, unconditionally monotone on x in [0,1] for any r > 0, and exactly
// symmetric: r and 1/r are mirror images of each other.
static inline double rational(double x, double curve)
{
    const double r = std::exp(-3.0 * curve);
    return x / (x + r * (1.0 - x));
}

// 3. CUBIC BEZIER with coincident control points, which is what constrains the
// curve to a single-valued function of x. The catch is that x is then a CUBIC in
// t, so evaluating y at a given x needs the cubic inverted first - per sample.
static inline double bezier(double x, double curve)
{
    // Control point y from the curve amount; x fixed at the midpoint so the
    // segment cannot double back.
    const double cy = 0.5 + 0.5 * curve;
    const double cx = 0.5;

    // Invert x(t) = 3(1-t)^2 t cx + 3(1-t) t^2 cx + t^3 by bisection. Newton
    // converges faster but needs a guard for the degenerate case, and this is
    // being measured as the honest cost of the approach.
    double lo = 0.0, hi = 1.0, t = x;
    for (int i = 0; i < 20; ++i)
    {
        t = 0.5 * (lo + hi);
        const double u = 1.0 - t;
        const double xt = 3.0 * u * u * t * cx + 3.0 * u * t * t * cx + t * t * t;
        if (xt < x) { lo = t; } else { hi = t; }
    }

    const double u = 1.0 - t;
    return 3.0 * u * u * t * cy + 3.0 * u * t * t * cy + t * t * t;
}

// ------------------------------------------------------------- measurements --
struct Report
{
    const char* name;
    bool finite { true };
    bool monotone { true };
    bool bounded { true };
    double worstSymmetryError { 0.0 };
    double nsPerSample { 0.0 };
};

template <typename Fn>
static Report measure(const char* name, Fn fn)
{
    Report report;
    report.name = name;

    // Extremes included deliberately: an envelope editor WILL be dragged to its
    // stops, and a curve that returns NaN there is a silent voice or a stuck
    // one.
    const double curves[] = { -1.0, -0.999, -0.75, -0.5, -0.25, 0.0,
                              0.25, 0.5, 0.75, 0.999, 1.0 };

    for (const auto curve : curves)
    {
        double previous = -1.0;
        for (int i = 0; i <= 2000; ++i)
        {
            const double x = static_cast<double>(i) / 2000.0;
            const double y = fn(x, curve);

            if (! std::isfinite(y)) { report.finite = false; continue; }
            if (y < -1e-9 || y > 1.0 + 1e-9) { report.bounded = false; }
            if (y < previous - 1e-9) { report.monotone = false; }
            previous = y;
        }

        // Symmetry: bending a segment up by c and down by c should give shapes
        // that are mirror images through the diagonal. Without it, "up 50%" and
        // "down 50%" are different amounts of bend and the control feels wrong.
        for (int i = 0; i <= 100; ++i)
        {
            const double x = static_cast<double>(i) / 100.0;
            const double up = fn(x, curve);
            const double down = fn(x, -curve);
            // Mirror of y=f(x,-c) through the diagonal is x = f(y,-c), so
            // f(f(x,c), -c) should be x.
            const double roundTrip = fn(up, -curve);
            if (std::isfinite(roundTrip))
            {
                report.worstSymmetryError = std::max(report.worstSymmetryError,
                                                     std::abs(roundTrip - x));
            }
            (void) down;
        }
    }

    // Cost, on the shape of work the audio thread actually does.
    volatile double sink = 0.0;
    const auto start = std::chrono::high_resolution_clock::now();
    constexpr int iterations = 2000000;
    for (int i = 0; i < iterations; ++i)
    {
        const double x = static_cast<double>(i % 1000) / 1000.0;
        sink = sink + fn(x, 0.6);
    }
    const auto end = std::chrono::high_resolution_clock::now();
    report.nsPerSample = std::chrono::duration<double, std::nano>(end - start).count() / iterations;

    return report;
}

// Does the power curve actually break at wide exponents, or is that received
// wisdom? Tested rather than repeated.
static void powerRangeCheck()
{
    std::printf("\n  Power curve as the exponent range widens:\n");
    std::printf("    %-12s %10s %10s %12s\n", "range", "finite", "monotone", "worst step");
    for (const double span : { 8.0, 64.0, 512.0, 4096.0, 65536.0 })
    {
        bool finite = true, monotone = true;
        double worstStep = 0.0;
        for (const double curve : { -1.0, -0.5, 0.5, 1.0 })
        {
            const double p = std::pow(span, -curve);
            double previous = 0.0;
            for (int i = 0; i <= 4000; ++i)
            {
                const double x = static_cast<double>(i) / 4000.0;
                const double y = std::pow(x, p);
                if (! std::isfinite(y)) { finite = false; continue; }
                if (y < previous - 1e-9) { monotone = false; }
                // A curve that leaps between adjacent samples is a click, even
                // if every value in it is finite and in order.
                if (i > 0) { worstStep = std::max(worstStep, std::abs(y - previous)); }
                previous = y;
            }
        }
        std::printf("    1/%-10.0f %10s %10s %12.4f\n", span,
                    finite ? "yes" : "NO", monotone ? "yes" : "NO", worstStep);
    }
}

int main()
{
    powerRangeCheck();
    std::printf("\nENVELOPE SEGMENT CURVE PROTOTYPE\n\n");
    std::printf("  %-10s %8s %9s %8s %14s %12s\n",
                "curve", "finite", "monotone", "bounded", "symmetry err", "ns/sample");

    const Report reports[] = {
        measure("power", power),
        measure("rational", rational),
        measure("bezier", bezier),
    };

    for (const auto& r : reports)
    {
        std::printf("  %-10s %8s %9s %8s %14.2e %12.2f\n",
                    r.name,
                    r.finite ? "yes" : "NO",
                    r.monotone ? "yes" : "NO",
                    r.bounded ? "yes" : "NO",
                    r.worstSymmetryError,
                    r.nsPerSample);
    }

    // What the shapes actually look like, so the choice is not made on
    // properties alone - a curve can be perfectly behaved and useless.
    std::printf("\n  Shape at curve = +0.6 (values at x = 0, .25, .5, .75, 1):\n");
    for (const auto& pair : { std::pair<const char*, double (*)(double, double)> { "power", power },
                              { "rational", rational },
                              { "bezier", bezier } })
    {
        std::printf("    %-10s", pair.first);
        for (const double x : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            std::printf(" %6.4f", pair.second(x, 0.6));
        }
        std::printf("\n");
    }

    // The one that decides it: how far does each stray from a musically
    // reasonable reference at the SAME nominal curve amount? Compared against
    // the rational curve, which is the proposal.
    std::printf("\n  Worst deviation from the rational curve at matched amounts:\n");
    for (const auto& pair : { std::pair<const char*, double (*)(double, double)> { "power", power },
                              { "bezier", bezier } })
    {
        double worst = 0.0;
        for (const double curve : { -0.9, -0.6, -0.3, 0.3, 0.6, 0.9 })
        {
            for (int i = 0; i <= 200; ++i)
            {
                const double x = static_cast<double>(i) / 200.0;
                const double d = std::abs(pair.second(x, curve) - rational(x, curve));
                if (std::isfinite(d)) { worst = std::max(worst, d); }
            }
        }
        std::printf("    %-10s %6.4f\n", pair.first, worst);
    }

    std::printf("\n");
    return 0;
}
