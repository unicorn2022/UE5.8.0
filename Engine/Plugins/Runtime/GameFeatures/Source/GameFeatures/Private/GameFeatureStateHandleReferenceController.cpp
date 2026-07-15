// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureStateHandleReferenceController.h"
#include "GameFeatureStateHandleReferenceControllerInternal.h"
#include "GameFeaturePluginOperationResult.h"
#include "Algo/TopologicalSort.h"
#include "GameFeaturesSubsystem.h"
#include "Logging/LogMacros.h"
#include "Logging/StructuredLog.h"
#include "Interfaces/IPluginManager.h"
#include "PluginReferenceDescriptor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Modules/ModuleManager.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameFeatureStateHandle, Log, All);

// A quick way for now to disable and fallback to how things are now
// something to remove as this system is proved out
static bool GGameFeatureStateHandleEnabled = true;
static FAutoConsoleVariableRef CVarGameFeatureStateHandleEnabled(
	TEXT("GameFeatureStateHandle.Enabled"),
	GGameFeatureStateHandleEnabled,
	TEXT("Enable GameFeatureStateHandle owning the lifetime of GFPs. Disable if you want to disable the clean up code for the controller"),
	ECVF_Default
);

static FString GetPluginNameFromURL(const FString& PluginURL)
{
	FGameFeaturePluginIdentifier GFPId(PluginURL);
	return FString(GFPId.GetPluginName());
}

static FString GetPluginURLFromName(const FString& PluginName)
{
	FString OutPluginURL;
	UGameFeaturesSubsystem::Get().GetPluginURLByName(PluginName, OutPluginURL);
	return OutPluginURL;
}

void FGameFeatureStateRefCount::AddRef(const FGuid& StateHandleId, EGameFeaturePluginState InDestPluginState)
{
	EGameFeaturePluginState& DestPluginState = OwnerToDestState.FindOrAdd(StateHandleId);
	if (InDestPluginState > DestPluginState)
	{
		DestPluginState = InDestPluginState;
	}

	if (DestPluginState > HighestDestPluginState)
	{
		HighestDestPluginState = DestPluginState;
	}
}

bool FGameFeatureStateRefCount::RemoveRefAndRequiresDowngrading(const FGuid& StateHandleId)
{
	bool bNeedsDowngrading = false;

	EGameFeaturePluginState RefDestStateRemoved = EGameFeaturePluginState::Uninitialized;
	if (OwnerToDestState.RemoveAndCopyValue(StateHandleId, RefDestStateRemoved))
	{
		// We just removed a ref count to the highest plugin state, have to find the next highest
		if (RefDestStateRemoved >= HighestDestPluginState)
		{
			// find the next highest dest plugin state and check if its lower or equal to our current
			EGameFeaturePluginState NextHighestDestPluginState = EGameFeaturePluginState::Installed;
			for (const TPair<FGuid, EGameFeaturePluginState>& OwnerState : OwnerToDestState)
			{
				if (OwnerState.Value > NextHighestDestPluginState)
				{
					NextHighestDestPluginState = OwnerState.Value;

					// cant be higher then this, so break early
					if (NextHighestDestPluginState >= EGameFeaturePluginState::Active)
					{
						break;
					}
				}
			}

			// Our next highest dest plugin state found in our map of owners is lower then the one we just removed and requires downgrading
			if (NextHighestDestPluginState < HighestDestPluginState)
			{
				HighestDestPluginState = NextHighestDestPluginState;
				bNeedsDowngrading = true;
			}
		}
	}

	// if our highest plugin state has changed to a lower plugin state we need to downgrade this plugin to the new highest
	return bNeedsDowngrading;
}

bool FGameFeatureStateRefCount::GetHighestRefCountDestPluginState(EGameFeaturePluginState& OutDestPluginState) const
{
	if (OwnerToDestState.IsEmpty())
	{
		OutDestPluginState = EGameFeaturePluginState::Uninitialized;
		return false;
	}

	OutDestPluginState = HighestDestPluginState;
	return true;
}

bool FGameFeatureStateRefCount::IsEmpty() const
{
	return OwnerToDestState.IsEmpty();
}

const TMap<FGuid, EGameFeaturePluginState>& FGameFeatureStateRefCount::GetOwnerToDestMap() const
{
	return OwnerToDestState;
}

// Only needed for ChangeTargetState when we downgrade states
static bool PluginStateToTargetState(EGameFeaturePluginState PluginState, EGameFeatureTargetState& OutTargetState)
{
	switch (PluginState)
	{
	case EGameFeaturePluginState::Installed:
		OutTargetState = EGameFeatureTargetState::Installed;
		return true;
	case EGameFeaturePluginState::Registered:
		OutTargetState = EGameFeatureTargetState::Registered;
		return true;
	case EGameFeaturePluginState::Loaded:
		OutTargetState = EGameFeatureTargetState::Loaded;
		return true;
	case EGameFeaturePluginState::Active:
		OutTargetState = EGameFeatureTargetState::Active;
		return true;
	default:
		break;
	}

	OutTargetState = EGameFeatureTargetState::Installed;
	return false;
}

FGameFeatureStateHandleReferenceController::FGameFeatureStateHandleReferenceController()
	: Internal(MakeUnique<FGameFeatureStateHandleReferenceControllerInternal>())
{
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ListGameFeatureStateHandles"),
		TEXT("List all the GameFeature StateHandles and their RefCounts"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([this](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			Internal->ListGameFeatureStateHandles(Args, World, Ar);
		}),
		ECVF_Default);
}

FGameFeatureStateHandleReferenceController& FGameFeatureStateHandleReferenceController::Get()
{
	static FGameFeatureStateHandleReferenceController Singleton;
	return Singleton;
}

bool FGameFeatureStateHandleReferenceController::IsLifetimeControlEnabled()
{
	return GGameFeatureStateHandleEnabled;
}

void FGameFeatureStateHandleReferenceController::RegisterNewStateHandle(const FGameFeatureStateHandle& StateHandle, const FString& Owner, EGameFeatureStateHandleOptions Options)
{
	FGuid StateHandleId = StateHandle.GetUniqueId();

	FScopeLock Lock(&Internal->StateHandlesLock);
	if (Internal->StateHandles.Contains(StateHandleId))
	{
		UE_LOGF(LogGameFeatureStateHandle, Error, "Registering a StateHandle that already exists, each StateHandle should be uniquely added");
		return;
	}

	Internal->StateHandles.Add(StateHandleId, FGameFeatureStateHandleInternal(Owner, Options));

	UE_LOGF(LogGameFeatureStateHandle, Verbose, "Registered new StateHandle: %ls %ls", *Owner, *StateHandle.ToString());
}

void FGameFeatureStateHandleReferenceController::UnregisterStateHandle(const FGameFeatureStateHandle& StateHandle)
{
	if (!IsEngineExitRequested())
	{
		FScopeLock Lock(&Internal->StateHandlesLock);
		UE_LOGF(LogGameFeatureStateHandle, Verbose, "Unregistereing and invalidating StateHandle: %ls %i", *StateHandle.ToString(), Internal->StateHandles.Num());

		Internal->StateHandles.Remove(StateHandle.GetUniqueId());
	}
}

// TODO possibly its faster/better to write an EnumerateOverDepends + a lambda callback to avoid storing a data structure to return
static TMap<FString, EGameFeaturePluginState> CollectDependencies(const FString& GameFeaturePluginURL, EGameFeaturePluginState PluginState, const FGameFeatureStateHandleInternal& InternalHandle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameFeatureStateHandle_CollectDependencies);

	UGameFeaturesSubsystem& GameFeatures = UGameFeaturesSubsystem::Get();
	TMap<FString, EGameFeaturePluginState> OutDependencies;
	OutDependencies.Add(GameFeaturePluginURL, PluginState);

	TArray<FString> DFSStack;
	DFSStack.Add(GameFeaturePluginURL);
	while (!DFSStack.IsEmpty())
	{
		const FString CurrentPluginURL = DFSStack.Pop(EAllowShrinking::No);
		EGameFeaturePluginState CurrentPluginRequiredState = EGameFeaturePluginState::Registered;

		// Find our current minimal required state from out depends for this plugin, to check our depends to see if their minimal state may be active or registered
		if (EGameFeaturePluginState* CurrentPluginState = OutDependencies.Find(CurrentPluginURL))
		{
			CurrentPluginRequiredState = *CurrentPluginState;
		}

		FGameFeaturePluginDetails PluginDetails;
		GameFeatures.GetGameFeaturePluginDetails(CurrentPluginURL, PluginDetails);

		// For our current plugin, iterate over its depends
		//   if our current plugin is set to be active and the depends has bShouldActivate
		//     Make minimual required state for the depends be Active
		//   else
		//     Miniumual required state is Registered
		//
		// We need to check if our Depends is already in our OutDepends, and if its new minimual required state is higher
		// then the one stored in our Out, due to the current plugin has a higher required state we need to update our minimual required state
		for (const FGameFeaturePluginReferenceDetails& Dependency : PluginDetails.PluginDependencies)
		{
			FString DependsPluginURL;
			if (GameFeatures.GetPluginURLByName(Dependency.PluginName, DependsPluginURL))
			{
				FGameFeaturePluginDetails DependsPluginDetails;
				if (GameFeatures.GetGameFeaturePluginDetails(DependsPluginURL, DependsPluginDetails))
				{
					// Lets assume our AutoState as our plugins minimal state, if we are installed as an AutoState we are Registered here since we our parent here is at least registered
					EGameFeaturePluginState DependsMinimualRequiredState = UGameFeaturesSubsystem::ConvertInitialFeatureStateToTargetState(DependsPluginDetails.BuiltInAutoState);
					if (DependsMinimualRequiredState <= EGameFeaturePluginState::Registered)
					{
						DependsMinimualRequiredState = EGameFeaturePluginState::Registered;
					}

					if (Dependency.bShouldActivate && CurrentPluginRequiredState == EGameFeaturePluginState::Active)
					{
						DependsMinimualRequiredState = EGameFeaturePluginState::Active;
					}

					if (EGameFeaturePluginState* DependsRequiredState = OutDependencies.Find(DependsPluginURL))
					{
						// If we have already been set as a depends with a lower required state, but find we must be at a higher state
						// then update our required minimum state to the new one
						if (DependsMinimualRequiredState > *DependsRequiredState)
						{
							*DependsRequiredState = DependsMinimualRequiredState;
						}
						continue;
					}

					// AddRef + ownership are upgrade-only; skipping a dep at >= state covers it and its subtree.
					const FString DependsPluginName = GetPluginNameFromURL(DependsPluginURL);
					EGameFeaturePluginState ExistingHandleState;
					if (InternalHandle.FindPluginRequiredState(DependsPluginName, ExistingHandleState) && ExistingHandleState >= DependsMinimualRequiredState)
					{
						continue;
					}

					DFSStack.Add(DependsPluginURL);
					OutDependencies.Add(DependsPluginURL, DependsMinimualRequiredState);
				}
			}
		}
	}

	return OutDependencies;
}

void FGameFeatureStateHandleReferenceController::AddOrUpdateReference(const FGameFeatureStateHandle& StateHandle, const FString& GameFeaturePluginURL, EGameFeaturePluginState PluginState, bool bCollectDepends)
{
	AddOrUpdateReference(StateHandle.GetUniqueId(), GameFeaturePluginURL, PluginState, bCollectDepends);
}

/** Adds a reference count to a PluginState to a GFP and owned by a StateHandle (GUID)
*    - From the StateHandle (GUID) get the Internal Handle
*    - If collecting all the depends for the GFP, collect them all + their current plugin state (this may be incorrect, and needs to be refreshed later)
*    - For each GFP + depends:
*      - Update the RefCount for the GFP with the StateHandle (GUID) + PluginState. Each RefCount can only hold a count for a StateHandle (GUID) + PluginState, no dup StateHandle (GUID) + different plugin states are allowed
*      - Add or Update the Internal Handle to own a GFP at a PluginState
*/
void FGameFeatureStateHandleReferenceController::AddOrUpdateReference(const FGuid& StateHandleId, const FString& GameFeaturePluginURL, EGameFeaturePluginState PluginState, bool bCollectDepends)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameFeatureStateHandle_AddReference);

	if (!StateHandleId.IsValid())
	{
		UE_LOGF(LogGameFeatureStateHandle, Error, "State Handle was not found valid. Make sure you called InitAndRegister when using GameFeatureStateHandle so it gets registered");
		return;
	}

	// since we have callbacks setup to update the ref here, if we are in the process of transitioning GFPs, but then exit the game triggering this callback, avoid doing anything here
	if (IsEngineExitRequested())
	{
		return;
	}

	FScopeLock Lock(&Internal->StateHandlesLock);
	FGameFeatureStateHandleInternal* InternalHandle = Internal->StateHandles.Find(StateHandleId);
	if (InternalHandle == nullptr)
	{
		UE_LOGF(LogGameFeatureStateHandle, Error, "Internal State Handle was not found for State Handle past in. Make sure you called InitAndRegister when using GameFeatureStateHandle so it gets registered (%ls)", *StateHandleId.ToString());
		return;
	}

	FString PluginName = GetPluginNameFromURL(GameFeaturePluginURL);
	EGameFeaturePluginState CurrentPluginState;
	if (InternalHandle->FindPluginRequiredState(PluginName, CurrentPluginState) && CurrentPluginState == PluginState) // if we are already at this state done recalc
	{
		return;
	}

	UGameFeaturesSubsystem& GameFeatures = UGameFeaturesSubsystem::Get();

	TMap<FString, EGameFeaturePluginState> AllGFPDependencies;
	if (bCollectDepends && EnumHasAnyFlags(InternalHandle->GetOptions(), EGameFeatureStateHandleOptions::TrackDependencies) && PluginState > EGameFeaturePluginState::Installed) // only being registered or higher requires depends to be around
	{
		AllGFPDependencies = CollectDependencies(GameFeaturePluginURL, PluginState, *InternalHandle);
	}
	else
	{
		AllGFPDependencies.Add(GameFeaturePluginURL, PluginState);
	}

	UE_LOGF(LogGameFeatureStateHandle, Verbose, "Adding reference to %ls (%ls) and depends. Self + Depends count: %i", *PluginName, *UE::GameFeatures::ToString(PluginState), AllGFPDependencies.Num());
	for (const TPair<FString, EGameFeaturePluginState>& Depends : AllGFPDependencies)
	{
		FString DependPluginName = GetPluginNameFromURL(Depends.Key);
		// Update our GFP Name -> RefCount mapping
		FGameFeatureStateRefCount& DependsRefCount = Internal->GFPReferenceCount.FindOrAdd(DependPluginName);
		DependsRefCount.AddRef(StateHandleId, Depends.Value);

		// Add or Update our InternalHandle on the Depends it requires at a min state
		InternalHandle->AddOrUpdateOwnershipIfHighestDestState(DependPluginName, Depends.Value);

		UE_LOGF(LogGameFeatureStateHandle, Verbose, "  depends %ls (%ls)", *DependPluginName, *UE::GameFeatures::ToString(Depends.Value));
	}
}

/** From the RefCounts return the lowest GFP state required by a GFP we are tracking */
bool FGameFeatureStateHandleReferenceController::GetMinimumRequiredStateForGameFeaturePlugin(const FString& GameFeaturePluginURL, EGameFeaturePluginState& OutPluginState)
{
	const FGameFeatureStateRefCount* RefCount = Internal->GFPReferenceCount.Find(GetPluginNameFromURL(GameFeaturePluginURL));
	if (RefCount)
	{
		return RefCount->GetHighestRefCountDestPluginState(OutPluginState);
	}

	return false;
}

bool FGameFeatureStateHandleReferenceController::GetDowngradedTargetState(const FString& PluginURL, EGameFeaturePluginState& OutTransitionState) const
{
	const FGameFeatureStateRefCount* RefCount = Internal->GFPReferenceCount.Find(GetPluginNameFromURL(PluginURL));
	if (!RefCount)
	{
		OutTransitionState = EGameFeaturePluginState::Uninitialized;
		return false; // possibly log a warning/error here as its not good
	}

	UGameFeaturesSubsystem& GameFeatures = UGameFeaturesSubsystem::Get();

	if (RefCount->IsEmpty())
	{

		// if its not a builtin plugin terminate
		if (!GameFeatures.WasGameFeaturePluginLoadedAsBuiltIn(PluginURL))
		{
			OutTransitionState = EGameFeaturePluginState::Uninitialized;
			return false;
		}

		FGameFeaturePluginDetails PluginDetails;
		if (GameFeatures.GetGameFeaturePluginDetails(PluginURL, PluginDetails))
		{
			OutTransitionState = UGameFeaturesSubsystem::ConvertInitialFeatureStateToTargetState(PluginDetails.BuiltInAutoState);
			UE_LOGF(LogGameFeatureStateHandle, Verbose, "   depends '%ls' has no references left and was a builtin -> (%ls)", *GetPluginNameFromURL(PluginURL), *UE::GameFeatures::ToString(OutTransitionState));

			return true;
		}
	}
	else if (RefCount->GetHighestRefCountDestPluginState(OutTransitionState))
	{
		// if we were loaded builtin, dont let us downgrade past our builtin state
		if (GameFeatures.WasGameFeaturePluginLoadedAsBuiltIn(PluginURL))
		{
			FGameFeaturePluginDetails PluginDetails;
			if (GameFeatures.GetGameFeaturePluginDetails(PluginURL, PluginDetails))
			{
				EGameFeaturePluginState BuiltinState = UGameFeaturesSubsystem::ConvertInitialFeatureStateToTargetState(PluginDetails.BuiltInAutoState);
				if (BuiltinState > OutTransitionState)
				{
					OutTransitionState = BuiltinState;
				}
			}
		}

		UE_LOGF(LogGameFeatureStateHandle, Verbose, "  depends '%ls' downgrading to -> (%ls)", *GetPluginNameFromURL(PluginURL), *UE::GameFeatures::ToString(OutTransitionState));
		return true;
	}

	return false;
}

/** Removes from a list of GFPs their StateHandle (GUID) associated with a RefCount. Once remove ensure the GFP gets downgraded if needed to the next highest PluginState in the RefCount
*   - Iterate over the GFPs to remove references:
*     - For each one, remove the StateHandle (GUID) for that RefCount
*     - If downgrading is required, collect those GFPs to be downgrade
*       - Downgrading is only done with the HighestPluginState for the RefCount has changed (ie. lowered), or there are no more references left
*   - Go through the sorted GFPs, and change their state to the desired state based on the ref count
*   - If no ref count is left, we use the AutoState, or if not a builtin plugin. Terminate
*/
void FGameFeatureStateHandleReferenceController::RemoveReferences(const FGameFeatureStateHandle& StateHandle, const TArray<FString>& PluginsToRemoveRefences, TFunction<void(bool)> OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameFeatureStateHandle_RemoveReferencesInternal);

	if (PluginsToRemoveRefences.IsEmpty())
	{
		OnComplete(true);
		return;
	}

	// Keep track of the plugins we need to downgrade, and leave the ones that are still at the correct ref count plugin state
	TSet<FString> PluginsToDowngrade;
	for (const FString& GameFeaturePluginName : PluginsToRemoveRefences)
	{
		FGameFeatureStateRefCount* RefCount = Internal->GFPReferenceCount.Find(GameFeaturePluginName);

		if (RefCount && RefCount->RemoveRefAndRequiresDowngrading(StateHandle.GetUniqueId()))
		{
			PluginsToDowngrade.Add(GameFeaturePluginName);
		}
	}

	FString StateHandleOwner;
	{
		FScopeLock Lock(&Internal->StateHandlesLock);
		if (FGameFeatureStateHandleInternal* InternalHandle = Internal->StateHandles.Find(StateHandle.GetUniqueId()))
		{
			StateHandleOwner = InternalHandle->GetOwner();
		}
		else
		{
			StateHandleOwner = StateHandle.ToString();
		}
	}

	if (PluginsToDowngrade.IsEmpty())
	{
		UE_LOGF(LogGameFeatureStateHandle, Verbose, "Removing References and Downgrading %i/%i GFPs that was owned from '%ls'", PluginsToDowngrade.Num(), PluginsToRemoveRefences.Num(), *StateHandleOwner);
	}
	else
	{
		UE_LOGF(LogGameFeatureStateHandle, Display, "Removing References and Downgrading %i/%i GFPs that was owned from '%ls'", PluginsToDowngrade.Num(), PluginsToRemoveRefences.Num(), *StateHandleOwner);
	}

	// return early if we have no downgrades to do
	if (PluginsToDowngrade.IsEmpty())
	{
		OnComplete(true);
		return;
	}

	TArray<FString> PluginsToMoveToLoaded;
	TArray<FString> PluginsToMoveToRegistered;
	TArray<FString> PluginsToMoveToInstalled;
	TArray<FString> PluginsToMoveToTerminate;
	for (const FString& PluginName : PluginsToDowngrade)
	{
		const FGameFeatureStateRefCount* RefCount = Internal->GFPReferenceCount.Find(PluginName);

		if (RefCount)
		{
			FString PluginURL = GetPluginURLFromName(PluginName);
			EGameFeaturePluginState TransitionState;
			EGameFeatureStateHandleOptions Options = EGameFeatureStateHandleOptions::None;
			{
				FScopeLock Lock(&Internal->StateHandlesLock);
				if (FGameFeatureStateHandleInternal* InternalHandle = Internal->StateHandles.Find(StateHandle.GetUniqueId()))
				{
					Options = InternalHandle->GetOptions();
				}
			}
			if (GetDowngradedTargetState(PluginURL, TransitionState))
			{
				EGameFeatureTargetState TargetState;
				if (PluginStateToTargetState(TransitionState, TargetState))
				{
					if (TargetState == EGameFeatureTargetState::Installed && EnumHasAnyFlags(Options, EGameFeatureStateHandleOptions::KeepRegistered))
					{
						TargetState = EGameFeatureTargetState::Registered;
					}

					switch (TargetState) {
					case EGameFeatureTargetState::Active:
						// Do nothing, we accept to be in all states, do not reactivate back
						break;
					case EGameFeatureTargetState::Loaded:
						PluginsToMoveToLoaded.Add(PluginURL);
						break;
					case EGameFeatureTargetState::Registered:
						PluginsToMoveToRegistered.Add(PluginURL);
						break;
					case EGameFeatureTargetState::Installed:
						PluginsToMoveToInstalled.Add(PluginURL);
						break;
					default:
						break;
					}
				}
				else
				{
					UE_LOGF(LogGameFeatureStateHandle, Error, "  failed to convert PluginState (%ls) to TargetState, unsure what state downgrade plugin '%ls'", *UE::GameFeatures::ToString(TransitionState), *PluginName);
				}
			}
			else
			{
				if (EnumHasAnyFlags(Options, EGameFeatureStateHandleOptions::KeepRegistered))
				{
					UE_LOGF(LogGameFeatureStateHandle, Verbose, "  depends '%ls' has no references left and was not builtin should be Terminating but StateHandle.KeepRegistered -> (Registered)", *PluginName);
					PluginsToMoveToRegistered.Add(PluginURL);
				}
				else
				{
					// TODO do we need to uninstall, install handles or is terminate just fine as it will transition through Uninstall
					UE_LOGF(LogGameFeatureStateHandle, Verbose, "  depends '%ls' has no references left and was not builtin or was a UEFN island -> (Terminating)", *PluginName);
					PluginsToMoveToTerminate.Add(PluginURL);
				}
			}

			if (RefCount->IsEmpty())
			{
				Internal->GFPReferenceCount.Remove(PluginName);
			}
		}
		else
		{
			UE_LOGF(LogGameFeatureStateHandle, Error, "  depends '%ls' was not in our ref count system, which should not be possible", *PluginName);
		}
	}

	struct FContext
	{
		FContext(TFunction<void(bool)> InOnComplete)
			: OnComplete(MoveTemp(InOnComplete))
		{
		}

		~FContext()
		{
			OnComplete(!bError);
		}

		bool bError = false;
		TFunction<void(bool)> OnComplete;
	};

	TSharedRef Context = MakeShared<FContext>(MoveTemp(OnComplete));

	// The callback will increase the ref count the context for each call to this, and the last call will trigger the ~FContext to trigger the OnComplete stage.
	FMultipleGameFeaturePluginChangeStateComplete ChangeStateCallback = FMultipleGameFeaturePluginChangeStateComplete::CreateLambda([Context](const TMap<FString, UE::GameFeatures::FResult>& Results)
	{
		for (const TPair<FString, UE::GameFeatures::FResult>& Result : Results)
		{
			if (Result.Value.HasError())
			{
				// possibly propagate an error, or if multiple errors happen what next?
				Context->bError = true;
			}
		}
	});

	UGameFeaturesSubsystem& GameFeatures = UGameFeaturesSubsystem::Get();
	const bool bKeepRegistered = true;

	// Move all to [Terminal, Loaded]
	if (!PluginsToMoveToLoaded.IsEmpty())
	{
		GameFeatures.DeactivateGameFeaturePlugin(PluginsToMoveToLoaded, ChangeStateCallback);
	}
	// Move all to [Terminal, Registered]
	if (!PluginsToMoveToRegistered.IsEmpty())
	{
		GameFeatures.UnloadGameFeaturePlugin(PluginsToMoveToRegistered, ChangeStateCallback, bKeepRegistered);
	}
	// Move all to [Terminal, Installed]
	if (!PluginsToMoveToInstalled.IsEmpty())
	{
		GameFeatures.UnloadGameFeaturePlugin(PluginsToMoveToInstalled, ChangeStateCallback, !bKeepRegistered);
	}
	// Move all to [Terminal, Terminal]
	if (!PluginsToMoveToTerminate.IsEmpty())
	{
		GameFeatures.TerminateGameFeaturePlugin(PluginsToMoveToTerminate, ChangeStateCallback);
	}
}

/** For all these functions, Register, Load, and Activate override the GameFeaturesSubsytem one to simply do for now:
*   - Add a ref count on the plugin(s) being loaded, which will chase all the depends and ref count those to StateHandle passed in
*   - Once these callbacks are called that these operations are done, refresh our StateHandles PluginState as our depends mave have shifted from the original PluginState found during collecting of the depends
*/
void FGameFeatureStateHandleReferenceController::LoadAndActivateGameFeaturePlugin(const FGameFeatureStateHandle& StateHandle, const FString& PluginURL, const FGameFeatureProtocolOptions& ProtocolOptions, const UGameFeatureStateHandleLoadComplete& CompleteDelegate)
{
	UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(PluginURL, ProtocolOptions, FGameFeaturePluginLoadComplete::CreateLambda([this, PluginURL, CompleteDelegate, StateHandleId = StateHandle.GetUniqueId()](const UE::GameFeatures::FResult& Result)
	{
		if (IsLifetimeControlEnabled())
		{
			AddOrUpdateReference(StateHandleId, PluginURL, EGameFeaturePluginState::Active);
		}

		CompleteDelegate.ExecuteIfBound(Result);
	}));
}

void FGameFeatureStateHandleReferenceController::LoadAndActivateGameFeaturePlugin(const FGameFeatureStateHandle& StateHandle, const TArray<FString>& PluginURLs, const FGameFeatureProtocolOptions& ProtocolOptions, const UMultipleGameFeatureStateHandleLoadComplete& CompleteDelegate)
{
	UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(PluginURLs, ProtocolOptions, FBuiltInGameFeaturePluginsLoaded::CreateLambda([this, PluginURLs, CompleteDelegate, StateHandleId = StateHandle.GetUniqueId()](const TMap<FString, UE::GameFeatures::FResult>& Results)
	{
		if (IsLifetimeControlEnabled())
		{
			for (const FString& PluginURL : PluginURLs)
			{
				AddOrUpdateReference(StateHandleId, PluginURL, EGameFeaturePluginState::Active);
			}
		}

		CompleteDelegate.ExecuteIfBound(Results);
	}));
}

void FGameFeatureStateHandleReferenceController::LoadGameFeaturePlugin(const FGameFeatureStateHandle& StateHandle, const FString& PluginURL, const FGameFeatureProtocolOptions& ProtocolOptions, const UGameFeatureStateHandleLoadComplete& CompleteDelegate)
{
	UGameFeaturesSubsystem::Get().LoadGameFeaturePlugin(PluginURL, ProtocolOptions, FGameFeaturePluginLoadComplete::CreateLambda([this, PluginURL, CompleteDelegate, StateHandleId = StateHandle.GetUniqueId()](const UE::GameFeatures::FResult& Result)
	{
		if (IsLifetimeControlEnabled())
		{
			AddOrUpdateReference(StateHandleId, PluginURL, EGameFeaturePluginState::Loaded);
		}

		CompleteDelegate.ExecuteIfBound(Result);
	}));
}

void FGameFeatureStateHandleReferenceController::LoadGameFeaturePlugin(const FGameFeatureStateHandle& StateHandle, const TArray<FString>& PluginURLs, const FGameFeatureProtocolOptions& ProtocolOptions, const UMultipleGameFeatureStateHandleLoadComplete& CompleteDelegate)
{
	UGameFeaturesSubsystem::Get().LoadGameFeaturePlugin(PluginURLs, ProtocolOptions, FBuiltInGameFeaturePluginsLoaded::CreateLambda([this, PluginURLs, CompleteDelegate, StateHandleId = StateHandle.GetUniqueId()](const TMap<FString, UE::GameFeatures::FResult>& Results)
	{
		if (IsLifetimeControlEnabled())
		{
			for (const FString& PluginURL : PluginURLs)
			{
				AddOrUpdateReference(StateHandleId, PluginURL, EGameFeaturePluginState::Loaded);
			}
		}

		CompleteDelegate.ExecuteIfBound(Results);
	}));
}

void FGameFeatureStateHandleReferenceController::RegisterGameFeaturePlugin(const FGameFeatureStateHandle& StateHandle, const FString& PluginURL, const FGameFeatureProtocolOptions& ProtocolOptions, const UGameFeatureStateHandleLoadComplete& CompleteDelegate)
{
	UGameFeaturesSubsystem::Get().RegisterGameFeaturePlugin(PluginURL, ProtocolOptions, FGameFeaturePluginLoadComplete::CreateLambda([this, PluginURL, CompleteDelegate, StateHandleId = StateHandle.GetUniqueId()](const UE::GameFeatures::FResult& Result)
	{
		if (IsLifetimeControlEnabled())
		{
			AddOrUpdateReference(StateHandleId, PluginURL, EGameFeaturePluginState::Registered);
		}

		CompleteDelegate.ExecuteIfBound(Result);
	}));
}

void FGameFeatureStateHandleReferenceController::RegisterGameFeaturePlugin(const FGameFeatureStateHandle& StateHandle, const TArray<FString>& PluginURLs, const FGameFeatureProtocolOptions& ProtocolOptions, const UMultipleGameFeatureStateHandleLoadComplete& CompleteDelegate)
{
	UGameFeaturesSubsystem::Get().RegisterGameFeaturePlugin(PluginURLs, ProtocolOptions, FBuiltInGameFeaturePluginsLoaded::CreateLambda([this, PluginURLs, CompleteDelegate, StateHandleId = StateHandle.GetUniqueId()](const TMap<FString, UE::GameFeatures::FResult>& Results)
	{
		if (IsLifetimeControlEnabled())
		{
			for (const FString& PluginURL : PluginURLs)
			{
				AddOrUpdateReference(StateHandleId, PluginURL, EGameFeaturePluginState::Registered);
			}
		}

		CompleteDelegate.ExecuteIfBound(Results);
	}));
}

void FGameFeatureStateHandleReferenceController::LoadBuiltInGameFeaturePlugins_Amortized(const FGameFeatureStateHandle& StateHandle, const TArray<TSharedRef<IPlugin>>& Plugins, const UGameFeaturesSubsystem::FBuiltInPluginAdditionalFilters_Copyable& AdditionalFilter, int32 AmortizeRateMS, const UMultipleGameFeatureStateHandleLoadComplete& CompleteDelegate)
{
	UGameFeaturesSubsystem::Get().LoadBuiltInGameFeaturePlugins_Amortized(Plugins, AdditionalFilter, AmortizeRateMS, FBuiltInGameFeaturePluginsLoaded::CreateLambda([this, CompleteDelegate, StateHandleId = StateHandle.GetUniqueId()](const TMap<FString, UE::GameFeatures::FResult>& Results)
		{
			if (IsLifetimeControlEnabled())
			{
				UGameFeaturesSubsystem& GameFeatureSubsystem = UGameFeaturesSubsystem::Get();
				for (const TPair<FString, UE::GameFeatures::FResult>& Result : Results)
				{
					FString PluginURL;
					if (GameFeatureSubsystem.GetPluginURLByName(Result.Key, PluginURL))
					{
						AddOrUpdateReference(StateHandleId, PluginURL, GameFeatureSubsystem.GetPluginState(PluginURL));
					}
					else
					{
						UE_LOGF(LogGameFeatureStateHandle, Error, "Failed to add reference to plugin '%ls'. Unable to find matching PluginURL.", *Result.Key);
					}
				}
			}

			CompleteDelegate.ExecuteIfBound(Results);
		}));
}

void FGameFeatureStateHandleReferenceController::ResetGameFeatureStateHandle(const FGameFeatureStateHandle& StateHandle, TFunction<void(bool)> OnComplete)
{
	if (!IsLifetimeControlEnabled())
	{
		OnComplete(true);
	}
	else if (IsEngineExitRequested())
	{
		UE_LOGF(LogGameFeatureStateHandle, Verbose, "Requesting a reset but the Engine is Exiting. This should have likely been done before we existed the Engine. StateHandle Owner '%ls'", *StateHandle.ToString());
		OnComplete(true);
	}
	else
	{
		FScopeLock Lock(&Internal->StateHandlesLock);
		if (FGameFeatureStateHandleInternal* InternalHandle = Internal->StateHandles.Find(StateHandle.GetUniqueId()))
		{
			const TArray<FString> Plugins = InternalHandle->GetPlugins();
			InternalHandle->Empty();

			RemoveReferences(StateHandle, Plugins, OnComplete);
		}
		else
		{
			OnComplete(false);
		}
	}
}

void FGameFeatureStateHandleReferenceController::MergeGameFeatureStateHandle(const FGameFeatureStateHandle& From, const FGameFeatureStateHandle& To)
{
	if (From == To)
	{
		return;
	}

	FScopeLock Lock(&Internal->StateHandlesLock);
	FGameFeatureStateHandleInternal* FromStateHandle = Internal->StateHandles.Find(From.GetUniqueId());
	FGameFeatureStateHandleInternal* ToStateHandle   = Internal->StateHandles.Find(To.GetUniqueId());

	if (FromStateHandle == nullptr || ToStateHandle == nullptr)
	{
		return;
	}

	// this will leave FromStateHandle with no GFPs it owns
	ToStateHandle->TakeOwnership(*FromStateHandle);
}

#if !UE_BUILD_SHIPPING
int32 FGameFeatureStateHandleReferenceController::CompareAndLogGameFeatureDifferences(const FGameFeatureStateHandle& A, const FGameFeatureStateHandle& B)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameFeatureStateHandle_CheckAndLogGameFeatureDifferences);

	FScopeLock Lock(&Internal->StateHandlesLock);

	FGameFeatureStateHandleInternal* InternalHandleA = A.IsValid() ? Internal->StateHandles.Find(A.GetUniqueId()) : nullptr;
	FGameFeatureStateHandleInternal* InternalHandleB = B.IsValid() ? Internal->StateHandles.Find(B.GetUniqueId()) : nullptr;
	
	const int32 PluginCountA = InternalHandleA == nullptr ? 0 : InternalHandleA->GetPluginCount();
	const int32 PluginCountB = InternalHandleB == nullptr ? 0 : InternalHandleB->GetPluginCount();
	UE_CLOGFMT(PluginCountA != PluginCountB, LogGameFeatureStateHandle, Verbose, "Plugin counts are different A:{NumA} B:{NumB}", PluginCountA, PluginCountB);

	if (InternalHandleA == nullptr || InternalHandleB == nullptr)
	{
		if (InternalHandleA != nullptr)
		{
			return PluginCountA;
		}
		else if (InternalHandleB != nullptr)
		{
			return PluginCountB;
		}
		else
		{
			// if both are null report no differences.
			return 0;
		}		
	}

	// Both handles are valid, check symmetric difference (don't just rely on counts)
	TSet<FString> PluginsA = TSet<FString>(InternalHandleA->GetPlugins());
	TSet<FString> PluginsB = TSet<FString>(InternalHandleB->GetPlugins());
	TSet<FString> Differences = PluginsA.SymmetricDifference(PluginsB);
	UE_CLOGFMT(!Differences.IsEmpty(), LogGameFeatureStateHandle, Verbose, "Plugin differences: {Differences}", *FString::Join(Differences, TEXT(",")));

	return Differences.Num();
}
#endif // !UE_BUILD_SHIPPING

void FGameFeatureStateHandleReferenceControllerInternal::ListGameFeatureStateHandles(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	FScopeLock Lock(&StateHandlesLock);

	Ar.Logf(TEXT("ListGameFeatureStateHandles StateHandles (Num=%i)"), StateHandles.Num());
	for (const TPair<FGuid, FGameFeatureStateHandleInternal>& InternalStateHandle : StateHandles)
	{
		Ar.Logf(TEXT("  %s (%s) %s"), *InternalStateHandle.Value.GetOwner(), *InternalStateHandle.Key.ToString(EGuidFormats::DigitsWithHyphensInParentheses), *InternalStateHandle.Value.ToString());
	}

	Ar.Logf(TEXT("RefCounts (Num=%i)"), GFPReferenceCount.Num());
	for (const TPair<FString, FGameFeatureStateRefCount>& RefCount : GFPReferenceCount)
	{
		const TMap<FGuid, EGameFeaturePluginState>& OwnerToDestMap = RefCount.Value.GetOwnerToDestMap();

		TStringBuilder<512> RefCountStr;
		EGameFeaturePluginState HighestDestPluginState;
		RefCount.Value.GetHighestRefCountDestPluginState(HighestDestPluginState);

		RefCountStr.Appendf(TEXT("{ ReqState: (%s) Owners (%i): "), *UE::GameFeatures::ToString(HighestDestPluginState), OwnerToDestMap.Num());
		for (const TPair<FGuid, EGameFeaturePluginState>& OwnerToState : OwnerToDestMap)
		{
			if (FGameFeatureStateHandleInternal* InternalHandle = StateHandles.Find(OwnerToState.Key))
			{
				RefCountStr.Appendf(TEXT("(%s (%s))"), *InternalHandle->GetOwner(), *UE::GameFeatures::ToString(OwnerToState.Value));
			}
		}
		RefCountStr.Append(TEXT("}"));


		Ar.Logf(TEXT("  %s [%.*s]"), *RefCount.Key, RefCountStr.Len(), RefCountStr.GetData());
	}
}
