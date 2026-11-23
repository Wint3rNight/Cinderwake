#include "UI/BossTestHUD.h"

#include "Boss/BossCharacter.h"
#include "Combat/CombatantComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Player/PlayerCharacter.h"

FHealthBarLayout LayoutHealthBar(float Current, float Max, float BaseMax, float PixelsPerPoint)
{
	FHealthBarLayout L;
	const float Health = FMath::Clamp(Current, 0.f, Max);
	L.Fill = Health * PixelsPerPoint;
	L.Empty = (Max - Health) * PixelsPerPoint;
	L.Lost = FMath::Max(0.f, BaseMax - Max) * PixelsPerPoint;
	return L;
}

FBossBarLayout LayoutBossBar(float Current, float Max, float TrailValue, float PhaseAt, float Width)
{
	FBossBarLayout L;
	if (Max <= 0.f)
	{
		return L;
	}
	const float Health = FMath::Clamp(Current, 0.f, Max);
	L.Fill = Health / Max * Width;
	L.Trail = FMath::Max(0.f, FMath::Clamp(TrailValue, 0.f, Max) - Health) / Max * Width;
	L.PhaseMark = FMath::Clamp(PhaseAt, 0.f, Max) / Max * Width;
	return L;
}

void FDamageTrail::Reset(float Current)
{
	TrailValue = LastCurrent = Current;
	HoldLeft = 0.f;
}

void FDamageTrail::Tick(float Dt, float Current)
{
	if (Current < LastCurrent)
	{
		HoldLeft = Hold; // a new hit restarts the hold; the trail stays where it was
	}
	LastCurrent = Current;
	if (Current >= TrailValue)
	{
		TrailValue = Current; // healed or reset
		HoldLeft = 0.f;
		return;
	}
	const float HoldUsed = FMath::Min(HoldLeft, FMath::Max(0.f, Dt));
	HoldLeft -= HoldUsed;
	const float DrainTime = FMath::Max(0.f, Dt) - HoldUsed;
	TrailValue = FMath::Max(Current, TrailValue - DrainPerSecond * DrainTime);
}

void ABossTestHUD::DrawHUD()
{
	Super::DrawHUD();
	DrawBossBar();
	const APlayerCharacter* Player = Cast<APlayerCharacter>(GetOwningPawn());
	if (!Player || !Player->GetCombatant())
	{
		return;
	}
	const FHealthModel& Health = Player->GetCombatant()->State.Health;
	const FPlayerActions& Actions = Player->GetActions();

	// Top left, souls style: health, stamina under it, heal charges under that.
	const float X = 48.f;
	float Y = 40.f;
	const float Border = 2.f;

	// Health: what you have, what's missing, and the part of max HP soul fragments took.
	const FHealthBarLayout Bar = LayoutHealthBar(Health.Current, Health.Max(), Health.BaseMax, HealthPixelsPerPoint);
	const float HealthHeight = 14.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - Border, Y - Border, Bar.Fill + Bar.Empty + Bar.Lost + Border * 2.f, HealthHeight + Border * 2.f);
	DrawRect(FLinearColor(0.62f, 0.07f, 0.06f), X, Y, Bar.Fill, HealthHeight);
	DrawRect(FLinearColor(0.12f, 0.03f, 0.03f), X + Bar.Fill, Y, Bar.Empty, HealthHeight);
	if (Bar.Lost > 0.f)
	{
		const float LostX = X + Bar.Fill + Bar.Empty;
		DrawRect(FLinearColor(0.20f, 0.08f, 0.28f), LostX, Y, Bar.Lost, HealthHeight);
		// Reclaim progress toward the next fragment, along the bottom of the lost part.
		const float OneFragment = Health.FragmentFraction * Health.BaseMax * HealthPixelsPerPoint;
		const float Progress = Health.ReclaimPerFragment > 0.f ? FMath::Clamp(Health.GetReclaimProgress() / Health.ReclaimPerFragment, 0.f, 1.f) : 0.f;
		DrawRect(FLinearColor(0.70f, 0.55f, 0.95f), LostX, Y + HealthHeight - 3.f, OneFragment * Progress, 3.f);
	}
	Y += HealthHeight + Border * 2.f + 4.f;

	// Stamina, dimmed while exhausted.
	const FStaminaModel& Stamina = Actions.Stamina;
	const float StaminaHeight = 9.f;
	const float StaminaWidth = Stamina.Max * StaminaPixelsPerPoint;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - Border, Y - Border, StaminaWidth + Border * 2.f, StaminaHeight + Border * 2.f);
	DrawRect(FLinearColor(0.05f, 0.08f, 0.04f), X, Y, StaminaWidth, StaminaHeight);
	const FLinearColor StaminaColor = Stamina.bExhausted ? FLinearColor(0.45f, 0.30f, 0.08f) : FLinearColor(0.25f, 0.62f, 0.18f);
	DrawRect(StaminaColor, X, Y, FMath::Clamp(Stamina.Current, 0.f, Stamina.Max) * StaminaPixelsPerPoint, StaminaHeight);
	Y += StaminaHeight + Border * 2.f + 6.f;

	// Heal charges, and the cast's progress while healing.
	const float Pip = 12.f;
	for (int32 i = 0; i < Actions.Tuning.HealCharges; ++i)
	{
		const bool bFull = i < Actions.GetHealCharges();
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X + i * (Pip + 6.f) - 1.f, Y - 1.f, Pip + 2.f, Pip + 2.f);
		DrawRect(bFull ? FLinearColor(0.85f, 0.65f, 0.20f) : FLinearColor(0.15f, 0.12f, 0.08f), X + i * (Pip + 6.f), Y, Pip, Pip);
	}
	if (Actions.GetState() == EPlayerState::Healing)
	{
		const float CastX = X + Actions.Tuning.HealCharges * (Pip + 6.f) + 6.f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), CastX, Y + 3.f, 80.f, 6.f);
		DrawRect(FLinearColor(0.85f, 0.65f, 0.20f), CastX, Y + 3.f, 80.f * Actions.GetHealProgress(), 6.f);
	}
}

void ABossTestHUD::DrawBossBar()
{
	if (!CachedBoss.IsValid())
	{
		if (TActorIterator<ABossCharacter> It(GetWorld()); It)
		{
			CachedBoss = *It;
		}
	}
	const ABossCharacter* Boss = CachedBoss.Get();
	if (!Boss || !Boss->GetCombatant() || !Canvas)
	{
		return;
	}
	const FHealthModel& Health = Boss->GetCombatant()->State.Health;
	if (!bTrailStarted)
	{
		BossTrail.Reset(Health.Current);
		bTrailStarted = true;
	}
	BossTrail.Tick(RenderDelta, Health.Current); // real time, so hit-stop doesn't freeze the trail

	// Bottom centre, souls style: his name, then the bar with the trail of recent damage.
	const float Width = Canvas->ClipX * BossBarWidthFraction;
	const float Height = 12.f;
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = Canvas->ClipY - 70.f;
	const FBossBarLayout Bar = LayoutBossBar(Health.Current, Health.Max(), BossTrail.Value(), Boss->PhaseTransitionHealth, Width);

	if (GEngine && GEngine->GetMediumFont())
	{
		DrawText(Boss->DisplayName.ToString(), FLinearColor(0.85f, 0.82f, 0.75f), X, Y - 24.f, GEngine->GetMediumFont());
	}
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - 2.f, Y - 2.f, Width + 4.f, Height + 4.f);
	DrawRect(FLinearColor(0.10f, 0.03f, 0.03f), X, Y, Width, Height);
	DrawRect(FLinearColor(0.85f, 0.68f, 0.18f), X + Bar.Fill, Y, Bar.Trail, Height);
	DrawRect(FLinearColor(0.55f, 0.05f, 0.04f), X, Y, Bar.Fill, Height);
	// Where the transformation will start (Step 8).
	DrawRect(FLinearColor(0.9f, 0.9f, 0.9f, 0.7f), X + Bar.PhaseMark - 1.f, Y - 3.f, 2.f, Height + 6.f);

	// His poise under it (Step 4c): you build it on purpose now, so it has to be readable. Grey while he's immune.
	const FPoiseModel& Poise = Boss->GetCombatant()->State.Poise;
	const FPoiseBarLayout PoiseBar = LayoutPoiseBar(Poise.Current(), Poise.Threshold, Poise.IsImmune() || Poise.IsStaggered(), Width);
	if (PoiseBar.bVisible)
	{
		const float PoiseY = Y + Height + 6.f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - 1.f, PoiseY - 1.f, Width + 2.f, 6.f);
		DrawRect(PoiseBar.bGrey ? FLinearColor(0.35f, 0.35f, 0.35f, 0.8f) : FLinearColor(0.10f, 0.08f, 0.04f), X, PoiseY, Width, 4.f);
		DrawRect(FLinearColor(0.95f, 0.80f, 0.35f), X, PoiseY, PoiseBar.Fill, 4.f);
	}
}

FPoiseBarLayout LayoutPoiseBar(float Current, float Threshold, bool bImmune, float Width)
{
	FPoiseBarLayout Out;
	Out.Fill = Threshold > 0.f ? FMath::Clamp(Current / Threshold, 0.f, 1.f) * Width : 0.f;
	Out.bVisible = Current > 0.f || bImmune;
	Out.bGrey = bImmune;
	return Out;
}
