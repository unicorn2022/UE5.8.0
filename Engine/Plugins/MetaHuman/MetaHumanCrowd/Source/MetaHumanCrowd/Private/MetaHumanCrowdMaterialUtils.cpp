// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCrowdMaterialUtils.h"

#include "Item/MetaHumanCrowdOutfitPipeline.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"
#include "MetaHumanCrowdLog.h"

#include "Algo/Find.h"
#include "Logging/StructuredLog.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "StructUtils/PropertyBag.h"

namespace UE::MetaHuman::MaterialUtils
{
	void SetInstanceParametersOnCustomData(
		const TArray<FMetaHumanMaterialParameter>& InMaterialParameters,
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& InInstancedComponentOverrideMaterials,
		const FInstancedPropertyBag& InPropertyBag,
		TArrayView<float> OutCustomData)
	{
		const UPropertyBag* PropertyBag = InPropertyBag.GetPropertyBagStruct();
		if (!PropertyBag)
		{
			return;
		}

		for (const FPropertyBagPropertyDesc& PropertyDesc : PropertyBag->GetPropertyDescs())
		{
			const FMetaHumanMaterialParameter* Parameter = Algo::FindBy(InMaterialParameters, PropertyDesc.Name, &FMetaHumanMaterialParameter::InstanceParameterName);
			if (!Parameter)
			{
				// TODO: log error as this suggests InMaterialParameters have changed since assembly
				continue;
			}

			// Crowd pipelines support SlotNames and AllSlots only. SlotIndices-targeted parameters
			// should already have been filtered out by FilterToCrowdSupportedParameters.
			TArray<FName> SlotNames;
			switch (Parameter->SlotTarget)
			{
				case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotNames:
					SlotNames = Parameter->SlotNames;
					break;
				case EMetaHumanRuntimeMaterialParameterSlotTarget::AllSlots:
					InInstancedComponentOverrideMaterials.GetKeys(SlotNames);
					break;
				default:
					continue;
			}

			for (const FName SlotName : SlotNames)
			{
				const FMetaHumanCrowdOutfitInstancedMaterial* InstancedMaterial = InInstancedComponentOverrideMaterials.Find(SlotName);

				if (!InstancedMaterial)
				{
					// TODO: log error. This shouldn't happen
					continue;
				}

				const FMetaHumanCrowdOutfitCustomDataFormat* CustomDataFormat = InstancedMaterial->InstanceParameterNameToCustomDataFormat.Find(Parameter->InstanceParameterName);
				if (!CustomDataFormat)
				{
					// TODO: log error. This shouldn't happen
					continue;
				}

				switch (Parameter->ParameterType)
				{
					case EMetaHumanRuntimeMaterialParameterType::Toggle:
					{
						const TValueOrError<bool, EPropertyBagResult> Result = InPropertyBag.GetValueBool(PropertyDesc);
						if (!Result.HasValue())
						{
							break;
						}

						if (!OutCustomData.IsValidIndex(CustomDataFormat->CustomDataOffset))
						{
							// TODO: log error. This shouldn't happen
							continue;
						}

						OutCustomData[CustomDataFormat->CustomDataOffset] = Result.GetValue() ? 1.0f : 0.0f;
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::Scalar:
					{
						const TValueOrError<float, EPropertyBagResult> Result = InPropertyBag.GetValueFloat(PropertyDesc);
						if (!Result.HasValue())
						{
							break;
						}

						if (!OutCustomData.IsValidIndex(CustomDataFormat->CustomDataOffset))
						{
							// TODO: log error. This shouldn't happen
							continue;
						}

						OutCustomData[CustomDataFormat->CustomDataOffset] = Result.GetValue();
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::Vector:
					{
						const TValueOrError<FLinearColor*, EPropertyBagResult> Result = InPropertyBag.GetValueStruct<FLinearColor>(PropertyDesc);
						if (!Result.HasValue()
							|| Result.GetValue() == nullptr)
						{
							break;
						}

						int32 ComponentCustomDataOffset = CustomDataFormat->CustomDataOffset;
						for (int32 ComponentIndex = 0; ComponentIndex < 4; ComponentIndex++)
						{
							bool bIsComponentUsed = false;
							switch (ComponentIndex)
							{
								case 0:
									bIsComponentUsed = CustomDataFormat->bUseChannelR;
									break;

								case 1:
									bIsComponentUsed = CustomDataFormat->bUseChannelG;
									break;

								case 2:
									bIsComponentUsed = CustomDataFormat->bUseChannelB;
									break;

								case 3:
									bIsComponentUsed = CustomDataFormat->bUseChannelA;
									break;

								default:
									checkNoEntry();
							}

							if (!bIsComponentUsed)
							{
								continue;
							}

							if (!OutCustomData.IsValidIndex(ComponentCustomDataOffset))
							{
								// TODO: log error
								continue;
							}

							OutCustomData[ComponentCustomDataOffset] = Result.GetValue()->Component(ComponentIndex);
							ComponentCustomDataOffset++;
						}
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::DoubleVector:
					{
						// TODO: support this
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::Texture:

					case EMetaHumanRuntimeMaterialParameterType::TextureCollection:

					case EMetaHumanRuntimeMaterialParameterType::Font:

					case EMetaHumanRuntimeMaterialParameterType::RuntimeVirtualTexture:

					case EMetaHumanRuntimeMaterialParameterType::SparseVolumeTexture:
						break;
				}
			}
		}
	}

	void SetInstanceParameterDefaultsOnCustomData(
		const TArray<FMetaHumanMaterialParameter>& InMaterialParameters,
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& InInstancedComponentOverrideMaterials,
		TArrayView<float> OutCustomData)
	{
		// Walk every (slot, parameter) pair and write the slot's InstancedComponentMaterial
		// default value into OutCustomData at the slot's authored CustomDataOffset.
		//
		// This ensures that any custom floats that aren't written by overridden Instance Parameters
		// will have default values from the material instead of being zeroed.

		for (const TPair<FName, FMetaHumanCrowdOutfitInstancedMaterial>& SlotPair : InInstancedComponentOverrideMaterials)
		{
			const UMaterialInterface* InstancedMaterial = SlotPair.Value.InstancedComponentMaterial;
			if (!InstancedMaterial)
			{
				continue;
			}

			for (const FMetaHumanMaterialParameter& Parameter : InMaterialParameters)
			{
				// Only seed parameters that actually target this slot. AllSlots and SlotNames
				// matching this slot count; SlotIndices is unsupported in the crowd path and
				// should already be filtered out by FilterToCrowdSupportedParameters.
				bool bAppliesToSlot = false;
				switch (Parameter.SlotTarget)
				{
					case EMetaHumanRuntimeMaterialParameterSlotTarget::AllSlots:
						bAppliesToSlot = true;
						break;
					case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotNames:
						bAppliesToSlot = Parameter.SlotNames.Contains(SlotPair.Key);
						break;
					case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotIndices:
					default:
						break;
				}
				if (!bAppliesToSlot)
				{
					continue;
				}

				const FMetaHumanCrowdOutfitCustomDataFormat* CustomDataFormat =
					SlotPair.Value.InstanceParameterNameToCustomDataFormat.Find(Parameter.InstanceParameterName);
				if (!CustomDataFormat)
				{
					// Parameter has no custom-data binding on this slot -- nothing to seed.
					continue;
				}

				switch (Parameter.ParameterType)
				{
					case EMetaHumanRuntimeMaterialParameterType::Toggle:
					{
						FMaterialParameterMetadata MaterialValue;
						if (!InstancedMaterial->GetParameterValue(EMaterialParameterType::Scalar, Parameter.MaterialParameter, MaterialValue))
						{
							break;
						}
						if (!OutCustomData.IsValidIndex(CustomDataFormat->CustomDataOffset))
						{
							break;
						}
						OutCustomData[CustomDataFormat->CustomDataOffset] = MaterialValue.Value.AsScalar() > 0.0f ? 1.0f : 0.0f;
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::Scalar:
					{
						FMaterialParameterMetadata MaterialValue;
						if (!InstancedMaterial->GetParameterValue(EMaterialParameterType::Scalar, Parameter.MaterialParameter, MaterialValue))
						{
							break;
						}
						if (!OutCustomData.IsValidIndex(CustomDataFormat->CustomDataOffset))
						{
							break;
						}
						OutCustomData[CustomDataFormat->CustomDataOffset] = MaterialValue.Value.AsScalar();
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::Vector:
					{
						FMaterialParameterMetadata MaterialValue;
						if (!InstancedMaterial->GetParameterValue(EMaterialParameterType::Vector, Parameter.MaterialParameter, MaterialValue))
						{
							break;
						}
						const FLinearColor Value = MaterialValue.Value.AsLinearColor();

						int32 ComponentCustomDataOffset = CustomDataFormat->CustomDataOffset;
						for (int32 ComponentIndex = 0; ComponentIndex < 4; ComponentIndex++)
						{
							bool bIsComponentUsed = false;
							switch (ComponentIndex)
							{
								case 0:
									bIsComponentUsed = CustomDataFormat->bUseChannelR;
									break;
								case 1:
									bIsComponentUsed = CustomDataFormat->bUseChannelG;
									break;
								case 2:
									bIsComponentUsed = CustomDataFormat->bUseChannelB;
									break;
								case 3:
									bIsComponentUsed = CustomDataFormat->bUseChannelA;
									break;
								default:
									checkNoEntry();
							}

							if (!bIsComponentUsed)
							{
								continue;
							}

							if (!OutCustomData.IsValidIndex(ComponentCustomDataOffset))
							{
								continue;
							}

							OutCustomData[ComponentCustomDataOffset] = Value.Component(ComponentIndex);
							ComponentCustomDataOffset++;
						}
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::DoubleVector:
					case EMetaHumanRuntimeMaterialParameterType::Texture:
					case EMetaHumanRuntimeMaterialParameterType::TextureCollection:
					case EMetaHumanRuntimeMaterialParameterType::Font:
					case EMetaHumanRuntimeMaterialParameterType::RuntimeVirtualTexture:
					case EMetaHumanRuntimeMaterialParameterType::SparseVolumeTexture:
						// Not representable as custom-data floats.
						break;
				}
			}
		}
	}

	TArray<FMetaHumanMaterialParameter> FilterToCrowdSupportedParameters(
		TConstArrayView<FMetaHumanMaterialParameter> InParameters,
		bool bLogWarningOnFilter,
		const FString& InContextForLogging)
	{
		TArray<FMetaHumanMaterialParameter> Filtered;
		Filtered.Reserve(InParameters.Num());

		for (const FMetaHumanMaterialParameter& Parameter : InParameters)
		{
			if (Parameter.SlotTarget == EMetaHumanRuntimeMaterialParameterSlotTarget::SlotIndices)
			{
				if (bLogWarningOnFilter)
				{
					UE_LOGFMT(LogMetaHumanCrowd, Warning,
						"Crowd pipeline ({Context}) skipped runtime material parameter '{Parameter}': "
						"SlotIndices targeting is not supported by the crowd pipeline because material "
						"slots are rebuilt during mesh generation. Use SlotNames or AllSlots instead.",
						InContextForLogging,
						Parameter.InstanceParameterName);
				}
				continue;
			}

			Filtered.Add(Parameter);
		}

		return Filtered;
	}

	int32 ComputeCustomDataSize(
		const TMap<FName, FMetaHumanCrowdOutfitCustomDataFormat>& InFormatMap)
	{
		int32 MaxOffset = -1;
		for (const TPair<FName, FMetaHumanCrowdOutfitCustomDataFormat>& FormatPair : InFormatMap)
		{
			const FMetaHumanCrowdOutfitCustomDataFormat& Format = FormatPair.Value;
			int32 SlotSize = Format.CustomDataOffset + 1;
			// Vector parameters occupy one float per enabled channel, starting at CustomDataOffset.
			if (Format.bUseChannelR || Format.bUseChannelG || Format.bUseChannelB || Format.bUseChannelA)
			{
				const int32 NumChannels = (int32)Format.bUseChannelR + (int32)Format.bUseChannelG
					+ (int32)Format.bUseChannelB + (int32)Format.bUseChannelA;
				SlotSize = Format.CustomDataOffset + NumChannels;
			}
			MaxOffset = FMath::Max(MaxOffset, SlotSize - 1);
		}
		return MaxOffset + 1;
	}

	int32 ComputeISKMCustomDataSize(
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& InInstancedComponentOverrideMaterials)
	{
		int32 MaxSize = 0;
		for (const TPair<FName, FMetaHumanCrowdOutfitInstancedMaterial>& SlotPair : InInstancedComponentOverrideMaterials)
		{
			MaxSize = FMath::Max(MaxSize, ComputeCustomDataSize(SlotPair.Value.InstanceParameterNameToCustomDataFormat));
		}
		return MaxSize;
	}

	bool ValidateNoOverlappingCustomDataOffsets(
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& InInstancedComponentOverrideMaterials,
		TConstArrayView<FName> InMeshSlotNames,
		const FString& InContextForLogging)
	{
		// Collect (Offset, NumFloats, OwnerSlot, OwnerParameter) tuples for every parameter on
		// every slot, then sort by offset and walk for overlaps. Sorting beats N^2 across all
		// slot/parameter pairs and the data is small (one entry per material parameter total).
		//
		// Restrict to slots actually present on the mesh being assembled. The source outfit's
		// InstancedComponentOverrideMaterials is authored against the full slot set, but a fitted
		// mesh may only carry a subset of slots. Two such never-co-occurring slots may 
		// legitimately share a CustomDataOffset, so we must not flag them.
		TSet<FName> MeshSlotNameSet;
		MeshSlotNameSet.Reserve(InMeshSlotNames.Num());
		for (const FName SlotName : InMeshSlotNames)
		{
			MeshSlotNameSet.Add(SlotName);
		}

		struct FRange
		{
			int32 Start = 0;
			// exclusive
			int32 End = 0;
			FName SlotName;
			FName ParameterName;
			// Packed channel mask (R=1, G=2, B=4, A=8). 0 for non-vector parameters.
			uint8 ChannelMask = 0;
		};

		TArray<FRange> Ranges;
		for (const TPair<FName, FMetaHumanCrowdOutfitInstancedMaterial>& SlotPair : InInstancedComponentOverrideMaterials)
		{
			const FName SlotName = SlotPair.Key;
			if (!MeshSlotNameSet.Contains(SlotName))
			{
				// Slot isn't on the mesh being assembled, so any offset it authors can't collide
				// with offsets that are on this mesh.
				continue;
			}
			for (const TPair<FName, FMetaHumanCrowdOutfitCustomDataFormat>& FormatPair : SlotPair.Value.InstanceParameterNameToCustomDataFormat)
			{
				const FMetaHumanCrowdOutfitCustomDataFormat& Format = FormatPair.Value;
				if (Format.CustomDataOffset == INDEX_NONE)
				{
					continue;
				}

				int32 NumFloats = 1;
				if (Format.bUseChannelR || Format.bUseChannelG || Format.bUseChannelB || Format.bUseChannelA)
				{
					NumFloats = (int32)Format.bUseChannelR + (int32)Format.bUseChannelG
						+ (int32)Format.bUseChannelB + (int32)Format.bUseChannelA;
				}

				FRange& Range = Ranges.AddDefaulted_GetRef();
				Range.Start = Format.CustomDataOffset;
				Range.End = Format.CustomDataOffset + NumFloats;
				Range.SlotName = SlotName;
				Range.ParameterName = FormatPair.Key;
				Range.ChannelMask =
					  (Format.bUseChannelR ? 0x1 : 0)
					| (Format.bUseChannelG ? 0x2 : 0)
					| (Format.bUseChannelB ? 0x4 : 0)
					| (Format.bUseChannelA ? 0x8 : 0);
			}
		}

		// Sort by Start ascending, then End descending so the widest range at any given Start
		// comes first. ParameterName breaks ties deterministically for stable warnings.
		Ranges.Sort([](const FRange& A, const FRange& B)
		{
			if (A.Start != B.Start)
			{
				return A.Start < B.Start;
			}
			if (A.End != B.End)
			{
				return A.End > B.End;
			}
			return A.ParameterName.LexicalLess(B.ParameterName);
		});

		// Sweep against the range with the furthest End seen so far
		bool bValid = true;
		int32 FrontierIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Ranges.Num(); ++Index)
		{
			const FRange& Curr = Ranges[Index];
			if (FrontierIndex == INDEX_NONE)
			{
				FrontierIndex = Index;
				continue;
			}

			const FRange& Frontier = Ranges[FrontierIndex];
			if (Curr.Start >= Frontier.End)
			{
				FrontierIndex = Index;
				continue;
			}

			// Identical declaration: same parameter authored on multiple materials with the
			// same offset/channels - treat as an alias of the frontier, not a collision.
			const bool bIdenticalDeclaration =
				Curr.Start == Frontier.Start
				&& Curr.End == Frontier.End
				&& Curr.ParameterName == Frontier.ParameterName
				&& Curr.ChannelMask == Frontier.ChannelMask;
			if (bIdenticalDeclaration)
			{
				continue;
			}

			bValid = false;
			UE_LOGFMT(LogMetaHumanCrowd, Warning,
				"Crowd pipeline ({Context}) custom-data offsets overlap on a single ISKM: "
				"slot '{SlotA}' parameter '{ParamA}' [{StartA}..{EndA}) collides with "
				"slot '{SlotB}' parameter '{ParamB}' [{StartB}..{EndB}). "
				"Per-instance custom data will be silently corrupted at runtime.",
				InContextForLogging,
				Frontier.SlotName, Frontier.ParameterName, Frontier.Start, Frontier.End,
				Curr.SlotName, Curr.ParameterName, Curr.Start, Curr.End);

			// Advance the frontier only when Curr extends further
			if (Curr.End > Frontier.End)
			{
				FrontierIndex = Index;
			}
		}

		return bValid;
	}
} // namespace UE::MetaHuman::MaterialUtils
