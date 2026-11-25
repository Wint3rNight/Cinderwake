#include "Combat/PoiseModel.h"

EPoiseOutcome FPoiseModel::Apply(float Amount, bool bHeavy, EArmor Armor)
{
	if (Amount <= 0.f || IsStaggered() || IsImmune())
	{
		return EPoiseOutcome::None;
	}
	if (bDeferred)
	{
		// The bar is already broken and waiting. A heavy hit still breaks windup armour (spec section 9).
		if (bHeavy && Armor == EArmor::Windup)
		{
			BeginStagger();
			return EPoiseOutcome::Stagger;
		}
		return EPoiseOutcome::None;
	}

	Bar += Amount;
	SinceDamage = 0.f;
	if (Bar < Threshold)
	{
		return EPoiseOutcome::None;
	}

	const bool bArmourHolds = (Armor == EArmor::Full) || (Armor == EArmor::Windup && !bHeavy);
	if (bArmourHolds)
	{
		// The stagger waits for the start of recovery. The bar stays full and stops decaying.
		Bar = Threshold;
		bDeferred = true;
		return EPoiseOutcome::DeferredStagger;
	}

	BeginStagger();
	return EPoiseOutcome::Stagger;
}

void FPoiseModel::BeginStagger()
{
	Bar = 0.f;
	SinceDamage = 0.f;
	bDeferred = false;
	StaggerLeft = StaggerDuration;
	StaggerLength = StaggerDuration;
	ImmunityLeft = 0.f;
}

void FPoiseModel::SetStaggerLength(float Length)
{
	if (!IsStaggered())
	{
		return;
	}
	const float Elapsed = StaggerElapsed();
	StaggerLength = FMath::Max(Length, Elapsed);
	StaggerLeft = FMath::Max(StaggerLength - Elapsed, KINDA_SMALL_NUMBER);
}

void FPoiseModel::Tick(float Dt)
{
	if (Dt <= 0.f)
	{
		return;
	}

	// One frame can cross several phases (stagger, then immunity, then decay), so spend the
	// time through them in order instead of treating the whole frame as one phase.
	if (StaggerLeft > 0.f)
	{
		if (Dt < StaggerLeft)
		{
			StaggerLeft -= Dt;
			return;
		}
		Dt -= StaggerLeft;
		StaggerLeft = 0.f;
		ImmunityLeft = ImmunityDuration;
	}

	if (ImmunityLeft > 0.f)
	{
		const float Used = FMath::Min(ImmunityLeft, Dt);
		ImmunityLeft -= Used;
		Dt -= Used;
		if (Dt <= 0.f)
		{
			return;
		}
	}

	if (bDeferred)
	{
		return; // a pending stagger holds the bar where it is
	}

	const float Before = SinceDamage;
	SinceDamage += Dt;
	const float DecayTime = FMath::Max(0.f, SinceDamage - FMath::Max(Before, DecayDelay));
	Bar = FMath::Max(0.f, Bar - DecayRate * DecayTime);
}

bool FPoiseModel::IsStaggered() const
{
	return StaggerLeft > 0.f;
}

bool FPoiseModel::IsImmune() const
{
	return ImmunityLeft > 0.f;
}

bool FPoiseModel::HasDeferredStagger() const
{
	return bDeferred;
}

float FPoiseModel::Current() const
{
	return Bar;
}

void FPoiseModel::CancelDeferredStagger()
{
	if (bDeferred)
	{
		bDeferred = false;
		Bar = 0.f;
		SinceDamage = 0.f;
	}
}

void FPoiseModel::ResetRuntime()
{
	Bar = 0.f;
	SinceDamage = 0.f;
	StaggerLeft = 0.f;
	StaggerLength = 0.f;
	ImmunityLeft = 0.f;
	bDeferred = false;
}

void FCastBreak::Open(float InThreshold)
{
	Threshold = InThreshold;
	Dealt = 0.f;
	bOpen = true;
}

void FCastBreak::Close()
{
	bOpen = false;
	Dealt = 0.f;
}

bool FCastBreak::IsOpen() const
{
	return bOpen;
}

bool FCastBreak::Apply(float PoiseAmount)
{
	if (!bOpen || PoiseAmount <= 0.f)
	{
		return false;
	}
	Dealt += PoiseAmount;
	if (Dealt >= Threshold)
	{
		Close();
		return true;
	}
	return false;
}
