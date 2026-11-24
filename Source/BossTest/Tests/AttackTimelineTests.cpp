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
	// The first 4e charged heavy (since replaced by a creeping gather in the crouch): trimmed at 0.13, easing into the
	// overhead pose at 0.17 over the whole 0.45 s hold (the drift is capped at the trim, so the hold starts at once).
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineHoldCreeps, "BossTest.Combat.Timeline.HoldCreeps", TimelineTestFlags)
bool FTimelineHoldCreeps::RunTest(const FString& Parameters)
{
	// After the 4e play (the charged heavy's frozen gather read as weird): a hold can creep at a steady slow rate from
	// ClipHoldAt to ClipHoldTo instead of freezing, and the windup carries on from ClipHoldTo at its own rate.
	FAttackTiming T; // the charged heavy's gather: the crouch at the clip's start, then the leap at full speed
	T.ClipHoldAt = 0.f; T.ClipHoldTo = 0.06f; T.ClipActiveStart = 0.41f; T.ClipActiveEnd = 0.50f; T.ClipEnd = 1.27f;
	T.Windup = 0.73f; T.Hold = 0.38f; T.Active = 0.10f; T.Recovery = 0.92f;
	TestTrue(TEXT("holding from the start"), EvaluateAttack(T, 0.f).Phase == EAttackPhase::Hold);
	TestEqual(TEXT("halfway through the hold, halfway along the creep"), EvaluateAttack(T, 0.19f).ClipTime, 0.03f, 0.001f);
	TestEqual(TEXT("at the creep's end when the hold ends"), EvaluateAttack(T, 0.38f).ClipTime, 0.06f, 0.001f);
	TestTrue(TEXT("then winding up again"), EvaluateAttack(T, 0.40f).Phase == EAttackPhase::Windup);
	TestEqual(TEXT("full speed from the creep's end (0.35 s of clip in 0.35 s)"), EvaluateAttack(T, 0.555f).ClipTime, 0.235f, 0.001f);
	TestEqual(TEXT("the strike at the windup's end"), EvaluateAttack(T, 0.73f).ClipTime, 0.41f, 0.001f);
	TestEqual(TEXT("valid"), ValidateTiming(T).Num(), 0);
	bool bAlwaysMoving = true;
	for (int32 i = 0; i < 11; ++i)
	{
		const float t = i / 30.f;
		bAlwaysMoving &= EvaluateAttack(T, t + 1.f / 30.f).ClipTime > EvaluateAttack(T, t).ClipTime + 0.001f;
	}
	TestTrue(TEXT("never frozen: the pose moves in every 1/30 s step of the hold"), bAlwaysMoving);

	// A creep partway through the windup: the raise reaches ClipHoldAt at its own rate, creeps, then carries on.
	FAttackTiming M = T;
	M.ClipHoldAt = 0.10f; M.ClipHoldTo = 0.14f; // moving: 0.37 s of clip in 0.35 s
	const float HoldStart = 0.10f / (0.37f / 0.35f);
	TestEqual(TEXT("mid-windup: the raise reaches the hold pose"), EvaluateAttack(M, HoldStart).ClipTime, 0.10f, 0.002f);
	TestEqual(TEXT("mid-windup: halfway along the creep"), EvaluateAttack(M, HoldStart + 0.19f).ClipTime, 0.12f, 0.002f);
	TestEqual(TEXT("mid-windup: the strike at the windup's end"), EvaluateAttack(M, 0.73f).ClipTime, 0.41f, 0.002f);

	// With a trim, the creep is in clip seconds like every other marker.
	FAttackTiming S = T;
	S.ClipStartAt = 0.02f; S.ClipHoldAt = 0.02f; S.ClipHoldTo = 0.08f;
	TestEqual(TEXT("trimmed: starts at the trim"), EvaluateAttack(S, 0.f).ClipTime, 0.02f, 0.001f);
	TestEqual(TEXT("trimmed: at the creep's end when the hold ends"), EvaluateAttack(S, 0.38f).ClipTime, 0.08f, 0.001f);
	TestEqual(TEXT("trimmed: the strike at the windup's end"), EvaluateAttack(S, 0.73f).ClipTime, 0.41f, 0.001f);

	FAttackTiming Frozen = T;
	Frozen.ClipHoldTo = 0.f;
	TestEqual(TEXT("no creep set: the hold still freezes"), EvaluateAttack(Frozen, 0.19f).ClipTime, 0.f, 0.001f);
	FAttackTiming Past = T;
	Past.ClipHoldTo = 0.45f;
	TestTrue(TEXT("a creep past the strike is an error"), ValidateTiming(Past).Num() > 0);
	FAttackTiming Both = T;
	Both.bEaseIntoHold = true;
	TestTrue(TEXT("a creep that also eases in is an error (they're two different holds)"), ValidateTiming(Both).Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineSpans, "BossTest.Combat.Timeline.Spans", TimelineTestFlags)
bool FTimelineSpans::RunTest(const FString& Parameters)
{
	// Step 5: an attack can run across three clips. Double Execution's second hit: a lead-in (Swing1_Slow 0.57-1.50,
	// his own re-raise), the main clip (Swing2_Slow), then a tail (its return clip, 0-0.97). The timeline works in one
	// extended clip coordinate: the lead-in before the main clip's start, the tail after its end.
	FAttackTiming T;
	T.LeadInFrom = 0.57f; T.LeadInTo = 1.50f;
	T.ClipActiveStart = 0.42f; T.ClipActiveEnd = 0.55f; T.ClipEnd = 1.53f;
	T.TailFrom = 0.f; T.TailTo = 0.97f;
	T.Windup = 0.55f; T.Active = 0.18f; T.Recovery = 1.15f;
	const float WindupRate = 1.35f / 0.55f; // 0.93 s of lead-in plus 0.42 s of the main clip
	FAttackSample S = EvaluateAttack(T, 0.f);
	TestTrue(TEXT("starts in the lead-in"), S.Span == EAttackSpan::LeadIn);
	TestEqual(TEXT("at the lead-in's first frame"), S.SpanTime, 0.57f, 0.001f);
	S = EvaluateAttack(T, 0.2f);
	TestTrue(TEXT("0.2 s: still the lead-in"), S.Span == EAttackSpan::LeadIn);
	TestEqual(TEXT("0.2 s: 0.49 s into it"), S.SpanTime, 0.57f + 0.2f * WindupRate, 0.002f);
	S = EvaluateAttack(T, 0.45f);
	TestTrue(TEXT("0.45 s: the main clip"), S.Span == EAttackSpan::Main);
	TestEqual(TEXT("0.45 s: its time"), S.SpanTime, 0.45f * WindupRate - 0.93f, 0.002f);
	S = EvaluateAttack(T, 0.55f);
	TestTrue(TEXT("the strike on the main clip"), S.Span == EAttackSpan::Main && S.Phase == EAttackPhase::Active);
	TestEqual(TEXT("at its active start"), S.SpanTime, 0.42f, 0.002f);
	const float RecoveryRate = (0.98f + 0.97f) / 1.15f;
	S = EvaluateAttack(T, 0.73f + 0.5f);
	TestTrue(TEXT("recovery: the main clip first"), S.Span == EAttackSpan::Main);
	TestEqual(TEXT("recovery: its time"), S.SpanTime, 0.55f + 0.5f * RecoveryRate, 0.002f);
	S = EvaluateAttack(T, 0.73f + 1.0f);
	TestTrue(TEXT("recovery: then the tail"), S.Span == EAttackSpan::Tail);
	TestEqual(TEXT("recovery: the tail's time"), S.SpanTime, 0.55f + 1.0f * RecoveryRate - 1.53f, 0.002f);
	S = EvaluateAttack(T, 5.f);
	TestTrue(TEXT("done: at the tail's end"), S.Phase == EAttackPhase::Done && S.Span == EAttackSpan::Tail);
	TestEqual(TEXT("done: the tail's last frame"), S.SpanTime, 0.97f, 0.002f);
	TestEqual(TEXT("valid"), ValidateTiming(T).Num(), 0);
	FAttackTiming Plain;
	Plain.ClipHoldAt = 0.43f; Plain.ClipActiveStart = 0.43f; Plain.ClipActiveEnd = 0.57f; Plain.ClipEnd = 1.60f;
	Plain.Windup = 1.05f; Plain.Active = 0.18f; Plain.Recovery = 0.90f;
	S = EvaluateAttack(Plain, 1.2f);
	TestTrue(TEXT("no spans: always the main clip, span time = clip time"), S.Span == EAttackSpan::Main && FMath::IsNearlyEqual(S.SpanTime, S.ClipTime));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineHoldEase, "BossTest.Combat.Timeline.HoldEase", TimelineTestFlags)
bool FTimelineHoldEase::RunTest(const FString& Parameters)
{
	// Delayed Execution: the same raise and eased stop as Executioner's Swing (a 0.40 s ease), then held still for the
	// rest of a longer hold. Until the swing would have resumed, the two are identical: the deception is the timing.
	FAttackTiming Swing;
	Swing.ClipHoldAt = 0.40f; Swing.ClipActiveStart = 0.43f; Swing.ClipActiveEnd = 0.57f; Swing.ClipEnd = 1.60f;
	Swing.Windup = 1.05f; Swing.Hold = 0.40f; Swing.bEaseIntoHold = true; Swing.Active = 0.18f; Swing.Recovery = 0.90f;
	FAttackTiming Delayed = Swing;
	Delayed.Windup = 1.85f; Delayed.Hold = 1.20f; Delayed.HoldEase = 0.40f; Delayed.Recovery = 0.95f;
	const FHoldWindow SwingHold = GetHoldWindow(Swing);
	const FHoldWindow DelayedHold = GetHoldWindow(Delayed);
	TestEqual(TEXT("both holds start together"), DelayedHold.Start, SwingHold.Start, 0.001f);
	TestEqual(TEXT("the swing's hold ends 0.40 s later"), SwingHold.End - SwingHold.Start, 0.40f, 0.001f);
	TestEqual(TEXT("the delayed hold ends 1.20 s later"), DelayedHold.End - DelayedHold.Start, 1.20f, 0.001f);
	bool bSame = true;
	for (float t = 0.f; t <= SwingHold.End; t += 1.f / 60.f)
	{
		bSame &= FMath::IsNearlyEqual(EvaluateAttack(Swing, t).ClipTime, EvaluateAttack(Delayed, t).ClipTime, 0.001f);
	}
	TestTrue(TEXT("identical to Executioner's Swing until its stop"), bSame);
	TestEqual(TEXT("then held still at the loaded pose"), EvaluateAttack(Delayed, SwingHold.End + 0.5f).ClipTime, 0.40f, 0.001f);
	TestTrue(TEXT("still in the hold"), EvaluateAttack(Delayed, SwingHold.End + 0.5f).Phase == EAttackPhase::Hold);
	TestEqual(TEXT("the strike at its windup's end"), EvaluateAttack(Delayed, 1.85f).ClipTime, 0.43f, 0.001f);
	TestEqual(TEXT("valid, no warnings"), ValidateTiming(Delayed).Num(), 0);
	FAttackTiming Varied = Delayed;
	AddHoldTime(Varied, 0.10f);
	TestEqual(TEXT("variation: a longer hold"), Varied.Hold, 1.30f, 0.001f);
	TestEqual(TEXT("variation: the strike moves with it"), Varied.Windup, 1.95f, 0.001f);
	TestEqual(TEXT("variation: the same raise"), EvaluateAttack(Varied, 0.5f).ClipTime, EvaluateAttack(Delayed, 0.5f).ClipTime, 0.001f);
	FAttackTiming Whip;
	Whip.ClipHoldAt = 0.42f; Whip.ClipActiveStart = 0.42f; Whip.ClipActiveEnd = 0.55f; Whip.ClipEnd = 1.53f;
	Whip.Windup = 0.55f; Whip.Active = 0.18f; Whip.Recovery = 1.15f;
	AddHoldTime(Whip, 0.05f);
	TestTrue(TEXT("no hold: the windup stretches instead of freezing somewhere"), Whip.Hold == 0.f && FMath::IsNearlyEqual(Whip.Windup, 0.60f));
	bool bMoving = true;
	for (int32 i = 0; i < 17; ++i)
	{
		bMoving &= EvaluateAttack(Whip, (i + 1) / 30.f).ClipTime > EvaluateAttack(Whip, i / 30.f).ClipTime + 0.001f;
	}
	TestTrue(TEXT("no hold: it moves in every frame of the windup"), bMoving);
	FAttackTiming BadEase = Delayed;
	BadEase.bEaseIntoHold = false;
	TestTrue(TEXT("an ease time without an eased hold is an error"), ValidateTiming(BadEase).Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimelineRecoveryHold, "BossTest.Combat.Timeline.RecoveryHold", TimelineTestFlags)
bool FTimelineRecoveryHold::RunTest(const FString& Parameters)
{
	// Judgment Slam: the hammer held in the floor (creeping 0.78-0.80) for 0.45 s of the recovery, then his own return.
	FAttackTiming T;
	T.ClipHoldAt = 0.64f; T.ClipActiveStart = 0.64f; T.ClipActiveEnd = 0.78f; T.ClipEnd = 2.31f;
	T.Windup = 1.00f; T.Active = 0.25f; T.Recovery = 1.55f;
	T.RecoveryHold = 0.45f; T.ClipRecoveryHoldAt = 0.78f; T.ClipRecoveryHoldTo = 0.80f;
	const float RecStart = 1.25f;
	TestEqual(TEXT("halfway through the held impact"), EvaluateAttack(T, RecStart + 0.225f).ClipTime, 0.79f, 0.001f);
	TestTrue(TEXT("a recovery hold is still recovery (no armour)"), EvaluateAttack(T, RecStart + 0.225f).Phase == EAttackPhase::Recovery);
	TestEqual(TEXT("its end"), EvaluateAttack(T, RecStart + 0.45f).ClipTime, 0.80f, 0.001f);
	TestEqual(TEXT("then the return at its own rate"), EvaluateAttack(T, RecStart + 0.45f + 0.55f).ClipTime, 0.80f + 0.55f * (1.51f / 1.10f), 0.002f);
	TestEqual(TEXT("ends at the clip end"), EvaluateAttack(T, RecStart + 1.549f).ClipTime, 2.31f, 0.003f);
	TestEqual(TEXT("valid"), ValidateTiming(T).Num(), 0);
	FAttackTiming Mid = T;
	Mid.ClipRecoveryHoldAt = 1.20f; Mid.ClipRecoveryHoldTo = 0.f; // still, partway through
	const float Rate = 1.53f / 1.10f;
	const float HoldAt = RecStart + (1.20f - 0.78f) / Rate;
	TestEqual(TEXT("a still recovery hold partway: reaches its pose"), EvaluateAttack(Mid, HoldAt).ClipTime, 1.20f, 0.002f);
	TestEqual(TEXT("and stays there"), EvaluateAttack(Mid, HoldAt + 0.4f).ClipTime, 1.20f, 0.002f);
	FAttackTiming Long = T;
	Long.RecoveryHold = 2.f;
	TestTrue(TEXT("a recovery hold longer than the recovery is an error"), ValidateTiming(Long).Num() > 0);
	FAttackTiming Outside = T;
	Outside.ClipRecoveryHoldAt = 0.70f;
	TestTrue(TEXT("a recovery hold before the active end is an error"), ValidateTiming(Outside).Num() > 0);
	return true;
}

#endif
