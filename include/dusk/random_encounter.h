#pragma once

namespace dusk {

/**
 * RandomEncounter
 *
 * When enabled, periodically spawns a burst of enemies around the player
 * while exploring the overworld - a "random encounter" system inspired by
 * the Cave of Ordeals randomizer's original (since removed) player-position
 * fallback spawn behaviour.
 *
 * Both the time between encounters and the number of enemies spawned per
 * encounter are configurable via four discrete tiers each:
 *
 *   Interval tiers:  Frequent (30-60s), Often (60-120s),
 *                    Occasionally (120-240s), Seldom (240-480s)
 *   Amount tiers:    Few (1-3), Handful (3-6), Plentiful (6-12), Countless (12-24)
 *
 * Safety rules enforced automatically:
 *   - No spawns while the player is in a "safe" stage where weapons/items
 *     cannot be used (indoor rooms, Castle Town, etc).
 *   - No spawns while a cutscene/event is running.
 *   - Enemies are placed in a ring around the player (not too close, not
 *     too far) and spread out so that avoiding all of them is non-trivial.
 *   - Each spawned enemy gets a brief warp-hole visual effect so its
 *     appearance doesn't feel like it just silently popped into existence.
 *
 * The original ISO data is never modified.
 */
class RandomEncounter {
public:
    enum class IntervalTier {
        Frequent     = 0, // 30-60 seconds
        Often        = 1, // 60-120 seconds
        Occasionally = 2, // 120-240 seconds
        Seldom       = 3, // 240-480 seconds
    };

    enum class AmountTier {
        Few       = 0, // 1-3 enemies
        Handful   = 1, // 3-6 enemies
        Plentiful = 2, // 6-12 enemies
        Countless = 3, // 12-24 enemies
    };

    static RandomEncounter& instance();

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setIntervalTier(IntervalTier tier);
    IntervalTier getIntervalTier() const;

    void setAmountTier(AmountTier tier);
    AmountTier getAmountTier() const;

    // Must be called once per frame while the game is running.
    void tick();

private:
    RandomEncounter();

    void rollNextEncounterDelay();
    void triggerEncounter();
    bool canSpawnRightNow() const;

    float m_secondsUntilNextEncounter;
    bool  m_hasRolledInitialDelay;
};

} // namespace dusk
