// Copyright (c) 2022 Solar Storm Interactive

#include "SectionedUVToolsFunctionLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshUtilities.h"
#include "Rendering/SkeletalMeshModel.h"
//#include "IMeshBuilderModule.h"

DEFINE_LOG_CATEGORY(LogSectionedUVTools);

namespace SectionedUVTools
{
	//--------------------------------------------------------------------------------------------------------------------
	/**
	*/
	static bool RemoveMeshSection(FSkeletalMeshLODModel& Model, int32 SectionIndex) 
	{
		// Need a valid section
		if (!Model.Sections.IsValidIndex(SectionIndex))
		{
			return false;
		}

		const FSkelMeshSection& SectionToRemove = Model.Sections[SectionIndex];

		if (SectionToRemove.CorrespondClothAssetIndex != INDEX_NONE)
		{
			// Can't remove this, clothing currently relies on it
			return false;
		}

		const uint32 NumVertsToRemove   = SectionToRemove.GetNumVertices();
		const uint32 BaseVertToRemove   = SectionToRemove.BaseVertexIndex;
		const uint32 NumIndicesToRemove = SectionToRemove.NumTriangles * 3;
		const uint32 BaseIndexToRemove  = SectionToRemove.BaseIndex;


		// Strip indices
		Model.IndexBuffer.RemoveAt(BaseIndexToRemove, NumIndicesToRemove);

		Model.Sections.RemoveAt(SectionIndex);

		// Fixup indices above base vert
		for (uint32& Index : Model.IndexBuffer)
		{
			if (Index >= BaseVertToRemove)
			{
				Index -= NumVertsToRemove;
			}
		}

		Model.NumVertices -= NumVertsToRemove;

		// Fixup anything needing section indices
		for (FSkelMeshSection& Section : Model.Sections)
		{
			// Push back clothing indices
			if (Section.CorrespondClothAssetIndex > SectionIndex)
			{
				Section.CorrespondClothAssetIndex--;
			}

			// Removed indices, re-base further sections
			if (Section.BaseIndex > BaseIndexToRemove)
			{
				Section.BaseIndex -= NumIndicesToRemove;
			}

			// Remove verts, re-base further sections
			if (Section.BaseVertexIndex > BaseVertToRemove)
			{
				Section.BaseVertexIndex -= NumVertsToRemove;
			}
		}
		return true;
	}
}

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
	
	// Merge the sections which will use the new sectioned material
	for(FSkeletalMeshLODModel& lodModel : skelMeshModel->LODModels)
	{
		FSkelMeshSection mergedSections;
		mergedSections.MaterialIndex = sectionedMatIndex;
		TArray<uint32> mergedIndexBuffer;

		int32 accumVertsCount = 0;
		int32 mergedVertsCount = 0;
		
		for(int32 sectionIndex = 0; sectionIndex < lodModel.Sections.Num(); ++sectionIndex)
		{
			FSkelMeshSection& section = lodModel.Sections[sectionIndex];
			if(materialSlots.Contains(section.MaterialIndex))
			{
				// This section will be merged into a new combined section
				mergedSections.NumTriangles += section.NumTriangles;
				mergedSections.SoftVertices.Append(section.SoftVertices);
				for(FBoneIndexType& boneIndex : section.BoneMap)
				{
					mergedSections.BoneMap.AddUnique(boneIndex);
				}
				mergedSections.NumVertices += section.NumVertices;
				mergedSections.MaxBoneInfluences = FMath::Max(mergedSections.MaxBoneInfluences, section.MaxBoneInfluences);
				if(section.bUse16BitBoneIndex)
				{
					mergedSections.bUse16BitBoneIndex = true;
				}

				const uint32 numSectionIndices = section.NumTriangles * 3;
				for(uint32 sectionVertIndex = 0; sectionVertIndex < numSectionIndices; ++sectionVertIndex)
				{
					// Add index, offsetting away the current accumulation to this point, and adding the merged count to this point
					mergedIndexBuffer.Add((lodModel.IndexBuffer[section.BaseIndex + sectionVertIndex] - accumVertsCount) + mergedVertsCount);
				}

				mergedVertsCount += section.GetNumVertices();
				accumVertsCount += section.GetNumVertices();
				SectionedUVTools::RemoveMeshSection(lodModel, sectionIndex);
			}
			else
			{
				accumVertsCount += section.GetNumVertices();
			}
		}

		// Add the merged section in at the end
		mergedSections.BaseIndex = lodModel.IndexBuffer.Num();
		mergedSections.BaseVertexIndex = lodModel.NumVertices;
		for(const int32& indexToReinsert : mergedIndexBuffer)
		{
			lodModel.IndexBuffer.Add(indexToReinsert + lodModel.NumVertices);
		}
		lodModel.Sections.Add(mergedSections);
		lodModel.NumVertices += mergedSections.GetNumVertices();
	}

	// Update the material sections with another UV for the 
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
					const float halfStride = (1.0f / numSections) / 2.0f;
					const float sectionMidX = sectionToUse * (1.0f / numSections) + halfStride;
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
