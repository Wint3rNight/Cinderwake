// Anything that can be hit: owns its combat state, ticks it on the actor's own clock,
// resolves incoming hits through the single pipeline and announces the outcome (Step 2).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/HitResolver.h"
#include "Combat/HitStop.h"
#include "CombatantComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCombatHitResolved, const FHitOutcome& /*Outcome*/, AActor* /*Attacker*/);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class BOSSTEST_API UCombatantComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatantComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = "Combatant")
	float MaxHealth = 300.f;

	UPROPERTY(EditAnywhere, Category = "Combatant|Poise")
	float PoiseThreshold = 650.f;

	UPROPERTY(EditAnywhere, Category = "Combatant|Poise")
	float PoiseDecayRate = 10.f;

	UPROPERTY(EditAnywhere, Category = "Combatant|Poise")
	float PoiseDecayDelay = 3.f;

	UPROPERTY(EditAnywhere, Category = "Combatant|Poise")
	float StaggerDuration = 1.25f;

	UPROPERTY(EditAnywhere, Category = "Combatant|Poise")
	float StaggerImmunity = 8.f;

	// Fills in the target key, resolves the hit and broadcasts the outcome.
	FHitOutcome ReceiveHit(FCombatHitContext Hit, FHitRegistry& Registry, AActor* Attacker);
	// Freezes the owner for Duration (real time). With a tremble, its mesh shakes TrembleCm along TrembleDir (world space), decaying.
	void StartHitStop(float Duration, const FVector& TrembleDir = FVector::ZeroVector, float TrembleCm = 0.f);
	void ResetCombatant();

	bool IsDead() const { return State.Health.IsDead(); }
	int32 GetTargetKey() const { return TargetKey; }

	FCombatantState State;
	FOnCombatHitResolved OnHitResolved;

private:
	void ApplyTuning();

	class USkeletalMeshComponent* OwnerMesh() const;
	void TickTremble();
	void StopTremble();

	FHitStop HitStop;
	bool bHitStopApplied = false;
	bool bTrembling = false;
	FVector TrembleDir = FVector::ZeroVector;
	float TrembleCm = 0.f;
	float TrembleDuration = 0.f;
	double TrembleStart = 0.0;
	FVector MeshBase = FVector::ZeroVector;  // the mesh's relative location before the tremble
	float TrembleSign = 1.f;
	int32 TargetKey = 0;
	static int32 NextTargetKey;
};
