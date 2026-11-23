// Attack timing in real seconds, mapped onto animation clip time (spec section 12, Step 2).
// The gameplay clock is authoritative: the animation is posed to the clip time computed here.
#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTypes.h"

enum class EAttackPhase : uint8 { Windup, Hold, Active, Recovery, Done };

struct FAttackTiming
{
	// Real seconds
	float Windup = 0.f;      // from the start to the active window, including the hold
	float Hold = 0.f;        // frozen time inside the windup
	float Active = 0.f;
	float Recovery = 0.f;
	float ChainFrom = -1.f;  // the next attack may start from here (-1 = never)

	// Clip seconds (measured markers)
	float ClipStartAt = 0.f; // the windup starts here (a borrowed clip trimmed at its start); 0 = the clip's start
	float ClipHoldAt = 0.f;  // the pose held during the hold (<= ClipActiveStart)
	float ClipHoldTo = 0.f;  // set past ClipHoldAt, the hold creeps steadily on to this pose instead of freezing (<= ClipActiveStart)
	float ClipActiveStart = 0.f;
	float ClipActiveEnd = 0.f;
	float ClipEnd = 0.f;

	bool bEaseIntoHold = false; // the raise slows smoothly into the held pose instead of stopping dead
	float Total() const { return Windup + Active + Recovery; } // free to move or dodge from here
};

struct FAttackSample
{
	EAttackPhase Phase = EAttackPhase::Windup;
	float ClipTime = 0.f;
	bool bChainOpen = false;
	bool bFree = false;
};

FAttackSample EvaluateAttack(const FAttackTiming& T, float Time);

// True if any part of [From, To] falls inside the active window, so a long frame never skips it.
bool ActiveOverlap(const FAttackTiming& T, float From, float To);

// The attacker's armour in each phase (spec section 9): windup and hold get the windup armour, the
// active window gets the active armour, recovery has none.
EArmor ArmorAt(EAttackPhase Phase, EArmor WindupArmor, EArmor ActiveArmor);

// When an attack's lunge runs, in real seconds from the start: from StartsBeforeActive before the
// active window to EndsIntoActive (a fraction) of the way through it.
struct FLungeWindow
{
	float Start = 0.f;
	float Duration = 0.f;
};
FLungeWindow LungeWindow(const FAttackTiming& T, float StartsBeforeActive, float EndsIntoActive);

// The branch point (the charged heavy): an unresolved branch clamps the step so the attack stops exactly
// at BranchAt and waits there; once resolved, or with no branch, the step is unchanged.
struct FBranchStep
{
	float Step = 0.f;
	bool bReachedBranch = false;
};
FBranchStep StepToBranch(float Elapsed, float Step, float BranchAt, bool bResolved);

// A branch must come before anything happens: at least 1/30 s before the attack's windup ends. On the same clip it
// must also be seamless (the same pose at BranchAt in both timings); a variant on another clip restarts its own
// timeline at the branch with a crossfade, so its pose is free. Messages start with "Error:".
TArray<FString> ValidateBranch(const FAttackTiming& Mine, const FAttackTiming& Variant, bool bSameClip, float BranchAt);

// Largest step an attack advances in one tick (1/30 s), so a hitch can never jump over an active window.
float ClampAttackStep(float DeltaSeconds);

// Messages starting "Error:" (unusable data) or "Warning:" (a segment stretched more than 2x, which reads as slow motion).
TArray<FString> ValidateTiming(const FAttackTiming& T);
