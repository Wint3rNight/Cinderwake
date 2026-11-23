// Step 1, Task 2: health and soul fragments (spec sections 4 and 8).
#include "Misc/AutomationTest.h"
#include "Combat/HealthModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HealthTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float HealthTol = 1e-3f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHealthFragmentLowersMax, "BossTest.Combat.Health.FragmentLowersMax", HealthTestFlags)
bool FHealthFragmentLowersMax::RunTest(const FString& Parameters)
{
	FHealthModel Full;
	Full.AddFragment();
	TestEqual(TEXT("max drops by 15% of 300"), Full.Max(), 255.f, HealthTol);
	TestEqual(TEXT("full health is clamped to the new max"), Full.Current, 255.f, HealthTol);

	FHealthModel Hurt;
	Hurt.Current = 100.f;
	Hurt.AddFragment();
	TestEqual(TEXT("health below the new max is untouched"), Hurt.Current, 100.f, HealthTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHealthFragmentCap, "BossTest.Combat.Health.FragmentCap", HealthTestFlags)
bool FHealthFragmentCap::RunTest(const FString& Parameters)
{
	FHealthModel H;
	H.AddFragment();
	H.AddFragment();
	H.AddFragment();
	TestEqual(TEXT("capped at 2 fragments"), H.Fragments, 2);
	TestEqual(TEXT("30% cap: max 210"), H.Max(), 210.f, HealthTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHealthFragmentNeverKills, "BossTest.Combat.Health.FragmentNeverKills", HealthTestFlags)
bool FHealthFragmentNeverKills::RunTest(const FString& Parameters)
{
	FHealthModel H;
	H.Current = 1.f;
	H.AddFragment();
	TestEqual(TEXT("max still drops"), H.Max(), 255.f, HealthTol);
	TestEqual(TEXT("1 HP stays 1 HP"), H.Current, 1.f, HealthTol);
	TestFalse(TEXT("a fragment alone never kills"), H.IsDead());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHealthReclaimRaisesMaxOnly, "BossTest.Combat.Health.ReclaimRaisesMaxOnly", HealthTestFlags)
bool FHealthReclaimRaisesMaxOnly::RunTest(const FString& Parameters)
{
	FHealthModel H;
	H.AddFragment();
	H.AddFragment();
	H.Current = 150.f;
	H.OnDamageDealtToBoss(100.f);
	TestEqual(TEXT("100 damage isn't enough to reclaim"), H.Fragments, 2);
	H.OnDamageDealtToBoss(150.f);
	TestEqual(TEXT("250 accumulated reclaims one"), H.Fragments, 1);
	TestEqual(TEXT("max rises back to 255"), H.Max(), 255.f, HealthTol);
	TestEqual(TEXT("current health doesn't change"), H.Current, 150.f, HealthTol);
	H.Heal(1000.f);
	TestEqual(TEXT("healing fills up to the raised max"), H.Current, 255.f, HealthTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHealthDamageAndDeath, "BossTest.Combat.Health.DamageAndDeath", HealthTestFlags)
bool FHealthDamageAndDeath::RunTest(const FString& Parameters)
{
	FHealthModel H;
	TestEqual(TEXT("dealt"), H.ApplyDamage(100.f), 100.f, HealthTol);
	TestEqual(TEXT("300 - 100"), H.Current, 200.f, HealthTol);
	TestFalse(TEXT("alive"), H.IsDead());
	TestEqual(TEXT("only the remaining 200 counts as dealt"), H.ApplyDamage(500.f), 200.f, HealthTol);
	TestEqual(TEXT("floored at 0"), H.Current, 0.f, HealthTol);
	TestTrue(TEXT("dead"), H.IsDead());
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHealthResetRuntime, "BossTest.Combat.Health.ResetRuntimeKeepsTuning", HealthTestFlags)
bool FHealthResetRuntime::RunTest(const FString& Parameters)
{
	FHealthModel H;
	H.ReclaimPerFragment = 300.f; // tuning loaded from data
	H.AddFragment();
	H.AddFragment();
	H.OnDamageDealtToBoss(290.f); // reclaim progress builds up, then the player dies and retries
	H.Current = 0.f;
	H.ResetRuntime();
	TestEqual(TEXT("full health"), H.Current, 300.f, HealthTol);
	TestEqual(TEXT("no fragments"), H.Fragments, 0);
	TestEqual(TEXT("tuning kept"), H.ReclaimPerFragment, 300.f, HealthTol);
	H.AddFragment();
	H.OnDamageDealtToBoss(20.f);
	TestEqual(TEXT("no stale reclaim progress"), H.Fragments, 1);
	return true;
}

#endif
