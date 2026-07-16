#pragma once

#include <cstring>

namespace dusk {

// ---------------------------------------------------------------------------
// A single boss/miniboss entry - stage + spawn info
// ---------------------------------------------------------------------------
struct BossEntry {
    const char* displayName;
    const char* stageName;
    s16         point;
    s8          roomNo;
    s8          layer;
};

// ---------------------------------------------------------------------------
// Snapshot of consumables taken before a Boss Rush starts, restored after
// ---------------------------------------------------------------------------
struct BossRushSnapshot {
    // Position
    char stageName[8];
    s16  point;
    s8   roomNo;
    s8   layer;

    // Health
    u16  life;
    u16  maxLife;

    // Consumables
    u8   arrows;
    u8   bombsNormal;
    u8   bombsWater;
    u8   bombsBombling;
    u8   seeds;    // Slingshot seeds (pachinko)
    u16  oil;      // Lantern oil
    u8   magic;    // Magic armor rupees (stored as u8 in save)

    // Bottles (4 max, content ID per slot)
    u8   bottleContent[4];

    bool valid;

    BossRushSnapshot() : point(0), roomNo(0), layer(-1),
        life(0), maxLife(0),
        arrows(0), bombsNormal(0), bombsWater(0), bombsBombling(0),
        seeds(0), oil(0), magic(0), valid(false)
    {
        stageName[0] = '\0';
        for (int i = 0; i < 4; ++i) bottleContent[i] = 0xFF;
    }
};

// ---------------------------------------------------------------------------
// BossRush singleton
// ---------------------------------------------------------------------------
class BossRush {
public:
    enum class Mode {
        BossSingle,       // One boss of choice
        MinibossSingle,   // One miniboss of choice
        BossRush,         // All bosses in story order
        MinibossRush,     // All minibosses in story order
        FullRush,         // All bosses + minibosses in story order
    };

    static BossRush& instance();

    // Data access
    static const BossEntry* getBossList();
    static int               getBossCount();
    static const BossEntry* getMinibossList();
    static int               getMinibossCount();

    // Build the sequence for Rush modes (caller must free or use internal buffer)
    // Returns the full ordered sequence for the given mode
    static void buildSequence(Mode mode, int optionalSingleIndex, bool isSingleMiniboss,
                               const BossEntry** outEntries, int& outCount);

    // Start a run
    void start(Mode mode, int singleIndex = 0, bool singleIsMiniboss = false);

    // Abort an active run, restore snapshot, return to start position
    void abort();

    // Per-frame tick
    void tick();

    bool isActive()  const { return m_active; }
    int  currentFightIndex()  const { return m_fightIndex; }
    int  totalFights()        const { return m_totalFights; }
    const BossEntry* currentEntry() const;

private:
    BossRush();

    void captureSnapshot();
    void restoreSnapshot();
    void warpToFight(int index);
    void returnToStart();
    bool isInArena() const;

    bool            m_active;
    bool            m_awaitingFightEnd;  // true while we are inside an arena
    int             m_fightIndex;        // which fight we're currently in (0-based)
    int             m_totalFights;
    Mode            m_mode;
    int             m_singleIndex;
    bool            m_singleIsMiniboss;

    // Sequence buffer (max boss+miniboss count = 12+12 = 24)
    static constexpr int kMaxSequence = 24;
    const BossEntry*     m_sequence[kMaxSequence];

    // The arena stage we warped into (to detect when we've left)
    char            m_currentArenaStageName[8];

    BossRushSnapshot m_snapshot;
};

} // namespace dusk
