// The player controller: game-only input mode, the camera's pitch range, and the debug keys and text.
// The boss keys drive the spike boss until Step 4 replaces it.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BossTestPlayerController.generated.h"

class ABossCharacter;
class APlayerCharacter;

UCLASS()
class BOSSTEST_API ABossTestPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABossTestPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	void HandleDebugKeys();
	void DrawDebugText() const;
	ABossCharacter* Boss() const;

	bool bShowHelp = true;
	mutable TWeakObjectPtr<ABossCharacter> CachedBoss;
};
