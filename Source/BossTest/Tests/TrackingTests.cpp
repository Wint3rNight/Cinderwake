// Step 4, Task 1: attack tracking.
#include "Misc/AutomationTest.h"
#include "Combat/Tracking.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TrackingTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float TrackingTol = 1e-3f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrackingYawStep, "BossTest.Combat.Tracking.YawStep", TrackingTestFlags)
bool FTrackingYawStep::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("150 deg/s for 0.1 s turns 15 deg"), TrackingYawStep(0.f, 90.f, 150.f, 0.1f), 15.f, TrackingTol);
	TestEqual(TEXT("never past the target"), TrackingYawStep(0.f, 10.f, 150.f, 0.1f), 10.f, TrackingTol);
	TestEqual(TEXT("the short way round: 170 toward -170 turns +20"), static_cast<float>(FRotator::NormalizeAxis(TrackingYawStep(170.f, -170.f, 150.f, 1.f))), -170.f, TrackingTol);
	TestEqual(TEXT("and only by the rate"), static_cast<float>(FRotator::NormalizeAxis(TrackingYawStep(170.f, -170.f, 150.f, 0.1f))), -175.f, TrackingTol);
	TestEqual(TEXT("turns left too"), TrackingYawStep(0.f, -90.f, 150.f, 0.1f), -15.f, TrackingTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrackingCutoff, "BossTest.Combat.Tracking.Cutoff", TrackingTestFlags)
bool FTrackingCutoff::RunTest(const FString& Parameters)
{
	FAttackTiming T;
	T.Windup = 1.05f;
	T.Active = 0.18f;
	T.Recovery = 0.90f;
	TestTrue(TEXT("tracking at 0.92 s"), IsTrackingAt(T, 0.12f, 0.92f));
	TestFalse(TEXT("committed at 0.94 s"), IsTrackingAt(T, 0.12f, 0.94f));
	TestFalse(TEXT("no tracking set: never"), IsTrackingAt(T, T.Windup, 0.f));
	return true;
}

#endif
