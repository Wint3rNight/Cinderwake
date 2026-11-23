#include "Combat/HitResolver.h"

namespace
{
	// Floating-point slack for interval checks, so a hit exactly one interval later counts.
	constexpr double IntervalSlack = 1e-6;
	// The same slack for the perfect guard's window, so a hit exactly at its edges counts.
	constexpr double WindowSlack = 1e-6;
}

bool FHitRegistry::CanRegister(const FCombatHitContext& Hit) const
{
	const TMap<int32, FRecord>* Targets = Records.Find(Hit.AttackInstanceId);
	const FRecord* Record = Targets ? Targets->Find(Hit.TargetKey) : nullptr;
	if (!Record)
	{
		return true;
	}

	switch (Hit.Policy)
	{
	case EHitPolicy::MultiHit:
	case EHitPolicy::PersistentContact:
	{
		// A max of 0 or less means unlimited (persistent contact ticks until contact ends).
		const bool bUnderMax = Hit.MultiHitMax <= 0 || Record->Count < Hit.MultiHitMax;
		const bool bIntervalPassed = Hit.Time - Record->LastTime >= Hit.MultiHitInterval - IntervalSlack;
		return bUnderMax && bIntervalPassed;
	}
	case EHitPolicy::SingleHitPerTarget:
	case EHitPolicy::Projectile:
	default:
		return false; // one hit per target per instance (each projectile is its own instance)
	}
}

bool FHitRegistry::TryRegister(const FCombatHitContext& Hit)
{
	if (!CanRegister(Hit))
	{
		return false;
	}
	FRecord& Record = Records.FindOrAdd(Hit.AttackInstanceId).FindOrAdd(Hit.TargetKey);
	++Record.Count;
	Record.LastTime = Hit.Time;
	return true;
}

void FHitRegistry::ForgetInstance(uint32 AttackInstanceId)
{
	Records.Remove(AttackInstanceId);
}

void FHitRegistry::Clear()
{
	Records.Empty();
}

FHitOutcome ResolveCombatHit(const FCombatHitContext& Hit, FCombatantState& Target, FHitRegistry& Registry)
{
	FHitOutcome Out;

	if (Target.Health.IsDead())
	{
		Out.Result = EHitResult::Invalid;
		return Out;
	}
	if (Target.bImmune)
	{
		Out.Result = EHitResult::Immune;
		return Out;
	}
	if (!Registry.CanRegister(Hit))
	{
		Out.Result = EHitResult::Invalid;
		return Out;
	}
	if (Target.bInvulnerable)
	{
		// Checked before registering, so the same sweep can still land once invulnerability ends.
		Out.Result = EHitResult::Dodged;
		return Out;
	}

	// Step 4c: a guard raised by a fresh press just before a blockable hit deflects it perfectly: no damage,
	// the hit used up, the raw damage reported (the defender's stamina cost, the poise sent back to the attacker).
	// Checked before the block, because the guard is only up from 0.18 s and the window reaches it.
	const double SincePress = Target.GuardPressTime >= 0.0 ? Hit.WorldTime - Target.GuardPressTime : -1.0;
	if (Hit.bBlockable && Hit.AngleFromTargetFront <= Target.GuardArc && Target.GuardPressTime >= 0.0)
	{
		if (SincePress >= Target.PerfectGuardFrom - WindowSlack && SincePress <= Target.PerfectGuardUntil + WindowSlack)
		{
			Registry.TryRegister(Hit);
			Out.Result = EHitResult::PerfectGuard;
			Out.BlockedDamage = Hit.Damage;
			Out.bKnockdown = Hit.bKnockdown;
			return Out;
		}
	}

	// Step 4: a raised guard stops a blockable hit from inside its arc. Chip damage still lands (and can
	// kill), the hit is used up, poise is untouched.
	// The guard counts as up from the press time too, so a block can't fall into the frame before the defender's
	// own tick raises bGuarding (nothing orders the two fighters' ticks).
	const bool bGuardUp = Target.bGuarding || (Target.GuardPressTime >= 0.0 && SincePress >= Target.GuardRaiseTime - WindowSlack);
	if (bGuardUp && Hit.bBlockable && Hit.AngleFromTargetFront <= Target.GuardArc)
	{
		Registry.TryRegister(Hit);
		Out.Result = EHitResult::Blocked;
		Out.BlockedDamage = Hit.Damage;
		Out.bKnockdown = Hit.bKnockdown; // blocked, it doesn't knock down: it's a heavy block
		Out.DamageDealt = Target.Health.ApplyDamage(Hit.Damage * (1.f - Target.GuardReduction));
		if (Target.Health.IsDead())
		{
			Out.bKilled = true;
			Target.Cast.Close();
		}
		return Out;
	}

	Registry.TryRegister(Hit);
	Out.Result = EHitResult::Hit;
	Out.bKnockdown = Hit.bKnockdown;
	Out.DamageDealt = Target.Health.ApplyDamage(Hit.Damage);

	if (Target.Health.IsDead())
	{
		Out.bKilled = true; // a kill cancels any stagger or cast break
		Target.Cast.Close();
		return Out;
	}

	// The cast counter runs even while poise is immune; then poise itself.
	Out.bCastBroken = Target.Cast.Apply(Hit.PoiseDamage);
	Out.Poise = Target.Poise.Apply(Hit.PoiseDamage, Hit.bHeavy, Target.Armor);

	if (Out.Poise == EPoiseOutcome::Stagger)
	{
		Out.bCastBroken = false; // a stagger wins over a cast break; the cast ends either way
		Target.Cast.Close();
	}
	return Out;
}
