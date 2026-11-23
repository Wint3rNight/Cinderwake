// The dodge's movement over time (spec section 4): an exponential ease-out, so most of the distance
// comes early (1.6 m of 2.7 m in the first 0.25 s) and the end settles instead of stopping dead.
#pragma once

#include "CoreMinimal.h"

struct FDodgeProfile
{
	// Solves the decay so the dodge covers EarlyDistance by EarlyTime and Distance by Duration.
	static FDodgeProfile Make(float Distance, float Duration, float EarlyDistance, float EarlyTime);

	// The same shape and timing over a different distance (the backstep).
	FDodgeProfile Scaled(float NewDistance) const;

	float DistanceAt(float T) const;   // cm travelled; exactly the full distance from Duration on
	float SpeedAt(float T) const;      // cm/s; 0 from Duration on

	float GetDistance() const { return Distance; }
	float GetDuration() const { return Duration; }
	float GetStartSpeed() const { return SpeedAt(0.f); }

private:
	float Distance = 0.f;
	float Duration = 0.f;
	float Decay = 0.f;      // k in v(t) = v0 * e^(-k t)
	float StartSpeed = 0.f; // v0
};
