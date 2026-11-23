#include "Player/PlayerMovementComponent.h"

void UPlayerMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// The move has used it already, and the next move recomputes it from input before using it. The
	// mesh (and its animation blueprint) ticks after this component, so it reads zero.
	if (bHideAccelerationFromAnimation)
	{
		Acceleration = FVector::ZeroVector;
	}
}
