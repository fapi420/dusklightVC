#pragma once

namespace dusk {

/**
 * CaveOfOrdealsRandomizer
 *
 * When enabled, this module intercepts every room transition inside
 * the Cave of Ordeals (stage "D_SB01") and dynamically spawns a random
 * selection of enemies around the player's position.
 *
 * The original ISO data is never modified – enemies are injected purely
 * at runtime via fopAcM_create(), the same API that the Actor Spawner
 * debug tool uses.
 *
 * When disabled the cave loads exactly as the original game defines it.
 */
class CaveOfOrdealsRandomizer {
public:
    static CaveOfOrdealsRandomizer& instance();

    // Enable / disable the randomizer.
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Returns the seed used for the current run.
    unsigned int getLastSeed() const;

    // Re-roll the RNG seed (also resets room tracking so the current
    // floor is re-randomized on the next tick).
    void rerollSeed();

    /**
     * Must be called once per frame while the game is running.
     * Detects room transitions inside D_SB01 and triggers enemy spawning.
     */
    void tick();

private:
    CaveOfOrdealsRandomizer();

    void spawnEnemiesForFloor(int roomNo);

    bool         m_enabled;
    int          m_lastRoomNo;   // last floor we already spawned enemies on
    unsigned int m_lastSeed;
};

} // namespace dusk
