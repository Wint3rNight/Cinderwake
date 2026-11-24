// How a knockdown plays (after the Step 5 playtest). Paragon's knock clips are airborne poses (both feet 40-80 cm up:
// in the MOBA, code threw the hero), so on the ground he floated. A knockdown is the stun fall onto the floor instead,
// then the stun get-up (they join exactly), both at one rate so he's standing when the knockdown ends.
#pragma once

#include "CoreMinimal.h"

struct FKnockdownPlan
{
	float Rate = 1.f;     // play rate for both clips
	float GetUpAt = 0.f;  // real seconds after the hit when the get-up starts
};

// FallFrom-FallEnd: the part of the fall clip played (from the drop, not the standing stagger); then the whole get-up.
inline FKnockdownPlan PlanKnockdown(float FallFrom, float FallEnd, float GetUpLength, float Total)
{
	FKnockdownPlan P;
	const float Fall = FMath::Max(0.f, FallEnd - FallFrom);
	const float Clip = Fall + FMath::Max(0.f, GetUpLength);
	P.Rate = Total > 0.f && Clip > 0.f ? Clip / Total : 1.f;
	P.GetUpAt = Fall / P.Rate;
	return P;
}
