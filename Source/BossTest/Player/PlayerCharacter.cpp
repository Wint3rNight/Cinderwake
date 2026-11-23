#include "Player/PlayerCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraComponent.h"
#include "Combat/AttackComponent.h"
#include "Combat/AttackDefinition.h"
#include "Combat/CombatantComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Misc/App.h"
#include "Player/LockOnComponent.h"
#include "Player/PlayerMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float PlayerCapsuleRadius = 38.f;
	constexpr float PlayerCapsuleHalfHeight = 100.f;
	constexpr int32 DodgeCurveKeys = 24;
}

APlayerCharacter::APlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPlayerMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(TEXT("/Game/ParagonFengMao/Characters/Heroes/FengMao/Meshes/FengMao_GDC.FengMao_GDC"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimClass(TEXT("/Game/ParagonFengMao/Characters/Heroes/FengMao/FengMao_AnimBlueprint"));

	GetCapsuleComponent()->InitCapsuleSize(PlayerCapsuleRadius, PlayerCapsuleHalfHeight);
	if (MeshAsset.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(MeshAsset.Object);
	}
	if (AnimClass.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimClass.Class);
	}
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -PlayerCapsuleHalfHeight));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // Paragon meshes face +Y
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(RootComponent);
	Arm->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
	Arm->TargetArmLength = 520.f; // the normal combat profile, 5.2 m
	Arm->SocketOffset = FVector(0.f, 55.f, 75.f);
	Arm->bUsePawnControlRotation = true;
	Arm->bEnableCameraLag = true;
	Arm->CameraLagSpeed = 12.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->bOrientRotationToMovement = true;
	MoveComp->RotationRate = FRotator(0.f, 900.f, 0.f);
	MoveComp->MaxWalkSpeed = RunSpeed;
	MoveComp->BrakingDecelerationWalking = 2400.f;
	// The movement component ticks after the character (ordered in BeginPlay), so input, pushes and
	// speed changes made this tick move him this frame, and the animation reads this frame's state.
	MoveComp->bTickBeforeOwner = false;

	Attacks = CreateDefaultSubobject<UAttackComponent>(TEXT("Attacks"));
	Attacks->MontageSlot = MontageSlot;
	Combatant = CreateDefaultSubobject<UCombatantComponent>(TEXT("Combatant"));
	Combatant->MaxHealth = 300.f;
	LockOn = CreateDefaultSubobject<ULockOnComponent>(TEXT("LockOn"));
}

void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	StartLocation = GetActorLocation();
	StartRotation = GetActorRotation();
	BaseSocketOffset = Arm->SocketOffset;
	GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
	Attacks->MontageSlot = MontageSlot;

	// Tick order: combatant (ends hit-stop) -> attacks (advances the swing) -> this character (input,
	// rules) -> movement -> mesh. The mesh already waits for the movement component and the attacks.
	Attacks->AddTickPrerequisiteComponent(Combatant);
	AddTickPrerequisiteComponent(Attacks);
	GetCharacterMovement()->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);

	// Stamina costs come from the attack assets, so tuning an asset retunes the rules.
	Actions.Costs.Lights.Reset();
	for (const UAttackDefinition* Light : LightAttacks)
	{
		Actions.Costs.Lights.Add(Light ? Light->StaminaCost : 0.f);
	}
	Actions.Costs.Heavy = HeavyAttack ? HeavyAttack->StaminaCost : 0.f;
	Actions.Costs.DodgeLights.Reset();
	for (const UAttackDefinition* DodgeLight : DodgeLightAttacks)
	{
		Actions.Costs.DodgeLights.Add(DodgeLight ? DodgeLight->StaminaCost : 0.f);
	}
	Actions.Costs.DodgeHeavy = DodgeHeavyAttack ? DodgeHeavyAttack->StaminaCost : 0.f;
	Actions.Costs.SprintAttack = SprintAttack ? SprintAttack->StaminaCost : 0.f;
	Actions.Tuning = Rules;
	Actions.Reset();
	ApplyGuardTuning();
	if (HeavyAttack && HeavyAttack->BranchVariant != ChargedHeavyAttack)
	{
		UE_LOG(LogTemp, Error, TEXT("BossTest: DA_Heavy's branch variant isn't the charged heavy set on %s; the charge won't match"), *GetName());
	}

	if (!LightRiseAttack)
	{
		UE_LOG(LogTemp, Error, TEXT("BossTest: %s is missing its Step 4d data (the stand-up Light 1); run Docs/tools/setup_player.py"), *GetName());
	}
	if (!ChargedHeavyAttack || !SprintAttack || !GuardRaiseClip || !GuardLoopClip || !BlockClip || !GuardBreakClip)
	{
		UE_LOG(LogTemp, Error, TEXT("BossTest: %s is missing its Step 4b data (charged heavy, sprint attack or guard clips); run Docs/tools/setup_player.py"), *GetName());
	}
	if (LightAttacks.IsEmpty() || !HeavyAttack || DodgeLightAttacks.IsEmpty() || !DodgeHeavyAttack || !DodgeClip || !BackstepClip || !HealClip || !SprintClip || !DeathClip)
	{
		UE_LOG(LogTemp, Error, TEXT("BossTest: %s is missing attack assets or clips; set them on BP_FengMao (Docs/tools/setup_player.py)"), *GetName());
	}

	// The dodge's progress over time, sampled from the tested profile. The push works from position,
	// so it lands on the full distance at any frame rate.
	DodgeProfile = FDodgeProfile::Make(DodgeDistance, Actions.Tuning.DodgeDuration, DodgeEarlyDistance, DodgeEarlyTime);
	BackstepProfile = DodgeProfile.Scaled(BackstepDistance);
	DodgeDistanceCurve = NewObject<UCurveFloat>(this);
	for (int32 i = 0; i <= DodgeCurveKeys; ++i)
	{
		const float Alpha = static_cast<float>(i) / DodgeCurveKeys;
		DodgeDistanceCurve->FloatCurve.AddKey(Alpha, DodgeProfile.DistanceAt(Alpha * DodgeProfile.GetDuration()) / FMath::Max(DodgeDistance, 1.f));
	}

	Combatant->OnHitResolved.AddUObject(this, &APlayerCharacter::OnHitResolved);
	Attacks->OnHitLanded.AddUObject(this, &APlayerCharacter::OnHitLanded);
}

void APlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// Read input after the controller has gathered it this frame.
	AddTickPrerequisiteActor(NewController);
}

void APlayerCharacter::Tick(float DeltaSeconds)
{
	// DeltaSeconds is already scaled by this actor's time dilation, so hit-stop slows every
	// gameplay clock below (i-frames, the dodge lock, the heal, regeneration, the buffer).
	Super::Tick(DeltaSeconds);
	LastEventTime -= DeltaSeconds;

	FPlayerFrame Frame;
	Frame.Dt = DeltaSeconds;
	Frame.bAttackRunning = Attacks->IsAttacking();
	Frame.bChainOpen = Frame.bAttackRunning && Attacks->GetSample().bChainOpen;
	Frame.bAtChargeBranch = Attacks->IsAtBranch();
	// The attack's own clock (capped at 1/30 s a step), so a slow frame rate can't drop a dodge pressed in time.
	// Given to the rules before this frame's presses (the attack has already ticked this frame).
	Frame.bPastActive = Frame.bAttackRunning && Attacks->GetSample().Phase >= EAttackPhase::Recovery;
	Frame.TimeToFree = Frame.bAttackRunning ? Attacks->GetTimeToFree() : 0.f;
	Actions.NoteAttack(Frame.bPastActive, Frame.TimeToFree);
	ReadInput(Frame);
	const bool bHadWindow = Actions.HasPerfectWindow();
	Execute(Actions.Tick(Frame));
	if (!bHadWindow && Actions.HasPerfectWindow())
	{
		// A fresh press raised the guard: the resolver measures the perfect window from this frame's world time,
		// so it doesn't matter whether his sweep runs before or after this character in a frame.
		Combatant->State.GuardPressTime = GetWorld()->GetTimeSeconds();
	}
	if (Attacks->IsAtBranch())
	{
		Attacks->ResolveBranch(false); // the rules didn't charge it: the normal heavy goes on, same frame
	}
	SyncCombatant();

	TickMovement();
	TickEvadeVisuals();
	TickGuardVisuals();
	TickChargeEffect();
	TickCamera();

}

void APlayerCharacter::ReadInput(FPlayerFrame& Frame)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	// All input is read here into plain values, so moving to Enhanced Input later touches one function.
	Move.X = (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
	Move.Y = (PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
	Frame.bMoveHeld = !Move.IsNearlyZero();
	Frame.bSprintHeld = PC->IsInputKeyDown(EKeys::LeftShift);
	Frame.bHeavyHeld = PC->IsInputKeyDown(EKeys::RightMouseButton) || PC->IsInputKeyDown(EKeys::F);
	Frame.bGuardHeld = PC->IsInputKeyDown(EKeys::Q) || PC->IsInputKeyDown(EKeys::ThumbMouseButton);
	Frame.bGuardPressed = PC->WasInputKeyJustPressed(EKeys::Q) || PC->WasInputKeyJustPressed(EKeys::ThumbMouseButton);

	if (!LockOn->IsLocked())
	{
		float MouseX = 0.f, MouseY = 0.f;
		PC->GetInputMouseDelta(MouseX, MouseY);
		AddControllerYawInput(MouseX * MouseSensitivity);
		AddControllerPitchInput(-MouseY * MouseSensitivity);
	}

	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)) Actions.Press(EPlayerInput::Light);
	if (PC->WasInputKeyJustPressed(EKeys::RightMouseButton) || PC->WasInputKeyJustPressed(EKeys::F)) Actions.Press(EPlayerInput::Heavy);
	if (PC->WasInputKeyJustPressed(EKeys::SpaceBar)) Actions.Press(EPlayerInput::Dodge);
	if (PC->WasInputKeyJustPressed(EKeys::E)) Actions.Press(EPlayerInput::Heal);

	if (PC->WasInputKeyJustPressed(EKeys::Tab) || PC->WasInputKeyJustPressed(EKeys::MiddleMouseButton))
	{
		if (LockOn->IsLocked())
		{
			LockOn->Unlock();
		}
		else if (PC->PlayerCameraManager && !LockOn->TryLock(PC->PlayerCameraManager->GetCameraLocation(), PC->PlayerCameraManager->GetCameraRotation().Vector()))
		{
			SetEvent(TEXT("nothing to lock on to"), 1.f);
		}
	}
}

void APlayerCharacter::Execute(const FPlayerCommands& Commands)
{
	for (const EPlayerCommand Command : Commands)
	{
		switch (Command)
		{
		case EPlayerCommand::StartLight:
			StartAttack(LightAttacks.IsValidIndex(Actions.GetLightIndex()) ? LightAttacks[Actions.GetLightIndex()].Get() : nullptr);
			break;
		case EPlayerCommand::StartHeavy:
			StartAttack(HeavyAttack);
			break;
		case EPlayerCommand::ChainHeavy:
			// Out of another attack's follow-through: a longer, eased blend turns it into the heavy's windup.
			StartAttack(HeavyAttack, ChainedHeavyBlend, true);
			break;
		case EPlayerCommand::StartLightRise:
			StartAttack(LightRiseAttack ? LightRiseAttack.Get() : (LightAttacks.IsValidIndex(0) ? LightAttacks[0].Get() : nullptr), LightRiseBlend, true);
			break;
		case EPlayerCommand::RefuseInput:
			StaminaFlashUntil = GetWorld()->GetTimeSeconds() + 0.25;
			break;
		case EPlayerCommand::StartDodgeLight:
			StartAttack(DodgeLightAttacks.IsValidIndex(Actions.GetLightIndex()) ? DodgeLightAttacks[Actions.GetLightIndex()].Get() : nullptr);
			break;
		case EPlayerCommand::StartDodgeHeavy:
			StartAttack(DodgeHeavyAttack);
			break;
		case EPlayerCommand::StartDodge:
			StartDodge(false);
			break;
		case EPlayerCommand::StartBackstep:
			StartDodge(true);
			break;
		case EPlayerCommand::EndDodge:
			EndDodgePush();
			break;
		case EPlayerCommand::StopAttack:
			Attacks->StopAttack(0.05f);
			break;
		case EPlayerCommand::StartHeal:
			FaceForAction();
			PlayClip(HealClip, 1.f, 0.15f, 0.25f);
			break;
		case EPlayerCommand::CancelHeal:
			StopClip(0.15f);
			SetEvent(TEXT("heal interrupted"), 1.f);
			break;
		case EPlayerCommand::FinishHeal:
			Combatant->State.Health.Heal(Actions.Tuning.HealAmount);
			StopClip(0.3f);
			SetEvent(FString::Printf(TEXT("healed +%.0f"), Actions.Tuning.HealAmount), 1.2f);
			break;
		case EPlayerCommand::StartSprint:
			SetSprinting(true);
			break;
		case EPlayerCommand::StopSprint:
			SetSprinting(false);
			break;
		case EPlayerCommand::PlayHitReact:
			PlayReaction(false);
			break;
		case EPlayerCommand::PlayKnockdown:
			PlayReaction(true);
			break;
		case EPlayerCommand::BeginCharge:
			// The heavy turns into the charged heavy: a crossfade to its own clip, whose timeline starts here.
			if (Attacks->ResolveBranch(true))
			{
				if (ChargeEffect)
				{
					ChargeFX = UGameplayStatics::SpawnEmitterAttached(ChargeEffect, GetMesh(), TEXT("FX_Trail_L_02"), FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
				}
				SetEvent(TEXT("charging"), 0.8f);
			}
			break;
		case EPlayerCommand::StartSprintAttack:
			StartAttack(SprintAttack);
			break;
		case EPlayerCommand::StartGuard:
			GuardClipShown = nullptr; // TickGuardVisuals raises it
			break;
		case EPlayerCommand::StopGuard:
			GuardClipShown = nullptr;
			if (Actions.GetState() == EPlayerState::Free)
			{
				PlayClip(GuardLowerClip, 1.f, 0.1f, 0.2f, 1, 0.15f); // any action that follows cuts it
			}
			break;
		case EPlayerCommand::PlayBlock:
		case EPlayerCommand::PlayHeavyBlock:
		{
			const bool bHeavy = Command == EPlayerCommand::PlayHeavyBlock;
			PlayClip(BlockClip, 1.f, 0.03f, 0.1f, 1, bHeavy ? 0.02f : 0.06f);
			PushBack(bHeavy ? 80.f : 35.f, bHeavy ? 0.25f : 0.15f);
			if (BlockEffect)
			{
				UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BlockEffect, GetMesh()->GetSocketLocation(TEXT("FX_Trail_L_02")), GetActorRotation());
			}
			CameraKick(bHeavy ? 10.f : 6.f, 0.15f);
			break;
		}
		case EPlayerCommand::EndGuardBreak:
			StopClip(0.25f); // its pose was held; let him stand back up
			break;
		case EPlayerCommand::PlayDeflect:
		case EPlayerCommand::PlayHeavyDeflect:
		{
			// The block's impact, a touch slower, over the short deflect-stun; a small push (his hammer still has
			// weight), a big spark, and no camera kick: a steady camera says you won the exchange.
			const bool bHeavy = Command == EPlayerCommand::PlayHeavyDeflect;
			PlayClip(BlockClip, 0.8f, 0.03f, 0.1f, 1, 0.06f);
			PushBack(bHeavy ? Actions.Tuning.HeavyDeflectPush : Actions.Tuning.DeflectPush, 0.1f);
			if (PerfectGuardEffect)
			{
				// Spawned in the world, not attached: an attached effect ticks on his dilated clock and would sit
				// on its first frame through the whole freeze.
				UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), PerfectGuardEffect, GetMesh()->GetSocketLocation(TEXT("FX_Trail_L_02")),
					GetActorRotation(), FVector(PerfectGuardEffectScale));
			}
			break;
		}
		case EPlayerCommand::PlayGuardBreak:
			PlayClip(GuardBreakClip, 1.f, 0.05f, 0.25f); // held at 0.30 s by TickGuardVisuals
			PushBack(50.f, 0.25f);
			CameraKick(16.f, 0.3f);
			SetEvent(TEXT("GUARD BROKEN"), 1.5f);
			break;
		case EPlayerCommand::PlayDeath:
			LockOn->Unlock();
			if (PlayClip(DeathClip, 1.f, 0.1f, 0.f))
			{
				// Hold the last pose instead of blending back to idle when the clip ends.
				if (FAnimMontageInstance* Instance = GetMesh()->GetAnimInstance()->GetActiveInstanceForMontage(ClipMontage))
				{
					Instance->bEnableAutoBlendOut = false;
				}
				// The clip ends mid-fall (Paragon handed over to physics here): a ragdoll finishes it.
				Combatant->RagdollAfter(ClipMontage ? ClipMontage->GetPlayLength() : DeathClip->GetPlayLength());
			}
			SetEvent(TEXT("YOU DIED  (R to retry)"), 1000.f);
			break;
		}
	}
}

void APlayerCharacter::FaceForAction()
{
	const FVector Dir = InputDirection();
	if (const AActor* Target = LockOn->GetTarget())
	{
		const FVector To = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!To.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
		}
	}
	else if (!Dir.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
	}
}

bool APlayerCharacter::IsStaminaFlashing() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < StaminaFlashUntil;
}

void APlayerCharacter::StartAttack(UAttackDefinition* Definition, float BlendIn, bool bEasedBlend)
{
	// Snap to the target or the stick at the start; no steering during the attack (spec section 4).
	// Without lock-on, a target roughly in front still pulls the swing toward him (a soft lock), so
	// the lunge goes at him rather than past his flank.
	FaceForAction();
	AActor* Target = LungeTarget();
	if (Target)
	{
		const FVector To = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!To.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
		}
	}
	Attacks->StartAttack(Definition, Target, BlendIn, bEasedBlend);
}

AActor* APlayerCharacter::LungeTarget() const
{
	if (AActor* Locked = LockOn->GetTarget())
	{
		return Locked;
	}
	// Without lock-on, only lunge at someone roughly in front.
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(LungeTargetAngle));
	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		const UCombatantComponent* Other = Candidate != this ? Candidate->FindComponentByClass<UCombatantComponent>() : nullptr;
		if (!Other || Other->IsDead())
		{
			continue;
		}
		const FVector To = Candidate->GetActorLocation() - GetActorLocation();
		const float Distance = To.Size2D();
		if (FVector::DotProduct(GetActorForwardVector(), To.GetSafeNormal2D()) >= MinDot && Distance < BestDistance)
		{
			Best = Candidate;
			BestDistance = Distance;
		}
	}
	return Best;
}

void APlayerCharacter::StartDodge(bool bBackstep)
{
	EndDodgePush();
	if (PushId != 0)
	{
		GetCharacterMovement()->RemoveRootMotionSourceByID(PushId); // a block's push never steers a dodge
		PushId = 0;
	}
	EndEvadeVisuals(0.f);
	FVector Dir;
	const FDodgeProfile& Profile = bBackstep ? BackstepProfile : DodgeProfile;
	UAnimSequenceBase* Clip = nullptr;
	if (bBackstep)
	{
		// Step back from where you face (from the target when locked on).
		FaceForAction();
		Dir = -GetActorForwardVector();
		Clip = BackstepClip;
		EvadeClipStart = BackstepClipStart;
		EvadeClipEnd = BackstepClipEnd;
	}
	else
	{
		// The pose only goes forward, so turn to face the dodge.
		Dir = InputDirection();
		SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
		Clip = DodgeClip;
		EvadeClipStart = DodgeClipStart;
		EvadeClipEnd = DodgeClipEnd;
	}

	// The pose follows the dodge's clock, like attacks do: paused, and set every tick.
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && Clip)
	{
		EvadeMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, MontageSlot, 0.05f, 0.15f, 1.f);
		if (EvadeMontage)
		{
			Anim->Montage_Pause(EvadeMontage);
			Anim->Montage_SetPosition(EvadeMontage, EvadeClipStart);
		}
	}
	ClipMontage = nullptr; // the evade replaced whatever clip was on the slot

	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (EvadeStartEffect)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), EvadeStartEffect, Feet, Dir.Rotation());
	}
	for (UParticleSystem* Trail : EvadeTrailEffects)
	{
		if (Trail)
		{
			EvadeTrails.Add(UGameplayStatics::SpawnEmitterAttached(Trail, GetCapsuleComponent(), NAME_None,
				EvadeTrailOffset - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true));
		}
	}

	TSharedPtr<FRootMotionSource_MoveToDynamicForce> Push = MakeShared<FRootMotionSource_MoveToDynamicForce>();
	Push->InstanceName = TEXT("Dodge");
	Push->AccumulateMode = ERootMotionAccumulateMode::Override;
	Push->Priority = 5;
	Push->StartLocation = GetActorLocation();
	Push->InitialTargetLocation = GetActorLocation() + Dir.GetSafeNormal2D() * Profile.GetDistance();
	Push->TargetLocation = Push->InitialTargetLocation;
	Push->Duration = Profile.GetDuration();
	Push->TimeMappingCurve = DodgeDistanceCurve;
	Push->bRestrictSpeedToExpected = true; // blocked by his body, it doesn't rush to catch up afterwards
	Push->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate); // gravity still applies
	Push->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Push->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	DodgeMoveId = GetCharacterMovement()->ApplyRootMotionSource(Push);
}

void APlayerCharacter::TickEvadeVisuals()
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Actions.GetState() == EPlayerState::Dodging)
	{
		if (Anim && EvadeMontage && Anim->Montage_IsActive(EvadeMontage))
		{
			const float Alpha = FMath::Clamp(Actions.GetStateTime() / FMath::Max(Actions.Tuning.DodgeDuration, KINDA_SMALL_NUMBER), 0.f, 1.f);
			Anim->Montage_SetPosition(EvadeMontage, FMath::Lerp(EvadeClipStart, EvadeClipEnd, Alpha));
		}
		return;
	}
	// The evade ended (finished, or an attack, reaction or death took over).
	EndEvadeVisuals(0.15f);
}

void APlayerCharacter::EndEvadeVisuals(float BlendOut)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && EvadeMontage && Anim->Montage_IsActive(EvadeMontage))
	{
		Anim->Montage_Stop(BlendOut, EvadeMontage);
	}
	EvadeMontage = nullptr;
	for (UParticleSystemComponent* Trail : EvadeTrails)
	{
		if (Trail)
		{
			Trail->Deactivate(); // what's already spawned fades out on its own
		}
	}
	EvadeTrails.Reset();
}

void APlayerCharacter::EndDodgePush()
{
	if (DodgeMoveId != 0)
	{
		GetCharacterMovement()->RemoveRootMotionSourceByID(DodgeMoveId);
		DodgeMoveId = 0;
	}
}

void APlayerCharacter::SetSprinting(bool bOn)
{
	// Sprint is ordinary input movement at a higher top speed. While it runs, the animation blueprint
	// doesn't see his acceleration, so it plays the slot (the sprint loop) over his whole body.
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->MaxWalkSpeed = bOn ? SprintSpeed : RunSpeed;
	if (UPlayerMovementComponent* PlayerMove = Cast<UPlayerMovementComponent>(MoveComp))
	{
		PlayerMove->bHideAccelerationFromAnimation = bOn;
	}
	if (bOn)
	{
		PlayClip(SprintClip, 1.f, 0.2f, 0.2f, 1000);
	}
	else
	{
		StopClip(0.2f);
	}
}

void APlayerCharacter::TickMovement()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const bool bSteer = Actions.CanSteer();
	const FVector Dir = InputDirection();
	const bool bMoving = bSteer && !Dir.IsNearlyZero();
	const bool bGuarding = Actions.GetState() == EPlayerState::Guard;

	// Sprint and guard walking both play their own clips on the slot, so the animation blueprint mustn't
	// see his acceleration (it would play the slot on the upper body only, over a forward jog).
	if (UPlayerMovementComponent* PlayerMove = Cast<UPlayerMovementComponent>(MoveComp))
	{
		PlayerMove->bHideAccelerationFromAnimation = Actions.IsSprinting() || bGuarding;
	}
	MoveComp->MaxWalkSpeed = Actions.IsSprinting() ? SprintSpeed : bGuarding ? GuardWalkSpeed : RunSpeed;
	if (bGuarding)
	{
		// Walking with the guard up: he keeps facing where the camera looks (the target, when locked on).
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bUseControllerDesiredRotation = true;
		if (!Dir.IsNearlyZero())
		{
			AddMovementInput(Dir, 1.f);
		}
		return;
	}

	// Running, he faces where he runs, locked on or not (his animation blueprint only has a forward
	// jog). Standing still while locked on, he turns to face the target (the camera looks at it).
	MoveComp->bOrientRotationToMovement = bSteer && bMoving;
	MoveComp->bUseControllerDesiredRotation = bSteer && !bMoving && LockOn->IsLocked();
	if (!bMoving)
	{
		return;
	}
	// Moving off blends out what's left of a reaction, dodge or heal clip, which would otherwise play
	// over the jog's upper body.
	if (!Actions.IsSprinting() && ClipMontage)
	{
		StopClip(0.15f);
	}
	AddMovementInput(Dir, 1.f);
}

void APlayerCharacter::TickGuardVisuals()
{
	const EPlayerState State = Actions.GetState();
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (State == EPlayerState::GuardBroken)
	{
		// Hold the stagger's leaning-back pose: the clip goes on to put him on the floor, which would read as a knockdown.
		if (Anim && ClipMontage && Anim->Montage_IsPlaying(ClipMontage) && Anim->Montage_GetPosition(ClipMontage) >= 0.30f)
		{
			Anim->Montage_Pause(ClipMontage);
		}
		return;
	}
	if (State != EPlayerState::Guard)
	{
		return; // block-stun plays its own twitch; free and the rest play theirs
	}

	UAnimSequenceBase* Wanted = GuardLoopClip;
	float Rate = 1.f;
	if (Actions.GetStateTime() < Actions.Tuning.GuardRaiseTime)
	{
		Wanted = GuardRaiseClip; // lifting the sword: only its first 0.18 s play before the loop takes over
	}
	else
	{
		const FVector Local = GetActorRotation().UnrotateVector(GetVelocity().GetSafeNormal2D() * GetVelocity().Size2D());
		const float Speed = Local.Size2D();
		if (Speed > 40.f)
		{
			// The walk clip by direction relative to his facing, with 15 degrees of hysteresis at the diagonals.
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)); // 0 ahead, +90 right
			auto InBand = [Angle](float Centre, float Half) { return FMath::Abs(FMath::FindDeltaAngleDegrees(Centre, Angle)) <= Half; };
			const bool bKeep = (GuardClipShown == GuardWalkForward && InBand(0.f, 60.f)) || (GuardClipShown == GuardWalkRight && InBand(90.f, 60.f))
				|| (GuardClipShown == GuardWalkBack && InBand(180.f, 60.f)) || (GuardClipShown == GuardWalkLeft && InBand(-90.f, 60.f));
			Wanted = bKeep ? GuardClipShown.Get() : InBand(0.f, 45.f) ? GuardWalkForward.Get() : InBand(90.f, 45.f) ? GuardWalkRight.Get() : InBand(-90.f, 45.f) ? GuardWalkLeft.Get() : GuardWalkBack.Get();
			Rate = FMath::Max(0.75f, Speed / FMath::Max(GuardWalkAuthoredSpeed, 1.f)); // slower plays the legs too slowly
		}
	}
	GuardClipAge += FApp::GetDeltaTime();
	const bool bWalkToWalk = GuardClipShown && GuardClipShown != GuardRaiseClip && GuardClipShown != GuardLoopClip
		&& Wanted != GuardRaiseClip && Wanted != GuardLoopClip;
	if (Wanted && Wanted != GuardClipShown && (!bWalkToWalk || GuardClipAge >= 0.2f))
	{
		// Walk to walk: start the new clip at the old one's phase, so the feet stay in step.
		float StartAt = 0.f;
		if (bWalkToWalk && Anim && ClipMontage && GuardClipShown->GetPlayLength() > 0.f)
		{
			const float Phase = FMath::Fmod(Anim->Montage_GetPosition(ClipMontage), GuardClipShown->GetPlayLength()) / GuardClipShown->GetPlayLength();
			StartAt = Phase * Wanted->GetPlayLength();
		}
		const bool bRaise = Wanted == GuardRaiseClip;
		PlayClip(Wanted, Rate, bRaise ? 0.08f : 0.15f, 0.15f, bRaise ? 1 : 1000, StartAt);
		GuardClipShown = Wanted;
		GuardClipAge = 0.f;
	}
	else if (Anim && ClipMontage && Wanted != GuardRaiseClip && Wanted != GuardLoopClip)
	{
		Anim->Montage_SetPlayRate(ClipMontage, Rate);
	}
}

void APlayerCharacter::PushBack(float Distance, float Duration)
{
	// Away from whoever hit him (or straight back), on the ground.
	const AActor* Attacker = LastAttacker.Get();
	const FVector Away = Attacker ? (GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D() : -GetActorForwardVector();
	TSharedPtr<FRootMotionSource_MoveToForce> Push = MakeShared<FRootMotionSource_MoveToForce>();
	Push->InstanceName = TEXT("GuardPush");
	Push->AccumulateMode = ERootMotionAccumulateMode::Override;
	Push->Priority = 4;
	Push->StartLocation = GetActorLocation();
	Push->TargetLocation = GetActorLocation() + Away * Distance;
	Push->Duration = Duration;
	Push->bRestrictSpeedToExpected = true;
	Push->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	Push->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Push->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	if (PushId != 0)
	{
		GetCharacterMovement()->RemoveRootMotionSourceByID(PushId);
	}
	PushId = GetCharacterMovement()->ApplyRootMotionSource(Push);
}

void APlayerCharacter::TickChargeEffect()
{
	// The glow lasts while the charged heavy is still winding up or holding; the strike ends it.
	if (!ChargeFX)
	{
		return;
	}
	const EAttackPhase Phase = Attacks->GetSample().Phase;
	if (!Attacks->IsAttacking() || (Phase != EAttackPhase::Windup && Phase != EAttackPhase::Hold))
	{
		ChargeFX->Deactivate();
		ChargeFX = nullptr;
	}
}

void APlayerCharacter::ApplyGuardTuning()
{
	// The guard's numbers live in the rules; the resolver reads them from the combatant.
	FCombatantState& S = Combatant->State;
	S.GuardReduction = Rules.GuardReduction;
	S.GuardArc = Rules.GuardArc;
	S.GuardRaiseTime = Rules.GuardRaiseTime;
	S.PerfectGuardFrom = Rules.PerfectGuardFrom;
	S.PerfectGuardUntil = Rules.PerfectGuardUntil;
	S.PerfectGuardHitStop = Rules.PerfectGuardHitStop;
	S.HeavyPerfectGuardHitStop = Rules.HeavyPerfectGuardHitStop;
}

void APlayerCharacter::SyncCombatant()
{
	// Written after every model call, so a hit landing later this frame sees the current i-frames.
	Combatant->State.bInvulnerable = Actions.IsInvulnerable();
	Combatant->State.bGuarding = Actions.IsGuardActive();
	if (Actions.GetState() != EPlayerState::Guard && Actions.GetState() != EPlayerState::BlockStun)
	{
		Combatant->State.GuardPressTime = -1.0; // the guard is down: no press to measure from
	}
}

void APlayerCharacter::PlayReaction(bool bKnockdown)
{
	const AActor* Attacker = LastAttacker.Get();
	const FVector ToAttacker = Attacker ? (Attacker->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() : GetActorForwardVector();
	const float Front = FVector::DotProduct(GetActorForwardVector(), ToAttacker);
	const float Side = FVector::DotProduct(GetActorRightVector(), ToAttacker);

	if (bKnockdown)
	{
		UAnimSequenceBase* Clip = Front >= 0.f ? KnockdownFront : KnockdownBack;
		// Played to fit the knockdown, so he's up when it ends.
		PlayClip(Clip, Clip ? Clip->GetPlayLength() / Actions.Tuning.Knockdown : 1.f, 0.05f, 0.2f);
		return;
	}
	UAnimSequenceBase* Clip = FMath::Abs(Front) >= FMath::Abs(Side) ? (Front >= 0.f ? HitReactFront : HitReactBack) : (Side >= 0.f ? HitReactRight : HitReactLeft);
	PlayClip(Clip, 1.f, 0.05f, 0.2f);
}

UAnimMontage* APlayerCharacter::PlayClip(UAnimSequenceBase* Clip, float Rate, float BlendIn, float BlendOut, int32 Loops, float StartAt)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim || !Clip)
	{
		return nullptr;
	}
	GuardClipShown = nullptr;
	ClipMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, MontageSlot, BlendIn, BlendOut, Rate, Loops, -1.f, StartAt);
	return ClipMontage;
}

void APlayerCharacter::StopClip(float BlendOut)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && ClipMontage && Anim->Montage_IsActive(ClipMontage))
	{
		Anim->Montage_Stop(BlendOut, ClipMontage);
	}
	ClipMontage = nullptr;
}

FVector APlayerCharacter::InputDirection() const
{
	if (Move.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	return (FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * Move.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * Move.X).GetSafeNormal();
}

void APlayerCharacter::TickCamera()
{
	// Camera work runs on real time, so hit-stop doesn't freeze the camera.
	const float RealDt = FApp::GetDeltaTime();
	AController* C = GetController();
	if (C && LockOn->IsLocked())
	{
		const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, 80.f);
		FRotator Want = (LockOn->GetAimPoint() - Eye).Rotation();
		Want.Pitch = FMath::Clamp(Want.Pitch - 8.f, -40.f, 20.f);
		C->SetControlRotation(FMath::RInterpTo(C->GetControlRotation(), Want, RealDt, 8.f));
	}

	if (KickTime > 0.f)
	{
		KickTime = FMath::Max(0.f, KickTime - RealDt);
		const float Strength = KickAmplitude * (KickTime / FMath::Max(KickDuration, 0.01f));
		Arm->SocketOffset = BaseSocketOffset + FVector(0.f, FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * Strength;
	}
	else
	{
		Arm->SocketOffset = BaseSocketOffset;
	}
}

void APlayerCharacter::CameraKick(float Amplitude, float Duration)
{
	KickAmplitude = Amplitude;
	KickDuration = Duration;
	KickTime = Duration;
}

void APlayerCharacter::SetEvent(const FString& Text, float Seconds)
{
	LastEvent = Text;
	LastEventTime = Seconds;
}

void APlayerCharacter::OnHitResolved(const FHitOutcome& Outcome, AActor* Attacker)
{
	if (Outcome.Result == EHitResult::Dodged)
	{
		SetEvent(TEXT("DODGED (i-frames)"), 1.2f);
		return;
	}
	// How long before the hit the guard was pressed, for the event line (perfect: 0.04-0.18 s).
	const double PressTime = Combatant->State.GuardPressTime;
	const double SincePress = PressTime >= 0.0 ? GetWorld()->GetTimeSeconds() - PressTime : -1.0;
	if (Outcome.Result == EHitResult::Hit || Outcome.Result == EHitResult::Blocked || Outcome.Result == EHitResult::PerfectGuard)
	{
		Combatant->State.GuardPressTime = -1.0; // one press answers one hit
	}
	if (Outcome.Result == EHitResult::PerfectGuard)
	{
		LastAttacker = Attacker;
		Execute(Actions.OnPerfectGuard(Outcome.BlockedDamage, Outcome.bKnockdown));
		SyncCombatant();
		SetEvent(FString::Printf(TEXT("PERFECT GUARD  pressed %.2f s before"), SincePress), 1.2f);
		return;
	}
	if (Outcome.Result == EHitResult::Blocked)
	{
		LastAttacker = Attacker;
		// Chip damage can kill; otherwise the block costs stamina, then block-stun or a guard break.
		Execute(Outcome.bKilled ? Actions.OnDeath() : Actions.OnBlock(Outcome.BlockedDamage, Outcome.bKnockdown));
		SyncCombatant();
		if (!Outcome.bKilled && Actions.GetState() != EPlayerState::GuardBroken)
		{
			SetEvent(SincePress >= 0.0
				? FString::Printf(TEXT("BLOCKED  -%.0f chip  (pressed %.2f s before: early)"), Outcome.DamageDealt, SincePress)
				: FString::Printf(TEXT("BLOCKED  -%.0f chip"), Outcome.DamageDealt), 1.2f);
		}
		return;
	}
	if (Outcome.Result != EHitResult::Hit)
	{
		return;
	}
	LastAttacker = Attacker;
	CameraKick(14.f, 0.25f);
	Execute(Outcome.bKilled ? Actions.OnDeath() : Actions.OnHit(Outcome.bKnockdown));
	SyncCombatant();
	if (!Outcome.bKilled)
	{
		SetEvent(FString::Printf(TEXT("HIT  -%.0f%s"), Outcome.DamageDealt, Outcome.bKnockdown ? TEXT("  knocked down") : TEXT("")), 1.5f);
	}
}

void APlayerCharacter::OnHitLanded(const FHitOutcome& Outcome, AActor* Victim)
{
	// Damage dealt reclaims soul fragments (spec section 8).
	Combatant->State.Health.OnDamageDealtToBoss(Outcome.DamageDealt);
	// "Tick, tick, thud": lights don't kick the camera; the heavy does, the charged heavy and the slam harder.
	if (const UAttackDefinition* Landed = Attacks->GetCurrent(); Landed && Landed->bHeavy)
	{
		const bool bBig = Landed == ChargedHeavyAttack || Landed == DodgeHeavyAttack;
		CameraKick(bBig ? 9.f : 6.f, bBig ? 0.15f : 0.12f);
	}
	SetEvent(FString::Printf(TEXT("you hit  %.0f%s"), Outcome.DamageDealt, Outcome.Poise == EPoiseOutcome::Stagger ? TEXT("  STAGGER") : TEXT("")), 1.2f);
}

void APlayerCharacter::ResetPlayer()
{
	Attacks->StopAttack(0.1f);
	EndDodgePush();
	EndEvadeVisuals(0.1f);
	SetSprinting(false);
	if (PushId != 0)
	{
		GetCharacterMovement()->RemoveRootMotionSourceByID(PushId); // it steers toward a spot from before the teleport
		PushId = 0;
	}
	if (ChargeFX)
	{
		ChargeFX->Deactivate();
		ChargeFX = nullptr;
	}
	GuardClipShown = nullptr;
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->StopAllMontages(0.1f);
	}
	ClipMontage = nullptr;
	Combatant->ResetCombatant();
	Actions.Reset();
	ApplyGuardTuning();
	SyncCombatant();
	LockOn->Unlock();
	TeleportTo(StartLocation, StartRotation);
	GetCharacterMovement()->StopMovementImmediately();
	if (AController* C = GetController())
	{
		C->SetControlRotation(StartRotation);
	}
	SetEvent(TEXT("reset"), 1.f);
}

void APlayerCharacter::AddFragmentForTesting()
{
	Combatant->State.Health.AddFragment();
	SetEvent(FString::Printf(TEXT("soul fragment taken (%d)"), Combatant->State.Health.Fragments), 1.5f);
}

FString APlayerCharacter::DebugLine() const
{
	static const TCHAR* States[] = { TEXT("free"), TEXT("attacking"), TEXT("dodging"), TEXT("healing"), TEXT("hit"), TEXT("knocked down"),
		TEXT("guarding"), TEXT("block-stun"), TEXT("guard broken"), TEXT("dead") };
	const FCombatantState& S = Combatant->State;
	FString Line = FString::Printf(TEXT("YOU  HP %.0f/%.0f  stamina %.0f%s  state %s  heals %d  fragments %d%s%s%s"),
		S.Health.Current, S.Health.Max(), Actions.Stamina.Current, Actions.Stamina.bExhausted ? TEXT(" EXHAUSTED") : TEXT(""),
		States[static_cast<int32>(Actions.GetState())], Actions.GetHealCharges(), S.Health.Fragments,
		Actions.IsInvulnerable() ? TEXT("  [I-FRAMES]") : Actions.IsGuardActive() ? TEXT("  [GUARD]") : TEXT(""), LockOn->IsLocked() ? TEXT("  [LOCKED]") : TEXT(""),
		Actions.IsSprinting() ? TEXT("  [SPRINT]") : TEXT(""));
	if (const UAttackDefinition* Current = Attacks->GetCurrent())
	{
		static const TCHAR* Phases[] = { TEXT("windup"), TEXT("hold"), TEXT("ACTIVE"), TEXT("recovery"), TEXT("done") };
		const FAttackSample Sample = Attacks->GetSample();
		Line += FString::Printf(TEXT("  %s %s t=%.2f%s"), *Current->GetName(), Phases[static_cast<int32>(Sample.Phase)], Attacks->GetElapsed(),
			Sample.bChainOpen ? TEXT(" [chain open]") : TEXT(""));
	}
	return Line;
}
