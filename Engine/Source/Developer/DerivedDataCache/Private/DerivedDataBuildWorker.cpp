// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildWorker.h"

#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "Compression/CompressedBuffer.h"
#include "Containers/Array.h"
#include "Containers/SharedString.h"
#include "Containers/Utf8String.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataRequestOwner.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMath.h"
#include "Hash/xxhash.h"
#include "Logging/StructuredLog.h"
#include "Misc/Guid.h"
#include "Misc/PathViews.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Templates/AlignmentTemplates.h"
#include "Templates/Function.h"
#include "Templates/Tuple.h"

namespace UE::DerivedData::Private
{

struct FBuildWorkerPackageAttachment
{
	FIoHash RawHash;
	uint32 RawSizeLo = 0;
	uint16 RawSizeHi = 0;
	uint16 CompressedSizeLo = 0;
	uint32 CompressedSizeHi = 0;
	uint64 CompressedOffset = 0;

	inline uint64 GetRawSize() const 
	{
		return (uint64(RawSizeHi) << 32) | RawSizeLo;
	}

	inline void SetRawSize(uint64 Size)
	{
		RawSizeHi = uint16(Size >> 32);
		RawSizeLo = uint32(Size);
	}

	inline uint64 GetCompressedSize() const 
	{
		return (uint64(CompressedSizeHi) << 16) | CompressedSizeLo;
	}

	inline void SetCompressedSize(uint64 Size)
	{
		CompressedSizeHi = uint32(Size >> 16);
		CompressedSizeLo = uint16(Size);
	}
};
static_assert(sizeof(FBuildWorkerPackageAttachment) == 40);

struct FBuildWorkerPackageHeader
{
	constexpr static uint32 ExpectedMagic = 0xb7756277; // <dot>ubw

	/** A magic number to identify a build worker package. Always 0xb7756277. */
	uint32 Magic = ExpectedMagic;
	/** The hash of the header from AttachmentCount to the end of the attachment table. */
	uint32 HeaderHash = 0;
	/** The number of attachments in this package. */
	uint32 AttachmentCount = 0;
	/** The offset of the attachment data relative to the start of this header. */
	uint32 AttachmentOffset = 0;
	/** The total size of the worker object following the header. */
	uint32 WorkerSize = 0;
	/** The hash of the worker object following the header. */
	FIoHash WorkerHash;
	/** The table of attachments in the package. Sorted by RawHash. Size is AttachmentCount. */
	FBuildWorkerPackageAttachment Attachments[0];

	/** Calculate HeaderHash using the low 32 bits from FXxHash64. */
	uint32 CalculateHash() const
	{
		const uint32 HeaderSize = sizeof(*this) + sizeof(*Attachments) * AttachmentCount;
		const uint32 HashOffset = offsetof(FBuildWorkerPackageHeader, AttachmentCount);
		const FMemoryView HeaderView = MakeMemoryView(this, HeaderSize);
		return (uint32)FXxHash64::HashBuffer(HeaderView.RightChop(HashOffset)).Hash;
	}
};

class FBuildWorkerPackageResolver final : public IBuildWorkerResolver
{
public:
	explicit FBuildWorkerPackageResolver(TNotNull<FBuildWorkerPackageHeader*> Header, FStringView PackagePath);
	~FBuildWorkerPackageResolver();

	FBuildWorkerPackageResolver(const FBuildWorkerPackageResolver&) = delete;
	FBuildWorkerPackageResolver& operator=(const FBuildWorkerPackageResolver&) = delete;

	void Resolve(TConstArrayView<FIoHash> RawHashes, IRequestOwner& Owner, FOnBuildWorkerResolved&& OnResolved) final;

	[[nodiscard]] FStringView GetPackagePath() const
	{
		return PackagePath;
	}

private:
	TNotNull<FBuildWorkerPackageHeader*> Header;
	FSharedString PackagePath;
};

FBuildWorkerPackageResolver::FBuildWorkerPackageResolver(TNotNull<FBuildWorkerPackageHeader*> InHeader, FStringView InPackagePath)
	: Header(InHeader)
	, PackagePath(InPackagePath)
{
}

FBuildWorkerPackageResolver::~FBuildWorkerPackageResolver()
{
	static_assert(std::is_trivially_destructible_v<FBuildWorkerPackageHeader>);
	FMemory::Free(Header);
}

void FBuildWorkerPackageResolver::Resolve(TConstArrayView<FIoHash> RawHashes, IRequestOwner& Owner, FOnBuildWorkerResolved&& OnResolved)
{
	TConstArrayView<FBuildWorkerPackageAttachment> Attachments(Header->Attachments, (int32)Header->AttachmentCount);
	TArray<FCompressedBuffer> ResolvedData;

	if (!RawHashes.IsEmpty())
	{
		if (TUniquePtr<FArchive> PackageAr{IFileManager::Get().CreateFileReader(*PackagePath, FILEREAD_Silent)})
		{
			const uint64 PackageSize = (uint64)FPlatformMath::Max(0, PackageAr->TotalSize());
			ResolvedData.Reserve(RawHashes.Num());
			for (const FIoHash& RawHash : RawHashes)
			{
				const int32 AttachmentIndex = Algo::BinarySearchBy(Attachments, RawHash, &FBuildWorkerPackageAttachment::RawHash);
				if (AttachmentIndex >= 0)
				{
					const FBuildWorkerPackageAttachment& Attachment = Attachments[AttachmentIndex];
					const uint64 CompressedOffset = Attachment.CompressedOffset + Header->AttachmentOffset;
					const uint64 CompressedSize = Attachment.GetCompressedSize();
					const uint64 CompressedEndOffset = CompressedOffset + CompressedSize;
					if (CompressedOffset <= CompressedEndOffset && CompressedEndOffset <= PackageSize)
					{
						FUniqueBuffer CompressedData = FUniqueBuffer::Alloc(CompressedSize);
						PackageAr->Seek((int64)CompressedOffset);
						PackageAr->Serialize(CompressedData.GetData(), (int64)CompressedSize);
						FCompressedBuffer Data = FCompressedBuffer::FromCompressed(CompressedData.MoveToShared());
						if (Data.GetRawHash() == RawHash)
						{
							ResolvedData.Add(MoveTemp(Data));
						}
					}
				}
			}
		}
	}

	const EStatus Status = ResolvedData.Num() == RawHashes.Num() ? EStatus::Ok : EStatus::Error;
	OnResolved({ResolvedData, Status});
}

} // UE::DerivedData::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData
{

FUtf8StringView FBuildWorker::GetName() const
{
	return Object.FindView(ANSITEXTVIEW("Name")).AsString();
}

FUtf8StringView FBuildWorker::GetPath() const
{
	return Object.FindView(ANSITEXTVIEW("Path")).AsString();
}

FUtf8StringView FBuildWorker::GetHostPlatform() const
{
	return Object.FindView(ANSITEXTVIEW("HostPlatform")).AsString();
}

FGuid FBuildWorker::GetBuildSystemVersion() const
{
	return Object.FindView(ANSITEXTVIEW("BuildSystemVersion")).AsUuid();
}

void FBuildWorker::IterateFunctions(TFunctionRef<void (FUtf8StringView Name, const FGuid& Version)> Visitor) const
{
	for (FCbFieldView It : Object.FindView(ANSITEXTVIEW("Functions")))
	{
		Visitor(It[ANSITEXTVIEW("Name")].AsString(), It[ANSITEXTVIEW("Version")].AsUuid());
	}
}

void FBuildWorker::IterateDirectories(TFunctionRef<void (FUtf8StringView Path)> Visitor) const
{
	for (FCbFieldView It : Object.FindView(ANSITEXTVIEW("Directories")))
	{
		Visitor(It[ANSITEXTVIEW("Path")].AsString());
	}
}

void FBuildWorker::IterateFiles(TFunctionRef<void (FUtf8StringView Path, const FIoHash& RawHash, uint64 RawSize)> Visitor) const
{
	for (FCbFieldView It : Object.FindView(ANSITEXTVIEW("Files")))
	{
		Visitor(It[ANSITEXTVIEW("Path")].AsString(), It[ANSITEXTVIEW("RawHash")].AsHash(), It[ANSITEXTVIEW("RawSize")].AsUInt64());
	}
}

void FBuildWorker::IterateExecutables(TFunctionRef<void (FUtf8StringView Path, const FIoHash& RawHash, uint64 RawSize)> Visitor) const
{
	for (FCbFieldView It : Object.FindView(ANSITEXTVIEW("Executables")))
	{
		Visitor(It[ANSITEXTVIEW("Path")].AsString(), It[ANSITEXTVIEW("RawHash")].AsHash(), It[ANSITEXTVIEW("RawSize")].AsUInt64());
	}
}

void FBuildWorker::IterateEnvironment(TFunctionRef<void (FUtf8StringView Name, FUtf8StringView Value)> Visitor) const
{
	for (FCbFieldView It : Object.FindView(ANSITEXTVIEW("Environment")))
	{
		Visitor(It[ANSITEXTVIEW("Name")].AsString(), It[ANSITEXTVIEW("Value")].AsString());
	}
}

void FBuildWorker::Resolve(TConstArrayView<FIoHash> RawHashes, IRequestOwner& Owner, FOnBuildWorkerResolved&& OnResolved) const
{
	Resolver->Resolve(RawHashes, Owner, MoveTemp(OnResolved));
}

bool FBuildWorker::Extract(FStringView OutputPathView) const
{
	std::atomic<bool> bOk = true;
	IFileManager& FileManager = IFileManager::Get();

	// Extract the worker to a temporary directory.
	TStringBuilder<256> OutputPath(InPlace, OutputPathView);
	TStringBuilder<256> ScratchPath(InPlace, OutputPath, '.', FGuid::NewGuid());

	// Create the directories for the build worker.
	IterateDirectories([this, &FileManager, &ScratchPath, &bOk](FUtf8StringView Path)
	{
		TStringBuilder<256> DirectoryPath;
		FPathViews::Append(DirectoryPath, ScratchPath, Path);
		if (!FileManager.MakeDirectory(*DirectoryPath, /*bTree*/ true))
		{
			UE_LOGFMT(LogDerivedDataBuild, Display,
				"Failed to create directory '{Path}' for build worker {WorkerName}.", DirectoryPath, GetName());
			bOk.store(false, std::memory_order_relaxed);
		}
	});

	// Gather the files that need to be manifested.
	TArray<FIoHash> WorkerFileHashes;
	TArray<TTuple<FUtf8StringView, bool>> WorkerFileMeta;

	IterateExecutables([&WorkerFileHashes, &WorkerFileMeta](FUtf8StringView Path, const FIoHash& RawHash, uint64 RawSize)
	{
		WorkerFileHashes.Emplace(RawHash);
		WorkerFileMeta.Emplace(Path, true);
	});

	IterateFiles([&WorkerFileHashes, &WorkerFileMeta](FUtf8StringView Path, const FIoHash& RawHash, uint64 RawSize)
	{
		WorkerFileHashes.Emplace(RawHash);
		WorkerFileMeta.Emplace(Path, false);
	});

	// Create the files for the build worker.
	FRequestOwner BlockingOwner(EPriority::Blocking);
	Resolve(WorkerFileHashes, BlockingOwner, [this, &FileManager, &ScratchPath, &WorkerFileMeta, &bOk](FBuildWorkerResolvedParams&& Params)
	{
		if (Params.Status != EStatus::Ok)
		{
			bOk.store(false, std::memory_order_relaxed);
		}
		uint32 MetaIndex = 0;
		for (const FCompressedBuffer& CompressedData : Params.Files)
		{
			TStringBuilder<256> FilePath;
			FPathViews::Append(FilePath, ScratchPath, WorkerFileMeta[MetaIndex].Key);
			if (TUniquePtr<FArchive> Ar{FileManager.CreateFileWriter(*FilePath, FILEWRITE_Silent)})
			{
				if (FCompositeBuffer RawData = CompressedData.DecompressToComposite())
				{
					for (auto& Segment : RawData.GetSegments())
					{
						Ar->Serialize((void*)Segment.GetData(), Segment.GetSize());
					}
				}
				else
				{
					UE_LOGFMT(LogDerivedDataBuild, Display,
						"Failed to decompress data for '{Path}' for build worker {WorkerName}.", FilePath, GetName());
					bOk.store(false, std::memory_order_relaxed);
				}
			}
			else
			{
				UE_LOGFMT(LogDerivedDataBuild, Display,
					"Failed to open '{Path}' for writing for build worker {WorkerName}.", FilePath, GetName());
				bOk.store(false, std::memory_order_relaxed);
			}

			++MetaIndex;
		}
	});
	BlockingOwner.Wait();

	// Move the temporary directory to its final location or delete it if there was a race.
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (bOk.load(std::memory_order_relaxed) && !PlatformFile.DirectoryExists(*OutputPath))
	{
		PlatformFile.MoveFile(*OutputPath, *ScratchPath);
	}
	PlatformFile.DeleteDirectoryRecursively(*ScratchPath);
	const bool bExists = PlatformFile.DirectoryExists(*OutputPath);
	UE_CLOGFMT(!bExists, LogDerivedDataBuild, Display,
		"Failed to rename '{TempPath}' to '{Path}' for build worker {WorkerName}.", ScratchPath, OutputPath, GetName());

	return bExists;
}

FStringView FBuildWorker::GetPackagePath() const
{
	return Resolver->GetPackagePath();
}

TOptional<FBuildWorker> FBuildWorker::Load(FStringView PackagePath)
{
	using namespace UE::DerivedData::Private;
	TUniquePtr<FArchive> PackageAr(IFileManager::Get().CreateFileReader(*WriteToString<256>(PackagePath), FILEREAD_Silent));
	if (!PackageAr)
	{
		return {};
	}

	const int64 TotalSize = PackageAr->TotalSize();

	FBuildWorkerPackageHeader HeaderOnly{};
	if (TotalSize < sizeof(HeaderOnly))
	{
		UE_LOGFMT(LogDerivedDataBuild, Warning, "Failed to load build worker package '{Package}' because "
			"the file size {FileSize} is below the minimum header size {HeaderSize}.",
			PackagePath, TotalSize, sizeof(HeaderOnly));
		return {};
	}

	PackageAr->Serialize(&HeaderOnly, sizeof(HeaderOnly));

	if (HeaderOnly.Magic != HeaderOnly.ExpectedMagic)
	{
		UE_LOGFMT(LogDerivedDataBuild, Warning, "Failed to load build worker package '{Package}' because "
			"the magic number in the header is incorrect.", PackagePath);
		return {};
	}

	const int64 HeaderSize = sizeof(HeaderOnly) + sizeof(*HeaderOnly.Attachments) * (int64)HeaderOnly.AttachmentCount;
	if (TotalSize < HeaderSize)
	{
		UE_LOGFMT(LogDerivedDataBuild, Warning, "Failed to load build worker package '{Package}' because "
			"the file size {FileSize} is below the reported header size {HeaderSize}.",
			PackagePath, TotalSize, HeaderSize);
		return {};
	}

	struct FDeleteByFree
	{
		void operator()(void* Ptr) const
		{
			FMemory::Free(Ptr);
		}
	};
	TUniquePtr<FBuildWorkerPackageHeader, FDeleteByFree> Header(new(FMemory::Malloc(HeaderSize, alignof(FBuildWorkerPackageHeader))) FBuildWorkerPackageHeader(HeaderOnly));

	PackageAr->Serialize(Header->Attachments, HeaderSize - sizeof(HeaderOnly));

	if (Header->CalculateHash() != Header->HeaderHash)
	{
		UE_LOGFMT(LogDerivedDataBuild, Warning, "Failed to load build worker package '{Package}' because "
			"the header hash does not match.", PackagePath);
		return {};
	}

	int64 PackageSize = HeaderSize + Header->WorkerSize;
	if (Header->AttachmentCount)
	{
		if (Header->AttachmentOffset < PackageSize)
		{
			UE_LOGFMT(LogDerivedDataBuild, Warning, "Failed to load build worker package '{Package}' because "
				"the attachment offset {AttachmentOffset} is before the end of the header and worker object at {PackageSize}.",
				PackagePath, Header->AttachmentOffset, PackageSize);
			return {};
		}
		const FBuildWorkerPackageAttachment& LastAttachment = Header->Attachments[Header->AttachmentCount - 1];
		PackageSize = Header->AttachmentOffset + LastAttachment.CompressedOffset + (int64)LastAttachment.GetCompressedSize();
	}
	if (TotalSize < PackageSize)
	{
		UE_LOGFMT(LogDerivedDataBuild, Warning, "Failed to load build worker package '{Package}' because "
			"the file size {FileSize} is below the reported package size {PackageSize}.",
			PackagePath, TotalSize, PackageSize);
		return {};
	}

	FUniqueBuffer WorkerObjectData = FUniqueBuffer::Alloc(Header->WorkerSize);
	PackageAr->Serialize(WorkerObjectData.GetData(), Header->WorkerSize);
	if (FIoHash::HashBuffer(WorkerObjectData) != Header->WorkerHash)
	{
		UE_LOGFMT(LogDerivedDataBuild, Warning, "Failed to load build worker package '{Package}' because "
			"the worker hash does not match. Package has been corrupted.", PackagePath);
		return {};
	}

	FCbObject WorkerObject(WorkerObjectData.MoveToShared());
	FBuildWorker Worker;
	Worker.Key = {Header->WorkerHash};
	Worker.Object = MoveTemp(WorkerObject);
	Worker.Resolver = MakeUnique<FBuildWorkerPackageResolver>(Header.Release(), PackagePath);
	return Worker;
}

FBuildWorker FBuildWorker::Load(FCbObject Object, TNotNull<TUniquePtr<IBuildWorkerResolver>> Resolver)
{
	Object.MakeOwned();
	FBuildWorker Worker;
	Worker.Object = MoveTemp(Object);
	Worker.Resolver = MoveTemp(Resolver);
	Worker.Key = {Worker.Object.GetHash()};
	return Worker;
}

} // UE::DerivedData

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData::Private
{

class FBuildWorkerBuilderInternal final : public IBuildWorkerBuilderInternal
{
public:
	void SetName(FUtf8StringView Name) final
	{
		WorkerName = Name;
	}

	void SetPath(FUtf8StringView Path) final
	{
		WorkerPath = Path;
	}

	void SetHostPlatform(FUtf8StringView Name) final
	{
		HostPlatform = Name;
	}

	void SetBuildSystemVersion(const FGuid& Version) final
	{
		BuildSystemVersion = Version;
	}

	void AddDirectory(FUtf8StringView Path) final
	{
		Directories.Emplace(Path);
	}

	void AddFunction(FUtf8StringView Name, const FGuid& Version) final
	{
		Functions.Emplace(Name, Version);
	}

	void AddFile(FUtf8StringView Path, const FCompressedBuffer& Data) final
	{
		Files.Emplace(Path, Data);
	}

	void AddExecutable(FUtf8StringView Path, const FCompressedBuffer& Data) final
	{
		Executables.Emplace(Path, Data);
	}

	void SetEnvironment(FUtf8StringView Name, FUtf8StringView Value) final
	{
		Environment.Emplace(Name, Value);
	}

	FBuildWorker Build(FStringView PackagePath) final;

private:
	FUtf8String WorkerName;
	FUtf8String WorkerPath;
	FUtf8String HostPlatform;
	FGuid BuildSystemVersion;
	TArray<TTuple<FUtf8String, FGuid>> Functions;
	TArray<FUtf8String> Directories;
	TArray<TTuple<FUtf8String, FCompressedBuffer>> Files;
	TArray<TTuple<FUtf8String, FCompressedBuffer>> Executables;
	TArray<TTuple<FUtf8String, FUtf8String>> Environment;
};

FBuildWorker FBuildWorkerBuilderInternal::Build(FStringView PackagePath)
{
	constexpr static uint32 AttachmentAlignment = 64;

	TArray<FCompressedBuffer> Attachments;

	// Create the worker object and gather attachments.
	Algo::Sort(Functions);
	Algo::Sort(Directories);
	Algo::SortBy(Files, &TTuple<FUtf8String, FCompressedBuffer>::Key);
	Algo::SortBy(Executables, &TTuple<FUtf8String, FCompressedBuffer>::Key);
	Algo::Sort(Environment);

	TCbWriter<4096> Writer;
	Writer.BeginObject();

	Writer.AddString(ANSITEXTVIEW("Name"), WorkerName);
	Writer.AddString(ANSITEXTVIEW("Path"), WorkerPath);
	Writer.AddString(ANSITEXTVIEW("HostPlatform"), HostPlatform);
	Writer.AddUuid(ANSITEXTVIEW("BuildSystemVersion"), BuildSystemVersion);

	if (!Functions.IsEmpty())
	{
		Writer.BeginArray(ANSITEXTVIEW("Functions"));
		for (const TTuple<FUtf8String, FGuid>& Function : Functions)
		{
			const FUtf8String& Name = Function.Get<FUtf8String>();
			const FGuid& Version = Function.Get<FGuid>();
			UE_CLOGF(!Version.IsValid(), LogDerivedDataBuild, Error,
				"Version of zero is not allowed in build function with the name %s in build worker '%s'.",
				*WriteToUtf8String<32>(Name), *WorkerName);
			Writer.BeginObject();
			Writer.AddString(ANSITEXTVIEW("Name"), Name);
			Writer.AddUuid(ANSITEXTVIEW("Version"), Version);
			Writer.EndObject();
		}
		Writer.EndArray();
	}

	if (!Directories.IsEmpty())
	{
		Writer.BeginArray(ANSITEXTVIEW("Directories"));
		for (const FUtf8String& Directory : Directories)
		{
			Writer.BeginObject();
			Writer.AddString(ANSITEXTVIEW("Path"), Directory);
			Writer.EndObject();
		}
		Writer.EndArray();
	}

	if (!Files.IsEmpty())
	{
		Writer.BeginArray(ANSITEXTVIEW("Files"));
		for (const TTuple<FUtf8String, FCompressedBuffer>& File : Files)
		{
			const FCompressedBuffer& Attachment = File.Get<FCompressedBuffer>();
			Attachments.Add(Attachment);
			Writer.BeginObject();
			Writer.AddString(ANSITEXTVIEW("Path"), File.Get<FUtf8String>());
			Writer.AddBinaryAttachment(ANSITEXTVIEW("RawHash"), Attachment.GetRawHash());
			Writer.AddInteger(ANSITEXTVIEW("RawSize"), Attachment.GetRawSize());
			Writer.EndObject();
		}
		Writer.EndArray();
	}

	if (!Executables.IsEmpty())
	{
		Writer.BeginArray(ANSITEXTVIEW("Executables"));
		for (const TTuple<FUtf8String, FCompressedBuffer>& Executable : Executables)
		{
			const FCompressedBuffer& Attachment = Executable.Get<FCompressedBuffer>();
			Attachments.Add(Attachment);
			Writer.BeginObject();
			Writer.AddString(ANSITEXTVIEW("Path"), Executable.Get<FUtf8String>());
			Writer.AddBinaryAttachment(ANSITEXTVIEW("RawHash"), Attachment.GetRawHash());
			Writer.AddInteger(ANSITEXTVIEW("RawSize"), Attachment.GetRawSize());
			Writer.EndObject();
		}
		Writer.EndArray();
	}

	if (!Environment.IsEmpty())
	{
		Writer.BeginArray(ANSITEXTVIEW("Environment"));
		for (const TTuple<FUtf8String, FUtf8String>& Var : Environment)
		{
			Writer.BeginObject();
			Writer.AddString(ANSITEXTVIEW("Name"), Var.Get<0>());
			Writer.AddString(ANSITEXTVIEW("Value"), Var.Get<1>());
			Writer.EndObject();
		}
		Writer.EndArray();
	}

	Writer.EndObject();
	FCbObject WorkerObject = Writer.Save().AsObject();

	// Build the worker package header from the sorted attachments.
	Algo::SortBy(Attachments, &FCompressedBuffer::GetRawHash);
	Attachments.SetNum(Algo::UniqueBy(Attachments, &FCompressedBuffer::GetRawHash), EAllowShrinking::No);

	const uint32 HeaderSize = sizeof(FBuildWorkerPackageHeader) + sizeof(FBuildWorkerPackageAttachment) * Attachments.Num();
	FBuildWorkerPackageHeader* Header = new(FMemory::Malloc(HeaderSize, alignof(FBuildWorkerPackageHeader))) FBuildWorkerPackageHeader;
	Header->AttachmentCount = (uint32)Attachments.Num();

	uint64 CompressedOffset = 0;
	for (int32 AttachmentIndex = 0, AttachmentCount = Attachments.Num(); AttachmentIndex < AttachmentCount; ++AttachmentIndex)
	{
		const FCompressedBuffer& Attachment = Attachments[AttachmentIndex];
		const uint64 CompressedSize = Attachment.GetCompressedSize();
		FBuildWorkerPackageAttachment& AttachmentHeader = Header->Attachments[AttachmentIndex];
		AttachmentHeader.RawHash = Attachment.GetRawHash();
		AttachmentHeader.SetRawSize(Attachment.GetRawSize());
		AttachmentHeader.SetCompressedSize(CompressedSize);
		AttachmentHeader.CompressedOffset = CompressedOffset;
		CompressedOffset = Align(CompressedOffset + CompressedSize, AttachmentAlignment);
	}

	// Create the worker and its contained worker object.
	FBuildWorker Worker = FBuildWorker::Load(WorkerObject, MakeUnique<FBuildWorkerPackageResolver>(Header, PackagePath));

	Header->WorkerSize = (uint32)WorkerObject.GetSize();
	Header->WorkerHash = Worker.GetKey().Hash;
	Header->AttachmentOffset = Align(HeaderSize + Header->WorkerSize, AttachmentAlignment);

	Header->HeaderHash = Header->CalculateHash();

	// Save the worker package to disk.
	uint8 AttachmentPadding[AttachmentAlignment]{};
	const auto SerializeAttachmentPadding = [&AttachmentPadding](FArchive& Ar)
	{
		const int64 Offset = Ar.Tell();
		if (const int64 Size = Align(Offset, AttachmentAlignment) - Offset)
		{
			Ar.Serialize(AttachmentPadding, Size);
		}
	};
	TUniquePtr<FArchive> PackageAr(IFileManager::Get().CreateFileWriter(*WriteToString<256>(PackagePath), FILEWRITE_NoFail));
	PackageAr->Serialize(Header, HeaderSize);
	WorkerObject.CopyTo(*PackageAr);
	for (int32 AttachmentIndex = 0, AttachmentCount = Attachments.Num(); AttachmentIndex < AttachmentCount; ++AttachmentIndex)
	{
		const FCompressedBuffer& Attachment = Attachments[AttachmentIndex];
		const FBuildWorkerPackageAttachment& AttachmentHeader = Header->Attachments[AttachmentIndex];
		SerializeAttachmentPadding(*PackageAr);
		check((uint64)PackageAr->Tell() == Header->AttachmentOffset + AttachmentHeader.CompressedOffset);
		Attachment.Save(*PackageAr);
	}

	return Worker;
}

} // UE::DerivedData::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData
{

FBuildWorkerBuilder::FBuildWorkerBuilder()
	: Builder(MakeUnique<Private::FBuildWorkerBuilderInternal>())
{
}

} // UE::DerivedData
