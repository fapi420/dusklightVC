#pragma once

namespace dusk {

/**
 * CaveOfOrdealsRandomizer
 *
 * When enabled, intercepts every room transition inside the Cave of Ordeals
 * (stage "D_SB01") and spawns a configurable number of random enemies at
 * the exact positions where the room's *original* enemies already are.
 *
 * Instead of guessing spawn coordinates, this module scans all live actors
 * in the current room, picks out the ones matching known Cave-of-Ordeals
 * enemy types, and uses their positions (which come straight from the
 * room's original stage data) as anchor points for the new random enemies.
 * This guarantees enemies always appear in valid, intended combat spots,
 * at the same time as the original enemies — there is no spawn delay.
 *
 * The number of enemies added per floor is fixed (not randomized) and can
 * be configured at runtime via setEnemiesPerFloor().
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

    // Re-roll the RNG seed and force an immediate re-spawn on the current floor.
    void         rerollSeed();

    // Number of extra enemies spawned per floor (clamped to a sane range).
    int  getEnemiesPerFloor() const;
    void setEnemiesPerFloor(int count);

    // Range bounds for UI sliders/inputs.
    int getMinEnemiesPerFloorSetting() const;
    int getMaxEnemiesPerFloorSetting() const;

    // Must be called once per frame while the game is running.
    void         tick();

private:
    CaveOfOrdealsRandomizer();

    void spawnEnemiesForFloor(int roomNo);

    bool         m_enabled;
    int          m_lastRoomNo;   // floor we last spawned enemies for
    unsigned int m_lastSeed;
    int          m_enemiesPerFloor;
};

} // namespace dusk
