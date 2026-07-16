#include "dusk/boss_rush.h"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "dusk/config.hpp"
#include "dusk/logging.h"
#include "dusk/main.h"

#include <cstring>

namespace dusk {

// ---------------------------------------------------------------------------
// Boss list — story order
// stage / point / roomNo / layer sourced from map_loader_definitions.h
// and confirmed by setNextStage calls in the actor source files
// ---------------------------------------------------------------------------
static const BossEntry kBossList[] = {
    { "Diababa",               "D_MN05A", 50,  0, -1 },
    { "Fyrus",                 "D_MN04A", 50,  0, -1 },
    { "Morpheel",              "D_MN01A", 50,  0, -1 },
    { "Stallord",              "D_MN10A", 50,  0, -1 },
    { "Blizzeta",              "D_MN11A", 50,  0, -1 },
    { "Armogohma",             "D_MN06A", 50,  0, -1 },
    { "Argorok",               "D_MN07A", 50,  0, -1 },
    { "Zant",                  "D_MN08D", 50,  0, -1 },
    { "Ganon's Puppet: Zelda", "D_MN09A", 50,  0, -1 },
    { "Dark Beast Ganon",      "D_MN09A", 51,  0, -1 },
    { "Dark Rider Ganondorf",  "D_MN09B",  0,  0, -1 },
    { "Dark Lord Ganondorf",   "D_MN09C",  0,  0, -1 },
};
static constexpr int kBossCount = static_cast<int>(sizeof(kBossList) / sizeof(kBossList[0]));

// ---------------------------------------------------------------------------
// Miniboss list — story order
// ---------------------------------------------------------------------------
static const BossEntry kMinibossList[] = {
    { "Ook",                   "D_MN05B", 51,  0, -1 },
    { "King Bulblin I",        "F_SP102",  1,  0,  4 },
    { "King Bulblin II",       "F_SP123",  0, 13,  0 },
    { "Dangoro",               "D_MN04B", 51,  0, -1 },
    { "Twilit Bloat",          "F_SP115", 25,  0,  0 },
    { "Deku Toad",             "D_MN01B", 51,  0, -1 },
    { "King Bulblin III",      "F_SP124",  0,  0,  0 },
    { "King Bulblin IV",       "D_MN09",   3,  8,  0 },
    { "Death Sword",           "D_MN10B", 51,  0, -1 },
    { "Darkhammer",            "D_MN11B", 51,  0, -1 },
    { "Darknut",               "D_MN06B", 51,  0, -1 },
    { "Aeralfos",              "D_MN07B", 51,  0, -1 },
};
static constexpr int kMinibossCount = static_cast<int>(sizeof(kMinibossList) / sizeof(kMinibossList[0]));

// Full rush order: minibosses and bosses interleaved in story chronology
// (miniboss comes before its dungeon's boss)
static const BossEntry* kFullSequence[] = {
    &kMinibossList[0],  // Ook
    &kBossList[0],      // Diababa
    &kMinibossList[1],  // King Bulblin I
    &kMinibossList[2],  // King Bulblin II
    &kMinibossList[3],  // Dangoro
    &kBossList[1],      // Fyrus
    &kMinibossList[4],  // Twilit Bloat
    &kMinibossList[5],  // Deku Toad
    &kBossList[2],      // Morpheel
    &kMinibossList[6],  // King Bulblin III
    &kMinibossList[8],  // Death Sword
    &kBossList[3],      // Stallord
    &kMinibossList[9],  // Darkhammer
    &kBossList[4],      // Blizzeta
    &kMinibossList[10], // Darknut
    &kBossList[5],      // Armogohma
    &kMinibossList[11], // Aeralfos
    &kBossList[6],      // Argorok
    &kBossList[7],      // Zant
    &kMinibossList[7],  // King Bulblin IV
    &kBossList[8],      // Ganon's Puppet: Zelda
    &kBossList[9],      // Dark Beast Ganon
    &kBossList[10],     // Dark Rider Ganondorf
    &kBossList[11],     // Dark Lord Ganondorf
};
static constexpr int kFullSequenceCount =
    static_cast<int>(sizeof(kFullSequence) / sizeof(kFullSequence[0]));

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
BossRush& BossRush::instance() {
    static BossRush s_instance;
    return s_instance;
}

BossRush::BossRush()
    : m_active(false)
    , m_awaitingFightEnd(false)
    , m_fightIndex(0)
    , m_totalFights(0)
    , m_mode(Mode::BossSingle)
    , m_singleIndex(0)
    , m_singleIsMiniboss(false)
{
    m_currentArenaStageName[0] = '\0';
    for (int i = 0; i < kMaxSequence; ++i) m_sequence[i] = nullptr;
}

// ---------------------------------------------------------------------------
// Public data access
// ---------------------------------------------------------------------------
const BossEntry* BossRush::getBossList()     { return kBossList; }
int              BossRush::getBossCount()     { return kBossCount; }
const BossEntry* BossRush::getMinibossList() { return kMinibossList; }
int              BossRush::getMinibossCount(){ return kMinibossCount; }

const BossEntry* BossRush::currentEntry() const {
    if (!m_active || m_fightIndex < 0 || m_fightIndex >= m_totalFights) return nullptr;
    return m_sequence[m_fightIndex];
}

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------
void BossRush::start(Mode mode, int singleIndex, bool singleIsMiniboss) {
    if (m_active) abort();

    m_mode             = mode;
    m_singleIndex      = singleIndex;
    m_singleIsMiniboss = singleIsMiniboss;
    m_fightIndex       = 0;

    // Build sequence
    switch (mode) {
        case Mode::BossSingle:
            m_totalFights = 1;
            m_sequence[0] = &kBossList[singleIndex];
            break;
        case Mode::MinibossSingle:
            m_totalFights = 1;
            m_sequence[0] = &kMinibossList[singleIndex];
            break;
        case Mode::BossRush:
            m_totalFights = kBossCount;
            for (int i = 0; i < kBossCount; ++i) m_sequence[i] = &kBossList[i];
            break;
        case Mode::MinibossRush:
            m_totalFights = kMinibossCount;
            for (int i = 0; i < kMinibossCount; ++i) m_sequence[i] = &kMinibossList[i];
            break;
        case Mode::FullRush:
            m_totalFights = kFullSequenceCount;
            for (int i = 0; i < kFullSequenceCount; ++i) m_sequence[i] = kFullSequence[i];
            break;
    }

    captureSnapshot();
    m_active = true;
    warpToFight(0);
}

void BossRush::abort() {
    if (!m_active) return;
    m_active           = false;
    m_awaitingFightEnd = false;
    restoreSnapshot();
    returnToStart();
}

// ---------------------------------------------------------------------------
// Per-frame tick
// ---------------------------------------------------------------------------
void BossRush::tick() {
    if (!m_active) return;
    if (!dusk::IsGameLaunched) return;

    const char* currentStage = dComIfGp_getStartStageName();
    if (currentStage == nullptr) return;

    if (m_awaitingFightEnd) {
        // Detect: we've left the arena stage (boss beaten OR game over + respawn)
        bool leftArena = (strncmp(currentStage, m_currentArenaStageName, 8) != 0);
        if (!leftArena) return;

        DuskLog.info("[BossRush] Left arena '{}', fight {} of {}",
                     m_currentArenaStageName, m_fightIndex + 1, m_totalFights);

        m_awaitingFightEnd = false;
        restoreSnapshot(); // Restore HP/items regardless of win or loss

        int next = m_fightIndex + 1;
        if (next < m_totalFights) {
            // Check whether the player died (game over then respawned) vs won.
            // After a game over the engine warps to the save/restart point;
            // after winning the boss warps the player to the dungeon exit.
            // In both cases we've left the arena - just proceed to the next fight.
            m_fightIndex = next;
            warpToFight(next);
        } else {
            // All fights done - return home
            m_active = false;
            returnToStart();
            DuskLog.info("[BossRush] Run complete, returning to start.");
        }
    } else {
        // We should be in the arena stage now
        if (m_active && !m_awaitingFightEnd) {
            // Confirm we actually arrived
            if (strncmp(currentStage, m_currentArenaStageName, 8) == 0) {
                m_awaitingFightEnd = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
void BossRush::captureSnapshot() {
    const char* stage = dComIfGp_getStartStageName();
    if (stage) {
        strncpy(m_snapshot.stageName, stage, 7);
        m_snapshot.stageName[7] = '\0';
    }
    m_snapshot.point  = dComIfGp_getStartStagePoint();
    m_snapshot.roomNo = dComIfGp_getStartStageRoomNo();
    m_snapshot.layer  = dComIfGp_getStartStageLayer();

    m_snapshot.life    = dComIfGs_getLife();
    m_snapshot.maxLife = dComIfGs_getMaxLife();
    m_snapshot.arrows  = dComIfGs_getArrowNum();
    m_snapshot.bombsNormal   = dComIfGs_getBombNum(0);
    m_snapshot.bombsWater    = dComIfGs_getBombNum(1);
    m_snapshot.bombsBombling = dComIfGs_getBombNum(2);
    m_snapshot.seeds  = dComIfGs_getPachinkoNum();
    m_snapshot.oil    = dComIfGs_getOil();
    m_snapshot.magic  = dComIfGs_getMagic();

    for (int i = 0; i < 4; ++i) {
        m_snapshot.bottleContent[i] = dComIfGs_getBottleNum(i);
    }

    m_snapshot.valid = true;
    DuskLog.info("[BossRush] Snapshot captured: stage={} life={}/{}",
                 m_snapshot.stageName, m_snapshot.life, m_snapshot.maxLife);
}

void BossRush::restoreSnapshot() {
    if (!m_snapshot.valid) return;

    dComIfGs_setLife(m_snapshot.life);
    dComIfGs_setArrowNum(m_snapshot.arrows);
    dComIfGs_setBombNum(0, m_snapshot.bombsNormal);
    dComIfGs_setBombNum(1, m_snapshot.bombsWater);
    dComIfGs_setBombNum(2, m_snapshot.bombsBombling);
    dComIfGs_setPachinkoNum(m_snapshot.seeds);
    dComIfGs_setOil(m_snapshot.oil);
    dComIfGs_setMagic(m_snapshot.magic);

    for (int i = 0; i < 4; ++i) {
        dComIfGs_setBottleNum(i, m_snapshot.bottleContent[i]);
    }

    DuskLog.info("[BossRush] Snapshot restored.");
}

void BossRush::warpToFight(int index) {
    const BossEntry* entry = m_sequence[index];
    if (!entry) return;

    strncpy(m_currentArenaStageName, entry->stageName, 7);
    m_currentArenaStageName[7] = '\0';
    m_awaitingFightEnd = false; // will become true once we confirm arrival

    DuskLog.info("[BossRush] Warping to fight {}/{}: {} ({})",
                 index + 1, m_totalFights, entry->displayName, entry->stageName);

    dComIfGp_setNextStage(entry->stageName, entry->point, entry->roomNo, entry->layer);
}

void BossRush::returnToStart() {
    if (!m_snapshot.valid) return;
    dComIfGp_setNextStage(m_snapshot.stageName, m_snapshot.point,
                          m_snapshot.roomNo, m_snapshot.layer);
    DuskLog.info("[BossRush] Returning to {}", m_snapshot.stageName);
}

} // namespace dusk
