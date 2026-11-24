// The stand-in brain (Step 5): picks among his actions by range, angle, cooldown and a repeat penalty, just enough
// to fight all nine until Step 6 builds the real director (bands, scoring tables, combos, circling). Plain C++, so
// it's unit tested.
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

enum class EBossChoice : uint8 { Wait, Turn, Swing, Approach, Attack };

// Where the player must be for an action (spec section 5, "Angles").
enum class EBossAngleRule : uint8
{
	FrontAndSides,      // melee: in front or to his side
	FrontOnly,          // Soul Mark, Judgment Slam
	FrontSidesAndRear,  // Reverse Sweep: also the rear quarter, scored up there
	Any,                // Executioner's Advance
};

struct FBossActionOption
{
	int32 Id = 0;
	float MinRange = 0.f;               // cm, edge to edge
	float MaxRange = 0.f;
	EBossAngleRule Angle = EBossAngleRule::FrontAndSides;
	float RearWeight = 1.f;             // FrontSidesAndRear: the weight's scale in the rear quarter
	float Weight = 1.f;
	float CooldownLeft = 0.f;           // above 0: not available
	bool bNeedsPunishTrigger = false;   // Punishment Sweep: only while the player keeps attacking
};

struct FBossSituation
{
	float EdgeDistance = 0.f;           // cm, edge to edge
	float AngleDeg = 0.f;               // 0 = straight ahead, 180 = behind him
	bool bPlayerDown = false;
	bool bPunishTrigger = false;        // the player landed hits on him repeatedly (Step 7's analyzer replaces this)
	int32 LastActionId = -1;
};

struct FBossChoiceResult
{
	EBossChoice Choice = EBossChoice::Wait;
	int32 ActionId = -1;                // when Choice == Attack
};

struct FBossBrainTuning
{
	float FrontAngle = 45.f;            // "in front"
	float SideAngle = 90.f;             // front and sides
	float RearQuarterAngle = 135.f;     // past this he's directly behind, and nothing is valid but Any
	float RepeatScale = 0.25f;          // the last action's weight is scaled by this
};

// Valid actions are weighed (Weight, the rear-quarter scale, the repeat penalty) and one is rolled. With none valid:
// wait if the player is down or there are no actions; turn if one would fit at this distance once he faces you;
// otherwise approach.
FBossChoiceResult ChooseBossAction(const FBossSituation& S, TArrayView<const FBossActionOption> Options, FRandomStream& Rng,
	const FBossBrainTuning& Tuning = FBossBrainTuning());

// Step 4's placeholder (Executioner's Swing only), kept until the character switches over.
struct FBossPlaceholderTuning
{
	float SwingRange = 430.f;
	float MeleeAngle = 90.f;
};
EBossChoice ChoosePlaceholderAction(float EdgeDistance, float AngleDeg, bool bPlayerDown, const FBossPlaceholderTuning& Tuning = FBossPlaceholderTuning());
