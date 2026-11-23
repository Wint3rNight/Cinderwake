#include "Player/LockOnComponent.h"

#include "Combat/CombatantComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"

bool CanAcquireTarget(const FLockOnRules& Rules, float EdgeDistance, float AngleFromView, bool bVisible)
{
	return bVisible && EdgeDistance <= Rules.AcquireRange && AngleFromView <= Rules.AcquireAngle;
}

bool ShouldBreakLock(const FLockOnRules& Rules, float EdgeDistance, float SecondsUnseen)
{
	return EdgeDistance > Rules.BreakRange || SecondsUnseen >= Rules.LostSightTime;
}

namespace
{
	bool IsAlive(const AActor* Actor)
	{
		const UCombatantComponent* Combatant = Actor ? Actor->FindComponentByClass<UCombatantComponent>() : nullptr;
		return Combatant && !Combatant->IsDead();
	}
}

ULockOnComponent::ULockOnComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

FVector ULockOnComponent::TargetPoint(const AActor* Candidate) const
{
	if (const ACharacter* Character = Cast<ACharacter>(Candidate))
	{
		const USkeletalMeshComponent* Mesh = Character->GetMesh();
		if (Mesh && Mesh->DoesSocketExist(AimSocket))
		{
			return Mesh->GetSocketLocation(AimSocket);
		}
		return Character->GetActorLocation() + FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.5f);
	}
	return Candidate->GetActorLocation();
}

float ULockOnComponent::EdgeDistanceTo(const AActor* Candidate) const
{
	auto Radius = [](const AActor* Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(Actor);
		return Character ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.f;
	};
	return FVector::Dist2D(GetOwner()->GetActorLocation(), Candidate->GetActorLocation()) - Radius(GetOwner()) - Radius(Candidate);
}

bool ULockOnComponent::HasLineOfSight(const AActor* Candidate, const FVector& From) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LockOnSight), false, GetOwner());
	Params.AddIgnoredActor(Candidate);
	return !GetWorld()->LineTraceTestByChannel(From, TargetPoint(Candidate), ECC_Visibility, Params);
}

bool ULockOnComponent::TryLock(const FVector& ViewLocation, const FVector& ViewForward)
{
	AActor* Best = nullptr;
	float BestAngle = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		if (Candidate == GetOwner() || !IsAlive(Candidate))
		{
			continue;
		}
		const FVector ToTarget = (TargetPoint(Candidate) - ViewLocation).GetSafeNormal();
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ViewForward.GetSafeNormal(), ToTarget), -1.f, 1.f)));
		if (Angle < BestAngle && CanAcquireTarget(Rules, EdgeDistanceTo(Candidate), Angle, HasLineOfSight(Candidate, ViewLocation)))
		{
			Best = Candidate;
			BestAngle = Angle;
		}
	}
	if (!Best)
	{
		return false;
	}
	Target = Best;
	AimPoint = TargetPoint(Best);
	SecondsUnseen = 0.f;
	return true;
}

void ULockOnComponent::Unlock()
{
	Target.Reset();
	SecondsUnseen = 0.f;
}

void ULockOnComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Locked = Target.Get();
	if (!Locked)
	{
		return;
	}
	if (!IsAlive(Locked) || !IsAlive(GetOwner()))
	{
		Unlock();
		return;
	}

	// Sight is checked from the owner's head, which is close to the camera and doesn't swing with it.
	const FVector Eye = GetOwner()->GetActorLocation() + FVector(0.f, 0.f, 80.f);
	SecondsUnseen = HasLineOfSight(Locked, Eye) ? 0.f : SecondsUnseen + DeltaTime;
	if (ShouldBreakLock(Rules, EdgeDistanceTo(Locked), SecondsUnseen))
	{
		Unlock();
		return;
	}
	// Smoothed, so the camera never snaps when the target changes height or plays a big animation.
	AimPoint = FMath::VInterpTo(AimPoint, TargetPoint(Locked), DeltaTime, AimSmoothing);
}
