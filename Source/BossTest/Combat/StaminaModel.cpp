#include "Combat/StaminaModel.h"

bool FStaminaModel::CanStartAction() const
{
	return Current > 0.f && !bExhausted;
}

void FStaminaModel::Spend(float Cost)
{
	Current -= Cost;
	if (Current <= 0.f)
	{
		Current = 0.f;
		bExhausted = true;
	}
	// Spending always restarts the wait, even outside an action.
	DelayLeft = bExhausted ? ExhaustedRegenDelay : RegenDelay;
}

void FStaminaModel::BeginAction()
{
	++ActionDepth;
}

void FStaminaModel::EndAction()
{
	if (ActionDepth > 0)
	{
		--ActionDepth;
	}
	if (ActionDepth == 0)
	{
		DelayLeft = bExhausted ? ExhaustedRegenDelay : RegenDelay;
	}
}

void FStaminaModel::SetSprinting(bool bOn)
{
	SetDrain(bOn ? SprintDrain : 0.f);
	bSprinting = DrainRate > 0.f;
}

void FStaminaModel::SetDrain(float PerSecond)
{
	const bool bWasDraining = DrainRate > 0.f;
	DrainRate = bExhausted ? 0.f : FMath::Max(0.f, PerSecond);
	if (bWasDraining && DrainRate <= 0.f)
	{
		DelayLeft = bExhausted ? ExhaustedRegenDelay : RegenDelay; // stopping a drain starts the usual wait
	}
}

void FStaminaModel::ResetRuntime()
{
	Current = Max;
	bExhausted = false;
	ActionDepth = 0;
	bSprinting = false;
	DrainRate = 0.f;
	DelayLeft = 0.f;
}

void FStaminaModel::Tick(float Dt)
{
	if (Dt <= 0.f || ActionDepth > 0)
	{
		return;
	}

	if (DrainRate > 0.f)
	{
		Current -= DrainRate * Dt;
		if (Current <= 0.f)
		{
			Current = 0.f;
			bExhausted = true;
			bSprinting = false;
			DrainRate = 0.f;
			DelayLeft = ExhaustedRegenDelay;
		}
		return;
	}

	// A long frame spends what's left of the delay first; only the remainder regenerates.
	const float DelayUsed = FMath::Min(DelayLeft, Dt);
	DelayLeft -= DelayUsed;
	const float RegenTime = Dt - DelayUsed;
	if (RegenTime > 0.f)
	{
		Current = FMath::Min(Max, Current + RegenRate * RegenTime);
	}

	if (bExhausted && Current >= ExhaustedUntil)
	{
		bExhausted = false;
	}
}

void FStaminaModel::RestartRegenDelay(float Seconds)
{
	DelayLeft = FMath::Max(0.f, Seconds);
}
