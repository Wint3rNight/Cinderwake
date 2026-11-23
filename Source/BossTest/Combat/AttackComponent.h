// Runs one attack at a time from a UAttackDefinition (Step 2): advances its timeline on the
// actor's own clock, poses the montage to that timeline, lunges, sweeps the weapon during the
// active window, resolves hits through the target's UCombatantComponent and applies hit-stop.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/AttackTimeline.h"
#include "Combat/HitResolver.h"
#include "AttackComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAttackHitLanded, const FHitOutcome& /*Outcome*/, AActor* /*Victim*/);

class UAnimMontage;
class UAnimSequenceBase;
class UCurveFloat;
class UAttackDefinition;
class USkeletalMeshComponent;

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class BOSSTEST_API UAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAttackComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = "Attack", meta = (ToolTip = "Montage slot in the owner's animation blueprint"))
	FName MontageSlot = TEXT("FullBody");

	// Hitboxes and lunge targets are drawn while the console variable BossTest.DrawHitboxes is on (key 2).
	static bool ShouldDrawDebug();
	static void SetDrawDebug(bool bOn);

	// BlendIn: seconds the attack's clip blends in over the slot (a chained heavy uses a longer one); eased with a
	// smooth curve instead of a linear crossfade.
	bool StartAttack(UAttackDefinition* Definition, AActor* InTarget, float BlendIn = 0.08f, bool bEasedBlend = false);
	void StopAttack(float BlendOut = 0.2f);
	// Swaps the running attack for its variant (the charged heavy), keeping the instance so nothing already hit can
	// be hit twice. Same clip: keeps the elapsed time and the montage, no pop. Another clip: crossfades to it
	// (SwitchBlend, eased) and the variant's own timeline starts at 0.
	bool SwitchTo(UAttackDefinition* Definition);

	UPROPERTY(EditAnywhere, Category = "Attack", meta = (ToolTip = "Seconds a branch variant on another clip crossfades in"))
	float SwitchBlend = 0.20f;

	// An attack with a branch (BranchAt) holds there until its owner chooses, later in the same frame:
	// true takes the variant (SwitchTo), false goes on as it is. Held more than 0.1 s, it goes on by itself.
	bool IsAtBranch() const;
	bool ResolveBranch(bool bTakeVariant);

	bool IsAttacking() const { return CurrentDefinition != nullptr; }
	const UAttackDefinition* GetCurrent() const { return CurrentDefinition; }
	FAttackSample GetSample() const;
	float GetElapsed() const { return Elapsed; }
	float GetTimeToFree() const;      // attack-clock seconds until the attack frees its owner; 0 when none runs

	// Fires for every hit this component lands (Result == Hit).
	FOnAttackHitLanded OnHitLanded;

	// The lunge's destination while one runs (for the debug overlay).
	bool IsLunging() const;
	FVector GetLungeTarget() const { return LungeTarget; }

private:
	void Pose(float ClipTime);
	UAnimMontage* PlayPaused(UAnimSequenceBase* Clip, float BlendIn, bool bEased);
	void SweepWeapon();
	void ApplyLunge(float Duration);
	void TrackTarget(float Step);
	void SetOwnerArmor(EArmor Armor);
	void EndAttack();
	USkeletalMeshComponent* GetOwnerMesh() const;

	UPROPERTY()
	TObjectPtr<UAttackDefinition> CurrentDefinition;

	UPROPERTY()
	TObjectPtr<UAnimMontage> Montage;

	FAttackTiming Timing;
	float Elapsed = 0.f;
	uint32 InstanceId = 0;
	TWeakObjectPtr<AActor> Target;
	FHitRegistry Registry;
	TArray<FVector> PrevPoints;
	bool bLungeDone = false;
	bool bAtBranch = false;
	bool bBranchResolved = false;
	float BranchWait = 0.f;
	uint16 LungeId = 0; // root motion source id, 0 when none is running
	FVector LungeTarget = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<UCurveFloat> EaseCurve; // smoothstep progress for eased lunges

	static uint32 NextInstanceId;
};
