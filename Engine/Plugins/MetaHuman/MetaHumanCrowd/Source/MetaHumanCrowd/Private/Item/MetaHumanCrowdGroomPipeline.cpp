// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdGroomPipeline.h"
#include "MetaHumanCrowdPipeline.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"
#include "MetaHumanPostAssemblyParameterOutput.h"

#include "MetaHumanCrowdLog.h"

#include "Logging/StructuredLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MetaHumanCrowdMaterialUtils.h"
#include "UObject/ObjectSaveContext.h"
#include "MetaHumanCharacterPalette.h"
#include "MetaHumanItemEditorPipeline.h"

UMetaHumanCrowdGroomPipeline::UMetaHumanCrowdGroomPipeline()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);
		Specification->BuildOutputStruct = FMetaHumanCrowdGroomBuildOutput::StaticStruct();
		Specification->AssemblyInputStruct = FMetaHumanCrowdGroomAssemblyInput::StaticStruct();
		Specification->AssemblyOutputStruct = FMetaHumanCrowdGroomAssemblyOutput::StaticStruct();
	}

#if WITH_EDITOR
	UpdateParameters();
#endif
}

#if WITH_EDITOR
void UMetaHumanCrowdGroomPipeline::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCrowdGroomPipeline, bUseCustomRuntimeMaterialParameters)
		&& !bUseCustomRuntimeMaterialParameters)
	{
		UpdateParameters();
	}
}
#endif

void UMetaHumanCrowdGroomPipeline::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
#if WITH_EDITOR
	UpdateParameters();
#endif

	Super::PreSave(ObjectSaveContext);
}

#if WITH_EDITOR
void UMetaHumanCrowdGroomPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanItemEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanItemEditorPipeline* UMetaHumanCrowdGroomPipeline::GetEditorPipeline() const
{
	// If there's no editor pipeline instance, we can use the Class Default Object, because 
	// pipelines are stateless and won't be modified when used.
	//
	// This is unfortunately a slow path, as it involves looking the class up by name. We could
	// cache this if it becomes a performance issue.
	if (!EditorPipeline)
	{
		const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
		if (EditorPipelineClass)
		{
			return EditorPipelineClass.GetDefaultObject();
		}
	}

	return EditorPipeline;
}

TSubclassOf<UMetaHumanItemEditorPipeline> UMetaHumanCrowdGroomPipeline::GetEditorPipelineClass() const
{
	const TSoftClassPtr<UMetaHumanItemEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanCrowdEditor.MetaHumanCrowdGroomEditorPipeline")));
	
	return SoftEditorPipelineClass.Get();
}
#endif

void UMetaHumanCrowdGroomPipeline::AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	const FInstancedStruct& BuildOutput = Params.ItemBuiltData[Params.BaseItemPath].BuildOutput;

	if (!BuildOutput.GetPtr<FMetaHumanCrowdGroomBuildOutput>())
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Build output not provided to Crowd Groom pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	if (!Params.AssemblyInput.GetPtr<FMetaHumanCrowdGroomAssemblyInput>())
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Assembly input not provided to Crowd Groom pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanCrowdGroomBuildOutput& GroomBuildOutput = BuildOutput.Get<FMetaHumanCrowdGroomBuildOutput>();
	const FMetaHumanCrowdGroomAssemblyInput& GroomAssemblyInput = Params.AssemblyInput.Get<FMetaHumanCrowdGroomAssemblyInput>();

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanCrowdGroomAssemblyOutput& GroomAssemblyOutput = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanCrowdGroomAssemblyOutput>();
	
	// Runtime AssembleItem reads the actor variant's bundle for material identity.
	// ProcessAssemblyParameters generates MIDs from these MICs to feed the actor mesh component override slots.
	const FMetaHumanCrowdMeshGeometryBundle* GroomBundle = GroomBuildOutput.ItemToGroomGeometryMap.Find(GroomAssemblyInput.TargetItem);

	if (!GroomBundle)
	{
		// Strands-only grooms (e.g. buzzcut) populate ItemToBakedGroomTexture but no geometry
		// bundle. They still need PostAssemblyParameters so SetPostAssemblyParameters routes
		// through the collection pipeline's face-MID write path. The collection pipeline knows
		// the parameter shape via GroomFaceParameterBindings; here we just emit an entry whose
		// Parameters property bag advertises the same parameter names as the groom's
		// RuntimeMaterialParameters so the framework's "is property compatible" check passes.
		const TArray<FMetaHumanMaterialParameter> FilteredRuntimeMaterialParameters =
			UE::MetaHuman::MaterialUtils::FilterToCrowdSupportedParameters(
				RuntimeMaterialParameters, /*bLogWarningOnFilter*/ true, GroomAssemblyInput.TargetItem.ToDebugString());

		if (GroomBuildOutput.ItemToBakedGroomTexture.Contains(GroomAssemblyInput.TargetItem)
			&& !FilteredRuntimeMaterialParameters.IsEmpty())
		{
			FMetaHumanPostAssemblyParameterOutput PostAssemblyParamOutput;
			for (const FMetaHumanMaterialParameter& Param : FilteredRuntimeMaterialParameters)
			{
				FPropertyBagPropertyDesc Desc(Param.InstanceParameterName, EPropertyBagPropertyType::Float);
				switch (Param.ParameterType)
				{
					case EMetaHumanRuntimeMaterialParameterType::Scalar:
						Desc.ValueType = EPropertyBagPropertyType::Float;
						break;
					case EMetaHumanRuntimeMaterialParameterType::Vector:
					case EMetaHumanRuntimeMaterialParameterType::DoubleVector:
						Desc.ValueType = EPropertyBagPropertyType::Struct;
						Desc.ValueTypeObject = TBaseStructure<FLinearColor>::Get();
						break;
					default:
						continue;
				}
				PostAssemblyParamOutput.Parameters.AddProperties({ Desc });
			}
			if (PostAssemblyParamOutput.Parameters.IsValid())
			{
				if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
				{
					PostAssemblyParamOutput.Parameters.CopyMatchingValuesByName(*AssemblyParameters);
				}
				AssemblyOutput.PostAssemblyParameters.Edit().Add(Params.BaseItemPath, MoveTemp(PostAssemblyParamOutput));
			}
		}

		OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
		return;
	}

	// Crowd pipelines rebuild material slots while generating meshes, so SlotIndices targeting is
	// unsupported. Drop any such parameters now (with a warning) and only use the filtered set.
	const FString ItemContext = GroomAssemblyInput.TargetItem.ToDebugString();
	const TArray<FMetaHumanMaterialParameter> FilteredRuntimeMaterialParameters =
		UE::MetaHuman::MaterialUtils::FilterToCrowdSupportedParameters(RuntimeMaterialParameters, /*bLogWarningOnFilter*/ true, ItemContext);

	if (!FilteredRuntimeMaterialParameters.IsEmpty())
	{
		const TArray<FSkeletalMaterial>& Materials = GroomBundle->Materials;
		FMetaHumanPostAssemblyParameterOutput PostAssemblyParamOutput;
		const UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate FetchSlotName = UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(Materials);

		UE::MetaHuman::MaterialUtils::ProcessAssemblyParameters(
			{},
			FilteredRuntimeMaterialParameters,
			ItemContext,
			{},
			Materials.Num(),
			FetchSlotName,
			UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(Materials),
			Params.OuterForGeneratedObjects,
			GroomAssemblyOutput.OverrideMaterials,
			PostAssemblyParamOutput.Parameters,
			AssemblyOutput.Metadata);

		if (PostAssemblyParamOutput.Parameters.IsValid())
		{
			if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
			{
				UE::MetaHuman::MaterialUtils::SetInstanceParameters(
					FilteredRuntimeMaterialParameters,
					UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(GroomAssemblyOutput.OverrideMaterials),
					FetchSlotName,
					*AssemblyParameters);

				PostAssemblyParamOutput.Parameters.CopyMatchingValuesByName(*AssemblyParameters);
			}

			AssemblyOutput.PostAssemblyParameters.Edit().Add(Params.BaseItemPath, MoveTemp(PostAssemblyParamOutput));
		}
	}

	// Populate instanced mesh material bindings from the pipeline's InstancedComponentOverrideMaterials.
	//
	// Custom-data floats live on the assembly output as a single ISKM-wide flat buffer; per-slot
	// CustomDataOffsets index into that buffer with non-overlapping ranges across slots.
	const FMetaHumanCrowdGroomMaterialOverride* InstancedOverrideForLookup =
		GroomBuildOutput.InstancedMaterialOverrides.Find(GroomAssemblyInput.TargetItem);
	for (const TPair<FName, FMetaHumanCrowdOutfitInstancedMaterial>& Pair : InstancedComponentOverrideMaterials)
	{
		UMaterialInterface* MaterialToAssign = Pair.Value.InstancedComponentMaterial;
		if (InstancedOverrideForLookup)
		{
			const FSkeletalMaterial* MatchingVariantMaterial = InstancedOverrideForLookup->Materials.FindByPredicate(
				[&Pair](const FSkeletalMaterial& InMaterial) { return InMaterial.MaterialSlotName == Pair.Key; });
			if (MatchingVariantMaterial && MatchingVariantMaterial->MaterialInterface)
			{
				MaterialToAssign = MatchingVariantMaterial->MaterialInterface;
			}
		}
		GroomAssemblyOutput.InstancedMaterialData.Add(Pair.Key, MaterialToAssign);
	}

	// Size the ISKM-wide custom-data buffer once and validate that authored offsets across slots
	// don't collide. The validation is a soft warning -- runtime writes still happen via absolute
	// offsets and silent corruption follows if an overlap goes uncaught here.
	//
	// Validation is scoped to the slot names present on this fitted mesh: fitted grooms derived
	// from a single source may carry disjoint subsets of the source's slots, and offsets that
	// would collide on the source's full slot set can be safely shared between slots that never
	// co-occur on any one fitted mesh.
	TArray<FName> MeshSlotNames;
	MeshSlotNames.Reserve(GroomBundle->Materials.Num());
	for (const FSkeletalMaterial& Material : GroomBundle->Materials)
	{
		MeshSlotNames.Add(Material.MaterialSlotName);
	}
	UE::MetaHuman::MaterialUtils::ValidateNoOverlappingCustomDataOffsets(InstancedComponentOverrideMaterials, MeshSlotNames, ItemContext);
	GroomAssemblyOutput.InstancedMeshCustomDataFloats.SetNumZeroed(
		UE::MetaHuman::MaterialUtils::ComputeISKMCustomDataSize(InstancedComponentOverrideMaterials));

	// Populate initial values from assembly parameters into the ISKM-wide buffer
	if (!GroomAssemblyOutput.InstancedMeshCustomDataFloats.IsEmpty())
	{
		UE::MetaHuman::MaterialUtils::SetInstanceParameterDefaultsOnCustomData(
			FilteredRuntimeMaterialParameters,
			InstancedComponentOverrideMaterials,
			GroomAssemblyOutput.InstancedMeshCustomDataFloats);

		if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
		{
			UE::MetaHuman::MaterialUtils::SetInstanceParametersOnCustomData(
				FilteredRuntimeMaterialParameters,
				InstancedComponentOverrideMaterials,
				*AssemblyParameters,
				GroomAssemblyOutput.InstancedMeshCustomDataFloats);
		}
	}

	// Build instanced-variant MIDs from the instanced bundle's MICs (which may be parented to
	// HelmetsMaterial for helmet LODs, distinct from the actor bundle's parents). Each MID
	// goes into InstancedMaterialData by slot - SetPostAssemblyParameters updates them
	// alongside the actor MIDs.
	if (const FMetaHumanCrowdGroomMaterialOverride* InstancedOverride =
			GroomBuildOutput.InstancedMaterialOverrides.Find(GroomAssemblyInput.TargetItem);
		InstancedOverride && !FilteredRuntimeMaterialParameters.IsEmpty())
	{
		const TArray<FSkeletalMaterial>& InstancedMaterials = InstancedOverride->Materials;
		if (!InstancedMaterials.IsEmpty())
		{
			const UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate FetchInstancedSlotName =
				UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(InstancedMaterials);

			TMap<FName, TObjectPtr<UMaterialInterface>> InstancedMIDMap;
			FInstancedPropertyBag UnusedParameters;

			UE::MetaHuman::MaterialUtils::ProcessAssemblyParameters(
				{},
				FilteredRuntimeMaterialParameters,
				ItemContext,
				{},
				InstancedMaterials.Num(),
				FetchInstancedSlotName,
				UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(InstancedMaterials),
				Params.OuterForGeneratedObjects,
				InstancedMIDMap,
				UnusedParameters,
				AssemblyOutput.Metadata);

			// Apply the initial parameter values from the assembly parameter bag, mirroring
			// the actor-path block above.
			if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
			{
				UE::MetaHuman::MaterialUtils::SetInstanceParameters(
					FilteredRuntimeMaterialParameters,
					UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(InstancedMIDMap),
					FetchInstancedSlotName,
					*AssemblyParameters);
			}

			// Push each instanced MID into InstancedMaterialData by slot. Slots already populated
			// by InstancedComponentOverrideMaterials (above) take precedence and aren't overwritten --
			// they use the authored ISKM material with the per-instance custom-data float buffer
			// (GroomAssemblyOutput.InstancedMeshCustomDataFloats) for parameter-driven variation,
			// rather than a MID.
			for (TPair<FName, TObjectPtr<UMaterialInterface>>& MIDPair : InstancedMIDMap)
			{
				if (!InstancedComponentOverrideMaterials.Contains(MIDPair.Key))
				{
					GroomAssemblyOutput.InstancedMaterialData.Add(MIDPair.Key, MIDPair.Value);
				}
			}
		}
	}

	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

void UMetaHumanCrowdGroomPipeline::SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const
{
	check(Params.ModifiedPostAssemblyParameters.IsValid());

	FMetaHumanCrowdGroomAssemblyOutput* GroomAssemblyOutput = InOutItemAssemblyOutput.GetMutablePtr<FMetaHumanCrowdGroomAssemblyOutput>();
	if (!GroomAssemblyOutput)
	{
		return;
	}

	const TArray<FMetaHumanMaterialParameter> FilteredRuntimeMaterialParameters = UE::MetaHuman::MaterialUtils::FilterToCrowdSupportedParameters(RuntimeMaterialParameters);

	UE::MetaHuman::MaterialUtils::SetInstanceParameters(
		FilteredRuntimeMaterialParameters,
		UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(GroomAssemblyOutput->OverrideMaterials),
		UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate(),
		Params.ModifiedPostAssemblyParameters);

	// Update instanced MIDs and per-instance custom data floats. Each slot in
	// InstancedMaterialData may use either path:
	//  - If Material is a UMaterialInstanceDynamic (created from the instanced bundle in
	//    AssembleItem), parameter writes go to that MID via SetInstanceParameters below.
	//  - If the slot was authored with InstancedComponentOverrideMaterials, parameter values
	//    flow through the ISKM-wide custom-data float buffer instead.
	TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> InstancedMIDMap;
	for (TPair<FName, TObjectPtr<UMaterialInterface>>& Pair : GroomAssemblyOutput->InstancedMaterialData)
	{
		if (InstancedComponentOverrideMaterials.Contains(Pair.Key))
		{
			continue;
		}
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Pair.Value))
		{
			InstancedMIDMap.Add(Pair.Key, MID);
		}
	}

	if (!InstancedMIDMap.IsEmpty())
	{
		UE::MetaHuman::MaterialUtils::SetInstanceParameters(
			FilteredRuntimeMaterialParameters,
			InstancedMIDMap,
			UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate(),
			Params.ModifiedPostAssemblyParameters);
	}

	// Update the ISKM-wide per-instance custom-data float buffer if present.
	if (!GroomAssemblyOutput->InstancedMeshCustomDataFloats.IsEmpty())
	{
		UE::MetaHuman::MaterialUtils::SetInstanceParametersOnCustomData(
			FilteredRuntimeMaterialParameters,
			InstancedComponentOverrideMaterials,
			Params.ModifiedPostAssemblyParameters,
			GroomAssemblyOutput->InstancedMeshCustomDataFloats);
	}
}

TNotNull<const UMetaHumanCharacterPipelineSpecification*> UMetaHumanCrowdGroomPipeline::GetSpecification() const
{
	return Specification;
}

#if WITH_EDITOR
void UMetaHumanCrowdGroomPipeline::UpdateParameters()
{
	if (bUseCustomRuntimeMaterialParameters)
	{
		return;
	}

	auto AddRuntimeParameter = [this](TNotNull<FProperty*> InProperty, const FName& InMaterialParameterName)
	{
		using namespace UE::MetaHuman::MaterialUtils;

		FMetaHumanMaterialParameter& Param = RuntimeMaterialParameters.AddDefaulted_GetRef();
		Param.InstanceParameterName = InProperty->GetFName();
		// We assume that all groom material slots support the standard hair parameters
		Param.SlotTarget = EMetaHumanRuntimeMaterialParameterSlotTarget::AllSlots;
		Param.MaterialParameter.Name = InMaterialParameterName;
		Param.ParameterType = PropertyToParameterType(InProperty);
		Param.PropertyMetadata = CopyMetadataFromProperty(InProperty);
	};

	RuntimeMaterialParameters.Empty();

	for (TFieldIterator<FProperty> PropertyIterator(UMetaHumanCrowdGroomPipelineMaterialParameters::StaticClass()); PropertyIterator; ++PropertyIterator)
	{
		FProperty* Property = *PropertyIterator;

		if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		const FString MaterialParamName = Property->GetMetaData("MaterialParamName");

		if (MaterialParamName.IsEmpty())
		{
			continue;
		}

		AddRuntimeParameter(Property, FName(MaterialParamName));
	}
}
#endif // WITH_EDITOR
