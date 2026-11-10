// Standalone boundary checks against the actual runtime prefilter header.
// Does not link Unreal or start the editor/game.
#include "VerdantDialoguePolicy.h"
#include <iostream>
#include <limits>

int main()
{
    using VerdantDialoguePolicy::Eligible;
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    int Checked = 0;
    int Failed = 0;
    const auto Check = [&](const char* Name, bool Actual, bool Expected)
    {
        ++Checked;
        if (Actual != Expected)
        {
            ++Failed;
            std::cerr << "FAIL " << Name << '\n';
        }
    };

    Check("near_equal_height", Eligible(100.0 * 100.0, 81, 81), true);
    Check("old_twenty_metre_boundary_remains_eligible", Eligible(2000.0 * 2000.0, 81, 81), true);
    Check("just_beyond_old_twenty_metre_boundary_now_eligible", Eligible(2000.001 * 2000.001, 81, 81), true);
    Check("thirty_metres_now_eligible", Eligible(3000.0 * 3000.0, 81, 81), true);
    Check("forty_metres_inclusive", Eligible(4000.0 * 4000.0, 81, 81), true);
    Check("just_outside_forty_metres", Eligible(4000.001 * 4000.001, 81, 81), false);
    Check("custom_twenty_metres_inclusive", Eligible(2000.0 * 2000.0, 81, 81, 2000), true);
    Check("custom_twenty_metres_excludes_just_outside", Eligible(2000.001 * 2000.001, 81, 81, 2000), false);
    Check("custom_twenty_metres_excludes_thirty", Eligible(3000.0 * 3000.0, 81, 81, 2000), false);
    Check("twice_height_inclusive", Eligible(100, 81, 162), true);
    Check("twice_height_symmetric", Eligible(100, 162, 81), true);
    Check("just_over_old_twice_height_now_eligible", Eligible(100, 81, 162.001), true);
    Check("just_over_old_twice_height_reversed_now_eligible", Eligible(100, 162.001, 81), true);
    Check("five_times_height_inclusive", Eligible(100, 81, 405), true);
    Check("five_times_height_symmetric", Eligible(100, 405, 81), true);
    Check("just_over_five_times_height", Eligible(100, 81, 405.001), false);
    Check("just_over_five_times_height_reversed", Eligible(100, 405.001, 81), false);
    Check("forty_metres_and_five_times_height_inclusive", Eligible(4000.0 * 4000.0, 81, 405), true);
    Check("five_times_height_but_just_beyond_forty_metres", Eligible(4000.001 * 4000.001, 81, 405), false);
    Check("forty_metres_but_just_over_five_times_height", Eligible(4000.0 * 4000.0, 81, 405.001), false);
    Check("same_position_distinct_actors", Eligible(0, 81, 81), true);
    Check("negative_distance_squared", Eligible(-1, 81, 81), false);
    Check("nan_distance", Eligible(NaN, 81, 81), false);
    Check("infinite_distance", Eligible(Infinity, 81, 81), false);
    Check("zero_height", Eligible(100, 0, 81), false);
    Check("negative_height", Eligible(100, 81, -1), false);
    Check("nan_height", Eligible(100, NaN, 81), false);
    Check("infinite_height", Eligible(100, 81, Infinity), false);
    Check("zero_radius", Eligible(0, 81, 81, 0), false);
    Check("negative_radius", Eligible(0, 81, 81, -100), false);
    Check("nan_radius", Eligible(0, 81, 81, NaN), false);
    Check("infinite_radius", Eligible(0, 81, 81, Infinity), false);
    Check("invalid_ratio", Eligible(0, 81, 81, 2000, .9), false);
    Check("nan_ratio", Eligible(0, 81, 81, 2000, NaN), false);
    Check("infinite_ratio", Eligible(0, 81, 81, 2000, Infinity), false);
    Check("custom_tighter_distance", Eligible(501.0 * 501.0, 81, 81, 500, 2), false);
    Check("custom_tighter_ratio", Eligible(100, 100, 151, 2000, 1.5), false);
    Check("custom_limits_inclusive", Eligible(500.0 * 500.0, 100, 150, 500, 1.5), true);

    std::cout << "{\"checked\":" << Checked << ",\"failed\":" << Failed << "}\n";
    return Failed ? 1 : 0;
}
