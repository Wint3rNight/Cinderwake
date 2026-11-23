// Step 1, Task 3: poise, stagger, armour and cast breaks (spec section 9).
#include "Misc/AutomationTest.h"
#include "Combat/PoiseModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags PoiseTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float PoiseTol = 1e-3f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseStaggerAtThreshold, "BossTest.Combat.Poise.StaggerAtThreshold", PoiseTestFlags)
bool FPoiseStaggerAtThreshold::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	for (int32 i = 0; i < 18; ++i)
	{
		TestTrue(TEXT("no stagger below 650"), P.Apply(35.f, false, EArmor::None) == EPoiseOutcome::None);
	}
	TestEqual(TEXT("18 x 35"), P.Current(), 630.f, PoiseTol);
	TestTrue(TEXT("the 19th light staggers"), P.Apply(35.f, false, EArmor::None) == EPoiseOutcome::Stagger);
	TestTrue(TEXT("staggered"), P.IsStaggered());

	FPoiseModel Exact;
	TestTrue(TEXT("exactly 650 staggers"), Exact.Apply(650.f, false, EArmor::None) == EPoiseOutcome::Stagger);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseDecaysAfterDelay, "BossTest.Combat.Poise.DecaysAfterDelay", PoiseTestFlags)
bool FPoiseDecaysAfterDelay::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	P.Apply(100.f, false, EArmor::None);
	P.Tick(3.0f);
	TestEqual(TEXT("held for 3 s"), P.Current(), 100.f, PoiseTol);
	P.Tick(1.0f);
	TestEqual(TEXT("then 10/s"), P.Current(), 90.f, PoiseTol);

	FPoiseModel LongFrame;
	LongFrame.Apply(100.f, false, EArmor::None);
	LongFrame.Tick(4.0f);
	TestEqual(TEXT("one 4 s tick only decays the 1 s past the delay"), LongFrame.Current(), 90.f, PoiseTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseResetAndImmunity, "BossTest.Combat.Poise.ResetAndImmunity", PoiseTestFlags)
bool FPoiseResetAndImmunity::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	P.Apply(650.f, false, EArmor::None);
	TestEqual(TEXT("bar resets when the stagger starts"), P.Current(), 0.f, PoiseTol);
	P.Apply(300.f, false, EArmor::None);
	TestEqual(TEXT("ignored during the stagger"), P.Current(), 0.f, PoiseTol);
	P.Tick(1.25f);
	TestFalse(TEXT("stagger over after 1.25 s"), P.IsStaggered());
	TestTrue(TEXT("immune"), P.IsImmune());
	P.Apply(300.f, false, EArmor::None);
	TestEqual(TEXT("ignored during immunity"), P.Current(), 0.f, PoiseTol);
	P.Tick(8.0f);
	TestFalse(TEXT("immunity over after 8 s"), P.IsImmune());
	P.Apply(300.f, false, EArmor::None);
	TestEqual(TEXT("accepted again"), P.Current(), 300.f, PoiseTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseWindupArmour, "BossTest.Combat.Poise.WindupArmour", PoiseTestFlags)
bool FPoiseWindupArmour::RunTest(const FString& Parameters)
{
	FPoiseModel Light;
	Light.Apply(640.f, false, EArmor::None);
	TestTrue(TEXT("a light hit breaking poise in windup defers"), Light.Apply(35.f, false, EArmor::Windup) == EPoiseOutcome::DeferredStagger);
	TestFalse(TEXT("not staggered yet"), Light.IsStaggered());
	TestTrue(TEXT("deferred stagger pending"), Light.HasDeferredStagger());

	FPoiseModel Heavy;
	Heavy.Apply(640.f, false, EArmor::None);
	TestTrue(TEXT("a heavy hit breaking poise in windup staggers now"), Heavy.Apply(120.f, true, EArmor::Windup) == EPoiseOutcome::Stagger);
	TestTrue(TEXT("staggered"), Heavy.IsStaggered());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseFullArmourDefers, "BossTest.Combat.Poise.FullArmourDefers", PoiseTestFlags)
bool FPoiseFullArmourDefers::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	P.Apply(640.f, false, EArmor::None);
	TestTrue(TEXT("even a heavy defers during active"), P.Apply(120.f, true, EArmor::Full) == EPoiseOutcome::DeferredStagger);
	P.Tick(10.f);
	TestTrue(TEXT("decay pauses while a stagger is pending"), P.Current() >= 650.f - PoiseTol);
	P.BeginStagger();
	TestTrue(TEXT("the deferred stagger plays at recovery"), P.IsStaggered());
	TestFalse(TEXT("no longer pending"), P.HasDeferredStagger());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastBreaksAtThreshold, "BossTest.Combat.Cast.BreaksAtThreshold", PoiseTestFlags)
bool FCastBreaksAtThreshold::RunTest(const FString& Parameters)
{
	FCastBreak C;
	TestFalse(TEXT("a closed cast never breaks"), C.Apply(500.f));
	C.Open(100.f);
	TestTrue(TEXT("open"), C.IsOpen());
	TestFalse(TEXT("35"), C.Apply(35.f));
	TestFalse(TEXT("70"), C.Apply(35.f));
	TestTrue(TEXT("105 breaks it"), C.Apply(35.f));
	TestFalse(TEXT("closed after breaking"), C.IsOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastCountsDuringImmunity, "BossTest.Combat.Cast.CountsDuringImmunity", PoiseTestFlags)
bool FCastCountsDuringImmunity::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	P.Apply(650.f, false, EArmor::None);
	P.Tick(1.25f);
	TestTrue(TEXT("poise is immune"), P.IsImmune());

	FCastBreak C;
	C.Open(70.f); // Soul Mark
	TestTrue(TEXT("poise ignores the hit"), P.Apply(35.f, false, EArmor::None) == EPoiseOutcome::None);
	TestFalse(TEXT("the cast still counts it"), C.Apply(35.f));
	TestTrue(TEXT("and breaks at 70"), C.Apply(35.f));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseDeferredCanBeCancelled, "BossTest.Combat.Poise.DeferredCanBeCancelled", PoiseTestFlags)
bool FPoiseDeferredCanBeCancelled::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	P.Apply(700.f, false, EArmor::Full);
	TestTrue(TEXT("pending"), P.HasDeferredStagger());
	P.CancelDeferredStagger();
	TestFalse(TEXT("no longer pending"), P.HasDeferredStagger());
	TestFalse(TEXT("cancelling doesn't stagger"), P.IsStaggered());
	P.Apply(35.f, false, EArmor::None);
	TestEqual(TEXT("the bar works again from 0"), P.Current(), 35.f, PoiseTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseLongFrameCrossesPhases, "BossTest.Combat.Poise.LongFrameCrossesPhases", PoiseTestFlags)
bool FPoiseLongFrameCrossesPhases::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	P.Apply(650.f, false, EArmor::None);
	P.Tick(2.0f); // crosses the 1.25 s stagger into immunity
	TestFalse(TEXT("stagger over"), P.IsStaggered());
	TestTrue(TEXT("immune"), P.IsImmune());
	P.Tick(7.0f); // 7.25 s of immunity was left: still 0.25 s to go
	TestTrue(TEXT("still immune"), P.IsImmune());
	P.Tick(0.5f); // crosses the end of immunity
	TestFalse(TEXT("immunity over"), P.IsImmune());
	P.Apply(100.f, false, EArmor::None);
	TestEqual(TEXT("accepting again"), P.Current(), 100.f, PoiseTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseResetRuntime, "BossTest.Combat.Poise.ResetRuntimeKeepsTuning", PoiseTestFlags)
bool FPoiseResetRuntime::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	P.Threshold = 800.f; // phase-two tuning
	P.Apply(900.f, false, EArmor::Full);
	P.ResetRuntime();
	TestFalse(TEXT("no pending stagger"), P.HasDeferredStagger());
	TestFalse(TEXT("not staggered"), P.IsStaggered());
	TestFalse(TEXT("not immune"), P.IsImmune());
	TestEqual(TEXT("empty bar"), P.Current(), 0.f, PoiseTol);
	TestEqual(TEXT("tuning kept"), P.Threshold, 800.f, PoiseTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoiseHeavyBreaksDeferred, "BossTest.Combat.Poise.HeavyBreaksDeferredInWindup", PoiseTestFlags)
bool FPoiseHeavyBreaksDeferred::RunTest(const FString& Parameters)
{
	FPoiseModel P;
	TestTrue(TEXT("a light breaks the bar under windup armour: deferred"), P.Apply(650.f, false, EArmor::Windup) == EPoiseOutcome::DeferredStagger);
	TestTrue(TEXT("a heavy during the same windup staggers now (spec section 9)"), P.Apply(10.f, true, EArmor::Windup) == EPoiseOutcome::Stagger);
	TestTrue(TEXT("staggered, nothing pending"), P.IsStaggered() && !P.HasDeferredStagger());
	FPoiseModel Q;
	Q.Apply(650.f, false, EArmor::Full);
	TestTrue(TEXT("full armour still holds a heavy"), Q.Apply(10.f, true, EArmor::Full) == EPoiseOutcome::None && Q.HasDeferredStagger());
	return true;
}

#endif
