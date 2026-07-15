// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Hash/ShaderHash.h"
#include "Misc/SecureHash.h"


/**
 * Lightweight per-shader metadata extracted from .ushaderbytecode headers.
 * No bytecode is loaded -- just size and location info for on-demand access.
 */
struct FShaderBytecodeInfo
{
	uint32 CompressedSize = 0;		// On-disk (compressed) size in bytes
	uint32 UncompressedSize = 0;	// Full size after decompression (includes SRT + code + optional)
	uint8 Frequency = 0;			// EShaderFrequency value

	// Where to find the bytecode on disk if we need to load it later
	uint16 ArchiveIndex = 0;		// Index into FShaderBytecodeDatabase::Archives
	uint64 OffsetInArchive = 0;		// Byte offset into the code blob within the archive file
	uint16 ArchiveCount = 1;			// Number of archives containing this shader (for duplication tracking)
};

/**
 * Bookkeeping for a single .ushaderbytecode file so we can go back and
 * read bytecodes on demand without keeping the whole file in memory.
 */
struct FShaderArchiveSource
{
	FString FilePath;
	uint64 CodeBlobFileOffset = 0;	// Byte position in the file where the code blob starts
};

/**
 * A database that maps OutputHash -> shader metadata (and optionally bytecode).
 *
 * Designed for two-phase use:
 *   1. Import: Load .ushaderbytecode headers (fast, small memory footprint).
 *              This gives size/frequency per shader.
 *   2. (Future) On-demand bytecode: Load individual shader bytecodes for
 *              disassembly, clustering, diffing.
 *
 * The OutputHash is the common key between .shk files (FStableShaderKeyAndValue::OutputHash)
 * and .ushaderbytecode files (FSerializedShaderArchive::ShaderHashes[i]).
 *
 * Thread safety: Import methods are not thread-safe. Call from game thread only.
 * After import, Find() is safe for concurrent reads.
 */
class FShaderBytecodeDatabase
{
public:
	/** Load headers from all .ushaderbytecode files in a directory. Returns number of files loaded. */
	int32 ImportDirectory(const FString& DirectoryPath);

	/** Load a single .ushaderbytecode file's header. Returns number of new shaders added. */
	int32 ImportShaderArchive(const FString& FilePath);

	/** Look up a shader by OutputHash. Returns nullptr if not found. */
	const FShaderBytecodeInfo* Find(const FShaderHash& Hash) const;

	/** Total number of unique shaders in the database. */
	int32 Num() const { return Entries.Num(); }

	/** Total number of loaded archive files. */
	int32 NumArchives() const { return Archives.Num(); }

	/** Clear all data. */
	void Reset();

	/**
	 * Read all compressed shader blobs from disk in archive-sequential order.
	 * Opens each archive file exactly once, reads entries sorted by offset for optimal I/O.
	 * The returned blobs are still compressed.
	 */
	int32 ReadAllCompressedBlobs(TMap<FShaderHash, TArray<uint8>>& OutBlobs, std::atomic<int32>* OutArchivesDone = nullptr) const;

	/** Access to the archive sources (for on-demand bytecode loading). */
	const TArray<FShaderArchiveSource>& GetArchives() const { return Archives; }

private:

	TArray<FShaderArchiveSource> Archives;
	TMap<FShaderHash, FShaderBytecodeInfo> Entries;
};
