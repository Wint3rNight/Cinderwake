// Step 4d: Feng Mao's attack assets as built. The rule tests fake the chain flag, so this checks the real
// chain points: each comes after its own hit window, and chaining never frees you earlier than finishing.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Combat/AttackDefinition.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Particles/ParticleSystem.h"
#include "Boss/BossCharacter.h"

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


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackDataBossAttacks, "BossTest.Data.BossAttacks", AttackDataTestFlags)
bool FAttackDataBossAttacks::RunTest(const FString& Parameters)
{
	// Step 5: his attacks as built. Clips on his skeleton with markers inside them, timings valid with no slow-motion
	// warnings, sockets that exist (a misspelt one sweeps his feet), full armour while every blockable swing is active
	// (so a perfect guard's poise can't flinch him out of it), and section 5's numbers.
	auto Load = [](const TCHAR* Name)
	{
		return LoadObject<UAttackDefinition>(nullptr, *FString::Printf(TEXT("/Game/BossTest/Attacks/Boss/%s.%s"), Name, Name));
	};
	const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Meshes/Sevarog.Sevarog"));
	if (!TestNotNull(TEXT("his mesh"), Mesh))
	{
		return false;
	}
	const USkeleton* Skeleton = Mesh->GetSkeleton();
	auto HasPoint = [Mesh](FName N) { return N.IsNone() || Mesh->FindSocket(N) || Mesh->GetRefSkeleton().FindBoneIndex(N) != INDEX_NONE; };
	struct FExpect { const TCHAR* Name; float Damage; bool bKnockdown; };
	const FExpect All[] = {
		{ TEXT("DA_ExecutionersSwing"), 35.f, false }, { TEXT("DA_DelayedExecution"), 40.f, false }, { TEXT("DA_ReverseSweep"), 35.f, false },
		{ TEXT("DA_CrushingOverhead"), 55.f, true }, { TEXT("DA_DoubleExecution1"), 35.f, false }, { TEXT("DA_DoubleExecutionStop"), 35.f, false },
		{ TEXT("DA_DoubleExecution2Fast"), 40.f, false }, { TEXT("DA_DoubleExecution2Delayed"), 40.f, false }, { TEXT("DA_PunishmentSweep"), 40.f, false },
		{ TEXT("DA_JudgmentSlam"), 60.f, true }, { TEXT("DA_SoulMark"), 0.f, false }, { TEXT("DA_AdvanceDash"), 0.f, false }, { TEXT("DA_AdvanceSwing"), 40.f, false },
	};
	for (const FExpect& E : All)
	{
		const UAttackDefinition* A = Load(E.Name);
		if (!TestNotNull(FString::Printf(TEXT("%s exists"), E.Name), A) || !TestNotNull(FString::Printf(TEXT("%s has its clip"), E.Name), A->Clip.Get()))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: on his skeleton"), E.Name), A->Clip->GetSkeleton() == Skeleton);
		TestTrue(FString::Printf(TEXT("%s: its clip end fits the clip"), E.Name), A->ClipEnd <= A->Clip->GetPlayLength() + 0.01f);
		if (A->LeadInClip)
		{
			TestTrue(FString::Printf(TEXT("%s: the lead-in fits its clip, on his skeleton"), E.Name),
				A->LeadInTo <= A->LeadInClip->GetPlayLength() + 0.01f && A->LeadInClip->GetSkeleton() == Skeleton);
		}
		if (A->TailClip)
		{
			TestTrue(FString::Printf(TEXT("%s: the tail fits its clip, on his skeleton"), E.Name),
				A->TailTo <= A->TailClip->GetPlayLength() + 0.01f && A->TailClip->GetSkeleton() == Skeleton);
		}
		const TArray<FString> Messages = ValidateTiming(A->GetTiming());
		TestEqual(FString::Printf(TEXT("%s: timing valid, no warnings (%s)"), E.Name, *FString::Join(Messages, TEXT(" "))), Messages.Num(), 0);
		bool bPoints = HasPoint(A->SocketStart) && HasPoint(A->SocketEnd) && HasPoint(A->DeliverySocket);
		for (const FAttackCue& Cue : A->Cues)
		{
			// A one-shot must really end by itself, or it would pile up on his mesh.
			bPoints &= HasPoint(Cue.Socket) && Cue.Effect != nullptr && !(Cue.bOneShot && Cue.Effect->IsLooping());
		}
		TestTrue(FString::Printf(TEXT("%s: every socket exists, every cue has its effect, no one-shot loops"), E.Name), bPoints);
		if (!A->SocketStart.IsNone() && !A->bUnblockable)
		{
			TestTrue(FString::Printf(TEXT("%s: full armour while active"), E.Name), A->ActiveArmor == EAttackArmor::Full);
		}
		TestEqual(FString::Printf(TEXT("%s: damage"), E.Name), A->Damage, E.Damage);
		TestEqual(FString::Printf(TEXT("%s: knockdown"), E.Name), A->bKnockdown, E.bKnockdown);
		if (A->Delivery != EAttackDelivery::None)
		{
			TestTrue(FString::Printf(TEXT("%s: the delivery launches inside the active window"), E.Name),
				A->DeliveryClipAt >= A->ClipActiveStart - 0.001f && A->DeliveryClipAt <= A->ClipActiveEnd + 0.001f);
		}
	}
	const UAttackDefinition* Slam = Load(TEXT("DA_JudgmentSlam"));
	TestTrue(TEXT("Judgment Slam: a 35-damage ring, 10 m/s out to 5 m"), Slam && Slam->Delivery == EAttackDelivery::Ring && Slam->DeliveryDamage == 35.f
		&& Slam->DeliverySpeed == 1000.f && Slam->DeliveryReach == 500.f);
	const UAttackDefinition* Mark = Load(TEXT("DA_SoulMark"));
	TestTrue(TEXT("Soul Mark: a 30-damage soul projectile at 14 m/s, no sweep"), Mark && Mark->Delivery == EAttackDelivery::Projectile
		&& Mark->DeliveryDamage == 30.f && Mark->DeliverySpeed == 1400.f && Mark->bDeliverySoulFragment && Mark->SocketStart.IsNone());
	const UAttackDefinition* Dash = Load(TEXT("DA_AdvanceDash"));
	TestTrue(TEXT("the Advance's dash: no sweep, aims at an interception point"), Dash && Dash->SocketStart.IsNone() && Dash->LungeAim == ELungeAim::Intercept);

	// The 0.70 s rule (spec section 5): Double Execution's second active window starts at least 0.70 s after the first,
	// at both ends of both bands.
	const UAttackDefinition* Hit1 = Load(TEXT("DA_DoubleExecution1"));
	const UAttackDefinition* Fast = Load(TEXT("DA_DoubleExecution2Fast"));
	const UAttackDefinition* Late = Load(TEXT("DA_DoubleExecution2Delayed"));
	if (Hit1 && Fast && Late)
	{
		const FAttackTiming T1 = Hit1->GetTiming();
		auto Gap = [&T1](const UAttackDefinition* Second, float Extra) { return T1.Total() + Second->GetTiming().Windup + Extra - T1.Windup; };
		TestTrue(FString::Printf(TEXT("fast band, earliest: %.2f s apart"), Gap(Fast, 0.f)), Gap(Fast, 0.f) >= 0.70f - 0.001f);
		TestTrue(TEXT("fast band, latest: 0.60 s windup"), FMath::IsNearlyEqual(Fast->GetTiming().Windup + 0.05f, 0.60f, 0.001f));
		TestTrue(FString::Printf(TEXT("delayed band, earliest: %.2f s apart"), Gap(Late, 0.f)), Gap(Late, 0.f) >= 0.70f);
		TestTrue(TEXT("delayed band: 0.95-1.15 s windup"), FMath::IsNearlyEqual(Late->GetTiming().Windup, 0.95f, 0.001f));
	}
	return true;
}

namespace
{
	const ABossCharacter* BossDefaults()
	{
		const UClass* Class = LoadClass<ABossCharacter>(nullptr, TEXT("/Game/BossTest/BP_Sevarog.BP_Sevarog_C"));
		return Class ? Class->GetDefaultObject<ABossCharacter>() : nullptr;
	}

	const FBossAction* FindAction(const ABossCharacter* Boss, FName Name)
	{
		return Boss->Actions.FindByPredicate([Name](const FBossAction& A) { return A.Name == Name; });
	}

	// From the action's start to its hitting active start, through its first variant's steps (the Advance: the dash's
	// whole length, then the swing's windup), with an extra hold on its last step.
	float ActiveStartOf(const FBossAction& A, float ExtraHold)
	{
		const TArray<TObjectPtr<UAttackDefinition>>& Steps = A.Variants[0].Steps;
		float T = 0.f;
		for (int32 i = 0; i + 1 < Steps.Num(); ++i)
		{
			T += Steps[i]->GetTiming().Total();
		}
		return T + Steps.Last()->GetTiming().Windup + ExtraHold;
	}

	const FAttackTiming LastStepTiming(const FBossAction& A)
	{
		return A.Variants[0].Steps.Last()->GetTiming();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackDataBossCombos, "BossTest.Data.BossCombos", AttackDataTestFlags)
bool FAttackDataBossCombos::RunTest(const FString& Parameters)
{
	// Step 6: section 5's combos as built, and the punish-window rules (DECISIONS.md): every follow-up active at least
	// 1.40 s after the opener's active start (one dodge light is always safe), Combo F's swing 0.70 s after the grasp
	// arrives at its range, chain points inside the recovery, and every follow-up able to track 90 deg (validated there).
	const ABossCharacter* Boss = BossDefaults();
	if (!TestNotNull(TEXT("BP_Sevarog"), Boss))
	{
		return false;
	}
	struct FExpect { const TCHAR* Name; const TCHAR* Opener; TArray<const TCHAR*> FollowUps; };
	const FExpect Expected[] = {
		{ TEXT("A"), TEXT("ExecutionersSwing"), { TEXT("PunishmentSweep") } },
		{ TEXT("B"), TEXT("ExecutionersSwing"), { TEXT("DelayedExecution") } },
		{ TEXT("C"), TEXT("ReverseSweep"), { TEXT("CrushingOverhead") } },
		{ TEXT("D"), TEXT("CrushingOverhead"), { TEXT("DelayedExecution") } },
		{ TEXT("E"), TEXT("ExecutionersSwing"), { TEXT("ExecutionersSwing"), TEXT("JudgmentSlam") } },
		{ TEXT("F"), TEXT("SoulMark"), { TEXT("ExecutionersAdvance") } },
	};
	TestEqual(TEXT("six combos"), Boss->Combos.Num(), 6);
	for (const FExpect& E : Expected)
	{
		const FBossCombo* C = Boss->Combos.FindByPredicate([&E](const FBossCombo& X) { return X.Name == FName(E.Name); });
		if (!TestNotNull(FString::Printf(TEXT("combo %s"), E.Name), C) || !TestEqual(FString::Printf(TEXT("%s: its opener"), E.Name), C->Opener, FName(E.Opener))
			|| !TestEqual(FString::Printf(TEXT("%s: its follow-ups"), E.Name), C->FollowUps.Num(), E.FollowUps.Num()))
		{
			continue;
		}
		const FBossAction* Prev = FindAction(Boss, C->Opener);
		for (int32 i = 0; i < C->FollowUps.Num(); ++i)
		{
			const FBossComboLink& Link = C->FollowUps[i];
			TestEqual(FString::Printf(TEXT("%s: follow-up %d"), E.Name, i + 1), Link.Action, FName(E.FollowUps[i]));
			const FBossAction* Next = FindAction(Boss, Link.Action);
			if (!TestTrue(FString::Printf(TEXT("%s: its actions exist"), E.Name), Prev && Next))
			{
				break;
			}
			const FAttackTiming Opener = LastStepTiming(*Prev);
			TestTrue(FString::Printf(TEXT("%s: chain point %.2f inside the %.2f s recovery"), E.Name, Link.ChainIntoRecovery, Opener.Recovery),
				Link.ChainIntoRecovery >= 0.f && Link.ChainIntoRecovery < Opener.Recovery);
			// The follow-up's shortest timing: its hold at the low end of its variation.
			const float Gap = Opener.Active + Link.ChainIntoRecovery + ActiveStartOf(*Next, Next->Variants[0].HoldExtraMin);
			TestTrue(FString::Printf(TEXT("%s: follow-up %d goes active %.2f s after the previous one (at least 1.40)"), E.Name, i + 1, Gap), Gap >= 1.40f - 0.001f);
			const UAttackDefinition* First = Next->Variants[0].Steps[0];
			const float Turn = First->TrackingRate * FMath::Max(0.f, First->GetTiming().Windup - First->TrackingStopsBeforeActive);
			TestTrue(FString::Printf(TEXT("%s: follow-up %d tracks %.0f deg before its cutoff (at least 90)"), E.Name, i + 1, Turn), Turn >= 90.f);
			Prev = Next;
		}
	}
	// Combo F: the Advance's swing at least 0.70 s after the grasp reaches the Advance's 11.5 m (the dodge lock plus a reaction).
	const FBossAction* Mark = FindAction(Boss, TEXT("SoulMark"));
	const FBossAction* Advance = FindAction(Boss, TEXT("ExecutionersAdvance"));
	const FBossCombo* F = Boss->Combos.FindByPredicate([](const FBossCombo& X) { return X.Name == FName(TEXT("F")); });
	if (Mark && Advance && F && F->FollowUps.Num() == 1)
	{
		const UAttackDefinition* Cast = Mark->Variants[0].Steps[0];
		const FAttackTiming T = Cast->GetTiming();
		const float Arrival = T.Windup + Advance->MaxRange / Cast->DeliverySpeed;
		const float Strike = T.Windup + T.Active + F->FollowUps[0].ChainIntoRecovery + ActiveStartOf(*Advance, 0.f);
		TestTrue(FString::Printf(TEXT("F: the swing %.2f s after the grasp arrives (at least 0.70)"), Strike - Arrival), Strike - Arrival >= 0.70f - 0.001f);
	}
	// Delayed Execution keeps its +-0.10 s as a follow-up (B and D), carried per step.
	const FBossAction* Delayed = FindAction(Boss, TEXT("DelayedExecution"));
	TestTrue(TEXT("Delayed Execution varies +-0.10 s"), Delayed && FMath::IsNearlyEqual(Delayed->Variants[0].HoldExtraMin, -0.10f) && FMath::IsNearlyEqual(Delayed->Variants[0].HoldExtraMax, 0.10f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackDataPunishLadder, "BossTest.Data.PunishLadder", AttackDataTestFlags)
bool FAttackDataPunishLadder::RunTest(const FString& Parameters)
{
	// The council's ladder (DECISIONS.md, the punish windows): after a clean dodge (t = 0 at his active start), each
	// opening's listed answer frees you at least 0.20 s before his fastest next attack can land: his active window and
	// recovery, the 0.25 s pause floor (a hit can't cut it shorter), then Reverse Sweep's 0.72 s windup. The answers'
	// free times are the council's, composed from the player's attacks.
	const ABossCharacter* Boss = BossDefaults();
	if (!TestNotNull(TEXT("BP_Sevarog"), Boss))
	{
		return false;
	}
	struct FRung { const TCHAR* Action; const TCHAR* Answer; float Free; };
	const FRung Ladder[] = {
		{ TEXT("ExecutionersSwing"), TEXT("dodge light and finisher"), 1.59f },
		{ TEXT("ExecutionersAdvance"), TEXT("dodge light and finisher"), 1.59f },
		{ TEXT("ReverseSweep"), TEXT("three hits"), 1.93f },
		{ TEXT("PunishmentSweep"), TEXT("three hits"), 1.93f },
		{ TEXT("DelayedExecution"), TEXT("the dodge slam"), 2.06f },
		{ TEXT("CrushingOverhead"), TEXT("slam, stand-up Light 1, Light 2"), 2.62f },
		{ TEXT("JudgmentSlam"), TEXT("dodge light, charged heavy, Light 1"), 2.83f },
	};
	const float PauseFloor = 0.25f;
	const float FastestOpener = 0.72f;
	for (const FRung& R : Ladder)
	{
		const FBossAction* A = FindAction(Boss, R.Action);
		if (!TestNotNull(FString::Printf(TEXT("%s exists"), R.Action), A))
		{
			continue;
		}
		const FAttackTiming T = LastStepTiming(*A);
		const float NextHit = T.Active + T.Recovery + PauseFloor + FastestOpener;
		TestTrue(FString::Printf(TEXT("%s: %s frees you at %.2f, his next hit at %.2f (0.20 s to spare)"), R.Action, R.Answer, R.Free, NextHit),
			R.Free + 0.20f <= NextHit + 0.001f);
	}
	return true;
}

#endif
