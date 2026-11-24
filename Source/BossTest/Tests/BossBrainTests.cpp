// Step 4's placeholder brain, still his fallback when he has no actions (the director's tests: BossDirectorTests.cpp).
#include "Misc/AutomationTest.h"
#include "Boss/BossBrain.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossBrainPlaceholder, "BossTest.Boss.Brain.Placeholder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBossBrainPlaceholder::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("player down: wait"), ChoosePlaceholderAction(200.f, 0.f, true) == EBossChoice::Wait);
	TestTrue(TEXT("4.2 m in front: swing"), ChoosePlaceholderAction(420.f, 0.f, false) == EBossChoice::Swing);
	TestTrue(TEXT("100 deg: turn"), ChoosePlaceholderAction(420.f, 100.f, false) == EBossChoice::Turn);
	TestTrue(TEXT("4.4 m in front: approach"), ChoosePlaceholderAction(440.f, 0.f, false) == EBossChoice::Approach);
	return true;
}

#endif
