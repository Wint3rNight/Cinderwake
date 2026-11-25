#include "Combat/CombatantComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/CollisionProfile.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

int32 UCombatantComponent::NextTargetKey = 0;

UCombatantComponent::UCombatantComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UCombatantComponent::BeginPlay()
{
	Super::BeginPlay();
	TargetKey = ++NextTargetKey;
	ApplyTuning();
	State.Health.ResetRuntime();
	State.Poise.ResetRuntime();
}

void UCombatantComponent::ApplyTuning()
{
	State.Health.BaseMax = MaxHealth;
	State.Poise.Threshold = PoiseThreshold;
	State.Poise.DecayRate = PoiseDecayRate;
	State.Poise.DecayDelay = PoiseDecayDelay;
	State.Poise.StaggerDuration = StaggerDuration;
	State.Poise.ImmunityDuration = StaggerImmunity;
}

void UCombatantComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// DeltaTime is already scaled by the owner's time dilation, so hit-stop pauses these timers too.
	State.Poise.Tick(DeltaTime);

	// Hit-stop ends in real time. Only touch the owner's dilation while ours is running or just ended,
	// so other code that sets it isn't overwritten every frame.
	AActor* Owner = GetOwner();
	const double Now = FPlatformTime::Seconds();
	if (HitStop.IsActive(Now))
	{
		Owner->CustomTimeDilation = FHitStop::FrozenDilation;
		bHitStopApplied = true;
	}
	else if (bHitStopApplied)
	{
		Owner->CustomTimeDilation = 1.f;
		bHitStopApplied = false;
	}
	TickTremble();

	// On the dilated clock, like the death clip it waits for.
	if (bRagdollPending)
	{
		RagdollIn -= DeltaTime;
		if (RagdollIn <= 0.f)
		{
			bRagdollPending = false;
			StartRagdoll();
		}
	}
}

USkeletalMeshComponent* UCombatantComponent::OwnerMesh() const
{
	// The character's own mesh, so a second skeletal mesh on the Blueprint can't be the one that shakes.
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}
	return GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
}

void UCombatantComponent::TickTremble()
{
	if (!bTrembling)
	{
		return;
	}
	USkeletalMeshComponent* Mesh = OwnerMesh();
	const float T = static_cast<float>(FPlatformTime::Seconds() - TrembleStart);
	if (!Mesh || T >= TrembleDuration)
	{
		StopTremble();
		return;
	}
	// The sign flips every frame and the size decays to 0, so it reads as a shudder at any frame rate.
	TrembleSign = -TrembleSign;
	const float Size = TrembleCm * (1.f - T / TrembleDuration);
	const FVector Local = GetOwner()->GetActorTransform().InverseTransformVectorNoScale(TrembleDir);
	Mesh->SetRelativeLocation(MeshBase + Local * Size * TrembleSign);
}

void UCombatantComponent::StopTremble()
{
	if (bTrembling)
	{
		if (USkeletalMeshComponent* Mesh = OwnerMesh())
		{
			Mesh->SetRelativeLocation(MeshBase);
		}
		bTrembling = false;
	}
}

FHitOutcome UCombatantComponent::ReceiveHit(FCombatHitContext Hit, FHitRegistry& Registry, AActor* Attacker)
{
	Hit.TargetKey = TargetKey;
	const FHitOutcome Outcome = ResolveCombatHit(Hit, State, Registry);
	OnHitResolved.Broadcast(Outcome, Attacker);
	return Outcome;
}

void UCombatantComponent::StartHitStop(float Duration, const FVector& InTrembleDir, float InTrembleCm)
{
	HitStop.Start(Duration, FPlatformTime::Seconds());
	if (InTrembleCm > 0.f && !InTrembleDir.IsNearlyZero())
	{
		if (!bTrembling)
		{
			if (const USkeletalMeshComponent* Mesh = OwnerMesh())
			{
				MeshBase = Mesh->GetRelativeLocation(); // only when still: a tremble on a tremble keeps the true rest
			}
		}
		bTrembling = true;
		TrembleDir = InTrembleDir;
		TrembleCm = InTrembleCm;
		TrembleDuration = Duration;
		TrembleStart = FPlatformTime::Seconds();
	}
	GetOwner()->CustomTimeDilation = FHitStop::FrozenDilation;
	bHitStopApplied = true;
}

void UCombatantComponent::ResetCombatant()
{
	StopRagdoll();
	StopTremble();
	HitStop = FHitStop(); // a reset mid-freeze mustn't leave him at 0.01 dilation
	if (bHitStopApplied)
	{
		GetOwner()->CustomTimeDilation = 1.f;
		bHitStopApplied = false;
	}
	ApplyTuning();
	State.Health.ResetRuntime();
	State.Poise.ResetRuntime();
	State.Cast.Close();
	State.Armor = EArmor::None;
	State.bInvulnerable = false;
	State.bImmune = false;
	State.bGuarding = false;
	State.GuardPressTime = -1.0;
}

void UCombatantComponent::RagdollAfter(float Seconds)
{
	bRagdollPending = true;
	RagdollIn = FMath::Max(0.f, Seconds);
}

void UCombatantComponent::StartRagdoll()
{
	USkeletalMeshComponent* Mesh = OwnerMesh();
	if (!Mesh || bRagdoll)
	{
		return;
	}
	StopTremble(); // so the saved place is the real one
	MeshProfile = Mesh->GetCollisionProfileName();
	MeshCollisionEnabled = Mesh->GetCollisionEnabled();
	MeshObjectType = Mesh->GetCollisionObjectType();
	MeshResponses = Mesh->GetCollisionResponseToChannels();
	MeshRelative = Mesh->GetRelativeTransform();
	if (RagdollPhysicsAsset)
	{
		SavedPhysicsOverride = Mesh->PhysicsAssetOverride;
		Mesh->SetPhysicsAsset(RagdollPhysicsAsset, true);
	}
	// Ragdoll collides with the floor but ignores pawns, so the capsule left standing doesn't hold the body up.
	Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh->SetSimulatePhysics(true);
	Mesh->WakeAllRigidBodies();
	bRagdoll = true;
}

void UCombatantComponent::StopRagdoll()
{
	bRagdollPending = false;
	if (!bRagdoll)
	{
		return;
	}
	bRagdoll = false;
	USkeletalMeshComponent* Mesh = OwnerMesh();
	if (!Mesh)
	{
		return;
	}
	Mesh->SetSimulatePhysics(false);
	// The collision first, so the physics asset swap below builds its bodies under the right profile. A mesh whose
	// collision was set per component (profile "Custom") gets its settings back one by one.
	if (MeshProfile == UCollisionProfile::CustomCollisionProfileName)
	{
		Mesh->SetCollisionEnabled(MeshCollisionEnabled);
		Mesh->SetCollisionObjectType(MeshObjectType);
		Mesh->SetCollisionResponseToChannels(MeshResponses);
	}
	else
	{
		Mesh->SetCollisionProfileName(MeshProfile);
	}
	if (RagdollPhysicsAsset)
	{
		Mesh->SetPhysicsAsset(SavedPhysicsOverride, true);
		SavedPhysicsOverride = nullptr;
	}
	// In 5.8 the simulating mesh stays attached but physics moves it; put it back in its place on the capsule.
	// (The attach is a safety net in case a future version detaches it.)
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (Mesh->GetAttachParent() != Character->GetCapsuleComponent())
		{
			Mesh->AttachToComponent(Character->GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		}
	}
	Mesh->SetRelativeTransform(MeshRelative);
}

FVector UCombatantComponent::LeadVelocityOf(const AActor* Target)
{
	if (!Target)
	{
		return FVector::ZeroVector;
	}
	if (const UCombatantComponent* C = Target->FindComponentByClass<UCombatantComponent>(); C && C->bReportsLeadVelocity)
	{
		return C->LeadVelocity;
	}
	const ACharacter* Character = Cast<ACharacter>(Target);
	const bool bRootMotion = Character && Character->GetCharacterMovement()->HasRootMotionSources();
	return bRootMotion ? FVector::ZeroVector : Target->GetVelocity();
}
