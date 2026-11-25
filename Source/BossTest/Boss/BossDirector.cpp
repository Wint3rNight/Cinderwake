#include "Boss/BossDirector.h"

#include "Misc/ScopeExit.h"

FBossPerception FBossDirector::Touching(FBossPerception P)
{
	// Pressed against him the capsule gap reads a little below zero: that's close, never out of range.
	P.EdgeCm = FMath::Max(0.f, P.EdgeCm);
	return P;
}

void FBossDirector::Init(TArray<FBossOption> InOptions, TArray<FBossComboOption> InCombos, const FBossDirectorTuning& InTuning)
{
	Options = MoveTemp(InOptions);
	Combos = MoveTemp(InCombos);
	Tuning = InTuning;
	Reset();
}

void FBossDirector::Reset()
{
	Cooldowns.Reset();
	History.Reset();
	LastCombo = -1;
	Majors = 0;
	NeutralTime = 0.f;
	FarTime = 0.f;
	Band = EBossBand::Close;
	AngleBand = EBossAngleBand::Front;
	bBandKnown = false;
	bLastChangeCloser = false;
	bJustHit = false;
	ExcludeOnce = EBossMove::Wait;
}

const FBossOption* FBossDirector::Find(int32 Action) const
{
	return Options.FindByPredicate([Action](const FBossOption& O) { return O.Id == Action; });
}

EBossBand FBossDirector::RawBand(float Edge) const
{
	return Edge <= Tuning.CloseMax ? EBossBand::Close : Edge <= Tuning.MediumMax ? EBossBand::Medium : Edge <= Tuning.FarMax ? EBossBand::Far : EBossBand::Extreme;
}

EBossAngleBand FBossDirector::RawAngleBand(float Angle) const
{
	return Angle <= Tuning.FrontAngle ? EBossAngleBand::Front : Angle <= Tuning.SideAngle ? EBossAngleBand::Side
		: Angle <= Tuning.RearAngle ? EBossAngleBand::RearQuarter : EBossAngleBand::Behind;
}

bool FBossDirector::Observe(const FBossPerception& InP, bool bNeutral, float Dt)
{
	const FBossPerception P = Touching(InP);
	// Bands change only past the edge plus the hysteresis, measured toward the current band, so standing on an edge
	// can't flicker his decisions.
	const EBossBand OldBand = Band;
	if (!bBandKnown)
	{
		Band = RawBand(P.EdgeCm);
		AngleBand = RawAngleBand(P.AngleDeg);
		bBandKnown = true;
	}
	else
	{
		const float Edges[] = { Tuning.CloseMax, Tuning.MediumMax, Tuning.FarMax };
		const int32 Current = static_cast<int32>(Band);
		const float Low = Current > 0 ? Edges[Current - 1] - Tuning.BandHysteresis : -1.f;
		const float High = Current < 3 ? Edges[Current] + Tuning.BandHysteresis : TNumericLimits<float>::Max();
		if (P.EdgeCm < Low || P.EdgeCm > High)
		{
			Band = RawBand(P.EdgeCm);
		}
		const float AngleEdges[] = { Tuning.FrontAngle, Tuning.SideAngle, Tuning.RearAngle };
		const int32 A = static_cast<int32>(AngleBand);
		const float ALow = A > 0 ? AngleEdges[A - 1] - Tuning.AngleHysteresis : -1.f;
		const float AHigh = A < 3 ? AngleEdges[A] + Tuning.AngleHysteresis : 1000.f;
		if (P.AngleDeg < ALow || P.AngleDeg > AHigh)
		{
			AngleBand = RawAngleBand(P.AngleDeg);
		}
	}

	for (TPair<int32, float>& C : Cooldowns)
	{
		C.Value = FMath::Max(0.f, C.Value - Dt);
	}
	if (bNeutral)
	{
		NeutralTime += Dt;
		if (NeutralTime >= Tuning.NeutralClearsMajors)
		{
			Majors = 0; // a beat of neutral counts as the rhythm's rest
		}
	}
	// Leaky, so a one-frame dip into Medium can't reset the chase.
	FarTime = Band == EBossBand::Far || Band == EBossBand::Extreme ? FMath::Min(FarTime + Dt, Tuning.PursuitAfter)
		: FMath::Max(0.f, FarTime - 2.f * Dt); // capped, so coming back ends a long chase at once
	if (OldBand != Band)
	{
		bLastChangeCloser = static_cast<int32>(Band) < static_cast<int32>(OldBand);
	}
	return bBandKnown && OldBand != Band;
}

float FBossDirector::GetCooldownLeft(int32 Action) const
{
	return Cooldowns.FindRef(Action);
}

bool FBossDirector::AngleFits(const FBossOption& O) const
{
	switch (O.Angle)
	{
	case EBossAngleRule::FrontOnly: return AngleBand == EBossAngleBand::Front;
	case EBossAngleRule::FrontAndSides: return AngleBand == EBossAngleBand::Front || AngleBand == EBossAngleBand::Side;
	case EBossAngleRule::FrontSidesAndRear: return AngleBand != EBossAngleBand::Behind;
	default: return true;
	}
}

float FBossDirector::AngleScale(const FBossOption& O) const
{
	if (!AngleFits(O))
	{
		return 0.f;
	}
	return O.Angle == EBossAngleRule::FrontSidesAndRear && AngleBand == EBossAngleBand::RearQuarter ? O.RearWeight : 1.f;
}

bool FBossDirector::IsValidNow(int32 Action, const FBossPerception& InP, bool bAsFollowUp) const
{
	const FBossPerception P = Touching(InP);
	const FBossOption* O = Find(Action);
	if (!O || P.EdgeCm < O->MinRange || P.EdgeCm > O->MaxRange)
	{
		return false;
	}
	if (bAsFollowUp)
	{
		// He can't turn in recovery, and every follow-up tracks at least 110 deg before its cutoff: anything within
		// 90 deg will do. Never while the player is down (section 10).
		return !P.bPlayerDown && P.AngleDeg <= 90.f;
	}
	if (!AngleFits(*O))
	{
		return false;
	}
	return GetCooldownLeft(Action) <= 0.f && (!O->bNeedsPunishTrigger || P.bPunishTrigger);
}

float FBossDirector::RepetitionScale(int32 Key) const
{
	int32 Uses = 0;
	for (const int32 H : History)
	{
		Uses += H == Key ? 1 : 0;
	}
	return FMath::Max(0.4f, 1.f - 0.2f * Uses);
}

bool FBossDirector::ComboValid(const FBossComboOption& C) const
{
	if (C.Id == LastCombo)
	{
		return false; // never the same combo twice in a row
	}
	for (const int32 F : C.FollowUps)
	{
		if (GetCooldownLeft(F) > 0.f || !Find(F))
		{
			return false;
		}
	}
	return true;
}

float FBossDirector::ComboWeight(int32 Combo) const
{
	const FBossComboOption* C = Combos.FindByPredicate([Combo](const FBossComboOption& X) { return X.Id == Combo; });
	return C ? C->Weight * FMath::Min(RepetitionScale(ComboKey(C->Id)), RepetitionScale(ActionKey(C->Opener))) : 0.f;
}

int32 FBossDirector::RollCombo(int32 Opener, FRandomStream& Rng) const
{
	if (Pinned >= 0)
	{
		return -1;
	}
	TArray<TPair<int32, float>, TInlineAllocator<8>> Choices;
	Choices.Add({ -1, 0.f });
	float Base = 0.f;
	float Total = 0.f;
	for (const FBossComboOption& C : Combos)
	{
		if (C.Opener == Opener && ComboValid(C))
		{
			const float W = ComboWeight(C.Id);
			Choices.Add({ C.Id, W });
			Base += FMath::Max(0.f, C.Weight);
			Total += W;
		}
	}
	if (Choices.Num() == 1)
	{
		return -1; // nothing valid to chain
	}
	Choices[0].Value = Tuning.StopShare * Base;
	Total += Choices[0].Value;
	float Roll = Rng.FRand() * Total;
	for (const TPair<int32, float>& Choice : Choices)
	{
		Roll -= Choice.Value;
		if (Roll <= 0.f)
		{
			return Choice.Key;
		}
	}
	return Choices.Last().Key;
}

FBossDecision FBossDirector::Decide(const FBossPerception& InP, FRandomStream& Rng)
{
	const FBossPerception P = Touching(InP);
	FBossDecision D;
	ON_SCOPE_EXIT
	{
		bJustHit = false;
		ExcludeOnce = EBossMove::Wait;
	};
	if (P.bPlayerDown)
	{
		D.Move = EBossMove::Wait;
		D.Duration = 0.5f;
		D.Why = TEXT("player down: waiting");
		return D; // he doesn't commit to an attack while you're knocked down (section 10)
	}

	// Score every valid attack, then the movement rows of the table.
	struct FScored { EBossMove Move; int32 Action; float Weight; };
	TArray<FScored, TInlineAllocator<16>> Scored;
	bool bAngleOnly = false;
	for (const FBossOption& O : Options)
	{
		if (Pinned >= 0 && O.Id != Pinned)
		{
			continue;
		}
		const bool bInRange = P.EdgeCm >= O.MinRange && P.EdgeCm <= O.MaxRange;
		const bool bAvailable = GetCooldownLeft(O.Id) <= 0.f && (!O.bNeedsPunishTrigger || P.bPunishTrigger);
		if (!bInRange || !bAvailable)
		{
			continue;
		}
		if (!AngleFits(O))
		{
			bAngleOnly = true;
			continue;
		}
		const float W = (Pinned >= 0 ? 1.f : O.Weights.Get(Band)) * AngleScale(O) * RepetitionScale(ActionKey(O.Id));
		if (W > 0.f)
		{
			Scored.Add({ EBossMove::Attack, O.Id, W });
		}
	}
	if (Pinned < 0 && AngleBand != EBossAngleBand::Behind)
	{
		const float Approach = ExcludeOnce == EBossMove::Approach ? 0.f : Tuning.ApproachWeights.Get(Band);
		// No circling right after a hit ended his beat, while he's chasing, or after a circle got nowhere.
		// Nor past 1.8 s of neutral: section 10 caps any stretch without an attack at 3 s.
		const bool bNoCircle = bJustHit || PursuitActive() || ExcludeOnce == EBossMove::Circle || NeutralTime >= 1.8f;
		const float Circle = bNoCircle ? 0.f : Tuning.CircleWeights.Get(Band);
		if (Approach > 0.f)
		{
			Scored.Add({ EBossMove::Approach, -1, Approach });
		}
		if (Circle > 0.f)
		{
			Scored.Add({ EBossMove::Circle, -1, Circle });
		}
	}

	float Total = 0.f;
	for (const FScored& S : Scored)
	{
		Total += S.Weight;
		D.Why += S.Move == EBossMove::Attack ? FString::Printf(TEXT("%d:%.2f "), S.Action, S.Weight)
			: FString::Printf(TEXT("%s:%.2f "), S.Move == EBossMove::Approach ? TEXT("approach") : TEXT("circle"), S.Weight);
	}

	if (Total <= 0.f)
	{
		// Fallbacks: an attack that fits once he faces you turns him, but right behind him and close he moves instead
		// (the backstab spot moves); far away with nothing fitting he closes in; otherwise he repositions.
		if (AngleBand == EBossAngleBand::Behind && Band != EBossBand::Close)
		{
			D.Move = EBossMove::Turn;
			D.Duration = 3.f;
			D.Why = TEXT("behind him, not close: turn");
		}
		else if (bAngleOnly && AngleBand != EBossAngleBand::Behind)
		{
			D.Move = EBossMove::Turn;
			D.Duration = 3.f;
			D.Why = TEXT("fits once he faces you: turn");
		}
		else if (Pinned >= 0 || (Band != EBossBand::Close && AngleBand != EBossAngleBand::Behind))
		{
			D.Move = EBossMove::Approach;
			D.Duration = Tuning.ApproachMax;
			D.Why = TEXT("nothing fits: approach");
		}
		else
		{
			D.Move = EBossMove::Reposition;
			D.Duration = Rng.FRandRange(Tuning.RepositionMin, Tuning.RepositionMax);
			D.Why = AngleBand == EBossAngleBand::Behind ? TEXT("behind and close: reposition") : TEXT("nothing fits: reposition");
		}
		return D;
	}

	float Roll = Rng.FRand() * Total;
	const FScored* Pick = &Scored.Last();
	for (const FScored& S : Scored)
	{
		Roll -= S.Weight;
		if (Roll <= 0.f)
		{
			Pick = &S;
			break;
		}
	}
	D.Move = Pick->Move;
	D.Action = Pick->Action;
	if (D.Move == EBossMove::Approach)
	{
		D.Duration = Tuning.ApproachMax;
	}
	else if (D.Move == EBossMove::Circle)
	{
		D.Duration = Rng.FRandRange(Tuning.CircleMin, Tuning.CircleMax);
		D.CircleSign = Rng.FRand() < 0.5f ? -1 : 1;
	}
	if (D.Move != EBossMove::Attack)
	{
		D.Duration = FMath::Min(D.Duration, FMath::Max(0.4f, 3.f - NeutralTime)); // the 3 s cap
	}
	D.Why = FString::Printf(TEXT("picked from %s"), *D.Why.TrimEnd());
	return D;
}

void FBossDirector::NoteCommitted(int32 Opener, int32 Combo)
{
	auto Push = [this](int32 Key)
	{
		History.Add(Key);
		while (History.Num() > Tuning.RepetitionWindow)
		{
			History.RemoveAt(0);
		}
	};
	Push(ActionKey(Opener));
	if (Combo >= 0)
	{
		Push(ComboKey(Combo));
	}
	LastCombo = Combo >= 0 ? Combo : LastCombo;
	++Majors;
	NeutralTime = 0.f;
}

void FBossDirector::NoteStepStarted(int32 Action)
{
	if (const FBossOption* O = Find(Action))
	{
		if (O->Cooldown > 0.f)
		{
			Cooldowns.Add(Action, O->Cooldown);
		}
	}
}

float FBossDirector::PauseAfterAttack()
{
	if (Majors >= Tuning.MajorsBeforeBreather)
	{
		Majors = 0;
		return Tuning.Breather;
	}
	return Tuning.CommitPause;
}

void FBossDirector::NoteBeatBroken()
{
	bJustHit = true;
}

float FBossDirector::PauseEndAfterHit(float PauseStart, float PauseLength, float Now) const
{
	return FMath::Min(PauseStart + PauseLength, FMath::Max(Now, PauseStart + Tuning.CommitPause));
}

bool FBossDirector::ShouldRedecide(EBossMove Move, float TimeInMove) const
{
	// Reaching a closer band ends an approach at once; anything else waits, so a wiggle on an edge can't flip him.
	return (Move == EBossMove::Approach && bLastChangeCloser) || TimeInMove >= Tuning.RedecideAfter;
}
