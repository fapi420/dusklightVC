#include "imgui.h"

#include "ImGuiMenuTools.hpp"
#include "dusk/cave_of_ordeals.h"
#include "d/d_com_inf_game.h"

#include <cstring>

namespace dusk {

void ImGuiMenuTools::ShowCaveOfOrdealsRandomizer() {
    if (!m_showCaveOfOrdealsRandomizer) {
        return;
    }

    if (!ImGui::Begin("Cave of Ordeals Randomizer", &m_showCaveOfOrdealsRandomizer)) {
        ImGui::End();
        return;
    }

    CaveOfOrdealsRandomizer& coor = CaveOfOrdealsRandomizer::instance();

    // -----------------------------------------------------------------------
    // Status banner
    // -----------------------------------------------------------------------
    const char* stageName = dComIfGp_getStartStageName();
    bool inCave = (stageName != nullptr && std::strcmp(stageName, "D_SB01") == 0);

    if (inCave) {
        int floor = dComIfGp_roomControl_getStayNo() + 1; // 1-indexed for display
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                           "In Cave of Ordeals – Floor %d / 51", floor);
    } else {
        ImGui::TextDisabled("Not currently in the Cave of Ordeals (D_SB01)");
    }

    ImGui::Separator();

    // -----------------------------------------------------------------------
    // Main toggle
    // -----------------------------------------------------------------------
    bool enabled = coor.isEnabled();
    if (ImGui::Checkbox("Enable Random Enemy Spawning", &enabled)) {
        coor.setEnabled(enabled);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(350.0f);
        ImGui::TextUnformatted(
            "When ON: entering any floor of the Cave of Ordeals spawns a "
            "random set of enemies in addition to (or instead of) the "
            "original enemies loaded from the ISO.\n\n"
            "When OFF: the cave loads exactly as the original game defines it. "
            "The ISO is never modified.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    // -----------------------------------------------------------------------
    // Seed display + reroll
    // -----------------------------------------------------------------------
    if (enabled) {
        ImGui::Spacing();
        ImGui::SeparatorText("Enemy Count");

        int enemiesPerFloor = coor.getEnemiesPerFloor();
        int minVal = coor.getMinEnemiesPerFloorSetting();
        int maxVal = coor.getMaxEnemiesPerFloorSetting();
        if (ImGui::SliderInt("Enemies per floor", &enemiesPerFloor, minVal, maxVal)) {
            coor.setEnemiesPerFloor(enemiesPerFloor);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(350.0f);
            ImGui::TextUnformatted(
                "How many additional enemies are spawned each time you enter "
                "a floor. This is a fixed amount, not randomized. Set to 0 "
                "to disable spawning without turning the feature off.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Randomization Seed");
        ImGui::Text("Current seed: 0x%08X", coor.getLastSeed());

        if (ImGui::Button("Re-roll Seed")) {
            coor.rerollSeed();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Generates a new seed and re-randomizes the current floor.");

        // -----------------------------------------------------------------------
        // Live hint about what will happen
        // -----------------------------------------------------------------------
        ImGui::Spacing();
        ImGui::SeparatorText("How it works");
        ImGui::TextWrapped(
            "Each time you enter a new floor, %d random enemies from the "
            "Cave's enemy pool are spawned at the same positions as the "
            "floor's original enemies. The spawn happens immediately, "
            "alongside the original enemies, with no delay.",
            enemiesPerFloor);
    }

    ImGui::End();
}

} // namespace dusk
