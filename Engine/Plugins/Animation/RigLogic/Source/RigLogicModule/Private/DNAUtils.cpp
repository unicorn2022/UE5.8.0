// Copyright Epic Games, Inc. All Rights Reserved.

#include "DNAUtils.h"

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "HAL/LowLevelMemTracker.h"
#include "DNACommon.h"
#include "DNAAsset.h"
#include "DNA.h"
#include "DNAReaderAdapter.h"
#include "FMemoryResource.h"
#include "LegacyDNAAssetUtils.h"
#include "LegacyDNAReaderAdapter.h"
#include "RigLogicMemoryStream.h"

#include "riglogic/RigLogic.h"
#include "tdm/TDM.h"

dna::DataLayer CalculateDNADataLayerBitmask(EDNADataLayer Layer)
{
	dna::DataLayer mask = static_cast<dna::DataLayer>(Layer);
	if ((Layer & EDNADataLayer::RBFBehavior) == EDNADataLayer::RBFBehavior) {
		mask = mask | dna::DataLayer::JointBehaviorMetadata;
		mask = mask | dna::DataLayer::TwistSwingBehavior;
	}
	return mask;
}

// Returns a copy of InConfig with all per-platform fields resolved to scalar
// values for the running host platform. The returned config is what the reader
// is actually being built with, and is what we cache on the reader so that
// later cook-time comparisons can ask "did the host build this differently than
// the cook target wants?" without going through TPerPlatformProperty::GetValue()
// (which reads UObject::OnGetPreviewPlatform -- an FNotThreadSafeDelegate that
// the cook server may rebind on another thread during InitializeSession).
FDNAConfig ResolveDNAConfigForHost(const FDNAConfig& InConfig)
{
	FDNAConfig Out = InConfig;
#if WITH_EDITOR
	const FName HostPlatform(FPlatformProperties::IniPlatformName());
	Out.MaxLODPerPlatform = FPerPlatformInt(InConfig.MaxLODPerPlatform.GetValueForPlatform(HostPlatform));
	Out.MinLODPerPlatform = FPerPlatformInt(InConfig.MinLODPerPlatform.GetValueForPlatform(HostPlatform));
#else
	// Non-editor builds have no PerPlatform map; Default already holds the cooked value.
	Out.MaxLODPerPlatform = FPerPlatformInt(InConfig.MaxLODPerPlatform.GetDefault());
	Out.MinLODPerPlatform = FPerPlatformInt(InConfig.MinLODPerPlatform.GetDefault());
#endif
	return Out;
}

// Note: callers should pass an already-resolved FDNAConfig (see ResolveDNAConfigForHost).
// We deliberately use GetDefault() here -- not GetValue() -- to avoid touching the
// racy OnGetPreviewPlatform delegate.
dna::Configuration GetDNAConfiguration(const FDNAConfig& DNAConfig)
{
	dna::Configuration OutConfig;
	OutConfig.coordinateSystemTransformPolicy = static_cast<dna::CoordinateSystemTransformPolicy>(DNAConfig.CoordinateSystemTransformPolicy);
	OutConfig.coordinateSystem.x = static_cast<dna::Direction>(DNAConfig.CoordinateSystem.XAxis);
	OutConfig.coordinateSystem.y = static_cast<dna::Direction>(DNAConfig.CoordinateSystem.YAxis);
	OutConfig.coordinateSystem.z = static_cast<dna::Direction>(DNAConfig.CoordinateSystem.ZAxis);
	OutConfig.layer = CalculateDNADataLayerBitmask(static_cast<EDNADataLayer>(DNAConfig.Layers));
	if (!DNAConfig.ExactLODs.IsEmpty())
	{
		DNAConfig.InternalExactLODs.Empty();
		DNAConfig.InternalExactLODs.Reserve(DNAConfig.ExactLODs.Num());
		Algo::Transform(DNAConfig.ExactLODs, DNAConfig.InternalExactLODs, [](uint8 V) { return uint16(V); });
		OutConfig.lods = dna::ConstArrayView<uint16>(DNAConfig.InternalExactLODs.GetData(), DNAConfig.InternalExactLODs.Num());
	}
	else
	{
		OutConfig.maxLOD = static_cast<std::uint16_t>(DNAConfig.MaxLODPerPlatform.GetDefault());
		OutConfig.minLOD = static_cast<std::uint16_t>(DNAConfig.MinLODPerPlatform.GetDefault());
	}
	const auto ConvertRotationDir = [](ERotationDirection RotationDir)
	{
		assert(RotationDir != ERotationDirection::None);
		return RotationDir == ERotationDirection::Negative ? dna::RotationDirection::negative : dna::RotationDirection::positive;
	};
	OutConfig.rotationSequence = static_cast<dna::RotationSequence>(DNAConfig.RotationSequence);
	OutConfig.rotationSign = {ConvertRotationDir(DNAConfig.RotationSign.XAxis), ConvertRotationDir(DNAConfig.RotationSign.YAxis), ConvertRotationDir(DNAConfig.RotationSign.ZAxis)};
	OutConfig.faceWindingOrder = static_cast<dna::FaceWindingOrder>(DNAConfig.FaceWindingOrder);
	return OutConfig;
}

template<typename ReaderWrapper>
static TSharedPtr<IDNAReader> LoadDNAFromStreamImpl(dna::BoundedIOStream* Stream, const FDNAConfig& DNAConfig)
{
	// Resolve per-platform fields once for the host. This is the config the reader is
	// actually being built with; we pass the same resolved config to GetDNAConfiguration
	// (which now reads .Default rather than .GetValue()) and cache it on the reader
	// for later host-vs-cook-target comparisons during cook save.
	const FDNAConfig ResolvedConfig = ResolveDNAConfigForHost(DNAConfig);

	auto DNAStreamReader = dna::makeScoped<dna::BinaryStreamReader>(Stream, GetDNAConfiguration(ResolvedConfig), FMemoryResource::Instance());
	DNAStreamReader->read();
	if (!dna::Status::isOk())
	{
		UE_LOGF(LogDNAReader, Error, "%ls", ANSI_TO_TCHAR(dna::Status::get().message));
		return nullptr;
	}

	// Handle incorrect Z-Up space Fortnite DNAs. This code will be removed once we are sure all such incorrect DNAs are phased out.
	if (IsLegacyDNAAsset(DNAStreamReader.get()))
	{
		auto MigratedDNAReader = MigrateLegacyDNAAsset(Stream, GetDNAConfiguration(ResolvedConfig));
		if (MigratedDNAReader)
		{
			DNAStreamReader = MoveTemp(MigratedDNAReader);
		}
	}

	return MakeShared<ReaderWrapper>(DNAStreamReader.release(), ResolvedConfig);
}

TSharedPtr<IDNAReader> LoadDNAFromStream(dna::BoundedIOStream* Stream, const FDNAConfig& DNAConfig)
{
	return LoadDNAFromStreamImpl<FDNAReader<dna::BinaryStreamReader>>(Stream, DNAConfig);
}

TSharedPtr<IDNAReader> ReadDNAFromStream(dna::BoundedIOStream* Stream, const FDNAConfig& DNAConfig)
{
	return LoadDNAFromStreamImpl<FLegacyDNAReader<dna::BinaryStreamReader>>(Stream, DNAConfig);
}

template<typename ReaderWrapper>
static TSharedPtr<IDNAReader> LoadDNAFromFileImpl(const FString& Path, const FDNAConfig& DNAConfig)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	auto DNAFileStream = dna::makeScoped<dna::MemoryMappedFileStream>(TCHAR_TO_UTF8(*Path), dna::MemoryMappedFileStream::AccessMode::Read, FMemoryResource::Instance());
	return LoadDNAFromStreamImpl<ReaderWrapper>(DNAFileStream.get(), DNAConfig);
}

TSharedPtr<IDNAReader> LoadDNAFromFile(const FString& Path, const FDNAConfig& DNAConfig)
{
	return LoadDNAFromFileImpl<FDNAReader<dna::BinaryStreamReader>>(Path, DNAConfig);
}

template<typename ReaderWrapper>
static TSharedPtr<IDNAReader> LoadDNAFromBufferImpl(TArray<uint8>* DNABuffer, const FDNAConfig& DNAConfig)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	FRigLogicMemoryStream DNAMemoryStream(DNABuffer);
	return LoadDNAFromStreamImpl<ReaderWrapper>(&DNAMemoryStream, DNAConfig);
}

TSharedPtr<IDNAReader> LoadDNAFromBuffer(TArray<uint8>* DNABuffer, const FDNAConfig& DNAConfig)
{
	return LoadDNAFromBufferImpl<FDNAReader<dna::BinaryStreamReader>>(DNABuffer, DNAConfig);
}

// Deprecated
TSharedPtr<IDNAReader> ReadDNAFromFile(const FString& Path, EDNADataLayer Layer, uint16_t MaxLOD)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	FDNAConfig DNAConfig = FDNAConfig::Source();
	DNAConfig.Layers = static_cast<int32>(Layer);
	DNAConfig.MaxLODPerPlatform = FPerPlatformInt(static_cast<int32>(MaxLOD));
	return LoadDNAFromFileImpl<FLegacyDNAReader<dna::BinaryStreamReader>>(Path, DNAConfig);
}

// Deprecated
TSharedPtr<IDNAReader> ReadDNAFromFile(const FString& Path, EDNADataLayer Layer, TArrayView<uint16_t> LODs)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	FDNAConfig DNAConfig = FDNAConfig::Source();
	DNAConfig.Layers = static_cast<int32>(Layer);
	DNAConfig.ExactLODs = LODs;
	return LoadDNAFromFileImpl<FLegacyDNAReader<dna::BinaryStreamReader>>(Path, DNAConfig);
}

// Deprecated
TSharedPtr<IDNAReader> ReadDNAFromBuffer(TArray<uint8>* DNABuffer, EDNADataLayer Layer, uint16_t MaxLOD)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	FDNAConfig DNAConfig = FDNAConfig::Source();
	DNAConfig.Layers = static_cast<int32>(Layer);
	DNAConfig.MaxLODPerPlatform = FPerPlatformInt(static_cast<int32>(MaxLOD));
	return LoadDNAFromBufferImpl<FLegacyDNAReader<dna::BinaryStreamReader>>(DNABuffer, DNAConfig);
}

// Deprecated
TSharedPtr<IDNAReader> ReadDNAFromBuffer(TArray<uint8>* DNABuffer, EDNADataLayer Layer, TArrayView<uint16_t> LODs)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	FDNAConfig DNAConfig = FDNAConfig::Source();
	DNAConfig.Layers = static_cast<int32>(Layer);
	DNAConfig.ExactLODs = LODs;
	return LoadDNAFromBufferImpl<FLegacyDNAReader<dna::BinaryStreamReader>>(DNABuffer, DNAConfig);
}

// Deprecated
TArray<uint8> ReadStreamFromDNA(const IDNAReader* Reader, EDNADataLayer Layer)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	TArray<uint8> DNABuffer;
	SaveDNAToBuffer(Reader, Layer, DNABuffer);
	return DNABuffer;
}

void SaveDNAToStream(const IDNAReader* Reader, EDNADataLayer Layer, TNotNull<dna::BoundedIOStream*> Stream)
{
	auto DNAStreamWriter = dna::makeScoped<dna::BinaryStreamWriter>(Stream, FMemoryResource::Instance());
	if (Reader != nullptr)
	{
		DNAStreamWriter->setFrom(Reader->Unwrap(), CalculateDNADataLayerBitmask(Layer), dna::UnknownLayerPolicy::Preserve, FMemoryResource::Instance());
	}
	DNAStreamWriter->write();
}

void SaveDNAToBuffer(const IDNAReader* Reader, EDNADataLayer Layer, TArray<uint8>& DNABuffer)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	FRigLogicMemoryStream DNAMemoryStream(&DNABuffer);
	SaveDNAToStream(Reader, Layer, &DNAMemoryStream);
}

void SaveDNAToFile(const IDNAReader* Reader, EDNADataLayer Layer, const FString& Path)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	auto DNAFileStream = dna::makeScoped<dna::FileStream>(TCHAR_TO_UTF8(*Path), dna::FileStream::AccessMode::Write, dna::FileStream::OpenMode::Binary, FMemoryResource::Instance());
	SaveDNAToStream(Reader, Layer, DNAFileStream.get());
}

void WriteDNAToFile(const IDNAReader* Reader, EDNADataLayer Layer, const FString& Path)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	SaveDNAToFile(Reader, Layer, Path);
}

template<typename ReaderWrapper>
static TObjectPtr<class UDNAAsset> LoadDNAAssetFromFileImpl(const FString& InFilePath, UObject* InOuter, EDNADataLayer InLayer, EDNACopyPolicy CopyPolicy, ERigLogicInitPolicy InitPolicy)
{
	UDNAAsset* DNAAsset = nullptr;
	TArray<uint8> DNADataAsBuffer;
	if (FFileHelper::LoadFileToArray(DNADataAsBuffer, *InFilePath))
	{
		FDNAConfig DNAConfig;
		DNAConfig.Layers = static_cast<int32>(InLayer);
		if (TSharedPtr<IDNAReader> DNAReader = LoadDNAFromBufferImpl<ReaderWrapper>(&DNADataAsBuffer, DNAConfig))
		{
			DNAAsset = NewObject<UDNAAsset>(InOuter);
			DNAAsset->SetDNAReader(DNAReader, CopyPolicy, InitPolicy);
		}
	}

	return DNAAsset;
}

TObjectPtr<class UDNA> ReadDNAAssetFromFile(const FString& InFilePath, UObject* InOuter, EDNADataLayer InLayer)
{
	UDNA* DNAAsset = nullptr;
	if (InOuter == nullptr)
	{
		InOuter = GetTransientPackage();
	}

	TArray<uint8> DNADataAsBuffer;
	if (FFileHelper::LoadFileToArray(DNADataAsBuffer, *InFilePath))
	{
		if (TSharedPtr<IDNAReader> DNAReader = ReadDNAFromBuffer(&DNADataAsBuffer, InLayer))
		{
			DNAAsset = NewObject<UDNA>(InOuter);
			DNAAsset->SetDNAReader(DNAReader, EDNACopyPolicy::Alias, ERigLogicInitPolicy::Defer);
			DNAAsset->RestoreLegacyUEMHCCompatibility();
		}
	}

	return DNAAsset;
}

TObjectPtr<class UDNAAsset> LoadDNAAssetFromFile(const FString& InFilePath, UObject* InOuter, EDNADataLayer InLayer)
{
	return LoadDNAAssetFromFileImpl<FDNAReader<dna::BinaryStreamReader>>(InFilePath, InOuter, InLayer, EDNACopyPolicy::Alias, ERigLogicInitPolicy::Initialize);
}

TObjectPtr<class UDNAAsset> GetDNAAssetFromFile(const FString& InFilePath, UObject* InOuter, EDNADataLayer InLayer)
{
	TObjectPtr<class UDNAAsset> OutAsset = LoadDNAAssetFromFileImpl<FLegacyDNAReader<dna::BinaryStreamReader>>(InFilePath, InOuter, InLayer, EDNACopyPolicy::Alias, ERigLogicInitPolicy::Defer);
	OutAsset->RestoreLegacyUEMHCCompatibility();
	return OutAsset;
}
