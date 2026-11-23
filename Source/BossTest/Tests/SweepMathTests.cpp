// Step 2, Task 3: weapon sweep sampling.
#include "Misc/AutomationTest.h"
#include "Combat/SweepMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags SweepTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSweepSubsteps, "BossTest.Combat.Sweep.Substeps", SweepTestFlags)
bool FSweepSubsteps::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("10 cm of travel at a 22 cm radius"), SweepSubsteps(10.f, 22.f), 1);
	TestEqual(TEXT("100 cm"), SweepSubsteps(100.f, 22.f), 5);
	TestEqual(TEXT("1000 cm is capped"), SweepSubsteps(1000.f, 22.f), 16);
	TestEqual(TEXT("no travel still samples once"), SweepSubsteps(0.f, 22.f), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSweepBladeSamples, "BossTest.Combat.Sweep.BladeSamples", SweepTestFlags)
bool FSweepBladeSamples::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("139 cm of blade at 22 cm"), BladeSampleCount(139.f, 22.f), 5);
	TestEqual(TEXT("a tiny blade still has both ends"), BladeSampleCount(5.f, 22.f), 2);
	TestEqual(TEXT("a huge blade is capped"), BladeSampleCount(2000.f, 22.f), 8);
	return true;
}

#endif
