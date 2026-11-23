// The placeholder brain for Step 4: just enough to fight with until Step 6 builds the real one
// (bands, scoring, combos, circling). Plain C++, so it's unit tested.
#pragma once

#include "CoreMinimal.h"

enum class EBossChoice : uint8 { Wait, Turn, Swing, Approach };

struct FBossPlaceholderTuning
{
	float SwingRange = 430.f;     // cm, edge to edge: Executioner's Swing's 2.5 m reach + 1.8 m lunge
	float MeleeAngle = 90.f;      // melee is valid in front or to his side (spec section 5)
};

EBossChoice ChoosePlaceholderAction(float EdgeDistance, float AngleDeg, bool bPlayerDown, const FBossPlaceholderTuning& Tuning = FBossPlaceholderTuning());
