// Step 2, Task 1: attack timelines (real seconds mapped onto clip time).
#include "Misc/AutomationTest.h"
#include "Combat/AttackTimeline.h"
#include "Combat/AttackDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TimelineTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float TimelineTol = 1e-3f;

	// Executioner's Swing on Swing1_Slow: windup 1.05, active 0.18, recovery 0.90.
	FAttackTiming SwingTiming()
	{
		FAttackTiming T;
		T.Windup = 1.05f; T.Active = 0.18f; T.Recovery = 0.90f;
		T.ClipActiveStart = 0.40f; T.ClipHoldAt = 0.40f; T.ClipActiveEnd = 0.58f; T.ClipEnd = 1.45f;
		return T;
	}

	bool HasPrefix(const TArray<FString>& Messages, const TCHAR* Prefix)
	{
		for (const FString& M : Messages)
		{
			if (M.StartsWith(Prefix)) { return true; }
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelinePhases, "BossTest.Combat.Timeline.PhasesInRealSeconds", TimelineTestFlags)
bool FTimelinePhases::RunTest(const FString& Parameters)
{
	const FAttackTiming T = SwingTiming();
	TestTrue(TEXT("windup at 0.5 s"), EvaluateAttack(T, 0.5f).Phase == EAttackPhase::Windup);
	TestTrue(TEXT("active at 1.05 s"), EvaluateAttack(T, 1.05f).Phase == EAttackPhase::Active);
	TestTrue(TEXT("recovery at 1.24 s"), EvaluateAttack(T, 1.24f).Phase == EAttackPhase::Recovery);
	TestTrue(TEXT("done at 2.14 s"), EvaluateAttack(T, 2.14f).Phase == EAttackPhase::Done);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineClipMapping, "BossTest.Combat.Timeline.ClipTimeMapping", TimelineTestFlags)
bool FTimelineClipMapping::RunTest(const FString& Parameters)
{
	const FAttackTiming T = SwingTiming();
	TestEqual(TEXT("half the windup = half of clip 0.40"), EvaluateAttack(T, 0.525f).ClipTime, 0.20f, TimelineTol);
	TestEqual(TEXT("mid-active"), EvaluateAttack(T, 1.14f).ClipTime, 0.49f, TimelineTol);
	TestEqual(TEXT("the end"), EvaluateAttack(T, T.Total()).ClipTime, 1.45f, TimelineTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineHold, "BossTest.Combat.Timeline.HoldFreezesPose", TimelineTestFlags)
bool FTimelineHold::RunTest(const FString& Parameters)
{
	// Delayed Execution: 1.85 s windup, 1.25 s of it held at the loaded pose (clip 0.35).
	FAttackTiming T = SwingTiming();
	T.Windup = 1.85f; T.Hold = 1.25f; T.ClipHoldAt = 0.35f;
	const FAttackSample Held = EvaluateAttack(T, 1.0f);
	TestTrue(TEXT("holding at 1.0 s"), Held.Phase == EAttackPhase::Hold);
	TestEqual(TEXT("held at the loaded pose"), Held.ClipTime, 0.35f, TimelineTol);
	const FAttackSample Strike = EvaluateAttack(T, 1.85f);
	TestTrue(TEXT("active at 1.85 s"), Strike.Phase == EAttackPhase::Active);
	TestEqual(TEXT("at the clip's active start"), Strike.ClipTime, 0.40f, TimelineTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineWindows, "BossTest.Combat.Timeline.ChainAndFreeWindows", TimelineTestFlags)
bool FTimelineWindows::RunTest(const FString& Parameters)
{
	// Light 1 (Primary_A at speed 1.0): active 0.15-0.28, free at 0.70, chain from 0.38.
	FAttackTiming T;
	T.Windup = 0.15f; T.Active = 0.13f; T.Recovery = 0.42f; T.ChainFrom = 0.38f;
	T.ClipHoldAt = 0.15f; T.ClipActiveStart = 0.15f; T.ClipActiveEnd = 0.28f; T.ClipEnd = 0.70f;
	TestFalse(TEXT("chain shut at 0.37"), EvaluateAttack(T, 0.37f).bChainOpen);
	TestTrue(TEXT("chain open at 0.38"), EvaluateAttack(T, 0.38f).bChainOpen);
	TestFalse(TEXT("not free at 0.69"), EvaluateAttack(T, 0.69f).bFree);
	TestTrue(TEXT("free at 0.70"), EvaluateAttack(T, 0.70f).bFree);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineLongFrame, "BossTest.Combat.Timeline.LongFrameOverlapsActive", TimelineTestFlags)
bool FTimelineLongFrame::RunTest(const FString& Parameters)
{
	const FAttackTiming T = SwingTiming();
	TestTrue(TEXT("a 1.0-1.3 s frame overlaps the 1.05-1.23 active window"), ActiveOverlap(T, 1.0f, 1.3f));
	TestFalse(TEXT("a frame entirely before it doesn't"), ActiveOverlap(T, 0.5f, 1.0f));
	TestFalse(TEXT("a frame entirely after it doesn't"), ActiveOverlap(T, 1.3f, 1.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineValidateOrder, "BossTest.Combat.Timeline.ValidateOrder", TimelineTestFlags)
bool FTimelineValidateOrder::RunTest(const FString& Parameters)
{
	FAttackTiming T = SwingTiming();
	TestFalse(TEXT("good data has no errors"), HasPrefix(ValidateTiming(T), TEXT("Error:")));
	T.ClipActiveEnd = 0.30f; // before the active start
	TestTrue(TEXT("markers out of order are an error"), HasPrefix(ValidateTiming(T), TEXT("Error:")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineValidateStretch, "BossTest.Combat.Timeline.ValidateStretch", TimelineTestFlags)
bool FTimelineValidateStretch::RunTest(const FString& Parameters)
{
	FAttackTiming T = SwingTiming(); // 0.40 s of clip played over a 1.05 s windup = 2.6x slower
	TestTrue(TEXT("over 2x stretch warns"), HasPrefix(ValidateTiming(T), TEXT("Warning:")));
	T.Hold = 0.6f;                   // the same windup with a hold: 0.40 s over 0.45 s
	TestFalse(TEXT("a hold instead of a stretch doesn't warn"), HasPrefix(ValidateTiming(T), TEXT("Warning:")));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineStepCap, "BossTest.Combat.Timeline.StepCappedAt30Hz", TimelineTestFlags)
bool FTimelineStepCap::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a 0.3 s hitch advances only 1/30 s"), ClampAttackStep(0.3f), 1.f / 30.f, TimelineTol);
	TestEqual(TEXT("a normal frame is untouched"), ClampAttackStep(0.01f), 0.01f, TimelineTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineArmorByPhase, "BossTest.Combat.Timeline.ArmorByPhase", TimelineTestFlags)
bool FTimelineArmorByPhase::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("windup: windup armour"), ArmorAt(EAttackPhase::Windup, EArmor::Windup, EArmor::Full) == EArmor::Windup);
	TestTrue(TEXT("hold: windup armour"), ArmorAt(EAttackPhase::Hold, EArmor::Windup, EArmor::Full) == EArmor::Windup);
	TestTrue(TEXT("active: full armour"), ArmorAt(EAttackPhase::Active, EArmor::Windup, EArmor::Full) == EArmor::Full);
	TestTrue(TEXT("recovery: none"), ArmorAt(EAttackPhase::Recovery, EArmor::Windup, EArmor::Full) == EArmor::None);
	TestTrue(TEXT("done: none"), ArmorAt(EAttackPhase::Done, EArmor::Windup, EArmor::Full) == EArmor::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineLungeWindow, "BossTest.Combat.Timeline.LungeWindow", TimelineTestFlags)
bool FTimelineLungeWindow::RunTest(const FString& Parameters)
{
	FAttackTiming Swing;
	Swing.Windup = 1.05f;
	Swing.Active = 0.18f;
	Swing.Recovery = 0.90f;
	const FLungeWindow Boss = LungeWindow(Swing, 0.12f, 0.5f);
	TestEqual(TEXT("Executioner's Swing: from the tracking cutoff"), Boss.Start, 0.93f, TimelineTol);
	TestEqual(TEXT("to mid-active"), Boss.Duration, 0.21f, TimelineTol);
	const FLungeWindow Player = LungeWindow(Swing, 0.f, 1.f);
	TestEqual(TEXT("defaults: from the windup's end"), Player.Start, 1.05f, TimelineTol);
	TestEqual(TEXT("over the active window"), Player.Duration, 0.18f, TimelineTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDefinitionDefaults, "BossTest.Combat.Definition.DefaultsKeepPlayerBehaviour", TimelineTestFlags)
bool FDefinitionDefaults::RunTest(const FString& Parameters)
{
	// The player's saved assets don't contain the Step 4 fields, so they get these C++ defaults.
	const UAttackDefinition* D = GetDefault<UAttackDefinition>();
	TestEqual(TEXT("no tracking"), D->TrackingRate, 0.f);
	TestEqual(TEXT("the lunge starts at the hit window"), D->LungeStartsBeforeActive, 0.f);
	TestEqual(TEXT("and runs through it"), D->LungeEndsIntoActive, 1.f);
	TestFalse(TEXT("at a constant speed"), D->bEaseLunge);
	TestTrue(TEXT("no armour"), D->GetWindupArmor() == EArmor::None && D->GetActiveArmor() == EArmor::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineEasedHold, "BossTest.Combat.Timeline.EasedHoldIsContinuous", TimelineTestFlags)
bool FTimelineEasedHold::RunTest(const FString& Parameters)
{
	// Executioner's Swing: a 1.05 s windup with a 0.40 s hold at the raised pose (clip 0.40), active at clip 0.43.
	FAttackTiming T;
	T.Windup = 1.05f; T.Hold = 0.40f; T.Active = 0.18f; T.Recovery = 0.90f;
	T.ClipHoldAt = 0.40f; T.ClipActiveStart = 0.43f; T.ClipActiveEnd = 0.57f; T.ClipEnd = 1.60f;
	T.bEaseIntoHold = true;
	// Find where the hold starts: the first sample marked Hold.
	float HoldStart = -1.f;
	for (float t = 0.f; t < T.Windup; t += 0.001f)
	{
		if (EvaluateAttack(T, t).Phase == EAttackPhase::Hold) { HoldStart = t; break; }
	}
	TestTrue(TEXT("there is a hold"), HoldStart > 0.f);
	const float d = 0.002f;
	const float SpeedBefore = (EvaluateAttack(T, HoldStart - d).ClipTime - EvaluateAttack(T, HoldStart - 2.f * d).ClipTime) / d;
	const float SpeedAfter = (EvaluateAttack(T, HoldStart + 2.f * d).ClipTime - EvaluateAttack(T, HoldStart + d).ClipTime) / d;
	TestTrue(TEXT("no sudden stop: the pose keeps its speed into the hold"), SpeedBefore > 0.f && FMath::Abs(SpeedAfter - SpeedBefore) < 0.1f * SpeedBefore);
	float Last = -1.f;
	bool bRising = true;
	for (float t = HoldStart; t < HoldStart + T.Hold; t += 0.01f)
	{
		const float Clip = EvaluateAttack(T, t).ClipTime;
		bRising &= Clip >= Last;
		Last = Clip;
	}
	TestTrue(TEXT("it keeps drifting forward through the hold, slowing down"), bRising);
	TestEqual(TEXT("and reaches the raised pose at the hold's end"), EvaluateAttack(T, HoldStart + T.Hold).ClipTime, 0.40f, 0.005f);
	TestTrue(TEXT("the strike still opens at 1.05 s"), EvaluateAttack(T, 1.05f).Phase == EAttackPhase::Active);
	TestEqual(TEXT("at clip 0.43"), EvaluateAttack(T, 1.05f).ClipTime, 0.43f, 0.005f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineBranchStep, "BossTest.Combat.Timeline.BranchStepClamps", TimelineTestFlags)
bool FTimelineBranchStep::RunTest(const FString& Parameters)
{
	const FBranchStep Reach = StepToBranch(0.38f, 1.f / 30.f, 0.40f, false);
	TestTrue(TEXT("a step that would pass the branch stops exactly on it"), Reach.bReachedBranch && FMath::IsNearlyEqual(Reach.Step, 0.02f, 1e-4f));
	const FBranchStep Before = StepToBranch(0.30f, 1.f / 30.f, 0.40f, false);
	TestTrue(TEXT("before it: unchanged"), !Before.bReachedBranch && FMath::IsNearlyEqual(Before.Step, 1.f / 30.f, 1e-6f));
	const FBranchStep Resolved = StepToBranch(0.40f, 1.f / 30.f, 0.40f, true);
	TestTrue(TEXT("resolved: unchanged"), !Resolved.bReachedBranch && FMath::IsNearlyEqual(Resolved.Step, 1.f / 30.f, 1e-6f));
	const FBranchStep None = StepToBranch(0.38f, 1.f / 30.f, -1.f, false);
	TestTrue(TEXT("no branch: unchanged"), !None.bReachedBranch && FMath::IsNearlyEqual(None.Step, 1.f / 30.f, 1e-6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineValidateBranch, "BossTest.Combat.Timeline.ValidateBranch", TimelineTestFlags)
bool FTimelineValidateBranch::RunTest(const FString& Parameters)
{
	// DA_Heavy and DA_ChargedHeavy as built.
	FAttackTiming Heavy;
	Heavy.Windup = 0.44f; Heavy.Active = 0.18f; Heavy.Recovery = 0.85f;
	Heavy.ClipHoldAt = 0.27f; Heavy.ClipActiveStart = 0.27f; Heavy.ClipActiveEnd = 0.43f; Heavy.ClipEnd = 1.27f;
	FAttackTiming Charged = Heavy;
	Charged.Windup = 0.94f; Charged.Hold = 0.50f; Charged.ClipHoldAt = 0.26f; Charged.Recovery = 0.90f;
	TestEqual(TEXT("the real pair at 0.40 s is valid"), ValidateBranch(Heavy, Charged, true, 0.40f).Num(), 0);
	TestTrue(TEXT("too close to the strike (0.43 s of a 0.44 s windup)"), ValidateBranch(Heavy, Charged, true, 0.43f).Num() > 0);
	// Step 4d: a variant on another clip restarts its own timeline at the branch (the charged heavy's deeper
	// gather on Melee_B), so no pose has to match; it still has to come before the strike.
	FAttackTiming Gather = Charged;
	Gather.Windup = 0.74f; Gather.Hold = 0.30f; Gather.ClipHoldAt = 0.04f; Gather.ClipActiveStart = 0.52f;
	TestEqual(TEXT("another clip, any pose: valid"), ValidateBranch(Heavy, Gather, false, 0.40f).Num(), 0);
	TestTrue(TEXT("another clip, but too close to the strike"), ValidateBranch(Heavy, Gather, false, 0.43f).Num() > 0);
	FAttackTiming Mismatched = Charged;
	Mismatched.Windup = 1.10f; // a slower pre-hold windup: the pose at 0.40 s differs
	TestTrue(TEXT("a different pose at the branch"), ValidateBranch(Heavy, Mismatched, true, 0.40f).Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineClipStartTrims, "BossTest.Combat.Timeline.ClipStartTrims", TimelineTestFlags)
bool FTimelineClipStartTrims::RunTest(const FString& Parameters)
{
	// Step 4e: a borrowed clip can start partway in (Kwang's Ability_R skips its rising cut): the windup runs from
	// ClipStartAt to the active start instead of from 0.
	FAttackTiming T;
	T.ClipStartAt = 0.18f; T.ClipHoldAt = 0.18f; T.ClipActiveStart = 0.43f; T.ClipActiveEnd = 0.52f; T.ClipEnd = 1.50f;
	T.Windup = 0.50f; T.Active = 0.09f; T.Recovery = 0.95f;
	TestEqual(TEXT("it starts at the trim"), EvaluateAttack(T, 0.f).ClipTime, 0.18f, 0.001f);
	TestEqual(TEXT("halfway through the windup, halfway from the trim to the strike"), EvaluateAttack(T, 0.25f).ClipTime, 0.305f, 0.001f);
	TestEqual(TEXT("the strike at the active start"), EvaluateAttack(T, 0.50f).ClipTime, 0.43f, 0.001f);
	TestEqual(TEXT("valid"), ValidateTiming(T).Num(), 0);
	// The charged heavy as built: trimmed at 0.13, easing into the overhead pose at 0.17 over the whole 0.45 s hold
	// (the drift is capped at the trim, so the hold starts at once), then the leap at about 0.86x to the strike.
	FAttackTiming C;
	C.ClipStartAt = 0.13f; C.ClipHoldAt = 0.17f; C.ClipActiveStart = 0.41f; C.ClipActiveEnd = 0.50f; C.ClipEnd = 1.27f;
	C.Windup = 0.73f; C.Hold = 0.45f; C.bEaseIntoHold = true; C.Active = 0.10f; C.Recovery = 0.92f;
	TestEqual(TEXT("charged: it starts at the trim"), EvaluateAttack(C, 0.f).ClipTime, 0.13f, 0.002f);
	TestTrue(TEXT("charged: easing through the hold from the start"), EvaluateAttack(C, 0.f).Phase == EAttackPhase::Hold);
	TestEqual(TEXT("charged: at the overhead pose when the hold ends"), EvaluateAttack(C, 0.45f).ClipTime, 0.17f, 0.002f);
	TestEqual(TEXT("charged: the strike at the windup's end"), EvaluateAttack(C, 0.73f).ClipTime, 0.41f, 0.002f);
	TestEqual(TEXT("charged: valid"), ValidateTiming(C).Num(), 0);
	FAttackTiming Bad = T;
	Bad.ClipHoldAt = 0.10f;
	TestTrue(TEXT("a hold before the trim is an error"), ValidateTiming(Bad).Num() > 0);
	return true;
}

#endif
