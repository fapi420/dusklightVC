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
 * If no original enemies could be sampled (e.g. their type isn't recognised,
 * or they've already been defeated), the player's own position is used as a
 * fallback so every floor still receives spawns. Ground-bound enemy types
 * additionally get a ground-raycast applied to their spawn Y so they never
 * end up floating mid-air after inheriting a flying enemy's anchor position.
 *
 * Whether the randomizer is enabled and how many enemies it adds per floor
 * are stored as persistent settings (game.caveOrdealsRandomizerEnabled /
 * game.caveOrdealsEnemiesPerFloor) and are also exposed in the Dusklight
 * options menu under the "Cave Randomizer" tab.
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

    int          m_lastRoomNo;   // floor we last spawned enemies for (runtime-only)
    unsigned int m_lastSeed;     // RNG seed (runtime-only)
};

} // namespace dusk
