// Proves the automation test harness runs and reports results (Step 1, Task 0).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossTestHarnessRuns, "BossTest.Combat.Harness.Runs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBossTestHarnessRuns::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("the harness runs"), true);
	return true;
}

#endif
