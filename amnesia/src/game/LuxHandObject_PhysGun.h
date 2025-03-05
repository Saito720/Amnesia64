#ifndef LUX_HAND_OBJECT_PHYSGUN_H
#define LUX_HAND_OBJECT_PHYSGUN_H

#include "LuxHandObject.h"

class cLuxHandObject_PhysGun : public iLuxHandObject
{
public:
	cLuxHandObject_PhysGun(const tString& asName, cLuxPlayerHands* apHands) : iLuxHandObject(asName, apHands) {};
	~cLuxHandObject_PhysGun() {};

	void LoadImplementedVars(cXmlElement* apVarsElem) {};

	void ImplementedCreateEntity(cLuxMap* apMap) {};
	void ImplementedDestroyEntity(cLuxMap* apMap) {};

	void ImplementedReset() {};

	void Update(float afTimeStep) {};

	bool DoAction(eLuxPlayerAction aAction, bool abPressed) { return false; };
	bool AnimationIsOver() { return true; };
};

#endif // LUX_HAND_OBJECT_PHYSGUN_H
