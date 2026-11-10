// Focused native checks against the production integration helper; no Unreal world.
#include "VerdantManualScaleRules.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

int main()
{
    using VerdantManualScale::NextSize;
    constexpr double Minimum = .1;
    constexpr double Maximum = 20.;
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    int Checked = 0;
    int Failed = 0;
    const auto Check = [&](const char* Name, double Actual, double Expected, double Tolerance = 1e-10)
    {
        ++Checked;
        if (!std::isfinite(Actual) || std::abs(Actual - Expected) > Tolerance)
        {
            ++Failed;
            std::cerr << Name << ": actual=" << Actual << " expected=" << Expected << '\n';
        }
    };
    const auto Advance = [&](double Current, int Direction, double Seconds)
    {
        return NextSize(Current, Direction, Seconds, Minimum, Maximum);
    };

    Check("one_second_growth", Advance(1, 1, 1), 1.5);
    Check("one_second_shrink", Advance(1, -1, 1), 1. / 1.5);
    Check("two_seconds_growth", Advance(1, 1, 2), 2.25);
    Check("two_seconds_shrink", Advance(1, -1, 2), 1. / 2.25);
    Check("half_second_growth", Advance(1, 1, .5), std::sqrt(1.5));
    Check("half_second_shrink", Advance(1, -1, .5), 1. / std::sqrt(1.5));
    Check("grow_then_shrink", Advance(Advance(1, 1, 1), -1, 1), 1.);
    Check("opposing_intent_cancels", Advance(1, 0, 4), 1.);
    Check("positive_direction_does_not_multiply_rate", Advance(1, 2, 1), 1.5);
    Check("negative_direction_does_not_multiply_rate", Advance(1, -2, 1), 1. / 1.5);

    double SplitTime = 1.;
    for (double Dt : {.013, .027, .1, .17, .2, .49}) SplitTime = Advance(SplitTime, 1, Dt);
    Check("irregular_frames_total_one_second", SplitTime, 1.5);
    double Released = Advance(1., 1, .75);
    const double AtRelease = Released;
    for (int Frame = 0; Frame < 120; ++Frame) Released = Advance(Released, 0, 1. / 60.);
    Check("release_stops_immediately", Released, AtRelease, 0.);

    std::cout << std::setprecision(10) << "{\"frame_rates\":[";
    bool First = true;
    for (int FPS : {30, 60, 144, 360})
    {
        float Grow = 1.f;
        float Shrink = 1.f;
        float Tiny = .1f;
        const float Dt = 1.f / FPS;
        for (int Frame = 0; Frame < FPS * 4; ++Frame)
        {
            Grow = static_cast<float>(Advance(Grow, 1, Dt));
            Shrink = static_cast<float>(Advance(Shrink, -1, Dt));
            if (Frame < FPS) Tiny = static_cast<float>(Advance(Tiny, 1, Dt));
        }
        Check("float_frames_growth_four_seconds", Grow, 5.0625, .0001);
        Check("float_frames_shrink_four_seconds", Shrink, 1. / 5.0625, .00001);
        Check("minimum_size_high_fps_still_changes", Tiny, .15, .000003);
        if (!First) std::cout << ',';
        First = false;
        std::cout << "{\"fps\":" << FPS << ",\"grow_after_4s\":" << Grow
                  << ",\"shrink_after_4s\":" << Shrink << ",\"tiny_after_1s\":" << Tiny << '}';
    }
    std::cout << ']';

    Check("upper_clamp", Advance(19., 1, 1), Maximum);
    Check("lower_clamp", Advance(.11, -1, 1), Minimum);
    Check("remain_at_upper_limit", Advance(Maximum, 1, 1), Maximum);
    Check("remain_at_lower_limit", Advance(Minimum, -1, 1), Minimum);
    Check("shrink_away_from_upper_limit", Advance(Maximum, -1, 1), Maximum / 1.5);
    Check("large_finite_dt_growth_clamps", Advance(1., 1, 100000), Maximum);
    Check("large_finite_dt_shrink_clamps", Advance(1., -1, 100000), Minimum);
    Check("zero_dt", Advance(1., 1, 0), 1.);
    Check("negative_dt", Advance(1., 1, -.1), 1.);
    Check("nan_dt", Advance(1., 1, NaN), 1.);
    Check("infinite_dt", Advance(1., 1, Infinity), 1.);
    Check("nan_minimum", NextSize(1., 1, 1., NaN, Maximum), 1.);
    Check("infinite_maximum", NextSize(1., 1, 1., Minimum, Infinity), 1.);
    Check("zero_minimum", NextSize(1., 1, 1., 0., Maximum), 1.);
    Check("negative_minimum", NextSize(1., 1, 1., -1., Maximum), 1.);
    Check("reversed_limits", NextSize(1., 1, 1., Maximum, Minimum), 1.);
    Check("locked_valid_limits", NextSize(3., 1, 1., 3., 3.), 3.);

    // Simulate SetSize rejecting expansion: always pass the unchanged real size,
    // never the rejected desired result. Unblocking must not replay missed growth.
    double ActualSize = .8;
    for (int Frame = 0; Frame < 120; ++Frame)
    {
        const double Rejected = Advance(ActualSize, 1, 1. / 60.);
        (void)Rejected;
    }
    Check("blocked_attempts_keep_actual_size", ActualSize, .8, 0.);
    ActualSize = Advance(ActualSize, 1, 1. / 60.);
    Check("unblocked_frame_has_no_accumulated_jump", ActualSize, .8 * std::pow(1.5, 1. / 60.));

    std::cout << ",\"checked\":" << Checked << ",\"failed\":" << Failed << "}\n";
    return Failed ? 1 : 0;
}
