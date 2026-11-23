#include "Combat/Lunge.h"

float LungeDistance(TOptional<float> EdgeDistance, float Cap, float StopDistance)
{
	if (!EdgeDistance.IsSet())
	{
		return Cap * 0.5f;
	}
	return FMath::Clamp(EdgeDistance.GetValue() - StopDistance, 0.f, Cap);
}
