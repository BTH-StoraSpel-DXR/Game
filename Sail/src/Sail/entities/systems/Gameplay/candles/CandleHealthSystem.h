#pragma once

#include "Sail/entities/systems/BaseComponentSystem.h"
#include "Sail/events/EventReceiver.h"

class CandleHealthSystem final : public BaseComponentSystem, public EventReceiver {
public:
	CandleHealthSystem();
	~CandleHealthSystem();

	void update(float dt) override;
	bool onEvent(const Event& event) override;

#ifdef DEVELOPMENT
	unsigned int getByteSize() const override;
#endif

private:
	// TODO: Replace using game settings when that is implemented
	int m_maxNumRespawns = 2;

};