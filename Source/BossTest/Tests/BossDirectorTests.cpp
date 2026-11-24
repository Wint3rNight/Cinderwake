// Step 6, Task 1: his phase-one director (spec section 10). Plain C++: bands, scoring, combos, the rhythm, fallbacks.
#include "Misc/AutomationTest.h"
#include "Boss/BossDirector.h"
#include "Boss/BossMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DirectorTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	enum EIds { Swing, Delayed, Reverse, Crushing, Double, Punish, Judgment, SoulMark, Advance };

	FBossOption Opt(int32 Id, float Min, float Max, EBossAngleRule Angle, FBossBandWeights W, float Cooldown = 0.f)
	{
		FBossOption O;
		O.Id = Id;
		O.MinRange = Min;
		O.MaxRange = Max;
		O.Angle = Angle;
		O.Weights = W;
		O.Cooldown = Cooldown;
		return O;
	}

	// Section 5's ranges and angles, section 10's phase-one weights.
	TArray<FBossOption> PhaseOne()
	{
		TArray<FBossOption> O = {
			Opt(Swing, 0.f, 430.f, EBossAngleRule::FrontAndSides, { 1.0f, 0.2f, 0.f, 0.f }),
			Opt(Delayed, 0.f, 490.f, EBossAngleRule::FrontAndSides, { 0.7f, 0.1f, 0.f, 0.f }),
			Opt(Reverse, 0.f, 320.f, EBossAngleRule::FrontSidesAndRear, { 0.8f, 0.f, 0.f, 0.f }),
			Opt(Crushing, 0.f, 400.f, EBossAngleRule::FrontAndSides, { 0.6f, 0.5f, 0.f, 0.f }),
			Opt(Double, 0.f, 430.f, EBossAngleRule::FrontAndSides, { 1.0f, 0.2f, 0.f, 0.f }),
			Opt(Punish, 0.f, 350.f, EBossAngleRule::FrontAndSides, { 0.6f, 0.f, 0.f, 0.f }),
			Opt(Judgment, 0.f, 800.f, EBossAngleRule::FrontOnly, { 0.3f, 0.8f, 0.2f, 0.f }, 12.f),
			Opt(SoulMark, 250.f, 800.f, EBossAngleRule::FrontOnly, { 0.f, 0.5f, 0.7f, 0.f }, 10.f),
			Opt(Advance, 400.f, 1150.f, EBossAngleRule::Any, { 0.f, 0.3f, 0.8f, 1.2f }, 7.f),
		};
		O[Reverse].RearWeight = 1.5f;
		O[Punish].bNeedsPunishTrigger = true;
		return O;
	}

	FBossComboOption Combo(int32 Id, int32 Opener, TArray<int32> FollowUps)
	{
		FBossComboOption C;
		C.Id = Id;
		C.Opener = Opener;
		C.FollowUps = MoveTemp(FollowUps);
		return C;
	}

	// Section 5's combos A-F.
	TArray<FBossComboOption> CombosAF()
	{
		return { Combo(0, Swing, { Punish }), Combo(1, Swing, { Delayed }), Combo(2, Reverse, { Crushing }),
			Combo(3, Crushing, { Delayed }), Combo(4, Swing, { Swing, Judgment }), Combo(5, SoulMark, { Advance }) };
	}

	FBossDirector Make(TArray<FBossComboOption> Combos = TArray<FBossComboOption>())
	{
		FBossDirector D;
		D.Init(PhaseOne(), MoveTemp(Combos), FBossDirectorTuning());
		return D;
	}

	FBossPerception See(float Edge, float Angle = 0.f)
	{
		FBossPerception P;
		P.EdgeCm = Edge;
		P.AngleDeg = Angle;
		return P;
	}

	// Attack picks over many rolls from one situation (the director's state is copied for each roll).
	TMap<int32, int32> Picks(const FBossDirector& Base, const FBossPerception& P, int32 Count = 3000)
	{
		FRandomStream Rng(77);
		TMap<int32, int32> Out;
		for (int32 i = 0; i < Count; ++i)
		{
			FBossDirector D = Base;
			const FBossDecision R = D.Decide(P, Rng);
			Out.FindOrAdd(R.Move == EBossMove::Attack ? R.Action : -1 - static_cast<int32>(R.Move))++;
		}
		return Out;
	}

	constexpr int32 ApproachKey = -1 - static_cast<int32>(EBossMove::Approach);
	constexpr int32 CircleKey = -1 - static_cast<int32>(EBossMove::Circle);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorBandHysteresis, "BossTest.Boss.Director.BandHysteresis", DirectorTestFlags)
bool FDirectorBandHysteresis::RunTest(const FString& Parameters)
{
	// Close 0-2.5 m, Medium 2.5-6, Far 6-10, Extreme beyond, with 0.2 m of hysteresis at every edge.
	FBossDirector D = Make();
	D.Observe(See(240.f), true, 0.1f);
	TestTrue(TEXT("2.4 m: close"), D.GetBand() == EBossBand::Close);
	TestFalse(TEXT("2.6 m: still close (inside the hysteresis)"), D.Observe(See(260.f), true, 0.1f));
	TestTrue(TEXT("still close"), D.GetBand() == EBossBand::Close);
	TestTrue(TEXT("2.75 m: medium, and the change is reported"), D.Observe(See(275.f), true, 0.1f) && D.GetBand() == EBossBand::Medium);
	TestFalse(TEXT("back to 2.4 m: still medium"), D.Observe(See(240.f), true, 0.1f));
	TestTrue(TEXT("2.25 m: close again"), D.Observe(See(225.f), true, 0.1f) && D.GetBand() == EBossBand::Close);
	TestTrue(TEXT("12 m: extreme"), D.Observe(See(1200.f), true, 0.1f) && D.GetBand() == EBossBand::Extreme);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorAngleHysteresis, "BossTest.Boss.Director.AngleHysteresis", DirectorTestFlags)
bool FDirectorAngleHysteresis::RunTest(const FString& Parameters)
{
	FBossDirector D = Make();
	D.Observe(See(200.f, 40.f), true, 0.1f);
	TestTrue(TEXT("40 deg: front"), D.GetAngleBand() == EBossAngleBand::Front);
	D.Observe(See(200.f, 48.f), true, 0.1f);
	TestTrue(TEXT("48 deg: still front"), D.GetAngleBand() == EBossAngleBand::Front);
	D.Observe(See(200.f, 52.f), true, 0.1f);
	TestTrue(TEXT("52 deg: side"), D.GetAngleBand() == EBossAngleBand::Side);
	D.Observe(See(200.f, 42.f), true, 0.1f);
	TestTrue(TEXT("42 deg: still side"), D.GetAngleBand() == EBossAngleBand::Side);
	D.Observe(See(200.f, 150.f), true, 0.1f);
	TestTrue(TEXT("150 deg: behind"), D.GetAngleBand() == EBossAngleBand::Behind);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorWeightTables, "BossTest.Boss.Director.WeightTables", DirectorTestFlags)
bool FDirectorWeightTables::RunTest(const FString& Parameters)
{
	// Close, in front: section 10's close column (Swing 1.0, Double 1.0, Reverse 0.8, Delayed 0.7, Crushing 0.6,
	// Judgment 0.3, Circle 0.8; Punishment needs its trigger, Soul Mark its 2.5 m minimum).
	FBossDirector D = Make();
	D.Observe(See(150.f), true, 0.1f);
	const TMap<int32, int32> R = Picks(D, See(150.f), 6000);
	const float Total = 1.0f + 1.0f + 0.8f + 0.7f + 0.6f + 0.3f + 0.8f;
	auto Share = [&R](int32 K) { return R.FindRef(K) / 6000.f; };
	TestTrue(FString::Printf(TEXT("the swing about %.2f (%.2f)"), 1.f / Total, Share(Swing)), FMath::Abs(Share(Swing) - 1.f / Total) < 0.03f);
	TestTrue(FString::Printf(TEXT("Judgment Slam about %.2f (%.2f)"), 0.3f / Total, Share(Judgment)), FMath::Abs(Share(Judgment) - 0.3f / Total) < 0.02f);
	TestTrue(FString::Printf(TEXT("circling about %.2f (%.2f)"), 0.8f / Total, Share(CircleKey)), FMath::Abs(Share(CircleKey) - 0.8f / Total) < 0.03f);
	TestFalse(TEXT("no approach when close"), R.Contains(ApproachKey));
	TestFalse(TEXT("no Soul Mark inside 2.5 m"), R.Contains(SoulMark));
	FBossDirector Far = Make();
	Far.Observe(See(800.f), true, 0.1f);
	const TMap<int32, int32> F = Picks(Far, See(800.f));
	TestTrue(TEXT("8 m: the Advance, Soul Mark, the slam, approach, circling; no melee"), F.Contains(Advance) && F.Contains(SoulMark) && F.Contains(ApproachKey)
		&& !F.Contains(Swing) && !F.Contains(Crushing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorRangeAndAngle, "BossTest.Boss.Director.RangeAndAngle", DirectorTestFlags)
bool FDirectorRangeAndAngle::RunTest(const FString& Parameters)
{
	FBossDirector D = Make();
	D.Observe(See(300.f, 70.f), true, 0.1f);
	TestFalse(TEXT("at his side, Judgment Slam (front only) isn't valid"), D.IsValidNow(Judgment, See(300.f, 70.f), false));
	TestTrue(TEXT("the swing is"), D.IsValidNow(Swing, See(300.f, 70.f), false));
	TestFalse(TEXT("out of range is invalid"), D.IsValidNow(Swing, See(500.f, 0.f), false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorRearQuarter, "BossTest.Boss.Director.RearQuarterSweep", DirectorTestFlags)
bool FDirectorRearQuarter::RunTest(const FString& Parameters)
{
	FBossDirector D = Make();
	D.Observe(See(150.f, 110.f), true, 0.1f);
	const TMap<int32, int32> R = Picks(D, See(150.f, 110.f));
	TestTrue(TEXT("rear quarter: Reverse Sweep is the only attack"), R.Contains(Reverse) && !R.Contains(Swing) && !R.Contains(Double));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorCooldown, "BossTest.Boss.Director.Cooldown", DirectorTestFlags)
bool FDirectorCooldown::RunTest(const FString& Parameters)
{
	FBossDirector D = Make();
	D.Observe(See(800.f), true, 0.1f);
	D.NoteStepStarted(Advance);
	TestTrue(TEXT("the cooldown starts when it plays"), FMath::IsNearlyEqual(D.GetCooldownLeft(Advance), 7.f));
	TestFalse(TEXT("on cooldown: not valid"), D.IsValidNow(Advance, See(800.f), false));
	D.Observe(See(800.f), true, 7.f);
	TestTrue(TEXT("7 s later: valid again"), D.IsValidNow(Advance, See(800.f), false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorRepetition, "BossTest.Boss.Director.RepetitionLastFive", DirectorTestFlags)
bool FDirectorRepetition::RunTest(const FString& Parameters)
{
	// M = max(0.4, 1 - 0.2n), n = uses among his last five offensive entries.
	FBossDirector D = Make();
	TestEqual(TEXT("unused: 1"), D.RepetitionScale(FBossDirector::ActionKey(Swing)), 1.f);
	D.NoteCommitted(Swing, -1);
	D.NoteCommitted(Swing, -1);
	TestEqual(TEXT("twice: 0.6"), D.RepetitionScale(FBossDirector::ActionKey(Swing)), 0.6f, 0.001f);
	D.NoteCommitted(Swing, -1);
	D.NoteCommitted(Swing, -1);
	TestEqual(TEXT("four times: floored at 0.4"), D.RepetitionScale(FBossDirector::ActionKey(Swing)), 0.4f, 0.001f);
	for (int32 i = 0; i < 5; ++i)
	{
		D.NoteCommitted(Reverse, -1);
	}
	TestEqual(TEXT("pushed out of the last five: 1 again"), D.RepetitionScale(FBossDirector::ActionKey(Swing)), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorComboStop, "BossTest.Boss.Director.ComboStopWeight", DirectorTestFlags)
bool FDirectorComboStop::RunTest(const FString& Parameters)
{
	// At commit: stopping after the opener (0.3) against each valid combo for it (1 each). The swing opens A, B and E.
	FBossDirector D = Make(CombosAF());
	FRandomStream Rng(5);
	TMap<int32, int32> R;
	for (int32 i = 0; i < 6600; ++i)
	{
		R.FindOrAdd(D.RollCombo(Swing, Rng))++;
	}
	const float Stop = R.FindRef(-1) / 6600.f;
	TestTrue(FString::Printf(TEXT("stops about 0.3/3.3 of the time (%.3f)"), Stop), FMath::Abs(Stop - 0.3f / 3.3f) < 0.02f);
	TestTrue(TEXT("A, B and E all come out, never C, D or F"), R.Contains(0) && R.Contains(1) && R.Contains(4) && !R.Contains(2) && !R.Contains(3) && !R.Contains(5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorComboNoRepeat, "BossTest.Boss.Director.ComboNoRepeat", DirectorTestFlags)
bool FDirectorComboNoRepeat::RunTest(const FString& Parameters)
{
	FBossDirector D = Make(CombosAF());
	D.NoteCommitted(Reverse, 2); // Combo C
	FRandomStream Rng(9);
	bool bRepeat = false;
	for (int32 i = 0; i < 500; ++i)
	{
		bRepeat |= D.RollCombo(Reverse, Rng) == 2;
	}
	TestFalse(TEXT("never the same combo twice in a row"), bRepeat);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorComboCooldown, "BossTest.Boss.Director.ComboCooldownInvalid", DirectorTestFlags)
bool FDirectorComboCooldown::RunTest(const FString& Parameters)
{
	// Combo E ends in Judgment Slam: while the slam cools down, E can't be rolled.
	FBossDirector D = Make(CombosAF());
	D.NoteStepStarted(Judgment);
	FRandomStream Rng(11);
	bool bE = false;
	for (int32 i = 0; i < 500; ++i)
	{
		bE |= D.RollCombo(Swing, Rng) == 4;
	}
	TestFalse(TEXT("a combo with an attack on cooldown is invalid"), bE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorComboRepetition, "BossTest.Boss.Director.ComboRepetitionUsesLower", DirectorTestFlags)
bool FDirectorComboRepetition::RunTest(const FString& Parameters)
{
	// A combo's weight uses the lower of its own and its opener's repetition multipliers.
	FBossDirector D = Make(CombosAF());
	D.NoteCommitted(Swing, -1);
	D.NoteCommitted(Swing, -1);
	D.NoteCommitted(Swing, -1); // the swing at 0.4
	TestEqual(TEXT("Combo A's weight is the opener's 0.4"), D.ComboWeight(0), 0.4f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorBreather, "BossTest.Boss.Director.BreatherAfterTwo", DirectorTestFlags)
bool FDirectorBreather::RunTest(const FString& Parameters)
{
	// The council's punish windows: a 0.25 s pause after each action, 1.0 s after every second major.
	FBossDirector D = Make();
	D.NoteCommitted(Swing, -1);
	TestEqual(TEXT("after one: the 0.25 s commitment pause"), D.PauseAfterAttack(), 0.25f);
	D.NoteCommitted(Reverse, -1);
	TestEqual(TEXT("after the second: the 1.0 s breather"), D.PauseAfterAttack(), 1.0f);
	D.NoteCommitted(Swing, -1);
	TestEqual(TEXT("the count starts again"), D.PauseAfterAttack(), 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorHitEndsBreather, "BossTest.Boss.Director.HitEndsBreather", DirectorTestFlags)
bool FDirectorHitEndsBreather::RunTest(const FString& Parameters)
{
	// A hit during the breather ends it (the character leaves Neutral); the breather still counts as taken.
	FBossDirector D = Make();
	D.NoteCommitted(Swing, -1);
	D.NoteCommitted(Swing, -1);
	TestEqual(TEXT("the breather"), D.PauseAfterAttack(), 1.0f);
	D.NoteBeatBroken();
	D.NoteCommitted(Swing, -1);
	TestEqual(TEXT("broken by a hit, it still reset the count"), D.PauseAfterAttack(), 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorNeutralClears, "BossTest.Boss.Director.NeutralTimeClearsMajors", DirectorTestFlags)
bool FDirectorNeutralClears::RunTest(const FString& Parameters)
{
	// A second of neutral (circling, approaching) is a beat too: the next action counts as the first again.
	FBossDirector D = Make();
	D.NoteCommitted(Swing, -1);
	D.PauseAfterAttack();
	D.Observe(See(300.f), true, 1.0f);
	D.NoteCommitted(Swing, -1);
	TestEqual(TEXT("one second of neutral cleared the count"), D.PauseAfterAttack(), 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorChainValidity, "BossTest.Boss.Director.ChainPointValidity", DirectorTestFlags)
bool FDirectorChainValidity::RunTest(const FString& Parameters)
{
	// Rule 4: at the chain point a follow-up checks range and angle (its cooldown was checked at commit).
	FBossDirector D = Make(CombosAF());
	D.Observe(See(200.f), true, 0.1f);
	TestTrue(TEXT("Crushing Overhead after Reverse Sweep at 2 m: valid"), D.IsValidNow(Crushing, See(200.f), true));
	TestFalse(TEXT("at 5 m: out of range, the opener's full recovery plays"), D.IsValidNow(Crushing, See(500.f), true));
	D.Observe(See(200.f, 150.f), true, 0.1f);
	TestFalse(TEXT("behind him: invalid"), D.IsValidNow(Crushing, See(200.f, 150.f), true));
	D.NoteStepStarted(Judgment);
	D.Observe(See(200.f), true, 0.1f);
	D.Observe(See(200.f), true, 0.1f);
	TestTrue(TEXT("a follow-up ignores its own cooldown (checked at commit)"), D.IsValidNow(Judgment, See(200.f), true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorFallbacks, "BossTest.Boss.Director.Fallbacks", DirectorTestFlags)
bool FDirectorFallbacks::RunTest(const FString& Parameters)
{
	FRandomStream Rng(3);
	FBossDirector Down = Make();
	FBossPerception P = See(200.f);
	P.bPlayerDown = true;
	TestTrue(TEXT("player down: wait"), Down.Decide(P, Rng).Move == EBossMove::Wait);
	FBossDirector Behind = Make();
	Behind.Observe(See(150.f, 160.f), true, 0.1f);
	const EBossMove BehindClose = Behind.Decide(See(150.f, 160.f), Rng).Move;
	TestTrue(TEXT("behind and close: he repositions (the backstab spot moves)"), BehindClose == EBossMove::Reposition);
	FBossDirector BehindMid = Make();
	BehindMid.Observe(See(350.f, 160.f), true, 0.1f);
	const EBossMove Mid = BehindMid.Decide(See(350.f, 160.f), Rng).Move;
	TestTrue(TEXT("behind at medium: he turns, or moves"), Mid == EBossMove::Turn || Mid == EBossMove::Approach || Mid == EBossMove::Circle);
	FBossDirector Empty;
	Empty.Init(TArray<FBossOption>(), TArray<FBossComboOption>(), FBossDirectorTuning());
	Empty.Observe(See(150.f), true, 0.1f);
	const EBossMove None = Empty.Decide(See(150.f), Rng).Move;
	TestTrue(TEXT("nothing valid: he repositions or circles, never stands still forever"), None == EBossMove::Reposition || None == EBossMove::Circle);
	const FBossDecision C = Make().Decide(See(150.f), Rng);
	TestTrue(TEXT("every decision explains itself"), !C.Why.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorPursuit, "BossTest.Boss.Director.PursuitAfterFour", DirectorTestFlags)
bool FDirectorPursuit::RunTest(const FString& Parameters)
{
	// Until the analyzer (Step 7), pursuit glide starts after 4 s continuously in Far or Extreme.
	FBossDirector D = Make();
	D.Observe(See(900.f), true, 3.9f);
	TestFalse(TEXT("3.9 s far: not yet"), D.PursuitActive());
	D.Observe(See(900.f), true, 0.2f);
	TestTrue(TEXT("4.1 s: pursuit"), D.PursuitActive());
	D.Observe(See(500.f), true, 0.1f);
	TestFalse(TEXT("back to medium: over"), D.PursuitActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorSeed, "BossTest.Boss.Director.SameSeedSameDecisions", DirectorTestFlags)
bool FDirectorSeed::RunTest(const FString& Parameters)
{
	FBossDirector A = Make(CombosAF());
	FBossDirector B = Make(CombosAF());
	FRandomStream RA(42), RB(42);
	bool bSame = true;
	for (int32 i = 0; i < 60; ++i)
	{
		const FBossPerception P = See(100.f + (i % 9) * 120.f, (i * 37) % 180);
		A.Observe(P, true, 0.3f);
		B.Observe(P, true, 0.3f);
		const FBossDecision X = A.Decide(P, RA);
		const FBossDecision Y = B.Decide(P, RB);
		bSame &= X.Move == Y.Move && X.Action == Y.Action && FMath::IsNearlyEqual(X.Duration, Y.Duration);
		if (X.Move == EBossMove::Attack)
		{
			const int32 CA = A.RollCombo(X.Action, RA);
			bSame &= CA == B.RollCombo(Y.Action, RB);
			A.NoteCommitted(X.Action, CA);
			B.NoteCommitted(Y.Action, CA);
		}
	}
	TestTrue(TEXT("the same seed gives the same 60 decisions"), bSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorPin, "BossTest.Boss.Director.PinAction", DirectorTestFlags)
bool FDirectorPin::RunTest(const FString& Parameters)
{
	// Tests (the guard smoke tests) pin him to one action: that action whenever it fits, and no combos.
	FBossDirector D = Make(CombosAF());
	D.PinAction(Swing);
	D.Observe(See(200.f), true, 0.1f);
	const TMap<int32, int32> R = Picks(D, See(200.f), 300);
	TestTrue(TEXT("only the pinned action"), R.Num() == 1 && R.Contains(Swing));
	FRandomStream Rng(1);
	TestEqual(TEXT("no combos"), D.RollCombo(Swing, Rng), -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementArena, "BossTest.Boss.Movement.ArenaClampAndCircle", DirectorTestFlags)
bool FMovementArena::RunTest(const FString& Parameters)
{
	const FVector Centre(1000.f, 0.f, 0.f);
	TestTrue(TEXT("inside: unchanged"), ClampToArena(FVector(1500.f, 0.f, 0.f), Centre, 1300.f).Equals(FVector(1500.f, 0.f, 0.f)));
	TestTrue(TEXT("outside: pulled onto the 13 m edge"), ClampToArena(FVector(3000.f, 0.f, 50.f), Centre, 1300.f).Equals(FVector(2300.f, 0.f, 50.f), 0.1));
	const FVector Tangent = CircleDirection(FVector(0.f, 0.f, 0.f), FVector(500.f, 0.f, 0.f), 1);
	TestTrue(TEXT("circling: across the line to the player"), FMath::IsNearlyZero(Tangent.X, 0.001) && FMath::IsNearlyEqual(FMath::Abs(Tangent.Y), 1.0, 0.001));
	TestTrue(TEXT("the other way flips it"), CircleDirection(FVector::ZeroVector, FVector(500.f, 0.f, 0.f), -1).Equals(-Tangent, 0.001));
	TestTrue(TEXT("heading out of the disc within 0.5 s"), LeavesArena(FVector(2250.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f), 200.f, 0.5f, Centre, 1300.f));
	TestFalse(TEXT("heading inward: fine"), LeavesArena(FVector(2250.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f), 200.f, 0.5f, Centre, 1300.f));
	const FVector Slide = SlideAlongWall(FVector(2290.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f), Centre, 1300.f);
	TestTrue(TEXT("backing into the wall: slides along it instead"), FMath::Abs(Slide.X) < 0.01f);
	return true;
}

// ---- From the QA breaker's review of the plan ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorHitInPause, "BossTest.Boss.Director.HitInPauseFloors", DirectorTestFlags)
bool FDirectorHitInPause::RunTest(const FString& Parameters)
{
	// A hit in his pause cuts it, but never below the 0.25 s commitment floor, or the ladder's answers miss their margin.
	const FBossDirector D = Make();
	TestEqual(TEXT("breather from 10.0, hit at 10.1: ends at 10.25"), D.PauseEndAfterHit(10.f, 1.f, 10.1f), 10.25f, 0.001f);
	TestEqual(TEXT("hit at 10.6: ends now"), D.PauseEndAfterHit(10.f, 1.f, 10.6f), 10.6f, 0.001f);
	TestEqual(TEXT("the short pause can't be cut"), D.PauseEndAfterHit(10.f, 0.25f, 10.05f), 10.25f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorNoMoveLoop, "BossTest.Boss.Director.NoMovementLoopAfterHit", DirectorTestFlags)
bool FDirectorNoMoveLoop::RunTest(const FString& Parameters)
{
	// A hit that ends a movement means the next decision doesn't circle, so hits can't farm interrupted movements.
	FBossDirector D = Make();
	D.Observe(See(150.f), true, 0.1f);
	D.NoteBeatBroken();
	const TMap<int32, int32> R = Picks(D, See(150.f), 2000);
	TestFalse(TEXT("no circling right after a hit"), R.Contains(CircleKey));
	FRandomStream Rng(4);
	D.Decide(See(150.f), Rng);
	TestTrue(TEXT("only for that one decision"), Picks(D, See(150.f), 2000).Contains(CircleKey));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorFollowUpAngle, "BossTest.Boss.Director.FollowUpAngle90", DirectorTestFlags)
bool FDirectorFollowUpAngle::RunTest(const FString& Parameters)
{
	// He can't turn in recovery and a sidestep leaves you ~57 deg off: follow-ups take anything within 90 deg (each
	// tracks at least 110 deg before its cutoff), so one sidestep can't delete Combo E's front-only finisher.
	FBossDirector D = Make();
	D.Observe(See(300.f, 70.f), true, 0.1f);
	TestTrue(TEXT("Judgment Slam as a follow-up at 70 deg"), D.IsValidNow(Judgment, See(300.f, 70.f), true));
	TestFalse(TEXT("but not as an opener"), D.IsValidNow(Judgment, See(300.f, 70.f), false));
	D.Observe(See(300.f, 110.f), true, 0.1f);
	TestFalse(TEXT("at 110 deg: refused"), D.IsValidNow(Judgment, See(300.f, 110.f), true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorFollowUpDown, "BossTest.Boss.Director.FollowUpRefusedWhenDown", DirectorTestFlags)
bool FDirectorFollowUpDown::RunTest(const FString& Parameters)
{
	// Combo D's chain point can come while a landed Overhead still has you down: never then (section 10).
	FBossDirector D = Make();
	FBossPerception P = See(200.f);
	D.Observe(P, true, 0.1f);
	P.bPlayerDown = true;
	TestFalse(TEXT("knocked down: no follow-up"), D.IsValidNow(Delayed, P, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorBandWiggle, "BossTest.Boss.Director.BandWiggle", DirectorTestFlags)
bool FDirectorBandWiggle::RunTest(const FString& Parameters)
{
	// Wiggling 0.3 m either side of the 6 m edge at 4 m/s for 10 s while he circles: a movement re-decides on a band
	// change only after 0.4 s in it, so at most 3 decisions a second.
	FBossDirector D = Make();
	D.Observe(See(600.f), true, 0.1f);
	float InMove = 0.f;
	int32 Decisions = 0;
	for (int32 i = 0; i < 600; ++i)
	{
		const float T = i / 60.f;
		const float Edge = 600.f + 30.f * FMath::Sin(T * 2.f * PI / 0.3f);
		InMove += 1.f / 60.f;
		if (D.Observe(See(Edge), true, 1.f / 60.f) && D.ShouldRedecide(EBossMove::Circle, InMove))
		{
			++Decisions;
			InMove = 0.f;
		}
	}
	TestTrue(FString::Printf(TEXT("%d re-decisions in 10 s (at most 30)"), Decisions), Decisions <= 30);
	FBossDirector A = Make();
	A.Observe(See(700.f), true, 0.1f);
	TestTrue(TEXT("an approach re-decides at once when the band gets closer"), A.Observe(See(570.f), true, 0.05f) && A.ShouldRedecide(EBossMove::Approach, 0.05f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorComboOneMajor, "BossTest.Boss.Director.ComboIsOneMajor", DirectorTestFlags)
bool FDirectorComboOneMajor::RunTest(const FString& Parameters)
{
	// Combo E (swing, swing, Judgment Slam) is one major: the opener and the combo each count once in the last five.
	FBossDirector D = Make(CombosAF());
	D.NoteCommitted(Swing, 4);
	TestEqual(TEXT("one major: the short pause"), D.PauseAfterAttack(), 0.25f);
	TestEqual(TEXT("the swing counted once"), D.RepetitionScale(FBossDirector::ActionKey(Swing)), 0.8f, 0.001f);
	TestEqual(TEXT("the combo counted once"), D.RepetitionScale(FBossDirector::ComboKey(4)), 0.8f, 0.001f);
	TestEqual(TEXT("its follow-ups not at all"), D.RepetitionScale(FBossDirector::ActionKey(Judgment)), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorNoProgress, "BossTest.Boss.Director.NoProgressExcluded", DirectorTestFlags)
bool FDirectorNoProgress::RunTest(const FString& Parameters)
{
	// A movement that got nowhere (under 0.3 m in 0.4 s: pinned at the wall) sits out the next roll.
	FBossDirector D = Make();
	D.Observe(See(150.f), true, 0.1f);
	D.NoteNoProgress(EBossMove::Circle);
	TestFalse(TEXT("no circling on the next roll"), Picks(D, See(150.f), 2000).Contains(CircleKey));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorPursuitLeaks, "BossTest.Boss.Director.PursuitLeaks", DirectorTestFlags)
bool FDirectorPursuitLeaks::RunTest(const FString& Parameters)
{
	// A one-frame dip into Medium doesn't reset the 4 s; and while pursuing he never picks circling.
	FBossDirector D = Make();
	D.Observe(See(900.f), true, 3.0f);
	D.Observe(See(560.f), true, 0.05f);
	D.Observe(See(900.f), true, 1.2f);
	TestTrue(TEXT("pursuit despite the dip"), D.PursuitActive());
	TestFalse(TEXT("no circling while pursuing"), Picks(D, See(900.f), 2000).Contains(CircleKey));
	return true;
}

// ---- From the Unreal engineer's final review ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorPursuitEnds, "BossTest.Boss.Director.PursuitEnds", DirectorTestFlags)
bool FDirectorPursuitEnds::RunTest(const FString& Parameters)
{
	// After a long chase the counter is capped, so coming back to Medium ends the pursuit at once.
	FBossDirector D = Make();
	D.Observe(See(900.f), true, 20.f);
	TestTrue(TEXT("20 s far: pursuit"), D.PursuitActive());
	D.Observe(See(500.f), true, 0.1f);
	TestFalse(TEXT("0.1 s back in medium: over"), D.PursuitActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorFarBehind, "BossTest.Boss.Director.FarBehindTurns", DirectorTestFlags)
bool FDirectorFarBehind::RunTest(const FString& Parameters)
{
	// Right after an Advance you run past him: far, behind, the Advance cooling. He turns, never backs away from you.
	FBossDirector D = Make();
	D.NoteStepStarted(Advance);
	D.Observe(See(900.f, 160.f), true, 0.1f);
	FRandomStream Rng(2);
	TestTrue(TEXT("far behind: turn"), D.Decide(See(900.f, 160.f), Rng).Move == EBossMove::Turn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorNeutralCap, "BossTest.Boss.Director.NeutralCapThreeSeconds", DirectorTestFlags)
bool FDirectorNeutralCap::RunTest(const FString& Parameters)
{
	// Section 10: any stretch without an attack is capped at 3 s. Past 1.8 s of neutral he won't start a circle, and a
	// movement never runs past the cap.
	FBossDirector D = Make();
	D.Observe(See(450.f), true, 2.0f);
	FRandomStream Rng(6);
	bool bCircle = false;
	bool bWithinCap = true;
	for (int32 i = 0; i < 300; ++i)
	{
		FBossDirector Copy = D;
		const FBossDecision R = Copy.Decide(See(450.f), Rng);
		bCircle |= R.Move == EBossMove::Circle;
		bWithinCap &= R.Move == EBossMove::Attack || R.Duration <= 1.0f + 0.001f;
	}
	TestFalse(TEXT("no new circle after 2 s of neutral"), bCircle);
	TestTrue(TEXT("movements end by the 3 s cap"), bWithinCap);
	return true;
}

#endif
