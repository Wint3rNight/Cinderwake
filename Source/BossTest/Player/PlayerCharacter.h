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

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "The finishing blow on his slumped stagger: the charged heavy's crash from its crouch, a guaranteed hit (the council's run, 2026-11-24)"))
	TObjectPtr<UAttackDefinition> RiposteAttack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips", meta = (ToolTip = "Heal clip seconds where the health lands; the clip plays faster so this lands at the rules' Heal Land At"))
	float HealClipLandAt = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Attacks", meta = (ToolTip = "Seconds the finishing blow's snap to its spot in front of him takes"))
	float RiposteSnapTime = 0.15f;

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

	UPROPERTY(EditDefaultsOnly, Category = "Player|Soul", meta = (ToolTip = "On you when a hit wins back a soul fragment (the newer player: the curse needed a sign it can be won back)"))
	TObjectPtr<UParticleSystem> FragmentReturnEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "The big spark of a perfect guard"))
	TObjectPtr<UParticleSystem> PerfectGuardEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Guard", meta = (ToolTip = "Scale of the perfect guard's spark"))
	float PerfectGuardEffectScale = 1.6f;

	UPROPERTY(EditAnywhere, Category = "Player|Guard", meta = (ToolTip = "cm/s walking with the guard up"))
	float GuardWalkSpeed = 270.f;

	// ---- The evade (dodge and backstep) ----
	// Each plays a segment of its clip, posed from the dodge's own clock (so it pauses in hit-stop), and leaves fading
	// afterimages behind him. (The start burst and the trails stay available; since the feel pass they're unset.)
	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade")
	TObjectPtr<UAnimSequenceBase> DodgeClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Clip seconds where the dodge's pose starts"))
	float DodgeClipStart = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Clip seconds reached when the dodge ends"))
	float DodgeClipEnd = 0.f;

	// The dodge's pose follows his speed, not the clock (the feel pass, 2026-11-25): the deepest lean creeps while he's faster
	// than a run; at Re Dodge At, with the stick held he plays on into the push-off, with none he plants into a stop.
	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Clip seconds: the lean the dodge snaps into..."))
	float DodgeLeanFrom = 0.33f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "... crept to by Re Dodge At (the clip's deepest lean)"))
	float DodgeLeanTo = 0.43f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Clip seconds: the push-off played on at 1x when the stick takes over, then blended out"))
	float DodgePushOffTo = 0.63f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "The plant when the dodge ends with no stick (Jog_Fwd_Stop)"))
	TObjectPtr<UAnimSequenceBase> DodgeStopClip;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Stop clip seconds: from the plant, at 1x to Dodge Stop Slow Until, then 1.5x to Dodge Stop To (his idle)"))
	float DodgeStopFrom = 1.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade")
	float DodgeStopSlowUntil = 1.50f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade")
	float DodgeStopTo = 2.05f;

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

	// Afterimages (after the Step 4 play: "afterimages, a few fading copies of his pose behind him, light, like Khazan's
	// quick dodge"): copies of his pose left where he was, fading out. A fixed pool, made once, so a dodge allocates nothing.
	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Translucent, used with skeletal meshes, with Opacity and Color parameters (create_afterimage_material.py). None: no afterimages"))
	TObjectPtr<UMaterialInterface> AfterimageMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Copies per evade (the pool's size); copy i appears when the dodge's clock passes i x the interval, so new copies mean invulnerable"))
	int32 AfterimageCount = 5;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Seconds of dodge clock between copies (five at 0.05 s all appear inside the 0.22 s of i-frames)"))
	float AfterimageInterval = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "Seconds each copy takes to fade out"))
	float AfterimageLife = 0.30f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade", meta = (ToolTip = "A copy's opacity when it appears"))
	float AfterimageOpacity = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Evade")
	FLinearColor AfterimageColor = FLinearColor(0.45f, 0.7f, 1.f);

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

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips", meta = (ToolTip = "Played after the knockdown's fall, from where the fall lands; both fit the knockdown"))
	TObjectPtr<UAnimSequenceBase> KnockdownGetUp;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Clips", meta = (ToolTip = "Clip second the knockdown's fall starts from (skips a standing stagger: the hit is the impact)"))
	float KnockdownFallFrom = 0.35f;

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

	// Strafing when locked on (the gap study, 2026-11-25): he keeps facing the target and the jog clip follows the direction
	// he moves in, played full body on the slot like the guard walk. Sprinting breaks it, as in souls games.
	UPROPERTY(EditDefaultsOnly, Category = "Player|Strafe")
	TObjectPtr<UAnimSequenceBase> StrafeForward;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Strafe")
	TObjectPtr<UAnimSequenceBase> StrafeBack;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Strafe")
	TObjectPtr<UAnimSequenceBase> StrafeLeft;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Strafe")
	TObjectPtr<UAnimSequenceBase> StrafeRight;

	UPROPERTY(EditAnywhere, Category = "Player|Strafe", meta = (ToolTip = "cm/s the jog clips are authored for; slower plays the legs slower, down to 0.75x"))
	float StrafeAuthoredSpeed = 400.f;

	UPROPERTY(EditAnywhere, Category = "Player|Strafe", meta = (ToolTip = "cm/s moving sideways or back while locked on (forward stays Run Speed)"))
	float StrafeSideSpeed = 350.f;

	// Orientation warping (after the 6d play, 2026-11-25): on a diagonal the nearest of the four clips steps up to 60 degrees off,
	// so the skeleton turns by the difference and the spine turns back part of it (OrientationWarp.h). Strafe and guard walk.
	UPROPERTY(EditAnywhere, Category = "Player|Strafe", meta = (ToolTip = "Share of the warp the spine turns back so the chest stays toward the target (0 none, 1 all)"))
	float WarpChestCounter = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Player|Strafe", meta = (ToolTip = "How fast the warp follows its target, per second (15: about 90% in 0.15 s, the clip switch's blend)"))
	float WarpInterpSpeed = 15.f;

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
	int32 GetVisibleAfterimages() const;
	const UAnimSequenceBase* GetStrafeClip() const { return StrafeClipShown; }
	const UAnimSequenceBase* GetGuardClip() const { return GuardClipShown; }
	float GetWarpAngle() const { return WarpAngle; } // degrees the skeleton is turned now (+ right)
	float GetWarpTarget() const { return WarpTarget; }
	bool IsLockedOn() const;
	FString DebugLine() const;
	FString LastEventText() const { return LastEventTime > 0.f ? LastEvent : FString(); }

	// The reset contract (spec section 15): full health, both heal charges, full stamina, no fragments,
	// back at the start.
	void ResetPlayer();
	void AddFragmentForTesting();
	void SetStaminaForTesting(float Value) { Actions.Stamina.Current = Value; }

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
	void TickEvadeVisuals(float DeltaSeconds);
	void TickDodgeStop();
	void TickStrafeVisuals();
	void StopStrafe();
	void TickWarp(float DeltaSeconds);
	// Which of four clips shows a movement at AngleDeg (0 ahead, +90 right), keeping Current within its hysteresis.
	UAnimSequenceBase* PickDirectionalClip(float AngleDeg, const UAnimSequenceBase* Current, UAnimSequenceBase* Forward, UAnimSequenceBase* Right,
		UAnimSequenceBase* Back, UAnimSequenceBase* Left);
	// The warp that makes Shown step along AngleDeg; 0 if Shown isn't one of the four.
	float WarpFor(float AngleDeg, const UAnimSequenceBase* Shown, const UAnimSequenceBase* Forward, const UAnimSequenceBase* Right,
		const UAnimSequenceBase* Back, const UAnimSequenceBase* Left) const;
	bool HasStrafeClips() const { return StrafeForward && StrafeBack && StrafeLeft && StrafeRight; }
	void ReleaseDodgePush();
	void EndEvadeVisuals(float BlendOut);
	void MakeAfterimagePool();
	void SpawnAfterimage();
	void TickAfterimages(float DeltaSeconds);
	void HideAfterimages();
	void SetSprinting(bool bOn);
	void TickGuardVisuals();
	void PushBack(float Distance, float Duration);
	void TickChargeEffect();
	void TickMovement();
	void SyncCombatant();
	void ApplyGuardTuning();
	void TickCamera();
	void PlayReaction(bool bKnockdown);
	float KnockdownGetUpIn = -1.f; // gameplay seconds until the knockdown's get-up clip starts; below 0 when none waits
	float KnockdownRate = 1.f;
	UAnimMontage* PlayClip(UAnimSequenceBase* Clip, float Rate, float BlendIn, float BlendOut, int32 Loops = 1, float StartAt = 0.f);
	void StopClip(float BlendOut);
	void FaceForAction();
	AActor* LungeTarget() const;
	class ABossCharacter* FindBoss() const;
	void StartRiposte();
	void TickRiposteSnap(float DeltaSeconds);
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

	UPROPERTY()
	TArray<TObjectPtr<class UPoseableMeshComponent>> Afterimages;

	UPROPERTY()
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> AfterimageMaterials;

	TArray<float> AfterimageAge;    // per copy, seconds since it appeared; below 0 when hidden
	int32 AfterimagesShown = 0;     // copies spawned in this evade so far
	int32 EvadePhase = 0;           // 0 the creeping lean, 1 the push-off, 2 the plant
	bool bEvadeBackstep = false;

	UPROPERTY()
	TObjectPtr<UAnimMontage> DodgeStopMontage; // the plant, played as an ordinary clip so moving ends it

	UPROPERTY()
	TObjectPtr<UAnimSequenceBase> StrafeClipShown; // which jog clip the strafe has on the slot
	float StrafeClipAge = 0.f;

	float WarpAngle = 0.f;   // degrees the skeleton is turned (smoothed toward WarpTarget)
	float WarpTarget = 0.f;  // set each tick by the strafe or the guard walk; 0 otherwise
	bool bWarpWarned = false;

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

	mutable TWeakObjectPtr<class ABossCharacter> CachedBoss;
	mutable double NextBossSearch = 0.0;
	FVector SnapFrom = FVector::ZeroVector;
	FVector SnapTo = FVector::ZeroVector;
	float SnapFromYaw = 0.f;
	float SnapToYaw = 0.f;
	float SnapLeft = 0.f;           // the finishing blow's snap, gameplay seconds left
};
