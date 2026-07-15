// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "BoneContainer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MemStack.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "AnimNode_RigLogic.h"
#include "DNA.h"
#include "DNAAsset.h"
#include "DNAAssetUserData.h"
#include "DNAReader.h"
#include "DNAUtils.h"
#include "SharedRigRuntimeContext.h"

namespace UE::RigLogic::Tests
{
	// FAnimInstanceProxy::Initialize(UAnimInstance*) is the only path that wires
	// SkeletalMeshComponent and Skeleton onto the proxy; RecalcRequiredBones is also
	// protected. A trivial subclass that re-exports both lets us bootstrap the proxy
	// without going through the full UAnimInstance::InitializeAnimation pipeline (which
	// depends on a registered component / World / state-machine arrays we do not need).
	struct FTestAnimInstanceProxy : public FAnimInstanceProxy
	{
		using FAnimInstanceProxy::FAnimInstanceProxy;
		using FAnimInstanceProxy::Initialize;
		using FAnimInstanceProxy::RecalcRequiredBones;
	};

	static FString GetTestDNAFilePath()
	{
		const FString PluginDir = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Animation"), TEXT("RigLogic"));
		return FPaths::Combine(PluginDir, TEXT("Content"), TEXT("Test"), TEXT("DNA"), TEXT("rl_unit_behavior_test.dna"));
	}

	// Build a transient skeleton whose joint hierarchy matches the joints reported by the
	// supplied DNA reader. Joint names are taken verbatim from the DNA so that
	// FDNAIndexMapping::MapJoints (RefSkeleton.FindBoneIndex by joint name) succeeds.
	// The DNA's self-parenting joint becomes the FReferenceSkeleton's INDEX_NONE root.
	static USkeleton* BuildSkeletonForDNA(const IDNAReader& DNAReader)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
		check(Skeleton != nullptr);
		FReferenceSkeletonModifier Modifier(Skeleton);

		const uint16 JointCount = DNAReader.GetJointCount();
		for (uint16 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
		{
			const FString JointName = DNAReader.GetJointName(JointIndex);
			const uint16 ParentIndex = DNAReader.GetJointParentIndex(JointIndex);

			int32 ParentBoneIndex = INDEX_NONE;
			if (ParentIndex != JointIndex)
			{
				const FString ParentName = DNAReader.GetJointName(ParentIndex);
				ParentBoneIndex = Modifier.FindBoneIndex(FName(*ParentName));
			}
			const FMeshBoneInfo Info(FName(*JointName), JointName, ParentBoneIndex);
			Modifier.Add(Info, FTransform::Identity);
		}
		return Skeleton;
	}

	// Build a transient SkeletalMesh that uses the supplied skeleton as its ref skeleton.
	// USkeletalMesh::RegisterMorphTarget ensureMsgf's on HasValidData() — true for cooked
	// morphs, but our synthetic UMorphTargets carry no morph LOD models. Sidestep that by
	// appending directly to the mesh's morph-target array, then call InitMorphTargets with
	// bInKeepEmptyMorphTargets=true to populate the index map.
	static USkeletalMesh* BuildSkeletalMeshForDNA(USkeleton* Skeleton, const IDNAReader& DNAReader)
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		check(Mesh != nullptr);
		Mesh->SetSkeleton(Skeleton);
		Mesh->SetRefSkeleton(Skeleton->GetReferenceSkeleton());

		const uint16 LODCount = DNAReader.GetLODCount();
		TSet<FName> RegisteredNames;
		TArray<UMorphTarget*> MorphTargetList = Mesh->GetMorphTargets();
		for (uint16 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
		{
			TArrayView<const uint16> Mappings = DNAReader.GetMeshBlendShapeChannelMappingIndicesForLOD(LODIndex);
			for (const uint16 MappingIndex : Mappings)
			{
				const FMeshBlendShapeChannelMapping Mapping = DNAReader.GetMeshBlendShapeChannelMapping(MappingIndex);
				const FString MeshName = DNAReader.GetMeshName(Mapping.MeshIndex);
				const FString BlendShapeName = DNAReader.GetBlendShapeChannelName(Mapping.BlendShapeChannelIndex);
				const FName MorphTargetName(*(MeshName + TEXT("__") + BlendShapeName));

				if (!RegisteredNames.Contains(MorphTargetName))
				{
					UMorphTarget* MorphTarget = NewObject<UMorphTarget>(Mesh, MorphTargetName, RF_Transient);
					MorphTargetList.Add(MorphTarget);
					RegisteredNames.Add(MorphTargetName);
				}
			}
		}
		Mesh->SetMorphTargets(MorphTargetList);
		Mesh->InitMorphTargets(/*bInKeepEmptyMorphTargets=*/true);
		return Mesh;
	}

	static UDNA* AttachDNAToMesh(USkeletalMesh* Mesh, const FString& DNAFilePath)
	{
		// ReadDNAAssetFromFile returns a UDNA outered to Mesh; LoadDNAFromFile returns
		// only the IDNAReader (which we don't need here -- UDNA wraps its own reader).
		UDNA* DNA = ReadDNAAssetFromFile(DNAFilePath, Mesh);
		if (DNA)
		{
			UDNAAssetUserData* UserData = NewObject<UDNAAssetUserData>(Mesh, NAME_None, RF_Transient);
			UserData->DNAAsset = DNA;
			Mesh->AddAssetUserData(UserData);
		}
		return DNA;
	}

	static UDNAAsset* AttachLegacyDNAToMesh(USkeletalMesh* Mesh, const FString& DNAFilePath)
	{
		UDNAAsset* DNAAsset = NewObject<UDNAAsset>(Mesh, NAME_None, RF_Transient);
		if (DNAAsset && DNAAsset->Init(DNAFilePath))
		{
			Mesh->AddAssetUserData(Cast<UAssetUserData>(DNAAsset));
		}
		return DNAAsset;
	}

	static USkeletalMeshComponent* BuildSkeletalMeshComponent(USkeletalMesh* Mesh)
	{
		USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		check(Component != nullptr);

		// SetSkinnedAssetAndUpdate is the only public path that pairs the asset write
		// with NotifyIfSkinnedAssetChanged() (which is protected). Its registered-component
		// branches (AllocateTransformData, UpdateLODStatus, mesh deformer recreation) are
		// gated on IsRegistered() / IsPreRegistering(), so on our unregistered transient
		// component they no-op. We deliberately avoid SetSkeletalMesh because it would
		// also call InitAnim / RecreatePhysicsState which DO need a registered component.
		Component->SetSkinnedAssetAndUpdate(Mesh, /*bReinitPose=*/true);

		const int32 NumBones = Mesh->GetRefSkeleton().GetNum();
		Component->RequiredBones.Reset(NumBones);
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			Component->RequiredBones.Add(static_cast<FBoneIndexType>(BoneIndex));
		}
		Component->RequiredBones.Sort();
		return Component;
	}

	static UAnimInstance* BuildAnimInstance(USkeletalMeshComponent* Component)
	{
		// UAnimInstance is UCLASS(Within=SkeletalMeshComponent), so the component MUST be
		// the outer for GetSkelMeshComponent() to resolve correctly.
		UAnimInstance* AnimInstance = NewObject<UAnimInstance>(Component, UAnimInstance::StaticClass(), NAME_None, RF_Transient);
		check(AnimInstance != nullptr);
		return AnimInstance;
	}

	// A non-identity, non-ref-pose sentinel transform we stamp onto every compact-pose
	// bone before Evaluate. Any bone the AnimNode wrote to will not equal this value
	// afterwards. The test DNA's neutral joint values happen to be identity, which makes
	// "this bone moved" indistinguishable from "this bone was never written" if the start
	// state is identity. A sentinel start is unambiguous.
	static FTransform GetSentinelTransform()
	{
		return FTransform(
			FQuat(0.4f, 0.3f, 0.2f, 0.1f).GetNormalized(),
			FVector(1234.5f, -987.65f, 4242.0f),
			FVector(2.5f, 3.5f, 4.5f));
	}

	// FPoseContext doesn't survive copy/move cleanly -- its copy ctor calls
	// Pose.SetBoneContainer which AddUninitialized's the bones array, wiping any state we
	// set. Tests must construct the FPoseContext locally and call SeedSentinelPose.
	static void SeedSentinelPose(FPoseContext& OutputContext)
	{
		OutputContext.ResetToRefPose();
		const FTransform Sentinel = GetSentinelTransform();
		for (FCompactPoseBoneIndex CompactIndex : OutputContext.Pose.ForEachBoneIndex())
		{
			OutputContext.Pose[CompactIndex] = Sentinel;
		}
	}

	static TMap<FName, float> SnapshotCurves(const FPoseContext& OutputContext)
	{
		TMap<FName, float> Result;
		OutputContext.Curve.ForEachElement([&Result](const UE::Anim::FCurveElement& InElement)
		{
			Result.Add(InElement.Name, InElement.Value);
		});
		return Result;
	}

	static void SetRawControlCurve(FPoseContext& InputContext, FName CurveName, float Value)
	{
		InputContext.Curve.Set(CurveName, Value);
	}

	// Holds every pinned UObject for a single test run. Test bodies construct one of
	// these, drive the AnimNode through it, then let it go out of scope.
	struct FAnimNodeFixture
	{
		FMemMark MemMark;

		TStrongObjectPtr<USkeleton> Skeleton;
		TStrongObjectPtr<USkeletalMesh> Mesh;
		TStrongObjectPtr<USkeletalMeshComponent> Component;
		TStrongObjectPtr<UAnimInstance> AnimInstance;
		TStrongObjectPtr<UDNA> DNA;

		TSharedPtr<IDNAReader> DNAReader;
		TUniquePtr<FTestAnimInstanceProxy> Proxy;

		TArray<int32> DNAJointToBoneIndex;
		TArray<FName> ExpectedMorphCurveNames;
		TArray<FName> ExpectedAnimMapCurveNames;
		TArray<FName> RawControlNames;

		FAnimNodeFixture() : MemMark(FMemStack::Get())
		{
		}

		// Returns false if any step of the bootstrap fails; the caller should AddError and
		// return false from RunTest. Splitting construction from Setup keeps the destructor
		// safe even when the bootstrap fails partway through.
		bool Setup(FAutomationTestBase& Test, bool bUseLegacyDNA)
		{
			const FString DNAFilePath = GetTestDNAFilePath();
			if (!FPaths::FileExists(DNAFilePath))
			{
				Test.AddError(FString::Printf(TEXT("Test DNA not found at %s"), *DNAFilePath));
				return false;
			}

			DNAReader = LoadDNAFromFile(DNAFilePath);
			if (!DNAReader.IsValid())
			{
				Test.AddError(FString::Printf(TEXT("LoadDNAFromFile returned an invalid reader for %s"), *DNAFilePath));
				return false;
			}

			Skeleton.Reset(BuildSkeletonForDNA(*DNAReader));
			Mesh.Reset(BuildSkeletalMeshForDNA(Skeleton.Get(), *DNAReader));

			if (bUseLegacyDNA)
			{
				if (AttachLegacyDNAToMesh(Mesh.Get(), DNAFilePath) == nullptr)
				{
					Test.AddError(FString::Printf(TEXT("Failed to attach legacy UDNAAsset to mesh from %s"), *DNAFilePath));
					return false;
				}
			}
			else
			{
				DNA.Reset(AttachDNAToMesh(Mesh.Get(), DNAFilePath));
				if (!DNA.IsValid())
				{
					Test.AddError(FString::Printf(TEXT("Failed to attach UDNA to mesh from %s"), *DNAFilePath));
					return false;
				}
			}

			Component.Reset(BuildSkeletalMeshComponent(Mesh.Get()));
			AnimInstance.Reset(BuildAnimInstance(Component.Get()));

			Proxy = MakeUnique<FTestAnimInstanceProxy>();
			Proxy->Initialize(AnimInstance.Get());
			Proxy->RecalcRequiredBones(Component.Get(), Mesh.Get());

			if (!Proxy->GetRequiredBones().IsValid())
			{
				Test.AddError(TEXT("Proxy required-bones container is invalid"));
				return false;
			}

			CacheDNAJointMapping();
			CacheExpectedCurveNames();
			CacheRawControlNames();
			return true;
		}

		void CacheDNAJointMapping()
		{
			const uint16 JointCount = DNAReader->GetJointCount();
			DNAJointToBoneIndex.Reset(JointCount);
			DNAJointToBoneIndex.AddUninitialized(JointCount);

			const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
			for (uint16 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
			{
				const FName JointName(*DNAReader->GetJointName(JointIndex));
				DNAJointToBoneIndex[JointIndex] = RefSkeleton.FindBoneIndex(JointName);
			}
		}

		void CacheExpectedCurveNames()
		{
			const uint16 LODIndex = 0;
			ExpectedMorphCurveNames.Reset();
			TArrayView<const uint16> Mappings = DNAReader->GetMeshBlendShapeChannelMappingIndicesForLOD(LODIndex);
			for (const uint16 MappingIndex : Mappings)
			{
				const FMeshBlendShapeChannelMapping Mapping = DNAReader->GetMeshBlendShapeChannelMapping(MappingIndex);
				const FString MeshName = DNAReader->GetMeshName(Mapping.MeshIndex);
				const FString BlendShapeName = DNAReader->GetBlendShapeChannelName(Mapping.BlendShapeChannelIndex);
				ExpectedMorphCurveNames.Add(FName(*(MeshName + TEXT("__") + BlendShapeName)));
			}

			ExpectedAnimMapCurveNames.Reset();
			TArrayView<const uint16> AnimMapIndices = DNAReader->GetAnimatedMapIndicesForLOD(LODIndex);
			for (const uint16 AnimMapIndex : AnimMapIndices)
			{
				const FString AnimMapName = DNAReader->GetAnimatedMapName(AnimMapIndex);
				FString ObjectName, AttrName;
				if (AnimMapName.Split(TEXT("."), &ObjectName, &AttrName))
				{
					ExpectedAnimMapCurveNames.Add(FName(*(ObjectName + TEXT("_") + AttrName)));
				}
			}
		}

		void CacheRawControlNames()
		{
			const uint16 ControlCount = DNAReader->GetRawControlCount();
			RawControlNames.Reset(ControlCount);
			for (uint16 ControlIndex = 0; ControlIndex < ControlCount; ++ControlIndex)
			{
				const FString ControlName = DNAReader->GetRawControlName(ControlIndex);
				FString ObjectName, AttrName;
				if (ControlName.Split(TEXT("."), &ObjectName, &AttrName))
				{
					RawControlNames.Add(FName(*(ObjectName + TEXT("_") + AttrName)));
				}
				else
				{
					RawControlNames.Add(FName(*ControlName));
				}
			}
		}

		void InitializeAndCacheBones(FAnimNode_RigLogic& Node)
		{
			FAnimationInitializeContext InitContext(Proxy.Get());
			Node.Initialize_AnyThread(InitContext);
			FAnimationCacheBonesContext CacheContext(Proxy.Get());
			Node.CacheBones_AnyThread(CacheContext);
		}

		bool DidJointMove(const FPoseContext& OutputContext, uint16 DNAJointIndex, float Tolerance = KINDA_SMALL_NUMBER) const
		{
			if (!DNAJointToBoneIndex.IsValidIndex(DNAJointIndex))
			{
				return false;
			}

			const int32 MeshBoneIndex = DNAJointToBoneIndex[DNAJointIndex];
			if (MeshBoneIndex == INDEX_NONE)
			{
				return false;
			}

			FBoneContainer& Bones = const_cast<FBoneContainer&>(OutputContext.Pose.GetBoneContainer());
			const FCompactPoseBoneIndex CompactIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
			if (CompactIndex.GetInt() == INDEX_NONE)
			{
				return false;
			}

			const FTransform Posed = OutputContext.Pose[CompactIndex];
			return !Posed.Equals(GetSentinelTransform(), Tolerance);
		}

		TArray<uint16> CollectMovedJoints(const FPoseContext& OutputContext, float Tolerance = KINDA_SMALL_NUMBER) const
		{
			TArray<uint16> Moved;
			for (uint16 JointIndex = 0; JointIndex < static_cast<uint16>(DNAJointToBoneIndex.Num()); ++JointIndex)
			{
				if (DidJointMove(OutputContext, JointIndex, Tolerance))
				{
					Moved.Add(JointIndex);
				}
			}
			return Moved;
		}
	};

	// Seeds every raw control on the input pose to 0. Tests should call this before
	// any per-control overrides so the input curve set stays stable across Evaluate
	// calls -- the cached path's index cache is keyed on InputContext.Curve.Num(), and
	// also on the slot order via FBlendedCurve's name-sorted iteration. Setting all
	// controls every frame is the contract the cached path documents.
	static void SeedAllRawControlsZero(FPoseContext& InputContext, const FAnimNodeFixture& Fixture)
	{
		for (const FName& Name : Fixture.RawControlNames)
		{
			InputContext.Curve.Set(Name, 0.0f);
		}
	}

	// Hand-captured fixture joint observations for the three input scenarios this suite targets.
	// Values were frozen by inspecting actual AnimNode output.
	struct FExpectedJoint
	{
		uint16 JointIndex;
		FVector Translation;
		FQuat Rotation;
		FVector Scale;
	};

	static void AssertJoint(FAutomationTestBase& Test, const FPoseContext& Output, const FAnimNodeFixture& Fixture, const FExpectedJoint& Expected, const TCHAR* ScenarioTag, float Tolerance = 1.e-3f)
	{
		const int32 MeshBoneIndex = Fixture.DNAJointToBoneIndex[Expected.JointIndex];
		if (MeshBoneIndex == INDEX_NONE)
		{
			Test.AddError(FString::Printf(TEXT("%s J%u: not in ref skeleton"), ScenarioTag, Expected.JointIndex));
			return;
		}

		FBoneContainer& Bones = const_cast<FBoneContainer&>(Output.Pose.GetBoneContainer());
		const FCompactPoseBoneIndex CompactIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
		if (CompactIndex.GetInt() == INDEX_NONE)
		{
			Test.AddError(FString::Printf(TEXT("%s J%u: not in compact pose"), ScenarioTag, Expected.JointIndex));
			return;
		}

		const FTransform Posed = Output.Pose[CompactIndex];
		const FVector T = Posed.GetTranslation();
		const FQuat   R = Posed.GetRotation();
		const float QuatLength = static_cast<float>(R.Size());
		Test.TestEqual(*FString::Printf(TEXT("%s J%u rotation must be a unit quaternion"), ScenarioTag, Expected.JointIndex), QuatLength, 1.0f, 1.e-3f);
		const FVector S = Posed.GetScale3D();

		Test.TestEqual(*FString::Printf(TEXT("%s J%u TX"), ScenarioTag, Expected.JointIndex), static_cast<float>(T.X), static_cast<float>(Expected.Translation.X), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u TY"), ScenarioTag, Expected.JointIndex), static_cast<float>(T.Y), static_cast<float>(Expected.Translation.Y), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u TZ"), ScenarioTag, Expected.JointIndex), static_cast<float>(T.Z), static_cast<float>(Expected.Translation.Z), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u RX"), ScenarioTag, Expected.JointIndex), static_cast<float>(R.X), static_cast<float>(Expected.Rotation.X), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u RY"), ScenarioTag, Expected.JointIndex), static_cast<float>(R.Y), static_cast<float>(Expected.Rotation.Y), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u RZ"), ScenarioTag, Expected.JointIndex), static_cast<float>(R.Z), static_cast<float>(Expected.Rotation.Z), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u RW"), ScenarioTag, Expected.JointIndex), static_cast<float>(R.W), static_cast<float>(Expected.Rotation.W), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u SX"), ScenarioTag, Expected.JointIndex), static_cast<float>(S.X), static_cast<float>(Expected.Scale.X), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u SY"), ScenarioTag, Expected.JointIndex), static_cast<float>(S.Y), static_cast<float>(Expected.Scale.Y), Tolerance);
		Test.TestEqual(*FString::Printf(TEXT("%s J%u SZ"), ScenarioTag, Expected.JointIndex), static_cast<float>(S.Z), static_cast<float>(Expected.Scale.Z), Tolerance);
	}

}

// EditorContext + EngineFilter lets these run from the editor's Session Frontend -> Automation panel as well as from `RunTests Animation.RigLogic`.
namespace
{
	constexpr EAutomationTestFlags GTestFlags =	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

// =============================================================================
// CacheBones - DNA resolution paths
// =============================================================================

namespace UE::RigLogic::Tests
{
	// Shared body for the two CacheBones tests below. Both DNA-attachment paths
	// (modern UDNAAssetUserData and legacy UDNAAsset) must produce identical
	// observable output: at least one DNA joint written, plus every morph curve
	// the DNA declares at LOD 0 present on the output pose.
	static void RunCacheBonesResolutionTest(FAutomationTestBase& Test, bool bUseLegacyDNA)
	{
		FAnimNodeFixture Fixture;
		if (!Fixture.Setup(Test, bUseLegacyDNA))
		{
			return;
		}

		FAnimNode_RigLogic Node;
		Fixture.InitializeAndCacheBones(Node);

		FPoseContext Output(Fixture.Proxy.Get());
		SeedSentinelPose(Output);
		SeedAllRawControlsZero(Output, Fixture);
		Node.Evaluate_AnyThread(Output);

		// At least one DNA joint should be written; if none are, CacheBones never resolved
		// LocalRigRuntimeContext and Evaluate early-returned.
		Test.TestFalse(TEXT("Some DNA joints should be written by Evaluate"), Fixture.CollectMovedJoints(Output).IsEmpty());

		// Every morph curve declared by the DNA at LOD0 should be present on the output.
		const TMap<FName, float> Curves = SnapshotCurves(Output);
		for (const FName& Expected : Fixture.ExpectedMorphCurveNames)
		{
			Test.TestTrue(*FString::Printf(TEXT("Morph curve missing: %s"), *Expected.ToString()), Curves.Contains(Expected));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_CacheBones_ResolvesDNAFromAssetUserData, "RigLogic.RigLogicModule.AnimNode_RigLogic.CacheBones.ResolvesDNAFromAssetUserData", GTestFlags)
bool FAnimNode_RigLogic_CacheBones_ResolvesDNAFromAssetUserData::RunTest(const FString&)
{
	UE::RigLogic::Tests::RunCacheBonesResolutionTest(*this, /*bUseLegacyDNA=*/false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_CacheBones_ResolvesDNAFromLegacyAssetUserData, "RigLogic.RigLogicModule.AnimNode_RigLogic.CacheBones.ResolvesDNAFromLegacyAssetUserData", GTestFlags)
bool FAnimNode_RigLogic_CacheBones_ResolvesDNAFromLegacyAssetUserData::RunTest(const FString&)
{
	UE::RigLogic::Tests::RunCacheBonesResolutionTest(*this, /*bUseLegacyDNA=*/true);
	return true;
}

// =============================================================================
// Evaluate - joint transforms written
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Evaluate_WritesNoNaN, "RigLogic.RigLogicModule.AnimNode_RigLogic.Evaluate.WritesNoNaN", GTestFlags)
bool FAnimNode_RigLogic_Evaluate_WritesNoNaN::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	for (const FName& ControlName : Fixture.RawControlNames)
	{
		SetRawControlCurve(Output, ControlName, 1.0e6f);
	}
	Node.Evaluate_AnyThread(Output);

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (FCompactPoseBoneIndex CompactIndex : Output.Pose.ForEachBoneIndex())
	{
		const FTransform& T = Output.Pose[CompactIndex];
		if (T.ContainsNaN())
		{
			const FMeshPoseBoneIndex MeshIndex = const_cast<FBoneContainer&>(Bones).MakeMeshPoseIndex(CompactIndex);
			AddError(FString::Printf(TEXT("Bone transform contains NaN: %s"), *Bones.GetReferenceSkeleton().GetBoneName(MeshIndex.GetInt()).ToString()));
			return false;
		}
	}

	bool bCurveNaN = false;
	Output.Curve.ForEachElement([&bCurveNaN](const UE::Anim::FCurveElement& InElement)
	{
		if (FMath::IsNaN(InElement.Value))
		{
			bCurveNaN = true;
		}
	});
	TestFalse(TEXT("No output curve should be NaN"), bCurveNaN);
	return true;
}

// =============================================================================
// Cached vs uncached curve lookup
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Evaluate_CachedAndUncachedAgree, "RigLogic.RigLogicModule.AnimNode_RigLogic.Evaluate.CachedAndUncachedAgree", GTestFlags)
bool FAnimNode_RigLogic_Evaluate_CachedAndUncachedAgree::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Uncached;
	Uncached.CacheAnimCurveNames = false;
	Fixture.InitializeAndCacheBones(Uncached);

	FAnimNode_RigLogic Cached;
	Cached.CacheAnimCurveNames = true;
	Fixture.InitializeAndCacheBones(Cached);

	auto SeedInputs = [&Fixture](FPoseContext& Ctx)
	{
		float Value = 0.1f;
		for (const FName& ControlName : Fixture.RawControlNames)
		{
			SetRawControlCurve(Ctx, ControlName, Value);
			Value = FMath::Min(Value + 0.05f, 1.0f);
		}
	};

	FPoseContext UncachedOutput(Fixture.Proxy.Get());
	SeedSentinelPose(UncachedOutput);
	SeedAllRawControlsZero(UncachedOutput, Fixture);
	SeedInputs(UncachedOutput);
	Uncached.Evaluate_AnyThread(UncachedOutput);

	// UpdateRawControlsCached lazy-builds the per-LOD index cache on first use, so a
	// single Evaluate call exercises the cache build + lookup paths in one go.
	FPoseContext CachedOutput(Fixture.Proxy.Get());
	SeedSentinelPose(CachedOutput);
	SeedAllRawControlsZero(CachedOutput, Fixture);
	SeedInputs(CachedOutput);
	Cached.Evaluate_AnyThread(CachedOutput);

	for (FCompactPoseBoneIndex CompactIndex : UncachedOutput.Pose.ForEachBoneIndex())
	{
		const FTransform& TUncached = UncachedOutput.Pose[CompactIndex];
		const FTransform& TCached = CachedOutput.Pose[CompactIndex];
		TestTrue(TEXT("Cached path produced a different bone transform"), TUncached.Equals(TCached, 1.e-4f));
	}

	const TMap<FName, float> UncachedCurves = SnapshotCurves(UncachedOutput);
	const TMap<FName, float> CachedCurves = SnapshotCurves(CachedOutput);
	TestEqual(TEXT("Cached and uncached paths emitted a different number of curves"), UncachedCurves.Num(), CachedCurves.Num());
	for (const TPair<FName, float>& Pair : UncachedCurves)
	{
		const float* Other = CachedCurves.Find(Pair.Key);
		if (Other == nullptr)
		{
			AddError(FString::Printf(TEXT("Curve %s present in uncached output but missing from cached"), *Pair.Key.ToString()));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Curve %s value differs between cached and uncached"), *Pair.Key.ToString()), Pair.Value, *Other, 1.e-4f);
	}
	return true;
}

// =============================================================================
// Fixtures values - captured from rl_unit_behavior_test.dna
//
// All numeric expectations below were captured by running RigLogic against the
// test DNA and recording the actual outputs. They capture RigLogic's behaviour
// at zero input, at a single non-zero input and at multiple non-zero inputs.
// All these test scenarios exercise specific conditional branches. If the DNA
// file changes, these fixture values must be re-captured and updated.
// =============================================================================

namespace UE::RigLogic::Tests::Fixtures
{
	// Scenario: all raw controls = 0. RigLogic writes each variable joint's (converted)
	// neutral pose. Translation matches the DNA's neutralJointTranslations after the
	// X/Y flip; rotation is the DNA's neutral Euler composed through the coord-system
	// pipeline; scale is identity (DNA has no neutralJointScales array).
	static const FExpectedJoint ExpectedJointsAtZero[] =
	{
		{ 0, FVector(-1.0000000f, -1.0000000f, 1.0000000f), FQuat(-0.1675188f, -0.5709414f, 0.1675187f, 0.7860665f), FVector(1.0f, 1.0f, 1.0f) },
		{ 2, FVector(-3.0000000f, -3.0000000f, 3.0000000f), FQuat(0.0653920f, -0.0753745f, -0.0653920f, 0.9928575f), FVector(1.0f, 1.0f, 1.0f) },
		{ 4, FVector(-5.0000000f, -5.0000000f, 5.0000000f), FQuat(0.6710627f, 0.0971734f, -0.6710627f, 0.2998449f), FVector(1.0f, 1.0f, 1.0f) },
		{ 5, FVector(-6.0000000f, -6.0000000f, 6.0000000f), FQuat(0.1580251f, 0.1185940f, -0.1580252f, 0.9674665f), FVector(1.0f, 1.0f, 1.0f) },
		{ 6, FVector(-7.0000000f, -7.0000000f, 7.0000000f), FQuat(-0.1923898f, -0.4228496f, 0.1923897f, 0.8643902f), FVector(1.0f, 1.0f, 1.0f) },
		{ 7, FVector(-8.0000000f, -8.0000000f, 8.0000000f), FQuat(0.0510307f, -0.6977183f, -0.0510307f, 0.7127279f), FVector(1.0f, 1.0f, 1.0f) },
		{ 1, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
		{ 3, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
		{ 8, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
	};

	// At zero input, the animated-map conditional table contributes only those
	// entries whose (from <= 0 <= to) check passes. Walking the table for input=0:
	//   entry 0 (in 0 -> out 0): contributes 0*1 + 0   = 0
	//   entry 1 (in 1 -> out 1): contributes 0*0.9 + 0.5 = 0.5
	//   entry 6 (in 4 -> out 4): contributes 0*0.6 + 1   = 1
	//   entry 10 (in 6 -> out 6): contributes 0*0.6 + 0.4 = 0.4
	//   entry 14 (in 8 -> out 8): contributes 0*0.9 + 0.2 = 0.2
	// All other rows fail the from-bounds check and contribute nothing.
	// Animated-map names are A.A..A.J, mapped to curves A_A..A_J in the output.
	static const TPair<const TCHAR*, float> ExpectedAnimMapCurvesAtZero[] =
	{
		{ TEXT("A_A"), 0.0f },
		{ TEXT("A_B"), 0.5f },
		{ TEXT("A_C"), 0.0f },
		{ TEXT("A_D"), 0.0f },
		{ TEXT("A_E"), 1.0f },
		{ TEXT("A_F"), 0.0f },
		{ TEXT("A_G"), 0.4f },
		{ TEXT("A_H"), 0.0f },
		{ TEXT("A_I"), 0.2f },
		{ TEXT("A_J"), 0.0f },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Fixtures_AnimMapCurvesAtZero, "RigLogic.RigLogicModule.AnimNode_RigLogic.Fixtures.AnimMapCurvesAtZero", GTestFlags)
bool FAnimNode_RigLogic_Fixtures_AnimMapCurvesAtZero::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	// No raw controls set -> all inputs are zero.
	Node.Evaluate_AnyThread(Output);

	const TMap<FName, float> Curves = SnapshotCurves(Output);
	for (const TPair<const TCHAR*, float>& Expected : Fixtures::ExpectedAnimMapCurvesAtZero)
	{
		const FName Name(Expected.Key);
		const float* ActualPtr = Curves.Find(Name);
		if (ActualPtr == nullptr)
		{
			AddError(FString::Printf(TEXT("Animated-map curve missing: %s"), Expected.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Animated-map curve %s value"), Expected.Key), *ActualPtr, Expected.Value, 1.e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Fixtures_BlendShapeCurvesAtZero, "RigLogic.RigLogicModule.AnimNode_RigLogic.Fixtures.BlendShapeCurvesAtZero", GTestFlags)
bool FAnimNode_RigLogic_Fixtures_BlendShapeCurvesAtZero::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	// At zero input, every blend-shape output is zero. Blend-shape outputs are
	// a direct selection from raw-control + PSD inputs (no constant offset),
	// so all-zero input -> all-zero output. Curve names in the output pose are
	// <mesh>__<blendshape> per FDNAIndexMapping::MapMorphTargets.
	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	Node.Evaluate_AnyThread(Output);

	const TMap<FName, float> Curves = SnapshotCurves(Output);
	for (const FName& Name : Fixture.ExpectedMorphCurveNames)
	{
		const float* ActualPtr = Curves.Find(Name);
		if (ActualPtr == nullptr)
		{
			AddError(FString::Printf(TEXT("Morph curve missing: %s"), *Name.ToString()));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Morph curve %s should be zero at zero input"), *Name.ToString()), *ActualPtr, 0.0f, 1.e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Fixtures_JointsAtZero, "RigLogic.RigLogicModule.AnimNode_RigLogic.Fixtures.JointsAtZero", GTestFlags)
bool FAnimNode_RigLogic_Fixtures_JointsAtZero::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	// At zero input, every joint group's delta is zero, so each variable joint's
	// final transform equals its DNA-declared neutral. The expected values are
	// captured in ExpectedJointsAtZero (translation after the X/Y coord flip,
	// rotation as the DNA's neutral Euler composed through RigLogic's pipeline,
	// and identity scale).
	//
	// Test DNA's joint groups touch joints {0, 2, 4, 5, 6, 7} at LOD 0; joints
	// {1, 3, 8} are not written by UpdateJoints, so they retain whatever the input
	// pose holds -- in this test, AnimSequence.Evaluate resets the pose to identity
	// before RigLogic runs, so those non-variable joints stay at identity in the
	// expected fixture.
	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	Node.Evaluate_AnyThread(Output);

	for (const FExpectedJoint& Expected : Fixtures::ExpectedJointsAtZero)
	{
		AssertJoint(*this, Output, Fixture, Expected, TEXT("Zero"));
	}
	return true;
}

// =============================================================================
// Fixtures values - non-zero input
//
// Drives raw control R.B (index 1) = 1.0 and asserts exact deltas relative to
// the neutral evaluation. All values derived from the DNA's JSON form. Any
// change to the test DNA invalidates these and they must be re-derived.
//
// Why R.B and not R.A?
//   PSDs are products of weighted raw inputs (PSD[row] = product over its
//   non-zero (row,col,value) of raw[col] * value). Single-control inputs other
//   than R.A drive PSDs to zero whenever any of their cols is zero, so for an
//   isolated R.B=1 input the entire post-PSD vector is just raw[1]=1 with all
//   PSDs zero. R.B happens to have a non-zero TRANSLATION coefficient in joint
//   group 0 (col 1, value 0.05 at output index 2 = TZ), giving a clean exact
//   translation delta to assert against. R.A's col-0 translation entry is 0,
//   which made it a poor choice.
//
// At R.B=1, post-PSD control = [0,1,0,0,0,0,0,0,0,0,0,0,...0] (PSD9..20 all zero).
//
// Joint group 0 (joint 0, 3 outputs x 7 inputs, row-major):
//   Inputs [0,1,2,3,6,7,8] -> col 1 active
//   M col 1 = [values[1], values[8], values[15]] = [0.05, 0.4, 0.75]
//   Outputs [2, 3, 5] -> joint 0 attrs 2 (TZ_DNA), 3 (RotEulX), 5 (RotEulZ)
//     joint 0 ATZ_DNA  += 0.05  -> UE Z += 0.05 (Z preserved)
//     joint 0 ARotEulX += 0.4
//     joint 0 ARotEulZ += 0.75
//
// Joint groups 1, 2, 3 do not include col 1 in their input lists, so they
// contribute nothing.
//
// Observable negations and swaps in the expected values are due to coordinate
// system conversion (DNA -> UE).
// =============================================================================

namespace UE::RigLogic::Tests::Fixtures
{
	// Scenario: R.B (raw control 1) = 1.0, all others 0. Only joint 0 gets a non-zero
	// delta (group 0 col 1: TZ_DNA +0.05, RotEulX +0.4 deg, RotEulZ +0.75 deg). All
	// other variable joints retain their neutral pose.
	static const FExpectedJoint ExpectedJointsAtRB1[] =
	{
		{ 0, FVector(-1.0000000f, -1.0000000f, 1.0500000f), FQuat(-0.4908471f, -0.5487088f, 0.3415895f, 0.5842123f), FVector(1.0f, 1.0f, 1.0f) },
		{ 2, FVector(-3.0000000f, -3.0000000f, 3.0000000f), FQuat(0.0653920f, -0.0753745f, -0.0653920f, 0.9928575f), FVector(1.0f, 1.0f, 1.0f) },
		{ 4, FVector(-5.0000000f, -5.0000000f, 5.0000000f), FQuat(0.6710627f, 0.0971734f, -0.6710627f, 0.2998449f), FVector(1.0f, 1.0f, 1.0f) },
		{ 5, FVector(-6.0000000f, -6.0000000f, 6.0000000f), FQuat(0.1580251f, 0.1185940f, -0.1580252f, 0.9674665f), FVector(1.0f, 1.0f, 1.0f) },
		{ 6, FVector(-7.0000000f, -7.0000000f, 7.0000000f), FQuat(-0.1923898f, -0.4228496f, 0.1923897f, 0.8643902f), FVector(1.0f, 1.0f, 1.0f) },
		{ 7, FVector(-8.0000000f, -8.0000000f, 8.0000000f), FQuat(0.0510307f, -0.6977183f, -0.0510307f, 0.7127279f), FVector(1.0f, 1.0f, 1.0f) },
		{ 1, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
		{ 3, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
		{ 8, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
	};

	// Blend-shape outputs are direct passthrough: BS[outputIndices[k]] =
	// control[inputIndices[k]]. With control 1 = 1, only BS channel 1 is non-zero.
	// meshBlendShapeChannelMapping at LOD0 routes channel 1 to mesh 0 -> "MA__BB".
	static const TPair<const TCHAR*, float> ExpectedMorphCurves_RB1[] =
	{
		{ TEXT("MA__BA"), 0.0f },
		{ TEXT("MA__BB"), 1.0f },
		{ TEXT("MA__BC"), 0.0f },
		{ TEXT("MB__BD"), 0.0f },
		{ TEXT("MB__BE"), 0.0f },
		{ TEXT("MB__BF"), 0.0f },
		{ TEXT("MB__BG"), 0.0f },
		{ TEXT("MC__BH"), 0.0f },
		{ TEXT("MC__BI"), 0.0f },
	};

	static const TPair<const TCHAR*, float> ExpectedAnimMapCurves_RB1[] =
	{
		{ TEXT("A_A"), 0.0f },
		{ TEXT("A_B"), 1.0f },
		{ TEXT("A_C"), 0.0f },
		{ TEXT("A_D"), 0.0f },
		{ TEXT("A_E"), 1.0f },
		{ TEXT("A_F"), 0.0f },
		{ TEXT("A_G"), 0.4f },
		{ TEXT("A_H"), 0.0f },
		{ TEXT("A_I"), 0.2f },
		{ TEXT("A_J"), 0.0f },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Fixtures_SingleControlAtRB1, "RigLogic.RigLogicModule.AnimNode_RigLogic.Fixtures.SingleControlAtRB1", GTestFlags)
bool FAnimNode_RigLogic_Fixtures_SingleControlAtRB1::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	// Drives R.B=1.0 and asserts every observable at that input: per-joint
	// translation/rotation/scale plus the full set of morph-target and animated-map
	// curves the AnimNode emits. Single test instead of three so we don't pay the
	// fixture bootstrap cost more than once for the same input scenario.
	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	SetRawControlCurve(Output, Fixture.RawControlNames[1], 1.0f); // R.B = 1.0
	Node.Evaluate_AnyThread(Output);

	for (const FExpectedJoint& Expected : Fixtures::ExpectedJointsAtRB1)
	{
		AssertJoint(*this, Output, Fixture, Expected, TEXT("RB1"));
	}

	const TMap<FName, float> Curves = SnapshotCurves(Output);
	for (const TPair<const TCHAR*, float>& Expected : Fixtures::ExpectedMorphCurves_RB1)
	{
		const FName Name(Expected.Key);
		const float* ActualPtr = Curves.Find(Name);
		if (ActualPtr == nullptr)
		{
			AddError(FString::Printf(TEXT("Morph curve missing: %s"), Expected.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Morph curve %s value at R.B=1"), Expected.Key), *ActualPtr, Expected.Value, 1.e-4f);
	}
	for (const TPair<const TCHAR*, float>& Expected : Fixtures::ExpectedAnimMapCurves_RB1)
	{
		const FName Name(Expected.Key);
		const float* ActualPtr = Curves.Find(Name);
		if (ActualPtr == nullptr)
		{
			AddError(FString::Printf(TEXT("Animated-map curve missing: %s"), Expected.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Animated-map curve %s value at R.B=1"), Expected.Key), *ActualPtr, Expected.Value, 1.e-4f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Fixtures_RawControlScalesLinearly, "RigLogic.RigLogicModule.AnimNode_RigLogic.Fixtures.RawControlScalesLinearly", GTestFlags)
bool FAnimNode_RigLogic_Fixtures_RawControlScalesLinearly::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	// R.B=1 produces a non-zero translation delta on joint 0 (TZ_UE = 0.05).
	// At R.B=0.5 it must produce exactly half that delta. Verifies linearity
	// of the joint-group computation in the input control value.
	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Neutral(Fixture.Proxy.Get());
	SeedSentinelPose(Neutral);
	SeedAllRawControlsZero(Neutral, Fixture);
	Node.Evaluate_AnyThread(Neutral);

	FPoseContext Half(Fixture.Proxy.Get());
	SeedSentinelPose(Half);
	SeedAllRawControlsZero(Half, Fixture);
	SetRawControlCurve(Half, Fixture.RawControlNames[1], 0.5f);
	Node.Evaluate_AnyThread(Half);

	FPoseContext Full(Fixture.Proxy.Get());
	SeedSentinelPose(Full);
	SeedAllRawControlsZero(Full, Fixture);
	SetRawControlCurve(Full, Fixture.RawControlNames[1], 1.0f);
	Node.Evaluate_AnyThread(Full);

	const int32 J0BoneIndex = Fixture.DNAJointToBoneIndex[0];
	FBoneContainer& B = const_cast<FBoneContainer&>(Full.Pose.GetBoneContainer());
	const FCompactPoseBoneIndex J0 = B.MakeCompactPoseIndex(FMeshPoseBoneIndex(J0BoneIndex));

	const FVector NeutralT = Neutral.Pose[J0].GetTranslation();
	const FVector HalfDelta = Half.Pose[J0].GetTranslation() - NeutralT;
	const FVector FullDelta = Full.Pose[J0].GetTranslation() - NeutralT;

	TestEqual(TEXT("Joint 0 dTx scales linearly"), static_cast<float>(HalfDelta.X) * 2.0f, static_cast<float>(FullDelta.X), 1.e-3f);
	TestEqual(TEXT("Joint 0 dTy scales linearly"), static_cast<float>(HalfDelta.Y) * 2.0f, static_cast<float>(FullDelta.Y), 1.e-3f);
	TestEqual(TEXT("Joint 0 dTz scales linearly"), static_cast<float>(HalfDelta.Z) * 2.0f, static_cast<float>(FullDelta.Z), 1.e-3f);
	// Sanity: Full TZ delta must actually equal 0.05 (the fixture value).
	TestEqual(TEXT("Joint 0 dTz at R.B=1 equals expected value 0.05f"), static_cast<float>(FullDelta.Z), 0.05f, 1.e-3f);
	return true;
}

// =============================================================================
// Multi-control input - full coverage (joints, rotations, scales, all curves)
//
// At R.A=R.D=R.G=1, post-PSD vector has raw[0]=raw[3]=raw[6]=1 and PSD9=0.81;
// every other PSD evaluates to zero (each PSD is the product of weighted inputs,
// and any zero column kills the product).
//
// JOINT GROUPS active at LOD 0:
//   Group 0 (joint 0; 3 outputs x 7 inputs, inputs [0,1,2,3,6,7,8] -> [1,0,0,1,1,0,0]):
//     out 2  / J0 TZ_DNA = values[0,3,4]   = [0, 0.15, 0.20]   -> 0.35
//     out 3  / J0 RotEulX_DNA = values[7,10,11]  = [0.35, 0.50, 0.55] -> 1.40 deg
//     out 5  / J0 RotEulZ_DNA = values[14,17,18] = [0.70, 0.85, 0.90] -> 2.45 deg
//
//   Group 1 (joints 2, 4; 4 outputs x 5 inputs, inputs [3,4,7,8,9] -> [1,0,0,0,0.81]):
//     out 18 / J2 TX_DNA = 0.01 + 0.05*0.81 = 0.0505
//     out 20 / J2 TZ_DNA = 0.06 + 0.10*0.81 = 0.141
//     out 36 / J4 TX_DNA = 0.11 + 0.15*0.81 = 0.2315
//     out 38 / J4 TZ_DNA = 0.16 + 0.20*0.81 = 0.322
//
//   Group 2 (joints 6, 7; 3 outputs x 4 inputs, inputs [4,5,8,9] -> [0,0,0,0.81]):
//     out 55 / J6 TY_DNA = 0.47*0.81 = 0.3807
//     out 56 / J6 TZ_DNA = 0.69*0.81 = 0.5589
//     out 63 / J7 TX_DNA = 0.91*0.81 = 0.7371
//
//   Group 3 (joints 5, 7; 3 outputs x 4 inputs, inputs [2,5,6,8] -> [0,0,1,0]):
//     out 45 / J5 TX_DNA = 0.42
//     out 46 / J5 TY_DNA = 0.64
//     out 71 / J7 SZ_DNA = 0.86  (note: this is SCALE, attribute 8)
//
// COORDINATE-SYSTEM CONVERSION (DNA -> UE):
//   Translations: X and Y negated, Z preserved.
//   Rotations: composed via FQuat with the converted neutral; we don't pin
//   exact quat components and instead assert "rotation changed / didn't" plus
//   approximate angular distance.
//   Scales: NOT flipped. Final scale = neutralScale (1) + delta. Only J7's SZ
//   gets a delta (+0.86), so J7's final scale is (1, 1, 1.86).
//
// BLEND-SHAPE CHANNELS (direct passthrough at LOD 0):
//   inputIndices [0,1,2,3,6,7,8] -> outputs [0,1,2,3,6,7,8]
//   At raw[0]=raw[3]=raw[6]=1: BS[0]=1, BS[3]=1, BS[6]=1, all others 0
//   meshBlendShapeChannelMapping at LOD 0 routes BS channel k to mesh m via
//   the mapping table. Mappings (in order): mesh0:ch0, mesh0:ch1, mesh0:ch2,
//   mesh1:ch3, mesh1:ch4, mesh1:ch5, mesh1:ch6, mesh2:ch7, mesh2:ch8.
//   So output curves: MA__BA=1, MA__BB=0, MA__BC=0, MB__BD=1, MB__BE=0,
//   MB__BF=0, MB__BG=1, MC__BH=0, MC__BI=0.
//
// ANIMATED MAPS (15 conditional rows, all active at LOD 0):
//   At raw[0]=raw[3]=raw[6]=1, all other raw=0, walking the conditional table
//   for each row's (from <= raw[inputIdx] <= to) check, contributing
//   slope*raw[inputIdx] + cut to the output:
//     A_A: row 0 fires (1*1+0)             = 1.0
//     A_B: row 1 fires (0.9*0+0.5)         = 0.5
//     A_C: no row fires                    = 0
//     A_D: row 5 fires (0.7*1+0.3)         = 1.0  (raw[3]=1 in [0.7, 1])
//     A_E: row 6 fires (0.6*0+1)           = 1.0
//     A_F: no row fires                    = 0
//     A_G: row 10 fires (0.6*1+0.4)        = 1.0  (raw[6]=1 in [0, 1])
//     A_H: no row fires                    = 0
//     A_I: row 14 fires (0.9*0+0.2)        = 0.2
//     A_J: no row references output 9      = 0
// =============================================================================

namespace UE::RigLogic::Tests::Fixtures
{
	// Scenario: R.A=R.D=R.G=1.0. Activates PSD9 (=0.81) feeding groups 1 and 2; group 0
	// col 0/3/4 active for joint 0; group 3 col 2 active for joints 5 and 7. See the
	// MultiControlPSDActivation test header for the per-group derivation.
	static const FExpectedJoint ExpectedJointsAtRABDG1[] =
	{
		{ 0, FVector(-1.0000000f, -1.0000000f, 1.3500000f), FQuat(-0.5243699f, -0.5404704f, 0.5859184f, -0.2993780f), FVector(1.0f, 1.0f, 1.0f) },
		{ 2, FVector(-3.0504999f, -3.0000000f, 3.1410000f), FQuat(0.0653920f, -0.0753745f, -0.0653920f, 0.9928575f), FVector(1.0f, 1.0f, 1.0f) },
		{ 4, FVector(-5.2315001f, -5.0000000f, 5.3220000f), FQuat(0.6710627f, 0.0971734f, -0.6710627f, 0.2998449f), FVector(1.0f, 1.0f, 1.0f) },
		{ 5, FVector(-6.4200001f, -6.6399999f, 6.0000000f), FQuat(0.1580251f, 0.1185940f, -0.1580252f, 0.9674665f), FVector(1.0f, 1.0f, 1.0f) },
		{ 6, FVector(-7.0000000f, -7.3807001f, 7.5588999f), FQuat(-0.1923898f, -0.4228496f, 0.1923897f, 0.8643902f), FVector(1.0f, 1.0f, 1.0f) },
		{ 7, FVector(-8.7370996f, -8.0000000f, 8.0000000f), FQuat(0.0510307f, -0.6977183f, -0.0510307f, 0.7127279f), FVector(1.0f, 1.0f, 1.8600000f) },
		{ 1, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
		{ 3, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
		{ 8, FVector::ZeroVector, FQuat::Identity, FVector::OneVector },
	};

	static const TPair<const TCHAR*, float> ExpectedMorphCurves_RABDG1[] =
	{
		{ TEXT("MA__BA"), 1.0f },
		{ TEXT("MA__BB"), 0.0f },
		{ TEXT("MA__BC"), 0.0f },
		{ TEXT("MB__BD"), 1.0f },
		{ TEXT("MB__BE"), 0.0f },
		{ TEXT("MB__BF"), 0.0f },
		{ TEXT("MB__BG"), 1.0f },
		{ TEXT("MC__BH"), 0.0f },
		{ TEXT("MC__BI"), 0.0f },
	};

	static const TPair<const TCHAR*, float> ExpectedAnimMapCurves_RABDG1[] =
	{
		{ TEXT("A_A"), 1.0f },
		{ TEXT("A_B"), 0.5f },
		{ TEXT("A_C"), 0.0f },
		{ TEXT("A_D"), 1.0f },
		{ TEXT("A_E"), 1.0f },
		{ TEXT("A_F"), 0.0f },
		{ TEXT("A_G"), 1.0f },
		{ TEXT("A_H"), 0.0f },
		{ TEXT("A_I"), 0.2f },
		{ TEXT("A_J"), 0.0f },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Fixtures_MultiControlPSDActivation, "RigLogic.RigLogicModule.AnimNode_RigLogic.Fixtures.MultiControlPSDActivation", GTestFlags)
bool FAnimNode_RigLogic_Fixtures_MultiControlPSDActivation::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	// --- Expected curves at R.A=R.D=R.G=1 ---
	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	SetRawControlCurve(Output, Fixture.RawControlNames[0], 1.0f); // R.A
	SetRawControlCurve(Output, Fixture.RawControlNames[3], 1.0f); // R.D
	SetRawControlCurve(Output, Fixture.RawControlNames[6], 1.0f); // R.G
	Node.Evaluate_AnyThread(Output);

	// --- Joint observations: translation, rotation, scale (fixture table) ---
	for (const FExpectedJoint& Expected : Fixtures::ExpectedJointsAtRABDG1)
	{
		AssertJoint(*this, Output, Fixture, Expected, TEXT("RABDG1"));
	}

	// --- Curve observations: morph + animated map, every name ---
	const TMap<FName, float> Curves = SnapshotCurves(Output);
	for (const TPair<const TCHAR*, float>& E : Fixtures::ExpectedMorphCurves_RABDG1)
	{
		const float* Actual = Curves.Find(FName(E.Key));
		if (Actual == nullptr)
		{
			AddError(FString::Printf(TEXT("Morph curve missing: %s"), E.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Morph curve %s"), E.Key), *Actual, E.Value, 1.e-4f);
	}
	for (const TPair<const TCHAR*, float>& E : Fixtures::ExpectedAnimMapCurves_RABDG1)
	{
		const float* Actual = Curves.Find(FName(E.Key));
		if (Actual == nullptr)
		{
			AddError(FString::Printf(TEXT("Animated-map curve missing: %s"), E.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Animated-map curve %s"), E.Key), *Actual, E.Value, 1.e-4f);
	}

	return true;
}

// =============================================================================
// Conditional in active region (animated map)
//
// At zero input the animated-map A_B curve evaluates to 0.5 (entry 1: from=0,
// to=0.6 includes 0; output += 0.9*0 + 0.5 = 0.5).
// At R.B=0.5 entry 1 still fires (0.5 is in [0, 0.6]) and now contributes
//   slope*input + cut = 0.9*0.5 + 0.5 = 0.95
// Entry 2 (from=0.6, to=1) still fails because 0.5 < 0.6.
// So A_B at R.B=0.5 must be exactly 0.95. Verifies the conditional is firing
// in its linear region (slope*x + cut), not just at the boundary.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Fixtures_ConditionalAtNonZeroInput, "RigLogic.RigLogicModule.AnimNode_RigLogic.Fixtures.ConditionalAtNonZeroInput", GTestFlags)
bool FAnimNode_RigLogic_Fixtures_ConditionalAtNonZeroInput::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	SetRawControlCurve(Output, Fixture.RawControlNames[1], 0.5f); // R.B = 0.5
	Node.Evaluate_AnyThread(Output);

	const TMap<FName, float> Curves = SnapshotCurves(Output);
	const float* AB = Curves.Find(FName(TEXT("A_B")));
	if (AB == nullptr)
	{
		AddError(TEXT("A_B animated-map curve missing"));
		return false;
	}
	TestEqual(TEXT("A_B at R.B=0.5 hits conditional in active region (slope*0.5 + cut = 0.95)"), *AB, 0.95f, 1.e-4f);
	return true;
}

// =============================================================================
// Configuration gates - LoadJoints / LoadBlendShapes / LoadAnimatedMaps
//
// Mutate the UDNA's RigLogicConfiguration before CacheBones, then drive a fake
// PostEditChangeProperty event (only available WITH_EDITOR; this whole test
// suite already runs in EditorContext) so the runtime context rebuilds with
// the new config. Verify the corresponding Update*Curves / UpdateJoints branch
// is actually skipped.
// =============================================================================

namespace UE::RigLogic::Tests
{
	// Apply a RigLogicConfiguration override to the loaded UDNA and re-trigger
	// runtime context initialisation via PostEditChangeProperty.
	static void OverrideRigLogicConfig(UDNA* DNA, TFunctionRef<void(FRigLogicConfiguration&)> Mutator)
	{
		Mutator(DNA->RigLogicConfiguration);
		FProperty* Prop = FindFProperty<FProperty>(UDNA::StaticClass(), GET_MEMBER_NAME_CHECKED(UDNA, RigLogicConfiguration));
		FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
		DNA->PostEditChangeProperty(Event);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Config_LoadJointsFalseSkipsJoints, "RigLogic.RigLogicModule.AnimNode_RigLogic.Config.LoadJointsFalseSkipsJoints", GTestFlags)
bool FAnimNode_RigLogic_Config_LoadJointsFalseSkipsJoints::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	// Strategy: evaluate twice with identical input - once with the default config
	// (LoadJoints=true), once with LoadJoints=false - and assert that the variable
	// joints differ between the two runs while non-joint outputs (curves) are identical.
	//
	// We cannot rely on the sentinel-vs-output comparison here because the AnimNode's
	// AnimSequence.Evaluate(OutputContext) writes a ref pose into the output before
	// our gate kicks in, overwriting the sentinel for every bone in the compact pose.
	// With LoadJoints=false, UpdateJoints is skipped, but the ref pose remains, so
	// "bone differs from sentinel" is true even though RigLogic wrote nothing.

	auto EvaluateWith = [this](TFunctionRef<void(FRigLogicConfiguration&)> ConfigMutator)
		-> TTuple<bool, TArray<FTransform>, TMap<FName, float>>
	{
		FAnimNodeFixture Fixture;
		if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
		{
			return {};
		}
		if (!Fixture.DNA.IsValid())
		{
			return {};
		}

		OverrideRigLogicConfig(Fixture.DNA.Get(), ConfigMutator);

		FAnimNode_RigLogic Node;
		Fixture.InitializeAndCacheBones(Node);

		FPoseContext Output(Fixture.Proxy.Get());
		SeedSentinelPose(Output);
		SeedAllRawControlsZero(Output, Fixture);
		SetRawControlCurve(Output, Fixture.RawControlNames[0], 1.0f);
		SetRawControlCurve(Output, Fixture.RawControlNames[3], 1.0f);
		SetRawControlCurve(Output, Fixture.RawControlNames[6], 1.0f);
		Node.Evaluate_AnyThread(Output);

		TArray<FTransform> JointTransforms;
		// Snapshot the variable joints' transforms (in DNA-joint-index order).
		static const uint16 VariableJoints[] = { 0, 2, 4, 5, 6, 7 };
		FBoneContainer& Bones = const_cast<FBoneContainer&>(Output.Pose.GetBoneContainer());
		for (const uint16 J : VariableJoints)
		{
			const int32 MeshIdx = Fixture.DNAJointToBoneIndex[J];
			const FCompactPoseBoneIndex CompactIdx = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIdx));
			JointTransforms.Add(CompactIdx.GetInt() != INDEX_NONE ? Output.Pose[CompactIdx] : FTransform::Identity);
		}
		return MakeTuple(true, JointTransforms, SnapshotCurves(Output));
	};

	auto Default = EvaluateWith([](FRigLogicConfiguration&) { /* leave defaults */ });
	auto NoJoints = EvaluateWith([](FRigLogicConfiguration& Cfg)
	{
		Cfg.LoadJoints = false;
	});
	if (!Default.Get<0>() || !NoJoints.Get<0>())
	{
		AddError(TEXT("Fixture setup failed"));
		return false;
	}

	const TArray<FTransform>& DefaultJoints = Default.Get<1>();
	const TArray<FTransform>& NoJointsJoints = NoJoints.Get<1>();

	// At least one variable joint must differ between the two configs.
	bool bAnyDiffer = false;
	for (int32 i = 0; i < DefaultJoints.Num(); ++i)
	{
		if (!DefaultJoints[i].Equals(NoJointsJoints[i], 1.e-3f))
		{
			bAnyDiffer = true;
			break;
		}
	}
	TestTrue(TEXT("Variable joint transforms must differ between LoadJoints=true and LoadJoints=false"), bAnyDiffer);

	// Every variable joint with LoadJoints=false must equal the ref pose (no RigLogic
	// delta). The ref pose for our skeleton is FTransform::Identity per BuildSkeletonForDNA.
	for (int32 i = 0; i < NoJointsJoints.Num(); ++i)
	{
		TestTrue(*FString::Printf(TEXT("Variable joint #%d should equal ref pose with LoadJoints=false"), i), NoJointsJoints[i].Equals(FTransform::Identity, 1.e-3f));
	}

	// Curves must still be emitted (LoadBlendShapes / LoadAnimatedMaps still on).
	TestTrue(TEXT("Morph curves should still be emitted with LoadJoints=false"), NoJoints.Get<2>().Contains(FName(TEXT("MA__BA"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Config_LoadBlendShapesFalseSkipsMorphs, "RigLogic.RigLogicModule.AnimNode_RigLogic.Config.LoadBlendShapesFalseSkipsMorphs", GTestFlags)
bool FAnimNode_RigLogic_Config_LoadBlendShapesFalseSkipsMorphs::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	if (!Fixture.DNA.IsValid())
	{
		AddError(TEXT("UDNA path required for this test"));
		return false;
	}

	OverrideRigLogicConfig(Fixture.DNA.Get(), [](FRigLogicConfiguration& Cfg)
	{
		Cfg.LoadBlendShapes = false;
	});

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	SetRawControlCurve(Output, Fixture.RawControlNames[0], 1.0f);
	Node.Evaluate_AnyThread(Output);

	const TMap<FName, float> Curves = SnapshotCurves(Output);
	for (const FName& MorphName : Fixture.ExpectedMorphCurveNames)
	{
		TestFalse(*FString::Printf(TEXT("Morph curve %s should NOT be emitted with LoadBlendShapes=false"), *MorphName.ToString()), Curves.Contains(MorphName));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_Config_LoadAnimatedMapsFalseSkipsAnimMaps, "RigLogic.RigLogicModule.AnimNode_RigLogic.Config.LoadAnimatedMapsFalseSkipsAnimMaps", GTestFlags)
bool FAnimNode_RigLogic_Config_LoadAnimatedMapsFalseSkipsAnimMaps::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	if (!Fixture.DNA.IsValid())
	{
		AddError(TEXT("UDNA path required for this test"));
		return false;
	}

	OverrideRigLogicConfig(Fixture.DNA.Get(), [](FRigLogicConfiguration& Cfg)
	{
		Cfg.LoadAnimatedMaps = false;
	});

	FAnimNode_RigLogic Node;
	Fixture.InitializeAndCacheBones(Node);

	FPoseContext Output(Fixture.Proxy.Get());
	SeedSentinelPose(Output);
	SeedAllRawControlsZero(Output, Fixture);
	Node.Evaluate_AnyThread(Output);

	const TMap<FName, float> Curves = SnapshotCurves(Output);
	for (const FName& AnimMapName : Fixture.ExpectedAnimMapCurveNames)
	{
		TestFalse(*FString::Printf(TEXT("Animated-map curve %s should NOT be emitted with LoadAnimatedMaps=false"), *AnimMapName.ToString()), Curves.Contains(AnimMapName));
	}
	return true;
}

// =============================================================================
// Input curve clamping
//
// FAnimNode_RigLogic::UpdateRawControls and UpdateRawControlsCached both apply
// FMath::Clamp(value, 0.0, 1.0) before writing to the RigInstance's raw control
// buffer. This test asserts that out-of-range input curve values produce the
// same output as their clamped equivalents.
//   R.B = 2.0  must produce identical output to R.B = 1.0
//   R.B = -0.5 must produce identical output to R.B = 0.0  (i.e. the neutral)
// We exercise both the cached and uncached code paths.
// =============================================================================

namespace UE::RigLogic::Tests
{
	// Snapshot of post-Evaluate observables. FPoseContext does not survive copy/move
	// (its copy ctor AddUninitialized's the bone array, wiping any state we set), so
	// tests that need to compare two evaluations capture into this plain struct first.
	struct FRunSnapshot
	{
		TArray<FTransform> BoneTransforms;
		TMap<FName, float> Curves;
	};

	// Seed sentinel + raw-control inputs on a locally-constructed FPoseContext, evaluate,
	// and capture the result into an FRunSnapshot. Setting all controls every frame is
	// the contract the cached path documents (stable input curve set across calls);
	// without it, the cached path can leave stale RigInstance state from a previous Evaluate.
	static FRunSnapshot EvaluateWithSingleControl(FAnimNodeFixture& Fixture, FAnimNode_RigLogic& Node, int32 ControlIndex, float Value)
	{
		FPoseContext Output(Fixture.Proxy.Get());
		SeedSentinelPose(Output);
		SeedAllRawControlsZero(Output, Fixture);
		if (ControlIndex != -1)
		{
			SetRawControlCurve(Output, Fixture.RawControlNames[ControlIndex], Value);
		}
		Node.Evaluate_AnyThread(Output);

		FRunSnapshot Snap;
		Snap.BoneTransforms.Reserve(Output.Pose.GetNumBones());
		for (FCompactPoseBoneIndex CompactIndex : Output.Pose.ForEachBoneIndex())
		{
			Snap.BoneTransforms.Add(Output.Pose[CompactIndex]);
		}
		Snap.Curves = SnapshotCurves(Output);
		return Snap;
	}

	// Returns true if two snapshots have matching joint transforms AND matching values for
	// the curves the AnimNode itself emits (morph targets and animated maps). Input control
	// curves (R.A, R.B, ...) leak through the FNamedValueArrayUtils::Union in UpdateRawControls
	// and end up in the output as-is, so comparing them across two different input scenarios
	// would always fail. We only care about derived outputs.
	static bool DerivedOutputsMatch(FAutomationTestBase& Test, FAnimNodeFixture& Fixture, const FRunSnapshot& A, const FRunSnapshot& B, const TCHAR* Label)
	{
		bool bOk = true;

		// Joint transforms: full compare (no input leakage).
		if (A.BoneTransforms.Num() != B.BoneTransforms.Num())
		{
			Test.AddError(FString::Printf(TEXT("%s: bone count differs (%d vs %d)"), Label, A.BoneTransforms.Num(), B.BoneTransforms.Num()));
			return false;
		}
		for (int32 i = 0; i < A.BoneTransforms.Num(); ++i)
		{
			if (!A.BoneTransforms[i].Equals(B.BoneTransforms[i], 1.e-3f))
			{
				Test.AddError(FString::Printf(TEXT("%s: bone %d transforms differ"), Label, i));
				bOk = false;
			}
		}

		// Curves: compare only the AnimNode-emitted ones (morph + anim-map).
		auto CompareDerivedCurve = [&](FName Name)
		{
			const float* PtrA = A.Curves.Find(Name);
			const float* PtrB = B.Curves.Find(Name);
			if (PtrA == nullptr && PtrB == nullptr)
			{
				return;
			}
			if (PtrA == nullptr || PtrB == nullptr)
			{
				Test.AddError(FString::Printf(TEXT("%s: derived curve %s presence differs"), Label, *Name.ToString()));
				bOk = false;
				return;
			}
			if (!FMath::IsNearlyEqual(*PtrA, *PtrB, 1.e-4f))
			{
				Test.AddError(FString::Printf(TEXT("%s: derived curve %s differs (%f vs %f)"), Label, *Name.ToString(), *PtrA, *PtrB));
				bOk = false;
			}
		};
		for (const FName& Name : Fixture.ExpectedMorphCurveNames)
		{
			CompareDerivedCurve(Name);
		}
		for (const FName& Name : Fixture.ExpectedAnimMapCurveNames)
		{
			CompareDerivedCurve(Name);
		}
		return bOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_InputClamping_Uncached, "RigLogic.RigLogicModule.AnimNode_RigLogic.InputClamping.Uncached", GTestFlags)
bool FAnimNode_RigLogic_InputClamping_Uncached::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Node.CacheAnimCurveNames = false;
	Fixture.InitializeAndCacheBones(Node);

	FRunSnapshot OneOutput = EvaluateWithSingleControl(Fixture, Node, 1, 1.0f);
	FRunSnapshot OverOneOutput = EvaluateWithSingleControl(Fixture, Node, 1, 2.0f);
	DerivedOutputsMatch(*this, Fixture, OneOutput, OverOneOutput, TEXT("Uncached: R.B=2.0 should match R.B=1.0"));

	FRunSnapshot ZeroOutput = EvaluateWithSingleControl(Fixture, Node, /*all zero=*/-1, 0.0f);
	FRunSnapshot NegativeOutput = EvaluateWithSingleControl(Fixture, Node, 1, -0.5f);
	DerivedOutputsMatch(*this, Fixture, ZeroOutput, NegativeOutput, TEXT("Uncached: R.B=-0.5 should match R.B=0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimNode_RigLogic_InputClamping_Cached, "RigLogic.RigLogicModule.AnimNode_RigLogic.InputClamping.Cached", GTestFlags)
bool FAnimNode_RigLogic_InputClamping_Cached::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests;

	FAnimNodeFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	FAnimNode_RigLogic Node;
	Node.CacheAnimCurveNames = true;
	Fixture.InitializeAndCacheBones(Node);

	FRunSnapshot OneOutput = EvaluateWithSingleControl(Fixture, Node, 1, 1.0f);
	FRunSnapshot OverOneOutput = EvaluateWithSingleControl(Fixture, Node, 1, 2.0f);
	DerivedOutputsMatch(*this, Fixture, OneOutput, OverOneOutput, TEXT("Cached: R.B=2.0 should match R.B=1.0"));

	FRunSnapshot ZeroOutput = EvaluateWithSingleControl(Fixture, Node, /*all zero=*/-1, 0.0f);
	FRunSnapshot NegativeOutput = EvaluateWithSingleControl(Fixture, Node, 1, -0.5f);
	DerivedOutputsMatch(*this, Fixture, ZeroOutput, NegativeOutput, TEXT("Cached: R.B=-0.5 should match R.B=0"));
	return true;
}

// =============================================================================
// LOD switching - TODO
//
// FAnimNode_RigLogic::CacheBones_AnyThread reads the current LOD via
// Context.AnimInstanceProxy->GetLODLevel() and caches a per-LOD joint mapping.
// At LOD 1 the test DNA reduces variable joints to {0, 2, 6} (groups 1, 2, 3
// have lods=2/2/0 respectively, so groups 0 and 1's first 2 outputs and group
// 2's first 2 outputs survive; group 3 is dropped entirely). A proper LOD test
// would drive an evaluation at LOD 1, confirm that joints {4, 5, 7} are not
// written, and confirm that joint 0 still gets its full delta.
//
// Blocker: FAnimInstanceProxy::LODLevel is private with no public setter; it
// is only updated via PreUpdate(InAnimInstance), which calls
// InAnimInstance->GetLODLevel() (virtual). Driving this cleanly requires a
// UCLASS-based UAnimInstance subclass that overrides GetLODLevel(), which in
// turn needs a .generated.h and UHT integration.
//
// Deferred: write a separate header for UTestAnimInstance with the right
// .generated.h scaffolding once we have a use case beyond LOD.
// =============================================================================

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
