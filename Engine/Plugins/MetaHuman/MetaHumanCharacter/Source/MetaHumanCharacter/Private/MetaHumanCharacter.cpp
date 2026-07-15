// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacter.h"

#include "MetaHumanCharacterCustomVersion.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCharacterLog.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanWardrobeItem.h"

#include "AssetRegistry/AssetData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/TransactionObjectEvent.h"
#include "Logging/StructuredLog.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Templates/SharedPointer.h"
#include "HAL/IConsoleManager.h"
#include "Algo/Compare.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCharacter)

#define LOCTEXT_NAMESPACE "MetaHumanCharacter"

bool FMetaHumanCharacterAssetsSection::operator==(const FMetaHumanCharacterAssetsSection& Other) const
{
	return
		(ContentDirectoryToMonitor.Path == Other.ContentDirectoryToMonitor.Path &&
			SlotName == Other.SlotName &&
			ClassesToFilter == Other.ClassesToFilter);
}

namespace UE::MetaHuman
{
	static TAutoConsoleVariable<bool> CVarMHCUseTextureCompression
	{
		TEXT("mh.Character.UseTextureCompression"),
		true,
		TEXT("Set to true to store the Character face and body textures in a compressed format."),
		ECVF_Default
	};

#if WITH_EDITOR
	namespace ThumbnailObjectName
	{
		static constexpr FStringView CharacterBody = TEXTVIEW("ThumbnailAux_CharacterBody");
		static constexpr FStringView Face = TEXTVIEW("ThumbnailAux_Face");
		static constexpr FStringView Body = TEXTVIEW("ThumbnailAux_Body");
	}
#endif

	/**
	* Utility function to read and write compressed editor bulk data
	* TODO: this not working as intended since UpdatePayload() uncompress the input compressed buffer before serializing
	*/
	static void CompressAndUpdateBulkData(UE::Serialization::FEditorBulkData& InBulkData, const FSharedBuffer& InBuffer, UObject* InOwner)
	{
		FCompressedBuffer CompressedPayLoad = FCompressedBuffer::Compress(InBuffer);
		InBulkData.UpdatePayload(CompressedPayLoad, InOwner);
	}

	static void CompressAndUpdateBulkData(UE::Serialization::FEditorBulkData& InBulkData, FMemoryView InData, UObject* InOwner)
	{
		FSharedBuffer Payload = FSharedBuffer::Clone(InData);
		CompressAndUpdateBulkData(InBulkData, Payload, InOwner);
	}

	[[nodiscard]] static FSharedBuffer GetBufferBulkData(const UE::Serialization::FEditorBulkData& InBulkData)
	{
		FSharedBuffer PayloadData;

		if (InBulkData.HasPayloadData())
		{
			TSharedFuture<FSharedBuffer> PayloadFuture = InBulkData.GetPayload();
			PayloadData = PayloadFuture.Get();
		}

		return PayloadData;
	}
}

UMetaHumanCharacter::UMetaHumanCharacter()
{
	//Need to make sure the map with bulk data doesn't change in order to avoid data from being moved
	// when adding new entries to it, so allocate all entries here
	for (EFaceTextureType TextureType : TEnumRange<EFaceTextureType>())
	{
		SynthesizedFaceTexturesData.Add(TextureType);
	}

	for (EBodyTextureType TextureType : TEnumRange<EBodyTextureType>())
	{
		HighResBodyTexturesData.Add(TextureType);
	}

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		InternalCollection = CreateDefaultSubobject<UMetaHumanCollection>(TEXT("InternalCollection"));
		InternalCollection->SetFlags(RF_Public);

#if WITH_EDITORONLY_DATA
		ThumbnailAux_CharacterBody = CreateDefaultSubobject<UMetaHumanCharacterThumbnailAux>(UE::MetaHuman::ThumbnailObjectName::CharacterBody.GetData());
		ThumbnailAux_Face = CreateDefaultSubobject<UMetaHumanCharacterThumbnailAux>(UE::MetaHuman::ThumbnailObjectName::Face.GetData());
		ThumbnailAux_Body = CreateDefaultSubobject<UMetaHumanCharacterThumbnailAux>(UE::MetaHuman::ThumbnailObjectName::Body.GetData());
#endif

#if WITH_EDITOR
		// If the palette's pipeline changes, that could cause this Character to be removed from its slot, so
		// make sure it's still set up correctly.
		InternalCollection->OnPipelineChanged.AddUObject(this, &UMetaHumanCharacter::ConfigureCollection);
#endif
	}
}

#if WITH_EDITOR
void UMetaHumanCharacter::PostInitProperties()
{
	Super::PostInitProperties();

	ConfigureCollection();

	// Default AssemblySettings.NameOverride to the asset's name so that the Assembly
	// tool's Name field is pre-populated on first open. This covers both newly created
	// assets (via the factory) and any loaded asset that didn't yet have an override
	// set. The RF_ClassDefaultObject guard skips the CDO.
	if (!HasAnyFlags(RF_ClassDefaultObject) && AssemblySettings.NameOverride.IsEmpty())
	{
		AssemblySettings.NameOverride = GetName();
	}
}

void UMetaHumanCharacter::PostLoad()
{
	Super::PostLoad();

	ConfigureCollection();
}

void UMetaHumanCharacter::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);

	// Keep AssemblySettings.NameOverride in sync with the asset's name when the user
	// renames the asset in the Content Browser. We only update the override if it was
	// still tracking the previous asset name (or was empty); a user-customized override
	// is preserved.
	if (!HasAnyFlags(RF_ClassDefaultObject)
		&& (AssemblySettings.NameOverride.IsEmpty() || AssemblySettings.NameOverride == OldName.ToString()))
	{
		AssemblySettings.NameOverride = GetName();
	}
}

void UMetaHumanCharacter::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	const FName PropertyName = InPropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName = InPropertyChangedEvent.GetMemberPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacter, WardrobePaths)
		|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacter, WardrobePaths))
	{
		OnWardrobePathsChanged.Broadcast();
	}
}

void UMetaHumanCharacter::PostTransacted(const FTransactionObjectEvent& InTransactionEvent)
{
	Super::PostTransacted(InTransactionEvent);

	if (InTransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		TArray<FName> PropertiesChanged = InTransactionEvent.GetChangedProperties();

		if (PropertiesChanged.Contains(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacter, WardrobePaths)))
		{
			OnWardrobePathsChanged.Broadcast();
		}
	}
}
#endif // WITH_EDITOR

void UMetaHumanCharacter::Serialize(FArchive& InAr)
{
	Super::Serialize(InAr);

	InAr.UsingCustomVersion(FMetaHumanCharacterCustomVersion::GUID);

	FaceStateBulkData.Serialize(InAr, this);
	FaceDNABulkData.Serialize(InAr, this);
	BodyStateBulkData.Serialize(InAr, this);
	BodyDNABulkData.Serialize(InAr, this);

	for (TPair<EFaceTextureType, UE::Serialization::FEditorBulkData>& TextureBulkDataPair : SynthesizedFaceTexturesData)
	{
		TextureBulkDataPair.Value.Serialize(InAr, this);
	}

	bool bSerializeHighResBodyTextures = true;
	if (InAr.IsLoading() && InAr.CustomVer(FMetaHumanCharacterCustomVersion::GUID) < FMetaHumanCharacterCustomVersion::BodyTexturesSerialized)
	{
		bSerializeHighResBodyTextures = false;
	}

	if (bSerializeHighResBodyTextures)
	{
		for (TPair<EBodyTextureType, UE::Serialization::FEditorBulkData>& TextureBulkDataPair : HighResBodyTexturesData)
		{
			TextureBulkDataPair.Value.Serialize(InAr, this);
		}
	}
	
	int32 NumTargetMeshKeys = BodyTargetPoseStateBulkData.Num();
	if (InAr.IsSaving())
	{
		if (InAr.IsObjectReferenceCollector())
		{
			// MH-18823: Reference-rewriting archives mutate keys in place, rehash the map if anything changed. 
			bool bNeedsRehash = false;
			for (TPair<FMetaHumanCharacterTargetMeshKey, TUniquePtr<UE::Serialization::FEditorBulkData>>& Pair : BodyTargetPoseStateBulkData)
			{
				const FMetaHumanCharacterTargetMeshKey BeforeSerialization = Pair.Key;
				InAr << Pair.Key;
				bNeedsRehash |= !(Pair.Key == BeforeSerialization);
			}

			if (bNeedsRehash)
			{
				TMap<FMetaHumanCharacterTargetMeshKey, TUniquePtr<UE::Serialization::FEditorBulkData>> Rehashed;
				Rehashed.Reserve(BodyTargetPoseStateBulkData.Num());
				for (TPair<FMetaHumanCharacterTargetMeshKey, TUniquePtr<UE::Serialization::FEditorBulkData>>& Pair : BodyTargetPoseStateBulkData)
				{
					Rehashed.Add(MoveTemp(Pair.Key), MoveTemp(Pair.Value));
				}
				BodyTargetPoseStateBulkData = MoveTemp(Rehashed);
			}
		}
		else
		{
			TArray<FMetaHumanCharacterTargetMeshKey> TargetMeshKeys;
			BodyTargetPoseStateBulkData.GetKeys(TargetMeshKeys);
			TargetMeshKeys.Sort();

			InAr << NumTargetMeshKeys;

			for (FMetaHumanCharacterTargetMeshKey& TargetMeshKey : TargetMeshKeys)
			{
				InAr << TargetMeshKey;

				if (TUniquePtr<UE::Serialization::FEditorBulkData>& BulkDataPtr = BodyTargetPoseStateBulkData.FindChecked(TargetMeshKey))
				{
					BulkDataPtr->Serialize(InAr, this);
				}
			}
		}
	}
	else if (InAr.IsLoading() && InAr.CustomVer(FMetaHumanCharacterCustomVersion::GUID) >= FMetaHumanCharacterCustomVersion::BodyTargetPoseStatesSerialized)
	{
		// Load posed target states
		InAr << NumTargetMeshKeys;
		
		constexpr int32 MaxTargetMeshKeys = 1024;        
        if (NumTargetMeshKeys < 0 || NumTargetMeshKeys > MaxTargetMeshKeys)
        {
        	UE_LOGFMT(LogMetaHumanCharacter, Error, "Invalid number of target states {} loaded from serialized character data: {}", NumTargetMeshKeys, *GetPathName());
            InAr.SetError();
            return;
        }
		
		BodyTargetPoseStateBulkData.Empty(NumTargetMeshKeys);
		
		for (int32 i = 0; i < NumTargetMeshKeys; ++i)
		{
			FMetaHumanCharacterTargetMeshKey TargetMeshKey;
			InAr << TargetMeshKey;
		
			TUniquePtr<UE::Serialization::FEditorBulkData>& BulkData = BodyTargetPoseStateBulkData.FindOrAdd(TargetMeshKey);
			if (!BulkData)
			{
				BulkData = MakeUnique<UE::Serialization::FEditorBulkData>();
			}			
			BulkData->Serialize(InAr, this);
		}
	}

	if (InAr.IsSaving())
	{
		if (InAr.IsObjectReferenceCollector())
		{
			// See note above on BodyTargetPoseStateBulkData (MH-18823).
			bool bNeedsRehash = false;
			for (TPair<FMetaHumanCharacterTargetMeshKey, TArray<float>>& Pair : FaceModelCoefficientsAtTargetPoseMap)
			{
				const FMetaHumanCharacterTargetMeshKey BeforeSerialization = Pair.Key;
				InAr << Pair.Key;
				bNeedsRehash |= !(Pair.Key == BeforeSerialization);
			}

			if (bNeedsRehash)
			{
				TMap<FMetaHumanCharacterTargetMeshKey, TArray<float>> Rehashed;
				Rehashed.Reserve(FaceModelCoefficientsAtTargetPoseMap.Num());
				for (TPair<FMetaHumanCharacterTargetMeshKey, TArray<float>>& Pair : FaceModelCoefficientsAtTargetPoseMap)
				{
					Rehashed.Add(MoveTemp(Pair.Key), MoveTemp(Pair.Value));
				}
				FaceModelCoefficientsAtTargetPoseMap = MoveTemp(Rehashed);
			}
		}
		else
		{
			TArray<FMetaHumanCharacterTargetMeshKey> TargetMeshKeys;
			FaceModelCoefficientsAtTargetPoseMap.GetKeys(TargetMeshKeys);
			TargetMeshKeys.Sort();

			int32 NumKeys = TargetMeshKeys.Num();
			InAr << NumKeys;

			for (FMetaHumanCharacterTargetMeshKey& TargetMeshKey : TargetMeshKeys)
			{
				InAr << TargetMeshKey;
				InAr << FaceModelCoefficientsAtTargetPoseMap[TargetMeshKey];
			}
		}
	}
	else if (InAr.IsLoading() && InAr.CustomVer(FMetaHumanCharacterCustomVersion::GUID) >= FMetaHumanCharacterCustomVersion::FaceModelCoefficientsAtTargetPoseSerialized)
	{
		FaceModelCoefficientsAtTargetPoseMap.Empty();

		int32 NumKeys = 0;
		InAr << NumKeys;

		constexpr int32 MaxKeys = 1024;
		if (NumKeys < 0 || NumKeys > MaxKeys)
		{
			UE_LOGFMT(LogMetaHumanCharacter, Error, "Invalid number of face model coefficient entries {} loaded from serialized character data: {}", NumKeys, *GetPathName());
			InAr.SetError();
			return;
		}

		for (int32 i = 0; i < NumKeys; ++i)
		{
			FMetaHumanCharacterTargetMeshKey TargetMeshKey;
			InAr << TargetMeshKey;
			InAr << FaceModelCoefficientsAtTargetPoseMap.FindOrAdd(TargetMeshKey);
		}
	}
}

bool UMetaHumanCharacter::IsCharacterValid() const
{
	// TODO: BodyState won't have valid data at this point
	return FaceStateBulkData.HasPayloadData(); // && BodyStateBulkData.HasPayloadData();
}

void UMetaHumanCharacter::SetFaceStateData(const FSharedBuffer& InFaceStateData)
{
	UE::MetaHuman::CompressAndUpdateBulkData(FaceStateBulkData, InFaceStateData, this);
}

FSharedBuffer UMetaHumanCharacter::GetFaceStateData() const
{
	return UE::MetaHuman::GetBufferBulkData(FaceStateBulkData);
}

void UMetaHumanCharacter::SetFaceDNABuffer(TConstArrayView<uint8> InFaceDNABuffer, bool bInHasFaceDNABlendshapes)
{
	FaceDNABulkData.UpdatePayload(FSharedBuffer::MakeView(InFaceDNABuffer.GetData(), InFaceDNABuffer.Num()));
	bHasFaceDNABlendshapes = bInHasFaceDNABlendshapes;

#if WITH_EDITOR
	NotifyRiggingStateChanged();
#endif
}

bool UMetaHumanCharacter::HasFaceDNA() const
{
	return FaceDNABulkData.HasPayloadData();
}

TArray<uint8> UMetaHumanCharacter::GetFaceDNABuffer() const
{
	if (FaceDNABulkData.HasPayloadData())
	{
		TArray<uint8> DNABuffer;
		FSharedBuffer Payload = FaceDNABulkData.GetPayload().Get();
		DNABuffer.Append((const uint8*)Payload.GetData(), Payload.GetSize());
	
		return DNABuffer;
	}

	return {};
}

bool UMetaHumanCharacter::HasFaceDNABlendshapes() const
{
	return bHasFaceDNABlendshapes;
}

void UMetaHumanCharacter::SetBodyStateData(const FSharedBuffer& InBodyStateData)
{
	UE::MetaHuman::CompressAndUpdateBulkData(BodyStateBulkData, InBodyStateData, this);
}

void UMetaHumanCharacter::SetBodyTargetPoseStateData(const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const FSharedBuffer& InBodyStateData)
{
	TUniquePtr<UE::Serialization::FEditorBulkData>& BulkData = BodyTargetPoseStateBulkData.FindOrAdd(InTargetMeshKey);;
	if (!BulkData)
	{
		BulkData = MakeUnique<UE::Serialization::FEditorBulkData>();
	}
	
	UE::MetaHuman::CompressAndUpdateBulkData(*BulkData, InBodyStateData, this);
}

void UMetaHumanCharacter::SetTargetFaceModelCoefficients(const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const TArray<float>& InCoefficients)
{
	FaceModelCoefficientsAtTargetPoseMap.FindOrAdd(InTargetMeshKey) = InCoefficients;
}

TArray<float> UMetaHumanCharacter::GetTargetFaceModelCoefficients(const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey) const
{
	if (const TArray<float>* Coefficients = FaceModelCoefficientsAtTargetPoseMap.Find(InTargetMeshKey))
	{
		return *Coefficients;
	}
	return {};
}

FSharedBuffer UMetaHumanCharacter::GetBodyStateData() const
{
	return UE::MetaHuman::GetBufferBulkData(BodyStateBulkData);
}

FSharedBuffer UMetaHumanCharacter::GetBodyTargetPoseStateData(const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey) const
{
	FSharedBuffer OutBuffer;
	if (BodyTargetPoseStateBulkData.Contains(InTargetMeshKey))
	{
		if (const TUniquePtr<UE::Serialization::FEditorBulkData>& BulkData = BodyTargetPoseStateBulkData[InTargetMeshKey])
		{
			OutBuffer = UE::MetaHuman::GetBufferBulkData(*BulkData);
		}
	}
	return OutBuffer;
}

void UMetaHumanCharacter::SetBodyDNABuffer(TConstArrayView<uint8> InBodyDNABuffer)
{
	BodyDNABulkData.UpdatePayload(FSharedBuffer::MakeView(InBodyDNABuffer.GetData(), InBodyDNABuffer.Num()));
}

bool UMetaHumanCharacter::HasBodyDNA() const
{
	return BodyDNABulkData.HasPayloadData();
}

TArray<uint8> UMetaHumanCharacter::GetBodyDNABuffer() const
{
	if (BodyDNABulkData.HasPayloadData())
	{
		TArray<uint8> DNABuffer;
		FSharedBuffer Payload = BodyDNABulkData.GetPayload().Get();
		DNABuffer.Append((const uint8*)Payload.GetData(), Payload.GetSize());
	
		return DNABuffer;
	}

	return {};
}

bool UMetaHumanCharacter::HasSynthesizedTextures() const
{
	return !SynthesizedFaceTexturesInfo.IsEmpty();
}

void UMetaHumanCharacter::SetHasHighResolutionTextures(bool bInHasHighResolutionTextures)
{
	bHasHighResolutionTextures = bInHasHighResolutionTextures;

	if (!bInHasHighResolutionTextures)
	{
		// Remove the animated maps texture infos since they are not valid anymore
		constexpr TStaticArray<EFaceTextureType, 6> AnimatedMapTypes =
		{
			EFaceTextureType::Basecolor_Animated_CM1,
			EFaceTextureType::Basecolor_Animated_CM2,
			EFaceTextureType::Basecolor_Animated_CM3,

			EFaceTextureType::Normal_Animated_WM1,
			EFaceTextureType::Normal_Animated_WM2,
			EFaceTextureType::Normal_Animated_WM3,
		};

		for (EFaceTextureType AnimatedMap : AnimatedMapTypes)
		{
			SynthesizedFaceTexturesInfo.Remove(AnimatedMap);
		}
	}
}

bool UMetaHumanCharacter::HasHighResolutionTextures() const
{
	return bHasHighResolutionTextures;
}

void UMetaHumanCharacter::StoreSynthesizedFaceTexture(EFaceTextureType InTextureType, const FImage& InTextureData)
{
	FMetaHumanCharacterTextureInfo& TextureInfo = SynthesizedFaceTexturesInfo.FindOrAdd(InTextureType);
	TextureInfo.Init(InTextureData);

	UE::Serialization::FEditorBulkData& BulkData = SynthesizedFaceTexturesData[InTextureType];
	
	if (UE::MetaHuman::CVarMHCUseTextureCompression.GetValueOnAnyThread())
	{
		// Compressing the images using png
		// Note that the asset data seem to be compressed when serialized into disk but not when loaded
		// This allows for using compressed data even when the MHC asset is loaded in memory
		TArray64<uint8> CompressedData;
		if (FImageUtils::CompressImage(CompressedData, TEXT("png"), InTextureData))
		{
			FSharedBuffer CompressedPayLoad = FSharedBuffer::MakeView(CompressedData.GetData(), CompressedData.NumBytes());
			BulkData.UpdatePayload(CompressedPayLoad, this);
		}
		else
		{
			FMemoryView TextureDataView(InTextureData.RawData.GetData(), InTextureData.GetImageSizeBytes());
			BulkData.UpdatePayload(FSharedBuffer::MakeView(TextureDataView), this);
		}
	}
	else
	{
		FMemoryView Data(InTextureData.RawData.GetData(), InTextureData.GetImageSizeBytes());
		UE::MetaHuman::CompressAndUpdateBulkData(BulkData, Data, this);
	}

	MarkPackageDirty();
}

FInt32Point UMetaHumanCharacter::GetSynthesizedFaceTexturesResolution(EFaceTextureType InFaceTextureType) const
{
	if (const FMetaHumanCharacterTextureInfo* Info = SynthesizedFaceTexturesInfo.Find(InFaceTextureType))
	{
		return FInt32Point(Info->SizeX, Info->SizeY);
	}
	return FInt32Point(0, 0);
}

bool UMetaHumanCharacter::NeedsToDownloadTextureSources() const
{
	const FInt32Point FaceAlbedoResolution = GetSynthesizedFaceTexturesResolution(EFaceTextureType::Basecolor);
	const FInt32Point FaceNormalResolution = GetSynthesizedFaceTexturesResolution(EFaceTextureType::Normal);
	const FInt32Point FaceCavityResolution = GetSynthesizedFaceTexturesResolution(EFaceTextureType::Cavity);
	const FInt32Point FaceAnimMapsResolutions = GetSynthesizedFaceTexturesResolution(EFaceTextureType::Basecolor_Animated_CM1);

	const FInt32Point BodyAlbedoResolution = GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Basecolor);
	const FInt32Point BodyNormalResolution = GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Normal);
	const FInt32Point BodyCavityResolution = GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Cavity);
	const FInt32Point BodyMasksResolution = GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Underwear_Mask);

	const TStaticArray<int32, 8> CurrentResolutionsList =
	{
		FaceAlbedoResolution.X,
		FaceNormalResolution.X,
		FaceCavityResolution.X,
		FaceAnimMapsResolutions.X,
		BodyAlbedoResolution.X,
		BodyNormalResolution.X,
		BodyCavityResolution.X,
		BodyMasksResolution.X
	};

	const FMetaHumanCharacterTextureSourceResolutions& DesiredResolutions = SkinSettings.DesiredTextureSourcesResolutions;

	const TStaticArray<int32, 8> DesiredResolutionsList =
	{
		(int32) DesiredResolutions.FaceAlbedo,
		(int32) DesiredResolutions.FaceNormal,
		(int32) DesiredResolutions.FaceCavity,
		(int32) DesiredResolutions.FaceAnimatedMaps,
		(int32) DesiredResolutions.BodyAlbedo,
		(int32) DesiredResolutions.BodyNormal,
		(int32) DesiredResolutions.BodyCavity,
		(int32) DesiredResolutions.BodyMasks,
	};

	const bool bIsUpToDate = Algo::Compare(CurrentResolutionsList, DesiredResolutionsList);
	return !bIsUpToDate;
}

TMap<EFaceTextureType, TObjectPtr<UTexture2D>> UMetaHumanCharacter::GetValidFaceTextures() const
{
	TMap<EFaceTextureType, TObjectPtr<UTexture2D>> ValidFaceTextures;

	for (const TPair<EFaceTextureType, FMetaHumanCharacterTextureInfo>& FaceTextureInfoPair : SynthesizedFaceTexturesInfo)
	{
		EFaceTextureType TextureType = FaceTextureInfoPair.Key;

		if (const TObjectPtr<UTexture2D>* FoundTexture = SynthesizedFaceTextures.Find(TextureType))
		{
			ValidFaceTextures.Add(TextureType, *FoundTexture);
		}
	}

	return ValidFaceTextures;
}

void UMetaHumanCharacter::StoreHighResBodyTexture(EBodyTextureType InTextureType, const FImage& InTextureData)
{
	FMetaHumanCharacterTextureInfo& TextureInfo = HighResBodyTexturesInfo.FindOrAdd(InTextureType);
	TextureInfo.Init(InTextureData);

	UE::Serialization::FEditorBulkData& BulkData = HighResBodyTexturesData[InTextureType];

	if (UE::MetaHuman::CVarMHCUseTextureCompression.GetValueOnAnyThread())
	{
		TArray64<uint8> CompressedData;
		if (FImageUtils::CompressImage(CompressedData, TEXT("png"), InTextureData))
		{
			FSharedBuffer CompressedPayLoad = FSharedBuffer::MakeView(CompressedData.GetData(), CompressedData.NumBytes());
			BulkData.UpdatePayload(CompressedPayLoad, this);
		}
		else
		{
			FMemoryView TextureDataView(InTextureData.RawData.GetData(), InTextureData.GetImageSizeBytes());
			BulkData.UpdatePayload(FSharedBuffer::MakeView(TextureDataView), this);
		}
	}
	else
	{
		FMemoryView Data(InTextureData.RawData.GetData(), InTextureData.GetImageSizeBytes());
		UE::MetaHuman::CompressAndUpdateBulkData(BulkData, Data, this);
	}

	MarkPackageDirty();
}

void UMetaHumanCharacter::ResetUnreferencedHighResTextureData()
{
	for (TPair<EFaceTextureType, UE::Serialization::FEditorBulkData>& Data : SynthesizedFaceTexturesData)
	{
		if (!SynthesizedFaceTexturesInfo.Contains(Data.Key))
		{
			Data.Value.Reset();
		}
	}

	for (TPair<EBodyTextureType, UE::Serialization::FEditorBulkData>& Data : HighResBodyTexturesData)
	{
		if (!HighResBodyTexturesInfo.Contains(Data.Key))
		{
			Data.Value.Reset();
		}
	}
}

void UMetaHumanCharacter::RemoveAllTextures()
{
	SynthesizedFaceTexturesInfo.Empty();
	HighResBodyTexturesInfo.Empty();
	
	for (TPair<EFaceTextureType, UE::Serialization::FEditorBulkData>& Data : SynthesizedFaceTexturesData)
	{
		Data.Value.Reset();
	}

	for (TPair<EBodyTextureType, UE::Serialization::FEditorBulkData>& Data : HighResBodyTexturesData)
	{
		Data.Value.Reset();
	}

	SetHasHighResolutionTextures(false);
}

FInt32Point UMetaHumanCharacter::GetSynthesizedBodyTexturesResolution(EBodyTextureType InBodyTextureType) const
{
	if (const TObjectPtr<class UTexture2D>* Texture = BodyTextures.Find(InBodyTextureType))
	{
		return FInt32Point(Texture->Get()->GetSizeX(), Texture->Get()->GetSizeY());
	}
	return FInt32Point(0, 0);
}

TFuture<FSharedBuffer> UMetaHumanCharacter::GetSynthesizedFaceTextureDataAsync(EFaceTextureType InTextureType) const
{
	check(SynthesizedFaceTexturesData.Contains(InTextureType));

	TSharedRef<TPromise<FSharedBuffer>> Promise = MakeShared<TPromise<FSharedBuffer>>();

	// Add a continuation to the bulk data async load to decompress the loaded buffer if needed
	SynthesizedFaceTexturesData[InTextureType].GetPayload().Next(
		[Promise](FSharedBuffer PayloadData)
		{
			FSharedBuffer FinalBuffer = PayloadData;

			// Check if the image was compressed when stored, if not just return the loaded buffer
			FImage DecompressedImage;
			if (FImageUtils::DecompressImage(PayloadData.GetData(), PayloadData.GetSize(), DecompressedImage))
			{
				FMemoryView Data(DecompressedImage.RawData.GetData(), DecompressedImage.GetImageSizeBytes());
				FinalBuffer = FSharedBuffer::Clone(Data);
			}

			Promise.Get().SetValue(FinalBuffer);
		});

	return Promise.Get().GetFuture();
}

TFuture<FSharedBuffer> UMetaHumanCharacter::GetHighResBodyTextureDataAsync(EBodyTextureType InTextureType) const
{
	check(HighResBodyTexturesData.Contains(InTextureType));

	TSharedRef<TPromise<FSharedBuffer>> Promise = MakeShared<TPromise<FSharedBuffer>>();

	// Add a continuation to the bulk data async load to decompress the loaded buffer if needed
	HighResBodyTexturesData[InTextureType].GetPayload().Next(
		[Promise](FSharedBuffer PayloadData)
		{
			FSharedBuffer FinalBuffer = PayloadData;

			// Check if the image was compressed when stored, if not just return the loaded buffer
			FImage DecompressedImage;
			if (FImageUtils::DecompressImage(PayloadData.GetData(), PayloadData.GetSize(), DecompressedImage))
			{
				FMemoryView Data(DecompressedImage.RawData.GetData(), DecompressedImage.GetImageSizeBytes());
				FinalBuffer = FSharedBuffer::Clone(Data);
			}

			Promise.Get().SetValue(FinalBuffer);
		});

	return Promise.Get().GetFuture();
}

TObjectPtr<UMetaHumanCollection> UMetaHumanCharacter::GetMutableInternalCollection()
{
	return InternalCollection;
}

const TObjectPtr<UMetaHumanCollection> UMetaHumanCharacter::GetInternalCollection() const
{
	return InternalCollection;
}

FMetaHumanPaletteItemKey UMetaHumanCharacter::GetInternalCollectionKey() const
{
	return InternalCollectionKey;
}

#if WITH_EDITOR
void UMetaHumanCharacter::ConfigureCollection()
{
	InternalCollectionKey.Reset();

	// Ensure the Character slot has this Character as the only item in it

	if (HasAnyFlags(RF_ClassDefaultObject | RF_NeedInitialization))
	{
		// No action needed on the CDO or when the object needs initialization.
		// This function will be called again during PostLoad, when palette
		// is properly initialized
		return;
	}

	// All instances apart from the CDO should have a palette
	check(InternalCollection);

	if (!InternalCollection->GetPipeline())
	{
		return;
	}

	int32 NumCharacters = 0;
	TOptional<FMetaHumanPaletteItemKey> CharacterItemKey;
	for (const FMetaHumanCharacterPaletteItem& Item : InternalCollection->GetItems())
	{
		if (Item.SlotName == UE::MetaHuman::CharacterPipelineSlots::Character)
		{
			NumCharacters++;

			if (NumCharacters == 1
				&& Item.WardrobeItem
				&& Item.WardrobeItem->PrincipalAsset == this)
			{
				CharacterItemKey = Item.GetItemKey();
				InternalCollection->GetMutableDefaultInstance()->SetSingleSlotSelection(UE::MetaHuman::CharacterPipelineSlots::Character, CharacterItemKey.GetValue());
			}
		}
	}

	if (NumCharacters == 1
		&& CharacterItemKey.IsSet())
	{
		// The palette contains just one character and it's the right one
		InternalCollectionKey = CharacterItemKey.GetValue();
		return;
	}

	// The palette is not set up for this character, so clear any existing characters and set it 
	// up correctly.

	InternalCollection->RemoveAllItemsForSlot(UE::MetaHuman::CharacterPipelineSlots::Character);

	if (!InternalCollection->GetEditorPipeline()->IsPrincipalAssetClassCompatibleWithSlot(UE::MetaHuman::CharacterPipelineSlots::Character, GetClass()))
	{
		UE_LOGFMT(LogMetaHumanCharacter, Error, "The Character Pipeline assigned to {Character} doesn't have a compatible Character slot", GetPathName());
		return;
	}

	verify(InternalCollection->TryAddItemFromPrincipalAsset(
		UE::MetaHuman::CharacterPipelineSlots::Character,
		this,
		InternalCollectionKey));

	InternalCollection->GetMutableDefaultInstance()->SetSingleSlotSelection(UE::MetaHuman::CharacterPipelineSlots::Character, InternalCollectionKey);
}

void UMetaHumanCharacter::NotifyRiggingStateChanged() const
{
	OnRiggingStateChanged.Broadcast();
}

FName UMetaHumanCharacter::GetThumbnailPathInPackage(const FString& InCharacterAssetPath, EMetaHumanCharacterThumbnailCameraPosition InThumbnailPosition)
{
	static const TMap<EMetaHumanCharacterThumbnailCameraPosition, FString> AuxSubobjectNames =
	{
		{ EMetaHumanCharacterThumbnailCameraPosition::Character_Body, UE::MetaHuman::ThumbnailObjectName::CharacterBody.GetData() },
		{ EMetaHumanCharacterThumbnailCameraPosition::Face, UE::MetaHuman::ThumbnailObjectName::Face.GetData() },
		{ EMetaHumanCharacterThumbnailCameraPosition::Body, UE::MetaHuman::ThumbnailObjectName::Body.GetData() },
	};

	if (const FString* SubobjectName = AuxSubobjectNames.Find(InThumbnailPosition))
	{
		return *FString::Format(TEXT("{0} {1}{2}{3}"), { UMetaHumanCharacterThumbnailAux::StaticClass()->GetName(), InCharacterAssetPath, SUBOBJECT_DELIMITER, *SubobjectName });
	}
	else
	{
		return *FString::Format(TEXT("{0} {1}"), { UMetaHumanCharacter::StaticClass()->GetName(), InCharacterAssetPath });
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
