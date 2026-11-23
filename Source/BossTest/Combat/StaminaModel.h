// Player stamina rules (spec section 4).
#pragma once

#include "CoreMinimal.h"

struct FStaminaModel
{
	float Max = 120.f;
	float RegenRate = 40.f;           // per second
	float RegenDelay = 0.85f;         // after an action ends
	float ExhaustedRegenDelay = 1.2f; // after the action that emptied the bar
	float ExhaustedUntil = 25.f;      // exhaustion lasts until stamina reaches this
	float SprintDrain = 12.f;         // per second

	float Current = 120.f;
	bool bExhausted = false;

	bool CanStartAction() const;  // Current > 0 and not exhausted
	void Spend(float Cost);       // can overdraw to 0, which sets bExhausted
	void BeginAction();           // no regeneration while an action runs
	void EndAction();             // the regen delay starts here (ExhaustedRegenDelay if exhausted)
	void SetSprinting(bool bOn);  // drains at SprintDrain, blocks regeneration (SetDrain(SprintDrain or 0))
	void SetDrain(float PerSecond);  // 0 = none; drains and blocks regeneration (sprint, guard); stopping starts the regen delay
	bool IsDraining() const { return DrainRate > 0.f; }
	void RestartRegenDelay(float Seconds); // the wait before regeneration starts again from now (the perfect guard's overdraw)
	void Tick(float Dt);
	void ResetRuntime();          // full bar, no exhaustion or pending actions; keeps the tuning

private:
	int32 ActionDepth = 0;        // a counter, so overlapping actions (light into light) keep regen off
	bool bSprinting = false;
	float DrainRate = 0.f;
	float DelayLeft = 0.f;
};
