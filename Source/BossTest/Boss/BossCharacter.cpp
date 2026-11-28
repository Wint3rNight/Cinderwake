#include "Boss/BossCharacter.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "BossTestLog.h"
#include "Combat/AttackComponent.h"
#include "Combat/AttackDefinition.h"
#include "Combat/CombatantComponent.h"
#include "Combat/DeliveryComponent.h"
#include "Combat/Lunge.h"
#include "Boss/BossBrain.h"
#include "Boss/BossMovement.h"
#include "Arena/ArenaBounds.h"
#include "Combat/Tracking.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
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
	Combatant->PoiseThreshold = 1100.f; // 2026-11-24 council: a stagger every 60-90 s at 20 DPS (650 gave ~40 s); a starting value
	Attacks = CreateDefaultSubobject<UAttackComponent>(TEXT("Attacks"));
	Attacks->MontageSlot = MontageSlot;
	Delivery = CreateDefaultSubobject<UDeliveryComponent>(TEXT("Delivery"));
	// The finishing blow's in-range cue: a soft light just in front of his slumped chest, placed every tick (a light at
	// the bone would sit inside his body and light nothing you can see).
	FinisherGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("FinisherGlow"));
	FinisherGlow->SetupAttachment(GetRootComponent());
	FinisherGlow->SetUsingAbsoluteLocation(true);
	FinisherGlow->SetIntensityUnits(ELightUnits::Candelas);
	FinisherGlow->SetIntensity(0.f);
	FinisherGlow->SetAttenuationRadius(140.f);
	FinisherGlow->SetSourceRadius(25.f);
	FinisherGlow->SetCastShadows(false);
	FinisherGlow->SetVisibility(false);
	IgnoreCamera();
}

void ABossCharacter::IgnoreCamera()
{
	// After the Step 6 play: the camera arm's probe treated his 3.7 m body as a wall and shoved the camera into him when
	// a dodge put him between it and the player. The camera passes through him instead, as in souls games. (The death
	// ragdoll saves and restores his collision responses, so this survives it.)
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
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
	// Step 6b: the arena's centre and radius come from the level's ArenaBounds; without one, his spawn point and 13 m.
	ArenaCentre = HomeTransform.GetLocation();
	float WallRadius = ArenaRadius + 100.f;
	if (const AArenaBounds* Arena = AArenaBounds::Find(GetWorld()))
	{
		ArenaCentre = Arena->GetCentre();
		ArenaRadius = Arena->AIRadius;
		WallRadius = Arena->WallRadius;
	}
	IgnoreCamera(); // again at play, in case the Blueprint carries an older collision setting
	ApplyScale();
	Attacks->MontageSlot = MontageSlot;
	Attacks->SetArena(ArenaCentre, WallRadius - GetCapsuleComponent()->GetScaledCapsuleRadius()); // the Advance's dash stops short of the wall

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

	// Step 5: his actions. Sequences chain inside the attack component; a pending stagger stops the chain.
	Rng.Initialize(Seed != 0 ? Seed : FMath::Rand());
	UE_LOG(LogBossTest, Log, TEXT("Sevarog: seed %d"), Rng.GetInitialSeed());
	Delivery->AddTickPrerequisiteComponent(Attacks); // a ring sees this frame's sweep results before it checks
	InitDirector();
	Attacks->CanContinue = [this]() { return CanContinuePlan(); };
	Attacks->OnStepStarted.AddUObject(this, &ABossCharacter::OnStepStarted);
	Attacks->OnSweepResolved.AddUObject(this, &ABossCharacter::OnOwnSweep);
	if (Actions.Num() == 0)
	{
		UE_LOG(LogBossTest, Warning, TEXT("%s has no actions: Executioner's Swing only (Docs/tools/setup_boss.py fills them)"), *GetName());
	}
	for (const FBossAction& A : Actions)
	{
		for (const FBossActionVariant& V : A.Variants)
		{
			for (const TObjectPtr<UAttackDefinition>& Step : V.Steps)
			{
				if (!Step || !Step->Clip)
				{
					UE_LOG(LogBossTest, Error, TEXT("%s: action %s has a step with no attack or no clip"), *GetName(), *A.Name.ToString());
				}
			}
		}
	}
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

	// His attack component ticks after the player's, so a light landing on a chain frame resolves the same way every
	// time (QA, Step 6). The player can spawn after him, so this is set up once he's there.
	if (!bTickOrdered)
	{
		if (const ACharacter* P = Player())
		{
			if (UAttackComponent* PlayerAttacks = P->FindComponentByClass<UAttackComponent>())
			{
				Attacks->AddTickPrerequisiteComponent(PlayerAttacks);
				bTickOrdered = true;
			}
		}
	}

	// The director watches every tick; a band change re-decides a movement (not an attack or the pause).
	const EBossState Now = States.GetState();
	const bool bMoving = Now == EBossState::Approach || Now == EBossState::Circle || Now == EBossState::Reposition || Now == EBossState::Turn;
	if (const APlayerCharacter* Seen = Cast<APlayerCharacter>(Player()))
	{
		const double NowS = GetWorld()->GetTimeSeconds();
		const EPlayerState PS = Seen->GetActions().GetState();
		const bool bAttacking = PS == EPlayerState::Attacking;
		const bool bHealing = PS == EPlayerState::Healing;
		if (bAttacking)
		{
			LastPlayerAttackTime = NowS; // the stalk
		}
		if (bAttacking && !bPlayerWasAttacking)
		{
			LastPlayerAttackStart = NowS; // the aura's exits
		}
		if (bHealing && !bPlayerWasHealing)
		{
			LastPlayerHealStart = NowS;
		}
		bPlayerWasAttacking = bAttacking;
		bPlayerWasHealing = bHealing;
		if (!bPlayerHitBound)
		{
			if (UCombatantComponent* PC = Seen->FindComponentByClass<UCombatantComponent>())
			{
				PC->OnHitResolved.AddUObject(this, &ABossCharacter::OnPlayerHit); // his hits on you earn the aura
				bPlayerHitBound = true;
			}
		}
	}
	if (Player() && Actions.Num() > 0 && Now != EBossState::Intro && Now != EBossState::Paused && Now != EBossState::Dead)
	{
		const bool bBandChanged = Director.Observe(Perception(), bMoving || Now == EBossState::Neutral || Now == EBossState::Posture, ClampAttackStep(DeltaSeconds));
		if (bBandChanged && bMoving && Director.ShouldRedecide(CurrentMove, States.GetStateTime()))
		{
			// A circle re-picked after a band change keeps its way round and only its remaining time.
			bKeepCircle = Now == EBossState::Circle;
			CircleLeft = MoveLength - States.GetStateTime();
			States.Complete(EBossState::Evaluate, TEXT("band changed"));
		}
	}

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
		if (States.GetStateTime() >= (Actions.Num() > 0 ? PauseLength : NeutralBeat))
		{
			States.Complete(EBossState::Evaluate, TEXT("beat over"));
		}
		break;
	case EBossState::Circle:
	case EBossState::Reposition:
		TickMovement(DeltaSeconds);
		break;
	case EBossState::Attack:
		TickAttack();
		break;
	case EBossState::CastBroken:
		if (States.GetStateTime() >= CastBrokenTime)
		{
			States.Complete(EBossState::Evaluate, TEXT("recoil over"));
		}
		break;
	case EBossState::Stagger:
		TickStagger(DeltaSeconds);
		break;
	case EBossState::Posture:
		TickPosture(DeltaSeconds);
		break;
	default:
		break;
	}

	// The curse's sign: your stolen soul swirls around him while you're missing fragments.
	const UCombatantComponent* PlayerCombat = Player() ? Player()->FindComponentByClass<UCombatantComponent>() : nullptr;
	const bool bHoldsSoul = PlayerCombat && PlayerCombat->State.Health.Fragments > 0 && !Combatant->IsDead();
	if (bHoldsSoul && StolenSoulEffect && !StolenSoulFX)
	{
		StolenSoulFX = UGameplayStatics::SpawnEmitterAttached(StolenSoulEffect, GetMesh(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, false);
	}
	else if (!bHoldsSoul && StolenSoulFX)
	{
		StolenSoulFX->Deactivate(); // what's spawned fades on its own
		StolenSoulFX = nullptr;
	}

	// The in-range cue: on while a light or heavy pressed now would finish him, faded so it never pops.
	const APlayerCharacter* P = Cast<APlayerCharacter>(Player());
	const bool bCue = P && CanBeFinishedBy(P) && P->GetActions().CouldRiposte();
	GlowAlpha = FMath::FInterpConstantTo(GlowAlpha, bCue ? 1.f : 0.f, DeltaSeconds, 8.f);
	FinisherGlow->SetVisibility(GlowAlpha > 0.f);
	if (GlowAlpha > 0.f)
	{
		FinisherGlow->SetLightColor(FinisherGlowColor);
		FinisherGlow->SetIntensity(FinisherGlowIntensity * GlowAlpha);
		FinisherGlow->SetWorldLocation(GetMesh()->GetBoneLocation(TEXT("spine_03")) + GetActorForwardVector() * 45.f);
	}

	DrawDebugShapes();
}

void ABossCharacter::TickStagger(float DeltaSeconds)
{
	// The poise model owns the stagger's length (and a finisher lengthens it); the state's timeout is only a backstop.
	const FPoiseModel& Poise = Combatant->State.Poise;
	if (!Poise.IsStaggered())
	{
		// Through the commitment pause, as after an attack: he never acts on the frame he stands (the council's run).
		PauseLength = Actions.Num() > 0 ? CommitPause : NeutralBeat;
		States.Complete(EBossState::Neutral, TEXT("stagger over"), PauseLength + 1.f);
		return;
	}
	const float T = Poise.StaggerElapsed();
	// While he slumps he turns toward the player, up to the limit from where the stagger caught him (QA: a sideways
	// dodge leaves you 55-60 deg off, so a finisher stays possible).
	const ACharacter* P = Player();
	if (P && !bFinisherTaken && T < StaggerTurnTime)
	{
		const float Want = (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation().Yaw;
		const float Goal = StaggerYaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(StaggerYaw, Want), -StaggerTurnLimit, StaggerTurnLimit);
		const float Max = StaggerTurnLimit / FMath::Max(StaggerTurnTime, 0.01f) * DeltaSeconds;
		const float Yaw = GetActorRotation().Yaw;
		SetActorRotation(FRotator(0.f, Yaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(Yaw, Goal), -Max, Max), 0.f));
	}
	if (!HasStunClips())
	{
		return;
	}
	// The slump (Stun_Start to its end pose), the loop, then the stand-up timed to end with the stagger.
	if (StunPhase == 0 && T >= StunStartTo / FMath::Max(StunStartRate, 0.01f))
	{
		PlayClip(StunLoopClip, 1.f, 0.15f, 0.2f, 1000);
		StunPhase = 1;
	}
	if (StunPhase < 2 && Poise.StaggerRemaining() <= StunEndClip->GetPlayLength() / FMath::Max(StunEndRate, 0.01f))
	{
		PlayClipEased(StunEndClip, StunEndRate, 0.2f, 0.25f);
		StunPhase = 2;
	}
}

void ABossCharacter::OnPlayerHit(const FHitOutcome& Outcome, AActor* Attacker)
{
	if (Attacker != this || Outcome.Result != EHitResult::Hit)
	{
		return;
	}
	const double NowS = GetWorld()->GetTimeSeconds();
	HitsOnPlayerTimes.RemoveAll([this, NowS](double T) { return NowS - T > PostureHitWindow; });
	HitsOnPlayerTimes.Add(NowS);
	if (Outcome.bKnockdown)
	{
		LastKnockdownOnPlayer = NowS;
	}
}

bool ABossCharacter::TryPosture()
{
	if (!PostureClip || !Player())
	{
		return false;
	}
	const double NowS = GetWorld()->GetTimeSeconds();
	HitsOnPlayerTimes.RemoveAll([this, NowS](double T) { return NowS - T > PostureHitWindow; });
	FPostureInputs In;
	In.RecentHitsOnPlayer = HitsOnPlayerTimes.Num();
	In.bRecentKnockdown = NowS - LastKnockdownOnPlayer <= PostureHitWindow;
	In.SinceLastPosture = static_cast<float>(NowS - LastPostureTime);
	In.Band = Director.GetBand();
	In.Angle = Director.GetAngleBand();
	In.bPursuit = Director.PursuitActive();
	In.SincePlayerAttack = static_cast<float>(NowS - LastPlayerAttackTime);
	In.Roll = Rng.FRand();
	FPostureRules Rules;
	Rules.HitsNeeded = PostureRules.HitsNeeded;
	Rules.Cooldown = PostureRules.Cooldown;
	Rules.QuietFor = PostureRules.QuietFor;
	Rules.Chance = PostureRules.Chance;
	if (!ShouldPosture(In, Rules))
	{
		return false;
	}
	PostureLength = Rng.FRandRange(PostureMin, PostureMax);
	PostureSign = Rng.FRand() < 0.5f ? -1 : 1;
	return States.Complete(EBossState::Posture, FString::Printf(TEXT("aura: %d hits in %.0f s%s"), In.RecentHitsOnPlayer, PostureHitWindow,
		In.bRecentKnockdown ? TEXT(", a knockdown") : TEXT("")), PostureLength + 1.3f);
}

void ABossCharacter::ForcePosture()
{
	PostureLength = Rng.FRandRange(PostureMin, PostureMax);
	PostureSign = Rng.FRand() < 0.5f ? -1 : 1;
	States.Request(EBossState::Posture, EBossPriority::Scripted, TEXT("forced aura"), PostureLength + 1.3f);
}

void ABossCharacter::TickPosture(float DeltaSeconds)
{
	const ACharacter* P = Player();
	const double NowS = GetWorld()->GetTimeSeconds();
	const EBossAngleBand Angle = Director.GetAngleBand();
	const bool bNearEnough = Director.GetBand() == EBossBand::Close || Director.GetBand() == EBossBand::Medium;
	const TCHAR* End = nullptr;
	if (!P || PlayerDown())
	{
		End = TEXT("no player");
	}
	else if (Director.GetBand() == EBossBand::Close)
	{
		End = TEXT("you came close");
	}
	else if (LastPlayerAttackStart > PostureStartTime && bNearEnough && NowS - LastPlayerAttackStart >= 0.25)
	{
		End = TEXT("you attacked"); // QA: a charged heavy from just outside Close mustn't land for free; seen 0.25 s later
	}
	else if (LastPlayerHealStart > PostureStartTime && NowS - LastPlayerHealStart >= 0.25)
	{
		End = TEXT("you healed");
	}
	else if (Angle == EBossAngleBand::RearQuarter || Angle == EBossAngleBand::Behind)
	{
		End = TEXT("you got round him");
	}
	else if (Director.PursuitActive())
	{
		End = TEXT("you ran");
	}
	else if (States.GetStateTime() >= PostureLength)
	{
		End = TEXT("done");
	}
	if (!End)
	{
		// A slow circle round you, watching rather than tracking; at the arena's edge he turns back once, a second time ends it.
		const FVector Centre = GetArenaCentre();
		FVector Dir = CircleDirection(GetActorLocation(), P->GetActorLocation(), PostureSign);
		if (LeavesArena(GetActorLocation(), Dir, PostureSpeed, 0.5f, Centre, ArenaRadius))
		{
			if (bPostureFlipped)
			{
				End = TEXT("the edge");
			}
			else
			{
				bPostureFlipped = true;
				PostureSign = -PostureSign;
				Dir = -Dir;
			}
		}
		if (!End)
		{
			GetCharacterMovement()->MaxWalkSpeed = PostureSpeed;
			AddMovementInput(Dir, 1.f); // no glide clip: the aura clip stays on the slot
			const FVector To = (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			if (!To.IsNearlyZero())
			{
				SetActorRotation(FRotator(0.f, TrackingYawStep(GetActorRotation().Yaw, To.Rotation().Yaw, PostureTurnRate, DeltaSeconds), 0.f));
			}
			return;
		}
	}
	// The rouse: the commitment pause itself (he turns at his full rate in it), then a decision.
	PauseLength = PostureRouse;
	States.Complete(EBossState::Neutral, FString::Printf(TEXT("aura ended: %s"), End), PauseLength + 1.f);
}

void ABossCharacter::EndPostureEffects()
{
	for (UParticleSystemComponent* FX : PostureFX)
	{
		if (FX)
		{
			FX->Deactivate();
		}
	}
	PostureFX.Reset();
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && ClipMontage && Anim->Montage_IsActive(ClipMontage))
	{
		FMontageBlendSettings Out(PostureRouse);
		Out.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
		Anim->Montage_StopWithBlendSettings(Out, ClipMontage); // back to his combat idle
	}
	ClipMontage = nullptr;
}

bool ABossCharacter::CanBeFinishedBy(const AActor* Who) const
{
	const ACharacter* C = Cast<ACharacter>(Who);
	if (!C || bFinisherTaken || States.GetState() != EBossState::Stagger || Combatant->IsDead())
	{
		return false; // one per stagger; a cast-broken flinch is not a stagger
	}
	const float T = Combatant->State.Poise.StaggerElapsed();
	if (T < FinisherFrom || T > FinisherUntil)
	{
		return false;
	}
	const FVector To = C->GetActorLocation() - GetActorLocation();
	const float Dot = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D());
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
	const float Edge = To.Size2D() - GetCapsuleComponent()->GetScaledCapsuleRadius() - C->GetCapsuleComponent()->GetScaledCapsuleRadius();
	return Angle <= FinisherArc && Edge <= FinisherReach;
}

bool ABossCharacter::BeginFinisher(const AActor* Who)
{
	if (!CanBeFinishedBy(Who))
	{
		return false;
	}
	bFinisherTaken = true;
	// He stands at the same moment wherever in the window it came (QA: a late one bought extra stagger time).
	Combatant->State.Poise.SetStaggerLength(FinisherStaggerLength);
	States.SetTimeout(FinisherStaggerLength + 1.f);
	UE_LOG(LogBossTest, Log, TEXT("Sevarog: FINISHER at %.2f s into the stagger"), Combatant->State.Poise.StaggerElapsed());
	return true;
}

FTransform ABossCharacter::FinisherSpot(const AActor* Who) const
{
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	FVector Spot = GetActorLocation() + Forward * FinisherStandOff;
	Spot.Z = Who ? Who->GetActorLocation().Z : Spot.Z;
	return FTransform((-Forward).Rotation(), Spot);
}

float ABossCharacter::GetFinisherGlow() const
{
	return GlowAlpha;
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
	if (Actions.Num() > 0)
	{
		const FBossDecision D = Director.Decide(Perception(), Rng);
		LastWhy = D.Why;
		// A circle re-picked after a band change keeps its way round and its remaining time (QA: no fresh rolls on an
		// edge); any other pick starts fresh.
		if (bKeepCircle && D.Move == EBossMove::Circle && CircleLeft > 0.1f)
		{
			MoveLength = CircleLeft;
		}
		else
		{
			CircleSign = D.CircleSign;
			MoveLength = D.Duration;
			bCircleFlipped = false;
		}
		bKeepCircle = false;
		CurrentMove = D.Move;
		ProgressFrom = GetActorLocation();
		ProgressTime = 0.f;
		switch (D.Move)
		{
		case EBossMove::Attack:
			StartAction(D.Action, D.Why, EBossPriority::CommittedAttack, -1, -1, true);
			break;
		case EBossMove::Turn:
			States.Request(EBossState::Turn, EBossPriority::Decision, D.Why);
			break;
		case EBossMove::Approach:
			States.Request(EBossState::Approach, EBossPriority::Decision, D.Why, MoveLength + 1.f);
			break;
		case EBossMove::Circle:
			States.Request(EBossState::Circle, EBossPriority::Decision, D.Why, MoveLength + 1.f);
			break;
		case EBossMove::Reposition:
			States.Request(EBossState::Reposition, EBossPriority::Decision, D.Why, MoveLength + 1.f);
			break;
		default:
			PauseLength = FMath::Max(D.Duration, 0.25f);
			States.Request(EBossState::Neutral, EBossPriority::Decision, D.Why, PauseLength + 1.f);
			break;
		}
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
	// The attack starts in OnStateChanged, once the machine accepts it (a one-step plan).
	PendingAttack = ExecutionersSwing;
	Plan.Reset();
	FPlannedStep& Only = Plan.AddDefaulted_GetRef();
	Only.Attack = ExecutionersSwing;
	Only.bFirstOfAction = true;
	const float Timeout = ExecutionersSwing->GetTiming().Total() + 1.f;
	if (!States.Request(EBossState::Attack, EBossPriority::CommittedAttack, TEXT("Executioner's Swing: ") + Reason, Timeout))
	{
		PendingAttack = nullptr;
		Plan.Reset();
	}
}

void ABossCharacter::TickApproach(float DeltaSeconds)
{
	ACharacter* P = Player();
	if (!P || PlayerDown())
	{
		States.Complete(EBossState::Evaluate, TEXT("player gone or down"));
		return;
	}
	if (Actions.Num() == 0 && EdgeDistanceToPlayer() <= SwingRange() && AngleToPlayer() <= 90.f)
	{
		StartSwing(TEXT("reached range"));
		return;
	}
	const float Limit = Actions.Num() > 0 ? MoveLength : ApproachTime;
	if (States.GetStateTime() >= Limit)
	{
		States.Complete(EBossState::Evaluate, TEXT("approach ran out"));
		return;
	}
	// Toward where you'll be (section 11: 0.45 s ahead, at most 3 m), never past the arena's edge; no lead while you
	// dodge, so a dodge can't drag him (the player reports what may be led).
	const FVector Centre = GetArenaCentre();
	FVector Aim = ClampToArena(InterceptPoint(P->GetActorLocation(), UCombatantComponent::LeadVelocityOf(P), 0.45f, 300.f), Centre, ArenaRadius);
	const FVector Dir = (Aim - GetActorLocation()).GetSafeNormal2D();
	FaceTarget(DeltaSeconds);
	// Chasing a runner at pursuit speed; at Medium, with you holding back, a slow stalk; otherwise his glide.
	const bool bStalk = Director.GetBand() == EBossBand::Medium && GetWorld()->GetTimeSeconds() - LastPlayerAttackTime > 2.0;
	SteerAndGlide(Dir, Director.PursuitActive() ? PursuitSpeed : bStalk ? StalkSpeed : GlideSpeed, DeltaSeconds);
	CheckProgress(DeltaSeconds, EBossMove::Approach);
}

void ABossCharacter::TickMovement(float DeltaSeconds)
{
	ACharacter* P = Player();
	if (!P || PlayerDown())
	{
		States.Complete(EBossState::Evaluate, TEXT("player gone or down"));
		return;
	}
	if (States.GetStateTime() >= MoveLength)
	{
		States.Complete(EBossState::Evaluate, TEXT("movement over"));
		return;
	}
	const FVector Centre = GetArenaCentre();
	const FVector Self = GetActorLocation();
	FaceTarget(DeltaSeconds);
	if (States.GetState() == EBossState::Circle)
	{
		// Round you; at the edge he flips once, and a second time ends it (section 10).
		FVector Dir = CircleDirection(Self, P->GetActorLocation(), CircleSign);
		if (LeavesArena(Self, Dir, CircleSpeed, 0.5f, Centre, ArenaRadius))
		{
			if (bCircleFlipped)
			{
				Director.NoteNoProgress(EBossMove::Circle);
				States.Complete(EBossState::Evaluate, TEXT("circled into the edge twice"));
				return;
			}
			bCircleFlipped = true;
			CircleSign = -CircleSign;
			Dir = -Dir;
		}
		SteerAndGlide(Dir, CircleSpeed, DeltaSeconds);
		CheckProgress(DeltaSeconds, EBossMove::Circle);
	}
	else
	{
		// Backing off, sliding along the edge instead of into it.
		const FVector Away = (Self - P->GetActorLocation()).GetSafeNormal2D();
		SteerAndGlide(SlideAlongWall(Self, Away, Centre, ArenaRadius), RepositionSpeed, DeltaSeconds);
		CheckProgress(DeltaSeconds, EBossMove::Reposition);
	}
}

void ABossCharacter::SteerAndGlide(const FVector& Dir, float Speed, float DeltaSeconds)
{
	GetCharacterMovement()->MaxWalkSpeed = Speed;
	AddMovementInput(Dir, 1.f);
	PlayGlideClip(Dir);
}

bool ABossCharacter::CheckProgress(float DeltaSeconds, EBossMove Move)
{
	// A movement that covers under 0.3 m in 0.4 s (pinned at the wall, blocked by you) ends and sits out the next roll.
	ProgressTime += DeltaSeconds;
	if (ProgressTime < 0.4f)
	{
		return true;
	}
	const bool bMoved = FVector::Dist2D(GetActorLocation(), ProgressFrom) >= 30.f;
	ProgressFrom = GetActorLocation();
	ProgressTime = 0.f;
	if (!bMoved)
	{
		Director.NoteNoProgress(Move);
		States.Complete(EBossState::Evaluate, TEXT("no progress"));
	}
	return bMoved;
}

void ABossCharacter::TickTurn(float DeltaSeconds)
{
	FaceTarget(DeltaSeconds);
	if (!Player() || AngleToPlayer() <= FacingTolerance)
	{
		States.Complete(EBossState::Evaluate, TEXT("facing you"));
	}
}

void ABossCharacter::SampleSwing()
{
	const UAttackDefinition* Def = Attacks->GetCurrent();
	const ACharacter* P = Player();
	if (!Def || !P || Def->SocketStart.IsNone())
	{
		return; // only steps with a weapon sweep
	}
	if (Attacks->GetInstanceId() != Swing.Instance)
	{
		FlushSwing();
		Swing = FSwingRecord();
		Swing.Instance = Attacks->GetInstanceId();
		Swing.Step = Def->GetName();
		Swing.Action = Actions.IsValidIndex(CurrentAction) ? Actions[CurrentAction].Name.ToString() : FString(TEXT("?"));
		Swing.EdgeAtWindup = EdgeDistanceToPlayer();
		const FVector Away = (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		Swing.RadialSpeed = FVector::DotProduct(P->GetVelocity(), Away);
	}
	const FAttackSample Sample = Attacks->GetSample();
	if (Sample.Phase == EAttackPhase::Active)
	{
		if (Swing.EdgeAtActive < 0.f)
		{
			Swing.EdgeAtActive = EdgeDistanceToPlayer();
		}
		const UCapsuleComponent* Cap = P->GetCapsuleComponent();
		const FVector Up(0.f, 0.f, FMath::Max(0.f, Cap->GetScaledCapsuleHalfHeight() - Cap->GetScaledCapsuleRadius()));
		FVector OnHammer, OnYou;
		FMath::SegmentDistToSegmentSafe(GetMesh()->GetSocketLocation(Def->SocketStart), GetMesh()->GetSocketLocation(Def->SocketEnd),
			P->GetActorLocation() - Up, P->GetActorLocation() + Up, OnHammer, OnYou);
		Swing.ClosestGap = FMath::Min(Swing.ClosestGap, FMath::Max(0.f, FVector::Dist(OnHammer, OnYou) - Cap->GetScaledCapsuleRadius() - Def->Radius));
	}
	if (const APlayerCharacter* PC = Cast<APlayerCharacter>(P))
	{
		Swing.bDodged |= PC->GetActions().GetState() == EPlayerState::Dodging;
		Swing.bGuarded |= PC->GetActions().IsGuardActive();
	}
}

void ABossCharacter::FlushSwing()
{
	if (Swing.Instance == 0)
	{
		return;
	}
	// A whiff with no dodge or guard and the hammer short of you is his miss; a gap that grew while you walked away is spacing.
	UE_LOG(LogBossTest, Log, TEXT("Sevarog SWING %s / %s: %.0f cm at windup (you %+.0f cm/s), %.0f at active, hammer closest %.0f cm, %s%s%s"),
		*Swing.Action, *Swing.Step, Swing.EdgeAtWindup, Swing.RadialSpeed, Swing.EdgeAtActive, Swing.ClosestGap < 1e8f ? Swing.ClosestGap : -1.f,
		Swing.Outcome.IsEmpty() ? TEXT("WHIFF") : *Swing.Outcome, Swing.bDodged ? TEXT(", you dodged") : TEXT(""), Swing.bGuarded ? TEXT(", you guarded") : TEXT(""));
	Swing = FSwingRecord();
}

void ABossCharacter::TickAttack()
{
	SampleSwing();
	if (!Attacks->IsAttacking())
	{
		// The commitment pause (0.25 s), or the breather after every second action (the punish windows, Step 6).
		PauseLength = Actions.Num() > 0 ? Director.PauseAfterAttack() : NeutralBeat;
		CurrentMove = EBossMove::Wait;
		if (Actions.Num() > 0 && PauseLength >= 0.5f && TryPosture())
		{
			return; // the breather became the aura circle
		}
		States.Complete(EBossState::Neutral, PauseLength < 0.5f ? TEXT("pause") : TEXT("breather"), PauseLength + 1.f);
		return;
	}
	// A cast can be broken only during its windup (spec section 9).
	if (bCastOpen && Attacks->GetSample().Phase >= EAttackPhase::Active)
	{
		Combatant->State.Cast.Close();
		bCastOpen = false;
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
	// He hovers, so his jog clips are glide poses: pick the one matching his movement relative to his facing, with 10 degrees
	// of hysteresis at the diagonals so he doesn't pop between strafe and forward near 45 (the AI engineer, 2026-11-25).
	const float Angle = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Dir, GetActorRightVector()), FVector::DotProduct(Dir, GetActorForwardVector())));
	auto InBand = [Angle](float Centre, float Half) { return FMath::Abs(FMath::FindDeltaAngleDegrees(Centre, Angle)) <= Half; };
	const bool bKeep = (GlideClip == GlideForward && InBand(0.f, 55.f)) || (GlideClip == GlideRight && InBand(90.f, 55.f))
		|| (GlideClip == GlideBack && InBand(180.f, 55.f)) || (GlideClip == GlideLeft && InBand(-90.f, 55.f));
	UAnimSequenceBase* Wanted = bKeep ? GlideClip.Get() : InBand(0.f, 45.f) ? GlideForward.Get() : InBand(90.f, 45.f) ? GlideRight.Get()
		: InBand(-90.f, 45.f) ? GlideLeft.Get() : GlideBack.Get();
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
	case EBossState::Posture:
		EndPostureEffects(); // every exit (an ending, a stagger, death, a reset, a timeout) cleans up
		break;
	case EBossState::Intro:
		Combatant->State.bImmune = false;
		FightStartTime = GetWorld()->GetTimeSeconds();
		break;
	case EBossState::Approach:
		break; // the glide carries on into another movement; Neutral, Turn and attacks stop it
	case EBossState::Attack:
		FlushSwing();
		if (Attacks->IsAttacking())
		{
			Attacks->StopAttack(0.15f); // interrupted: a stagger, death or a timeout (clears his armour and the queue)
		}
		Combatant->State.Cast.Close(); // a cast never stays open into the next attack
		bCastOpen = false;
		Plan.Reset();
		CurrentAction = -1;
		CurrentCombo = -1;
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
		if (PendingAttack && Plan.Num() > 0)
		{
			Attacks->StartAttack(PendingAttack, Player(), PendingAttack->BlendIn, PendingAttack->bEaseBlendIn, Plan[0].ExtraHold);
			StepIndex = 0;
			Director.NoteCommitted(CurrentAction, CurrentCombo);
			Director.NoteStepStarted(CurrentAction);
			if (Plan.IsValidIndex(1))
			{
				const FPlannedStep& Next = Plan[1];
				if (Next.ChainIntoRecovery >= 0.f)
				{
					Attacks->QueueNextAt(Next.Attack, Next.ExtraHold, Next.ChainIntoRecovery);
				}
				else
				{
					Attacks->QueueNext(Next.Attack, Next.ExtraHold);
				}
			}
			if (Actions.IsValidIndex(CurrentAction) && Actions[CurrentAction].CastBreakThreshold > 0.f)
			{
				Combatant->State.Cast.Open(Actions[CurrentAction].CastBreakThreshold);
				bCastOpen = true;
			}
		}
		PendingAttack = nullptr;
		bRecoveringSent = false;
		break;
	case EBossState::CastBroken:
	{
		StopGlide();
		UAnimSequenceBase* Clip = CastBrokenClip ? CastBrokenClip.Get() : StaggerClip.Get();
		PlayClip(Clip, Clip ? Clip->GetPlayLength() / FMath::Max(CastBrokenTime, 0.1f) : 1.f, 0.1f, 0.2f);
		break;
	}
	case EBossState::Stagger:
	{
		StopGlide();
		if (!Poise.IsStaggered())
		{
			Poise.BeginStagger(); // a deferred or forced stagger; a direct one began in the poise model already
		}
		bFinisherTaken = false;
		StunPhase = 0;
		StaggerYaw = GetActorRotation().Yaw;
		const float Length = FMath::Max(Poise.StaggerDuration, 0.1f);
		// The five-run stagger measurement (the punish windows, Step 6): how long since the last one.
		const double NowS = GetWorld()->GetTimeSeconds();
		UE_LOG(LogBossTest, Log, TEXT("Sevarog: STAGGER %.1f s into the fight (%s)"), NowS - FightStartTime,
			LastStaggerTime >= 0.0 ? *FString::Printf(TEXT("%.1f s since the last"), NowS - LastStaggerTime) : TEXT("the first"));
		LastStaggerTime = NowS;
		if (HasStunClips())
		{
			PlayClip(StunStartClip, StunStartRate, 0.1f, 0.2f); // the slump; the loop and the stand-up follow (TickStagger)
		}
		else
		{
			PlayClip(StaggerClip, StaggerClip ? StaggerClip->GetPlayLength() / Length : 1.f, 0.1f, 0.25f);
		}
		break;
	}
	case EBossState::Dead:
		Attacks->StopAttack(0.1f);
		ClearCombat(); // rings and projectiles in flight die with him (spec section 12)
		StopGlide();
		if (PlayClip(DeathClip, 1.f, 0.1f, 0.f))
		{
			// Hold the last pose instead of blending back to idle.
			if (FAnimMontageInstance* Instance = GetMesh()->GetAnimInstance()->GetActiveInstanceForMontage(ClipMontage))
			{
				Instance->bEnableAutoBlendOut = false;
			}
			// The clip ends mid-collapse: a ragdoll puts him on the floor.
			Combatant->RagdollAfter(ClipMontage ? ClipMontage->GetPlayLength() : DeathClip->GetPlayLength());
		}
		break;
	case EBossState::Posture:
	{
		StopGlide();
		PostureStartTime = GetWorld()->GetTimeSeconds();
		LastPostureTime = PostureStartTime;
		bPostureFlipped = false;
		PlayClipEased(PostureClip, 1.f, 0.5f, 0.3f, 1000);
		if (PostureSmoke)
		{
			PostureFX.Add(UGameplayStatics::SpawnEmitterAttached(PostureSmoke, GetMesh(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, false));
		}
		if (PostureHammerEffect)
		{
			PostureFX.Add(UGameplayStatics::SpawnEmitterAttached(PostureHammerEffect, GetMesh(), TEXT("Weapon_01"), FVector::ZeroVector,
				FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false));
		}
		break;
	}
	case EBossState::Paused:
	case EBossState::Neutral:
	case EBossState::Turn:
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
	if (Outcome.DamageDealt > 0.f && Attacker && Attacker == Player())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		PlayerHitTimes.RemoveAll([this, Now](double T) { return Now - T > PunishWindow; });
		PlayerHitTimes.Add(Now); // Punishment Sweep's stand-in trigger (not a perfect guard's poise)
		// A hit ends his pause (never below its 0.25 s floor) or any movement (the punish windows and QA, Step 6).
		const EBossState S = States.GetState();
		if (Actions.Num() > 0 && !Outcome.bKilled && Outcome.Poise == EPoiseOutcome::None)
		{
			if (S == EBossState::Neutral)
			{
				PauseLength = Director.PauseEndAfterHit(0.f, PauseLength, States.GetStateTime());
				Director.NoteBeatBroken();
			}
			else if (S == EBossState::Posture)
			{
				Director.NoteBeatBroken();
				PauseLength = PostureRouse;
				States.Complete(EBossState::Neutral, TEXT("aura broken: hit"), PauseLength + 1.f);
			}
			else if (S == EBossState::Approach || S == EBossState::Circle || S == EBossState::Reposition || S == EBossState::Turn)
			{
				Director.NoteBeatBroken();
				States.Complete(EBossState::Evaluate, TEXT("hit ends the movement"));
			}
		}
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
	else if (Outcome.bCastBroken && Attacks->GetSample().Phase < EAttackPhase::Active)
	{
		// Only before the release: the attack ticks first, so a hit landing the frame the grasp leaves can't break it.
		bCastOpen = false;
		States.Request(EBossState::CastBroken, EBossPriority::Stagger, TEXT("cast broken"), CastBrokenTime + 1.f);
	}
}

void ABossCharacter::InitDirector()
{
	// The director sees each action by its index in Actions, and each combo by its index in Combos (names resolved).
	TArray<FBossOption> Options;
	for (int32 i = 0; i < Actions.Num(); ++i)
	{
		const FBossAction& A = Actions[i];
		FBossOption& O = Options.AddDefaulted_GetRef();
		O.Id = i;
		O.MinRange = A.MinRange;
		O.MaxRange = A.MaxRange;
		O.Angle = static_cast<EBossAngleRule>(A.Angle);
		O.RearWeight = A.RearWeight;
		O.Weights = { A.Weights.Close, A.Weights.Medium, A.Weights.Far, A.Weights.Extreme };
		O.Cooldown = A.Cooldown;
		O.bNeedsPunishTrigger = A.bNeedsPunishTrigger;
	}
	auto IndexOf = [this](FName Name) { return Actions.IndexOfByPredicate([Name](const FBossAction& A) { return A.Name == Name; }); };
	TArray<FBossComboOption> ComboOptions;
	ComboNames.Reset();
	for (int32 c = 0; c < Combos.Num(); ++c)
	{
		const FBossCombo& Combo = Combos[c];
		FBossComboOption C;
		C.Id = c;
		C.Opener = IndexOf(Combo.Opener);
		C.Weight = Combo.Weight;
		C.Cooldown = Combo.Cooldown;
		C.bNeedsFreshBeat = Combo.bNeedsFreshBeat;
		C.bNeedsSeen = Combo.bNeedsSeen;
		C.Majors = Combo.Majors;
		bool bResolved = C.Opener != INDEX_NONE;
		for (const FBossComboLink& Link : Combo.FollowUps)
		{
			const int32 F = IndexOf(Link.Action);
			bResolved &= F != INDEX_NONE;
			C.FollowUps.Add(F);
		}
		ComboNames.Add(Combo.Name);
		if (bResolved)
		{
			ComboOptions.Add(MoveTemp(C));
		}
		else
		{
			UE_LOG(LogBossTest, Error, TEXT("%s: combo %s names an action he doesn't have"), *GetName(), *Combo.Name.ToString());
		}
	}
	FBossDirectorTuning Tuning;
	Tuning.CommitPause = CommitPause;
	Tuning.Breather = Breather;
	Tuning.StopShare = ComboStopShare;
	Director.Init(MoveTemp(Options), MoveTemp(ComboOptions), Tuning);
}

FBossPerception ABossCharacter::Perception() const
{
	FBossPerception P;
	P.EdgeCm = EdgeDistanceToPlayer();
	P.AngleDeg = AngleToPlayer();
	P.bPlayerDown = PlayerDown();
	P.bPunishTrigger = PunishTriggered();
	return P;
}

bool ABossCharacter::CanContinuePlan() const
{
	// The next planned step goes ahead unless a stagger waits for the safe point; a combo's follow-up also needs to fit
	// right now (rule 4: range and 90 deg, the player not down), checked once at its chain point.
	if (States.GetState() != EBossState::Attack || States.HasPendingSafePoint())
	{
		return false;
	}
	if (Plan.IsValidIndex(StepIndex + 1) && Plan[StepIndex + 1].bFirstOfAction && Player())
	{
		const FPlannedStep& Next = Plan[StepIndex + 1];
		if (!Director.IsValidNow(Next.Action, Perception(), true))
		{
			UE_LOG(LogBossTest, Log, TEXT("Sevarog: %s ends: %s doesn't fit at the chain point (%.1f m, %.0f deg)"),
				ComboNames.IsValidIndex(CurrentCombo) ? *ComboNames[CurrentCombo].ToString() : TEXT("the combo"),
				*Actions[Next.Action].Name.ToString(), EdgeDistanceToPlayer() / 100.f, AngleToPlayer());
			return false;
		}
	}
	return true;
}

bool ABossCharacter::PunishTriggered() const
{
	const double Now = GetWorld()->GetTimeSeconds();
	int32 Recent = 0;
	for (const double T : PlayerHitTimes)
	{
		Recent += Now - T <= PunishWindow ? 1 : 0;
	}
	return Recent >= PunishHits;
}

void ABossCharacter::OnOwnSweep(const FHitOutcome& Outcome, AActor* Victim, uint32 InstanceId)
{
	if (InstanceId == Swing.Instance && Swing.Outcome.IsEmpty())
	{
		Swing.Outcome = Outcome.Result == EHitResult::Hit ? TEXT("hit") : Outcome.Result == EHitResult::Blocked ? TEXT("blocked")
			: Outcome.Result == EHitResult::PerfectGuard ? TEXT("perfect guard") : Outcome.Result == EHitResult::Dodged ? TEXT("through your i-frames") : TEXT("other");
	}
	// The player's guard broke on the action's first step (the block that emptied the bar resolves inside this sweep,
	// before the attack's end): he stops there instead of going on (Double Execution's hit 2 would land inside the break).
	const APlayerCharacter* P = Cast<APlayerCharacter>(Victim);
	if (Outcome.Result != EHitResult::Blocked || !P || P->GetActions().GetState() != EPlayerState::GuardBroken || StepIndex != 0 || Plan.Num() < 2
		|| !Actions.IsValidIndex(CurrentAction) || !Actions[CurrentAction].GuardBreakRecovery)
	{
		return;
	}
	if (Attacks->SwapRecovery(Actions[CurrentAction].GuardBreakRecovery))
	{
		Plan.SetNum(1);
		const float Left = Attacks->GetTimeToFree();
		States.NextStep(TEXT("guard broken: he stops"), Left + 1.f);
	}
}

bool ABossCharacter::StartAction(int32 Index, const FString& Reason, EBossPriority Priority, int32 Combo, int32 ForcedVariant, bool bRollCombo)
{
	if (!Actions.IsValidIndex(Index) || Actions[Index].Variants.Num() == 0 || !FBossStateMachine::IsLegal(States.GetState(), EBossState::Attack))
	{
		return false; // checked before any roll, so a rejected request (key 3 mid-attack) can't disturb the running action
	}
	// Flatten the action and, for a combo, each follow-up: every action rolls its variant now (section 5: Double
	// Execution's branch at commit), and its last step carries that variant's hold variation.
	TArray<FPlannedStep> NewPlan;
	auto AddAction = [this, &NewPlan](int32 ActionIndex, float ChainIntoRecovery, int32 Forced, UAttackDefinition* Own = nullptr) -> int32
	{
		const FBossAction& A = Actions[ActionIndex];
		float Total = 0.f;
		for (const FBossActionVariant& V : A.Variants)
		{
			Total += FMath::Max(0.f, V.Chance);
		}
		float Roll = Rng.FRand() * Total;
		int32 Pick = A.Variants.Num() - 1;
		for (int32 i = 0; i < A.Variants.Num(); ++i)
		{
			Roll -= FMath::Max(0.f, A.Variants[i].Chance);
			if (Roll <= 0.f)
			{
				Pick = i;
				break;
			}
		}
		if (A.Variants.IsValidIndex(Forced))
		{
			Pick = Forced; // tests and debugging (the roll still advanced, so the seeded stream doesn't shift)
		}
		const FBossActionVariant& V = A.Variants[Pick];
		const float Hold = V.HoldExtraMax > V.HoldExtraMin ? Rng.FRandRange(V.HoldExtraMin, V.HoldExtraMax) : V.HoldExtraMin;
		if (Own)
		{
			// A combo's own version of the action replaces its variants (the rolls above still ran, so the stream doesn't shift).
			FPlannedStep& Step = NewPlan.AddDefaulted_GetRef();
			Step.Attack = Own;
			Step.Action = ActionIndex;
			Step.bFirstOfAction = true;
			Step.ChainIntoRecovery = ChainIntoRecovery;
			return Pick;
		}
		for (int32 s = 0; s < V.Steps.Num(); ++s)
		{
			FPlannedStep& Step = NewPlan.AddDefaulted_GetRef();
			Step.Attack = V.Steps[s];
			Step.Action = ActionIndex;
			Step.bFirstOfAction = s == 0;
			Step.ChainIntoRecovery = s == 0 ? ChainIntoRecovery : -1.f;
			Step.ExtraHold = s == V.Steps.Num() - 1 ? Hold : 0.f;
		}
		return Pick;
	};
	const int32 Picked = AddAction(Index, -1.f, ForcedVariant);
	if (bRollCombo)
	{
		// After the variant: a branch that ends the action alone (Double Execution's stop) takes no combo.
		Combo = Actions[Index].Variants[Picked].bEndsAlone ? -1 : Director.RollCombo(Index, Rng);
	}
	if (Combos.IsValidIndex(Combo))
	{
		for (const FBossComboLink& Link : Combos[Combo].FollowUps)
		{
			const int32 F = Actions.IndexOfByPredicate([&Link](const FBossAction& A) { return A.Name == Link.Action; });
			if (F == INDEX_NONE || Actions[F].Variants.Num() == 0)
			{
				break;
			}
			AddAction(F, Link.ChainIntoRecovery, -1, Link.Attack);
		}
	}
	if (NewPlan.Num() == 0 || !NewPlan[0].Attack)
	{
		return false;
	}
	Plan = MoveTemp(NewPlan);
	PendingAttack = Plan[0].Attack;
	CurrentAction = Index;
	CurrentCombo = Combos.IsValidIndex(Combo) ? Combo : -1;
	const float Timeout = Plan[0].Attack->GetTiming().Total() + Plan[0].ExtraHold + 1.f;
	const FString Label = FString::Printf(TEXT("%s%s: %s"), *Actions[Index].Name.ToString(),
		CurrentCombo >= 0 ? *FString::Printf(TEXT(" (combo %s)"), *ComboNames[CurrentCombo].ToString()) : TEXT(""), *Reason);
	if (!States.Request(EBossState::Attack, Priority, Label, Timeout))
	{
		PendingAttack = nullptr;
		Plan.Reset();
		CurrentAction = -1;
		CurrentCombo = -1;
		return false;
	}
	LastAction = Index;
	return true;
}

void ABossCharacter::OnStepStarted(const UAttackDefinition* Step, uint32 InstanceId)
{
	// The attack component already started the step; the machine treats it as a fresh commitment.
	++StepIndex;
	const FPlannedStep* Planned = Plan.IsValidIndex(StepIndex) ? &Plan[StepIndex] : nullptr;
	const float Timeout = Step ? Step->GetTiming().Total() + (Planned ? Planned->ExtraHold : 0.f) + 1.f : -1.f;
	if (!States.NextStep(FString::Printf(TEXT("next step: %s"), Step ? *Step->GetName() : TEXT("?")), Timeout))
	{
		return; // a pending request played instead; leaving the attack state stops the step
	}
	bRecoveringSent = false;
	if (Planned && Planned->bFirstOfAction)
	{
		Director.NoteStepStarted(Planned->Action); // its cooldown starts when it really plays
		CurrentAction = Planned->Action;
	}
	if (Plan.IsValidIndex(StepIndex + 1))
	{
		const FPlannedStep& Next = Plan[StepIndex + 1];
		if (Next.ChainIntoRecovery >= 0.f)
		{
			Attacks->QueueNextAt(Next.Attack, Next.ExtraHold, Next.ChainIntoRecovery);
		}
		else
		{
			Attacks->QueueNext(Next.Attack, Next.ExtraHold);
		}
	}
}

void ABossCharacter::ClearCombat()
{
	Delivery->ClearAll();
	Attacks->ClearEffects();
	Combatant->State.Cast.Close();
	bCastOpen = false;
}

UAnimMontage* ABossCharacter::PlayClipEased(UAnimSequenceBase* Clip, float Rate, float BlendIn, float BlendOut, int32 Loops)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim || !Clip)
	{
		return nullptr;
	}
	GlideClip = nullptr;
	FMontageBlendSettings In(BlendIn);
	In.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
	FMontageBlendSettings Out(BlendOut);
	Out.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
	ClipMontage = Anim->PlaySlotAnimationAsDynamicMontage_WithBlendSettings(Clip, MontageSlot, In, Out, Rate, Loops);
	return ClipMontage;
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
	if (Actions.Num() > 0)
	{
		static const TCHAR* Bands[] = { TEXT("close"), TEXT("medium"), TEXT("far"), TEXT("extreme") };
		static const TCHAR* Angles[] = { TEXT("front"), TEXT("side"), TEXT("rear quarter"), TEXT("behind") };
		Lines.Add(FString::Printf(TEXT("  action %s%s  |  band %s, %s%s  |  key 3 forces: %s (4 picks the next)%s"),
			Actions.IsValidIndex(CurrentAction) ? *Actions[CurrentAction].Name.ToString() : TEXT("-"),
			ComboNames.IsValidIndex(CurrentCombo) ? *FString::Printf(TEXT(" (combo %s)"), *ComboNames[CurrentCombo].ToString()) : TEXT(""),
			Bands[static_cast<int32>(Director.GetBand())], Angles[static_cast<int32>(Director.GetAngleBand())],
			Director.PursuitActive() ? TEXT(", PURSUING") : TEXT(""), *GetSelectedActionName().ToString(),
			PunishTriggered() ? TEXT("  [punish ready]") : TEXT("")));
		Lines.Add(FString::Printf(TEXT("  last decision: %s"), *LastWhy.Left(110)));
	}

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
	if (!bAIEnabled && (S == EBossState::Evaluate || S == EBossState::Approach || S == EBossState::Turn || S == EBossState::Neutral
		|| S == EBossState::Circle || S == EBossState::Reposition))
	{
		States.Request(EBossState::Paused, EBossPriority::Decision, TEXT("AI off"));
	}
	else if (bAIEnabled && S == EBossState::Paused)
	{
		States.Request(EBossState::Evaluate, EBossPriority::Decision, TEXT("AI on"));
	}
}

FName ABossCharacter::GetSelectedActionName() const
{
	return Actions.IsValidIndex(SelectedAction) ? Actions[SelectedAction].Name : FName(TEXT("ExecutionersSwing"));
}

void ABossCharacter::SelectNextAction()
{
	SelectedAction = Actions.Num() > 0 ? (SelectedAction + 1) % Actions.Num() : 0;
}

bool ABossCharacter::ForceAction(int32 Index, int32 Variant)
{
	if (States.GetState() == EBossState::Neutral)
	{
		States.Complete(EBossState::Evaluate, TEXT("forced action"));
	}
	return StartAction(Index, TEXT("forced"), EBossPriority::Scripted, -1, Variant);
}

bool ABossCharacter::ForceCombo(int32 ComboIndex)
{
	if (!Combos.IsValidIndex(ComboIndex))
	{
		return false;
	}
	const FName Opener = Combos[ComboIndex].Opener;
	const int32 Index = Actions.IndexOfByPredicate([Opener](const FBossAction& A) { return A.Name == Opener; });
	if (States.GetState() == EBossState::Neutral)
	{
		States.Complete(EBossState::Evaluate, TEXT("forced combo"));
	}
	return Index != INDEX_NONE && StartAction(Index, TEXT("forced"), EBossPriority::Scripted, ComboIndex);
}

void ABossCharacter::PinAction(int32 Index)
{
	Director.PinAction(Index);
}

void ABossCharacter::ForceSwing()
{
	if (Actions.Num() > 0)
	{
		ForceAction(SelectedAction);
		return;
	}
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
	Plan.Reset();
	CurrentAction = -1;
	CurrentCombo = -1;
	LastAction = -1;
	CurrentMove = EBossMove::Wait;
	Director.Reset(); // bands, cooldowns, history: a retry starts clean
	LastStaggerTime = -1.0;
	bKeepCircle = false;
	bFinisherTaken = false;
	StunPhase = 0;
	GlowAlpha = 0.f;
	FinisherGlow->SetVisibility(false);
	EndPostureEffects();
	HitsOnPlayerTimes.Reset();
	LastKnockdownOnPlayer = -100.0;
	LastPostureTime = -1000.0;
	PlayerHitTimes.Reset();
	ClearCombat();
	Rng.Initialize(Seed != 0 ? Seed : FMath::Rand()); // a retry starts its own rolls (the same ones with a fixed seed)
	UE_LOG(LogBossTest, Log, TEXT("Sevarog: seed %d"), Rng.GetInitialSeed());
	bRecoveringSent = false;
	Combatant->ResetCombatant();
	TeleportTo(HomeTransform.GetLocation(), HomeTransform.Rotator());
	GetCharacterMovement()->StopMovementImmediately();
	States.Reset(IntroLength + 1.f);
	StartIntro();
}
