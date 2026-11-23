// Step 4d: Feng Mao's attack assets as built. The rule tests fake the chain flag, so this checks the real
// chain points: each comes after its own hit window, and chaining never frees you earlier than finishing.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Combat/AttackDefinition.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"

namespace
{
	constexpr EAutomationTestFlags AttackDataTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const UAttackDefinition* LoadAttack(const TCHAR* Name)
	{
		return LoadObject<UAttackDefinition>(nullptr, *FString::Printf(TEXT("/Game/BossTest/Attacks/Player/%s.%s"), Name, Name));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackDataChainPoints, "BossTest.Data.ChainPoints", AttackDataTestFlags)
bool FAttackDataChainPoints::RunTest(const FString& Parameters)
{
	// Each attack, the light that follows it in its line, its chain point, and the heavy that follows it (Step 4e:
	// heavy 2 after heavy 1, the charged heavy and the slam; heavy 1 after everything else).
	struct FEdge { const TCHAR* Attack; const TCHAR* NextLight; const TCHAR* ChainFrom; const TCHAR* NextHeavy = TEXT("DA_Heavy"); };
	const FEdge Edges[] = {
		{ TEXT("DA_Light1"), TEXT("DA_Light2"), TEXT("0.38") },
		{ TEXT("DA_Light2"), TEXT("DA_Light3"), TEXT("0.28") },
		{ TEXT("DA_Light3"), TEXT("DA_Light4"), TEXT("0.34") },
		{ TEXT("DA_Light4"), TEXT("DA_Light1"), TEXT("0.45") },
		{ TEXT("DA_SprintAttack"), TEXT("DA_Light2"), TEXT("0.55") },
		{ TEXT("DA_DodgeLight1"), TEXT("DA_DodgeLight2"), TEXT("0.32") },
		{ TEXT("DA_DodgeLight2"), TEXT("DA_Light2"), TEXT("0.55") },
		{ TEXT("DA_Heavy"), TEXT("DA_Light1"), TEXT("0.95"), TEXT("DA_Heavy2") },
		{ TEXT("DA_Heavy2"), TEXT("DA_Light1"), TEXT("0.95") },
		{ TEXT("DA_DodgeHeavy"), TEXT("DA_Light1Rise"), TEXT("1.05"), TEXT("DA_Heavy2") },
		{ TEXT("DA_Light1Rise"), TEXT("DA_Light2"), TEXT("0.51") },
	};
	const UAttackDefinition* Heavy = LoadAttack(TEXT("DA_Heavy"));
	if (!TestNotNull(TEXT("DA_Heavy exists"), Heavy))
	{
		return false;
	}
	for (const FEdge& E : Edges)
	{
		const UAttackDefinition* A = LoadAttack(E.Attack);
		const UAttackDefinition* Next = LoadAttack(E.NextLight);
		if (!TestNotNull(FString::Printf(TEXT("%s exists"), E.Attack), A) || !TestNotNull(FString::Printf(TEXT("%s exists"), E.NextLight), Next))
		{
			continue;
		}
		const FAttackTiming T = A->GetTiming();
		TestEqual(FString::Printf(TEXT("%s: chain point %s s"), E.Attack, E.ChainFrom), T.ChainFrom, FCString::Atof(E.ChainFrom), 0.005f);
		TestTrue(FString::Printf(TEXT("%s: the chain point (%.2f) comes after its own hit window (%.2f)"), E.Attack, T.ChainFrom, T.Windup + T.Active),
			T.ChainFrom > T.Windup + T.Active);
		TestTrue(FString::Printf(TEXT("%s: chaining into %s never frees you earlier"), E.Attack, E.NextLight), T.ChainFrom + Next->GetTiming().Total() > T.Total());
		const UAttackDefinition* NextHeavy = LoadAttack(E.NextHeavy);
		TestTrue(FString::Printf(TEXT("%s: nor does chaining into %s"), E.Attack, E.NextHeavy), NextHeavy && T.ChainFrom + NextHeavy->GetTiming().Total() > T.Total());
		TestTrue(FString::Printf(TEXT("%s: the chain point comes before free, or a held attack could never fire"), E.Attack), T.ChainFrom < T.Total());
	}

	// Every player attack has its clip, on Feng Mao's skeleton, and its markers fit the clip. Borrowed clips live in the
	// git-ignored /Game/Retargeted (Scripts/build_borrowed_clips.py); without them this fails by name. (A blend-in
	// longer than the windup is allowed: Light 2 has always started mid-blend, and the sweep follows the real pose.)
	const UAttackDefinition* Reference = LoadAttack(TEXT("DA_Light1"));
	const USkeleton* Skeleton = Reference && Reference->Clip ? Reference->Clip->GetSkeleton() : nullptr;
	for (const TCHAR* Name : { TEXT("DA_Light1"), TEXT("DA_Light2"), TEXT("DA_Light3"), TEXT("DA_Light4"), TEXT("DA_Heavy"), TEXT("DA_Heavy2"),
		TEXT("DA_ChargedHeavy"), TEXT("DA_DodgeLight1"), TEXT("DA_DodgeLight2"), TEXT("DA_DodgeHeavy"), TEXT("DA_SprintAttack"), TEXT("DA_Light1Rise") })
	{
		const UAttackDefinition* A = LoadAttack(Name);
		if (!TestNotNull(FString::Printf(TEXT("%s exists"), Name), A) || !TestNotNull(FString::Printf(TEXT("%s has its clip"), Name), A->Clip.Get()))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: on Feng Mao's skeleton"), Name), A->Clip->GetSkeleton() == Skeleton);
		TestTrue(FString::Printf(TEXT("%s: its clip end (%.2f) fits the clip (%.2f)"), Name, A->ClipEnd, A->Clip->GetPlayLength()), A->ClipEnd <= A->Clip->GetPlayLength() + 0.01f);
	}
	const UAttackDefinition* Heavy2 = LoadAttack(TEXT("DA_Heavy2"));
	TestTrue(TEXT("DA_Heavy2 has no branch (it never charges)"), Heavy2 && Heavy2->BranchAt < 0.f && !Heavy2->BranchVariant);

	// The charged heavy restarts its own timeline at the heavy's branch, so check it from the press.
	const UAttackDefinition* Charged = LoadAttack(TEXT("DA_ChargedHeavy"));
	const UAttackDefinition* Light1 = LoadAttack(TEXT("DA_Light1"));
	if (TestNotNull(TEXT("DA_ChargedHeavy exists"), Charged) && TestNotNull(TEXT("DA_Light1 exists"), Light1))
	{
		const float At = Heavy->BranchAt;
		const FAttackTiming C = Charged->GetTiming();
		TestEqual(TEXT("charged heavy: decided 0.15 s after the press"), At, 0.15f, 0.005f);
		TestEqual(TEXT("charged heavy: the strike 0.88 s after the press"), At + C.Windup, 0.88f, 0.005f);
		TestEqual(TEXT("charged heavy: its chain point 1.31 s after the press"), At + C.ChainFrom, 1.31f, 0.005f);
		TestEqual(TEXT("charged heavy: free 1.90 s after the press"), At + C.Total(), 1.90f, 0.005f);
		TestTrue(TEXT("charged heavy: the chain point after its hit and before free"), C.ChainFrom > C.Windup + C.Active && C.ChainFrom < C.Total());
		TestTrue(TEXT("charged heavy: chaining into Light 1 never frees you earlier"), C.ChainFrom + Light1->GetTiming().Total() > C.Total());
		// After the 4e play: the switch and the gather looked weird. The heavy barely moves before the decision, so a
		// charge has no thrust to switch away from, and the gather creeps instead of freezing on a pose.
		const FAttackTiming H = Heavy->GetTiming();
		TestTrue(TEXT("heavy: still creeping in its hold at the branch"), EvaluateAttack(H, At - 0.01f).Phase == EAttackPhase::Hold);
		TestTrue(FString::Printf(TEXT("heavy: at most 0.05 s into its clip at the branch (%.3f)"), EvaluateAttack(H, At).ClipTime), EvaluateAttack(H, At).ClipTime <= 0.05f);
		TestTrue(TEXT("charged heavy: the gather creeps (never a frozen pose)"), C.ClipHoldTo > C.ClipHoldAt && !C.bEaseIntoHold);
	}
	return true;
}

#endif
