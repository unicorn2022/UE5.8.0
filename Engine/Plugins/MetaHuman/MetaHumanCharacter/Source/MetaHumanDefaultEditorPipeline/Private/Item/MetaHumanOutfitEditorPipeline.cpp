// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanOutfitEditorPipeline.h"

#include "Item/MetaHumanOutfitPipeline.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"
#include "MetaHumanDefaultEditorPipelineLog.h"
#include "MetaHumanPinnedSlotSelection.h"
#include "MetaHumanWardrobeItem.h"

#include "ChaosOutfitAsset/CollectionOutfitFacade.h"
#include "ChaosOutfitAsset/OutfitAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"

#include "Logging/StructuredLog.h"

UMetaHumanOutfitEditorPipeline::UMetaHumanOutfitEditorPipeline()
{
	Specification = CreateDefaultSubobject<UMetaHumanCharacterEditorPipelineSpecification>("Specification");
	Specification->BuildInputStruct = FMetaHumanOutfitPipelineBuildInput::StaticStruct();
}

UE::Tasks::TTask<FMetaHumanPaletteBuiltData> UMetaHumanOutfitEditorPipeline::BuildItem(const FBuildItemParams& Params) const
{
	const UMetaHumanOutfitPipeline* RuntimePipeline = Cast<UMetaHumanOutfitPipeline>(GetRuntimePipeline());
	if (!RuntimePipeline)
	{
		UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Runtime pipeline for item {ItemPath} must be a UMetaHumanOutfitPipeline", Params.ItemPath.ToDebugString());
		
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	if (!Params.BuildInput.GetPtr<FMetaHumanOutfitPipelineBuildInput>())
	{
		UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Build input not provided to Outfit pipeline during build");
		
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	UChaosOutfitAsset* OutfitAsset = Cast<UChaosOutfitAsset>(Params.WardrobeItem->PrincipalAsset.LoadSynchronous());
	if (!OutfitAsset)
	{
		UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Outfit pipeline failed to load outfit asset from {PrincipalAsset}", Params.WardrobeItem->PrincipalAsset.ToString());
		
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	const FMetaHumanOutfitPipelineBuildInput& OutfitBuildInput = Params.BuildInput.Get<FMetaHumanOutfitPipelineBuildInput>();

	FMetaHumanPaletteBuiltData BuiltDataResult;
	FMetaHumanPipelineBuiltData& OutfitBuiltData = BuiltDataResult.ItemBuiltData.Edit().Add(Params.ItemPath);
	FMetaHumanOutfitPipelineBuildOutput& OutfitBuildOutput = OutfitBuiltData.BuildOutput.InitializeAs<FMetaHumanOutfitPipelineBuildOutput>();
	
	// Generate a fitted version of this cloth for each Character
	for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanOutfitPipelineBuildCharacterData>& Pair : OutfitBuildInput.CharacterData)
	{
		FName AutoSelectedSourceSize;
		TArray<FName> AvailableSourceSizes;

		// Try to fit this cloth to the Character's body
		UChaosOutfitAsset* ClothForCharacter = nullptr;
		if (OutfitBuildInput.OutfitResizeDataflowAsset
			&& Pair.Value.MergedHeadAndBody)
		{
			UChaosOutfitAsset* FittedOutfit = NewObject<UChaosOutfitAsset>(Params.OuterForGeneratedObjects, NAME_None, RF_Public);
			FittedOutfit->SetDataflow(OutfitBuildInput.OutfitResizeDataflowAsset);

			FDataflowVariableOverrides& FittedOutfitVariableOverrides = FittedOutfit->GetDataflowInstance().GetVariableOverrides();

			FittedOutfitVariableOverrides.OverrideVariableObject(OutfitBuildInput.TargetBodyPropertyName, Pair.Value.MergedHeadAndBody);
			FittedOutfitVariableOverrides.OverrideVariableObject(OutfitBuildInput.ResizableOutfitPropertyName, OutfitAsset);

			FittedOutfitVariableOverrides.OverrideVariableBool("TransferSkinWeights", Pair.Value.bTransferSkinWeights);
			FittedOutfitVariableOverrides.OverrideVariableBool("GenerateLOD0Only", Pair.Value.bGenerateLOD0Only);
			FittedOutfitVariableOverrides.OverrideVariableBool("StripSimMesh", Pair.Value.bStripSimMesh);
			
					
			const FMetaHumanPinnedSlotSelection* PinnedSelection = nullptr;
			if (FMetaHumanPinnedSlotSelection::TryGetPinnedItem(Params.SortedPinnedSlotSelections, Params.ItemPath, PinnedSelection))
			{
				auto OverrideBoolVariable = [PinnedSelection, &FittedOutfitVariableOverrides](const FName& VariableName)
				{
					TValueOrError<bool, EPropertyBagResult> Value = PinnedSelection->AssemblyParameters.GetValueBool(VariableName);
					if (Value.HasValue())
					{
						FittedOutfitVariableOverrides.OverrideVariableBool(VariableName, Value.GetValue());
					}
				};

				OverrideBoolVariable(TEXT("PruneSkinWeights"));
				OverrideBoolVariable(TEXT("RelaxSkinWeights"));
				OverrideBoolVariable(TEXT("HammerSkinWeights"));
				OverrideBoolVariable(TEXT("ClampSkinWeights"));
				OverrideBoolVariable(TEXT("NormalizeSkinWeights"));
				OverrideBoolVariable(TEXT("ResizeUVs"));
				OverrideBoolVariable(TEXT("CustomRegionResizing"));
												
				{
					// Important: Result must be stored in a local variable here to keep 
					// the FString alive.
					//
					// Chaining TryGetValue directly onto GetValueString will result in the
					// FString being deleted and a dangling pointer being returned.
					TValueOrError<FString, EPropertyBagResult> Result = PinnedSelection->AssemblyParameters.GetValueString("SourceSizeOverride");
					const FString* VarPtr = Result.TryGetValue();
					if (VarPtr != nullptr)
					{
						FittedOutfitVariableOverrides.OverrideVariableName("OverrideBodySizeName", **VarPtr);
					}
				}
			}

			FittedOutfit->GetDataflowInstance().UpdateOwnerAsset(true);

			{
				const UE::Chaos::OutfitAsset::FCollectionOutfitConstFacade OutfitFacade(OutfitAsset->GetOutfitCollection());
				if (OutfitFacade.IsValid())
				{
					const int32 ClosestBodySize = OutfitFacade.FindClosestBodySize(*Pair.Value.MergedHeadAndBody);
					AutoSelectedSourceSize = *OutfitFacade.GetBodySizeName(ClosestBodySize);

					for (int32 BodySizeIndex = 0; BodySizeIndex < OutfitFacade.GetNumBodySizes(); BodySizeIndex++)
					{
						AvailableSourceSizes.Add(*OutfitFacade.GetBodySizeName(BodySizeIndex));
					}
				}
			}

			ClothForCharacter = FittedOutfit;
		}

		if (!ClothForCharacter)
		{
			// Failed to fit the cloth, so pass through the original cloth -- it may not need fitting
			ClothForCharacter = OutfitAsset;
		}

		FMetaHumanOutfitGeneratedAssets* OutfitGeneratedAssets = nullptr;

		if (Params.Quality == EMetaHumanCharacterPaletteBuildQuality::Production)
		{
			USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Params.OuterForGeneratedObjects, NAME_None, RF_Public);

			// For Production quality, we bake to meshes, because Outfits can't yet be cooked
			if (ClothForCharacter->ExportToSkeletalMesh(*SkeletalMesh))
			{
				SkeletalMesh->SetSkeleton(Pair.Value.SkeletonForOutputMesh);

				OutfitGeneratedAssets = &OutfitBuildOutput.CharacterAssets.Add(Pair.Key);
				OutfitGeneratedAssets->OutfitMesh = SkeletalMesh;
			}
		}
		else
		{
			OutfitGeneratedAssets = &OutfitBuildOutput.CharacterAssets.Add(Pair.Key);
			OutfitGeneratedAssets->Outfit = ClothForCharacter;
			OutfitGeneratedAssets->CombinedBodyMesh = Pair.Value.MergedHeadAndBody;
		}

		if (OutfitGeneratedAssets)
		{
			OutfitGeneratedAssets->AvailableSourceSizes = AvailableSourceSizes;
			OutfitGeneratedAssets->AutoSelectedSourceSize = AutoSelectedSourceSize;
		}
	}

	if (HeadHiddenFaceMapTexture.Texture)
	{
		if (HeadHiddenFaceMapTexture.Texture->Source.IsValid())
		{
			for (TPair<FMetaHumanPaletteItemKey, FMetaHumanOutfitGeneratedAssets>& Pair : OutfitBuildOutput.CharacterAssets)
			{
				Pair.Value.HeadHiddenFaceMap = HeadHiddenFaceMapTexture;
			}
		}
		else
		{
			UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Head hidden face map {Texture} on outfit pipeline {Pipeline} has no source data and can't be used",
				HeadHiddenFaceMapTexture.Texture->GetPathName(), GetPathName());
		}
	}
						
	if (BodyHiddenFaceMapTexture.Texture)
	{
		if (BodyHiddenFaceMapTexture.Texture->Source.IsValid())
		{
			for (TPair<FMetaHumanPaletteItemKey, FMetaHumanOutfitGeneratedAssets>& Pair : OutfitBuildOutput.CharacterAssets)
			{
				Pair.Value.BodyHiddenFaceMap = BodyHiddenFaceMapTexture;
			}
		}
		else
		{
			UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Body hidden face map {Texture} on outfit pipeline {Pipeline} has no source data and can't be used",
				BodyHiddenFaceMapTexture.Texture->GetPathName(), GetPathName());
		}
	}

	// Generate Assembly Parameters
	{
		const TArray<FSkeletalMaterial>& MaterialSections = OutfitAsset->GetMaterials();

		UE::MetaHuman::MaterialUtils::GenerateAssemblyParameters(
			RuntimePipeline->OverrideMaterials,
			RuntimePipeline->RuntimeMaterialParameters,
			MaterialSections.Num(),
			UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSections),
			UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(MaterialSections),
			OutfitBuiltData.AssemblyParameters);
	}
	
	// Set up outfit fitting parameters
	{
		auto SetBoolParameters = [](FInstancedPropertyBag& InOutPropertyBag, const FName& InPropertyName, bool bInValue)
		{
			InOutPropertyBag.AddProperty(InPropertyName, EPropertyBagPropertyType::Bool);
			InOutPropertyBag.SetValueBool(InPropertyName, bInValue);
		};

		SetBoolParameters(OutfitBuiltData.AssemblyParameters, TEXT("PruneSkinWeights"), true);
		SetBoolParameters(OutfitBuiltData.AssemblyParameters, TEXT("RelaxSkinWeights"), true);
		SetBoolParameters(OutfitBuiltData.AssemblyParameters, TEXT("HammerSkinWeights"), true);
		SetBoolParameters(OutfitBuiltData.AssemblyParameters, TEXT("ClampSkinWeights"), true);
		SetBoolParameters(OutfitBuiltData.AssemblyParameters, TEXT("NormalizeSkinWeights"), true);
		SetBoolParameters(OutfitBuiltData.AssemblyParameters, TEXT("ResizeUVs"), true);
		SetBoolParameters(OutfitBuiltData.AssemblyParameters, TEXT("CustomRegionResizing"), true);

		if (OutfitBuildOutput.CharacterAssets.Num() > 0)
		{
			// The source size selection should be per-character, but we can't support this in the 
			// current framework, so for now we just use an arbitrary character.
			const FMetaHumanOutfitGeneratedAssets& GeneratedAssetsToUse = OutfitBuildOutput.CharacterAssets.CreateConstIterator().Value();

			{
				const FName PropertyName = "AutoSelectedSourceSize";
				const FPropertyBagPropertyDesc PropertyDesc(PropertyName, EPropertyBagContainerType::None, EPropertyBagPropertyType::Name, nullptr, CPF_Edit | CPF_EditConst);
				OutfitBuiltData.AssemblyParameters.AddProperties({ PropertyDesc });
				OutfitBuiltData.AssemblyParameters.SetValueName(PropertyName, GeneratedAssetsToUse.AutoSelectedSourceSize);
			}

			{
				const FName SourceSizeOverrideName = "SourceSizeOverride";
				FPropertyBagPropertyDesc SourceSizeOverrideDesc(SourceSizeOverrideName, EPropertyBagContainerType::None, EPropertyBagPropertyType::String, nullptr, CPF_Edit);

				{
					// Generate a temporary enum to hold the names of the available sizes for display to the user
					TArray<TPair<FName, int64>> AvailableSizes;
					AvailableSizes.Reserve(GeneratedAssetsToUse.AvailableSourceSizes.Num());
					for (FName AvailableSize : GeneratedAssetsToUse.AvailableSourceSizes)
					{
						AvailableSizes.Add(TPair<FName, int64>(AvailableSize, AvailableSizes.Num()));
					}

					UEnum* SourceSizesEnum = NewObject<UEnum>(Params.OuterForGeneratedObjects);
					SourceSizesEnum->SetEnums(AvailableSizes, UEnum::ECppForm::Regular, UEnum::EUnderlyingType::uint8, EEnumFlags::None, UEnum::EAddMaxKeyIfMissing::Yes);

					// Ensure the editor displays the exact names and not generated friendly names
					for (const TPair<FName, int64>& AvailableSize : AvailableSizes)
					{
						SourceSizesEnum->SetMetaData(TEXT("DisplayName"), *AvailableSize.Key.ToString(), static_cast<int32>(AvailableSize.Value));
					}
				 
					// Use the generated enum as the entries of a dropdown, instead of making this a free text field
					SourceSizeOverrideDesc.SetMetaData("Enum", SourceSizesEnum->GetPathName());

					// Add a hidden property to hold a hard reference to the generated enum
					{
						const FName PropertyName = "SourceSizeOverrideEnum_Hidden";
						const FPropertyBagPropertyDesc PropertyDesc(PropertyName, EPropertyBagContainerType::None, EPropertyBagPropertyType::Object, UEnum::StaticClass(), CPF_None);
						OutfitBuiltData.AssemblyParameters.AddProperties({ PropertyDesc });
						OutfitBuiltData.AssemblyParameters.SetValueObject(PropertyName, SourceSizesEnum);
					}
				}

				OutfitBuiltData.AssemblyParameters.AddProperties({ SourceSizeOverrideDesc });
			}
		}
	}

	return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>(MoveTemp(BuiltDataResult));
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanOutfitEditorPipeline::GetSpecification() const
{
	return Specification;
}

void UMetaHumanOutfitEditorPipeline::PostLoad()
{
	Super::PostLoad();

	if (BodyHiddenFaceMap_DEPRECATED)
	{
		BodyHiddenFaceMapTexture.Texture = BodyHiddenFaceMap_DEPRECATED;
		BodyHiddenFaceMap_DEPRECATED = nullptr;
	}
}
