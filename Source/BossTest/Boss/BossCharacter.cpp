#include "Boss/BossCharacter.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "BossTestLog.h"
#include "Combat/AttackComponent.h"
#include "Combat/AttackDefinition.h"
#include "Combat/CombatantComponent.h"
#include "Combat/Tracking.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Player/PlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float BossCapsuleRadius = 75.f;
	constexpr float BossCapsuleHalfHeight = 150.f;
	constexpr float FacingTolerance = 20.f; // degrees: close enough to stop turning in place

	const TCHAR* DistanceBand(float EdgeCm)
	{
		return EdgeCm <= 250.f ? TEXT("close") : EdgeCm <= 600.f ? TEXT("medium") : EdgeCm <= 1000.f ? TEXT("far") : TEXT("extreme");
	}

	const TCHAR* AngleBand(float Degrees)
	{
		return Degrees <= 45.f ? TEXT("front") : Degrees <= 90.f ? TEXT("side") : Degrees <= 135.f ? TEXT("rear quarter") : TEXT("behind");
	}

	const TCHAR* ArmorName(EArmor Armor)
	{
		return Armor == EArmor::Full ? TEXT("full") : Armor == EArmor::Windup ? TEXT("windup") : TEXT("none");
	}
}

ABossCharacter::ABossCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Meshes/Sevarog.Sevarog"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimClass(TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Sevarog_AnimBlueprint"));

	GetCapsuleComponent()->InitCapsuleSize(BossCapsuleRadius, BossCapsuleHalfHeight);
	if (MeshAsset.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(MeshAsset.Object);
	}
	if (AnimClass.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimClass.Class);
	}
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -BossCapsuleHalfHeight));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // Paragon meshes face +Y
	// Keep evaluating the pose off screen and headless, or the hammer's sockets go stale and his sweeps miss.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = false; // he faces where his state wants
	Move->bUseControllerDesiredRotation = false;
	Move->GroundFriction = 3.f;
	Move->bTickBeforeOwner = false;          // ordered after the character in BeginPlay

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();

	Combatant = CreateDefaultSubobject<UCombatantComponent>(TEXT("Combatant")); // the arena saves data under this name
	Combatant->MaxHealth = 5500.f;
	Combatant->StaggerDuration = 1.8f; // 2026-11-23 council: so the charged heavy has a window (was 1.25 s)
	Attacks = CreateDefaultSubobject<UAttackComponent>(TEXT("Attacks"));
	Attacks->MontageSlot = MontageSlot;
}

void ABossCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyScale(); // so his size shows in the editor viewport too
}

void ABossCharacter::ApplyScale()
{
	SetActorScale3D(FVector(BossScale));
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = GlideSpeed;
	Move->MaxAcceleration = GlideAcceleration;
	Move->BrakingDecelerationWalking = GlideAcceleration;
}

float ABossCharacter::SwingRange() const
{
	// The attack's own range (data), so Step 6's brain reads every attack the same way.
	return ExecutionersSwing && ExecutionersSwing->Range > 0.f ? ExecutionersSwing->Range : 430.f;
}

void ABossCharacter::BeginPlay()
{
	Super::BeginPlay();
	HomeTransform = GetActorTransform();
	ApplyScale();
	Attacks->MontageSlot = MontageSlot;

	// Tick order, as for the player: combatant (ends hit-stop, runs the stagger clock) -> attacks
	// (advance the swing, track, lunge) -> this character (state and requests) -> movement -> mesh.
	Attacks->AddTickPrerequisiteComponent(Combatant);
	AddTickPrerequisiteComponent(Attacks);
	GetCharacterMovement()->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);

	if (!ExecutionersSwing || !IntroClip || !StaggerClip || !DeathClip || !GlideForward)
	{
		UE_LOG(LogBossTest, Error, TEXT("%s is missing its attack or clips; set them on BP_Sevarog (Docs/tools/setup_boss.py)"), *GetName());
	}

	Combatant->OnHitResolved.AddUObject(this, &ABossCharacter::OnHitResolved);
	States.OnTransition = [this](EBossState From, EBossState To) { OnStateChanged(From, To); };
	// Every request, its outcome and every timeout go to the log (spec section 17); timeouts as warnings.
	States.OnRecord = [](const FBossRequestRecord& R, bool bTimeout)
	{
		const FString Line = FString::Printf(TEXT("Sevarog: %s %s -> %s (%s): %s"), R.bAccepted ? TEXT("[+]") : TEXT("[x]"),
			FBossStateMachine::StateName(R.From), FBossStateMachine::StateName(R.To), FBossStateMachine::PriorityName(R.Priority), *R.Reason);
		if (bTimeout)
		{
			UE_LOG(LogBossTest, Warning, TEXT("%s"), *Line);
		}
		else
		{
			UE_LOG(LogBossTest, Log, TEXT("%s"), *Line);
		}
	};
	IntroLength = IntroClip ? IntroClip->GetPlayLength() : 0.5f;
	States.Reset(IntroLength + 1.f);
	StartIntro();
}

void ABossCharacter::StartIntro()
{
	// Nobody can damage anyone during the intro (spec section 15).
	Combatant->State.bImmune = true;
	PlayClip(IntroClip, 1.f, 0.2f, 0.4f);
	UE_LOG(LogBossTest, Log, TEXT("Sevarog: intro (%.2f s)"), IntroLength);
}

void ABossCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The state clock steps like the attack clock (dilated, at most 1/30 s per tick), so a slow frame
	// rate stretches both together and an attack never outlives its own timeout.
	States.Tick(ClampAttackStep(DeltaSeconds));

	switch (States.GetState())
	{
	case EBossState::Intro:
		if (States.GetStateTime() >= IntroLength)
		{
			States.Complete(bAIEnabled ? EBossState::Evaluate : EBossState::Paused, TEXT("intro over"));
		}
		break;
	case EBossState::Evaluate:
		Evaluate();
		break;
	case EBossState::Approach:
		TickApproach(DeltaSeconds);
		break;
	case EBossState::Turn:
		TickTurn(DeltaSeconds);
		break;
	case EBossState::Neutral:
		FaceTarget(DeltaSeconds);
		if (States.GetStateTime() >= NeutralBeat)
		{
			States.Complete(EBossState::Evaluate, TEXT("beat over"));
		}
		break;
	case EBossState::Attack:
		TickAttack();
		break;
	case EBossState::Stagger:
		// The poise model owns the stagger's length; the state's timeout is only a backstop.
		if (!Combatant->State.Poise.IsStaggered())
		{
			States.Complete(EBossState::Evaluate, TEXT("stagger over"));
		}
		break;
	default:
		break;
	}

	DrawDebugShapes();
}

void ABossCharacter::Evaluate()
{
	// Decides once, then the chosen state runs until it ends. The placeholder brain until Step 6.
	if (!bAIEnabled)
	{
		States.Request(EBossState::Paused, EBossPriority::Decision, TEXT("AI off"));
		return;
	}
	if (!Player())
	{
		States.Request(EBossState::Neutral, EBossPriority::Decision, TEXT("no player"), NeutralBeat + 1.f);
		return;
	}
	FBossPlaceholderTuning Tuning;
	Tuning.SwingRange = SwingRange();
	switch (ChoosePlaceholderAction(EdgeDistanceToPlayer(), AngleToPlayer(), PlayerDown(), Tuning))
	{
	case EBossChoice::Wait:
		States.Request(EBossState::Neutral, EBossPriority::Decision, TEXT("player down: waiting"), NeutralBeat + 1.f);
		break;
	case EBossChoice::Turn:
		States.Request(EBossState::Turn, EBossPriority::Decision, TEXT("player outside his front and sides"));
		break;
	case EBossChoice::Swing:
		StartSwing(TEXT("in range"));
		break;
	case EBossChoice::Approach:
		States.Request(EBossState::Approach, EBossPriority::Decision, TEXT("closing in"), ApproachTime + 1.f);
		break;
	}
}

void ABossCharacter::StartSwing(const FString& Reason)
{
	if (!ExecutionersSwing)
	{
		States.Request(EBossState::Neutral, EBossPriority::Decision, TEXT("no attack asset"), NeutralBeat + 1.f);
		return;
	}
	// The attack starts in OnStateChanged, once the machine accepts it.
	PendingAttack = ExecutionersSwing;
	const float Timeout = ExecutionersSwing->GetTiming().Total() + 1.f;
	if (!States.Request(EBossState::Attack, EBossPriority::CommittedAttack, TEXT("Executioner's Swing: ") + Reason, Timeout))
	{
		PendingAttack = nullptr;
	}
}

void ABossCharacter::TickApproach(float DeltaSeconds)
{
	const ACharacter* P = Player();
	if (!P || PlayerDown())
	{
		States.Complete(EBossState::Evaluate, TEXT("player gone or down"));
		return;
	}
	if (EdgeDistanceToPlayer() <= SwingRange() && AngleToPlayer() <= 90.f)
	{
		StartSwing(TEXT("reached range"));
		return;
	}
	if (States.GetStateTime() >= ApproachTime)
	{
		States.Complete(EBossState::Evaluate, TEXT("approach ran out"));
		return;
	}
	FaceTarget(DeltaSeconds);
	const FVector Dir = (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	GetCharacterMovement()->MaxWalkSpeed = GlideSpeed;
	AddMovementInput(Dir, 1.f);
	PlayGlideClip(Dir);
}

void ABossCharacter::TickTurn(float DeltaSeconds)
{
	FaceTarget(DeltaSeconds);
	if (!Player() || AngleToPlayer() <= FacingTolerance)
	{
		States.Complete(EBossState::Evaluate, TEXT("facing you"));
	}
}

void ABossCharacter::TickAttack()
{
	if (!Attacks->IsAttacking())
	{
		States.Complete(EBossState::Neutral, TEXT("swing over"), NeutralBeat + 1.f);
		return;
	}
	// Recovery is the safe point: a stagger deferred by his armour starts here.
	if (!bRecoveringSent && Attacks->GetSample().Phase >= EAttackPhase::Recovery)
	{
		bRecoveringSent = true;
		States.SetAttackRecovering(true);
	}
}

void ABossCharacter::FaceTarget(float DeltaSeconds)
{
	const ACharacter* P = Player();
	if (!P)
	{
		return;
	}
	const FVector To = (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	if (!To.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, TrackingYawStep(GetActorRotation().Yaw, To.Rotation().Yaw, TurnRate, DeltaSeconds), 0.f));
	}
}

void ABossCharacter::PlayGlideClip(const FVector& Dir)
{
	// He hovers, so his jog clips are glide poses: pick the one matching his movement relative to his facing.
	const float Forward = FVector::DotProduct(Dir, GetActorForwardVector());
	const float Side = FVector::DotProduct(Dir, GetActorRightVector());
	UAnimSequenceBase* Wanted = FMath::Abs(Forward) >= FMath::Abs(Side) ? (Forward >= 0.f ? GlideForward : GlideBack) : (Side >= 0.f ? GlideRight : GlideLeft);
	const UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	const bool bPlaying = Anim && ClipMontage && Anim->Montage_IsActive(ClipMontage);
	if (Wanted && (GlideClip != Wanted || !bPlaying))
	{
		PlayClip(Wanted, 1.f, 0.3f, 0.3f, 1000);
		GlideClip = Wanted;
	}
}

void ABossCharacter::StopGlide()
{
	if (!GlideClip)
	{
		return;
	}
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && ClipMontage && Anim->Montage_IsActive(ClipMontage))
	{
		Anim->Montage_Stop(0.3f, ClipMontage);
	}
	GlideClip = nullptr;
}

void ABossCharacter::OnStateChanged(EBossState From, EBossState To)
{
	FPoiseModel& Poise = Combatant->State.Poise;

	// Leaving a state.
	switch (From)
	{
	case EBossState::Intro:
		Combatant->State.bImmune = false;
		break;
	case EBossState::Approach:
		StopGlide();
		break;
	case EBossState::Attack:
		if (Attacks->IsAttacking())
		{
			Attacks->StopAttack(0.15f); // interrupted: a stagger, death or a timeout (clears his armour)
		}
		bRecoveringSent = false;
		if (To == EBossState::Dead)
		{
			Poise.CancelDeferredStagger();
		}
		else if (To != EBossState::Stagger && Poise.HasDeferredStagger())
		{
			// The machine plays a deferred stagger at the attack's end, so this shouldn't happen. If it
			// does, don't leave poise blocked.
			UE_LOG(LogBossTest, Warning, TEXT("Sevarog: a deferred stagger outlived its attack; cancelled"));
			Poise.CancelDeferredStagger();
		}
		break;
	default:
		break;
	}

	// Entering a state.
	switch (To)
	{
	case EBossState::Attack:
		StopGlide();
		GetCharacterMovement()->StopMovementImmediately(); // no glide momentum carried into the windup
		if (PendingAttack)
		{
			Attacks->StartAttack(PendingAttack, Player());
		}
		PendingAttack = nullptr;
		bRecoveringSent = false;
		break;
	case EBossState::Stagger:
	{
		StopGlide();
		if (!Poise.IsStaggered())
		{
			Poise.BeginStagger(); // a deferred or forced stagger; a direct one began in the poise model already
		}
		const float Length = FMath::Max(Poise.StaggerDuration, 0.1f);
		PlayClip(StaggerClip, StaggerClip ? StaggerClip->GetPlayLength() / Length : 1.f, 0.1f, 0.25f);
		break;
	}
	case EBossState::Dead:
		Attacks->StopAttack(0.1f);
		StopGlide();
		if (PlayClip(DeathClip, 1.f, 0.1f, 0.f))
		{
			// Hold the last pose instead of blending back to idle.
			if (FAnimMontageInstance* Instance = GetMesh()->GetAnimInstance()->GetActiveInstanceForMontage(ClipMontage))
			{
				Instance->bEnableAutoBlendOut = false;
			}
		}
		break;
	case EBossState::Paused:
		StopGlide();
		break;
	default:
		break;
	}
}

void ABossCharacter::OnHitResolved(const FHitOutcome& Outcome, AActor* Attacker)
{
	if (Outcome.Result != EHitResult::Hit)
	{
		return;
	}
	if (Outcome.bKilled)
	{
		Attacks->StopAttack(0.05f); // his weapon stops this frame, before anything else ticks
		States.Request(EBossState::Dead, EBossPriority::Death, TEXT("killed"));
		return;
	}
	// The stagger's timeout follows its length (data), plus the usual 1 s.
	const float StaggerTimeout = Combatant->State.Poise.StaggerDuration + 1.f;
	if (Outcome.Poise == EPoiseOutcome::Stagger)
	{
		States.Request(EBossState::Stagger, EBossPriority::Stagger, TEXT("poise broken"), StaggerTimeout);
	}
	else if (Outcome.Poise == EPoiseOutcome::DeferredStagger)
	{
		States.RequestAtSafePoint(EBossState::Stagger, EBossPriority::Stagger, TEXT("poise broken under armour"), StaggerTimeout);
	}
}

UAnimMontage* ABossCharacter::PlayClip(UAnimSequenceBase* Clip, float Rate, float BlendIn, float BlendOut, int32 Loops)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim || !Clip)
	{
		return nullptr;
	}
	GlideClip = nullptr;
	ClipMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, MontageSlot, BlendIn, BlendOut, Rate, Loops);
	return ClipMontage;
}

ACharacter* ABossCharacter::Player() const
{
	return UGameplayStatics::GetPlayerCharacter(this, 0);
}

bool ABossCharacter::PlayerDown() const
{
	// He doesn't commit to an attack while you're knocked down or dead (spec section 10).
	if (const APlayerCharacter* P = Cast<APlayerCharacter>(Player()))
	{
		const EPlayerState S = P->GetActions().GetState();
		return S == EPlayerState::Knockdown || S == EPlayerState::Dead;
	}
	const UCombatantComponent* Other = Player() ? Player()->FindComponentByClass<UCombatantComponent>() : nullptr;
	return Other && Other->IsDead();
}

float ABossCharacter::EdgeDistanceToPlayer() const
{
	const ACharacter* P = Player();
	if (!P)
	{
		return TNumericLimits<float>::Max();
	}
	return FVector::Dist2D(GetActorLocation(), P->GetActorLocation())
		- GetCapsuleComponent()->GetScaledCapsuleRadius() - P->GetCapsuleComponent()->GetScaledCapsuleRadius();
}

float ABossCharacter::AngleToPlayer() const
{
	const ACharacter* P = Player();
	if (!P)
	{
		return 0.f;
	}
	const FVector To = (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	return FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, To.Rotation().Yaw));
}

void ABossCharacter::DrawDebugShapes() const
{
	if (!UAttackComponent::ShouldDrawDebug() || !GetWorld())
	{
		return;
	}
	const FVector Chest = GetActorLocation() + FVector(0.f, 0.f, 60.f);
	if (States.GetState() == EBossState::Attack && Attacks->IsAttacking())
	{
		const UAttackDefinition* Def = Attacks->GetCurrent();
		const bool bTracking = Def && Def->TrackingRate > 0.f && IsTrackingAt(Def->GetTiming(), Def->TrackingStopsBeforeActive, Attacks->GetElapsed());
		// Where he faces: blue while tracking, grey once committed.
		DrawDebugLine(GetWorld(), Chest, Chest + GetActorForwardVector() * 400.f, bTracking ? FColor::Blue : FColor(128, 128, 128), false, -1.f, 0, 3.f);
		if (bTracking && Player())
		{
			DrawDebugLine(GetWorld(), Chest, Player()->GetActorLocation(), FColor::Cyan, false, -1.f, 0, 1.f);
		}
	}
	if (Attacks->IsLunging())
	{
		DrawDebugSphere(GetWorld(), Attacks->GetLungeTarget(), 30.f, 10, FColor::Orange, false, -1.f, 0, 2.f);
	}
}

TArray<FString> ABossCharacter::DebugLines() const
{
	TArray<FString> Lines;
	const FCombatantState& C = Combatant->State;
	const FPoiseModel& Poise = C.Poise;
	Lines.Add(FString::Printf(TEXT("SEVAROG  HP %.0f/%.0f  poise %.0f/%.0f%s%s  armour %s  state %s %.1f/%.1f s  AI %s"),
		C.Health.Current, C.Health.Max(), Poise.Current(), Poise.Threshold,
		Poise.IsImmune() ? TEXT(" (immune)") : TEXT(""), Poise.HasDeferredStagger() ? TEXT(" (stagger deferred)") : TEXT(""),
		ArmorName(C.Armor), FBossStateMachine::StateName(States.GetState()), States.GetStateTime(), States.GetTimeout(),
		bAIEnabled ? TEXT("on") : TEXT("off")));

	if (const UAttackDefinition* Def = Attacks->GetCurrent())
	{
		static const TCHAR* Phases[] = { TEXT("windup"), TEXT("hold"), TEXT("ACTIVE"), TEXT("recovery"), TEXT("done") };
		const FAttackSample Sample = Attacks->GetSample();
		const bool bTracking = Def->TrackingRate > 0.f && IsTrackingAt(Def->GetTiming(), Def->TrackingStopsBeforeActive, Attacks->GetElapsed());
		Lines.Add(FString::Printf(TEXT("  attack %s %s t=%.2f%s%s"), *Def->GetName(), Phases[static_cast<int32>(Sample.Phase)], Attacks->GetElapsed(),
			bTracking ? TEXT("  [tracking]") : TEXT(""), Attacks->IsLunging() ? TEXT("  [lunging]") : TEXT("")));
	}
	if (Player())
	{
		const float Edge = EdgeDistanceToPlayer();
		const float Angle = AngleToPlayer();
		Lines.Add(FString::Printf(TEXT("  you: %.1f m (%s), %.0f deg (%s)%s"), Edge / 100.f, DistanceBand(Edge), Angle, AngleBand(Angle),
			PlayerDown() ? TEXT("  [down]") : TEXT("")));
	}
	const TArray<FBossRequestRecord>& History = States.GetHistory();
	for (int32 i = FMath::Max(0, History.Num() - 4); i < History.Num(); ++i)
	{
		const FBossRequestRecord& R = History[i];
		Lines.Add(FString::Printf(TEXT("  %s %s -> %s (%s): %s"), R.bAccepted ? TEXT("[+]") : TEXT("[x]"), FBossStateMachine::StateName(R.From),
			FBossStateMachine::StateName(R.To), FBossStateMachine::PriorityName(R.Priority), *R.Reason));
	}
	return Lines;
}

void ABossCharacter::ToggleAI()
{
	bAIEnabled = !bAIEnabled;
	const EBossState S = States.GetState();
	if (!bAIEnabled && (S == EBossState::Evaluate || S == EBossState::Approach || S == EBossState::Turn || S == EBossState::Neutral))
	{
		States.Request(EBossState::Paused, EBossPriority::Decision, TEXT("AI off"));
	}
	else if (bAIEnabled && S == EBossState::Paused)
	{
		States.Request(EBossState::Evaluate, EBossPriority::Decision, TEXT("AI on"));
	}
}

void ABossCharacter::ForceSwing()
{
	if (States.GetState() == EBossState::Neutral)
	{
		States.Complete(EBossState::Evaluate, TEXT("forced swing"));
	}
	StartSwing(TEXT("forced"));
}

void ABossCharacter::ForceStagger()
{
	if (States.GetState() == EBossState::Dead || States.GetState() == EBossState::Stagger)
	{
		return;
	}
	// Entering the state starts the stagger clock (and the immunity after it); a rejected request
	// (during the intro, say) leaves his poise untouched.
	States.Request(EBossState::Stagger, EBossPriority::Stagger, TEXT("forced stagger"), Combatant->State.Poise.StaggerDuration + 1.f);
}

void ABossCharacter::ResetBoss()
{
	Attacks->StopAttack(0.1f);
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->StopAllMontages(0.1f);
	}
	ClipMontage = nullptr;
	GlideClip = nullptr;
	PendingAttack = nullptr;
	bRecoveringSent = false;
	Combatant->ResetCombatant();
	TeleportTo(HomeTransform.GetLocation(), HomeTransform.Rotator());
	GetCharacterMovement()->StopMovementImmediately();
	States.Reset(IntroLength + 1.f);
	StartIntro();
}
