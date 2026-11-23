#include "Combat/AttackDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FAttackTiming UAttackDefinition::GetTiming() const
{
	FAttackTiming T;
	T.Windup = Windup;
	T.Hold = Hold;
	T.Active = Active;
	T.Recovery = Recovery;
	T.ChainFrom = ChainFrom;
	T.ClipStartAt = ClipStartAt;
	T.ClipHoldAt = ClipHoldAt;
	T.ClipHoldTo = ClipHoldTo;
	T.ClipActiveStart = ClipActiveStart;
	T.ClipActiveEnd = ClipActiveEnd;
	T.ClipEnd = ClipEnd;
	T.bEaseIntoHold = bEaseIntoHold;
	return T;
}

EHitPolicy UAttackDefinition::GetHitPolicy() const
{
	switch (HitPolicy)
	{
	case EAttackHitPolicy::MultiHit: return EHitPolicy::MultiHit;
	case EAttackHitPolicy::PersistentContact: return EHitPolicy::PersistentContact;
	case EAttackHitPolicy::Projectile: return EHitPolicy::Projectile;
	default: return EHitPolicy::SingleHitPerTarget;
	}
}

#if WITH_EDITOR
EDataValidationResult UAttackDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!Clip)
	{
		Context.AddError(FText::FromString(TEXT("No clip set.")));
		Result = EDataValidationResult::Invalid;
	}
	if (BranchAt >= 0.f)
	{
		const FAttackTiming Variant = BranchVariant ? BranchVariant->GetTiming() : FAttackTiming();
		for (const FString& Message : ValidateBranch(GetTiming(), Variant, BranchVariant && BranchVariant->Clip == Clip, BranchAt))
		{
			Context.AddError(FText::FromString(Message));
			Result = EDataValidationResult::Invalid;
		}
	}
	for (const FString& Message : ValidateTiming(GetTiming()))
	{
		if (Message.StartsWith(TEXT("Error:")))
		{
			Context.AddError(FText::FromString(Message));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			Context.AddWarning(FText::FromString(Message));
		}
	}
	return Result;
}
#endif
