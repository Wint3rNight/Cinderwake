#include "Boss/BossBrain.h"

namespace
{
	bool InRange(const FBossActionOption& O, float Distance)
	{
		return Distance >= O.MinRange && Distance <= O.MaxRange;
	}

	// The angle part of an option's weight: 0 when the player is outside its angle rule.
	float AngleScale(const FBossActionOption& O, float Angle, const FBossBrainTuning& T)
	{
		switch (O.Angle)
		{
		case EBossAngleRule::FrontOnly:
			return Angle <= T.FrontAngle ? 1.f : 0.f;
		case EBossAngleRule::FrontAndSides:
			return Angle <= T.SideAngle ? 1.f : 0.f;
		case EBossAngleRule::FrontSidesAndRear:
			return Angle <= T.SideAngle ? 1.f : (Angle <= T.RearQuarterAngle ? O.RearWeight : 0.f);
		default:
			return 1.f;
		}
	}
}

FBossChoiceResult ChooseBossAction(const FBossSituation& S, TArrayView<const FBossActionOption> Options, FRandomStream& Rng, const FBossBrainTuning& Tuning)
{
	FBossChoiceResult Result;
	if (S.bPlayerDown || Options.Num() == 0)
	{
		return Result; // he doesn't commit to an attack while you're knocked down (spec section 10)
	}

	TArray<float, TInlineAllocator<16>> Weights;
	float Total = 0.f;
	bool bTurnWouldHelp = false;
	for (const FBossActionOption& O : Options)
	{
		float W = 0.f;
		const bool bAvailable = O.CooldownLeft <= 0.f && (!O.bNeedsPunishTrigger || S.bPunishTrigger);
		if (bAvailable && InRange(O, S.EdgeDistance))
		{
			const float Angle = AngleScale(O, S.AngleDeg, Tuning);
			bTurnWouldHelp |= Angle <= 0.f;
			W = O.Weight * Angle * (O.Id == S.LastActionId ? Tuning.RepeatScale : 1.f);
		}
		Weights.Add(W);
		Total += W;
	}

	if (Total <= 0.f)
	{
		Result.Choice = bTurnWouldHelp ? EBossChoice::Turn : EBossChoice::Approach;
		return Result;
	}
	float Roll = Rng.FRand() * Total;
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		if (Weights[i] <= 0.f)
		{
			continue;
		}
		Result.Choice = EBossChoice::Attack;
		Result.ActionId = Options[i].Id;
		Roll -= Weights[i];
		if (Roll <= 0.f)
		{
			break;
		}
	}
	return Result;
}

EBossChoice ChoosePlaceholderAction(float EdgeDistance, float AngleDeg, bool bPlayerDown, const FBossPlaceholderTuning& Tuning)
{
	if (bPlayerDown)
	{
		return EBossChoice::Wait;
	}
	if (AngleDeg > Tuning.MeleeAngle)
	{
		return EBossChoice::Turn;
	}
	return EdgeDistance <= Tuning.SwingRange ? EBossChoice::Swing : EBossChoice::Approach;
}
