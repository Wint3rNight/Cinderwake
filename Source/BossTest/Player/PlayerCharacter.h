// Feng Mao, the player (Step 3). The rules live in FPlayerActions; this class reads input, feeds the
// model, and carries out its commands: clips on the montage slot, root-motion pushes for the dodge
// and sprint, attacks through UAttackComponent. Attack assets and clips are set on BP_FengMao.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Player/DodgeProfile.h"
#include "Player/PlayerActions.h"
#include "PlayerCharacter.generated.h"

class UAnimMontage;
class UParticleSystem;
class UParticleSystemComponent;
class UAnimSequenceBase;
class UAttackComponent;
class UAttackDefinition;
class UCameraComponent;
class UCombatantComponent;
class UCurveFloat;
class ULockOnComponent;
class USpringArmComponent;
struct FHitOutcome;

UCLASS()
class BOSSTEST_API APlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	explicit APlayerCharacter(const FObjectInitializer& ObjectInitializer);
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;

	// ---- Attacks (data assets in Content/BossTest/Attacks/Player) ----
	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "Light attacks in combo order; loops back to the first"))
	TArray<TObjectPtr<UAttackDefinition>> LightAttacks;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks")
	TObjectPtr<UAttackDefinition> HeavyAttack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "The quick combo: lights that come out 0.50-0.90 s after a dodge started, in order"))
	TArray<TObjectPtr<UAttackDefinition>> DodgeLightAttacks;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "The leaping slam: a heavy that comes out 0.50-0.90 s after a dodge started"))
	TObjectPtr<UAttackDefinition> DodgeHeavyAttack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "The heavy, charged: switched to (a crossfade to its own clip, Kwang's gather, leap and crashing diagonal) when RMB is still held at the heavy's branch, down since the heavy started"))
	TObjectPtr<UAttackDefinition> ChargedHeavyAttack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "A light after sprinting for a quarter of a second"))
	TObjectPtr<UAttackDefinition> SprintAttack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "The second heavy, a rising uppercut: the heavy that chains from heavy 1, the charged heavy or the slam (Step 4e)"))
	TObjectPtr<UAttackDefinition> Heavy2Attack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "Light 1 out of the slam's landing crouch: a longer windup so he stands up first (Step 4d)"))
	TObjectPtr<UAttackDefinition> LightRiseAttack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "Seconds a heavy started from another attack's chain point blends in (eased); a fresh attack uses 0.08"))
	float ChainedHeavyBlend = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "Seconds the stand-up Light 1 blends in out of the slam's crouch (eased)"))
	float LightRiseBlend = 0.22f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "Glow on the blade while the heavy charges"))
	TObjectPtr<UParticleSystem> ChargeEffect;

	// ---- Guard (hold Q or the mouse's back button) ----
	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "Its first 0.18 s raise the guard"))
	TObjectPtr<UAnimSequenceBase> GuardRaiseClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard")
	TObjectPtr<UAnimSequenceBase> GuardLoopClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "Played from 0.15 s when the guard drops; any action cuts it"))
	TObjectPtr<UAnimSequenceBase> GuardLowerClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard")
	TObjectPtr<UAnimSequenceBase> GuardWalkForward;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard")
	TObjectPtr<UAnimSequenceBase> GuardWalkBack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard")
	TObjectPtr<UAnimSequenceBase> GuardWalkLeft;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard")
	TObjectPtr<UAnimSequenceBase> GuardWalkRight;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "cm/s the guard-walk clips are authored for (measured about 350)"))
	float GuardWalkAuthoredSpeed = 350.f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "A short twitch for a blocked hit (Shield, from 0.06 s)"))
	TObjectPtr<UAnimSequenceBase> BlockClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "The guard break's stagger (its first 0.30 s, held)"))
	TObjectPtr<UAnimSequenceBase> GuardBreakClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "A small spark: a normal block"))
	TObjectPtr<UParticleSystem> BlockEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "The big spark of a perfect guard"))
	TObjectPtr<UParticleSystem> PerfectGuardEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "Scale of the perfect guard's spark"))
	float PerfectGuardEffectScale = 1.6f;

	UPROPERTY(EditAnywhere, Category = "Player|Guard", meta = (ToolTip = "cm/s walking with the guard up"))
	float GuardWalkSpeed = 270.f;

	// ---- The evade (dodge and backstep) ----
	// Each plays a segment of its clip, posed from the dodge's own clock (so it pauses in hit-stop),
	// with a burst where he leaves and trails while he moves.
	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade")
	TObjectPtr<UAnimSequenceBase> DodgeClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Clip seconds where the dodge's pose starts"))
	float DodgeClipStart = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Clip seconds reached when the dodge ends"))
	float DodgeClipEnd = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade")
	TObjectPtr<UAnimSequenceBase> BackstepClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade")
	float BackstepClipStart = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade")
	float BackstepClipEnd = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Spawned at his feet where the evade starts"))
	TObjectPtr<UParticleSystem> EvadeStartEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Attached to him while he evades, then left to fade"))
	TArray<TObjectPtr<UParticleSystem>> EvadeTrailEffects;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Where the trails sit, relative to his feet"))
	FVector EvadeTrailOffset = FVector(0.f, 0.f, 100.f);

	// ---- Clips (played on MontageSlot) ----
	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips")
	TObjectPtr<UAnimSequenceBase> HitReactFront;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips")
	TObjectPtr<UAnimSequenceBase> HitReactBack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips")
	TObjectPtr<UAnimSequenceBase> HitReactLeft;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips")
	TObjectPtr<UAnimSequenceBase> HitReactRight;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips")
	TObjectPtr<UAnimSequenceBase> KnockdownFront;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips")
	TObjectPtr<UAnimSequenceBase> KnockdownBack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips", meta = (ToolTip = "Its first HealCast seconds play during the heal"))
	TObjectPtr<UAnimSequenceBase> HealClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips", meta = (ToolTip = "Looped while sprinting"))
	TObjectPtr<UAnimSequenceBase> SprintClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips")
	TObjectPtr<UAnimSequenceBase> DeathClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips", meta = (ToolTip = "Feng Mao's animation blueprint only has an UpperBody slot; it plays full body when he isn't steering"))
	FName MontageSlot = TEXT("UpperBody");

	// ---- Rules (spec section 4), copied into the action model when play starts ----
	UPROPERTY(EditDefaultsOnly, Category = "Player|Rules", meta = (ShowOnlyInnerProperties))
	FPlayerTuning Rules;

	// ---- Movement ----
	UPROPERTY(EditAnywhere, Category = "Player|Movement", meta = (ToolTip = "cm/s. His jog clip is authored for about 400; faster slides"))
	float RunSpeed = 400.f;

	UPROPERTY(EditAnywhere, Category = "Player|Movement", meta = (ToolTip = "cm/s"))
	float SprintSpeed = 650.f;

	UPROPERTY(EditAnywhere, Category = "Player|Movement", meta = (ToolTip = "cm over the whole dodge"))
	float DodgeDistance = 350.f;

	UPROPERTY(EditAnywhere, Category = "Player|Movement", meta = (ToolTip = "cm covered by DodgeEarlyTime (front-loading)"))
	float DodgeEarlyDistance = 210.f;

	UPROPERTY(EditAnywhere, Category = "Player|Movement")
	float DodgeEarlyTime = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Player|Movement", meta = (ToolTip = "cm; same timing and shape as the dodge"))
	float BackstepDistance = 200.f;

	UPROPERTY(EditAnywhere, Category = "Player|Movement", meta = (ToolTip = "Without lock-on, an attack lunges at a target within this many degrees of where you face"))
	float LungeTargetAngle = 60.f;

	UPROPERTY(EditAnywhere, Category = "Player|Movement")
	float MouseSensitivity = 1.f;

	// ---- Read by the HUD and the debug controller ----
	const FPlayerActions& GetActions() const { return Actions; }
	const UCombatantComponent* GetCombatant() const { return Combatant; }
	bool IsStaminaFlashing() const;   // a press was just refused (the HUD flashes the stamina bar)
	UAttackComponent* GetAttacks() const { return Attacks; }
	FString DebugLine() const;
	FString LastEventText() const { return LastEventTime > 0.f ? LastEvent : FString(); }

	// The reset contract (spec section 15): full health, both heal charges, full stamina, no fragments,
	// back at the start.
	void ResetPlayer();
	void AddFragmentForTesting();

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> Arm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAttackComponent> Attacks;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCombatantComponent> Combatant;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<ULockOnComponent> LockOn;

private:
	double StaminaFlashUntil = 0.0; // world time; a refused press flashes the stamina bar until then
	void ReadInput(FPlayerFrame& Frame);
	void Execute(const FPlayerCommands& Commands);
	// Blends in over the attack's own BlendIn, or MinBlendIn if that's longer (a chained heavy), eased if either asks.
	void StartAttack(UAttackDefinition* Definition, float MinBlendIn = 0.f, bool bForceEase = false);
	void StartDodge(bool bBackstep);
	void EndDodgePush();
	void TickEvadeVisuals();
	void EndEvadeVisuals(float BlendOut);
	void SetSprinting(bool bOn);
	void TickGuardVisuals();
	void PushBack(float Distance, float Duration);
	void TickChargeEffect();
	void TickMovement();
	void SyncCombatant();
	void ApplyGuardTuning();
	void TickCamera();
	void PlayReaction(bool bKnockdown);
	UAnimMontage* PlayClip(UAnimSequenceBase* Clip, float Rate, float BlendIn, float BlendOut, int32 Loops = 1, float StartAt = 0.f);
	void StopClip(float BlendOut);
	void FaceForAction();
	AActor* LungeTarget() const;
	FVector InputDirection() const;
	void CameraKick(float Amplitude, float Duration);
	void SetEvent(const FString& Text, float Seconds);
	void OnHitResolved(const FHitOutcome& Outcome, AActor* Attacker);
	void OnHitLanded(const FHitOutcome& Outcome, AActor* Victim);

	FPlayerActions Actions;
	FDodgeProfile DodgeProfile;
	FDodgeProfile BackstepProfile;

	UPROPERTY()
	TObjectPtr<UCurveFloat> DodgeDistanceCurve; // the profile's distance over normalized time, 0 to 1; dodge and backstep share it

	UPROPERTY()
	TObjectPtr<UAnimMontage> ClipMontage;

	UPROPERTY()
	TObjectPtr<UAnimMontage> EvadeMontage;

	UPROPERTY()
	TObjectPtr<UAnimSequenceBase> GuardClipShown; // which guard clip is on the slot (raise, loop or a walk)

	float GuardClipAge = 0.f; // since the guard clip last changed (a minimum dwell stops flicker)

	UPROPERTY()
	TObjectPtr<UParticleSystemComponent> ChargeFX;

	UPROPERTY()
	TArray<TObjectPtr<UParticleSystemComponent>> EvadeTrails;

	float EvadeClipStart = 0.f;
	float EvadeClipEnd = 0.f;

	uint16 DodgeMoveId = 0;
	uint16 PushId = 0; // the block or guard-break push
	FVector2D Move = FVector2D::ZeroVector;
	TWeakObjectPtr<AActor> LastAttacker;

	FVector StartLocation = FVector::ZeroVector;
	FRotator StartRotation = FRotator::ZeroRotator;
	FVector BaseSocketOffset = FVector::ZeroVector;
	float KickAmplitude = 0.f;
	float KickTime = 0.f;
	float KickDuration = 0.f;
	FString LastEvent;
	float LastEventTime = 0.f;
};
