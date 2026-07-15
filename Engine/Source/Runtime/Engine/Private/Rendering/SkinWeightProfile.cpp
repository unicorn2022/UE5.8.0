// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/SkinWeightProfile.h"

#include "Animation/SkinWeightProfileManager.h"
#include "Misc/ScopeRWLock.h"
#include "RenderingThread.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Components/SkinnedMeshComponent.h"
#include "Containers/Ticker.h"
#include "ContentStreaming.h"
#include "UObject/AnimObjectVersion.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Rendering/RenderCommandPipes.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshTypes.h"
#include "UObject/ObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Rendering/SkeletalMeshLODImporterData.h"
#else
#include "Engine/GameEngine.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkinWeightProfile)

class ENGINE_API FSkinnedMeshComponentUpdateSkinWeightsContext
{
public:
	FSkinnedMeshComponentUpdateSkinWeightsContext(USkinnedAsset* InSkinnedAsset)
	{
		for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
		{
			if (It->GetSkinnedAsset() == InSkinnedAsset)
			{
				checkf(!It->IsUnreachable(), TEXT("%s"), *It->GetFullName());

				if (It->IsRenderStateCreated())
				{
					check(It->IsRegistered());
					MeshComponents.Add(*It);
				}
			}
		}
	}

	~FSkinnedMeshComponentUpdateSkinWeightsContext()
	{
		const int32 ComponentCount = MeshComponents.Num();
		for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
		{
			USkinnedMeshComponent* Component = MeshComponents[ComponentIndex];

			if (Component->IsRegistered())
			{
				Component->UpdateSkinWeightOverrideBuffer();
			}
		}
	}


private:
	TArray< class USkinnedMeshComponent*> MeshComponents;
};


static void OnDefaultProfileCVarsChanged(IConsoleVariable* Variable)
{
	if (GSkinWeightProfilesLoadByDefaultMode >= 0)
	{
		const bool bClearBuffer = GSkinWeightProfilesLoadByDefaultMode == 2 || GSkinWeightProfilesLoadByDefaultMode == 0;
		const bool bSetBuffer = GSkinWeightProfilesLoadByDefaultMode == 3;

		if (bClearBuffer || bSetBuffer)
		{
			// Make sure no pending skeletal mesh LOD updates
			if (IStreamingManager::Get_Concurrent() && IStreamingManager::Get().IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::SkeletalMesh))
			{
				IStreamingManager::Get().GetRenderAssetStreamingManager().BlockTillAllRequestsFinished();
			}

			for (TObjectIterator<USkeletalMesh> It; It; ++It)
			{
				if (FSkeletalMeshRenderData* RenderData = It->GetResourceForRendering())
				{
					FSkinnedMeshComponentRecreateRenderStateContext RecreateState(*It);
					for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
					{
						FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[LODIndex];
						if (bClearBuffer)
						{
							LOD.SkinWeightProfilesData.ClearDynamicDefaultSkinWeightProfile(*It, LODIndex);
						}
						else if (bSetBuffer)
						{
							LOD.SkinWeightProfilesData.ClearDynamicDefaultSkinWeightProfile(*It, LODIndex);
							LOD.SkinWeightProfilesData.SetDynamicDefaultSkinWeightProfile(*It, LODIndex);
						}
					}
				}
			}
		}
	}
}

int32 GSkinWeightProfilesLoadByDefaultMode = -1;
FAutoConsoleVariableRef CVarSkinWeightsLoadByDefaultMode(
	TEXT("r.SkinWeightProfile.LoadByDefaultMode"),
	GSkinWeightProfilesLoadByDefaultMode,
	TEXT("Enables/disables run-time optimization to override the original skin weights with a profile designated as the default to replace it. Can be used to optimize memory for specific platforms or devices")
	TEXT("-1 = disabled")
	TEXT("0 = static disabled")
	TEXT("1 = static enabled")
	TEXT("2 = dynamic disabled")
	TEXT("3 = dynamic enabled"),
	FConsoleVariableDelegate::CreateStatic(&OnDefaultProfileCVarsChanged),
	ECVF_Default
);

int32 GSkinWeightProfilesDefaultLODOverride = -1;
FAutoConsoleVariableRef CVarSkinWeightProfilesDefaultLODOverride(
	TEXT("r.SkinWeightProfile.DefaultLODOverride"),
	GSkinWeightProfilesDefaultLODOverride,
	TEXT("Override LOD index from which on the default Skin Weight Profile should override the Skeletal Mesh's default Skin Weights"),
	FConsoleVariableDelegate::CreateStatic(&OnDefaultProfileCVarsChanged),
	ECVF_Scalability
);

int32 GSkinWeightProfilesAllowedFromLOD = -1;
FAutoConsoleVariableRef CVarSkinWeightProfilesAllowedFromLOD(
	TEXT("r.SkinWeightProfile.AllowedFromLOD"),
	GSkinWeightProfilesAllowedFromLOD,
	TEXT("Override LOD index from which on the Skin Weight Profile can be applied"),
	FConsoleVariableDelegate::CreateStatic(&OnDefaultProfileCVarsChanged),
	ECVF_Scalability
);

static int32 GAllowCPU = 1;
static FAutoConsoleVariableRef CVarAllowCPU(
	TEXT("r.SkinWeightProfile.AllowCPU"),
	GAllowCPU,
	TEXT("Whether or not to allow cpu buffer generation"),
	ECVF_Cheat
);

static int32 GResetReadbackOnSizeMismatch = 1;
static FAutoConsoleVariableRef CVarResetReadbackOnSizeMismatch(
	TEXT("r.SkinWeightProfile.ResetReadbackOnSizeMismatch"),
	GResetReadbackOnSizeMismatch,
	TEXT("1 = reset and re-enqueue GPU readback on staging/dst size mismatch (default). 0 = zero dst buffer and mark readback finished."),
	ECVF_Default
);

static int32 GHandleDelayedLoads = 1;
static FAutoConsoleVariableRef CHandleDelayedLoads(
	TEXT("r.SkinWeightProfile.HandleDelayedLoads"),
	GHandleDelayedLoads,
	TEXT("Whether or not to handle skeletal meshes that are severely delayed in loading"),
	ECVF_Cheat
);

static bool GSkinWeightProfileDeferredCleanUp = false;
static FAutoConsoleVariableRef CVarSkinWeightProfileDeferredCleanUp(
	TEXT("r.SkinWeightProfile.DeferredCleanUp"),
	GSkinWeightProfileDeferredCleanUp,
	TEXT("When true, defer FSkinWeightVertexBuffer cleanup to the next GT tick after the RT release command runs. ")
	TEXT("This avoids races with in-flight RHI cmd lists that hold raw FRHIBuffer* (from cached MDCs' SetStreamSource). ")
	TEXT("When false (default), BeginCleanup() is called directly from the RT release command."),
	ECVF_Default
);

/** Redirection for old CVars **/

static FAutoConsoleVariableDeprecated CVarHandleDelayedLoadsDeprecated(
	TEXT("SkinWeightProfileManager.HandleDelayedLoads"),
	TEXT("r.SkinWeightProfile.HandleDelayedLoads"),
	TEXT("5.8"),
	EShadowCVarBehavior::Warn,
	EShadowCVarBehavior::Ensure,
	TEXT("Use r.SkinWeightProfile.HandleDelayedLoads instead")
);

static FAutoConsoleVariableDeprecated CVarSkinWeightProfilesAllowedFromLODDeprecated(
	TEXT("a.SkinWeightProfile.AllowedFromLOD"),
	TEXT("r.SkinWeightProfile.AllowedFromLOD"),
	TEXT("5.8"),
	EShadowCVarBehavior::Warn,
	EShadowCVarBehavior::Ensure,
	TEXT("Use r.SkinWeightProfile.AllowedFromLOD instead")
);

static FAutoConsoleVariableDeprecated CVarSkinWeightProfilesDefaultLODOverrideDeprecated(
	TEXT("a.SkinWeightProfile.DefaultLODOverride"),
	TEXT("r.SkinWeightProfile.DefaultLODOverride"),
	TEXT("5.8"),
	EShadowCVarBehavior::Warn,
	EShadowCVarBehavior::Ensure,
	TEXT("Use r.SkinWeightProfile.DefaultLODOverride instead")
);

static FAutoConsoleVariableDeprecated CVarAllowCPUDeprecated(
	TEXT("SkinWeightProfileManager.AllowCPU"),
	TEXT("r.SkinWeightProfile.AllowCPU"),
	TEXT("5.8"),
	EShadowCVarBehavior::Warn,
	EShadowCVarBehavior::Ensure,
	TEXT("Use r.SkinWeightProfile.AllowCPU instead")
);


static FAutoConsoleVariableDeprecated CVarSkinWeightsLoadByDefaultModeDeprecated(
	TEXT("a.SkinWeightProfile.LoadByDefaultMode"),
	TEXT("r.SkinWeightProfile.LoadByDefaultMode"),
	TEXT("5.8"),
	EShadowCVarBehavior::Warn,
	EShadowCVarBehavior::Ensure,
	TEXT("Use r.SkinWeightProfile.LoadByDefaultMode instead")
);

/** End redirection for old CVars **/

FArchive& operator<<(FArchive& Ar, FRuntimeSkinWeightProfileData& OverrideData)
{
#if WITH_EDITOR
	if (Ar.UEVer() < VER_UE4_SKINWEIGHT_PROFILE_DATA_LAYOUT_CHANGES)
	{
		Ar << OverrideData.OverridesInfo_DEPRECATED;
		Ar << OverrideData.Weights_DEPRECATED;
	}
	else
#endif
	{	
		Ar << OverrideData.BoneIDs;
		Ar << OverrideData.BoneWeights;
		Ar << OverrideData.NumWeightsPerVertex;
	}
	
	Ar << OverrideData.VertexIndexToInfluenceOffset;


	return Ar;
}

FArchive& operator<<(FArchive& Ar, FSkinWeightProfilesData& LODData)
{
	FScopeLock ScopeLock(&LODData.OverrideDataLock);
	
	Ar << LODData.OverrideData;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FRuntimeSkinWeightProfileData::FSkinWeightOverrideInfo& OverrideInfo)
{
#if WITH_EDITOR
	Ar << OverrideInfo.InfluencesOffset;
	Ar << OverrideInfo.NumInfluences_DEPRECATED;
#endif

	return Ar;
}

#if WITH_EDITORONLY_DATA
FArchive& operator<<(FArchive& Ar, FImportedSkinWeightProfileData& ProfileData)
{
	Ar << ProfileData.SkinWeights;
	Ar << ProfileData.SourceModelInfluences;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FRawSkinWeight& OverrideEntry)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	if (Ar.IsLoading())
	{
		FMemory::Memzero(OverrideEntry.InfluenceBones);
		FMemory::Memzero(OverrideEntry.InfluenceWeights);
	}

	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::UnlimitedBoneInfluences)
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < EXTRA_BONE_INFLUENCES; ++InfluenceIndex)
		{
			if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::IncreaseBoneIndexLimitPerChunk)
			{
				uint8 BoneIndex = 0;
				Ar << BoneIndex;
				OverrideEntry.InfluenceBones[InfluenceIndex] = BoneIndex;
			}
			else
			{
				Ar << OverrideEntry.InfluenceBones[InfluenceIndex];
			}

			uint8 Weight = 0;
			Ar << Weight;
			OverrideEntry.InfluenceWeights[InfluenceIndex] = (static_cast<uint16>(Weight) << 8) | Weight;
		}
	}
	else if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::IncreasedSkinWeightPrecision)
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
		{
			uint8 Weight = 0;
			Ar << OverrideEntry.InfluenceBones[InfluenceIndex];
			Ar << Weight;
			OverrideEntry.InfluenceWeights[InfluenceIndex] = (static_cast<uint16>(Weight) << 8) | Weight;
		}
	}
	else
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
		{
			Ar << OverrideEntry.InfluenceBones[InfluenceIndex];
			Ar << OverrideEntry.InfluenceWeights[InfluenceIndex];
		}
	}

	return Ar;
}
#endif // WITH_EDITORONLY_DATA

// A helper class to ensure that the vertex buffers are only deleted once all render commands have been completed at the end of the
// engine loop.
struct FSkinWeightVertexBuffersDeleter : FDeferredCleanupInterface
{
	explicit FSkinWeightVertexBuffersDeleter(TArray<FSkinWeightVertexBuffer*>&& InBuffers) :
		Buffers(MoveTemp(InBuffers))
	{ }
			
	~FSkinWeightVertexBuffersDeleter() override
	{
		for (FSkinWeightVertexBuffer* Buffer: Buffers)
		{
			delete Buffer;
		}
	}
			
	TArray<FSkinWeightVertexBuffer*> Buffers;
};


void FSkinWeightProfilesData::Init(FSkinWeightVertexBuffer* InBaseBuffer) 
{
	BaseBuffer = InBaseBuffer;
}

FSkinWeightProfilesData::~FSkinWeightProfilesData()
{
	ReleaseResources();
}

FSkinWeightProfilesData::FOnPickOverrideSkinWeightProfile FSkinWeightProfilesData::OnPickOverrideSkinWeightProfile;

#if !WITH_EDITOR
void FSkinWeightProfilesData::OverrideBaseBufferSkinWeightData(USkeletalMesh* Mesh, int32 LODIndex)
{
	if (GSkinWeightProfilesLoadByDefaultMode == 1)
	{
		const TArray<FSkinWeightProfileInfo>& Profiles = Mesh->GetSkinWeightProfiles();
		// Try and find a default buffer and whether it is set for this LOD index 
		int32 DefaultProfileIndex = INDEX_NONE;

		// Setup to not apply any skin weight profiles at this LOD level
		if (LODIndex >= GSkinWeightProfilesAllowedFromLOD)
		{
			DefaultProfileIndex = OnPickOverrideSkinWeightProfile.IsBound() ? OnPickOverrideSkinWeightProfile.Execute(Mesh, MakeArrayView(Profiles), LODIndex) : Profiles.IndexOfByPredicate([LODIndex](FSkinWeightProfileInfo ProfileInfo)
			{
				// In case the default LOD index has been overridden check against that
				if (GSkinWeightProfilesDefaultLODOverride >= 0)
				{
					return (ProfileInfo.DefaultProfile.Default && LODIndex >= GSkinWeightProfilesDefaultLODOverride);
				}

				// Otherwise check if this profile is set as default and the current LOD index is applicable
				return (ProfileInfo.DefaultProfile.Default && LODIndex >= ProfileInfo.DefaultProfileFromLODIndex.Default);
			});
		}

		// If we found a profile try and find the override skin weights and apply if found
		if (DefaultProfileIndex != INDEX_NONE)
		{
			FScopeLock ScopeLock(&OverrideDataLock);
			
			const FName ProfileName = Profiles[DefaultProfileIndex].Name;
			if (const FRuntimeSkinWeightProfileData* ProfilePtr = OverrideData.Find(ProfileName))
			{
				ProfilePtr->ApplyDefaultOverride(BaseBuffer);
			}

			bDefaultOverridden = true;
			bStaticOverridden = true;
			DefaultProfileStack = FSkinWeightProfileStack{ProfileName};
		}
	}
}
#endif 

void FSkinWeightProfilesData::SetDynamicDefaultSkinWeightProfile(USkeletalMesh* Mesh, int32 LODIndex, bool bSerialization /*= false*/)
{
	if (bStaticOverridden)
	{
		if (!bEmittedOverrideWarning)
		{
			UE_LOGF(LogSkeletalMesh, Error, "[%ls] Skeletal Mesh has overridden the default Skin Weights buffer during serialization, cannot set any other skin weight profile.", *Mesh->GetName());
			bEmittedOverrideWarning = true;
		}
		return;
	}

	if (GSkinWeightProfilesLoadByDefaultMode == 3)
	{
		const TArray<FSkinWeightProfileInfo>& Profiles = Mesh->GetSkinWeightProfiles();
		// Try and find a default buffer and whether or not it is set for this LOD index 
		const int32 DefaultProfileIndex = Profiles.IndexOfByPredicate([LODIndex](FSkinWeightProfileInfo ProfileInfo)
		{
			// Setup to not apply any skin weight profiles at this LOD level
			if (LODIndex < GSkinWeightProfilesAllowedFromLOD)
			{
				return false;
			}

			// In case the default LOD index has been overridden check against that
			if (GSkinWeightProfilesDefaultLODOverride >= 0)
			{
				return (ProfileInfo.DefaultProfile.Default && LODIndex >= GSkinWeightProfilesDefaultLODOverride);
			}

			// Otherwise check if this profile is set as default and the current LOD index is applicable
			return (ProfileInfo.DefaultProfile.Default && LODIndex >= ProfileInfo.DefaultProfileFromLODIndex.Default);
		});

		// If we found a profile try and find the override skin weights and apply if found
		if (DefaultProfileIndex != INDEX_NONE)
		{
			const FName& ProfileName = Profiles[DefaultProfileIndex].Name;
			const FSkinWeightProfileStack ProfileStack(ProfileName);
			
			const bool bNoDefaultProfile = DefaultOverrideSkinWeightBuffer == nullptr;
			const bool bDifferentDefaultProfile = bNoDefaultProfile && (!bDefaultOverridden || DefaultProfileStack != ProfileStack);
			if (bNoDefaultProfile || bDifferentDefaultProfile)
			{
				if (GetOverrideBuffer(ProfileStack) == nullptr)
				{
					if (bSerialization)
					{
						// During serialization the CPU copy of the weight should still be available
						const uint8* BaseBufferData = BaseBuffer->GetDataVertexBuffer()->GetWeightData();
						
						if (ensure(BaseBufferData))
						{
							FScopeLock ScopeLock(&OverrideDataLock);
							
							if (const FRuntimeSkinWeightProfileData* ProfilePtr = OverrideData.Find(ProfileName))
							{
								FSkinnedMeshComponentUpdateSkinWeightsContext Context(Mesh);

								FSkinWeightVertexBuffer* OverrideBuffer = new FSkinWeightVertexBuffer();
								
								{
									UE::TWriteScopeLock Lock(ProfileBufferMapLock);
									ProfileStackToBuffer.Add(ProfileStack, OverrideBuffer);
								}

								ApplyOverrideProfileStack(ProfileStack, OverrideBuffer);

								DefaultOverrideSkinWeightBuffer = OverrideBuffer;
								bDefaultOverridden = true;
								DefaultProfileStack = ProfileStack;
								
#if RHI_ENABLE_RESOURCE_INFO
								const FName OwnerName(USkinnedAsset::GetLODPathName(Mesh, LODIndex));
								OverrideBuffer->SetOwnerName(OwnerName);
#endif
								OverrideBuffer->BeginInitResources();
							}
						}
					}
					else
					{
						FSkinWeightProfilesData* DataPtr = this;
						FRequestFinished Callback = [DataPtr](TWeakObjectPtr<USkeletalMesh> WeakMesh, FSkinWeightProfileStack ProfileStackRequested)
						{
							if (WeakMesh.IsValid())
							{
								FSkinnedMeshComponentRecreateRenderStateContext RecreateState(WeakMesh.Get());
								DataPtr->bDefaultOverridden = true;
								DataPtr->DefaultProfileStack = ProfileStackRequested;
								DataPtr->SetupDynamicDefaultSkinWeightProfile();
							}
						};

						UWorld* World = nullptr;
#if WITH_EDITOR
						World = GWorld;
#else
						UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
						if (GameEngine)
						{
							World = GameEngine->GetGameWorld();
						}
#endif

						if (World)
						{
							if (FSkinWeightProfileManager* Manager = FSkinWeightProfileManager::Get(World))
							{
								Manager->RequestSkinWeightProfileStack(ProfileStack, Mesh, Mesh, Callback, LODIndex);
							}
						}
					}
				}
				else
				{
					bDefaultOverridden = true;
					DefaultProfileStack = ProfileStack;

					SetupDynamicDefaultSkinWeightProfile();
				}
			}
		}
	}
}

void FSkinWeightProfilesData::ClearDynamicDefaultSkinWeightProfile(USkeletalMesh* Mesh, int32 LODIndex)
{
	if (bStaticOverridden)
	{
		UE_LOGF(LogSkeletalMesh, Error, "[%ls] Skeletal Mesh has overridden the default Skin Weights buffer during serialization, cannot clear the skin weight profile.", *Mesh->GetName());		
		return;
	}

	if (bDefaultOverridden)
	{
		if (DefaultOverrideSkinWeightBuffer != nullptr)
		{
#if !WITH_EDITOR
			// Only release when not in Editor, as any other viewport / editor could be relying on this buffer
			ReleaseBuffer(DefaultProfileStack, true);
#endif // !WITH_EDITOR
			DefaultOverrideSkinWeightBuffer = nullptr;
		}

		bDefaultOverridden = false;
		DefaultProfileStack = {};		
	}
}

void FSkinWeightProfilesData::SetupDynamicDefaultSkinWeightProfile()
{
	UE::TReadScopeLock Lock(ProfileBufferMapLock);
	if (ProfileStackToBuffer.Contains(DefaultProfileStack) && bDefaultOverridden && !bStaticOverridden)
	{
		DefaultOverrideSkinWeightBuffer = ProfileStackToBuffer.FindChecked(DefaultProfileStack);
	}
}

bool FSkinWeightProfilesData::ContainsProfile(const FName& ProfileName) const
{
	FScopeLock ScopeLock(&OverrideDataLock);
	
	return OverrideData.Contains(ProfileName);
}

FSkinWeightVertexBuffer* FSkinWeightProfilesData::GetOverrideBuffer(const FSkinWeightProfileStack& InProfileStack) const
{
	SCOPED_NAMED_EVENT(FSkinWeightProfilesData_GetOverrideBuffer, FColor::Red);

	FSkinWeightProfileStack ProfileStack{InProfileStack.Normalized()};
	
	// In case we have overridden the default skin weight buffer we do not need to create an override buffer, if it was statically overridden we cannot load any other profile
	if (bDefaultOverridden && (ProfileStack == DefaultProfileStack || bStaticOverridden))
	{	
		if (!bEmittedOverrideWarning && bStaticOverridden && ProfileStack != DefaultProfileStack)
		{
			UE_LOGF(LogSkeletalMesh, Error, "Skeletal Mesh has overridden the default Skin Weights buffer during serialization, cannot set any other skin weight profile.");
			bEmittedOverrideWarning = true;
		}	

		return nullptr;
	}

	if (BaseBuffer)
	{
		check(BaseBuffer->GetNumVertices() > 0);

		UE::TReadScopeLock Lock(ProfileBufferMapLock);
		if (FSkinWeightVertexBuffer* const* BufferPtr = ProfileStackToBuffer.Find(ProfileStack))
		{
			return *BufferPtr;
		}
	}

	return nullptr;
}


bool FSkinWeightProfilesData::ContainsOverrideBuffer(const FSkinWeightProfileStack& InProfileStack) const
{
	UE::TReadScopeLock Lock(ProfileBufferMapLock);
	return ProfileStackToBuffer.Contains(InProfileStack.Normalized());
}


const FRuntimeSkinWeightProfileData* FSkinWeightProfilesData::GetOverrideData(const FName& ProfileName) const
{
	FScopeLock ScopeLock(&OverrideDataLock);
	return OverrideData.Find(ProfileName);
}

FRuntimeSkinWeightProfileData& FSkinWeightProfilesData::AddOverrideData(const FName& ProfileName)
{
	FScopeLock ScopeLock(&OverrideDataLock);
	return OverrideData.FindOrAdd(ProfileName);
}


void FSkinWeightProfilesData::ReleaseBuffer(const FSkinWeightProfileStack& InProfileStack, bool bForceRelease /*= false*/)
{
	FSkinWeightProfileStack ProfileStack{InProfileStack.Normalized()};
	
	UE::TWriteScopeLock Lock(ProfileBufferMapLock);
	if (ProfileStackToBuffer.Contains(ProfileStack) && (!bDefaultOverridden || ProfileStack != DefaultProfileStack || bForceRelease))
	{
		FSkinWeightVertexBuffer* Buffer = nullptr;
		ProfileStackToBuffer.RemoveAndCopyValue(ProfileStack, Buffer);

		if (Buffer)
		{
			DEC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, Buffer->GetVertexDataSize());
			if (GSkinWeightProfileDeferredCleanUp)
			{
				ENQUEUE_RENDER_COMMAND(ReleaseSkinSkinWeightProfilesDataBufferCommand)(
					UE::RenderCommandPipe::SkeletalMesh,
					[Buffer](FRHICommandList& RHICmdList)
				{
					Buffer->ReleaseResources();

					// Two-stage deferral: release RHI on RT, then BeginCleanup() the wrapper
					// next GT tick. Calling BeginCleanup() directly races in-flight RHI cmd
					// lists that hold raw FRHIBuffer* (from cached MDCs' SetStreamSource).
					FTSTicker::GetCoreTicker().AddTicker(
						TEXT("SkinWeightProfileBufferCleanup"),
						0.0f,
						[Buffer](float) -> bool
						{
							BeginCleanup(new FSkinWeightVertexBuffersDeleter({ Buffer }));
							return false;
						});
				});
			}
			else
			{
				ENQUEUE_RENDER_COMMAND(ReleaseSkinSkinWeightProfilesDataBufferCommand)(
					UE::RenderCommandPipe::SkeletalMesh,
					[Buffer](FRHICommandList& RHICmdList)
				{
					Buffer->ReleaseResources();
				});

				BeginCleanup(new FSkinWeightVertexBuffersDeleter({ Buffer }));
			}
		}
	}
}

void FSkinWeightProfilesData::ReleaseResources()
{
	TArray<FSkinWeightVertexBuffer*> Buffers;

	{
		UE::TWriteScopeLock Lock(ProfileBufferMapLock);
		ProfileStackToBuffer.GenerateValueArray(Buffers);
		ProfileStackToBuffer.Empty();

		// Never release a default _dynamic_ buffer
		if (bDefaultOverridden && !bStaticOverridden)
		{
			ensure(DefaultOverrideSkinWeightBuffer != nullptr);
			Buffers.Remove(DefaultOverrideSkinWeightBuffer);
			ProfileStackToBuffer.Add(DefaultProfileStack, DefaultOverrideSkinWeightBuffer);
		}

		Buffers.Remove(nullptr);
	}

	ResetGPUReadback();

	if (Buffers.Num())
	{
		if (GSkinWeightProfileDeferredCleanUp)
		{
			ENQUEUE_RENDER_COMMAND(ReleaseSkinSkinWeightProfilesDataBufferCommand)(
				UE::RenderCommandPipe::SkeletalMesh,
				[LambdaBuffers = MoveTemp(Buffers)](FRHICommandList& RHICmdList) mutable
			{
				for (FSkinWeightVertexBuffer* Buffer : LambdaBuffers)
				{
					Buffer->ReleaseResources();
				}

				// Two-stage deferral: release RHI on RT, then BeginCleanup() the wrapper
				// next GT tick. Calling BeginCleanup() directly races in-flight RHI cmd
				// lists that hold raw FRHIBuffer* (from cached MDCs' SetStreamSource).
				FTSTicker::GetCoreTicker().AddTicker(
					TEXT("SkinWeightProfileBufferCleanup"),
					0.0f,
					[Buffers = MoveTemp(LambdaBuffers)](float) mutable -> bool
					{
						BeginCleanup(new FSkinWeightVertexBuffersDeleter(MoveTemp(Buffers)));
						return false;
					});
			});
		}
		else
		{
			ENQUEUE_RENDER_COMMAND(ReleaseSkinSkinWeightProfilesDataBufferCommand)(
				UE::RenderCommandPipe::SkeletalMesh,
				[Buffers](FRHICommandList& RHICmdList)
			{
				for (FSkinWeightVertexBuffer* Buffer : Buffers)
				{
					Buffer->ReleaseResources();
				}
			});

			BeginCleanup(new FSkinWeightVertexBuffersDeleter(MoveTemp(Buffers)));
		}
	}
}

SIZE_T FSkinWeightProfilesData::GetResourcesSize() const
{
	UE::TReadScopeLock Lock(ProfileBufferMapLock);

	SIZE_T SummedSize = 0;
	for (const TPair<FSkinWeightProfileStack, FSkinWeightVertexBuffer*>& Item : ProfileStackToBuffer)
	{
		SummedSize += Item.Value->GetVertexDataSize();
	}

	return SummedSize;
}

SIZE_T FSkinWeightProfilesData::GetCPUAccessMemoryOverhead() const
{
	UE::TReadScopeLock Lock(ProfileBufferMapLock);
	SIZE_T Result = 0;
	for (const TPair<FSkinWeightProfileStack, FSkinWeightVertexBuffer*>& Item : ProfileStackToBuffer)
	{
		Result += Item.Value->GetNeedsCPUAccess() ? Item.Value->GetVertexDataSize() : 0;
	}
	return Result;
}

void FSkinWeightProfilesData::SerializeMetaData(FArchive& Ar)
{
	FScopeLock ScopeLock(&OverrideDataLock);
	
	TArray<FName, TInlineAllocator<8>> ProfileNames;
	if (Ar.IsSaving())
	{
		OverrideData.GenerateKeyArray(ProfileNames);
		Ar << ProfileNames;
	}
	else
	{
		Ar << ProfileNames;
		OverrideData.Empty(ProfileNames.Num());
		for (int32 Idx = 0; Idx < ProfileNames.Num(); ++Idx)
		{
			OverrideData.Add(ProfileNames[Idx]);
		}
	}
}

void FSkinWeightProfilesData::ReleaseCPUResources()
{
	FScopeLock ScopeLock(&OverrideDataLock);
	for (TPair<FName, FRuntimeSkinWeightProfileData>& Item: OverrideData)
	{
		Item.Value = FRuntimeSkinWeightProfileData();
	}

	ResetGPUReadback();
}

void FSkinWeightProfilesData::CreateRHIBuffers(FRHICommandListBase& RHICmdList, TArray<TPair<FSkinWeightProfileStack, FSkinWeightRHIInfo>>& OutBuffers)
{
	// Write lock: ApplyOverrideProfileStack mutates each OverrideBuffer's WeightData / Data /
	// NumBoneWeights via delete + new + memcpy. Concurrent mutators (InitialiseProfileBuffer,
	// SetupDynamicDefaultSkinWeightProfile) must be excluded from the buffer values, not just
	// the map, otherwise CreateRHIBuffer can read a torn / freed WeightData and produce a
	// null-InitialData buffer create (FORT-1093721 / similar stream-in crashes).
	UE::TWriteScopeLock Lock(ProfileBufferMapLock);

	const int32 NumActiveProfiles = ProfileStackToBuffer.Num();
	check(BaseBuffer || !NumActiveProfiles);
	OutBuffers.Empty(NumActiveProfiles);
	for (TPair<FSkinWeightProfileStack, FSkinWeightVertexBuffer*>& Item : ProfileStackToBuffer)
	{
		const FSkinWeightProfileStack& ProfileStack = Item.Key;
		FSkinWeightVertexBuffer* OverrideBuffer = Item.Value;
		ApplyOverrideProfileStack(ProfileStack, OverrideBuffer);

		OutBuffers.Emplace(ProfileStack, OverrideBuffer->CreateRHIBuffer(RHICmdList));
	}
}

bool FSkinWeightProfilesData::IsPendingReadback() const
{
	FScopeLock Lock(&ReadbackData.Mutex);
	
	return !ReadbackData.BufferReadback.IsValid();
}

bool FSkinWeightProfilesData::ShouldEnqueueGPUReadback() const
{
	if (!BaseBuffer)
	{
		return false;
	}

	const FSkinWeightDataVertexBuffer* DataVB = BaseBuffer->GetDataVertexBuffer();
	if (!DataVB || DataVB->GetVertexDataSize() == 0)
	{
		return false;
	}

	// EnqueueGPUReadback's non-delayed-loads branch additionally requires weight data to be valid.
	if (!FSkinWeightProfileManager::HandleDelayedLoads() && !DataVB->IsWeightDataValid())
	{
		return false;
	}

	// CPU-side weights already available — caller should take the CPU path
	// (FSkinWeightProfilesData::InitialiseProfileBuffer) instead of paying for a GPU readback.

	if (FSkinWeightProfileManager::AllowCPU())
	{
		const bool bHasCPUData =
			(FSkinWeightProfileManager::HandleDelayedLoads() && DataVB->IsWeightDataValid())
			|| (!FSkinWeightProfileManager::HandleDelayedLoads() && BaseBuffer->GetNeedsCPUAccess());
		if (bHasCPUData)
		{
			return false;
		}
	}

	const EReadbackStatus Status = GetReadbackStatus();
	if (Status == EReadbackStatus::BufferInitialized)
	{
		return false;
	}

	return true;
}

void FSkinWeightProfilesData::EnqueueGPUReadback()
{
	FScopeLock Lock(&ReadbackData.Mutex);

	ensure(!ReadbackData.BufferReadback.IsValid());
	SetReadbackStatus(EReadbackStatus::PendingGPUReadback);
	if (FSkinWeightProfileManager::HandleDelayedLoads())
	{
		if (BaseBuffer && BaseBuffer->GetDataVertexBuffer()->GetVertexDataSize())
		{
			static const FName ReadbackName("ReadbackSkinWeightBuffer");
			ENQUEUE_RENDER_COMMAND(FSkinWeightProfilesData_EnqueueGPUReadback)(
				[&Data=ReadbackData, SkinWeightBuffer=BaseBuffer, &Status = ReadbackStatus](FRHICommandListImmediate& RHICmdList)
			{
				FScopeLock Lock(&Data.Mutex);
					
				FRHIBuffer* VB = SkinWeightBuffer->GetDataVertexBuffer()->VertexBufferRHI;
				if (VB && VB->GetSize())
				{
					// Only set up the readback buffer if we have a vertex buffer to read from. It's possible we're still waiting for
					// the mesh to be streamed in.
					Data.BufferReadback.Reset(new FRHIGPUBufferReadback(ReadbackName));
					Data.BufferReadback->EnqueueCopy(RHICmdList, VB);
					Data.CapturedSize = VB->GetSize();
					Status.Store((uint32)EReadbackStatus::GPUReadbackEnqueued);
				}
				else
				{
					// If the mesh has not been streamed in, reset status
					// so that callee knows to try readback again.
					Data.CapturedSize = 0;
					Status.Store((uint32)EReadbackStatus::None);
				}
			});
		}
	}
	else
	{
		if (BaseBuffer && BaseBuffer->GetDataVertexBuffer()->IsWeightDataValid() && BaseBuffer->GetDataVertexBuffer()->GetVertexDataSize())
		{
			static const FName ReadbackName("ReadbackSkinWeightBuffer");
			ENQUEUE_RENDER_COMMAND(FSkinWeightProfilesData_EnqueueGPUReadback)(
				[&Data=ReadbackData, SkinWeightBuffer=BaseBuffer, &Status = ReadbackStatus](FRHICommandListImmediate& RHICmdList)
			{
				FScopeLock Lock(&Data.Mutex);
			
				Data.BufferReadback.Reset(new FRHIGPUBufferReadback(ReadbackName));
				FRHIBuffer* VB = SkinWeightBuffer->GetDataVertexBuffer()->VertexBufferRHI;
				if (Data.BufferReadback.IsValid() && VB && VB->GetSize())
				{
					Data.BufferReadback->EnqueueCopy(RHICmdList, VB);
					Data.CapturedSize = VB->GetSize();
					Status.Store((uint32)EReadbackStatus::GPUReadbackEnqueued);
				}
				else
				{
					Data.CapturedSize = 0;
					Status.Store((uint32)EReadbackStatus::None);
				}
			});
		}
	}
}

bool FSkinWeightProfilesData::IsGPUReadbackFinished() const
{
	FScopeLock Lock(&ReadbackData.Mutex);

	return !IsPendingReadback() && ReadbackData.BufferReadback->IsReady();
}

void FSkinWeightProfilesData::EnqueueDataReadback()
{
	if (GetReadbackStatus() >= EReadbackStatus::DataReadbackEnqueued)
	{
		return;
	}

	FScopeLock Lock(&ReadbackData.Mutex);

	ensure(ReadbackData.ReadbackData.Num() == 0 && ReadbackData.BufferReadback->IsReady());

	if ( BaseBuffer )
	{
		SetReadbackStatus(EReadbackStatus::DataReadbackEnqueued);
		ReadbackData.ReadbackData.SetNumZeroed(BaseBuffer->GetVertexDataSize());

		ENQUEUE_RENDER_COMMAND(FSkinWeightProfilesData_EnqueueDataReadback)(
				[&Data=ReadbackData, &Status = ReadbackStatus](FRHICommandListImmediate& )
		{
			FScopeLock Lock(&Data.Mutex);
		
			if (Data.BufferReadback.IsValid() && Data.CapturedSize != 0)
			{
				const uint32 CapturedSize = Data.CapturedSize;
				const uint32 ExpectedSize = static_cast<uint32>(Data.ReadbackData.Num());
				if (CapturedSize != ExpectedSize)
				{
					// Source VertexBufferRHI re-init'd between EnqueueGPUReadback and here.
					UE_LOGF(LogSkeletalMesh, Verbose, "SkinWeight readback size mismatch: expected %u, captured %u - %ls.", ExpectedSize, CapturedSize, GResetReadbackOnSizeMismatch ? TEXT("retrying") : TEXT("zeroing dst"));
					if (GResetReadbackOnSizeMismatch)
					{
						++Data.RetryCount;
						Data.BufferReadback.Reset();
						Data.ReadbackData.Empty();
						Data.CapturedSize = 0;
						Data.ReadbackFinishedFrameIndex = INDEX_NONE;
						Status.Store((uint32)EReadbackStatus::None);
					}
					else
					{
						FMemory::Memzero(Data.ReadbackData.GetData(), Data.ReadbackData.Num());
						Data.ReadbackFinishedFrameIndex = GFrameNumberRenderThread;
						Status.Store((uint32)EReadbackStatus::DataReadbackFinished);
					}
				}
				else
				{
					ensure(Data.BufferReadback->IsReady());
					const void* BufferPtr = Data.BufferReadback->Lock(Data.ReadbackData.Num());
					FMemory::Memcpy(Data.ReadbackData.GetData(), BufferPtr, Data.ReadbackData.Num());
					Data.BufferReadback->Unlock();

					if (Data.RetryCount > 0)
					{
						UE_LOGF(LogSkeletalMesh, Verbose, "SkinWeight readback succeeded after %u retry(ies), size %u.", Data.RetryCount, ExpectedSize);
						Data.RetryCount = 0;
					}

					Data.ReadbackFinishedFrameIndex = GFrameNumberRenderThread;
					Status.Store((uint32)EReadbackStatus::DataReadbackFinished);
				}
			}
		});
	}
}

bool FSkinWeightProfilesData::IsDataReadbackPending() const
{
	FScopeLock Lock(&ReadbackData.Mutex);
	
	return ReadbackData.ReadbackData.Num() > 0;
}

bool FSkinWeightProfilesData::IsDataReadbackFinished() const
{
	FScopeLock Lock(&ReadbackData.Mutex);
	const EReadbackStatus Status = GetReadbackStatus();
	const bool bReadbackDone = Status == EReadbackStatus::DataReadbackFinished || Status == EReadbackStatus::BufferInitialized;
	return bReadbackDone || (!IsPendingReadback() && IsGPUReadbackFinished() && ReadbackData.ReadbackFinishedFrameIndex != INDEX_NONE && GFrameNumberRenderThread > ReadbackData.ReadbackFinishedFrameIndex);
}

void FSkinWeightProfilesData::ResetGPUReadback()
{
	FScopeLock Lock(&ReadbackData.Mutex);
	
	ReadbackData.BufferReadback.Reset();
	ReadbackData.ReadbackData.Empty();
	ReadbackData.CapturedSize = 0;
	ReadbackData.RetryCount = 0;
	ReadbackData.ReadbackFinishedFrameIndex = INDEX_NONE;
	SetReadbackStatus(EReadbackStatus::None);
}

void FSkinWeightProfilesData::PumpGPUReadback()
{
	// GPU readback work is mutex protected and profile buffer map
	// access is protected by a spin lock during this flow.

	const EReadbackStatus Status = GetReadbackStatus();
	ensure(Status != EReadbackStatus::BufferInitialized);

	bool bDataReady = (Status == EReadbackStatus::DataReadbackFinished);
	if (Status == EReadbackStatus::None)
	{
		EnqueueGPUReadback();
	}
	else if (!bDataReady)
	{
		if (IsGPUReadbackFinished() && !IsDataReadbackPending())
		{
			EnqueueDataReadback();
		}
		else if (IsDataReadbackFinished())
		{
			bDataReady = true;
		}
	}

	// Caller is expected to set this value. 
	ensure(!PendingProfileStack.IsEmpty());
	if (bDataReady)
	{
		InitialiseProfileBuffer(PendingProfileStack);
	}
}

bool FSkinWeightProfilesData::DoReadbackIfNeeded(const FSkinWeightProfileStack& InProfileStack)
{
	const EReadbackStatus Status = GetReadbackStatus();
	if (Status != EReadbackStatus::BufferInitialized)
	{
		const bool bIsCPUData =
			(FSkinWeightProfileManager::AllowCPU() && FSkinWeightProfileManager::HandleDelayedLoads() && BaseBuffer && BaseBuffer->GetDataVertexBuffer()->IsWeightDataValid()) ||
			(!FSkinWeightProfileManager::HandleDelayedLoads() && BaseBuffer && BaseBuffer->GetNeedsCPUAccess());

		if (bIsCPUData)
		{
			InitialiseProfileBuffer(InProfileStack);

			if (GetReadbackStatus() == EReadbackStatus::BufferInitialized)
			{
				return true;
			}
		}
		// This path is only taken if weight data was not available on
		// the CPU side when the skin weight profile request was made.
		// Pump the GPU Readback work until buffer has been initialized. 
		SetPendingProfileStack(InProfileStack);
		PumpGPUReadback(); // Todo: Maybe pass profile stack here directly and remove class variable?
		return false;
	}

	// Buffer data is available, return true.
    return Status == EReadbackStatus::BufferInitialized;
}

bool FSkinWeightProfilesData::HandleDelayedLoads()
{
	return GHandleDelayedLoads != 0;
}

bool FSkinWeightProfilesData::AllowCPU()
{
	return GAllowCPU != 0;
}

bool FSkinWeightProfilesData::HasProfileStack(const FSkinWeightProfileStack& InProfileStack) const
{
	UE::TReadScopeLock Lock(ProfileBufferMapLock);
	check(InProfileStack == InProfileStack.Normalized());
	return ProfileStackToBuffer.Contains(InProfileStack);
}

void FSkinWeightProfilesData::InitialiseProfileBuffer(const FSkinWeightProfileStack& InProfileStack)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	// Have we already constructed this particular profile stack?
	if (HasProfileStack(InProfileStack))
	{
		return;
	}

	if (BaseBuffer)
	{
		const uint8* BaseBufferData;

		const bool bIsCPUData =
			(FSkinWeightProfileManager::AllowCPU() && FSkinWeightProfileManager::HandleDelayedLoads() && BaseBuffer->GetDataVertexBuffer()->IsWeightDataValid()) ||
			(!FSkinWeightProfileManager::HandleDelayedLoads() && BaseBuffer->GetNeedsCPUAccess());  

		// If we have the weight data, then just use that directly. Otherwise, assume that we've been called as a result of a successful
		// GPU readback.
		if (bIsCPUData)
		{
			BaseBufferData = BaseBuffer->GetDataVertexBuffer()->GetWeightData();
		}
		else
		{
			// Make sure we have a lock on the readback data, in case ResetGPUReadback is called while trying to access the data.
			ReadbackData.Mutex.Lock();
			ensure(IsDataReadbackFinished());
			BaseBufferData = ReadbackData.ReadbackData.GetData();
		}
		
		if (ensure(BaseBufferData))
		{
			FSkinWeightVertexBuffer* OverrideBuffer = new FSkinWeightVertexBuffer();
			OverrideBuffer->SetNeedsCPUAccess(BaseBuffer->GetNeedsCPUAccess());

			// Hold the write lock across Add + Apply so concurrent readers (CreateRHIBuffers
			// on the streaming worker, GetSkinWeightVertexBuffer on the render thread) cannot
			// observe the entry while its WeightData / Data / NumBoneWeights are still being
			// populated. See CreateRHIBuffers for the full contract.
			{
				UE::TWriteScopeLock Lock(ProfileBufferMapLock);
				ProfileStackToBuffer.Add(InProfileStack, OverrideBuffer);
				ApplyOverrideProfileStack(InProfileStack, OverrideBuffer, BaseBufferData);
				SetReadbackStatus(EReadbackStatus::BufferInitialized);
			}

			if (!bIsCPUData)
			{
				// Unlock the readback data as soon as we're done with it.
				ReadbackData.Mutex.Unlock();
				PendingProfileStack = FSkinWeightProfileStack::GetEmpty();
			}
			
#if RHI_ENABLE_RESOURCE_INFO
			const FName OwnerName = FName(InProfileStack.GetUniqueId() + TEXT("_FSkinWeightProfilesData"));
			OverrideBuffer->SetOwnerName(OwnerName);
#endif
			OverrideBuffer->BeginInitResources();
		}
		else if (!bIsCPUData)
		{
			// Unlock the readback data immediately in case of failure.
			ReadbackData.Mutex.Unlock();
		}

	}
}

void FSkinWeightProfilesData::ApplyOverrideProfileStack(
	const FSkinWeightProfileStack& InProfileStack,
	FSkinWeightVertexBuffer* OverrideBuffer,
	const uint8* BaseBufferData
	)
{
	if (!BaseBufferData)
	{
		BaseBufferData = BaseBuffer->GetDataVertexBuffer()->GetWeightData();
	}
	
	OverrideBuffer->CopyMetaData(*BaseBuffer);
	OverrideBuffer->CopySkinWeightRawDataFromBuffer(BaseBufferData, BaseBuffer->GetNumVertices());
	
	for (int32 LayerIndex = 0; LayerIndex < FSkinWeightProfileStack::MaxLayerCount; ++LayerIndex)
	{
		if (FName ProfileName = InProfileStack.Layers[LayerIndex];
			!ProfileName.IsNone())
		{
			FScopeLock ScopeLock(&OverrideDataLock);
			
			if (const FRuntimeSkinWeightProfileData* ProfilePtr = OverrideData.Find(ProfileName))
			{
				// Only copy the base buffer's weights when applying the first layer.
				ProfilePtr->ApplyOverrides(OverrideBuffer);
			}	
		}
	}

}

void FSkinWeightProfilesData::InitRHIForStreaming(
	const TArray<TPair<FSkinWeightProfileStack, FSkinWeightRHIInfo>>& IntermediateBuffers,
	FRHIResourceReplaceBatcher& Batcher
	)
{
	// Write lock: publishing new VertexBufferRHI handles via the Batcher. Pair this with
	// the write lock taken by CreateRHIBuffers / ReleaseRHIForStreaming so the streaming
	// pipeline runs as one critical section per LOD.
	UE::TWriteScopeLock Lock(ProfileBufferMapLock);
	for (int32 Idx = 0; Idx < IntermediateBuffers.Num(); ++Idx)
	{
		const FSkinWeightProfileStack& ProfileStack = IntermediateBuffers[Idx].Key;
		const FSkinWeightRHIInfo& IntermediateBuffer = IntermediateBuffers[Idx].Value;
		ProfileStackToBuffer.FindChecked(ProfileStack)->InitRHIForStreaming(IntermediateBuffer, Batcher);
	}
}

void FSkinWeightProfilesData::ReleaseRHIForStreaming(FRHIResourceReplaceBatcher& Batcher)
{
	// Write lock: nulling VertexBufferRHI handles via the Batcher. See CreateRHIBuffers
	// for the contract — buffer-content access (including RHI pointer publication) requires
	// the write lock so concurrent readers cannot observe a torn state.
	UE::TWriteScopeLock Lock(ProfileBufferMapLock);
	for (TPair<FSkinWeightProfileStack, FSkinWeightVertexBuffer*>& Item: ProfileStackToBuffer)
	{
		Item.Value->ReleaseRHIForStreaming(Batcher);
	}
}

void FRuntimeSkinWeightProfileData::ApplyOverrides(FSkinWeightVertexBuffer* OverrideBuffer) const
{
	if (OverrideBuffer)
	{
		uint8* TargetSkinWeightData = OverrideBuffer->GetDataVertexBuffer()->GetWeightData();
		
		const uint8 VertexStride = OverrideBuffer->GetConstantInfluencesVertexStride();
		const uint8 WeightDataOffset = OverrideBuffer->GetBoneIndexByteSize() * OverrideBuffer->GetMaxBoneInfluences();

		// Apply overrides
		for (auto VertexIndexOverridePair : VertexIndexToInfluenceOffset)
		{
			const uint32 VertexIndex = VertexIndexOverridePair.Key;
			const uint32 InfluenceOffset = VertexIndexOverridePair.Value;
			
			uint8* BoneData = TargetSkinWeightData + (VertexIndex * VertexStride);
			uint8* WeightData = BoneData + WeightDataOffset;

#if !UE_BUILD_SHIPPING
			uint32 VertexOffset = 0;
			uint32 VertexInfluenceCount = 0;
			OverrideBuffer->GetVertexInfluenceOffsetCount(VertexIndex, VertexOffset, VertexInfluenceCount);
			check(NumWeightsPerVertex <= VertexInfluenceCount);
			check((void*)(((uint8*)TargetSkinWeightData) + VertexOffset) == (void*)BoneData);
			check(b16BitBoneIndices == OverrideBuffer->Use16BitBoneIndex());
#endif
			// BoneIDs either contains FBoneIndexType entries spanning (2) uint8 values, or single uint8 bone indices (1)
			const uint32 BoneIndexByteSize = OverrideBuffer->GetBoneIndexByteSize();
			const uint32 BoneWeightByteSize = OverrideBuffer->GetBoneWeightByteSize();
			FMemory::Memcpy(BoneData, &BoneIDs[InfluenceOffset * NumWeightsPerVertex * BoneIndexByteSize], BoneIndexByteSize * NumWeightsPerVertex);
			FMemory::Memcpy(WeightData, &BoneWeights[InfluenceOffset * NumWeightsPerVertex * BoneWeightByteSize], BoneWeightByteSize * NumWeightsPerVertex);
		}
	}
}

void FRuntimeSkinWeightProfileData::ApplyDefaultOverride(FSkinWeightVertexBuffer* Buffer) const
{
	if (Buffer)
	{
		const int32 ExpectedNumVerts = Buffer->GetNumVertices();
		if (ExpectedNumVerts)
		{
			uint8* TargetSkinWeightData = (uint8*)Buffer->GetDataVertexBuffer()->GetWeightData();

			const uint8 VertexStride = Buffer->GetConstantInfluencesVertexStride();
			const uint8 WeightDataOffset = Buffer->GetBoneIndexByteSize() * Buffer->GetMaxBoneInfluences();

			for (auto VertexIndexOverridePair : VertexIndexToInfluenceOffset)
			{
				const uint32 VertexIndex = VertexIndexOverridePair.Key;
				const uint32 InfluenceOffset = VertexIndexOverridePair.Value;

				const uint8* BoneData = TargetSkinWeightData + (VertexIndex * VertexStride);
				const uint8* WeightData = BoneData + WeightDataOffset;

#if !UE_BUILD_SHIPPING
				uint32 VertexOffset = 0;
				uint32 VertexInfluenceCount = 0;
				Buffer->GetVertexInfluenceOffsetCount(VertexIndex, VertexOffset, VertexInfluenceCount);
				check(NumWeightsPerVertex <= VertexInfluenceCount);
				check((void*)(((uint8*)TargetSkinWeightData) + VertexOffset) == (void*)BoneData);
				check(b16BitBoneIndices == Buffer->Use16BitBoneIndex());
#endif
				// BoneIDs either contains FBoneIndexType entries spanning (2) uint8 values, or single uint8 bone indices (1)
				FMemory::Memcpy((void*)BoneData, &BoneIDs[InfluenceOffset * NumWeightsPerVertex * Buffer->GetBoneIndexByteSize()], Buffer->GetBoneIndexByteSize() * NumWeightsPerVertex);
				FMemory::Memcpy((void*)WeightData, &BoneWeights[InfluenceOffset * NumWeightsPerVertex], sizeof(uint8) * NumWeightsPerVertex);
			}
		}
	}
}
