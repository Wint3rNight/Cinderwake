#include "BossTestGameMode.h"

#include "Boss/BossCharacter.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Player/BossTestPlayerController.h"
#include "Player/PlayerCharacter.h"
#include "UI/BossTestHUD.h"
#include "UObject/ConstructorHelpers.h"

ABossTestGameMode::ABossTestGameMode()
{
	// The Blueprint versions carry the data (attacks, clips), so use them when they exist.
	static ConstructorHelpers::FClassFinder<APawn> PlayerBlueprint(TEXT("/Game/BossTest/BP_FengMao"));
	DefaultPawnClass = PlayerBlueprint.Succeeded() ? PlayerBlueprint.Class : TSubclassOf<APawn>(APlayerCharacter::StaticClass());
	PlayerControllerClass = ABossTestPlayerController::StaticClass();
	HUDClass = ABossTestHUD::StaticClass();
}

void ABossTestGameMode::StartPlay()
{
	Super::StartPlay();

	// The arena places the boss. Only spawn one on levels without a boss.
	if (TActorIterator<ABossCharacter> It(GetWorld()); It)
	{
		return;
	}
	FVector Origin = FVector(100.f, 0.f, 92.f);
	FVector Forward = FVector::ForwardVector;
	if (TActorIterator<APlayerStart> It(GetWorld()); It)
	{
		Origin = It->GetActorLocation();
		Forward = It->GetActorForwardVector().GetSafeNormal2D();
	}
	FVector BossLocation = Origin + Forward * 1200.f;
	BossLocation.Z = Origin.Z + 150.f;

	UClass* BossClass = LoadClass<ABossCharacter>(nullptr, TEXT("/Game/BossTest/BP_Sevarog.BP_Sevarog_C"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	GetWorld()->SpawnActor<ABossCharacter>(BossClass ? BossClass : ABossCharacter::StaticClass(), BossLocation, (-Forward).Rotation(), Params);
}
