// How finely a weapon sweep samples space, so fast swings can't pass through a target between frames.
#pragma once

#include "CoreMinimal.h"

// Sub-steps between last frame's blade and this frame's: ceil(travel / radius), between 1 and 16.
int32 SweepSubsteps(float MaxEndpointTravel, float Radius);

// Spheres along the blade: ceil(length / (1.5 x radius)), between 2 and 8.
int32 BladeSampleCount(float BladeLength, float Radius);
