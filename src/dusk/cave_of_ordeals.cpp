#include "dusk/cave_of_ordeals.h"

#include "d/actor/d_a_alink.h"
#include "d/d_bg_s.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
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
// Enemy pool – exactly the enemies that appear in the original Cave of Ordeals.
// Source: known floor contents of D_SB01 (Floors 1-51).
// ---------------------------------------------------------------------------
static const s16 kEnemyPool[] = {
    0x1D0, // E_TK   – Keese           (Floors 1-3, 21-22)
    0x1AF, // E_AI   – Fire Keese      (Floors 2-3)
    0x1C8, // E_GB   – Bokoblin        (Floors 4-6, 11-13)
    0x1BD, // E_SM   – Skulltula       (Floors 7-9, 16)
    0x1B0, // E_GS   – Gold Skulltula  (Floors 8-9, 17-18)
    0x1BE, // E_SM2  – Giant Skulltula (Floors 10, 19-20)
    0x1C3, // E_CR   – Lizalfos        (Floors 14-15, 23-25)
    0x1B7, // E_BS   – Bubble          (Floors 16, 26-27)
    0x1BF, // E_ST   – Stalfos         (Floors 21-23, 31-33)
    0x1B8, // E_SF   – Fire Stalfos    (Floors 28-30)
    0x1CC, // E_YD   – Poe             (Floors 31-33)
    0x1D4, // E_RD   – ReDead / Gibdo  (Floors 34-36)
    0x0EF, // E_WB   – Wolfos          (Floors 37-39)
    0x209, // E_WW   – White Wolfos    (Floors 40-42)
    0x1B9, // E_SH   – Lizalfos        (Floors 40-42)
    0x1B3, // E_DN   – Darknut         (Floors 43-45, 49-51)
    0x1C7, // E_GA   – Aeralfos        (Floors 46-48)
    0x1C1, // E_SB   – Silver Skulltula (various)
};
static const int kEnemyPoolSize = static_cast<int>(sizeof(kEnemyPool) / sizeof(kEnemyPool[0]));

static constexpr int kMinEnemiesPerFloor = 2;
static constexpr int kMaxEnemiesPerFloor = 4;

static constexpr const char* kCaveStageName = "D_SB01";
static constexpr int kCaveFloorCount        = 51;

// How many frames to wait after a room transition before spawning.
// This gives the room geometry (collision) time to fully load so that
// ground-raycasts succeed and enemies land correctly.
static constexpr int kSpawnDelayFrames = 30;

// Radius around the player's XZ position to scatter enemies.
static constexpr float kSpawnRadius = 400.0f;

// Height above the detected ground to place the enemy (so it doesn't clip).
static constexpr float kSpawnGroundOffset = 10.0f;

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

CaveOfOrdealsRandomizer& CaveOfOrdealsRandomizer::instance() {
    static CaveOfOrdealsRandomizer s_instance;
    return s_instance;
}

CaveOfOrdealsRandomizer::CaveOfOrdealsRandomizer()
    : m_enabled(false)
    , m_lastRoomNo(-2)   // -2 = "never entered any room"
    , m_pendingRoomNo(-1)
    , m_framesUntilSpawn(-1)
    , m_lastSeed(0)
{
    m_lastSeed = static_cast<unsigned int>(std::time(nullptr));
    std::srand(m_lastSeed);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!m_enabled) {
        m_lastRoomNo      = -2;
        m_pendingRoomNo   = -1;
        m_framesUntilSpawn = -1;
    }
}

bool CaveOfOrdealsRandomizer::isEnabled() const { return m_enabled; }

unsigned int CaveOfOrdealsRandomizer::getLastSeed() const { return m_lastSeed; }

void CaveOfOrdealsRandomizer::rerollSeed() {
    m_lastSeed = static_cast<unsigned int>(std::time(nullptr))
                 ^ (m_lastSeed * 6364136223846793005ULL + 1);
    std::srand(m_lastSeed);
    // Force a re-spawn on the current floor next tick.
    m_lastRoomNo      = -2;
    m_framesUntilSpawn = -1;
    m_pendingRoomNo   = -1;
}

// ---------------------------------------------------------------------------
// Per-frame tick
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::tick() {
    if (!m_enabled) return;

    const char* stageName = dComIfGp_getStartStageName();
    if (stageName == nullptr || std::strcmp(stageName, kCaveStageName) != 0) {
        // Not in the cave – reset everything.
        m_lastRoomNo       = -2;
        m_pendingRoomNo    = -1;
        m_framesUntilSpawn = -1;
        return;
    }

    // getStayNo() returns -1 while transitioning / loading.
    // Fall back to getStartStageRoomNo() which is set as soon as the stage
    // transition is committed, giving us the correct floor on first entry.
    int stayNo   = dComIfGp_roomControl_getStayNo();
    int startNo  = static_cast<int>(dComIfGp_getStartStageRoomNo());
    int roomNo   = (stayNo >= 0) ? stayNo : startNo;

    if (roomNo < 0 || roomNo >= kCaveFloorCount) return;

    // -------------------------------------------------------------------
    // Detect room transition → start the spawn-delay timer.
    // -------------------------------------------------------------------
    if (roomNo != m_lastRoomNo) {
        m_lastRoomNo       = roomNo;
        m_pendingRoomNo    = roomNo;
        m_framesUntilSpawn = kSpawnDelayFrames;
    }

    // -------------------------------------------------------------------
    // Count down the delay timer.
    // -------------------------------------------------------------------
    if (m_framesUntilSpawn > 0) {
        --m_framesUntilSpawn;
        return;
    }

    // Timer just hit 0 – time to spawn.
    if (m_framesUntilSpawn == 0 && m_pendingRoomNo >= 0) {
        m_framesUntilSpawn = -1;
        spawnEnemiesForFloor(m_pendingRoomNo);
        m_pendingRoomNo = -1;
    }
}

// ---------------------------------------------------------------------------
// Spawn enemies for a given floor
// ---------------------------------------------------------------------------

void CaveOfOrdealsRandomizer::spawnEnemiesForFloor(int roomNo) {
    daAlink_c* player = static_cast<daAlink_c*>(dComIfGp_getPlayer(0));
    if (player == nullptr) return;

    // Attach new actors to the play-scene layer.
    layer_class* savedLayer = fpcLy_CurrentLayer();
    base_process_class* playScene = fpcM_SearchByName(fpcNm_PLAY_SCENE_e);
    if (playScene != nullptr) {
        fpcLy_SetCurrentLayer(
            &reinterpret_cast<process_node_class*>(playScene)->layer);
    }

    // Difficulty scaling: deeper floors get more enemies.
    int count = kMinEnemiesPerFloor
                + (std::rand() % (kMaxEnemiesPerFloor - kMinEnemiesPerFloor + 1))
                + (roomNo / 10);
    if (count > kMaxEnemiesPerFloor + 2) count = kMaxEnemiesPerFloor + 2;

    const cXyz& playerPos = player->current.pos;
    cXyz scale(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        s16 enemyId = kEnemyPool[std::rand() % kEnemyPoolSize];

        // Spread enemies in a circle around the player's XZ position.
        float angleRad = (static_cast<float>(i) / static_cast<float>(count))
                         * 6.2831853f;
        float offsetX = std::cosf(angleRad) * kSpawnRadius;
        float offsetZ = std::sinf(angleRad) * kSpawnRadius;

        // Raycast downward from well above the player to find the actual floor.
        cXyz probePos;
        probePos.x = playerPos.x + offsetX;
        probePos.y = playerPos.y + 2000.0f; // start high above
        probePos.z = playerPos.z + offsetZ;

        dBgS_ObjGndChk gndChk;
        gndChk.SetPos(&probePos);
        float groundY = dComIfG_Bgsp().GroundCross(&gndChk);

        // GroundCross returns -99999.0f when it finds nothing.
        // In that case fall back to the player's Y so the enemy is at least
        // in the same vertical plane.
        if (groundY < -9000.0f) {
            groundY = playerPos.y;
        }

        cXyz spawnPos;
        spawnPos.x = probePos.x;
        spawnPos.y = groundY + kSpawnGroundOffset;
        spawnPos.z = probePos.z;

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
