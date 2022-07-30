// Copyright (c) 2022 Solar Storm Interactive

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "SectionedUVToolsFunctionLibrary.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSectionedUVTools, Log, All);

/**
 * 
 */
UCLASS()
class SECTIONEDUVTOOLS_API USectionedUVToolsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	/**
	 * Creates a sectioned UV for the passed in skeletal mesh and condenses the desired material slots into 1
	 * Pass in and empty array for material slots to condense them all.
	 * @param skeletalMesh The skeletal mesh to create a new sectioned mesh from. The new mesh will be suffixed with "_sectioned".
	 * @param materialSlots The material slots to condense into a single slot which should use the sectioned UV material.
	 * @param numSections The number of horizonal sections.
	 * @return The created skeletal mesh, or None if the function failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sectioned UV", meta=(AdvancedDisplay="numSections"), DisplayName="Create Sectioned UV Skeletal Mesh")
	static class USkeletalMesh* CreateSectionedUVSkeletalMesh(class USkeletalMesh* skeletalMesh,
														       TArray<int32> materialSlots,
														       const int32 numSections = 16);

	/**
	 * Creates a sectioned UV for the passed in static mesh and condenses the desired material slots into 1
	 * @param staticMesh The static mesh to create a new sectioned mesh from. The new mesh will be suffixed with "_sectioned".
	 * @param materialSlots The material slots to condense into a single slot which should use the sectioned UV material
	 * @param numSections The number of horizonal sections.
	 * @return The created static mesh, or None if the function failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sectioned UV", meta=(AdvancedDisplay="numSections"), DisplayName="Create Sectioned UV Static Mesh")
	static class UStaticMesh* CreateSectionedUVStaticMesh(class UStaticMesh* staticMesh,
														  TArray<int32> materialSlots,
														  const int32 numSections = 16);
};
