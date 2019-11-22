#include "pch.h"
#include "InGameGui.h"
#include "Sail/Application.h"
#include "Sail/utils/SailImGui/CustomImGuiComponents/CustomImGuiComponents.h"
#include "Sail/utils/GameDataTracker.h"

#include "Sail/entities/components/SanityComponent.h"
#include "Sail/entities/components/SprintingComponent.h"
#include "Sail/entities/components/CrosshairComponent.h"
#include "Sail/entities/components/CandleComponent.h"
#include "Sail/entities/components/SpectatorComponent.h"

InGameGui::InGameGui(bool showWindow) {
}

InGameGui::~InGameGui() {
}

void InGameGui::renderWindow() {
	float screenWidth = Application::getInstance()->getWindow()->getWindowWidth();
	float screenHeight = Application::getInstance()->getWindow()->getWindowHeight();
	float progresbarLenght = 300;
	float progresbarHeight = 40;
	float outerPadding = 50;

	ImGui::SetNextWindowPos(ImVec2(
		screenWidth - progresbarLenght,
		screenHeight - progresbarHeight * 3
	));

	ImGui::SetNextWindowSize(ImVec2(
		progresbarLenght,
		progresbarHeight - progresbarHeight * 2.2
	));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	flags |= ImGuiWindowFlags_NoResize;
	flags |= ImGuiWindowFlags_NoMove;
	flags |= ImGuiWindowFlags_NoNav; 
	flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
	flags |= ImGuiWindowFlags_NoTitleBar;
	flags |= ImGuiWindowFlags_AlwaysAutoResize; 
	flags |= ImGuiWindowFlags_NoSavedSettings;
	flags |= ImGuiWindowFlags_NoBackground;
	ImGui::Begin("GUI", NULL, flags);

	if (m_player && !m_player->hasComponent<SpectatorComponent>()) {

		SanityComponent* c1 = m_player->getComponent<SanityComponent>();
		SprintingComponent* c2 = m_player->getComponent<SprintingComponent>();
		CandleComponent* c3;
		for (auto e : m_player->getChildEntities()) {
			if (!e->isAboutToBeDestroyed() && e->hasComponent<CandleComponent>()) {
				c3 = e->getComponent<CandleComponent>();
			}
		}
		if (c1) {
			float val = c1->sanity / 100.f;
			float val_inv = 1 - val;

			CustomImGui::CustomProgressBar(val, ImVec2(-1, 0), "Sanity", ImVec4(1 - val_inv * 0.3, 0.6 - val_inv * 0.6, 0, 1));
		}

		if (c2) {
			float val;
			float val_inv;
			ImVec4 color;

			if (c2->exhausted) {
				val = (c2->downTimer / MAX_SPRINT_DOWN_TIME);
				val_inv = 1 - val;
				color = ImVec4(0.5, 0.5, 0.5, 1);
			}
			else {
				val = 1 - (c2->sprintTimer / MAX_SPRINT_TIME);
				val_inv = 1 - val;
				color = ImVec4(1 - val_inv * 0.3, 0.6 - val_inv * 0.6, 0, 1);
			}
			CustomImGui::CustomProgressBar(val, ImVec2(-1, 0), "Stamina", color);
		}
		ImGui::End();
		ImGui::Begin("TorchThrowButton", NULL, flags);
		if (c3) {
			auto* imguiHandler = Application::getInstance()->getImGuiHandler();
			Texture& testTexture = Application::getInstance()->getResourceManager().getTexture("Icons/TorchThrow2.tga");

			if (  c3->isLit && c3->isCarried && c3->candleToggleTimer > 2.f) {
				ImGui::Image(imguiHandler->getTextureID(&testTexture), ImVec2(55, 55),ImVec2(0,0),ImVec2(1,1),ImVec4(1,1,1,1));
			}
			else {
				ImGui::Image(imguiHandler->getTextureID(&testTexture), ImVec2(55, 55), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0.3f, 0.3f, 0.3f, 1));
			}
			ImGui::SetWindowPos(ImVec2(screenWidth - ImGui::GetWindowSize().x - 300, screenHeight - ImGui::GetWindowSize().y - 50));
			ImGui::End();
		}
		int nrOfTorchesLeft = GameDataTracker::getInstance().getTorchesLeft();
		auto* imguiHandler = Application::getInstance()->getImGuiHandler();
		Texture& testTexture = Application::getInstance()->getResourceManager().getTexture("Icons/TorchLeft.tga");
		if (ImGui::Begin("TorchesLeft", nullptr, flags)) {
			for (int i = 0; i < nrOfTorchesLeft; i++) {
				ImGui::Image(imguiHandler->getTextureID(&testTexture), ImVec2(55, 55));
				ImGui::SameLine(0.f, 0);
			}
			ImGui::SetWindowPos(ImVec2(
				screenWidth - ImGui::GetWindowSize().x,
				screenHeight - ImGui::GetWindowSize().y - 110
			));
		}
	}

	ImGui::End();


	if (m_crosshairEntity) {
		if (!m_crosshairEntity->getComponent<CrosshairComponent>()->sprinting) {
			renderCrosshair(screenWidth, screenHeight);
		}
	}

	renderNumberOfPlayersLeft(screenWidth, screenHeight);

}

void InGameGui::setPlayer(Entity* player) {
	m_player = player;
}

void InGameGui::setCrosshair(Entity* pCrosshairEntity) {
	m_crosshairEntity = pCrosshairEntity;
}

void InGameGui::renderCrosshair(float screenWidth, float screenHeight) {
	// Crosshair settings window
	CrosshairComponent* c = m_crosshairEntity->getComponent<CrosshairComponent>();

	// Crosshair
	ImVec2 crosshairSize{
		c->size,
		c->size
	};
	ImVec2 center{
		screenWidth * 0.505f,
		screenHeight * 0.55f
	};
	ImVec2 topLeft{
		center.x - crosshairSize.x * 0.5f,
		center.y - crosshairSize.y * 0.5f
	};
	ImVec2 top{
		topLeft.x + crosshairSize.x * 0.5f,
		topLeft.y
	};
	ImVec2 bot{
		center.x,
		center.y + crosshairSize.y * 0.5f
	};
	ImVec2 right{
		center.x + crosshairSize.x * 0.5f,
		center.y
	};
	ImVec2 left{
		center.x - crosshairSize.x * 0.5f,
		center.y
	};

	ImGui::SetNextWindowPos(topLeft);
	ImGui::SetNextWindowSize(crosshairSize);

	ImGuiWindowFlags crosshairFlags = ImGuiWindowFlags_NoCollapse;
	crosshairFlags |= ImGuiWindowFlags_NoResize;
	crosshairFlags |= ImGuiWindowFlags_NoMove;
	crosshairFlags |= ImGuiWindowFlags_NoNav;
	crosshairFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
	crosshairFlags |= ImGuiWindowFlags_NoTitleBar;
	crosshairFlags |= ImGuiWindowFlags_NoBackground;
	ImGui::Begin("Crosshair", NULL, crosshairFlags);

	const ImU32 color = ImColor(c->color);

	ImVec2 center_padded_top{
		top.x,
		center.y - c->centerPadding
	};
	ImVec2 center_padded_bot{
		top.x,
		center.y + c->centerPadding
	};
	ImVec2 center_padded_right{
		center.x + c->centerPadding,
		right.y
	};
	ImVec2 center_padded_left{
		center.x - c->centerPadding,
		left.y
	};

	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	//		|
	//   
	//
	draw_list->AddLine(
		top,
		center_padded_top,
		color,
		c->thickness
	);

	//		|
	//   
	//		|
	draw_list->AddLine(
		bot,
		center_padded_bot,
		color,
		c->thickness
	);

	//		|
	//		    --
	//		|
	draw_list->AddLine(
		right,
		center_padded_right,
		color,
		c->thickness
	);

	//		|
	//	--	   --
	//		|
	draw_list->AddLine(
		left,
		center_padded_left,
		color,
		c->thickness
	);



	// Set to True/False by  CrosshairSystem
	if (c->currentlyAltered) {
		ImVec2 topRight{
			right.x,
			top.y
		};
		ImVec2 botRight{
			right.x,
			center.y + c->size * 0.5f
		};
		ImVec2 botLeft{
			left.x,
			botRight.y
		};

		ImVec2 center_padded_topLeft{
			center.x - c->centerPadding,
			center.y - c->centerPadding
		};
		ImVec2 center_padded_topRight{
			center.x + c->centerPadding,
			center.y - c->centerPadding
		};
		ImVec2 center_padded_botRight{
			center.x + c->centerPadding,
			center.y + c->centerPadding
		};
		ImVec2 center_padded_botLeft{
			center.x - c->centerPadding,
			center.y + c->centerPadding
		};

		// Set alpha-value of the color based on how long it has been altered for (F1->0)
		ImVec4 onHitColor = c->color;
		onHitColor.w = 1 - (c->passedTimeSinceAlteration / c->durationOfAlteredCrosshair);
		const ImU32 onHitcolor = ImColor(onHitColor);

		//	\
		//
		//
		// Draw an additional cross
		draw_list->AddLine(
			topLeft,
			center_padded_topLeft,
			onHitcolor,
			c->thickness
		);
		//	\	/
		//
		//
		// Draw an additional cross
		draw_list->AddLine(
			topRight,
			center_padded_topRight,
			onHitcolor,
			c->thickness
		);
		//	\	/
		//
		//		\
		// Draw an additional cross
		draw_list->AddLine(
			botRight,
			center_padded_botRight,
			onHitcolor,
			c->thickness
		);
		//	\	/
		//
		//	/   \
		// Draw an additional cross
		draw_list->AddLine(
			botLeft,
			center_padded_botLeft,
			onHitcolor,
			c->thickness
		);
	}

	ImGui::End();
}

void InGameGui::renderNumberOfPlayersLeft(float screenWidth, float screenHeight) {

}