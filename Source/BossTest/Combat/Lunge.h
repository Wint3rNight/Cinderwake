// How far an attack's lunge carries its owner (Step 3).
#pragma once

#include "CoreMinimal.h"

// Edge-to-edge distance to the target when there is one in front. The lunge closes the gap down to
// StopDistance, never more than Cap. With no target it goes half the cap straight ahead.
float LungeDistance(TOptional<float> EdgeDistance, float Cap, float StopDistance);

// Where a dash should aim to meet a moving target (Step 5, Executioner's Advance): the target's position plus its
// horizontal velocity times the dash's duration, the lead capped at MaxLead cm so a dodge's burst can't fling him past.
FVector InterceptPoint(const FVector& TargetLocation, const FVector& TargetVelocity, float Duration, float MaxLead);

// A point pulled onto the disc's edge if it's outside (its height kept). Z is ignored.
inline FVector ClampToArena(const FVector& Point, const FVector& Centre, float Radius)
{
	const FVector Flat(Point.X - Centre.X, Point.Y - Centre.Y, 0.f);
	if (Flat.SizeSquared() <= FMath::Square(Radius))
	{
		return Point;
	}
	const FVector OnEdge = Flat.GetSafeNormal() * Radius;
	return FVector(Centre.X + OnEdge.X, Centre.Y + OnEdge.Y, Point.Z);
}
