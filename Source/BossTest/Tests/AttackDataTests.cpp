// Step 4d: Feng Mao's attack assets as built. The rule tests fake the chain flag, so this checks the real
// chain points: each comes after its own hit window, and chaining never frees you earlier than finishing.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Combat/AttackDefinition.h"

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
	// Each attack, the light that follows it in its line, and whether a heavy can follow (every chain point allows one).
	struct FEdge { const TCHAR* Attack; const TCHAR* NextLight; const TCHAR* ChainFrom; };
	const FEdge Edges[] = {
		{ TEXT("DA_Light1"), TEXT("DA_Light2"), TEXT("0.38") },
		{ TEXT("DA_Light2"), TEXT("DA_Light3"), TEXT("0.28") },
		{ TEXT("DA_Light3"), TEXT("DA_Light4"), TEXT("0.32") },
		{ TEXT("DA_Light4"), TEXT("DA_Light1"), TEXT("0.45") },
		{ TEXT("DA_SprintAttack"), TEXT("DA_Light2"), TEXT("0.55") },
		{ TEXT("DA_DodgeLight1"), TEXT("DA_DodgeLight2"), TEXT("0.32") },
		{ TEXT("DA_DodgeLight2"), TEXT("DA_Light2"), TEXT("0.55") },
		{ TEXT("DA_Heavy"), TEXT("DA_Light1"), TEXT("0.95") },
		{ TEXT("DA_DodgeHeavy"), TEXT("DA_Light1Rise"), TEXT("1.05") },
		{ TEXT("DA_Light1Rise"), TEXT("DA_Light2"), TEXT("0.51") },
	};
	const UAttackDefinition* Heavy = LoadAttack(TEXT("DA_Heavy"));
	if (!TestNotNull(TEXT("DA_Heavy exists"), Heavy))
	{
		return false;
	}
	const float HeavyTotal = Heavy->GetTiming().Total();
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
		TestTrue(FString::Printf(TEXT("%s: nor does chaining into the heavy"), E.Attack), T.ChainFrom + HeavyTotal > T.Total());
		TestTrue(FString::Printf(TEXT("%s: the chain point comes before free, or a held attack could never fire"), E.Attack), T.ChainFrom < T.Total());
	}

	// The charged heavy restarts its own timeline at the heavy's branch, so check it from the press.
	const UAttackDefinition* Charged = LoadAttack(TEXT("DA_ChargedHeavy"));
	const UAttackDefinition* Light1 = LoadAttack(TEXT("DA_Light1"));
	if (TestNotNull(TEXT("DA_ChargedHeavy exists"), Charged) && TestNotNull(TEXT("DA_Light1 exists"), Light1))
	{
		const float At = Heavy->BranchAt;
		const FAttackTiming C = Charged->GetTiming();
		TestEqual(TEXT("charged heavy: the strike 1.14 s after the press"), At + C.Windup, 1.14f, 0.005f);
		TestEqual(TEXT("charged heavy: its chain point 1.56 s after the press"), At + C.ChainFrom, 1.56f, 0.005f);
		TestEqual(TEXT("charged heavy: free 2.16 s after the press"), At + C.Total(), 2.16f, 0.005f);
		TestTrue(TEXT("charged heavy: the chain point after its hit and before free"), C.ChainFrom > C.Windup + C.Active && C.ChainFrom < C.Total());
		TestTrue(TEXT("charged heavy: chaining into Light 1 never frees you earlier"), C.ChainFrom + Light1->GetTiming().Total() > C.Total());
	}
	return true;
}

#endif
