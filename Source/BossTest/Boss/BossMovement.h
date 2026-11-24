// His movement helpers (Step 6), plain maths so they're unit tested: the arena disc he stays inside (section 14: movement
// targets clamped to 13 m), circling, and sliding along the edge instead of backing into it. Z is ignored throughout.
#pragma once

#include "CoreMinimal.h"

// A point pulled onto the disc's edge if it's outside (its height kept).
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

// The direction that circles the target: across the line to it, Sign picking the side.
inline FVector CircleDirection(const FVector& Self, const FVector& Target, int32 Sign)
{
	const FVector To = FVector(Target.X - Self.X, Target.Y - Self.Y, 0.f).GetSafeNormal();
	return FVector(-To.Y, To.X, 0.f) * (Sign >= 0 ? 1.f : -1.f);
}

// True if moving along Dir at Speed for Lookahead seconds would leave the disc.
inline bool LeavesArena(const FVector& Pos, const FVector& Dir, float Speed, float Lookahead, const FVector& Centre, float Radius)
{
	const FVector Ahead = Pos + Dir.GetSafeNormal2D() * Speed * Lookahead;
	return FVector::DistSquared2D(Ahead, Centre) > FMath::Square(Radius);
}

// At the edge, the part of Dir that would leave the disc is removed, so he slides along the wall; straight into it, he
// takes the edge's tangent.
inline FVector SlideAlongWall(const FVector& Pos, const FVector& Dir, const FVector& Centre, float Radius)
{
	const FVector Out = FVector(Pos.X - Centre.X, Pos.Y - Centre.Y, 0.f).GetSafeNormal();
	const FVector D = Dir.GetSafeNormal2D();
	if (FVector::DistSquared2D(Pos, Centre) < FMath::Square(Radius * 0.97f) || FVector::DotProduct(D, Out) <= 0.f)
	{
		return D;
	}
	const FVector Along = D - Out * FVector::DotProduct(D, Out);
	return Along.SizeSquared() > 1e-4f ? Along.GetSafeNormal() : FVector(-Out.Y, Out.X, 0.f);
}
