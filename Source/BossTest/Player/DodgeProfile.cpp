#include "Player/DodgeProfile.h"

namespace
{
	// Fraction of the full distance covered by time T for decay K over Duration.
	double CoveredFraction(double K, double T, double Duration)
	{
		return (1.0 - FMath::Exp(-K * T)) / (1.0 - FMath::Exp(-K * Duration));
	}
}

FDodgeProfile FDodgeProfile::Make(float InDistance, float InDuration, float EarlyDistance, float EarlyTime)
{
	FDodgeProfile P;
	P.Distance = InDistance;
	P.Duration = FMath::Max(InDuration, KINDA_SMALL_NUMBER);

	// Constant speed covers EarlyTime / Duration of the distance early on. Asking for that or less
	// means no ease-out at all.
	const double Wanted = InDistance > 0.f ? EarlyDistance / InDistance : 0.0;
	if (Wanted <= EarlyTime / P.Duration + 1e-4 || EarlyTime <= 0.f || EarlyTime >= P.Duration)
	{
		P.StartSpeed = InDistance / P.Duration;
		return P;
	}

	// The early fraction grows with the decay, so bisect for it.
	double Lo = 1e-4, Hi = 100.0;
	for (int32 i = 0; i < 60; ++i)
	{
		const double Mid = 0.5 * (Lo + Hi);
		(CoveredFraction(Mid, EarlyTime, P.Duration) < Wanted ? Lo : Hi) = Mid;
	}
	P.Decay = static_cast<float>(0.5 * (Lo + Hi));
	P.StartSpeed = static_cast<float>(InDistance * P.Decay / (1.0 - FMath::Exp(-static_cast<double>(P.Decay) * P.Duration)));
	return P;
}

FDodgeProfile FDodgeProfile::Scaled(float NewDistance) const
{
	FDodgeProfile P = *this;
	const float Scale = Distance > 0.f ? NewDistance / Distance : 0.f;
	P.Distance = NewDistance;
	P.StartSpeed *= Scale;
	return P;
}

float FDodgeProfile::DistanceAt(float T) const
{
	if (T <= 0.f)
	{
		return 0.f;
	}
	if (T >= Duration)
	{
		return Distance;
	}
	return Decay > 0.f ? StartSpeed / Decay * (1.f - FMath::Exp(-Decay * T)) : StartSpeed * T;
}

float FDodgeProfile::SpeedAt(float T) const
{
	if (T < 0.f || T >= Duration)
	{
		return 0.f;
	}
	return StartSpeed * FMath::Exp(-Decay * T);
}
