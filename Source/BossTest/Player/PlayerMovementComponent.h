// Feng Mao's movement component. Only one addition: while sprinting it hides his acceleration from
// the animation blueprint, which plays the montage slot full body only when he isn't accelerating.
// That lets the sprint loop play over his whole body while movement stays ordinary input movement.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlayerMovementComponent.generated.h"

UCLASS()
class BOSSTEST_API UPlayerMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool bHideAccelerationFromAnimation = false;
};
