// One attack as editable data (Step 2). Timings are real seconds; markers are measured clip seconds.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/AttackTimeline.h"
#include "Combat/CombatTypes.h"
#include "AttackDefinition.generated.h"

class UAnimSequenceBase;
class UParticleSystem;
class UMaterialInterface;
class UStaticMesh;

UENUM(BlueprintType)
enum class EAttackHitPolicy : uint8
{
	SingleHitPerTarget,
	MultiHit,
	PersistentContact,
	Projectile
};

// The attacker's armour in a phase (spec section 9). Windup: light hits can't stagger.
UENUM(BlueprintType)
enum class EAttackArmor : uint8
{
	None,
	Windup,
	Full
};

// Where a lunge aims (Step 5): along the attacker's facing, or at the point where a moving target will be.
UENUM(BlueprintType)
enum class ELungeAim : uint8
{
	Facing,
	Intercept
};

// A delivery launched by the attack besides its weapon sweep (Step 5, spec section 12).
UENUM(BlueprintType)
enum class EAttackDelivery : uint8
{
	None,
	Ring,        // a shockwave from the floor under the socket (Judgment Slam)
	Projectile   // a straight shot from the socket at the target (Soul Mark)
};

// An effect tied to the attack's clip time (Step 5): the hammer's glow during a hold, dust on impact.
USTRUCT(BlueprintType)
struct FAttackCue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Cue")
	TObjectPtr<UParticleSystem> Effect;

	UPROPERTY(EditAnywhere, Category = "Cue", meta = (ToolTip = "Socket on the attacker's mesh; None = at his feet"))
	FName Socket;

	UPROPERTY(EditAnywhere, Category = "Cue", meta = (ToolTip = "Clip second it starts at (extended: a lead-in comes before Clip Start At, a tail after Clip End)"))
	float ClipFrom = 0.f;

	UPROPERTY(EditAnywhere, Category = "Cue", meta = (ToolTip = "Clip second it stops at; at or before Clip From = when the attack ends. One-shots ignore it"))
	float ClipTo = -1.f;

	UPROPERTY(EditAnywhere, Category = "Cue")
	float Scale = 1.f;

	UPROPERTY(EditAnywhere, Category = "Cue", meta = (ToolTip = "Follows the socket; otherwise spawned where the socket is"))
	bool bAttached = true;

	UPROPERTY(EditAnywhere, Category = "Cue", meta = (ToolTip = "Plays out by itself (a burst); otherwise it's stopped at Clip To or the attack's end"))
	bool bOneShot = false;
};

UCLASS(BlueprintType)
class BOSSTEST_API UAttackDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---- Animation: the clip and its measured markers (clip seconds) ----
	UPROPERTY(EditAnywhere, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Clip;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Seconds the clip blends in over whatever played before (a pose far from the last one wants longer)"))
	float BlendIn = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Blend in on a smooth curve instead of a straight crossfade"))
	bool bEaseBlendIn = false;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Clip second the attack starts from (a borrowed clip trimmed at its start); 0 = the clip's start"))
	float ClipStartAt = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Clip second of the pose held during the hold (at most Clip Active Start)"))
	float ClipHoldAt = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Set past Clip Hold At, the hold creeps steadily on to this clip second instead of freezing (at most Clip Active Start; not with Ease Into Hold)"))
	float ClipHoldTo = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Clip second where the hit window opens (measured from the weapon's speed)"))
	float ClipActiveStart = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Clip second where the hit window closes"))
	float ClipActiveEnd = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Clip second where the attack ends and blends out"))
	float ClipEnd = 0.f;

	// ---- Timing: real seconds ----
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ToolTip = "Real seconds from the start to the hit window, including the hold"))
	float Windup = 0.f;

	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ToolTip = "Real seconds at the loaded pose, inside the windup"))
	float Hold = 0.f;

	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ToolTip = "Slow smoothly into the held pose instead of freezing on it"))
	bool bEaseIntoHold = false;

	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ToolTip = "An eased hold reaches its stop after this many seconds, then holds still. 0 = it eases over the whole hold"))
	float HoldEase = 0.f;

	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ToolTip = "Real seconds held inside the recovery (the hammer left in the floor)"))
	float RecoveryHold = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Clip second (extended: past Clip End is the tail) where the recovery hold starts"))
	float ClipRecoveryHoldAt = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ToolTip = "Set past Clip Recovery Hold At, the recovery hold creeps on to here instead of holding still"))
	float ClipRecoveryHoldTo = 0.f;

	// ---- Other clips around the main one (Step 5) ----
	UPROPERTY(EditAnywhere, Category = "Animation|Spans", meta = (ToolTip = "Played before the main clip, as part of the windup (Double Execution's re-raise)"))
	TObjectPtr<UAnimSequenceBase> LeadInClip;

	UPROPERTY(EditAnywhere, Category = "Animation|Spans")
	float LeadInFrom = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation|Spans")
	float LeadInTo = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation|Spans", meta = (ToolTip = "Played after the main clip's Clip End, as part of the recovery (a return to idle)"))
	TObjectPtr<UAnimSequenceBase> TailClip;

	UPROPERTY(EditAnywhere, Category = "Animation|Spans")
	float TailFrom = 0.f;

	UPROPERTY(EditAnywhere, Category = "Animation|Spans")
	float TailTo = 0.f;


	UPROPERTY(EditAnywhere, Category = "Timing")
	float Active = 0.f;

	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ToolTip = "Real seconds after the hit window before you can move or dodge"))
	float Recovery = 0.f;

	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ToolTip = "The next attack may start from this real second. -1 = never"))
	float ChainFrom = -1.f;

	// ---- Damage ----
	UPROPERTY(EditAnywhere, Category = "Damage")
	float Damage = 0.f;

	UPROPERTY(EditAnywhere, Category = "Damage")
	float PoiseDamage = 0.f;

	UPROPERTY(EditAnywhere, Category = "Damage", meta = (ToolTip = "Heavy, dash and charged attacks: these can stagger through windup armour"))
	bool bHeavy = false;

	UPROPERTY(EditAnywhere, Category = "Damage")
	bool bKnockdown = false;

	UPROPERTY(EditAnywhere, Category = "Damage", meta = (ToolTip = "Used from Step 3"))
	float StaminaCost = 0.f;

	UPROPERTY(EditAnywhere, Category = "Damage", meta = (ToolTip = "Real seconds both fighters freeze when this lands"))
	float HitStop = 0.055f;

	UPROPERTY(EditAnywhere, Category = "Damage")
	EAttackHitPolicy HitPolicy = EAttackHitPolicy::SingleHitPerTarget;

	UPROPERTY(EditAnywhere, Category = "Damage")
	float MultiHitInterval = 0.f;

	UPROPERTY(EditAnywhere, Category = "Damage")
	int32 MultiHitMax = 1;

	// ---- Weapon sweep ----
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ToolTip = "Socket at one end of the swept blade or hammer"))
	FName SocketStart;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ToolTip = "Socket at the other end"))
	FName SocketEnd;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ToolTip = "Sweep radius in cm"))
	float Radius = 22.f;

	// ---- Movement ----
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ToolTip = "The AI's range for this attack: edge-to-edge cm inside which it's worth starting (reach plus lunge). The hitbox is always the swept weapon"))
	float Range = 0.f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ToolTip = "Max cm pushed toward the target when the hit window opens"))
	float LungeCap = 0.f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ToolTip = "The lunge stops this far from the target, edge to edge (cm)"))
	float LungeStopDistance = 40.f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ToolTip = "Real seconds before the hit window that the lunge starts (and its distance is measured). 0 = at the window"))
	float LungeStartsBeforeActive = 0.f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ToolTip = "How far into the hit window the lunge ends, 0-1. 1 = the window's end", ClampMin = "0", ClampMax = "1"))
	float LungeEndsIntoActive = 1.f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ToolTip = "Ease the lunge in and out instead of a constant speed"))
	bool bEaseLunge = false;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ToolTip = "Facing: along where the attacker faces. Intercept: at where a moving target will be when the lunge ends (the Advance's dash)"))
	ELungeAim LungeAim = ELungeAim::Facing;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ToolTip = "Intercept: the most the aim leads the target, cm (a dodge's burst can't fling him past you)"))
	float InterceptLeadMax = 200.f;

	// ---- Tracking: turning toward the target during the windup ----
	UPROPERTY(EditAnywhere, Category = "Tracking", meta = (ToolTip = "Degrees per second the attacker turns toward the target. 0 = none"))
	float TrackingRate = 0.f;

	UPROPERTY(EditAnywhere, Category = "Tracking", meta = (ToolTip = "Real seconds before the hit window that tracking stops, so a late sidestep works"))
	float TrackingStopsBeforeActive = 0.f;

	// ---- Branch: a held variant (the charged heavy) ----
	UPROPERTY(EditAnywhere, Category = "Branch", meta = (ToolTip = "Real second where the attack waits for its owner to choose this or Branch Variant (the charge). -1 = no branch"))
	float BranchAt = -1.f;

	UPROPERTY(EditAnywhere, Category = "Branch", meta = (ToolTip = "Same clip: its pose at Branch At must match this attack's. Another clip: it crossfades in and its own timeline starts at the branch"))
	TObjectPtr<UAttackDefinition> BranchVariant;

	// ---- Delivery ----
	UPROPERTY(EditAnywhere, Category = "Damage", meta = (ToolTip = "A guard can't stop it (the Finale's impact, soul attacks)"))
	bool bUnblockable = false;

	UPROPERTY(EditAnywhere, Category = "Delivery")
	EAttackDelivery Delivery = EAttackDelivery::None;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "Clip second (extended) it launches at: the floor contact, the release"))
	float DeliveryClipAt = 0.f;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "Where it starts: the ring under this socket, the projectile at it"))
	FName DeliverySocket;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "cm/s: the ring's growth or the projectile's speed"))
	float DeliverySpeed = 1000.f;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "cm: the ring's largest radius, or the projectile's range"))
	float DeliveryReach = 500.f;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "cm: the projectile's radius"))
	float DeliveryRadius = 20.f;

	UPROPERTY(EditAnywhere, Category = "Delivery")
	float DeliveryDamage = 0.f;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "A soul hit: a landed one takes a fragment of max HP (spec section 8)"))
	bool bDeliverySoulFragment = false;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "Real seconds the victim freezes when it lands"))
	float DeliveryHitStop = 0.07f;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "An effect at the ring's centre (the impact), or the projectile's look"))
	TObjectPtr<UParticleSystem> DeliveryEffect;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "The ring's edge: a flat mesh on the floor rescaled to the ring's radius every tick, so the look always matches the hitbox"))
	TObjectPtr<UStaticMesh> DeliveryRingMesh;

	UPROPERTY(EditAnywhere, Category = "Delivery")
	TObjectPtr<UMaterialInterface> DeliveryRingMaterial;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "The ring mesh's scale per cm of radius (a 100 cm plane whose texture's ring touches its edge: 0.02)"))
	float DeliveryRingMeshScale = 0.02f;

	UPROPERTY(EditAnywhere, Category = "Delivery")
	float DeliveryEffectScale = 1.f;

	UPROPERTY(EditAnywhere, Category = "Delivery", meta = (ToolTip = "Where the projectile ends: on a hit or a wall"))
	TObjectPtr<UParticleSystem> DeliveryImpactEffect;

	// ---- Effects (Step 5) ----
	UPROPERTY(EditAnywhere, Category = "Effects")
	TArray<FAttackCue> Cues;

	UPROPERTY(EditAnywhere, Category = "Effects", meta = (ToolTip = "A Cascade anim trail between the two weapon sockets while the hit window is open"))
	TObjectPtr<UParticleSystem> TrailEffect;

	UPROPERTY(EditAnywhere, Category = "Effects", meta = (ToolTip = "Played at the weapon's end socket when the sweep lands"))
	TObjectPtr<UParticleSystem> HitEffect;

	UPROPERTY(EditAnywhere, Category = "Effects")
	float HitEffectScale = 1.f;

	// ---- Armour: whether hits can stagger the attacker in each phase (recovery never has any) ----
	UPROPERTY(EditAnywhere, Category = "Armour")
	EAttackArmor WindupArmor = EAttackArmor::None;

	UPROPERTY(EditAnywhere, Category = "Armour")
	EAttackArmor ActiveArmor = EAttackArmor::None;

	FAttackTiming GetTiming() const;
	EHitPolicy GetHitPolicy() const;
	EArmor GetWindupArmor() const { return ToArmor(WindupArmor); }
	EArmor GetActiveArmor() const { return ToArmor(ActiveArmor); }
	static EArmor ToArmor(EAttackArmor Armor) { return Armor == EAttackArmor::Full ? EArmor::Full : Armor == EAttackArmor::Windup ? EArmor::Windup : EArmor::None; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
