#include "Tools/BossTestEditorTools.h"

#if WITH_EDITOR
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
#endif
