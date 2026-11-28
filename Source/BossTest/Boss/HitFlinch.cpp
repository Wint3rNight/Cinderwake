#include "Boss/HitFlinch.h"

float HitFlinch::Envelope(float T, float Amplitude, float Rise, float Fall)
{
	if (T < 0.f || Amplitude <= 0.f)
	{
		return 0.f;
	}
	if (T < Rise)
	{
		return Amplitude * FMath::Sin(HALF_PI * T / FMath::Max(Rise, 1e-4f)); // fast out, easing into the peak
	}
	const float Back = (T - Rise) / FMath::Max(Fall, 1e-4f);
	if (Back >= 1.f)
	{
		return 0.f;
	}
	const float Left = 1.f - Back;
	return Amplitude * Left * Left * (3.f - 2.f * Left); // smoothstep back to rest
}

float HitFlinch::RiseTimeFor(float Current, float Amplitude, float Rise)
{
	if (Amplitude <= 0.f || Current <= 0.f)
	{
		return 0.f;
	}
	return Rise * FMath::Asin(FMath::Clamp(Current / Amplitude, 0.f, 1.f)) / HALF_PI;
}

float HitFlinch::HoldStrain(float T, float LeanMax, float LeanTime, float Tremble)
{
	if (T <= 0.f)
	{
		return 0.f;
	}
	const float L = FMath::Clamp(T / FMath::Max(LeanTime, 1e-3f), 0.f, 1.f);
	const float Lean = LeanMax * L * L * (3.f - 2.f * L);
	// Two sines at unrelated rates read as a strain, not a vibration.
	const float Shake = 0.6f * FMath::Sin(2.f * PI * 7.3f * T) + 0.4f * FMath::Sin(2.f * PI * 11.9f * T + 1.3f);
	const float FadeIn = FMath::Clamp(T / 0.15f, 0.f, 1.f);
	return Lean + Tremble * Shake * FadeIn;
}

FVector HitFlinch::AxisFor(const FVector& HitDirComponent)
{
	return FVector::CrossProduct(FVector::UpVector, FVector(HitDirComponent.X, HitDirComponent.Y, 0.f)).GetSafeNormal();
}

void HitFlinch::Apply(FTransform (&Chain)[5], const FVector& AxisComponent, float AngleDeg, float LowerShare)
{
	if (AxisComponent.IsNearlyZero() || FMath::IsNearlyZero(AngleDeg))
	{
		return;
	}
	// Component-space rotations down the chain (a child's is its local times its parent's).
	FQuat Comp[5];
	Comp[0] = Chain[0].GetRotation();
	for (int32 i = 1; i < 5; ++i)
	{
		Comp[i] = Comp[i - 1] * Chain[i].GetRotation();
	}
	const float Radians = FMath::DegreesToRadians(AngleDeg);
	const FQuat Lower(AxisComponent, Radians * LowerShare);
	const FQuat Full(AxisComponent, Radians);
	// spine_02 turns by the lower share; spine_03 ends up turned by the full angle. Each local rotation is re-expressed
	// under its (already turned) parent, so translations ride along and nothing below spine_02 moves.
	const FQuat New2 = Lower * Comp[3];
	const FQuat New3 = Full * Comp[4];
	Chain[3].SetRotation((Comp[2].Inverse() * New2).GetNormalized());
	Chain[4].SetRotation((New2.Inverse() * New3).GetNormalized());
}
