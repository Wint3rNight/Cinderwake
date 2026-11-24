// Step 4's placeholder brain, kept only as the fallback when he has no actions (Executioner's Swing alone). His real
// decisions come from FBossDirector (Step 6). Plain C++, so it's unit tested.
#pragma once

#include "CoreMinimal.h"

enum class EBossChoice : uint8 { Wait, Turn, Swing, Approach };

struct FBossPlaceholderTuning
{
	float SwingRange = 430.f;     // cm, edge to edge
	float MeleeAngle = 90.f;      // in front or to his side
};

EBossChoice ChoosePlaceholderAction(float EdgeDistance, float AngleDeg, bool bPlayerDown, const FBossPlaceholderTuning& Tuning = FBossPlaceholderTuning());
