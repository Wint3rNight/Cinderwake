// The single owner of the boss's state (spec section 12). Other code requests transitions with a
// priority; the machine checks legality, priority and life, accepts or rejects, records every request,
// and times every non-terminal state out so nothing can lock him. Plain C++, so it's unit tested.
#pragma once

#include "CoreMinimal.h"

enum class EBossState : uint8 { Intro, Evaluate, Approach, Turn, Neutral, Attack, Stagger, Paused, Dead, CastBroken };

// Higher wins. A request needs at least the priority his current state holds.
enum class EBossPriority : uint8 { Decision, Recovery, CommittedAttack, Stagger, Scripted, Transformation, Death };

struct FBossRequestRecord
{
	EBossState From = EBossState::Intro;
	EBossState To = EBossState::Intro;
	EBossPriority Priority = EBossPriority::Decision;
	bool bAccepted = false;
	FString Reason;
	double Time = 0.0;
};

class BOSSTEST_API FBossStateMachine
{
public:
	static constexpr int32 HistorySize = 16;

	// Timeout < 0 uses the state's default. Returns whether the request was accepted.
	bool Request(EBossState To, EBossPriority Priority, const FString& Reason, float Timeout = -1.f);
	// The current state's own natural end (attack over, stagger over, intro over): legality only, no
	// priority check. Leaving an attack this way plays a pending safe-point request instead, if any.
	bool Complete(EBossState To, const FString& Reason, float Timeout = -1.f);
	// Waits for the next safe point if he's attacking (the attack's recovery, or its end), else
	// requests now. The deferred stagger is the first user; the transformation will be the next.
	// Timeout < 0 uses the state's default when it plays. A lower-priority request never replaces a pending one.
	void RequestAtSafePoint(EBossState To, EBossPriority Priority, const FString& Reason, float Timeout = -1.f);
	bool HasPendingSafePoint() const { return bHasPending; }
	// Called on every transition: accepted requests, completions, safe points and timeouts.
	TFunction<void(EBossState From, EBossState To)> OnTransition;
	// Called for every record: accepted, rejected, queued and timed out (the log).
	TFunction<void(const FBossRequestRecord& Record, bool bTimeout)> OnRecord;
	int32 GetTimeoutCount() const { return TimeoutCount; }
	// A committed attack's recovery only holds recovery priority (a stagger can land, decisions still can't).
	void SetAttackRecovering(bool bRecovering);
	// A sequence's next step (Step 5): the attack goes on as a fresh commitment (committed priority, its own state clock
	// and timeout). If a safe-point request is pending, that plays instead and this returns false.
	bool NextStep(const FString& Reason, float Timeout = -1.f);
	// Advances the state's clock. Returns true when a timeout fired: it's recorded and he goes back to Evaluate.
	bool Tick(float Dt);
	void Reset(float IntroTimeout = -1.f);   // back to the intro (its clip length plus 1 s), no history

	EBossState GetState() const { return State; }
	float GetStateTime() const { return StateTime; }
	float GetTimeout() const { return Timeout; }
	EBossPriority GetHeldPriority() const;
	const TArray<FBossRequestRecord>& GetHistory() const { return History; }

	static const TCHAR* StateName(EBossState S);
	static const TCHAR* PriorityName(EBossPriority P);
	static bool IsLegal(EBossState From, EBossState To);
	static float DefaultTimeout(EBossState S);

private:
	void Record(EBossState From, EBossState To, EBossPriority Priority, bool bAccepted, const FString& Reason, bool bTimeout = false);
	void Enter(EBossState To, float InTimeout);
	bool PlayPending(const FString& Why);

	struct FPending
	{
		EBossState To = EBossState::Evaluate;
		EBossPriority Priority = EBossPriority::Decision;
		FString Reason;
		float Timeout = -1.f;
	};
	FPending Pending;
	int32 TimeoutCount = 0;
	bool bHasPending = false;

	EBossState State = EBossState::Intro;
	float StateTime = 0.f;
	float Timeout = 10.f;
	bool bAttackRecovering = false;
	double Clock = 0.0;
	TArray<FBossRequestRecord> History;
};
