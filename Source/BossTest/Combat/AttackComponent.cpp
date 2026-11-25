#include "Combat/AttackComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Combat/AttackDefinition.h"
#include "Combat/CombatantComponent.h"
#include "Combat/DeliveryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
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
	const TCHAR* AttackResultName(EHitResult Result)
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

uint32 UAttackComponent::AllocateInstanceId()
{
	uint32 Id = ++NextInstanceId;
	if (Id == 0)
	{
		Id = ++NextInstanceId; // 0 is never a valid instance
	}
	return Id;
}

void UAttackComponent::QueueNext(UAttackDefinition* Next, float ExtraHold)
{
	QueuedNext = Next;
	QueuedHold = ExtraHold;
	QueuedChainAt = -1.f;
}

void UAttackComponent::QueueNextAt(UAttackDefinition* Next, float ExtraHold, float ChainIntoRecovery)
{
	QueuedNext = Next;
	QueuedHold = ExtraHold;
	QueuedChainAt = FMath::Max(0.f, ChainIntoRecovery);
}

void UAttackComponent::ClearQueue()
{
	QueuedNext = nullptr;
	QueuedHold = 0.f;
	QueuedChainAt = -1.f;
}

bool UAttackComponent::SwapRecovery(UAttackDefinition* SameClip)
{
	if (!CurrentDefinition || !SameClip || SameClip->Clip != CurrentDefinition->Clip)
	{
		return false;
	}
	const FAttackTiming New = SameClip->GetTiming();
	if (!FMath::IsNearlyEqual(New.Windup, Timing.Windup, 0.001f) || !FMath::IsNearlyEqual(New.Active, Timing.Active, 0.001f)
		|| Elapsed > Timing.Windup + Timing.Active)
	{
		return false; // a different windup or active window would jump the pose; too late once the swing is over
	}
	ClearQueue();
	CurrentDefinition = SameClip;
	Timing = New;
	UE_LOG(LogTemp, Log, TEXT("BossTest: %s swaps to %s at %.2f s"), *GetOwner()->GetName(), *SameClip->GetName(), Elapsed);
	return true;
}

bool UAttackComponent::StartQueued()
{
	// Asked once: a refusal (a pending stagger, or the follow-up no longer fits) clears the queue for good.
	UAttackDefinition* Next = QueuedNext;
	const float NextHold = QueuedHold;
	if (!Next)
	{
		return false;
	}
	if (CanContinue && !CanContinue())
	{
		UE_LOG(LogTemp, Log, TEXT("BossTest: %s doesn't continue into %s"), *GetOwner()->GetName(), *Next->GetName());
		ClearQueue();
		return false;
	}
	AActor* KeepTarget = Target.Get();
	if (StartAttack(Next, KeepTarget, Next->BlendIn, Next->bEaseBlendIn, NextHold))
	{
		OnStepStarted.Broadcast(Next, InstanceId);
		return true;
	}
	return false;
}

void UAttackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearEffects();
	if (TrailComponent)
	{
		TrailComponent->DestroyComponent();
		TrailComponent = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UAttackComponent::ClearEffects()
{
	for (TObjectPtr<UParticleSystemComponent>& Cue : CueComponents)
	{
		if (Cue)
		{
			Cue->DeactivateSystem();
			Cue->DestroyComponent();
		}
		Cue = nullptr;
	}
	// Attached one-shots (the cast glow on his hand) stop emitting when he's interrupted, and finish by themselves.
	for (TObjectPtr<UParticleSystemComponent>& Cue : OneShotCues)
	{
		if (IsValid(Cue))
		{
			Cue->DeactivateSystem();
		}
	}
	OneShotCues.Reset();
	EndTrails();
}

bool UAttackComponent::StartAttack(UAttackDefinition* Definition, AActor* InTarget, float BlendIn, bool bEasedBlend, float ExtraHold)
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
	SettleMontage = nullptr; // the new attack's clip takes the slot and blends the settle out
	CurrentDefinition = Definition;
	Timing = Definition->GetTiming();
	if (ExtraHold != 0.f)
	{
		AddHoldTime(Timing, ExtraHold);
	}
	// Armour from the first frame, before this component next ticks.
	const FAttackSample First = EvaluateAttack(Timing, 0.f);
	SetOwnerArmor(ArmorAt(First.Phase, Definition->GetWindupArmor(), Definition->GetActiveArmor()));
	Elapsed = 0.f;
	InstanceId = AllocateInstanceId();
	Target = InTarget;
	bDeliveryDone = false;
	bStruckTarget = false;
	CueFired.Init(false, Definition->Cues.Num());
	CueComponents.SetNum(Definition->Cues.Num());
	PrevPoints.Reset();
	bLungeDone = false;
	bAtBranch = false;
	// A branch whose variant can't play (no clip) counts as already decided, so the attack never waits there.
	bBranchResolved = !(Definition->BranchVariant && Definition->BranchVariant->Clip);
	BranchWait = 0.f;

	// An attack with a lead-in starts on that clip (Double Execution's re-raise).
	CurrentSpan = First.Span;
	Montage = PlayPaused(SpanClip(First.Span), BlendIn, bEasedBlend);
	if (!Montage)
	{
		CurrentDefinition = nullptr;
		return false;
	}
	Anim->Montage_SetPosition(Montage, First.SpanTime);

	UE_LOG(LogTemp, Log, TEXT("BossTest: %s starts %s (instance %u)"), *GetOwner()->GetName(), *Definition->GetName(), InstanceId);
	return true;
}

void UAttackComponent::StopAttack(float BlendOut, bool bEased)
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
				FMontageBlendSettings Out(BlendOut);
				Out.Blend.BlendOption = bEased ? EAlphaBlendOption::HermiteCubic : EAlphaBlendOption::Linear;
				Anim->Montage_StopWithBlendSettings(Out, Montage);
			}
		}
	}
	EndAttack();
}

void UAttackComponent::StopSettle(float BlendOut)
{
	if (!SettleMontage)
	{
		return;
	}
	if (USkeletalMeshComponent* Mesh = GetOwnerMesh())
	{
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			if (Anim->Montage_IsActive(SettleMontage))
			{
				FMontageBlendSettings Out(BlendOut);
				Out.Blend.BlendOption = bSettleEased ? EAlphaBlendOption::HermiteCubic : EAlphaBlendOption::Linear;
				Anim->Montage_StopWithBlendSettings(Out, SettleMontage);
			}
		}
	}
	SettleMontage = nullptr;
}

void UAttackComponent::TickSettle(float DeltaTime)
{
	// Another clip took the slot (a new attack, a dodge, a hit): the settle is over.
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || !Anim->Montage_IsActive(SettleMontage))
	{
		SettleMontage = nullptr;
		return;
	}
	SettleLeft -= DeltaTime; // the owner's clock: hit-stop pauses it too
	if (SettleLeft <= 0.f)
	{
		StopSettle(SettleBlendOut);
	}
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
	ClearQueue();
	ClearEffects();
	if (UDeliveryComponent* Delivery = GetOwner()->FindComponentByClass<UDeliveryComponent>())
	{
		Delivery->ForgetInstance(InstanceId); // rings already launched keep their own skip sets
	}
	bDeliveryDone = false;
	CurrentSpan = EAttackSpan::Main;
	CueFired.Reset();
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
		UAnimMontage* Crossfaded = PlayPaused(Definition->Clip, FMath::Max(SwitchBlend, Definition->BlendIn), true);
		if (!Crossfaded)
		{
			return false;
		}
		Montage = Crossfaded;
		Elapsed = 0.f;
		if (USkeletalMeshComponent* Mesh = GetOwnerMesh())
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				Anim->Montage_SetPosition(Montage, EvaluateAttack(Definition->GetTiming(), 0.f).SpanTime); // from its trim
			}
		}
		PrevPoints.Reset();
		bAtBranch = false;
		bBranchResolved = true; // the variant has no branch of its own to wait at
	}
	CurrentDefinition = Definition;
	Timing = Definition->GetTiming();
	CurrentSpan = EAttackSpan::Main;
	ClearEffects();
	CueFired.Init(false, Definition->Cues.Num());
	CueComponents.SetNum(Definition->Cues.Num());
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
	if (SettleMontage)
	{
		TickSettle(DeltaTime);
	}
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
	// A combo's follow-up at its chain point in this attack's recovery: same tick, so no frame without armour.
	if (QueuedNext && QueuedChainAt >= 0.f && Elapsed >= Timing.Windup + Timing.Active + QueuedChainAt && StartQueued())
	{
		return;
	}
	if (!CurrentDefinition)
	{
		return; // a queued step that couldn't start (a missing clip) already ended this attack
	}
	Pose(Sample);
	SetOwnerArmor(ArmorAt(Sample.Phase, CurrentDefinition->GetWindupArmor(), CurrentDefinition->GetActiveArmor()));
	UpdateCues(Sample.ClipTime);
	UpdateTrails(Sample.Phase == EAttackPhase::Active);
	if (!bDeliveryDone && CurrentDefinition->Delivery != EAttackDelivery::None && Sample.Phase >= EAttackPhase::Active
		&& Sample.ClipTime >= CurrentDefinition->DeliveryClipAt - KINDA_SMALL_NUMBER)
	{
		Deliver();
	}

	const FLungeWindow Window = LungeWindow(Timing, CurrentDefinition->LungeStartsBeforeActive, CurrentDefinition->LungeEndsIntoActive);
	if (!bLungeDone && Elapsed >= Window.Start)
	{
		bLungeDone = true;
		ApplyLunge(Window.Duration);
	}

	// No sockets, no sweep: an empty socket name gives the mesh's own transform, which would sweep his feet.
	const bool bHasWeapon = !CurrentDefinition->SocketStart.IsNone() && !CurrentDefinition->SocketEnd.IsNone();
	if (CurrentDefinition->bStrikesTarget)
	{
		// The finishing blow: no sweep, one guaranteed hit on the target as the active window opens (the clip and the
		// snap put the blade through him there; the riposte smoke test checks it).
		PrevPoints.Reset();
		if (!bStruckTarget && Sample.Phase >= EAttackPhase::Active)
		{
			bStruckTarget = true;
			// Land on the exact strike pose: a frame that overshot the active start steps back to it, so the hit-stop
			// holds the blade where it crosses him (it moves up to 65 cm a frame there).
			if (Elapsed > Timing.Windup)
			{
				Elapsed = Timing.Windup;
				Pose(EvaluateAttack(Timing, Elapsed));
			}
			UCombatantComponent* Victim = Target.IsValid() ? Target->FindComponentByClass<UCombatantComponent>() : nullptr;
			if (Victim)
			{
				ResolveVictims({ Victim });
			}
		}
	}
	else if (bHasWeapon && ActiveOverlap(Timing, Before, Elapsed))
	{
		SweepWeapon();
	}
	else
	{
		PrevPoints.Reset();
	}

	if (Sample.Phase == EAttackPhase::Done && CurrentDefinition)
	{
		// A queued sequence step starts here, in the same tick: no frame without armour, a new instance, its own blend-in.
		if (QueuedNext && QueuedChainAt < 0.f && StartQueued())
		{
			return;
		}
		// Free. With a settle the clip plays on through its follow-through (the look only), then blends out; without,
		// it blends out now, over the attack's own (sized, eased) blend-out.
		const UAttackDefinition* Def = CurrentDefinition;
		USkeletalMeshComponent* SettleMesh = GetOwnerMesh();
		UAnimInstance* SettleAnim = SettleMesh ? SettleMesh->GetAnimInstance() : nullptr;
		if (Def->SettleTime > 0.f && SettleAnim && Montage && Anim->Montage_IsActive(Montage) && CurrentSpan == EAttackSpan::Main)
		{
			SettleMontage = Montage;
			SettleLeft = Def->SettleTime;
			SettleBlendOut = Def->BlendOut;
			bSettleEased = Def->bEaseBlendOut;
			SettleAnim->Montage_SetPlayRate(Montage, 1.f);
			SettleAnim->Montage_Resume(Montage);
			EndAttack(); // the attack is over for the rules; only the montage plays on
			return;
		}
		StopAttack(Def->BlendOut, Def->bEaseBlendOut);
	}
}

UAnimSequenceBase* UAttackComponent::SpanClip(EAttackSpan Span) const
{
	if (!CurrentDefinition)
	{
		return nullptr;
	}
	switch (Span)
	{
	case EAttackSpan::LeadIn: return CurrentDefinition->LeadInClip;
	case EAttackSpan::Tail: return CurrentDefinition->TailClip;
	default: return CurrentDefinition->Clip;
	}
}

void UAttackComponent::UpdateCues(float ClipTime)
{
	const TArray<FAttackCue>& Cues = CurrentDefinition->Cues;
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	if (!Mesh || CueFired.Num() != Cues.Num() || CueComponents.Num() != Cues.Num())
	{
		return;
	}
	for (int32 i = 0; i < Cues.Num(); ++i)
	{
		const FAttackCue& Cue = Cues[i];
		if (!CueFired[i] && Cue.Effect && ClipTime >= Cue.ClipFrom)
		{
			CueFired[i] = true;
			UParticleSystemComponent* Spawned = nullptr;
			if (Cue.bAttached)
			{
				Spawned = UGameplayStatics::SpawnEmitterAttached(Cue.Effect, Mesh, Cue.Socket, FVector::ZeroVector, FRotator::ZeroRotator,
					FVector(Cue.Scale), EAttachLocation::SnapToTarget, Cue.bOneShot);
			}
			else
			{
				const ACharacter* Character = Cast<ACharacter>(GetOwner());
				const FVector Feet = Character ? Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) : GetOwner()->GetActorLocation();
				const FVector At = Cue.Socket.IsNone() ? Feet : Mesh->GetSocketLocation(Cue.Socket);
				Spawned = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Cue.Effect, At, GetOwner()->GetActorRotation(), FVector(Cue.Scale), Cue.bOneShot);
			}
			if (!Cue.bOneShot)
			{
				CueComponents[i] = Spawned; // looping: stopped at ClipTo or the attack's end, never leaked
			}
			else if (Cue.bAttached && Spawned)
			{
				OneShotCues.Add(Spawned);
			}
		}
		if (CueComponents[i] && Cue.ClipTo > Cue.ClipFrom && ClipTime >= Cue.ClipTo)
		{
			CueComponents[i]->DeactivateSystem();
			CueComponents[i]->DestroyComponent();
			CueComponents[i] = nullptr;
		}
	}
}

void UAttackComponent::UpdateTrails(bool bActive)
{
	UParticleSystem* Effect = CurrentDefinition ? CurrentDefinition->TrailEffect : nullptr;
	if (bActive && Effect && !bTrailsOn)
	{
		USkeletalMeshComponent* Mesh = GetOwnerMesh();
		if (!Mesh)
		{
			return;
		}
		// Cascade's anim trail reads its parent mesh's sockets, so it must be attached to the mesh (a free one asserts).
		if (!TrailComponent || TrailComponent->Template != Effect)
		{
			if (TrailComponent)
			{
				TrailComponent->DestroyComponent();
			}
			TrailComponent = UGameplayStatics::SpawnEmitterAttached(Effect, Mesh, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, false);
		}
		else
		{
			TrailComponent->ActivateSystem(true);
		}
		if (TrailComponent)
		{
			TrailComponent->BeginTrails(CurrentDefinition->SocketStart, CurrentDefinition->SocketEnd, ETrailWidthMode_FromCentre, 1.f);
			bTrailsOn = true;
		}
	}
	else if (!bActive && bTrailsOn)
	{
		EndTrails();
	}
}

void UAttackComponent::EndTrails()
{
	if (TrailComponent && bTrailsOn)
	{
		TrailComponent->EndTrails();
	}
	bTrailsOn = false;
}

void UAttackComponent::Deliver()
{
	bDeliveryDone = true;
	UDeliveryComponent* Delivery = GetOwner()->FindComponentByClass<UDeliveryComponent>();
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	const UAttackDefinition* Def = CurrentDefinition;
	if (!Delivery || !Mesh || !Def)
	{
		UE_LOG(LogTemp, Warning, TEXT("BossTest: %s can't launch %s's delivery (no delivery component or mesh)"), *GetOwner()->GetName(), Def ? *Def->GetName() : TEXT("?"));
		return;
	}
	const FVector From = Def->DeliverySocket.IsNone() ? GetOwner()->GetActorLocation() : Mesh->GetSocketLocation(Def->DeliverySocket);
	if (Def->Delivery == EAttackDelivery::Ring)
	{
		Delivery->LaunchRing(*Def, InstanceId, From);
	}
	else if (Def->Delivery == EAttackDelivery::Projectile)
	{
		// Aimed once, at where the target is now (spec section 5: Soul Mark aims at its windup's end, then commits).
		const AActor* TargetActor = Target.Get();
		const FVector Aim = TargetActor ? TargetActor->GetActorLocation() : From + GetOwner()->GetActorForwardVector() * 1000.f;
		Delivery->LaunchProjectile(*Def, From, (Aim - From).GetSafeNormal());
	}
}

void UAttackComponent::Pose(const FAttackSample& Sample)
{
	// Crossing into another span's clip: Paragon's chained clips join exactly, so the switch is instant.
	if (Sample.Span != CurrentSpan)
	{
		if (UAnimSequenceBase* Clip = SpanClip(Sample.Span))
		{
			if (UAnimMontage* Next = PlayPaused(Clip, 0.f, false))
			{
				Montage = Next;
			}
		}
		CurrentSpan = Sample.Span;
	}
	if (USkeletalMeshComponent* Mesh = GetOwnerMesh())
	{
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			Anim->Montage_SetPosition(Montage, Sample.SpanTime);
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
	// faces now; otherwise half the cap straight ahead. An intercepting lunge (the Advance's dash) aims at where
	// the target will be instead, whichever way he faces.
	FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
	TOptional<float> Edge;
	if (const AActor* TargetActor = Target.Get())
	{
		float TargetRadius = 0.f;
		if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
		{
			TargetRadius = TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
		}
		FVector Aim = TargetActor->GetActorLocation();
		if (CurrentDefinition->LungeAim == ELungeAim::Intercept)
		{
			// A dodge or a lunge is root motion: never led (it would fling him 2 m with it). Running and sprinting are.
			const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
			const bool bRootMotion = TargetCharacter && TargetCharacter->GetCharacterMovement()->HasRootMotionSources();
			Aim = InterceptPoint(Aim, bRootMotion ? FVector::ZeroVector : TargetActor->GetVelocity(), Duration, CurrentDefinition->InterceptLeadMax);
			const FVector ToAim = (Aim - Character->GetActorLocation()).GetSafeNormal2D();
			if (!ToAim.IsNearlyZero())
			{
				Forward = ToAim;
			}
		}
		const float Ahead = FVector::DotProduct(Aim - Character->GetActorLocation(), Forward);
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
	ResolveVictims(Victims);
}

void UAttackComponent::ResolveVictims(const TArray<UCombatantComponent*>& Victims)
{
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	const UAttackDefinition* Def = CurrentDefinition;
	if (!Mesh || !Def)
	{
		return;
	}
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
		OnSweepResolved.Broadcast(Outcome, Victim->GetOwner(), InstanceId);
		if (Outcome.Result != EHitResult::Immune)
		{
			// One slam, one dodge: whoever the direct strike hit, or who dodged, blocked or deflected it, is skipped by
			// this attack's ring.
			if (UDeliveryComponent* Delivery = GetOwner()->FindComponentByClass<UDeliveryComponent>())
			{
				Delivery->NoteDirectResult(InstanceId, Victim->GetTargetKey());
			}
		}
		UE_LOG(LogTemp, Log, TEXT("BossTest: %s -> %s with %s: %s, %.0f damage%s%s%s"),
			*GetOwner()->GetName(), *Victim->GetOwner()->GetName(), *Def->GetName(), AttackResultName(Outcome.Result), Outcome.DamageDealt,
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
				Back.AttackInstanceId = AllocateInstanceId();
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
			if (Def->HitEffect)
			{
				UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Def->HitEffect, Mesh->GetSocketLocation(Def->SocketEnd), FRotator::ZeroRotator,
					FVector(Def->HitEffectScale), true);
			}
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
