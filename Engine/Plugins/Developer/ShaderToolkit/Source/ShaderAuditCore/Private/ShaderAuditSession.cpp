// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditSession.h"

#include "Features/IModularFeatures.h"
#include "IShaderAuditExtension.h"
#include "ShaderAuditProgress.h"
#include "ShaderBytecodeDatabase.h"
#include "PipelineCacheUtilities.h"

DEFINE_LOG_CATEGORY_STATIC(LogShaderAuditSession, Log, All);

// ============================================================================
// Session ID
// ============================================================================

static std::atomic<int32> GNextSessionId{1};

int32 FShaderAuditSession::AllocateSessionId()
{
	return GNextSessionId.fetch_add(1);
}

// ============================================================================
// Static session registry
// ============================================================================

TArray<TSharedPtr<FShaderAuditSession>>& FShaderAuditSession::GetSessions()
{
	static TArray<TSharedPtr<FShaderAuditSession>> GSessions;
	return GSessions;
}

TSharedPtr<FShaderAuditSession> FShaderAuditSession::FindSession(int32 Id)
{
	for (const TSharedPtr<FShaderAuditSession>& S : GetSessions())
	{
		if (S.IsValid() && S->SessionId == Id)
		{
			return S;
		}
	}
	return nullptr;
}

bool FShaderAuditSession::CloseSession(int32 Id)
{
	// Notify extensions before removing the session
	TArray<IShaderAuditExtension*> Extensions = IModularFeatures::Get().GetModularFeatureImplementations<IShaderAuditExtension>(IShaderAuditExtension::FeatureName);
	for (IShaderAuditExtension* Extension : Extensions)
	{
		Extension->OnSessionClosed(Id);
	}

	TArray<TSharedPtr<FShaderAuditSession>>& Sessions = GetSessions();
	for (int32 i = 0; i < Sessions.Num(); ++i)
	{
		if (Sessions[i].IsValid() && Sessions[i]->SessionId == Id)
		{
			Sessions.RemoveAt(i);
			return true;
		}
	}
	return false;
}
// ============================================================================
// Indexes & Views
// ============================================================================

void FShaderAuditSession::InitFromFullName(const FString& FullName, FString& OutPath, FString& OutClassName)
{
	int32 FirstDotIndex;
	if (FullName.FindChar(' ', FirstDotIndex))
	{
		OutClassName = FullName.Left(FirstDotIndex);
		OutPath = FullName.Mid(FirstDotIndex+1);
	}
	else
	{
		OutClassName.Reset();
		OutPath = FullName;
	}
}


void FShaderAuditSession::BuildIndex()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderAuditSession::BuildIndex);

	// Clear any previous index data (in case of rebuild)
	UniqueMaterials.Reset();
	UniqueShaders.Reset();
	ShaderHashToIndex.Reset();
	EntryMaterialIndex.Reset();

	const int32 Num = StableShaderKeyAndValueArray.Num();
	EntryMaterialIndex.SetNum(Num);

	FScopedProgressTask Progress(20.f + 1.f,
		NSLOCTEXT("ShaderAudit", "BuildIndex", "Building index..."));

	// Key by FCompactFullName directly -- avoids ToString() overhead and
	// eliminates string-parsing ambiguity that caused 2x duplication.
	TMap<FCompactFullName, int32> MatPathToIdx;
	TMap<FShaderHash, TSet<int32>> TempShaderMats;

	const int32 ChunkSize = FMath::Max(1, Num / 20); // 5% increments
	int32 NextChunk = ChunkSize;

	for (int32 Idx = 0; Idx < Num; ++Idx)
	{
		if (Idx >= NextChunk)
		{
			Progress.EnterProgressFrame(1.f,
				FText::Format(NSLOCTEXT("ShaderAudit", "BuildIndexProgress", "Indexing entries ({0}%)..."),
					FText::AsNumber((Idx * 100) / Num)));
			NextChunk += ChunkSize;
		}

		const FStableShaderKeyAndValue& Key = StableShaderKeyAndValueArray[Idx];

		// Assign or find material index
		int32* ExistingMatIdx = MatPathToIdx.Find(Key.ClassNameAndObjectPath);
		int32 MatIdx;
		if (ExistingMatIdx)
		{
			MatIdx = *ExistingMatIdx;
		}
		else
		{
			MatIdx = UniqueMaterials.Num();
			MatPathToIdx.Add(Key.ClassNameAndObjectPath, MatIdx);
			FUniqueMaterial& Mat = UniqueMaterials.AddDefaulted_GetRef();
			FShaderAuditSession::InitFromFullName(Key.ClassNameAndObjectPath.ToString(), Mat.Path, Mat.ClassName);
		}

		UniqueMaterials[MatIdx].ShaderIndices.Add(Idx);
		EntryMaterialIndex[Idx] = MatIdx;

		// Assign or find unique shader ID
		int32& ShaderID = ShaderHashToIndex.FindOrAdd(Key.OutputHash, INDEX_NONE);
		if (ShaderID == INDEX_NONE)
		{
			ShaderID = UniqueShaders.Num();
			FUniqueShader& S = UniqueShaders.AddDefaulted_GetRef();
			S.FirstEntryIdx = Idx;
		}

		TempShaderMats.FindOrAdd(Key.OutputHash).Add(MatIdx);
	}

	// Second pass: populate UniqueShader::MaterialIndices from deduped sets
	Progress.EnterProgressFrame(1.f,
		NSLOCTEXT("ShaderAudit", "BuildIndexFinalize", "Finalizing shader indices..."));

	for (auto& Pair : TempShaderMats)
	{
		const int32* ShaderID = ShaderHashToIndex.Find(Pair.Key);
		if (ShaderID)
		{
			UniqueShaders[*ShaderID].MaterialIndices = Pair.Value.Array();
		}
	}

	UE_LOGF(LogShaderAuditSession, Display, "BuildIndex: %d entries -> %d materials, %d unique shaders",
		Num, UniqueMaterials.Num(), UniqueShaders.Num());
}

// ============================================================================
// Bytecode database public API
// ============================================================================

bool FShaderAuditSession::HasBytecodeDatabase() const
{
	return BytecodeDatabase.IsValid();
}

int32 FShaderAuditSession::GetBytecodeArchiveCount() const
{
	return BytecodeDatabase.IsValid() ? BytecodeDatabase->NumArchives() : 0;
}

FString FShaderAuditSession::GetBytecodeArchiveDirectory() const
{
	if (BytecodeDatabase.IsValid() && BytecodeDatabase->GetArchives().Num() > 0)
	{
		return FPaths::GetPath(BytecodeDatabase->GetArchives()[0].FilePath);
	}
	return FString();
}

bool FShaderAuditSession::GetShaderBytecodeInfo(const FShaderHash& Hash, uint8& OutFrequency, uint32& OutCompressedSize, uint32& OutUncompressedSize) const
{
	if (!BytecodeDatabase.IsValid())
	{
		return false;
	}

	const FShaderBytecodeInfo* Info = BytecodeDatabase->Find(Hash);
	if (!Info)
	{
		return false;
	}

	OutFrequency = Info->Frequency;
	OutCompressedSize = Info->CompressedSize;
	OutUncompressedSize = Info->UncompressedSize;
	return true;
}

int32 FShaderAuditSession::ReadAllCompressedShaderBlobs(TMap<FShaderHash, TArray<uint8>>& OutBlobs, std::atomic<int32>* OutArchivesDone) const
{
	if (!BytecodeDatabase.IsValid())
	{
		return 0;
	}

	return BytecodeDatabase->ReadAllCompressedBlobs(OutBlobs, OutArchivesDone);
}
