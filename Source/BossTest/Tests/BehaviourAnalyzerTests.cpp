// Step 7: the behaviour analyzer (spec section 10's habit table and inputs).
#include "Misc/AutomationTest.h"
#include "Boss/BehaviourAnalyzer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AnalyzerTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float AnalyzerStep = 0.05f;

	// No perception delay, so the numbers below are the formulas' own (one test checks the delay).
	FBehaviourAnalyzer MakeAnalyzer(float Delay = 0.f)
	{
		FAnalyzerTuning T;
		T.PerceptionDelay = Delay;
		FBehaviourAnalyzer A;
		A.Init(T);
		return A;
	}

	// The player standing 1.5 m from his edge, him in neutral, nothing happening.
	FPlayerObservation QuietFrame()
	{
		FPlayerObservation O;
		O.Dt = AnalyzerStep;
		O.EdgeCm = 150.f;
		O.ToPlayer = FVector2D(250.f, 0.f);
		O.bBossNeutral = true;
		return O;
	}

	void RunFrames(FBehaviourAnalyzer& A, const FPlayerObservation& O, float Seconds)
	{
		for (float T = 0.f; T < Seconds - 0.001f; T += O.Dt)
		{
			A.Observe(O);
		}
	}

	float HabitScore(const FBehaviourAnalyzer& A, EBossHabit H) { return A.GetSnapshot().Get(H); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerDodges, "BossTest.Boss.Analyzer.DodgesBuildSlowly", AnalyzerTestFlags)
bool FAnalyzerDodges::RunTest(const FString& Parameters)
{
	// Dodges per 4 s, full at 3, averaged over about 6 s: two quick dodges don't make a habit; three every four seconds do.
	FBehaviourAnalyzer A = MakeAnalyzer();
	FPlayerObservation Dodge = QuietFrame();
	Dodge.bDodgeStarted = true;
	A.Observe(Dodge);
	RunFrames(A, QuietFrame(), 0.5f);
	A.Observe(Dodge);
	TestTrue(FString::Printf(TEXT("two dodges: below a habit (%.2f)"), HabitScore(A, EBossHabit::Dodging)), HabitScore(A, EBossHabit::Dodging) < 0.5f);
	TestTrue(TEXT("but they count"), HabitScore(A, EBossHabit::Dodging) > 0.3f);

	FBehaviourAnalyzer B = MakeAnalyzer();
	for (int32 i = 0; i < 15; ++i)
	{
		B.Observe(Dodge);
		RunFrames(B, QuietFrame(), 4.f / 3.f - AnalyzerStep);
	}
	TestTrue(FString::Printf(TEXT("three every 4 s for 20 s: a habit (%.2f)"), HabitScore(B, EBossHabit::Dodging)), HabitScore(B, EBossHabit::Dodging) > 0.8f);
	RunFrames(B, QuietFrame(), 20.f);
	TestTrue(FString::Printf(TEXT("20 s without one: gone (%.2f)"), HabitScore(B, EBossHabit::Dodging)), HabitScore(B, EBossHabit::Dodging) < 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerDelay, "BossTest.Boss.Analyzer.PerceptionDelay", AnalyzerTestFlags)
bool FAnalyzerDelay::RunTest(const FString& Parameters)
{
	// He sees you a quarter second late: a dodge shows in the snapshot 0.25 s after it, not before.
	FBehaviourAnalyzer A = MakeAnalyzer(0.25f);
	FPlayerObservation Dodge = QuietFrame();
	Dodge.bDodgeStarted = true;
	A.Observe(Dodge);
	RunFrames(A, QuietFrame(), 0.15f);
	TestEqual(TEXT("0.2 s later: not seen yet"), HabitScore(A, EBossHabit::Dodging), 0.f);
	RunFrames(A, QuietFrame(), 0.1f);
	TestTrue(TEXT("0.3 s later: seen"), HabitScore(A, EBossHabit::Dodging) > 0.f);

	FBehaviourAnalyzer H = MakeAnalyzer(0.25f);
	FPlayerObservation Heal = QuietFrame();
	Heal.bHealing = true;
	RunFrames(H, Heal, 0.2f);
	TestFalse(TEXT("a heal 0.2 s old isn't seen yet"), H.IsHealingSeen());
	RunFrames(H, Heal, 0.1f);
	TestTrue(TEXT("at 0.3 s it is"), H.IsHealingSeen());
	RunFrames(H, QuietFrame(), 0.2f);
	TestTrue(TEXT("and it still looks like a heal 0.2 s after it ended"), H.IsHealingSeen());
	RunFrames(H, QuietFrame(), 0.1f);
	TestFalse(TEXT("then it's over"), H.IsHealingSeen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerAttacks, "BossTest.Boss.Analyzer.AttacksCountOnlyInNeutral", AnalyzerTestFlags)
bool FAnalyzerAttacks::RunTest(const FString& Parameters)
{
	// Attacks started while he's in neutral, per 5 s, full at 4. Punishing his recovery is the game, not a habit.
	FBehaviourAnalyzer A = MakeAnalyzer();
	FPlayerObservation Punish = QuietFrame();
	Punish.bBossNeutral = false;
	Punish.bAttackStarted = true;
	for (int32 i = 0; i < 12; ++i)
	{
		A.Observe(Punish);
		RunFrames(A, QuietFrame(), 1.25f - AnalyzerStep);
	}
	TestEqual(TEXT("attacks into his recovery: no habit"), HabitScore(A, EBossHabit::Attacking), 0.f);

	FPlayerObservation Press = QuietFrame();
	Press.bAttackStarted = true;
	for (int32 i = 0; i < 16; ++i)
	{
		A.Observe(Press);
		RunFrames(A, QuietFrame(), 1.25f - AnalyzerStep);
	}
	TestTrue(FString::Printf(TEXT("four per 5 s into his neutral: a habit (%.2f)"), HabitScore(A, EBossHabit::Attacking)), HabitScore(A, EBossHabit::Attacking) > 0.8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerDisengaged, "BossTest.Boss.Analyzer.DisengagedNeedsDistanceAndSpeed", AnalyzerTestFlags)
bool FAnalyzerDisengaged::RunTest(const FString& Parameters)
{
	// The share of time in Far or beyond while moving faster than 3 m/s.
	FPlayerObservation FarStill = QuietFrame();
	FarStill.EdgeCm = 900.f;
	FarStill.ToPlayer = FVector2D(1000.f, 0.f);
	FPlayerObservation CloseRun = QuietFrame();
	CloseRun.PlayerVelocity = FVector2D(0.f, 600.f);
	FPlayerObservation FarRun = FarStill;
	FarRun.PlayerVelocity = FVector2D(600.f, 0.f);

	FBehaviourAnalyzer A = MakeAnalyzer();
	RunFrames(A, FarStill, 8.f);
	TestEqual(TEXT("standing far away: not running"), HabitScore(A, EBossHabit::Disengaged), 0.f);
	RunFrames(A, CloseRun, 8.f);
	TestEqual(TEXT("running close by: not away"), HabitScore(A, EBossHabit::Disengaged), 0.f);
	RunFrames(A, FarRun, 6.f);
	const float SixSeconds = HabitScore(A, EBossHabit::Disengaged);
	TestTrue(FString::Printf(TEXT("running far away for 6 s: about 1 - 1/e (%.2f)"), SixSeconds), FMath::Abs(SixSeconds - (1.f - FMath::Exp(-1.f))) < 0.02f);
	RunFrames(A, FarRun, 4.f);
	TestTrue(TEXT("10 s: past 0.78"), HabitScore(A, EBossHabit::Disengaged) > 0.78f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerCircling, "BossTest.Boss.Analyzer.CirclingUsesThePlayersOwnMotion", AnalyzerTestFlags)
bool FAnalyzerCircling::RunTest(const FString& Parameters)
{
	// Your angle around him changing faster than 60 deg/s, from your own velocity (his circling doesn't make you a circler),
	// within Medium.
	FPlayerObservation Strafe = QuietFrame();
	Strafe.PlayerVelocity = FVector2D(0.f, 300.f); // 3 m/s across, 2.5 m from his centre: about 69 deg/s
	FPlayerObservation Radial = QuietFrame();
	Radial.PlayerVelocity = FVector2D(300.f, 0.f);
	FPlayerObservation FarStrafe = QuietFrame();
	FarStrafe.EdgeCm = 800.f;
	FarStrafe.ToPlayer = FVector2D(900.f, 0.f);
	FarStrafe.PlayerVelocity = FVector2D(0.f, 1200.f);
	FPlayerObservation SlowStrafe = QuietFrame();
	SlowStrafe.PlayerVelocity = FVector2D(0.f, 200.f); // about 46 deg/s

	FBehaviourAnalyzer A = MakeAnalyzer();
	RunFrames(A, Radial, 6.f);
	RunFrames(A, FarStrafe, 6.f);
	RunFrames(A, SlowStrafe, 6.f);
	TestEqual(TEXT("backing off, strafing far away and strafing slowly: no circling"), HabitScore(A, EBossHabit::Circling), 0.f);
	RunFrames(A, Strafe, 6.f);
	TestTrue(FString::Printf(TEXT("6 s of fast strafing around him: about 0.63 (%.2f)"), HabitScore(A, EBossHabit::Circling)), FMath::Abs(HabitScore(A, EBossHabit::Circling) - 0.632f) < 0.02f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerHeal, "BossTest.Boss.Analyzer.HealingAtRange", AnalyzerTestFlags)
bool FAnalyzerHeal::RunTest(const FString& Parameters)
{
	// A heal started beyond 6 m is a full score, decaying; one up close isn't a heal at range. Each heal is counted once.
	FBehaviourAnalyzer A = MakeAnalyzer();
	FPlayerObservation Near = QuietFrame();
	Near.bHealing = true;
	RunFrames(A, Near, 1.7f);
	RunFrames(A, QuietFrame(), 1.f);
	TestEqual(TEXT("a heal at 1.5 m: no score"), HabitScore(A, EBossHabit::HealingAtRange), 0.f);
	TestEqual(TEXT("but it was seen"), A.GetHealsSeen(), 1);
	FPlayerObservation Far = QuietFrame();
	Far.EdgeCm = 800.f;
	Far.ToPlayer = FVector2D(900.f, 0.f);
	Far.bHealing = true;
	A.Observe(Far);
	TestTrue(FString::Printf(TEXT("a heal at 8 m: full (%.2f)"), HabitScore(A, EBossHabit::HealingAtRange)), HabitScore(A, EBossHabit::HealingAtRange) > 0.99f);
	RunFrames(A, Far, 1.7f);
	TestEqual(TEXT("one heal, however long, counts once"), A.GetHealsSeen(), 2);
	FPlayerObservation FarQuiet = Far;
	FarQuiet.bHealing = false;
	RunFrames(A, FarQuiet, 6.f - 1.7f);
	TestTrue(FString::Printf(TEXT("6 s later: about 1/e (%.2f)"), HabitScore(A, EBossHabit::HealingAtRange)), FMath::Abs(HabitScore(A, EBossHabit::HealingAtRange) - FMath::Exp(-1.f)) < 0.03f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerHitAndRun, "BossTest.Boss.Analyzer.HitAndRun", AnalyzerTestFlags)
bool FAnalyzerHitAndRun::RunTest(const FString& Parameters)
{
	// Retreating more than 4 m within 1.5 s of landing a hit; full at three within the time constant.
	auto HitThenMove = [](FBehaviourAnalyzer& A, float Delay, float To)
	{
		FPlayerObservation Hit = QuietFrame();
		Hit.EdgeCm = 100.f;
		Hit.bLandedHit = true;
		A.Observe(Hit);
		FPlayerObservation Stay = QuietFrame();
		Stay.EdgeCm = 100.f;
		RunFrames(A, Stay, Delay);
		FPlayerObservation Away = QuietFrame();
		Away.EdgeCm = To;
		RunFrames(A, Away, 0.5f);
	};
	FBehaviourAnalyzer A = MakeAnalyzer();
	HitThenMove(A, 0.6f, 300.f);
	TestEqual(TEXT("a hit, then 2 m back: no"), HabitScore(A, EBossHabit::HitAndRun), 0.f);
	HitThenMove(A, 1.6f, 600.f);
	TestEqual(TEXT("a hit, then 5 m back but too late: no"), HabitScore(A, EBossHabit::HitAndRun), 0.f);
	HitThenMove(A, 0.6f, 600.f);
	const float One = HabitScore(A, EBossHabit::HitAndRun);
	TestTrue(FString::Printf(TEXT("a hit, then 5 m back within 1.5 s: a third (%.2f)"), One), FMath::Abs(One - 1.f / 3.f) < 0.03f);
	HitThenMove(A, 0.6f, 600.f);
	HitThenMove(A, 0.6f, 600.f);
	TestTrue(FString::Printf(TEXT("three in a few seconds: a habit (%.2f)"), HabitScore(A, EBossHabit::HitAndRun)), HabitScore(A, EBossHabit::HitAndRun) > 0.75f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerScale, "BossTest.Boss.Analyzer.HabitScaleTakesTheLargest", AnalyzerTestFlags)
bool FAnalyzerScale::RunTest(const FString& Parameters)
{
	// Each modifier is 1 + (cap - 1) x score, and when an action counters several habits only the largest counts.
	FHabitSnapshot S;
	S.Set(EBossHabit::Dodging, 1.f);
	S.Set(EBossHabit::HitAndRun, 1.f);
	S.Set(EBossHabit::Circling, 0.5f);
	const FHabitCaps Caps;
	int32 Driver = -1;
	TestEqual(TEXT("no habits countered: 1"), HabitScale(0, S, Caps, &Driver), 1.f);
	TestEqual(TEXT("and no driver"), Driver, -1);
	const float Both = HabitScale(HabitBit(EBossHabit::Dodging) | HabitBit(EBossHabit::HitAndRun), S, Caps, &Driver);
	TestEqual(TEXT("dodging (1.6) and hit and run (1.3): 1.6, not their product"), Both, 1.6f, 0.001f);
	TestEqual(TEXT("driven by dodging"), Driver, static_cast<int32>(EBossHabit::Dodging));
	TestEqual(TEXT("circling at half: 1.2"), HabitScale(HabitBit(EBossHabit::Circling), S, Caps), 1.2f, 0.001f);
	TestEqual(TEXT("a habit at zero raises nothing"), HabitScale(HabitBit(EBossHabit::Attacking), S, Caps, &Driver), 1.f);
	TestEqual(TEXT("and drives nothing"), Driver, -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyzerReset, "BossTest.Boss.Analyzer.ResetForgets", AnalyzerTestFlags)
bool FAnalyzerReset::RunTest(const FString& Parameters)
{
	// A retry starts clean: no habits, nothing waiting in the delay line.
	FBehaviourAnalyzer A = MakeAnalyzer(0.25f);
	FPlayerObservation Dodge = QuietFrame();
	Dodge.bDodgeStarted = true;
	Dodge.bHealing = true;
	RunFrames(A, Dodge, 1.f);
	A.Reset();
	RunFrames(A, QuietFrame(), 0.5f);
	bool bClean = !A.IsHealingSeen() && A.GetHealsSeen() == 0;
	for (int32 h = 0; h < BossHabitCount; ++h)
	{
		bClean &= A.GetSnapshot().Scores[h] == 0.f;
	}
	TestTrue(TEXT("after a reset nothing is left, not even what was in flight"), bClean);
	return true;
}

#endif
