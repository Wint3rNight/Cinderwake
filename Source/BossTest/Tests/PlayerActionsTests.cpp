// Step 3, Task 3: the player's action rules (spec section 4).
#include "Misc/AutomationTest.h"
#include "Player/PlayerActions.h"
#include "Player/KnockdownPlan.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ActionsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float ActionsTol = 1e-3f;
	using C = EPlayerCommand;

	// The rules are tested with fixed numbers (the original section 4 set), so retuning the defaults
	// doesn't rewrite every test. DefaultsMatchSpec checks the current defaults.
	FPlayerActions MakeActions()
	{
		FPlayerActions A;
		A.Tuning.StaminaMax = 120.f;
		A.Tuning.StaminaRegenDelay = 0.85f;
		A.Tuning.DodgeCost = 28.f;
		A.Tuning.BackstepCost = 28.f;
		A.Costs.Lights = { 14.f, 14.f, 14.f, 18.f };
		A.Costs.Heavy = 32.f;
		A.Costs.DodgeLights = { 12.f, 16.f };
		A.Costs.DodgeHeavy = 36.f;
		A.Costs.SprintAttack = 18.f;
		A.Reset();
		return A;
	}

	struct FFrameBuilder
	{
		FPlayerFrame F;
		explicit FFrameBuilder(float Dt) { F.Dt = Dt; }
		FFrameBuilder& Move() { F.bMoveHeld = true; return *this; }
		FFrameBuilder& Sprint() { F.bSprintHeld = true; return *this; }
		FFrameBuilder& Running() { F.bAttackRunning = true; return *this; }
		FFrameBuilder& Chain() { F.bChainOpen = true; return *this; }
		FFrameBuilder& HeavyHeld() { F.bHeavyHeld = true; return *this; }
		FFrameBuilder& Guard() { F.bGuardHeld = true; return *this; }
		FFrameBuilder& GuardPressed() { F.bGuardHeld = true; F.bGuardPressed = true; return *this; }
		FFrameBuilder& AtBranch() { F.bAtChargeBranch = true; return *this; }
		FFrameBuilder& PastActive(float ToFree) { F.bPastActive = true; F.TimeToFree = ToFree; return *this; }
		operator FPlayerFrame() const { return F; }
	};
	FFrameBuilder Frame(float Dt) { return FFrameBuilder(Dt); }

	bool Same(const FPlayerCommands& Got, std::initializer_list<EPlayerCommand> Want)
	{
		if (Got.Num() != static_cast<int32>(Want.size()))
		{
			return false;
		}
		int32 i = 0;
		for (EPlayerCommand W : Want)
		{
			if (Got[i++] != W)
			{
				return false;
			}
		}
		return true;
	}

	// Presses and ticks once with no time passing.
	FPlayerCommands PressNow(FPlayerActions& A, EPlayerInput Input, const FPlayerFrame& F = FPlayerFrame())
	{
		A.Press(Input);
		FPlayerFrame Now = F;
		Now.Dt = 0.f;
		return A.Tick(Now);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsLightCostsStamina, "BossTest.Player.Actions.LightCostsStamina", ActionsTestFlags)
bool FActionsLightCostsStamina::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("a light starts"), Same(PressNow(A, EPlayerInput::Light), { C::StartLight }));
	TestEqual(TEXT("Light 1"), A.GetLightIndex(), 0);
	TestEqual(TEXT("14 spent"), A.Stamina.Current, 106.f, ActionsTol);
	TestTrue(TEXT("attacking"), A.GetState() == EPlayerState::Attacking);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsChainOnlyWhenOpen, "BossTest.Player.Actions.ChainOnlyWhenOpen", ActionsTestFlags)
bool FActionsChainOnlyWhenOpen::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Press(EPlayerInput::Light);
	TestTrue(TEXT("no chain before the window opens"), A.Tick(Frame(0.05f).Running()).IsEmpty());
	TestTrue(TEXT("chains once it opens"), Same(A.Tick(Frame(0.05f).Running().Chain()), { C::StartLight }));
	TestEqual(TEXT("Light 2"), A.GetLightIndex(), 1);
	PressNow(A, EPlayerInput::Light, Frame(0.f).Running().Chain());
	PressNow(A, EPlayerInput::Light, Frame(0.f).Running().Chain());
	TestEqual(TEXT("Light 4"), A.GetLightIndex(), 3);
	TestTrue(TEXT("Light 4 chains"), Same(PressNow(A, EPlayerInput::Light, Frame(0.f).Running().Chain()), { C::StartLight }));
	TestEqual(TEXT("back to Light 1"), A.GetLightIndex(), 0);
	TestEqual(TEXT("14 + 14 + 14 + 18 + 14 spent"), A.Stamina.Current, 46.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsFreeOnTheFrameAttackEnds, "BossTest.Player.Actions.FreeOnTheFrameAttackEnds", ActionsTestFlags)
bool FActionsFreeOnTheFrameAttackEnds::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.25f).Running().PastActive(0.10f)); // after the swing (Step 4d: a dodge in the windup is dropped)
	A.Press(EPlayerInput::Dodge);
	TestTrue(TEXT("recovery is the commitment: no dodge yet"), A.Tick(Frame(0.05f).Move().Running().PastActive(0.05f)).IsEmpty());
	TestTrue(TEXT("the dodge comes out on the tick the attack ends"), Same(A.Tick(Frame(0.05f).Move()), { C::StartDodge }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsNoRegenDuringActions, "BossTest.Player.Actions.NoRegenDuringActions", ActionsTestFlags)
bool FActionsNoRegenDuringActions::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(1.0f).Running());
	TestEqual(TEXT("no regeneration while attacking"), A.Stamina.Current, 106.f, ActionsTol);
	A.Tick(Frame(0.01f));
	A.Tick(Frame(0.85f));
	TestEqual(TEXT("none during the 0.85 s delay"), A.Stamina.Current, 106.f, ActionsTol);
	A.Tick(Frame(0.25f));
	TestEqual(TEXT("then 40/s"), A.Stamina.Current, 116.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsExhaustionBlocks, "BossTest.Player.Actions.ExhaustionBlocks", ActionsTestFlags)
bool FActionsExhaustionBlocks::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Stamina.Current = 10.f;
	TestTrue(TEXT("a light can overdraw"), Same(PressNow(A, EPlayerInput::Light), { C::StartLight }));
	TestTrue(TEXT("exhausted at 0"), A.Stamina.bExhausted && A.Stamina.Current == 0.f);
	A.Tick(Frame(0.01f));
	TestTrue(TEXT("no light while exhausted"), PressNow(A, EPlayerInput::Light).IsEmpty());
	TestTrue(TEXT("no dodge"), PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move()).IsEmpty());
	A.Buffer.Clear();
	TestTrue(TEXT("no sprint"), A.Tick(Frame(0.f).Move().Sprint()).IsEmpty());
	TestTrue(TEXT("the heal still works"), Same(PressNow(A, EPlayerInput::Heal), { C::StartHeal }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDodgeTimings, "BossTest.Player.Actions.DodgeTimings", ActionsTestFlags)
bool FActionsDodgeTimings::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("a dodge toward the held direction"), Same(PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move()), { C::StartDodge }));
	TestEqual(TEXT("28 spent"), A.Stamina.Current, 92.f, ActionsTol);
	A.Tick(Frame(0.21f).Move());
	TestTrue(TEXT("invulnerable at 0.21 s"), A.IsInvulnerable());
	A.Tick(Frame(0.02f).Move());
	TestFalse(TEXT("not at 0.23 s"), A.IsInvulnerable());
	A.Tick(Frame(0.07f).Move());
	TestTrue(TEXT("a second dodge at 0.30 s is held"), PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move()).IsEmpty());
	TestTrue(TEXT("and comes out at 0.40 s (the feel pass, 2026-11-25)"), Same(A.Tick(Frame(0.10f).Move()), { C::EndDodge, C::StartDodge }));
	TestEqual(TEXT("another 28"), A.Stamina.Current, 64.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDodgePressSweep, "BossTest.Player.Actions.DodgePressSweep", ActionsTestFlags)
bool FActionsDodgePressSweep::RunTest(const FString& Parameters)
{
	// A dodge pressed during a dodge (game feel and QA, 2026-11-25): before 0.15 s it's dropped silently (no refusal
	// flash: the flash means "can't afford"); from 0.15 s it's held and comes out at max(press, 0.40), never lost.
	for (int32 i = 0; i < 72; ++i)
	{
		const float PressAt = i * 0.01f;
		FPlayerActions A = MakeActions();
		PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
		float T = 0.f;
		while (T < PressAt - 0.001f)
		{
			A.Tick(Frame(0.01f));
			T += 0.01f;
		}
		const FPlayerCommands AtPress = PressNow(A, EPlayerInput::Dodge, Frame(0.f));
		float FiredAt = AtPress.Contains(C::StartDodge) || AtPress.Contains(C::StartBackstep) ? T : -1.f;
		bool bRefused = AtPress.Contains(C::RefuseInput);
		while (FiredAt < 0.f && T < 0.72f)
		{
			const FPlayerCommands Out = A.Tick(Frame(0.01f));
			T += 0.01f;
			bRefused |= Out.Contains(C::RefuseInput);
			if (Out.Contains(C::StartDodge) || Out.Contains(C::StartBackstep))
			{
				FiredAt = T;
			}
		}
		if (PressAt < 0.15f - 0.001f)
		{
			TestTrue(FString::Printf(TEXT("pressed at %.2f s: dropped"), PressAt), FiredAt < 0.f);
		}
		else
		{
			const float Want = FMath::Max(PressAt, 0.40f);
			TestTrue(FString::Printf(TEXT("pressed at %.2f s: out at %.2f (got %.2f)"), PressAt, Want, FiredAt), FMath::Abs(FiredAt - Want) <= 0.011f);
		}
		TestFalse(FString::Printf(TEXT("pressed at %.2f s: never a refusal flash"), PressAt), bRefused);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsLightReplacesHeldDodge, "BossTest.Player.Actions.LightReplacesHeldDodge", ActionsTestFlags)
bool FActionsLightReplacesHeldDodge::RunTest(const FString& Parameters)
{
	// One held slot, latest press wins: a dodge held at 0.30, then a light at 0.35, gives the dodge light at 0.50 (the
	// punish ladder's answers don't move).
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.30f).Move());
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.05f).Move());
	PressNow(A, EPlayerInput::Light, Frame(0.f).Move());
	TestFalse(TEXT("no second dodge at 0.40"), A.Tick(Frame(0.05f).Move()).Contains(C::StartDodge));
	TestTrue(TEXT("the dodge light at 0.50"), A.Tick(Frame(0.10f).Move()).Contains(C::StartDodgeLight));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHeldDodgeExhausted, "BossTest.Player.Actions.HeldDodgeExhaustedRefused", ActionsTestFlags)
bool FActionsHeldDodgeExhausted::RunTest(const FString& Parameters)
{
	// A dodge that overdrew the bar: the held second dodge meets exhaustion at 0.40 and is refused, with the flash.
	FPlayerActions A = MakeActions();
	A.Stamina.Current = 10.f;
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.20f).Move());
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	const FPlayerCommands Out = A.Tick(Frame(0.20f).Move());
	TestTrue(TEXT("refused and shown"), Out.Contains(C::RefuseInput) && !Out.Contains(C::StartDodge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsBackstepWithoutDirection, "BossTest.Player.Actions.BackstepWithoutDirection", ActionsTestFlags)
bool FActionsBackstepWithoutDirection::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("no direction: a backstep"), Same(PressNow(A, EPlayerInput::Dodge), { C::StartBackstep }));
	TestEqual(TEXT("28 spent"), A.Stamina.Current, 92.f, ActionsTol);
	A.Tick(Frame(0.21f));
	TestTrue(TEXT("the same i-frames"), A.IsInvulnerable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsSteerFromReDodge, "BossTest.Player.Actions.SteerFromReDodge", ActionsTestFlags)
bool FActionsSteerFromReDodge::RunTest(const FString& Parameters)
{
	// The feel pass (2026-11-25): from 0.40 s the stick takes over (the push hands its speed to the run) while the state
	// stays Dodging, so attacks still wait for 0.50. With no stick it settles to 0.72 as before.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	TestTrue(TEXT("nothing before 0.40"), A.Tick(Frame(0.39f).Move()).IsEmpty());
	TestFalse(TEXT("no steering yet"), A.CanSteer());
	TestTrue(TEXT("at 0.40 the push hands over"), Same(A.Tick(Frame(0.01f).Move()), { C::ReleaseDodge }));
	TestTrue(TEXT("he steers, still dodging"), A.CanSteer() && A.GetState() == EPlayerState::Dodging);
	TestTrue(TEXT("only once"), A.Tick(Frame(0.05f).Move()).IsEmpty());
	A.Tick(Frame(0.27f).Move());
	TestTrue(TEXT("free at 0.72"), A.GetState() == EPlayerState::Free);

	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Dodge, Frame(0.f).Move());
	TestTrue(TEXT("no stick: no hand-over"), B.Tick(Frame(0.45f)).IsEmpty());
	TestFalse(TEXT("and no steering"), B.CanSteer());
	TestTrue(TEXT("free at 0.72, nothing to remove"), B.Tick(Frame(0.27f)).IsEmpty() && B.GetState() == EPlayerState::Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDodgeEndsOnInputFromLock, "BossTest.Player.Actions.DodgeEndsOnInputFromLock", ActionsTestFlags)
bool FActionsDodgeEndsOnInputFromLock::RunTest(const FString& Parameters)
{
	// From 0.50 s the guard, a sprint or a heal ends the dodge at once; before it they wait.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.45f).Guard());
	TestTrue(TEXT("guard held at 0.45: still dodging"), A.GetState() == EPlayerState::Dodging);
	TestTrue(TEXT("at 0.50 the guard rises"), A.Tick(Frame(0.05f).Guard()).Contains(C::StartGuard) && A.GetState() == EPlayerState::Guard);

	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Dodge, Frame(0.f).Move());
	B.Tick(Frame(0.45f).Move());
	TestTrue(TEXT("at 0.50 a held sprint takes over"), B.Tick(Frame(0.05f).Move().Sprint()).Contains(C::StartSprint) && B.GetState() == EPlayerState::Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDodgeAttackWindow, "BossTest.Player.Actions.DodgeAttackWindow", ActionsTestFlags)
bool FActionsDodgeAttackWindow::RunTest(const FString& Parameters)
{
	{
		FPlayerActions A = MakeActions();
		PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
		A.Tick(Frame(0.40f));
		TestTrue(TEXT("a light pressed at 0.40 s is held"), PressNow(A, EPlayerInput::Light).IsEmpty());
		TestTrue(TEXT("and comes out at 0.50 s as the dodge combo's first hit"), Same(A.Tick(Frame(0.10f)), { C::EndDodge, C::StartDodgeLight }));
		TestEqual(TEXT("its first hit"), A.GetLightIndex(), 0);
		TestEqual(TEXT("28 + 12 spent"), A.Stamina.Current, 80.f, ActionsTol);
	}
	{
		FPlayerActions A = MakeActions();
		PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
		A.Tick(Frame(0.40f));
		PressNow(A, EPlayerInput::Heavy);
		TestTrue(TEXT("a heavy in the window is the leaping slam"), Same(A.Tick(Frame(0.10f)), { C::EndDodge, C::StartDodgeHeavy }));
		TestEqual(TEXT("28 + 36 spent"), A.Stamina.Current, 56.f, ActionsTol);
	}
	{
		FPlayerActions A = MakeActions();
		PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
		A.Tick(Frame(0.72f));
		A.Tick(Frame(0.08f));
		TestTrue(TEXT("free, 0.80 s after the dodge: still a dodge attack"), Same(PressNow(A, EPlayerInput::Light), { C::StartDodgeLight }));
	}
	{
		FPlayerActions A = MakeActions();
		PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
		A.Tick(Frame(0.72f));
		A.Tick(Frame(0.23f));
		TestTrue(TEXT("0.95 s after: Light 1"), Same(PressNow(A, EPlayerInput::Light), { C::StartLight }));
		TestEqual(TEXT("Light 1"), A.GetLightIndex(), 0);
	}
	{
		FPlayerActions A = MakeActions();
		PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
		A.Tick(Frame(0.72f));
		A.Tick(Frame(0.23f));
		TestTrue(TEXT("and a heavy is the normal heavy"), Same(PressNow(A, EPlayerInput::Heavy), { C::StartHeavy }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDodgeLightChains, "BossTest.Player.Actions.DodgeLightChains", ActionsTestFlags)
bool FActionsDodgeLightChains::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.50f));
	PressNow(A, EPlayerInput::Light);
	TestTrue(TEXT("one press: one hit, nothing more on its own"), A.Tick(Frame(0.1f).Running().Chain()).IsEmpty());
	TestTrue(TEXT("pressing again makes it a combo: the dodge finisher, not Light 2"), Same(PressNow(A, EPlayerInput::Light, Frame(0.f).Running().Chain()), { C::StartDodgeLight }));
	TestEqual(TEXT("the finisher"), A.GetLightIndex(), 1);
	// Step 4d: a third press carries on into Light 2 of the normal string (the evade is three hits).
	TestTrue(TEXT("a third press: Light 2"), Same(PressNow(A, EPlayerInput::Light, Frame(0.f).Running().Chain()), { C::StartLight }) && A.GetLightIndex() == 1);
	TestEqual(TEXT("28 + 12 + 16 + 14 spent"), A.Stamina.Current, 50.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHitReactThenHeldDodge, "BossTest.Player.Actions.HitReactThenHeldDodge", ActionsTestFlags)
bool FActionsHitReactThenHeldDodge::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("a hit reaction"), Same(A.OnHit(false), { C::PlayHitReact }));
	A.Tick(Frame(0.05f));
	TestTrue(TEXT("a dodge pressed during it waits"), PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move()).IsEmpty());
	TestTrue(TEXT("past the 0.18 s buffer, still held"), A.Tick(Frame(0.29f).Move()).IsEmpty());
	TestTrue(TEXT("out the moment the reaction ends"), Same(A.Tick(Frame(0.01f).Move()), { C::StartDodge }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHitRestartsReaction, "BossTest.Player.Actions.HitRestartsReaction", ActionsTestFlags)
bool FActionsHitRestartsReaction::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.OnHit(false);
	A.Tick(Frame(0.2f));
	TestTrue(TEXT("a second hit plays another reaction"), Same(A.OnHit(false), { C::PlayHitReact }));
	A.Tick(Frame(0.34f));
	TestTrue(TEXT("restarted: still reacting 0.54 s after the first hit"), A.GetState() == EPlayerState::HitReact);
	A.Tick(Frame(0.01f));
	TestTrue(TEXT("free 0.35 s after the second"), A.GetState() == EPlayerState::Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHitCancelsAttack, "BossTest.Player.Actions.HitCancelsAttack", ActionsTestFlags)
bool FActionsHitCancelsAttack::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	TestTrue(TEXT("the attack stops, then the reaction"), Same(A.OnHit(false), { C::StopAttack, C::PlayHitReact }));
	TestTrue(TEXT("reacting"), A.GetState() == EPlayerState::HitReact);
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Dodge, Frame(0.f).Move());
	B.Tick(Frame(0.3f));
	TestTrue(TEXT("a hit after the i-frames ends the dodge"), Same(B.OnHit(false), { C::EndDodge, C::PlayHitReact }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsKnockdownInvulnerable, "BossTest.Player.Actions.KnockdownInvulnerable", ActionsTestFlags)
bool FActionsKnockdownInvulnerable::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("a knockdown"), Same(A.OnHit(true), { C::PlayKnockdown }));
	A.Tick(Frame(1.39f));
	TestTrue(TEXT("invulnerable at 1.39 s"), A.IsInvulnerable());
	A.Tick(Frame(0.01f));
	TestTrue(TEXT("free at 1.40 s"), A.GetState() == EPlayerState::Free);
	TestFalse(TEXT("and vulnerable"), A.IsInvulnerable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsKnockdownHeldDodge, "BossTest.Player.Actions.KnockdownHeldDodge", ActionsTestFlags)
bool FActionsKnockdownHeldDodge::RunTest(const FString& Parameters)
{
	// The get-up dodge (the player's kit pass, 2026-11-25): a dodge pressed from 0.80 s into the 1.4 s knockdown is held
	// and comes out the moment it ends; earlier ones are dropped (the buffer used to lose a single press at 1.0 s).
	FPlayerActions A = MakeActions();
	A.OnHit(true);
	A.Tick(Frame(0.85f));
	A.Press(EPlayerInput::Dodge);
	TestTrue(TEXT("held through the get-up"), A.Tick(Frame(0.5f).Move()).IsEmpty());
	TestTrue(TEXT("out at 1.40"), A.Tick(Frame(0.05f).Move()).Contains(C::StartDodge));

	FPlayerActions B = MakeActions();
	B.OnHit(true);
	B.Tick(Frame(0.70f));
	B.Press(EPlayerInput::Dodge);
	B.Tick(Frame(0.65f).Move());
	TestFalse(TEXT("pressed at 0.70: dropped"), B.Tick(Frame(0.10f).Move()).Contains(C::StartDodge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHealFlow, "BossTest.Player.Actions.HealFlow", ActionsTestFlags)
bool FActionsHealFlow::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	// The committed heal (the user's call, 2026-11-25): the health lands at 1.30 s, then a tail to 1.70.
	TestTrue(TEXT("the heal starts"), Same(PressNow(A, EPlayerInput::Heal), { C::StartHeal }));
	TestTrue(TEXT("nothing at 1.29 s"), A.Tick(Frame(1.29f)).IsEmpty());
	TestTrue(TEXT("lands at 1.30 s"), Same(A.Tick(Frame(0.01f)), { C::FinishHeal }));
	TestEqual(TEXT("one charge left"), A.GetHealCharges(), 1);
	TestTrue(TEXT("still in its tail"), A.GetState() == EPlayerState::Healing);
	A.Tick(Frame(0.40f));
	TestTrue(TEXT("free at 1.70"), A.GetState() == EPlayerState::Free);
	PressNow(A, EPlayerInput::Heal);
	A.Tick(Frame(1.7f));
	TestEqual(TEXT("none left"), A.GetHealCharges(), 0);
	TestTrue(TEXT("refused with no charges"), PressNow(A, EPlayerInput::Heal).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHealInterruptedKeepsCharge, "BossTest.Player.Actions.HealInterruptedKeepsCharge", ActionsTestFlags)
bool FActionsHealInterruptedKeepsCharge::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heal);
	A.Tick(Frame(1.0f));
	TestTrue(TEXT("a hit interrupts it"), Same(A.OnHit(false), { C::CancelHeal, C::PlayHitReact }));
	TestEqual(TEXT("the charge is kept"), A.GetHealCharges(), 2);
	A.Tick(Frame(0.35f));
	A.Tick(Frame(0.15f));
	TestTrue(TEXT("refused 0.5 s after the interruption"), PressNow(A, EPlayerInput::Heal).IsEmpty());
	A.Tick(Frame(0.5f));
	TestTrue(TEXT("allowed 1.0 s after"), Same(PressNow(A, EPlayerInput::Heal), { C::StartHeal }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHealIsCommitted, "BossTest.Player.Actions.HealIsCommitted", ActionsTestFlags)
bool FActionsHealIsCommitted::RunTest(const FString& Parameters)
{
	// No dodging out before the health lands: a dodge pressed before 1.00 s is refused with the flash; from 1.00 s it's held
	// and comes out at 1.30, after the health (FinishHeal first, then the dodge: one charge used).
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heal);
	A.Tick(Frame(0.90f));
	const FPlayerCommands Early = PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	TestTrue(TEXT("refused at 0.90, shown"), A.Tick(Frame(0.f).Move()).Contains(C::RefuseInput) || Early.Contains(C::RefuseInput));
	TestTrue(TEXT("still healing"), A.GetState() == EPlayerState::Healing);
	A.Tick(Frame(0.20f).Move());
	TestTrue(TEXT("pressed at 1.10: held"), PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move()).IsEmpty());
	A.Tick(Frame(0.19f).Move());
	TestTrue(TEXT("at 1.30: the health, then the dodge"), Same(A.Tick(Frame(0.01f).Move()), { C::FinishHeal, C::StartDodge }));
	TestEqual(TEXT("one charge used"), A.GetHealCharges(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHealTail, "BossTest.Player.Actions.HealTail", ActionsTestFlags)
bool FActionsHealTail::RunTest(const FString& Parameters)
{
	// After the health lands: a dodge or the guard ends the tail at once; attacks wait for 1.70; a hit in the tail is an
	// ordinary hit (the health stays, no "heal interrupted", no retry delay).
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heal);
	A.Tick(Frame(1.40f));
	TestTrue(TEXT("a dodge in the tail comes out"), PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move()).Contains(C::StartDodge));

	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Heal);
	B.Tick(Frame(1.60f));
	TestTrue(TEXT("a light at 1.60 waits"), PressNow(B, EPlayerInput::Light).IsEmpty());
	TestTrue(TEXT("and comes out at 1.70 (buffered)"), B.Tick(Frame(0.10f)).Contains(C::StartLight));

	FPlayerActions H = MakeActions();
	PressNow(H, EPlayerInput::Heal);
	H.Tick(Frame(1.50f));
	TestTrue(TEXT("a hit in the tail is a plain hit reaction"), Same(H.OnHit(false), { C::PlayHitReact }));
	TestEqual(TEXT("the charge was used"), H.GetHealCharges(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsSprintDrainsThenStops, "BossTest.Player.Actions.SprintDrainsThenStops", ActionsTestFlags)
bool FActionsSprintDrainsThenStops::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("sprint starts"), Same(A.Tick(Frame(0.f).Move().Sprint()), { C::StartSprint }));
	A.Tick(Frame(1.0f).Move().Sprint());
	TestEqual(TEXT("12/s"), A.Stamina.Current, 108.f, ActionsTol);
	A.Stamina.Current = 5.f;
	TestTrue(TEXT("an empty bar stops it"), Same(A.Tick(Frame(0.5f).Move().Sprint()), { C::StopSprint }));
	TestTrue(TEXT("and exhausts"), A.Stamina.bExhausted);
	TestFalse(TEXT("not sprinting"), A.IsSprinting());
	TestTrue(TEXT("and it can't restart while exhausted"), A.Tick(Frame(0.1f).Move().Sprint()).IsEmpty());
	FPlayerActions B = MakeActions();
	B.Tick(Frame(0.f).Move().Sprint());
	TestTrue(TEXT("letting go stops it"), Same(B.Tick(Frame(0.1f).Move()), { C::StopSprint }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsActionStopsSprint, "BossTest.Player.Actions.ActionStopsSprint", ActionsTestFlags)
bool FActionsActionStopsSprint::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Move().Sprint());
	TestTrue(TEXT("sprint stops, then the light"), Same(PressNow(A, EPlayerInput::Light, Frame(0.f).Move().Sprint()), { C::StopSprint, C::StartLight }));
	TestFalse(TEXT("not sprinting"), A.IsSprinting());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDeathStopsEverything, "BossTest.Player.Actions.DeathStopsEverything", ActionsTestFlags)
bool FActionsDeathStopsEverything::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	TestTrue(TEXT("the attack stops, death plays"), Same(A.OnDeath(), { C::StopAttack, C::PlayDeath }));
	TestTrue(TEXT("dead"), A.GetState() == EPlayerState::Dead);
	TestTrue(TEXT("no more actions"), PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move()).IsEmpty());
	TestTrue(TEXT("no more reactions"), A.OnHit(false).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsResetRestores, "BossTest.Player.Actions.ResetRestores", ActionsTestFlags)
bool FActionsResetRestores::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heal);
	A.Tick(Frame(2.0f));
	A.Stamina.Current = 5.f;
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Press(EPlayerInput::Light);
	A.Reset();
	TestEqual(TEXT("full stamina"), A.Stamina.Current, 120.f, ActionsTol);
	TestFalse(TEXT("not exhausted"), A.Stamina.bExhausted);
	TestEqual(TEXT("both charges"), A.GetHealCharges(), 2);
	TestTrue(TEXT("free"), A.GetState() == EPlayerState::Free);
	TestTrue(TEXT("empty buffer"), A.Buffer.Peek() == EPlayerInput::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHitStopsSprint, "BossTest.Player.Actions.HitStopsSprint", ActionsTestFlags)
bool FActionsHitStopsSprint::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Move().Sprint());
	TestTrue(TEXT("sprint stops, then the reaction"), Same(A.OnHit(false), { C::StopSprint, C::PlayHitReact }));
	TestFalse(TEXT("not sprinting"), A.IsSprinting());
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Dodge, Frame(0.f).Move());
	B.Tick(Frame(0.3f));
	TestTrue(TEXT("death mid-dodge ends the push"), Same(B.OnDeath(), { C::EndDodge, C::PlayDeath }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHitMidAttackRegenResumes, "BossTest.Player.Actions.HitMidAttackRegenResumes", ActionsTestFlags)
bool FActionsHitMidAttackRegenResumes::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.1f).Running());
	A.OnHit(false);
	A.Tick(Frame(0.85f));
	TestEqual(TEXT("the regen delay runs from the hit"), A.Stamina.Current, 106.f, ActionsTol);
	A.Tick(Frame(0.25f));
	TestEqual(TEXT("then it regenerates: the cancelled attack didn't leave stamina blocked"), A.Stamina.Current, 116.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsNoDodgeAttackAfterHit, "BossTest.Player.Actions.NoDodgeAttackAfterHit", ActionsTestFlags)
bool FActionsNoDodgeAttackAfterHit::RunTest(const FString& Parameters)
{
	// Dodged too early and got hit after the i-frames: the light mashed during the reaction is Light 1,
	// not a dodge attack, even though it comes out inside the 0.50-0.90 s window.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.30f));
	A.OnHit(false);
	A.Tick(Frame(0.25f));
	A.Press(EPlayerInput::Light);
	TestTrue(TEXT("Light 1 when the reaction ends"), Same(A.Tick(Frame(0.10f)), { C::StartLight }));
	TestEqual(TEXT("Light 1"), A.GetLightIndex(), 0);
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Dodge, Frame(0.f).Move());
	B.Tick(Frame(0.30f));
	B.OnHit(false);
	B.Tick(Frame(0.25f));
	B.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("and a heavy is the normal heavy"), Same(B.Tick(Frame(0.10f)), { C::StartHeavy }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDefaultsMatchSpec, "BossTest.Player.Actions.DefaultsMatchSpec", ActionsTestFlags)
bool FActionsDefaultsMatchSpec::RunTest(const FString& Parameters)
{
	// Section 4 after the 2026-11-23 playtest ("5 dodges and it's gone").
	const FPlayerTuning T;
	TestEqual(TEXT("150 stamina"), T.StaminaMax, 150.f);
	TestEqual(TEXT("regenerating at 40/s"), T.StaminaRegenRate, 40.f);
	TestEqual(TEXT("0.6 s after an action chain"), T.StaminaRegenDelay, 0.6f);
	TestEqual(TEXT("a dodge costs 22"), T.DodgeCost, 22.f);
	TestEqual(TEXT("a backstep 18"), T.BackstepCost, 18.f);
	TestEqual(TEXT("the charge costs 12 more stamina (where it's decided is the heavy asset's branch point)"), T.ChargeExtraCost, 12.f);
	TestEqual(TEXT("the sprint attack after 0.25 s of sprint"), T.SprintAttackAfter, 0.25f);
	TestTrue(TEXT("guard: up in 0.18 s, 10/s held, 1.5 per point blocked"), T.GuardRaiseTime == 0.18f && T.GuardDrain == 10.f && T.BlockCostPerDamage == 1.5f);
	TestTrue(TEXT("block-stun 0.32 s, 0.70 s heavy, a 1.0 s break"), T.BlockStun == 0.32f && T.HeavyBlockStun == 0.70f && T.GuardBreakStun == 1.0f);
	TestTrue(TEXT("perfect guard: 0.04-0.18 s after the press, a 0.80 s lockout, 0.6 per point"), T.PerfectGuardFrom == 0.04f && T.PerfectGuardUntil == 0.18f && T.PerfectGuardLockout == 0.80f && T.PerfectGuardCostPerDamage == 0.6f);
	TestTrue(TEXT("deflect-stun 0.15 s, 0.30 s heavy"), T.DeflectStun == 0.15f && T.HeavyDeflectStun == 0.30f);
	FPlayerActions A;
	A.Reset();
	TestEqual(TEXT("Reset applies them"), A.Stamina.Current, 150.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDoubleClickDuringDodge, "BossTest.Player.Actions.DoubleClickDuringDodgeGivesCombo", ActionsTestFlags)
bool FActionsDoubleClickDuringDodge::RunTest(const FString& Parameters)
{
	// Both clicks land during the dodge, before the window opens (at 0.35 and 0.42 s).
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.35f));
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.07f));
	PressNow(A, EPlayerInput::Light);
	TestTrue(TEXT("the backhand comes out at 0.50 s"), Same(A.Tick(Frame(0.08f)), { C::EndDodge, C::StartDodgeLight }));
	TestTrue(TEXT("and the second click becomes the finisher at the chain point, no third press needed"), Same(A.Tick(Frame(0.32f).Running().Chain()), { C::StartDodgeLight }));
	TestEqual(TEXT("the finisher"), A.GetLightIndex(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsEarlySecondClickKept, "BossTest.Player.Actions.EarlySecondClickIsKept", ActionsTestFlags)
bool FActionsEarlySecondClickKept::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.50f));
	PressNow(A, EPlayerInput::Light);
	A.Press(EPlayerInput::Light); // a fast double-click: the second lands in the backhand's windup
	TestTrue(TEXT("held through the windup"), A.Tick(Frame(0.05f).Running()).IsEmpty());
	TestTrue(TEXT("and past the 0.18 s buffer"), A.Tick(Frame(0.25f).Running()).IsEmpty());
	TestTrue(TEXT("it comes out when the chain point opens"), Same(A.Tick(Frame(0.02f).Running().Chain()), { C::StartDodgeLight }));
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Light);
	B.Press(EPlayerInput::Light);
	B.Tick(Frame(0.30f).Running());
	TestTrue(TEXT("the normal string keeps an early click too"), Same(B.Tick(Frame(0.05f).Running().Chain()), { C::StartLight }));
	TestEqual(TEXT("Light 2"), B.GetLightIndex(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsQueuedClickDoesntLeak, "BossTest.Player.Actions.QueuedClickDoesntLeak", ActionsTestFlags)
bool FActionsQueuedClickDoesntLeak::RunTest(const FString& Parameters)
{
	// An extra click during the finisher has no next hit to become: nothing starts when it ends.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.50f));
	PressNow(A, EPlayerInput::Light);
	PressNow(A, EPlayerInput::Light, Frame(0.f).Running().Chain());
	A.Press(EPlayerInput::Light);
	A.Tick(Frame(0.30f).Running().Chain());
	TestTrue(TEXT("no Light 1 after the combo ends"), A.Tick(Frame(0.05f)).IsEmpty());
	// A hit clears a held click too.
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Light);
	B.Press(EPlayerInput::Light);
	B.Tick(Frame(0.05f).Running());
	B.OnHit(false);
	B.Tick(Frame(0.35f));
	TestTrue(TEXT("no attack after the reaction from a click held before the hit"), B.Tick(Frame(0.05f)).IsEmpty());
	return true;
}

// ---- Step 4b: the charged heavy and the sprint attack ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHoldChargesHeavy, "BossTest.Player.Actions.HoldChargesHeavy", ActionsTestFlags)
bool FActionsHoldChargesHeavy::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("RMB starts the heavy"), Same(PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld()), { C::StartHeavy }));
	TestTrue(TEXT("nothing before the cocked pose"), A.Tick(Frame(0.30f).Running().HeavyHeld()).IsEmpty());
	TestTrue(TEXT("still held at the cocked pose: the charge"), Same(A.Tick(Frame(0.12f).Running().HeavyHeld().AtBranch()), { C::BeginCharge }));
	TestEqual(TEXT("32 + 12 spent"), A.Stamina.Current, 76.f, ActionsTol);
	TestTrue(TEXT("decided once"), A.Tick(Frame(0.1f).Running().HeavyHeld().AtBranch()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsReleaseBeforeCock, "BossTest.Player.Actions.ReleaseBeforeCockIsNormalHeavy", ActionsTestFlags)
bool FActionsReleaseBeforeCock::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld());
	A.Tick(Frame(0.30f).Running()); // released
	TestTrue(TEXT("released at the cocked pose: the normal heavy"), A.Tick(Frame(0.12f).Running().AtBranch()).IsEmpty());
	TestTrue(TEXT("pressing again later doesn't charge it"), A.Tick(Frame(0.1f).Running().HeavyHeld().AtBranch()).IsEmpty());
	TestEqual(TEXT("only the heavy's 32"), A.Stamina.Current, 88.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDodgeHeavyNeverCharges, "BossTest.Player.Actions.DodgeHeavyNeverCharges", ActionsTestFlags)
bool FActionsDodgeHeavyNeverCharges::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.50f));
	TestTrue(TEXT("the dodge heavy"), Same(PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld()), { C::EndDodge, C::StartDodgeHeavy }));
	TestTrue(TEXT("never charges"), A.Tick(Frame(0.5f).Running().HeavyHeld().AtBranch()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsSprintAttack, "BossTest.Player.Actions.SprintAttackAfterQuarterSecond", ActionsTestFlags)
bool FActionsSprintAttack::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Move().Sprint());
	A.Tick(Frame(0.24f).Move().Sprint());
	TestTrue(TEXT("0.24 s into a sprint: Light 1"), Same(PressNow(A, EPlayerInput::Light, Frame(0.f).Move().Sprint()), { C::StopSprint, C::StartLight }));
	FPlayerActions B = MakeActions();
	B.Tick(Frame(0.f).Move().Sprint());
	B.Tick(Frame(0.25f).Move().Sprint());
	TestTrue(TEXT("0.25 s: the sprint attack"), Same(PressNow(B, EPlayerInput::Light, Frame(0.f).Move().Sprint()), { C::StopSprint, C::StartSprintAttack }));
	TestEqual(TEXT("3 drained by the sprint, 18 for the attack"), B.Stamina.Current, 99.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsSprintAttackChains, "BossTest.Player.Actions.SprintAttackChainsIntoLight2", ActionsTestFlags)
bool FActionsSprintAttackChains::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Move().Sprint());
	A.Tick(Frame(0.3f).Move().Sprint());
	PressNow(A, EPlayerInput::Light, Frame(0.f).Move().Sprint());
	TestTrue(TEXT("a light at its chain point is Light 2"), Same(PressNow(A, EPlayerInput::Light, Frame(0.f).Running().Chain()), { C::StartLight }));
	TestEqual(TEXT("Light 2"), A.GetLightIndex(), 1);
	return true;
}

// ---- Step 4b: guard ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsGuardRaises, "BossTest.Player.Actions.GuardRaisesAfter018", ActionsTestFlags)
bool FActionsGuardRaises::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("holding the key from free raises the guard"), Same(A.Tick(Frame(0.f).Guard()), { C::StartGuard }));
	A.Tick(Frame(0.17f).Guard());
	TestFalse(TEXT("not blocking at 0.17 s"), A.IsGuardActive());
	A.Tick(Frame(0.01f).Guard());
	TestTrue(TEXT("blocking from 0.18 s"), A.IsGuardActive());
	TestTrue(TEXT("releasing drops it at once"), Same(A.Tick(Frame(0.f)), { C::StopGuard }));
	TestTrue(TEXT("free"), A.GetState() == EPlayerState::Free && !A.IsGuardActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsGuardDrains, "BossTest.Player.Actions.GuardDrainsAndDropsAtZero", ActionsTestFlags)
bool FActionsGuardDrains::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(1.f).Guard());
	TestEqual(TEXT("10/s while held"), A.Stamina.Current, 110.f, ActionsTol);
	A.Stamina.Current = 3.f;
	TestTrue(TEXT("draining to 0 drops the guard, with no stun"), Same(A.Tick(Frame(0.5f).Guard()), { C::StopGuard }));
	TestTrue(TEXT("free and exhausted"), A.GetState() == EPlayerState::Free && A.Stamina.bExhausted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsNoGuardExhausted, "BossTest.Player.Actions.NoGuardWhileExhausted", ActionsTestFlags)
bool FActionsNoGuardExhausted::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Stamina.Current = 0.f;
	A.Stamina.bExhausted = true;
	TestTrue(TEXT("no guard while exhausted"), A.Tick(Frame(0.f).Guard()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsBlockCosts, "BossTest.Player.Actions.BlockCostsStaminaAndStuns", ActionsTestFlags)
bool FActionsBlockCosts::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(0.2f).Guard());
	TestTrue(TEXT("a block"), Same(A.OnBlock(35.f, false), { C::PlayBlock }));
	TestEqual(TEXT("2 drained, 52.5 for the block"), A.Stamina.Current, 65.5f, ActionsTol);
	TestTrue(TEXT("block-stun, guard still up"), A.GetState() == EPlayerState::BlockStun && A.IsGuardActive());
	A.Tick(Frame(0.31f).Guard());
	TestTrue(TEXT("still stunned at 0.31 s"), A.GetState() == EPlayerState::BlockStun);
	A.Tick(Frame(0.01f).Guard());
	TestTrue(TEXT("back to guard at 0.32 s, blocking straight away"), A.GetState() == EPlayerState::Guard && A.IsGuardActive());
	FPlayerActions B = MakeActions();
	B.Tick(Frame(0.f).Guard());
	B.Tick(Frame(0.2f).Guard());
	B.OnBlock(35.f, false);
	TestTrue(TEXT("released during the stun: free when it ends"), Same(B.Tick(Frame(0.32f)), { C::StopGuard }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHeavyBlockStun, "BossTest.Player.Actions.HeavyBlockStunIs070", ActionsTestFlags)
bool FActionsHeavyBlockStun::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(0.2f).Guard());
	TestTrue(TEXT("a knockdown hit, blocked"), Same(A.OnBlock(55.f, true), { C::PlayHeavyBlock }));
	A.Tick(Frame(0.69f).Guard());
	TestTrue(TEXT("stunned at 0.69 s"), A.GetState() == EPlayerState::BlockStun);
	A.Tick(Frame(0.01f).Guard());
	TestTrue(TEXT("guard at 0.70 s"), A.GetState() == EPlayerState::Guard);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsGuardBreak, "BossTest.Player.Actions.GuardBreakOnOverdraw", ActionsTestFlags)
bool FActionsGuardBreak::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(0.2f).Guard());
	A.Stamina.Current = 40.f;
	TestTrue(TEXT("a block that overdraws breaks the guard"), Same(A.OnBlock(35.f, false), { C::PlayGuardBreak }));
	TestTrue(TEXT("broken and exhausted"), A.GetState() == EPlayerState::GuardBroken && A.Stamina.bExhausted && !A.IsGuardActive());
	A.Tick(Frame(0.99f).Guard());
	TestTrue(TEXT("staggered at 0.99 s"), A.GetState() == EPlayerState::GuardBroken);
	TestTrue(TEXT("the break ends at 1.0 s (so its held pose is released)"), Same(A.Tick(Frame(0.01f)), { C::EndGuardBreak }));
	// After the Step 5 play (the user: "the boss is already a bit hard"): stamina regenerates through the break stun,
	// so the break costs the turn but never a free hit. 1.0 s at 40/s is past exhaustion's 25.
	TestTrue(FString::Printf(TEXT("free, no longer exhausted (%.0f stamina)"), A.Stamina.Current), A.GetState() == EPlayerState::Free && !A.Stamina.bExhausted);
	TestTrue(TEXT("a dodge the moment the break ends"), Same(PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move()), { C::StartDodge }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHitDuringBreak, "BossTest.Player.Actions.HitDuringBreakDoesntShortenIt", ActionsTestFlags)
bool FActionsHitDuringBreak::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(0.2f).Guard());
	A.Stamina.Current = 40.f;
	A.OnBlock(35.f, false);
	A.Tick(Frame(0.5f));
	TestTrue(TEXT("a hit during the break plays nothing new"), A.OnHit(false).IsEmpty());
	A.Tick(Frame(0.49f));
	TestTrue(TEXT("still broken: the hit didn't restart or shorten it"), A.GetState() == EPlayerState::GuardBroken);
	A.Tick(Frame(0.01f));
	TestTrue(TEXT("over at 1.0 s"), A.GetState() == EPlayerState::Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDodgeOutOfBlockStun, "BossTest.Player.Actions.DodgeDuringBlockStunComesOutAtEnd", ActionsTestFlags)
bool FActionsDodgeOutOfBlockStun::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(0.2f).Guard());
	A.OnBlock(35.f, false);
	A.Tick(Frame(0.05f).Guard());
	TestTrue(TEXT("a dodge pressed during block-stun waits"), PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move().Guard()).IsEmpty());
	TestTrue(TEXT("and comes out the moment it ends"), Same(A.Tick(Frame(0.27f).Move().Guard()), { C::StopGuard, C::StartDodge }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsAttackFromGuard, "BossTest.Player.Actions.AttackFromGuardDropsIt", ActionsTestFlags)
bool FActionsAttackFromGuard::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(0.3f).Guard());
	TestTrue(TEXT("a light drops the guard and comes out"), Same(PressNow(A, EPlayerInput::Light, Frame(0.f).Guard()), { C::StopGuard, C::StartLight }));
	FPlayerActions B = MakeActions();
	B.Tick(Frame(0.f).Guard());
	B.Tick(Frame(0.3f).Guard());
	TestTrue(TEXT("so does a dodge"), Same(PressNow(B, EPlayerInput::Dodge, Frame(0.f).Move().Guard()), { C::StopGuard, C::StartDodge }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHitDropsGuard, "BossTest.Player.Actions.UnblockedHitDropsGuard", ActionsTestFlags)
bool FActionsHitDropsGuard::RunTest(const FString& Parameters)
{
	// An unblocked hit (from behind, a projectile) on a raised guard, and on a guard in block-stun.
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(0.3f).Guard());
	TestTrue(TEXT("the guard drops into a reaction"), Same(A.OnHit(false), { C::StopGuard, C::PlayHitReact }));
	TestTrue(TEXT("reacting, not guarding"), A.GetState() == EPlayerState::HitReact && !A.IsGuardActive());
	const float Before = A.Stamina.Current;
	A.Tick(Frame(0.35f));
	A.Tick(Frame(0.6f));
	A.Tick(Frame(0.25f));
	TestTrue(TEXT("and the drain stopped: stamina regenerates after the usual delay"), A.Stamina.Current > Before);
	FPlayerActions B = MakeActions();
	B.Tick(Frame(0.f).Guard());
	B.Tick(Frame(0.3f).Guard());
	B.OnBlock(35.f, false);
	TestTrue(TEXT("in block-stun too"), Same(B.OnHit(false), { C::StopGuard, C::PlayHitReact }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsSecondBlockKeepsStun, "BossTest.Player.Actions.SecondBlockDoesntShortenStun", ActionsTestFlags)
bool FActionsSecondBlockKeepsStun::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).Guard());
	A.Tick(Frame(0.2f).Guard());
	A.OnBlock(55.f, true);   // 0.70 s
	A.Tick(Frame(0.1f).Guard());
	A.OnBlock(10.f, false);  // a 0.32 s stun mustn't cut the 0.60 s left
	A.Tick(Frame(0.59f).Guard());
	TestTrue(TEXT("still stunned 0.69 s after the heavy block"), A.GetState() == EPlayerState::BlockStun);
	A.Tick(Frame(0.01f).Guard());
	TestTrue(TEXT("over at 0.70 s"), A.GetState() == EPlayerState::Guard);
	return true;
}

// ---- Step 4c: the perfect guard (window, lockout, deflect) ----

namespace
{
	bool Has(const FPlayerCommands& Got, EPlayerCommand Want)
	{
		return Got.Contains(Want);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsFreshPressWindow, "BossTest.Player.Actions.FreshPressOpensWindow", ActionsTestFlags)
bool FActionsFreshPressWindow::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("a press from free raises the guard"), Same(A.Tick(Frame(0.f).GuardPressed()), { C::StartGuard }));
	TestTrue(TEXT("and opens a window"), A.HasPerfectWindow());
	A.Tick(Frame(0.18f).Guard());
	TestTrue(TEXT("still open at 0.18 s"), A.HasPerfectWindow());
	A.Tick(Frame(0.01f).Guard());
	TestFalse(TEXT("closed after 0.18 s"), A.HasPerfectWindow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHeldGuardNoWindow, "BossTest.Player.Actions.HeldGuardOpensNoWindow", ActionsTestFlags)
bool FActionsHeldGuardNoWindow::RunTest(const FString& Parameters)
{
	// Held through an attack: the guard rises when it ends, with no window.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.3f).Running().Guard());
	A.Tick(Frame(0.4f).Guard()); // the attack ended
	TestTrue(TEXT("the guard rose"), A.GetState() == EPlayerState::Guard);
	TestFalse(TEXT("but opened no window"), A.HasPerfectWindow());
	// Held through a block-stun: back to guard, with no window.
	FPlayerActions B = MakeActions();
	B.Tick(Frame(0.f).GuardPressed());
	B.Tick(Frame(0.2f).Guard());
	B.OnBlock(35.f, false);
	B.Tick(Frame(0.32f).Guard());
	TestTrue(TEXT("back to guard after the block-stun"), B.GetState() == EPlayerState::Guard);
	TestFalse(TEXT("with no window"), B.HasPerfectWindow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsWhiffLocksOut, "BossTest.Player.Actions.WhiffLocksOut", ActionsTestFlags)
bool FActionsWhiffLocksOut::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.19f).Guard()); // the window ended at 0.18 s with no hit: locked out until 0.98 s
	A.Tick(Frame(0.f));            // released
	A.Tick(Frame(0.78f));          // 0.97 s
	A.Tick(Frame(0.f).GuardPressed());
	TestTrue(TEXT("0.79 s after the window's end: a normal guard"), A.GetState() == EPlayerState::Guard && !A.HasPerfectWindow());
	A.Tick(Frame(0.f));            // released (no window, so not a whiff)
	A.Tick(Frame(0.02f));          // 0.99 s
	A.Tick(Frame(0.f).GuardPressed());
	TestTrue(TEXT("0.81 s after: a window again"), A.HasPerfectWindow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsEarlyReleaseWhiff, "BossTest.Player.Actions.EarlyReleaseIsAWhiff", ActionsTestFlags)
bool FActionsEarlyReleaseWhiff::RunTest(const FString& Parameters)
{
	// Tapping can't dodge the lockout: releasing at 0.05 s counts from where the window would have ended (0.18 s).
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.05f).Guard());
	A.Tick(Frame(0.f));            // released at 0.05 s: locked out until 0.98 s
	A.Tick(Frame(0.92f));          // 0.97 s
	A.Tick(Frame(0.f).GuardPressed());
	TestFalse(TEXT("no window at 0.97 s"), A.HasPerfectWindow());
	A.Tick(Frame(0.f));
	A.Tick(Frame(0.02f));          // 0.99 s
	A.Tick(Frame(0.f).GuardPressed());
	TestTrue(TEXT("a window at 0.99 s"), A.HasPerfectWindow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsAnyHitClearsLockout, "BossTest.Player.Actions.AnyHitClearsLockout", ActionsTestFlags)
bool FActionsAnyHitClearsLockout::RunTest(const FString& Parameters)
{
	// An early press on hit one becomes a block; that block clears the lockout, so hit two can be perfected.
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.2f).Guard());   // whiffed
	A.OnBlock(35.f, false);
	A.Tick(Frame(0.32f));          // released during the stun: free when it ends
	A.Tick(Frame(0.f).GuardPressed());
	TestTrue(TEXT("a block cleared it"), A.HasPerfectWindow());
	// A hit clears it too.
	FPlayerActions B = MakeActions();
	B.Tick(Frame(0.f).GuardPressed());
	B.Tick(Frame(0.05f).Guard());
	B.Tick(Frame(0.f));            // whiffed by releasing
	B.OnHit(false);
	B.Tick(Frame(0.35f));          // the reaction ends
	B.Tick(Frame(0.f).GuardPressed());
	TestTrue(TEXT("a hit cleared it"), B.HasPerfectWindow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsPerfectGuardCosts, "BossTest.Player.Actions.PerfectGuardCostsAndStuns", ActionsTestFlags)
bool FActionsPerfectGuardCosts::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.10f).Guard());
	const float Before = A.Stamina.Current;
	TestTrue(TEXT("a deflect"), Same(A.OnPerfectGuard(35.f, false), { C::PlayDeflect }));
	TestEqual(TEXT("0.6 x 35 stamina"), Before - A.Stamina.Current, 21.f, ActionsTol);
	TestTrue(TEXT("deflect-stun with the guard up"), A.GetState() == EPlayerState::BlockStun && A.IsGuardActive());
	TestFalse(TEXT("the window is used"), A.HasPerfectWindow());
	A.Tick(Frame(0.14f).Guard());
	TestTrue(TEXT("still in the stun at 0.14 s"), A.GetState() == EPlayerState::BlockStun);
	A.Tick(Frame(0.01f).Guard());
	TestTrue(TEXT("back to guard at 0.15 s"), A.GetState() == EPlayerState::Guard && A.IsGuardActive());
	FPlayerActions B = MakeActions();
	B.Tick(Frame(0.f).GuardPressed());
	B.Tick(Frame(0.10f).Guard());
	TestTrue(TEXT("a would-be knockdown: the heavy deflect"), Same(B.OnPerfectGuard(55.f, true), { C::PlayHeavyDeflect }));
	B.Tick(Frame(0.29f).Guard());
	TestTrue(TEXT("still in the stun at 0.29 s"), B.GetState() == EPlayerState::BlockStun);
	B.Tick(Frame(0.01f).Guard());
	TestTrue(TEXT("over at 0.30 s"), B.GetState() == EPlayerState::Guard);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsPerfectGuardNeverBreaks, "BossTest.Player.Actions.PerfectGuardNeverBreaks", ActionsTestFlags)
bool FActionsPerfectGuardNeverBreaks::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tuning.StaminaRegenDelay = 0.6f;
	A.Reset();
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.10f).Guard());
	A.Stamina.Current = 10.f;
	TestTrue(TEXT("an overdraw still deflects, with no break"), Same(A.OnPerfectGuard(55.f, false), { C::PlayDeflect }));
	TestTrue(TEXT("exhausted, in the deflect-stun"), A.Stamina.bExhausted && A.GetState() == EPlayerState::BlockStun);
	TestTrue(TEXT("exhaustion drops the guard when the stun ends"), Same(A.Tick(Frame(0.15f).Guard()), { C::StopGuard }));
	A.Tick(Frame(0.6f));
	TestEqual(TEXT("regeneration starts 0.6 s after the stun"), A.Stamina.Current, 0.f, ActionsTol);
	A.Tick(Frame(0.62f));
	TestTrue(TEXT("still exhausted at 1.37 s"), A.Stamina.bExhausted);
	A.Tick(Frame(0.02f));
	TestTrue(TEXT("a dodge is ready by 1.4 s"), A.Stamina.CanStartAction());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsAttackThroughDeflect, "BossTest.Player.Actions.AttackBuffersThroughDeflect", ActionsTestFlags)
bool FActionsAttackThroughDeflect::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.10f).Guard());
	A.OnPerfectGuard(35.f, false);
	A.Tick(Frame(0.05f).Guard());
	A.Press(EPlayerInput::Light);
	const FPlayerCommands Out = A.Tick(Frame(0.10f).Guard());
	TestTrue(TEXT("the light comes out the frame the stun ends, guard held or not"), Has(Out, C::StartLight) && A.GetState() == EPlayerState::Attacking);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsResetClearsWindow, "BossTest.Player.Actions.ResetClearsWindow", ActionsTestFlags)
bool FActionsResetClearsWindow::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).GuardPressed());
	A.Reset();
	TestFalse(TEXT("no window after a reset"), A.HasPerfectWindow());
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.2f).Guard());   // whiffed: locked out
	A.Reset();
	A.Tick(Frame(0.f).GuardPressed());
	TestTrue(TEXT("no lockout after a reset"), A.HasPerfectWindow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsBlockBeforeOwnTick, "BossTest.Player.Actions.BlockAcceptedBeforeRaiseTick", ActionsTestFlags)
bool FActionsBlockBeforeOwnTick::RunTest(const FString& Parameters)
{
	// The resolver measures the raise from the press, so a block can arrive before this frame's tick has passed 0.18 s.
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.17f).Guard());
	const float Before = A.Stamina.Current;
	TestTrue(TEXT("the block is played"), Same(A.OnBlock(35.f, false), { C::PlayBlock }));
	TestEqual(TEXT("and costs its stamina"), Before - A.Stamina.Current, 52.5f, ActionsTol);
	TestTrue(TEXT("into block-stun"), A.GetState() == EPlayerState::BlockStun);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsAttackThroughHeavyDeflect, "BossTest.Player.Actions.EarlyAttackSurvivesHeavyDeflect", ActionsTestFlags)
bool FActionsAttackThroughHeavyDeflect::RunTest(const FString& Parameters)
{
	// The heavy deflect-stun (0.30 s) outlasts the 0.18 s buffer: an early press is held like a dodge is.
	FPlayerActions A = MakeActions();
	A.Tick(Frame(0.f).GuardPressed());
	A.Tick(Frame(0.10f).Guard());
	A.OnPerfectGuard(55.f, true);
	A.Tick(Frame(0.05f).Guard());
	A.Press(EPlayerInput::Light);
	A.Tick(Frame(0.01f).Guard());  // the next frame reads the press
	A.Tick(Frame(0.23f).Guard());  // long past the 0.18 s buffer
	TestTrue(TEXT("still in the stun at 0.29 s"), A.GetState() == EPlayerState::BlockStun);
	const FPlayerCommands Out = A.Tick(Frame(0.01f).Guard());
	TestTrue(TEXT("the light comes out at 0.30 s"), Has(Out, C::StartLight) && A.GetState() == EPlayerState::Attacking);
	return true;
}

// ---- Step 4d: Elden Ring flow (one chain rule, held presses) ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsLightLightHeavy, "BossTest.Player.Actions.HeldHeavyChainsFromLight", ActionsTestFlags)
bool FActionsLightLightHeavy::RunTest(const FString& Parameters)
{
	// R1 R1 R2: Light 1, Light 2 from its chain point, then the heavy from Light 2's.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.10f).Running());
	A.Press(EPlayerInput::Light);
	A.Tick(Frame(0.27f).Running());
	TestTrue(TEXT("held until Light 1's chain point"), A.GetLightIndex() == 0);
	TestTrue(TEXT("Light 2 at the chain point"), Same(A.Tick(Frame(0.01f).Running().Chain()), { C::StartLight }) && A.GetLightIndex() == 1);
	A.Tick(Frame(0.10f).Running());
	A.Press(EPlayerInput::Heavy);
	const float Before = A.Stamina.Current;
	TestTrue(TEXT("the heavy comes out of Light 2's chain point"), Has(A.Tick(Frame(0.18f).Running().Chain()), C::ChainHeavy));
	TestEqual(TEXT("at the heavy's own cost"), Before - A.Stamina.Current, 32.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHeavyIntoLight1, "BossTest.Player.Actions.HeavyChainsIntoLight1", ActionsTestFlags)
bool FActionsHeavyIntoLight1::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld());
	A.Tick(Frame(0.50f).Running());
	A.Press(EPlayerInput::Light);
	A.Tick(Frame(0.40f).Running());
	TestTrue(TEXT("held until the heavy's chain point"), A.GetState() == EPlayerState::Attacking);
	const FPlayerCommands Out = A.Tick(Frame(0.05f).Running().Chain());
	TestTrue(TEXT("Light 1 out of the heavy"), Same(Out, { C::StartLight }) && A.GetLightIndex() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsFinisherIntoLight2, "BossTest.Player.Actions.FinisherChainsIntoLight2", ActionsTestFlags)
bool FActionsFinisherIntoLight2::RunTest(const FString& Parameters)
{
	// The evade as three hits: the dodge light, the finisher, then Light 2 of the normal string.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.30f));
	A.Press(EPlayerInput::Light);
	TestTrue(TEXT("the dodge light at the lock"), Has(A.Tick(Frame(0.20f)), C::StartDodgeLight));
	A.Press(EPlayerInput::Light);
	TestTrue(TEXT("the finisher at its chain point"), Same(A.Tick(Frame(0.32f).Running().Chain()), { C::StartDodgeLight }) && A.GetLightIndex() == 1);
	A.Press(EPlayerInput::Light);
	TestTrue(TEXT("Light 2 out of the finisher"), Same(A.Tick(Frame(0.55f).Running().Chain()), { C::StartLight }) && A.GetLightIndex() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsSlamIntoRise, "BossTest.Player.Actions.SlamChainsIntoRise", ActionsTestFlags)
bool FActionsSlamIntoRise::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.30f));
	A.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("the slam at the lock"), Has(A.Tick(Frame(0.20f).HeavyHeld()), C::StartDodgeHeavy));
	A.Press(EPlayerInput::Light);
	TestTrue(TEXT("out of the landing crouch, the stand-up Light 1"), Same(A.Tick(Frame(1.05f).Running().Chain()), { C::StartLightRise }) && A.GetLightIndex() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHeldLatestWins, "BossTest.Player.Actions.HeldPressLatestWins", ActionsTestFlags)
bool FActionsHeldLatestWins::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Press(EPlayerInput::Light);
	A.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("light then heavy: the heavy"), Has(A.Tick(Frame(0.38f).Running().Chain()), C::ChainHeavy));
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Light);
	B.Press(EPlayerInput::Heavy);
	B.Press(EPlayerInput::Light);
	TestTrue(TEXT("heavy then light: the light"), Same(B.Tick(Frame(0.38f).Running().Chain()), { C::StartLight }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsEarlyDodgeDropped, "BossTest.Player.Actions.EarlyDodgeDroppedAndClears", ActionsTestFlags)
bool FActionsEarlyDodgeDropped::RunTest(const FString& Parameters)
{
	// A panic dodge while your own blade is moving: ignored, the refusal shows, and it takes back the queued slash.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.10f).Running());
	A.Press(EPlayerInput::Light);
	A.Press(EPlayerInput::Dodge);
	TestTrue(TEXT("the refusal is shown"), Has(A.Tick(Frame(0.01f).Running()), C::RefuseInput));
	TestTrue(TEXT("no light at the chain point"), A.Tick(Frame(0.27f).Running().Chain()).IsEmpty());
	TestTrue(TEXT("and no dodge when he's free"), A.Tick(Frame(0.32f)).IsEmpty() && A.GetState() == EPlayerState::Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsLateDodgeHeld, "BossTest.Player.Actions.LateDodgeHeldToFree", ActionsTestFlags)
bool FActionsLateDodgeHeld::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.26f).Running().PastActive(0.44f));
	A.Press(EPlayerInput::Dodge);
	A.Tick(Frame(0.20f).Running().Chain().PastActive(0.24f));
	TestTrue(TEXT("0.44 s before free, after the swing: held, out the moment he's free"), Has(A.Tick(Frame(0.24f).Move()), C::StartDodge));
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Light);
	B.Tick(Frame(0.24f).Running().PastActive(0.46f));
	B.Press(EPlayerInput::Dodge);
	TestTrue(TEXT("0.46 s before free: dropped"), Has(B.Tick(Frame(0.01f).Running().PastActive(0.45f)), C::RefuseInput));
	TestFalse(TEXT("so no dodge at free"), Has(B.Tick(Frame(0.45f).Move()), C::StartDodge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsGuardClearsHeld, "BossTest.Player.Actions.GuardPressClearsHeld", ActionsTestFlags)
bool FActionsGuardClearsHeld::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Press(EPlayerInput::Light);
	A.Tick(Frame(0.10f).Running().GuardPressed());
	TestFalse(TEXT("a fresh guard press takes back the queued light"), Has(A.Tick(Frame(0.28f).Running().Chain()), C::StartLight));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsExhaustedHeldDropped, "BossTest.Player.Actions.ExhaustedHeldDropped", ActionsTestFlags)
bool FActionsExhaustedHeldDropped::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Stamina.Spend(1000.f); // exhausted
	A.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("nothing at the chain point, and the refusal shows"), Same(A.Tick(Frame(0.38f).Running().Chain()), { C::RefuseInput }));
	A.Tick(Frame(0.32f)); // free
	A.Tick(Frame(3.0f));  // long after exhaustion ends
	TestTrue(TEXT("and nothing later"), A.GetState() == EPlayerState::Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsRePressNoCharge, "BossTest.Player.Actions.RePressDoesntCharge", ActionsTestFlags)
bool FActionsRePressNoCharge::RunTest(const FString& Parameters)
{
	// Released and pressed again before the branch: that's queuing a second heavy, not charging this one.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld());
	A.Tick(Frame(0.10f).Running());
	A.Press(EPlayerInput::Heavy);
	A.Tick(Frame(0.20f).Running().HeavyHeld());
	TestFalse(TEXT("no charge"), Has(A.Tick(Frame(0.10f).Running().HeavyHeld().AtBranch()), C::BeginCharge));
	// A chained heavy whose button stays down from the press does charge.
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Light);
	B.Press(EPlayerInput::Heavy);
	B.Tick(Frame(0.38f).Running().Chain().HeavyHeld());
	B.Tick(Frame(0.30f).Running().HeavyHeld());
	const float Before = B.Stamina.Current;
	TestTrue(TEXT("held through the chained heavy: it charges"), Has(B.Tick(Frame(0.10f).Running().HeavyHeld().AtBranch()), C::BeginCharge));
	TestEqual(TEXT("for 12 more"), Before - B.Stamina.Current, 12.f, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsChainedHeavyNotSlam, "BossTest.Player.Actions.ChainedHeavyIsNotSlam", ActionsTestFlags)
bool FActionsChainedHeavyNotSlam::RunTest(const FString& Parameters)
{
	// Dodge light into heavy starts 0.82 s after the dodge, inside the dodge-attack window: still the normal heavy.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.30f));
	A.Press(EPlayerInput::Light);
	A.Tick(Frame(0.20f));
	A.Press(EPlayerInput::Heavy);
	const FPlayerCommands Out = A.Tick(Frame(0.32f).Running().Chain().HeavyHeld());
	TestTrue(TEXT("the heavy, not the slam"), Has(Out, C::ChainHeavy) && !Has(Out, C::StartDodgeHeavy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsDodgeHeavyPrecedence, "BossTest.Player.Actions.DodgeThenHeavyPrecedence", ActionsTestFlags)
bool FActionsDodgeHeavyPrecedence::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.10f));
	A.Press(EPlayerInput::Heavy);
	A.Tick(Frame(0.30f));
	A.Press(EPlayerInput::Dodge);
	TestTrue(TEXT("heavy then dodge: the dodge, the later press"), Has(A.Tick(Frame(0.10f).Move()), C::StartDodge));
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Dodge, Frame(0.f).Move());
	B.Tick(Frame(0.20f));
	B.Press(EPlayerInput::Dodge);
	B.Tick(Frame(0.10f));
	B.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("dodge then heavy: the slam"), Has(B.Tick(Frame(0.20f).HeavyHeld()), C::StartDodgeHeavy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHitClearsHeld, "BossTest.Player.Actions.HitClearsHeld", ActionsTestFlags)
bool FActionsHitClearsHeld::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Press(EPlayerInput::Light);
	A.OnHit(false);
	TestTrue(TEXT("a held attack never comes out after the reaction"), A.Tick(Frame(0.35f)).IsEmpty() && A.GetState() == EPlayerState::Free);
	// A held dodge younger than 0.18 s survives the hit; an older one doesn't.
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Light);
	B.Tick(Frame(0.30f).Running().PastActive(0.40f));
	B.Press(EPlayerInput::Dodge);
	B.Tick(Frame(0.10f).Running().PastActive(0.30f));
	B.OnHit(false);
	TestTrue(TEXT("a 0.10 s old dodge comes out when the reaction ends"), Has(B.Tick(Frame(0.35f).Move()), C::StartDodge));
	FPlayerActions D = MakeActions();
	PressNow(D, EPlayerInput::Light);
	D.Tick(Frame(0.30f).Running().PastActive(0.40f));
	D.Press(EPlayerInput::Dodge);
	D.Tick(Frame(0.30f).Running().PastActive(0.10f));
	D.OnHit(false);
	TestFalse(TEXT("a 0.30 s old one is cleared"), Has(D.Tick(Frame(0.35f).Move()), C::StartDodge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsPressOnFreeFrame, "BossTest.Player.Actions.PressOnFreeFrameStillChains", ActionsTestFlags)
bool FActionsPressOnFreeFrame::RunTest(const FString& Parameters)
{
	// The attack ends before the character reads input, so a press can arrive while the model still says Attacking
	// on the frame he's free. It must still come out, in the attack's line.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.69f).Running());
	A.Press(EPlayerInput::Light);
	const FPlayerCommands Out = A.Tick(Frame(0.01f));
	TestTrue(TEXT("Light 2, on the free frame"), Has(Out, C::StartLight) && A.GetLightIndex() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsRePressSameFrame, "BossTest.Player.Actions.RePressInOneFrameDoesntCharge", ActionsTestFlags)
bool FActionsRePressSameFrame::RunTest(const FString& Parameters)
{
	// Released and pressed again between two ticks (a quick tap at 30 fps): the tick never sees RMB up, but a new
	// press is still a queue, not a charge.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld());
	A.Press(EPlayerInput::Heavy);
	TestFalse(TEXT("no charge"), Has(A.Tick(Frame(0.40f).Running().HeavyHeld().AtBranch()), C::BeginCharge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsGuardDuringDodge, "BossTest.Player.Actions.GuardDuringDodgeClearsHeld", ActionsTestFlags)
bool FActionsGuardDuringDodge::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.20f));
	A.Press(EPlayerInput::Light);
	A.Tick(Frame(0.10f).GuardPressed());
	TestFalse(TEXT("a guard press during the dodge takes back the held light"), Has(A.Tick(Frame(0.20f)), C::StartDodgeLight));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsExhaustedAtLock, "BossTest.Player.Actions.ExhaustedAtLockRefused", ActionsTestFlags)
bool FActionsExhaustedAtLock::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Dodge, Frame(0.f).Move());
	A.Tick(Frame(0.20f));
	A.Press(EPlayerInput::Heavy);
	A.Stamina.Spend(1000.f); // exhausted
	const FPlayerCommands Out = A.Tick(Frame(0.30f).HeavyHeld());
	TestTrue(TEXT("no slam, and the refusal shows"), Has(Out, C::RefuseInput) && !Has(Out, C::StartDodgeHeavy));
	return true;
}

// ---- Step 4e: the second heavy ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHeavyLine, "BossTest.Player.Actions.ChainedHeavyFollowsLine", ActionsTestFlags)
bool FActionsHeavyLine::RunTest(const FString& Parameters)
{
	// Heavy 1, heavy 2, heavy 1: a heavy never follows itself.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld());
	A.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("heavy 1 into heavy 2"), Has(A.Tick(Frame(0.95f).Running().Chain()), C::ChainHeavyTwo));
	A.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("heavy 2 into heavy 1"), Has(A.Tick(Frame(0.95f).Running().Chain()), C::ChainHeavy));
	// Out of a light: heavy 1.
	FPlayerActions B = MakeActions();
	PressNow(B, EPlayerInput::Light);
	B.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("a light into heavy 1"), Has(B.Tick(Frame(0.38f).Running().Chain()), C::ChainHeavy));
	// Out of the slam: heavy 2.
	FPlayerActions D = MakeActions();
	PressNow(D, EPlayerInput::Dodge, Frame(0.f).Move());
	D.Tick(Frame(0.30f));
	D.Press(EPlayerInput::Heavy);
	D.Tick(Frame(0.20f).HeavyHeld());
	D.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("the slam into heavy 2"), Has(D.Tick(Frame(1.05f).Running().Chain()), C::ChainHeavyTwo));
	// Out of the charged heavy: heavy 2.
	FPlayerActions E = MakeActions();
	PressNow(E, EPlayerInput::Heavy, Frame(0.f).HeavyHeld());
	E.Tick(Frame(0.15f).Running().HeavyHeld().AtBranch()); // charges
	E.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("the charged heavy into heavy 2"), Has(E.Tick(Frame(1.31f).Running().Chain()), C::ChainHeavyTwo));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsHeavy2NoCharge, "BossTest.Player.Actions.Heavy2NeverCharges", ActionsTestFlags)
bool FActionsHeavy2NoCharge::RunTest(const FString& Parameters)
{
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld());
	A.Tick(Frame(0.10f).Running()); // released: heavy 1 won't charge either
	A.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("heavy 2 started"), Has(A.Tick(Frame(0.85f).Running().Chain().HeavyHeld()), C::ChainHeavyTwo)); // RMB down from its press
	const float Before = A.Stamina.Current;
	TestFalse(TEXT("held through heavy 2: no charge"), Has(A.Tick(Frame(0.15f).Running().HeavyHeld().AtBranch()), C::BeginCharge));
	TestEqual(TEXT("and no extra stamina"), A.Stamina.Current, Before, ActionsTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsEndFrameHeavy2, "BossTest.Player.Actions.EndFrameHeavyIsHeavy2", ActionsTestFlags)
bool FActionsEndFrameHeavy2::RunTest(const FString& Parameters)
{
	// A heavy pressed on the frame heavy 1 ends is still heavy 2 (the line is the rules', not the attack component's).
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Heavy, Frame(0.f).HeavyHeld());
	A.Tick(Frame(1.46f).Running());
	A.Press(EPlayerInput::Heavy);
	TestTrue(TEXT("heavy 2 on the free frame"), Has(A.Tick(Frame(0.01f)), C::ChainHeavyTwo));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsKnockdownPlan, "BossTest.Player.Actions.KnockdownPlan", ActionsTestFlags)
bool FActionsKnockdownPlan::RunTest(const FString& Parameters)
{
	// After the Step 5 play: he floated when knocked down (the knock clips are airborne). The stun fall from its drop
	// (0.35-0.97 s) and the stun get-up (1.07 s) share one rate so he's standing when the 1.4 s knockdown ends.
	const FKnockdownPlan P = PlanKnockdown(0.35f, 0.97f, 1.07f, 1.4f);
	TestEqual(TEXT("one rate: 1.69 s of clips in 1.4 s"), P.Rate, 1.69f / 1.4f, 0.001f);
	TestEqual(TEXT("the get-up starts when the fall reaches the floor"), P.GetUpAt, 0.62f / P.Rate, 0.001f);
	TestEqual(TEXT("and ends with the knockdown"), P.GetUpAt + 1.07f / P.Rate, 1.4f, 0.001f);
	const FKnockdownPlan None = PlanKnockdown(0.f, 0.f, 0.f, 1.4f);
	TestTrue(TEXT("no clips: a rate of 1, nothing scheduled"), None.Rate == 1.f && None.GetUpAt == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsRiposteFromFree, "BossTest.Player.Actions.RiposteFromFree", ActionsTestFlags)
bool FActionsRiposteFromFree::RunTest(const FString& Parameters)
{
	// The finishing blow (the council's run, 2026-11-24): a light or heavy while his window is open, no stamina spent,
	// invulnerable until free, then free as usual.
	for (const EPlayerInput Input : { EPlayerInput::Light, EPlayerInput::Heavy })
	{
		FPlayerActions A = MakeActions();
		FPlayerFrame Ready;
		Ready.bRiposteReady = true;
		TestTrue(TEXT("could riposte when free"), A.CouldRiposte());
		TestTrue(TEXT("the press is the finishing blow"), Same(PressNow(A, Input, Ready), { C::StartRiposte }));
		TestEqual(TEXT("no stamina spent"), A.Stamina.Current, A.Tuning.StaminaMax, ActionsTol);
		TestTrue(TEXT("invulnerable while it runs"), A.IsRiposting() && A.IsInvulnerable());
		TestFalse(TEXT("no second one from it"), A.CouldRiposte());
		A.Tick(Frame(0.5f).Running());
		TestTrue(TEXT("still invulnerable before free"), A.IsInvulnerable());
		A.Tick(Frame(0.f));
		TestTrue(TEXT("free, and vulnerable again"), A.GetState() == EPlayerState::Free && !A.IsInvulnerable() && !A.IsRiposting());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsRiposteNeedsWindow, "BossTest.Player.Actions.RiposteNeedsWindowAndStamina", ActionsTestFlags)
bool FActionsRiposteNeedsWindow::RunTest(const FString& Parameters)
{
	// Without the window a press is a normal attack; exhausted, no finishing blow either (it costs nothing, but QA's
	// overdrawn finisher must not be possible: exhaustion blocks it like any attack).
	FPlayerActions A = MakeActions();
	TestTrue(TEXT("no window: a normal light"), Same(PressNow(A, EPlayerInput::Light), { C::StartLight }));
	FPlayerActions B = MakeActions();
	B.Stamina.Current = 10.f;
	PressNow(B, EPlayerInput::Light);
	B.Tick(Frame(0.01f));
	B.Tick(Frame(0.f));
	TestTrue(TEXT("exhausted"), B.Stamina.bExhausted && B.GetState() == EPlayerState::Free);
	TestFalse(TEXT("so no cue"), B.CouldRiposte());
	FPlayerFrame Ready;
	Ready.bRiposteReady = true;
	TestTrue(TEXT("and no finishing blow"), PressNow(B, EPlayerInput::Light, Ready).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionsRiposteAtChain, "BossTest.Player.Actions.RiposteAtChainPoint", ActionsTestFlags)
bool FActionsRiposteAtChain::RunTest(const FString& Parameters)
{
	// The hit that staggered him is still running: a light pressed during it is held to the chain point as usual, and
	// comes out as the finishing blow if the window is open by then.
	FPlayerActions A = MakeActions();
	PressNow(A, EPlayerInput::Light);
	A.Tick(Frame(0.1f).Running());
	A.Press(EPlayerInput::Light);
	TestTrue(TEXT("held until the chain point"), A.Tick(Frame(0.f).Running()).IsEmpty());
	FPlayerFrame AtChain = Frame(0.1f).Running().Chain();
	AtChain.bRiposteReady = true;
	TestTrue(TEXT("the finishing blow at the chain point"), Same(A.Tick(AtChain), { C::StartRiposte }) && A.IsRiposting());
	return true;
}

#endif
