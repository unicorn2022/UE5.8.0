// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanHashUtilities.h"

#include "MetaHumanCrowdEditorLog.h"
#include "Logging/StructuredLog.h"

#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "Serialization/ArchiveProxy.h"
#include "UObject/UnrealType.h"

static TAutoConsoleVariable<int32> CVarMetaHumanHashBreakAtOffset(
	TEXT("MetaHuman.Crowd.HashBreakAtOffset"),
	-1,
	TEXT("When >= 0, HashUObject will break in the debugger when a Serialize() call\n")
	TEXT("writes data that spans this byte offset in the serialized buffer.\n")
	TEXT("Use the hex dump (MetaHuman.Crowd.HashDiagnostics 2) or chunked hashes\n")
	TEXT("(MetaHuman.Crowd.HashDiagnostics 1) to identify the offset of interest,\n")
	TEXT("then set this cvar and re-run to get a callstack.\n")
	TEXT("Example: MetaHuman.Crowd.HashBreakAtOffset 4096"),
	ECVF_Default);

// Proxy archive that breaks in the debugger when a Serialize() call covers the target byte offset.
class FBreakAtOffsetArchive : public FArchiveProxy
{
public:
	FBreakAtOffsetArchive(FArchive& InInner, int64 InTargetOffset)
		: FArchiveProxy(InInner)
		, TargetOffset(InTargetOffset)
		, bTriggered(false)
	{
	}

	virtual void Serialize(void* V, int64 Length) override
	{
		const int64 Pos = InnerArchive.Tell();
		if (!bTriggered && TargetOffset >= Pos && TargetOffset < Pos + Length)
		{
			bTriggered = true;
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "HashBreakAtOffset: Breaking - Serialize({Length} bytes) at offset {Pos}, target offset {TargetOffset} is {BytesIntoWrite} bytes into this write",
				Length, Pos, TargetOffset, TargetOffset - Pos);
			UE_DEBUG_BREAK();
		}
		FArchiveProxy::Serialize(V, Length);
	}

private:
	int64 TargetOffset;
	bool bTriggered;
};

static TAutoConsoleVariable<int32> CVarMetaHumanHashDiagnostics(
	TEXT("MetaHuman.Crowd.HashDiagnostics"),
	0,
	TEXT("Diagnostic logging level for HashUObject/HashUStruct.\n")
	TEXT("  0 = Off (only basic hash logging when bLogDetails is true)\n")
	TEXT("  1 = Verbose (double-serialize check, tagged-vs-full breakdown,\n")
	TEXT("      per-property hashes, 4KB chunked hashes, class hierarchy)\n")
	TEXT("  2 = Full hex dump of serialized bytes (64 bytes/line, hex + ASCII)\n")
	TEXT("Use when debugging DDC cache key instability."),
	ECVF_Default);

// Writes a hex + ASCII dump of a byte buffer to a uniquely named file under Saved/HashDiagnostics/.
// BaseName is sanitized and combined with a timestamp. If the file already exists, a numeric suffix is appended.
static void WriteHexDumpToFile(const TArray<uint8>& Bytes, const FString& BaseName)
{
	// Sanitize the base name: replace path separators and other problematic chars
	FString SafeName = BaseName;
	SafeName.ReplaceInline(TEXT("/"), TEXT("_"));
	SafeName.ReplaceInline(TEXT("\\"), TEXT("_"));
	SafeName.ReplaceInline(TEXT(":"), TEXT("_"));
	SafeName.ReplaceInline(TEXT(" "), TEXT("_"));

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("HashDiagnostics");

	// Find a unique filename
	FString FilePath;
	for (int32 Suffix = 0; ; ++Suffix)
	{
		const FString FileName = Suffix == 0
			? FString::Printf(TEXT("%s_%s.txt"), *SafeName, *Timestamp)
			: FString::Printf(TEXT("%s_%s_%d.txt"), *SafeName, *Timestamp, Suffix);
		FilePath = Directory / FileName;
		if (!FPaths::FileExists(FilePath))
		{
			break;
		}
	}

	// Build the hex dump content
	constexpr int32 BytesPerLine = 64;
	const int32 NumLines = (Bytes.Num() + BytesPerLine - 1) / BytesPerLine;

	FString Content;
	Content.Reserve(NumLines * (8 + 2 + BytesPerLine * 2 + 2 + BytesPerLine + 2));

	for (int32 LineIdx = 0; LineIdx < NumLines; ++LineIdx)
	{
		const int32 LineOffset = LineIdx * BytesPerLine;
		const int32 LineLen = FMath::Min(BytesPerLine, Bytes.Num() - LineOffset);

		// Offset
		Content += FString::Printf(TEXT("%08X  "), LineOffset);

		// Hex portion
		for (int32 i = 0; i < LineLen; ++i)
		{
			const uint8 B = Bytes[LineOffset + i];
			Content += FString::Printf(TEXT("%02X"), B);
		}
		// Pad short final line
		for (int32 i = LineLen; i < BytesPerLine; ++i)
		{
			Content += TEXT("  ");
		}

		Content += TEXT("  ");

		// ASCII portion
		for (int32 i = 0; i < LineLen; ++i)
		{
			const uint8 B = Bytes[LineOffset + i];
			Content.AppendChar((B >= 0x20 && B <= 0x7E) ? static_cast<TCHAR>(B) : TEXT('.'));
		}

		Content += TEXT("\n");
	}

	if (FFileHelper::SaveStringToFile(Content, *FilePath))
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Hex dump written to: {FilePath} ({NumBytes} bytes)", FilePath, Bytes.Num());
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "  Failed to write hex dump to: {FilePath}", FilePath);
	}
}

// Serializes the data a second time and compares the result against the first serialization
// to detect within-process non-determinism. SerializeFunc should serialize into the provided archive.
static void LogDoubleSerializeCheck(
	const TArray<uint8>& FirstBytes,
	const FIoHash& FirstHash,
	TFunctionRef<void(FArchive&)> SerializeFunc)
{
	TArray<uint8> Bytes2;
	FMemoryWriter MemWriter2(Bytes2);
	FObjectAndNameAsStringProxyArchive ProxyAr2(MemWriter2, /*bInLoadIfFindFails=*/ false);
	ProxyAr2.SetIsSaving(true);
	ProxyAr2.ArNoDelta = true;
	SerializeFunc(ProxyAr2);

	const FIoHash Result2 = FIoHash::HashBuffer(Bytes2.GetData(), Bytes2.Num());
	if (FirstHash == Result2)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Double-serialize check: DETERMINISTIC (same hash both times)");
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "  Double-serialize check: NON-DETERMINISTIC within same call!");
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "    Second hash: {Hash} ({NumBytes} bytes)", LexToString(Result2), Bytes2.Num());

		// Find first divergent byte
		const int32 MinLen = FMath::Min(FirstBytes.Num(), Bytes2.Num());
		for (int32 i = 0; i < MinLen; ++i)
		{
			if (FirstBytes[i] != Bytes2[i])
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "    First difference at byte offset {Offset} (0x{OffsetHex})", i, FString::Printf(TEXT("%X"), i));
				break;
			}
		}
	}
}

// Logs the hash of each property in the given struct type, serialized individually from the data pointer.
// Returns the total number of serialized property bytes.
static int32 LogPerPropertyHashes(const UStruct* Struct, void* Data, EFieldIterationFlags IterationFlags = EFieldIterationFlags::Default)
{
	int32 TotalPropBytes = 0;
	UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Per-property hashes:");
	for (TFieldIterator<FProperty> PropIt(Struct, IterationFlags); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;

		TArray<uint8> PropBytes;
		FMemoryWriter PropWriter(PropBytes);
		FObjectAndNameAsStringProxyArchive PropAr(PropWriter, /*bInLoadIfFindFails=*/ false);
		PropAr.SetIsSaving(true);
		PropAr.ArNoDelta = true;

		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Data);
		FStructuredArchiveFromArchive PropAdapter(PropAr);
		Prop->SerializeItem(PropAdapter.GetSlot(), const_cast<void*>(ValuePtr));

		TotalPropBytes += PropBytes.Num();

		if (PropBytes.Num() > 0)
		{
			const FIoHash PropHash = FIoHash::HashBuffer(PropBytes.GetData(), PropBytes.Num());
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "    [{Owner}] {Property}: {NumBytes} bytes  hash={Hash}",
				Prop->GetOwnerStruct()->GetName(), Prop->GetName(), PropBytes.Num(), LexToString(PropHash));
		}
	}
	UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Sum of individual property bytes: {TotalPropBytes}", TotalPropBytes);
	return TotalPropBytes;
}

// Hashes the buffer in 4KB chunks and logs each chunk's hash, helping narrow down the byte range
// where instability starts between runs.
static void LogChunkedHashes(const TArray<uint8>& Bytes)
{
	constexpr int32 ChunkSize = 4096;
	const int32 NumChunks = (Bytes.Num() + ChunkSize - 1) / ChunkSize;
	UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Chunked hashes ({ChunkSize}-byte chunks, {NumChunks} chunks):", ChunkSize, NumChunks);
	for (int32 ChunkIdx = 0; ChunkIdx < NumChunks; ++ChunkIdx)
	{
		const int32 Offset = ChunkIdx * ChunkSize;
		const int32 ThisChunkSize = FMath::Min(ChunkSize, Bytes.Num() - Offset);
		const FIoHash ChunkHash = FIoHash::HashBuffer(Bytes.GetData() + Offset, ThisChunkSize);
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "    Chunk[{ChunkIdx}] offset={Offset}  size={Size}  hash={Hash}",
			ChunkIdx, Offset, ThisChunkSize, LexToString(ChunkHash));
	}
}

namespace UE::MetaHuman::HashUtilities
{

FIoHash HashUObject(TNotNull<const UObject*> Object, bool bLogDetails)
{
	TArray<uint8> Bytes;
	FMemoryWriter MemWriter(Bytes);

	FObjectAndNameAsStringProxyArchive ProxyAr(MemWriter, /*bInLoadIfFindFails=*/ false);
	ProxyAr.SetIsSaving(true);
	// Full property state, not delta from archetype
	ProxyAr.ArNoDelta = true;

	// Serialize takes a non-const object. Object won't be modified.
	UObject* MutableObject = const_cast<UObject*>(static_cast<const UObject*>(Object));

	const int32 BreakAtOffset = CVarMetaHumanHashBreakAtOffset.GetValueOnAnyThread();
	if (BreakAtOffset >= 0)
	{
		// Wrap the proxy archive in a breakpoint archive that will fire UE_DEBUG_BREAK()
		// when a Serialize() call writes data spanning the target byte offset.
		FBreakAtOffsetArchive BreakAr(ProxyAr, static_cast<int64>(BreakAtOffset));
		BreakAr.SetIsSaving(true);
		BreakAr.ArNoDelta = true;
		MutableObject->Serialize(BreakAr);
	}
	else
	{
		MutableObject->Serialize(ProxyAr);
	}

	const FIoHash Result = FIoHash::HashBuffer(Bytes.GetData(), Bytes.Num());

	if (bLogDetails)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "HashUObject: {Object} (Class={Class})", Object->GetPathName(), Object->GetClass()->GetName());
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Full Serialize() bytes: {NumBytes}", Bytes.Num());
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Full Serialize() hash:  {Hash}", LexToString(Result));

		// Verbose diagnostics gated behind MetaHuman.Crowd.HashDiagnostics cvar.
		const int32 DiagLevel = CVarMetaHumanHashDiagnostics.GetValueOnAnyThread();
		if (DiagLevel >= 1)
		{
			// Serialize a second time in the same call to check within-process determinism.
			LogDoubleSerializeCheck(Bytes, Result, [MutableObject](FArchive& Ar) { MutableObject->Serialize(Ar); });

			// Serialize only the tagged properties (UPROPERTYs) into a separate buffer.
			// UObject::Serialize = tagged properties + custom override data. Comparing
			// the two reveals whether the difference is in a UPROPERTY or in custom
			// Serialize() override code.
			{
				TArray<uint8> TaggedBytes;
				FMemoryWriter TaggedWriter(TaggedBytes);
				FObjectAndNameAsStringProxyArchive TaggedAr(TaggedWriter, /*bInLoadIfFindFails=*/ false);
				TaggedAr.SetIsSaving(true);
				TaggedAr.ArNoDelta = true;

				MutableObject->GetClass()->SerializeTaggedProperties(TaggedAr, reinterpret_cast<uint8*>(MutableObject), MutableObject->GetClass(), nullptr);

				const FIoHash TaggedHash = FIoHash::HashBuffer(TaggedBytes.GetData(), TaggedBytes.Num());
				UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Tagged props bytes:     {NumBytes}", TaggedBytes.Num());
				UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Tagged props hash:      {Hash}", LexToString(TaggedHash));
				UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Non-property overhead:  {NumBytes} bytes (Full - Tagged)", Bytes.Num() - TaggedBytes.Num());
			}

			// Per-property breakdown across the full class hierarchy (including superclasses).
			// TFieldIterator with IncludeSuper visits every UPROPERTY from UObject down to the leaf class.
			// Each property is serialized individually and its hash logged alongside the owning class.
			LogPerPropertyHashes(Object->GetClass(), MutableObject, EFieldIterationFlags::IncludeSuper);

			// Hash the full buffer in 4KB chunks. Compare chunk hashes between runs
			// to narrow down the exact byte range where instability starts.
			LogChunkedHashes(Bytes);

			// Log class hierarchy so you know which Serialize() overrides to inspect.
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Class hierarchy (innermost first):");
			for (UClass* Class = Object->GetClass(); Class; Class = Class->GetSuperClass())
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "    {ClassName}", Class->GetName());
			}
		}

		// Level 2: full hex + ASCII dump of the serialized buffer, written to a file.
		if (DiagLevel >= 2)
		{
			WriteHexDumpToFile(Bytes, Object->GetName());
		}
	}

	return Result;
}

FIoHash HashUStruct(TNotNull<const UScriptStruct*> Struct, TNotNull<const void*> StructData, bool bLogDetails)
{
	TArray<uint8> Bytes;
	FMemoryWriter MemWriter(Bytes);

	FObjectAndNameAsStringProxyArchive ProxyAr(MemWriter, /*bInLoadIfFindFails=*/ false);
	ProxyAr.SetIsSaving(true);
	ProxyAr.ArNoDelta = true;

	// SerializeItem takes a non-const data pointer. The data won't be modified.
	UScriptStruct* MutableStruct = const_cast<UScriptStruct*>(static_cast<const UScriptStruct*>(Struct));
	void* MutableStructData = const_cast<void*>(static_cast<const void*>(StructData));
	MutableStruct->SerializeItem(ProxyAr, MutableStructData, /*Defaults=*/ nullptr);

	const FIoHash Result = FIoHash::HashBuffer(Bytes.GetData(), Bytes.Num());

	if (bLogDetails)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "HashUStruct: {Struct}", Struct->GetName());
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Total serialized bytes: {NumBytes}", Bytes.Num());
		UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Full struct hash: {Hash}", LexToString(Result));

		// Verbose diagnostics gated behind MetaHuman.Crowd.HashDiagnostics cvar.
		const int32 DiagLevel = CVarMetaHumanHashDiagnostics.GetValueOnAnyThread();
		if (DiagLevel >= 1)
		{
			// Serialize a second time in the same call to check within-process determinism.
			LogDoubleSerializeCheck(Bytes, Result, [MutableStruct, MutableStructData](FArchive& Ar)
			{
				MutableStruct->SerializeItem(Ar, MutableStructData, /*Defaults=*/ nullptr);
			});

			// Per-property breakdown: serialize each property individually and log its hash.
			LogPerPropertyHashes(MutableStruct, MutableStructData);

			// Hash the full buffer in 4KB chunks. Compare chunk hashes between runs
			// to narrow down the exact byte range where instability starts.
			LogChunkedHashes(Bytes);
		}

		// Level 2: full hex + ASCII dump of the serialized buffer, written to a file.
		if (DiagLevel >= 2)
		{
			WriteHexDumpToFile(Bytes, Struct->GetName());
		}
	}

	return Result;
}

} // namespace UE::MetaHuman::HashUtilities
