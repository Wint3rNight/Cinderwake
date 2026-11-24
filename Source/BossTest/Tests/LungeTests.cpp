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


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLungeIntercept, "BossTest.Combat.Lunge.InterceptPoint", LungeTestFlags)
bool FLungeIntercept::RunTest(const FString& Parameters)
{
	// Step 5: Executioner's Advance dashes to where you'll be, not where you are, with the lead capped at 2 m.
	const FVector You(1000.f, 0.f, 0.f);
	TestTrue(TEXT("standing still: your position"), InterceptPoint(You, FVector::ZeroVector, 0.30f, 200.f).Equals(You, 0.1));
	TestTrue(TEXT("running 4 m/s sideways for 0.3 s: 1.2 m ahead of you"), InterceptPoint(You, FVector(0.f, 400.f, 0.f), 0.30f, 200.f).Equals(FVector(1000.f, 120.f, 0.f), 0.1));
	// (A dodge isn't led at all: the attack component passes no velocity while the target moves by root motion.)
	TestTrue(TEXT("a fast target: the lead is capped at 2 m"), InterceptPoint(You, FVector(0.f, 1000.f, 0.f), 0.30f, 200.f).Equals(FVector(1000.f, 200.f, 0.f), 0.1));
	TestTrue(TEXT("vertical speed is ignored"), InterceptPoint(You, FVector(0.f, 0.f, 500.f), 0.30f, 200.f).Equals(You, 0.1));
	return true;
}

#endif
