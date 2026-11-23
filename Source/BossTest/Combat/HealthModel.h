// Player health and soul fragments (spec sections 4 and 8).
#pragma once

#include "CoreMinimal.h"

struct FHealthModel
{
	float BaseMax = 300.f;
	float FragmentFraction = 0.15f;     // each fragment lowers max HP by 15% of BaseMax
	int32 MaxFragments = 2;             // 2 x 15% = the 30% cap
	float ReclaimPerFragment = 250.f;   // damage dealt to the boss that reclaims one fragment

	float Current = 300.f;
	int32 Fragments = 0;

	float Max() const;                      // BaseMax * (1 - FragmentFraction * Fragments)
	bool IsDead() const;
	float ApplyDamage(float Amount);        // returns the damage actually dealt
	void AddFragment();                     // clamps Current to Max(), never below 1
	void OnDamageDealtToBoss(float Amount); // reclaims fragments: raises Max, not Current
	void Heal(float Amount);                // up to Max()
	void ResetRuntime();                    // full health, no fragments or reclaim progress; keeps the tuning
	float GetReclaimProgress() const { return ReclaimProgress; } // damage dealt toward the next fragment

private:
	float ReclaimProgress = 0.f;
};
