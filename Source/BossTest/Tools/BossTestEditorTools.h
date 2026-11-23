// Editor-only helpers for the Python tools in Docs/tools: engine functions that aren't exposed to Python.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BossTestEditorTools.generated.h"

class UAnimSequence;
class UMirrorDataTable;
class UPhysicsAsset;
class USkeletalMesh;
class USkeleton;

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

	// A mirror table for a skeleton, filled from the project's left/right naming rules (the editor's factory does
	// the same behind a dialog). Mirrors across X, the side axis of the Paragon characters.
	UFUNCTION(BlueprintCallable, Category = "BossTest|Editor")
	static UMirrorDataTable* CreateMirrorTable(USkeleton* Skeleton, const FString& PackagePath, const FString& AssetName);

	// Mirrors a clip in place with the engine's mirror modifier (Step 4e: right-handed borrowed swings onto Feng
	// Mao's left hand). Applying a modifier isn't exposed to Python.
	UFUNCTION(BlueprintCallable, Category = "BossTest|Editor")
	static bool MirrorAnimation(UAnimSequence* Sequence, UMirrorDataTable* Table);
#endif
};
