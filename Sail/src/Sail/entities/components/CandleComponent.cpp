#include "pch.h"
#include "CandleComponent.h"

#ifdef DEVELOPMENT
void CandleComponent::imguiRender(Entity** e) {
	ImGui::InputInt("Lives", &respawns);
	ImGui::InputFloat("HP", &health);
}
#endif
