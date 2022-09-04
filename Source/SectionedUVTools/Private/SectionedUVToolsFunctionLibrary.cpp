// Copyright (c) 2022 Solar Storm Interactive

#include "SectionedUVToolsFunctionLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "RawMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshUtilities.h"
#include "Rendering/SkeletalMeshModel.h"
//#include "StaticMeshOperations.h"

DEFINE_LOG_CATEGORY(LogSectionedUVTools);

namespace SectionedUVTools
{
	static FName SectionedSlotName = FName("sectioned");
	
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

	bool GetSectionFromVertexIndex(TArray<FSkelMeshSection>& Sections, int32 InVertIndex, int32& OutSectionIndex, int32& OutVertIndex)
	{
		OutSectionIndex = 0;
		OutVertIndex = 0;

		int32 VertCount = 0;

		// Iterate over each chunk
		for (int32 SectionCount = 0; SectionCount < Sections.Num(); SectionCount++)
		{
			const FSkelMeshSection& Section = Sections[SectionCount];
			OutSectionIndex = SectionCount;

			// Is it in Soft vertex range?
			if (InVertIndex < VertCount + Section.GetNumVertices())
			{
				OutVertIndex = InVertIndex - VertCount;
				return true;
			}
			VertCount += Section.GetNumVertices();
		}

		return false;
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

	for(FSkeletalMaterial& material : skeletalMesh->GetMaterials())
	{
		if(material.MaterialSlotName == SectionedUVTools::SectionedSlotName)
		{
			UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the skeletal mesh. Mesh already contains a 'sectioned' material slot!"));
			return nullptr;
		}
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
	const int32 sectionedMatIndex = materials.Emplace(nullptr, true, false, SectionedUVTools::SectionedSlotName, SectionedUVTools::SectionedSlotName);

	TArray<UMorphTarget*>& morphTargets = sectionedMesh->GetMorphTargets();
	
	// Merge the sections which will use the new sectioned material
	int32 lodIndex = 0;
	for(FSkeletalMeshLODModel& lodModel : skelMeshModel->LODModels)
	{
		FSkelMeshSection mergedSections;
		mergedSections.MaterialIndex = sectionedMatIndex;
		TArray<uint32> mergedIndexBuffer;

		int32 accumVertsCount = 0;
		int32 mergedVertsCount = 0;
		int32 boneMapAccum = 0;

		TArray<int32> sectionsToRemove;

		// Add the extra tex coord for the sectioning
		lodModel.NumTexCoords += 1;
		
		TArray<int32> oldToNewSectionMap;
		int32 newSectionIndex = 0;
		
		TMap<int32, int32> sectionedSectionMapping;
		
		for(int32 sectionIndex = 0; sectionIndex < lodModel.Sections.Num(); ++sectionIndex)
		{
			FSkelMeshSection& section = lodModel.Sections[sectionIndex];
			if(materialSlots.Contains(section.MaterialIndex))
			{
				oldToNewSectionMap.Add(INDEX_NONE);
				
				// This section will be merged into a new combined section
				mergedSections.NumTriangles += section.NumTriangles;

				mergedSections.MaxBoneInfluences = FMath::Max(mergedSections.MaxBoneInfluences, section.MaxBoneInfluences);

				const int32 sectionToUse = matIndexToUVSection.FindChecked(section.MaterialIndex);
				section.MaterialIndex = sectionedMatIndex;

				TArray<FSoftSkinVertex> softVerts = section.SoftVertices;
				for(FSoftSkinVertex& vert : softVerts)
				{
					for(int32 boneInfIndex = 0; boneInfIndex < section.MaxBoneInfluences; ++boneInfIndex)
					{
						vert.InfluenceBones[boneInfIndex] += boneMapAccum;
					}

					// Add a UV section with all verts UV x squished into the UV section
					vert.UVs[lodModel.NumTexCoords - 1] = vert.UVs[0];
					const float halfStride = (1.0f / numSections) / 2.0f;
					const float sectionMidX = sectionToUse * (1.0f / numSections) + halfStride;
					vert.UVs[lodModel.NumTexCoords - 1].X = sectionMidX;
				}
				
				boneMapAccum += section.BoneMap.Num();
				
				mergedSections.SoftVertices.Append(softVerts);
				mergedSections.BoneMap.Append(section.BoneMap);

				mergedSections.NumVertices += section.NumVertices;
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

				sectionedSectionMapping.Add(sectionIndex, mergedVertsCount);

				mergedVertsCount += section.GetNumVertices();
				accumVertsCount += section.GetNumVertices();
				sectionsToRemove.Add(sectionIndex);
			}
			else
			{
				oldToNewSectionMap.Add(newSectionIndex++);
				
				accumVertsCount += section.GetNumVertices();

				// Just assign the new material and create a copy of UV index 0
				section.MaterialIndex = slotRemap.FindChecked(section.MaterialIndex);
				for(FSoftSkinVertex& vert : section.SoftVertices)
				{
					vert.UVs[lodModel.NumTexCoords - 1] = vert.UVs[0];
				}
			}
		}

		// Copy all sections so we can reference how they were after to fixup morph targets
		// This kinda sucks but is the easiest way I could think of without building mappings
		// and adding a lot more code.
		TArray<FSkelMeshSection> sectionsCopy = lodModel.Sections;
		
		// Actually remove the sections
		for(int32 sectionIndex = sectionsToRemove.Num() - 1; sectionIndex >= 0; --sectionIndex)
		{
			const int32& sectionToRemove = sectionsToRemove[sectionIndex];
			SectionedUVTools::RemoveMeshSection(lodModel, sectionToRemove);
		}

		// Add the merged section in at the end
		mergedSections.BaseIndex = lodModel.IndexBuffer.Num();
		mergedSections.BaseVertexIndex = lodModel.NumVertices;
		for(const int32& indexToReinsert : mergedIndexBuffer)
		{
			lodModel.IndexBuffer.Add(indexToReinsert + lodModel.NumVertices);
		}
		const int32 sectionedSectionIndex = lodModel.Sections.Add(mergedSections);
		lodModel.NumVertices += mergedSections.GetNumVertices();

		// Cache off the number of verts to each section so we can re-offset the morph targets next
		TArray<int32> sectionBaseVertices;
		int32 accumVerts = 0;
		for(FSkelMeshSection& section : lodModel.Sections)
		{
			sectionBaseVertices.Add(accumVerts);
			accumVerts += section.GetNumVertices();
		}

		// Fixup all of the morph targets with the new vertex offsets
		for(UMorphTarget* morphTarget : morphTargets)
		{
#if ENGINE_MAJOR_VERSION >= 5
			if(!morphTarget->GetMorphLODModels().IsValidIndex(lodIndex))
#else
			if(!morphTarget->MorphLODModels.IsValidIndex(lodIndex))
#endif
			{
				continue;
			}
#if ENGINE_MAJOR_VERSION >= 5
			FMorphTargetLODModel& morphLOD = morphTarget->GetMorphLODModels()[lodIndex];
#else
			FMorphTargetLODModel& morphLOD = morphTarget->MorphLODModels[lodIndex];
#endif
			morphLOD.SectionIndices.Empty();
			for(FMorphTargetDelta& morphVert : morphLOD.Vertices)
			{
				// Get the original section and vertex index
				int32 outSectionIndex; int32 outVertIndex;
				SectionedUVTools::GetSectionFromVertexIndex(sectionsCopy, morphVert.SourceIdx, outSectionIndex, outVertIndex);

				// Translate into the new section locations
				int32 foundNewIndex = oldToNewSectionMap[outSectionIndex];
				int32 subSectionVertCount = 0;
				if(foundNewIndex == INDEX_NONE)
				{
					foundNewIndex = sectionedSectionIndex;
					subSectionVertCount = sectionedSectionMapping[outSectionIndex];
				}

				morphVert.SourceIdx = outVertIndex + sectionBaseVertices[foundNewIndex] + subSectionVertCount;
				morphLOD.SectionIndices.AddUnique(foundNewIndex);
			}
			
			morphTarget->PostEditChange();
		}

		++lodIndex;
	}

	// Push new GUID so the DDC gets updated
	sectionedMesh->InvalidateDeriveDataCacheGUID();

	// Post edit to rebuild the resources etc and mark dirty
	sectionedMesh->PostEditChange();
	sectionedMesh->MarkPackageDirty();

	sectionedMesh->InitMorphTargets();

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
	if(!staticMesh || !staticMesh->GetPackage())
	{
		return nullptr;
	}

	if(numSections < 2)
	{
		UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the static mesh. Number of sections should be greater than 2. 8 or 16 are good choices."));
		return nullptr;
	}

	for(FStaticMaterial& material : staticMesh->GetStaticMaterials())
	{
		if(material.MaterialSlotName == SectionedUVTools::SectionedSlotName)
		{
			UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the static mesh. Mesh already contains a 'sectioned' material slot!"));
			return nullptr;
		}
	}
	
	if(materialSlots.Num())
	{
		const TArray<FStaticMaterial>& materials = staticMesh->GetStaticMaterials();
		for(const int32& materialSlot : materialSlots)
		{
			if(!materials.IsValidIndex(materialSlot))
			{
				UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the static mesh. Material slot index '%d' is invalid!"), materialSlot);
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
		const TArray<FStaticMaterial>& materials = staticMesh->GetStaticMaterials();
		for(int32 materialIndex = 0; materialIndex < materials.Num(); ++materialIndex)
		{
			materialSlots.Add(materialIndex);
		}
	}

	if(numSections < materialSlots.Num())
	{
		UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the static mesh. Number of sections needs to be greater than or equal to the number of materials!"));
		return nullptr;
	}

	TMap<int32, int32> matIndexToUVSection;
	int32 curSectionIndex = 0;
	for(const int32& materialSlot : materialSlots)
	{
		matIndexToUVSection.Add(materialSlot, curSectionIndex++);
	}
	
	FString packageName = staticMesh->GetPackage()->GetPathName() + TEXT("_sectioned");
	if(FindPackage(nullptr, *packageName))
	{
		int32 sectionIndex = 1;
		while(FindPackage(nullptr, *(packageName + FString::FromInt(sectionIndex))))
		{
			++sectionIndex;
		}
		packageName = packageName + FString::FromInt(sectionIndex);
	}

	UPackage* staticMeshPackage = CreatePackage(*packageName);
	if(!staticMeshPackage)
	{
		UE_LOG(LogSectionedUVTools, Error, TEXT("Unable to create package for new sectioned mesh!"));
		return nullptr;
	}
	
	UStaticMesh* sectionedMesh = DuplicateObject<UStaticMesh>(staticMesh, staticMeshPackage, FName(*FPaths::GetBaseFilename(packageName)));
	if(!sectionedMesh)
	{
		UE_LOG(LogSectionedUVTools, Error, TEXT("Unable to create static mesh asset to make into a sectioned mesh!"));
		return nullptr;
	}
	
	if(!sectionedMesh->GetNumSourceModels())
	{
		sectionedMesh->ConditionalBeginDestroy();
		staticMeshPackage->ConditionalBeginDestroy();
		UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the static mesh. No source models in this mesh?"));
		return nullptr;
	}

	int32 sectionedUVChannel = 0;
	for(int32 sourceModelIndex = 0; sourceModelIndex < sectionedMesh->GetNumSourceModels(); ++sourceModelIndex)
	{
		FStaticMeshSourceModel& srcModel = sectionedMesh->GetSourceModel(sourceModelIndex);
		const int32 numUVChannels = sectionedMesh->GetNumUVChannels(sourceModelIndex);
		if(sourceModelIndex == 0)
		{
			sectionedUVChannel = numUVChannels;
			if(srcModel.BuildSettings.bGenerateLightmapUVs)
			{
				// Make sure it is after the generated lightmap UV
				sectionedUVChannel = srcModel.BuildSettings.DstLightmapIndex + 1;
				if(numUVChannels > sectionedUVChannel)
				{
					// Extra channels were already added, so put us after that
					sectionedUVChannel = numUVChannels;
				}
			}
		}
		if(numUVChannels == MAX_MESH_TEXTURE_COORDS || numUVChannels > sectionedUVChannel)
		{
			sectionedMesh->ConditionalBeginDestroy();
			staticMeshPackage->ConditionalBeginDestroy();
			UE_LOG(LogSectionedUVTools, Error, TEXT("Cannot section the static mesh. The mesh cannot support a new UV channel because of max channel limit or inconsistent UV num per LOD!"));
			return nullptr;
		}

		// Make sure each LOD has the sectioned UV as the same index
		for(int32 UVChan = numUVChannels; UVChan < sectionedUVChannel + 1; ++UVChan)
		{
			//FStaticMeshOperations::AddUVChannel(*srcModel.MeshDescription);
			sectionedMesh->AddUVChannel(sourceModelIndex);
		}
	}
	
	// Get rid of the material slots we are merging
	TArray<FStaticMaterial>& materials = sectionedMesh->GetStaticMaterials();

	// Add the new material for the sectioned mesh parts
	const int32 sectionedMatIndex = materials.AddDefaulted();
	materials[sectionedMatIndex].MaterialSlotName = SectionedUVTools::SectionedSlotName;
	materials[sectionedMatIndex].UVChannelData = FMeshUVChannelInfo(1.0f);

	// Create a mapping of what the material slots will be after to re-assign once we have removed the slots
	TMap<int32, int32> slotRemap;
	int32 actualSlotIndex = 0;
	for(int32 materialSlotIndex = 0; materialSlotIndex < materials.Num() - 1; ++materialSlotIndex, ++actualSlotIndex)
	{
		if(materialSlots.Contains(materialSlotIndex))
		{
			--actualSlotIndex;
			slotRemap.Add(materialSlotIndex, sectionedMatIndex);
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
	
	for(int32 sourceModelIndex = 0; sourceModelIndex < sectionedMesh->GetNumSourceModels(); ++sourceModelIndex)
	{
#if ENGINE_MAJOR_VERSION >= 5
		FStaticMeshSourceModel& sourceModel = sectionedMesh->GetSourceModel(sourceModelIndex);
#else
		FStaticMeshSourceModel& sourceModel = sectionedMesh->GetSourceModels()[sourceModelIndex];
#endif
		
		FRawMesh outRawMesh;
		sourceModel.LoadRawMesh(outRawMesh);

		// Create a copy of the wedge texture coordinates. We will modify the ones using the new section material.
		outRawMesh.WedgeTexCoords[sectionedUVChannel] = outRawMesh.WedgeTexCoords[0];

		// Remap the material indices to the new reduced set
		for(int32 faceIndex = 0; faceIndex < outRawMesh.FaceMaterialIndices.Num(); ++faceIndex)
		{
			int32& matIndex = outRawMesh.FaceMaterialIndices[faceIndex];
			int32* sectionToUse = matIndexToUVSection.Find(matIndex);
			matIndex = slotRemap[matIndex];
			if(matIndex == sectionedMatIndex)
			{
				check(sectionToUse);
				const float halfStride = (1.0f / numSections) / 2.0f;
				const float sectionMidX = *sectionToUse * (1.0f / numSections) + halfStride;
				
				// Update the UVs for this face
				const int32 firstWedgeIndex = faceIndex * 3;
				for(int32 wedgeIndex = 0; wedgeIndex < 3; ++wedgeIndex)
				{
					outRawMesh.WedgeTexCoords[sectionedUVChannel][firstWedgeIndex + wedgeIndex].X = sectionMidX;
				}
			}
		}

		sourceModel.SaveRawMesh(outRawMesh);
	}

	// Post edit to rebuild the resources etc and mark dirty
	sectionedMesh->PostEditChange();
	sectionedMesh->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(sectionedMesh);
	
	return sectionedMesh;
}
