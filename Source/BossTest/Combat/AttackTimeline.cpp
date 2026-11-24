#include "Combat/AttackTimeline.h"

namespace
{
	constexpr float MaxStretch = 2.f; // slower than this reads as slow motion rather than menace

	// Clip seconds per real second for a segment; 0 when the segment has no real duration.
	float Rate(float ClipSpan, float RealSpan)
	{
		return RealSpan > 0.f ? ClipSpan / RealSpan : 0.f;
	}

	float LeadInLength(const FAttackTiming& T) { return FMath::Max(0.f, T.LeadInTo - T.LeadInFrom); }
	float TailLength(const FAttackTiming& T) { return FMath::Max(0.f, T.TailTo - T.TailFrom); }

	// A creeping hold: how far the pose moves during it, in clip seconds (0 = a frozen or eased hold).
	float HoldCreep(const FAttackTiming& T)
	{
		return FMath::Max(0.f, T.ClipHoldTo - T.ClipHoldAt);
	}

	float RecoveryCreep(const FAttackTiming& T)
	{
		return T.RecoveryHold > 0.f ? FMath::Max(0.f, T.ClipRecoveryHoldTo - T.ClipRecoveryHoldAt) : 0.f;
	}

	// How long an eased hold takes to reach its stop.
	float EaseTime(const FAttackTiming& T)
	{
		return T.HoldEase > 0.f ? FMath::Min(T.HoldEase, T.Hold) : T.Hold;
	}

	// With an eased hold, the pose keeps drifting into the hold, slowing to a stop at ClipHoldAt
	// (an ease-out: speed falls linearly to 0). The drift is sized so the pose enters the hold at the
	// raise's own speed, so there's no visible stop: Drift = Rate * Ease / 2, Rate = (ActiveStart - Drift) / Moving.
	// Markers here are already relative to the windup's start.
	float HoldDrift(const FAttackTiming& T)
	{
		const float Moving = FMath::Max(0.f, T.Windup - T.Hold);
		if (!T.bEaseIntoHold || T.Hold <= 0.f || Moving <= 0.f || HoldCreep(T) > 0.f)
		{
			return 0.f;
		}
		const float K = EaseTime(T) / (2.f * Moving);
		return FMath::Min(T.ClipActiveStart * K / (1.f + K), T.ClipHoldAt);
	}

	// The windup's start in the extended clip coordinate: the lead-in comes before ClipStartAt.
	float ExtendedStart(const FAttackTiming& T)
	{
		return T.ClipStartAt - LeadInLength(T);
	}

	// The timing with its markers moved so the windup starts at clip 0, and the tail added to the end.
	FAttackTiming FromExtendedStart(const FAttackTiming& T)
	{
		const float Start = ExtendedStart(T);
		FAttackTiming Shifted = T;
		Shifted.ClipStartAt = 0.f;
		Shifted.ClipHoldAt -= Start;
		Shifted.ClipHoldTo -= Start;
		Shifted.ClipActiveStart -= Start;
		Shifted.ClipActiveEnd -= Start;
		Shifted.ClipEnd += TailLength(T) - Start;
		Shifted.ClipRecoveryHoldAt -= Start;
		Shifted.ClipRecoveryHoldTo -= Start;
		Shifted.LeadInFrom = Shifted.LeadInTo = 0.f;
		Shifted.TailFrom = Shifted.TailTo = 0.f;
		return Shifted;
	}

	struct FWindupShape
	{
		float Moving = 0.f;
		float Drift = 0.f;
		float Creep = 0.f;
		float Rate = 0.f;
		float HoldStart = 0.f;
	};

	// Markers relative to the windup's start (FromExtendedStart).
	FWindupShape WindupShape(const FAttackTiming& T)
	{
		// The windup moves through clip 0 -> ClipActiveStart at one constant rate over (Windup - Hold),
		// pausing at ClipHoldAt for Hold seconds along the way (or creeping on to ClipHoldTo).
		FWindupShape W;
		W.Moving = FMath::Max(0.f, T.Windup - T.Hold);
		W.Drift = HoldDrift(T);
		W.Creep = HoldCreep(T);
		W.Rate = Rate(T.ClipActiveStart - W.Drift - W.Creep, W.Moving);
		W.HoldStart = W.Rate > 0.f ? (T.ClipHoldAt - W.Drift) / W.Rate : 0.f;
		return W;
	}

	FAttackSample EvaluateFromStart(const FAttackTiming& T, float Time)
	{
		FAttackSample S;
		S.bChainOpen = T.ChainFrom >= 0.f && Time >= T.ChainFrom;
		S.bFree = Time >= T.Total();
		const FWindupShape W = WindupShape(T);

		if (Time < 0.f)
		{
			S.Phase = EAttackPhase::Windup;
			S.ClipTime = 0.f;
		}
		else if (Time < W.HoldStart)
		{
			S.Phase = EAttackPhase::Windup;
			S.ClipTime = Time * W.Rate;
		}
		else if (Time < W.HoldStart + T.Hold)
		{
			S.Phase = EAttackPhase::Hold;
			if (W.Creep > 0.f)
			{
				S.ClipTime = T.ClipHoldAt + W.Creep * (Time - W.HoldStart) / T.Hold;
			}
			else
			{
				const float Ease = EaseTime(T);
				const float U = Ease > 0.f ? FMath::Min(1.f, (Time - W.HoldStart) / Ease) : 1.f;
				S.ClipTime = T.ClipHoldAt - W.Drift + W.Drift * (1.f - (1.f - U) * (1.f - U));
			}
		}
		else if (Time < T.Windup)
		{
			S.Phase = EAttackPhase::Windup;
			S.ClipTime = T.ClipHoldAt + W.Creep + (Time - W.HoldStart - T.Hold) * W.Rate;
		}
		else if (Time < T.Windup + T.Active)
		{
			S.Phase = EAttackPhase::Active;
			S.ClipTime = T.ClipActiveStart + (Time - T.Windup) * Rate(T.ClipActiveEnd - T.ClipActiveStart, T.Active);
		}
		else if (Time < T.Total())
		{
			S.Phase = EAttackPhase::Recovery;
			const float R = Time - T.Windup - T.Active;
			const float Creep = RecoveryCreep(T);
			const float RecRate = Rate(T.ClipEnd - T.ClipActiveEnd - Creep, T.Recovery - T.RecoveryHold);
			if (T.RecoveryHold <= 0.f)
			{
				S.ClipTime = T.ClipActiveEnd + R * RecRate;
			}
			else
			{
				const float HoldStart = RecRate > 0.f ? (T.ClipRecoveryHoldAt - T.ClipActiveEnd) / RecRate : 0.f;
				if (R < HoldStart)
				{
					S.ClipTime = T.ClipActiveEnd + R * RecRate;
				}
				else if (R < HoldStart + T.RecoveryHold)
				{
					S.ClipTime = T.ClipRecoveryHoldAt + Creep * (R - HoldStart) / T.RecoveryHold;
				}
				else
				{
					S.ClipTime = T.ClipRecoveryHoldAt + Creep + (R - HoldStart - T.RecoveryHold) * RecRate;
				}
			}
		}
		else
		{
			S.Phase = EAttackPhase::Done;
			S.ClipTime = T.ClipEnd;
		}
		return S;
	}
}

FAttackSample EvaluateAttack(const FAttackTiming& T, float Time)
{
	const float Start = ExtendedStart(T);
	FAttackSample S = EvaluateFromStart(FromExtendedStart(T), Time);
	S.ClipTime += Start;
	// Which clip that extended time falls on.
	if (LeadInLength(T) > 0.f && S.ClipTime < T.ClipStartAt)
	{
		S.Span = EAttackSpan::LeadIn;
		S.SpanTime = T.LeadInFrom + (S.ClipTime - Start);
	}
	else if (TailLength(T) > 0.f && S.ClipTime > T.ClipEnd)
	{
		S.Span = EAttackSpan::Tail;
		S.SpanTime = T.TailFrom + (S.ClipTime - T.ClipEnd);
	}
	else
	{
		S.Span = EAttackSpan::Main;
		S.SpanTime = S.ClipTime;
	}
	return S;
}

FHoldWindow GetHoldWindow(const FAttackTiming& T)
{
	FHoldWindow H;
	if (T.Hold > 0.f)
	{
		H.Start = WindupShape(FromExtendedStart(T)).HoldStart;
		H.End = H.Start + T.Hold;
	}
	return H;
}

void AddHoldTime(FAttackTiming& T, float Extra)
{
	if (T.Hold <= 0.f)
	{
		// No hold to lengthen: the whole windup stretches a little (Double Execution's fast band, 0.55-0.60 s), never a
		// freeze mid-motion.
		const float Stretched = FMath::Max(Extra, -0.5f * T.Windup);
		T.Windup += Stretched;
		if (T.ChainFrom >= 0.f)
		{
			T.ChainFrom += Stretched;
		}
		return;
	}
	const float Applied = FMath::Max(Extra, -T.Hold); // never a negative hold
	T.Hold += Applied;
	T.Windup += Applied;
	if (T.ChainFrom >= 0.f)
	{
		T.ChainFrom += Applied;
	}
}

bool ActiveOverlap(const FAttackTiming& T, float From, float To)
{
	const float ActiveStart = T.Windup;
	const float ActiveEnd = T.Windup + T.Active;
	return To >= ActiveStart && From < ActiveEnd && To >= From;
}

float ClampAttackStep(float DeltaSeconds)
{
	return FMath::Min(DeltaSeconds, 1.f / 30.f);
}

EArmor ArmorAt(EAttackPhase Phase, EArmor WindupArmor, EArmor ActiveArmor)
{
	switch (Phase)
	{
	case EAttackPhase::Windup:
	case EAttackPhase::Hold:
		return WindupArmor;
	case EAttackPhase::Active:
		return ActiveArmor;
	default:
		return EArmor::None;
	}
}

FLungeWindow LungeWindow(const FAttackTiming& T, float StartsBeforeActive, float EndsIntoActive)
{
	FLungeWindow W;
	const float Before = FMath::Clamp(StartsBeforeActive, 0.f, T.Windup);
	W.Start = T.Windup - Before;
	W.Duration = Before + T.Active * FMath::Clamp(EndsIntoActive, 0.f, 1.f);
	return W;
}

FBranchStep StepToBranch(float Elapsed, float Step, float BranchAt, bool bResolved)
{
	FBranchStep R;
	R.Step = Step;
	if (!bResolved && BranchAt >= 0.f && Elapsed <= BranchAt && Elapsed + Step >= BranchAt)
	{
		R.Step = FMath::Max(0.f, BranchAt - Elapsed);
		R.bReachedBranch = true;
	}
	return R;
}

TArray<FString> ValidateBranch(const FAttackTiming& Mine, const FAttackTiming& Variant, bool bSameClip, float BranchAt)
{
	TArray<FString> Out;
	// A variant on another clip restarts its own timeline at the branch with a crossfade (Step 4d), so no pose has
	// to match. On the same clip it carries on seamlessly, so the poses must agree.
	if (bSameClip && FMath::Abs(EvaluateAttack(Mine, BranchAt).ClipTime - EvaluateAttack(Variant, BranchAt).ClipTime) > 0.005f)
	{
		Out.Add(TEXT("Error: the pose at the branch differs between the attack and its variant."));
	}
	if (BranchAt + 1.f / 30.f >= Mine.Windup)
	{
		Out.Add(TEXT("Error: the branch must be at least 1/30 s before the windup ends."));
	}
	return Out;
}

TArray<FString> ValidateTiming(const FAttackTiming& T)
{
	TArray<FString> Out;

	if (T.Windup < 0.f || T.Hold < 0.f || T.Active <= 0.f || T.Recovery < 0.f)
	{
		Out.Add(TEXT("Error: durations must be non-negative, and the active window must be longer than 0."));
	}
	if (T.Hold > T.Windup)
	{
		Out.Add(TEXT("Error: the hold can't be longer than the windup that contains it."));
	}
	if (!(T.ClipStartAt >= 0.f && T.ClipHoldAt >= ExtendedStart(T) && T.ClipHoldAt <= T.ClipActiveStart && T.ClipActiveStart < T.ClipActiveEnd && T.ClipActiveEnd <= T.ClipEnd))
	{
		Out.Add(TEXT("Error: clip markers must be in order: 0 <= start (less any lead-in) <= hold-at <= active start < active end <= end."));
	}
	if (T.HoldEase > 0.f && (!T.bEaseIntoHold || T.HoldEase > T.Hold))
	{
		Out.Add(TEXT("Error: a hold ease time needs an eased hold, and can't be longer than the hold."));
	}
	if (T.RecoveryHold < 0.f || T.RecoveryHold > T.Recovery)
	{
		Out.Add(TEXT("Error: the recovery hold can't be longer than the recovery."));
	}
	if (T.RecoveryHold > 0.f && !(T.ClipRecoveryHoldAt >= T.ClipActiveEnd && T.ClipRecoveryHoldAt + RecoveryCreep(T) <= T.ClipEnd + TailLength(T)))
	{
		Out.Add(TEXT("Error: the recovery hold must sit between the active end and the end (tail included)."));
	}
	if (T.LeadInTo < T.LeadInFrom || T.LeadInFrom < 0.f || T.TailTo < T.TailFrom || T.TailFrom < 0.f)
	{
		Out.Add(TEXT("Error: a lead-in or tail must run forward from 0 or later."));
	}
	if (HoldCreep(T) > 0.f && T.ClipHoldTo > T.ClipActiveStart)
	{
		Out.Add(TEXT("Error: a creeping hold must end at or before the active start."));
	}
	if (HoldCreep(T) > 0.f && T.bEaseIntoHold)
	{
		Out.Add(TEXT("Error: a hold either creeps (Clip Hold To) or eases in, not both."));
	}
	const FAttackTiming Shifted = FromExtendedStart(T);

	auto CheckStretch = [&Out](const TCHAR* Segment, float RealSpan, float ClipSpan)
	{
		if (ClipSpan > 0.f && RealSpan / ClipSpan > MaxStretch)
		{
			Out.Add(FString::Printf(TEXT("Warning: the %s plays %.2fx slower than the clip, which reads as slow motion. Use a hold instead."),
				Segment, RealSpan / ClipSpan));
		}
	};
	CheckStretch(TEXT("windup"), T.Windup - T.Hold, Shifted.ClipActiveStart - HoldDrift(Shifted) - HoldCreep(Shifted));
	CheckStretch(TEXT("active window"), T.Active, T.ClipActiveEnd - T.ClipActiveStart);
	CheckStretch(TEXT("recovery"), T.Recovery - T.RecoveryHold, Shifted.ClipEnd - Shifted.ClipActiveEnd - RecoveryCreep(T));
	return Out;
}
