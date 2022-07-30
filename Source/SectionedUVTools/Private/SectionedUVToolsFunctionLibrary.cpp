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
	if(!skeletalMesh || !skeletalMesh->GetPackage())
	{
		return nullptr;
	}

	if(numSections < 2)
	{
		UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the skeletal mesh. Number of sections should be greater than 2. 8 or 16 are good choices."));
		return nullptr;
	}
	
	if(materialSlots.Num())
	{
		const TArray<FSkeletalMaterial>& materials = skeletalMesh->GetMaterials();
		for(const int32& materialSlot : materialSlots)
		{
			if(!materials.IsValidIndex(materialSlot))
			{
				UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the skeletal mesh. Material slot index '%d' is invalid!"), materialSlot);
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

	if(numSections < materialSlots.Num())
	{
		UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the skeletal mesh. Number of sections needs to be greater than or equal to the number of materials!"));
		return nullptr;
	}

	TMap<int32, int32> matIndexToUVSection;
	int32 curSectionIndex = 0;
	for(const int32& materialSlot : materialSlots)
	{
		matIndexToUVSection.Add(materialSlot, curSectionIndex++);
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
		sectionedMesh->ConditionalBeginDestroy();
		skelMeshPackage->ConditionalBeginDestroy();
		UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the skeletal mesh. No imported model on original skeletal mesh?!"));
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
		// Add another texture coordinate
		lodModel.NumTexCoords += 1;
		for(FSkelMeshSection& section : lodModel.Sections)
		{
			if(materialSlots.Contains(section.MaterialIndex))
			{
				const int32 sectionToUse = matIndexToUVSection.FindChecked(section.MaterialIndex);
				section.MaterialIndex = sectionedMatIndex;

				// Add a UV section with all verts UV x squished into the UV section
				for(FSoftSkinVertex& vert : section.SoftVertices)
				{
					vert.UVs[lodModel.NumTexCoords - 1] = vert.UVs[0];
					constexpr float halfStride = (1.0f / 16.0f) / 2.0f;
					const float sectionMidX = sectionToUse * (1.0f / 16.0f) + halfStride;
					vert.UVs[lodModel.NumTexCoords - 1].X = sectionMidX;
				}
			}
			else
			{
				// Just assign the new material and create a copy of UV index 0
				section.MaterialIndex = slotRemap.FindChecked(section.MaterialIndex);
				for(FSoftSkinVertex& vert : section.SoftVertices)
				{
					vert.UVs[lodModel.NumTexCoords - 1] = vert.UVs[0];
				}
			}
		}
	}

	// Push new GUID so the DDC gets updated
	sectionedMesh->InvalidateDeriveDataCacheGUID();

	// Post edit to rebuild the resources etc and mark dirty
	sectionedMesh->PostEditChange();
	sectionedMesh->MarkPackageDirty();

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
