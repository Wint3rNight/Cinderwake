// The game mode (Step 4, replacing the spike's): Feng Mao, the debug controller, the HUD, and a boss
// spawned on levels that don't place one. The encounter lifecycle comes in Step 8.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BossTestGameMode.generated.h"

UCLASS()
class BOSSTEST_API ABossTestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABossTestGameMode();
	virtual void StartPlay() override;
};
