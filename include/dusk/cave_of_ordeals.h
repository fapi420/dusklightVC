#pragma once

namespace dusk {

/**
 * CaveOfOrdealsRandomizer
 *
 * When enabled, intercepts every room transition inside the Cave of Ordeals
 * (stage "D_SB01") and spawns a random selection of enemies at the exact
 * positions where the room's *original* enemies already are.
 *
 * Instead of guessing spawn coordinates, this module scans all live actors
 * in the current room, picks out the ones matching known Cave-of-Ordeals
 * enemy types, and uses their positions (which come straight from the
 * room's original stage data) as anchor points for the new random enemies.
 * This guarantees enemies always appear in valid, intended combat spots,
 * at the same time as the original enemies — there is no spawn delay.
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

    // Must be called once per frame while the game is running.
    void         tick();

private:
    CaveOfOrdealsRandomizer();

    void spawnEnemiesForFloor(int roomNo);

    bool         m_enabled;
    int          m_lastRoomNo;   // floor we last spawned enemies for
    unsigned int m_lastSeed;
};

} // namespace dusk
