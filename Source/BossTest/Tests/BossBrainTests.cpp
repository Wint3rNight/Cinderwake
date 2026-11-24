// Step 5, Task 3: the stand-in brain. It picks among his actions by range, angle, cooldown and a repeat penalty
// until Step 6's director replaces it.
#include "Misc/AutomationTest.h"
#include "Boss/BossBrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags BrainTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	enum EIds { Swing = 1, Reverse, SoulMark, Slam, Advance, Punish };

	FBossActionOption Option(int32 Id, float Min, float Max, EBossAngleRule Angle, float Weight = 1.f)
	{
		FBossActionOption O;
		O.Id = Id;
		O.MinRange = Min;
		O.MaxRange = Max;
		O.Angle = Angle;
		O.Weight = Weight;
		return O;
	}

	FBossSituation At(float Distance, float Angle, int32 Last = -1)
	{
		FBossSituation S;
		S.EdgeDistance = Distance;
		S.AngleDeg = Angle;
		S.LastActionId = Last;
		return S;
	}

	// How often each id comes out over many rolls (attacks only).
	TMap<int32, int32> Rolls(const FBossSituation& S, const TArray<FBossActionOption>& Options, int32 Count = 2000)
	{
		FRandomStream Rng(1234);
		TMap<int32, int32> Out;
		for (int32 i = 0; i < Count; ++i)
		{
			const FBossChoiceResult R = ChooseBossAction(S, Options, Rng);
			if (R.Choice == EBossChoice::Attack)
			{
				Out.FindOrAdd(R.ActionId)++;
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainRangeAndAngle, "BossTest.Boss.Brain.RangeAndAngle", BrainTestFlags)
bool FBossBrainRangeAndAngle::RunTest(const FString& Parameters)
{
	const TArray<FBossActionOption> Options = {
		Option(Swing, 0.f, 430.f, EBossAngleRule::FrontAndSides),
		Option(SoulMark, 250.f, 800.f, EBossAngleRule::FrontOnly),
		Option(Slam, 0.f, 800.f, EBossAngleRule::FrontOnly),
	};
	TMap<int32, int32> R = Rolls(At(300.f, 0.f), Options);
	TestTrue(TEXT("3 m in front: all three come out"), R.Contains(Swing) && R.Contains(SoulMark) && R.Contains(Slam));
	R = Rolls(At(300.f, 70.f), Options);
	TestTrue(TEXT("3 m at his side: only the swing (the others need you in front)"), R.Num() == 1 && R.Contains(Swing));
	R = Rolls(At(600.f, 0.f), Options);
	TestTrue(TEXT("6 m: Soul Mark and the slam, never the swing"), !R.Contains(Swing) && R.Contains(SoulMark) && R.Contains(Slam));
	R = Rolls(At(200.f, 0.f), Options);
	TestTrue(TEXT("2 m: too close for Soul Mark"), !R.Contains(SoulMark) && R.Contains(Swing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainRearQuarter, "BossTest.Boss.Brain.RearQuarterSweep", BrainTestFlags)
bool FBossBrainRearQuarter::RunTest(const FString& Parameters)
{
	FBossActionOption Rev = Option(Reverse, 0.f, 320.f, EBossAngleRule::FrontSidesAndRear);
	Rev.RearWeight = 1.5f;
	const TArray<FBossActionOption> Options = { Option(Swing, 0.f, 430.f, EBossAngleRule::FrontAndSides), Rev };
	FRandomStream Rng(7);
	const FBossChoiceResult Rear = ChooseBossAction(At(250.f, 110.f), Options, Rng);
	TestTrue(TEXT("rear quarter: only Reverse Sweep"), Rear.Choice == EBossChoice::Attack && Rear.ActionId == Reverse);
	TestTrue(TEXT("directly behind: he turns"), ChooseBossAction(At(250.f, 150.f), Options, Rng).Choice == EBossChoice::Turn);
	// Scored 1.5x in the rear quarter: against an any-angle option of weight 1, it comes out 60% of the time.
	const TArray<FBossActionOption> Pair = { Rev, Option(Advance, 0.f, 1000.f, EBossAngleRule::Any) };
	const TMap<int32, int32> R = Rolls(At(250.f, 110.f), Pair, 4000);
	const float Share = R.FindRef(Reverse) / 4000.f;
	TestTrue(FString::Printf(TEXT("rear quarter: Reverse Sweep scored 1.5x (%.2f of rolls, expect 0.60)"), Share), Share > 0.56f && Share < 0.64f);
	const TMap<int32, int32> Front = Rolls(At(250.f, 30.f), Pair, 4000);
	const float FrontShare = Front.FindRef(Reverse) / 4000.f;
	TestTrue(FString::Printf(TEXT("in front: no bonus (%.2f, expect 0.50)"), FrontShare), FrontShare > 0.46f && FrontShare < 0.54f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainCooldown, "BossTest.Boss.Brain.Cooldown", BrainTestFlags)
bool FBossBrainCooldown::RunTest(const FString& Parameters)
{
	FBossActionOption Adv = Option(Advance, 400.f, 1150.f, EBossAngleRule::Any);
	TArray<FBossActionOption> Options = { Adv, Option(SoulMark, 250.f, 800.f, EBossAngleRule::FrontOnly) };
	TestTrue(TEXT("ready: the Advance comes out"), Rolls(At(600.f, 0.f), Options).Contains(Advance));
	Options[0].CooldownLeft = 2.f;
	const TMap<int32, int32> R = Rolls(At(600.f, 0.f), Options);
	TestTrue(TEXT("on cooldown: never"), !R.Contains(Advance) && R.Contains(SoulMark));
	FRandomStream Rng(3);
	TestTrue(TEXT("on cooldown and nothing else fits: he closes in"), ChooseBossAction(At(1000.f, 0.f), Options, Rng).Choice == EBossChoice::Approach);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainRepeat, "BossTest.Boss.Brain.RepeatPenalty", BrainTestFlags)
bool FBossBrainRepeat::RunTest(const FString& Parameters)
{
	const TArray<FBossActionOption> Options = { Option(Swing, 0.f, 430.f, EBossAngleRule::FrontAndSides), Option(Reverse, 0.f, 430.f, EBossAngleRule::FrontAndSides) };
	const TMap<int32, int32> R = Rolls(At(200.f, 0.f, Swing), Options, 4000);
	const float Share = R.FindRef(Swing) / 4000.f;
	TestTrue(FString::Printf(TEXT("the last action is scored down to a quarter (%.2f of rolls, expect 0.20)"), Share), Share > 0.16f && Share < 0.24f);
	FRandomStream Rng(5);
	const TArray<FBossActionOption> Only = { Option(Swing, 0.f, 430.f, EBossAngleRule::FrontAndSides) };
	TestTrue(TEXT("but it can still repeat when it's the only fit"), ChooseBossAction(At(200.f, 0.f, Swing), Only, Rng).ActionId == Swing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainPunish, "BossTest.Boss.Brain.PunishTrigger", BrainTestFlags)
bool FBossBrainPunish::RunTest(const FString& Parameters)
{
	FBossActionOption P = Option(Punish, 0.f, 350.f, EBossAngleRule::FrontAndSides);
	P.bNeedsPunishTrigger = true;
	const TArray<FBossActionOption> Options = { Option(Swing, 0.f, 430.f, EBossAngleRule::FrontAndSides), P };
	TestFalse(TEXT("no persistent attacking: never Punishment Sweep"), Rolls(At(200.f, 0.f), Options).Contains(Punish));
	FBossSituation S = At(200.f, 0.f);
	S.bPunishTrigger = true;
	TestTrue(TEXT("persistent attacking: it comes out"), Rolls(S, Options).Contains(Punish));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainApproachOrTurn, "BossTest.Boss.Brain.ApproachOrTurn", BrainTestFlags)
bool FBossBrainApproachOrTurn::RunTest(const FString& Parameters)
{
	const TArray<FBossActionOption> Options = { Option(Swing, 0.f, 430.f, EBossAngleRule::FrontAndSides), Option(SoulMark, 250.f, 800.f, EBossAngleRule::FrontOnly) };
	FRandomStream Rng(9);
	FBossSituation Down = At(200.f, 0.f);
	Down.bPlayerDown = true;
	TestTrue(TEXT("player down: wait"), ChooseBossAction(Down, Options, Rng).Choice == EBossChoice::Wait);
	TestTrue(TEXT("beyond every range: approach"), ChooseBossAction(At(900.f, 0.f), Options, Rng).Choice == EBossChoice::Approach);
	TestTrue(TEXT("5 m at his side (Soul Mark needs the front): turn"), ChooseBossAction(At(500.f, 80.f), Options, Rng).Choice == EBossChoice::Turn);
	TestTrue(TEXT("behind: turn"), ChooseBossAction(At(200.f, 140.f), Options, Rng).Choice == EBossChoice::Turn);
	TestTrue(TEXT("no options at all: wait"), ChooseBossAction(At(200.f, 0.f), TArray<FBossActionOption>(), Rng).Choice == EBossChoice::Wait);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainSeed, "BossTest.Boss.Brain.SameSeedSameRolls", BrainTestFlags)
bool FBossBrainSeed::RunTest(const FString& Parameters)
{
	// Seeded rolls replay: a bug seen in a fight can be reproduced, and tests can check his choices.
	const TArray<FBossActionOption> Options = { Option(Swing, 0.f, 430.f, EBossAngleRule::FrontAndSides), Option(Reverse, 0.f, 430.f, EBossAngleRule::FrontAndSides),
		Option(Slam, 0.f, 800.f, EBossAngleRule::FrontOnly) };
	FRandomStream A(42), B(42);
	bool bSame = true;
	int32 Last = -1;
	for (int32 i = 0; i < 50; ++i)
	{
		const FBossChoiceResult RA = ChooseBossAction(At(200.f, 0.f, Last), Options, A);
		const FBossChoiceResult RB = ChooseBossAction(At(200.f, 0.f, Last), Options, B);
		bSame &= RA.ActionId == RB.ActionId;
		Last = RA.ActionId;
	}
	TestTrue(TEXT("the same seed gives the same 50 picks"), bSame);
	return true;
}

#endif
