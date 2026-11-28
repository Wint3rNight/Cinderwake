// The flinch on Sevarog when your hit lands (the feel pass after Step 7, 2026-11-29: "landing hits on him feels like hitting
// air"). His armour means he keeps swinging, so the reaction is procedural: his upper spine tilts away from the hit and
// springs back, on top of whatever clip is playing. Plain maths, so it's unit tested; UBossAnimInstance applies it.
#pragma once

#include "CoreMinimal.h"

namespace HitFlinch
{
	// The tilt in degrees T seconds after a hit of Amplitude: up over Rise (eased out), back down over Fall (eased), then 0.
	BOSSTEST_API float Envelope(float T, float Amplitude, float Rise, float Fall);

	// How far into the rise a flinch already at Current degrees is, for a new one of Amplitude to carry on from without a pop.
	BOSSTEST_API float RiseTimeFor(float Current, float Amplitude, float Rise);

	// The axis (component space) that tilts the top of the spine along HitDir: Up x HitDir. Zero for a vertical hit.
	BOSSTEST_API FVector AxisFor(const FVector& HitDirComponent);

	// Tilts the chain root, pelvis, spine_01, spine_02, spine_03 (local transforms) by AngleDeg about Axis (component space):
	// LowerShare of it at spine_02, the rest at spine_03, so the bend spreads over the chest. Lower bones are untouched.
	BOSSTEST_API void Apply(FTransform (&Chain)[5], const FVector& AxisComponent, float AngleDeg, float LowerShare = 0.4f);

	// The strain while he holds a swing up (2026-11-29: "for a very brief moment looks like he has frozen"): a lean back that
	// builds to LeanMax over LeanTime, plus a small tremble of +-Tremble degrees, faded in over the first 0.15 s. T is seconds
	// into the hold. Visual only: the hold's timing and the swing's hit window don't change.
	BOSSTEST_API float HoldStrain(float T, float LeanMax, float LeanTime, float Tremble);
}
