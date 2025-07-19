#include "gui/ImGuiHPL.h"

#include <imgui.h>
#include "impl/imgui_impl_sdl2.h"
#include "impl/imgui_impl_opengl3.h"

namespace hpl {
	cImGui::cImGui() : iUpdateable("ImGui_HPL") {}

	cImGui::~cImGui() {}

	void cImGui::OnPostRender(float afFrameTime)
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("HPL2 Engine Debug");
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();
		ImGui::Render();

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
}
