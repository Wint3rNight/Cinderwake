// The player's action rules (spec section 4) as a plain state machine: no engine code, so every
// timing rule is unit tested. Each tick it takes the frame's input and the running attack's state
// and answers with commands; APlayerCharacter carries them out (clips, pushes, the attack component).
#pragma once

#include "CoreMinimal.h"
#include "Combat/StaminaModel.h"
#include "Player/InputBuffer.h"
#include "PlayerActions.generated.h"

// Section 4's numbers. A reflected struct, so APlayerCharacter exposes them on BP_FengMao and they tune
// without a rebuild; the model itself stays plain C++.
USTRUCT(BlueprintType)
struct FPlayerTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Stamina")
	float StaminaMax = 150.f;

	UPROPERTY(EditAnywhere, Category = "Stamina", meta = (ToolTip = "Per second"))
	float StaminaRegenRate = 40.f;

	UPROPERTY(EditAnywhere, Category = "Stamina", meta = (ToolTip = "Seconds after an action chain ends"))
	float StaminaRegenDelay = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Stamina", meta = (ToolTip = "Seconds after the action that emptied the bar"))
	float ExhaustedRegenDelay = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Stamina", meta = (ToolTip = "Exhausted until stamina is back to this"))
	float ExhaustedUntil = 25.f;

	UPROPERTY(EditAnywhere, Category = "Stamina", meta = (ToolTip = "Per second while sprinting"))
	float SprintDrain = 12.f;

	UPROPERTY(EditAnywhere, Category = "Input", meta = (ToolTip = "Seconds an early press is remembered"))
	float InputBuffer = 0.18f;

	UPROPERTY(EditAnywhere, Category = "Dodge")
	float DodgeCost = 22.f;

	UPROPERTY(EditAnywhere, Category = "Dodge")
	float BackstepCost = 18.f;

	UPROPERTY(EditAnywhere, Category = "Dodge")
	float DodgeDuration = 0.72f;

	UPROPERTY(EditAnywhere, Category = "Dodge")
	float DodgeIFrames = 0.22f;

	UPROPERTY(EditAnywhere, Category = "Dodge", meta = (ToolTip = "Another dodge or an attack from here; moving waits for the end"))
	float DodgeLock = 0.50f;

	UPROPERTY(EditAnywhere, Category = "Dodge", meta = (ToolTip = "A light or heavy coming out in this window after a dodge started is a dodge attack: the quick combo or the leaping slam"))
	float DodgeAttackFrom = 0.50f;

	UPROPERTY(EditAnywhere, Category = "Dodge")
	float DodgeAttackUntil = 0.90f;

	UPROPERTY(EditAnywhere, Category = "Reactions")
	float HitReaction = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Reactions", meta = (ToolTip = "Invulnerable throughout"))
	float Knockdown = 1.4f;

	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealCast = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Heal", meta = (ToolTip = "After an interrupted heal"))
	float HealRetryDelay = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealAmount = 120.f;

	UPROPERTY(EditAnywhere, Category = "Heal")
	int32 HealCharges = 2;

	UPROPERTY(EditAnywhere, Category = "Charged heavy", meta = (ToolTip = "Stamina on top of the heavy's own cost"))
	float ChargeExtraCost = 12.f;

	UPROPERTY(EditAnywhere, Category = "Sprint attack", meta = (ToolTip = "Seconds of sprinting before a light becomes the sprint attack"))
	float SprintAttackAfter = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Guard", meta = (ToolTip = "Seconds to raise the guard before it blocks"))
	float GuardRaiseTime = 0.18f;

	UPROPERTY(EditAnywhere, Category = "Guard", meta = (ToolTip = "Stamina per second while held; no regeneration"))
	float GuardDrain = 10.f;

	UPROPERTY(EditAnywhere, Category = "Guard", meta = (ToolTip = "Stamina per point of a blocked hit's raw damage"))
	float BlockCostPerDamage = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Guard")
	float BlockStun = 0.32f;

	UPROPERTY(EditAnywhere, Category = "Guard", meta = (ToolTip = "Block-stun for a hit that would knock down"))
	float HeavyBlockStun = 0.70f;

	UPROPERTY(EditAnywhere, Category = "Guard", meta = (ToolTip = "The stagger after a block overdraws stamina; the exhaustion clock starts at the break"))
	float GuardBreakStun = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Guard", meta = (ToolTip = "Share of a blocked hit's damage stopped; the rest is chip"))
	float GuardReduction = 0.7f;

	UPROPERTY(EditAnywhere, Category = "Guard", meta = (ToolTip = "Degrees from his front the guard covers"))
	float GuardArc = 60.f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "A blockable hit this long after a fresh guard press, up to Perfect Guard Until, is a perfect guard; sooner is a hit"))
	float PerfectGuardFrom = 0.04f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "Later than this after the press is a normal block"))
	float PerfectGuardUntil = 0.18f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "After a whiffed window (no hit, or released early), presses open no window for this long from the window's end; any hit on you clears it"))
	float PerfectGuardLockout = 0.80f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "Stamina per point of the deflected hit's raw damage"))
	float PerfectGuardCostPerDamage = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard")
	float DeflectStun = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "Deflect-stun for a hit that would knock down"))
	float HeavyDeflectStun = 0.30f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "Seconds both fighters freeze"))
	float PerfectGuardHitStop = 0.10f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "Seconds both fighters freeze on a hit that would knock down"))
	float HeavyPerfectGuardHitStop = 0.12f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "cm"))
	float DeflectPush = 15.f;

	UPROPERTY(EditAnywhere, Category = "Perfect guard", meta = (ToolTip = "cm, on a hit that would knock down"))
	float HeavyDeflectPush = 35.f;
};

struct FAttackCosts
{
	TArray<float> Lights;           // in combo order
	float Heavy = 0.f;
	TArray<float> DodgeLights;      // the quick combo out of a dodge, in order
	float DodgeHeavy = 0.f;         // the leaping slam out of a dodge
	float SprintAttack = 0.f;
};

enum class EPlayerState : uint8 { Free, Attacking, Dodging, Healing, HitReact, Knockdown, Guard, BlockStun, GuardBroken, Dead };

enum class EPlayerCommand : uint8
{
	StartLight,         // GetLightIndex() says which
	StartHeavy,
	StartDodgeLight,    // the quick combo out of a dodge; GetLightIndex() says which hit
	StartDodgeHeavy,    // the leaping slam out of a dodge
	StartDodge,         // toward the held direction
	StartBackstep,      // no direction held
	EndDodge,           // remove the rest of the dodge's push (an action took over)
	StopAttack,
	StartHeal,
	CancelHeal,         // interrupted: the charge is kept
	FinishHeal,         // the heal lands: add Tuning.HealAmount
	StartSprint,
	StopSprint,
	PlayHitReact,
	PlayKnockdown,
	PlayDeath,
	BeginCharge,        // still holding RMB at the heavy's cocked pose: switch to the charged heavy
	StartSprintAttack,
	StartGuard,
	StopGuard,
	PlayBlock,
	PlayHeavyBlock,     // a blocked hit that would have knocked down
	PlayGuardBreak,
	EndGuardBreak,      // the break's stagger is over: release its held pose
	PlayDeflect,        // a perfect guard
	PlayHeavyDeflect    // a perfect guard on a hit that would have knocked down
};

using FPlayerCommands = TArray<EPlayerCommand, TInlineAllocator<4>>;

struct FPlayerFrame
{
	float Dt = 0.f;                 // the actor's own (dilated) time, so hit-stop pauses every clock here
	bool bMoveHeld = false;
	bool bSprintHeld = false;
	bool bAttackRunning = false;    // UAttackComponent::IsAttacking()
	bool bChainOpen = false;        // the running attack's sample
	bool bHeavyHeld = false;        // RMB is down (the charge)
	bool bGuardHeld = false;        // the guard key is down
	bool bGuardPressed = false;     // ...and went down this frame (only a fresh press opens a perfect window)
	bool bAtChargeBranch = false;   // the heavy is held at its cocked pose, waiting for the charge decision (UAttackComponent::IsAtBranch)
};

class BOSSTEST_API FPlayerActions
{
public:
	FPlayerTuning Tuning;     // applied to the stamina model and the buffer by Reset()
	FAttackCosts Costs;
	FStaminaModel Stamina;
	FInputBuffer Buffer;

	// A light pressed during a light attack, or a second one while one is still waiting (a double-click
	// during a dodge), is held for the chain point, however early it comes.
	void Press(EPlayerInput Input);
	FPlayerCommands Tick(const FPlayerFrame& Frame);
	FPlayerCommands OnHit(bool bKnockdown);   // a hit that landed (dodged hits never get here)
	FPlayerCommands OnDeath();
	// A hit the guard stopped (the resolver said Blocked): costs stamina, block-stun, or a guard break.
	FPlayerCommands OnBlock(float RawDamage, bool bHeavyBlock);
	// A hit the resolver called a perfect guard: cheap in stamina, a short deflect-stun, never a guard break.
	FPlayerCommands OnPerfectGuard(float RawDamage, bool bKnockdownHit);
	void Reset();                             // full stamina and charges, free, empty buffer

	EPlayerState GetState() const { return State; }
	float GetStateTime() const { return StateTime; }
	int32 GetLightIndex() const { return LightIndex; }
	int32 GetHealCharges() const { return HealCharges; }
	float GetHealProgress() const;            // 0-1 while healing
	bool IsInvulnerable() const;
	bool IsGuardActive() const;               // raised (Guard from GuardRaiseTime) or in block-stun
	bool HasPerfectWindow() const;            // a fresh press raised the guard and its window hasn't passed
	bool IsSprinting() const { return bSprinting; }
	bool CanSteer() const { return State == EPlayerState::Free; }

private:
	bool TryStartBuffered(const FPlayerFrame& Frame, FPlayerCommands& Out);
	bool TryStartAttack(float Cost, FPlayerCommands& Out);
	void Enter(EPlayerState NewState);
	void StopSprint(FPlayerCommands& Out);
	void UpdateSprint(const FPlayerFrame& Frame, FPlayerCommands& Out);
	void DropGuard(FPlayerCommands& Out);
	void ClearPerfectWindow();

	EPlayerState State = EPlayerState::Free;
	float StateTime = 0.f;
	float SinceDodge = 100.f;     // since the last dodge or backstep started
	int32 LightIndex = 0;
	bool bLightAttack = false;    // the running attack is part of a light combo
	bool bDodgeChain = false;     // ...the dodge combo, not the normal string
	bool bChainQueued = false;    // a light clicked during a light attack (or a second during the dodge), held for the chain point
	int32 HealCharges = 2;
	float HealRetryLeft = 0.f;
	bool bDodgeHeld = false;      // a dodge pressed during a hit reaction, released when it ends
	bool bSprinting = false;
	float SprintTime = 0.f;       // since the sprint started
	bool bChargeable = false;     // the running attack is a normal heavy that can still become a charge
	float BlockStunTime = 0.f;    // this block-stun's length
	bool bPerfectWindow = false;  // a fresh press raised the guard and its window is still open
	float PerfectWindowAge = 0.f; // since that press
	float PerfectLockoutLeft = 0.f; // after a whiffed window, no new one until this runs out
	EPlayerInput StunHeld = EPlayerInput::None; // a dodge or attack pressed during block- or deflect-stun, for its end
	bool bBusy = false;           // an attack or dodge chain is running: no regeneration
};
