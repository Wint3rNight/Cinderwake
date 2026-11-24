// Shared combat types (Step 1: combat rules core). Plain C++, no UObjects, so the rules test without a world.
#pragma once

#include "CoreMinimal.h"

enum class EHitPolicy : uint8 { SingleHitPerTarget, MultiHit, PersistentContact, Projectile };
enum class EHitResult : uint8 { Hit, Blocked, PerfectGuard, Dodged, Immune, Invalid };
enum class EArmor : uint8 { None, Windup, Full };               // Windup: light hits can't stagger
enum class EPoiseOutcome : uint8 { None, Stagger, DeferredStagger };

struct FCombatHitContext
{
	uint32 AttackInstanceId = 0;
	int32 TargetKey = 0;
	float Damage = 0.f;
	float PoiseDamage = 0.f;
	bool bHeavy = false;            // heavy, dash and charged attacks
	bool bKnockdown = false;        // a landed hit knocks the target down instead of a normal reaction
	bool bBlockable = false;        // a weapon sweep: a guard can stop it (projectiles, tethers, bursts, rings can't)
	bool bSoulFragment = false;     // a soul hit: on a Hit, the target loses a fragment of max HP (spec section 8)
	float AngleFromTargetFront = 0.f; // degrees between the target's facing and the direction to the attacker
	EHitPolicy Policy = EHitPolicy::SingleHitPerTarget;
	float MultiHitInterval = 0.f;
	int32 MultiHitMax = 1;
	double Time = 0.0;              // gameplay clock, seconds
	double WorldTime = 0.0;         // the world clock when the hit resolves (the perfect guard's window is measured on it)
};

struct FHitOutcome
{
	EHitResult Result = EHitResult::Invalid;
	float DamageDealt = 0.f;
	EPoiseOutcome Poise = EPoiseOutcome::None;
	bool bCastBroken = false;
	bool bFragmentTaken = false;    // a soul hit landed and took a fragment
	bool bKilled = false;
	bool bKnockdown = false;        // on a Hit: knocked down; on a Blocked hit: a heavy block (no knockdown)
	float BlockedDamage = 0.f;      // a Blocked or PerfectGuard hit's raw damage (the stamina cost; a perfect sends it back as poise)
};
