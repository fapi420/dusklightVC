#pragma once

namespace dusk {

/**
 * CaveOfOrdealsRandomizer
 *
 * When enabled, intercepts every room transition inside the Cave of Ordeals
 * (stage "D_SB01") and dynamically spawns a random selection of enemies on
 * the actual floor geometry using ground-raycasts.
 *
 * A short delay (kSpawnDelayFrames) is applied after each room transition so
 * that the collision mesh is fully loaded before raycasts are performed –
 * this ensures enemies land correctly on the first floor as well as all
 * subsequent ones.
 *
 * The original ISO data is never modified.
 * When disabled the cave loads exactly as the original game defines it.
 */
class CaveOfOrdealsRandomizer {
public:
    static CaveOfOrdealsRandomizer& instance();

    void         setEnabled(bool enabled);
    bool         isEnabled() const;

    unsigned int getLastSeed() const;

    // Re-roll the RNG seed and force a re-spawn on the current floor.
    void         rerollSeed();

    // Must be called once per frame while the game is running.
    void         tick();

private:
    CaveOfOrdealsRandomizer();

    void spawnEnemiesForFloor(int roomNo);

    bool         m_enabled;
    int          m_lastRoomNo;       // floor we last recorded a transition for
    int          m_pendingRoomNo;    // floor waiting for the delay timer
    int          m_framesUntilSpawn; // countdown; -1 = idle
    unsigned int m_lastSeed;
};

} // namespace dusk
