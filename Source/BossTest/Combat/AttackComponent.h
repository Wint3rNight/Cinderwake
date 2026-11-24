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
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAttackStepStarted, const class UAttackDefinition* /*Step*/, uint32 /*InstanceId*/);

class UAnimMontage;
class UAnimSequenceBase;
class UCurveFloat;
class UParticleSystemComponent;
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Category = "Attack", meta = (ToolTip = "Montage slot in the owner's animation blueprint"))
	FName MontageSlot = TEXT("FullBody");

	// Hitboxes and lunge targets are drawn while the console variable BossTest.DrawHitboxes is on (key 2).
	static bool ShouldDrawDebug();
	static void SetDrawDebug(bool bOn);

	// BlendIn: seconds the attack's clip blends in over the slot (a chained heavy uses a longer one); eased with a
	// smooth curve instead of a linear crossfade.
	// ExtraHold: this instance's hold is longer (or shorter) by this much (Delayed Execution's variation).
	bool StartAttack(UAttackDefinition* Definition, AActor* InTarget, float BlendIn = 0.08f, bool bEasedBlend = false, float ExtraHold = 0.f);

	// A sequence's next step (Step 5): when the running attack is done, this one starts inside the same tick (no gap
	// without armour), as a new instance with its own blend-in, unless CanContinue says no (a stagger is pending).
	// Cleared whenever an attack ends.
	void QueueNext(UAttackDefinition* Next, float ExtraHold = 0.f);
	void ClearQueue();
	TFunction<bool()> CanContinue;
	FOnAttackStepStarted OnStepStarted;

	// Stops every effect the attack owns (looping cues, the trail). Called when an attack ends, and by the owner on
	// death and reset.
	void ClearEffects();

	uint32 GetInstanceId() const { return CurrentDefinition ? InstanceId : 0; }
	static uint32 AllocateInstanceId(); // ids for anything that hits (attacks, rings, projectiles): never 0, never reused
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

	// Fires for every result of a sweep (hit, blocked, perfect guard, dodged, immune), with the instance.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnSweepResolved, const FHitOutcome& /*Outcome*/, AActor* /*Victim*/, uint32 /*InstanceId*/);
	FOnSweepResolved OnSweepResolved;

	// The lunge's destination while one runs (for the debug overlay).
	bool IsLunging() const;
	FVector GetLungeTarget() const { return LungeTarget; }

private:
	void Pose(const FAttackSample& Sample);
	UAnimSequenceBase* SpanClip(EAttackSpan Span) const;
	void UpdateCues(float ClipTime);
	void UpdateTrails(bool bActive);
	void EndTrails();
	void Deliver();
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
	EAttackSpan CurrentSpan = EAttackSpan::Main;
	bool bDeliveryDone = false;
	bool bTrailsOn = false;
	TArray<bool> CueFired;

	UPROPERTY()
	TObjectPtr<UAttackDefinition> QueuedNext;
	float QueuedHold = 0.f;

	UPROPERTY()
	TArray<TObjectPtr<UParticleSystemComponent>> CueComponents; // looping cues, by cue index (null when none)

	UPROPERTY()
	TArray<TObjectPtr<UParticleSystemComponent>> OneShotCues; // attached one-shots: stopped (not cut) when the attack ends

	UPROPERTY()
	TObjectPtr<UParticleSystemComponent> TrailComponent; // one, reused: Cascade's anim trails need it attached to the mesh
	FVector LungeTarget = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<UCurveFloat> EaseCurve; // smoothstep progress for eased lunges

	static uint32 NextInstanceId;
};
