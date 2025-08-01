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
		void SetConsoleActive(bool abActive) { mbConsoleActive = abActive; };
		bool GetConsoleActive() const { return mbConsoleActive; };

		bool ShouldHost() const { return mbHost; }
		bool ShouldJoin() const { return mbJoin; }
		bool ShouldDisconnect() const { return mbDisconnect; }

		void SetSessionStatus(bool abActive) { mbSessionActive = abActive; };
		std::string GetAddress() const { return msAddress; }
		int GetPort() const { return mlPort; }

	private:
		bool mbConsoleActive;
		bool mbSessionActive;
		char msAddress[128];
		int	 mlPort;
		bool mbHost;
		bool mbJoin;
		bool mbDisconnect;
	};
};

#endif // IMGUI_HPL_H
