// Step 3: plays the arena in the editor (headless) and checks the player kit is wired up: the right
// pawn, controller and HUD, the starting resources, and a few seconds of play with no errors logged
// (an error-level log fails the test, so a missing asset or a broken tick order shows up here).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Boss/BossCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodySetup.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Combat/AttackComponent.h"
#include "Combat/CombatantComponent.h"
#include "Combat/DeliveryComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/HUD.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "InputKeyEventArgs.h"
#include "Combat/AttackDefinition.h"
#include "GameFramework/PlayerController.h"
#include "Player/BossTestPlayerController.h"
#include "Player/PlayerCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/BossTestHUD.h"

namespace
{
	constexpr EAutomationTestFlags SmokeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	UWorld* PlayWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCheckPlayerKitInArena, FAutomationTestBase*, Test);
bool FCheckPlayerKitInArena::Update()
{
	UWorld* World = PlayWorld();
	if (!World)
	{
		Test->AddError(TEXT("no play world"));
		return true;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	const APlayerCharacter* Player = PC ? Cast<APlayerCharacter>(PC->GetPawn()) : nullptr;
	Test->TestNotNull(TEXT("the possessed pawn is an APlayerCharacter"), Player);
	Test->TestTrue(TEXT("the debug player controller"), PC && PC->IsA<ABossTestPlayerController>());
	Test->TestTrue(TEXT("the canvas HUD"), PC && PC->GetHUD() && PC->GetHUD()->IsA<ABossTestHUD>());
	if (Player)
	{
		const FPlayerActions& Actions = Player->GetActions();
		Test->TestEqual(TEXT("full stamina, from the rules on the Blueprint"), Actions.Stamina.Current, Actions.Tuning.StaminaMax);
		Test->TestEqual(TEXT("two heal charges"), Actions.GetHealCharges(), 2);
		Test->TestEqual(TEXT("full health"), Player->GetCombatant()->State.Health.Current, 300.f);
		Test->TestEqual(TEXT("four lights, so the costs match the combo"), Actions.Costs.Lights.Num(), 4);
		const UCharacterMovementComponent* Move = Player->GetCharacterMovement();
		const FVector At = Player->GetActorLocation();
		Test->TestTrue(FString::Printf(TEXT("free, standing on the floor (state %d, movement mode %d, at %.0f %.0f %.0f, velocity z %.0f)"),
			static_cast<int32>(Actions.GetState()), static_cast<int32>(Move->MovementMode.GetValue()), At.X, At.Y, At.Z, Move->Velocity.Z),
			Actions.GetState() == EPlayerState::Free && Move->IsMovingOnGround());
	}
	TActorIterator<ABossCharacter> Boss(World);
	Test->TestTrue(TEXT("the boss is an ABossCharacter in the arena"), static_cast<bool>(Boss));
	if (Boss)
	{
		Test->TestTrue(TEXT("with his swing set"), Boss->ExecutionersSwing != nullptr);
		Test->TestEqual(TEXT("and his stagger at 1.8 s (no Blueprint or level override left at 1.25)"), Boss->GetCombatant()->StaggerDuration, 1.8f);
	}
	return true;
}

// Waits (up to 40 s of play) for the spike boss to land a hit on the standing player, which proves his
// attacks reach the new player through the resolver and the hit reaction plays without trouble.
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FWaitForBossHitOnPlayer, FAutomationTestBase*, Test, double, StartTime);
bool FWaitForBossHitOnPlayer::Update()
{
	if (StartTime <= 0.0)
	{
		StartTime = FPlatformTime::Seconds(); // timed from when this command starts, not when it was queued
	}
	UWorld* World = PlayWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCharacter* Player = PC ? Cast<APlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!Player)
	{
		Test->AddError(TEXT("no player to watch"));
		return true;
	}
	if (Player->GetCombatant()->State.Health.Current < 300.f)
	{
		const EPlayerState State = Player->GetActions().GetState();
		Test->TestTrue(TEXT("the hit put him in a reaction, a knockdown, or killed him"),
			State == EPlayerState::HitReact || State == EPlayerState::Knockdown || State == EPlayerState::Dead);
		Test->TestTrue(TEXT("and the character reported it"), Player->LastEventText().StartsWith(TEXT("HIT")) || State == EPlayerState::Dead);
		return true;
	}
	if (FPlatformTime::Seconds() - StartTime > 40.0)
	{
		FString BossInfo = TEXT("no boss");
		if (TActorIterator<ABossCharacter> It(World); It)
		{
			const UCharacterMovementComponent* Move = It->GetCharacterMovement();
			BossInfo = FString::Printf(TEXT("boss at %s, mode %d, velocity %s, state %s, controller %s, AI system %s"), *It->GetActorLocation().ToString(),
				static_cast<int32>(Move->MovementMode.GetValue()), *Move->Velocity.ToString(), FBossStateMachine::StateName(It->GetStates().GetState()),
				It->GetController() ? *It->GetController()->GetName() : TEXT("none"), World->GetWorldSettings()->IsAISystemEnabled() ? TEXT("on") : TEXT("off"));
		}
		Test->AddError(FString::Printf(TEXT("the boss never hit the player in 40 s (player at %s; %s)"), *Player->GetActorLocation().ToString(), *BossInfo));
		return true;
	}
	return false;
}

// Forces a stagger on the boss, then checks it holds for its 1.25 s and ends on its own.
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCheckForcedStagger, FAutomationTestBase*, Test, int32, Phase);
bool FCheckForcedStagger::Update()
{
	UWorld* World = PlayWorld();
	ABossCharacter* Boss = nullptr;
	if (World)
	{
		if (TActorIterator<ABossCharacter> It(World); It)
		{
			Boss = *It;
		}
	}
	if (!Boss)
	{
		Test->AddError(TEXT("no boss to stagger"));
		return true;
	}
	if (Phase == 0)
	{
		Boss->ForceStagger();
		Test->TestTrue(TEXT("a forced stagger is accepted"), Boss->GetStates().GetState() == EBossState::Stagger);
	}
	else if (Phase == 1)
	{
		Test->TestTrue(TEXT("still staggered 0.6 s in"), Boss->GetStates().GetState() == EBossState::Stagger);
	}
	else
	{
		Test->TestTrue(TEXT("over by 2 s"), Boss->GetStates().GetState() != EBossState::Stagger);
	}
	return true;
}

// Watches one of his swings live (up to 20 s): his combatant's armour in each phase, and that the lunge runs.
struct FSwingWatch
{
	double Start = 0.0;
	bool bSawWindup = false, bSawActive = false, bSawRecovery = false, bSawLunge = false;
	bool bWindupArmour = true, bActiveArmour = true, bRecoveryClear = true;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FWatchBossSwing, FAutomationTestBase*, Test, TSharedRef<FSwingWatch>, Watch);
bool FWatchBossSwing::Update()
{
	FSwingWatch& W = Watch.Get();
	if (W.Start <= 0.0)
	{
		W.Start = FPlatformTime::Seconds();
	}
	UWorld* World = PlayWorld();
	ABossCharacter* Boss = nullptr;
	if (World)
	{
		if (TActorIterator<ABossCharacter> It(World); It)
		{
			Boss = *It;
		}
	}
	const UAttackComponent* Attacks = Boss ? Boss->FindComponentByClass<UAttackComponent>() : nullptr;
	if (!Attacks)
	{
		Test->AddError(TEXT("no boss attack component to watch"));
		return true;
	}
	const EArmor Armor = Boss->GetCombatant()->State.Armor;
	// Only a swing watched from its first moment counts, so the windup check can't pass by missing it.
	if (!W.bSawWindup && (!Attacks->IsAttacking() || Attacks->GetElapsed() > 0.1f))
	{
		if (FPlatformTime::Seconds() - W.Start > 20.0)
		{
			Test->AddError(TEXT("no swing seen from its start in 20 s"));
			return true;
		}
		return false;
	}
	if (Attacks->IsAttacking())
	{
		switch (Attacks->GetSample().Phase)
		{
		case EAttackPhase::Windup:
		case EAttackPhase::Hold:
			W.bSawWindup = true;
			W.bWindupArmour &= Armor == EArmor::Windup;
			break;
		case EAttackPhase::Active:
			W.bSawActive = true;
			W.bActiveArmour &= Armor == EArmor::Full;
			break;
		default:
			W.bSawRecovery = true;
			W.bRecoveryClear &= Armor == EArmor::None;
			break;
		}
		W.bSawLunge |= Attacks->IsLunging();
	}
	else if (W.bSawRecovery)
	{
		Test->TestTrue(TEXT("windup armour throughout the windup, from its first frame"), W.bSawWindup && W.bWindupArmour);
		Test->TestTrue(TEXT("full armour throughout the active window"), W.bSawActive && W.bActiveArmour);
		Test->TestTrue(TEXT("no armour in recovery"), W.bRecoveryClear);
		Test->TestTrue(TEXT("and none once the swing is over"), Armor == EArmor::None);
		Test->TestTrue(TEXT("the lunge ran"), W.bSawLunge);
		return true;
	}
	if (FPlatformTime::Seconds() - W.Start > 20.0)
	{
		Test->AddError(TEXT("no complete swing to watch in 20 s"));
		return true;
	}
	return false;
}

// Through the boss himself: a light hit that breaks his poise during the windup defers the stagger
// (he keeps swinging), and the stagger starts when his recovery does.
struct FDeferredWatch
{
	double Start = 0.0;
	bool bHit = false;
	FHitRegistry Registry;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCheckDeferredStagger, FAutomationTestBase*, Test, TSharedRef<FDeferredWatch>, Watch);
bool FCheckDeferredStagger::Update()
{
	FDeferredWatch& W = Watch.Get();
	if (W.Start <= 0.0)
	{
		W.Start = FPlatformTime::Seconds();
	}
	if (FPlatformTime::Seconds() - W.Start > 25.0)
	{
		Test->AddError(W.bHit ? TEXT("the deferred stagger never started") : TEXT("no clean windup to hit in 25 s"));
		return true;
	}
	UWorld* World = PlayWorld();
	ABossCharacter* Boss = nullptr;
	if (World)
	{
		if (TActorIterator<ABossCharacter> It(World); It)
		{
			Boss = *It;
		}
	}
	UCombatantComponent* Combatant = Boss ? Boss->FindComponentByClass<UCombatantComponent>() : nullptr;
	const UAttackComponent* Attacks = Boss ? Boss->FindComponentByClass<UAttackComponent>() : nullptr;
	if (!Combatant || !Attacks)
	{
		Test->AddError(TEXT("no boss to hit"));
		return true;
	}
	if (!W.bHit)
	{
		const FPoiseModel& Poise = Combatant->State.Poise;
		// An armoured windup (Soul Mark's cast has none, so a light hit staggers it at once).
		const bool bCleanWindup = Attacks->IsAttacking() && Attacks->GetSample().Phase == EAttackPhase::Windup && !Poise.IsImmune() && !Poise.IsStaggered()
			&& Combatant->State.Armor == EArmor::Windup;
		if (!bCleanWindup)
		{
			return false;
		}
		FCombatHitContext Hit;
		Hit.AttackInstanceId = 999999;
		Hit.Damage = 1.f;
		Hit.PoiseDamage = Poise.Threshold; // a light hit that breaks the bar on its own
		Hit.Time = World->GetTimeSeconds();
		const FHitOutcome Outcome = Combatant->ReceiveHit(Hit, W.Registry, nullptr);
		Test->TestTrue(TEXT("a light hit under windup armour defers the stagger"), Outcome.Poise == EPoiseOutcome::DeferredStagger);
		Test->TestTrue(TEXT("he keeps swinging, with the stagger waiting for the safe point"),
			Boss->GetStates().GetState() == EBossState::Attack && Boss->GetStates().HasPendingSafePoint());
		W.bHit = true;
		return false;
	}
	if (Boss->GetStates().GetState() != EBossState::Stagger)
	{
		return false;
	}
	const TArray<FBossRequestRecord>& H = Boss->GetStates().GetHistory();
	// A safe point: his recovery, or the end of a sequence step with no recovery of its own (Double Execution's first
	// hit, the Advance's dash), so a stagger earned on hit one lands before hit two (Step 5).
	const bool bSafePoint = H.Num() > 0 && H.Last().To == EBossState::Stagger
		&& (H.Last().Reason.Contains(TEXT("recovery")) || H.Last().Reason.Contains(TEXT("attack's end")) || H.Last().Reason.Contains(TEXT("between steps")));
	Test->TestTrue(FString::Printf(TEXT("the stagger started at a safe point (%s)"), H.Num() > 0 ? *H.Last().Reason : TEXT("-")), bSafePoint);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCheckNoTimeouts, FAutomationTestBase*, Test);
bool FCheckNoTimeouts::Update()
{
	UWorld* World = PlayWorld();
	if (World)
	{
		if (TActorIterator<ABossCharacter> It(World); It)
		{
			Test->TestEqual(TEXT("no boss state timed out during the whole run"), It->GetStates().GetTimeoutCount(), 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokePlayerKitInArena, "BossTest.Smoke.PlayerKitInArena", SmokeTestFlags)
bool FSmokePlayerKitInArena::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckPlayerKitInArena(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForBossHitOnPlayer(this, 0.0));
	ADD_LATENT_AUTOMATION_COMMAND(FWatchBossSwing(this, MakeShared<FSwingWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckDeferredStagger(this, MakeShared<FDeferredWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckForcedStagger(this, 0));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.6f));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckForcedStagger(this, 1));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.4f));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckForcedStagger(this, 2));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckNoTimeouts(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// The same fight in the Paragon arena test level (Content/Developers/ArenaTest, made by
// Docs/tools/setup_arena_test.py, git-ignored): the characters, the ground and the boss must behave
// in real scenery too. Skipped with a warning where the level doesn't exist (a fresh clone).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeArenaTest, "BossTest.Smoke.ArenaTestLevel", SmokeTestFlags)
bool FSmokeArenaTest::RunTest(const FString& Parameters)
{
	const FString Arena = TEXT("/Game/Developers/ArenaTest/Arena_ParagonSample");
	if (!FPackageName::DoesPackageExist(Arena))
	{
		AddWarning(TEXT("no arena test level; run Docs/tools/setup_arena_test.py"));
		return true;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Arena));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckPlayerKitInArena(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForBossHitOnPlayer(this, 0.0));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckNoTimeouts(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---- Step 4b, played live: simulated key presses through the player controller ----

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FSimulateKey, FKey, Key, bool, bPress);
bool FSimulateKey::Update()
{
	UWorld* World = PlayWorld();
	if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, bPress ? IE_Pressed : IE_Released, bPress ? 1.f : 0.f));
	}
	return true;
}

// Raises the guard once his swing starts (so the hold's drain can't empty the bar first), then checks the
// swing is blocked: 30% chip, and the block itself costing 1.5 x 35 stamina on that frame.
struct FBlockWatch
{
	double Start = 0.0;
	bool bRaised = false;
	float LastStamina = -1.f;
};

// The guard tests time their press to Executioner's Swing, so they pin him to that one action and no combos (Step 5
// gave him nine, Step 6 combos), from the moment play starts: his first pick comes as soon as his intro ends.
DEFINE_LATENT_AUTOMATION_COMMAND(FPinBossToSwing);
bool FPinBossToSwing::Update()
{
	UWorld* World = PlayWorld();
	if (World)
	{
		if (TActorIterator<ABossCharacter> It(World); It)
		{
			It->PinAction(0); // through the director: that action whenever it fits, never a combo
			return true;
		}
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FGuardNextSwing, FAutomationTestBase*, Test, TSharedRef<FBlockWatch>, Watch);
bool FGuardNextSwing::Update()
{
	FBlockWatch& W = Watch.Get();
	if (W.Start <= 0.0)
	{
		W.Start = FPlatformTime::Seconds();
	}
	UWorld* World = PlayWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCharacter* Player = PC ? Cast<APlayerCharacter>(PC->GetPawn()) : nullptr;
	ABossCharacter* Boss = nullptr;
	if (World)
	{
		if (TActorIterator<ABossCharacter> It(World); It)
		{
			Boss = *It;
		}
	}
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	// These guard tests time the press to Executioner's Swing: keep him to that one action (Step 5 gave him nine).
	Boss->PinAction(0);
	if (FPlatformTime::Seconds() - W.Start > 40.0)
	{
		Test->AddError(FString::Printf(TEXT("no blocked swing in 40 s (raised %s, player state %d)"), W.bRaised ? TEXT("yes") : TEXT("no"), static_cast<int32>(Player->GetActions().GetState())));
		return true;
	}
	if (!W.bRaised)
	{
		if (Boss->GetStates().GetState() == EBossState::Attack)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Q, IE_Pressed, 1.f)); // up in 0.18 s; his windup is 1.05 s
			W.bRaised = true;
		}
		return false;
	}
	const float Stamina = Player->GetActions().Stamina.Current;
	const float Health = Player->GetCombatant()->State.Health.Current;
	if (Health < 300.f)
	{
		Test->TestEqual(TEXT("the guard took his swing: only the 30% chip got through"), Health, 300.f - 35.f * 0.3f, 0.01f);
		Test->TestTrue(TEXT("and it was reported as a block"), Player->LastEventText().StartsWith(TEXT("BLOCKED")));
		Test->TestEqual(TEXT("and the block itself cost 1.5 x 35 stamina, on that frame"), W.LastStamina - Stamina, 52.5f, 3.f);
		return true;
	}
	W.LastStamina = Stamina;
	return false;
}

// Holding RMB through the heavy's windup turns it into the charged heavy at the branch point.
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCheckCharged, FAutomationTestBase*, Test);
bool FCheckCharged::Update()
{
	UWorld* World = PlayWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCharacter* Player = PC ? Cast<APlayerCharacter>(PC->GetPawn()) : nullptr;
	const UAttackDefinition* Current = Player && Player->GetAttacks() ? Player->GetAttacks()->GetCurrent() : nullptr;
	Test->TestTrue(FString::Printf(TEXT("holding RMB charged the heavy (current attack: %s)"), Current ? *Current->GetName() : TEXT("none")),
		Current && Current->GetName() == TEXT("DA_ChargedHeavy"));
	// The charge crossfades to its own clip (Kwang's Ability_R, Step 4e) and restarts its timeline at the 0.15 s branch.
	if (Current && Player->GetAttacks())
	{
		Test->TestTrue(FString::Printf(TEXT("its timeline restarted at the 0.15 s switch (%.2f s in, 0.7 s after the press)"), Player->GetAttacks()->GetElapsed()), Player->GetAttacks()->GetElapsed() < 0.65f);
		const UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
		const UAnimMontage* Playing = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		const UAnimSequenceBase* Clip = Playing && Playing->SlotAnimTracks.Num() > 0 && Playing->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0
			? Playing->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() : nullptr;
		Test->TestTrue(FString::Printf(TEXT("and Kwang's Ability_R is what's playing (%s)"), Clip ? *Clip->GetName() : TEXT("none")), Clip && Clip->GetName() == TEXT("Kwang_Ability_R_Mirrored"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeGuardAndCharge, "BossTest.Smoke.GuardAndCharge", SmokeTestFlags)
bool FSmokeGuardAndCharge::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FPinBossToSwing());
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FGuardNextSwing(this, MakeShared<FBlockWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FSimulateKey(EKeys::Q, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.1f));
	ADD_LATENT_AUTOMATION_COMMAND(FSimulateKey(EKeys::RightMouseButton, true));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.7f));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckCharged(this));
	ADD_LATENT_AUTOMATION_COMMAND(FSimulateKey(EKeys::RightMouseButton, false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckNoTimeouts(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---- Step 4c, played live: a perfect guard ----

// Presses guard 0.10 s before his swing's hit window opens (inside 0.04-0.18 s even a frame or two late), then
// checks the result: a perfect guard, no health lost, and the swing's 35 on his poise bar.
struct FPerfectWatch
{
	double Start = 0.0;
	bool bPressed = false;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FPerfectGuardNextSwing, FAutomationTestBase*, Test, TSharedRef<FPerfectWatch>, Watch);
bool FPerfectGuardNextSwing::Update()
{
	FPerfectWatch& W = Watch.Get();
	if (W.Start <= 0.0)
	{
		W.Start = FPlatformTime::Seconds();
	}
	UWorld* World = PlayWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCharacter* Player = PC ? Cast<APlayerCharacter>(PC->GetPawn()) : nullptr;
	ABossCharacter* Boss = nullptr;
	if (World)
	{
		if (TActorIterator<ABossCharacter> It(World); It)
		{
			Boss = *It;
		}
	}
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	// These guard tests time the press to Executioner's Swing: keep him to that one action (Step 5 gave him nine).
	Boss->PinAction(0);
	if (FPlatformTime::Seconds() - W.Start > 40.0)
	{
		Test->AddError(FString::Printf(TEXT("no guarded swing in 40 s (pressed %s, last event \"%s\")"), W.bPressed ? TEXT("yes") : TEXT("no"), *Player->LastEventText()));
		return true;
	}
	if (!W.bPressed)
	{
		const UAttackComponent* Attacks = Boss->GetAttacks();
		const UAttackDefinition* Swing = Attacks ? Attacks->GetCurrent() : nullptr;
		if (Swing && Attacks->GetElapsed() >= Swing->Windup - 0.10f)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Q, IE_Pressed, 1.f));
			W.bPressed = true;
		}
		return false;
	}
	const FString Event = Player->LastEventText();
	if (Event.StartsWith(TEXT("PERFECT")) || Event.StartsWith(TEXT("BLOCKED")) || Event.StartsWith(TEXT("HIT")))
	{
		Test->TestTrue(FString::Printf(TEXT("pressed 0.10 s before his swing landed: a perfect guard (event \"%s\")"), *Event), Event.StartsWith(TEXT("PERFECT")));
		Test->TestEqual(TEXT("no health lost"), Player->GetCombatant()->State.Health.Current, 300.f, 0.01f);
		Test->TestEqual(TEXT("his poise took the swing's 35"), Boss->GetCombatant()->State.Poise.Current(), 35.f, 0.5f);
		return true;
	}
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokePerfectGuard, "BossTest.Smoke.PerfectGuard", SmokeTestFlags)
bool FSmokePerfectGuard::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FPinBossToSwing());
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FPerfectGuardNextSwing(this, MakeShared<FPerfectWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FSimulateKey(EKeys::Q, false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckNoTimeouts(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---- Death: the clips end mid-fall, so a ragdoll finishes it; R puts both fighters back ----

struct FDeathWatch
{
	double Start = 0.0;
	bool bPlayerDead = false;
	bool bBossDead = false;
	float FloorZ = 0.f;
	FVector PlayerMeshRelative = FVector::ZeroVector;
	FVector BossMeshRelative = FVector::ZeroVector;
};

namespace
{
	APlayerCharacter* SmokePlayer(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? Cast<APlayerCharacter>(PC->GetPawn()) : nullptr;
	}

	ABossCharacter* SmokeBoss(UWorld* World)
	{
		if (World)
		{
			if (TActorIterator<ABossCharacter> It(World); It)
			{
				return *It;
			}
		}
		return nullptr;
	}

	float HeadAboveFloor(const ACharacter* Character, float FloorZ)
	{
		return Character->GetMesh()->GetSocketLocation(TEXT("head")).Z - FloorZ;
	}
}

// Kills both fighters with one lethal hit each (retrying while the boss's intro keeps him immune).
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FKillBoth, FAutomationTestBase*, Test, TSharedRef<FDeathWatch>, Watch);
bool FKillBoth::Update()
{
	FDeathWatch& W = Watch.Get();
	if (W.Start <= 0.0)
	{
		W.Start = FPlatformTime::Seconds();
	}
	UWorld* World = PlayWorld();
	APlayerCharacter* Player = SmokePlayer(World);
	ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	if (FPlatformTime::Seconds() - W.Start > 20.0)
	{
		Test->AddError(TEXT("couldn't kill both fighters in 20 s"));
		return true;
	}
	if (W.FloorZ == 0.f)
	{
		W.FloorZ = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		W.PlayerMeshRelative = Player->GetMesh()->GetRelativeLocation();
		W.BossMeshRelative = Boss->GetMesh()->GetRelativeLocation();
	}
	FCombatHitContext Lethal;
	Lethal.Damage = 100000.f;
	FHitRegistry Registry;
	if (!W.bPlayerDead)
	{
		Lethal.AttackInstanceId = 900001;
		W.bPlayerDead = Player->FindComponentByClass<UCombatantComponent>()->ReceiveHit(Lethal, Registry, Boss).bKilled;
	}
	if (!W.bBossDead)
	{
		Lethal.AttackInstanceId = 900002;
		W.bBossDead = Boss->FindComponentByClass<UCombatantComponent>()->ReceiveHit(Lethal, Registry, Player).bKilled;
	}
	return W.bPlayerDead && W.bBossDead;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCheckBodiesOnFloor, FAutomationTestBase*, Test, TSharedRef<FDeathWatch>, Watch);
bool FCheckBodiesOnFloor::Update()
{
	const FDeathWatch& W = Watch.Get();
	UWorld* World = PlayWorld();
	const APlayerCharacter* Player = SmokePlayer(World);
	const ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	Test->TestTrue(TEXT("Feng Mao's body is a ragdoll after his death clip"), Player->GetMesh()->IsSimulatingPhysics());
	Test->TestTrue(FString::Printf(TEXT("and his head lies near the floor (%.0f cm up)"), HeadAboveFloor(Player, W.FloorZ)), HeadAboveFloor(Player, W.FloorZ) < 45.f);
	Test->TestTrue(TEXT("Sevarog's body is a ragdoll after his"), Boss->GetMesh()->IsSimulatingPhysics());
	Test->TestTrue(FString::Printf(TEXT("and his head lies near the floor (%.0f cm up)"), HeadAboveFloor(Boss, W.FloorZ)), HeadAboveFloor(Boss, W.FloorZ) < 80.f);
	// In one piece: a ragdoll with missing joints scatters its limbs (Feng Mao's head once landed 30 m from his pelvis).
	for (const ACharacter* C : { static_cast<const ACharacter*>(Player), static_cast<const ACharacter*>(Boss) })
	{
		const USkeletalMeshComponent* Mesh = C->GetMesh();
		const float HeadToPelvis = FVector::Dist(Mesh->GetSocketLocation(TEXT("head")), Mesh->GetSocketLocation(TEXT("pelvis")));
		const float Limit = C == Player ? 120.f : 200.f;
		Test->TestTrue(FString::Printf(TEXT("%s: the body stays in one piece (head %.0f cm from the pelvis)"), *C->GetName(), HeadToPelvis), HeadToPelvis < Limit);
		const UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
		const FString Want = C == Player ? TEXT("PA_FengMaoRagdoll") : TEXT("PA_SevarogRagdoll");
		Test->TestTrue(FString::Printf(TEXT("%s: on the generated ragdoll asset (%s)"), *C->GetName(), Asset ? *Asset->GetName() : TEXT("none")), Asset && Asset->GetName() == Want);
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCheckBodiesRestored, FAutomationTestBase*, Test, TSharedRef<FDeathWatch>, Watch);
bool FCheckBodiesRestored::Update()
{
	const FDeathWatch& W = Watch.Get();
	UWorld* World = PlayWorld();
	const APlayerCharacter* Player = SmokePlayer(World);
	const ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	for (const ACharacter* C : { static_cast<const ACharacter*>(Player), static_cast<const ACharacter*>(Boss) })
	{
		const USkeletalMeshComponent* Mesh = C->GetMesh();
		Test->TestFalse(FString::Printf(TEXT("%s: R ends the ragdoll"), *C->GetName()), Mesh->IsSimulatingPhysics());
		Test->TestTrue(FString::Printf(TEXT("%s: the mesh is back on the capsule"), *C->GetName()), Mesh->GetAttachParent() == C->GetCapsuleComponent());
		const FVector Want = C == Player ? W.PlayerMeshRelative : W.BossMeshRelative;
		Test->TestTrue(FString::Printf(TEXT("%s: in its old place"), *C->GetName()), Mesh->GetRelativeLocation().Equals(Want, 1.f));
		Test->TestEqual(FString::Printf(TEXT("%s: its collision profile is back"), *C->GetName()), Mesh->GetCollisionProfileName(), FName(TEXT("CharacterMesh")));
	}
	const UPhysicsAsset* BossPhysics = Boss->GetMesh()->GetPhysicsAsset();
	Test->TestTrue(FString::Printf(TEXT("Sevarog is back on the Paragon physics asset (%s)"), BossPhysics ? *BossPhysics->GetName() : TEXT("none")),
		BossPhysics && BossPhysics->GetName() == TEXT("Sevarog_Extents"));
	const UPhysicsAsset* PlayerPhysics = Player->GetMesh()->GetPhysicsAsset();
	Test->TestTrue(FString::Printf(TEXT("Feng Mao is back on his Paragon physics asset (%s)"), PlayerPhysics ? *PlayerPhysics->GetName() : TEXT("none")),
		PlayerPhysics && PlayerPhysics->GetName() == TEXT("FengMao_Extents"));
	Test->TestTrue(FString::Printf(TEXT("Feng Mao stands again (head %.0f cm up)"), HeadAboveFloor(Player, W.FloorZ)), HeadAboveFloor(Player, W.FloorZ) > 140.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeDeathRagdoll, "BossTest.Smoke.DeathRagdollAndReset", SmokeTestFlags)
bool FSmokeDeathRagdoll::RunTest(const FString& Parameters)
{
	const TSharedRef<FDeathWatch> Watch = MakeShared<FDeathWatch>();
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FKillBoth(this, Watch));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckBodiesOnFloor(this, Watch));
	ADD_LATENT_AUTOMATION_COMMAND(FSimulateKey(EKeys::R, true));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FSimulateKey(EKeys::R, false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckBodiesRestored(this, Watch));
	// A second death after the reset ragdolls just the same.
	const TSharedRef<FDeathWatch> Again = MakeShared<FDeathWatch>();
	ADD_LATENT_AUTOMATION_COMMAND(FKillBoth(this, Again));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckBodiesOnFloor(this, Again));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---- Step 4d, played live: light, light, heavy from early presses ----

// Presses LMB, LMB again once Light 1 is running, then RMB once Light 2 is (released at once, so no charge),
// and checks the heavy comes out of Light 2's chain point.
struct FChainWatch
{
	double Start = 0.0;
	int32 Step = 0;
	FString Seen;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FPlayLightLightHeavy, FAutomationTestBase*, Test, TSharedRef<FChainWatch>, Watch);
bool FPlayLightLightHeavy::Update()
{
	FChainWatch& W = Watch.Get();
	if (W.Start <= 0.0)
	{
		W.Start = FPlatformTime::Seconds();
	}
	UWorld* World = PlayWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCharacter* Player = SmokePlayer(World);
	if (!PC || !Player)
	{
		Test->AddError(TEXT("no player"));
		return true;
	}
	if (FPlatformTime::Seconds() - W.Start > 6.0)
	{
		Test->AddError(FString::Printf(TEXT("light, light, heavy never got to the heavy (attacks seen: %s)"), *W.Seen));
		return true;
	}
	const UAttackDefinition* Current = Player->GetAttacks() ? Player->GetAttacks()->GetCurrent() : nullptr;
	const FString Name = Current ? Current->GetName() : FString();
	if (!Name.IsEmpty() && !W.Seen.EndsWith(Name))
	{
		W.Seen += W.Seen.IsEmpty() ? Name : TEXT(" > ") + Name;
	}
	auto Tap = [PC](const FKey& Key)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
	};
	switch (W.Step)
	{
	case 0:
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
		W.Step = 1;
		return false;
	case 1:
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0.f));
		if (Name == TEXT("DA_Light1"))
		{
			Tap(EKeys::LeftMouseButton); // early: held until Light 1's chain point
			W.Step = 2;
		}
		return false;
	case 2:
		if (Name == TEXT("DA_Light2"))
		{
			Tap(EKeys::RightMouseButton); // early: held until Light 2's chain point
			W.Step = 3;
		}
		return false;
	default:
		if (Name == TEXT("DA_Heavy"))
		{
			Test->TestTrue(FString::Printf(TEXT("light, light, heavy came out in order (%s)"), *W.Seen), W.Seen == TEXT("DA_Light1 > DA_Light2 > DA_Heavy"));
			return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeLightLightHeavy, "BossTest.Smoke.LightLightHeavy", SmokeTestFlags)
bool FSmokeLightLightHeavy::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FPlayLightLightHeavy(this, MakeShared<FChainWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---- Step 4e, played live: the borrowed moves play their borrowed clips ----

// Light, light, light gives Light 3 on Aurora's clip; once free, heavy, heavy gives heavy 2 on Kwang's.
struct FBorrowWatch
{
	double Start = 0.0;
	int32 Step = 0;
	bool bLight3 = false;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FPlayBorrowedMoves, FAutomationTestBase*, Test, TSharedRef<FBorrowWatch>, Watch);
bool FPlayBorrowedMoves::Update()
{
	FBorrowWatch& W = Watch.Get();
	if (W.Start <= 0.0)
	{
		W.Start = FPlatformTime::Seconds();
	}
	UWorld* World = PlayWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCharacter* Player = SmokePlayer(World);
	if (!PC || !Player)
	{
		Test->AddError(TEXT("no player"));
		return true;
	}
	if (FPlatformTime::Seconds() - W.Start > 10.0)
	{
		Test->AddError(FString::Printf(TEXT("the borrowed moves didn't all come out (step %d)"), W.Step));
		return true;
	}
	const UAttackDefinition* Current = Player->GetAttacks() ? Player->GetAttacks()->GetCurrent() : nullptr;
	const FString Name = Current ? Current->GetName() : FString();
	const FString Clip = Current && Current->Clip ? Current->Clip->GetName() : FString();
	auto Tap = [PC](const FKey& Key)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
	};
	switch (W.Step)
	{
	case 0: Tap(EKeys::LeftMouseButton); W.Step = 1; return false;
	case 1: if (Name == TEXT("DA_Light1")) { Tap(EKeys::LeftMouseButton); W.Step = 2; } return false;
	case 2: if (Name == TEXT("DA_Light2")) { Tap(EKeys::LeftMouseButton); W.Step = 3; } return false;
	case 3:
		if (Name == TEXT("DA_Light3"))
		{
			Test->TestEqual(TEXT("Light 3 plays Aurora's rising backhand"), Clip, FString(TEXT("Aurora_Primary_Attack_Slow_C_Mirrored")));
			W.Step = 4;
		}
		return false;
	case 4: if (Name.IsEmpty()) { Tap(EKeys::RightMouseButton); W.Step = 5; } return false; // free again
	case 5: if (Name == TEXT("DA_Heavy")) { Tap(EKeys::RightMouseButton); W.Step = 6; } return false;
	default:
		if (Name == TEXT("DA_Heavy2"))
		{
			Test->TestEqual(TEXT("heavy 2 plays Kwang's uppercut"), Clip, FString(TEXT("Kwang_PrimaryAttack_A_Slow_Mirrored")));
			return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeBorrowedMoves, "BossTest.Smoke.BorrowedMovesPlay", SmokeTestFlags)
bool FSmokeBorrowedMoves::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FPlayBorrowedMoves(this, MakeShared<FBorrowWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---- Step 5, played live: his phase-one actions ----

namespace
{
	// Puts the player Edge cm (edge to edge) from the boss, AngleDeg off his facing.
	void PlaceSmokePlayer(APlayerCharacter* Player, const ABossCharacter* Boss, float Edge, float AngleDeg)
	{
		const float Radii = Boss->GetCapsuleComponent()->GetScaledCapsuleRadius() + Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const FVector Dir = Boss->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(AngleDeg, FVector::UpVector);
		FVector At = Boss->GetActorLocation() + Dir * (Edge + Radii);
		At.Z = Player->GetActorLocation().Z;
		Player->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		Player->GetCharacterMovement()->StopMovementImmediately();
	}

	float SmokeEdge(const ACharacter* A, const ACharacter* B)
	{
		return FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) - A->GetCapsuleComponent()->GetScaledCapsuleRadius() - B->GetCapsuleComponent()->GetScaledCapsuleRadius();
	}

	// Past the intro with his AI off, so only forced actions run.
	bool SmokeBossReady(ABossCharacter* Boss)
	{
		const EBossState S = Boss->GetStates().GetState();
		if (S == EBossState::Intro)
		{
			return false;
		}
		if (Boss->bAIEnabled)
		{
			Boss->ToggleAI();
		}
		return S == EBossState::Paused || S == EBossState::Evaluate || S == EBossState::Neutral;
	}
}

struct FActionsWatch
{
	double Start = 0.0;
	double ActionStart = 0.0;
	int32 Index = 0;
	bool bForced = false;
	bool bSawActive = false;
	bool bSawDelivery = false;
	int32 Timeouts = 0;
};

// Forces each action in turn with the player (immune, so nothing ends early) placed inside its range, and checks it
// reaches its hit window and ends by itself, without a timeout.
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FForceEachAction, FAutomationTestBase*, Test, TSharedRef<FActionsWatch>, Watch);
bool FForceEachAction::Update()
{
	FActionsWatch& W = Watch.Get();
	UWorld* World = PlayWorld();
	APlayerCharacter* Player = SmokePlayer(World);
	ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	if (W.Start <= 0.0)
	{
		W.Start = FPlatformTime::Seconds();
		W.Timeouts = Boss->GetStates().GetTimeoutCount();
	}
	if (FPlatformTime::Seconds() - W.Start > 90.0)
	{
		Test->AddError(FString::Printf(TEXT("ran out of time at action %d"), W.Index));
		return true;
	}
	if (W.Index >= Boss->GetActionCount())
	{
		Test->TestEqual(TEXT("nine actions"), Boss->GetActionCount(), 9);
		Test->TestEqual(TEXT("no timeouts"), Boss->GetStates().GetTimeoutCount(), W.Timeouts);
		return true;
	}
	if (!W.bForced)
	{
		if (!SmokeBossReady(Boss))
		{
			return false;
		}
		Player->FindComponentByClass<UCombatantComponent>()->State.bImmune = true;
		const float Edges[] = { 250.f, 250.f, 200.f, 250.f, 250.f, 200.f, 450.f, 500.f, 700.f };
		PlaceSmokePlayer(Player, Boss, Edges[FMath::Min(W.Index, 8)], 0.f);
		W.bForced = Boss->ForceAction(W.Index);
		W.bSawActive = W.bSawDelivery = false;
		W.ActionStart = FPlatformTime::Seconds();
		return false;
	}
	const EBossState S = Boss->GetStates().GetState();
	if (S == EBossState::Attack)
	{
		W.bSawActive |= Boss->GetAttacks()->GetSample().Phase == EAttackPhase::Active;
		W.bSawDelivery |= Boss->GetDelivery()->GetLiveCount() > 0;
		if (FPlatformTime::Seconds() - W.ActionStart > 8.0)
		{
			Test->AddError(FString::Printf(TEXT("action %d never ended"), W.Index));
			return true;
		}
		return false;
	}
	const FString Name = Boss->GetActionCount() > W.Index ? FString::FromInt(W.Index) : TEXT("?");
	Test->TestTrue(FString::Printf(TEXT("action %s reached its hit window"), *Name), W.bSawActive);
	if (W.Index == 6 || W.Index == 7)
	{
		Test->TestTrue(FString::Printf(TEXT("action %s launched its delivery"), *Name), W.bSawDelivery);
	}
	++W.Index;
	W.bForced = false;
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeBossActions, "BossTest.Smoke.BossActions", SmokeTestFlags)
bool FSmokeBossActions::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FForceEachAction(this, MakeShared<FActionsWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

struct FDeliveryWatch
{
	double Start = 0.0;
	double PhaseStart = 0.0;
	int32 Phase = 0;
	float Health = 0.f;
};

// The ring hits a player standing beyond the hammer's reach for 35, without a knockdown; Soul Mark's grasp takes 30
// and a fragment; a reset with a ring in flight leaves nothing behind and no hit afterwards.
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FDeliveriesLive, FAutomationTestBase*, Test, TSharedRef<FDeliveryWatch>, Watch);
bool FDeliveriesLive::Update()
{
	FDeliveryWatch& W = Watch.Get();
	UWorld* World = PlayWorld();
	APlayerCharacter* Player = SmokePlayer(World);
	ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (W.Start <= 0.0)
	{
		W.Start = Now;
	}
	if (Now - W.Start > 60.0)
	{
		Test->AddError(FString::Printf(TEXT("ran out of time in phase %d"), W.Phase));
		return true;
	}
	const FCombatantState& P = Player->GetCombatant()->State;
	const UDeliveryComponent* Delivery = Boss->GetDelivery();
	auto Force = [&](int32 Action, float Edge)
	{
		PlaceSmokePlayer(Player, Boss, Edge, 0.f);
		W.Health = P.Health.Current;
		W.PhaseStart = Now;
		return Boss->ForceAction(Action);
	};
	switch (W.Phase)
	{
	case 0: // Judgment Slam from 7.5 m: the hammer can't reach, the ring can
		if (SmokeBossReady(Boss) && Force(6, 750.f))
		{
			W.Phase = 1;
		}
		return false;
	case 1:
		if (Boss->GetStates().GetState() == EBossState::Attack || Delivery->GetLiveCount() > 0)
		{
			return false;
		}
		Test->TestEqual(TEXT("the ring hit for 35"), W.Health - P.Health.Current, 35.f, 0.5f);
		W.Phase = 2;
		return false;
	case 2: // Soul Mark from 6 m
		if (SmokeBossReady(Boss) && Now - W.PhaseStart > 2.0 && Force(7, 600.f))
		{
			W.Phase = 3;
		}
		return false;
	case 3:
		if (Boss->GetStates().GetState() == EBossState::Attack || Delivery->GetLiveCount() > 0)
		{
			return false;
		}
		Test->TestEqual(TEXT("the soul grasp hit for 30"), W.Health - P.Health.Current, 30.f, 0.5f);
		Test->TestEqual(TEXT("and took a fragment"), P.Health.Fragments, 1);
		W.Phase = 4;
		return false;
	case 4: // Judgment Slam again; reset the boss while the ring is out
		if (SmokeBossReady(Boss) && Now - W.PhaseStart > 2.0 && Force(6, 750.f))
		{
			W.Phase = 5;
		}
		return false;
	case 5:
		if (Delivery->GetLiveCount() == 0)
		{
			if (Now - W.PhaseStart > 6.0)
			{
				Test->AddError(TEXT("the second ring never launched"));
				return true;
			}
			return false;
		}
		W.Health = P.Health.Current;
		Boss->ResetBoss();
		Test->TestEqual(TEXT("a reset clears the ring"), Delivery->GetLiveCount(), 0);
		Test->TestEqual(TEXT("and every effect it spawned"), Delivery->GetEffectCount(), 0);
		W.PhaseStart = Now;
		W.Phase = 6;
		return false;
	default:
		if (Now - W.PhaseStart < 1.0)
		{
			return false;
		}
		Test->TestEqual(TEXT("no hit after the reset"), P.Health.Current, W.Health, 0.5f);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeDeliveries, "BossTest.Smoke.Deliveries", SmokeTestFlags)
bool FSmokeDeliveries::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FDeliveriesLive(this, MakeShared<FDeliveryWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

struct FAdvanceWatch
{
	double Start = 0.0;
	double Forced = 0.0;
	float StartEdge = 0.f;
};

// The Advance from 120 degrees off his facing (behind his side): the dash still closes the distance.
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FAdvanceFromBehind, FAutomationTestBase*, Test, TSharedRef<FAdvanceWatch>, Watch);
bool FAdvanceFromBehind::Update()
{
	FAdvanceWatch& W = Watch.Get();
	UWorld* World = PlayWorld();
	APlayerCharacter* Player = SmokePlayer(World);
	ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (W.Start <= 0.0)
	{
		W.Start = Now;
	}
	if (Now - W.Start > 30.0)
	{
		Test->AddError(TEXT("the Advance never started"));
		return true;
	}
	if (W.Forced <= 0.0)
	{
		if (!SmokeBossReady(Boss))
		{
			return false;
		}
		Player->FindComponentByClass<UCombatantComponent>()->State.bImmune = true;
		PlaceSmokePlayer(Player, Boss, 700.f, 120.f);
		W.StartEdge = SmokeEdge(Boss, Player);
		if (Boss->ForceAction(8))
		{
			W.Forced = Now;
		}
		return false;
	}
	if (Now - W.Forced < 0.7)
	{
		return false; // the coil and the dash (0.50 s), with a little slack
	}
	const float Edge = SmokeEdge(Boss, Player);
	Test->TestTrue(FString::Printf(TEXT("the dash closed from %.0f to %.0f cm"), W.StartEdge, Edge), Edge < 350.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeAdvanceFromBehind, "BossTest.Smoke.AdvanceFromBehind", SmokeTestFlags)
bool FSmokeAdvanceFromBehind::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FAdvanceFromBehind(this, MakeShared<FAdvanceWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---- Step 6, played live: his director ----

struct FDirectorWatch
{
	double Start = 0.0;
	double NextMove = 0.0;
	int32 Spot = 0;
	int32 Timeouts = 0;
	bool bCombo = false;
	bool bCircle = false;
	bool bBreather = false;
	float MaxFromCentre = 0.f;
};

// A minute of fighting with an immune player moved between the bands every 5 s: he fights in combos, circles, takes his
// breather, stays inside the 13 m disc, and nothing times out.
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FDirectorFights, FAutomationTestBase*, Test, TSharedRef<FDirectorWatch>, Watch);
bool FDirectorFights::Update()
{
	FDirectorWatch& W = Watch.Get();
	UWorld* World = PlayWorld();
	APlayerCharacter* Player = SmokePlayer(World);
	ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (W.Start <= 0.0)
	{
		W.Start = Now;
		W.Timeouts = Boss->GetStates().GetTimeoutCount();
		Player->FindComponentByClass<UCombatantComponent>()->State.bImmune = true;
	}
	const EBossState S = Boss->GetStates().GetState();
	W.bCombo |= Boss->GetCurrentCombo() >= 0 && Boss->GetAttacks()->IsAttacking();
	W.bCircle |= S == EBossState::Circle;
	W.bBreather |= S == EBossState::Neutral && Boss->GetPauseLength() >= 0.99f;
	W.MaxFromCentre = FMath::Max(W.MaxFromCentre, static_cast<float>(FVector::Dist2D(Boss->GetActorLocation(), Boss->GetArenaCentre())));
	if (Now >= W.NextMove)
	{
		// Close, medium, far, close at his side, extreme: in front of him, so every band and both angle rules come up.
		const float Edges[] = { 150.f, 450.f, 800.f, 200.f, 1100.f };
		const float Angles[] = { 0.f, 20.f, 0.f, 70.f, 0.f };
		PlaceSmokePlayer(Player, Boss, Edges[W.Spot % 5], Angles[W.Spot % 5]);
		// Never outside the arena: 6b's wall keeps you in; his own movement is what this checks (his dash and lunges
		// follow you, and only get the boundary clamp with that wall).
		const FVector Centre = Boss->GetArenaCentre();
		FVector At = Player->GetActorLocation();
		const FVector Flat(At.X - Centre.X, At.Y - Centre.Y, 0.f);
		if (Flat.Size() > 1200.f)
		{
			At = FVector(Centre.X, Centre.Y, At.Z) + Flat.GetSafeNormal() * 1200.f;
			Player->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		}
		++W.Spot;
		W.NextMove = Now + 5.0;
	}
	if (Now - W.Start < 60.0)
	{
		return false;
	}
	Test->TestEqual(TEXT("no state timed out"), Boss->GetStates().GetTimeoutCount(), W.Timeouts);
	Test->TestTrue(TEXT("at least one combo"), W.bCombo);
	Test->TestTrue(TEXT("he circled"), W.bCircle);
	Test->TestTrue(TEXT("he took a breather"), W.bBreather);
	Test->TestTrue(FString::Printf(TEXT("he stayed inside the disc (%.0f cm at most; 13 m plus a lunge's reach)"), W.MaxFromCentre), W.MaxFromCentre <= 1300.f + 250.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeDirectorFights, "BossTest.Smoke.DirectorFights", SmokeTestFlags)
bool FSmokeDirectorFights::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FDirectorFights(this, MakeShared<FDirectorWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

struct FComboWatch
{
	double Start = 0.0;
	int32 Phase = 0;
	bool bForced = false;
	float OpenerElapsed = 0.f;
	bool bFollowUp = false;
	bool bArmourFromStart = false;
	float LastOpenerElapsed = 0.f;
};

// Combo C (Reverse Sweep into Crushing Overhead) forced at 2 m: the follow-up starts at its chain point with windup armour
// from its first frame. Forced again with the player moved to 6 m once the sweep starts: no follow-up, and the sweep
// plays its whole recovery.
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FComboChain, FAutomationTestBase*, Test, TSharedRef<FComboWatch>, Watch);
bool FComboChain::Update()
{
	FComboWatch& W = Watch.Get();
	UWorld* World = PlayWorld();
	APlayerCharacter* Player = SmokePlayer(World);
	ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (W.Start <= 0.0)
	{
		W.Start = Now;
		Player->FindComponentByClass<UCombatantComponent>()->State.bImmune = true;
	}
	if (Now - W.Start > 40.0)
	{
		Test->AddError(FString::Printf(TEXT("ran out of time in phase %d"), W.Phase));
		return true;
	}
	const UAttackComponent* Attacks = Boss->GetAttacks();
	const FString Current = Attacks->GetCurrent() ? Attacks->GetCurrent()->GetName() : FString();
	if (!W.bForced)
	{
		if (!SmokeBossReady(Boss))
		{
			return false;
		}
		PlaceSmokePlayer(Player, Boss, 200.f, 0.f);
		const int32 C = Boss->Combos.IndexOfByPredicate([](const FBossCombo& X) { return X.Name == FName(TEXT("C")); });
		W.bForced = Boss->ForceCombo(C);
		W.bFollowUp = false;
		W.LastOpenerElapsed = 0.f;
		return false;
	}
	if (Current == TEXT("DA_ReverseSweep"))
	{
		W.LastOpenerElapsed = Attacks->GetElapsed();
		if (W.Phase == 1 && Attacks->GetElapsed() < 0.3f)
		{
			PlaceSmokePlayer(Player, Boss, 600.f, 0.f); // out of Crushing Overhead's 4 m before the chain point
		}
		return false;
	}
	if (Current == TEXT("DA_CrushingOverhead") && !W.bFollowUp)
	{
		W.bFollowUp = true;
		W.bArmourFromStart = Boss->GetCombatant()->State.Armor == EArmor::Windup && Attacks->GetElapsed() < 0.05f;
		W.OpenerElapsed = W.LastOpenerElapsed;
		return false;
	}
	if (Boss->GetStates().GetState() == EBossState::Attack)
	{
		return false;
	}
	if (W.Phase == 0)
	{
		Test->TestTrue(TEXT("at 2 m the follow-up came"), W.bFollowUp);
		Test->TestTrue(TEXT("with windup armour from its first frame"), W.bArmourFromStart);
		Test->TestTrue(FString::Printf(TEXT("at the chain point (the sweep at %.2f s; 0.16 + 0.72 + 0.20 = 1.08)"), W.OpenerElapsed),
			W.OpenerElapsed >= 1.05f && W.OpenerElapsed <= 1.13f);
		W.Phase = 1;
		W.bForced = false;
		return false;
	}
	Test->TestFalse(TEXT("at 6 m: no follow-up"), W.bFollowUp);
	Test->TestTrue(FString::Printf(TEXT("and the sweep played its whole recovery (%.2f s of 1.98)"), W.LastOpenerElapsed), W.LastOpenerElapsed >= 1.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeComboChain, "BossTest.Smoke.ComboChain", SmokeTestFlags)
bool FSmokeComboChain::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FComboChain(this, MakeShared<FComboWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

struct FHugWatch
{
	double Start = 0.0;
	double LastTick = 0.0;
	bool bAttacked = false;
	float LongestIdle = 0.f;
};

// A runner trying to stay at his back at contact range (moving at 4 m/s, a little under what holding it takes): he moves
// the backstab spot, gets an attack in within 10 s, and no state other than an attack lasts past 3 s.
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHugBack, FAutomationTestBase*, Test, TSharedRef<FHugWatch>, Watch);
bool FHugBack::Update()
{
	FHugWatch& W = Watch.Get();
	UWorld* World = PlayWorld();
	APlayerCharacter* Player = SmokePlayer(World);
	ABossCharacter* Boss = SmokeBoss(World);
	if (!Player || !Boss)
	{
		Test->AddError(TEXT("no player or boss"));
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (W.Start <= 0.0)
	{
		if (Boss->GetStates().GetState() == EBossState::Intro)
		{
			return false;
		}
		W.Start = W.LastTick = Now;
		Player->FindComponentByClass<UCombatantComponent>()->State.bImmune = true;
		PlaceSmokePlayer(Player, Boss, 30.f, 170.f);
	}
	const float Dt = static_cast<float>(Now - W.LastTick);
	W.LastTick = Now;
	// Toward the spot right behind him, at most 4 m/s.
	const float Radii = Boss->GetCapsuleComponent()->GetScaledCapsuleRadius() + Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
	FVector Spot = Boss->GetActorLocation() - Boss->GetActorForwardVector().GetSafeNormal2D() * (Radii + 30.f);
	Spot.Z = Player->GetActorLocation().Z;
	const FVector Step = (Spot - Player->GetActorLocation()).GetClampedToMaxSize(400.f * Dt);
	Player->SetActorLocation(Player->GetActorLocation() + Step, false, nullptr, ETeleportType::TeleportPhysics);
	const EBossState S = Boss->GetStates().GetState();
	W.bAttacked |= S == EBossState::Attack;
	if (S != EBossState::Attack && S != EBossState::Stagger && S != EBossState::Intro)
	{
		W.LongestIdle = FMath::Max(W.LongestIdle, Boss->GetStates().GetStateTime());
	}
	if (Now - W.Start < 10.0)
	{
		return false;
	}
	Test->TestTrue(TEXT("he got an attack in"), W.bAttacked);
	Test->TestTrue(FString::Printf(TEXT("no other state lasted past 3 s (%.2f)"), W.LongestIdle), W.LongestIdle <= 3.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeHugBack, "BossTest.Smoke.HugBack", SmokeTestFlags)
bool FSmokeHugBack::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHugBack(this, MakeShared<FHugWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
