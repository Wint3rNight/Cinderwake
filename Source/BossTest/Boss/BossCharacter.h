// Sevarog (Step 4). Every state change goes through FBossStateMachine; this class decides what to
// request, and carries out each transition in one place (OnStateChanged): clips, attacks, armour,
// staggers, death. His attacks run on the same UAttackComponent as the player's.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Boss/BossDirector.h"
#include "Boss/BossStateMachine.h"
#include "BossCharacter.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class UAttackComponent;
class UAttackDefinition;
class UCombatantComponent;
class UDeliveryComponent;
struct FHitOutcome;

// Where the player must be for an action (spec section 5, "Angles"); mirrors EBossAngleRule for the editor.
UENUM()
enum class EBossActionAngle : uint8
{
	FrontAndSides,
	FrontOnly,
	FrontSidesAndRear,
	Any
};

// One way an action can play: a single attack or a sequence (Double Execution's hits, the Advance's dash and swing).
USTRUCT()
struct FBossActionVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "Relative chance this variant is rolled when the action commits"))
	float Chance = 1.f;

	UPROPERTY(EditAnywhere, Category = "Action")
	TArray<TObjectPtr<UAttackDefinition>> Steps;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "The last step's hold varies by an even pick in [min, max] seconds"))
	float HoldExtraMin = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action")
	float HoldExtraMax = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "A branch that ends the action (Double Execution's stop): no combo follows it"))
	bool bEndsAlone = false;
};

// Section 10's weight table row for one action (Step 6).
USTRUCT()
struct FBossActionWeights
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action")
	float Close = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action")
	float Medium = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action")
	float Far = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action")
	float Extreme = 0.f;
};

// One link of a combo: the follow-up action, and its chain point into the previous action's recovery (Step 6).
USTRUCT()
struct FBossComboLink
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Combo")
	FName Action;

	UPROPERTY(EditAnywhere, Category = "Combo", meta = (ToolTip = "Seconds into the previous action's recovery that this one starts (section 5: 0.2 by default)"))
	float ChainIntoRecovery = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Combo", meta = (ToolTip = "This combo's own version of the action, played instead of its variants (Combo E's quicker Reverse Sweep). Empty: the action's own roll"))
	TObjectPtr<UAttackDefinition> Attack;
};

// A combo (spec section 5): an opener and its follow-ups, one purpose each.
USTRUCT()
struct FBossCombo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Combo")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Combo")
	FName Opener;

	UPROPERTY(EditAnywhere, Category = "Combo")
	TArray<FBossComboLink> FollowUps;

	UPROPERTY(EditAnywhere, Category = "Combo")
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Combo", meta = (ToolTip = "Seconds before it can be rolled again"))
	float Cooldown = 0.f;

	UPROPERTY(EditAnywhere, Category = "Combo", meta = (ToolTip = "Only with no major action since his last breather"))
	bool bNeedsFreshBeat = false;

	UPROPERTY(EditAnywhere, Category = "Combo", meta = (ToolTip = "Only once its opener and every follow-up have played this attempt"))
	bool bNeedsSeen = false;

	UPROPERTY(EditAnywhere, Category = "Combo", meta = (ToolTip = "How many majors it counts as for the breather"))
	int32 Majors = 1;
};

// One of his actions as the director sees it (Step 5, weights by band since Step 6).
USTRUCT()
struct FBossAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Action")
	TArray<FBossActionVariant> Variants;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "cm, edge to edge"))
	float MinRange = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "cm, edge to edge"))
	float MaxRange = 430.f;

	UPROPERTY(EditAnywhere, Category = "Action")
	EBossActionAngle Angle = EBossActionAngle::FrontAndSides;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "FrontSidesAndRear: the weight's scale in his rear quarter"))
	float RearWeight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "Section 10's weight in each distance band"))
	FBossActionWeights Weights;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "Seconds before it can be picked again"))
	float Cooldown = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "Only while the player keeps landing hits on him (Punishment Sweep)"))
	bool bNeedsPunishTrigger = false;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "A cast: poise damage during the first step's windup that breaks it (Soul Mark 70). 0 = not a cast"))
	float CastBreakThreshold = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "If the player's guard breaks on the first step, he swaps to this (same clip and timing, its own recovery) and stops: Double Execution's second hit would land inside the break"))
	TObjectPtr<UAttackDefinition> GuardBreakRecovery;
};

// The aura circle's rules as Blueprint data (copied into the director's plain FPostureRules).
USTRUCT()
struct FPostureRulesData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Aura", meta = (ToolTip = "His hits on you within the window that earn it (a knockdown earns it too)"))
	int32 HitsNeeded = 2;

	UPROPERTY(EditAnywhere, Category = "Aura", meta = (ToolTip = "Seconds before he can do it again"))
	float Cooldown = 60.f;

	UPROPERTY(EditAnywhere, Category = "Aura", meta = (ToolTip = "Seconds you must have held back"))
	float QuietFor = 2.f;

	UPROPERTY(EditAnywhere, Category = "Aura", meta = (ToolTip = "Chance when all the rest hold"))
	float Chance = 0.5f;
};

UCLASS()
class BOSSTEST_API ABossCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	explicit ABossCharacter(const FObjectInitializer& ObjectInitializer);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Attacks ----
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Attacks")
	TObjectPtr<UAttackDefinition> ExecutionersSwing;

	// Everything the director can pick (Step 5). Empty: Executioner's Swing only, as in Step 4.
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Attacks")
	TArray<FBossAction> Actions;

	// Section 5's combos A-F (Step 6).
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Attacks")
	TArray<FBossCombo> Combos;

	UPROPERTY(EditAnywhere, Category = "Boss|Director", meta = (ToolTip = "cm from the arena's centre: his movement stays inside (section 14: 13 m). An ArenaBounds in the level overrides it at play"))
	float ArenaRadius = 1300.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Director", meta = (ToolTip = "cm/s"))
	float CircleSpeed = 200.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Director", meta = (ToolTip = "cm/s"))
	float RepositionSpeed = 250.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Director", meta = (ToolTip = "cm/s: after 4 s of you far away"))
	float PursuitSpeed = 500.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Director", meta = (ToolTip = "cm/s: his approach at Medium range while you haven't attacked for 2 s, a stalk (more menace, more time to read him)"))
	float StalkSpeed = 200.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Director", meta = (ToolTip = "Seconds he pauses after an action (the commitment cooldown; a hit can't cut it shorter)"))
	float CommitPause = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Boss|Director", meta = (ToolTip = "Seconds of breather after every second action (a hit cuts it to the pause)"))
	float Breather = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Boss|Director", meta = (ToolTip = "At commit: stopping after the opener weighs this times the sum of its valid combos' weights (0.5: it stops alone a third of the time)"))
	float ComboStopShare = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Boss|Attacks", meta = (ToolTip = "The rolls' seed (variants, timing variation, picks). 0 = a new one every play"))
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, Category = "Boss|Attacks", meta = (ToolTip = "Punishment Sweep's stand-in trigger: this many of your hits land on him..."))
	int32 PunishHits = 2;

	UPROPERTY(EditAnywhere, Category = "Boss|Attacks", meta = (ToolTip = "... within this many seconds"))
	float PunishWindow = 4.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Attacks", meta = (ToolTip = "A broken cast's recoil (spec section 9)"))
	float CastBrokenTime = 0.8f;

	// ---- The finishing blow (the council's run, 2026-11-24) ----
	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "Seconds into his stagger the window opens (he's slumping by then)"))
	float FinisherFrom = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "... and closes (he starts to stand at the normal 1.8 s stagger's 1.25)"))
	float FinisherUntil = 1.25f;

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "Degrees from his front the player must be in"))
	float FinisherArc = 45.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "cm, the player's capsule to his"))
	float FinisherReach = 150.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "With a finisher the stagger lasts this long in all, wherever it was triggered"))
	float FinisherStaggerLength = 2.40f;

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "cm from his centre to the player's: the crash's blade crosses his slumped chest there (probe_finisher.py)"))
	float FinisherStandOff = 164.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "He turns toward the player by up to this many degrees..."))
	float StaggerTurnLimit = 60.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "... during this many seconds from the stagger's start"))
	float StaggerTurnTime = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "The in-range cue: a soft light on his slumped chest while a press would finish him"))
	FLinearColor FinisherGlowColor = FLinearColor(1.f, 0.72f, 0.38f);

	UPROPERTY(EditAnywhere, Category = "Boss|Finisher", meta = (ToolTip = "Candelas"))
	float FinisherGlowIntensity = 40.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Encounter", meta = (ToolTip = "Swirls around him while you're missing soul fragments: your stolen soul is on him (hit him to take it back)"))
	TObjectPtr<class UParticleSystem> StolenSoulEffect;

	// ---- The aura circle (the user, 2026-11-25): rare and earned, after he's been winning; replaces a breather ----
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Aura", meta = (ToolTip = "Travel_Mode_Idle: hammer held out at his side, far from both windups' silhouettes"))
	TObjectPtr<UAnimSequenceBase> PostureClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Aura", meta = (ToolTip = "Smoke drifting off him while he circles"))
	TObjectPtr<class UParticleSystem> PostureSmoke;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Aura", meta = (ToolTip = "On the hammer head while he circles"))
	TObjectPtr<class UParticleSystem> PostureHammerEffect;

	UPROPERTY(EditAnywhere, Category = "Boss|Aura", meta = (ToolTip = "When: his hits needed in the window, the cooldown, your quiet time and the chance"))
	FPostureRulesData PostureRules;

	UPROPERTY(EditAnywhere, Category = "Boss|Aura", meta = (ToolTip = "Seconds his hits count toward earning it"))
	float PostureHitWindow = 15.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Aura")
	float PostureMin = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Boss|Aura")
	float PostureMax = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Boss|Aura", meta = (ToolTip = "cm/s: a slow circle around you"))
	float PostureSpeed = 110.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Aura", meta = (ToolTip = "Degrees per second: he watches rather than tracks"))
	float PostureTurnRate = 60.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Aura", meta = (ToolTip = "Seconds of rouse when it ends: it is the commitment pause, not extra"))
	float PostureRouse = 0.30f;


	// ---- Clips (played on MontageSlot) ----
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	TObjectPtr<UAnimSequenceBase> IntroClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "Fitted to the stagger's length; only when the stun clips are missing"))
	TObjectPtr<UAnimSequenceBase> StaggerClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "The stagger's slump: played to Stun Start To at Stun Start Rate"))
	TObjectPtr<UAnimSequenceBase> StunStartClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "Looped while slumped"))
	TObjectPtr<UAnimSequenceBase> StunLoopClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "The stand-up, timed to end with the stagger"))
	TObjectPtr<UAnimSequenceBase> StunEndClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "Clip seconds: the slump's end (head 243 to 143 cm)"))
	float StunStartTo = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	float StunStartRate = 1.67f;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	float StunEndRate = 1.33f;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	TObjectPtr<UAnimSequenceBase> DeathClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "Fitted to the cast-broken recoil"))
	TObjectPtr<UAnimSequenceBase> CastBrokenClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "He hovers, so his jog clips are glide poses"))
	TObjectPtr<UAnimSequenceBase> GlideForward;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	TObjectPtr<UAnimSequenceBase> GlideBack;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	TObjectPtr<UAnimSequenceBase> GlideLeft;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	TObjectPtr<UAnimSequenceBase> GlideRight;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	FName MontageSlot = TEXT("FullBody");

	// ---- Movement (spec section 3) ----
	UPROPERTY(EditAnywhere, Category = "Boss|Movement", meta = (ToolTip = "1.15 makes him about 3.7 m tall"))
	float BossScale = 1.15f;

	UPROPERTY(EditAnywhere, Category = "Boss|Movement", meta = (ToolTip = "cm/s"))
	float GlideSpeed = 340.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Movement", meta = (ToolTip = "cm/s^2"))
	float GlideAcceleration = 900.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Movement", meta = (ToolTip = "Degrees per second outside attacks; attack tracking has its own rate"))
	float TurnRate = 150.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Movement", meta = (ToolTip = "Seconds an approach lasts at most"))
	float ApproachTime = 3.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Movement", meta = (ToolTip = "Seconds he holds still, facing you, after each swing"))
	float NeutralBeat = 1.f;

	// ---- Encounter ----
	UPROPERTY(EditAnywhere, Category = "Boss|Encounter")
	FText DisplayName = FText::FromString(TEXT("Sevarog, the Soul Harvester"));

	UPROPERTY(EditAnywhere, Category = "Boss|Encounter", meta = (ToolTip = "Health where the transformation will start (Step 8); marked on his bar"))
	float PhaseTransitionHealth = 3300.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Encounter")
	bool bAIEnabled = true;

	// ---- Read by the HUD and the debug controller ----
	const UCombatantComponent* GetCombatant() const { return Combatant; }
	const UAttackComponent* GetAttacks() const { return Attacks; }
	const FBossStateMachine& GetStates() const { return States; }
	TArray<FString> DebugLines() const;

	// ---- Debug commands ----
	void ToggleAI();
	void ForceSwing();          // the selected action (Executioner's Swing until another is picked)
	void SelectNextAction();    // cycles which action ForceSwing forces
	bool ForceAction(int32 Index, int32 Variant = -1);
	void PinAction(int32 Index);  // tests: that action whenever it fits, no combos (-1 = none)
	const FBossDirector& GetDirector() const { return Director; }
	bool ForceCombo(int32 ComboIndex);                  // tests and debugging: its opener, then its follow-ups
	int32 GetCurrentCombo() const { return CurrentCombo; }
	float GetPauseLength() const { return PauseLength; }
	FVector GetArenaCentre() const { return ArenaCentre; } // the ArenaBounds' centre, or his spawn point without one
	int32 GetActionCount() const { return Actions.Num(); }
	FName GetSelectedActionName() const;
	const UDeliveryComponent* GetDelivery() const { return Delivery; }
	void ForceStagger();
	void ForcePosture();        // tests and debugging: the aura circle now (AI off)
	bool IsPostureFXActive() const { return PostureFX.Num() > 0; }
	void ResetBoss();

	// ---- The finishing blow ----
	// His stagger's window is open (one per stagger, never in a flinch) and Who stands within the arc and reach.
	bool CanBeFinishedBy(const AActor* Who) const;
	// The player committed: the window closes, the stagger runs to FinisherStaggerLength. False if it can't.
	bool BeginFinisher(const AActor* Who);
	// Where the player stands for it: FinisherStandOff in front of him, facing him (the caller sweep-tests it).
	FTransform FinisherSpot(const AActor* Who) const;
	bool IsFinisherTaken() const { return bFinisherTaken; }
	float GetFinisherGlow() const;

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCombatantComponent> Combatant;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAttackComponent> Attacks;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDeliveryComponent> Delivery;

private:
	void ApplyScale();
	void IgnoreCamera();
	float SwingRange() const;
	void StartIntro();
	void Evaluate();
	void StartSwing(const FString& Reason);
	// With bRollCombo, the combo is rolled after the variant, and a branch that ends alone takes none (QA: Combo G could follow
	// Double Execution's stop branch when the combo was rolled first).
	bool StartAction(int32 Index, const FString& Reason, EBossPriority Priority = EBossPriority::CommittedAttack, int32 Combo = -1, int32 ForcedVariant = -1, bool bRollCombo = false);
	void OnOwnSweep(const FHitOutcome& Outcome, AActor* Victim, uint32 InstanceId);
	void TickStagger(float DeltaSeconds);
	void TickPosture(float DeltaSeconds);
	bool TryPosture();
	void OnPlayerHit(const FHitOutcome& Outcome, AActor* Attacker);
	void EndPostureEffects();
	void SampleSwing();
	void FlushSwing();
	bool HasStunClips() const { return StunStartClip && StunLoopClip && StunEndClip; }
	UAnimMontage* PlayClipEased(UAnimSequenceBase* Clip, float Rate, float BlendIn, float BlendOut, int32 Loops = 1);
	void InitDirector();
	FBossPerception Perception() const;
	bool PunishTriggered() const;
	bool CanContinuePlan() const;
	void TickMovement(float DeltaSeconds);
	void SteerAndGlide(const FVector& Dir, float Speed, float DeltaSeconds);
	bool CheckProgress(float DeltaSeconds, EBossMove Move);
	void OnStepStarted(const UAttackDefinition* Step, uint32 InstanceId);
	void ClearCombat();
	void TickApproach(float DeltaSeconds);
	void TickTurn(float DeltaSeconds);
	void TickAttack();
	void FaceTarget(float DeltaSeconds);
	void PlayGlideClip(const FVector& Dir);
	void StopGlide();
	void DrawDebugShapes() const;
	void OnStateChanged(EBossState From, EBossState To);
	void OnHitResolved(const FHitOutcome& Outcome, AActor* Attacker);
	UAnimMontage* PlayClip(UAnimSequenceBase* Clip, float Rate, float BlendIn, float BlendOut, int32 Loops = 1);
	ACharacter* Player() const;
	bool PlayerDown() const;
	float EdgeDistanceToPlayer() const;
	float AngleToPlayer() const;

	FBossStateMachine States;

	UPROPERTY()
	TObjectPtr<UAttackDefinition> PendingAttack;

	// The committed action, flattened: its steps, then each follow-up's (Step 6). The attacks are held by Actions.
	struct FPlannedStep
	{
		UAttackDefinition* Attack = nullptr;
		float ExtraHold = 0.f;
		float ChainIntoRecovery = -1.f; // below 0: a sequence join (at the previous step's end)
		int32 Action = -1;
		bool bFirstOfAction = false;
	};
	TArray<FPlannedStep> Plan;
	int32 StepIndex = 0;
	int32 CurrentCombo = -1;
	FBossDirector Director;
	TArray<FName> ComboNames;
	float PauseLength = 0.25f;
	EBossMove CurrentMove = EBossMove::Wait;
	float MoveLength = 0.f;
	int32 CircleSign = 1;
	bool bCircleFlipped = false;
	FVector ProgressFrom = FVector::ZeroVector;
	float ProgressTime = 0.f;
	double LastStaggerTime = -1.0;
	FString LastWhy;
	bool bTickOrdered = false;
	bool bKeepCircle = false;
	float CircleLeft = 0.f;
	double FightStartTime = 0.0;
	int32 CurrentAction = -1;
	int32 LastAction = -1;
	int32 SelectedAction = 0;
	bool bCastOpen = false;
	TArray<double> PlayerHitTimes;
	double LastPlayerAttackTime = -100.0; // world seconds the player was last seen attacking
	FRandomStream Rng;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ClipMontage;

	UPROPERTY()
	TObjectPtr<UAnimSequenceBase> GlideClip; // the glide clip on the slot, if one is playing

	FTransform HomeTransform;
	FVector ArenaCentre = FVector::ZeroVector;
	float IntroLength = 0.f;
	bool bRecoveringSent = false;

	UPROPERTY()
	TObjectPtr<class UPointLightComponent> FinisherGlow;

	UPROPERTY()
	TObjectPtr<class UParticleSystemComponent> StolenSoulFX;

	UPROPERTY()
	TArray<TObjectPtr<class UParticleSystemComponent>> PostureFX;
	TArray<double> HitsOnPlayerTimes;      // world seconds his hits landed on you
	double LastKnockdownOnPlayer = -100.0;
	double LastPostureTime = -1000.0;
	double PostureStartTime = 0.0;
	double LastPlayerAttackStart = -100.0; // world seconds you started an attack (the aura's exit, seen 0.25 s later)
	double LastPlayerHealStart = -100.0;
	bool bPlayerWasAttacking = false;
	bool bPlayerWasHealing = false;
	bool bPlayerHitBound = false;
	float PostureLength = 2.f;

	// The per-swing log (the gap study, 2026-11-25): one line per swept step, so a play tells his misses from your spacing.
	struct FSwingRecord
	{
		uint32 Instance = 0;
		FString Step;
		FString Action;
		float EdgeAtWindup = -1.f;
		float RadialSpeed = 0.f;       // you moving away (+) or in (-) at windup start, cm/s
		float EdgeAtActive = -1.f;
		float ClosestGap = 1e9f;       // hammer to your capsule during the active window, cm (0 = touching)
		FString Outcome;               // what the sweep resolved, empty if it never touched you
		bool bDodged = false;
		bool bGuarded = false;
	};
	FSwingRecord Swing;
	int32 PostureSign = 1;
	bool bPostureFlipped = false;
	bool bFinisherTaken = false;  // this stagger's one finishing blow has started
	int32 StunPhase = 0;          // 0 slumping, 1 slumped (the loop), 2 standing
	float StaggerYaw = 0.f;       // his facing when the stagger began (the turn's limit is measured from it)
	float GlowAlpha = 0.f;
};
