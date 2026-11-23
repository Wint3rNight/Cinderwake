#include "Combat/Tracking.h"

float TrackingYawStep(float CurrentYaw, float TargetYaw, float RateDegPerSec, float Dt)
{
	const float Delta = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
	const float MaxStep = FMath::Max(0.f, RateDegPerSec * Dt);
	return CurrentYaw + FMath::Clamp(Delta, -MaxStep, MaxStep);
}

bool IsTrackingAt(const FAttackTiming& T, float StopsBeforeActive, float Time)
{
	return Time < T.Windup - StopsBeforeActive;
}
