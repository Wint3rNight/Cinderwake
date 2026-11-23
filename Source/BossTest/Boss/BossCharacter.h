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
struct FHitOutcome;

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


	// ---- Clips (played on MontageSlot) ----
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	TObjectPtr<UAnimSequenceBase> IntroClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips", meta = (ToolTip = "Fitted to the stagger's length"))
	TObjectPtr<UAnimSequenceBase> StaggerClip;

	UPROPERTY(EditDefaultsOnly, Category = "Boss|Clips")
	TObjectPtr<UAnimSequenceBase> DeathClip;

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
	void ForceSwing();
	void ForceStagger();
	void ResetBoss();

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCombatantComponent> Combatant;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAttackComponent> Attacks;

private:
	void ApplyScale();
	float SwingRange() const;
	void StartIntro();
	void Evaluate();
	void StartSwing(const FString& Reason);
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
	TObjectPtr<UAnimMontage> ClipMontage;

	UPROPERTY()
	TObjectPtr<UAnimSequenceBase> GlideClip; // the glide clip on the slot, if one is playing

	FTransform HomeTransform;
	float IntroLength = 0.f;
	bool bRecoveringSent = false;
};
