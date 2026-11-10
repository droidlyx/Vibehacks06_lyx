#include "../../Source/Verdant/VerdantNPCScale.h"
#include "../../Source/Verdant/VerdantEconomyRules.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    using namespace VerdantNPCScale;
    assert(HeightAtLogQuantile(0.f) == 30.f);
    assert(HeightAtLogQuantile(1.f) == 1000.f);
    assert(HeightAtLogQuantile(-1.f) == MinimumHeightCm);
    assert(HeightAtLogQuantile(2.f) == MaximumHeightCm);
    assert(std::isfinite(HeightAtLogQuantile(std::numeric_limits<float>::quiet_NaN())));
    // Equal probability widths must cover equal multiplicative size ratios,
    // not equal centimetres. This fails for linear-height interpolation.
    const double ratio = HeightAtLogQuantile(.05f) / HeightAtLogQuantile(0.f);
    for (int i = 1; i <= 20; ++i)
    {
        const float before = HeightAtLogQuantile(float(i - 1) / 20.f);
        const float after = HeightAtLogQuantile(float(i) / 20.f);
        assert(after > before);
        assert(std::abs(after / before - ratio) < .00001);
    }
    int aboveFiveMetres = 0, aboveEightMetres = 0;
    for (int i = 0; i < 20; ++i)
    {
        // Representative stratified population, with one explicit top endpoint.
        const float height = HeightAtLogQuantile(i == 19 ? 1.f : (i + .5f) / 20.f);
        aboveFiveMetres += height >= 500.f;
        aboveEightMetres += height >= 800.f;
    }
    assert(aboveFiveMetres == 4 && aboveEightMetres == 1);
    assert(VerdantEconomy::Tier(1000.f) == 4);
    assert(VerdantEconomy::MaximumHeightCm == 450.f); // Separate player cap.
    std::cout << "NPC scale: endpoints, geometric spacing, rare giant tail and independent player limit passed.\n";
}
