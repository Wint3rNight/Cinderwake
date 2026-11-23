// One attack as editable data (Step 2). Timings are real seconds; markers are measured clip seconds.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/AttackTimeline.h"
#include "Combat/CombatTypes.h"
#include "AttackDefinition.generated.h"

class UAnimSequenceBase;

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
