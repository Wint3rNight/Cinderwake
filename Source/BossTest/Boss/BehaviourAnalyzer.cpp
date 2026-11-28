#include "Boss/BehaviourAnalyzer.h"

const TCHAR* HabitName(int32 Habit)
{
	static const TCHAR* Names[BossHabitCount] = { TEXT("dodging"), TEXT("attacking"), TEXT("keeping away"), TEXT("circling"),
		TEXT("healing at range"), TEXT("hit and run") };
	return Habit >= 0 && Habit < BossHabitCount ? Names[Habit] : TEXT("none");
}

float HabitScale(uint8 Mask, const FHabitSnapshot& Habits, const FHabitCaps& Caps, int32* OutDriver)
{
	float Best = 1.f;
	int32 Driver = -1;
	for (int32 h = 0; h < BossHabitCount; ++h)
	{
		if (Mask & (1u << h))
		{
			const float M = 1.f + (Caps.Values[h] - 1.f) * FMath::Clamp(Habits.Scores[h], 0.f, 1.f);
			if (M > Best)
			{
				Best = M;
				Driver = h;
			}
		}
	}
	if (OutDriver)
	{
		*OutDriver = Driver;
	}
	return Best;
}

void FBehaviourAnalyzer::Reset()
{
	Pending.Reset();
	Clock = 0.0;
	DodgeRate = AttackRate = HealRate = HitRunRate = 0.f;
	Disengaged = Circling = 0.f;
	bHealSeen = false;
	HealsSeen = 0;
	bHitRunArmed = false;
	SinceHit = 0.f;
	HitFromEdge = 0.f;
	Snapshot = FHabitSnapshot();
}

void FBehaviourAnalyzer::Observe(const FPlayerObservation& Obs)
{
	Clock += Obs.Dt;
	Pending.Add({ Clock, Obs });
	int32 Ready = 0;
	while (Ready < Pending.Num() && Pending[Ready].Time <= Clock - Tuning.PerceptionDelay + 1e-4)
	{
		Apply(Pending[Ready].Obs);
		++Ready;
	}
	if (Ready > 0)
	{
		Pending.RemoveAt(0, Ready, EAllowShrinking::No);
	}
}

void FBehaviourAnalyzer::Apply(const FPlayerObservation& Obs)
{
	const float Tau = FMath::Max(Tuning.TimeConstant, 0.1f);
	const float Keep = FMath::Exp(-FMath::Max(Obs.Dt, 0.f) / Tau);

	// Events become rates that decay with the time constant: each adds 1/tau, so a rate is "events per second lately".
	DodgeRate *= Keep;
	AttackRate *= Keep;
	HealRate *= Keep;
	HitRunRate *= Keep;
	DodgeRate += Obs.bDodgeStarted ? 1.f / Tau : 0.f;
	AttackRate += Obs.bAttackStarted && Obs.bBossNeutral ? 1.f / Tau : 0.f;
	if (Obs.bHealing && !bHealSeen)
	{
		++HealsSeen;
		HealRate += Obs.EdgeCm > Tuning.HealRangeFrom ? 1.f / Tau : 0.f;
	}
	bHealSeen = Obs.bHealing;

	// Hit and run: the latest landed hit arms it; a big enough retreat soon after counts once.
	if (Obs.bLandedHit)
	{
		bHitRunArmed = true;
		SinceHit = 0.f;
		HitFromEdge = Obs.EdgeCm;
	}
	else if (bHitRunArmed)
	{
		SinceHit += Obs.Dt;
		if (SinceHit > Tuning.HitRunWindow)
		{
			bHitRunArmed = false;
		}
		else if (Obs.EdgeCm - HitFromEdge > Tuning.HitRunDistance)
		{
			HitRunRate += 1.f / Tau;
			bHitRunArmed = false;
		}
	}

	// Shares of time: the same average applied to "is it happening right now".
	const float Speed = Obs.PlayerVelocity.Size();
	const bool bAway = Obs.EdgeCm > Tuning.FarFrom && Speed > Tuning.RunSpeed;
	const float Distance = Obs.ToPlayer.Size();
	const float AngularDeg = Distance > 1.f ? FMath::RadiansToDegrees(FMath::Abs(FVector2D::CrossProduct(Obs.ToPlayer / Distance, Obs.PlayerVelocity)) / Distance) : 0.f;
	const bool bCircling = Obs.EdgeCm <= Tuning.CircleWithin && AngularDeg > Tuning.CircleRate;
	Disengaged = (bAway ? 1.f : 0.f) + (Disengaged - (bAway ? 1.f : 0.f)) * Keep;
	Circling = (bCircling ? 1.f : 0.f) + (Circling - (bCircling ? 1.f : 0.f)) * Keep;

	Snapshot.Set(EBossHabit::Dodging, FMath::Clamp(DodgeRate * Tuning.DodgeWindow / FMath::Max(Tuning.DodgeFull, 0.01f), 0.f, 1.f));
	Snapshot.Set(EBossHabit::Attacking, FMath::Clamp(AttackRate * Tuning.AttackWindow / FMath::Max(Tuning.AttackFull, 0.01f), 0.f, 1.f));
	Snapshot.Set(EBossHabit::Disengaged, Disengaged);
	Snapshot.Set(EBossHabit::Circling, Circling);
	Snapshot.Set(EBossHabit::HealingAtRange, FMath::Clamp(HealRate * Tau / FMath::Max(Tuning.HealFull, 0.01f), 0.f, 1.f));
	Snapshot.Set(EBossHabit::HitAndRun, FMath::Clamp(HitRunRate * Tau / FMath::Max(Tuning.HitRunFull, 0.01f), 0.f, 1.f));
}
