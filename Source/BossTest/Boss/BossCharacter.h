// Sevarog (Step 4). Every state change goes through FBossStateMachine; this class decides what to
// request, and carries out each transition in one place (OnStateChanged): clips, attacks, armour,
// staggers, death. His attacks run on the same UAttackComponent as the player's.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Boss/BossBrain.h"
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
};

// One of his actions as the stand-in brain sees it (Step 5).
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

	UPROPERTY(EditAnywhere, Category = "Action")
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "Seconds before it can be picked again"))
	float Cooldown = 0.f;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "Only while the player keeps landing hits on him (Punishment Sweep)"))
	bool bNeedsPunishTrigger = false;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ToolTip = "A cast: poise damage during the first step's windup that breaks it (Soul Mark 70). 0 = not a cast"))
	float CastBreakThreshold = 0.f;
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

	// Everything the stand-in brain can pick (Step 5). Empty: Executioner's Swing only, as in Step 4.
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Attacks")
	TArray<FBossAction> Actions;

	UPROPERTY(EditAnywhere, Category = "Boss|Attacks", meta = (ToolTip = "The rolls' seed (variants, timing variation, picks). 0 = a new one every play"))
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, Category = "Boss|Attacks", meta = (ToolTip = "Punishment Sweep's stand-in trigger: this many of your hits land on him..."))
	int32 PunishHits = 2;

	UPROPERTY(EditAnywhere, Category = "Boss|Attacks", meta = (ToolTip = "... within this many seconds"))
	float PunishWindow = 4.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Attacks", meta = (ToolTip = "A broken cast's recoil (spec section 9)"))
	float CastBrokenTime = 0.8f;


	// ---- Clips (played on MontageSlot) ----
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	TObjectPtr<UAnimSequenceBase> IntroClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "Fitted to the stagger's length"))
	TObjectPtr<UAnimSequenceBase> StaggerClip;

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
	bool ForceAction(int32 Index);
	int32 GetActionCount() const { return Actions.Num(); }
	FName GetSelectedActionName() const;
	const UDeliveryComponent* GetDelivery() const { return Delivery; }
	void ForceStagger();
	void ResetBoss();

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCombatantComponent> Combatant;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAttackComponent> Attacks;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDeliveryComponent> Delivery;

private:
	void ApplyScale();
	float SwingRange() const;
	void StartIntro();
	void Evaluate();
	void StartSwing(const FString& Reason);
	bool StartAction(int32 Index, const FString& Reason, EBossPriority Priority = EBossPriority::CommittedAttack);
	TArray<struct FBossActionOption> ActionOptions() const;
	struct FBossSituation Situation() const;
	bool PunishTriggered() const;
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

	UPROPERTY()
	TArray<TObjectPtr<UAttackDefinition>> PendingSteps; // the running action's steps
	int32 StepIndex = 0;
	float LastStepHold = 0.f;
	int32 CurrentAction = -1;
	int32 LastAction = -1;
	int32 SelectedAction = 0;
	bool bCastOpen = false;
	TArray<float> Cooldowns;
	TArray<double> PlayerHitTimes;
	FRandomStream Rng;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ClipMontage;

	UPROPERTY()
	TObjectPtr<UAnimSequenceBase> GlideClip; // the glide clip on the slot, if one is playing

	FTransform HomeTransform;
	float IntroLength = 0.f;
	bool bRecoveringSent = false;
};
