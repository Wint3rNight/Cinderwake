#include "Combat/AttackTimeline.h"

namespace
{
	constexpr float MaxStretch = 2.f; // slower than this reads as slow motion rather than menace

	// Clip seconds per real second for a segment; 0 when the segment has no real duration.
	float Rate(float ClipSpan, float RealSpan)
	{
		return RealSpan > 0.f ? ClipSpan / RealSpan : 0.f;
	}

	// With an eased hold, the pose keeps drifting through the hold, slowing to a stop at ClipHoldAt
	// (an ease-out: speed falls linearly to 0). The drift is sized so the pose enters the hold at the
	// raise's own speed, so there's no visible stop: Drift = Rate * Hold / 2, Rate = (ActiveStart - Drift) / Moving.
	float HoldDrift(const FAttackTiming& T)
	{
		const float Moving = FMath::Max(0.f, T.Windup - T.Hold);
		if (!T.bEaseIntoHold || T.Hold <= 0.f || Moving <= 0.f)
		{
			return 0.f;
		}
		const float K = T.Hold / (2.f * Moving);
		return FMath::Min(T.ClipActiveStart * K / (1.f + K), T.ClipHoldAt);
	}
}

FAttackSample EvaluateAttack(const FAttackTiming& T, float Time)
{
	FAttackSample S;
	S.bChainOpen = T.ChainFrom >= 0.f && Time >= T.ChainFrom;
	S.bFree = Time >= T.Total();

	// The windup moves through clip 0 -> ClipActiveStart at one constant rate over (Windup - Hold),
	// pausing at ClipHoldAt for Hold seconds along the way.
	const float Moving = FMath::Max(0.f, T.Windup - T.Hold);
	const float Drift = HoldDrift(T);
	const float WindupRate = Rate(T.ClipActiveStart - Drift, Moving);
	const float HoldStart = WindupRate > 0.f ? (T.ClipHoldAt - Drift) / WindupRate : 0.f;

	if (Time < 0.f)
	{
		S.Phase = EAttackPhase::Windup;
		S.ClipTime = 0.f;
	}
	else if (Time < HoldStart)
	{
		S.Phase = EAttackPhase::Windup;
		S.ClipTime = Time * WindupRate;
	}
	else if (Time < HoldStart + T.Hold)
	{
		S.Phase = EAttackPhase::Hold;
		const float U = T.Hold > 0.f ? (Time - HoldStart) / T.Hold : 1.f;
		S.ClipTime = T.ClipHoldAt - Drift + Drift * (1.f - (1.f - U) * (1.f - U));
	}
	else if (Time < T.Windup)
	{
		S.Phase = EAttackPhase::Windup;
		S.ClipTime = T.ClipHoldAt + (Time - HoldStart - T.Hold) * WindupRate;
	}
	else if (Time < T.Windup + T.Active)
	{
		S.Phase = EAttackPhase::Active;
		S.ClipTime = T.ClipActiveStart + (Time - T.Windup) * Rate(T.ClipActiveEnd - T.ClipActiveStart, T.Active);
	}
	else if (Time < T.Total())
	{
		S.Phase = EAttackPhase::Recovery;
		S.ClipTime = T.ClipActiveEnd + (Time - T.Windup - T.Active) * Rate(T.ClipEnd - T.ClipActiveEnd, T.Recovery);
	}
	else
	{
		S.Phase = EAttackPhase::Done;
		S.ClipTime = T.ClipEnd;
	}
	return S;
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
	if (!(T.ClipHoldAt >= 0.f && T.ClipHoldAt <= T.ClipActiveStart && T.ClipActiveStart < T.ClipActiveEnd && T.ClipActiveEnd <= T.ClipEnd))
	{
		Out.Add(TEXT("Error: clip markers must be in order: 0 <= hold-at <= active start < active end <= end."));
	}

	auto CheckStretch = [&Out](const TCHAR* Segment, float RealSpan, float ClipSpan)
	{
		if (ClipSpan > 0.f && RealSpan / ClipSpan > MaxStretch)
		{
			Out.Add(FString::Printf(TEXT("Warning: the %s plays %.2fx slower than the clip, which reads as slow motion. Use a hold instead."),
				Segment, RealSpan / ClipSpan));
		}
	};
	CheckStretch(TEXT("windup"), T.Windup - T.Hold, T.ClipActiveStart - HoldDrift(T));
	CheckStretch(TEXT("active window"), T.Active, T.ClipActiveEnd - T.ClipActiveStart);
	CheckStretch(TEXT("recovery"), T.Recovery, T.ClipEnd - T.ClipActiveEnd);
	return Out;
}
