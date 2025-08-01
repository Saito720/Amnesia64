#include "gui/ImGuiHPL.h"

#include <imgui.h>
#include "impl/imgui_impl_sdl2.h"
#include "impl/imgui_impl_opengl3.h"

namespace hpl {
	cImGui::cImGui() : iUpdateable("ImGui_HPL")
	{
		mbConsoleActive = false;
		strcpy_s(msAddress, sizeof(msAddress), "localhost");
		mlPort = 1234;
		mbSessionActive = false;
		mbHost = false;
		mbJoin = false;
		mbDisconnect = false;
	}

	cImGui::~cImGui() {}

	void cImGui::OnPostRender(float afFrameTime)
	{
		mbHost = false;
		mbJoin = false;
		mbDisconnect = false;

		if (mbConsoleActive)
		{
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplSDL2_NewFrame();
			ImGui::NewFrame();

			ImGui::SetNextWindowSize(ImVec2(400, 250), ImGuiCond_Once);
			ImGui::Begin("Multiplayer Game");
			ImGui::BeginDisabled(mbSessionActive);
			{
				ImGui::InputText("Address", msAddress, sizeof(msAddress));
				ImGui::InputInt("Port", &mlPort, 0);

				if (ImGui::Button("Host")) {
					mbHost = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("Join")) {
					mbJoin = true;
				}
			}
			ImGui::EndDisabled();

			if (mbSessionActive) {
				ImGui::SameLine();
				if (ImGui::Button("Disconnect")) {
					mbDisconnect = true;
				}
			}

			ImGui::End();
			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		}
	}
}
