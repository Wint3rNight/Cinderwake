// The feel pass after Step 7: when a fighter's voice line may play.
#include "Misc/AutomationTest.h"
#include "Combat/FighterVoice.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags VoiceTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	bool VoiceAllows(FVoiceGate& Gate, EVoiceEvent E, double Now, float Length = 0.5f, bool* bCut = nullptr)
	{
		bool bInterrupt = false;
		const bool bOk = Gate.Allow(E, Now, bInterrupt);
		if (bOk)
		{
			Gate.Note(E, Now, Length);
		}
		if (bCut)
		{
			*bCut = bInterrupt;
		}
		return bOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoiceGateRules, "BossTest.Combat.Voice.LinesDontStack", VoiceTestFlags)
bool FVoiceGateRules::RunTest(const FString& Parameters)
{
	// A line plays out unless something more important comes (death over pain over efforts); a short gap between lines and a
	// longer one between pains, so a flurry of hits isn't a stutter of grunts.
	FVoiceGate Gate;
	TestTrue(TEXT("an effort plays"), VoiceAllows(Gate, EVoiceEvent::Effort, 0.0));
	TestFalse(TEXT("another effort while it plays: no"), VoiceAllows(Gate, EVoiceEvent::Effort, 0.2));
	bool bCut = false;
	TestTrue(TEXT("a pain while it plays: yes..."), VoiceAllows(Gate, EVoiceEvent::Pain, 0.3, 0.5f, &bCut));
	TestTrue(TEXT("...cutting the effort short"), bCut);
	TestFalse(TEXT("a second pain 0.3 s later: no (the pain gap)"), VoiceAllows(Gate, EVoiceEvent::Pain, 0.6));
	TestTrue(TEXT("death always: it cuts the pain"), VoiceAllows(Gate, EVoiceEvent::Death, 0.65, 1.f, &bCut) && bCut);
	FVoiceGate Quiet;
	VoiceAllows(Quiet, EVoiceEvent::Effort, 0.0, 0.2f);
	TestFalse(TEXT("just after a line ends: the short gap still holds"), VoiceAllows(Quiet, EVoiceEvent::Effort, 0.3));
	TestTrue(TEXT("0.35 s after it started and over: fine"), VoiceAllows(Quiet, EVoiceEvent::Effort, 0.36));
	VoiceAllows(Quiet, EVoiceEvent::Pain, 2.0, 0.2f);
	TestTrue(TEXT("a pain 0.6 s after the last one: fine"), VoiceAllows(Quiet, EVoiceEvent::Pain, 2.61));
	Quiet.Reset();
	TestTrue(TEXT("a reset forgets everything"), VoiceAllows(Quiet, EVoiceEvent::Pain, 2.62));
	return true;
}

#endif
