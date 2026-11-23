// Attack tracking (spec section 5): the attacker turns toward the target at the attack's rate until a
// set time before the active window, then commits.
#pragma once

#include "CoreMinimal.h"
#include "Combat/AttackTimeline.h"

// One step of turning toward TargetYaw, the short way round, never past it.
float TrackingYawStep(float CurrentYaw, float TargetYaw, float RateDegPerSec, float Dt);

// Tracking runs while Time < Windup - StopsBeforeActive.
bool IsTrackingAt(const FAttackTiming& T, float StopsBeforeActive, float Time);
