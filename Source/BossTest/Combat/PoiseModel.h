// The boss's poise, stagger and cast breaks (spec section 9).
#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTypes.h"

struct FPoiseModel
{
	float Threshold = 650.f;
	float DecayRate = 10.f;          // per second, once DecayDelay has passed without poise damage
	float DecayDelay = 3.f;
	float StaggerDuration = 1.25f;
	float ImmunityDuration = 8.f;    // from the end of a stagger

	// Adds poise damage. Ignored while staggered or immune. Reaching the threshold staggers now,
	// or defers the stagger under armour (Windup: light hits defer, heavy hits stagger; Full: everything defers).
	EPoiseOutcome Apply(float Amount, bool bHeavy, EArmor Armor);
	void BeginStagger();             // bar to 0; poise is ignored until immunity ends
	void Tick(float Dt);             // decay; stagger end starts immunity
	bool IsStaggered() const;
	bool IsImmune() const;
	bool HasDeferredStagger() const;
	float Current() const;
	void CancelDeferredStagger();    // death or the transformation skip the recovery that would play it
	void ResetRuntime();             // empty bar, no stagger, immunity or pending stagger; keeps the tuning

private:
	float Bar = 0.f;
	float SinceDamage = 0.f;
	float StaggerLeft = 0.f;
	float ImmunityLeft = 0.f;
	bool bDeferred = false;
};

// Interruptible casts break on their own threshold, counted only while the cast is open,
// and independent of the poise bar's immunity.
struct FCastBreak
{
	void Open(float InThreshold);    // 70 Soul Mark, 100 Soul Siphon
	void Close();
	bool IsOpen() const;
	bool Apply(float PoiseAmount);   // true when the cast breaks (it then closes)

private:
	float Threshold = 0.f;
	float Dealt = 0.f;
	bool bOpen = false;
};
