// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/LegacyClothingConverter.h"

#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ImportedValue.h"
#include "ChaosClothAsset/ProxyDeformerNode.h"
#include "ChaosClothAsset/ReferenceBoneNode.h"
#include "ChaosClothAsset/SelectionNode.h"
#include "ChaosClothAsset/SetPhysicsAssetNode.h"
#include "ChaosClothAsset/SkeletalMeshImportNode.h"
#include "ChaosClothAsset/SkinningBlendNode.h"
#include "ChaosClothAsset/SimulationAerodynamicsConfigNode.h"
#include "ChaosClothAsset/SimulationAnimDriveConfigNode.h"
#include "ChaosClothAsset/SimulationBackstopConfigNode.h"
#include "ChaosClothAsset/SimulationBendingConfigNode.h"
#include "ChaosClothAsset/SimulationCollisionConfigNode.h"
#include "ChaosClothAsset/SimulationDampingConfigNode.h"
#include "ChaosClothAsset/SimulationGravityConfigNode.h"
#include "ChaosClothAsset/SimulationLongRangeAttachmentConfigNode.h"
#include "ChaosClothAsset/SimulationMassConfigNode.h"
#include "ChaosClothAsset/SimulationMaxDistanceConfigNode.h"
#include "ChaosClothAsset/SimulationPressureConfigNode.h"
#include "ChaosClothAsset/SimulationSelfCollisionConfigNode.h"
#include "ChaosClothAsset/SimulationConfigNodePropertyTypes.h"
#include "ChaosClothAsset/SimulationSelfCollisionSpheresConfigNode.h"
#include "ChaosClothAsset/SimulationSolverConfigNode.h"
#include "ChaosClothAsset/SimulationStretchConfigNode.h"
#include "ChaosClothAsset/SimulationVelocityScaleConfigNode.h"
#include "ChaosClothAsset/WeightMapNode.h"
#include "ChaosClothAsset/WeightedValue.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ClothingAsset.h"
#include "ClothLODData.h"
#include "ClothPhysicalMeshData.h"
#include "ClothTetherData.h"
#include "ClothVertBoneData.h"
#include "Dataflow/DataflowAssetEditUtils.h"
#include "Dataflow/DataflowBlueprintLibrary.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowGraph.h"
#include "Dataflow/DataflowInstance.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "ReferenceSkeleton.h"
#include "PointWeightMap.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogChaosClothAssetLegacyClothingConverter, Log, All);

#define LOCTEXT_NAMESPACE "ChaosClothAssetLegacyClothingConverter"

namespace UE::Chaos::ClothAsset
{
	namespace Private
	{
		static UDataflow* GetEmbeddedDataflow(UChaosClothAsset* InAsset)
		{
			if (!InAsset)
			{
				return nullptr;
			}
			return InAsset->GetDataflowInstance().GetDataflowAsset();
		}

		/** Find a node by name; ensure on miss so template drift surfaces, then return nullptr. */
		static TSharedPtr<FDataflowNode> FindNodeByName(UDataflow* InDataflow, FName Name)
		{
			if (!InDataflow)
			{
				return nullptr;
			}
			TSharedPtr<UE::Dataflow::FGraph> Graph = InDataflow->GetDataflow();
			if (!Graph)
			{
				return nullptr;
			}
			TSharedPtr<FDataflowNode> Found = Graph->FindBaseNode(Name);
			ensureMsgf(Found.IsValid(),
				TEXT("Legacy conversion template is missing the expected '%s' node; conversion will skip the write that depends on it."),
				*Name.ToString());
			return Found;
		}

		/** Find the first node whose USTRUCT matches TNodeStruct; ensure on miss so template drift surfaces. */
		template <typename TNodeStruct>
		static TSharedPtr<FDataflowNode> FindNodeByType(UDataflow* InDataflow)
		{
			if (!InDataflow)
			{
				return nullptr;
			}
			TSharedPtr<UE::Dataflow::FGraph> Graph = InDataflow->GetDataflow();
			if (!Graph)
			{
				return nullptr;
			}
			const UScriptStruct* const TargetStruct = TNodeStruct::StaticStruct();
			for (const TSharedPtr<FDataflowNode>& Node : Graph->GetNodes())
			{
				if (Node && Node->TypedScriptStruct() == TargetStruct)
				{
					return Node;
				}
			}
			ensureMsgf(false,
				TEXT("Legacy conversion template is missing the expected '%s' node; conversion will skip the write that depends on it."),
				TargetStruct ? *TargetStruct->GetName() : TEXT("<unknown>"));
			return nullptr;
		}

		/**
		 * Reflection-based typed pointer into a node's UPROPERTY. TValue is not checked against the
		 * property's declared CPP type -- but sizeof(TValue) is, so a property whose type silently
		 * changes under the converter surfaces an ensure instead of corrupting adjacent memory.
		 */
		template <typename TValue>
		static TValue* GetNodePropertyPtr(FDataflowNode* Node, FName PropertyName)
		{
			if (!Node)
			{
				return nullptr;
			}
			const UScriptStruct* const Struct = Node->TypedScriptStruct();
			if (!Struct)
			{
				return nullptr;
			}
			const FProperty* const Property = Struct->FindPropertyByName(PropertyName);
			if (!Property)
			{
				return nullptr;
			}
			if (!ensureMsgf(Property->GetSize() == sizeof(TValue),
				TEXT("Legacy converter is writing '%s.%s' as a %d-byte value but the property is %d bytes -- the property type likely changed under the converter. Skipping the write so adjacent memory is not corrupted."),
				*Struct->GetName(), *PropertyName.ToString(),
				static_cast<int32>(sizeof(TValue)), static_cast<int32>(Property->GetSize())))
			{
				return nullptr;
			}
			return Property->ContainerPtrToValuePtr<TValue>(Node);
		}

		template <typename TWeighted>
		static void WriteScalarToWeighted(FDataflowNode* Node, FName Prop, float Value)
		{
			if (TWeighted* const W = GetNodePropertyPtr<TWeighted>(Node, Prop))
			{
				W->Low = Value;
				W->High = Value;
			}
		}

		template <typename TWeighted>
		static void WriteLegacyWeighted(FDataflowNode* Node, FName Prop, const FChaosClothWeightedValue& Legacy)
		{
			if (TWeighted* const W = GetNodePropertyPtr<TWeighted>(Node, Prop))
			{
				W->Low = Legacy.Low;
				W->High = Legacy.High;
			}
		}

		template <typename TImported, typename TValue>
		static void WriteToImported(FDataflowNode* Node, FName Prop, const TValue& Value)
		{
			if (TImported* const I = GetNodePropertyPtr<TImported>(Node, Prop))
			{
				I->ImportedValue = Value;
			}
		}

		template <typename TValue>
		static void WritePlain(FDataflowNode* Node, FName Prop, const TValue& Value)
		{
			if (TValue* const V = GetNodePropertyPtr<TValue>(Node, Prop))
			{
				*V = Value;
			}
		}

		/** Returns max(values, 1) so dividing by the result is a safe no-op when the mask is already [0,1]. */
		static float ComputeLegacyMax(TConstArrayView<float> Values)
		{
			float Max = 0.f;
			for (const float V : Values)
			{
				Max = FMath::Max(Max, V);
			}
			return Max > 1.f ? Max : 1.f;
		}

		/** Reflection-based Low/High extractor that works on any weighted-value struct variant. */
		static bool FindLowHighFloats(FDataflowNode* Node, FName WeightedPropName, float*& OutLow, float*& OutHigh)
		{
			OutLow = nullptr;
			OutHigh = nullptr;
			if (!Node)
			{
				return false;
			}
			const UScriptStruct* const NodeStruct = Node->TypedScriptStruct();
			if (!NodeStruct)
			{
				return false;
			}
			FStructProperty* const StructProp = CastField<FStructProperty>(NodeStruct->FindPropertyByName(WeightedPropName));
			if (!StructProp || !StructProp->Struct)
			{
				return false;
			}
			void* const StructPtr = StructProp->ContainerPtrToValuePtr<void>(Node);
			if (!StructPtr)
			{
				return false;
			}
			if (FFloatProperty* const LowFloat = CastField<FFloatProperty>(StructProp->Struct->FindPropertyByName(TEXT("Low"))))
			{
				OutLow = LowFloat->ContainerPtrToValuePtr<float>(StructPtr);
			}
			if (FFloatProperty* const HighFloat = CastField<FFloatProperty>(StructProp->Struct->FindPropertyByName(TEXT("High"))))
			{
				OutHigh = HighFloat->ContainerPtrToValuePtr<float>(StructPtr);
			}
			return OutLow != nullptr && OutHigh != nullptr;
		}

		/** Per-vertex-output preserving rescale modes; see RescaleConsumerLowHigh for the invariant. */
		enum class EWeightMapRescaleMode : uint8
		{
			Value,    // Absolute-distance masks (MaxDistance / BackstopDistance / BackstopRadius).
			Weighted, // [0,1] blend weight against the consumer's Low/High span.
		};

		/**
		 * Counter-scale a consumer's Low/High to undo the LegacyMax normalization so the per-vertex
		 * output stays bit-equal to the legacy mask. Value-mode sets [0, LegacyMax]; Weighted-mode
		 * expands the existing span by LegacyMax (no-op when span is flat or LegacyMax == 1).
		 */
		static void RescaleConsumerLowHigh(FDataflowNode* Node, FName WeightedPropName, float LegacyMax, EWeightMapRescaleMode Mode)
		{
			float* Low = nullptr;
			float* High = nullptr;
			if (!FindLowHighFloats(Node, WeightedPropName, Low, High))
			{
				return;
			}
			switch (Mode)
			{
			case EWeightMapRescaleMode::Value:
				if (Low)  { *Low  = 0.0f; }
				if (High) { *High = LegacyMax; }
				break;
			case EWeightMapRescaleMode::Weighted:
				if (Low && High)
				{
					const float LegacyLow  = *Low;
					const float LegacyHigh = *High;
					*High = LegacyLow + LegacyMax * (LegacyHigh - LegacyLow);
					// Low stays at LegacyLow.
				}
				break;
			}
		}

		/** ConsumerNode is null when the mask has no binding (e.g. TetherEndsMask is handled by the SelectionNode path). */
		struct FWeightMapConsumer
		{
			TSharedPtr<FDataflowNode> ConsumerNode;
			FString                   ConsumerWeightMapPinName;
			EWeightMapRescaleMode     RescaleMode = EWeightMapRescaleMode::Weighted;
		};

		/** TetherEndsMask is intentionally absent -- handled by ConfigureTethers via a SelectionNode, not a WeightMap. */
		struct FMaskBinding
		{
			const TCHAR* MaskName;
			UScriptStruct* (*GetNodeStruct)();   // Resolved at first call via StaticStruct().
			const TCHAR* PropertyName;           // The weighted-value field name; ".WeightMap" is appended for the pin.
			EWeightMapRescaleMode RescaleMode;
		};

		static const FMaskBinding GMaskBindings[] = {
			{ TEXT("MaxDistance"),        &FChaosClothAssetSimulationMaxDistanceConfigNode::StaticStruct,           TEXT("MaxDistance"),        EWeightMapRescaleMode::Value    },
			{ TEXT("BackstopDistance"),   &FChaosClothAssetSimulationBackstopConfigNode::StaticStruct,              TEXT("BackstopDistance"),   EWeightMapRescaleMode::Value    },
			{ TEXT("BackstopRadius"),     &FChaosClothAssetSimulationBackstopConfigNode::StaticStruct,              TEXT("BackstopRadius"),     EWeightMapRescaleMode::Value    },
			{ TEXT("AnimDriveStiffness"), &FChaosClothAssetSimulationAnimDriveConfigNode::StaticStruct,             TEXT("AnimDriveStiffness"), EWeightMapRescaleMode::Weighted },
			{ TEXT("AnimDriveDamping"),   &FChaosClothAssetSimulationAnimDriveConfigNode::StaticStruct,             TEXT("AnimDriveDamping"),   EWeightMapRescaleMode::Weighted },
			{ TEXT("TetherStiffness"),    &FChaosClothAssetSimulationLongRangeAttachmentConfigNode_v2::StaticStruct, TEXT("TetherStiffness"),   EWeightMapRescaleMode::Weighted },
			{ TEXT("TetherScale"),        &FChaosClothAssetSimulationLongRangeAttachmentConfigNode_v2::StaticStruct, TEXT("TetherScale"),       EWeightMapRescaleMode::Weighted },
			{ TEXT("Drag"),               &FChaosClothAssetSimulationAerodynamicsConfigNode::StaticStruct,          TEXT("Drag"),               EWeightMapRescaleMode::Weighted },
			{ TEXT("OuterDrag"),          &FChaosClothAssetSimulationAerodynamicsConfigNode::StaticStruct,          TEXT("OuterDrag"),          EWeightMapRescaleMode::Weighted },
			{ TEXT("Lift"),               &FChaosClothAssetSimulationAerodynamicsConfigNode::StaticStruct,          TEXT("Lift"),               EWeightMapRescaleMode::Weighted },
			{ TEXT("OuterLift"),          &FChaosClothAssetSimulationAerodynamicsConfigNode::StaticStruct,          TEXT("OuterLift"),          EWeightMapRescaleMode::Weighted },
			{ TEXT("Pressure"),           &FChaosClothAssetSimulationPressureConfigNode::StaticStruct,              TEXT("Pressure"),           EWeightMapRescaleMode::Weighted },
		};

		static FWeightMapConsumer ResolveWeightMapConsumer(UDataflow* InDataflow, const FString& MaskName)
		{
			FWeightMapConsumer Result;
			for (const FMaskBinding& Binding : GMaskBindings)
			{
				if (MaskName == Binding.MaskName)
				{
					const UScriptStruct* const TargetStruct = Binding.GetNodeStruct();
					if (TargetStruct && InDataflow)
					{
						if (const TSharedPtr<UE::Dataflow::FGraph> Graph = InDataflow->GetDataflow())
						{
							for (const TSharedPtr<FDataflowNode>& Node : Graph->GetNodes())
							{
								if (Node && Node->TypedScriptStruct() == TargetStruct)
								{
									Result.ConsumerNode = Node;
									break;
								}
							}
							ensureMsgf(Result.ConsumerNode.IsValid(),
								TEXT("Legacy conversion template is missing the expected '%s' node consumer for legacy mask '%s'."),
								*TargetStruct->GetName(), *MaskName);
						}
					}
					Result.ConsumerWeightMapPinName = FString::Printf(TEXT("%s.WeightMap"), Binding.PropertyName);
					Result.RescaleMode = Binding.RescaleMode;
					break;
				}
			}
			return Result;
		}

		// Find the UDataflowEdNode in a UDataflow whose underlying FDataflowNode has the given GUID.
		// Used to map an FDataflowNode runtime pointer back to its visual editor node so we can read
		// or write NodePosX/NodePosY.
		static UDataflowEdNode* FindEdNodeByGuid(UDataflow* InDataflow, const FGuid& NodeGuid)
		{
			if (!InDataflow)
			{
				return nullptr;
			}
			for (UEdGraphNode* const Node : InDataflow->Nodes)
			{
				if (UDataflowEdNode* const EdNode = Cast<UDataflowEdNode>(Node))
				{
					if (EdNode->GetDataflowNodeGuid() == NodeGuid)
					{
						return EdNode;
					}
				}
			}
			return nullptr;
		}

		// Splice a WeightMap node immediately upstream of a consumer simulation node in the
		// Collection chain, and wire its OutputName.StringValue -> ConsumerWeightMapPinName.
		// Returns true if the splice + pin connection both succeeded. When WMEdNode is non-null and
		// the consumer had an upstream provider, the WeightMap's editor position is set to read as
		// a visible insertion directly below the upstream node.
		static bool SpliceWeightMapUpstreamOfConsumer(
			UDataflow* InDataflow,
			UE::Dataflow::FGraph& Graph,
			const TSharedPtr<FDataflowNode>& WMNode,
			UDataflowEdNode* WMEdNode,
			const TSharedPtr<FDataflowNode>& ConsumerNode,
			const FString& ConsumerWeightMapPinName,
			TSet<FGuid>& InOutNodesToRefresh)
		{
			if (!WMNode || !ConsumerNode)
			{
				return false;
			}

			FDataflowInput* const  WMCollectionIn      = WMNode->FindInput(FName(TEXT("Collection")));
			FDataflowOutput* const WMCollectionOut     = WMNode->FindOutput(FName(TEXT("Collection")));
			FDataflowOutput* const WMOutputNameOut     = WMNode->FindOutput(FName(TEXT("OutputName.StringValue")));
			FDataflowInput* const  ConsumerCollectionIn = ConsumerNode->FindInput(FName(TEXT("Collection")));
			FDataflowInput* const  ConsumerPropertyIn  = ConsumerNode->FindInput(FName(*ConsumerWeightMapPinName));
			if (!WMCollectionIn || !WMCollectionOut || !ConsumerCollectionIn)
			{
				return false;
			}

			FDataflowOutput* UpstreamOutput = nullptr;
			FGuid UpstreamGuid;
			if (const FDataflowOutput* const ConnectedFromOutput = ConsumerCollectionIn->GetConnection())
			{
				FDataflowNode* const UpstreamNode = const_cast<FDataflowNode*>(ConnectedFromOutput->GetOwningNode());
				if (UpstreamNode)
				{
					UpstreamOutput = UpstreamNode->FindOutput(ConnectedFromOutput->GetName());
					UpstreamGuid = UpstreamNode->GetGuid();
					InOutNodesToRefresh.Add(UpstreamGuid);
				}
			}

			// Position the new WeightMap visually directly below its upstream provider so the chain
			// reads top-to-bottom rather than the WeightMap sitting off in a separate stack. 192 is a
			// natural vertical step matching the existing template's row spacing.
			if (WMEdNode && UpstreamGuid.IsValid())
			{
				if (const UDataflowEdNode* const UpstreamEdNode = FindEdNodeByGuid(InDataflow, UpstreamGuid))
				{
					constexpr int32 VerticalStep = 192;
					WMEdNode->NodePosX = UpstreamEdNode->NodePosX;
					WMEdNode->NodePosY = UpstreamEdNode->NodePosY + VerticalStep;
				}
			}

			InDataflow->Modify();
			Graph.ClearConnections(ConsumerCollectionIn);
			if (UpstreamOutput)
			{
				Graph.Connect(*UpstreamOutput, *WMCollectionIn);
			}
			Graph.Connect(*WMCollectionOut, *ConsumerCollectionIn);

			// Wire the string pin so the consumer reads its weight map by an explicit pin link
			// (rather than only by name lookup on the cloth collection).
			if (WMOutputNameOut && ConsumerPropertyIn)
			{
				Graph.Connect(*WMOutputNameOut, *ConsumerPropertyIn);
			}

			InOutNodesToRefresh.Add(WMNode->GetGuid());
			InOutNodesToRefresh.Add(ConsumerNode->GetGuid());
			return true;
		}

		/**
		 * Resolve an EChaosWeightMapTarget value into its enum name via FindObject (the enum lives
		 * in a private ChaosCloth header). TWeakObjectPtr cache survives both pre-module-load
		 * lookups and GC / hot-reload between calls.
		 */
		static FString ResolveLegacyMaskName(uint32 TargetID)
		{
			static TWeakObjectPtr<UEnum> CachedEnum;
			UEnum* EnumPtr = CachedEnum.Get();
			if (!EnumPtr)
			{
				EnumPtr = FindObject<UEnum>(nullptr, TEXT("/Script/ChaosCloth.EChaosWeightMapTarget"));
				CachedEnum = EnumPtr;
			}
			if (EnumPtr)
			{
				const FString Name = EnumPtr->GetNameStringByValue(static_cast<int64>(TargetID));
				if (!Name.IsEmpty())
				{
					return Name;
				}
			}
			// Unknown target -- fall back to a stable, debuggable name.
			return FString::Printf(TEXT("LegacyMask_%u"), TargetID);
		}

		/** Shared inputs threaded through every conversion helper. */
		struct FConversionContext
		{
			UDataflow*                        Dataflow;
			UChaosClothAsset*                 NewClothAsset;
			const UClothingAssetCommon*       SourceAsset;
			const FClothPhysicalMeshData&     PhysData;
			const UChaosClothConfig*          PerCloth;        // May be null.
			const UChaosClothSharedSimConfig* Shared;          // May be null.
			USkeletalMesh*                    OwningMesh;      // May be null.
			TConstArrayView<int32>            CompactToLegacyVertex;
		};

		// One-shot bake of the legacy FClothPhysicalMeshData into a cloth collection stored as the
		// "ImportedSimClothCollection" Dataflow variable override; the template consumes it via a
		// Get-Variable node.
		//
		// Outputs CompactToLegacyVertex (legacy vertex ID for each post-compact SimVertex3D).
		// SimImportVertexID is a Sim2D attribute, but legacy cloth has no UV seams (1:1
		// Sim2D<->Sim3D), so the same array indexes both.
		//
		// TODO: multi-LOD -- PhysData here is always LOD 0; higher legacy LODs are dropped.
		static void ImportSimClothCollectionAndApplyVariables(
			UChaosClothAsset* NewClothAsset,
			const UClothingAssetCommon* SourceAsset,
			const FClothPhysicalMeshData& PhysData,
			USkeletalMesh* OwningMesh,
			TArray<int32>& OutCompactToLegacyVertex)
		{
			const int32 NumLegacyVerts = PhysData.Vertices.Num();

			const TSharedRef<FManagedArrayCollection> ImportedCollection = MakeShared<FManagedArrayCollection>();
			{
				FCollectionClothFacade ImportedFacade(ImportedCollection);
				ImportedFacade.DefineSchema(EClothCollectionExtendedSchemas::Import);

				// 2D positions are unused for this asset type -- legacy cloth has no UV seams and there is
				// a one-to-one correspondence between 2D and 3D vertices, so populate 2D with zeros.
				TArray<FVector2f> Positions2D;
				Positions2D.Init(FVector2f::ZeroVector, NumLegacyVerts);

				FCollectionClothSimPatternFacade SimPattern = ImportedFacade.AddGetSimPattern();
				SimPattern.Initialize(Positions2D, PhysData.Vertices, PhysData.Indices, INDEX_NONE, PhysData.Normals);

				// Identity SimImportVertexID so each Sim2D index maps back to its legacy vertex index.
				// Downstream WeightMap nodes use this to apply per-legacy-vertex mask values onto sim
				// vertices, surviving any later CleanupAndCompactMesh reordering.
				TArrayView<int32> SimImportVertexID = ImportedFacade.GetSimImportVertexID();
				for (int32 Index = 0; Index < SimImportVertexID.Num(); ++Index)
				{
					SimImportVertexID[Index] = Index;
				}

				// Copy FClothVertBoneData (bone indices + weights, max 12) into the cloth collection's
				// sim 3D bone arrays. FClothVertBoneData::BoneIndices stores *compact* indices into the
				// legacy asset's UsedBoneIndices array -- they're NOT direct reference-skeleton indices.
				// The new cloth collection (and UChaosClothAsset::ImportFacade's skin-weight validation)
				// expects global reference-skeleton bone indices, so remap each compact index through
				// UsedBoneIndices before writing. Influences whose compact index is out of range are
				// skipped so we don't poison the skinning table.
				const TArray<int32>& UsedBoneIndices = SourceAsset->UsedBoneIndices;
				TArrayView<TArray<int32>> SimBoneIndices = ImportedFacade.GetSimBoneIndices();
				TArrayView<TArray<float>> SimBoneWeights = ImportedFacade.GetSimBoneWeights();
				const int32 NumSim3D = FMath::Min(SimBoneIndices.Num(), PhysData.BoneData.Num());
				for (int32 V = 0; V < NumSim3D; ++V)
				{
					const FClothVertBoneData& BoneDatum = PhysData.BoneData[V];
					const int32 NumInfluences = FMath::Clamp(BoneDatum.NumInfluences, 0, static_cast<int32>(FClothVertBoneData::MaxTotalInfluences));
					SimBoneIndices[V].Reset(NumInfluences);
					SimBoneWeights[V].Reset(NumInfluences);
					for (int32 I = 0; I < NumInfluences; ++I)
					{
						const int32 CompactBoneIdx = static_cast<int32>(BoneDatum.BoneIndices[I]);
						if (!UsedBoneIndices.IsValidIndex(CompactBoneIdx))
						{
							continue;
						}
						SimBoneIndices[V].Add(UsedBoneIndices[CompactBoneIdx]);
						SimBoneWeights[V].Add(BoneDatum.BoneWeights[I]);
					}
				}

				// Skeletal mesh path -- downstream consumers need to know which skeleton the bone weights
				// were authored against.
				if (OwningMesh)
				{
					ImportedFacade.SetSkeletalMeshSoftObjectPathName(OwningMesh->GetPathName());
				}

				FClothGeometryTools::CleanupAndCompactMesh(ImportedCollection);

				// Capture the post-compact SimImportVertexID into OutCompactToLegacyVertex so Steps 5
				// and 6 can remap per-legacy-vertex data onto the compacted sim mesh.
				TConstArrayView<int32> PostCompactSimImportVertexID = ImportedFacade.GetSimImportVertexID();
				OutCompactToLegacyVertex.Append(PostCompactSimImportVertexID.GetData(), PostCompactSimImportVertexID.Num());
			}

			// Apply variable overrides on the new asset's FDataflowInstance.
			FDataflowVariableOverrides& Overrides = NewClothAsset->GetDataflowInstance().GetVariableOverrides();
			Overrides.OverrideVariableStruct<FManagedArrayCollection>(FName(TEXT("ImportedSimClothCollection")), *ImportedCollection);
			if (OwningMesh)
			{
				Overrides.OverrideVariableObject(FName(TEXT("RenderSkeletalMesh")), OwningMesh);
			}
		}

		/**
		 * Drive SkeletalMeshImport / ProxyDeformer / SkinningBlend from the legacy binding. Settings
		 * live on FClothLODDataCommon (not UChaosClothConfig), so they don't flow through
		 * BakeClothConfigScalars. The section index is resolved by scanning the owning
		 * SkeletalMesh's imported model for the FSkelMeshSection that references SourceAsset's GUID.
		 *
		 * TODO: multi-LOD -- only reads LodData[0] and only matches SkelLods whose LodMap entry == 0.
		 */
		static void ConfigureRenderImportNodes(const FConversionContext& Ctx)
		{
			if (!Ctx.SourceAsset)
			{
				return;
			}
			const FClothLODDataCommon& LodData = Ctx.SourceAsset->LodData[0];

			// Prefer v3, fall back to v2 -- same field layout, v3 just defaults bPreserveRenderTangents.
			TSharedPtr<FDataflowNode> ProxyDeformerNode = FindNodeByType<FChaosClothAssetProxyDeformerNode_v3>(Ctx.Dataflow);
			if (!ProxyDeformerNode)
			{
				ProxyDeformerNode = FindNodeByType<FChaosClothAssetProxyDeformerNode_v2>(Ctx.Dataflow);
			}
			if (ProxyDeformerNode)
			{
				WritePlain<bool>(ProxyDeformerNode.Get(), TEXT("bUseMultipleInfluences"), LodData.bUseMultipleInfluences);
				WritePlain<float>(ProxyDeformerNode.Get(), TEXT("InfluenceRadius"), LodData.SkinningKernelRadius);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSkinningBlendNode>(Ctx.Dataflow))
			{
				WritePlain<bool>(N.Get(), TEXT("bUseSmoothTransition"), LodData.bSmoothTransition);
			}

			// LodMap[SkelLod] = ClothingLod -- we want the SkelLod that pairs with clothing LOD 0
			// (the only clothing LOD this converter imports). Empty LodMap entries pass through.
			if (!Ctx.OwningMesh)
			{
				return;
			}
			const FSkeletalMeshModel* const ImportedModel = Ctx.OwningMesh->GetImportedModel();
			if (!ImportedModel)
			{
				return;
			}
			const FGuid LegacyAssetGuid = Ctx.SourceAsset->GetAssetGuid();
			int32 FoundLodIndex = INDEX_NONE;
			int32 FoundSectionIndex = INDEX_NONE;
			for (int32 SkelLodIndex = 0; SkelLodIndex < ImportedModel->LODModels.Num(); ++SkelLodIndex)
			{
				// Prefer SkelLods whose clothing-LOD entry is 0 (the LOD we're importing). When
				// LodMap is empty/short, accept any SkelLod -- most legacy assets have a single LOD.
				if (Ctx.SourceAsset->LodMap.IsValidIndex(SkelLodIndex) &&
					Ctx.SourceAsset->LodMap[SkelLodIndex] != 0)
				{
					continue;
				}
				const FSkeletalMeshLODModel& LodModel = ImportedModel->LODModels[SkelLodIndex];
				for (int32 SectionIndex = 0; SectionIndex < LodModel.Sections.Num(); ++SectionIndex)
				{
					if (LodModel.Sections[SectionIndex].ClothingData.AssetGuid == LegacyAssetGuid)
					{
						FoundLodIndex = SkelLodIndex;
						FoundSectionIndex = SectionIndex;
						break;
					}
				}
				if (FoundSectionIndex != INDEX_NONE)
				{
					break;
				}
			}
			if (FoundSectionIndex == INDEX_NONE)
			{
				UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
					TEXT("Could not find a SkeletalMesh section bound to legacy clothing asset '%s' (guid %s); SkeletalMeshImport node will keep its template defaults and may import the wrong render section."),
					*Ctx.SourceAsset->GetName(), *LegacyAssetGuid.ToString());
				return;
			}
			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSkeletalMeshImportNode_v2>(Ctx.Dataflow))
			{
				WritePlain<bool>(N.Get(), TEXT("bImportSingleSection"), true);
				WritePlain<int32>(N.Get(), TEXT("LODIndex"), FoundLodIndex);
				WritePlain<int32>(N.Get(), TEXT("SectionIndex"), FoundSectionIndex);
			}
		}

		/** Direct UProperty mutations onto pre-existing template config nodes (no AddDataflowNode). */
		static void BakeClothConfigScalars(const FConversionContext& Ctx)
		{
			const UChaosClothConfig* const PerCloth = Ctx.PerCloth;
			UDataflow* const Dataflow = Ctx.Dataflow;
			if (!PerCloth)
			{
				return;
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationMassConfigNode>(Dataflow))
			{
				WritePlain<EClothMassMode>(N.Get(), TEXT("MassMode"), PerCloth->MassMode);
				WriteScalarToWeighted<FChaosClothAssetWeightedValueNonAnimatable>(N.Get(), TEXT("UniformMassWeighted"), PerCloth->UniformMass);
				WritePlain<float>(N.Get(), TEXT("TotalMass"), PerCloth->TotalMass);
				WriteScalarToWeighted<FChaosClothAssetWeightedValueNonAnimatable>(N.Get(), TEXT("DensityWeighted"), PerCloth->Density);
				WritePlain<float>(N.Get(), TEXT("MinPerParticleMass"), PerCloth->MinPerParticleMass);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationStretchConfigNode>(Dataflow))
			{
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("StretchStiffness"), PerCloth->EdgeStiffnessWeighted);
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("AreaStiffness"), PerCloth->AreaStiffnessWeighted);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationBendingConfigNode>(Dataflow))
			{
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("BendingStiffness"), PerCloth->BendingStiffnessWeighted);
				WriteScalarToWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("BucklingRatioWeighted"), PerCloth->BucklingRatio);
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("BucklingStiffness"), PerCloth->BucklingStiffnessWeighted);
				WriteLegacyWeighted<FChaosClothAssetWeightedValueNonAnimatable>(N.Get(), TEXT("FlatnessRatio"), PerCloth->FlatnessRatio);

				// Legacy bending elements <-> new HingeAngles; legacy edge springs <-> new FacesSpring.
				WritePlain<EChaosClothAssetBendingConstraintType>(N.Get(), TEXT("ConstraintType"),
					PerCloth->bUseBendingElements
						? EChaosClothAssetBendingConstraintType::HingeAngles
						: EChaosClothAssetBendingConstraintType::FacesSpring);

				// FlatnessRatio's EditCondition gates it on bUseBendingElements; only switch
				// RestAngleType when the legacy value is meaningfully non-default.
				if (PerCloth->bUseBendingElements &&
					(PerCloth->FlatnessRatio.Low != 0.f || PerCloth->FlatnessRatio.High != 0.f))
				{
					WritePlain<EChaosClothAssetRestAngleConstructionType>(N.Get(), TEXT("RestAngleType"),
						EChaosClothAssetRestAngleConstructionType::FlatnessRatio);
				}
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationCollisionConfigNode>(Dataflow))
			{
				WriteToImported<FChaosClothAssetImportedFloatValue, float>(N.Get(), TEXT("CollisionThicknessImported"), PerCloth->CollisionThickness);
				WriteScalarToWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("FrictionCoefficientWeighted"), PerCloth->FrictionCoefficient);
				WritePlain<bool>(N.Get(), TEXT("bUseCCD"), PerCloth->bUseCCD);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationSelfCollisionConfigNode_v2>(Dataflow))
			{
				WriteScalarToWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("SelfCollisionThickness"), PerCloth->SelfCollisionThickness);
				WriteToImported<FChaosClothAssetImportedFloatValue, float>(N.Get(), TEXT("SelfCollisionFriction"), PerCloth->SelfCollisionFriction);
				WritePlain<bool>(N.Get(), TEXT("bUseSelfIntersections"), PerCloth->bUseSelfIntersections);
			}
			// The template gates SelfCollisionConfig on a "UseSelfCollisions" branch node.
			if (const TSharedPtr<FDataflowNode> Branch = FindNodeByName(Dataflow, TEXT("UseSelfCollisions")))
			{
				WritePlain<bool>(Branch.Get(), TEXT("bCondition"), PerCloth->bUseSelfCollisions);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationSelfCollisionSpheresConfigNode>(Dataflow))
			{
				WritePlain<float>(N.Get(), TEXT("SelfCollisionSphereRadius"), PerCloth->SelfCollisionSphereRadius);
				WritePlain<float>(N.Get(), TEXT("SelfCollisionSphereStiffness"), PerCloth->SelfCollisionSphereStiffness);
				WritePlain<float>(N.Get(), TEXT("SelfCollisionSphereRadiusCullMultiplier"), PerCloth->SelfCollisionSphereRadiusCullMultiplier);
			}
			if (const TSharedPtr<FDataflowNode> Branch = FindNodeByName(Dataflow, TEXT("UseSelfCollisionSpheres")))
			{
				WritePlain<bool>(Branch.Get(), TEXT("bCondition"), PerCloth->bUseSelfCollisionSpheres);
			}

			// Legacy LocalDampingCoefficient drove both linear and angular damping from a single scalar.
			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationDampingConfigNode>(Dataflow))
			{
				WriteScalarToWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("DampingCoefficientWeighted"), PerCloth->DampingCoefficient);
				WritePlain<float>(N.Get(), TEXT("LocalDampingLinearCoefficient"), PerCloth->LocalDampingCoefficient);
				WritePlain<float>(N.Get(), TEXT("LocalDampingAngularCoefficient"), PerCloth->LocalDampingCoefficient);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationAerodynamicsConfigNode>(Dataflow))
			{
				WritePlain<bool>(N.Get(), TEXT("bUsePointBasedWindModel"), PerCloth->bUsePointBasedWindModel);
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("Drag"), PerCloth->Drag);
				WritePlain<bool>(N.Get(), TEXT("bEnableOuterDrag"), PerCloth->bEnableOuterDrag);
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("OuterDrag"), PerCloth->OuterDrag);
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("Lift"), PerCloth->Lift);
				WritePlain<bool>(N.Get(), TEXT("bEnableOuterLift"), PerCloth->bEnableOuterLift);
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("OuterLift"), PerCloth->OuterLift);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationGravityConfigNode>(Dataflow))
			{
				WritePlain<bool>(N.Get(), TEXT("bUseGravityOverride"), PerCloth->bUseGravityOverride);
				WriteScalarToWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("GravityScaleWeighted"), PerCloth->GravityScale);
				WriteToImported<FChaosClothAssetImportedVectorValue, FVector3f>(N.Get(), TEXT("GravityOverrideImported"), FVector3f(PerCloth->Gravity));
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationPressureConfigNode>(Dataflow))
			{
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("Pressure"), PerCloth->Pressure);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationAnimDriveConfigNode>(Dataflow))
			{
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("AnimDriveStiffness"), PerCloth->AnimDriveStiffness);
				WriteLegacyWeighted<FChaosClothAssetWeightedValue>(N.Get(), TEXT("AnimDriveDamping"), PerCloth->AnimDriveDamping);
			}

			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationVelocityScaleConfigNode>(Dataflow))
			{
				WritePlain<EChaosSoftsSimulationSpace>(N.Get(), TEXT("VelocityScaleSpace"), PerCloth->VelocityScaleSpace);
				WritePlain<FVector3f>(N.Get(), TEXT("LinearVelocityScale"), FVector3f(PerCloth->LinearVelocityScale));
				WritePlain<bool>(N.Get(), TEXT("bEnableLinearVelocityClamping"), PerCloth->bEnableLinearVelocityClamping);
				WritePlain<FVector3f>(N.Get(), TEXT("MaxLinearVelocity"), PerCloth->MaxLinearVelocity);
				WritePlain<bool>(N.Get(), TEXT("bEnableLinearAccelerationClamping"), PerCloth->bEnableLinearAccelerationClamping);
				WritePlain<FVector3f>(N.Get(), TEXT("MaxLinearAcceleration"), PerCloth->MaxLinearAcceleration);
				WritePlain<float>(N.Get(), TEXT("AngularVelocityScale"), PerCloth->AngularVelocityScale);
				WritePlain<bool>(N.Get(), TEXT("bEnableAngularVelocityClamping"), PerCloth->bEnableAngularVelocityClamping);
				WritePlain<float>(N.Get(), TEXT("MaxAngularVelocity"), PerCloth->MaxAngularVelocity);
				WritePlain<bool>(N.Get(), TEXT("bEnableAngularAccelerationClamping"), PerCloth->bEnableAngularAccelerationClamping);
				WritePlain<float>(N.Get(), TEXT("MaxAngularAcceleration"), PerCloth->MaxAngularAcceleration);
				WritePlain<EChaosClothingSimulationSolverFictitiousForcesModel>(N.Get(), TEXT("FictitiousForcesModel"), PerCloth->FictitiousForcesModel);
				WritePlain<float>(N.Get(), TEXT("FictitiousAngularScale"), PerCloth->FictitiousAngularScale);
			}
		}

		/**
		 * One user-editable FChaosClothAssetWeightMapNode per legacy FPointWeightMap, normalized to
		 * [0,1] by LegacyMax, spliced upstream of its consumer in the Collection chain and wired
		 * into <Property>.WeightMap so consumption is an explicit graph connection.
		 *
		 * AddNewNode's NodeTypeName must be the USTRUCT name "FChaosClothAssetWeightMapNode", not
		 * the DATAFLOW_NODE_DEFINE_INTERNAL display name "WeightMap" -- mismatch silently returns nullptr.
		 */
		static void CreateWeightMapNodes(const FConversionContext& Ctx)
		{
			UDataflow* const Dataflow = Ctx.Dataflow;
			const FClothPhysicalMeshData& PhysData = Ctx.PhysData;
			const bool bUseLegacyBackstop = Ctx.PerCloth && Ctx.PerCloth->bUseLegacyBackstop;
			TSharedPtr<UE::Dataflow::FGraph> Graph = Dataflow->GetDataflow();
			TSet<FGuid> NodesToRefresh;

			for (const TPair<uint32, FPointWeightMap>& TargetAndMap : PhysData.WeightMaps)
			{
				const uint32 TargetID = TargetAndMap.Key;
				const FPointWeightMap& Map = TargetAndMap.Value;
				if (Map.Values.IsEmpty())
				{
					continue;
				}

				const FString MaskName = ResolveLegacyMaskName(TargetID);

				const FWeightMapConsumer Consumer = ResolveWeightMapConsumer(Dataflow, MaskName);
				if (!Consumer.ConsumerNode)
				{
					if (MaskName != TEXT("TetherEndsMask"))
					{
						UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
							TEXT("Legacy mask '%s' has no consumer node in the converted asset's template; skipping its WeightMap node creation."),
							*MaskName);
					}
					continue;
				}

				// Legacy bUseLegacyBackstop stored BackstopDistance as distance-to-center; the new
				// system stores distance-to-surface (radius re-added at runtime). Subtract the
				// per-vertex radius and clamp to >= 0 -- inside-the-sphere verts collapse to the
				// surface rather than carrying negatives that would slip past the Weighted-mode
				// rescale (which only runs when LegacyMax > 1).
				TArray<float> AdjustedValues;
				TConstArrayView<float> ValuesToImport = Map.Values;
				if (bUseLegacyBackstop && TargetID == static_cast<uint32>(EWeightMapTargetCommon::BackstopDistance))
				{
					const FPointWeightMap* const RadiusMap = PhysData.FindWeightMap(EWeightMapTargetCommon::BackstopRadius);
					if (RadiusMap && RadiusMap->Values.Num() == Map.Values.Num())
					{
						AdjustedValues.SetNumUninitialized(Map.Values.Num());
						for (int32 Index = 0; Index < Map.Values.Num(); ++Index)
						{
							AdjustedValues[Index] = FMath::Max(0.0f, Map.Values[Index] - RadiusMap->Values[Index]);
						}
						ValuesToImport = AdjustedValues;
					}
					else
					{
						// Without a matching radius mask we can't translate legacy distance-to-center
						// values into distance-to-surface values, so the new system would interpret
						// them as the wrong quantity (cloth penetrating the backstop sphere by
						// `radius` units). Disable the Backstop config node entirely and skip the
						// orphan WeightMap node creation -- Step 9's missing-mask path only catches
						// the absent-mask case, not this length-mismatch case.
						UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
							TEXT("bUseLegacyBackstop is true but BackstopRadius mask is missing or has mismatched length (%d vs %d); cannot translate legacy BackstopDistance into the new distance-to-surface form. Disabling the Backstop config node -- set it up manually if backstop behavior is required."),
							RadiusMap ? RadiusMap->Values.Num() : 0, Map.Values.Num());
						if (const TSharedPtr<FDataflowNode> BackstopNode = FindNodeByType<FChaosClothAssetSimulationBackstopConfigNode>(Dataflow))
						{
							WritePlain<bool>(BackstopNode.Get(), TEXT("bActive"), false);
						}
						continue;
					}
				}

				const float LegacyMax = ComputeLegacyMax(ValuesToImport);
				const FString NodeBaseName = FString::Printf(TEXT("LegacyMask_%s"), *MaskName);

				// Initial position is a placeholder -- SpliceWeightMapUpstreamOfConsumer will move
				// the node directly below its upstream provider once the chain is resolved.
				UDataflowEdNode* const NewEdNode = UE::Dataflow::FEditAssetUtils::AddNewNode(
					Dataflow,
					FVector2D(64.0, 0.0),
					FName(*NodeBaseName),
					TEXT("FChaosClothAssetWeightMapNode"),  // USTRUCT name, not display name
					/*FromPin*/ nullptr);

				if (!NewEdNode)
				{
					UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
						TEXT("Failed to create WeightMap node for legacy mask '%s' (target id %u)."),
						*MaskName, TargetID);
					continue;
				}

				TSharedPtr<FDataflowNode> WMNodeBase = NewEdNode->GetDataflowNode();
				if (!WMNodeBase)
				{
					continue;
				}

				if (FChaosClothAssetConnectableOStringValue* const OutputName =
					GetNodePropertyPtr<FChaosClothAssetConnectableOStringValue>(WMNodeBase.Get(), TEXT("OutputName")))
				{
					OutputName->StringValue = MaskName;
				}

				if (TArray<float>* const VertexWeights =
					GetNodePropertyPtr<TArray<float>>(WMNodeBase.Get(), TEXT("VertexWeights")))
				{
					// CompactToLegacyVertex remaps the legacy-vertex-indexed ValuesToImport onto
					// the post-CleanupAndCompactMesh SimVertex3D order. Clamp keeps the [0,1]
					// VertexWeights contract intact in the face of stray legacy negatives that
					// ComputeLegacyMax would not catch.
					const int32 NumCompactVerts = Ctx.CompactToLegacyVertex.Num();
					VertexWeights->SetNumZeroed(NumCompactVerts);
					for (int32 CompactIdx = 0; CompactIdx < NumCompactVerts; ++CompactIdx)
					{
						const int32 LegacyIdx = Ctx.CompactToLegacyVertex[CompactIdx];
						if (ValuesToImport.IsValidIndex(LegacyIdx))
						{
							(*VertexWeights)[CompactIdx] = FMath::Clamp(ValuesToImport[LegacyIdx] / LegacyMax, 0.0f, 1.0f);
						}
					}
				}

				if (Graph.IsValid())
				{
					SpliceWeightMapUpstreamOfConsumer(
						Dataflow, *Graph, WMNodeBase, NewEdNode,
						Consumer.ConsumerNode, Consumer.ConsumerWeightMapPinName,
						NodesToRefresh);
				}

				// Compensate the consumer's Low/High for the LegacyMax normalization. Weighted-mode
				// is a no-op when LegacyMax <= 1; Value-mode always runs so Low/High describe the
				// legacy data range rather than the template's no-mask default.
				static const FString WeightMapSuffix(TEXT(".WeightMap"));
				if (Consumer.ConsumerWeightMapPinName.EndsWith(WeightMapSuffix) &&
					(Consumer.RescaleMode == EWeightMapRescaleMode::Value || LegacyMax > 1.0f))
				{
					const FString WeightedFieldName = Consumer.ConsumerWeightMapPinName.LeftChop(WeightMapSuffix.Len());
					RescaleConsumerLowHigh(
						Consumer.ConsumerNode.Get(),
						FName(*WeightedFieldName),
						LegacyMax,
						Consumer.RescaleMode);
				}
			}

			for (const FGuid& NodeGuid : NodesToRefresh)
			{
				Dataflow->RefreshEdNodeByGuid(NodeGuid);
			}
		}

		/**
		 * Bake LRA_v2 scalars, and when the legacy asset has a TetherEndsMask, switch LRA_v2 into
		 * custom-tether generation by splicing two SelectionNodes ("LegacyTetherEnds",
		 * "LegacyAllSimVertices3D") into the Collection chain and wiring their names into
		 * CustomTetherData[0]. The default FixedEndSet wiring (MaxDistanceConfig.KinematicVertices3D)
		 * stays intact for assets whose tether anchors derive from MaxDistance.
		 *
		 * NeedsTethers() == false disables LRA_v2 and returns early -- graph mutations downstream of
		 * a disabled node would just be dead wiring.
		 */
		static void ConfigureTethers(const FConversionContext& Ctx)
		{
			const UChaosClothConfig* const PerCloth = Ctx.PerCloth;
			UDataflow* const Dataflow = Ctx.Dataflow;
			if (!PerCloth)
			{
				return;
			}

			TSharedPtr<FDataflowNode> LraNode = FindNodeByType<FChaosClothAssetSimulationLongRangeAttachmentConfigNode_v2>(Dataflow);
			if (!LraNode)
			{
				return;
			}

			if (!PerCloth->NeedsTethers())
			{
				WritePlain<bool>(LraNode.Get(), TEXT("bActive"), false);
				return;
			}

			WriteLegacyWeighted<FChaosClothAssetWeightedValue>(LraNode.Get(), TEXT("TetherStiffness"), PerCloth->TetherStiffness);
			WriteLegacyWeighted<FChaosClothAssetWeightedValue>(LraNode.Get(), TEXT("TetherScale"), PerCloth->TetherScale);
			WritePlain<bool>(LraNode.Get(), TEXT("bUseGeodesicTethers"), PerCloth->bUseGeodesicDistance);

			const FPointWeightMap* const TetherEndsMask = Ctx.PhysData.FindWeightMap(EWeightMapTargetCommon::TetherEndsMask);
			if (!TetherEndsMask)
			{
				return;
			}

			// SelectionNode indices live in compacted SimVertex3D space; remap legacy-indexed mask
			// reads through CompactToLegacyVertex.
			const int32 NumSimVertices3D = Ctx.CompactToLegacyVertex.Num();

			TSet<int32> TetherEnds;
			for (int32 CompactIdx = 0; CompactIdx < NumSimVertices3D; ++CompactIdx)
			{
				const int32 LegacyIdx = Ctx.CompactToLegacyVertex[CompactIdx];
				if (TetherEndsMask->Values.IsValidIndex(LegacyIdx) &&
					TetherEndsMask->Values[LegacyIdx] < FClothTetherData::KinematicDistanceThreshold)
				{
					TetherEnds.Add(CompactIdx);
				}
			}

			TSet<int32> AllSimVertices3D;
			AllSimVertices3D.Reserve(NumSimVertices3D);
			for (int32 Index = 0; Index < NumSimVertices3D; ++Index)
			{
				AllSimVertices3D.Add(Index);
			}

			auto AddSimVertex3DSelection = [&](const FString& BaseName, const FString& SetName, const TSet<int32>& Indices) -> TSharedPtr<FDataflowNode>
			{
				UDataflowEdNode* const NewEdNode = UE::Dataflow::FEditAssetUtils::AddNewNode(
					Dataflow,
					FVector2D(384.0, 1300.0),
					FName(*BaseName),
					TEXT("FChaosClothAssetSelectionNode_v2"),
					/*FromPin*/ nullptr);
				if (!NewEdNode)
				{
					return nullptr;
				}
				TSharedPtr<FDataflowNode> SelNode = NewEdNode->GetDataflowNode();
				if (!SelNode)
				{
					return nullptr;
				}
				if (FChaosClothAssetConnectableOStringValue* const OutputName =
					GetNodePropertyPtr<FChaosClothAssetConnectableOStringValue>(SelNode.Get(), TEXT("OutputName")))
				{
					OutputName->StringValue = SetName;
				}
				if (FChaosClothAssetNodeSelectionGroup* const Group =
					GetNodePropertyPtr<FChaosClothAssetNodeSelectionGroup>(SelNode.Get(), TEXT("Group")))
				{
					Group->Name = TEXT("SimVertices3D");
				}
				if (TSet<int32>* const IndicesPtr = GetNodePropertyPtr<TSet<int32>>(SelNode.Get(), TEXT("Indices")))
				{
					*IndicesPtr = Indices;
				}
				return SelNode;
			};

			const FString TetherEndsSetName  = TEXT("LegacyTetherEnds");
			const FString AllSimVertsSetName = TEXT("LegacyAllSimVertices3D");
			TSharedPtr<FDataflowNode> SelTetherEnds  = AddSimVertex3DSelection(TEXT("LegacyTetherEnds"),       TetherEndsSetName,  TetherEnds);
			TSharedPtr<FDataflowNode> SelAllSimVerts = AddSimVertex3DSelection(TEXT("LegacyAllSimVertices3D"), AllSimVertsSetName, AllSimVertices3D);

			TSharedPtr<UE::Dataflow::FGraph> Graph = Dataflow->GetDataflow();
			if (!Graph || !SelTetherEnds || !SelAllSimVerts)
			{
				return;
			}

			// SelTetherEnds and SelAllSimVerts have already been added to the graph by AddNewNode;
			// track them here so the editor view is refreshed even if the splice path below bails
			// (otherwise the newly-added nodes would be invisible until the user touches the asset).
			TSet<FGuid> TetherNodesToRefresh;
			TetherNodesToRefresh.Add(LraNode->GetGuid());
			TetherNodesToRefresh.Add(SelTetherEnds->GetGuid());
			TetherNodesToRefresh.Add(SelAllSimVerts->GetGuid());

			// Splice [SelTetherEnds, SelAllSimVerts] into the Collection chain immediately upstream
			// of LRA_v2 so the named selections are written onto the cloth collection before LRA
			// evaluates.
			FDataflowInput* const LraCollectionInput = LraNode->FindInput(FName(TEXT("Collection")));
			if (LraCollectionInput)
			{
				const FDataflowOutput* const ConnectedFromOutput = LraCollectionInput->GetConnection();

				Dataflow->Modify();

				FDataflowOutput* MutableUpstreamOutput = nullptr;
				if (ConnectedFromOutput)
				{
					FDataflowNode* const UpstreamNode = const_cast<FDataflowNode*>(ConnectedFromOutput->GetOwningNode());
					if (UpstreamNode)
					{
						MutableUpstreamOutput = UpstreamNode->FindOutput(ConnectedFromOutput->GetName());
						TetherNodesToRefresh.Add(UpstreamNode->GetGuid());
					}
				}
				Graph->ClearConnections(LraCollectionInput);

				// Chain: UpstreamOutput -> SelTetherEnds.Collection -> SelAllSimVerts.Collection -> LraCollectionInput.
				FDataflowInput* const  Sel1Input  = SelTetherEnds->FindInput(FName(TEXT("Collection")));
				FDataflowOutput* const Sel1Output = SelTetherEnds->FindOutput(FName(TEXT("Collection")));
				FDataflowInput* const  Sel2Input  = SelAllSimVerts->FindInput(FName(TEXT("Collection")));
				FDataflowOutput* const Sel2Output = SelAllSimVerts->FindOutput(FName(TEXT("Collection")));

				if (MutableUpstreamOutput && Sel1Input)
				{
					Graph->Connect(*MutableUpstreamOutput, *Sel1Input);
				}
				if (Sel1Output && Sel2Input)
				{
					Graph->Connect(*Sel1Output, *Sel2Input);
				}
				if (Sel2Output)
				{
					Graph->Connect(*Sel2Output, *LraCollectionInput);
				}

				FDataflowOutput* const Sel1NameOut    = SelTetherEnds->FindOutput(FName(TEXT("OutputName.StringValue")));
				FDataflowOutput* const Sel2NameOut    = SelAllSimVerts->FindOutput(FName(TEXT("OutputName.StringValue")));
				FDataflowInput* const  LraCustomFixed   = LraNode->FindInput(FName(TEXT("CustomTetherData[0].CustomFixedEndSet.StringValue")));
				FDataflowInput* const  LraCustomDynamic = LraNode->FindInput(FName(TEXT("CustomTetherData[0].CustomDynamicEndSet.StringValue")));
				if (Sel1NameOut && LraCustomFixed)
				{
					Graph->Connect(*Sel1NameOut, *LraCustomFixed);
				}
				if (Sel2NameOut && LraCustomDynamic)
				{
					Graph->Connect(*Sel2NameOut, *LraCustomDynamic);
				}

				WritePlain<bool>(LraNode.Get(), TEXT("bEnableCustomTetherGeneration"), true);
			}

			// Refresh runs unconditionally -- SelectionNodes were added to the graph before the
			// splice path, and would otherwise stay invisible until the user touches the asset.
			for (const FGuid& NodeGuid : TetherNodesToRefresh)
			{
				Dataflow->RefreshEdNodeByGuid(NodeGuid);
			}
		}

		static void BakePhysicsAssetAndReferenceBone(const FConversionContext& Ctx)
		{
			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSetPhysicsAssetNode>(Ctx.Dataflow))
			{
				if (TObjectPtr<UPhysicsAsset>* const P = GetNodePropertyPtr<TObjectPtr<UPhysicsAsset>>(N.Get(), TEXT("PhysicsAsset")))
				{
					*P = Ctx.SourceAsset->PhysicsAsset;
				}
			}

			// Legacy stores ReferenceBoneIndex into the SkeletalMesh ref skeleton; the new node
			// holds it by name. Resolve at convert time so the binding survives skeleton edits.
			if (!Ctx.OwningMesh)
			{
				return;
			}
			const FReferenceSkeleton& RefSkeleton = Ctx.OwningMesh->GetRefSkeleton();
			if (!RefSkeleton.IsValidIndex(Ctx.SourceAsset->ReferenceBoneIndex))
			{
				return;
			}
			const FName ReferenceBoneName = RefSkeleton.GetBoneName(Ctx.SourceAsset->ReferenceBoneIndex);
			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetReferenceBoneNode>(Ctx.Dataflow))
			{
				if (FChaosClothAssetReferenceBoneSelection* const Ref =
					GetNodePropertyPtr<FChaosClothAssetReferenceBoneSelection>(N.Get(), TEXT("ReferenceBone")))
				{
					Ref->Name = ReferenceBoneName;
				}
			}
		}

		/**
		 * Translate the legacy runtime's implicit disables -- missing required mask, or
		 * FClothingSimulationConfig::Initialize skipping zero-stiffness constraints -- into explicit
		 * bActive=false (or bAddAreaConstraint=false on Stretch). LRA_v2 is handled by
		 * ConfigureTethers via NeedsTethers().
		 */
		static void DisableUnusedConstraints(const FConversionContext& Ctx)
		{
			const bool bHasMaxDistance        = Ctx.PhysData.FindWeightMap(EWeightMapTargetCommon::MaxDistance) != nullptr;
			const bool bHasBackstopDistance   = Ctx.PhysData.FindWeightMap(EWeightMapTargetCommon::BackstopDistance) != nullptr;
			const bool bHasBackstopRadius     = Ctx.PhysData.FindWeightMap(EWeightMapTargetCommon::BackstopRadius) != nullptr;
			const bool bHasAnimDriveStiffness = Ctx.PhysData.FindWeightMap(EWeightMapTargetCommon::AnimDriveStiffness) != nullptr;

			if (!bHasMaxDistance)
			{
				if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationMaxDistanceConfigNode>(Ctx.Dataflow))
				{
					WritePlain<bool>(N.Get(), TEXT("bActive"), false);
				}
			}
			// Backstop is meaningless without both masks (legacy runtime no-ops the same way).
			if (!bHasBackstopDistance || !bHasBackstopRadius)
			{
				if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationBackstopConfigNode>(Ctx.Dataflow))
				{
					WritePlain<bool>(N.Get(), TEXT("bActive"), false);
				}
			}

			if (Ctx.PerCloth)
			{
				auto IsZeroWeighted = [](const FChaosClothWeightedValue& W)
				{
					return !(W.Low > 0.f || W.High > 0.f);
				};

				// Edge and Area share FChaosClothAssetSimulationStretchConfigNode, which has no
				// representation for "edges off, area on" -- warn and leave it active if we hit that.
				const bool bEdgeOff = IsZeroWeighted(Ctx.PerCloth->EdgeStiffnessWeighted);
				const bool bAreaOff = IsZeroWeighted(Ctx.PerCloth->AreaStiffnessWeighted);
				if (const TSharedPtr<FDataflowNode> StretchNode = FindNodeByType<FChaosClothAssetSimulationStretchConfigNode>(Ctx.Dataflow))
				{
					if (bEdgeOff && bAreaOff)
					{
						WritePlain<bool>(StretchNode.Get(), TEXT("bActive"), false);
					}
					else if (!bEdgeOff && bAreaOff)
					{
						WritePlain<bool>(StretchNode.Get(), TEXT("bAddAreaConstraint"), false);
					}
					else if (bEdgeOff && !bAreaOff)
					{
						UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
							TEXT("Legacy UChaosClothConfig has EdgeStiffness disabled but AreaStiffness enabled; the new SimulationStretchConfig node cannot represent area-only stretch, so the converted asset will simulate edge stretch at the legacy stiffness values. Manual adjustment required if edge-off behavior must be preserved."));
					}
				}

				// Buckling only counts toward bending when bUseBendingElements is set.
				const bool bBendingOff = IsZeroWeighted(Ctx.PerCloth->BendingStiffnessWeighted) &&
					(!Ctx.PerCloth->bUseBendingElements || IsZeroWeighted(Ctx.PerCloth->BucklingStiffnessWeighted));
				if (bBendingOff)
				{
					if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationBendingConfigNode>(Ctx.Dataflow))
					{
						WritePlain<bool>(N.Get(), TEXT("bActive"), false);
					}
				}

				if (!bHasAnimDriveStiffness || IsZeroWeighted(Ctx.PerCloth->AnimDriveStiffness))
				{
					if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationAnimDriveConfigNode>(Ctx.Dataflow))
					{
						WritePlain<bool>(N.Get(), TEXT("bActive"), false);
					}
				}
			}
			else if (!bHasAnimDriveStiffness)
			{
				// PerCloth absent -- zero-stiffness branch is unreachable, but missing-mask still applies.
				if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationAnimDriveConfigNode>(Ctx.Dataflow))
				{
					WritePlain<bool>(N.Get(), TEXT("bActive"), false);
				}
			}
		}

		/** Warn for legacy properties with no clean remap onto the new system. */
		static void EmitBucketDWarnings(const FConversionContext& Ctx)
		{
			if (Ctx.PerCloth)
			{
				if (Ctx.PerCloth->VolumeStiffness != 0.f)
				{
					UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
						TEXT("Legacy UChaosClothConfig::VolumeStiffness=%f is non-default but has no equivalent in the new ChaosClothAsset; behavior will differ."),
						Ctx.PerCloth->VolumeStiffness);
				}
				if (Ctx.PerCloth->ShapeTargetStiffness != 0.f)
				{
					UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
						TEXT("Legacy UChaosClothConfig::ShapeTargetStiffness=%f is non-default but has no equivalent in the new ChaosClothAsset; behavior will differ."),
						Ctx.PerCloth->ShapeTargetStiffness);
				}
				if (Ctx.PerCloth->bUseTetrahedralConstraints)
				{
					UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
						TEXT("Legacy UChaosClothConfig::bUseTetrahedralConstraints=true has no equivalent in the new ChaosClothAsset; tetrahedral constraints will not be applied."));
				}
				if (Ctx.PerCloth->bUseThinShellVolumeConstraints)
				{
					UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
						TEXT("Legacy UChaosClothConfig::bUseThinShellVolumeConstraints=true has no equivalent in the new ChaosClothAsset; thin-shell volume constraints will not be applied."));
				}
				if (Ctx.PerCloth->bUseContinuousCollisionDetection)
				{
					UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
						TEXT("Legacy UChaosClothConfig::bUseContinuousCollisionDetection=true has no equivalent in the new ChaosClothAsset; use SimulationCollisionConfig::bUseCCD on the converted asset instead."));
				}
			}
			if (Ctx.Shared)
			{
				if (!Ctx.Shared->bUseLocalSpaceSimulation)
				{
					UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
						TEXT("Legacy UChaosClothSharedSimConfig::bUseLocalSpaceSimulation=false is unsupported in the new ChaosClothAsset (local-space simulation is always enabled); converted asset will use local-space, which may cause jitter behavior to differ for distant-from-origin characters."));
				}
				if (Ctx.Shared->bUseXPBDConstraints)
				{
					UE_LOG(LogChaosClothAssetLegacyClothingConverter, Warning,
						TEXT("Legacy UChaosClothSharedSimConfig::bUseXPBDConstraints=true conversion is unsupported. Manual conversion of SimulationStretchConfig and SimulationBendConfig is required."));
				}
			}
		}

		static void ApplySolverConfig(const FConversionContext& Ctx)
		{
			if (!Ctx.Shared)
			{
				return;
			}
			if (const TSharedPtr<FDataflowNode> N = FindNodeByType<FChaosClothAssetSimulationSolverConfigNode>(Ctx.Dataflow))
			{
				WritePlain<int32>(N.Get(), TEXT("NumIterations"), Ctx.Shared->IterationCount);
				WritePlain<int32>(N.Get(), TEXT("MaxNumIterations"), Ctx.Shared->MaxIterationCount);
				WriteToImported<FChaosClothAssetImportedIntValue, int32>(N.Get(), TEXT("NumSubstepsImported"), Ctx.Shared->SubdivisionCount);
			}
		}

		/** Bake the converted graph into the asset immediately so the user doesn't have to open it. */
		static void ForceTemplateEvaluation(const FConversionContext& Ctx)
		{
			const FName TerminalName = Ctx.NewClothAsset->GetDataflowInstance().GetDataflowTerminal();
			UDataflowBlueprintLibrary::EvaluateTerminalNodeByName(Ctx.Dataflow, TerminalName, Ctx.NewClothAsset);
		}
	}

	FLegacyClothingConverterResult FLegacyClothingConverter::Convert(
		const UClothingAssetCommon* SourceAsset,
		const FString& OutputPackagePath,
		const FString& AssetName)
	{
		FLegacyClothingConverterResult Result;

		if (!SourceAsset)
		{
			Result.ErrorText = LOCTEXT("NullSource", "SourceAsset is null.");
			return Result;
		}

		// Resolve the output package + asset name, then create an empty UChaosClothAsset. The
		// embedded Dataflow + the per-graph conversion work is deferred to ConvertInto, which we
		// share with the right-click "Export to Chaos Cloth Asset" path.
		const FString ResolvedAssetName = AssetName.IsEmpty()
			? FString::Printf(TEXT("CA_Converted_%s"), *SourceAsset->GetName())
			: AssetName;
		const FString PackageName = FString::Printf(TEXT("%s/%s"),
			OutputPackagePath.EndsWith(TEXT("/")) ? *OutputPackagePath.LeftChop(1) : *OutputPackagePath,
			*ResolvedAssetName);

		UPackage* const Package = CreatePackage(*PackageName);
		if (!Package)
		{
			Result.ErrorText = FText::Format(LOCTEXT("CreatePackageFailed", "Failed to create package at {0}."), FText::FromString(PackageName));
			return Result;
		}

		UChaosClothAsset* const NewAsset = NewObject<UChaosClothAsset>(
			Package, FName(*ResolvedAssetName), RF_Public | RF_Standalone | RF_Transactional);
		if (!NewAsset)
		{
			Result.ErrorText = LOCTEXT("AssetCreateFailed", "Failed to create UChaosClothAsset.");
			return Result;
		}
		FAssetRegistryModule::AssetCreated(NewAsset);

		Result = ConvertInto(SourceAsset, NewAsset);

		// Clean up the just-created asset if ConvertInto failed (CreatedAsset is null on failure
		// after the validation reordering in ConvertInto). Without this, a failed Convert leaves
		// an orphan UChaosClothAsset registered with the asset registry that the operator then
		// has to manually delete from the content browser.
		if (!Result.CreatedAsset)
		{
			FAssetRegistryModule::AssetDeleted(NewAsset);
			NewAsset->ClearFlags(RF_Public | RF_Standalone);
			NewAsset->MarkAsGarbage();
			Package->ClearFlags(RF_Public | RF_Standalone);
			Package->MarkAsGarbage();
		}
		return Result;
	}

	FLegacyClothingConverterResult FLegacyClothingConverter::ConvertInto(
		const UClothingAssetCommon* SourceAsset,
		UChaosClothAsset* TargetAsset)
	{
		FLegacyClothingConverterResult Result;

		if (!SourceAsset)
		{
			Result.ErrorText = LOCTEXT("NullSource", "SourceAsset is null.");
			return Result;
		}
		if (!TargetAsset)
		{
			Result.ErrorText = LOCTEXT("NullTarget", "TargetAsset is null.");
			return Result;
		}

		// Pre-flight: validate everything that doesn't require mutating TargetAsset, so a failure
		// here leaves the target untouched (and Convert() can safely garbage-collect the asset it
		// just created without having half-applied state to roll back).
		// TODO: multi-LOD -- only LOD 0 is imported; LodData[1..] are silently dropped.
		if (!SourceAsset->LodData.IsValidIndex(0))
		{
			Result.ErrorText = LOCTEXT("NoLOD0", "Source clothing asset has no LOD 0.");
			return Result;
		}
		const FClothPhysicalMeshData& PhysData = SourceAsset->LodData[0].PhysicalMeshData;

		const int32 NumLegacyVerts = PhysData.Vertices.Num();
		const int32 NumLegacyTris  = PhysData.Indices.Num() / 3;
		if (NumLegacyVerts == 0 || NumLegacyTris == 0)
		{
			Result.ErrorText = LOCTEXT("EmptyMesh", "Source clothing asset LOD 0 has no simulation mesh data.");
			return Result;
		}

		// Embed the legacy conversion template's Dataflow into the target asset, replacing any
		// existing one. Mirrors the embed step that UE::DataflowAssetDefinitionHelpers::FactoryCreateNew
		// does when creating from a template. Idempotent -- invoking back-to-back with the same
		// template just replaces it with a fresh duplicate.
		static const FString LegacyTemplatePath(TEXT("/ChaosClothAsset/DF_LegacyClothingAssetTemplate.DF_LegacyClothingAssetTemplate"));
		UDataflow* const Template = LoadObject<UDataflow>(nullptr, *LegacyTemplatePath);
		if (!Template)
		{
			Result.ErrorText = FText::Format(
				LOCTEXT("LoadTemplateFailed", "Failed to load legacy conversion Dataflow template at {0}."),
				FText::FromString(LegacyTemplatePath));
			return Result;
		}
		UDataflow* const Embedded = DuplicateObject(Template, TargetAsset, TEXT("EmbeddedDataflow"));
		if (!Embedded)
		{
			Result.ErrorText = LOCTEXT("EmbedTemplateFailed", "Failed to embed legacy template Dataflow into target asset.");
			return Result;
		}
		Embedded->ClearFlags(RF_Public | RF_Standalone);
		Embedded->SetFlags(RF_Transactional);
		TargetAsset->GetDataflowInstance().SetDataflowAsset(Embedded);

		UDataflow* const Dataflow = Private::GetEmbeddedDataflow(TargetAsset);
		if (!Dataflow)
		{
			Result.ErrorText = LOCTEXT("NoEmbeddedDataflow", "Target cloth asset has no embedded Dataflow graph. Verify the legacy template asset.");
			return Result;
		}

		// All pre-flight checks passed and the dataflow has been embedded; commit success on
		// Result. Subsequent step failures are non-fatal (ensure-fires, warning logs) and the
		// returned asset is still usable.
		UChaosClothAsset* const NewClothAsset = TargetAsset;
		Result.CreatedAsset = NewClothAsset;
		Result.CreatedAssetPath = NewClothAsset->GetPathName();

		USkeletalMesh* const OwningMesh = Cast<USkeletalMesh>(SourceAsset->GetOuter());
		TArray<int32> CompactToLegacyVertex;
		Private::ImportSimClothCollectionAndApplyVariables(NewClothAsset, SourceAsset, PhysData, OwningMesh, CompactToLegacyVertex);

		const UChaosClothConfig* const PerCloth = SourceAsset->GetClothConfig<UChaosClothConfig>();
		const UChaosClothSharedSimConfig* const Shared = SourceAsset->GetClothConfig<UChaosClothSharedSimConfig>();
		const Private::FConversionContext Ctx
		{
			Dataflow,
			NewClothAsset,
			SourceAsset,
			PhysData,
			PerCloth,
			Shared,
			OwningMesh,
			CompactToLegacyVertex
		};

		Private::ConfigureRenderImportNodes(Ctx);
		Private::BakeClothConfigScalars(Ctx);
		Private::CreateWeightMapNodes(Ctx);
		Private::ConfigureTethers(Ctx);
		Private::ApplySolverConfig(Ctx);
		Private::BakePhysicsAssetAndReferenceBone(Ctx);
		Private::DisableUnusedConstraints(Ctx);
		Private::EmitBucketDWarnings(Ctx);
		Private::ForceTemplateEvaluation(Ctx);

		if (UPackage* const AssetPackage = NewClothAsset->GetPackage())
		{
			AssetPackage->MarkPackageDirty();
		}

		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
