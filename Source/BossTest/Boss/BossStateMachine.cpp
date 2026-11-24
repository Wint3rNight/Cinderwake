#include "Boss/BossStateMachine.h"

namespace
{
	// Timers accumulate frame by frame, so compare with a little slack: 3.13 + 0.01 must reach 3.14.
	constexpr float StateTimeSlack = 1e-4f;
}

const TCHAR* FBossStateMachine::StateName(EBossState S)
{
	switch (S)
	{
	case EBossState::Intro: return TEXT("intro");
	case EBossState::Evaluate: return TEXT("evaluate");
	case EBossState::Approach: return TEXT("approach");
	case EBossState::Turn: return TEXT("turn");
	case EBossState::Neutral: return TEXT("neutral");
	case EBossState::Attack: return TEXT("attack");
	case EBossState::Stagger: return TEXT("stagger");
	case EBossState::Paused: return TEXT("paused");
	case EBossState::Dead: return TEXT("dead");
	case EBossState::CastBroken: return TEXT("cast broken");
	}
	return TEXT("?");
}

const TCHAR* FBossStateMachine::PriorityName(EBossPriority P)
{
	switch (P)
	{
	case EBossPriority::Decision: return TEXT("decision");
	case EBossPriority::Recovery: return TEXT("recovery");
	case EBossPriority::CommittedAttack: return TEXT("committed attack");
	case EBossPriority::Stagger: return TEXT("stagger");
	case EBossPriority::Scripted: return TEXT("scripted");
	case EBossPriority::Transformation: return TEXT("transformation");
	case EBossPriority::Death: return TEXT("death");
	}
	return TEXT("?");
}

bool FBossStateMachine::IsLegal(EBossState From, EBossState To)
{
	if (From == EBossState::Dead)
	{
		return false; // only Reset() leaves death
	}
	if (To == EBossState::Dead)
	{
		return true;
	}
	if (To == EBossState::Stagger)
	{
		return From != EBossState::Stagger; // a stagger doesn't restart itself
	}
	if (To == EBossState::CastBroken)
	{
		return From == EBossState::Attack; // only a cast (an attack) can be broken
	}
	switch (From)
	{
	case EBossState::Intro:
		return To == EBossState::Evaluate || To == EBossState::Paused;
	case EBossState::Evaluate:
		return To == EBossState::Approach || To == EBossState::Turn || To == EBossState::Neutral || To == EBossState::Attack || To == EBossState::Paused;
	case EBossState::Approach:
	case EBossState::Turn:
		return To == EBossState::Evaluate || To == EBossState::Attack || To == EBossState::Paused;
	case EBossState::Neutral:
		return To == EBossState::Evaluate || To == EBossState::Paused;
	case EBossState::Attack:
		return To == EBossState::Neutral || To == EBossState::Evaluate;
	case EBossState::Paused:
		return To == EBossState::Evaluate || To == EBossState::Attack;
	case EBossState::Stagger:
	case EBossState::CastBroken:
		return To == EBossState::Evaluate;
	default:
		return false;
	}
}

float FBossStateMachine::DefaultTimeout(EBossState S)
{
	switch (S)
	{
	case EBossState::Intro: return 10.f;
	case EBossState::Evaluate: return 1.f;     // evaluating should resolve at once
	case EBossState::Approach: return 4.f;     // the approach itself ends at 3 s
	case EBossState::Turn: return 3.f;
	case EBossState::Neutral: return 2.f;
	case EBossState::Attack: return 5.f;       // callers pass the attack's length plus 1 s
	case EBossState::Stagger: return 2.8f;     // 1.8 s plus 1 s (callers pass the stagger's own length plus 1 s)
	case EBossState::CastBroken: return 1.8f;  // the 0.8 s recoil plus 1 s
	default: return -1.f;                      // paused (a debug state) and dead never time out
	}
}

EBossPriority FBossStateMachine::GetHeldPriority() const
{
	switch (State)
	{
	case EBossState::Intro: return EBossPriority::Scripted;
	case EBossState::Attack: return bAttackRecovering ? EBossPriority::Recovery : EBossPriority::CommittedAttack;
	case EBossState::Stagger:
	case EBossState::CastBroken: return EBossPriority::Stagger;
	case EBossState::Dead: return EBossPriority::Death;
	default: return EBossPriority::Decision;
	}
}

void FBossStateMachine::Record(EBossState From, EBossState To, EBossPriority Priority, bool bAccepted, const FString& Reason, bool bTimeout)
{
	FBossRequestRecord& R = History.AddDefaulted_GetRef();
	R.From = From;
	R.To = To;
	R.Priority = Priority;
	R.bAccepted = bAccepted;
	R.Reason = Reason;
	R.Time = Clock;
	const FBossRequestRecord Copy = R;
	if (History.Num() > HistorySize)
	{
		History.RemoveAt(0);
	}
	if (OnRecord)
	{
		OnRecord(Copy, bTimeout);
	}
}

void FBossStateMachine::Enter(EBossState To, float InTimeout)
{
	const EBossState From = State;
	if (From == EBossState::Attack)
	{
		bHasPending = false; // played, or made moot by whatever ended the attack
	}
	State = To;
	StateTime = 0.f;
	Timeout = InTimeout >= 0.f ? InTimeout : DefaultTimeout(To);
	bAttackRecovering = false;
	if (OnTransition)
	{
		OnTransition(From, To);
	}
}

bool FBossStateMachine::Request(EBossState To, EBossPriority Priority, const FString& Reason, float InTimeout)
{
	if (State == EBossState::Dead)
	{
		Record(State, To, Priority, false, Reason + TEXT(" (rejected: dead)"));
		return false;
	}
	if (!IsLegal(State, To))
	{
		Record(State, To, Priority, false, Reason + FString::Printf(TEXT(" (rejected: no %s -> %s)"), StateName(State), StateName(To)));
		return false;
	}
	if (Priority < GetHeldPriority())
	{
		Record(State, To, Priority, false, Reason + FString::Printf(TEXT(" (rejected: %s is below %s)"), PriorityName(Priority), PriorityName(GetHeldPriority())));
		return false;
	}
	Record(State, To, Priority, true, Reason);
	Enter(To, InTimeout);
	return true;
}

bool FBossStateMachine::PlayPending(const FString& Why)
{
	if (!bHasPending)
	{
		return false;
	}
	const FPending P = Pending;
	bHasPending = false;
	return Request(P.To, P.Priority, P.Reason + Why, P.Timeout);
}

bool FBossStateMachine::Complete(EBossState To, const FString& Reason, float InTimeout)
{
	if (State == EBossState::Attack && PlayPending(TEXT(" (at the attack's end)")))
	{
		return true;
	}
	if (State == EBossState::Dead || !IsLegal(State, To))
	{
		Record(State, To, GetHeldPriority(), false, Reason + FString::Printf(TEXT(" (rejected: no %s -> %s)"), StateName(State), StateName(To)));
		return false;
	}
	Record(State, To, GetHeldPriority(), true, Reason);
	Enter(To, InTimeout);
	return true;
}

void FBossStateMachine::RequestAtSafePoint(EBossState To, EBossPriority Priority, const FString& Reason, float InTimeout)
{
	if (State != EBossState::Attack || bAttackRecovering)
	{
		Request(To, Priority, Reason, InTimeout);
		return;
	}
	if (bHasPending && Priority < Pending.Priority)
	{
		Record(State, To, Priority, false, Reason + FString::Printf(TEXT(" (rejected: a %s request already waits)"), PriorityName(Pending.Priority)));
		return;
	}
	Pending.To = To;
	Pending.Priority = Priority;
	Pending.Reason = Reason;
	Pending.Timeout = InTimeout;
	bHasPending = true;
	Record(State, To, Priority, true, Reason + TEXT(" (waits for the safe point)"));
}

bool FBossStateMachine::NextStep(const FString& Reason, float InTimeout)
{
	if (State != EBossState::Attack)
	{
		Record(State, EBossState::Attack, EBossPriority::CommittedAttack, false, Reason + TEXT(" (rejected: not attacking)"));
		return false;
	}
	if (PlayPending(TEXT(" (between steps)")))
	{
		return false;
	}
	bAttackRecovering = false;
	StateTime = 0.f;
	Timeout = InTimeout >= 0.f ? InTimeout : DefaultTimeout(EBossState::Attack);
	Record(EBossState::Attack, EBossState::Attack, EBossPriority::CommittedAttack, true, Reason);
	return true;
}

void FBossStateMachine::SetAttackRecovering(bool bRecovering)
{
	bAttackRecovering = State == EBossState::Attack && bRecovering;
	if (bAttackRecovering)
	{
		PlayPending(TEXT(" (at the start of recovery)"));
	}
}

bool FBossStateMachine::Tick(float Dt)
{
	Clock += Dt;
	StateTime += Dt;
	if (State == EBossState::Dead || Timeout <= 0.f || StateTime < Timeout - StateTimeSlack)
	{
		return false;
	}
	// Stuck: log it and fall back to a state that always recovers (spec section 12). An attack that
	// times out still plays its pending safe-point request (a deferred stagger) instead.
	const FString Why = FString::Printf(TEXT("timeout after %.2f s in %s"), StateTime, StateName(State));
	++TimeoutCount;
	if (State == EBossState::Attack && bHasPending)
	{
		const FPending P = Pending;
		bHasPending = false;
		Record(State, P.To, EBossPriority::Scripted, true, Why + TEXT(", playing ") + P.Reason, true);
		Enter(P.To, P.Timeout);
		return true;
	}
	Record(State, EBossState::Evaluate, EBossPriority::Scripted, true, Why, true);
	Enter(EBossState::Evaluate, -1.f);
	return true;
}

void FBossStateMachine::Reset(float IntroTimeout)
{
	State = EBossState::Intro;
	StateTime = 0.f;
	Timeout = IntroTimeout >= 0.f ? IntroTimeout : DefaultTimeout(EBossState::Intro);
	bAttackRecovering = false;
	bHasPending = false;
	History.Reset();
}
