// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/SubdividePoly.h"
#include "GroupTopology.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "BoneWeights.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SubdividePoly)

// OpenSubdiv currently only available on Windows, Mac and Unix. On other platforms we will make this a no-op
#if PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_UNIX
#define HAVE_OPENSUBDIV 1
#else
#define HAVE_OPENSUBDIV 0
#endif


#if HAVE_OPENSUBDIV

// OpenSubdiv needs M_PI defined
#ifndef M_PI
#define M_PI PI
#define LOCAL_M_PI 1
#endif

#pragma warning(push, 0)     
#include "opensubdiv/far/topologyRefiner.h"
#include "opensubdiv/far/topologyDescriptor.h"
#include "opensubdiv/far/primvarRefiner.h"
#pragma warning(pop)     

#ifdef LOCAL_M_PI
#undef M_PI
#endif

#endif

using namespace UE::Geometry;

namespace SubdividePolyLocal
{
	// Generic interpolation wrapper for OpenSubdiv PrimvarRefiner. Used for positions, UVs, colors,
	// normals, weights, etc. bIsValid propagates overlay element invalidity in AddWithWeight.
	template<typename T>
	struct TSubdValue
	{
		T Value{};
		bool bIsValid = true;

		TSubdValue() = default;
		explicit TSubdValue(const T& InValue) : Value(InValue) {}

		void Clear()
		{
			Value = T{};
			bIsValid = true;
		}

		void AddWithWeight(const TSubdValue& Src, float Weight)
		{
			Value += Weight * Src.Value;
			bIsValid &= Src.bIsValid;
		}
	};

	using FSubdFloat = TSubdValue<float>;
	using FSubdVec2f = TSubdValue<FVector2f>;
	using FSubdVec3f = TSubdValue<FVector3f>;
	using FSubdVec3d = TSubdValue<FVector3d>;
	using FSubdVec4f = TSubdValue<FVector4f>;


	// Get the indices of GroupTopology "Corners" for a particular group boundary.
	void GetBoundaryCorners(const FGroupTopology::FGroupBoundary& Boundary,
							const FGroupTopology& Topology,
							TArray<int>& Corners)
	{
		int FirstEdgeIndex = Boundary.GroupEdges[0];
		Corners.Add(Topology.Edges[FirstEdgeIndex].EndpointCorners[0]);
		Corners.Add(Topology.Edges[FirstEdgeIndex].EndpointCorners[1]);

		int NextEdgeIndex = Boundary.GroupEdges[1];
		FIndex2i NextEdgeCorners = Topology.Edges[NextEdgeIndex].EndpointCorners;
		if (Corners[1] != NextEdgeCorners[0] && Corners[1] != NextEdgeCorners[1])
		{
			Swap(Corners[0], Corners[1]);
			check(Corners[1] == NextEdgeCorners[0] || Corners[1] == NextEdgeCorners[1]);
		}

		for (int i = 1; i < Boundary.GroupEdges.Num() - 1; ++i)
		{
			int EdgeIndex = Boundary.GroupEdges[i];
			FIndex2i CurrEdgeCorners = Topology.Edges[EdgeIndex].EndpointCorners;
			if (Corners.Last() == CurrEdgeCorners[0])
			{
				Corners.Add(CurrEdgeCorners[1]);
			}
			else
			{
				check(Corners.Last() == CurrEdgeCorners[1]);
				Corners.Add(CurrEdgeCorners[0]);
			}
		}
	}

	// Pick the most common value of an attribute for triangles in a polygroup --
	// used to select a single material/polygroup/etc to use on the corresponding OpenSubdiv face
	template<typename AttrType>
	int32 MostCommonTriangleValueInGroup(const FGroupTopology::FGroup& Group, const AttrType* Attr)
	{
		TMap<int32, int32> Votes;
		for (int32 TriangleID : Group.Triangles)
		{
			Votes.FindOrAdd(Attr->GetValue(TriangleID), 0)++;
		}
		int32 MaxVotes = -1;
		int32 BestVal = 0;
		for (const TPair<int32, int32>& V : Votes)
		{
			if (V.Value > MaxVotes)
			{
				MaxVotes = V.Value;
				BestVal = V.Key;
			}
		}
		return BestVal;
	}

	// Given a group and a vertex ID, return:
	// - a triangle in the group with one of its corners equal to vertex ID
	// - the (0-2) triangle corner index corresponding to the given vertex ID
	bool FindTriangleVertex(const FGroupTopology::FGroup& Group,
							int VertexID,
							const FDynamicMesh3& Mesh,
							TTuple<int, int>& OutTriangleVertex)
	{
		for (int Tri : Group.Triangles)		// TODO: do better than linear search
		{
			FIndex3i TriVertices = Mesh.GetTriangle(Tri);
			for (int i = 0; i < 3; ++i)
			{
				if (TriVertices[i] == VertexID)
				{
					OutTriangleVertex = TTuple<int, int>{ Tri, i };
					return true;
				}
			}
		}
		return false;
	}


	// Helper for GetGroupPolyMeshOverlayData and GetMeshOverlayData below
	template<bool bBuildingCoordBuffer, typename OverlayType, typename ElementWrapperType>
	int AddOrFindOverlayElement(const OverlayType& Overlay, int CornerElementID, TMap<int, int>& ElementIDToBufferIndex,
		int& NumBufferElems, TArray<ElementWrapperType>* OutElements)
	{
		if (const int* ExistingIdx = ElementIDToBufferIndex.Find(CornerElementID))
		{
			return *ExistingIdx;
		}

		const int NewIndex = NumBufferElems++;
		if (CornerElementID != FDynamicMesh3::InvalidID)
		{
			ElementIDToBufferIndex.Add(CornerElementID, NewIndex);
		}
		if constexpr (bBuildingCoordBuffer)
		{
			if (CornerElementID == FDynamicMesh3::InvalidID)
			{
				// Missing overlay data for this triangle corner
				ElementWrapperType InvalidElem{};
				InvalidElem.bIsValid = false;
				OutElements->Add(InvalidElem);
			}
			else
			{
				OutElements->Add(ElementWrapperType(Overlay.GetElement(CornerElementID)));
			}
		}
		return NewIndex;
	}


	// Get face-varying overlay data, treating each FGroupTopology group as a polygonal face.
	// Assumes overlay seams only occur at polygon boundaries (not within a polygonal face).

	template<bool bBuildingCoordBuffer, bool bBuildingIndexBuffer, typename OverlayType, typename ElementWrapperType>
	bool GetGroupPolyMeshOverlayData(const FGroupTopology& Topology,
									 const FDynamicMesh3& Mesh,
									 const OverlayType& Overlay,
									 TArray<ElementWrapperType>* OutElements,
									 TArray<int>* OutFaceIndices,
									 int* OutNumBufferElems = nullptr)
	{
		TMap<int, int> ElementIDToBufferIndex;
		int NumBufferElems = 0;

		for (const FGroupTopology::FGroup& Group : Topology.Groups)
		{
			if (!ensure(Group.Boundaries.Num() == 1))
			{
				return false;
			}
			if (!ensure(Group.Triangles.Num() > 0))
			{
				return false;
			}

			const FGroupTopology::FGroupBoundary& Bdry = Group.Boundaries[0];

			TArray<int> Corners;
			GetBoundaryCorners(Bdry, Topology, Corners);

			TArray<int> CornerIndices;

			for (int CornerID : Corners)
			{
				const int CornerVertexID = Topology.Corners[CornerID].VertexID;

				// Find a triangle in the group that has a vertex ID equal to the given corner
				TTuple<int, int> TriangleVertex;
				if (FindTriangleVertex(Group, CornerVertexID, Mesh, TriangleVertex))
				{
					const int TriangleID = TriangleVertex.Get<0>();
					const int TriVertexIndex = TriangleVertex.Get<1>();	// The (0,1,2) index of the polygon corner wrt the triangle

					const FIndex3i ElementTri = Overlay.GetTriangle(TriangleID);
					const int CornerElementID = ElementTri[TriVertexIndex];

					const int BufferSlot = AddOrFindOverlayElement<bBuildingCoordBuffer>(Overlay, CornerElementID, ElementIDToBufferIndex, NumBufferElems, OutElements);
					if constexpr (bBuildingIndexBuffer)
					{
						CornerIndices.Add(BufferSlot);
					}
				}
				else
				{
					return false;
				}
			}

			if constexpr (bBuildingIndexBuffer)
			{
				OutFaceIndices->Append(CornerIndices);
			}
		}

		if (OutNumBufferElems)
		{
			*OutNumBufferElems = NumBufferElems;
		}
		return true;
	}


	template<typename OverlayType, typename ElementWrapperType>
	inline bool GetGroupPolyMeshOverlayCoords(const FGroupTopology& Topology,
		const FDynamicMesh3& Mesh, const OverlayType& Overlay, TArray<ElementWrapperType>* OutElements)
	{
		return GetGroupPolyMeshOverlayData<true, false>(Topology, Mesh, Overlay, OutElements, nullptr);
	}

	template<typename OverlayType>
	inline bool GetGroupPolyMeshOverlayIndexBuffer(const FGroupTopology& Topology,
		const FDynamicMesh3& Mesh, const OverlayType& Overlay, TArray<int>* OutFaceIndices,
		int* OutNumBufferElems = nullptr)
	{
		return GetGroupPolyMeshOverlayData<false, true, OverlayType, int>(Topology, Mesh, Overlay, nullptr, OutFaceIndices, OutNumBufferElems);
	}


	// Extract face-varying data from an overlay for a triangle mesh (no group topology).

	template<bool bBuildingCoordBuffer, bool bBuildingIndexBuffer, typename OverlayType, typename ElementWrapperType>
	bool GetMeshOverlayData(const FDynamicMesh3& Mesh,
							const OverlayType& Overlay,
							TArray<ElementWrapperType>* OutElements,
							TArray<int>* OutFaceIndices,
							int* OutNumBufferElems = nullptr)
	{
		TMap<int, int> ElementIDToBufferIndex;
		int NumBufferElems = 0;

		for (int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const FIndex3i ElementTri = Overlay.GetTriangle(TriangleID);

			FIndex3i CornerIndices{ FDynamicMesh3::InvalidID, FDynamicMesh3::InvalidID, FDynamicMesh3::InvalidID };

			for (int I = 0; I < 3; ++I)
			{
				const int CornerElementID = ElementTri[I];
				const int BufferSlot = AddOrFindOverlayElement<bBuildingCoordBuffer>(Overlay, CornerElementID, ElementIDToBufferIndex, NumBufferElems, OutElements);
				if constexpr (bBuildingIndexBuffer)
				{
					CornerIndices[I] = BufferSlot;
				}
			}

			if constexpr (bBuildingIndexBuffer)
			{
				OutFaceIndices->Add(CornerIndices[0]);
				OutFaceIndices->Add(CornerIndices[1]);
				OutFaceIndices->Add(CornerIndices[2]);
			}
		}

		if (OutNumBufferElems)
		{
			*OutNumBufferElems = NumBufferElems;
		}
		return true;
	}




	template<typename OverlayType, typename ElementWrapperType>
	inline bool GetMeshOverlayCoords(const FDynamicMesh3& Mesh,
		const OverlayType& Overlay, TArray<ElementWrapperType>* OutElements)
	{
		return GetMeshOverlayData<true, false>(Mesh, Overlay, OutElements, nullptr);
	}

	template<typename OverlayType>
	inline bool GetMeshOverlayIndexBuffer(const FDynamicMesh3& Mesh,
		const OverlayType& Overlay, TArray<int>* OutFaceIndices,
		int* OutNumBufferElems = nullptr)
	{
		return GetMeshOverlayData<false, true, OverlayType, int>(Mesh, Overlay, nullptr, OutFaceIndices, OutNumBufferElems);
	}



	// Helper to run vertex-level interpolation (eg per-vertex weights) through all refinement levels.
	// SourceData is consumed (swapped with refined output each level).
	template<typename T>
	void InterpolateVertexData(
		OpenSubdiv::Far::PrimvarRefiner& Interpolator,
		const OpenSubdiv::Far::TopologyRefiner& TopologyRefiner,
		int Level,
		TArray<T>& SourceData,
		TArray<T>& OutRefinedData)
	{
		for (int CurrentLevel = 1; CurrentLevel <= Level; ++CurrentLevel)
		{
			int NumVertices = TopologyRefiner.GetLevel(CurrentLevel).GetNumVertices();
			OutRefinedData.SetNumUninitialized(NumVertices);
			T* Src = SourceData.GetData();
			T* Dst = OutRefinedData.GetData();
			Interpolator.Interpolate(CurrentLevel, Src, Dst);
			SourceData = OutRefinedData;
		}
	}

	// Helper to run face-varying interpolation (eg UVs, color overlays) through all refinement levels.
	template<typename T>
	void InterpolateFaceVaryingData(
		OpenSubdiv::Far::PrimvarRefiner& Interpolator,
		const OpenSubdiv::Far::TopologyRefiner& TopologyRefiner,
		int Level,
		int FVarChannel,
		TArray<T>& SourceData,
		TArray<T>& OutRefinedData)
	{
		for (int CurrentLevel = 1; CurrentLevel <= Level; ++CurrentLevel)
		{
			int NumFVarValues = TopologyRefiner.GetLevel(CurrentLevel).GetNumFVarValues(FVarChannel);
			OutRefinedData.SetNumUninitialized(NumFVarValues);
			T* Src = SourceData.GetData();
			T* Dst = OutRefinedData.GetData();
			Interpolator.InterpolateFaceVarying(CurrentLevel, Src, Dst, FVarChannel);
			SourceData = OutRefinedData;
		}
	}

	// Helper to run face-uniform interpolation (eg material IDs, triangle groups) through all refinement levels.
	template<typename T>
	void InterpolateFaceUniformData(
		OpenSubdiv::Far::PrimvarRefiner& Interpolator,
		const OpenSubdiv::Far::TopologyRefiner& TopologyRefiner,
		int Level,
		TArray<T>& SourceData,
		TArray<T>& OutRefinedData)
	{
		for (int CurrentLevel = 1; CurrentLevel <= Level; ++CurrentLevel)
		{
			OutRefinedData.SetNumUninitialized(TopologyRefiner.GetLevel(CurrentLevel).GetNumFaces());
			T* Src = SourceData.GetData();
			T* Dst = OutRefinedData.GetData();
			Interpolator.InterpolateFaceUniform(CurrentLevel, Src, Dst);
			SourceData = OutRefinedData;
		}
	}

	// Initialize a face-varying overlay (normal, UV, color) on the output mesh from refined element data and per-triangle element indices.
	template<typename OverlayType, typename ElementType>
	void InitializeOverlayFromRefinedData(
		OverlayType* Overlay,
		const TArray<FIndex3i>& ElementTriangles,
		const TArray<ElementType>& Elements)
	{
		const FDynamicMesh3* Mesh = Overlay->GetParentMesh();
		check(Mesh->IsCompact());

		Overlay->ClearElements();
		for (int ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
		{
			Overlay->AppendElement(Elements[ElementIndex]);
		}

		int NumTriangles = Mesh->TriangleCount();
		check(NumTriangles == Mesh->MaxTriangleID());
		check(ElementTriangles.Num() == NumTriangles);

		Overlay->InitializeTriangles(NumTriangles);
		for (int TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
		{
			const FIndex3i& ElemTri = ElementTriangles[TriangleIndex];

			if (ensure(Mesh->IsTriangle(TriangleIndex)))
			{
				const FIndex3i MeshTriVertices = Mesh->GetTriangle(TriangleIndex);
				for (int I = 0; I < 3; ++I)
				{
					if (ElemTri[I] != FDynamicMesh3::InvalidID)
					{
						Overlay->SetParentVertex(ElemTri[I], MeshTriVertices[I]);
					}
				}
			}

			if (ElemTri[0] == FDynamicMesh3::InvalidID || ElemTri[1] == FDynamicMesh3::InvalidID || ElemTri[2] == FDynamicMesh3::InvalidID)
			{
				Overlay->UnsetTriangle(TriangleIndex);
			}
			else
			{
				Overlay->SetTriangle(TriangleIndex, ElemTri);
			}
		}
	}


}	// namespace SubdivisionSurfaceLocal


// Stores the face-varying channel mapping so ComputeSubdividedMesh knows which OpenSubdiv channel corresponds to which mesh attribute.
struct FFVarChannelMapping
{
	// Index of each UV layer's face-varying channel in the OpenSubdiv descriptor (INDEX_NONE if not present)
	TArray<int> UVLayerFVarChannels;

	// Face-varying channel index for the color overlay (INDEX_NONE if not present)
	int ColorFVarChannel = INDEX_NONE;

	// Face-varying channel index for the primary normal overlay (INDEX_NONE if not present or not interpolated)
	int NormalFVarChannel = INDEX_NONE;
};


class FSubdividePoly::RefinerImpl
{
public:
#if HAVE_OPENSUBDIV
	OpenSubdiv::Far::TopologyRefiner* TopologyRefiner = nullptr;

	// Mapping from mesh attributes to OpenSubdiv face-varying channel indices
	FFVarChannelMapping FVarMapping;
#endif
};

FSubdividePoly::FSubdividePoly(const FGroupTopology& InTopology,
							   const FDynamicMesh3& InOriginalMesh,
							   int InLevel) :
	GroupTopology(InTopology)
	, OriginalMesh(InOriginalMesh)
	, Level(InLevel)
{
	Refiner = MakeUnique<RefinerImpl>();
}

FSubdividePoly::~FSubdividePoly()
{
#if HAVE_OPENSUBDIV
	if (Refiner && Refiner->TopologyRefiner)
	{
		// This was created by TopologyRefinerFactory; looks like we are responsible for cleaning it up.
		delete Refiner->TopologyRefiner;
	}
#endif
	Refiner = nullptr;
}


bool FSubdividePoly::ComputeTopologySubdivision()
{
#if HAVE_OPENSUBDIV
	if (Level < 1)
	{
		return false;
	}

	TArray<int> BoundaryVertsPerFace;
	TArray<int> NumVertsPerFace;
	
	TArray<OpenSubdiv::Far::TopologyDescriptor::FVarChannel> FVarChannels;
	// Index buffers for face-varying channels. Must outlive the TopologyDescriptor (which holds raw
	// pointers into them) but are not needed after TopologyRefinerFactory::Create copies the data.
	TArray<TArray<int>> FVarIndexBuffers;
	FFVarChannelMapping& FVarMapping = Refiner->FVarMapping;
	FVarMapping = FFVarChannelMapping();

	// Helper to register a face-varying channel for any overlay type (UV, color, etc)
	auto RegisterFVarChannelForOverlay = [this, &FVarChannels, &FVarIndexBuffers]
	<typename OverlayType>(const OverlayType& Overlay, bool bUseGroupTopology) -> int
	{
		int ChannelIndex = FVarChannels.Num();
		TArray<int>& IndexBuffer = FVarIndexBuffers.AddDefaulted_GetRef();
		int NumBufferElems = 0;

		bool bGetIndexOK = bUseGroupTopology ?
			 SubdividePolyLocal::GetGroupPolyMeshOverlayIndexBuffer(GroupTopology, OriginalMesh, Overlay, &IndexBuffer, &NumBufferElems)
			 : SubdividePolyLocal::GetMeshOverlayIndexBuffer(OriginalMesh, Overlay, &IndexBuffer, &NumBufferElems);

		if (!bGetIndexOK)
		{
			return INDEX_NONE;
		}

		OpenSubdiv::Far::TopologyDescriptor::FVarChannel Channel;
		Channel.numValues = NumBufferElems;
		Channel.valueIndices = IndexBuffer.GetData();
		FVarChannels.Add(Channel);
		return ChannelIndex;
	};

	// Register all face-varying channels (UV layers, color overlay, normal overlay) and finalize the descriptor
	auto RegisterAllFVarChannels = [this, &FVarChannels, &FVarMapping, &RegisterFVarChannelForOverlay]
	(OpenSubdiv::Far::TopologyDescriptor& Descriptor, bool bUseGroupTopology) -> bool
	{
		// Register face-varying channels for all UV layers
		if (UVComputationMethod == ESubdivisionOutputUVs::Interpolated && OriginalMesh.HasAttributes())
		{
			const int NumUVLayers = OriginalMesh.Attributes()->NumUVLayers();
			FVarMapping.UVLayerFVarChannels.SetNumUninitialized(NumUVLayers);

			for (int LayerIdx = 0; LayerIdx < NumUVLayers; ++LayerIdx)
			{
				const FDynamicMeshUVOverlay* UVLayer = OriginalMesh.Attributes()->GetUVLayer(LayerIdx);
				if (UVLayer != nullptr && UVLayer->ElementCount() > 0)
				{
					int Channel = RegisterFVarChannelForOverlay(*UVLayer, bUseGroupTopology);
					if (Channel < 0)
					{
						return false;
					}
					FVarMapping.UVLayerFVarChannels[LayerIdx] = Channel;
				}
				else
				{
					FVarMapping.UVLayerFVarChannels[LayerIdx] = INDEX_NONE;
				}
			}
		}

		// Register face-varying channel for color overlay
		if (OriginalMesh.HasAttributes() && OriginalMesh.Attributes()->HasPrimaryColors())
		{
			const FDynamicMeshColorOverlay* ColorOverlay = OriginalMesh.Attributes()->PrimaryColors();
			if (ColorOverlay != nullptr && ColorOverlay->ElementCount() > 0)
			{
				int Channel = RegisterFVarChannelForOverlay(*ColorOverlay, bUseGroupTopology);
				if (Channel < 0)
				{
					return false;
				}
				FVarMapping.ColorFVarChannel = Channel;
			}
		}

		// Register face-varying channel for the primary normal overlay (in Interpolated mode)
		if (NormalComputationMethod == ESubdivisionOutputNormals::Interpolated
			&& OriginalMesh.HasAttributes() && OriginalMesh.Attributes()->NumNormalLayers() > 0)
		{
			const FDynamicMeshNormalOverlay* NormalOverlay = OriginalMesh.Attributes()->PrimaryNormals();
			if (NormalOverlay != nullptr && NormalOverlay->ElementCount() > 0)
			{
				int Channel = RegisterFVarChannelForOverlay(*NormalOverlay, bUseGroupTopology);
				if (Channel < 0)
				{
					return false;
				}
				FVarMapping.NormalFVarChannel = Channel;
			}
		}

		Descriptor.numFVarChannels = FVarChannels.Num();
		Descriptor.fvarChannels = FVarChannels.Num() > 0 ? FVarChannels.GetData() : nullptr;

		return true;
	};

	auto DescriptorFromTriangleMesh = [this, &NumVertsPerFace, &BoundaryVertsPerFace, &RegisterAllFVarChannels]
	(OpenSubdiv::Far::TopologyDescriptor& Descriptor)
	{
		for (int32 TriangleID : OriginalMesh.TriangleIndicesItr())
		{
			FIndex3i TriangleVertices = OriginalMesh.GetTriangle(TriangleID);
			NumVertsPerFace.Add(3);
			BoundaryVertsPerFace.Add(TriangleVertices[0]);
			BoundaryVertsPerFace.Add(TriangleVertices[1]);
			BoundaryVertsPerFace.Add(TriangleVertices[2]);
		}

		// TODO: We should probably create a compact mesh descriptor for subdivision to operate on. UETOOL-2944
		Descriptor.numVertices = OriginalMesh.MaxVertexID();
		Descriptor.numFaces = OriginalMesh.TriangleCount();
		Descriptor.numVertsPerFace = NumVertsPerFace.GetData();
		Descriptor.vertIndicesPerFace = BoundaryVertsPerFace.GetData();

		return RegisterAllFVarChannels(Descriptor, /*bUseGroupTopology=*/ false);
	};

	auto DescriptorFromGroupTopology = [this, &NumVertsPerFace, &BoundaryVertsPerFace, &RegisterAllFVarChannels]
	(OpenSubdiv::Far::TopologyDescriptor& Descriptor)
	{
		for (const FGroupTopology::FGroup& Group : GroupTopology.Groups)
		{
			if (Group.Boundaries.Num() != 1)
			{
				return false;
			}

			const FGroupTopology::FGroupBoundary& Boundary = Group.Boundaries[0];
			if (Boundary.GroupEdges.Num() < 2)
			{
				return false;
			}

			TArray<int> Corners;
			SubdividePolyLocal::GetBoundaryCorners(Boundary, GroupTopology, Corners);

			NumVertsPerFace.Add(Corners.Num());
			BoundaryVertsPerFace.Append(Corners);
		}

		Descriptor.numVertices = GroupTopology.Corners.Num();
		Descriptor.numFaces = GroupTopology.Groups.Num();
		Descriptor.numVertsPerFace = NumVertsPerFace.GetData();
		Descriptor.vertIndicesPerFace = BoundaryVertsPerFace.GetData();

		return RegisterAllFVarChannels(Descriptor, /*bUseGroupTopology=*/ true);
	};

	OpenSubdiv::Far::TopologyDescriptor Descriptor;

	if (SubdivisionScheme == ESubdivisionScheme::Loop)
	{
		if (!DescriptorFromTriangleMesh(Descriptor))
		{
			return false;
		}
	}
	else
	{
		if (!DescriptorFromGroupTopology(Descriptor))
		{
			return false;
		}
	}

	using RefinerFactory = OpenSubdiv::Far::TopologyRefinerFactory<OpenSubdiv::Far::TopologyDescriptor>;
	RefinerFactory::Options RefinerOptions;

	OpenSubdiv::Sdc::Options::VtxBoundaryInterpolation BoundaryInterpolation = OpenSubdiv::Sdc::Options::VTX_BOUNDARY_EDGE_ONLY;
	switch (BoundaryScheme)
	{
	case ESubdivisionBoundaryScheme::SmoothCorners:
		BoundaryInterpolation = OpenSubdiv::Sdc::Options::VTX_BOUNDARY_EDGE_ONLY;
		break;
	case ESubdivisionBoundaryScheme::SharpCorners:
		BoundaryInterpolation = OpenSubdiv::Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER;
		break;

	// Note that supporting the below option is a little bit more work than just uncommenting.
	// This option tags the boundary faces as holes, so we have to use the FTopologyLevel::IsFaceHole
	// function where we output the faces, and then probably remove unused verts on the boundary.
	//case ESubdivisionBoundaryScheme::NoBoundaryFaces:
	//	BoundaryInterpolation = OpenSubdiv::Sdc::Options::VTX_BOUNDARY_NONE;
	//	break;

	default:
		ensure(false);
		break;
	}
	RefinerOptions.schemeOptions.SetVtxBoundaryInterpolation(BoundaryInterpolation);

	switch (SubdivisionScheme)
	{
	case ESubdivisionScheme::Bilinear:
		RefinerOptions.schemeType = OpenSubdiv::Sdc::SchemeType::SCHEME_BILINEAR;
		break;
	case ESubdivisionScheme::CatmullClark:
		RefinerOptions.schemeType = OpenSubdiv::Sdc::SchemeType::SCHEME_CATMARK;
		break;
	case ESubdivisionScheme::Loop:
		RefinerOptions.schemeType = OpenSubdiv::Sdc::SchemeType::SCHEME_LOOP;
		break;
	}

	Refiner->TopologyRefiner = RefinerFactory::Create(Descriptor, RefinerOptions);

	if (Refiner->TopologyRefiner == nullptr)
	{
		return false;
	}

	Refiner->TopologyRefiner->RefineUniform(OpenSubdiv::Far::TopologyRefiner::UniformOptions(Level));

#endif		// HAVE_OPENSUBDIV

	return true;
}


bool FSubdividePoly::ComputeSubdividedMesh(FDynamicMesh3& OutMesh)
{
#if HAVE_OPENSUBDIV
	if (Level < 1)
	{
		return false;
	}

	if (!Refiner || !(Refiner->TopologyRefiner))
	{
		return false;
	}

	OpenSubdiv::Far::PrimvarRefiner Interpolator(*Refiner->TopologyRefiner);

	// Helper: iterate over source vertices, accounting for subdiv type
	auto ForEachSourceVertex = [this](TFunctionRef<void(int32)> Fn)
	{
		if (SubdivisionScheme == ESubdivisionScheme::Loop)
		{
			for (int32 VertexID : OriginalMesh.VertexIndicesItr())
			{
				Fn(VertexID);
			}
		}
		else
		{
			for (const FGroupTopology::FCorner& Corner : GroupTopology.Corners)
			{
				Fn(Corner.VertexID);
			}
		}
	};

	// Helper: Get face-uniform int32 (for materials/polygroups) per OpenSubdiv face (by 'voting' most common value in polygroup case)
	auto CollectFaceUniformAttributeSource = [this]<typename AttrType>(const AttrType* Attr, TArray<int32>& OutSource)
	{
		if (SubdivisionScheme == ESubdivisionScheme::Loop)
		{
			for (int32 TriangleID : OriginalMesh.TriangleIndicesItr())
			{
				OutSource.Add(Attr->GetValue(TriangleID));
			}
		}
		else
		{
			for (const FGroupTopology::FGroup& Group : GroupTopology.Groups)
			{
				OutSource.Add(SubdividePolyLocal::MostCommonTriangleValueInGroup(Group, Attr));
			}
		}
	};

	// Helper: extract source / interpolate face-varying overlay values through refinement levels
	auto ExtractAndInterpolateFVarOverlay = [this, &Interpolator]
		<typename SubdValueType, typename OverlayType>
		(int FVarChannel, const OverlayType& Overlay, TArray<SubdValueType>& OutRefined) -> bool
	{
		TArray<SubdValueType> Source;
		const bool bOK = (SubdivisionScheme == ESubdivisionScheme::Loop) ?
			SubdividePolyLocal::GetMeshOverlayCoords(OriginalMesh, Overlay, &Source) :
			SubdividePolyLocal::GetGroupPolyMeshOverlayCoords(GroupTopology, OriginalMesh, Overlay, &Source);
		if (!bOK)
		{
			return false;
		}
		SubdividePolyLocal::InterpolateFaceVaryingData(
			Interpolator, *Refiner->TopologyRefiner, Level, FVarChannel, Source, OutRefined);
		return true;
	};

	//
	// Interpolate vertex positions from initial mesh/group-topology cage down to refinement level
	//
	TArray<SubdividePolyLocal::FSubdVec3d> SourcePositions;
	ForEachSourceVertex([this, &SourcePositions](int32 VertexID)
	{
		SourcePositions.Add(SubdividePolyLocal::FSubdVec3d(OriginalMesh.GetVertex(VertexID)));
	});
	ensure(SourcePositions.Num() == Refiner->TopologyRefiner->GetLevel(0).GetNumVertices());

	TArray<SubdividePolyLocal::FSubdVec3d> RefinedPositions;
	SubdividePolyLocal::InterpolateVertexData(
		Interpolator, *Refiner->TopologyRefiner, Level,
		SourcePositions, RefinedPositions);

	//
	// Interpolate face group IDs
	// 
	TArray<int32> SourceGroupIDs;
	if (SubdivisionScheme == ESubdivisionScheme::Loop)
	{
		for ( int TriangleID : OriginalMesh.TriangleIndicesItr() )
		{
			SourceGroupIDs.Add(OriginalMesh.GetTriangleGroup(TriangleID));
		}
	}
	else
	{
		for (const FGroupTopology::FGroup& Group : GroupTopology.Groups)
		{
			SourceGroupIDs.Add(Group.GroupID);
		}
	}
	check(SourceGroupIDs.Num() == Refiner->TopologyRefiner->GetLevel(0).GetNumFaces());

	TArray<int32> RefinedGroupIDs;
	if (!bNewPolyGroups)
	{
		SubdividePolyLocal::InterpolateFaceUniformData(Interpolator, *Refiner->TopologyRefiner, Level, SourceGroupIDs, RefinedGroupIDs);
	}

	//
	// Interpolate material IDs
	//

	const bool bHasMaterialIDs = OriginalMesh.HasAttributes() && OriginalMesh.Attributes()->HasMaterialID();

	TArray<int32> RefinedMaterialIDs;
	if (bHasMaterialIDs)
	{
		const FDynamicMeshMaterialAttribute* MaterialIDs = OriginalMesh.Attributes()->GetMaterialID();

		TArray<int32> SourceMaterialIDs;
		CollectFaceUniformAttributeSource(MaterialIDs, SourceMaterialIDs);

		SubdividePolyLocal::InterpolateFaceUniformData(Interpolator, *Refiner->TopologyRefiner, Level, SourceMaterialIDs, RefinedMaterialIDs);
	}


	//
	// Interpolate UVs (all layers)
	//
	const FFVarChannelMapping& FVarMapping = Refiner->FVarMapping;
	const bool bHasAttributes = OriginalMesh.HasAttributes();
	const bool bShouldInterpolateUVs = (UVComputationMethod == ESubdivisionOutputUVs::Interpolated) && bHasAttributes;

	// Per-UV-layer refined data (bIsValid on each element tracks overlay element validity)
	const int NumUVLayers = bShouldInterpolateUVs ? FVarMapping.UVLayerFVarChannels.Num() : 0;
	TArray<TArray<SubdividePolyLocal::FSubdVec2f>> AllRefinedUVs;
	AllRefinedUVs.SetNum(NumUVLayers);

	for (int LayerIdx = 0; LayerIdx < NumUVLayers; ++LayerIdx)
	{
		const int FVarChannel = FVarMapping.UVLayerFVarChannels[LayerIdx];
		if (FVarChannel < 0)
		{
			continue;
		}

		const FDynamicMeshUVOverlay* UVLayer = OriginalMesh.Attributes()->GetUVLayer(LayerIdx);
		if (UVLayer == nullptr || UVLayer->ElementCount() == 0)
		{
			continue;
		}

		if (!ExtractAndInterpolateFVarOverlay(FVarChannel, *UVLayer, AllRefinedUVs[LayerIdx]))
		{
			return false;
		}
	}

	//
	// Interpolate vertex colors
	//
	const bool bShouldInterpolateColors = bHasAttributes
		&& FVarMapping.ColorFVarChannel >= 0
		&& OriginalMesh.Attributes()->HasPrimaryColors();

	TArray<SubdividePolyLocal::FSubdVec4f> RefinedColors;

	if (bShouldInterpolateColors)
	{
		const FDynamicMeshColorOverlay* ColorOverlay = OriginalMesh.Attributes()->PrimaryColors();
		if (!ExtractAndInterpolateFVarOverlay(FVarMapping.ColorFVarChannel, *ColorOverlay, RefinedColors))
		{
			return false;
		}
	}

	//
	// Interpolate normals (when NormalComputationMethod is Interpolated)
	//
	const bool bShouldInterpolateNormals = bHasAttributes
		&& FVarMapping.NormalFVarChannel >= 0
		&& OriginalMesh.Attributes()->NumNormalLayers() > 0;

	TArray<SubdividePolyLocal::FSubdVec3f> RefinedNormals;

	if (bShouldInterpolateNormals)
	{
		const FDynamicMeshNormalOverlay* NormalOverlay = OriginalMesh.Attributes()->PrimaryNormals();
		if (!ExtractAndInterpolateFVarOverlay(FVarMapping.NormalFVarChannel, *NormalOverlay, RefinedNormals))
		{
			return false;
		}
	}

	//
	// Interpolate skin weights (all named profiles).
	//
	// FBoneWeights is a sparse representation ({bone_index, weight} pairs per vertex), but OpenSubdiv
	// needs a dense fixed-size buffer per vertex attribute. So we "densify" for interpolation:
	//   1. Collect the set of all bone indices referenced anywhere in the profile -> BoneIndices.
	//   2. For each of those N bone indices, build a per-vertex TSubdValue<float> channel holding
	//      that bone's weight at each vertex (0 where the vertex doesn't reference that bone).
	//   3. Interpolate each channel independently through OpenSubdiv.
	// Later in the "Write skin weight profiles" step we re-sparsify back to FBoneWeights by
	// gathering the non-zero channels for each output vertex.
	struct FSkinWeightProfile
	{
		FName ProfileName;
		TArray<int32> BoneIndices;  // Sorted list of all bone indices referenced in this profile
		TArray<TArray<SubdividePolyLocal::FSubdFloat>> RefinedPerBoneWeights;  // [BoneChannel][VertexIdx]
	};
	TArray<FSkinWeightProfile> RefinedSkinWeightProfiles;

	if (bHasAttributes)
	{
		const TMap<FName, TUniquePtr<FDynamicMeshVertexSkinWeightsAttribute>>& SkinWeightsMap = OriginalMesh.Attributes()->GetSkinWeightsAttributes();
		for (const TPair<FName, TUniquePtr<FDynamicMeshVertexSkinWeightsAttribute>>& Pair : SkinWeightsMap)
		{
			const FName& ProfileName = Pair.Key;
			const FDynamicMeshVertexSkinWeightsAttribute* SkinAttr = Pair.Value.Get();
			if (SkinAttr == nullptr)
			{
				continue;
			}

			FSkinWeightProfile& Profile = RefinedSkinWeightProfiles.AddDefaulted_GetRef();
			Profile.ProfileName = ProfileName;

			TSet<int32> BoneIndexSet = SkinAttr->GetBoundBoneIndices();
			Profile.BoneIndices = BoneIndexSet.Array();
			Profile.BoneIndices.Sort();

			if (Profile.BoneIndices.Num() == 0)
			{
				continue;
			}

			TMap<int32, int32> BoneToChannel;
			for (int32 Ch = 0; Ch < Profile.BoneIndices.Num(); ++Ch)
			{
				BoneToChannel.Add(Profile.BoneIndices[Ch], Ch);
			}

			const int32 NumBoneChannels = Profile.BoneIndices.Num();
			Profile.RefinedPerBoneWeights.SetNum(NumBoneChannels);

			TArray<TArray<SubdividePolyLocal::FSubdFloat>> SourcePerBoneWeights;
			SourcePerBoneWeights.SetNum(NumBoneChannels);

			ForEachSourceVertex([&SourcePerBoneWeights, &BoneToChannel, SkinAttr, NumBoneChannels](int32 VertexID)
			{
				UE::AnimationCore::FBoneWeights BW;
				SkinAttr->GetValue(VertexID, BW);

				for (int32 Ch = 0; Ch < NumBoneChannels; ++Ch)
				{
					SourcePerBoneWeights[Ch].Add(SubdividePolyLocal::FSubdFloat(0.0f));
				}

				for (int32 Idx = 0; Idx < BW.Num(); ++Idx)
				{
					if (int32* ChannelPtr = BoneToChannel.Find(BW[Idx].GetBoneIndex()))
					{
						SourcePerBoneWeights[*ChannelPtr].Last().Value = BW[Idx].GetWeight();
					}
				}
			});

			for (int32 Ch = 0; Ch < NumBoneChannels; ++Ch)
			{
				SubdividePolyLocal::InterpolateVertexData(Interpolator, *Refiner->TopologyRefiner, Level, SourcePerBoneWeights[Ch], Profile.RefinedPerBoneWeights[Ch]);
			}
		}
	}

	//
	// Interpolate morph targets
	//
	struct FMorphTargetData
	{
		FName MorphName;
		TArray<SubdividePolyLocal::FSubdVec3f> RefinedDeltas;
	};
	TArray<FMorphTargetData> RefinedMorphTargets;

	if (bHasAttributes)
	{
		const TMap<FName, TUniquePtr<FDynamicMeshMorphTargetAttribute>>& MorphMap = OriginalMesh.Attributes()->GetMorphTargetAttributes();
		for (const TPair<FName, TUniquePtr<FDynamicMeshMorphTargetAttribute>>& Pair : MorphMap)
		{
			const FName& MorphName = Pair.Key;
			const FDynamicMeshMorphTargetAttribute* MorphAttr = Pair.Value.Get();
			if (MorphAttr == nullptr)
			{
				continue;
			}

			FMorphTargetData& MorphData = RefinedMorphTargets.AddDefaulted_GetRef();
			MorphData.MorphName = MorphName;

			TArray<SubdividePolyLocal::FSubdVec3f> SourceDeltas;
			ForEachSourceVertex([&SourceDeltas, MorphAttr](int32 VertexID)
			{
				FVector3f Delta;
				MorphAttr->GetValue(VertexID, Delta);
				SourceDeltas.Add(SubdividePolyLocal::FSubdVec3f(Delta));
			});

			SubdividePolyLocal::InterpolateVertexData(Interpolator, *Refiner->TopologyRefiner, Level, SourceDeltas, MorphData.RefinedDeltas);
		}
	}

	//
	// Interpolate weight layers
	//
	TArray<TArray<SubdividePolyLocal::FSubdFloat>> RefinedWeightLayers;

	if (bHasAttributes)
	{
		const int NumWeightLayers = OriginalMesh.Attributes()->NumWeightLayers();
		for (int LayerIdx = 0; LayerIdx < NumWeightLayers; ++LayerIdx)
		{
			const FDynamicMeshWeightAttribute* WeightAttr = OriginalMesh.Attributes()->GetWeightLayer(LayerIdx);
			// GetWeightLayer returns non-null for any LayerIdx < NumWeightLayers
			check(WeightAttr != nullptr);

			TArray<SubdividePolyLocal::FSubdFloat>& RefinedLayerWeights = RefinedWeightLayers.AddDefaulted_GetRef();

			TArray<SubdividePolyLocal::FSubdFloat> SourceWeights;
			ForEachSourceVertex([&SourceWeights, WeightAttr](int32 VertexID)
			{
				float W;
				WeightAttr->GetValue(VertexID, &W);
				SourceWeights.Add(SubdividePolyLocal::FSubdFloat(W));
			});

			SubdividePolyLocal::InterpolateVertexData(Interpolator, *Refiner->TopologyRefiner, Level, SourceWeights, RefinedLayerWeights);
		}
	}

	//
	// Interpolate additional polygroup layers (face-uniform)
	//
	TArray<TArray<int32>> RefinedPolygroupLayers;

	if (bHasAttributes)
	{
		const int NumPolygroupLayers = OriginalMesh.Attributes()->NumPolygroupLayers();
		for (int LayerIdx = 0; LayerIdx < NumPolygroupLayers; ++LayerIdx)
		{
			const FDynamicMeshPolygroupAttribute* PolyAttr = OriginalMesh.Attributes()->GetPolygroupLayer(LayerIdx);
			// GetPolygroupLayer returns non-null for any LayerIdx < NumPolygroupLayers
			check(PolyAttr != nullptr);

			TArray<int32>& RefinedLayerPGIDs = RefinedPolygroupLayers.AddDefaulted_GetRef();

			TArray<int32> SourcePGIDs;
			CollectFaceUniformAttributeSource(PolyAttr, SourcePGIDs);

			SubdividePolyLocal::InterpolateFaceUniformData(Interpolator, *Refiner->TopologyRefiner, Level, SourcePGIDs, RefinedLayerPGIDs);
		}
	}

	// Now transfer to output mesh
	OutMesh.Clear();

	OutMesh.EnableTriangleGroups();

	// Enable attributes if we have any attribute data to write
	const bool bNeedAttributes = (NormalComputationMethod != ESubdivisionOutputNormals::None)
		|| (UVComputationMethod != ESubdivisionOutputUVs::None)
		|| bHasMaterialIDs
		|| bShouldInterpolateColors
		|| RefinedSkinWeightProfiles.Num() > 0
		|| RefinedMorphTargets.Num() > 0
		|| RefinedWeightLayers.Num() > 0
		|| RefinedPolygroupLayers.Num() > 0
		|| (bHasAttributes && OriginalMesh.Attributes()->HasBones());

	if (bNeedAttributes)
	{
		OutMesh.EnableAttributes();
	}

	// Add the vertices
	for (const SubdividePolyLocal::FSubdVec3d& V : RefinedPositions)
	{
		OutMesh.AppendVertex(V.Value);
	}

	const OpenSubdiv::Far::TopologyLevel& FinalLevel = Refiner->TopologyRefiner->GetLevel(Level);
	check(bNewPolyGroups || FinalLevel.GetNumFaces() == RefinedGroupIDs.Num());

	// Build a flat list of (FVarChannel, TriangleArray) pairs for all face-varying attributes.
	// This lets us collect triangle indices for all overlays in a single pass through the face loop.
	struct FFVarTriangleCollector
	{
		int FVarChannel;
		TArray<FIndex3i> Triangles;
	};
	TArray<FFVarTriangleCollector> FVarCollectors;

	TArray<int> UVLayerToCollector;
	UVLayerToCollector.SetNumUninitialized(NumUVLayers);
	for (int LayerIdx = 0; LayerIdx < NumUVLayers; ++LayerIdx)
	{
		int FVarCh = FVarMapping.UVLayerFVarChannels[LayerIdx];
		if (FVarCh >= 0)
		{
			UVLayerToCollector[LayerIdx] = FVarCollectors.Num();
			FVarCollectors.Add({ FVarCh, {} });
		}
		else
		{
			UVLayerToCollector[LayerIdx] = INDEX_NONE;
		}
	}

	int ColorCollectorIdx = INDEX_NONE;
	if (bShouldInterpolateColors)
	{
		ColorCollectorIdx = FVarCollectors.Num();
		FVarCollectors.Add({ FVarMapping.ColorFVarChannel, {} });
	}

	int NormalCollectorIdx = INDEX_NONE;
	if (bShouldInterpolateNormals)
	{
		NormalCollectorIdx = FVarCollectors.Num();
		FVarCollectors.Add({ FVarMapping.NormalFVarChannel, {} });
	}

	if (bHasMaterialIDs)
	{
		OutMesh.Attributes()->EnableMaterialID();
	}

	// Prepare polygroup layer attributes on the output mesh so they can be written per-face in the triangulation pass below.
	const int32 NumPolygroupLayerAttrs = RefinedPolygroupLayers.Num();
	if (NumPolygroupLayerAttrs > 0)
	{
		OutMesh.Attributes()->SetNumPolygroupLayers(NumPolygroupLayerAttrs);
	}

	// Triangulate and output the faces
	for (int FaceID = 0; FaceID < FinalLevel.GetNumFaces(); ++FaceID)
	{
		int GroupID = bNewPolyGroups ? OutMesh.AllocateTriangleGroup() : RefinedGroupIDs[FaceID];

		OpenSubdiv::Far::ConstIndexArray Face = FinalLevel.GetFaceVertices(FaceID);

		int TriAIndex = INDEX_NONE;
		int TriBIndex = INDEX_NONE;

		if (!ensure(Face.size() == 3 || Face.size() == 4))
		{
			continue;
		}
		TriAIndex = OutMesh.AppendTriangle(FIndex3i{ Face[0], Face[1], Face[2] }, GroupID);
		if (Face.size() == 4)
		{
			TriBIndex = OutMesh.AppendTriangle(FIndex3i{ Face[0], Face[2], Face[3] }, GroupID);
		}
		if (TriAIndex < 0 && TriBIndex < 0)
		{
			continue;
		}

		// Collect face-varying triangle indices for all overlay channels
		for (FFVarTriangleCollector& Collector : FVarCollectors)
		{
			OpenSubdiv::Far::ConstIndexArray FVarIndices = FinalLevel.GetFaceFVarValues(FaceID, Collector.FVarChannel);
			if (TriAIndex >= 0)
			{
				Collector.Triangles.Add(FIndex3i{ FVarIndices[0], FVarIndices[1], FVarIndices[2] });
			}
			if (TriBIndex >= 0)
			{
				Collector.Triangles.Add(FIndex3i{ FVarIndices[0], FVarIndices[2], FVarIndices[3] });
			}
		}

		if (bHasMaterialIDs)
		{
			if (TriAIndex >= 0)
			{
				OutMesh.Attributes()->GetMaterialID()->SetValue(TriAIndex, RefinedMaterialIDs[FaceID]);
			}
			if (TriBIndex >= 0)
			{
				OutMesh.Attributes()->GetMaterialID()->SetValue(TriBIndex, RefinedMaterialIDs[FaceID]);
			}
		}

		for (int32 LayerIdx = 0; LayerIdx < NumPolygroupLayerAttrs; ++LayerIdx)
		{
			FDynamicMeshPolygroupAttribute* PolyAttr = OutMesh.Attributes()->GetPolygroupLayer(LayerIdx);
			const int32 GroupValue = RefinedPolygroupLayers[LayerIdx][FaceID];
			if (TriAIndex >= 0)
			{
				PolyAttr->SetValue(TriAIndex, GroupValue);
			}
			if (TriBIndex >= 0)
			{
				PolyAttr->SetValue(TriBIndex, GroupValue);
			}
		}
	}

	// Helper: unwrap TSubdValue<T> array to raw T array, and mark invalid elements in the
	// triangle index array with InvalidID so InitializeOverlayFromRefinedData will call UnsetTriangle.
	auto UnwrapAndApplyValidity = []<typename SubdValueType, typename RawType>(
		const TArray<SubdValueType>& SubdArray, TArray<RawType>& OutArray, TArray<FIndex3i>& TriangleIndices)
	{
		OutArray.Reserve(SubdArray.Num());
		for (const SubdValueType& Elem : SubdArray)
		{
			OutArray.Add(Elem.Value);
		}

		for (FIndex3i& Tri : TriangleIndices)
		{
			for (int Idx = 0; Idx < 3; ++Idx)
			{
				if (Tri[Idx] != FDynamicMesh3::InvalidID && !SubdArray[Tri[Idx]].bIsValid)
				{
					Tri[Idx] = FDynamicMesh3::InvalidID;
				}
			}
		}
	};

	// Write attribute data to the output mesh
	if (OutMesh.HasAttributes())
	{
		// Initialize UV layers
		if (bShouldInterpolateUVs)
		{
			OutMesh.Attributes()->SetNumUVLayers(NumUVLayers);
			for (int LayerIdx = 0; LayerIdx < NumUVLayers; ++LayerIdx)
			{
				int CollectorIdx = UVLayerToCollector[LayerIdx];
				if (CollectorIdx < 0 || AllRefinedUVs[LayerIdx].Num() == 0)
				{
					continue;
				}

				TArray<FVector2f> UVElements;
				UnwrapAndApplyValidity(AllRefinedUVs[LayerIdx], UVElements, FVarCollectors[CollectorIdx].Triangles);

				SubdividePolyLocal::InitializeOverlayFromRefinedData(OutMesh.Attributes()->GetUVLayer(LayerIdx), FVarCollectors[CollectorIdx].Triangles, UVElements);
			}
		}

		// Initialize color overlay
		if (ColorCollectorIdx >= 0)
		{
			OutMesh.Attributes()->EnablePrimaryColors();

			TArray<FVector4f> ColorElements;
			UnwrapAndApplyValidity(RefinedColors, ColorElements, FVarCollectors[ColorCollectorIdx].Triangles);

			SubdividePolyLocal::InitializeOverlayFromRefinedData(OutMesh.Attributes()->PrimaryColors(), FVarCollectors[ColorCollectorIdx].Triangles, ColorElements);
		}

		// Initialize primary normal overlay.
		if (NormalCollectorIdx >= 0) // A valid NormalCollectorIdx implies NormalComputationMethod == Interpolated
		{
			TArray<FVector3f> NormalElements;
			UnwrapAndApplyValidity(RefinedNormals, NormalElements, FVarCollectors[NormalCollectorIdx].Triangles);

			SubdividePolyLocal::InitializeOverlayFromRefinedData(OutMesh.Attributes()->PrimaryNormals(), FVarCollectors[NormalCollectorIdx].Triangles, NormalElements);
		}
		else if (NormalComputationMethod == ESubdivisionOutputNormals::Generated)
		{
			FMeshNormals::InitializeOverlayToPerVertexNormals(OutMesh.Attributes()->PrimaryNormals(), /*bUseMeshVertexNormalsIfAvailable=*/ false);
		}

		// Write skin weight profiles
		for (const FSkinWeightProfile& Profile : RefinedSkinWeightProfiles)
		{
			if (Profile.BoneIndices.Num() == 0 || Profile.RefinedPerBoneWeights.Num() == 0)
			{
				continue;
			}

			FDynamicMeshVertexSkinWeightsAttribute* NewSkinAttr = new FDynamicMeshVertexSkinWeightsAttribute(&OutMesh);
			NewSkinAttr->Initialize();

			const int32 NumBoneChannels = Profile.BoneIndices.Num();
			const int32 NumOutputVerts = Profile.RefinedPerBoneWeights[0].Num();

			for (int32 VID = 0; VID < NumOutputVerts; ++VID)
			{
				// Gather non-zero bone weights for this vertex and reconstruct FBoneWeights
				TArray<FBoneIndexType, TInlineAllocator<UE::AnimationCore::MaxInlineBoneWeightCount>> BoneIndicesOut;
				TArray<float, TInlineAllocator<UE::AnimationCore::MaxInlineBoneWeightCount>> BoneWeightsOut;
				for (int32 Ch = 0; Ch < NumBoneChannels; ++Ch)
				{
					float W = Profile.RefinedPerBoneWeights[Ch][VID].Value;
					if (W > UE_SMALL_NUMBER)
					{
						BoneIndicesOut.Add(static_cast<FBoneIndexType>(Profile.BoneIndices[Ch]));
						BoneWeightsOut.Add(W);
					}
				}

				if (BoneIndicesOut.Num() > 0)
				{
					UE::AnimationCore::FBoneWeights BW = UE::AnimationCore::FBoneWeights::Create(BoneIndicesOut.GetData(), BoneWeightsOut.GetData(), BoneIndicesOut.Num());
					NewSkinAttr->SetValue(VID, BW);
				}
			}

			OutMesh.Attributes()->AttachSkinWeightsAttribute(Profile.ProfileName, NewSkinAttr);
		}

		// Write morph targets
		for (const FMorphTargetData& MorphData : RefinedMorphTargets)
		{
			if (MorphData.RefinedDeltas.Num() == 0)
			{
				continue;
			}

			FDynamicMeshMorphTargetAttribute* NewMorphAttr = new FDynamicMeshMorphTargetAttribute(&OutMesh);
			NewMorphAttr->Initialize();

			for (int32 VID = 0; VID < MorphData.RefinedDeltas.Num(); ++VID)
			{
				const FVector3f& Delta = MorphData.RefinedDeltas[VID].Value;
				NewMorphAttr->SetValue(VID, Delta);
			}

			OutMesh.Attributes()->AttachMorphTargetAttribute(MorphData.MorphName, NewMorphAttr);
		}

		// Copy skeleton/bone data
		if (bHasAttributes && OriginalMesh.Attributes()->HasBones())
		{
			const int32 NumBones = OriginalMesh.Attributes()->GetNumBones();
			OutMesh.Attributes()->EnableBones(NumBones);

			auto CopyBoneAttr = [NumBones]<typename BoneAttrType>(const BoneAttrType* Src, BoneAttrType* Dst)
			{
				if (Src && Dst)
				{
					for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
					{
						Dst->SetValue(BoneIdx, Src->GetValue(BoneIdx));
					}
				}
			};
			CopyBoneAttr(OriginalMesh.Attributes()->GetBoneNames(), OutMesh.Attributes()->GetBoneNames());
			CopyBoneAttr(OriginalMesh.Attributes()->GetBoneParentIndices(), OutMesh.Attributes()->GetBoneParentIndices());
			CopyBoneAttr(OriginalMesh.Attributes()->GetBonePoses(), OutMesh.Attributes()->GetBonePoses());
			CopyBoneAttr(OriginalMesh.Attributes()->GetBoneColors(), OutMesh.Attributes()->GetBoneColors());
		}

		// Write weight layers
		if (RefinedWeightLayers.Num() > 0)
		{
			OutMesh.Attributes()->SetNumWeightLayers(RefinedWeightLayers.Num());

			for (int32 LayerIdx = 0; LayerIdx < RefinedWeightLayers.Num(); ++LayerIdx)
			{
				FDynamicMeshWeightAttribute* WeightAttr = OutMesh.Attributes()->GetWeightLayer(LayerIdx);
				const TArray<SubdividePolyLocal::FSubdFloat>& RefinedWeights = RefinedWeightLayers[LayerIdx];
				for (int32 VID = 0; VID < RefinedWeights.Num(); ++VID)
				{
					float W = RefinedWeights[VID].Value;
					WeightAttr->SetValue(VID, &W);
				}
			}
		}

	}

	// Remove any vertices that are not referenced by a face
	OutMesh.RemoveUnusedVertices();

#else	// HAVE_OPENSUBDIV

	OutMesh = OriginalMesh;

#endif	// HAVE_OPENSUBDIV


	return true;
}



FSubdividePoly::ETopologyCheckResult FSubdividePoly::ValidateTopology()
{
	if (GroupTopology.Groups.Num() == 0)
	{
		return ETopologyCheckResult::NoGroups;
	}

	for (const FGroupTopology::FGroup& Group : GroupTopology.Groups)
	{
		if (Group.Boundaries.Num() == 0)
		{
			return ETopologyCheckResult::UnboundedPolygroup;
		}

		if (Group.Boundaries.Num() > 1)
		{
			return ETopologyCheckResult::MultiBoundaryPolygroup;
		}

		for (const FGroupTopology::FGroupBoundary& Boundary : Group.Boundaries)
		{
			if (Boundary.GroupEdges.Num() < 3)
			{
				return ETopologyCheckResult::DegeneratePolygroup;
			}
		}
	}

	// May be necessary if we ever support this option:
	//if (BoundaryScheme == ESubdivisionBoundaryScheme::NoBoundaryFaces)
	//{
	//	// Verify that there is at least one non-boundary face.
	//}

	return ETopologyCheckResult::Ok;
}


