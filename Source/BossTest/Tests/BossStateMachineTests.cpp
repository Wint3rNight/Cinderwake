// Step 4, Task 2: the boss's state authority (spec section 12).
#include "Misc/AutomationTest.h"
#include "Boss/BossStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags BossStateTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using S = EBossState;
	using P = EBossPriority;

	// Past the intro, deciding.
	FBossStateMachine Evaluating()
	{
		FBossStateMachine M;
		M.Reset();
		M.Request(S::Evaluate, P::Scripted, TEXT("intro over"));
		return M;
	}

	FBossStateMachine Attacking()
	{
		FBossStateMachine M = Evaluating();
		M.Request(S::Attack, P::CommittedAttack, TEXT("swing"), 3.14f);
		return M;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesDeathWins, "BossTest.Boss.States.DeathWinsAnything", BossStateTestFlags)
bool FBossStatesDeathWins::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking();
	TestTrue(TEXT("death interrupts a committed attack"), M.Request(S::Dead, P::Death, TEXT("killed")));
	TestTrue(TEXT("dead"), M.GetState() == S::Dead);
	FBossStateMachine N = Evaluating();
	N.Request(S::Stagger, P::Stagger, TEXT("poise"));
	TestTrue(TEXT("death interrupts a stagger"), N.Request(S::Dead, P::Death, TEXT("killed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesDeadIsTerminal, "BossTest.Boss.States.DeadIsTerminal", BossStateTestFlags)
bool FBossStatesDeadIsTerminal::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Evaluating();
	M.Request(S::Dead, P::Death, TEXT("killed"));
	TestFalse(TEXT("nothing leaves dead"), M.Request(S::Evaluate, P::Death, TEXT("revive?")));
	TestFalse(TEXT("not even a stagger"), M.Request(S::Stagger, P::Death, TEXT("poise")));
	TestFalse(TEXT("dead never times out"), M.Tick(100.f));
	TestTrue(TEXT("still dead"), M.GetState() == S::Dead);
	M.Reset();
	TestTrue(TEXT("only a reset brings him back"), M.GetState() == S::Intro);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesDecisionCantInterruptAttack, "BossTest.Boss.States.DecisionCantInterruptAttack", BossStateTestFlags)
bool FBossStatesDecisionCantInterruptAttack::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking();
	TestTrue(TEXT("attacking"), M.GetState() == S::Attack);
	TestFalse(TEXT("a decision can't interrupt a committed attack"), M.Request(S::Approach, P::Decision, TEXT("player ran")));
	TestTrue(TEXT("still attacking"), M.GetState() == S::Attack);
	TestTrue(TEXT("the attack itself can end"), M.Request(S::Neutral, P::CommittedAttack, TEXT("swing over")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesStaggerInterruptsAttack, "BossTest.Boss.States.StaggerInterruptsAttack", BossStateTestFlags)
bool FBossStatesStaggerInterruptsAttack::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking();
	TestTrue(TEXT("a stagger outranks a committed attack"), M.Request(S::Stagger, P::Stagger, TEXT("poise broken")));
	TestTrue(TEXT("staggered"), M.GetState() == S::Stagger);
	TestFalse(TEXT("a second stagger doesn't restart it"), M.Request(S::Stagger, P::Stagger, TEXT("again")));
	TestFalse(TEXT("a decision can't cut it short"), M.Request(S::Approach, P::Decision, TEXT("bored")));
	TestTrue(TEXT("it ends into evaluate"), M.Request(S::Evaluate, P::Stagger, TEXT("stagger over")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesRecoveryIsLowerPriority, "BossTest.Boss.States.RecoveryIsLowerPriority", BossStateTestFlags)
bool FBossStatesRecoveryIsLowerPriority::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking();
	TestTrue(TEXT("committed: holds committed-attack priority"), M.GetHeldPriority() == P::CommittedAttack);
	M.SetAttackRecovering(true);
	TestTrue(TEXT("recovering: holds recovery priority"), M.GetHeldPriority() == P::Recovery);
	TestFalse(TEXT("a decision still can't interrupt"), M.Request(S::Approach, P::Decision, TEXT("player ran")));
	TestTrue(TEXT("a stagger can"), M.Request(S::Stagger, P::Stagger, TEXT("deferred stagger")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesIllegalRejected, "BossTest.Boss.States.IllegalTransitionRejected", BossStateTestFlags)
bool FBossStatesIllegalRejected::RunTest(const FString& Parameters)
{
	FBossStateMachine M;
	M.Reset();
	TestFalse(TEXT("no attack straight out of the intro"), M.Request(S::Attack, P::CommittedAttack, TEXT("swing")));
	TestTrue(TEXT("still in the intro"), M.GetState() == S::Intro);
	FBossStateMachine N = Evaluating();
	N.Request(S::Neutral, P::Decision, TEXT("beat"));
	TestFalse(TEXT("a neutral beat goes back to evaluate, not straight into an attack"), N.Request(S::Attack, P::Decision, TEXT("swing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesTimeoutRecovers, "BossTest.Boss.States.TimeoutRecovers", BossStateTestFlags)
bool FBossStatesTimeoutRecovers::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking(); // a 2.14 s attack, timeout 3.14 s
	TestFalse(TEXT("no timeout at 3.10 s"), M.Tick(3.10f));
	TestTrue(TEXT("still attacking"), M.GetState() == S::Attack);
	TestTrue(TEXT("timed out by 3.15 s"), M.Tick(0.05f));
	TestTrue(TEXT("back to evaluate"), M.GetState() == S::Evaluate);
	const TArray<FBossRequestRecord>& H = M.GetHistory();
	TestTrue(TEXT("the timeout is recorded"), H.Num() > 0 && H.Last().From == S::Attack && H.Last().To == S::Evaluate && H.Last().Reason.Contains(TEXT("timeout")));
	TestEqual(TEXT("and counted"), M.GetTimeoutCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesHistory, "BossTest.Boss.States.HistoryRecordsOutcomes", BossStateTestFlags)
bool FBossStatesHistory::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking();
	M.Request(S::Approach, P::Decision, TEXT("player ran"));
	const TArray<FBossRequestRecord>& H = M.GetHistory();
	TestTrue(TEXT("the accepted attack is recorded"), H.Num() >= 2 && H[H.Num() - 2].bAccepted && H[H.Num() - 2].To == S::Attack);
	TestTrue(TEXT("so is the rejected approach"), H.Num() > 0 && !H.Last().bAccepted && H.Last().To == S::Approach && !H.Last().Reason.IsEmpty());
	for (int32 i = 0; i < 20; ++i)
	{
		M.Request(S::Approach, P::Decision, TEXT("again"));
	}
	TestEqual(TEXT("capped at 16"), M.GetHistory().Num(), FBossStateMachine::HistorySize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesAttackEnd, "BossTest.Boss.States.AttackEndGoesToNeutral", BossStateTestFlags)
bool FBossStatesAttackEnd::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking();
	M.SetAttackRecovering(true);
	TestTrue(TEXT("the attack's own end needs no priority"), M.Complete(S::Neutral, TEXT("swing over")));
	TestTrue(TEXT("neutral"), M.GetState() == S::Neutral);
	TestTrue(TEXT("and the recovering flag is gone"), M.GetHeldPriority() == P::Decision);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesStaggerEnd, "BossTest.Boss.States.StaggerEndsToEvaluate", BossStateTestFlags)
bool FBossStatesStaggerEnd::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Evaluating();
	M.Request(S::Stagger, P::Stagger, TEXT("poise"));
	TestFalse(TEXT("completing still checks legality"), M.Complete(S::Attack, TEXT("swing")));
	TestTrue(TEXT("a stagger ends into evaluate"), M.Complete(S::Evaluate, TEXT("stagger over")));
	FBossStateMachine Paused = Evaluating();
	Paused.Request(S::Stagger, P::Stagger, TEXT("poise"));
	Paused.SetTimeout(3.4f);
	TestEqual(TEXT("a finisher lengthens the stagger's backstop"), Paused.GetTimeout(), 3.4f);
	TestTrue(TEXT("or into the commitment pause (the council's run, 2026-11-24)"), Paused.Complete(S::Neutral, TEXT("stagger over")));
	// The aura circle (2026-11-25): it replaces a breather after an attack, never goes straight into one, and ends in the pause.
	TestTrue(TEXT("an attack can end into the aura"), FBossStateMachine::IsLegal(S::Attack, S::Posture));
	TestFalse(TEXT("the aura never goes straight into an attack"), FBossStateMachine::IsLegal(S::Posture, S::Attack));
	TestTrue(TEXT("it ends into the pause"), FBossStateMachine::IsLegal(S::Posture, S::Neutral));
	TestTrue(TEXT("a stagger interrupts it"), FBossStateMachine::IsLegal(S::Posture, S::Stagger));
	TestTrue(TEXT("it times out"), FBossStateMachine::DefaultTimeout(S::Posture) > 0.f);
	FBossStateMachine N;
	N.Reset(4.5f);
	TestEqual(TEXT("the intro's timeout follows its clip"), N.GetTimeout(), 4.5f);
	TestTrue(TEXT("and the intro ends into evaluate"), N.Complete(S::Evaluate, TEXT("intro over")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesSafePoint, "BossTest.Boss.States.SafePointWaitsForRecovery", BossStateTestFlags)
bool FBossStatesSafePoint::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking();
	TArray<TPair<S, S>> Seen;
	M.OnTransition = [&Seen](S From, S To) { Seen.Emplace(From, To); };
	M.RequestAtSafePoint(S::Stagger, P::Stagger, TEXT("deferred stagger"));
	TestTrue(TEXT("still attacking: armour holds until recovery"), M.GetState() == S::Attack && M.HasPendingSafePoint());
	M.SetAttackRecovering(true);
	TestTrue(TEXT("recovery is the safe point: staggered"), M.GetState() == S::Stagger && !M.HasPendingSafePoint());
	TestTrue(TEXT("the hook saw it"), Seen.Num() == 1 && Seen[0].Key == S::Attack && Seen[0].Value == S::Stagger);

	FBossStateMachine N = Attacking();
	N.RequestAtSafePoint(S::Stagger, P::Stagger, TEXT("deferred stagger"));
	N.Complete(S::Neutral, TEXT("swing over"));
	TestTrue(TEXT("an attack ending any other way also plays it"), N.GetState() == S::Stagger);

	FBossStateMachine T = Attacking();
	T.RequestAtSafePoint(S::Stagger, P::Stagger, TEXT("deferred stagger"));
	T.Tick(5.f);
	TestTrue(TEXT("so does a timeout"), T.GetState() == S::Stagger);

	FBossStateMachine D = Attacking();
	D.RequestAtSafePoint(S::Stagger, P::Stagger, TEXT("deferred stagger"));
	D.Request(S::Dead, P::Death, TEXT("killed"));
	TestTrue(TEXT("death drops it"), D.GetState() == S::Dead && !D.HasPendingSafePoint());

	FBossStateMachine E = Evaluating();
	E.RequestAtSafePoint(S::Stagger, P::Stagger, TEXT("forced"));
	TestTrue(TEXT("not attacking: it happens now"), E.GetState() == S::Stagger);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesHook, "BossTest.Boss.States.TransitionHookFiresForAll", BossStateTestFlags)
bool FBossStatesHook::RunTest(const FString& Parameters)
{
	FBossStateMachine M;
	int32 Calls = 0;
	M.OnTransition = [&Calls](S, S) { ++Calls; };
	M.Reset();
	M.Complete(S::Evaluate, TEXT("intro over"));
	M.Request(S::Attack, P::CommittedAttack, TEXT("swing"), 3.14f);
	M.Request(S::Approach, P::Decision, TEXT("rejected"));
	M.Tick(5.f);
	TestEqual(TEXT("completion, request and timeout each fire it once; a rejection doesn't"), Calls, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesSafePointDetails, "BossTest.Boss.States.SafePointKeepsPriorityAndTimeout", BossStateTestFlags)
bool FBossStatesSafePointDetails::RunTest(const FString& Parameters)
{
	FBossStateMachine M = Attacking();
	M.RequestAtSafePoint(S::Stagger, P::Stagger, TEXT("deferred stagger"), 3.5f);
	M.RequestAtSafePoint(S::Evaluate, P::Decision, TEXT("lower priority"));
	M.SetAttackRecovering(true);
	TestTrue(TEXT("the higher-priority request stays queued"), M.GetState() == S::Stagger);
	TestEqual(TEXT("and plays with its own timeout"), M.GetTimeout(), 3.5f);
	FBossStateMachine T = Attacking();
	T.RequestAtSafePoint(S::Stagger, P::Stagger, TEXT("deferred stagger"), 3.5f);
	T.Tick(5.f);
	TestEqual(TEXT("an attack timing out plays it with its timeout too"), T.GetTimeout(), 3.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesRecordHook, "BossTest.Boss.States.RecordHookSeesEverything", BossStateTestFlags)
bool FBossStatesRecordHook::RunTest(const FString& Parameters)
{
	FBossStateMachine M;
	int32 Accepted = 0, Rejected = 0, Timeouts = 0;
	M.OnRecord = [&](const FBossRequestRecord& R, bool bTimeout) { (R.bAccepted ? Accepted : Rejected)++; Timeouts += bTimeout ? 1 : 0; };
	M.Reset();
	M.Complete(S::Evaluate, TEXT("intro over"));
	M.Request(S::Attack, P::CommittedAttack, TEXT("swing"), 3.14f);
	M.Request(S::Approach, P::Decision, TEXT("rejected"));
	M.Tick(5.f);
	TestEqual(TEXT("accepted: the completion, the attack and the timeout"), Accepted, 3);
	TestEqual(TEXT("rejected requests are logged too"), Rejected, 1);
	TestEqual(TEXT("and the timeout is flagged"), Timeouts, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesCastBroken, "BossTest.Boss.States.CastBroken", BossStateTestFlags)
bool FBossStatesCastBroken::RunTest(const FString& Parameters)
{
	// Step 5: breaking Soul Mark's cast (spec section 9) puts him in a 0.8 s recoil. It isn't a stagger, but it holds
	// stagger priority, and a real stagger can still take over.
	FBossStateMachine M = Attacking();
	TestTrue(TEXT("a broken cast interrupts the attack"), M.Request(S::CastBroken, P::Stagger, TEXT("cast broken"), 1.8f));
	TestTrue(TEXT("he's recoiling"), M.GetState() == S::CastBroken);
	TestTrue(TEXT("it holds stagger priority"), M.GetHeldPriority() == P::Stagger);
	TestFalse(TEXT("a decision can't cut it short"), M.Request(S::Approach, P::Decision, TEXT("chase")));
	TestTrue(TEXT("a stagger can take over"), M.Request(S::Stagger, P::Stagger, TEXT("poise broke")));
	FBossStateMachine N = Attacking();
	N.Request(S::CastBroken, P::Stagger, TEXT("cast broken"), 1.8f);
	TestTrue(TEXT("its natural end goes back to deciding"), N.Complete(S::Evaluate, TEXT("recoil over")) && N.GetState() == S::Evaluate);
	FBossStateMachine T = Attacking();
	T.Request(S::CastBroken, P::Stagger, TEXT("cast broken"));
	TestTrue(TEXT("its default timeout is 0.8 s plus 1 s"), FMath::IsNearlyEqual(T.GetTimeout(), 1.8f));
	FBossStateMachine E = Evaluating();
	TestFalse(TEXT("only an attack (a cast) can be broken"), E.Request(S::CastBroken, P::Stagger, TEXT("not casting")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesNextStep, "BossTest.Boss.States.NextStep", BossStateTestFlags)
bool FBossStatesNextStep::RunTest(const FString& Parameters)
{
	// Step 5 review: a sequence's next step (Double Execution's hit 2, the Advance's swing) is a fresh commitment:
	// committed-attack priority again, its own clock and timeout. A pending safe-point request plays instead.
	FBossStateMachine M = Attacking();
	M.SetAttackRecovering(true);
	M.Tick(1.f);
	TestTrue(TEXT("the next step starts"), M.NextStep(TEXT("hit 2"), 2.5f));
	TestTrue(TEXT("still attacking"), M.GetState() == S::Attack);
	TestTrue(TEXT("committed again, not recovering"), M.GetHeldPriority() == P::CommittedAttack);
	TestEqual(TEXT("its own clock"), M.GetStateTime(), 0.f);
	TestEqual(TEXT("its own timeout"), M.GetTimeout(), 2.5f);
	TestTrue(TEXT("recorded"), M.GetHistory().Num() > 0 && M.GetHistory().Last().Reason.Contains(TEXT("hit 2")));
	TestFalse(TEXT("a decision still can't interrupt it"), M.Request(S::Approach, P::Decision, TEXT("chase")));
	TestFalse(TEXT("2.4 s in: no timeout"), M.Tick(2.4f));
	FBossStateMachine N = Attacking();
	N.RequestAtSafePoint(S::Stagger, P::Stagger, TEXT("deferred stagger"));
	TestFalse(TEXT("a pending stagger: no next step"), N.NextStep(TEXT("hit 2"), 2.5f));
	TestTrue(TEXT("the stagger plays instead"), N.GetState() == S::Stagger);
	FBossStateMachine E = Evaluating();
	TestFalse(TEXT("not attacking: no next step"), E.NextStep(TEXT("hit 2"), 2.5f));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossStatesMovement, "BossTest.Boss.States.MovementStatesTimeOut", BossStateTestFlags)
bool FBossStatesMovement::RunTest(const FString& Parameters)
{
	// Step 6: circling and repositioning are states of their own, entered from Evaluate, and they time out like the rest.
	FBossStateMachine M = Evaluating();
	TestTrue(TEXT("he can circle"), M.Request(S::Circle, P::Decision, TEXT("circle"), 2.0f));
	TestTrue(TEXT("and attack out of it"), FBossStateMachine::IsLegal(S::Circle, S::Attack) && FBossStateMachine::IsLegal(S::Reposition, S::Attack));
	TestTrue(TEXT("a circle times out"), M.Tick(2.05f) && M.GetState() == S::Evaluate);
	TestTrue(TEXT("repositioning, by default within 2.3 s"), M.Request(S::Reposition, P::Decision, TEXT("reposition")) && FMath::IsNearlyEqual(M.GetTimeout(), 2.3f));
	TestTrue(TEXT("a circle's default is 3.6 s"), FBossStateMachine::DefaultTimeout(S::Circle) > 3.5f);
	TestTrue(TEXT("a stagger can interrupt a movement"), M.Request(S::Stagger, P::Stagger, TEXT("poise")));
	return true;
}

#endif
