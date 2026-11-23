// Step 2, Task 2: hit-stop.
#include "Misc/AutomationTest.h"
#include "Combat/HitStop.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HitStopTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float HitStopTol = 1e-4f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitStopFreezes, "BossTest.Combat.HitStop.FreezesForRealTime", HitStopTestFlags)
bool FHitStopFreezes::RunTest(const FString& Parameters)
{
	FHitStop H;
	H.Start(0.055f, 10.0);
	TestEqual(TEXT("frozen at +0.02 s"), H.Dilation(10.02), FHitStop::FrozenDilation, HitStopTol);
	TestEqual(TEXT("normal at +0.06 s"), H.Dilation(10.06), 1.f, HitStopTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitStopOverlap, "BossTest.Combat.HitStop.OverlapKeepsLaterEnd", HitStopTestFlags)
bool FHitStopOverlap::RunTest(const FString& Parameters)
{
	FHitStop H;
	H.Start(0.09f, 10.0);    // ends 10.09
	H.Start(0.055f, 10.01);  // would end 10.065: the later end stays
	TestTrue(TEXT("still frozen at 10.08"), H.IsActive(10.08));
	H.Start(0.055f, 10.08);  // ends 10.135: later, so it extends
	TestTrue(TEXT("extended to 10.12"), H.IsActive(10.12));
	TestFalse(TEXT("over at 10.14"), H.IsActive(10.14));
	return true;
}

#endif
