#ifndef IMGUI_HPL_H
#define IMGUI_HPL_H

#include "engine/Updateable.h"

namespace hpl {
	class cImGui : public iUpdateable
	{
	public:
		cImGui();
		~cImGui();

		void OnPostRender(float afFrameTime);
	private:
	};
};

#endif // IMGUI_HPL_H
