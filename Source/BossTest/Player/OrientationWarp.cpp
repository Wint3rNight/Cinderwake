#include "Player/OrientationWarp.h"

float OrientationWarp::Centre(EDir Dir)
{
	switch (Dir)
	{
	case EDir::Right: return 90.f;
	case EDir::Back: return 180.f;
	case EDir::Left: return -90.f;
	default: return 0.f;
	}
}

OrientationWarp::FPick OrientationWarp::Pick(float AngleDeg, TOptional<EDir> Current, float KeepHalf)
{
	auto Off = [AngleDeg](EDir Dir) { return FMath::Abs(FMath::FindDeltaAngleDegrees(Centre(Dir), AngleDeg)); };
	FPick Out;
	if (Current.IsSet() && Off(*Current) <= KeepHalf)
	{
		Out.Dir = *Current;
	}
	else
	{
		// The nearest, ties going forward, then right, then left (as the jog and the guard walk have always picked).
		Out.Dir = Off(EDir::Forward) <= 45.f ? EDir::Forward : Off(EDir::Right) <= 45.f ? EDir::Right : Off(EDir::Left) <= 45.f ? EDir::Left : EDir::Back;
	}
	Out.Residual = FMath::FindDeltaAngleDegrees(Centre(Out.Dir), AngleDeg);
	return Out;
}

void OrientationWarp::Apply(TArrayView<FTransform> Chain, int32 FirstSpine, float AngleDeg, float CounterAlpha, const FVector& Up)
{
	if (Chain.Num() == 0 || FMath::IsNearlyZero(AngleDeg))
	{
		return;
	}
	const float Angle = FMath::DegreesToRadians(AngleDeg);
	const FQuat Turn(Up, Angle);
	Chain[0].SetRotation(Turn * Chain[0].GetRotation());
	Chain[0].SetTranslation(Turn.RotateVector(Chain[0].GetTranslation()));

	const int32 FirstBack = FMath::Max(FirstSpine, 1);
	const int32 Spines = Chain.Num() - FirstBack;
	if (Spines <= 0 || FMath::IsNearlyZero(CounterAlpha))
	{
		return;
	}
	// A turn about the component's vertical, applied at a bone, is a turn about that vertical seen from the bone's parent.
	const float Back = -Angle * CounterAlpha / Spines;
	FQuat Parent = FQuat::Identity; // the component-space rotation of bone i's parent
	for (int32 i = 0; i < Chain.Num(); ++i)
	{
		if (i >= FirstBack)
		{
			const FVector Axis = Parent.Inverse().RotateVector(Up).GetSafeNormal();
			Chain[i].SetRotation((FQuat(Axis, Back) * Chain[i].GetRotation()).GetNormalized());
		}
		Parent = Parent * Chain[i].GetRotation();
	}
}
