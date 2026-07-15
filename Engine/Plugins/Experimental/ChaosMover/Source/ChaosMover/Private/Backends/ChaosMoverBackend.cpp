// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosMover/Backends/ChaosMoverBackend.h"

#include "Backends/ChaosMoverSubsystem.h"
#include "MoverGameplayTagLog.h"
#include "ChaosMover/ChaosMoverConsoleVariables.h"
#include "ChaosMover/ChaosMoverLog.h"
#include "ChaosMover/ChaosMoverSimulation.h"
#include "ChaosMover/Character/ChaosCharacterMoverComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Framework/Threading.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PhysicsVolume.h"
#include "MoveLibrary/MovementMixer.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "MovementModeStateMachine.h"
#include "NetworkChaosMoverData.h"
#include "PBDRigidsSolver.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "PhysicsProxy/CharacterGroundConstraintProxy.h"
#include "PhysicsProxy/JointConstraintProxy.h"
#include "Runtime/Experimental/Chaos/Private/Chaos/PhysicsObjectInternal.h"
#include "ChaosMover/ChaosMoverActionTypes.h"
#include "MotionWarpingComponent.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionWarpingTypes.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosMoverBackend)

UChaosMoverBackendComponent::UChaosMoverBackendComponent()
	: ActuationConstraintPhysicsUserData(&ActuationConstraintInstance)
{
	PrimaryComponentTick.bCanEverTick = false;

	bWantsInitializeComponent = true;
	bAutoActivate = true;

	SimulationClass = UChaosMoverSimulation::StaticClass();

	if (const Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		bIsUsingAsyncPhysics = Solver->IsUsingAsyncResults();
	}

	if (Chaos::FPhysicsSolverBase::IsNetworkPhysicsPredictionEnabled())
	{
		SetIsReplicatedByDefault(true);

		// Let's make sure PhysicsReplicationMode is set to Resimulation
		UWorld* World = GetWorld();
		AActor* MyActor = GetOwner();
		if (MyActor && World && World->IsGameWorld() && (World->GetNetMode() != ENetMode::NM_Standalone))
		{
			if (MyActor->GetPhysicsReplicationMode() != EPhysicsReplicationMode::Resimulation)
			{
				MyActor->SetPhysicsReplicationMode(EPhysicsReplicationMode::Resimulation);
				UE_LOGF(LogChaosMover, Log, "ChaosMoverBackend: Setting Physics Replication Mode to Resimulation for %ls or movement will not replicate correctly", *GetNameSafe(MyActor));
			}
		}
	}
}

void UChaosMoverBackendComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		if (const Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
		{
			bIsUsingAsyncPhysics = Solver->IsUsingAsyncResults();
		}

		UMoverComponent& MoverComp = GetMoverComponent();

		Simulation = NewObject<UChaosMoverSimulation>(GetOwner(), SimulationClass, TEXT("ChaosMoverSimulation"), RF_Transient);
		Simulation->SetOwner(MoverComp.GetOwner());
		MoverComp.SetSimulation(*Simulation);

		NullMovementMode = NewObject<UNullMovementMode>(&MoverComp, TEXT("NullMovementMode"), RF_Transient);
		ImmediateModeTransition = NewObject<UImmediateMovementModeTransition>(&GetMoverComponent(), TEXT("ImmediateModeTransition"), RF_Transient);
		TransformOnInit = GetMoverComponent().GetUpdatedComponentTransform();

		// Create NetworkPhysicsComponent
		if ((World->GetNetMode() != ENetMode::NM_Standalone) && Chaos::FPhysicsSolverBase::IsNetworkPhysicsPredictionEnabled() && bIsUsingAsyncPhysics)
		{
			NetworkPhysicsComponent = NewObject<UNetworkPhysicsComponent>(GetOwner(), TEXT("PhysMover_NetworkPhysicsComponent"), RF_Transient);

			// This isn't technically a DSO component, but set it net addressable as though it is
			NetworkPhysicsComponent->SetNetAddressable();
			NetworkPhysicsComponent->SetIsReplicated(true);
			NetworkPhysicsComponent->RegisterComponent();
			if (!NetworkPhysicsComponent->HasBeenInitialized())
			{
				NetworkPhysicsComponent->InitializeComponent();
			}
			NetworkPhysicsComponent->Activate(/* bReset = */ true);
			NetworkPhysicsComponent->SetActionHandler(Simulation);

			// Register network data for recording and rewind/resim
			NetworkPhysicsComponent->CreateDataHistory<UE::ChaosMover::FNetworkDataTraits>(this);

			if (NetworkPhysicsComponent->HasServerWorld())
			{
				if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
				{
					// When we're owned by a pawn, keep an eye on whether it's currently player-controlled or not
					PawnOwner->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandleOwningPawnControllerChanged_Server);
					HandleOwningPawnControllerChanged_Server(PawnOwner, nullptr, PawnOwner->Controller);
				}
				else
				{
					// If the owner isn't a pawn, there's no chance of player input happening, so inputs to the simulation are always produced on the server
					NetworkPhysicsComponent->SetIsRelayingLocalInputs(true);
				}
			}
		}
	}
}

void UChaosMoverBackendComponent::UninitializeComponent()
{
	if (NetworkPhysicsComponent)
	{
		NetworkPhysicsComponent->SetActionHandler(nullptr);
		NetworkPhysicsComponent->RemoveDataHistory();
		NetworkPhysicsComponent->DestroyComponent();
		NetworkPhysicsComponent = nullptr;
	}

	Super::UninitializeComponent();
}

void UChaosMoverBackendComponent::CreatePhysics()
{
	// Prevent the character particle from sleeping
	Chaos::FPhysicsSolver* Solver = GetPhysicsSolver();
	Chaos::FPBDRigidParticle* ControlledParticle = GetControlledParticle();
	Chaos::FSingleParticlePhysicsProxy* ControlledParticleProxy = ControlledParticle ? static_cast<Chaos::FSingleParticlePhysicsProxy*>(ControlledParticle->GetProxy()) : nullptr;
	if (ControlledParticleProxy)
	{
		ControlledParticle->SetSleepType(Chaos::ESleepType::NeverSleep);
	}

	// Create all possible constraints...
	// ... a character ground constraint, for constraint based character-like movement on ground
	CreateCharacterGroundConstraint();
	// ... a general purpose actuation joint constraint, for example, for constraint based pathed movement
	CreateActuationConstraint();
}

void UChaosMoverBackendComponent::DestroyPhysics()
{
	// Destroy all constraints
	DestroyCharacterGroundConstraint();
	DestroyActuationConstraint();
}

void UChaosMoverBackendComponent::CreateCharacterGroundConstraint()
{
	if (Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		if (Chaos::FPBDRigidParticle* ControlledParticle = GetControlledParticle())
		{
			if (Chaos::FSingleParticlePhysicsProxy* ControlledParticleProxy = static_cast<Chaos::FSingleParticlePhysicsProxy*>(ControlledParticle->GetProxy()))
			{
				// Create the character ground constraint, for character-like movement on ground
				CharacterGroundConstraint = MakeUnique<Chaos::FCharacterGroundConstraint>();
				CharacterGroundConstraint->Init(ControlledParticleProxy);
				Solver->RegisterObject(CharacterGroundConstraint.Get());
			}
		}
	}
}

void UChaosMoverBackendComponent::DestroyCharacterGroundConstraint()
{
	if (Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		if (CharacterGroundConstraint.IsValid())
		{
			// Note: Proxy gets destroyed when the constraint is deregistered and that deletes the constraint
			Solver->UnregisterObject(CharacterGroundConstraint.Release());
		}
	}
}

void UChaosMoverBackendComponent::CreateActuationConstraint()
{
	const UMoverComponent& MoverComp = GetMoverComponent();
	if (Chaos::FPhysicsObject* PhysicsObject = GetPhysicsObject())
	{
		const FTransform ComponentWorldTransform = MoverComp.GetUpdatedComponent()->GetComponentTransform();
		// Create the constraint via FChaosEngineInterface directly because it allows jointing a "real" object with a point in space (it creates a dummy particle for us)
		FPhysicsConstraintHandle Handle = FChaosEngineInterface::CreateConstraint(PhysicsObject, nullptr, FTransform::Identity, FTransform::Identity);

		bool bIsConstraintValid = false;
		if (Handle.IsValid() && ensure(Handle->IsType(Chaos::EConstraintType::JointConstraintType)))
		{
			if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(Handle.Constraint))
			{
				// Since we didn't use the ConstraintInstance to actually create the constraint (it requires both bodies exist, see comment above), link everything up manually
				ActuationConstraintHandle = Handle;						
				ActuationConstraintInstance.ConstraintHandle = ActuationConstraintHandle;
				Constraint->SetUserData(&ActuationConstraintPhysicsUserData/*has a (void*)FConstraintInstanceBase*/);
				bIsConstraintValid = true;

				if (Chaos::FPBDRigidParticle* EndpointParticle = Constraint->GetPhysicsBodies()[1]->GetParticle<Chaos::EThreadContext::External>()->CastToRigidParticle())
				{
					EndpointParticle->SetX(ComponentWorldTransform.GetLocation());
					EndpointParticle->SetR(ComponentWorldTransform.GetRotation());
				}
			}
		}

		if (!bIsConstraintValid)
		{
			FChaosEngineInterface::ReleaseConstraint(Handle);
		}
	}
}

void UChaosMoverBackendComponent::DestroyActuationConstraint()
{
	if (ActuationConstraintHandle.IsValid())
	{
		FChaosEngineInterface::ReleaseConstraint(ActuationConstraintHandle);
	}
}

void UChaosMoverBackendComponent::HandleOwningPawnControllerChanged_Server(APawn* OwnerPawn, AController* OldController, AController* NewController)
{
	// Inputs for player-controlled pawns originate on the player's client, all others originate on the server
	if (NetworkPhysicsComponent)
	{
		NetworkPhysicsComponent->SetIsRelayingLocalInputs(!OwnerPawn->IsPlayerControlled());
	}
}

void UChaosMoverBackendComponent::HandleUpdatedComponentPhysicsStateChanged(UPrimitiveComponent* ChangedComponent, EComponentPhysicsStateChange StateChange)
{
	if (StateChange == EComponentPhysicsStateChange::Destroyed)
	{
		bWantsDestroySim = true;
		DestroyPhysics();
	}
	else if (StateChange == EComponentPhysicsStateChange::Created)
	{
		bWantsCreateSim = true;
		CreatePhysics();
	}
}

Chaos::FPhysicsSolver* UChaosMoverBackendComponent::GetPhysicsSolver() const
{
	if (UWorld* World = GetWorld())
	{
		if (FPhysScene* Scene = World->GetPhysicsScene())
		{
			return Scene->GetSolver();
		}
	}
	return nullptr;
}

UMoverComponent& UChaosMoverBackendComponent::GetMoverComponent() const
{
	return *GetOuterUMoverComponent();
}

Chaos::FPhysicsObject* UChaosMoverBackendComponent::GetPhysicsObject() const
{
	IPhysicsComponent* PhysicsComponent = Cast<IPhysicsComponent>(GetMoverComponent().GetUpdatedComponent());
	return PhysicsComponent ? PhysicsComponent->GetPhysicsObjectByName(NAME_None) : nullptr;
}

Chaos::FPBDRigidParticle* UChaosMoverBackendComponent::GetControlledParticle() const
{
	Chaos::EnsureIsInGameThreadContext();

	if (Chaos::FPhysicsObject* PhysicsObject = GetPhysicsObject())
	{
		return FPhysicsObjectExternalInterface::LockRead(PhysicsObject)->GetRigidParticle(PhysicsObject);
	}

	return nullptr;
}

void UChaosMoverBackendComponent::InitSimulation()
{
	UMoverComponent& MoverComp = GetMoverComponent();

	Chaos::FCharacterGroundConstraintHandle* CharacterGroundConstraintHandle = nullptr;
	if (CharacterGroundConstraint)
	{
		if (Chaos::FCharacterGroundConstraintProxy* Proxy = CharacterGroundConstraint->GetProxy<Chaos::FCharacterGroundConstraintProxy>())
		{
			CharacterGroundConstraintHandle = Proxy->GetPhysicsThreadAPI();
		}
	}
	if (!CharacterGroundConstraintHandle)
	{
		return;
	}

	Chaos::FPBDJointConstraintHandle* JointConstraintHandle = nullptr;
	Chaos::FKinematicGeometryParticleHandle* JointEndPointParticle = nullptr;
	if (ActuationConstraintHandle.IsValid())
	{
		if (Chaos::FJointConstraintPhysicsProxy* Proxy = ActuationConstraintHandle->GetProxy<Chaos::FJointConstraintPhysicsProxy>())
		{
			JointConstraintHandle = Proxy->GetHandle();
			if (Chaos::FSingleParticlePhysicsProxy* EndPointProxy = Proxy->GetConstraint()->GetKinematicEndPoint())
			{
				JointEndPointParticle = EndPointProxy->GetHandle_LowLevel()->CastToKinematicParticle();
			}
		}
	}
	if (!JointConstraintHandle || !JointEndPointParticle)
	{
		return;
	}

	UChaosMoverSimulation::FInitParams Params;
	for (const TPair<FName, TObjectPtr<UBaseMovementMode>>& Pair : MoverComp.MovementModes)
	{
		Params.ModesToRegister.Add(Pair.Key, TWeakObjectPtr<UBaseMovementMode>(Pair.Value.Get()));
	}
	for (const TObjectPtr<UBaseMovementModeTransition>& Transition : MoverComp.Transitions)
	{
		Params.TransitionsToRegister.Add(TWeakObjectPtr<UBaseMovementModeTransition>(Transition.Get()));
	}
	Params.MovementMixer = TWeakObjectPtr<UMovementMixer>(MoverComp.MovementMixer.Get());
	Params.ImmediateModeTransition = TWeakObjectPtr<UImmediateMovementModeTransition>(ImmediateModeTransition.Get());
	Params.NullMovementMode = TWeakObjectPtr<UNullMovementMode>(NullMovementMode.Get());
	Params.StartingMovementMode = MoverComp.StartingMovementMode;
	Params.CharacterGroundConstraintHandle = CharacterGroundConstraintHandle;
	Params.ActuationConstraintHandle = JointConstraintHandle;
	Params.ActuationConstraintEndPointParticleHandle = JointEndPointParticle;
	Params.TransformOnInit = TransformOnInit;
	Params.PhysicsObject = GetPhysicsObject();
	Params.Solver = GetPhysicsSolver();
	Params.World = GetWorld();
	Params.bIsUsingAsyncPhysics = bIsUsingAsyncPhysics;
	
	SimOutputRecord.Clear();

#if !NO_LOGGING
	// Mover Gameplay Tag Logging
	{
		const AActor* DebugOwner = MoverComp.GetOwner();
		const UWorld* DebugWorld = DebugOwner ? DebugOwner->GetWorld() : nullptr;
		SimOutputRecord.DebugOwnerName = FString::Printf(TEXT("%s[%s]"), *GetNameSafe(DebugOwner), DebugWorld ? *DebugWorld->GetOutermost()->GetName() : TEXT("null"));
	}
	SimOutputRecord.DebugOwnerRole = MoverComp.GetOwner() ? StaticEnum<ENetRole>()->GetNameStringByValue((int64)MoverComp.GetOwner()->GetLocalRole()) : TEXT("null");
	Simulation->DebugOwnerName = SimOutputRecord.DebugOwnerName;
	Simulation->DebugOwnerRole = SimOutputRecord.DebugOwnerRole;
#endif

	UE::ChaosMover::FSimulationOutputData OutputData;
	FMoverInputCmdContext UnusedInputCmd;
	FMoverAuxStateContext UnusedAuxState;
	MoverComp.GetDefaultInputAndState(OUT UnusedInputCmd, OUT OutputData.SyncState, OUT UnusedAuxState);
	MoverComp.OnSimulationStateInitialized(OutputData.SyncState, UnusedAuxState);

	FMoverTimeStep TimeStep;
	if (Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		TimeStep.BaseSimTimeMs = Solver->GetPhysicsResultsTime_External() * 1000.0;
		TimeStep.ServerFrame = Solver->GetCurrentFrame();
		TimeStep.StepMs = Solver->GetAsyncDeltaTime() * 1000.0f;
	}

	SimOutputRecord.Add(TimeStep, OutputData);

	Params.InitialSyncState = OutputData.SyncState;

	Simulation->Init(Params);

	bWantsCreateSim = false;
}

void UChaosMoverBackendComponent::DeinitSimulation()
{
	Simulation->Deinit();
	bWantsDestroySim = false;
}

void UChaosMoverBackendComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		if (UPrimitiveComponent* UpdatedPrimitiveComp = Cast<UPrimitiveComponent>(GetMoverComponent().GetUpdatedComponent()))
		{
			if (!UpdatedPrimitiveComp->IsSimulatingPhysics())
			{
				UE_LOGF(LogChaosMover, Warning, "ChaosMoverBackend: Updated primitive component '%ls' on actor '%ls' does not have Simulate Physics enabled. ChaosMover requires updating the primitive component through physics, so 'Simulate Physics' must be enabled on the updated primitive component.", *GetNameSafe(UpdatedPrimitiveComp), *GetNameSafe(GetOwner()));
			}
		}

		CreatePhysics();

		// Register with the world subsystem
		if (UChaosMoverSubsystem* ChaosMoverSubsystem = UWorld::GetSubsystem<UChaosMoverSubsystem>(GetWorld()))
		{
			ChaosMoverSubsystem->Register(this);
		}

		// Register a callback to watch for component state changes
		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetMoverComponent().GetUpdatedComponent()))
		{
			PrimComp->OnComponentPhysicsStateChanged.AddUniqueDynamic(this, &ThisClass::HandleUpdatedComponentPhysicsStateChanged);
		}
	}
}

void UChaosMoverBackendComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeinitSimulation();
	DestroyPhysics();

	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetMoverComponent().GetUpdatedComponent()))
	{
		PrimComp->OnComponentPhysicsStateChanged.RemoveDynamic(this, &ThisClass::HandleUpdatedComponentPhysicsStateChanged);
	}

	if (UChaosMoverSubsystem* ChaosMoverSubsystem = UWorld::GetSubsystem<UChaosMoverSubsystem>(GetWorld()))
	{
		ChaosMoverSubsystem->Unregister(this);
	}

	Super::EndPlay(EndPlayReason);
}

double UChaosMoverBackendComponent::GetCurrentSimTimeMs()
{
	// Note: this is implicitly an _External function
	if (const Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		return Solver->IsUsingAsyncResults() ? Solver->GetAsyncDeltaTime() * GetCurrentSimFrame() * 1000.0 : Solver->GetSolverTime() * 1000.0;
	}
	return 0.0;
}

int32 UChaosMoverBackendComponent::GetCurrentSimFrame()
{
	// Note: this is implicitly an _External function
	if (UWorld* World = GetWorld())
	{
		return UE::NetworkPhysicsUtils::GetUpcomingServerFrame_External(World);
	}
	return 0;
}

float UChaosMoverBackendComponent::GetEventSchedulingMinDelaySeconds() const
{
	// The event scheduling falls back to using MaxSupportedLatencyPrediction, but this is usually quite high (e.g. 1 second by default, 0.6 seconds on Fortnite)
	// We expect this function to only be called once on init of the mover component, so we can bear the cost of calling FindComponentByClass here.
	float EventSchedulingMinDelaySeconds = 0.3f;
	if (UNetworkPhysicsSettingsComponent* NetworkPhysicsSettingsComponent = GetOwner()->FindComponentByClass<UNetworkPhysicsSettingsComponent>())
	{
		EventSchedulingMinDelaySeconds = NetworkPhysicsSettingsComponent->GetSettings().GeneralSettings.EventSchedulingMinDelaySeconds;
	}
	else
	{
		EventSchedulingMinDelaySeconds = UPhysicsSettings::Get()->PhysicsPrediction.MaxSupportedLatencyPrediction;
	}

	return EventSchedulingMinDelaySeconds;
}

FMoverTime UChaosMoverBackendComponent::GetScheduledNetworkTime(const FMoverTime& Time) const
{
	UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr)
	{
		const double DeltaTime = Solver->GetAsyncDeltaTime();
		const int32 DelayFrames = (DeltaTime > UE_SMALL_NUMBER) ? FMath::CeilToInt32(GetEventSchedulingMinDelaySeconds() / DeltaTime) : 0;
		const int32 ScheduledFrame = Time.FrameCount + DelayFrames;

		return FMoverTime(ScheduledFrame, ScheduledFrame * DeltaTime * 1000);
	}
	return IMoverBackendLiaisonInterface::GetScheduledNetworkTime(Time);
}

bool UChaosMoverBackendComponent::IsAsync() const
{
	return bIsUsingAsyncPhysics;
}

bool UChaosMoverBackendComponent::IsFixedDt() const
{
	return bIsUsingAsyncPhysics;
}

bool UChaosMoverBackendComponent::ShouldResim() const
{
	return Simulation ? Simulation->ShouldResim() : IMoverBackendLiaisonInterface::ShouldResim();
}

UChaosMoverSimulation* UChaosMoverBackendComponent::GetSimulation()
{
	return Simulation;
}

const UChaosMoverSimulation* UChaosMoverBackendComponent::GetSimulation() const
{
	return Simulation;
}

void UChaosMoverBackendComponent::ProduceInputData(int32 PhysicsStep, int32 NumSteps, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData)
{
	Chaos::EnsureIsInGameThreadContext();

	// Recreate the simulation if necessary
	if (bWantsDestroySim)
	{
		DeinitSimulation();
		return;
	}
	if (bWantsCreateSim)
	{
		InitSimulation();
	}

	if (bWantsCreateSim)
	{
		return;
	}

	const bool bIsGeneratingInputsLocally = !NetworkPhysicsComponent || NetworkPhysicsComponent->IsLocallyControlled();
	if (bIsGeneratingInputsLocally)
	{
		GenerateInput(TimeStep, InputData);
	}

	if (UE::ChaosMover::CVars::bEnableServerLaunchOverride)
	{
		const bool bIsServer = NetworkPhysicsComponent && NetworkPhysicsComponent->HasServerWorld();
		if (bIsServer)
		{
			GenerateServerInput(TimeStep, InputData);
		}
	}

	UMoverComponent& MoverComp = GetMoverComponent();

	// Add default simulation input data
	FChaosMoverSimulationDefaultInputs& SimInputs = Simulation->GetLocalSimInput_Mutable().FindOrAddMutableDataByType<FChaosMoverSimulationDefaultInputs>();
	SimInputs.Gravity = MoverComp.GetGravityAcceleration();
	SimInputs.UpDir = MoverComp.GetUpDirection();
	SimInputs.bIsGeneratingInputsLocally = bIsGeneratingInputsLocally;
	APawn* OwnerAsPawn = Cast<APawn>(GetOwner());
	SimInputs.bIsRemotelyControlled = OwnerAsPawn ? (OwnerAsPawn->GetController() && !OwnerAsPawn->IsLocallyControlled()) : false;
	SimInputs.OwningActor = GetOwner();
	SimInputs.World = GetWorld();
	SimInputs.AsyncNetworkPhysicsComponent = NetworkPhysicsComponent ? NetworkPhysicsComponent->GetNetworkPhysicsComponent_Internal() : nullptr;
	SimInputs.PrimaryVisualComponentRelativeTransform = MoverComp.GetBaseVisualComponentTransform();

	// Snapshot warp targets from UMotionWarpingComponent into the replicated input collection.
	// Only the locally-controlled machine has valid targets registered; the network delivers them
	// to all other participants so every endpoint uses consistent targets during root motion warping.
	if (bIsGeneratingInputsLocally)
	{
		FMoverMotionWarpingInputs& WarpInputs = InputData.InputCmd.InputCollection.FindOrAddMutableDataByType<FMoverMotionWarpingInputs>();
		WarpInputs.WarpTargets.Reset();
		if (const UMotionWarpingComponent* WarpComp = MoverComp.GetOwner()->GetComponentByClass<UMotionWarpingComponent>())
		{
			for (const FMotionWarpingTarget& Target : WarpComp->GetWarpTargets())
			{
				FMoverResolvedWarpTarget& Resolved = WarpInputs.WarpTargets.AddDefaulted_GetRef();
				Resolved.Name     = Target.Name;
				const FTransform TargetTransform = Target.GetTargetTrasform();
				Resolved.Location = TargetTransform.GetLocation();
				Resolved.Rotation = TargetTransform.GetRotation();
			}
		}
	}

	if (USceneComponent* UpdatedComponent = MoverComp.GetUpdatedComponent())
	{
		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(UpdatedComponent))
		{
			SimInputs.CollisionQueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(ChaosMoverQuery), false, PrimComp->GetOwner());
			SimInputs.CollisionQueryParams.bTraceIntoSubComponents = false;
			SimInputs.CollisionResponseParams = FCollisionResponseParams(ECR_Overlap);
			SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);
			SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_WorldDynamic, ECR_Block);
			SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_Vehicle, ECR_Block);
			SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_Destructible, ECR_Block);
			SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Block);
			PrimComp->InitSweepCollisionParams(SimInputs.CollisionQueryParams, SimInputs.CollisionResponseParams);

			SimInputs.CollisionChannel = PrimComp->GetCollisionObjectType();
			PrimComp->CalcBoundingCylinder(SimInputs.PawnCollisionRadius, SimInputs.PawnCollisionHalfHeight);
		}
		if (IPhysicsComponent* PhysComp = Cast<IPhysicsComponent>(UpdatedComponent))
		{
			SimInputs.PhysicsObject = PhysComp->GetPhysicsObjectById(0); // Get the root physics object
		}
		if (const APhysicsVolume* CurPhysVolume = UpdatedComponent->GetPhysicsVolume())
		{
			SimInputs.PhysicsObjectGravity = CurPhysVolume->GetGravityZ();
		}
	}

	// Flush any pending move registrations/unregistrations before syncing the PT snapshot,
	// so SetRegisteredMoves always sees the up-to-date RegisteredMoves array.
	MoverComp.FlushPendingMoveRegistrations();

	// Keep the cached RegisteredMoves snapshot current so the simulation thread can re-link
	// FLayeredMoveInstance logic pointers after network deserialization.
	if (const TArray<TObjectPtr<ULayeredMoveLogic>>* RegisteredMoves = MoverComp.GetRegisteredMoves())
	{
		Simulation->SetRegisteredMoves(*RegisteredMoves);
	}

	// Evict past-due entries from the local sim input so stale moves are not re-marshalled to the Sim on every tick
	CullPastDueLocalSimInputMoves();

	// TODO: Generalize this to sim commands so we can support this mechanism for IMEs, layered moves, modifiers, in one single queue. Also each command could be networked or not (the local ones don't get marshalled via input)
	// and scheduled or not
	// This needs to happen after local sim inputs because some of the logic reads from local sim inputs
	InjectInstantMovementEffectsIntoSim(InputData);
	InjectLayeredMovesIntoSim(InputData);
	InjectLayeredMoveInstancesIntoSim(InputData);

	// TODO @Harsha consider moving ProduceLocalInput() to the base mover component class
	if (const UChaosCharacterMoverComponent* ChaosCharacterMoverComp = Cast<UChaosCharacterMoverComponent>(&MoverComp))
	{
		ChaosCharacterMoverComp->ProduceLocalInput(Simulation->GetLocalSimInput_Mutable());
	}

	if (MoverComp.OnPreSimulationTick.IsBound())
	{
		MoverComp.OnPreSimulationTick.Broadcast(TimeStep, InputData.InputCmd);
	}
}

void UChaosMoverBackendComponent::GenerateInput(const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData)
{
	UMoverComponent& MoverComp = GetMoverComponent();
	MoverComp.ProduceInput(TimeStep.StepMs, &InputData.InputCmd);
}

void UChaosMoverBackendComponent::GenerateServerInput(const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData)
{
	UMoverComponent& MoverComp = GetMoverComponent();

	if (UChaosCharacterMoverComponent* CharacterMoverComp = Cast<UChaosCharacterMoverComponent>(&MoverComp))
	{
		CharacterMoverComp->ProduceServerInput(TimeStep.StepMs, &InputData.InputCmd);
	}
}

void UChaosMoverBackendComponent::InjectInstantMovementEffectsIntoSim(UE::ChaosMover::FSimulationInputData& OutInputData)
{
	const bool bIsGeneratingInputsLocally = !NetworkPhysicsComponent || NetworkPhysicsComponent->IsLocallyControlled();

	TArray<FScheduledInstantMovementEffect> Effects = Simulation->ConsumeQueuedInstantMovementEffects();
	if (!Effects.IsEmpty())
	{
		if (UE::ChaosMover::CVars::bNetworkInstantMovementEffectsWithSimActions && NetworkPhysicsComponent)
		{
			// Sim action path: all roles independently detect from GetLastInputCmd() in PostSim and enqueue
			// the same action with Proposed style. Server-authoritative and cheat-resistant.
			// BP must read GetLastInputCmd() in PostSim -- NOT the PreSim input -- for this to work correctly.
			InjectInstantMovementEffectsAsActions(Effects);
		}
		else if (bIsGeneratingInputsLocally)
		{
			// Input generating simulations inject instant movement effects from the Game Thread into the simulation input collection.
			// It will be used directly by the state machine (same as when it is received from the network or overwritten during resim),
			// and will also be networked via NetInputCmd.
			InjectInstantMovementEffectsIntoInput(OutInputData, Effects);
		}
		else
		{
			// Transfer queued instant movement effects to the simulation.
			// Instant effects should still be consumed by a non input generating actor on a server if it is not controlled remotely
			// (this is an approximation of "no other instance is input producing").
			InjectInstantMovementEffectsIntoLocalSimInput(Effects);
		}
	}
}

void UChaosMoverBackendComponent::InjectInstantMovementEffectsIntoInput(UE::ChaosMover::FSimulationInputData& OutInputData, const TArray<FScheduledInstantMovementEffect>& Effects)
{
	FChaosNetInstantMovementEffectsQueue& NetInstantMovementEffectsQueue = OutInputData.InputCmd.InputCollection.FindOrAddMutableDataByType<FChaosNetInstantMovementEffectsQueue>();
	ConvertToSerializable(Effects, NetInstantMovementEffectsQueue);
}

void UChaosMoverBackendComponent::InjectInstantMovementEffectsIntoLocalSimInput(const TArray<FScheduledInstantMovementEffect>& Effects)
{
	FMoverDataCollection& LocalSimInput = Simulation->GetLocalSimInput_Mutable();
	FChaosMoverSimulationDefaultInputs& SimInputs = LocalSimInput.FindOrAddMutableDataByType<FChaosMoverSimulationDefaultInputs>();
	ENetMode NetMode = SimInputs.OwningActor ? SimInputs.OwningActor->GetNetMode() : NM_MAX;
	if (NetMode == NM_DedicatedServer || (NetMode == NM_ListenServer && !SimInputs.bIsRemotelyControlled))
	{
		FChaosNetInstantMovementEffectsQueue& NetInstantMovementEffectsQueue = LocalSimInput.FindOrAddMutableDataByType<FChaosNetInstantMovementEffectsQueue>();
		ConvertToSerializable(Effects, NetInstantMovementEffectsQueue);
	}
}

void UChaosMoverBackendComponent::InjectInstantMovementEffectsAsActions(const TArray<FScheduledInstantMovementEffect>& Effects)
{
	if (!NetworkPhysicsComponent)
	{
		return;
	}

	for (const FScheduledInstantMovementEffect& Scheduled : Effects)
	{
		if (!Scheduled.Effect.IsValid())
		{
			continue;
		}

		const UScriptStruct* ScriptStruct = Scheduled.Effect->GetScriptStruct();
		if (!ensure(ScriptStruct))
		{
			continue;
		}

		FChaosMoverInstantMovementEffectAction Action;
		// Proposed: all roles independently enqueue this action from the same last-used input (PostSim).
		// Unlike ProposedAutonomousOnly, Proposed lets SimulatedProxy also apply the action locally.
		Action.AuthorStyle = FNetworkPhysicsActionPayload::EActionAuthorStyle::Proposed;
		// Clone via virtual dispatch to respect any custom copy logic in derived effect types.
		TUniquePtr<FInstantMovementEffect> Cloned(Scheduled.Effect.Get()->Clone());
		Action.Effect.InitializeAsScriptStruct(ScriptStruct, reinterpret_cast<const uint8*>(Cloned.Get()));

		// SourceId = 0: action is triggered by the owner of this NetworkPhysicsComponent (no external object hash needed).
		NetworkPhysicsComponent->EnqueueScheduledAction_External(Action, /* SourceId = */ 0u, 0.f, /* bReliable = */ true);

#if !UE_BUILD_SHIPPING
		UE_LOGF(LogChaosMover, Verbose, "UChaosMoverBackendComponent: Scheduling instant effect as sim action [%ls]: %ls.",
			*Scheduled.SchedulingInfo.ToString(), Scheduled.Effect.IsValid() ? *Scheduled.Effect->ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
#endif // !UE_BUILD_SHIPPING
	}
}

void UChaosMoverBackendComponent::ConvertToSerializable(const TArray<FScheduledInstantMovementEffect>& ScheduledInstantMovementEffects, FChaosNetInstantMovementEffectsQueue& OutNetInstantMovementEffectsQueue)
{
	OutNetInstantMovementEffectsQueue.Effects.Empty();
	for (const FScheduledInstantMovementEffect& ScheduledEffect : ScheduledInstantMovementEffects)
	{
		uint8 UniqueID = GetNextUniqueSimCommandID();

		// All effects issued by the Game Thread can't roll back since the GT will not be resimmed and will not have a chance to reissue them
		OutNetInstantMovementEffectsQueue.Add(ScheduledEffect, /*bShouldRollBack = */false, UniqueID);

#if !UE_BUILD_SHIPPING
		ENetMode NetMode = GetWorld() ? GetWorld()->GetNetMode() : NM_MAX;
		UE_LOGF(LogChaosMover, Verbose, "(%ls) UChaosMoverBackendComponent: Transferring Instant Effect Scheduled [%ls] (Assigning Net ID %d) from the mover component to the async simulation: %ls.",
			*ToString(NetMode), *ScheduledEffect.SchedulingInfo.ToString(), UniqueID, ScheduledEffect.Effect.IsValid() ? *ScheduledEffect.Effect->ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
#endif // !UE_BUILD_SHIPPING
	}
	// The effects queue is cleared in ProduceInputData
}

void UChaosMoverBackendComponent::InjectLayeredMovesIntoSim(UE::ChaosMover::FSimulationInputData& OutInputData)
{
	const bool bIsGeneratingInputsLocally = !NetworkPhysicsComponent || NetworkPhysicsComponent->IsLocallyControlled();

	TArray<FScheduledLayeredMove> Moves = Simulation->ConsumeQueuedLayeredMoves();
	if (!Moves.IsEmpty())
	{
		if (UE::ChaosMover::CVars::bNetworkMovesWithSimActions && NetworkPhysicsComponent)
		{
			// Sim action path: all roles independently detect from GetLastInputCmd() in PostSim and enqueue
			// the same action with Proposed style. Server-authoritative and cheat-resistant.
			// BP must read GetLastInputCmd() in PostSim -- NOT the PreSim input -- for this to work correctly.
			InjectLayeredMovesAsActions(Moves);
		}
		else if (bIsGeneratingInputsLocally)
		{
			// Input generating simulations inject moves from the Game Thread into the simulation input collection.
			// It will be used directly by the state machine (same as when it is received from the network or overwritten during resim),
			// and will also be networked via NetInputCmd.
			InjectLayeredMovesIntoInput(OutInputData, Moves);
		}
		else
		{
			// Transfer queued moves to the simulation.
			// Moves should still be consumed by a non input generating actor on a server if it is not controlled remotely
			// (this is an approximation of "no other instance is input producing").
			InjectLayeredMovesIntoLocalSimInput(Moves);
		}
	}
}

// Transfers queued layered moves to the (networked) simulation input.
// This is used in the input producing case.
void UChaosMoverBackendComponent::InjectLayeredMovesIntoInput(UE::ChaosMover::FSimulationInputData& OutInputData, const TArray<FScheduledLayeredMove>& Moves)
{
	FChaosNetLayeredMovesQueue& NetLayeredMovesQueue = OutInputData.InputCmd.InputCollection.FindOrAddMutableDataByType<FChaosNetLayeredMovesQueue>();
	ConvertToSerializable(Moves, NetLayeredMovesQueue);
}

// Transfers queued layered moves to the simulation directly, bypassing the networked input.
// This is used in the non input producing case.
void UChaosMoverBackendComponent::InjectLayeredMovesIntoLocalSimInput(const TArray<FScheduledLayeredMove>& Moves)
{
	FMoverDataCollection& LocalSimInput = Simulation->GetLocalSimInput_Mutable();
	FChaosMoverSimulationDefaultInputs& SimInputs = LocalSimInput.FindOrAddMutableDataByType<FChaosMoverSimulationDefaultInputs>();
	ENetMode NetMode = SimInputs.OwningActor ? SimInputs.OwningActor->GetNetMode() : NM_MAX;
	if (NetMode == NM_DedicatedServer || (NetMode == NM_ListenServer && !SimInputs.bIsRemotelyControlled))
	{
		FChaosNetLayeredMovesQueue& NetLayeredMovesQueue = LocalSimInput.FindOrAddMutableDataByType<FChaosNetLayeredMovesQueue>();
		ConvertToSerializable(Moves, NetLayeredMovesQueue);
	}
}

void UChaosMoverBackendComponent::InjectLayeredMovesAsActions(const TArray<FScheduledLayeredMove>& Moves)
{
	if (!NetworkPhysicsComponent)
	{
		return;
	}

	const TArray<FScheduledLayeredMove>& ScheduledLayeredMoves = Moves;

	for (const FScheduledLayeredMove& Scheduled : ScheduledLayeredMoves)
	{
		if (!Scheduled.Move.IsValid())
		{
			continue;
		}

		const FLayeredMoveBase* MovePtr = Scheduled.Move.Get();
		const UScriptStruct* ScriptStruct = MovePtr->GetScriptStruct();
		if (!ensure(ScriptStruct))
		{
			continue;
		}

		FChaosMoverLayeredMoveAction Action;
		// Proposed: all roles independently enqueue this action from the same last-used input (PostSim).
		// Unlike ProposedAutonomousOnly, Proposed lets SimulatedProxy also apply the action locally.
		Action.AuthorStyle = FNetworkPhysicsActionPayload::EActionAuthorStyle::Proposed;
		// Clone via virtual dispatch to respect any custom copy logic in derived move types.
		TUniquePtr<FLayeredMoveBase> Cloned(MovePtr->Clone());
		Action.Move.InitializeAsScriptStruct(ScriptStruct, reinterpret_cast<const uint8*>(Cloned.Get()));

		// SourceId = 0: action is triggered by the owner of this NetworkPhysicsComponent (no external object hash needed).
		NetworkPhysicsComponent->EnqueueScheduledAction_External(Action, /* SourceId = */ 0u, 0.f, /* bReliable = */ true);

#if !UE_BUILD_SHIPPING
		UE_LOGF(LogChaosMover, Verbose, "UChaosMoverBackendComponent: Scheduling layered move as sim action [%ls]: %ls.",
			*Scheduled.SchedulingInfo.ToString(), MovePtr->ToSimpleString().IsEmpty() ? TEXT("UNKNOWN MOVE") : *MovePtr->ToSimpleString());
#endif // !UE_BUILD_SHIPPING
	}
}

void UChaosMoverBackendComponent::InjectLayeredMoveInstancesAsActions(const TArray<FChaosScheduledLayeredMoveInstance>& ScheduledInstances)
{
	if (!NetworkPhysicsComponent)
	{
		return;
	}

	for (const FChaosScheduledLayeredMoveInstance& Scheduled : ScheduledInstances)
	{
		if (!Scheduled.Move.IsValid() || !Scheduled.Move->HasLogic())
		{
			continue;
		}

		const FLayeredMoveInstancedData* DataPtr = Scheduled.Move->GetMoveData();
		if (!ensure(DataPtr))
		{
			continue;
		}

		const UScriptStruct* DataScriptStruct = DataPtr->GetScriptStruct();
		if (!ensure(DataScriptStruct))
		{
			continue;
		}

		FChaosMoverLayeredMoveInstanceAction Action;
		// Proposed: all roles independently enqueue this action from the same last-used input (PostSim).
		// Unlike ProposedAutonomousOnly, Proposed lets SimulatedProxy also apply the action locally.
		Action.AuthorStyle = FNetworkPhysicsActionPayload::EActionAuthorStyle::Proposed;
		Action.MoveLogicClass = const_cast<UClass*>(Scheduled.Move->GetLogicClass());
		// Clone via virtual dispatch to respect any custom copy logic in derived data types.
		TUniquePtr<FLayeredMoveInstancedData> Cloned(DataPtr->Clone());
		Action.InstancedData.InitializeAsScriptStruct(DataScriptStruct, reinterpret_cast<const uint8*>(Cloned.Get()));

		// SourceId = 0: action is triggered by the owner of this NetworkPhysicsComponent (no external object hash needed).
		NetworkPhysicsComponent->EnqueueScheduledAction_External(Action, /* SourceId = */ 0u, 0.f, /* bReliable = */ true);

#if !UE_BUILD_SHIPPING
		UE_LOGF(LogChaosMover, Verbose, "UChaosMoverBackendComponent: Scheduling layered move instance as sim action [%ls]: LogicClass=%ls.",
			*Scheduled.SchedulingInfo.ToString(), Scheduled.Move->GetLogicClass() ? *Scheduled.Move->GetLogicClass()->GetName() : TEXT("null"));
#endif // !UE_BUILD_SHIPPING
	}
}

void UChaosMoverBackendComponent::CullPastDueLocalSimInputMoves()
{
	const int32 CurrentServerFrame = GetCurrentSimFrame();
	FMoverDataCollection& LocalSimInput = Simulation->GetLocalSimInput_Mutable();

	if (FChaosNetLayeredMovesQueue* Queue = LocalSimInput.FindMutableDataByType<FChaosNetLayeredMovesQueue>())
	{
		Queue->Moves.RemoveAll([CurrentServerFrame](const FChaosNetLayeredMove& Move)
		{
			return Move.ExecutionServerFrame < CurrentServerFrame;
		});
	}

	if (FChaosNetLayeredMoveInstancesQueue* Queue = LocalSimInput.FindMutableDataByType<FChaosNetLayeredMoveInstancesQueue>())
	{
		Queue->Moves.RemoveAll([CurrentServerFrame](const FChaosNetLayeredMoveInstance& Move)
		{
			return Move.ExecutionServerFrame < CurrentServerFrame;
		});
	}
}

void UChaosMoverBackendComponent::QueueLayeredMoveInstance(TSharedPtr<FLayeredMoveInstance> Move)
{
	ensure(IsInGameThread());
	if (Simulation)
	{
		Simulation->QueueLayeredMoveInstance(Move);
	}
}

void UChaosMoverBackendComponent::InjectLayeredMoveInstancesIntoSim(UE::ChaosMover::FSimulationInputData& OutInputData)
{
	TArray<TSharedPtr<FLayeredMoveInstance>> RawInstances = Simulation->ConsumeQueuedLayeredMoveInstances();
	if (RawInstances.IsEmpty())
	{
		return;
	}

	// Build scheduled wrappers with timing computed at inject time.
	// GT-queued instances are never rolled back: the GT is not resimulated and cannot re-issue them.
	const FMoverTime IssuanceTime = FMoverTime(GetCurrentSimFrame(), /* TimeMs = */ 0.0);
	const FMoverTime ExecutionTime = GetScheduledNetworkTime(IssuanceTime);
	TArray<FChaosScheduledLayeredMoveInstance> ScheduledInstances;
	ScheduledInstances.Reserve(RawInstances.Num());
	for (const TSharedPtr<FLayeredMoveInstance>& MoveInstance : RawInstances)
	{
		FChaosScheduledLayeredMoveInstance& Scheduled = ScheduledInstances.AddDefaulted_GetRef();
		Scheduled.bShouldRollBack = false;
		Scheduled.SchedulingInfo  = FMoverSchedulingInfo(IssuanceTime, ExecutionTime, /* bIsFixedDt = */ true);
		Scheduled.Move            = MoveInstance;
	}

	const bool bIsGeneratingInputsLocally = !NetworkPhysicsComponent || NetworkPhysicsComponent->IsLocallyControlled();
	if (UE::ChaosMover::CVars::bNetworkMovesWithSimActions && NetworkPhysicsComponent)
	{
		// Sim action path: all roles independently detect from GetLastInputCmd() in PostSim and enqueue
		// the same action with Proposed style. Server-authoritative and cheat-resistant.
		// BP must read GetLastInputCmd() in PostSim -- NOT the PreSim input -- for this to work correctly.
		InjectLayeredMoveInstancesAsActions(ScheduledInstances);
	}
	else if (bIsGeneratingInputsLocally)
	{
		InjectLayeredMoveInstancesIntoInput(OutInputData, ScheduledInstances);
	}
	else
	{
		InjectLayeredMoveInstancesIntoLocalSimInput(ScheduledInstances);
	}
}

void UChaosMoverBackendComponent::InjectLayeredMoveInstancesIntoInput(UE::ChaosMover::FSimulationInputData& OutInputData, const TArray<FChaosScheduledLayeredMoveInstance>& ScheduledInstances)
{
	FChaosNetLayeredMoveInstancesQueue& NetQueue = OutInputData.InputCmd.InputCollection.FindOrAddMutableDataByType<FChaosNetLayeredMoveInstancesQueue>();
	ConvertToSerializable(ScheduledInstances, NetQueue);
}

void UChaosMoverBackendComponent::InjectLayeredMoveInstancesIntoLocalSimInput(const TArray<FChaosScheduledLayeredMoveInstance>& ScheduledInstances)
{
	FMoverDataCollection& LocalSimInput = Simulation->GetLocalSimInput_Mutable();
	FChaosMoverSimulationDefaultInputs& SimInputs = LocalSimInput.FindOrAddMutableDataByType<FChaosMoverSimulationDefaultInputs>();
	ENetMode NetMode = SimInputs.OwningActor ? SimInputs.OwningActor->GetNetMode() : NM_MAX;
	if (NetMode == NM_DedicatedServer || (NetMode == NM_ListenServer && !SimInputs.bIsRemotelyControlled))
	{
		FChaosNetLayeredMoveInstancesQueue& NetQueue = LocalSimInput.FindOrAddMutableDataByType<FChaosNetLayeredMoveInstancesQueue>();
		ConvertToSerializable(ScheduledInstances, NetQueue);
	}
}

uint8 UChaosMoverBackendComponent::GetNextUniqueSimCommandID()
{
	uint8 UniqueID = NextUniqueSimCommandID++;
	if (UniqueID == 0xFF)
	{
		UniqueID = 0;
		NextUniqueSimCommandID = 1;
	}
	return UniqueID;
}

void UChaosMoverBackendComponent::ConvertToSerializable(const TArray<FScheduledLayeredMove>& ScheduledLayeredMoves, FChaosNetLayeredMovesQueue& OutNetLayeredMovesQueue)
{
	OutNetLayeredMovesQueue.Moves.Empty();
	for (const FScheduledLayeredMove& ScheduledMove : ScheduledLayeredMoves)
	{
		uint8 UniqueID = GetNextUniqueSimCommandID();

		// All moves issued by the Game Thread can't roll back since the GT will not be resimmed and will not have a chance to reissue them
		OutNetLayeredMovesQueue.Add(ScheduledMove, /*bShouldRollBack = */false, UniqueID);

#if !UE_BUILD_SHIPPING
		ENetMode NetMode = GetWorld() ? GetWorld()->GetNetMode() : NM_MAX;
		UE_LOGF(LogChaosMover, Verbose, "(%ls) UChaosMoverBackendComponent: Transferring move scheduled [%ls] (Assigning Net ID %d) from the mover component to the async simulation: %ls.",
			*ToString(NetMode), *ScheduledMove.SchedulingInfo.ToString(), UniqueID, ScheduledMove.Move.IsValid() ? *ScheduledMove.Move->ToSimpleString() : TEXT("INVALID MOVE"));
#endif // !UE_BUILD_SHIPPING
	}
	// The moves queue is cleared in ProduceInputData
}

void UChaosMoverBackendComponent::ConvertToSerializable(const TArray<FChaosScheduledLayeredMoveInstance>& ScheduledMoveInstances, FChaosNetLayeredMoveInstancesQueue& OutNetQueue)
{
	OutNetQueue.Moves.Empty();
	for (const FChaosScheduledLayeredMoveInstance& ScheduledMove : ScheduledMoveInstances)
	{
		uint8 UniqueID = GetNextUniqueSimCommandID();
		OutNetQueue.Add(ScheduledMove, ScheduledMove.bShouldRollBack, UniqueID);
#if !UE_BUILD_SHIPPING
		ENetMode NetMode = GetWorld() ? GetWorld()->GetNetMode() : NM_MAX;
		UE_LOGF(LogChaosMover, Verbose, "(%ls) UChaosMoverBackendComponent: Transferring Move Instance scheduled [%ls] (Assigning Net ID %d) from the backend to the async simulation.",
			*ToString(NetMode), *ScheduledMove.SchedulingInfo.ToString(), UniqueID);
#endif // !UE_BUILD_SHIPPING
	}
}

void UChaosMoverBackendComponent::ConsumeOutputData(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationOutputData& OutputData)
{
	Chaos::EnsureIsInGameThreadContext();

	if (bWantsCreateSim)
	{
		return;
	}

	if (OutputData.IsValid())
	{
		SimOutputRecord.Add(TimeStep, OutputData);

		if (!TimeStep.bIsResimulating)
		{
			PreRollbackTimeStep = TimeStep;
		}

		// Mover Gameplay Tag Logging
		MOVER_TAG_LOG(LogChaosMover, "[GT:Consume] Actor=%s Role=%s Frame=%d TimeMs=%.3f IsResim=%d IsFirstResim=%d | EventsInRecord=%d",
			*Simulation->DebugOwnerName, *Simulation->DebugOwnerRole, TimeStep.ServerFrame, TimeStep.BaseSimTimeMs, (int32)TimeStep.bIsResimulating, (int32)TimeStep.bIsFirstResimFrame, 0);
	}
}

void UChaosMoverBackendComponent::OnSimulationRollback(const FMoverSyncState& NewSyncState, const FMoverTimeStep& NewBaseTimeStep, const FMoverTimeStep& InPreRollbackTimeStep)
{
	Chaos::EnsureIsInGameThreadContext();

	// Discard predicted events from the frames being re-simulated.
	// Gameplay Tag Specific: the corrective rollback events produced by DiffAndEmitGameplayTagEvents
	// arrive in the first resim frame's ConsumeOutputData call and will be added then, so they are
	// unaffected by this pruning.
	SimOutputRecord.PruneEventsFromFrame(NewBaseTimeStep.ServerFrame);

	// Mover Gameplay Tag Logging
	MOVER_TAG_LOG(LogChaosMover, "[GT:SimRecordPrune] Actor=%s Role=%s Frame=%d IsResim=%d IsRollback=%d | pruned events with ServerFrame>=%d",
		*Simulation->DebugOwnerName, *Simulation->DebugOwnerRole, NewBaseTimeStep.ServerFrame, (int32)NewBaseTimeStep.bIsResimulating, (int32)true, NewBaseTimeStep.ServerFrame);
}

void UChaosMoverBackendComponent::FinalizeFrame(double ResultsTimeInMs)
{
	Chaos::EnsureIsInGameThreadContext();

	if (bWantsCreateSim)
	{
		return;
	}

	Chaos::FPhysicsSolver* Solver = GetPhysicsSolver();
	if (!Solver)
	{
		return;
	}

	UMoverComponent& MoverComp = GetMoverComponent();

	FMoverTimeStep TimeStep;
	UE::ChaosMover::FSimulationOutputData InterpolatedOutput;
	// ResultsTimeInMs is the time at the end of the step while CreateInterpolatedResult expects the time at the beginning of the step so we correct by subtracting one async dt
	const double AtBaseTimeMs = ResultsTimeInMs - Solver->GetAsyncDeltaTime() * 1000.0;

	// Mover Gameplay Tag Logging
	MOVER_TAG_LOG(LogChaosMover, "[GT:Finalize] Actor=%s Role=%s AtTimeMs=%.3f", *SimOutputRecord.DebugOwnerName, *Simulation->DebugOwnerRole, AtBaseTimeMs);

	SimOutputRecord.CreateInterpolatedResult(AtBaseTimeMs, /*OUT*/ TimeStep, /*OUT*/ InterpolatedOutput);

	// Physics interactions in the last frame may have caused a change in position or velocity that's different from what a simple lerp would predict,
	// so stomp the lerped sync state's transform data with that of the actual particle after the last sim frame
	FMoverDefaultSyncState& TransformSyncState = InterpolatedOutput.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	if (Chaos::FPBDRigidParticle* Particle = GetControlledParticle())
	{
		TransformSyncState.SetTransforms_WorldSpace(
			Particle->GetX(),
			FRotator(Particle->GetR()),
			Particle->GetV(),
			FMath::RadiansToDegrees(Particle->GetW()),
			TransformSyncState.GetMovementBase(),
			TransformSyncState.GetMovementBaseBoneName());

		if (!Chaos::FVec3::IsNearlyEqual(Chaos::FVec3(TransformSyncState.GetLocation_WorldSpace()), Particle->GetX(), UE_KINDA_SMALL_NUMBER))
		{
			UE_LOGF(LogChaosMover, Warning, "SyncState location (%ls) differs from particle location (%ls)", *TransformSyncState.GetLocation_WorldSpace().ToString(), *Particle->GetX().ToString());
		}
	}

	MoverComp.SetSimulationOutput(TimeStep, InterpolatedOutput);
	
	if (MoverComp.OnPostSimulationTick.IsBound())
	{
		MoverComp.OnPostSimulationTick.Broadcast(TimeStep);
	}

	if (MoverComp.OnPostFinalize.IsBound())
	{
		FMoverAuxStateContext UnusedAuxStateContext;
		MoverComp.OnPostFinalize.Broadcast(InterpolatedOutput.SyncState, UnusedAuxStateContext);
	}
}
