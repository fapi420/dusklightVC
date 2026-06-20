#include "dusk/cave_of_ordeals.h"

#include "d/actor/d_a_alink.h"
#include "d/d_bg_s.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "dusk/config.hpp"
#include "dusk/settings.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_iter.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_base.h"
#include "f_pc/f_pc_layer.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_node.h"
#include "f_pc/f_pc_name.h"
#include "SSystem/SComponent/c_sxyz.h"
#include "SSystem/SComponent/c_xyz.h"

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <ctime>

namespace dusk {

// ---------------------------------------------------------------------------
// Enemy pool – verified against the Zelda Wiki's full floor-by-floor enemy
// list for the Cave of Ordeals, with every actor ID cross-checked against
// its @class/@brief doc comment in this repository's headers (not guessed).
//
// Beamos (Obj_Bemos) is deliberately excluded: it is an "object" actor, not
// an "enemy" one, and its Create() path reads specific parameter bits (wall
// rotation direction, etc.) that a generic default-parameter spawn cannot
// safely provide. Spawning it like a normal enemy risks broken behaviour or
// a crash, so it is left out despite appearing on floor 31 in the original.
//
// Darknut (B_TN) and Aeralfos (B_GG) are also excluded: both are categorised
// as "boss" actors (B_* prefix) rather than regular enemies (E_* prefix),
// and in practice behave incorrectly when spawned outside their intended
// scripted encounters.
//
// Imp Poe (E_PO) is also excluded after testing showed it causes problems
// when spawned outside its intended scripted encounters.
// ---------------------------------------------------------------------------
static const s16 kEnemyPool[] = {
    0x1FE, // E_OC  – Bokoblin
    0x1E7, // E_MS  – Rat
    0x1EA, // E_BA  – Keese (incl. Fire/Ice Keese variants)
    0x1C9, // E_HB  – Baba Serpent (Hebi Baba)
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
static const int kEnemyPoolSize = static_cast<int>(sizeof(kEnemyPool) / sizeof(kEnemyPool[0]));

// ---------------------------------------------------------------------------
// Ground-bound enemies. These walk/sit on the floor and have no built-in
// correction for being spawned slightly above or below ground level, unlike
// e.g. flying enemies (Keese) which don't care about exact ground contact.
// Spawning them at an anchor position sampled from a non-ground-bound
// original enemy (e.g. a Bubble or Keese) can leave them floating in mid-air.
// For these specific actors we perform an explicit ground raycast and snap
// their spawn Y to the detected floor height.
// ---------------------------------------------------------------------------
static const s16 kGroundBoundEnemies[] = {
    0x1FE, // E_OC  – Bokoblin
    0x1E7, // E_MS  – Rat
    0x1C9, // E_HB  – Baba Serpent
    0x1BF, // E_ST  – Skulltula
    0x1D4, // E_RD  – Bulblin / Bulblin Archer
    0x1CF, // E_HM  – Torch Slug
    0x1B2, // E_DD  – Dodongo
    0x1B3, // E_DN  – Lizalfos
    0x1DD, // E_MM  – Helmasaur
    0x1B9, // E_SH  – Stalhound
    0x1E9, // E_NZ  – Ghoul Rat
    0x20A, // E_GI  – Gibdo
    0x1B8, // E_SF  – Stalfos
    0x1E5, // E_FB  – Freezard
    0x1E0, // E_KK  – Chilfos
    0x1AF, // E_AI  – Armos
    0x1B5, // E_MF  – Dynalfos
};
static const int kGroundBoundEnemiesSize =
    static_cast<int>(sizeof(kGroundBoundEnemies) / sizeof(kGroundBoundEnemies[0]));

static bool isGroundBoundEnemy(s16 enemyId) {
    for (int i = 0; i < kGroundBoundEnemiesSize; ++i) {
        if (kGroundBoundEnemies[i] == enemyId) return true;
    }
    return false;
}

// Raycasts straight down from well above i_pos and returns the detected
// ground height, or the original i_pos.y if no ground is found.
static float snapToGroundY(const cXyz& i_pos) {
    cXyz probePos = i_pos;
    probePos.y += 1000.0f;

    dBgS_ObjGndChk gndChk;
    gndChk.SetPos(&probePos);
    float groundY = dComIfG_Bgsp().GroundCross(&gndChk);

    // GroundCross returns a large negative sentinel when nothing is hit.
    if (groundY < -9000.0f) {
        return i_pos.y;
    }
    return groundY;
}

// The same set, used to recognise existing live enemies in a room so we can
// sample their positions as spawn anchors for the new random enemies.
static const s16 kEnemyProcNames[] = {
    0x1FE, 0x1E7, 0x1EA, 0x1C9, 0x1BF, 0x1D4,
    0x1CF, 0x1B2, 0x206, 0x1B3, 0x1DD, 0x1BE,
    0x1BD, 0x1EB, 0x1B9, 0x1D3, 0x1E9,
    0x20A, 0x1B8, 0x1E5, 0x1E0,
    0x1AF, 0x1B5,
};
static const int kEnemyProcNamesSize =
    static_cast<int>(sizeof(kEnemyProcNames) / sizeof(kEnemyProcNames[0]));

// Bounds for the "enemies per floor" setting (see settings.cpp for the
// persisted default value).
static constexpr int kMinEnemiesPerFloorSetting = 0;
static constexpr int kMaxEnemiesPerFloorSetting = 100;

static constexpr const char* kCaveStageName = "D_SB01";
static constexpr int kCaveFloorCount        = 51;

static constexpr int kMaxSampledPositions = 16;

// ---------------------------------------------------------------------------
// Actor iterator callback data
// ---------------------------------------------------------------------------
struct EnemyPosScanData {
    int  targetRoomNo;
    cXyz positions[kMaxSampledPositions];
    int  count;
};

static int collectEnemyPositions(void* process, void* userData) {
    auto* data = static_cast<EnemyPosScanData*>(userData);
    if (data->count >= kMaxSampledPositions) return 1;

    auto* base = static_cast<base_process_class*>(process);
    if (!fopAcM_IsActor(base)) return 0;

    auto* actor = static_cast<fopAc_ac_c*>(process);

    if (actor->current.roomNo != data->targetRoomNo) return 0;

    // NOTE: base_process_class has both `name` (the fpcNm_*_e identifier we
    // compare against here) and `profname` (an unrelated internal profile
    // index) - using the wrong one silently fails to match most enemies.
    s16 pname = base->name;
    bool isEnemy = false;
    for (int i = 0; i < kEnemyProcNamesSize; ++i) {
        if (kEnemyProcNames[i] == pname) { isEnemy = true; break; }
    }
    if (!isEnemy) return 0;

    data->positions[data->count++] = actor->current.pos;
    return 0;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

CaveOfOrdealsRandomizer& CaveOfOrdealsRandomizer::instance() {
    static CaveOfOrdealsRandomizer s_instance;
    return s_instance;
}

CaveOfOrdealsRandomizer::CaveOfOrdealsRandomizer()
    : m_lastRoomNo(-2)
    , m_lastSeed(0)
{
    m_lastSeed = static_cast<unsigned int>(std::time(nullptr));
    std::srand(m_lastSeed);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::setEnabled(bool enabled) {
    bool wasEnabled = getSettings().game.caveOrdealsRandomizerEnabled.getValue();
    if (wasEnabled == enabled) return;
    getSettings().game.caveOrdealsRandomizerEnabled.setValue(enabled);
    config::Save();
    if (!enabled) {
        m_lastRoomNo = -2;
    }
}

bool CaveOfOrdealsRandomizer::isEnabled() const {
    return getSettings().game.caveOrdealsRandomizerEnabled.getValue();
}

unsigned int CaveOfOrdealsRandomizer::getLastSeed() const { return m_lastSeed; }

int CaveOfOrdealsRandomizer::getEnemiesPerFloor() const {
    return getSettings().game.caveOrdealsEnemiesPerFloor.getValue();
}

void CaveOfOrdealsRandomizer::setEnemiesPerFloor(int count) {
    if (count < kMinEnemiesPerFloorSetting) count = kMinEnemiesPerFloorSetting;
    if (count > kMaxEnemiesPerFloorSetting) count = kMaxEnemiesPerFloorSetting;
    getSettings().game.caveOrdealsEnemiesPerFloor.setValue(count);
    config::Save();
}

int CaveOfOrdealsRandomizer::getMinEnemiesPerFloorSetting() const { return kMinEnemiesPerFloorSetting; }
int CaveOfOrdealsRandomizer::getMaxEnemiesPerFloorSetting() const { return kMaxEnemiesPerFloorSetting; }

void CaveOfOrdealsRandomizer::rerollSeed() {
    m_lastSeed = static_cast<unsigned int>(std::time(nullptr))
                 ^ (m_lastSeed * 6364136223846793005ULL + 1);
    std::srand(m_lastSeed);
    // Force an immediate re-spawn on the current floor on the next tick.
    m_lastRoomNo = -2;
}

// ---------------------------------------------------------------------------
// Per-frame tick – no delay, spawns immediately on room transition.
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::tick() {
    if (!isEnabled()) return;

    const char* stageName = dComIfGp_getStartStageName();
    if (stageName == nullptr || std::strcmp(stageName, kCaveStageName) != 0) {
        m_lastRoomNo = -2;
        return;
    }

    // Use getStayNo(); fall back to getStartStageRoomNo() while the stage
    // transition is still being committed (stayNo is briefly -1 then).
    int stayNo  = dComIfGp_roomControl_getStayNo();
    int startNo = static_cast<int>(dComIfGp_getStartStageRoomNo());
    int roomNo  = (stayNo >= 0) ? stayNo : startNo;

    if (roomNo < 0 || roomNo >= kCaveFloorCount) return;

    if (roomNo == m_lastRoomNo) return; // already handled this floor
    m_lastRoomNo = roomNo;

    // Spawn immediately – the room's original actors are created
    // synchronously during room load, so they already exist by the time
    // we observe the room-number change.
    spawnEnemiesForFloor(roomNo);
}

// ---------------------------------------------------------------------------
// Spawn enemies using existing actor positions as anchors
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::spawnEnemiesForFloor(int roomNo) {
    int count = getEnemiesPerFloor();
    if (count <= 0) {
        // User has set 0 enemies per floor – nothing to do.
        return;
    }

    EnemyPosScanData scan;
    scan.targetRoomNo = roomNo;
    scan.count        = 0;
    fopAcIt_Executor(collectEnemyPositions, &scan);

    if (scan.count == 0) {
        // No original enemies could be sampled in this room (e.g. the
        // room's enemies use a procname this module doesn't recognise,
        // or they were already defeated). Fall back to the player's own
        // position so that this room still receives spawns instead of
        // being silently skipped.
        daAlink_c* player = static_cast<daAlink_c*>(dComIfGp_getPlayer(0));
        if (player == nullptr) {
            return;
        }
        scan.positions[0] = player->current.pos;
        scan.count        = 1;
    }

    layer_class* savedLayer = fpcLy_CurrentLayer();
    base_process_class* playScene = fpcM_SearchByName(fpcNm_PLAY_SCENE_e);
    if (playScene != nullptr) {
        fpcLy_SetCurrentLayer(
            &reinterpret_cast<process_node_class*>(playScene)->layer);
    }

    cXyz scale(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        s16 enemyId = kEnemyPool[std::rand() % kEnemyPoolSize];

        int posIdx = std::rand() % scan.count;
        cXyz spawnPos = scan.positions[posIdx];

        // Ground-bound enemies must sit exactly on the floor; the sampled
        // anchor position may have come from a flying/floating original
        // enemy (e.g. Keese, Bubble), so re-snap the Y coordinate here.
        if (isGroundBoundEnemy(enemyId)) {
            spawnPos.y = snapToGroundY(spawnPos);
        }

        csXyz spawnAngle;
        spawnAngle.set(0, static_cast<s16>(std::rand() % 65536), 0);

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
