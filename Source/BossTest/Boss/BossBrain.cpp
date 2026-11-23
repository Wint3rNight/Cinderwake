#include "Boss/BossBrain.h"

EBossChoice ChoosePlaceholderAction(float EdgeDistance, float AngleDeg, bool bPlayerDown, const FBossPlaceholderTuning& Tuning)
{
	if (bPlayerDown)
	{
		return EBossChoice::Wait; // he doesn't commit to an attack while you're knocked down (spec section 10)
	}
	if (AngleDeg > Tuning.MeleeAngle)
	{
		return EBossChoice::Turn;
	}
	return EdgeDistance <= Tuning.SwingRange ? EBossChoice::Swing : EBossChoice::Approach;
}
