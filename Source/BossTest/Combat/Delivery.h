// Delivery types beyond the weapon sweep (Step 5, spec section 12), as plain C++ so they're unit tested: the shockwave
// ring (Judgment Slam) and the projectile (Soul Mark). The boss's delivery component runs them on his clock and
// resolves their hits through the target's combatant.
#pragma once

#include "CoreMinimal.h"

// The stretch of radius a ring's edge swept in one step.
struct FRingBand
{
	float From = 0.f;
	float To = 0.f;
	// True if the band overlaps a target's capsule (its centre Distance cm from the ring's centre, horizontally).
	bool Touches(float Distance, float TargetRadius) const
	{
		return To >= Distance - TargetRadius && From <= Distance + TargetRadius;
	}
};

struct FShockwaveRing
{
	float Radius = 0.f;
	float Speed = 1000.f;      // cm/s (10 m/s)
	float MaxRadius = 500.f;   // cm (5 m from the impact)
	TSet<int32> Skip;          // target keys the direct strike already hit or that dodged it: one slam, one dodge

	FRingBand Advance(float Dt)
	{
		FRingBand Band;
		Band.From = Radius;
		Radius = FMath::Min(MaxRadius, Radius + Speed * FMath::Max(0.f, Dt));
		Band.To = Radius;
		return Band;
	}
	bool IsDone() const { return Radius >= MaxRadius; }
	bool CanHit(int32 TargetKey) const { return !Skip.Contains(TargetKey); }
};

struct FProjectileFlight
{
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;  // fixed at release: never steered
	float Travelled = 0.f;
	float MaxRange = 1500.f;

	// Moves along its line, never past its range. Returns the new position (the step ran from the old one).
	FVector Advance(float Dt)
	{
		const float Speed = Velocity.Size();
		if (Speed <= 0.f)
		{
			return Position;
		}
		const float Step = FMath::Min(Speed * FMath::Max(0.f, Dt), MaxRange - Travelled);
		Position += Velocity / Speed * Step;
		Travelled += Step;
		return Position;
	}
	bool IsSpent() const { return Travelled >= MaxRange - KINDA_SMALL_NUMBER; }
};

// True if a sphere of Radius moving from A to B touches an upright capsule (centre, radius, half height).
bool SegmentHitsCapsule(const FVector& A, const FVector& B, float Radius, const FVector& CapsuleCentre, float CapsuleRadius, float CapsuleHalfHeight);
