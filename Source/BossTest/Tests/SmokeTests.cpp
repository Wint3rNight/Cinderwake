// Step 3: plays the arena in the editor (headless) and checks the player kit is wired up: the right
// pawn, controller and HUD, the starting resources, and a few seconds of play with no errors logged
// (an error-level log fails the test, so a missing asset or a broken tick order shows up here).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Boss/BossCharacter.h"
#include "Combat/AttackComponent.h"
#include "Combat/CombatantComponent.h"
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
		const bool bCleanWindup = Attacks->IsAttacking() && Attacks->GetSample().Phase == EAttackPhase::Windup && !Poise.IsImmune() && !Poise.IsStaggered();
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
	Test->TestTrue(TEXT("the stagger started at his recovery"), H.Num() > 0 && H.Last().To == EBossState::Stagger && H.Last().Reason.Contains(TEXT("recovery")));
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
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmokeGuardAndCharge, "BossTest.Smoke.GuardAndCharge", SmokeTestFlags)
bool FSmokeGuardAndCharge::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
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
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FPerfectGuardNextSwing(this, MakeShared<FPerfectWatch>()));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FSimulateKey(EKeys::Q, false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckNoTimeouts(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
