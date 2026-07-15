// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosModularVehicle/ModularVehicleBaseComponent.h"
#include "ChaosModularVehicle/ChaosSimModuleManager.h"
#include "ChaosModularVehicle/VehicleSimBaseComponent.h"
#include "ChaosModularVehicle/ModularVehicleDefaultAsyncInput.h"
#include "ChaosModularVehicle/ModularVehicleAnimationInstance.h"
#include "ChaosModularVehicle/InputProducer.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "Engine/OverlapResult.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/ClusterUnionComponent.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "SimModule/SimModuleTree.h"
#include "SimModule/SimulationModuleBase.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/ClusterUnionPhysicsProxy.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "ChaosModularVehicle/ChaosSimModuleManagerAsyncCallback.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ModularVehicleBaseComponent)

DEFINE_LOG_CATEGORY(LogModularBase);


bool bModularVehicle_SuspensionConstraint_Enabled = true;
FAutoConsoleVariableRef CVarModularVehicleSuspensionConstraintEnabled(TEXT("p.ModularVehicle.SuspensionConstraint.Enabled"), bModularVehicle_SuspensionConstraint_Enabled, TEXT("Enable/Disable suspension constraint falling back to simple forces when constraint disabled (requires restart)."));

UModularVehicleBaseComponent::UModularVehicleBaseComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = false;
	bRequiresControllerForInputs = true;

	LinearDamping = 0.01f;
	AngularDamping = 0.0f;

	bAutoAddComponentsFromWorld = false;
	AutoAddOverlappingBoxSize = FVector(500, 500, 500);
	ClusteringCount = 0;
	DelayClusteringCount = 0;

	SetIsReplicatedByDefault(true);
	bUsingNetworkPhysicsPrediction = Chaos::FPhysicsSolverBase::IsNetworkPhysicsPredictionEnabled();

	SuspensionTraceCollisionResponses = FCollisionResponseContainer::GetDefaultResponseContainer();
	SuspensionTraceCollisionResponses.Vehicle = ECR_Ignore;
	SuspensionCollisionChannel = ECollisionChannel::ECC_WorldDynamic;
	bSuspensionTraceComplex = true;
	bKeepVehicleAwake = true;

	CurrentGear = 0;
	EngineRPM = 0.0f;
	EngineTorque = 0.0f;

	if (bUsingNetworkPhysicsPrediction)
	{
		static const FName NetworkPhysicsComponentName(TEXT("PC_NetworkPhysicsComponent"));

		NetworkPhysicsComponent = CreateDefaultSubobject<UNetworkPhysicsComponent, UNetworkPhysicsComponent>(NetworkPhysicsComponentName);
		NetworkPhysicsComponent->SetNetAddressable();
		NetworkPhysicsComponent->SetIsReplicated(true);
	}

	bIsLocallyControlled = false;

	InputProducerClass = UVehicleDefaultInputProducer::StaticClass();
}

UModularVehicleBaseComponent::~UModularVehicleBaseComponent()
{
}

APlayerController* UModularVehicleBaseComponent::GetPlayerController() const
{
	if (AController* Controller = GetController())
	{
		return Cast<APlayerController>(Controller);
	}

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (AController* Controller = Pawn->GetController())
		{
			return Cast<APlayerController>(Controller);
		}

		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetOwner()))
		{
			return PC;
		}

	}
	return nullptr;
}


bool UModularVehicleBaseComponent::IsLocallyControlled() const
{
	// When using network physics prediction, the NetworkPhysicsComponent is the single source of truth
	// for local control state. It tracks both the force-override (bIsRelayingLocalInputs)
	// and the controller-based check.
	if (bUsingNetworkPhysicsPrediction && NetworkPhysicsComponent)
	{
		return NetworkPhysicsComponent->IsLocallyControlled();
	}

	// Fallback for when NetworkPhysicsComponent is not available
	if (bIsLocallyControlled)
	{
		return true;
	}

	if (APlayerController* PlayerController = GetPlayerController())
	{
		return PlayerController->IsLocalController();
	}
	return false;
}

void UModularVehicleBaseComponent::ProduceInput(int32 PhysicsStep, int32 NumSteps, FModularVehicleAsyncInput* AsyncInput)
{
	if (InputProducer)
	{
		InputProducer->ProduceInput(PhysicsStep, NumSteps, InputNameMap, InputsContainer);
	}

	if (AsyncInput && GetPhysicsProxy())
	{
		AsyncInput->SetVehicle(this);
		AsyncInput->bIsLocallyControlled = IsLocallyControlled();
		CurAsyncInput = AsyncInput;
		AsyncInput->Proxy = GetPhysicsProxy();
		AsyncInput->PhysicsInputs.NetworkInputs.VehicleInputs.KeepAwake = bKeepVehicleAwake;

		AsyncInput->PhysicsInputs.CurrentTimeDilation = 1.0f;
		if (GetWorld())
		{
			if (AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings())
			{
				AsyncInput->PhysicsInputs.CurrentTimeDilation = FMath::Max(WorldSettings->GetEffectiveTimeDilation(), SMALL_NUMBER);
			}
		}

		// All control inputs
		AsyncInput->PhysicsInputs.NetworkInputs.VehicleInputs.Container = InputsContainer;

		FCollisionQueryParams TraceParams(NAME_None, FCollisionQueryParams::GetUnknownStatId(), false, nullptr);
		TraceParams.bReturnPhysicalMaterial = true;	// we need this to get the surface friction coefficient
		TraceParams.AddIgnoredActors(ActorsToIgnore);
		TraceParams.bTraceComplex = bSuspensionTraceComplex;
		AsyncInput->PhysicsInputs.CollisionChannel = SuspensionCollisionChannel;
		AsyncInput->PhysicsInputs.TraceParams = TraceParams;
		AsyncInput->PhysicsInputs.TraceCollisionResponse = SuspensionTraceCollisionResponses;
		AsyncInput->PhysicsInputs.TraceType = TraceType;

		// game state container
		AsyncInput->PhysicsInputs.StateInputs.StateInputContainer = StateInputContainer;
	}
}

void UModularVehicleBaseComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	if (ClusterUnionComponent)
	{
		// piggyback on the Add/Remove component events from the cluster union to add/remove simulation modules
		ClusterUnionComponent->OnComponentAddedNativeEvent.AddUObject(this, &UModularVehicleBaseComponent::AddComponentToSimulation);
		ClusterUnionComponent->OnComponentRemovedNativeEvent.AddUObject(this, &UModularVehicleBaseComponent::RemoveComponentFromSimulation);


		// #TODO: this doesn't appear to be working
		if (bKeepVehicleAwake && ClusterUnionComponent->GetPhysicsProxy())
		{
			if (auto* Particle = ClusterUnionComponent->GetPhysicsProxy()->GetParticle_External())
			{
				Particle->SetSleepType(Chaos::ESleepType::NeverSleep);
			}
		}
	}

	OnModuleAddedNativeEvent.AddUObject(this, &UModularVehicleBaseComponent::OnModuleInitialized);
	OnModuleRemovedNativeEvent.AddUObject(this, &UModularVehicleBaseComponent::OnModuleRemoved);

	// convert the game thread side UVehicleSimBaseComponent(s) to physics thread simulation ISimulationModuleBase(s)
	CreateVehicleSim();

	SetupInputConfiguration();

	if (bUsingNetworkPhysicsPrediction)
	{
		if (NetworkPhysicsComponent)
		{
			// register interface to handle network prediction callbacks
			// #Note: in our case we don't yet know what the replication data will be since the modules are built after this point at runtime
			FScopedModuleInputInitializer SetSetup(InputConfig);
			NetworkPhysicsComponent->CreateDataHistory<FPhysicsModularVehicleTraits>(this);

			if (bIsLocallyControlled)
			{
				NetworkPhysicsComponent->SetIsRelayingLocalInputs(true);
			}

		}
	}

	NextConstructionIndex = 0;

	ActorsToIgnore.Add(GetOwner()); // vehicle ignore self in scene query

}

void UModularVehicleBaseComponent::OnDestroyPhysicsState()
{
	Super::OnDestroyPhysicsState();

	if (ClusterUnionComponent)
	{
		ClusterUnionComponent->OnComponentAddedNativeEvent.RemoveAll(this);
		ClusterUnionComponent->OnComponentRemovedNativeEvent.RemoveAll(this);
	}

	OnModuleAddedNativeEvent.RemoveAll(this);
	OnModuleRemovedNativeEvent.RemoveAll(this);

	DestroyVehicleSim();

	if (bUsingNetworkPhysicsPrediction && NetworkPhysicsComponent)
	{
		NetworkPhysicsComponent->RemoveDataHistory();
	}

	CachedPhysicsProxy = nullptr;
}

int UModularVehicleBaseComponent::GenerateNewGuid()
{
	static int Val = 0;
	return Val++;
}

void UModularVehicleBaseComponent::CacheRootPhysicsObject(IPhysicsProxyBase* Proxy)
{
	Chaos::EnsureIsInGameThreadContext();
	using namespace Chaos;
	RootPhysicsObject = nullptr;
	
	if (Proxy == nullptr)
	{
		return;
	}

	switch (Proxy->GetType())
	{
		case EPhysicsProxyType::ClusterUnionProxy:
		{
			if (FClusterUnionPhysicsProxy* CUProxy = static_cast<FClusterUnionPhysicsProxy*>(Proxy))
			{
				RootPhysicsObject = CUProxy->GetPhysicsObjectHandle();
			}
		}
		break;

		case EPhysicsProxyType::SingleParticleProxy:
		{
			if (FSingleParticlePhysicsProxy* ParticleProxy = static_cast<FSingleParticlePhysicsProxy*>(Proxy))
			{
				RootPhysicsObject = ParticleProxy->GetPhysicsObject();
			}
		}
		break;

		default:
		{
			UE_LOGF(LogModularBase, Error, "Unsupported Particle type");
		}
		break;
	}
}

void UModularVehicleBaseComponent::CreateAssociatedSimComponents(USceneComponent* ParentComponent, USceneComponent* AttachedComponent, int ParentIndex, int InTransformIndex, Chaos::FSimTreeUpdates& TreeUpdatesOut)
{
	using namespace Chaos;
	if (AttachedComponent == nullptr || ClusterUnionComponent == nullptr)
	{
		return;
	}

	UE_LOGF(LogModularBase, Log, "CreateAssociatedSimComponents Attaching %ls, TransformIndex %d", *AttachedComponent->GetName(), InTransformIndex);

	TArray<Chaos::FClusterUnionChildData> ChildParticles = ClusterUnionComponent->GetPhysicsProxy()->GetSyncedData_External().ChildParticles;

	if (InTransformIndex >= ChildParticles.Num())
	{
		// #TODO: look into the underlying cause of this indexing issue, possibly not an easy fix
		ensureMsgf(false, TEXT("Unexpected state - likely due to adding and removing components on same frame leading to an array indexing issue") );
		return;
	}

	int TransformIndex = InTransformIndex;
	if (RootPhysicsObject == nullptr)
	{
		CacheRootPhysicsObject(GetPhysicsProxy());
	}


	if (IVehicleSimBaseComponentInterface* ComponentInterface = Cast<IVehicleSimBaseComponentInterface>(AttachedComponent))
	{

		// if we find the parent is a skeletal mesh then the indices are ...
		if (ParentComponent)
		{
			if (USkeletalMeshComponent* SKMComponent = Cast<USkeletalMeshComponent>(ParentComponent))
			{
				const FName BoneName = ComponentInterface->GetBoneName();

				int32 BodyBoneIndex = INDEX_NONE;

				for (int32 ArrayIndex = 0; ArrayIndex < SKMComponent->Bodies.Num(); ArrayIndex++)
				{
					// Get body index of bone
					if (SKMComponent->Bodies[ArrayIndex] && SKMComponent->Bodies[ArrayIndex]->IsValidBodyInstance())
					{
						const FName BodyName = SKMComponent->Bodies[ArrayIndex]->GetBodySetup()->BoneName;

						if (BodyName == BoneName)
						{
							BodyBoneIndex = ArrayIndex;
							break;
						}

					}
				}

				if (BodyBoneIndex != INDEX_NONE)
				{
					TransformIndex = InTransformIndex + BodyBoneIndex;
				}

			}
			else
			{
				TransformIndex = InTransformIndex;
			}

		}

		ensure(TransformIndex < ChildParticles.Num());

		if (Chaos::ISimulationModuleBase* NewModule = ComponentInterface->CreateNewCoreModule())
		{
			FTransform ClusterUnionComponentTransform = ClusterUnionComponent->GetComponentTransform();
			FTransform ComponentTransform = AttachedComponent->GetComponentTransform().GetRelativeTransform(ClusterUnionComponentTransform);
			int TreeIndex = INDEX_NONE;
			
			TUniquePtr<Chaos::FSimModuleTree>& SimModuleTree = VehicleSimulationPT->AccessSimComponentTree();

	#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			FString DebugString;
			NewModule->GetDebugString(DebugString);
			UE_LOGF(LogModularBase, Log, "CreateAssociatedSimComponents Found Sim Component %ls (%ls)", *NewModule->GetDebugName(), *DebugString);
	#endif

			FVector LocalOffset(0, 0, 0);
			FVector LocalOffsetCOM(0, 0, 0);
			FTransform PhysicsTransform = FTransform::Identity;

			FTransform InitialTransform = ChildParticles[TransformIndex].ChildToParent;
			InitialTransform.SetLocation(InitialTransform.GetLocation());
			NewModule->SetSimModuleTree(SimModuleTree.Get());
			TreeIndex = TreeUpdatesOut.AddNodeBelow(ParentIndex, NewModule);
			NewModule->SetTransformIndex(TransformIndex);
			NewModule->SetParticleIndex(ChildParticles[TransformIndex].ParticleIdx);

			NewModule->SetIntactTransform(FTransform::Identity);
			NewModule->SetClusteredTransform(InitialTransform);
			NewModule->SetClustered(true);

			FVehicleComponentData& ComponentData = ComponentToPhysicsObjects.FindOrAdd(AttachedComponent);
			ComponentData.Guid = GenerateNewGuid();
			NewModule->SetGuid(ComponentData.Guid);

			PhysicsGuidToComponent.Add(ComponentData.Guid, AttachedComponent);

			NewModule->SetInitialParticleTransform(InitialTransform);
			NewModule->SetComponentTransform(ComponentTransform);

			FTransform ClusterredTransform(FQuat::Identity, InitialTransform.GetLocation());
			NewModule->SetClusteredTransform(ClusterredTransform);

			const bool bIsAnimationEnabled = ComponentInterface->GetAnimationEnabled();
			const FName ComponentBoneName = ComponentInterface->GetBoneName();
			// Now always using ModuleAnimationSetups even when there is no bone specified. ModuleAnimationSetups can be used to drive
			// transforms in components, not just skeletal mesh bones.
			if (bIsAnimationEnabled)
			{
				// if bone already exists then use that (separate wheel and suspension modules can share same bone)

				int FoundIndex = INDEX_NONE;
				if (ComponentBoneName != NAME_None)
				{
					for (int I = 0; I < ModuleAnimationSetups.Num(); I++)
					{
						if (ModuleAnimationSetups[I].BoneName == ComponentBoneName)
						{
							FoundIndex = I;
							break;
						}
					}
				}

				const FVector& ComponentAnimationOffset = ComponentInterface->GetAnimationOffset();
				if (FoundIndex != INDEX_NONE)
				{
					NewModule->SetAnimationData(ComponentBoneName, ComponentAnimationOffset, FoundIndex);
				}
				else
				{
					NewModule->SetAnimationData(ComponentBoneName, ComponentAnimationOffset, ModuleAnimationSetups.Num());
					FModuleAnimationSetup AnimSetup(NewModule->GetBoneName(), TransformIndex);
					ModuleAnimationSetups.Add(AnimSetup);
				}
			}

			// store the tree index in the original sim component
			ComponentInterface->SetTreeIndex(TreeIndex);
			ParentIndex = TreeIndex;

			if (GetPhysicsProxy())
			{
				NewModule->OnConstruction_External(RootPhysicsObject);
			}
		}
	}

	// get this components children, not all children
	TArray<USceneComponent*> Children;
	AttachedComponent->GetChildrenComponents(false, Children);

	// recurse down tree, converting all SimComponents to proper simulation modules
	for (USceneComponent* Child : Children)
	{
		if (IVehicleSimBaseComponentInterface* ChildSimComponent = Cast<IVehicleSimBaseComponentInterface>(Child))
		{
			CreateAssociatedSimComponents(AttachedComponent, Child, ParentIndex, TransformIndex, TreeUpdatesOut);
		}
	}

	UpdatePhysicalProperties();
}

/**
 * Helper function to find the visual component that should be animated.
 * Walks up the component tree from the sim component to find the first
 * StaticMeshComponent or other visual component.
 * To be used at initialisation and not every frame for perf reasons.
 *
 * @param SimComponent - The sim component to start searching from
 * @return The visual component to animate, or nullptr if not found
 */
static USceneComponent* FindVisualComponentToAnimate(USceneComponent* SimComponent)
{
	if (!SimComponent)
	{
		return nullptr;
	}

	USceneComponent* Current = SimComponent->GetAttachParent();

	while (Current)
	{
		// Check if this is a visual component we want to animate

		// StaticMeshComponent
		if (UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Current))
		{
			return StaticMesh;
		}

		// SkeletalMeshComponent
		if (USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(Current))
		{
			// Animate skeletal meshes that are not the root component
			return SkeletalMesh;
		}

		// Check if this is a generic scene component with a visual (child static mesh)      
		// If we reach a scene component that has NO static/skeletal mesh children,
		// we might want to animate it anyway (for hierarchical setups)

		// Move up one level
		Current = Current->GetAttachParent();
	}

	return nullptr;
}

/**
* This code path is used only when we are not using the cluster union as a root component for part assembly
* i.e. when Skeletal Mesh or Static Meshes are the only physics representation
*/
void UModularVehicleBaseComponent::CreateIndependentSimComponents(USceneComponent* RootComponent, USceneComponent* AttachedComponent, int ParentIndex, int TransformIndex, Chaos::FSimTreeUpdates& TreeUpdatesOut)
{
	using namespace Chaos;
	if (AttachedComponent == nullptr || RootComponent == nullptr)
	{
		return;
	}

	if (RootPhysicsObject == nullptr)
	{
		CacheRootPhysicsObject(GetPhysicsProxy());
	}

	UE_LOGF(LogModularBase, Log, "CreateIndependentSimComponents Attaching %ls, TransformIndex %d", *AttachedComponent->GetName(), TransformIndex);

	if (IVehicleSimBaseComponentInterface* ComponentInterface = Cast<IVehicleSimBaseComponentInterface>(AttachedComponent))
	{
		if (Chaos::ISimulationModuleBase* NewModule = ComponentInterface->CreateNewCoreModule())
		{
			FTransform RootComponentTransform = RootComponent->GetComponentTransform();
			FTransform ComponentTransform = AttachedComponent->GetComponentTransform().GetRelativeTransform(RootComponentTransform);

			// Get the static or skeletal mesh parent of the simulation component
			USceneComponent* VisualComponent = FindVisualComponentToAnimate(AttachedComponent);
			if (VisualComponent && (VisualComponent != RootComponent))
			{
				ComponentTransform = VisualComponent->GetComponentTransform().GetRelativeTransform(RootComponentTransform);
			}

			const FName BoneName = ComponentInterface->GetBoneName();

			// Try to get bone transform from RootComponent (if it's a skeletal mesh)
			// This handles the original case where all modules are part of a single skeletal mesh
			if (USkeletalMeshComponent* RootSKMComponent = Cast<USkeletalMeshComponent>(RootComponent))
			{
				if (BoneName != NAME_None)
				{
					for (int32 ArrayIndex = 0; ArrayIndex < RootSKMComponent->Bodies.Num(); ArrayIndex++)
					{
						if (FBodyInstance* BodyInstance = RootSKMComponent->Bodies[ArrayIndex])
						{
							if (UBodySetup* BodySetup = BodyInstance->GetBodySetup())
							{
								const FName BodyName = BodySetup->BoneName;

								if (BodyName == BoneName)
								{
									// Use the skeletal mesh bone transform rather than the physics body's
									// current world transform. GetUnrealWorldTransform() can return a
									// gravity-drifted position if physics has already ticked before BeginPlay,
									// which differs between client and server. The bone transform from the
									// SKM pose is driven by the reference skeleton and is identical on all roles.
									const int32 BoneIndex = RootSKMComponent->GetBoneIndex(BoneName);
									if (BoneIndex != INDEX_NONE)
									{
										ComponentTransform = RootSKMComponent->GetBoneTransform(BoneIndex).GetRelativeTransform(RootComponentTransform);
									}

									BodyInstance->bSimulatePhysics = false;
									RootSKMComponent->bBlendPhysics = false;

									UE_LOGF(LogModularBase, Log, "  -> Found bone '%ls' in ROOT skeletal mesh '%ls'", *BoneName.ToString(), *RootSKMComponent->GetName());
									break;
								}
							}
						}
					}
				}
			}

			int TreeIndex = INDEX_NONE;
			TUniquePtr<Chaos::FSimModuleTree>& SimModuleTree = VehicleSimulationPT->AccessSimComponentTree();

	#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			FString DebugString;
			NewModule->GetDebugString(DebugString);
			UE_LOGF(LogModularBase, Log, "CreateIndependentSimComponents Found Sim Component %ls (%ls)", *NewModule->GetDebugName(), *DebugString);
	#endif

			FVector LocalOffset(0, 0, 0);
			FVector LocalOffsetCOM(0, 0, 0);
			FTransform PhysicsTransform = FTransform::Identity;

			FTransform InitialTransform = ComponentTransform;
			InitialTransform.SetLocation(InitialTransform.GetLocation());
			NewModule->SetSimModuleTree(SimModuleTree.Get());
			TreeIndex = TreeUpdatesOut.AddNodeBelow(ParentIndex, NewModule);
			NewModule->SetTransformIndex(TransformIndex);
			NewModule->SetParticleIndex(Chaos::FUniqueIdx());

			NewModule->SetIntactTransform(FTransform::Identity);
			NewModule->SetClusteredTransform(InitialTransform);
			NewModule->SetClustered(true);

			FVehicleComponentData& ComponentData = ComponentToPhysicsObjects.FindOrAdd(AttachedComponent);
			ComponentData.Guid = GenerateNewGuid();
			NewModule->SetGuid(ComponentData.Guid);

			// Where we want to animate the component transform directly and not using an AnimationInstance
			if (VisualComponent != RootComponent)
			{
				ComponentData.VisualComponentToAnimate = VisualComponent;
			}

			PhysicsGuidToComponent.Add(ComponentData.Guid, AttachedComponent);

			NewModule->SetInitialParticleTransform(InitialTransform);
			NewModule->SetComponentTransform(ComponentTransform);

			FTransform ClusterredTransform(FQuat::Identity, InitialTransform.GetLocation());
			NewModule->SetClusteredTransform(ClusterredTransform);

			const bool bIsAnimationEnabled = ComponentInterface->GetAnimationEnabled();
			const FName ComponentBoneName = ComponentInterface->GetBoneName();
			// Changed behaviour - now always using ModuleAnimationSetups even when there is no bone setup. ModuleAnimationSetups is used to drive
			// transforms in components, not just skeletal mesh bones.
			if (bIsAnimationEnabled)
			{
				// if bone already exists then use that (separate wheel and suspension modules can share same bone)

				int FoundIndex = INDEX_NONE;
				if (ComponentBoneName != NAME_None)
				{
					for (int I = 0; I < ModuleAnimationSetups.Num(); I++)
					{
						if (ModuleAnimationSetups[I].BoneName == ComponentBoneName)
						{
							FoundIndex = I;
							break;
						}
					}
				}

				const FVector& ComponentAnimationOffset = ComponentInterface->GetAnimationOffset();
				if (FoundIndex != INDEX_NONE)
				{
					NewModule->SetAnimationData(ComponentBoneName, ComponentAnimationOffset, FoundIndex);
				}
				else
				{
					NewModule->SetAnimationData(ComponentBoneName, ComponentAnimationOffset, ModuleAnimationSetups.Num());
					FModuleAnimationSetup AnimSetup(NewModule->GetBoneName(), TransformIndex);

					// Store initial transform for animation
					// This ensures that animated static meshes can calculate deltas from their initial pose.
					AnimSetup.ModuleGUID = ComponentData.Guid;  // Link AnimSetup to module
					AnimSetup.InitialRotOffset = ComponentTransform.GetRotation();
					AnimSetup.InitialLocOffset = ComponentTransform.GetTranslation();

					ModuleAnimationSetups.Add(AnimSetup);
				}
			}

			// store the tree index in the original sim component
			ComponentInterface->SetTreeIndex(TreeIndex);
			ParentIndex = TreeIndex;

			if (GetPhysicsProxy())
			{
				NewModule->OnConstruction_External(RootPhysicsObject);
			}
		}
	}

	// get this components children, not all children
	TArray<USceneComponent*> Children;
	AttachedComponent->GetChildrenComponents(false, Children);

	// recurse down tree, converting all SimComponents to proper simulation modules
	for (USceneComponent* Child : Children)
	{
		if (USceneComponent* ChildSimComponent = Cast<USceneComponent>(Child))
		{
			CreateIndependentSimComponents(RootComponent, Child, ParentIndex, TransformIndex, TreeUpdatesOut);
		}
	}

	UpdatePhysicalProperties();
}


int UModularVehicleBaseComponent::AddSimModule(Chaos::ISimulationModuleBase* NewModule, const FTransform& LocalTransform, int ParentIndex, int TransformIndex)
{
	if (VehicleSimulationPT == nullptr)
	{
		ensureMsgf(false, TEXT("VehicleSimulationPT ptr is null, This is unexpected behaviour."));
		return INDEX_NONE;
	}

	TUniquePtr<Chaos::FSimModuleTree>& SimModuleTree = VehicleSimulationPT->AccessSimComponentTree();

	FVector LocalOffset(0, 0, 0);
	FVector LocalOffsetCOM(0, 0, 0);
	FTransform PhysicsTransform = FTransform::Identity;

	FTransform InitialTransform = LocalTransform;
	InitialTransform.SetLocation(InitialTransform.GetLocation());
	NewModule->SetSimModuleTree(SimModuleTree.Get());
	int TreeIndex = StoredTreeUpdates.AddNodeBelow(ParentIndex, NewModule);

	NewModule->SetGuid(GenerateNewGuid());

	NewModule->SetTransformIndex(TransformIndex);
	NewModule->SetParticleIndex(Chaos::FUniqueIdx());

	NewModule->SetIntactTransform(FTransform::Identity);
	NewModule->SetClusteredTransform(InitialTransform);
	NewModule->SetClustered(true);

	NewModule->SetInitialParticleTransform(InitialTransform);
	NewModule->SetComponentTransform(LocalTransform);

	FTransform ClusterredTransform(FQuat::Identity, InitialTransform.GetLocation());
	NewModule->SetClusteredTransform(ClusterredTransform);

	NewModule->SetAnimationData(NewModule->GetBoneName(), NewModule->GetAnimationOffset(), ModuleAnimationSetups.Num());
	FModuleAnimationSetup AnimSetup(NewModule->GetBoneName(), TransformIndex, NewModule->GetGuid());
	AnimSetup.InitialRotOffset = InitialTransform.GetRotation();
	AnimSetup.InitialLocOffset = InitialTransform.GetTranslation();
	ModuleAnimationSetups.Add(AnimSetup);

	return TreeIndex;
}

void UModularVehicleBaseComponent::RemoveSimModule(int ModuleGuid)
{
	StoredTreeUpdates.RemoveNode(ModuleGuid);
}

void UModularVehicleBaseComponent::FinalizeModuleUpdates()
{
	ActionTreeUpdates(&StoredTreeUpdates);
	StoredTreeUpdates.ClearUpdates();

	if (RootPhysicsObject == nullptr)
	{
		ensure(GetPhysicsProxy() != nullptr); // should never happen
		CacheRootPhysicsObject(GetPhysicsProxy()); //#TODO where to put / refresh?
	}

	UpdatePhysicalProperties();
}


void UModularVehicleBaseComponent::UpdatePhysicalProperties()
{
	if (ClusterUnionComponent)
	{
		if (Chaos::FClusterUnionPhysicsProxy* Proxy = ClusterUnionComponent->GetPhysicsProxy())
		{
			Chaos::FPBDRigidsSolver* Solver = Proxy->GetSolver<Chaos::FPBDRigidsSolver>();
			Solver->EnqueueCommandImmediate([Proxy, this]() mutable
				{
					Proxy->GetParticle_Internal()->SetLinearEtherDrag(LinearDamping);
					Proxy->GetParticle_Internal()->SetAngularEtherDrag(AngularDamping);
				});
		}
	}

	if (RootPhysicsObject)
	{
		TArray<Chaos::FPhysicsObjectHandle> PhysicsObjects{ RootPhysicsObject };
		FLockedWritePhysicsObjectExternalInterface Interface = FPhysicsObjectExternalInterface::LockWrite(PhysicsObjects);
		Interface->SetLinearEtherDrag(PhysicsObjects, LinearDamping);
		Interface->SetAngularEtherDrag(PhysicsObjects, AngularDamping);
	}
}

void UModularVehicleBaseComponent::BeginPlay()
{
	Super::BeginPlay();

	// ClusterUnion paths only apply on the server - the client is setup from OnComponentAddedNativeEvent callback
	const bool bHasAuthority = GetOwner()->HasAuthority();
	if (bHasAuthority)
	{
		if (bAutoAddComponentsFromWorld && (DelayClusteringCount == 0))
		{
			AddOverlappingComponentsToCluster();
		}
		else if (ClusterUnionComponent)
		{
			// add components to cluster union
			AddComponentsFromOwnedActor();
		}
	}

	// Independent single skeletal mesh path runs on all net roles
	if (!bAutoAddComponentsFromWorld && !ClusterUnionComponent)
	{
		if (VehicleSimulationPT)
		{
			if (AActor* Owner = GetOwner())
			{
				if (USceneComponent* RootBodyComponent = Owner->GetRootComponent())
				{
					if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(RootBodyComponent))
					{
						if (FBodyInstance* BI = Prim->GetBodyInstance())
						{
							// Get physics proxy from root body
							IPhysicsProxyBase* Proxy = BI->GetPhysicsActorHandle();
							CacheRootPhysicsObject(Proxy);
						}

						int ParentID = FindParentsLastSimComponent(RootBodyComponent);

						Chaos::FSimTreeUpdates LatestTreeUpdates;
						CreateIndependentSimComponents(RootBodyComponent, RootBodyComponent, ParentID, NextTransformIndex, LatestTreeUpdates);

						Chaos::FSimTreeUpdates* NextTreeUpdates = &LatestTreeUpdates;
						ActionTreeUpdates(NextTreeUpdates);
					}
				}
			}
		}
	}

}

void UModularVehicleBaseComponent::SetupInputConfiguration()
{
	// control input setup - unfortunately can't do this in OnCreatePhysics since RootComponent->GetChildrenComponents will not work 
	// at that time and AssimilateComponentInputs will not find any controls in the component hierarchy
	TArray<FModuleInputSetup> CombinedInputConfiguration;
	AssimilateComponentInputs(CombinedInputConfiguration);

	if (!InputProducer && InputProducerClass)
	{
		InputProducer = NewObject<UVehicleInputProducerBase>(this, InputProducerClass);
	}

	if (InputProducer)
	{
		InputProducer->InitializeContainer(CombinedInputConfiguration, InputNameMap, InputQuantizationType);
		if (InputProducer->GetTestInputBuffer())
		{
			// deliberate deep copy rather than copy pointer
			VehicleSimulationPT->SetTestInputBuffer(*InputProducer->GetTestInputBuffer(), InputProducer->IsLoopingTestInputBuffer(), InputProducer->GetTestInputStartFrame());
		}
	}

	InputsContainer.Initialize(CombinedInputConfiguration, InputNameMap);
	StateInputContainer.Initialize(StateInputConfiguration, StateNameMap);

	if (!bUsingNetworkPhysicsPrediction)
	{
		ReplicatedState.Container = InputsContainer;
	}

	if (VehicleSimulationPT)
	{
		VehicleSimulationPT->SetInputQuantizationType(InputQuantizationType);
		VehicleSimulationPT->SetInputMappings(InputNameMap);
		VehicleSimulationPT->SetStateMappings(StateNameMap);
	}

}

void UModularVehicleBaseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const bool bHasAuthority = GetOwner()->HasAuthority();
	if (bAutoAddComponentsFromWorld && bHasAuthority && (++ClusteringCount == DelayClusteringCount))
	{
		AddOverlappingComponentsToCluster();
	}

}

void UModularVehicleBaseComponent::CreateOrUpdateComponentAddOrder(USceneComponent* InComponent, int32 InNextConstructionIndex)
{
	if (InComponent)
	{
		for (FConstructionData& Data : ConstructionDatas)
		{
			if (Data.Component == InComponent)
			{
				Data.ConstructionIndex = InNextConstructionIndex;
				return;
			}
		}

		FConstructionData ConstructionData;
		ConstructionData.Component = InComponent;
		ConstructionData.ConstructionIndex = InNextConstructionIndex;
		ConstructionDatas.Add(ConstructionData);
	}
}

int32 UModularVehicleBaseComponent::FindComponentAddOrder(USceneComponent* InComponent)
{
	int32 ComponentAddOrder = INDEX_NONE;

	if (InComponent)
	{
		for (const FConstructionData& Data : ConstructionDatas)
		{
			if (Data.Component == InComponent)
			{
				ComponentAddOrder = Data.ConstructionIndex;
				break;
			}
		}

		if (ComponentAddOrder == INDEX_NONE)
		{
			UE_LOGF(LogModularBase, Warning, "FindComponentAddOrder failed to find component %ls", *InComponent->GetName());
		}
	}

	return ComponentAddOrder;
}

bool UModularVehicleBaseComponent::FindAndRemoveNextPendingUpdate(int32 NextIndex, Chaos::FSimTreeUpdates* OutData)
{
	bool bFound = false;
	for (const TPair<TObjectKey<USceneComponent>, Chaos::FSimTreeUpdates>& Kvp : PendingTreeUpdates)
	{
		if (USceneComponent* Component = Kvp.Key.ResolveObjectPtr())
		{
			if (FindComponentAddOrder(Component) == NextIndex)
			{
				*OutData = PendingTreeUpdates.FindAndRemoveChecked(Component);
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		UE_LOGF(LogModularBase, Warning, "FindAndRemoveNextPendingUpdate failed to find data for index %d", NextIndex);
	}

	return bFound;
}

void UModularVehicleBaseComponent::AddActorsToIgnore(TArray<AActor*>& ActorsIn)
{
	for (AActor* Actor : ActorsIn)
	{
		ActorsToIgnore.AddUnique(Actor);
	}
}

void UModularVehicleBaseComponent::RemoveActorsToIgnore(TArray<AActor*>& ActorsIn)
{
	for (AActor* Actor : ActorsIn)
	{
		ActorsToIgnore.Remove(Actor);
	}
}

void UModularVehicleBaseComponent::PreTickGT(float DeltaTime)
{
	if (PendingTreeUpdates.Num() > 0)
	{
		Chaos::FSimTreeUpdates Updates;
		while (FindAndRemoveNextPendingUpdate(LastComponentAddIndex + 1, &Updates))
		{
			ActionTreeUpdates(&Updates);
			LastComponentAddIndex++;
		}
	}

	// process control inputs and other data
	UpdateState(DeltaTime);

}

void UModularVehicleBaseComponent::UpdateState(float DeltaTime)
{
	// update input values
	bool bProcessLocally = IsLocallyControlled();

	// IsLocallyControlled will fail if the owner is unpossessed (i.e. Controller == nullptr);
	// Should we remove input instead of relying on replicated state in that case?
	if (bProcessLocally && PVehicleOutput)
	{
		if (!bUsingNetworkPhysicsPrediction)
		{
			// Produce input but only for remote server (ServerInputsContainer), not the local client (InputsContainer)
			if (InputProducer)
			{
				FModuleInputContainer ServerInputsContainer;
				InputProducer->ProduceInput(0, 0, InputNameMap, ServerInputsContainer);
				ServerUpdateState(ServerInputsContainer, bKeepVehicleAwake);
			}
		}

		if (PawnOwner && PawnOwner->IsNetMode(NM_Client))
		{
			MarkForClientCameraUpdate();
		}
	}
	
	if (!bUsingNetworkPhysicsPrediction)
	{
		// use the replicated values for all pawns Autonomous and Simulated
		InputsContainer = ReplicatedState.Container;
		bKeepVehicleAwake = ReplicatedState.KeepAwake;
	}
}

bool UModularVehicleBaseComponent::ServerUpdateState_Validate(const FModuleInputContainer& InputsIn, bool KeepAwake)
{
	return true;
}

void UModularVehicleBaseComponent::ServerUpdateState_Implementation(const FModuleInputContainer& InputsIn, bool KeepAwake)
{
	// update state of inputs that will be replicated back to the clients
	ReplicatedState.KeepAwake = KeepAwake;
	ReplicatedState.Container = InputsIn;

	MARK_PROPERTY_DIRTY_FROM_NAME(UModularVehicleBaseComponent, ReplicatedState, this);

	// apply control inputs on server as received from the original client
	InputsContainer = InputsIn;
}


void UModularVehicleBaseComponent::SetCurrentAsyncData(FChaosSimModuleManagerAsyncOutput* CurOutput, FChaosSimModuleManagerAsyncOutput* NextOutput, float Alpha, int32 VehicleManagerTimestamp)
{
	SetCurrentAsyncDataInternal(CurOutput, NextOutput, Alpha, VehicleManagerTimestamp);
}

/************************************************************************/
/* Setup the current async I/O data                                     */
/************************************************************************/
void UModularVehicleBaseComponent::SetCurrentAsyncDataInternal(FChaosSimModuleManagerAsyncOutput* CurOutput, FChaosSimModuleManagerAsyncOutput* NextOutput, float Alpha, int32 VehicleManagerTimestamp)
{
	ensure(CurAsyncOutput == nullptr);	//should get reset after update is done

	NextAsyncOutput = nullptr;
	OutputInterpAlpha = 0.f;

	// We need to find our vehicle in the output given
	if (CurOutput)
	{
		for (int32 CurOutputIdx = 0; CurOutputIdx < CurOutput->VehicleOutputs.Num(); ++CurOutputIdx)
		{
			FModularVehicleAsyncOutput* VehicleOutput = CurOutput->VehicleOutputs[CurOutputIdx].Get();
			if (VehicleOutput && VehicleOutput->bValid && VehicleOutput->Type == CurAsyncType)
			{
				if (VehicleOutput->Vehicle == this)
				{
					// Found the correct vehicle output.
					CurAsyncOutput = VehicleOutput;
					break;
				}
			}
		}

	}

	if(NextOutput)
	{
		for (int32 NextOutputIdx = 0; NextOutputIdx < NextOutput->VehicleOutputs.Num(); ++NextOutputIdx)
		{
			FModularVehicleAsyncOutput* VehicleNextOutput = NextOutput->VehicleOutputs[NextOutputIdx].Get();
			if (VehicleNextOutput && VehicleNextOutput->bValid && VehicleNextOutput->Type == CurAsyncType)
			{
				if (VehicleNextOutput->Vehicle == this)
				{
					NextAsyncOutput = VehicleNextOutput;
					OutputInterpAlpha = Alpha;
					break;
				}
			}
		}
	}
}

IPhysicsProxyBase* UModularVehicleBaseComponent::GetPhysicsProxy() const 
{ 
	if (CachedPhysicsProxy)
	{
		return CachedPhysicsProxy;
	}

	// ClusterUnionPhysicsProxy case
	if (ClusterUnionComponent)
	{
		return CachedPhysicsProxy = ClusterUnionComponent->GetPhysicsProxy();
	}
	
	// SingleParticlePhysicsProxy case
	if (const FBodyInstance* BodyInstance = UpdatedPrimitive ? UpdatedPrimitive->GetBodyInstance() : nullptr)
	{
		return CachedPhysicsProxy = BodyInstance->GetPhysicsActor();
	}

	return nullptr;
}

Chaos::FSimOutputData* UModularVehicleBaseComponent::FindModuleOutputFromGuid(const FPhysicsVehicleOutput& OutputContainer, int Guid) const
{
	for (Chaos::FSimOutputData* DataPtr : OutputContainer.SimTreeOutputData)
	{
		if (DataPtr && (DataPtr->ModuleGuid == Guid))
		{
			return DataPtr;
		}
	}

	return nullptr;
}

void UModularVehicleBaseComponent::SetClusterComponent(UClusterUnionComponent* InPhysicalComponent)
{
	ClusterUnionComponent = InPhysicalComponent;
}

/***************************************************************************/
/* READ OUTPUT DATA - Access the async output data from the Physics Thread */
/***************************************************************************/
void UModularVehicleBaseComponent::ParallelUpdate(const Chaos::FCreatedModules& ModuleEvents)
{
	if (FModularVehicleAsyncOutput* CurrentOutput = static_cast<FModularVehicleAsyncOutput*>(CurAsyncOutput))
	{
		if (CurrentOutput->bValid && PVehicleOutput)
		{
			PVehicleOutput->Clean();

			PVehicleOutput->NewlyCreatedModuleGuids = ModuleEvents.ModuleEvents;

			int NumItems = CurrentOutput->VehicleSimOutput.SimTreeOutputData.Num();
			PVehicleOutput->SimTreeOutputData.Reserve(NumItems);

			if (const FModularVehicleAsyncOutput* NextOutput = static_cast<FModularVehicleAsyncOutput*>(NextAsyncOutput))
			{
				for (int I = 0; I < NumItems; I++)
				{
					Chaos::FSimOutputData* CurrentSimData = CurrentOutput->VehicleSimOutput.SimTreeOutputData[I];
					if (Chaos::FSimOutputData* NextSimData = FindModuleOutputFromGuid(NextOutput->VehicleSimOutput, CurrentSimData->ModuleGuid)) //NextOutput->VehicleSimOutput.SimTreeOutputData[I];
					{
						PVehicleOutput->SimTreeOutputData.EmplaceAt(I, CurrentSimData->MakeNewData());
						PVehicleOutput->SimTreeOutputData[I]->Lerp(*CurrentSimData, *NextSimData, OutputInterpAlpha);
					}
					else
					{
						PVehicleOutput->SimTreeOutputData.EmplaceAt(I, CurrentSimData->MakeNewData());
						PVehicleOutput->SimTreeOutputData[I]->Lerp(*CurrentSimData, *CurrentSimData, 0);
					}
				}
			}
			else
			{
				for (int I = 0; I < NumItems; I++)
				{
					Chaos::FSimOutputData* CurrentSimData = CurrentOutput->VehicleSimOutput.SimTreeOutputData[I];
					PVehicleOutput->SimTreeOutputData.EmplaceAt(I, CurrentSimData->MakeNewData());
					PVehicleOutput->SimTreeOutputData[I]->Lerp(*CurrentSimData, *CurrentSimData, 0);
				}
			}


			for (int I = 0; I < NumItems; I++)
			{
				if (PVehicleOutput->SimTreeOutputData[I])
				{
					// pass output data to modules originating components
					{
						int32 Guid = PVehicleOutput->SimTreeOutputData[I]->ModuleGuid;

						ensureMsgf(Guid != INDEX_NONE, TEXT("Output Module Guid is invalid, have you forgotten to call the base Chaos::FSimOutputData::Lerp in your module's Lerp function"));

						// PhysicsGuidToComponent only applies to traditional actor component setups, not UEFN entity components
						if (const TWeakObjectPtr<USceneComponent>* Component = PhysicsGuidToComponent.Find(Guid))
						{
							if (Component->IsValid())
							{
								if (UVehicleSimBaseSceneComponent* BaseSceneComponent = Cast<UVehicleSimBaseSceneComponent>(Component->Get()))
								{
									BaseSceneComponent->OnOutputReady(PVehicleOutput->SimTreeOutputData[I]);
								}
								else if (UVehicleSimBaseComponent* BaseComponent = Cast<UVehicleSimBaseComponent>(Component->Get()))
								{
									BaseComponent->OnOutputReady(PVehicleOutput->SimTreeOutputData[I]);
								}
							}
						}
					}
				
					// extract/cache some generally useful values as we go as trying to locate this data later requires a search
					if (PVehicleOutput->SimTreeOutputData[I]->IsSimType<Chaos::FTransmissionSimModule>())
					{
						// if there is more than one transmission then the last one will inform us of the current gear
						CurrentGear = static_cast<Chaos::FTransmissionOutputData*>(PVehicleOutput->SimTreeOutputData[I])->CurrentGear;
					}
					else if (PVehicleOutput->SimTreeOutputData[I]->IsSimType<Chaos::FEngineSimModule>())
					{
						// if there is more than one engine then the last one will inform us of the engine RPM
						Chaos::FEngineOutputData* Engine = static_cast<Chaos::FEngineOutputData*>(PVehicleOutput->SimTreeOutputData[I]);

						EngineRPM = Engine->RPM;
						EngineTorque = Engine->Torque;
					}
				}

				// Pass animation data to final location in ModuleAnimationSetups, which is used by the animation instance
				if (Chaos::FSimOutputData* ModuleOutput = PVehicleOutput->SimTreeOutputData[I])
				{
					FTransform NewTransform = FTransform::Identity;
					if ( GetOwner() &&  GetOwner()->GetRootComponent())
					{
						NewTransform = GetOwner()->GetRootComponent()->GetComponentToWorld();
					}

					Chaos::FSimModuleAnimationData AnimData;
					ModuleOutput->GetFinalAnimDataGameThread(NewTransform, AnimData);

					int Index = AnimData.AnimationSetupIndex;
					if ((Index >= 0) && (Index < ModuleAnimationSetups.Num()))
					{
						ModuleAnimationSetups[Index].AnimFlags |= AnimData.AnimFlags;
						ModuleAnimationSetups[Index].CombinedRotation = AnimData.CombinedRotation;

						if (AnimData.AnimFlags & Chaos::EAnimationFlags::AnimateRotation)
						{
							ModuleAnimationSetups[Index].RotOffset = AnimData.AnimationRotOffset;
						}

						if (AnimData.AnimFlags & Chaos::EAnimationFlags::AnimatePosition)
						{
							ModuleAnimationSetups[Index].LocOffset = AnimData.AnimationLocOffset;
						}
					}
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (PVehicleOutput && !PVehicleOutput->SimTreeOutputData.IsEmpty() && PVehicleOutput->SimTreeOutputData[I])
				{
					PVehicleOutput->SimTreeOutputData[I]->DebugString = CurrentOutput->VehicleSimOutput.SimTreeOutputData[I]->DebugString;
				}
#endif

			}

			UpdateNonSkeletalAnimations();

		}
	}
}


void UModularVehicleBaseComponent::Update(float DeltaTime)
{
}


void UModularVehicleBaseComponent::PostUpdate()
{
	ensure(PVehicleOutput);

	if (PVehicleOutput)
	{
		// broadcast events
		for (Chaos::FCreatedModule AddedModule : PVehicleOutput->NewlyCreatedModuleGuids)
		{
			BroadcastModuleAddedEvent(AddedModule.SimType, AddedModule.Guid, AddedModule.TreeIndex);
		}

		// Update replication structures
		if (!PVehicleOutput->NewlyCreatedModuleGuids.IsEmpty())
		{
			if (NetworkPhysicsComponent)
			{
				TSharedPtr<Chaos::FBaseRewindHistory>& History = NetworkPhysicsComponent->GetStateHistory_Internal();
				Chaos::TDataRewindHistory<FNetworkModularVehicleStates>* StateHistory = static_cast<Chaos::TDataRewindHistory<FNetworkModularVehicleStates>*>(History.Get());
				if (StateHistory)
				{
					for (int I = 0; I < StateHistory->GetDataHistory().Num(); I++)
					{
						FNetworkModularVehicleStates& State = StateHistory->GetDataHistory()[I];
						VehicleSimulationPT->GenerateReplicationStructure(State);
					}
				}
			}
		}
	}
}

void UModularVehicleBaseComponent::FinalizeSimCallbackData(FChaosSimModuleManagerAsyncInput& Input)
{
	CurAsyncInput = nullptr;
	CurAsyncOutput = nullptr;
}

const FTransform& UModularVehicleBaseComponent::GetComponentTransform() const
{
	return MyComponent->GetComponentTransform();
}

/**
 * Updates transforms for non-skeletal components based on animation data.
 * This allows static meshes and scene components to be animated by the physics simulation.
 *
 * Call this from ParallelUpdate() after ModuleAnimationSetups is populated with physics data.
 */
void UModularVehicleBaseComponent::UpdateNonSkeletalAnimations()
{
	for (int32 AnimIndex = 0; AnimIndex < ModuleAnimationSetups.Num(); AnimIndex++)
	{
		const FModuleAnimationSetup& AnimSetup = ModuleAnimationSetups[AnimIndex];
		int32 ModuleGuid = AnimSetup.ModuleGUID;

		if (ModuleGuid == INDEX_NONE)
		{
			continue;
		}

		// Find the component data
		for (TPair<TObjectPtr<USceneComponent>, FVehicleComponentData>& Pair : ComponentToPhysicsObjects)
		{
			if (Pair.Value.Guid == ModuleGuid)
			{
				// Use cached visual component
				USceneComponent* ComponentToAnimate = Pair.Value.VisualComponentToAnimate;

				if (!ComponentToAnimate || !ComponentToAnimate->IsValidLowLevel())
				{
					continue;
				}

				// Apply animation
				FTransform NewRelativeTransform = ComponentToAnimate->GetRelativeTransform();

				if (AnimSetup.AnimFlags & Chaos::EAnimationFlags::AnimateRotation)
				{
					NewRelativeTransform.SetRotation(AnimSetup.InitialRotOffset *
						AnimSetup.CombinedRotation);
				}

				if (AnimSetup.AnimFlags & Chaos::EAnimationFlags::AnimatePosition)
				{
					NewRelativeTransform.SetLocation(AnimSetup.InitialLocOffset +
						AnimSetup.LocOffset);
				}

				ComponentToAnimate->SetRelativeTransform(NewRelativeTransform, false, nullptr,
					ETeleportType::TeleportPhysics);
				break;
			}
		}
	}
}


void UModularVehicleBaseComponent::ActionTreeUpdates(Chaos::FSimTreeUpdates* NextTreeUpdates)
{
	if (VehicleSimulationPT)
	{
		VehicleSimulationPT->AppendTreeUpdates(NextTreeUpdates);
	}
}

int32 UModularVehicleBaseComponent::FindParentsLastSimComponent(const USceneComponent* AttachedComponent)
{
	if (USceneComponent* AttachParent = AttachedComponent->GetAttachParent())
	{
		TArray<USceneComponent*> Children;
		AttachParent->GetChildrenComponents(false, Children);

		for (int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
		{
			if (IVehicleSimBaseComponentInterface* ChildSimComponent = Cast<IVehicleSimBaseComponentInterface>(Children[ChildIndex]))
			{
				return ChildSimComponent->GetTreeIndex();
			}
		}
	}

	return INDEX_NONE;
}


void UModularVehicleBaseComponent::OnModuleInitialized(const FName& SimType, int Guid, int TreeIndex)
{
	if (const TWeakObjectPtr<USceneComponent>* Component = PhysicsGuidToComponent.Find(Guid))
	{
		if (Component->IsValid())
		{
			if (UVehicleSimBaseSceneComponent* BaseSceneComponent = Cast<UVehicleSimBaseSceneComponent>(Component->Get()))
			{
				BaseSceneComponent->OnAdded();
			}
			else if (UVehicleSimBaseComponent* BaseComponent = Cast<UVehicleSimBaseComponent>(Component->Get()))
			{
				BaseComponent->OnAdded();
			}
		}
	}
}

void UModularVehicleBaseComponent::OnModuleRemoved(const FName& SimType, int Guid, int TreeIndex)
{
	if (const TWeakObjectPtr<USceneComponent>* Component = PhysicsGuidToComponent.Find(Guid))
	{
		if (Component->IsValid())
		{
			if (UVehicleSimBaseSceneComponent* BaseSceneComponent = Cast<UVehicleSimBaseSceneComponent>(Component->Get()))
			{
				BaseSceneComponent->OnRemoved();
			}
			else if (UVehicleSimBaseComponent* BaseComponent = Cast<UVehicleSimBaseComponent>(Component->Get()))
			{
				BaseComponent->OnRemoved();
			}
		}
	}
}

const Chaos::FSimOutputData* UModularVehicleBaseComponent::GetOutputData(int ModuleGuid)
{
	if (PVehicleOutput)
	{
		return PVehicleOutput->GetOutputData(ModuleGuid);
	}

	return nullptr;
}


void UModularVehicleBaseComponent::AddComponentToSimulation(UPrimitiveComponent* InComponent, const TArray<FClusterUnionBoneData>& BonesData, const TArray<FClusterUnionBoneData>& RemovedBoneIDs, bool bIsNew)
{
	AddComponentToSimulationImpl(InComponent, BonesData, RemovedBoneIDs, bIsNew);
}

void UModularVehicleBaseComponent::AddComponentToSimulationImpl(UPrimitiveComponent* InComponent, const TArray<FClusterUnionBoneData>& BonesData, const TArray<FClusterUnionBoneData>& RemovedBoneIDs, bool bIsNew)
{
	check(ClusterUnionComponent);

	int32 ComponentAddOrder = INDEX_NONE;
	if (InComponent && bIsNew)
	{
		UE_LOGF(LogModularBase, Log, "AddComponentToSimulation %ls, NetMode %d", *InComponent->GetName(), InComponent->GetNetMode());
		
		if (ClusterUnionComponent->IsAuthority())
		{
			// retain the order that the components were constructed
			CreateOrUpdateComponentAddOrder(InComponent, NextConstructionIndex);
			ComponentAddOrder = NextConstructionIndex++;
		}
		else
		{
			ComponentAddOrder = FindComponentAddOrder(InComponent);
		}

		int ParentID = FindParentsLastSimComponent(InComponent);

		Chaos::FSimTreeUpdates LatestTreeUpdates;
		CreateAssociatedSimComponents(nullptr, InComponent, ParentID, NextTransformIndex, LatestTreeUpdates);

		Chaos::FSimTreeUpdates* NextTreeUpdates = nullptr;
		if (LastComponentAddIndex + 1 == ComponentAddOrder)
		{
			// has the next in line just come in..
			NextTreeUpdates = &LatestTreeUpdates;
			LastComponentAddIndex = ComponentAddOrder;
			ActionTreeUpdates(NextTreeUpdates);
		}
		else
		{
			// add to pending and action later
			PendingTreeUpdates.Add(InComponent, MoveTemp(LatestTreeUpdates));
		}

		int Size = 1; // for UStaticMeshComponent && UGeometryCollectionComponent cases
		if (USkeletalMeshComponent* SKMComponent = Cast<USkeletalMeshComponent>(InComponent))
		{
			Size = SKMComponent->Bodies.Num();
		}

		NextTransformIndex += Size;
	}
}

void UModularVehicleBaseComponent::RemoveComponentFromSimulation(UPrimitiveComponent* InComponent, const TArray<FClusterUnionBoneData>& RemovedBonesData)
{
	RemoveComponentFromSimulationImpl(InComponent, RemovedBonesData);
}

void UModularVehicleBaseComponent::RemoveComponentFromSimulationImpl(UPrimitiveComponent* InComponent, const TArray<FClusterUnionBoneData>& RemovedBonesData)
{
	if (InComponent && VehicleSimulationPT)
	{
		UE_LOGF(LogModularBase, Log, "UModularVehicleBaseComponent::RemoveComponentFromSimulation: %ls, NetMode %d", *InComponent->GetFullName(), InComponent->GetNetMode());

		Chaos::FSimTreeUpdates LatestTreeUpdates;

		TArray<USceneComponent*> Components;
		InComponent->GetChildrenComponents(true, Components);

		for (USceneComponent* ComponentPart : Components)
		{
			if (IVehicleSimBaseComponentInterface* ChangedComponent = Cast<IVehicleSimBaseComponentInterface>(ComponentPart))
			{
				if (FVehicleComponentData* ComponentData = ComponentToPhysicsObjects.Find(ComponentPart))
				{
					LatestTreeUpdates.RemoveNode(ComponentData->Guid);
					ComponentToPhysicsObjects.Remove(ComponentPart);
				}
			}
		}

		TUniquePtr<Chaos::FSimModuleTree>& SimModuleTree = VehicleSimulationPT->AccessSimComponentTree();
		if(SimModuleTree.IsValid())
		{
			for (const Chaos::FPendingModuleDeletions& TreeUpdate : LatestTreeUpdates.GetDeletedModules())
			{
				for (int Index = 0; Index < SimModuleTree->GetNumNodes(); Index++)
				{
					if (Chaos::ISimulationModuleBase* SimModule = SimModuleTree->GetNode(Index).SimModule)
					{
						if (SimModule->GetGuid() == TreeUpdate.Guid)
						{
							BroadcastModuleRemovedEvent(SimModule->GetSimType(), SimModule->GetGuid(), SimModule->GetTreeIndex());
							PhysicsGuidToComponent.Remove(SimModule->GetGuid());

							SimModule->SetAnimationEnabled(false);
							SimModule->SetStateFlags(Chaos::eSimModuleState::Disabled);
							SimModule->OnTermination_External();
							break;
						}
					}
				}
			}
		}

		if (ClusterUnionComponent->IsAuthority())
		{
			int32 Index = ConstructionDatas.IndexOfByPredicate([InComponent](const FConstructionData& InData) { return InData.Component == InComponent; });
			if (Index != INDEX_NONE)
			{
				ConstructionDatas.RemoveAtSwap(Index);
			}
		}

		ActionTreeUpdates(&LatestTreeUpdates);

		NextTransformIndex--;
	}

}

void UModularVehicleBaseComponent::SetLocallyControlled(bool bLocallyControlledIn)
{
	if (bUsingNetworkPhysicsPrediction && NetworkPhysicsComponent)
	{
		// NetworkPhysicsComponent is the single source of truth, guard against sim-proxy misuse
		if (GetOwner() && GetOwner()->GetLocalRole() != ENetRole::ROLE_SimulatedProxy)
		{
			NetworkPhysicsComponent->SetIsRelayingLocalInputs(bLocallyControlledIn);
		}
		else
		{
			NetworkPhysicsComponent->SetIsRelayingLocalInputs(false);
		}
		return;
	}

	// Fallback for when NetworkPhysicsComponent is not available
	bIsLocallyControlled = false;
	if (UWorld* World = GetWorld())
	{
		if (GetOwner() && GetOwner()->GetLocalRole() != ENetRole::ROLE_SimulatedProxy)
		{
			bIsLocallyControlled = bLocallyControlledIn;
		}
	}
}

void UModularVehicleBaseComponent::AssimilateComponentInputs(TArray<FModuleInputSetup>& OutCombinedInputs)
{
	// copy the input setup from this class
	OutCombinedInputs = InputConfig;

	// append the input setup from all module sim components attached to same actor
	if (AActor* Actor = GetOwner())
	{
		if (USceneComponent* RootComponent = Actor->GetRootComponent())
		{
			TArray<USceneComponent*> ChildComponents;
			RootComponent->GetChildrenComponents(true, ChildComponents);

			for (USceneComponent* Component : ChildComponents)
			{
				if (IVehicleSimBaseComponentInterface* GCComponent = Cast<IVehicleSimBaseComponentInterface>(Component))
				{
					// don't add duplicates, i.e. 4 wheels could be looking for a single steering input
					for (FModuleInputSetup& Config : GCComponent->GetInputConfig())
					{
						if (OutCombinedInputs.Find(Config) == INDEX_NONE)
						{
							OutCombinedInputs.Append(GCComponent->GetInputConfig());
						}
					}
				}
			}
		}
	}
}

void UModularVehicleBaseComponent::CreateVehicleSim()
{
	UWorld* World = GetWorld();

	// register our vehicle with the modular vehicle Sim Manager
	if (World)
	{
		if (World->IsGameWorld())
		{
			UE_LOGF(LogModularBase, Log, "UModularVehicleBaseComponent::CreateVehicleSim");

			// create the simulation class
			VehicleSimulationPT = MakeUnique<FModularVehicleSimulation>(bUsingNetworkPhysicsPrediction, (int8)World->GetNetMode());

			// create physics output container
			PVehicleOutput = MakeUnique<FPhysicsVehicleOutput>();

			// generate the simulation tree with initial components
			FModularVehicleBuilder::GenerateSimTree(this);

			FPhysScene* PhysScene = World->GetPhysicsScene();

			if (FChaosSimModuleManager* SimManager = FChaosSimModuleManager::GetManagerFromScene(PhysScene))
			{
				SimManager->AddVehicle(this);
			}
		}
	}

}

void UModularVehicleBaseComponent::DestroyVehicleSim()
{
	if (UWorld* World = GetWorld())
	{
		if (World->IsGameWorld())
		{
			UE_LOGF(LogModularBase, Log, "UModularVehicleBaseComponent::DestroyVehicleSim");

			FPhysScene* PhysScene = World->GetPhysicsScene();
			check(PhysScene);

			if (Chaos::FPhysicsSolver* LocalSolver = PhysScene->GetSolver())
			{
				if (FChaosSimModuleManager* SimManager = FChaosSimModuleManager::GetManagerFromScene(PhysScene))
				{
					SimManager->RemoveVehicle(this);
				}

				if (PVehicleOutput.IsValid())
				{
					PVehicleOutput.Reset(nullptr);
				}

				// postpone deletion of object while simultaneously setting it to null in the component (TUniquePtr)
				LocalSolver->EnqueueCommandImmediate([Pointer = MoveTemp(VehicleSimulationPT)]() mutable
					{
						Pointer.Reset(nullptr);
					});

			}

			if (bUsingNetworkPhysicsPrediction && NetworkPhysicsComponent)
			{
				NetworkPhysicsComponent->RemoveDataHistory();
			}

		}
	}
}


void UModularVehicleBaseComponent::AddComponentToCluster(USceneComponent* Component)
{
	TArray<int32> BoneIds;

	if (ClusterUnionComponent)
	{
	if (UGeometryCollectionComponent* GCComponent = Cast<UGeometryCollectionComponent>(Component))
	{
		BoneIds.Add(0);

		UE_LOGF(LogModularBase, Log, "AddComponentToCluster: Geometry Collection");
		ClusterUnionComponent->AddComponentToCluster(GCComponent, BoneIds);
	}

	if (UStaticMeshComponent* SMComponent = Cast<UStaticMeshComponent>(Component))
	{
		if (SMComponent->HasValidPhysicsState())
		{
			BoneIds.Add(0);

			UE_LOGF(LogModularBase, Log, "AddComponentToCluster: Static Mesh");
			ClusterUnionComponent->AddComponentToCluster(SMComponent, BoneIds);
		}
	}

	if (USkeletalMeshComponent* SKMComponent = Cast<USkeletalMeshComponent>(Component))
	{
		for (int I = 0; I < SKMComponent->Bodies.Num(); I++)
		{
			BoneIds.Add(I);
		}
		if (BoneIds.Num() > 0)
		{
			UE_LOGF(LogModularBase, Log, "AddComponentToCluster: Skeletal Mesh");
			ClusterUnionComponent->AddComponentToCluster(SKMComponent, BoneIds);
		}
	}
}
}


void UModularVehicleBaseComponent::AddOverlappingComponentsToCluster()
{
	//if (UWorld* World = GetWorld())
	//{
	//	FVector HalfSize = AutoAddOverlappingBoxSize * 0.5f;
	//	FBox LocalBox = FBox(-HalfSize, HalfSize);
	//	TArray<FOverlapResult> OverlapResults;
	//	FCollisionShape CollisionBox;
	//	CollisionBox.SetBox((FVector3f)LocalBox.GetExtent());

	//	const FCollisionQueryParams QueryParams;
	//	const FCollisionResponseParams ResponseParams;
	//	const ECollisionChannel CollisionChannel = ECollisionChannel::ECC_WorldDynamic;
	//	const bool bOverlapHit = World->OverlapMultiByChannel(OverlapResults, GetActorLocation(), FQuat::Identity, CollisionChannel, CollisionBox, QueryParams, ResponseParams);

	//	TArray<int32> BoneIds;
	//	BoneIds.Add(0);
	//	// Test each overlapped object for a hit result
	//	for (FOverlapResult OverlapResult : OverlapResults)
	//	{
	//		if (UGeometryCollectionComponent* OverlapGCComponent = Cast<UGeometryCollectionComponent>(OverlapResult.Component.Get()))
	//		{
	//			ClusterUnionComponent->AddComponentToCluster(OverlapGCComponent, BoneIds);
	//		}
	//	}
	//}
}

bool UModularVehicleBaseComponent::AddComponentsFromOwnedActor()
{
	bool bIsSuccess = false;

	TArray<int32> BoneIds;
	BoneIds.Add(0);

	if (AActor* Actor = GetOwner())
	{
		if (UClusterUnionComponent* ClusterUnion = Actor->GetComponentByClass<UClusterUnionComponent>())
		{
			TArray<USceneComponent*> ChildComponents;
			ClusterUnion->GetChildrenComponents(true, ChildComponents);

			for (USceneComponent* Component : ChildComponents)
			{
				AddComponentToCluster(Component);
				bIsSuccess = true;
			}
		}
	}

	return bIsSuccess;
}

void UModularVehicleBaseComponent::AddSimulationComponentsFromOwnedActor()
{
	// get raw simulation components that are not connected with any component - required for scene graph
	if (AActor* Actor = GetOwner())
	{
		if (USceneComponent* RootComponent = Actor->GetRootComponent())
		{
			TArray<USceneComponent*> ChildComponents;
			RootComponent->GetChildrenComponents(false, ChildComponents);

			int32 ComponentAddOrder = INDEX_NONE;
			int ParentIndex=-1;
			int TransformIndex=0;
			Chaos::FSimTreeUpdates LatestTreeUpdates;

			for (USceneComponent* Component : ChildComponents)
			{
				if (UVehicleSimBaseComponent* SimulationComponent = Cast<UVehicleSimBaseComponent>(Component))
				{
					CreateIndependentSimComponents(RootComponent, SimulationComponent, ParentIndex, TransformIndex, LatestTreeUpdates);
					NextTransformIndex++;
				}
			}

			Chaos::FSimTreeUpdates* NextTreeUpdates = nullptr;
			NextTreeUpdates = &LatestTreeUpdates;
			LastComponentAddIndex = ComponentAddOrder;
			ActionTreeUpdates(NextTreeUpdates);
		}
	}
}

void UModularVehicleBaseComponent::SetInputProducerClass(TSubclassOf<UVehicleInputProducerBase> InInputProducerClass, bool bForceNewInstance)
{
	InputProducerClass = InInputProducerClass;

	if (bForceNewInstance)
	{
		InputProducer = nullptr;
	}
	
	SetupInputConfiguration();
}


void UModularVehicleBaseComponent::SetInputBool(const FName Name, const bool Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}

void UModularVehicleBaseComponent::SetInputInteger(const FName Name, const int32 Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}


void UModularVehicleBaseComponent::SetInputAxis1D(const FName Name, const double Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}

void UModularVehicleBaseComponent::SetInputAxis2D(const FName Name, const FVector2D Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}

void UModularVehicleBaseComponent::SetInputAxis3D(const FName Name, const FVector Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}

void UModularVehicleBaseComponent::SetInput(const FName& Name, const bool Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}

void UModularVehicleBaseComponent::SetInput(const FName& Name, const double Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}

void UModularVehicleBaseComponent::SetInput(const FName& Name, const FVector2D& Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}

void UModularVehicleBaseComponent::SetInput(const FName& Name, const FVector& Value, EModuleInputBufferActionType BufferAction)
{
	if (InputProducer)
	{
		InputProducer->BufferInput(InputNameMap, Name, Value, BufferAction);
	}
}

void UModularVehicleBaseComponent::SetState(const FName& InName, const bool InValue)
{
	FInputInterface Inputs(StateNameMap, StateInputContainer, InputQuantizationType);
	Inputs.SetBool(InName, InValue);
}

void UModularVehicleBaseComponent::SetState(const FName& InName, const int32 InValue)
{
	FInputInterface Inputs(StateNameMap, StateInputContainer, InputQuantizationType);
	Inputs.SetInteger(InName, InValue);
}

void UModularVehicleBaseComponent::SetState(const FName& InName, const double InValue)
{
	FInputInterface Inputs(StateNameMap, StateInputContainer, InputQuantizationType);
	Inputs.SetFloat(InName, InValue);
}

void UModularVehicleBaseComponent::SetState(const FName& InName, const FVector2D& InValue)
{
	FInputInterface Inputs(StateNameMap, StateInputContainer, InputQuantizationType);
	Inputs.SetVector2D(InName, InValue);
}

void UModularVehicleBaseComponent::SetState(const FName& InName, const FVector& InValue)
{
	FInputInterface Inputs(StateNameMap, StateInputContainer, InputQuantizationType);
	Inputs.SetVector(InName, InValue);
}

void UModularVehicleBaseComponent::SetGearInput(int32 Gear)
{
	GearInput = Gear;
}

int32 UModularVehicleBaseComponent::GetCurrentGear()
{
	return CurrentGear;
}

bool UModularVehicleBaseComponent::IsReversing()
{
	return (GetCurrentGear() < 0);
}

void UModularVehicleBaseComponent::BroadcastModuleAddedEvent(const FName& SimType, int Guid, int TreeIndex)
{
	if (OnModuleAddedEvent.IsBound())
	{
		OnModuleAddedEvent.Broadcast(SimType, Guid, TreeIndex);
	}
	if (OnModuleAddedNativeEvent.IsBound())
	{
		OnModuleAddedNativeEvent.Broadcast(SimType, Guid, TreeIndex);
	}
}

void UModularVehicleBaseComponent::BroadcastModuleRemovedEvent(const FName& SimType, int Guid, int TreeIndex)
{
	if (OnModuleRemovedEvent.IsBound())
	{
		OnModuleRemovedEvent.Broadcast(SimType, Guid, TreeIndex);
	}
	if (OnModuleRemovedNativeEvent.IsBound())
	{
		OnModuleRemovedNativeEvent.Broadcast(SimType, Guid, TreeIndex);
	}
}

void UModularVehicleBaseComponent::AddInput(FModuleInputSetup InputSetup)
{
	InputConfig.Add(InputSetup);
}



//-=====================================================
// Networking Replication

void UModularVehicleBaseComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UModularVehicleBaseComponent, ReplicatedState);
	DOREPLIFETIME(UModularVehicleBaseComponent, ConstructionDatas);
}

void UModularVehicleBaseComponent::ShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
	UFont* RenderFont = GEngine->GetMediumFont();

	// draw input values
	Canvas->SetDrawColor(FColor::White);

	for (int I = 0; I < InputsContainer.GetNumInputs(); I++)
	{
		float Interpolated = InputsContainer.GetValueAtIndex(I).GetMagnitude();

		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("%s %3.2f"), *InputConfig[I].Name.ToString(), Interpolated), 4, YPos);
	}

	YPos += 10;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	for (Chaos::FSimOutputData* Data : PVehicleOutput->SimTreeOutputData)
	{
		YPos += Canvas->DrawText(RenderFont, FString::Printf(TEXT("%s"), *Data->ToString()), 4, YPos);
	}
#endif

}

void UModularVehicleBaseComponent::LogInputSetup()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const FModuleInputContainer::FInputNameMap& NameMap = InputNameMap;

	for (auto& NamePair : NameMap)
	{ 
		UE_LOGF(LogModularBase, Log, "Input: %ls %d", *NamePair.Key.ToString(), NamePair.Value);
	}
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}
