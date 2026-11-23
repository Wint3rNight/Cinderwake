#include "Tools/BossTestEditorTools.h"

#if WITH_EDITOR
#include "AnimationModifier.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationSettings.h"
#include "Animation/MirrorDataTable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/Package.h"

UPhysicsAsset* UBossTestEditorTools::CreateRagdollPhysicsAsset(USkeletalMesh* Mesh, const FString& PackagePath, const FString& AssetName)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UPackage* Package = CreatePackage(*(PackagePath / AssetName));
	UPhysicsAsset* Asset = NewObject<UPhysicsAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	const FPhysAssetCreateParams Params; // capsules on the bones big enough to matter, joints between them
	FText Error;
	if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(Asset, Mesh, Params, Error, /*bSetToMesh*/ false, /*bShowProgress*/ false))
	{
		UE_LOG(LogTemp, Error, TEXT("BossTest: couldn't build a physics asset from %s: %s"), *Mesh->GetName(), *Error.ToString());
		return nullptr;
	}
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();
	return Asset;
}

UMirrorDataTable* UBossTestEditorTools::CreateMirrorTable(USkeleton* Skeleton, const FString& PackagePath, const FString& AssetName)
{
	if (!Skeleton)
	{
		return nullptr;
	}
	UPackage* Package = CreatePackage(*(PackagePath / AssetName));
	UMirrorDataTable* Table = NewObject<UMirrorDataTable>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	Table->RowStruct = FMirrorTableRow::StaticStruct();
	Table->Skeleton = Skeleton;
	Table->MirrorAxis = EAxis::X;
	Table->MirrorFindReplaceExpressions = GetDefault<UAnimationSettings>()->MirrorFindReplaceExpressions;
	Table->UpdateFromFindReplaceExpressions(UMirrorDataTable::FFindReplaceOptions::Sync());
	FAssetRegistryModule::AssetCreated(Table);
	Package->MarkPackageDirty();
	return Table;
}

bool UBossTestEditorTools::MirrorAnimation(UAnimSequence* Sequence, UMirrorDataTable* Table)
{
	if (!Sequence || !Table)
	{
		return false;
	}
	// The modifier's class isn't exported from its plugin, so it's found and configured through reflection.
	UClass* ModifierClass = LoadClass<UAnimationModifier>(nullptr, TEXT("/Script/AnimationModifierLibrary.MirrorModifier"));
	FObjectProperty* TableProperty = ModifierClass ? FindFProperty<FObjectProperty>(ModifierClass, TEXT("MirrorDataTable")) : nullptr;
	if (!TableProperty)
	{
		UE_LOG(LogTemp, Error, TEXT("BossTest: the AnimationModifierLibrary plugin's MirrorModifier isn't available"));
		return false;
	}
	UAnimationModifier* Modifier = NewObject<UAnimationModifier>(GetTransientPackage(), ModifierClass);
	TableProperty->SetObjectPropertyValue_InContainer(Modifier, Table);
	// Headless there's no one to answer the warning dialog, so a warning reverts the mirror; report whether it stuck.
	UE::Anim::FApplyModifiersScope Scope(UE::Anim::FApplyModifiersScope::RevertAtWarning);
	Modifier->ApplyToAnimationSequence(Sequence);
	const bool bApplied = Modifier->CanRevert(Sequence);
	if (bApplied)
	{
		Sequence->MarkPackageDirty();
	}
	return bApplied;
}
#endif
