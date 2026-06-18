#include "dusk/cave_of_ordeals.h"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_layer.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_node.h"
#include "f_pc/f_pc_name.h"
#include "SSystem/SComponent/c_sxyz.h"
#include "SSystem/SComponent/c_xyz.h"

#include <cstring>
#include <cstdlib>
#include <ctime>

namespace dusk {

// ---------------------------------------------------------------------------
// Enemy pool: actor IDs of enemies that can appear in the Cave of Ordeals.
// These correspond to the fpcNm_E_*_e values in f_pc/f_pc_name.h.
// ---------------------------------------------------------------------------
static const s16 kEnemyPool[] = {
    0x1B0, // E_GS  – Skulltula
    0x1B3, // E_DN  – Darknut
    0x1B7, // E_BS  – Bubble (fire)
    0x1B8, // E_SF  – Stalfos (with fire)
    0x1B9, // E_SH  – Lizalfos (Shield)
    0x1BC, // E_MD  – Moblin (Darknut-type)
    0x1BD, // E_SM  – Skulltula (gold/mini)
    0x1BE, // E_SM2 – Giant Skulltula
    0x1BF, // E_ST  – Stalfos
    0x1C1, // E_SB  – Skulltula (silver)
    0x1C2, // E_TH  – Tektite (Red/Blue)
    0x1C3, // E_CR  – Lizalfos
    0x1C5, // E_DB  – Deku Baba
    0x1C8, // E_GB  – Bokoblin
    0x1C9, // E_HB  – Helmasaur
    0x1CC, // E_YD  – Poe
    0x1D0, // E_TK  – Keese
    0x1D4, // E_RD  – ReaDead / Gibdo
    0x1D9, // E_PM  – Puppet (Lizalfos-type)
    0x0EF, // E_WB  – Wolfos
    0x1E5, // E_FB  – Fire Baba
};
static const int kEnemyPoolSize = static_cast<int>(sizeof(kEnemyPool) / sizeof(kEnemyPool[0]));

// How many enemies to place per floor (min / max, randomly chosen).
static constexpr int kMinEnemiesPerFloor = 1;
static constexpr int kMaxEnemiesPerFloor = 4;

// Cave of Ordeals stage name and total number of rooms (floors 0-50).
static constexpr const char* kCaveStageName = "D_SB01";
static constexpr int kCaveFloorCount = 51;

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

CaveOfOrdealsRandomizer& CaveOfOrdealsRandomizer::instance() {
    static CaveOfOrdealsRandomizer s_instance;
    return s_instance;
}

CaveOfOrdealsRandomizer::CaveOfOrdealsRandomizer()
    : m_enabled(false)
    , m_lastRoomNo(-1)
    , m_lastSeed(0)
{
    // Seed the RNG once at startup.
    m_lastSeed = static_cast<unsigned int>(std::time(nullptr));
    std::srand(m_lastSeed);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!m_enabled) {
        // Reset tracking so a fresh randomization happens next time.
        m_lastRoomNo = -1;
    }
}

bool CaveOfOrdealsRandomizer::isEnabled() const {
    return m_enabled;
}

unsigned int CaveOfOrdealsRandomizer::getLastSeed() const {
    return m_lastSeed;
}

void CaveOfOrdealsRandomizer::rerollSeed() {
    m_lastSeed = static_cast<unsigned int>(std::time(nullptr)) ^ (m_lastSeed * 6364136223846793005ULL + 1);
    std::srand(m_lastSeed);
    m_lastRoomNo = -1; // Force a respawn on the current floor.
}

// ---------------------------------------------------------------------------
// Per-frame tick – called from ImGuiConsole::Draw (after the game has ticked).
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::tick() {
    if (!m_enabled) {
        return;
    }

    // Check whether we are currently inside the Cave of Ordeals.
    const char* stageName = dComIfGp_getStartStageName();
    if (stageName == nullptr || std::strcmp(stageName, kCaveStageName) != 0) {
        // Left the cave – reset tracking so next entry gets fresh randomization.
        m_lastRoomNo = -1;
        return;
    }

    // Current room == current floor (0-indexed).
    int roomNo = dComIfGp_roomControl_getStayNo();
    if (roomNo < 0 || roomNo >= kCaveFloorCount) {
        return;
    }

    // Only spawn once per room transition.
    if (roomNo == m_lastRoomNo) {
        return;
    }
    m_lastRoomNo = roomNo;

    spawnEnemiesForFloor(roomNo);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::spawnEnemiesForFloor(int roomNo) {
    daAlink_c* player = static_cast<daAlink_c*>(dComIfGp_getPlayer(0));
    if (player == nullptr) {
        return;
    }

    // Set the layer context to the play scene so actors attach correctly.
    layer_class* savedLayer = fpcLy_CurrentLayer();
    base_process_class* playScene = fpcM_SearchByName(fpcNm_PLAY_SCENE_e);
    if (playScene != nullptr) {
        fpcLy_SetCurrentLayer(&(reinterpret_cast<process_node_class*>(playScene))->layer);
    }

    // Determine how many enemies to spawn this floor.
    int count = kMinEnemiesPerFloor
                + (std::rand() % (kMaxEnemiesPerFloor - kMinEnemiesPerFloor + 1));

    // Scale difficulty slightly towards the deeper floors.
    // Every 10 floors we allow one extra enemy (capped at kMaxEnemiesPerFloor + 2).
    int bonus = roomNo / 10;
    count = count + bonus;
    if (count > kMaxEnemiesPerFloor + 2) {
        count = kMaxEnemiesPerFloor + 2;
    }

    cXyz basePos = player->current.pos;
    csXyz angle;
    angle.set(0, static_cast<s16>(std::rand() % 65536), 0);
    cXyz scale(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        // Pick a random enemy from the pool.
        s16 enemyId = kEnemyPool[std::rand() % kEnemyPoolSize];

        // Spread enemies in a small circle around the player's spawn position.
        float angle_rad = (static_cast<float>(i) / static_cast<float>(count)) * 6.2831853f;
        cXyz spawnPos = basePos;
        spawnPos.x += std::cosf(angle_rad) * 200.0f;
        spawnPos.z += std::sinf(angle_rad) * 200.0f;

        csXyz spawnAngle;
        spawnAngle.set(0, static_cast<s16>(std::rand() % 65536), 0);

        fopAcM_create(
            enemyId,
            static_cast<u32>(-1), // default params
            &spawnPos,
            roomNo,
            &spawnAngle,
            &scale,
            static_cast<s8>(-1)  // default argument
        );
    }

    fpcLy_SetCurrentLayer(savedLayer);
}

} // namespace dusk
