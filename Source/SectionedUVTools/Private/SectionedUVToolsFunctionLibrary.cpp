// Copyright (c) 2022 Solar Storm Interactive

#include "SectionedUVToolsFunctionLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshUtilities.h"
#include "Rendering/SkeletalMeshModel.h"
#include "IMeshBuilderModule.h"

DEFINE_LOG_CATEGORY(LogSectionedUVTools);

//--------------------------------------------------------------------------------------------------------------------
/**
*/
USkeletalMesh* USectionedUVToolsFunctionLibrary::CreateSectionedUVSkeletalMesh(USkeletalMesh* skeletalMesh,
																			   TArray<int32> materialSlots,
																			   const int32 numSections)
{
	if(!skeletalMesh || !skeletalMesh->GetPackage() || numSections < 2)
	{
		return nullptr;
	}

	if(materialSlots.Num())
	{
		const TArray<FSkeletalMaterial>& materials = skeletalMesh->GetMaterials();
		for(const int32& materialSlot : materialSlots)
		{
			if(!materials.IsValidIndex(materialSlot))
			{
				return nullptr;
			}
		}

		// Make sure the slots are in order so we can reverse remove them
		Algo::Sort(materialSlots, [](const int32& a, const int32& b) -> bool
		{
			return a < b;
		});
	}
	else
	{
		const TArray<FSkeletalMaterial>& materials = skeletalMesh->GetMaterials();
		for(int32 materialIndex = 0; materialIndex < materials.Num(); ++materialIndex)
		{
			materialSlots.Add(materialIndex);
		}
	}
	
	FString packageName = skeletalMesh->GetPackage()->GetPathName() + TEXT("_sectioned");
	if(FindPackage(nullptr, *packageName))
	{
		int32 sectionIndex = 1;
		while(FindPackage(nullptr, *(packageName + FString::FromInt(sectionIndex))))
		{
			++sectionIndex;
		}
		packageName = packageName + FString::FromInt(sectionIndex);
	}

	UPackage* skelMeshPackage = CreatePackage(*packageName);
	if(!skelMeshPackage)
	{
		UE_LOG(LogSectionedUVTools, Error, TEXT("Unable to create package for new sectioned mesh!"));
		return nullptr;
	}
	
	USkeletalMesh* sectionedMesh = DuplicateObject<USkeletalMesh>(skeletalMesh, skelMeshPackage, FName(*FPaths::GetBaseFilename(packageName)));
	if(!sectionedMesh)
	{
		UE_LOG(LogSectionedUVTools, Error, TEXT("Unable to create skeletal mesh asset to make into a sectioned mesh!"));
		return nullptr;
	}

	FSkeletalMeshModel* skelMeshModel = sectionedMesh->GetImportedModel();
	if(!skelMeshModel)
	{
		return nullptr;
	}
	
	// Get rid of the material slots we are merging
	TArray<FSkeletalMaterial>& materials = sectionedMesh->GetMaterials();

	// Create a mapping of what the material slots will be after to re-assign once we have removed the slots
	TMap<int32, int32> slotRemap;
	int32 actualSlotIndex = 0;
	for(int32 materialSlotIndex = 0; materialSlotIndex < materials.Num(); ++materialSlotIndex, ++actualSlotIndex)
	{
		if(materialSlots.Contains(materialSlotIndex))
		{
			--actualSlotIndex;
		}
		else
		{
			slotRemap.Add(materialSlotIndex, actualSlotIndex);
		}
	}

	// Remove the material slots we don't want
	for(int32 materialSlotIndex = materialSlots.Num() - 1; materialSlotIndex >= 0; --materialSlotIndex)
	{
		materials.RemoveAt(materialSlots[materialSlotIndex]);
	}

	// Add the new material for the sectioned mesh parts
	const int32 sectionedMatIndex = materials.Emplace(nullptr, true, false, FName("sectioned"), FName("sectioned"));
	
	for(FSkeletalMeshLODModel& lodModel : skelMeshModel->LODModels)
	{
		for(FSkelMeshSection& section : lodModel.Sections)
		{
			if(materialSlots.Contains(section.MaterialIndex))
			{
				section.MaterialIndex = sectionedMatIndex;
			}
			else
			{
				section.MaterialIndex = slotRemap.FindChecked(section.MaterialIndex);
			}
		}
		
		// IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
		// bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(lodModel,
		// 													 sectionedMesh->GetPathName(),
		// 													 sectionedMesh->GetRefSkeleton(),
		// 													 LODInfluences,
		// 													 LODWedges,
		// 													 LODFaces,
		// 													 LODPoints,
		// 													 LODPointToRawMap,
		// 													 BuildOptions,
		// 													 &WarningMessages,
		// 													 &WarningNames);
	}

	sectionedMesh->PostEditChange();
	sectionedMesh->MarkPackageDirty();

	// Rebuild resources
	sectionedMesh->ReleaseResources();
	sectionedMesh->InitResources();

	FAssetRegistryModule::AssetCreated(sectionedMesh);
	return sectionedMesh;
}

//--------------------------------------------------------------------------------------------------------------------
/**
*/
UStaticMesh* USectionedUVToolsFunctionLibrary::CreateSectionedUVStaticMesh(UStaticMesh* staticMesh,
																		   TArray<int32> materialSlots,
																		   const int32 numSections)
{
	if(!staticMesh)
	{
		return nullptr;
	}
	
	return nullptr;
}
