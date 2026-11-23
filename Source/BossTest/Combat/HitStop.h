// Hit-stop: a brief freeze measured in real time, so it ends even while the fighter's own clock is paused.
#pragma once

#include "CoreMinimal.h"

struct FHitStop
{
	static constexpr float FrozenDilation = 0.01f;

	void Start(float Duration, double RealNow);   // overlapping hit-stops keep the later end
	float Dilation(double RealNow) const;          // FrozenDilation while frozen, else 1
	bool IsActive(double RealNow) const;

private:
	double EndTime = 0.0;
};
