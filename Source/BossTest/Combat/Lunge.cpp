#include "Combat/Lunge.h"

float LungeDistance(TOptional<float> EdgeDistance, float Cap, float StopDistance)
{
	if (!EdgeDistance.IsSet())
	{
		return Cap * 0.5f;
	}
	return FMath::Clamp(EdgeDistance.GetValue() - StopDistance, 0.f, Cap);
}

FVector InterceptPoint(const FVector& TargetLocation, const FVector& TargetVelocity, float Duration, float MaxLead)
{
	const FVector Lead = FVector(TargetVelocity.X, TargetVelocity.Y, 0.f) * FMath::Max(0.f, Duration);
	return TargetLocation + Lead.GetClampedToMaxSize(FMath::Max(0.f, MaxLead));
}
