// Step 3, Task 1: how far a lunge carries.
#include "Misc/AutomationTest.h"
#include "Combat/Lunge.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags LungeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float LungeTol = 1e-3f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLungeDistance, "BossTest.Combat.Lunge.Distance", LungeTestFlags)
bool FLungeDistance::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("closes the gap down to the stop distance"), LungeDistance(100.f, 70.f, 40.f), 60.f, LungeTol);
	TestEqual(TEXT("never more than the cap"), LungeDistance(300.f, 70.f, 40.f), 70.f, LungeTol);
	TestEqual(TEXT("already closer than the stop distance: no lunge"), LungeDistance(30.f, 70.f, 40.f), 0.f, LungeTol);
	TestEqual(TEXT("no target: half the cap"), LungeDistance(TOptional<float>(), 70.f, 40.f), 35.f, LungeTol);
	return true;
}

#endif
