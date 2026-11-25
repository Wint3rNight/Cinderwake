#pragma once

#include "CoreMinimal.h"

// Orientation warping, the fix Paragon shipped for four-direction locomotion (and Lyra's today). Locked on, the jog and the
// guard walk each have four clips (forward, right, back, left), so on a diagonal the nearest clip steps up to 60 degrees off
// the way he really moves. The warp turns the whole skeleton by what's left over, so his steps follow his movement, then turns
// the spine back by part of it, so his chest stays toward the target. Pure functions, so the unit tests can check the maths.
namespace OrientationWarp
{
	enum class EDir : uint8 { Forward, Right, Back, Left };

	struct FPick
	{
		EDir Dir = EDir::Forward;
		float Residual = 0.f; // degrees from the clip's direction to the real one: what the warp turns (-180 to 180)
	};

	// The clip's direction relative to his facing, in degrees: 0 ahead, 90 right, 180 back, -90 left.
	float Centre(EDir Dir);

	// AngleDeg: the way he moves relative to his facing (0 ahead, +90 right). The clip on now stays while the angle is within
	// KeepHalf degrees of its direction (15 degrees of hysteresis at the diagonals with the default), otherwise the nearest wins.
	BOSSTEST_API FPick Pick(float AngleDeg, TOptional<EDir> Current, float KeepHalf = 60.f);

	// Chain: local transforms of one parent chain, from the root (index 0, no parent) to the top of the spine. Turns the root
	// by AngleDeg about Up (component space, through the origin under his feet), then turns each bone from FirstSpine on back
	// about the vertical through it, sharing CounterAlpha of the angle equally, so the chest ends (1 - CounterAlpha) of the way.
	BOSSTEST_API void Apply(TArrayView<FTransform> Chain, int32 FirstSpine, float AngleDeg, float CounterAlpha, const FVector& Up = FVector::UpVector);
}
