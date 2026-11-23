// How far an attack's lunge carries its owner (Step 3).
#pragma once

#include "CoreMinimal.h"

// Edge-to-edge distance to the target when there is one in front. The lunge closes the gap down to
// StopDistance, never more than Cap. With no target it goes half the cap straight ahead.
float LungeDistance(TOptional<float> EdgeDistance, float Cap, float StopDistance);
