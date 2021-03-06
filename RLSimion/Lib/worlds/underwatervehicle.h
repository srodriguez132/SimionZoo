#pragma once

#include "world.h"

class SetPoint;
class RewardFunction;

class UnderwaterVehicle : public DynamicModel
{
	size_t m_sVSetpoint, m_sV, m_sVDeviation;
	size_t m_aUThrust;
	SetPoint *m_pSetpoint;
public:

	UnderwaterVehicle(ConfigNode* pParameters);
	virtual ~UnderwaterVehicle();

	void reset(State *s);
	void executeAction(State *s, const Action *a, double dt);
};
