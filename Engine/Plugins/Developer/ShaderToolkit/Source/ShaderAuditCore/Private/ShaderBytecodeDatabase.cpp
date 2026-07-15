// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderBytecodeDatabase.h"
#include "ShaderCodeArchive.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogShaderBytecodeDatabase, Log, All);

int32 FShaderBytecodeDatabase::ImportDirectory(const FString& DirectoryPath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderBytecodeDatabase::ImportDirectory);

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *DirectoryPath, TEXT(".ushaderbytecode"));

	if (FoundFiles.Num() == 0)
	{
		UE_LOGF(LogShaderBytecodeDatabase, Warning, "No .ushaderbytecode files found in: %ls", *DirectoryPath);
		return 0;
	}

	int32 FilesLoaded = 0;
	for (const FString& Filename : FoundFiles)
	{
		const FString FullPath = FPaths::Combine(DirectoryPath, Filename);
		const int32 NewShaders = ImportShaderArchive(FullPath);
		if (NewShaders >= 0)
		{
			++FilesLoaded;
		}
	}

	UE_LOGF(LogShaderBytecodeDatabase, Log, "Imported %d / %d archive files from %ls. Total unique shaders: %d",
		FilesLoaded, FoundFiles.Num(), *DirectoryPath, Entries.Num());

	return FilesLoaded;
}

int32 FShaderBytecodeDatabase::ImportShaderArchive(const FString& FilePath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderBytecodeDatabase::ImportShaderArchive);

	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*FilePath));
	if (!Ar)
	{
		UE_LOGF(LogShaderBytecodeDatabase, Error, "Failed to open: %ls", *FilePath);
		return -1;
	}

	// Read and verify version
	FSerializedShaderArchive SerializedShaders;
	if (!FSerializedShaderArchive::SerializeHeaderVersion(*Ar))
	{
		UE_LOGF(LogShaderBytecodeDatabase, Error, "Version mismatch in: %ls", *FilePath);
		return -1;
	}

	// Deserialize the header arrays (ShaderHashes, ShaderEntries, etc.).
	// This reads only the header portion of the file -- the bytecode blob
	// that follows is not touched.
	SerializedShaders.Serialize(*Ar);

	// The code blob starts right after the header
	const uint64 CodeBlobFileOffset = static_cast<uint64>(Ar->Tell());

	// Done with the file -- close immediately to avoid holding handles
	Ar->Close();
	Ar.Reset();

	// Register this archive
	if (Archives.Num() >= MAX_uint16)
	{
		UE_LOGF(LogShaderBytecodeDatabase, Error, "Too many archives loaded (max %d)", (int32)MAX_uint16);
		return -1;
	}
	const uint16 ArchiveIdx = static_cast<uint16>(Archives.Num());

	FShaderArchiveSource& Source = Archives.AddDefaulted_GetRef();
	Source.FilePath = FilePath;
	Source.CodeBlobFileOffset = CodeBlobFileOffset;

	// Extract per-shader metadata
	const TArrayView<const FShaderHash> ShaderHashes = SerializedShaders.GetShaderHashes();
	const TArrayView<const FShaderCodeEntry> ShaderEntries = SerializedShaders.GetShaderEntries();
	if (ShaderHashes.Num() != ShaderEntries.Num())
	{
		UE_LOGF(LogShaderBytecodeDatabase, Warning, "Mismatched hash/entry count in '%ls' (%d vs %d) -- skipping archive.",
			*FilePath, ShaderHashes.Num(), ShaderEntries.Num());
		Archives.Pop(EAllowShrinking::No);
		return -1;
	}

	int32 NewCount = 0;
	for (int32 Idx = 0; Idx < ShaderHashes.Num(); ++Idx)
	{
		const FShaderHash& Hash = ShaderHashes[Idx];
		const FShaderCodeEntry& Entry = ShaderEntries[Idx];

		// Same shader can appear in multiple chunk archives.
		// Track the first occurrence for metadata, but count all archives for duplication metrics.
		FShaderBytecodeInfo* Existing = Entries.Find(Hash);
		if (Existing)
		{
			++Existing->ArchiveCount;
		}
		else
		{
			FShaderBytecodeInfo& Info = Entries.Add(Hash);
			Info.CompressedSize = Entry.Size;
			Info.UncompressedSize = Entry.GetUncompressedSize();
			Info.Frequency = Entry.GetFrequency();
			Info.ArchiveIndex = ArchiveIdx;
			Info.OffsetInArchive = Entry.Offset;
			++NewCount;
		}
	}

	UE_LOGF(LogShaderBytecodeDatabase, Log, "Imported %ls: %d shaders (%d new). Header ends at file offset %llu.",
		*FPaths::GetCleanFilename(FilePath), ShaderHashes.Num(), NewCount, CodeBlobFileOffset);

	return NewCount;
}

const FShaderBytecodeInfo* FShaderBytecodeDatabase::Find(const FShaderHash& Hash) const
{
	return Entries.Find(Hash);
}

void FShaderBytecodeDatabase::Reset()
{
	Archives.Empty();
	Entries.Empty();
}

int32 FShaderBytecodeDatabase::ReadAllCompressedBlobs(TMap<FShaderHash, TArray<uint8>>& OutBlobs, std::atomic<int32>* OutArchivesDone) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderBytecodeDatabase::ReadAllCompressedBlobs);

	OutBlobs.Reserve(Entries.Num());

	using FSortedShaderArray = TArray<TPair<uint64, const FShaderHash*>>;
	TArray<FSortedShaderArray> SortedShaderArrays;
	{
		SortedShaderArrays.SetNum(Archives.Num());
		// Gather entries belonging to this archive, sorted by offset for sequential I/O
		for (const auto& [Hash, Info] : Entries)
		{
			SortedShaderArrays[Info.ArchiveIndex].Add({ Info.OffsetInArchive, &Hash });
		}

		for (FSortedShaderArray& Sorted : SortedShaderArrays)
		{
			Sorted.Sort([](const TPair<uint64, const FShaderHash*>& A, const TPair<uint64, const FShaderHash*>& B) { return A.Key < B.Key; });
		}
	}

	for (int32 ArchiveIdx = 0; ArchiveIdx < Archives.Num(); ++ArchiveIdx)
	{
		const FShaderArchiveSource& Source = Archives[ArchiveIdx];

		const FSortedShaderArray& Sorted = SortedShaderArrays[ArchiveIdx];

		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*Source.FilePath));
		if (!Ar)
		{
			UE_LOGF(LogShaderBytecodeDatabase, Error, "ReadAllCompressedBlobs: Failed to open archive: %ls", *Source.FilePath);
			continue;
		}

		for (const auto& [Offset, HashPtr] : Sorted)
		{
			const FShaderBytecodeInfo& Info = Entries[*HashPtr];
			const int64 AbsOffset = static_cast<int64>(Source.CodeBlobFileOffset + Offset);
			Ar->Seek(AbsOffset);

			TArray<uint8>& Blob = OutBlobs.Add(*HashPtr);
			Blob.SetNumUninitialized(Info.CompressedSize);
			Ar->Serialize(Blob.GetData(), Info.CompressedSize);
			if (Ar->IsError())
			{
				UE_LOGF(LogShaderBytecodeDatabase, Error, "ReadAllCompressedBlobs: I/O error reading shader blob at offset %lld in '%ls'", AbsOffset, *FPaths::GetCleanFilename(Source.FilePath));
				OutBlobs.Remove(*HashPtr);
				break;
			}
		}

		UE_LOGF(LogShaderBytecodeDatabase, Log, "ReadAllCompressedBlobs: Read %d shaders from %ls",
			Sorted.Num(), *FPaths::GetCleanFilename(Source.FilePath));

		if (OutArchivesDone)
		{
			OutArchivesDone->fetch_add(1, std::memory_order_relaxed);
		}
	}

	return OutBlobs.Num();
}

