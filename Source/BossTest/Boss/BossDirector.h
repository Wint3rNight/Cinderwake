// His phase-one director (Step 6, spec section 10): owns every decision rule and timer, as plain C++ so it's unit tested.
// The character perceives (distance, angle, the analyzer's habits), executes what it's told and reports back (an action
// committed, a step started, his beat broken by a hit). Step 7: an action's weight carries the modifier of the habit it
// counters, and when a real habit drove the pick he plays a reading beat first.
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Boss/BehaviourAnalyzer.h"

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
	uint8 Habits = 0;                   // the habits it counters (HabitBit), Step 7
};

// A combo (section 5): its opener and the follow-ups, by action id. Chain points are the character's data.
struct FBossComboOption
{
	int32 Id = 0;
	int32 Opener = 0;
	TArray<int32> FollowUps;
	float Weight = 1.f;
	float Cooldown = 0.f;               // seconds before it can be rolled again (Combo G: 25)
	bool bNeedsFreshBeat = false;       // only with no major since his last breather (QA: never five swings with one break)
	bool bNeedsSeen = false;            // only once its opener and every follow-up have played this attempt
	int32 Majors = 1;                   // how many majors it counts as for the breather (Combo G: 2)
	uint8 Habits = 0;                   // the habit its purpose answers (A: attacking, B: dodging, C: circling, F: keeping away)
};

struct FBossPerception
{
	float EdgeCm = 0.f;                 // edge to edge
	float AngleDeg = 0.f;               // 0 = straight ahead of him, 180 = behind
	bool bPlayerDown = false;
	bool bPunishTrigger = false;        // the player keeps landing hits, or attacks into his neutral as a habit
	bool bAnswerHeal = false;           // he can see you healing: no movement but a chase from beyond 8 m (section 11)
	FHabitSnapshot Habits;              // the analyzer's scores, a quarter second old
};

struct FBossDecision
{
	EBossMove Move = EBossMove::Wait;
	int32 Action = -1;                  // when Move == Attack
	float Duration = 0.f;               // movements: how long at most
	int32 CircleSign = 1;               // Circle: which way round
	bool bReadingBeat = false;          // a real habit drove this attack: he shows he noticed before he plays it
	int32 Habit = -1;                   // the habit behind the pick's modifier (-1: none)
	float HabitModifier = 1.f;
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
	FBossBandWeights RepositionWeights = { 0.25f, 0.f, 0.f, 0.f }; // making space on purpose brings his Medium moves in (2026-11-25)
	float MustAttackAfter = 2.5f;       // seconds of neutral after which a movement never beats a scoring attack
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
	FHabitCaps HabitCaps;               // section 10's maximum modifier per habit
	float ReadingScore = 0.6f;          // the reading beat needs the habit itself this high, not just its modifier at 1.2
	                                    // (at 1.2 two quick dodges would trigger it: 2026-11-28)
	float ReadingCooldown = 20.f;       // seconds before the same habit gets another beat: he always counters, he rarely announces
	float PursuitHabit = 0.6f;          // keeping away this much starts the chase at once in Far or Extreme (section 10)
	float HealChaseFrom = 800.f;        // answering a heal from beyond 8 m he may chase instead of attacking (section 11)
};

// The aura circle (the user, 2026-11-25: "he's been beating my shi off and maybe once he just circles us like this is who you
// messing with... decrease the frequency to very less"): a rare, earned beat that replaces a breather. Pure, so it's tested.
struct FPostureInputs
{
	int32 RecentHitsOnPlayer = 0;       // his hits that landed in the last window
	bool bRecentKnockdown = false;      // he knocked you down in the last window
	float SinceLastPosture = 1000.f;    // seconds
	EBossBand Band = EBossBand::Close;
	EBossAngleBand Angle = EBossAngleBand::Front;
	bool bPursuit = false;
	float SincePlayerAttack = 1000.f;   // seconds since you were last attacking
	bool bBeatBroken = false;           // a hit just broke his beat
	float Roll = 0.f;                   // 0-1, from his seeded stream
};

struct FPostureRules
{
	int32 HitsNeeded = 2;
	float Cooldown = 60.f;
	float QuietFor = 2.f;
	float Chance = 0.5f;
};

BOSSTEST_API bool ShouldPosture(const FPostureInputs& In, const FPostureRules& Rules);

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
	bool PursuitActive() const;   // 4 s in Far or Extreme, or there now while keeping away is a habit
	void PinAction(int32 Action) { Pinned = Action; } // tests: that action whenever it fits, no combos or beats (-1 = none)
	float GetReadingCooldownLeft(int32 Habit) const { return Habit >= 0 && Habit < BossHabitCount ? ReadingLeft[Habit] : 0.f; }

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
	TSet<int32> Seen;                 // actions that have played this attempt
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
	FHabitSnapshot Habits;            // from the last Observe: combos and pursuit read it
	float ReadingLeft[BossHabitCount] = {};
};
