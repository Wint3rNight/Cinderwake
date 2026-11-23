// Hit registration and the single damage-resolution path (spec section 12).
#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTypes.h"
#include "Combat/HealthModel.h"
#include "Combat/PoiseModel.h"

struct FCombatantState
{
	FHealthModel Health;
	FPoiseModel Poise;
	FCastBreak Cast;
	EArmor Armor = EArmor::None;
	bool bInvulnerable = false;   // dodge invulnerability, knockdown
	bool bImmune = false;         // transformation, intro
	bool bGuarding = false;       // a raised guard (the player); see GuardReduction and GuardArc
	float GuardReduction = 0.7f;  // share of a blocked hit's damage stopped; the rest is chip
	float GuardArc = 60.f;        // degrees from the front a guard covers
	double GuardPressTime = -1.0; // world time of the press that raised the guard with a perfect window, or -1
	float PerfectGuardFrom = 0.04f;  // a blockable hit this long after that press, up to PerfectGuardUntil, is a perfect guard
	float PerfectGuardUntil = 0.18f;
	float GuardRaiseTime = 0.18f;    // a press this long before the hit means the guard is up, even before the defender's own tick says so
	float PerfectGuardTrembleCm = 1.5f;     // the attacker shudders this far along the hit during a perfect guard's freeze
	float PerfectGuardHitStop = 0.10f;      // both fighters freeze this long on a perfect guard
	float HeavyPerfectGuardHitStop = 0.12f; // ... on a hit that would have knocked down
};

// Remembers which targets each attack instance has hit, and applies the hit policy.
class FHitRegistry
{
public:
	bool CanRegister(const FCombatHitContext& Hit) const;
	bool TryRegister(const FCombatHitContext& Hit);
	void ForgetInstance(uint32 AttackInstanceId);
	void Clear();                 // forget everything (encounter reset)

private:
	struct FRecord
	{
		int32 Count = 0;
		double LastTime = 0.0;
	};
	TMap<uint32, TMap<int32, FRecord>> Records;
};

// Order: dead target -> Invalid; immune -> Immune; already hit under the policy -> Invalid;
// invulnerable -> Dodged (doesn't use up the hit); a fresh guard press inside its window -> PerfectGuard (no
// damage); a guard in its arc -> Blocked (chip only); then health, cast counter, poise.
// A kill cancels any stagger; a stagger wins over a cast break.
FHitOutcome ResolveCombatHit(const FCombatHitContext& Hit, FCombatantState& Target, FHitRegistry& Registry);
