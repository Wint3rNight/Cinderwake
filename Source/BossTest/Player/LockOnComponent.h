// Lock-on (spec section 13): which target can be locked, when the lock breaks, and a smoothed aim
// point for the camera. The rules are plain functions so they're unit tested.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LockOnComponent.generated.h"

struct FLockOnRules
{
	float AcquireRange = 1500.f;   // cm, edge to edge
	float AcquireAngle = 60.f;     // degrees from the camera's forward
	float BreakRange = 2500.f;
	float LostSightTime = 2.f;     // seconds without line of sight
};

bool CanAcquireTarget(const FLockOnRules& Rules, float EdgeDistance, float AngleFromView, bool bVisible);
bool ShouldBreakLock(const FLockOnRules& Rules, float EdgeDistance, float SecondsUnseen);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class BOSSTEST_API ULockOnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULockOnComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = "Lock-on", meta = (ToolTip = "Socket on the target the camera aims at (upper torso)"))
	FName AimSocket = TEXT("Chest");

	UPROPERTY(EditAnywhere, Category = "Lock-on", meta = (ToolTip = "How quickly the aim point follows the socket; higher is snappier"))
	float AimSmoothing = 10.f;

	FLockOnRules Rules;

	// Locks the best target in view (smallest angle) if one qualifies. Returns whether it locked.
	bool TryLock(const FVector& ViewLocation, const FVector& ViewForward);
	void Unlock();
	AActor* GetTarget() const { return Target.Get(); }
	bool IsLocked() const { return Target.IsValid(); }
	FVector GetAimPoint() const { return AimPoint; }

private:
	FVector TargetPoint(const AActor* Candidate) const;
	float EdgeDistanceTo(const AActor* Candidate) const;
	bool HasLineOfSight(const AActor* Candidate, const FVector& From) const;

	TWeakObjectPtr<AActor> Target;
	FVector AimPoint = FVector::ZeroVector;
	float SecondsUnseen = 0.f;
};
