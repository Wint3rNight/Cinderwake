#include "Boss/BossBrain.h"

EBossChoice ChoosePlaceholderAction(float EdgeDistance, float AngleDeg, bool bPlayerDown, const FBossPlaceholderTuning& Tuning)
{
	if (bPlayerDown)
	{
		return EBossChoice::Wait;
	}
	if (AngleDeg > Tuning.MeleeAngle)
	{
		return EBossChoice::Turn;
	}
	return EdgeDistance <= Tuning.SwingRange ? EBossChoice::Swing : EBossChoice::Approach;
}
