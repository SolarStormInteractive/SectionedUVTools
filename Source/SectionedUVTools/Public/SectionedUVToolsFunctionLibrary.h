// Solar Storm Interaction, 2022

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "SectionedUVToolsFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class SECTIONEDUVTOOLS_API USectionedUVToolsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	// Merges material slots of a skeletal mesh into ;
	UFUNCTION(BlueprintCallable, Category = "Mesh Merging|Skeletal Mesh")
	static USkeletalMesh* MergeSkeletalMeshMaterialSlots(USkeletalMesh* skeletalMesh,
														 const FString& newPackagePath,
														 const TArray<int32>& materialSlots);
};
