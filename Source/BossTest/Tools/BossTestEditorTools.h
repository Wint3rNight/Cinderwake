// Editor-only helpers for the Python tools in Docs/tools: engine functions that aren't exposed to Python.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BossTestEditorTools.generated.h"

class UPhysicsAsset;
class USkeletalMesh;

UCLASS()
class BOSSTEST_API UBossTestEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	// Generates a ragdoll physics asset (bodies and joints, the engine's default settings) from a mesh into a new
	// package, without the dialog and without touching the mesh. Save it from Python afterwards.
	UFUNCTION(BlueprintCallable, Category = "BossTest|Editor")
	static UPhysicsAsset* CreateRagdollPhysicsAsset(USkeletalMesh* Mesh, const FString& PackagePath, const FString& AssetName);
#endif
};
