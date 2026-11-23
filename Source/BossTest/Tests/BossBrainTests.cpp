// Step 4, Task 3: the placeholder brain.
#include "Misc/AutomationTest.h"
#include "Boss/BossBrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags BrainTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainPlaceholder, "BossTest.Boss.Brain.Placeholder", BrainTestFlags)
bool FBossBrainPlaceholder::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("player down: wait"), ChoosePlaceholderAction(200.f, 0.f, true) == EBossChoice::Wait);
	TestTrue(TEXT("4.2 m in front: swing"), ChoosePlaceholderAction(420.f, 0.f, false) == EBossChoice::Swing);
	TestTrue(TEXT("4.2 m at 80 deg (his side): swing"), ChoosePlaceholderAction(420.f, 80.f, false) == EBossChoice::Swing);
	TestTrue(TEXT("4.2 m at 100 deg (rear quarter): turn"), ChoosePlaceholderAction(420.f, 100.f, false) == EBossChoice::Turn);
	TestTrue(TEXT("140 deg (behind): turn"), ChoosePlaceholderAction(200.f, 140.f, false) == EBossChoice::Turn);
	TestTrue(TEXT("4.4 m in front: approach"), ChoosePlaceholderAction(440.f, 0.f, false) == EBossChoice::Approach);
	return true;
}

#endif
