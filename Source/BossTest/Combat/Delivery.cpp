#include "Combat/Delivery.h"

bool SegmentHitsCapsule(const FVector& A, const FVector& B, float Radius, const FVector& CapsuleCentre, float CapsuleRadius, float CapsuleHalfHeight)
{
	// The capsule is a segment (its axis, shortened by the radius at each end) grown by its radius.
	const float AxisHalf = FMath::Max(0.f, CapsuleHalfHeight - CapsuleRadius);
	const FVector Bottom = CapsuleCentre - FVector(0.f, 0.f, AxisHalf);
	const FVector Top = CapsuleCentre + FVector(0.f, 0.f, AxisHalf);
	FVector OnPath, OnAxis;
	FMath::SegmentDistToSegmentSafe(A, B, Bottom, Top, OnPath, OnAxis);
	return FVector::DistSquared(OnPath, OnAxis) <= FMath::Square(Radius + CapsuleRadius);
}
