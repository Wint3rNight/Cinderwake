#include "Player/BossTestPlayerController.h"

#include "Boss/BossCharacter.h"
#include "Combat/AttackComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Player/PlayerCharacter.h"

ABossTestPlayerController::ABossTestPlayerController()
{
	bShowMouseCursor = false;
}

void ABossTestPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetInputMode(FInputModeGameOnly());
	if (PlayerCameraManager)
	{
		// The normal combat camera profile (spec section 13).
		PlayerCameraManager->ViewPitchMin = -40.f;
		PlayerCameraManager->ViewPitchMax = 20.f;
	}
}

ABossCharacter* ABossTestPlayerController::Boss() const
{
	if (!CachedBoss.IsValid())
	{
		if (TActorIterator<ABossCharacter> It(GetWorld()); It)
		{
			CachedBoss = *It;
		}
	}
	return CachedBoss.Get();
}

void ABossTestPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	HandleDebugKeys();
	DrawDebugText();
}

void ABossTestPlayerController::HandleDebugKeys()
{
	APlayerCharacter* Player = Cast<APlayerCharacter>(GetPawn());
	ABossCharacter* B = Boss();

	if (WasInputKeyJustPressed(EKeys::H)) bShowHelp = !bShowHelp;
	if (WasInputKeyJustPressed(EKeys::Two)) UAttackComponent::SetDrawDebug(!UAttackComponent::ShouldDrawDebug());
	if (WasInputKeyJustPressed(EKeys::R))
	{
		if (Player) Player->ResetPlayer();
		if (B) B->ResetBoss();
	}
	if (Player && WasInputKeyJustPressed(EKeys::Seven)) Player->AddFragmentForTesting();

	if (!B)
	{
		return;
	}
	if (WasInputKeyJustPressed(EKeys::One)) B->ToggleAI();
	if (WasInputKeyJustPressed(EKeys::Three)) B->ForceSwing();
	if (WasInputKeyJustPressed(EKeys::Four)) B->SelectNextAction();
	if (WasInputKeyJustPressed(EKeys::Nine)) B->ForceStagger();
}

void ABossTestPlayerController::DrawDebugText() const
{
	if (!GEngine)
	{
		return;
	}
	int32 Key = 1000;
	auto Line = [&Key](const FString& Text, const FColor& Color)
	{
		GEngine->AddOnScreenDebugMessage(Key++, 0.f, Color, Text);
	};

	if (bShowHelp)
	{
		Line(TEXT("H hide help | WASD move, mouse look, Shift sprint (LMB = sprint attack) | LMB light, RMB or F heavy (hold to charge) | Space dodge (LMB / RMB after: dodge attacks) | Q or mouse back button: guard | E heal | Tab lock-on"), FColor::Silver);
		Line(TEXT("1 boss AI on/off | 2 hitboxes | 3 force his selected action, 4 select the next (shown in his debug lines) | 9 force a stagger | 7 take a soul fragment | R reset"), FColor::Silver);
	}
	if (const ABossCharacter* B = Boss())
	{
		for (const FString& BossLine : B->DebugLines())
		{
			Line(BossLine, FColor::Orange);
		}
	}
	if (const APlayerCharacter* Player = Cast<APlayerCharacter>(GetPawn()))
	{
		Line(Player->DebugLine(), FColor::Green);
		const FString Event = Player->LastEventText();
		if (!Event.IsEmpty())
		{
			Line(Event, FColor::White);
		}
	}
}
