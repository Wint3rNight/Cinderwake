// The player's HUD (Step 3): health with the part lost to soul fragments, stamina, heal charges.
// Drawn on the canvas, so it needs no widget assets.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BossTestHUD.generated.h"

// Widths in pixels of the health bar's three parts, left to right: health, missing health, and max HP
// lost to fragments.
struct FHealthBarLayout
{
	float Fill = 0.f;
	float Empty = 0.f;
	float Lost = 0.f;
};

FHealthBarLayout LayoutHealthBar(float Current, float Max, float BaseMax, float PixelsPerPoint);

// The boss bar, in pixels: health, the yellow trail of recent damage after it, and where PhaseAt falls.
struct FBossBarLayout
{
	float Fill = 0.f;
	float Trail = 0.f;
	float PhaseMark = 0.f;
};

FBossBarLayout LayoutBossBar(float Current, float Max, float TrailValue, float PhaseAt, float Width);

// His poise bar (Step 4c), under his health: shown above 0, grey and empty during his stagger immunity.
struct FPoiseBarLayout
{
	float Fill = 0.f;
	bool bVisible = false;
	bool bGrey = false;
};

FPoiseBarLayout LayoutPoiseBar(float Current, float Threshold, bool bImmune, float Width);

// The yellow trail of recent damage: it holds after each hit, then drains to the current health. It
// never sits below it, and healing or a reset snaps it up. Ticked on real time.
struct FDamageTrail
{
	float Hold = 0.6f;              // seconds after the latest hit
	float DrainPerSecond = 1100.f;  // health points per second (a fifth of his bar)

	void Reset(float Current);
	void Tick(float Dt, float Current);
	float Value() const { return TrailValue; }

private:
	float TrailValue = 0.f;
	float LastCurrent = 0.f;
	float HoldLeft = 0.f;
};

UCLASS()
class BOSSTEST_API ABossTestHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, Category = "HUD")
	float HealthPixelsPerPoint = 1.2f;

	UPROPERTY(EditAnywhere, Category = "HUD")
	float StaminaPixelsPerPoint = 2.4f;

	UPROPERTY(EditAnywhere, Category = "HUD", meta = (ToolTip = "Fraction of the screen width his bar spans"))
	float BossBarWidthFraction = 0.55f;

private:
	void DrawBossBar();

	TWeakObjectPtr<class ABossCharacter> CachedBoss;
	FDamageTrail BossTrail;
	bool bTrailStarted = false;
};
