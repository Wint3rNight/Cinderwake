// Step 3, Task 5: HUD bar layout.
#include "Misc/AutomationTest.h"
#include "UI/BossTestHUD.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HudTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float HudTol = 1e-2f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudHealthBarLayout, "BossTest.UI.HealthBar.Layout", HudTestFlags)
bool FHudHealthBarLayout::RunTest(const FString& Parameters)
{
	// 300 base HP with two fragments: max 210, at 150 HP, 1.2 px per point.
	const FHealthBarLayout L = LayoutHealthBar(150.f, 210.f, 300.f, 1.2f);
	TestEqual(TEXT("health"), L.Fill, 180.f, HudTol);
	TestEqual(TEXT("missing health"), L.Empty, 72.f, HudTol);
	TestEqual(TEXT("lost to fragments"), L.Lost, 108.f, HudTol);
	const FHealthBarLayout Full = LayoutHealthBar(300.f, 300.f, 300.f, 1.2f);
	TestEqual(TEXT("full, no fragments: all health"), Full.Fill, 360.f, HudTol);
	TestEqual(TEXT("nothing lost"), Full.Lost, 0.f, HudTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudBossBarLayout, "BossTest.UI.BossBar.Layout", HudTestFlags)
bool FHudBossBarLayout::RunTest(const FString& Parameters)
{
	const FBossBarLayout L = LayoutBossBar(4400.f, 5500.f, 4950.f, 3300.f, 800.f);
	TestEqual(TEXT("health"), L.Fill, 640.f, HudTol);
	TestEqual(TEXT("the trail of recent damage"), L.Trail, 80.f, HudTol);
	TestEqual(TEXT("the transformation mark"), L.PhaseMark, 480.f, HudTol);
	const FBossBarLayout Caught = LayoutBossBar(4400.f, 5500.f, 4000.f, 3300.f, 800.f);
	TestEqual(TEXT("a trail below health shows nothing"), Caught.Trail, 0.f, HudTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudDamageTrail, "BossTest.UI.BossBar.Trail", HudTestFlags)
bool FHudDamageTrail::RunTest(const FString& Parameters)
{
	FDamageTrail T;
	T.DrainPerSecond = 1000.f;
	T.Reset(5500.f);
	T.Tick(0.f, 5000.f);
	TestEqual(TEXT("a hit leaves the trail where health was"), T.Value(), 5500.f, HudTol);
	T.Tick(0.6f, 5000.f);
	TestEqual(TEXT("it holds for 0.6 s"), T.Value(), 5500.f, HudTol);
	T.Tick(0.25f, 5000.f);
	TestEqual(TEXT("then drains at 1000/s"), T.Value(), 5250.f, HudTol);
	T.Tick(1.f, 5000.f);
	TestEqual(TEXT("never below health"), T.Value(), 5000.f, HudTol);
	T.Tick(0.f, 5500.f);
	TestEqual(TEXT("healing or a reset snaps it up"), T.Value(), 5500.f, HudTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudPoiseBarLayout, "BossTest.UI.PoiseBar.Layout", HudTestFlags)
bool FHudPoiseBarLayout::RunTest(const FString& Parameters)
{
	// Step 4c: his poise, under his health bar.
	const FPoiseBarLayout Half = LayoutPoiseBar(325.f, 650.f, false, 400.f);
	TestTrue(TEXT("half full: half the width, shown, not grey"), FMath::IsNearlyEqual(Half.Fill, 200.f, HudTol) && Half.bVisible && !Half.bGrey);
	TestFalse(TEXT("empty: hidden"), LayoutPoiseBar(0.f, 650.f, false, 400.f).bVisible);
	const FPoiseBarLayout Immune = LayoutPoiseBar(0.f, 650.f, true, 400.f);
	TestTrue(TEXT("immune: shown empty and grey"), Immune.bVisible && Immune.bGrey && Immune.Fill == 0.f);
	TestEqual(TEXT("clamped to the width"), LayoutPoiseBar(800.f, 650.f, false, 400.f).Fill, 400.f, HudTol);
	return true;
}

#endif
