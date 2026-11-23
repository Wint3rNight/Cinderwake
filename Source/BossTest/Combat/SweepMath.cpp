#include "Combat/SweepMath.h"

int32 SweepSubsteps(float MaxEndpointTravel, float Radius)
{
	if (Radius <= 0.f)
	{
		return 16;
	}
	return FMath::Clamp(FMath::CeilToInt(MaxEndpointTravel / Radius), 1, 16);
}

int32 BladeSampleCount(float BladeLength, float Radius)
{
	if (Radius <= 0.f)
	{
		return 8;
	}
	return FMath::Clamp(FMath::CeilToInt(BladeLength / (1.5f * Radius)), 2, 8);
}
