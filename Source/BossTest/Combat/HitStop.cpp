#include "Combat/HitStop.h"

void FHitStop::Start(float Duration, double RealNow)
{
	EndTime = FMath::Max(EndTime, RealNow + FMath::Max(0.f, Duration));
}

float FHitStop::Dilation(double RealNow) const
{
	return IsActive(RealNow) ? FrozenDilation : 1.f;
}

bool FHitStop::IsActive(double RealNow) const
{
	return RealNow < EndTime;
}
