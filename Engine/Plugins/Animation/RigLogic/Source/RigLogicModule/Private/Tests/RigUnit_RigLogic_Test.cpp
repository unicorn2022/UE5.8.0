// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MemStack.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "DNA.h"
#include "DNAAsset.h"
#include "DNAAssetUserData.h"
#include "DNAReader.h"
#include "DNAUtils.h"
#include "RigUnit_RigLogic.h"
#include "Units/RigUnitContext.h"

// Test-only accessor: FRigUnit_RigLogic declares "friend TestAccessor", giving us
// permission to read/write its private Data and bIsInitialized members from tests.
struct FRigUnit_RigLogic::TestAccessor
{
	static FRigUnit_RigLogic_Data& GetData(FRigUnit_RigLogic& Unit)
	{
		return Unit.Data;
	}

	static void ResetInitFlag(FRigUnit_RigLogic& Unit)
	{
		Unit.bIsInitialized = false;
	}
};

namespace UE::RigLogic::Tests::RigUnit
{
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


	// Populate the URigHierarchy with bones (matching DNA joints), control curves
	// (matching DNA raw controls), morph target curves and animated map curves.
	// FRigUnit_RigLogic_Data resolves these names via URigHierarchy::GetIndex; if a
	// name is not present, the lookup returns INDEX_NONE and that channel becomes a
	// silent no-op. So every name the RigUnit will ever query has to be present here.
	static void PopulateHierarchy(URigHierarchy* Hierarchy, URigHierarchyController* Controller, const IDNAReader& DNAReader)
	{
		// Bones, parented per the DNA joint hierarchy.
		const uint16 JointCount = DNAReader.GetJointCount();
		TArray<FRigElementKey> BoneKeys;
		BoneKeys.SetNum(JointCount);
		for (uint16 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
		{
			const FString JointName = DNAReader.GetJointName(JointIndex);
			const uint16 ParentIndex = DNAReader.GetJointParentIndex(JointIndex);
			FRigElementKey ParentKey;
			if (ParentIndex != JointIndex)
			{
				ParentKey = BoneKeys[ParentIndex];
			}
			BoneKeys[JointIndex] = Controller->AddBone(FName(*JointName), ParentKey, FTransform::Identity, /*bTransformInGlobal=*/false);
		}

		// Raw controls: DNA names use "<obj>.<attr>", FRigUnit_RigLogic rewrites them to
		// "<obj>_<attr>" before looking them up against the hierarchy.
		const uint16 ControlCount = DNAReader.GetRawControlCount();
		for (uint16 ControlIndex = 0; ControlIndex < ControlCount; ++ControlIndex)
		{
			const FString ControlName = DNAReader.GetRawControlName(ControlIndex);
			FString ObjectName, AttrName;
			FName CurveName;
			if (ControlName.Split(TEXT("."), &ObjectName, &AttrName))
			{
				CurveName = FName(*(ObjectName + TEXT("_") + AttrName));
			}
			else
			{
				CurveName = FName(*ControlName);
			}
			Controller->AddCurve(CurveName, 0.f);
		}

		// Morph targets - one curve per <mesh>__<blendshape> pair declared at any LOD.
		{
			const uint16 LODCount = DNAReader.GetLODCount();
			TSet<FName> Seen;
			for (uint16 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
			{
				TArrayView<const uint16> Mappings = DNAReader.GetMeshBlendShapeChannelMappingIndicesForLOD(LODIndex);
				for (const uint16 MappingIndex : Mappings)
				{
					const FMeshBlendShapeChannelMapping Mapping = DNAReader.GetMeshBlendShapeChannelMapping(MappingIndex);
					const FString MeshName = DNAReader.GetMeshName(Mapping.MeshIndex);
					const FString BlendShapeName = DNAReader.GetBlendShapeChannelName(Mapping.BlendShapeChannelIndex);
					const FName CurveName(*(MeshName + TEXT("__") + BlendShapeName));
					bool bSeen = false;
					Seen.Add(CurveName, &bSeen);
					if (!bSeen)
					{
						Controller->AddCurve(CurveName, 0.f);
					}
				}
			}
		}

		// Animated maps - one curve per <obj>_<attr> at any LOD.
		{
			const uint16 LODCount = DNAReader.GetLODCount();
			TSet<FName> Seen;
			for (uint16 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
			{
				TArrayView<const uint16> AnimMapIndicesForLOD = DNAReader.GetAnimatedMapIndicesForLOD(LODIndex);
				for (const uint16 AnimMapIndex : AnimMapIndicesForLOD)
				{
					const FString AnimMapName = DNAReader.GetAnimatedMapName(AnimMapIndex);
					FString ObjectName, AttrName;
					if (AnimMapName.Split(TEXT("."), &ObjectName, &AttrName))
					{
						const FName CurveName(*(ObjectName + TEXT("_") + AttrName));
						bool bSeen = false;
						Seen.Add(CurveName, &bSeen);
						if (!bSeen)
						{
							Controller->AddCurve(CurveName, 0.f);
						}
					}
				}
			}
		}
	}

	// A non-identity, non-ref-pose sentinel transform we stamp onto every bone in the
	// URigHierarchy before Execute. This matches the AnimNode test convention so the
	// driver-joint feed (which reads the input pose) produces the same RigInstance state
	// as the AnimNode tests, allowing us to share fixture values.
	//
	// The test DNAs neutral joint values happen to be identity, which makes
	// "this bone moved" indistinguishable from "this bone was never written" if the start
	// state is identity. A sentinel start is unambiguous.
	static FTransform GetSentinelTransform()
	{
		return FTransform(
			FQuat(0.4f, 0.3f, 0.2f, 0.1f).GetNormalized(),
			FVector(1234.5f, -987.65f, 4242.0f),
			FVector(2.5f, 3.5f, 4.5f));
	}

	// Holds every pinned UObject for a single test run. Test bodies construct one of
	// these, drive the RigUnit through it, then let it go out of scope.
	struct FRigUnitFixture
	{
		FMemMark MemMark;

		TStrongObjectPtr<USkeleton> Skeleton;
		TStrongObjectPtr<USkeletalMesh> Mesh;
		TStrongObjectPtr<USkeletalMeshComponent> Component;
		TStrongObjectPtr<UDNA> DNA;
		TStrongObjectPtr<URigHierarchy> Hierarchy;

		TSharedPtr<IDNAReader> DNAReader;
		FControlRigExecuteContext ExecuteContext;

		TArray<FRigElementKey> BoneKeys;
		TArray<FName> ExpectedMorphCurveNames;
		TArray<FName> ExpectedAnimMapCurveNames;
		TArray<FName> RawControlNames;

		FRigUnitFixture() : MemMark(FMemStack::Get())
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

			// FRigUnit_RigLogic reads the SkeletalMesh from Data.SkelMeshComponent->GetSkeletalMeshAsset()
			// during initialisation. SetSkinnedAssetAndUpdate is the only public path that pairs
			// the asset write with NotifyIfSkinnedAssetChanged() (which is protected); on our
			// unregistered transient component, its registered-component branches no-op.
			Component.Reset(NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient));
			Component->SetSkinnedAssetAndUpdate(Mesh.Get());

			Hierarchy.Reset(NewObject<URigHierarchy>(GetTransientPackage(), NAME_None, RF_Transient));
			URigHierarchyController* Controller = Hierarchy->GetController(true);
			PopulateHierarchy(Hierarchy.Get(), Controller, *DNAReader);

			ExecuteContext.Hierarchy = Hierarchy.Get();

			CacheBoneKeys();
			CacheRawControlNames();
			CacheExpectedCurveNames();
			return true;
		}

		// Configure the unit and run a single Execute pass. Tests should call
		// SeedSentinelHierarchy() explicitly before Run() to match the AnimNode test
		// pattern of seeding the input pose before each Evaluate.
		void Run(FRigUnit_RigLogic& Unit)
		{
			FRigUnit_RigLogic::TestAccessor::GetData(Unit).SkelMeshComponent = Component.Get();
			Unit.Execute(ExecuteContext);
		}

		void SeedSentinelHierarchy()
		{
			const FTransform Sentinel = GetSentinelTransform();
			for (const FRigElementKey& BoneKey : BoneKeys)
			{
				Hierarchy->SetLocalTransform(BoneKey, Sentinel);
			}
		}

		void CacheBoneKeys()
		{
			BoneKeys.Reset();
			const uint16 JointCount = DNAReader->GetJointCount();
			BoneKeys.Reserve(JointCount);
			for (uint16 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
			{
				const FString JointName = DNAReader->GetJointName(JointIndex);
				BoneKeys.Add(FRigElementKey(FName(*JointName), ERigElementType::Bone));
			}
		}

		void CacheRawControlNames()
		{
			RawControlNames.Reset();
			const uint16 ControlCount = DNAReader->GetRawControlCount();
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

		void CacheExpectedCurveNames()
		{
			ExpectedMorphCurveNames.Reset();
			ExpectedAnimMapCurveNames.Reset();
			TSet<FName> SeenMorphs;
			TSet<FName> SeenAnimMaps;
			const uint16 LODCount = DNAReader->GetLODCount();
			for (uint16 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
			{
				TArrayView<const uint16> Mappings = DNAReader->GetMeshBlendShapeChannelMappingIndicesForLOD(LODIndex);
				for (const uint16 MappingIndex : Mappings)
				{
					const FMeshBlendShapeChannelMapping Mapping = DNAReader->GetMeshBlendShapeChannelMapping(MappingIndex);
					const FString MeshName = DNAReader->GetMeshName(Mapping.MeshIndex);
					const FString BlendShapeName = DNAReader->GetBlendShapeChannelName(Mapping.BlendShapeChannelIndex);
					const FName CurveName(*(MeshName + TEXT("__") + BlendShapeName));
					bool bSeen = false;
					SeenMorphs.Add(CurveName, &bSeen);
					if (!bSeen)
					{
						ExpectedMorphCurveNames.Add(CurveName);
					}
				}

				TArrayView<const uint16> AnimMapIndices = DNAReader->GetAnimatedMapIndicesForLOD(LODIndex);
				for (const uint16 AnimMapIndex : AnimMapIndices)
				{
					const FString AnimMapName = DNAReader->GetAnimatedMapName(AnimMapIndex);
					FString ObjectName, AttrName;
					if (AnimMapName.Split(TEXT("."), &ObjectName, &AttrName))
					{
						const FName CurveName(*(ObjectName + TEXT("_") + AttrName));
						bool bSeen = false;
						SeenAnimMaps.Add(CurveName, &bSeen);
						if (!bSeen)
						{
							ExpectedAnimMapCurveNames.Add(CurveName);
						}
					}
				}
			}
		}

		// Returns the indices of every DNA joint whose backing bone moved away from the
		// sentinel transform stamped by SeedSentinelHierarchy.
		TArray<uint16> CollectMovedJoints(float Tolerance = KINDA_SMALL_NUMBER) const
		{
			TArray<uint16> Moved;
			const FTransform Sentinel = GetSentinelTransform();
			for (uint16 JointIndex = 0; JointIndex < static_cast<uint16>(BoneKeys.Num()); ++JointIndex)
			{
				const FTransform T = Hierarchy->GetLocalTransform(BoneKeys[JointIndex]);
				if (!T.Equals(Sentinel, Tolerance))
				{
					Moved.Add(JointIndex);
				}
			}
			return Moved;
		}
	};

	// Seeds every raw control on the URigHierarchy to 0. Tests should call this before
	// any per-control overrides so the input curve set stays stable across Execute calls.
	static void SeedAllRawControlsZero(URigHierarchy* Hierarchy, const FRigUnitFixture& Fixture)
	{
		for (const FName& ControlName : Fixture.RawControlNames)
		{
			const FRigElementKey Key(ControlName, ERigElementType::Curve);
			Hierarchy->SetCurveValue(Key, 0.f);
		}
	}

	static void SetRawControlCurve(URigHierarchy* Hierarchy, FName CurveName, float Value)
	{
		const FRigElementKey Key(CurveName, ERigElementType::Curve);
		Hierarchy->SetCurveValue(Key, Value);
	}

	// Snapshot every named curve currently on the URigHierarchy.
	static TMap<FName, float> SnapshotCurves(URigHierarchy* Hierarchy)
	{
		TMap<FName, float> Out;
		Hierarchy->ForEach<FRigCurveElement>([&Out](FRigCurveElement* Curve) -> bool
		{
			Out.Add(Curve->GetFName(), Curve->Get());
			return true;
		});
		return Out;
	}

	static FTransform GetBoneLocal(URigHierarchy* Hierarchy, const FRigUnitFixture& Fixture, uint16 DNAJointIndex)
	{
		if (!Fixture.BoneKeys.IsValidIndex(DNAJointIndex))
		{
			return FTransform::Identity;
		}
		return Hierarchy->GetLocalTransform(Fixture.BoneKeys[DNAJointIndex]);
	}

	// Hand-captured fixture joint observations for the three input scenarios this suite targets.
	// Values were frozen by inspecting actual RigUnit output and verified to match AnimNode output.
	struct FExpectedJoint
	{
		uint16 JointIndex;
		FVector Translation;
		FQuat Rotation;
		FVector Scale;
	};

	static void AssertJoint(FAutomationTestBase& Test, URigHierarchy* Hierarchy, const FRigUnitFixture& Fixture, const FExpectedJoint& Expected, const TCHAR* ScenarioTag, float Tolerance = 1.e-3f)
	{
		if (!Fixture.BoneKeys.IsValidIndex(Expected.JointIndex))
		{
			Test.AddError(FString::Printf(TEXT("%s J%u: out-of-range JointIndex"), ScenarioTag, Expected.JointIndex));
			return;
		}
		const FTransform Posed = Hierarchy->GetLocalTransform(Fixture.BoneKeys[Expected.JointIndex]);
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
	// Seed sentinel + raw-control inputs onto a fresh fixture and run the unit. Returns
	// a snapshot of the resulting URigHierarchy joint transforms and curves.
	struct FRunSnapshot
	{
		TArray<FTransform> BoneTransforms;
		TMap<FName, float> Curves;
	};

	static FRunSnapshot EvaluateWithSingleControl(FAutomationTestBase& Test, int32 ControlIndex, float Value)
	{
		FRunSnapshot Out;
		FRigUnitFixture Fixture;
		if (!Fixture.Setup(Test, /*bUseLegacyDNA=*/false))
		{
			return Out;
		}

		URigHierarchy* H = Fixture.Hierarchy.Get();
		SeedAllRawControlsZero(H, Fixture);
		if (ControlIndex != -1)
		{
			SetRawControlCurve(H, Fixture.RawControlNames[ControlIndex], Value);
		}
		FRigUnit_RigLogic Unit;
		Fixture.SeedSentinelHierarchy();
		Fixture.Run(Unit);
		Out.BoneTransforms.Reserve(Fixture.BoneKeys.Num());
		for (const FRigElementKey& Key : Fixture.BoneKeys)
		{
			Out.BoneTransforms.Add(H->GetLocalTransform(Key));
		}
		Out.Curves = SnapshotCurves(H);
		return Out;
	}

	// Returns true if two snapshots have matching joint transforms AND matching values for
	// the curves the RigUnit itself emits (morph targets and animated maps). Input control
	// curves (R.A, R.B, ...) leak through the URigHierarchy as-is, so comparing them across two
	// different input scenarios would always fail. We only care about derived outputs.
	static bool DerivedOutputsMatch(FAutomationTestBase& Test, const FRigUnitFixture& Fixture, const FRunSnapshot& A, const FRunSnapshot& B, const TCHAR* Label)
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

		// Curves: compare only the RigUnit-emitted ones (morph + anim-map). Input control
		// curves and any pre-existing hierarchy curves are ignored.
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
}  // namespace UE::RigLogic::Tests::RigUnit

// EditorContext + EngineFilter lets these run from the editor's Session Frontend -> Automation panel as well as from `RunTests RigLogic`.
namespace
{
	constexpr EAutomationTestFlags GRigUnitTestFlags =	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

// =============================================================================
// CacheBones - DNA resolution paths
// =============================================================================

namespace UE::RigLogic::Tests::RigUnit
{
	// Shared body for the two CacheBones tests below. Both DNA-attachment paths
	// (modern UDNAAssetUserData and legacy UDNAAsset) must produce identical
	// observable output: at least one DNA joint written, plus every morph curve
	// the DNA declares at LOD 0 present on the output pose.
	static void RunCacheBonesResolutionTest(FAutomationTestBase& Test, bool bUseLegacyDNA)
	{
		FRigUnitFixture Fixture;
		if (!Fixture.Setup(Test, bUseLegacyDNA))
		{
			return;
		}

		FRigUnit_RigLogic Unit;
		Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

		// At least one DNA joint should be written; if none are, CacheBones never resolved
		// LocalRigRuntimeContext and Execute early-returned.
		Test.TestFalse(TEXT("Some DNA joints should be written by Execute"), Fixture.CollectMovedJoints().IsEmpty());

		// Every morph curve declared by the DNA at LOD0 should be present on the output.
		const TMap<FName, float> Curves = SnapshotCurves(Fixture.Hierarchy.Get());
		for (const FName& Expected : Fixture.ExpectedMorphCurveNames)
		{
			Test.TestTrue(*FString::Printf(TEXT("Morph curve missing: %s"), *Expected.ToString()), Curves.Contains(Expected));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_CacheBones_ResolvesDNAFromAssetUserData, "RigLogic.RigLogicModule.RigUnit_RigLogic.CacheBones.ResolvesDNAFromAssetUserData", GRigUnitTestFlags)
bool FRigUnit_RigLogic_CacheBones_ResolvesDNAFromAssetUserData::RunTest(const FString&)
{
	UE::RigLogic::Tests::RigUnit::RunCacheBonesResolutionTest(*this, /*bUseLegacyDNA=*/false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_CacheBones_ResolvesDNAFromLegacyAssetUserData, "RigLogic.RigLogicModule.RigUnit_RigLogic.CacheBones.ResolvesDNAFromLegacyAssetUserData", GRigUnitTestFlags)
bool FRigUnit_RigLogic_CacheBones_ResolvesDNAFromLegacyAssetUserData::RunTest(const FString&)
{
	UE::RigLogic::Tests::RigUnit::RunCacheBonesResolutionTest(*this, /*bUseLegacyDNA=*/true);
	return true;
}

// =============================================================================
// Evaluate - joint transforms written
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Evaluate_WritesNoNaN, "RigLogic.RigLogicModule.RigUnit_RigLogic.Evaluate.WritesNoNaN", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Evaluate_WritesNoNaN::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	FRigUnitFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	for (const FName& ControlName : Fixture.RawControlNames)
	{
		SetRawControlCurve(H, ControlName, 1.0e6f);
	}
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	bool bAnyNaN = false;
	H->ForEach<FRigCurveElement>([&bAnyNaN](FRigCurveElement* Curve) -> bool
	{
		if (FMath::IsNaN(Curve->Get()))
		{
			bAnyNaN = true;
		}
		return true;
	});
	TestFalse(TEXT("No output curve should be NaN"), bAnyNaN);

	H->ForEach<FRigBoneElement>([&bAnyNaN, H](FRigBoneElement* Bone) -> bool
	{
		if (H->GetLocalTransform(Bone->GetIndex()).ContainsNaN())
		{
			bAnyNaN = true;
		}
		return true;
	});
	TestFalse(TEXT("No bone transform should contain NaN"), bAnyNaN);
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

namespace UE::RigLogic::Tests::RigUnit::Fixtures
{
	// Scenario: all raw controls = 0. RigLogic writes each variable joint's (converted)
	// neutral pose. Translation matches the DNA's neutralJointTranslations after the
	// X/Y flip; rotation is the DNA's neutral Euler composed through the coord-system
	// pipeline; scale is identity (DNA has no neutralJointScales array).
	//
	// Joints {1, 3, 8} are not written by RigLogic at LOD 0. Since we seed every bone
	// with a sentinel transform before Execute, those joints retain the sentinel; the
	// expected values for those slots reproduce GetSentinelTransform's contents.
	static const UE::RigLogic::Tests::RigUnit::FExpectedJoint ExpectedJointsAtZero[] =
	{
		{ 0, FVector(-1.0000000f, -1.0000000f, 1.0000000f), FQuat(-0.1675188f, -0.5709414f, 0.1675187f, 0.7860665f), FVector(1.0f, 1.0f, 1.0f) },
		{ 2, FVector(-3.0000000f, -3.0000000f, 3.0000000f), FQuat(0.0653920f, -0.0753745f, -0.0653920f, 0.9928575f), FVector(1.0f, 1.0f, 1.0f) },
		{ 4, FVector(-5.0000000f, -5.0000000f, 5.0000000f), FQuat(0.6710627f, 0.0971734f, -0.6710627f, 0.2998449f), FVector(1.0f, 1.0f, 1.0f) },
		{ 5, FVector(-6.0000000f, -6.0000000f, 6.0000000f), FQuat(0.1580251f, 0.1185940f, -0.1580252f, 0.9674665f), FVector(1.0f, 1.0f, 1.0f) },
		{ 6, FVector(-7.0000000f, -7.0000000f, 7.0000000f), FQuat(-0.1923898f, -0.4228496f, 0.1923897f, 0.8643902f), FVector(1.0f, 1.0f, 1.0f) },
		{ 7, FVector(-8.0000000f, -8.0000000f, 8.0000000f), FQuat(0.0510307f, -0.6977183f, -0.0510307f, 0.7127279f), FVector(1.0f, 1.0f, 1.0f) },
		{ 1, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
		{ 3, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
		{ 8, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Fixtures_AnimMapCurvesAtZero, "RigLogic.RigLogicModule.RigUnit_RigLogic.Fixtures.AnimMapCurvesAtZero", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Fixtures_AnimMapCurvesAtZero::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	FRigUnitFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	const TMap<FName, float> Curves = SnapshotCurves(H);
	for (const TPair<const TCHAR*, float>& E : Fixtures::ExpectedAnimMapCurvesAtZero)
	{
		const float* Actual = Curves.Find(FName(E.Key));
		if (Actual == nullptr)
		{
			AddError(FString::Printf(TEXT("Animated-map curve missing: %s"), E.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Anim-map curve %s at zero input"), E.Key), *Actual, E.Value, 1.e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Fixtures_BlendShapeCurvesAtZero, "RigLogic.RigLogicModule.RigUnit_RigLogic.Fixtures.BlendShapeCurvesAtZero", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Fixtures_BlendShapeCurvesAtZero::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	// At zero input, every blend-shape output is zero. Blend-shape outputs are
	// a direct selection from raw-control + PSD inputs (no constant offset),
	// so all-zero input -> all-zero output. Curve names in the output
	// are <mesh>__<blendshape> per FRigUnit_RigLogic_Data::MapMorphTargets.
	FRigUnitFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	const TMap<FName, float> Curves = SnapshotCurves(H);
	for (const FName& Name : Fixture.ExpectedMorphCurveNames)
	{
		const float* Actual = Curves.Find(Name);
		if (Actual == nullptr)
		{
			AddError(FString::Printf(TEXT("Morph curve missing: %s"), *Name.ToString()));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Morph curve %s should be zero at zero input"), *Name.ToString()), *Actual, 0.f, 1.e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Fixtures_JointsAtZero, "RigLogic.RigLogicModule.RigUnit_RigLogic.Fixtures.JointsAtZero", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Fixtures_JointsAtZero::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	// At zero input, every joint group's delta is zero, so each variable joint's
	// final transform equals its DNA-declared neutral. The expected values are
	// captured in ExpectedJointsAtZero (translation after the X/Y coord flip,
	// rotation as the DNA's neutral Euler composed through RigLogic's pipeline,
	// and identity scale).
	//
	// Test DNA's joint groups touch joints {0, 2, 4, 5, 6, 7} at LOD 0; joints
	// {1, 3, 8} are not written by UpdateJoints, and instead retain the sentinel
	// transform we seed every bone with before Execute.
	FRigUnitFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	for (const FExpectedJoint& Expected : Fixtures::ExpectedJointsAtZero)
	{
		AssertJoint(*this, H, Fixture, Expected, TEXT("Zero"));
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

namespace UE::RigLogic::Tests::RigUnit::Fixtures
{
	// Scenario: R.B (raw control 1) = 1.0, all others 0. Only joint 0 gets a non-zero
	// delta (group 0 col 1: TZ_DNA +0.05, RotEulX +0.4 deg, RotEulZ +0.75 deg).
	static const UE::RigLogic::Tests::RigUnit::FExpectedJoint ExpectedJointsAtRB1[] =
	{
		{ 0, FVector(-1.0000000f, -1.0000000f, 1.0500000f), FQuat(-0.4908471f, -0.5487088f, 0.3415895f, 0.5842123f), FVector(1.0f, 1.0f, 1.0f) },
		{ 2, FVector(-3.0000000f, -3.0000000f, 3.0000000f), FQuat(0.0653920f, -0.0753745f, -0.0653920f, 0.9928575f), FVector(1.0f, 1.0f, 1.0f) },
		{ 4, FVector(-5.0000000f, -5.0000000f, 5.0000000f), FQuat(0.6710627f, 0.0971734f, -0.6710627f, 0.2998449f), FVector(1.0f, 1.0f, 1.0f) },
		{ 5, FVector(-6.0000000f, -6.0000000f, 6.0000000f), FQuat(0.1580251f, 0.1185940f, -0.1580252f, 0.9674665f), FVector(1.0f, 1.0f, 1.0f) },
		{ 6, FVector(-7.0000000f, -7.0000000f, 7.0000000f), FQuat(-0.1923898f, -0.4228496f, 0.1923897f, 0.8643902f), FVector(1.0f, 1.0f, 1.0f) },
		{ 7, FVector(-8.0000000f, -8.0000000f, 8.0000000f), FQuat(0.0510307f, -0.6977183f, -0.0510307f, 0.7127279f), FVector(1.0f, 1.0f, 1.0f) },
		{ 1, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
		{ 3, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
		{ 8, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
	};

	// Blend-shape outputs are a direct selection from raw control inputs by index.
	// inputIndices[k] -> outputs[k]; control[inputIndices[k]] = control[k].
	// With control 1 = 1, only BS channel 1 is non-zero.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Fixtures_SingleControlAtRB1, "RigLogic.RigLogicModule.RigUnit_RigLogic.Fixtures.SingleControlAtRB1", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Fixtures_SingleControlAtRB1::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	// Drives R.B=1.0 and asserts every observable at that input: per-joint
	// translation/rotation/scale plus the full set of morph-target and animated-map
	// curves the RigUnit emits. Single test instead of three so we don't pay the
	// fixture bootstrap cost more than once for the same input scenario.
	FRigUnitFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	SetRawControlCurve(H, Fixture.RawControlNames[1], 1.0f); // R.B = 1.0
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	for (const FExpectedJoint& Expected : Fixtures::ExpectedJointsAtRB1)
	{
		AssertJoint(*this, H, Fixture, Expected, TEXT("RB1"));
	}

	const TMap<FName, float> Curves = SnapshotCurves(H);
	for (const TPair<const TCHAR*, float>& E : Fixtures::ExpectedMorphCurves_RB1)
	{
		const float* Actual = Curves.Find(FName(E.Key));
		if (Actual == nullptr)
		{
			AddError(FString::Printf(TEXT("Morph curve missing: %s"), E.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Morph curve %s value at R.B=1"), E.Key), *Actual, E.Value, 1.e-4f);
	}
	for (const TPair<const TCHAR*, float>& E : Fixtures::ExpectedAnimMapCurves_RB1)
	{
		const float* Actual = Curves.Find(FName(E.Key));
		if (Actual == nullptr)
		{
			AddError(FString::Printf(TEXT("Animated-map curve missing: %s"), E.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Animated-map curve %s value at R.B=1"), E.Key), *Actual, E.Value, 1.e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Fixtures_RawControlScalesLinearly, "RigLogic.RigLogicModule.RigUnit_RigLogic.Fixtures.RawControlScalesLinearly", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Fixtures_RawControlScalesLinearly::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	// R.B=1 produces a non-zero translation delta on joint 0 (TZ_UE = 0.05).
	// At R.B=0.5 it must produce exactly half that delta. Verifies linearity
	// of the joint-group computation in the input control value.
	FTransform J0_Full;
	FTransform J0_Half;
	{
		FRigUnitFixture Fixture;
		if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
		{
			return false;
		}
		URigHierarchy* H = Fixture.Hierarchy.Get();
		SeedAllRawControlsZero(H, Fixture);
		SetRawControlCurve(H, Fixture.RawControlNames[1], 1.0f);
		FRigUnit_RigLogic Unit;
		Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);
		J0_Full = GetBoneLocal(H, Fixture, 0);
	}
	{
		FRigUnitFixture Fixture;
		if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
		{
			return false;
		}
		URigHierarchy* H = Fixture.Hierarchy.Get();
		SeedAllRawControlsZero(H, Fixture);
		SetRawControlCurve(H, Fixture.RawControlNames[1], 0.5f);
		FRigUnit_RigLogic Unit;
		Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);
		J0_Half = GetBoneLocal(H, Fixture, 0);
	}

	const FVector NeutralTUE(-1.f, -1.f, 1.f);
	const FVector FullDelta = J0_Full.GetTranslation() - NeutralTUE;
	const FVector HalfDelta = J0_Half.GetTranslation() - NeutralTUE;
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

namespace UE::RigLogic::Tests::RigUnit::Fixtures
{
	// Scenario: R.A=R.D=R.G=1.0. Activates PSD9 (=0.81) feeding groups 1 and 2; group 0
	// col 0/3/4 active for joint 0; group 3 col 2 active for joints 5 and 7. See the
	// MultiControlPSDActivation test header for the per-group derivation.
	static const UE::RigLogic::Tests::RigUnit::FExpectedJoint ExpectedJointsAtRABDG1[] =
	{
		{ 0, FVector(-1.0000000f, -1.0000000f, 1.3500000f), FQuat(-0.5243699f, -0.5404704f, 0.5859184f, -0.2993780f), FVector(1.0f, 1.0f, 1.0f) },
		{ 2, FVector(-3.0504999f, -3.0000000f, 3.1410000f), FQuat(0.0653920f, -0.0753745f, -0.0653920f, 0.9928575f), FVector(1.0f, 1.0f, 1.0f) },
		{ 4, FVector(-5.2315001f, -5.0000000f, 5.3220000f), FQuat(0.6710627f, 0.0971734f, -0.6710627f, 0.2998449f), FVector(1.0f, 1.0f, 1.0f) },
		{ 5, FVector(-6.4200001f, -6.6399999f, 6.0000000f), FQuat(0.1580251f, 0.1185940f, -0.1580252f, 0.9674665f), FVector(1.0f, 1.0f, 1.0f) },
		{ 6, FVector(-7.0000000f, -7.3807001f, 7.5588999f), FQuat(-0.1923898f, -0.4228496f, 0.1923897f, 0.8643902f), FVector(1.0f, 1.0f, 1.0f) },
		{ 7, FVector(-8.7370996f, -8.0000000f, 8.0000000f), FQuat(0.0510307f, -0.6977183f, -0.0510307f, 0.7127279f), FVector(1.0f, 1.0f, 1.8600000f) },
		{ 1, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
		{ 3, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
		{ 8, FVector(1234.5f, -987.65f, 4242.0f), FQuat(0.7302967f, 0.5477226f, 0.3651484f, 0.1825742f), FVector(2.5f, 3.5f, 4.5f) },
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Fixtures_MultiControlPSDActivation, "RigLogic.RigLogicModule.RigUnit_RigLogic.Fixtures.MultiControlPSDActivation", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Fixtures_MultiControlPSDActivation::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	FRigUnitFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	SetRawControlCurve(H, Fixture.RawControlNames[0], 1.0f); // R.A
	SetRawControlCurve(H, Fixture.RawControlNames[3], 1.0f); // R.D
	SetRawControlCurve(H, Fixture.RawControlNames[6], 1.0f); // R.G
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	// --- Joint observations: translation, rotation, scale (fixture table) ---
	for (const FExpectedJoint& Expected : Fixtures::ExpectedJointsAtRABDG1)
	{
		AssertJoint(*this, H, Fixture, Expected, TEXT("RABDG1"));
	}

	// --- Curve observations: morph + animated map, every name ---
	const TMap<FName, float> Curves = SnapshotCurves(H);
	for (const TPair<const TCHAR*, float>& E : Fixtures::ExpectedMorphCurves_RABDG1)
	{
		const float* Actual = Curves.Find(FName(E.Key));
		if (Actual == nullptr)
		{
			AddError(FString::Printf(TEXT("Morph curve missing: %s"), E.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Morph curve %s at RABDG1"), E.Key), *Actual, E.Value, 1.e-4f);
	}
	for (const TPair<const TCHAR*, float>& E : Fixtures::ExpectedAnimMapCurves_RABDG1)
	{
		const float* Actual = Curves.Find(FName(E.Key));
		if (Actual == nullptr)
		{
			AddError(FString::Printf(TEXT("Animated-map curve missing: %s"), E.Key));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Animated-map curve %s at RABDG1"), E.Key), *Actual, E.Value, 1.e-4f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Fixtures_ConditionalAtNonZeroInput, "RigLogic.RigLogicModule.RigUnit_RigLogic.Fixtures.ConditionalAtNonZeroInput", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Fixtures_ConditionalAtNonZeroInput::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	FRigUnitFixture Fixture;
	if (!Fixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	SetRawControlCurve(H, Fixture.RawControlNames[1], 0.5f); // R.B = 0.5
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	const TMap<FName, float> Curves = SnapshotCurves(H);
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

namespace UE::RigLogic::Tests::RigUnit
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Config_LoadJointsFalseSkipsJoints, "RigLogic.RigLogicModule.RigUnit_RigLogic.Config.LoadJointsFalseSkipsJoints", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Config_LoadJointsFalseSkipsJoints::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	FRigUnitFixture Fixture;
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
		Cfg.LoadJoints = false;
	});

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	SetRawControlCurve(H, Fixture.RawControlNames[0], 1.0f);
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	// With LoadJoints=false the RigUnit must skip UpdateJoints entirely; every
	// hierarchy bone should still hold the sentinel transform we seeded before Execute.
	const TArray<uint16> Moved = Fixture.CollectMovedJoints();
	TestTrue(*FString::Printf(TEXT("LoadJoints=false but %d joints were written"), Moved.Num()), Moved.IsEmpty());

	// Curves must still be emitted (LoadBlendShapes / LoadAnimatedMaps still on).
	const TMap<FName, float> Curves = SnapshotCurves(H);
	TestTrue(TEXT("Morph curves should still be emitted with LoadJoints=false"), Curves.Contains(FName(TEXT("MA__BA"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Config_LoadBlendShapesFalseSkipsMorphs, "RigLogic.RigLogicModule.RigUnit_RigLogic.Config.LoadBlendShapesFalseSkipsMorphs", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Config_LoadBlendShapesFalseSkipsMorphs::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	FRigUnitFixture Fixture;
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

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	SetRawControlCurve(H, Fixture.RawControlNames[0], 1.0f);
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	const TMap<FName, float> Curves = SnapshotCurves(H);
	for (const FName& MorphName : Fixture.ExpectedMorphCurveNames)
	{
		const float* V = Curves.Find(MorphName);
		TestTrue(*FString::Printf(TEXT("Morph curve %s should NOT be emitted with LoadBlendShapes=false"), *MorphName.ToString()), V == nullptr || FMath::IsNearlyZero(*V, 1.e-4f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_Config_LoadAnimatedMapsFalseSkipsAnimMaps, "RigLogic.RigLogicModule.RigUnit_RigLogic.Config.LoadAnimatedMapsFalseSkipsAnimMaps", GRigUnitTestFlags)
bool FRigUnit_RigLogic_Config_LoadAnimatedMapsFalseSkipsAnimMaps::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	FRigUnitFixture Fixture;
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

	URigHierarchy* H = Fixture.Hierarchy.Get();
	SeedAllRawControlsZero(H, Fixture);
	FRigUnit_RigLogic Unit;
	Fixture.SeedSentinelHierarchy();
	Fixture.Run(Unit);

	const TMap<FName, float> Curves = SnapshotCurves(H);
	for (const FName& MapName : Fixture.ExpectedAnimMapCurveNames)
	{
		const float* V = Curves.Find(MapName);
		TestTrue(*FString::Printf(TEXT("Animated-map curve %s should NOT be emitted with LoadAnimatedMaps=false"), *MapName.ToString()), V == nullptr || FMath::IsNearlyZero(*V, 1.e-4f));
	}
	return true;
}

// =============================================================================
// Input curve clamping
//
// FRigUnit_RigLogic_Data::UpdateControlCurves applies FMath::Clamp(value, 0.0, 1.0)
// before writing to the RigInstance's raw control buffer (matching AnimNode behaviour).
// This test asserts that out-of-range input curve values produce the same output as
// their clamped equivalents.
//   R.B = 2.0  must produce identical output to R.B = 1.0
//   R.B = -0.5 must produce identical output to R.B = 0.0  (i.e. the neutral)
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigUnit_RigLogic_InputClamping, "RigLogic.RigLogicModule.RigUnit_RigLogic.InputClamping", GRigUnitTestFlags)
bool FRigUnit_RigLogic_InputClamping::RunTest(const FString&)
{
	using namespace UE::RigLogic::Tests::RigUnit;

	// Reference fixture used solely to read ExpectedMorphCurveNames / ExpectedAnimMapCurveNames
	// in DerivedOutputsMatch. The actual evaluations construct their own fresh fixtures inside
	// EvaluateWithSingleControl.
	FRigUnitFixture RefFixture;
	if (!RefFixture.Setup(*this, /*bUseLegacyDNA=*/false))
	{
		return false;
	}

	const FRunSnapshot OneOutput     = EvaluateWithSingleControl(*this, 1, 1.0f);
	const FRunSnapshot OverOneOutput = EvaluateWithSingleControl(*this, 1, 2.0f);
	DerivedOutputsMatch(*this, RefFixture, OneOutput, OverOneOutput, TEXT("R.B=2.0 should match R.B=1.0"));

	const FRunSnapshot ZeroOutput     = EvaluateWithSingleControl(*this, /*all zero=*/-1, 0.0f);
	const FRunSnapshot NegativeOutput = EvaluateWithSingleControl(*this, 1, -0.5f);
	DerivedOutputsMatch(*this, RefFixture, ZeroOutput, NegativeOutput, TEXT("R.B=-0.5 should match R.B=0"));
	return true;
}
// =============================================================================
// LOD switching - TODO
//
// FRigUnit_RigLogic reads the predicted LOD from
// Data.SkelMeshComponent->GetPredictedLODLevel() each Execute. Driving an
// alternate LOD requires a registered SkeletalMeshComponent so that the LOD
// machinery is wired up; on our unregistered transient component the predicted
// LOD always reads as 0.
//
// At LOD 1 the test DNA reduces variable joints to {0, 2, 6} (groups 1, 2, 3
// have lods=2/2/0 respectively, so groups 0 and 1's first 2 outputs and group
// 2's first 2 outputs survive; group 3 is dropped entirely). A proper LOD test
// would confirm joints {4, 5, 7} are not written and that joint 0 still gets its full delta.
//
// Deferred: low priority since RigLogicLib already covers per-LOD behaviour at the
// engine level.
// =============================================================================

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
