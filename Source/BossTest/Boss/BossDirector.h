// His phase-one director (Step 6, spec section 10): owns every decision rule and timer, as plain C++ so it's unit tested.
// The character perceives (distance, angle), executes what it's told and reports back (an action committed, a step
// started, his beat broken by a hit). The behaviour analyzer, the reading beat and the perception delay are Step 7:
// until then every habit modifier is 1.
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

enum class EBossBand : uint8 { Close, Medium, Far, Extreme };
enum class EBossAngleBand : uint8 { Front, Side, RearQuarter, Behind };
enum class EBossMove : uint8 { Wait, Attack, Approach, Circle, Reposition, Turn };

// Where the player must be for an action (spec section 5, "Angles").
enum class EBossAngleRule : uint8
{
	FrontAndSides,      // melee: in front or to his side
	FrontOnly,          // Soul Mark, Judgment Slam
	FrontSidesAndRear,  // Reverse Sweep: also the rear quarter, scored up there
	Any,                // Executioner's Advance
};

// Section 10's weight table row for one action.
struct FBossBandWeights
{
	float Close = 0.f;
	float Medium = 0.f;
	float Far = 0.f;
	float Extreme = 0.f;
	float Get(EBossBand Band) const
	{
		return Band == EBossBand::Close ? Close : Band == EBossBand::Medium ? Medium : Band == EBossBand::Far ? Far : Extreme;
	}
};

// One action as the director sees it. Id is its index in the character's action list.
struct FBossOption
{
	int32 Id = 0;
	float MinRange = 0.f;               // cm, edge to edge
	float MaxRange = 0.f;
	EBossAngleRule Angle = EBossAngleRule::FrontAndSides;
	float RearWeight = 1.f;             // FrontSidesAndRear: the weight's scale in the rear quarter
	FBossBandWeights Weights;
	float Cooldown = 0.f;
	bool bNeedsPunishTrigger = false;   // Punishment Sweep, as an opener only
};

// A combo (section 5): its opener and the follow-ups, by action id. Chain points are the character's data.
struct FBossComboOption
{
	int32 Id = 0;
	int32 Opener = 0;
	TArray<int32> FollowUps;
	float Weight = 1.f;
};

struct FBossPerception
{
	float EdgeCm = 0.f;                 // edge to edge
	float AngleDeg = 0.f;               // 0 = straight ahead of him, 180 = behind
	bool bPlayerDown = false;
	bool bPunishTrigger = false;        // the player keeps landing hits (Step 7's analyzer replaces this)
};

struct FBossDecision
{
	EBossMove Move = EBossMove::Wait;
	int32 Action = -1;                  // when Move == Attack
	float Duration = 0.f;               // movements: how long at most
	int32 CircleSign = 1;               // Circle: which way round
	FString Why;                        // the scored options, for the log
};

struct FBossDirectorTuning
{
	float CloseMax = 250.f;             // cm: Close 0-2.5 m, Medium 2.5-6, Far 6-10, Extreme beyond
	float MediumMax = 600.f;
	float FarMax = 1000.f;
	float BandHysteresis = 20.f;
	float FrontAngle = 45.f;
	float SideAngle = 90.f;
	float RearAngle = 135.f;
	float AngleHysteresis = 5.f;
	FBossBandWeights ApproachWeights = { 0.f, 0.8f, 1.0f, 1.0f };
	FBossBandWeights CircleWeights = { 0.8f, 1.0f, 0.5f, 0.f };
	float StopShare = 0.5f;             // at commit: stopping weighs this times the sum of the opener's valid combos' base
	                                    // weights, so every opener stops alone about a third of the time (council, 2026-11-24)
	float CommitPause = 0.25f;          // the commitment cooldown after any action
	float Breather = 1.0f;              // after every second major (the punish windows, Step 6)
	int32 MajorsBeforeBreather = 2;
	float NeutralClearsMajors = 1.0f;   // this much neutral (moving, waiting) is a beat too
	float CircleMin = 1.2f;
	float CircleMax = 2.6f;
	float RepositionMin = 0.7f;
	float RepositionMax = 1.3f;
	float ApproachMax = 3.0f;
	float PursuitAfter = 4.0f;          // seconds in Far or Extreme (leaking at twice the rate outside them)
	float RedecideAfter = 0.4f;         // a movement re-decides on a band change only after this long in it
	int32 RepetitionWindow = 5;         // his last five offensive entries
};

class BOSSTEST_API FBossDirector
{
public:
	void Init(TArray<FBossOption> InOptions, TArray<FBossComboOption> InCombos, const FBossDirectorTuning& InTuning);
	void Reset();   // a retry: bands, cooldowns, history, counters (keeps the options)

	// Every tick: bands (with hysteresis), cooldowns, neutral time, time far away. True when the distance band changed.
	bool Observe(const FBossPerception& P, bool bNeutral, float Dt);
	FBossDecision Decide(const FBossPerception& P, FRandomStream& Rng);
	// At commit: stop after the opener (-1), or one of its valid combos. Stopping weighs StopShare times the valid combos'
	// base weights (not their repetition-scaled ones, which would cancel Step 7's habits), against each at its scaled weight.
	int32 RollCombo(int32 Opener, FRandomStream& Rng) const;
	// Range and angle (and, for an opener, cooldown and its trigger). A follow-up's cooldown was checked at commit.
	bool IsValidNow(int32 Action, const FBossPerception& P, bool bAsFollowUp) const;

	void NoteCommitted(int32 Opener, int32 Combo);  // one major; its entries go into the repetition history
	void NoteStepStarted(int32 Action);              // an action really plays: its cooldown starts
	float PauseAfterAttack();                        // the pause before he decides again (and the count it resets)
	void NoteBeatBroken();                           // a hit ended his pause or movement: the next decision won't circle
	float PauseEndAfterHit(float PauseStart, float PauseLength, float Now) const; // a hit cuts the pause, never below the floor
	bool ShouldRedecide(EBossMove Move, float TimeInMove) const; // after a band change, during a movement
	void NoteNoProgress(EBossMove Move) { ExcludeOnce = Move; }  // that movement sits out the next roll
	bool PursuitActive() const { return FarTime >= Tuning.PursuitAfter; }
	void PinAction(int32 Action) { Pinned = Action; } // tests: that action whenever it fits, no combos (-1 = none)

	EBossBand GetBand() const { return Band; }
	EBossAngleBand GetAngleBand() const { return AngleBand; }
	float GetCooldownLeft(int32 Action) const;
	static int32 ActionKey(int32 Action) { return Action; }
	static int32 ComboKey(int32 Combo) { return 1000 + Combo; }
	float RepetitionScale(int32 Key) const;           // max(0.4, 1 - 0.2n)
	float ComboWeight(int32 Combo) const;             // its weight times the lower of its and its opener's repetition

private:
	static FBossPerception Touching(FBossPerception P);
	const FBossOption* Find(int32 Action) const;
	bool AngleFits(const FBossOption& O) const;
	float AngleScale(const FBossOption& O) const;
	bool ComboValid(const FBossComboOption& C) const;
	EBossBand RawBand(float Edge) const;
	EBossAngleBand RawAngleBand(float Angle) const;

	TArray<FBossOption> Options;
	TArray<FBossComboOption> Combos;
	FBossDirectorTuning Tuning;
	TMap<int32, float> Cooldowns;
	TArray<int32> History;            // offensive entries, newest last
	int32 LastCombo = -1;
	int32 Majors = 0;
	float NeutralTime = 0.f;
	float FarTime = 0.f;
	EBossBand Band = EBossBand::Close;
	EBossAngleBand AngleBand = EBossAngleBand::Front;
	bool bBandKnown = false;
	bool bLastChangeCloser = false;
	bool bJustHit = false;
	EBossMove ExcludeOnce = EBossMove::Wait; // Wait = nothing excluded
	int32 Pinned = -1;
};
