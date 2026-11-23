// Step 3, Task 2: the dodge's distance over time.
#include "Misc/AutomationTest.h"
#include "Player/DodgeProfile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DodgeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float DodgeTol = 0.5f; // cm
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDodgeFrontLoaded, "BossTest.Player.Dodge.FrontLoaded", DodgeTestFlags)
bool FDodgeFrontLoaded::RunTest(const FString& Parameters)
{
	const FDodgeProfile P = FDodgeProfile::Make(270.f, 0.72f, 160.f, 0.25f);
	TestEqual(TEXT("1.6 m by 0.25 s"), P.DistanceAt(0.25f), 160.f, DodgeTol);
	TestEqual(TEXT("2.7 m by 0.72 s"), P.DistanceAt(0.72f), 270.f, DodgeTol);
	TestEqual(TEXT("nothing more after the end"), P.DistanceAt(1.0f), 270.f, DodgeTol);
	TestEqual(TEXT("starts at rest"), P.DistanceAt(0.f), 0.f, DodgeTol);
	TestTrue(TEXT("fastest at the start"), P.SpeedAt(0.f) > P.SpeedAt(0.25f));
	TestTrue(TEXT("still slowing"), P.SpeedAt(0.25f) > P.SpeedAt(0.7f));
	TestTrue(TEXT("still moving just before the end"), P.SpeedAt(0.7f) > 0.f);
	TestEqual(TEXT("stopped after the end"), P.SpeedAt(0.8f), 0.f, DodgeTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDodgeBackstepScales, "BossTest.Player.Dodge.BackstepScales", DodgeTestFlags)
bool FDodgeBackstepScales::RunTest(const FString& Parameters)
{
	const FDodgeProfile Backstep = FDodgeProfile::Make(270.f, 0.72f, 160.f, 0.25f).Scaled(160.f);
	TestEqual(TEXT("1.6 m by 0.72 s"), Backstep.DistanceAt(0.72f), 160.f, DodgeTol);
	TestEqual(TEXT("the same shape: 160 x 160 / 270 by 0.25 s"), Backstep.DistanceAt(0.25f), 94.8f, DodgeTol);
	return true;
}

#endif
