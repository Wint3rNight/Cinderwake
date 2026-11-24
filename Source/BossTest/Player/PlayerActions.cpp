#include "Player/PlayerActions.h"

namespace
{
	// Timers accumulate frame by frame, so compare with a little slack: 0.34 + 0.01 must reach 0.35.
	constexpr float TimeSlack = 1e-4f;
	bool Reached(float Time, float Mark) { return Time >= Mark - TimeSlack; }
}

void FPlayerActions::Press(EPlayerInput Input)
{
	// Classified the moment it's pressed, so even a long frame right after the click can't lose it.
	const bool bAttackInput = Input == EPlayerInput::Light || Input == EPlayerInput::Heavy;
	if (State == EPlayerState::Attacking)
	{
		if (bAttackInput)
		{
			Held = Input; // until the chain point, however early
			HeldAge = 0.f;
			if (Input == EPlayerInput::Heavy)
			{
				bChargeable = false; // any new press is a re-press: it queues a heavy, it never charges this one
			}
		}
		else if (Input == EPlayerInput::Dodge)
		{
			// Only after his own hit, and close enough to free that it won't fire long after he chose it.
			if (bLastPastActive && LastTimeToFree <= Tuning.DodgeHoldBeforeFree + TimeSlack)
			{
				Held = EPlayerInput::Dodge;
				HeldAge = 0.f;
			}
			else
			{
				Held = EPlayerInput::None; // dropped, and it takes back a held attack: a panic dodge never adds a slash
				bRefused = true;
			}
		}
		else
		{
			Held = EPlayerInput::None;
			Buffer.Press(Input); // a heal still only starts from free, within the buffer
		}
		return;
	}
	if (State == EPlayerState::Dodging)
	{
		if (bAttackInput)
		{
			if (Input == EPlayerInput::Light && Held == EPlayerInput::Light)
			{
				bChainQueued = true; // the double-click: the finisher after the dodge light
				return;
			}
			Held = Input; // until the lock: the dodge light or the slam
			HeldAge = 0.f;
			bChainQueued = false;
			Buffer.Clear(); // the latest press wins over a dodge waiting in the buffer
			return;
		}
		Held = EPlayerInput::None;
		bChainQueued = false;
	}
	Buffer.Press(Input);
}

void FPlayerActions::Reset()
{
	Stamina.Max = Tuning.StaminaMax;
	Stamina.RegenRate = Tuning.StaminaRegenRate;
	Stamina.RegenDelay = Tuning.StaminaRegenDelay;
	Stamina.ExhaustedRegenDelay = Tuning.ExhaustedRegenDelay;
	Stamina.ExhaustedUntil = Tuning.ExhaustedUntil;
	Stamina.SprintDrain = Tuning.SprintDrain;
	Stamina.ResetRuntime();
	Buffer.Window = Tuning.InputBuffer;
	Buffer.Clear();
	State = EPlayerState::Free;
	StateTime = 0.f;
	SinceDodge = 100.f;
	LightIndex = 0;
	Line = EAttackLine::None;
	Held = EPlayerInput::None;
	HeldAge = 0.f;
	bChainQueued = false;
	bRefused = false;
	bLastPastActive = false;
	LastTimeToFree = 100.f;
	bChargeable = false;
	SprintTime = 0.f;
	BlockStunTime = 0.f;
	bPerfectWindow = false;
	PerfectWindowAge = 0.f;
	PerfectLockoutLeft = 0.f;
	StunHeld = EPlayerInput::None;
	HealCharges = Tuning.HealCharges;
	HealRetryLeft = 0.f;
	bDodgeHeld = false;
	bSprinting = false;
	bBusy = false;
}

float FPlayerActions::GetHealProgress() const
{
	return State == EPlayerState::Healing && Tuning.HealCast > 0.f ? FMath::Clamp(StateTime / Tuning.HealCast, 0.f, 1.f) : 0.f;
}

bool FPlayerActions::IsInvulnerable() const
{
	return (State == EPlayerState::Dodging && StateTime < Tuning.DodgeIFrames) || State == EPlayerState::Knockdown;
}

void FPlayerActions::Enter(EPlayerState NewState)
{
	// Attacks and dodges chain into each other without regenerating; the delay starts when the chain ends.
	const bool bAction = NewState == EPlayerState::Attacking || NewState == EPlayerState::Dodging;
	if (bAction && !bBusy)
	{
		Stamina.BeginAction();
		bBusy = true;
	}
	else if (!bAction && bBusy)
	{
		Stamina.EndAction();
		bBusy = false;
	}
	State = NewState;
	StateTime = 0.f;
	if (NewState == EPlayerState::Attacking)
	{
		// A new attack: presses until its first sample are early ones.
		bLastPastActive = false;
		LastTimeToFree = 100.f;
	}
}

void FPlayerActions::StopSprint(FPlayerCommands& Out)
{
	if (bSprinting)
	{
		bSprinting = false;
		Stamina.SetSprinting(false);
		Out.Add(EPlayerCommand::StopSprint);
	}
}

void FPlayerActions::UpdateSprint(const FPlayerFrame& Frame, FPlayerCommands& Out)
{
	const bool bWant = Frame.bSprintHeld && Frame.bMoveHeld && Stamina.CanStartAction();
	if (bSprinting && (!bWant || Stamina.bExhausted))
	{
		StopSprint(Out);
	}
	else if (!bSprinting && bWant)
	{
		bSprinting = true;
		SprintTime = 0.f;
		Stamina.SetSprinting(true);
		Out.Add(EPlayerCommand::StartSprint);
	}
}

bool FPlayerActions::TryStartAttack(float Cost, FPlayerCommands& Out)
{
	if (!Stamina.CanStartAction())
	{
		return false;
	}
	StopSprint(Out);
	DropGuard(Out);
	if (State == EPlayerState::Dodging)
	{
		Out.Add(EPlayerCommand::EndDodge);
	}
	Stamina.Spend(Cost);
	return true;
}

bool FPlayerActions::TryStartBuffered(const FPlayerFrame& Frame, FPlayerCommands& Out)
{
	switch (Buffer.Peek())
	{
	case EPlayerInput::Dodge:
	{
		if (!Stamina.CanStartAction())
		{
			return false;
		}
		bChainQueued = false;
		StopSprint(Out);
		DropGuard(Out);
		if (State == EPlayerState::Dodging)
		{
			Out.Add(EPlayerCommand::EndDodge);
		}
		else if (State == EPlayerState::Healing)
		{
			Out.Add(EPlayerCommand::CancelHeal);
			HealRetryLeft = Tuning.HealRetryDelay;
		}
		Stamina.Spend(Frame.bMoveHeld ? Tuning.DodgeCost : Tuning.BackstepCost);
		Out.Add(Frame.bMoveHeld ? EPlayerCommand::StartDodge : EPlayerCommand::StartBackstep);
		Buffer.Consume();
		SinceDodge = 0.f;
		Enter(EPlayerState::Dodging);
		return true;
	}

	case EPlayerInput::Light:
	{
		const bool bDash = Reached(SinceDodge, Tuning.DodgeAttackFrom) && SinceDodge <= Tuning.DodgeAttackUntil + TimeSlack;
		if (bDash)
		{
			if (!TryStartAttack(Costs.DodgeLights.IsValidIndex(0) ? Costs.DodgeLights[0] : 0.f, Out))
			{
				return false;
			}
			LightIndex = 0;
			Out.Add(EPlayerCommand::StartDodgeLight);
			Line = EAttackLine::DodgeLight;
			bChargeable = false;
		}
		else if (bSprinting && Reached(SprintTime, Tuning.SprintAttackAfter))
		{
			// The sprint attack; a light at its chain point continues the normal string at Light 2.
			if (!TryStartAttack(Costs.SprintAttack, Out))
			{
				return false;
			}
			LightIndex = 0;
			Out.Add(EPlayerCommand::StartSprintAttack);
			Line = EAttackLine::Sprint;
			bChargeable = false;
		}
		else
		{
			if (!TryStartAttack(Costs.Lights.IsValidIndex(0) ? Costs.Lights[0] : 0.f, Out))
			{
				return false;
			}
			LightIndex = 0;
			Out.Add(EPlayerCommand::StartLight);
			Line = EAttackLine::Light;
			bChargeable = false;
		}
		Buffer.Consume();
		Enter(EPlayerState::Attacking);
		return true;
	}

	case EPlayerInput::Heavy:
	{
		// Out of a dodge, the heavy is the leaping slam.
		const bool bDodgeHeavy = Reached(SinceDodge, Tuning.DodgeAttackFrom) && SinceDodge <= Tuning.DodgeAttackUntil + TimeSlack;
		if (!TryStartAttack(bDodgeHeavy ? Costs.DodgeHeavy : Costs.Heavy, Out))
		{
			return false;
		}
		Out.Add(bDodgeHeavy ? EPlayerCommand::StartDodgeHeavy : EPlayerCommand::StartHeavy);
		Line = bDodgeHeavy ? EAttackLine::Slam : EAttackLine::Heavy;
		bChainQueued = false;
		bChargeable = !bDodgeHeavy && Frame.bHeavyHeld; // charges only while RMB stays down from here
		Buffer.Consume();
		Enter(EPlayerState::Attacking);
		return true;
	}

	case EPlayerInput::Heal:
		if (State != EPlayerState::Free || HealCharges <= 0 || HealRetryLeft > TimeSlack)
		{
			return false;
		}
		StopSprint(Out);
		bChainQueued = false;
		Out.Add(EPlayerCommand::StartHeal);
		Buffer.Consume();
		Enter(EPlayerState::Healing);
		return true;

	default:
		return false;
	}
}

void FPlayerActions::NoteAttack(bool bPastActive, float TimeToFree)
{
	bLastPastActive = bPastActive;
	LastTimeToFree = TimeToFree;
}

FPlayerCommands FPlayerActions::Tick(const FPlayerFrame& Frame)
{
	FPlayerCommands Out;
	const float Dt = FMath::Max(0.f, Frame.Dt);
	if (bRefused)
	{
		Out.Add(EPlayerCommand::RefuseInput);
		bRefused = false;
	}
	// The running attack's own clock, for classifying the presses that arrive before the next tick.
	bLastPastActive = Frame.bPastActive;
	LastTimeToFree = Frame.TimeToFree;
	HeldAge += Dt;

	// Age the buffer first, so a press is judged by how long ago it happened.
	Buffer.Tick(Dt);
	// The drain follows the state every tick, so no exit path can leave it running.
	Stamina.SetDrain(State == EPlayerState::Guard || State == EPlayerState::BlockStun ? Tuning.GuardDrain : bSprinting ? Tuning.SprintDrain : 0.f);
	Stamina.Tick(Dt);
	StateTime += Dt;
	SinceDodge += Dt;
	if (bSprinting)
	{
		SprintTime += Dt;
	}
	HealRetryLeft = FMath::Max(0.f, HealRetryLeft - Dt);
	PerfectLockoutLeft = FMath::Max(0.f, PerfectLockoutLeft - Dt);
	if (bPerfectWindow)
	{
		PerfectWindowAge += Dt;
		if (PerfectWindowAge > Tuning.PerfectGuardUntil + TimeSlack)
		{
			// Passed with no hit: a whiff. No new window for the lockout, counted from the window's end.
			bPerfectWindow = false;
			PerfectLockoutLeft = FMath::Max(0.f, Tuning.PerfectGuardLockout - (PerfectWindowAge - Tuning.PerfectGuardUntil));
		}
	}

	// When a state ends this tick, free runs straight away, so a buffered action comes out on the exact frame.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const EPlayerState Before = State;
		switch (State)
		{
		case EPlayerState::Free:
			if (TryStartBuffered(Frame, Out))
			{
				break;
			}
			if (Frame.bGuardHeld && Stamina.CanStartAction())
			{
				// The guard rises only from free, never while exhausted; holding it drains and blocks regeneration.
				StopSprint(Out);
				Stamina.SetDrain(Tuning.GuardDrain);
				Out.Add(EPlayerCommand::StartGuard);
				Enter(EPlayerState::Guard);
				// Only a press that raises it opens a perfect window (a guard rising because the key was held doesn't).
				if (Frame.bGuardPressed && PerfectLockoutLeft <= TimeSlack)
				{
					bPerfectWindow = true;
					PerfectWindowAge = 0.f;
				}
				break;
			}
			UpdateSprint(Frame, Out);
			break;

		case EPlayerState::Guard:
			if (Buffer.Peek() == EPlayerInput::Dodge || Buffer.Peek() == EPlayerInput::Light || Buffer.Peek() == EPlayerInput::Heavy)
			{
				if (TryStartBuffered(Frame, Out)) // drops the guard first
				{
					break;
				}
			}
			if (!Frame.bGuardHeld || Stamina.bExhausted)
			{
				// Released, or drained to 0 just by holding: down at once, with no stun.
				DropGuard(Out);
				Enter(EPlayerState::Free);
			}
			break;

		case EPlayerState::BlockStun:
			if (Buffer.Peek() == EPlayerInput::Dodge || Buffer.Peek() == EPlayerInput::Light || Buffer.Peek() == EPlayerInput::Heavy)
			{
				// Held, latest press wins, and it comes out the moment the stun ends, however long the stun is
				// (a heavy deflect-stun outlasts the buffer).
				StunHeld = Buffer.Peek();
				Buffer.Consume();
			}
			if (Reached(StateTime, BlockStunTime))
			{
				if (StunHeld != EPlayerInput::None)
				{
					Buffer.Press(StunHeld); // free runs again this tick and starts it
					StunHeld = EPlayerInput::None;
					DropGuard(Out);
					Enter(EPlayerState::Free);
				}
				else if (Frame.bGuardHeld && !Stamina.bExhausted)
				{
					Enter(EPlayerState::Guard);
					StateTime = Tuning.GuardRaiseTime; // still raised: it blocks straight away
				}
				else
				{
					DropGuard(Out);
					Enter(EPlayerState::Free);
				}
			}
			break;

		case EPlayerState::GuardBroken:
			if (Reached(StateTime, Tuning.GuardBreakStun))
			{
				Out.Add(EPlayerCommand::EndGuardBreak);
				Enter(EPlayerState::Free);
			}
			break;

		case EPlayerState::Attacking:
			if (bChargeable && !Frame.bHeavyHeld)
			{
				bChargeable = false; // released since this heavy started: a press now queues a heavy, it doesn't charge this one
			}
			if (bChargeable && Frame.bAttackRunning && Frame.bAtChargeBranch)
			{
				// The heavy waits at its cocked pose for this: RMB still down since its start commits to the charge (if there's stamina).
				bChargeable = false;
				if (Stamina.CanStartAction())
				{
					Stamina.Spend(Tuning.ChargeExtraCost);
					Out.Add(EPlayerCommand::BeginCharge);
				}
			}
			if (Frame.bGuardPressed && Held != EPlayerInput::None)
			{
				Held = EPlayerInput::None; // a fresh guard press takes back a queued press
			}
			if (!Frame.bAttackRunning && (Held == EPlayerInput::Light || Held == EPlayerInput::Heavy))
			{
				// Pressed on the frame the attack ended (it ticks before input is read): free is past every chain
				// point, so it starts now, in this attack's line.
				const EPlayerInput Next = Held;
				Held = EPlayerInput::None;
				if (!TryStartChained(Next, Frame, Out))
				{
					Out.Add(EPlayerCommand::RefuseInput);
					bChainQueued = false;
					bChargeable = false;
					Line = EAttackLine::None;
					Enter(EPlayerState::Free);
				}
			}
			else if (!Frame.bAttackRunning)
			{
				// Free. A held dodge comes out now (the free pass below); a held attack that never chained is dropped.
				const bool bDodgeNow = Held == EPlayerInput::Dodge;
				Held = EPlayerInput::None;
				bChainQueued = false;
				bChargeable = false;
				Line = EAttackLine::None;
				Enter(EPlayerState::Free);
				if (bDodgeNow)
				{
					Buffer.Press(EPlayerInput::Dodge);
				}
			}
			else if (Frame.bChainOpen && (Held == EPlayerInput::Light || Held == EPlayerInput::Heavy))
			{
				const EPlayerInput Next = Held;
				Held = EPlayerInput::None;
				if (!TryStartChained(Next, Frame, Out))
				{
					Out.Add(EPlayerCommand::RefuseInput); // exhausted: dropped now, never fired later
				}
			}
			break;

		case EPlayerState::Dodging:
			if (Frame.bGuardPressed && Held != EPlayerInput::None)
			{
				Held = EPlayerInput::None; // a fresh guard press takes back a held attack here too
				bChainQueued = false;
			}
			if (Reached(StateTime, Tuning.DodgeLock))
			{
				if (Held == EPlayerInput::Light || Held == EPlayerInput::Heavy)
				{
					if (Stamina.CanStartAction())
					{
						Buffer.Press(Held); // the dodge light or the slam, now
					}
					else
					{
						Out.Add(EPlayerCommand::RefuseInput); // exhausted: dropped, shown
						bChainQueued = false;
					}
					Held = EPlayerInput::None;
				}
				if (TryStartBuffered(Frame, Out))
				{
					if (bChainQueued && State == EPlayerState::Attacking)
					{
						Held = EPlayerInput::Light; // the double-click's second light: the finisher at the dodge light's chain point
						HeldAge = 0.f;
					}
					bChainQueued = false;
					break;
				}
			}
			if (Reached(StateTime, Tuning.DodgeDuration))
			{
				Enter(EPlayerState::Free);
			}
			break;

		case EPlayerState::Healing:
			if (Buffer.Peek() == EPlayerInput::Dodge && TryStartBuffered(Frame, Out))
			{
				break;
			}
			if (Reached(StateTime, Tuning.HealCast))
			{
				Out.Add(EPlayerCommand::FinishHeal);
				--HealCharges;
				Enter(EPlayerState::Free);
			}
			break;

		case EPlayerState::HitReact:
			// A dodge pressed during the reaction is held until it ends; any later press replaces it.
			if (Buffer.Peek() == EPlayerInput::Dodge)
			{
				bDodgeHeld = true;
				Buffer.Consume();
			}
			else if (Buffer.Peek() != EPlayerInput::None)
			{
				bDodgeHeld = false;
			}
			if (Reached(StateTime, Tuning.HitReaction))
			{
				if (bDodgeHeld)
				{
					Buffer.Press(EPlayerInput::Dodge);
					bDodgeHeld = false;
				}
				Enter(EPlayerState::Free);
			}
			break;

		case EPlayerState::Knockdown:
			if (Reached(StateTime, Tuning.Knockdown))
			{
				Enter(EPlayerState::Free);
			}
			break;

		case EPlayerState::Dead:
			break;
		}

		if (State == Before || State != EPlayerState::Free)
		{
			break;
		}
	}
	return Out;
}

FPlayerCommands FPlayerActions::OnHit(bool bKnockdown)
{
	FPlayerCommands Out;
	if (State == EPlayerState::Dead || State == EPlayerState::Knockdown)
	{
		return Out;
	}
	if (State == EPlayerState::GuardBroken && !bKnockdown)
	{
		return Out; // the damage lands, but the break's stagger is neither restarted nor shortened
	}
	DropGuard(Out); // an unblocked hit (from behind, a projectile) drops a raised guard
	ClearPerfectWindow();
	bChargeable = false;
	switch (State)
	{
	case EPlayerState::Attacking:
		Out.Add(EPlayerCommand::StopAttack);
		break;
	case EPlayerState::Dodging:
		Out.Add(EPlayerCommand::EndDodge);
		break;
	case EPlayerState::Healing:
		Out.Add(EPlayerCommand::CancelHeal);
		HealRetryLeft = Tuning.HealRetryDelay;
		break;
	default:
		break;
	}
	StopSprint(Out);
	SinceDodge = 100.f; // getting hit closes the dodge-attack window: a light after the reaction is Light 1
	bChainQueued = false;
	StunHeld = EPlayerInput::None;
	// Held attacks never come out after a hit; a held dodge does if it's fresh (it waits out the reaction).
	const bool bFreshDodge = Held == EPlayerInput::Dodge && HeldAge < Tuning.InputBuffer - TimeSlack;
	Held = EPlayerInput::None;
	if (bKnockdown)
	{
		bDodgeHeld = false; // only a hit reaction holds a dodge
		Out.Add(EPlayerCommand::PlayKnockdown);
		Enter(EPlayerState::Knockdown);
	}
	else
	{
		Out.Add(EPlayerCommand::PlayHitReact);
		Enter(EPlayerState::HitReact);
		if (bFreshDodge)
		{
			bDodgeHeld = true;
		}
	}
	return Out;
}

bool FPlayerActions::IsGuardActive() const
{
	return (State == EPlayerState::Guard && StateTime >= Tuning.GuardRaiseTime - TimeSlack) || State == EPlayerState::BlockStun;
}

void FPlayerActions::DropGuard(FPlayerCommands& Out)
{
	if (State == EPlayerState::Guard || State == EPlayerState::BlockStun)
	{
		Out.Add(EPlayerCommand::StopGuard);
	}
	Stamina.SetDrain(0.f);
	if (bPerfectWindow)
	{
		// Dropped before its window ended (released, or an action): a whiff, locked out from where the window would have ended.
		bPerfectWindow = false;
		PerfectLockoutLeft = Tuning.PerfectGuardLockout + FMath::Max(0.f, Tuning.PerfectGuardUntil - PerfectWindowAge);
	}
}

void FPlayerActions::ClearPerfectWindow()
{
	// Any of his hits resolving on you closes the window and clears the lockout, so one early press never costs the next hit.
	bPerfectWindow = false;
	PerfectLockoutLeft = 0.f;
}

FPlayerCommands FPlayerActions::OnBlock(float RawDamage, bool bHeavyBlock)
{
	FPlayerCommands Out;
	// The resolver has decided (it can see the raise finish before this frame's tick does), so any raised guard takes it.
	if (State != EPlayerState::Guard && State != EPlayerState::BlockStun)
	{
		return Out;
	}
	ClearPerfectWindow();
	// Every block costs stamina; a block that overdraws to 0 is still blocked, then the guard breaks.
	Stamina.Spend(RawDamage * Tuning.BlockCostPerDamage);
	if (Stamina.bExhausted)
	{
		// Stamina regenerates through the break stun (the user, after Step 5): the break costs the turn, but the dodge
		// is back when the stun ends, so his next attack stays dodgeable (the charter's guard and exhaustion lesson).
		Stamina.SetDrain(0.f);
		Stamina.RestartRegenDelay(0.f);
		bDodgeHeld = false;
		StunHeld = EPlayerInput::None;
		Out.Add(EPlayerCommand::PlayGuardBreak);
		Enter(EPlayerState::GuardBroken);
		return Out;
	}
	Out.Add(bHeavyBlock ? EPlayerCommand::PlayHeavyBlock : EPlayerCommand::PlayBlock);
	const float NewStun = bHeavyBlock ? Tuning.HeavyBlockStun : Tuning.BlockStun;
	if (State == EPlayerState::BlockStun && BlockStunTime - StateTime >= NewStun)
	{
		return Out; // a lighter block mustn't cut short the stun that's still running
	}
	BlockStunTime = NewStun;
	Enter(EPlayerState::BlockStun); // the guard stays up through it
	return Out;
}

FPlayerCommands FPlayerActions::OnDeath()
{
	FPlayerCommands Out;
	if (State == EPlayerState::Dead)
	{
		return Out;
	}
	switch (State)
	{
	case EPlayerState::Attacking:
		Out.Add(EPlayerCommand::StopAttack);
		break;
	case EPlayerState::Dodging:
		Out.Add(EPlayerCommand::EndDodge);
		break;
	case EPlayerState::Healing:
		Out.Add(EPlayerCommand::CancelHeal);
		break;
	default:
		break;
	}
	StopSprint(Out);
	DropGuard(Out);
	Out.Add(EPlayerCommand::PlayDeath);
	SinceDodge = 100.f;
	bChainQueued = false;
	Buffer.Clear();
	bDodgeHeld = false;
	StunHeld = EPlayerInput::None;
	Held = EPlayerInput::None;
	Enter(EPlayerState::Dead);
	return Out;
}

FPlayerCommands FPlayerActions::OnPerfectGuard(float RawDamage, bool bKnockdownHit)
{
	FPlayerCommands Out;
	if (State != EPlayerState::Guard && State != EPlayerState::BlockStun)
	{
		return Out;
	}
	ClearPerfectWindow();
	const float Stun = bKnockdownHit ? Tuning.HeavyDeflectStun : Tuning.DeflectStun;
	// Cheap, and it never breaks the guard: an overdraw only exhausts, and regeneration starts the normal delay
	// after the deflect-stun, so his next opener stays dodgeable.
	Stamina.Spend(RawDamage * Tuning.PerfectGuardCostPerDamage);
	if (Stamina.bExhausted)
	{
		Stamina.SetDrain(0.f);
		Stamina.RestartRegenDelay(Stun + Tuning.StaminaRegenDelay);
	}
	Out.Add(bKnockdownHit ? EPlayerCommand::PlayHeavyDeflect : EPlayerCommand::PlayDeflect);
	if (State == EPlayerState::BlockStun && BlockStunTime - StateTime >= Stun)
	{
		return Out; // a shorter stun mustn't cut short the one still running
	}
	BlockStunTime = Stun;
	Enter(EPlayerState::BlockStun); // the guard stays up through it
	return Out;
}

bool FPlayerActions::HasPerfectWindow() const
{
	return bPerfectWindow;
}

bool FPlayerActions::TryStartChained(EPlayerInput Input, const FPlayerFrame& Frame, FPlayerCommands& Out)
{
	if (Input == EPlayerInput::Heavy)
	{
		// Out of heavy 1, the charged heavy or the slam it's heavy 2 (the rising uppercut), out of anything else heavy 1,
		// so a heavy never follows itself. Never the slam, even inside the dodge-attack window. Only heavy 1 charges.
		const bool bSecond = Line == EAttackLine::Heavy || Line == EAttackLine::Slam;
		if (!TryStartAttack(bSecond ? Costs.Heavy2 : Costs.Heavy, Out))
		{
			return false;
		}
		Out.Add(bSecond ? EPlayerCommand::ChainHeavyTwo : EPlayerCommand::ChainHeavy);
		Line = bSecond ? EAttackLine::Heavy2 : EAttackLine::Heavy;
		bChainQueued = false;
		bChargeable = !bSecond && Frame.bHeavyHeld; // heavy 1 still down since the press can charge
		Enter(EPlayerState::Attacking);
		return true;
	}

	// The next light in this attack's line.
	int32 Next = 0;
	EPlayerCommand Command = EPlayerCommand::StartLight;
	EAttackLine NextLine = EAttackLine::Light;
	switch (Line)
	{
	case EAttackLine::Light:
		Next = Costs.Lights.Num() > 0 ? (LightIndex + 1) % Costs.Lights.Num() : 0;
		break;
	case EAttackLine::Sprint:
		Next = 1; // the sprint attack carries on into Light 2
		break;
	case EAttackLine::DodgeLight:
		if (LightIndex == 0 && Costs.DodgeLights.IsValidIndex(1))
		{
			Next = 1; // the finisher
			Command = EPlayerCommand::StartDodgeLight;
			NextLine = EAttackLine::DodgeLight;
		}
		else
		{
			Next = 1; // after the finisher, Light 2: its blade comes back the other way
		}
		break;
	case EAttackLine::Slam:
		Next = 0;
		Command = EPlayerCommand::StartLightRise; // he stands up out of the landing crouch first
		break;
	default:
		Next = 0; // after a heavy, Light 1 rises out of the thrust
		break;
	}
	const TArray<float>& Table = Command == EPlayerCommand::StartDodgeLight ? Costs.DodgeLights : Costs.Lights;
	if (!Table.IsValidIndex(Next) || !TryStartAttack(Table[Next], Out))
	{
		return false;
	}
	LightIndex = Next;
	Out.Add(Command);
	Line = NextLine;
	bChargeable = false;
	Enter(EPlayerState::Attacking);
	return true;
}
