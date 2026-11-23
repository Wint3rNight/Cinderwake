#include "Combat/AttackComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Combat/AttackDefinition.h"
#include "Combat/CombatantComponent.h"
#include "Combat/Lunge.h"
#include "Combat/Tracking.h"
#include "Curves/CurveFloat.h"
#include "HAL/IConsoleManager.h"
#include "Combat/SweepMath.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"

uint32 UAttackComponent::NextInstanceId = 0;

static TAutoConsoleVariable<bool> CVarDrawHitboxes(TEXT("BossTest.DrawHitboxes"), true,
	TEXT("Draw attack hitboxes, hits and lunge targets"), ECVF_Default);

bool UAttackComponent::ShouldDrawDebug()
{
	return CVarDrawHitboxes.GetValueOnGameThread();
}

void UAttackComponent::SetDrawDebug(bool bOn)
{
	CVarDrawHitboxes->Set(bOn, ECVF_SetByConsole); // same rank as typing it, so the key keeps working after the console was used
}

namespace
{
	const TCHAR* ResultName(EHitResult Result)
	{
		switch (Result)
		{
		case EHitResult::Hit: return TEXT("hit");
		case EHitResult::Blocked: return TEXT("blocked");
		case EHitResult::PerfectGuard: return TEXT("perfect guard");
		case EHitResult::Dodged: return TEXT("dodged");
		case EHitResult::Immune: return TEXT("immune");
		default: return TEXT("invalid");
		}
	}
}

UAttackComponent::UAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UAttackComponent::BeginPlay()
{
	Super::BeginPlay();
	// Smoothstep: eases a lunge in and out, so a boss's hammer lands on the target instead of sliding past.
	EaseCurve = NewObject<UCurveFloat>(this);
	for (int32 i = 0; i <= 10; ++i)
	{
		const float X = i / 10.f;
		EaseCurve->FloatCurve.AddKey(X, X * X * (3.f - 2.f * X));
	}
	// Tick before the mesh, so the pose set this frame is the one the mesh evaluates this frame.
	// The sweep reads the sockets the mesh evaluated last frame, so hits trail the visible pose by
	// exactly one frame, every frame, instead of by zero or one depending on tick order.
	if (USkeletalMeshComponent* Mesh = GetOwnerMesh())
	{
		Mesh->AddTickPrerequisiteComponent(this);
	}
}

USkeletalMeshComponent* UAttackComponent::GetOwnerMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
}

UAnimMontage* UAttackComponent::PlayPaused(UAnimSequenceBase* Clip, float BlendIn, bool bEased)
{
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || !Clip)
	{
		return nullptr;
	}
	// The montage is only a pose source: it's paused, and every tick its position is set from the gameplay
	// timeline, so animation and hit timing can never drift apart. Playing on the same slot blends out whatever
	// played there before (a chained attack).
	UAnimMontage* Played = nullptr;
	if (bEased)
	{
		FMontageBlendSettings In(BlendIn);
		In.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
		FMontageBlendSettings Out(0.2f);
		Out.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
		Played = Anim->PlaySlotAnimationAsDynamicMontage_WithBlendSettings(Clip, MontageSlot, In, Out, 1.f);
	}
	else
	{
		Played = Anim->PlaySlotAnimationAsDynamicMontage(Clip, MontageSlot, BlendIn, 0.2f, 1.f);
	}
	if (Played)
	{
		Anim->Montage_Pause(Played);
		Anim->Montage_SetPosition(Played, 0.f);
	}
	return Played;
}

float UAttackComponent::GetTimeToFree() const
{
	return CurrentDefinition ? FMath::Max(0.f, Timing.Total() - Elapsed) : 0.f;
}

bool UAttackComponent::StartAttack(UAttackDefinition* Definition, AActor* InTarget, float BlendIn, bool bEasedBlend)
{
	if (!Definition || !Definition->Clip)
	{
		return false;
	}
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return false;
	}

	EndAttack();
	CurrentDefinition = Definition;
	Timing = Definition->GetTiming();
	// Armour from the first frame, before this component next ticks.
	SetOwnerArmor(ArmorAt(EvaluateAttack(Timing, 0.f).Phase, Definition->GetWindupArmor(), Definition->GetActiveArmor()));
	Elapsed = 0.f;
	InstanceId = ++NextInstanceId;
	if (InstanceId == 0)
	{
		InstanceId = ++NextInstanceId; // 0 is never a valid instance
	}
	Target = InTarget;
	PrevPoints.Reset();
	bLungeDone = false;
	bAtBranch = false;
	bBranchResolved = false;
	BranchWait = 0.f;

	Montage = PlayPaused(Definition->Clip, BlendIn, bEasedBlend);
	if (!Montage)
	{
		CurrentDefinition = nullptr;
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("BossTest: %s starts %s (instance %u)"), *GetOwner()->GetName(), *Definition->GetName(), InstanceId);
	return true;
}

void UAttackComponent::StopAttack(float BlendOut)
{
	if (!CurrentDefinition)
	{
		return;
	}
	if (USkeletalMeshComponent* Mesh = GetOwnerMesh())
	{
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			if (Montage && Anim->Montage_IsActive(Montage))
			{
				Anim->Montage_Stop(BlendOut, Montage);
			}
		}
	}
	EndAttack();
}

void UAttackComponent::EndAttack()
{
	// An interrupted or finished attack must not keep sliding the fighter forward.
	if (LungeId != 0)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			Character->GetCharacterMovement()->RemoveRootMotionSourceByID(LungeId);
		}
		LungeId = 0;
	}
	SetOwnerArmor(EArmor::None); // an interrupted attack mustn't leave its armour behind
	bAtBranch = false;
	bBranchResolved = false;
	BranchWait = 0.f;
	Registry.ForgetInstance(InstanceId);
	CurrentDefinition = nullptr;
	Montage = nullptr;
	PrevPoints.Reset();
}

bool UAttackComponent::IsLunging() const
{
	// True only while the lunge's root motion is still running, not for the rest of the attack.
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return LungeId != 0 && Character && Character->GetCharacterMovement()->GetRootMotionSourceByID(LungeId).IsValid();
}

void UAttackComponent::SetOwnerArmor(EArmor Armor)
{
	if (UCombatantComponent* Self = GetOwner()->FindComponentByClass<UCombatantComponent>())
	{
		Self->State.Armor = Armor;
	}
}

void UAttackComponent::TrackTarget(float Step)
{
	const AActor* TargetActor = Target.Get();
	if (!TargetActor || CurrentDefinition->TrackingRate <= 0.f || !IsTrackingAt(Timing, CurrentDefinition->TrackingStopsBeforeActive, Elapsed))
	{
		return;
	}
	const FVector To = (TargetActor->GetActorLocation() - GetOwner()->GetActorLocation()).GetSafeNormal2D();
	if (To.IsNearlyZero())
	{
		return;
	}
	// Never turn past the cutoff, even on a long frame.
	const float Cutoff = Timing.Windup - CurrentDefinition->TrackingStopsBeforeActive;
	const float TurnTime = FMath::Min(Step, FMath::Max(0.f, Cutoff - Elapsed));
	const float Yaw = TrackingYawStep(GetOwner()->GetActorRotation().Yaw, To.Rotation().Yaw, CurrentDefinition->TrackingRate, TurnTime);
	GetOwner()->SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

bool UAttackComponent::IsAtBranch() const
{
	// Only a branch that can actually be taken: a variant with a clip.
	return bAtBranch && CurrentDefinition && CurrentDefinition->BranchVariant && CurrentDefinition->BranchVariant->Clip;
}

bool UAttackComponent::ResolveBranch(bool bTakeVariant)
{
	if (!IsAtBranch())
	{
		return false;
	}
	bAtBranch = false;
	bBranchResolved = true;
	return bTakeVariant && CurrentDefinition && SwitchTo(CurrentDefinition->BranchVariant);
}

bool UAttackComponent::SwitchTo(UAttackDefinition* Definition)
{
	if (!CurrentDefinition || !Definition || !Definition->Clip)
	{
		return false;
	}
	const bool bSameClip = Definition->Clip == CurrentDefinition->Clip;
	// Too late once the active window has started: something may already have been hit. (A variant on another
	// clip starts its own timeline at 0, so only this attack's windup matters for it.)
	if (Elapsed >= Timing.Windup || (bSameClip && Elapsed >= Definition->GetTiming().Windup))
	{
		return false;
	}
	if (!bSameClip)
	{
		UAnimMontage* Crossfaded = PlayPaused(Definition->Clip, SwitchBlend, true);
		if (!Crossfaded)
		{
			return false;
		}
		Montage = Crossfaded;
		Elapsed = 0.f;
		PrevPoints.Reset();
		bAtBranch = false;
		bBranchResolved = true; // the variant has no branch of its own to wait at
	}
	CurrentDefinition = Definition;
	Timing = Definition->GetTiming();
	// The lunge belongs to the new attack's window; a running one stops.
	if (LungeId != 0)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			Character->GetCharacterMovement()->RemoveRootMotionSourceByID(LungeId);
		}
		LungeId = 0;
	}
	bLungeDone = Elapsed >= LungeWindow(Timing, Definition->LungeStartsBeforeActive, Definition->LungeEndsIntoActive).Start;
	SetOwnerArmor(ArmorAt(EvaluateAttack(Timing, Elapsed).Phase, Definition->GetWindupArmor(), Definition->GetActiveArmor()));
	UE_LOG(LogTemp, Log, TEXT("BossTest: %s switches to %s at %.2f s"), *GetOwner()->GetName(), *Definition->GetName(), Elapsed);
	return true;
}

FAttackSample UAttackComponent::GetSample() const
{
	return CurrentDefinition ? EvaluateAttack(Timing, Elapsed) : FAttackSample();
}

void UAttackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!CurrentDefinition)
	{
		return;
	}

	// Something else took the slot (a hit reaction, a dodge): the attack is over.
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || !Montage || !Anim->Montage_IsActive(Montage))
	{
		EndAttack();
		return;
	}

	// A dead attacker's weapon stops at once (spec section 12).
	const UCombatantComponent* Self = GetOwner()->FindComponentByClass<UCombatantComponent>();
	if (Self && Self->IsDead())
	{
		StopAttack(0.1f);
		return;
	}

	// DeltaTime is scaled by the owner's time dilation, so hit-stop pauses the attack too.
	// A hitch advances at most 1/30 s, so the attack runs a little late instead of skipping its active window.
	float Step = ClampAttackStep(DeltaTime);
	// A branch point holds the attack until the owner chooses (it ticks later this frame). If nobody does,
	// it goes on as it is after 0.1 s.
	if (bAtBranch)
	{
		BranchWait += Step;
		if (BranchWait < 0.1f)
		{
			return;
		}
		bAtBranch = false;
		bBranchResolved = true;
	}
	const FBranchStep Branch = StepToBranch(Elapsed, Step, CurrentDefinition->BranchAt, bBranchResolved);
	Step = Branch.Step;
	if (Branch.bReachedBranch)
	{
		bAtBranch = true;
		BranchWait = 0.f;
	}
	const float Before = Elapsed;
	TrackTarget(Step); // before advancing: tracking covers every moment up to the cutoff
	Elapsed += Step;
	const FAttackSample Sample = EvaluateAttack(Timing, Elapsed);
	Pose(Sample.ClipTime);
	SetOwnerArmor(ArmorAt(Sample.Phase, CurrentDefinition->GetWindupArmor(), CurrentDefinition->GetActiveArmor()));

	const FLungeWindow Window = LungeWindow(Timing, CurrentDefinition->LungeStartsBeforeActive, CurrentDefinition->LungeEndsIntoActive);
	if (!bLungeDone && Elapsed >= Window.Start)
	{
		bLungeDone = true;
		ApplyLunge(Window.Duration);
	}

	if (ActiveOverlap(Timing, Before, Elapsed))
	{
		SweepWeapon();
	}
	else
	{
		PrevPoints.Reset();
	}

	if (Sample.Phase == EAttackPhase::Done)
	{
		StopAttack(0.2f);
	}
}

void UAttackComponent::Pose(float ClipTime)
{
	if (USkeletalMeshComponent* Mesh = GetOwnerMesh())
	{
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			Anim->Montage_SetPosition(Montage, ClipTime);
		}
	}
}

void UAttackComponent::ApplyLunge(float Duration)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || CurrentDefinition->LungeCap <= 0.f)
	{
		return;
	}
	// Toward the target when there is one (the owner picks it), measured along where the attacker
	// faces now; otherwise half the cap straight ahead.
	const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
	TOptional<float> Edge;
	if (const AActor* TargetActor = Target.Get())
	{
		float TargetRadius = 0.f;
		if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
		{
			TargetRadius = TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
		}
		const float Ahead = FVector::DotProduct(TargetActor->GetActorLocation() - Character->GetActorLocation(), Forward);
		Edge = Ahead - Character->GetCapsuleComponent()->GetScaledCapsuleRadius() - TargetRadius;
	}
	const float Distance = LungeDistance(Edge, CurrentDefinition->LungeCap, CurrentDefinition->LungeStopDistance);
	if (Distance < 5.f)
	{
		return;
	}

	const FVector Start = Character->GetActorLocation();
	LungeTarget = Start + Forward * Distance;
	TSharedPtr<FRootMotionSource> Source;
	if (CurrentDefinition->bEaseLunge)
	{
		// Position-based with an eased progress curve: lands exactly, at any frame rate.
		TSharedPtr<FRootMotionSource_MoveToDynamicForce> Move = MakeShared<FRootMotionSource_MoveToDynamicForce>();
		Move->StartLocation = Start;
		Move->InitialTargetLocation = LungeTarget;
		Move->TargetLocation = LungeTarget;
		Move->TimeMappingCurve = EaseCurve;
		Move->bRestrictSpeedToExpected = true;
		Move->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
		Move->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
		Source = Move;
	}
	else
	{
		TSharedPtr<FRootMotionSource_MoveToForce> Move = MakeShared<FRootMotionSource_MoveToForce>();
		Move->StartLocation = Start;
		Move->TargetLocation = LungeTarget;
		Move->bRestrictSpeedToExpected = true;
		Move->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
		Move->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
		Source = Move;
	}
	Source->InstanceName = TEXT("AttackLunge");
	Source->AccumulateMode = ERootMotionAccumulateMode::Override;
	Source->Priority = 5;
	Source->Duration = FMath::Max(0.05f, Duration);
	Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	LungeId = Character->GetCharacterMovement()->ApplyRootMotionSource(Source);
}

void UAttackComponent::SweepWeapon()
{
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	if (!Mesh)
	{
		return;
	}
	const UAttackDefinition* Def = CurrentDefinition;
	const FVector A = Mesh->GetSocketLocation(Def->SocketStart);
	const FVector B = Mesh->GetSocketLocation(Def->SocketEnd);
	const float Radius = Def->Radius;

	const int32 BladeSamples = BladeSampleCount(FVector::Dist(A, B), Radius);
	TArray<FVector> Points;
	Points.SetNum(BladeSamples);
	for (int32 i = 0; i < BladeSamples; ++i)
	{
		Points[i] = FMath::Lerp(A, B, static_cast<float>(i) / (BladeSamples - 1));
	}
	if (PrevPoints.Num() != BladeSamples)
	{
		PrevPoints = Points; // first active frame: a zero-length sweep acts as an overlap check
	}

	float Travel = 0.f;
	for (int32 i = 0; i < BladeSamples; ++i)
	{
		Travel = FMath::Max(Travel, FVector::Dist(PrevPoints[i], Points[i]));
	}
	const int32 Substeps = SweepSubsteps(Travel, Radius);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AttackSweep), false, GetOwner());
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
	TArray<UCombatantComponent*> Victims;
	for (int32 Step = 1; Step <= Substeps; ++Step)
	{
		const float From = static_cast<float>(Step - 1) / Substeps;
		const float To = static_cast<float>(Step) / Substeps;
		for (int32 i = 0; i < BladeSamples; ++i)
		{
			const FVector Start = FMath::Lerp(PrevPoints[i], Points[i], From);
			const FVector End = FMath::Lerp(PrevPoints[i], Points[i], To);
			TArray<FHitResult> Hits;
			GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), Sphere, Params);
			for (const FHitResult& Hit : Hits)
			{
				AActor* HitActor = Hit.GetActor();
				if (HitActor && HitActor != GetOwner())
				{
					if (UCombatantComponent* Victim = HitActor->FindComponentByClass<UCombatantComponent>())
					{
						Victims.AddUnique(Victim);
					}
				}
			}
			if (ShouldDrawDebug())
			{
				DrawDebugSphere(GetWorld(), End, Radius, 8, FColor::Red, false, 0.3f);
			}
		}
	}
	PrevPoints = Points;

	for (UCombatantComponent* Victim : Victims)
	{
		FCombatHitContext Hit;
		Hit.AttackInstanceId = InstanceId;
		Hit.Damage = Def->Damage;
		Hit.PoiseDamage = Def->PoiseDamage;
		Hit.bHeavy = Def->bHeavy;
		Hit.bKnockdown = Def->bKnockdown;
		// A weapon sweep: a guard can stop it if the attacker is inside the guard's arc (unless marked unblockable).
		Hit.bBlockable = !Def->bUnblockable;
		{
			const AActor* VictimActor = Victim->GetOwner();
			const FVector ToAttacker = (GetOwner()->GetActorLocation() - VictimActor->GetActorLocation()).GetSafeNormal2D();
			const float Dot = FVector::DotProduct(VictimActor->GetActorForwardVector().GetSafeNormal2D(), ToAttacker);
			Hit.AngleFromTargetFront = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
		}
		Hit.Policy = Def->GetHitPolicy();
		Hit.MultiHitInterval = Def->MultiHitInterval;
		Hit.MultiHitMax = Def->MultiHitMax;
		Hit.Time = Elapsed;
		Hit.WorldTime = GetWorld()->GetTimeSeconds();

		const FHitOutcome Outcome = Victim->ReceiveHit(Hit, Registry, GetOwner());
		if (Outcome.Result == EHitResult::Invalid)
		{
			continue; // already hit by this instance: not worth logging every frame
		}
		UE_LOG(LogTemp, Log, TEXT("BossTest: %s -> %s with %s: %s, %.0f damage%s%s%s"),
			*GetOwner()->GetName(), *Victim->GetOwner()->GetName(), *Def->GetName(), ResultName(Outcome.Result), Outcome.DamageDealt,
			Outcome.Poise == EPoiseOutcome::Stagger ? TEXT(", STAGGER") : Outcome.Poise == EPoiseOutcome::DeferredStagger ? TEXT(", deferred stagger") : TEXT(""),
			Outcome.bCastBroken ? TEXT(", cast broken") : TEXT(""), Outcome.bKilled ? TEXT(", KILLED") : TEXT(""));

		if (Outcome.Result == EHitResult::Blocked)
		{
			// A shorter freeze than a clean hit, so a block still lands with weight.
			Victim->StartHitStop(Def->HitStop * 0.6f);
			if (UCombatantComponent* Self = GetOwner()->FindComponentByClass<UCombatantComponent>())
			{
				Self->StartHitStop(Def->HitStop * 0.6f);
			}
		}
		if (Outcome.Result == EHitResult::PerfectGuard)
		{
			// Both freeze a little longer than a clean hit, and the attacker trembles along the hit instead of
			// recoiling: his swing carries on.
			const float Freeze = Outcome.bKnockdown ? Victim->State.HeavyPerfectGuardHitStop : Victim->State.PerfectGuardHitStop;
			Victim->StartHitStop(Freeze);
			if (UCombatantComponent* Self = GetOwner()->FindComponentByClass<UCombatantComponent>())
			{
				const FVector Along = (GetOwner()->GetActorLocation() - Victim->GetOwner()->GetActorLocation()).GetSafeNormal2D();
				Self->StartHitStop(Freeze, Along, Victim->State.PerfectGuardTrembleCm);
				// The deflect pays him in poise: a 0-damage hit through his own combatant, so his armour, a deferred
				// stagger and his immunity all apply exactly as they do to a sword hit. Its own registry, so the
				// attack's never collects one-off entries.
				FCombatHitContext Back;
				Back.AttackInstanceId = ++NextInstanceId;
				Back.PoiseDamage = Outcome.BlockedDamage;
				Back.Time = Elapsed;
				Back.WorldTime = Hit.WorldTime;
				FHitRegistry BackRegistry;
				Self->ReceiveHit(Back, BackRegistry, Victim->GetOwner());
				if (!CurrentDefinition)
				{
					break; // the poise staggered him at once and his attack stopped: no more victims this sweep
				}
			}
		}
		if (Outcome.Result == EHitResult::Hit)
		{
			OnHitLanded.Broadcast(Outcome, Victim->GetOwner());
			Victim->StartHitStop(Def->HitStop);
			if (UCombatantComponent* Self = GetOwner()->FindComponentByClass<UCombatantComponent>())
			{
				Self->StartHitStop(Def->HitStop);
			}
			if (ShouldDrawDebug())
			{
				DrawDebugSphere(GetWorld(), Victim->GetOwner()->GetActorLocation(), 30.f, 12, FColor::Yellow, false, 0.5f, 0, 2.f);
			}
		}
	}
}
