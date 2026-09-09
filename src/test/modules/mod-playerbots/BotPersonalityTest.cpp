/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * Tests for the mod-playerbots BotPersonality primitive. Guarded so builds without the module
 * (see .github/workflows/core-build*.yml) still compile this translation unit.
 */

#ifdef MOD_PLAYERBOTS

#include "BotPersonality.h"

#include <cmath>
#include <gtest/gtest.h>

namespace
{
    float Magnitude(BotPersonality const& p)
    {
        return std::sqrt(p.personalSpaceX * p.personalSpaceX + p.personalSpaceY * p.personalSpaceY);
    }
}

// Layer 1 must be pure: the same character has to behave like the same individual across restarts,
// and nothing is persisted, so the seed alone has to reproduce every trait exactly.
TEST(BotPersonalityTest, SameSeedIsDeterministic)
{
    BotPersonality const a = BotPersonality::FromSeed(12345u, 1.0f);
    BotPersonality const b = BotPersonality::FromSeed(12345u, 1.0f);

    EXPECT_FLOAT_EQ(a.reactionScale, b.reactionScale);
    EXPECT_FLOAT_EQ(a.personalSpaceX, b.personalSpaceX);
    EXPECT_FLOAT_EQ(a.personalSpaceY, b.personalSpaceY);
    EXPECT_FLOAT_EQ(a.dismountSlack, b.dismountSlack);
    EXPECT_FLOAT_EQ(a.socialScale, b.socialScale);
    EXPECT_EQ(a.slotBias, b.slotBias);
}

// PersonalityStrength = 0 is the documented escape hatch back to uniform bots, so it must be exact
// rather than merely close - any drift here silently changes behaviour for anyone who disabled it.
TEST(BotPersonalityTest, ZeroStrengthIsExactlyNeutral)
{
    for (uint32_t seed = 1; seed <= 1000u; ++seed)
    {
        BotPersonality const p = BotPersonality::FromSeed(seed, 0.0f);

        ASSERT_FLOAT_EQ(p.reactionScale, 1.0f) << "seed " << seed;
        ASSERT_FLOAT_EQ(p.dismountSlack, 1.0f) << "seed " << seed;
        ASSERT_FLOAT_EQ(p.socialScale, 1.0f) << "seed " << seed;
        ASSERT_FLOAT_EQ(p.personalSpaceX, 0.0f) << "seed " << seed;
        ASSERT_FLOAT_EQ(p.personalSpaceY, 0.0f) << "seed " << seed;
    }
}

TEST(BotPersonalityTest, TraitsStayInsideSampledBandsAtDefaultStrength)
{
    for (uint32_t seed = 1; seed <= 50000u; ++seed)
    {
        BotPersonality const p = BotPersonality::FromSeed(seed, 1.0f);

        ASSERT_GE(p.reactionScale, BotPersonality::REACTION_BAND_LOW) << "seed " << seed;
        ASSERT_LE(p.reactionScale, BotPersonality::REACTION_BAND_HIGH) << "seed " << seed;
        ASSERT_GE(p.dismountSlack, BotPersonality::DISMOUNT_BAND_LOW) << "seed " << seed;
        ASSERT_LE(p.dismountSlack, BotPersonality::DISMOUNT_BAND_HIGH) << "seed " << seed;
        ASSERT_GE(p.socialScale, BotPersonality::SOCIAL_BAND_LOW) << "seed " << seed;
        ASSERT_LE(p.socialScale, BotPersonality::SOCIAL_BAND_HIGH) << "seed " << seed;
    }
}

// Formation::GetMaxDistance() derives the follow tolerance from this magnitude. If the offset could
// exceed 1 the tolerance would understate how far the bot may legitimately stand, and both the
// follow logic and the dismount gate that reads it would be wrong.
TEST(BotPersonalityTest, PersonalSpaceStaysOnTheUnitDisc)
{
    for (uint32_t seed = 1; seed <= 50000u; ++seed)
        ASSERT_LE(Magnitude(BotPersonality::FromSeed(seed, 1.0f)), 1.0f + 1e-5f) << "seed " << seed;
}

TEST(BotPersonalityTest, ExaggeratedStrengthStaysInsideHardBounds)
{
    for (uint32_t seed = 1; seed <= 50000u; ++seed)
    {
        BotPersonality const p = BotPersonality::FromSeed(seed, BotPersonality::STRENGTH_MAX);

        ASSERT_GE(p.reactionScale, BotPersonality::REACTION_MIN) << "seed " << seed;
        ASSERT_LE(p.reactionScale, BotPersonality::REACTION_MAX) << "seed " << seed;
        ASSERT_GE(p.dismountSlack, BotPersonality::DISMOUNT_MIN) << "seed " << seed;
        ASSERT_LE(p.dismountSlack, BotPersonality::DISMOUNT_MAX) << "seed " << seed;
        ASSERT_GE(p.socialScale, BotPersonality::SOCIAL_MIN) << "seed " << seed;
        ASSERT_LE(p.socialScale, BotPersonality::SOCIAL_MAX) << "seed " << seed;
        ASSERT_LE(Magnitude(p), BotPersonality::SPACE_MAX + 1e-5f) << "seed " << seed;
    }
}

// Strength above the clamp must behave as the clamp, not extrapolate further.
TEST(BotPersonalityTest, StrengthIsClamped)
{
    BotPersonality const clamped = BotPersonality::FromSeed(777u, BotPersonality::STRENGTH_MAX);
    BotPersonality const beyond = BotPersonality::FromSeed(777u, 99.0f);

    EXPECT_FLOAT_EQ(clamped.reactionScale, beyond.reactionScale);
    EXPECT_FLOAT_EQ(clamped.socialScale, beyond.socialScale);
}

// The reason FromSeed hashes at all. GUID counters are handed out sequentially, so bots created
// together would otherwise be near-identical - which is the uniformity this whole feature removes.
// For two independent uniforms on a band of width w the mean absolute difference is w/3; correlated
// seeds would produce a value far below that.
TEST(BotPersonalityTest, AdjacentSeedsAreDecorrelated)
{
    constexpr uint32_t samples = 20000u;
    double total = 0.0;

    for (uint32_t seed = 1; seed <= samples; ++seed)
    {
        total += std::fabs(BotPersonality::FromSeed(seed, 1.0f).reactionScale -
                           BotPersonality::FromSeed(seed + 1u, 1.0f).reactionScale);
    }

    double const mean = total / samples;
    double const expected =
        (BotPersonality::REACTION_BAND_HIGH - BotPersonality::REACTION_BAND_LOW) / 3.0;

    EXPECT_NEAR(mean, expected, expected * 0.1);
}

// Traits should not move together either: a bot that is slow should not therefore be quiet.
TEST(BotPersonalityTest, TraitsAreIndependentOfEachOther)
{
    constexpr uint32_t samples = 20000u;
    double sumR = 0.0, sumS = 0.0, sumRS = 0.0, sumRR = 0.0, sumSS = 0.0;

    for (uint32_t seed = 1; seed <= samples; ++seed)
    {
        BotPersonality const p = BotPersonality::FromSeed(seed, 1.0f);
        double const r = p.reactionScale;
        double const s = p.socialScale;

        sumR += r;
        sumS += s;
        sumRS += r * s;
        sumRR += r * r;
        sumSS += s * s;
    }

    double const n = samples;
    double const cov = sumRS / n - (sumR / n) * (sumS / n);
    double const varR = sumRR / n - (sumR / n) * (sumR / n);
    double const varS = sumSS / n - (sumS / n) * (sumS / n);
    double const correlation = cov / std::sqrt(varR * varS);

    EXPECT_LT(std::fabs(correlation), 0.05);
}

// Layer 2. A trait alone would make each bot metronomic, which reads as machine just as much as all
// bots being identical; call sites re-roll around the trait through this.
TEST(BotPersonalityTest, JitterSpansTheBandAndCentresOnTheTrait)
{
    EXPECT_FLOAT_EQ(BotPersonality::ApplyJitter(2.0f, 0.15f, 0.0f), 1.70f);
    EXPECT_FLOAT_EQ(BotPersonality::ApplyJitter(2.0f, 0.15f, 0.5f), 2.00f);
    EXPECT_FLOAT_EQ(BotPersonality::ApplyJitter(2.0f, 0.15f, 1.0f), 2.30f);
}

TEST(BotPersonalityTest, JitterClampsOutOfRangeRolls)
{
    EXPECT_FLOAT_EQ(BotPersonality::ApplyJitter(1.0f, 0.2f, -5.0f), 0.80f);
    EXPECT_FLOAT_EQ(BotPersonality::ApplyJitter(1.0f, 0.2f, 5.0f), 1.20f);
}

TEST(BotPersonalityTest, ZeroJitterLeavesTheTraitUntouched)
{
    EXPECT_FLOAT_EQ(BotPersonality::ApplyJitter(1.37f, 0.0f, 0.9f), 1.37f);
}

#endif  // MOD_PLAYERBOTS
