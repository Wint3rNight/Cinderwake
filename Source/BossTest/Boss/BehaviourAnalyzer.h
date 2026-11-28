// Step 7: the behaviour analyzer (spec section 10). It watches what the player visibly does, a quarter second late, and
// keeps six habit scores (0-1); it decides nothing itself. The director multiplies an action's weight by the habit it
// counters, so a habit makes its answer likelier, never certain. Plain C++, so it's unit tested.
#pragma once

#include "CoreMinimal.h"

enum class EBossHabit : uint8 { Dodging, Attacking, Disengaged, Circling, HealingAtRange, HitAndRun };
constexpr int32 BossHabitCount = 6;

constexpr uint8 HabitBit(EBossHabit H) { return static_cast<uint8>(1u << static_cast<uint8>(H)); }
BOSSTEST_API const TCHAR* HabitName(int32 Habit);

struct FHabitSnapshot
{
	float Scores[BossHabitCount] = {};
	float Get(EBossHabit H) const { return Scores[static_cast<int32>(H)]; }
	void Set(EBossHabit H, float Value) { Scores[static_cast<int32>(H)] = Value; }
};

// Section 10's maximum modifier per habit: frequent dodging x1.6 on delayed attacks, and so on.
struct FHabitCaps
{
	float Values[BossHabitCount] = { 1.6f, 1.5f, 1.5f, 1.4f, 1.3f, 1.3f };
};

// The modifier for an action that counters the habits in Mask: 1 + (cap - 1) x score for each, and only the largest counts
// (section 10: modifiers never stack). OutDriver gets the habit behind it, or -1 when nothing raised it.
BOSSTEST_API float HabitScale(uint8 Mask, const FHabitSnapshot& Habits, const FHabitCaps& Caps, int32* OutDriver = nullptr);

// One frame of what he can see of the player.
struct FPlayerObservation
{
	float Dt = 0.f;
	float EdgeCm = 0.f;                 // capsule edge to capsule edge
	FVector2D ToPlayer = FVector2D::ZeroVector;      // from his centre to the player's, cm
	FVector2D PlayerVelocity = FVector2D::ZeroVector; // the player's own, cm/s (his movement doesn't make you a circler)
	bool bBossNeutral = false;          // he isn't attacking, staggered or recoiling: attacks now press into his neutral
	bool bDodgeStarted = false;         // a dodge began this frame (a chained re-dodge counts)
	bool bAttackStarted = false;        // an attack began this frame (a chained hit counts)
	bool bHealing = false;              // the player is in the heal
	bool bLandedHit = false;            // the player's hit landed on him this frame
};

// Section 10's habit inputs.
struct FAnalyzerTuning
{
	float TimeConstant = 6.f;           // every score is an average over about this long, so two dodges don't define you
	float PerceptionDelay = 0.25f;      // he sees you this late (spec principle 2: no input reading)
	float DodgeWindow = 4.f;            // dodges per 4 s, full at 3
	float DodgeFull = 3.f;
	float AttackWindow = 5.f;           // attacks started while he's in neutral, per 5 s, full at 4
	float AttackFull = 4.f;
	float FarFrom = 600.f;              // disengaged: in Far or beyond (edge, cm)...
	float RunSpeed = 300.f;             // ...and moving faster than 3 m/s
	float CircleRate = 60.f;            // circling: your angle around him changing faster than 60 deg/s...
	float CircleWithin = 600.f;         // ...within Medium (further out, that's keeping away, not circling)
	float HealRangeFrom = 600.f;        // a heal started beyond 6 m is a heal at range; one is a full score, decaying
	float HealFull = 1.f;
	float HitRunDistance = 400.f;       // hit and run: retreating more than 4 m...
	float HitRunWindow = 1.5f;          // ...within 1.5 s of landing a hit; full at 3 within the time constant
	float HitRunFull = 3.f;
};

class BOSSTEST_API FBehaviourAnalyzer
{
public:
	void Init(const FAnalyzerTuning& InTuning) { Tuning = InTuning; Reset(); }
	void Reset();   // a retry: no history, no habits

	// Queued and applied once it's PerceptionDelay old, so the snapshot always describes the player a quarter second ago.
	void Observe(const FPlayerObservation& Obs);
	const FHabitSnapshot& GetSnapshot() const { return Snapshot; }
	bool IsHealingSeen() const { return bHealSeen; }   // the player is healing, as far as he can tell yet
	int32 GetHealsSeen() const { return HealsSeen; }

private:
	void Apply(const FPlayerObservation& Obs);

	struct FQueued
	{
		double Time = 0.0;
		FPlayerObservation Obs;
	};
	FAnalyzerTuning Tuning;
	TArray<FQueued> Pending;
	double Clock = 0.0;
	// Event rates (per second, decaying with the time constant) and time shares (0-1).
	float DodgeRate = 0.f;
	float AttackRate = 0.f;
	float HealRate = 0.f;
	float HitRunRate = 0.f;
	float Disengaged = 0.f;
	float Circling = 0.f;
	bool bHealSeen = false;
	int32 HealsSeen = 0;
	bool bHitRunArmed = false;
	float SinceHit = 0.f;
	float HitFromEdge = 0.f;
	FHabitSnapshot Snapshot;
};
