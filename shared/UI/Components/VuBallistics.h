#pragma once

#include <algorithm>
#include <cmath>

namespace px3::ui
{

// The mechanical response of a moving-coil VU movement.
//
// Deliberately free of JUCE, of any component, and of any notion of drawing:
// it is a differential equation with a position and a velocity, and it can be
// stepped and asserted on without a GUI. That separation is the point - the
// previous meter had the detector and the ballistics collapsed into one
// one-pole smoother, which is a first-order system and therefore physically
// incapable of the overshoot the standard requires.
//
// Both constants are DERIVED from ANSI C16.5 / IEC 60268-17 rather than tuned
// by eye. See docs/VU_METER_IMPLEMENTATION.md for the derivation:
//
//   overshoot 1% to 1.5%     ->  damping ratio 0.8134
//   99% of full scale in 300 ms  ->  natural frequency 13.536 rad/s
//
// which together give 1.24% overshoot peaking at 399 ms - inside the permitted
// window by construction rather than by adjustment.
class VuBallistics
{
public:
    // The published specification, as constants rather than as magic numbers.
    static constexpr double kDampingRatio = 0.8134;
    static constexpr double kNaturalFrequency = 13.5357;   // rad/s

    // A frame longer than this is a stall - a debugger pause, a suspended DAW,
    // a window being dragged between displays - not a slow frame. Integrating
    // one would inject enormous energy and throw the needle off its stops, so
    // the step is capped and the simulation simply advances less than real
    // time for that frame.
    static constexpr double kMaxTimeStep = 0.100;
    // Above this, one step is no longer a good approximation of the curve, so
    // long frames are integrated as several short ones.
    static constexpr double kMaxSubStep = 0.004;

    void reset(double position = 0.0)
    {
        currentPosition = position;
        currentVelocity = 0.0;
    }

    // `target` and the position are both 0..1 across the meter's sweep; `dt` is
    // the REAL elapsed time since the last call, in seconds.
    void step(double target, double dt)
    {
        if (! (dt > 0.0))
        {
            return;
        }

        // Sub-stepped semi-implicit Euler. Velocity is updated from the force
        // at the current position and the position from the NEW velocity,
        // which is what makes the integrator stable for an oscillator - plain
        // explicit Euler gains energy every step and slowly diverges.
        auto remaining = std::min(dt, kMaxTimeStep);

        while (remaining > 0.0)
        {
            const auto h = std::min(remaining, kMaxSubStep);
            remaining -= h;

            const auto acceleration =
                kNaturalFrequency * kNaturalFrequency * (target - currentPosition)
                - 2.0 * kDampingRatio * kNaturalFrequency * currentVelocity;

            currentVelocity += acceleration * h;
            currentPosition += currentVelocity * h;
        }

        // The movement has physical stops. It may overshoot the target - that
        // is the whole point - but it cannot leave the scale.
        if (currentPosition < kLowerStop)
        {
            currentPosition = kLowerStop;
            currentVelocity = std::max(0.0, currentVelocity);
        }
        else if (currentPosition > kUpperStop)
        {
            currentPosition = kUpperStop;
            currentVelocity = std::min(0.0, currentVelocity);
        }
    }

    double position() const noexcept { return currentPosition; }
    double velocity() const noexcept { return currentVelocity; }

    // A little past each end of the scale, as a real movement has: the needle
    // can visibly touch its stop rather than stopping exactly on the last mark.
    static constexpr double kLowerStop = -0.02;
    static constexpr double kUpperStop = 1.02;

private:
    double currentPosition { 0.0 };
    double currentVelocity { 0.0 };
};

} // namespace px3::ui
