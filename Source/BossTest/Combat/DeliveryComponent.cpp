#include "Combat/DeliveryComponent.h"

#include "Combat/AttackComponent.h"
#include "Combat/AttackDefinition.h"
#include "Combat/CombatantComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"

UDeliveryComponent::UDeliveryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDeliveryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAll();
	Super::EndPlay(EndPlayReason);
}

int32 UDeliveryComponent::GetEffectCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<UParticleSystemComponent>& P : Owned)
	{
		Count += IsValid(P) ? 1 : 0;
	}
	for (const TObjectPtr<UStaticMeshComponent>& E : OwnedEdges)
	{
		Count += IsValid(E) ? 1 : 0;
	}
	return Count;
}

void UDeliveryComponent::DestroyEffect(TWeakObjectPtr<UParticleSystemComponent>& Effect)
{
	if (UParticleSystemComponent* P = Effect.Get())
	{
		Owned.Remove(P);
		P->DeactivateSystem();
		P->DestroyComponent();
	}
	Effect = nullptr;
}

void UDeliveryComponent::ClearAll()
{
	for (TObjectPtr<UParticleSystemComponent>& P : Owned)
	{
		if (IsValid(P))
		{
			P->DeactivateSystem();
			P->DestroyComponent();
		}
	}
	Owned.Reset();
	for (TObjectPtr<UStaticMeshComponent>& E : OwnedEdges)
	{
		if (IsValid(E))
		{
			E->DestroyComponent();
		}
	}
	OwnedEdges.Reset();
	Rings.Reset();
	Projectiles.Reset();
	DirectResults.Reset();
}

void UDeliveryComponent::NoteDirectResult(uint32 AttackInstance, int32 TargetKey)
{
	DirectResults.FindOrAdd(AttackInstance).Add(TargetKey);
	for (FLiveRing& R : Rings)
	{
		if (R.AttackInstance == AttackInstance)
		{
			R.Ring.Skip.Add(TargetKey);
		}
	}
}

void UDeliveryComponent::ForgetInstance(uint32 AttackInstance)
{
	DirectResults.Remove(AttackInstance);
}

void UDeliveryComponent::LaunchRing(const UAttackDefinition& Definition, uint32 AttackInstance, const FVector& Center)
{
	FLiveRing& R = Rings.AddDefaulted_GetRef();
	R.Ring.Speed = Definition.DeliverySpeed;
	R.Ring.MaxRadius = Definition.DeliveryReach;
	R.Ring.Skip = DirectResults.FindRef(AttackInstance); // a copy: it survives the slam being interrupted
	R.AttackInstance = AttackInstance;
	R.Id = UAttackComponent::AllocateInstanceId();
	R.Damage = Definition.DeliveryDamage;
	R.HitStop = Definition.DeliveryHitStop;
	// On the floor under the impact.
	FVector Floor = Center;
	if (const ACharacter* Owner = Cast<ACharacter>(GetOwner()))
	{
		Floor.Z = Owner->GetActorLocation().Z - Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	R.Center = Floor;
	if (Definition.DeliveryEffect)
	{
		if (UParticleSystemComponent* P = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Definition.DeliveryEffect, Floor,
			FRotator::ZeroRotator, FVector(Definition.DeliveryEffectScale), false))
		{
			Owned.Add(P);
			R.Effect = P;
		}
	}
	if (Definition.DeliveryRingMesh)
	{
		UStaticMeshComponent* Edge = NewObject<UStaticMeshComponent>(GetOwner());
		Edge->SetStaticMesh(Definition.DeliveryRingMesh);
		if (Definition.DeliveryRingMaterial)
		{
			Edge->SetMaterial(0, Definition.DeliveryRingMaterial);
		}
		Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Edge->SetCastShadow(false);
		Edge->SetUsingAbsoluteLocation(true);
		Edge->SetUsingAbsoluteRotation(true);
		Edge->SetUsingAbsoluteScale(true);
		Edge->RegisterComponent();
		Edge->SetWorldLocationAndRotation(Floor + FVector(0.f, 0.f, 3.f), FRotator::ZeroRotator);
		Edge->SetWorldScale3D(FVector(0.01f, 0.01f, 1.f));
		OwnedEdges.Add(Edge);
		R.Edge = Edge;
		R.EdgeScale = Definition.DeliveryRingMeshScale;
	}
	UE_LOG(LogTemp, Log, TEXT("BossTest: %s's %s ring from (%.0f, %.0f), skipping %d"), *GetOwner()->GetName(), *Definition.GetName(), Floor.X, Floor.Y, R.Ring.Skip.Num());
}

void UDeliveryComponent::LaunchProjectile(const UAttackDefinition& Definition, const FVector& Start, const FVector& Direction)
{
	FLiveProjectile& P = Projectiles.AddDefaulted_GetRef();
	P.Flight.Position = Start;
	P.Flight.Velocity = Direction.GetSafeNormal() * Definition.DeliverySpeed;
	P.Flight.MaxRange = Definition.DeliveryReach;
	P.Id = UAttackComponent::AllocateInstanceId();
	P.Radius = Definition.DeliveryRadius;
	P.Damage = Definition.DeliveryDamage;
	P.HitStop = Definition.DeliveryHitStop;
	P.bSoulFragment = Definition.bDeliverySoulFragment;
	P.Impact = Definition.DeliveryImpactEffect;
	if (Definition.DeliveryEffect)
	{
		if (UParticleSystemComponent* Look = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Definition.DeliveryEffect, Start,
			Direction.Rotation(), FVector(Definition.DeliveryEffectScale), false))
		{
			Owned.Add(Look);
			P.Effect = Look;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("BossTest: %s's %s projectile launched"), *GetOwner()->GetName(), *Definition.GetName());
}

TArray<UCombatantComponent*> UDeliveryComponent::Targets() const
{
	TArray<UCombatantComponent*> Out;
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		if (*It == GetOwner())
		{
			continue;
		}
		if (UCombatantComponent* C = It->FindComponentByClass<UCombatantComponent>())
		{
			if (!C->IsDead())
			{
				Out.Add(C);
			}
		}
	}
	return Out;
}

FHitOutcome UDeliveryComponent::Resolve(UCombatantComponent* Victim, uint32 Id, float Damage, bool bSoulFragment, float Age, FHitRegistry& Registry, float HitStop)
{
	FCombatHitContext Hit;
	Hit.AttackInstanceId = Id;
	Hit.Damage = Damage;
	Hit.bBlockable = false; // rings and projectiles: a guard can't stop them (spec section 12)
	Hit.bSoulFragment = bSoulFragment;
	Hit.Time = Age;
	Hit.WorldTime = GetWorld()->GetTimeSeconds();
	const FHitOutcome Outcome = Victim->ReceiveHit(Hit, Registry, GetOwner());
	if (Outcome.Result == EHitResult::Hit)
	{
		Victim->StartHitStop(HitStop); // the victim only: he's metres away
		UE_LOG(LogTemp, Log, TEXT("BossTest: %s's delivery hits %s for %.0f%s"), *GetOwner()->GetName(), *Victim->GetOwner()->GetName(),
			Outcome.DamageDealt, Outcome.bFragmentTaken ? TEXT(", a soul fragment") : TEXT(""));
	}
	return Outcome;
}

void UDeliveryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Rings.Num() == 0 && Projectiles.Num() == 0)
	{
		return;
	}
	const TArray<UCombatantComponent*> Victims = Targets();
	const bool bDraw = UAttackComponent::ShouldDrawDebug();

	for (int32 i = Rings.Num() - 1; i >= 0; --i)
	{
		FLiveRing& R = Rings[i];
		R.Age += DeltaTime;
		if (!R.Ring.IsDone())
		{
			const FRingBand Band = R.Ring.Advance(DeltaTime);
			for (UCombatantComponent* Victim : Victims)
			{
				const ACharacter* Body = Cast<ACharacter>(Victim->GetOwner());
				const float Radius = Body ? Body->GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.f;
				const float Distance = FVector::Dist2D(Victim->GetOwner()->GetActorLocation(), R.Center);
				if (R.Ring.CanHit(Victim->GetTargetKey()) && Band.Touches(Distance, Radius))
				{
					Resolve(Victim, R.Id, R.Damage, false, R.Age, R.Registry, R.HitStop);
				}
			}
			if (UStaticMeshComponent* Edge = R.Edge.Get())
			{
				const float S = FMath::Max(0.01f, R.Ring.Radius * R.EdgeScale);
				Edge->SetWorldScale3D(FVector(S, S, 1.f));
			}
			if (R.Ring.IsDone() && R.Edge.IsValid())
			{
				OwnedEdges.Remove(R.Edge.Get());
				R.Edge->DestroyComponent(); // the edge has gone as far as it hits
				R.Edge = nullptr;
			}
			if (bDraw)
			{
				DrawDebugCircle(GetWorld(), R.Center + FVector(0, 0, 5), R.Ring.Radius, 48, FColor::Orange, false, 0.f, 0, 3.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			}
		}
		else if (R.Age >= R.Ring.MaxRadius / FMath::Max(1.f, R.Ring.Speed) + RingLinger)
		{
			DestroyEffect(R.Effect);
			Rings.RemoveAtSwap(i);
		}
	}

	for (int32 i = Projectiles.Num() - 1; i >= 0; --i)
	{
		FLiveProjectile& P = Projectiles[i];
		P.Age += DeltaTime;
		const FVector From = P.Flight.Position;
		FVector To = P.Flight.Advance(DeltaTime);
		bool bEnded = P.Flight.IsSpent();
		// Walls stop it (spec section 12: projectiles don't pass through solid geometry).
		FHitResult Wall;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DeliveryProjectile), false, GetOwner());
		// Static geometry only: a channel trace would also stop at the player's capsule, and a dodged shot must fly on.
		if (GetWorld()->SweepSingleByObjectType(Wall, From, To, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(P.Radius), Params))
		{
			To = Wall.Location;
			P.Flight.Position = To;
			bEnded = true;
		}
		for (UCombatantComponent* Victim : Victims)
		{
			const ACharacter* Body = Cast<ACharacter>(Victim->GetOwner());
			if (!Body)
			{
				continue;
			}
			const UCapsuleComponent* Capsule = Body->GetCapsuleComponent();
			if (SegmentHitsCapsule(From, To, P.Radius, Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()))
			{
				const FHitOutcome Out = Resolve(Victim, P.Id, P.Damage, P.bSoulFragment, P.Age, P.Registry, P.HitStop);
				if (Out.Result == EHitResult::Hit)
				{
					bEnded = true; // resolved on contact; a dodged one flies on
				}
			}
		}
		if (UParticleSystemComponent* Look = P.Effect.Get())
		{
			Look->SetWorldLocation(To);
		}
		if (bDraw)
		{
			DrawDebugSphere(GetWorld(), To, P.Radius, 8, FColor::Cyan, false, 0.f);
		}
		if (bEnded)
		{
			if (UParticleSystem* Impact = P.Impact.Get())
			{
				UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Impact, To, FRotator::ZeroRotator, true);
			}
			DestroyEffect(P.Effect);
			Projectiles.RemoveAtSwap(i);
		}
	}
}
