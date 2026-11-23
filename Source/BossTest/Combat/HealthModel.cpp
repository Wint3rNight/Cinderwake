#include "Combat/HealthModel.h"

float FHealthModel::Max() const
{
	return BaseMax * (1.f - FragmentFraction * Fragments);
}

bool FHealthModel::IsDead() const
{
	return Current <= 0.f;
}

float FHealthModel::ApplyDamage(float Amount)
{
	const float Dealt = FMath::Clamp(Amount, 0.f, Current);
	Current -= Dealt;
	return Dealt;
}

void FHealthModel::AddFragment()
{
	if (Fragments >= MaxFragments)
	{
		return;
	}
	++Fragments;
	// A fragment lowers the ceiling. It can squeeze current health down, but never to death.
	if (Current > 0.f)
	{
		Current = FMath::Max(1.f, FMath::Min(Current, Max()));
	}
}

void FHealthModel::OnDamageDealtToBoss(float Amount)
{
	if (Fragments <= 0 || Amount <= 0.f)
	{
		return;
	}
	ReclaimProgress += Amount;
	while (Fragments > 0 && ReclaimProgress >= ReclaimPerFragment)
	{
		ReclaimProgress -= ReclaimPerFragment;
		--Fragments; // the max rises back; the space is there to heal into
	}
	if (Fragments == 0)
	{
		ReclaimProgress = 0.f;
	}
}

void FHealthModel::ResetRuntime()
{
	Fragments = 0;
	ReclaimProgress = 0.f;
	Current = BaseMax;
}

void FHealthModel::Heal(float Amount)
{
	if (IsDead() || Amount <= 0.f)
	{
		return;
	}
	Current = FMath::Min(Max(), Current + Amount);
}
