// Step 1, Task 1: stamina rules (spec section 4).
#include "Misc/AutomationTest.h"
#include "Combat/StaminaModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags StaminaTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float StaminaTol = 1e-3f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaSpendWithinBudget, "BossTest.Combat.Stamina.SpendWithinBudget", StaminaTestFlags)
bool FStaminaSpendWithinBudget::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.Spend(28.f);
	TestEqual(TEXT("120 - 28"), S.Current, 92.f, StaminaTol);
	TestFalse(TEXT("not exhausted"), S.bExhausted);
	TestTrue(TEXT("can still act"), S.CanStartAction());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaOverdrawTriggersExhaustion, "BossTest.Combat.Stamina.OverdrawTriggersExhaustion", StaminaTestFlags)
bool FStaminaOverdrawTriggersExhaustion::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.Current = 10.f;
	TestTrue(TEXT("any stamina above 0 lets an action start"), S.CanStartAction());
	S.Spend(28.f);
	TestEqual(TEXT("overdraw clamps to 0"), S.Current, 0.f, StaminaTol);
	TestTrue(TEXT("exhausted"), S.bExhausted);
	TestFalse(TEXT("can't start while exhausted"), S.CanStartAction());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaRegenWaitsForActionEnd, "BossTest.Combat.Stamina.RegenWaitsForActionEnd", StaminaTestFlags)
bool FStaminaRegenWaitsForActionEnd::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.BeginAction();
	S.Spend(28.f);
	S.Tick(1.0f);
	TestEqual(TEXT("no regen during the action"), S.Current, 92.f, StaminaTol);
	S.EndAction();
	S.Tick(0.85f);
	TestEqual(TEXT("no regen during the 0.85 s delay"), S.Current, 92.f, StaminaTol);
	S.Tick(0.5f);
	TestEqual(TEXT("40/s after the delay"), S.Current, 112.f, StaminaTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaExhaustionRecovers, "BossTest.Combat.Stamina.ExhaustionRecovers", StaminaTestFlags)
bool FStaminaExhaustionRecovers::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.Current = 10.f;
	S.BeginAction();
	S.Spend(28.f);
	S.EndAction();
	S.Tick(1.2f);
	TestEqual(TEXT("nothing during the 1.2 s exhausted delay"), S.Current, 0.f, StaminaTol);
	S.Tick(0.5f);
	TestEqual(TEXT("regenerating"), S.Current, 20.f, StaminaTol);
	TestTrue(TEXT("still exhausted below 25"), S.bExhausted);
	S.Tick(0.2f);
	TestEqual(TEXT("28 after another 0.2 s"), S.Current, 28.f, StaminaTol);
	TestFalse(TEXT("recovered past 25"), S.bExhausted);
	TestTrue(TEXT("can act again"), S.CanStartAction());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaSprintDrains, "BossTest.Combat.Stamina.SprintDrains", StaminaTestFlags)
bool FStaminaSprintDrains::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.Current = 100.f;
	S.SetSprinting(true);
	S.Tick(2.0f);
	TestEqual(TEXT("12/s drain, no regeneration"), S.Current, 76.f, StaminaTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaLongFrameCrossesDelay, "BossTest.Combat.Stamina.LongFrameCrossesDelay", StaminaTestFlags)
bool FStaminaLongFrameCrossesDelay::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.BeginAction();
	S.Spend(28.f);
	S.EndAction();
	S.Tick(1.35f);
	TestEqual(TEXT("only the 0.5 s past the delay regenerates"), S.Current, 112.f, StaminaTol);
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaOverlappingActions, "BossTest.Combat.Stamina.OverlappingActionsKeepRegenOff", StaminaTestFlags)
bool FStaminaOverlappingActions::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.BeginAction();      // light 1
	S.Spend(14.f);
	S.BeginAction();      // light 2 starts before light 1's end arrives
	S.Spend(14.f);
	S.EndAction();        // light 1 ends late
	S.Tick(2.f);
	TestEqual(TEXT("still acting, so no regen"), S.Current, 92.f, StaminaTol);
	S.EndAction();        // light 2 ends
	S.Tick(0.85f);
	S.Tick(0.5f);
	TestEqual(TEXT("regen after the last action ends"), S.Current, 112.f, StaminaTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaExhaustionClearsAt25, "BossTest.Combat.Stamina.ExhaustionClearsAtExactly25", StaminaTestFlags)
bool FStaminaExhaustionClearsAt25::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.Current = 5.f;
	S.Spend(10.f);
	S.Tick(1.2f);
	S.Tick(0.625f); // 0.625 s x 40/s = exactly 25
	TestEqual(TEXT("exactly 25"), S.Current, 25.f, StaminaTol);
	TestFalse(TEXT("reaching 25 ends exhaustion"), S.bExhausted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaResetRuntime, "BossTest.Combat.Stamina.ResetRuntimeKeepsTuning", StaminaTestFlags)
bool FStaminaResetRuntime::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.RegenRate = 50.f;   // tuning loaded from data
	S.BeginAction();      // reset arrives mid-action
	S.Spend(130.f);
	S.ResetRuntime();
	TestEqual(TEXT("full bar"), S.Current, 120.f, StaminaTol);
	TestFalse(TEXT("not exhausted"), S.bExhausted);
	TestEqual(TEXT("tuning kept"), S.RegenRate, 50.f, StaminaTol);
	S.Spend(20.f);        // a fresh action pattern regenerates normally
	S.Tick(0.85f);
	S.Tick(0.2f);
	TestEqual(TEXT("no stuck action after the reset"), S.Current, 110.f, StaminaTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaDrainBlocksRegen, "BossTest.Combat.Stamina.DrainBlocksRegen", StaminaTestFlags)
bool FStaminaDrainBlocksRegen::RunTest(const FString& Parameters)
{
	FStaminaModel S;
	S.ResetRuntime();
	S.Current = 100.f;
	S.SetDrain(10.f);
	S.Tick(1.f);
	TestEqual(TEXT("drains 10/s, with no regeneration"), S.Current, 90.f, StaminaTol);
	S.SetDrain(0.f);
	S.Tick(0.85f);
	TestEqual(TEXT("stopping starts the usual delay"), S.Current, 90.f, StaminaTol);
	S.Tick(0.25f);
	TestEqual(TEXT("then 40/s"), S.Current, 100.f, StaminaTol);
	S.SetDrain(10.f);
	S.Current = 5.f;
	S.Tick(1.f);
	TestTrue(TEXT("draining to 0 exhausts and stops the drain"), S.Current == 0.f && S.bExhausted && !S.IsDraining());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaminaRestartRegenDelay, "BossTest.Combat.Stamina.RestartRegenDelay", StaminaTestFlags)
bool FStaminaRestartRegenDelay::RunTest(const FString& Parameters)
{
	// Step 4c: a perfect guard that overdraws restarts the wait at its own length (0.75 s), not the exhausted 1.2 s.
	FStaminaModel S;
	S.Spend(200.f);
	S.RestartRegenDelay(0.75f);
	S.Tick(0.74f);
	TestEqual(TEXT("still waiting at 0.74 s"), S.Current, 0.f, StaminaTol);
	S.Tick(0.02f);
	TestEqual(TEXT("regenerating from 0.75 s"), S.Current, 0.4f, StaminaTol);
	return true;
}

#endif
