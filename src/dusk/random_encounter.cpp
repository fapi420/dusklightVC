#include "dusk/random_encounter.h"

#include "d/actor/d_a_alink.h"
#include "d/d_bg_s.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_kankyo_tev_str.h"
#include "dusk/config.hpp"
#include "dusk/main.h"
#include "dusk/settings.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_layer.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_node.h"
#include "f_pc/f_pc_name.h"
#include "SSystem/SComponent/c_sxyz.h"
#include "SSystem/SComponent/c_xyz.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace dusk {

// ---------------------------------------------------------------------------
// Enemy pool - reuses the same roster as the Cave of Ordeals randomizer
// (see cave_of_ordeals.cpp), since that pool has already been verified
// live in-game. None of these actors were found to depend on dungeon-
// specific stage types, so they're expected to behave correctly in the
// overworld as well.
// ---------------------------------------------------------------------------
static const s16 kFieldEnemyPool[] = {
    0x1FE, // E_OC  – Bokoblin
    0x1E7, // E_MS  – Rat
    0x1EA, // E_BA  – Keese (incl. Fire/Ice Keese variants)
    0x1C5, // E_DB  – Baba Serpent (Deku Baba)
    0x1BF, // E_ST  – Skulltula
    0x1D4, // E_RD  – Bulblin / Bulblin Archer
    0x1CF, // E_HM  – Torch Slug
    0x1B2, // E_DD  – Dodongo
    0x206, // E_TT  – Tektite (Red/Blue)
    0x1B3, // E_DN  – Lizalfos
    0x1DD, // E_MM  – Helmasaur
    0x1BE, // E_SM2 – Chu (Red/Blue/Yellow/Purple)
    0x1BD, // E_SM  – Chu Worm
    0x1EB, // E_BU  – Bubble (incl. Fire/Ice Bubble variants)
    0x1B9, // E_SH  – Stalhound
    0x1D3, // E_RB  – Leever
    0x1E9, // E_NZ  – Ghoul Rat
    0x20A, // E_GI  – Gibdo
    0x1B8, // E_SF  – Stalfos (incl. mini/Stalchild variant)
    0x1E5, // E_FB  – Freezard
    0x1E0, // E_KK  – Chilfos
    0x1AF, // E_AI  – Armos
    0x1B5, // E_MF  – Dynalfos
};
static const int kFieldEnemyPoolSize =
    static_cast<int>(sizeof(kFieldEnemyPool) / sizeof(kFieldEnemyPool[0]));

// Spawn ring around the player: not so close it feels unfair to react to,
// not so far that running away trivially avoids every enemy.
static constexpr float kMinSpawnRadius = 500.0f;
static constexpr float kMaxSpawnRadius = 1200.0f;

// ---------------------------------------------------------------------------
// Tier -> seconds-range lookup tables
// ---------------------------------------------------------------------------
struct Range { float minSeconds; float maxSeconds; };

static Range intervalRangeFor(RandomEncounter::IntervalTier tier) {
    switch (tier) {
        case RandomEncounter::IntervalTier::Frequent:     return {30.0f, 60.0f};
        case RandomEncounter::IntervalTier::Often:        return {60.0f, 120.0f};
        case RandomEncounter::IntervalTier::Occasionally: return {120.0f, 240.0f};
        case RandomEncounter::IntervalTier::Seldom:       return {240.0f, 480.0f};
    }
    return {60.0f, 120.0f};
}

struct CountRange { int minCount; int maxCount; };

static CountRange amountRangeFor(RandomEncounter::AmountTier tier) {
    switch (tier) {
        case RandomEncounter::AmountTier::Few:       return {1, 3};
        case RandomEncounter::AmountTier::Handful:   return {3, 6};
        case RandomEncounter::AmountTier::Plentiful: return {6, 12};
        case RandomEncounter::AmountTier::Countless: return {12, 24};
    }
    return {1, 3};
}

// ---------------------------------------------------------------------------
// Wall-clock delta time, independent of game speed/framerate. The encounter
// timer is a real-world countdown, not a simulation-tick countdown.
// ---------------------------------------------------------------------------
static float consumeRealDeltaSeconds() {
    using clock = std::chrono::steady_clock;
    static clock::time_point lastTime = clock::now();
    clock::time_point now = clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    // Guard against huge spikes (e.g. the game was paused/minimized).
    if (dt > 1.0f) dt = 1.0f;
    if (dt < 0.0f) dt = 0.0f;
    return dt;
}

static float randomFloat(float minVal, float maxVal) {
    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return minVal + t * (maxVal - minVal);
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

RandomEncounter& RandomEncounter::instance() {
    static RandomEncounter s_instance;
    return s_instance;
}

RandomEncounter::RandomEncounter()
    : m_secondsUntilNextEncounter(0.0f)
    , m_hasRolledInitialDelay(false)
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)) ^ 0x9E3779B9u);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void RandomEncounter::setEnabled(bool enabled) {
    bool wasEnabled = getSettings().game.randomEncounterEnabled.getValue();
    if (wasEnabled == enabled) return;
    getSettings().game.randomEncounterEnabled.setValue(enabled);
    config::Save();
    if (enabled) {
        // Roll a fresh delay so turning the feature on doesn't immediately
        // spawn enemies on top of the player.
        rollNextEncounterDelay();
    } else {
        m_hasRolledInitialDelay = false;
    }
}

bool RandomEncounter::isEnabled() const {
    return getSettings().game.randomEncounterEnabled.getValue();
}

void RandomEncounter::setIntervalTier(IntervalTier tier) {
    getSettings().game.randomEncounterIntervalTier.setValue(static_cast<int>(tier));
    config::Save();
}

RandomEncounter::IntervalTier RandomEncounter::getIntervalTier() const {
    int v = getSettings().game.randomEncounterIntervalTier.getValue();
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    return static_cast<IntervalTier>(v);
}

void RandomEncounter::setAmountTier(AmountTier tier) {
    getSettings().game.randomEncounterAmountTier.setValue(static_cast<int>(tier));
    config::Save();
}

RandomEncounter::AmountTier RandomEncounter::getAmountTier() const {
    int v = getSettings().game.randomEncounterAmountTier.getValue();
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    return static_cast<AmountTier>(v);
}

// ---------------------------------------------------------------------------
// Per-frame tick
// ---------------------------------------------------------------------------

void RandomEncounter::tick() {
    // Always consume real delta time so the timer doesn't jump forward by a
    // huge amount the next time the feature is re-enabled.
    float dt = consumeRealDeltaSeconds();

    if (!isEnabled()) {
        m_hasRolledInitialDelay = false;
        return;
    }

    if (!IsGameLaunched) {
        return;
    }

    if (!m_hasRolledInitialDelay) {
        rollNextEncounterDelay();
        return;
    }

    m_secondsUntilNextEncounter -= dt;
    if (m_secondsUntilNextEncounter > 0.0f) {
        return;
    }

    // Timer elapsed - only actually spawn if it's currently safe to do so.
    // If not safe right now, keep checking every frame without re-rolling
    // the timer, so the encounter fires as soon as it becomes safe rather
    // than being silently skipped.
    if (!canSpawnRightNow()) {
        return;
    }

    triggerEncounter();
    rollNextEncounterDelay();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void RandomEncounter::rollNextEncounterDelay() {
    Range r = intervalRangeFor(getIntervalTier());
    m_secondsUntilNextEncounter = randomFloat(r.minSeconds, r.maxSeconds);
    m_hasRolledInitialDelay = true;
}

bool RandomEncounter::canSpawnRightNow() const {
    daAlink_c* player = static_cast<daAlink_c*>(dComIfGp_getPlayer(0));
    if (player == nullptr) {
        return false;
    }

    // No spawns in safe areas where weapons/items can't be used (indoor
    // rooms, dungeons-as-rooms, Castle Town, etc).
    if (daAlink_c::checkNotBattleStage()) {
        return false;
    }

    // No spawns during cutscenes/scripted events.
    if (dComIfGp_event_runCheck()) {
        return false;
    }
    if (player->checkEventRun()) {
        return false;
    }

    return true;
}

void RandomEncounter::triggerEncounter() {
    daAlink_c* player = static_cast<daAlink_c*>(dComIfGp_getPlayer(0));
    if (player == nullptr) {
        return;
    }

    CountRange cr = amountRangeFor(getAmountTier());
    int count = cr.minCount + (std::rand() % (cr.maxCount - cr.minCount + 1));

    int roomNo = player->current.roomNo;
    const cXyz& playerPos = player->current.pos;

    layer_class* savedLayer = fpcLy_CurrentLayer();
    base_process_class* playScene = fpcM_SearchByName(fpcNm_PLAY_SCENE_e);
    if (playScene != nullptr) {
        fpcLy_SetCurrentLayer(
            &reinterpret_cast<process_node_class*>(playScene)->layer);
    }

    cXyz scale(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        s16 enemyId = kFieldEnemyPool[std::rand() % kFieldEnemyPoolSize];

        // Distribute spawn angles roughly evenly around the player, with
        // a little jitter so it doesn't look like a perfectly even ring,
        // then pick a random radius within the configured band. Together
        // this surrounds the player widely enough that dodging every
        // enemy at once isn't trivial.
        float baseAngle = (static_cast<float>(i) / static_cast<float>(count)) * 6.2831853f;
        float jitter = randomFloat(-0.4f, 0.4f);
        float angle = baseAngle + jitter;
        float radius = randomFloat(kMinSpawnRadius, kMaxSpawnRadius);

        cXyz probePos;
        probePos.x = playerPos.x + std::cosf(angle) * radius;
        probePos.y = playerPos.y + 1500.0f;
        probePos.z = playerPos.z + std::sinf(angle) * radius;

        dBgS_ObjGndChk gndChk;
        gndChk.SetPos(&probePos);
        float groundY = dComIfG_Bgsp().GroundCross(&gndChk);
        if (groundY < -9000.0f) {
            groundY = playerPos.y;
        }

        cXyz spawnPos;
        spawnPos.x = probePos.x;
        spawnPos.y = groundY;
        spawnPos.z = probePos.z;

        csXyz spawnAngle;
        spawnAngle.set(0, static_cast<s16>(std::rand() % 65536), 0);

        // Spawn-in visual: a small smoke puff plays at the target position
        // right before the enemy is created there - the same effect several
        // ground enemies (Bokoblin, Hebi Baba) use themselves when emerging,
        // so it reads as a natural "arrival" rather than the much larger,
        // boss-encounter-style warp hole effect.
        dKy_tevstr_c tevStr;
        g_env_light.settingTevStruct(0x10, &spawnPos, &tevStr);

        u32 smokeHandle1 = 0;
        u32 smokeHandle2 = 0;
        fopAcM_effSmokeSet1(&smokeHandle1, &smokeHandle2, &spawnPos, &spawnAngle,
                             2.0f, &tevStr, 1);

        fopAcM_create(
            enemyId,
            static_cast<u32>(-1),
            &spawnPos,
            roomNo,
            &spawnAngle,
            &scale,
            static_cast<s8>(-1)
        );
    }

    fpcLy_SetCurrentLayer(savedLayer);
}

} // namespace dusk
