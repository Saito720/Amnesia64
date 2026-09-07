#ifndef LUX_SPAWN_HANDLER_H
#define LUX_SPAWN_HANDLER_H

#include "LuxBase.h"

// Session-local spawning and undo. Entries contain identity, never entity pointers:
// gameplay can destroy a spawned entity at any point before the next undo.
class cLuxSpawnHandler : public iLuxUpdateable
{
public:
	cLuxSpawnHandler();
	~cLuxSpawnHandler();

	bool SpawnEntity(const tString& asFile);
	bool UndoLastSpawn();
	tWString GetStatusText() const { return msStatus; }

	void Reset();
	void OnMapEnter(cLuxMap *apMap);
	void OnMapLeave(cLuxMap *apMap);
	void DestroyWorldEntities(cLuxMap *apMap);

private:
	struct cSpawnedEntity
	{
		int mlID;
		tString msName;
		tString msFile;
	};

	cLuxMap *mpMap;
	unsigned long mlNextName;
	std::vector<cSpawnedEntity> mvSpawnedEntities;
	tWString msStatus;
};

#endif
