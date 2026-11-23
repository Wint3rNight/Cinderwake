// Step 3, Task 4: lock-on rules (spec section 13).
#include "Misc/AutomationTest.h"
#include "Player/LockOnComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags LockTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockOnAcquire, "BossTest.Player.LockOn.Acquire", LockTestFlags)
bool FLockOnAcquire::RunTest(const FString& Parameters)
{
	const FLockOnRules R;
	TestTrue(TEXT("14.99 m, 59 degrees, in sight"), CanAcquireTarget(R, 1499.f, 59.f, true));
	TestFalse(TEXT("15.01 m is too far"), CanAcquireTarget(R, 1501.f, 59.f, true));
	TestFalse(TEXT("61 degrees is too far off centre"), CanAcquireTarget(R, 1499.f, 61.f, true));
	TestFalse(TEXT("never through walls"), CanAcquireTarget(R, 500.f, 10.f, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockOnBreak, "BossTest.Player.LockOn.Break", LockTestFlags)
bool FLockOnBreak::RunTest(const FString& Parameters)
{
	const FLockOnRules R;
	TestTrue(TEXT("beyond 25 m"), ShouldBreakLock(R, 2501.f, 0.f));
	TestFalse(TEXT("24.99 m, unseen for 1.9 s: holds"), ShouldBreakLock(R, 2499.f, 1.9f));
	TestTrue(TEXT("unseen for 2 s"), ShouldBreakLock(R, 2499.f, 2.0f));
	return true;
}

#endif
