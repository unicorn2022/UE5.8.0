// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/Accumulate.h"
#include "Algo/AllOf.h"
#include "Algo/Compare.h"
#include "Algo/Find.h"
#include "Algo/NoneOf.h"
#include "Algo/StableSort.h"
#include "Algo/Transform.h"
#include "Async/Async.h"
#include "Async/ManualResetEvent.h"
#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "Containers/StaticBitArray.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheMaintainer.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataChunk.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataValue.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/Thread.h"
#include "Hash/xxhash.h"
#include "HashingArchiveProxy.h"
#include "Logging/StructuredLog.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreMisc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CookStats.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Tasks/Task.h"
#include "Templates/Greater.h"

#include <atomic>

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace UE::DerivedData
{

TRACE_DECLARE_ATOMIC_INT_COUNTER(FileSystemDDC_Get, TEXT("FileSystemDDC Get"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(FileSystemDDC_GetHit, TEXT("FileSystemDDC Get Hit"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(FileSystemDDC_Put, TEXT("FileSystemDDC Put"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(FileSystemDDC_PutHit, TEXT("FileSystemDDC Put Hit"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(FileSystemDDC_BytesRead, TEXT("FileSystemDDC Bytes Read"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(FileSystemDDC_BytesWritten, TEXT("FileSystemDDC Bytes Written"));

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PLATFORM_LINUX
// PATH_MAX on Linux is 4096 (getconf PATH_MAX /, also see limits.h), so this value can be larger (note that it is still arbitrary).
// This should not affect sharing the cache between platforms as the absolute paths will be different anyway.
static constexpr int32 GMaxCacheRootLen = 3119;
#else
static constexpr int32 GMaxCacheRootLen = 119;
#endif // PLATFORM_LINUX

static constexpr int32 GMaxCacheKeyLen =
	FCacheBucket::MaxNameLen + // Name
	sizeof(FIoHash) * 2 +      // Hash
	4 +                        // Separators /<Name>/<Hash01>/<Hash23>/<Hash4-40>
	4;                         // Extension (.udd)

static const TCHAR* GBucketsDirectoryName = TEXT("Buckets");
static const TCHAR* GContentDirectoryName = TEXT("Content");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BuildPathForCachePackage(const FCacheKey& CacheKey, FStringBuilderBase& Path)
{
	const FIoHash::ByteArray& Bytes = CacheKey.Hash.GetBytes();
	Path.Appendf(TEXT("%s/%hs/%02x/%02x/"), GBucketsDirectoryName, CacheKey.Bucket.ToCString(), Bytes[0], Bytes[1]);
	UE::String::BytesToHexLower(MakeArrayView(Bytes).RightChop(2), Path);
	Path << TEXTVIEW(".udd");
}

void BuildPathForCacheContent(const FIoHash& RawHash, FStringBuilderBase& Path)
{
	const FIoHash::ByteArray& Bytes = RawHash.GetBytes();
	Path.Appendf(TEXT("%s/%02x/%02x/"), GContentDirectoryName, Bytes[0], Bytes[1]);
	UE::String::BytesToHexLower(MakeArrayView(Bytes).RightChop(2), Path);
	Path << TEXTVIEW(".udd");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint64 RandFromGuid()
{
	const FGuid Guid = FGuid::NewGuid();
	return FXxHash64::HashBuffer(&Guid, sizeof(FGuid)).Hash;
}

/** A LCG in which the modulus is a power of two where the exponent is the bit width of T. */
template <typename T, T Modulus = 0>
class TLinearCongruentialGenerator
{
	static_assert(!TIsSigned<T>::Value);
	static_assert((Modulus & (Modulus - 1)) == 0, "Modulus must be a power of two.");

public:
	constexpr inline TLinearCongruentialGenerator(T InMultiplier, T InIncrement)
		: Multiplier(InMultiplier)
		, Increment(InIncrement)
	{
	}

	constexpr inline T GetNext(T& Value)
	{
		Value = (Value * Multiplier + Increment) & (Modulus - 1);
		return Value;
	}

private:
	const T Multiplier;
	const T Increment;
};

class FRandomStream
{
public:
	inline explicit FRandomStream(const uint32 Seed)
		: Random(1103515245, 12345) // From ANSI C
		, Value(Seed)
	{
	}

	/** Returns a random value in [Min, Max). */
	inline uint32 GetRandRange(const uint32 Min, const uint32 Max)
	{
		return Min + uint32((uint64(Max - Min) * Random.GetNext(Value)) >> 32);
	}

private:
	TLinearCongruentialGenerator<uint32> Random;
	uint32 Value;
};

template <uint32 Modulus, uint32 Count = Modulus>
class TRandomOrder
{
	static_assert((Modulus & (Modulus - 1)) == 0 && Modulus > 16, "Modulus must be a power of two greater than 16.");
	static_assert(Count > 0 && Count <= Modulus, "Count must be in the range (0, Modulus].");

public:
	inline explicit TRandomOrder(FRandomStream& Stream)
		: Random(Stream.GetRandRange(0, Modulus / 16) * 8 + 5, 12345)
		, First(Stream.GetRandRange(0, Count))
		, Value(First)
	{
	}

	inline uint32 GetFirst() const
	{
		return First;
	}

	inline uint32 GetNext()
	{
		if constexpr (Count < Modulus)
		{
			for (;;)
			{
				if (const uint32 Next = Random.GetNext(Value); Next < Count)
				{
					return Next;
				}
			}
		}
		else
		{
			return Random.GetNext(Value);
		}
	}

private:
	TLinearCongruentialGenerator<uint32, Modulus> Random;
	uint32 First;
	uint32 Value;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FFileSystemCacheStoreMaintainerParams
{
	/** Files older than this will be deleted. */
	FTimespan MaxFileAge = FTimespan::FromDays(15.0);
	/** Limits the number of paths scanned in one second. */
	uint32 MaxScanRate = MAX_uint32;
	/** Limits the number of directories scanned in each cache bucket or content root. */
	uint32 MaxDirectoryScanCount = MAX_uint32;
	/** Minimum duration between the start of consecutive scans. Use MaxValue to scan only once. */
	FTimespan ScanFrequency = FTimespan::FromHours(1.0);
	/** Time to wait after initialization before maintenance begins. */
	FTimespan TimeToWaitAfterInit = FTimespan::FromMinutes(1.0);
};

class FFileSystemCacheStoreMaintainer final : public ICacheStoreMaintainer
{
public:
	FFileSystemCacheStoreMaintainer(
		const FFileSystemCacheStoreMaintainerParams& Params,
		FStringView CachePath,
		ICacheStoreOwner& Owner);
	~FFileSystemCacheStoreMaintainer();

	bool IsIdle() const final { return bIdle; }
	void WaitForIdle() const { IdleEvent.Wait(); }
	void BoostPriority() final;

private:
	void Tick();
	void Loop();
	void Scan();

	void CreateContentRoot();
	void CreateBucketRoots();
	void ScanHashRoot(uint32 RootIndex);
	TStaticBitArray<256> ScanHashDirectory(FStringBuilderBase& BasePath);

	TStaticBitArray<10> ScanLegacyDirectory(FStringBuilderBase& BasePath);
	void CreateLegacyRoot();
	void ScanLegacyRoot();

	void ResetRoots();

	void ProcessDirectory(const TCHAR* Path);
	void ProcessFile(const TCHAR* Path, const FFileStatData& Stat, bool& bOutDeletedFile);
	void ProcessWait();

	void DeleteDirectory(const TCHAR* Path);

private:
	struct FRoot;
	struct FLegacyRoot;

	ICacheStoreOwner& StoreOwner;
	FFileSystemCacheStoreMaintainerParams Params;
	/** Path to the root of the cache store. */
	FString CachePath;
	/** True when there is no active maintenance scan. */
	bool bIdle = false;
	/** True when maintenance is expected to exit as soon as possible. */
	bool bExit = false;
	/** True when maintenance is expected to exit at the end of the scan. */
	bool bExitAfterScan = false;
	/** Ignore the scan rate for one maintenance scan. */
	bool bIgnoreScanRate = false;

	uint32 FileCount = 0;
	uint32 FolderCount = 0;
	uint32 ProcessCount = 0;
	uint32 DeleteFileCount = 0;
	uint32 DeleteFolderCount = 0;
	uint64 DeleteSize = 0;
	uint64 ScannedSize = 0;

	double BatchStartTime = 0.0;

	IFileManager& FileManager = IFileManager::Get();

	TArray<TUniquePtr<FRoot>> Roots;
	TUniquePtr<FLegacyRoot> LegacyRoot;
	FRandomStream Random{uint32(RandFromGuid())};

	mutable FManualResetEvent IdleEvent;
	FEventRef WaitEvent;
	FThread Thread;

	static constexpr double MaxScanFrequencyDays = 365.0;
};

struct FFileSystemCacheStoreMaintainer::FRoot
{
	inline FRoot(const FStringView RootPath, FRandomStream& Stream)
		: Order(Stream)
	{
		Path.Append(RootPath);
	}

	TStringBuilder<256> Path;
	TRandomOrder<256 * 256> Order;
	TStaticBitArray<256> ScannedLevel0;
	TStaticBitArray<256> ExistsLevel0;
	TStaticBitArray<256> ExistsLevel1[256];
	uint32 DirectoryScanCount = 0;
	bool bScannedRoot = false;
};

// Temp workaround for icx compiler crash
#if defined(__INTEL_LLVM_COMPILER)
#pragma optimize("", off)
#endif // #if defined(__INTEL_LLVM_COMPILER)
// End temp workaround for icx compiler crash
struct FFileSystemCacheStoreMaintainer::FLegacyRoot
{
	inline explicit FLegacyRoot(FRandomStream& Stream)
		: Order(Stream)
	{
	}

	TRandomOrder<1024, 1000> Order;
	TStaticBitArray<10> ScannedLevel0;
	TStaticBitArray<10> ScannedLevel1[10];
	TStaticBitArray<10> ExistsLevel0;
	TStaticBitArray<10> ExistsLevel1[10];
	TStaticBitArray<10> ExistsLevel2[10][10];
	uint32 DirectoryScanCount = 0;
};
// Temp workaround for icx compiler crash
#if defined(__INTEL_LLVM_COMPILER)
#pragma optimize("", on)
#endif // #if defined(__INTEL_LLVM_COMPILER)
// End temp workaround for icx compiler crash

FFileSystemCacheStoreMaintainer::FFileSystemCacheStoreMaintainer(
	const FFileSystemCacheStoreMaintainerParams& InParams,
	const FStringView InCachePath,
	ICacheStoreOwner& InOwner)
	: StoreOwner(InOwner)
	, Params(InParams)
	, CachePath(InCachePath)
	, bExitAfterScan(Params.ScanFrequency.GetTotalDays() > MaxScanFrequencyDays)
	, WaitEvent(EEventMode::AutoReset)
	, Thread(
		TEXT("FileSystemCacheStoreMaintainer"),
		[this] { Loop(); },
		[this] { Tick(); },
		/*StackSize*/ 32 * 1024,
		TPri_BelowNormal)
{
	StoreOwner.AddMaintainer(this);
}

FFileSystemCacheStoreMaintainer::~FFileSystemCacheStoreMaintainer()
{
	bExit = true;
	StoreOwner.RemoveMaintainer(this);
	WaitEvent->Trigger();
	Thread.Join();
}

void FFileSystemCacheStoreMaintainer::BoostPriority()
{
	bIgnoreScanRate = true;
	WaitEvent->Trigger();
}

void FFileSystemCacheStoreMaintainer::Tick()
{
	// Scan once and exit if the priority has been boosted.
	if (bIgnoreScanRate)
	{
		bExitAfterScan = true;
		Loop();
	}
	bIdle = true;
	IdleEvent.Notify();
}

void FFileSystemCacheStoreMaintainer::Loop()
{
	WaitEvent->Wait(Params.TimeToWaitAfterInit, /*bIgnoreThreadIdleStats*/ true);

	while (!bExit)
	{
		const FDateTime ScanStart = FDateTime::Now();
		FileCount = 0;
		FolderCount = 0;
		DeleteFileCount = 0;
		DeleteFolderCount = 0;
		DeleteSize = 0;
		ScannedSize = 0;
		IdleEvent.Reset();
		bIdle = false;
		Scan();
		bIdle = true;
		IdleEvent.Notify();
		bIgnoreScanRate = false;
		const FDateTime ScanEnd = FDateTime::Now();

		UE_LOG(LogDerivedDataCache, Log,
			TEXT("%s: Maintenance finished in %s and deleted %u files with total size %" UINT64_FMT " MiB "
				 "and %u empty folders. Scanned %u files in %u folders with total size %" UINT64_FMT " MiB."),
			*CachePath, *(ScanEnd - ScanStart).ToString(), DeleteFileCount, DeleteSize / 1024 / 1024,
			DeleteFolderCount, FileCount, FolderCount, ScannedSize / 1024 / 1024);

		if (bExit || bExitAfterScan)
		{
			break;
		}

		const FDateTime ScanTime = ScanStart + Params.ScanFrequency;
		UE_CLOG(ScanEnd < ScanTime, LogDerivedDataCache, Verbose,
			TEXT("%s: Maintenance is paused until the next scan at %s."), *CachePath, *ScanTime.ToString());
		for (FDateTime Now = ScanEnd; !bExit && Now < ScanTime; Now = FDateTime::Now())
		{
			WaitEvent->Wait(ScanTime - Now, /*bIgnoreThreadIdleStats*/ true);
		}
	}

	bIdle = true;
	IdleEvent.Notify();
}

void FFileSystemCacheStoreMaintainer::Scan()
{
	CreateContentRoot();
	CreateBucketRoots();
	CreateLegacyRoot();

	while (!bExit)
	{
		const uint32 RootCount = uint32(Roots.Num());
		const uint32 TotalRootCount = uint32(RootCount + LegacyRoot.IsValid());
		if (TotalRootCount == 0)
		{
			break;
		}
		if (const uint32 RootIndex = Random.GetRandRange(0, TotalRootCount); RootIndex < RootCount)
		{
			ScanHashRoot(RootIndex);
		}
		else
		{
			ScanLegacyRoot();
		}
	}

	ResetRoots();
}

void FFileSystemCacheStoreMaintainer::CreateContentRoot()
{
	TStringBuilder<256> ContentPath;
	FPathViews::Append(ContentPath, CachePath, GContentDirectoryName);
	if (FileManager.DirectoryExists(*ContentPath))
	{
		Roots.Add(MakeUnique<FRoot>(ContentPath, Random));
	}
}

void FFileSystemCacheStoreMaintainer::CreateBucketRoots()
{
	TStringBuilder<256> BucketsPath;
	FPathViews::Append(BucketsPath, CachePath, GBucketsDirectoryName);
	if (FileManager.DirectoryExists(*BucketsPath))
	{
		++FolderCount;
		const int32 StartRootCount = Roots.Num();
		FileManager.IterateDirectoryStat(*BucketsPath, [this](const TCHAR* Path, const FFileStatData& Stat) -> bool
		{
			if (Stat.bIsDirectory)
			{
				Roots.Add(MakeUnique<FRoot>(Path, Random));
			}
			return !bExit;
		});
		if (StartRootCount == Roots.Num())
		{
			DeleteDirectory(*BucketsPath);
		}
	}
}

void FFileSystemCacheStoreMaintainer::ScanHashRoot(const uint32 RootIndex)
{
	FRoot& Root = *Roots[int32(RootIndex)];
	const uint32 DirectoryIndex = Root.Order.GetNext();
	const uint32 IndexLevel0 = DirectoryIndex / 256;
	const uint32 IndexLevel1 = DirectoryIndex % 256;

	bool bScanned = false;
	ON_SCOPE_EXIT
	{
		if ((DirectoryIndex == Root.Order.GetFirst()) ||
			(bScanned && ++Root.DirectoryScanCount >= Params.MaxDirectoryScanCount))
		{
			UE_LOGF(LogDerivedDataCache, VeryVerbose,
				"%ls: Maintenance finished scanning %ls.", *CachePath, *Root.Path);
			Roots.RemoveAt(int32(RootIndex));
		}
	};

	if (!Root.bScannedRoot)
	{
		Root.ExistsLevel0 = ScanHashDirectory(Root.Path);
		Root.bScannedRoot = true;
	}

	if (!Root.ExistsLevel0[IndexLevel0])
	{
		return;
	}
	if (!Root.ScannedLevel0[IndexLevel0])
	{
		TStringBuilder<256> Path;
		Path.Appendf(TEXT("%s/%02x"), *Root.Path, IndexLevel0);
		Root.ExistsLevel1[IndexLevel0] = ScanHashDirectory(Path);
		Root.ScannedLevel0[IndexLevel0] = true;
	}

	if (!Root.ExistsLevel1[IndexLevel0][IndexLevel1])
	{
		return;
	}

	TStringBuilder<256> Path;
	Path.Appendf(TEXT("%s/%02x/%02x"), *Root.Path, IndexLevel0, IndexLevel1);
	ProcessDirectory(*Path);

	bScanned = true;
}

TStaticBitArray<256> FFileSystemCacheStoreMaintainer::ScanHashDirectory(FStringBuilderBase& BasePath)
{
	++FolderCount;
	TStaticBitArray<256> Exists;
	FileManager.IterateDirectoryStat(*BasePath, [this, &Exists](const TCHAR* Path, const FFileStatData& Stat) -> bool
	{
		FStringView View = FPathViews::GetCleanFilename(Path);
		if (Stat.bIsDirectory && View.Len() == 2 && Algo::AllOf(View, FChar::IsHexDigit))
		{
			uint8 Byte;
			if (String::HexToBytes(View, &Byte) == 1)
			{
				Exists[Byte] = true;
			}
		}
		return !bExit;
	});
	if (Exists.FindFirstSetBit() == INDEX_NONE)
	{
		DeleteDirectory(*BasePath);
	}
	return Exists;
}

TStaticBitArray<10> FFileSystemCacheStoreMaintainer::ScanLegacyDirectory(FStringBuilderBase& BasePath)
{
	++FolderCount;
	TStaticBitArray<10> Exists;
	FileManager.IterateDirectoryStat(*BasePath, [this, &Exists](const TCHAR* Path, const FFileStatData& Stat) -> bool
	{
		FStringView View = FPathViews::GetCleanFilename(Path);
		if (Stat.bIsDirectory && View.Len() == 1 && Algo::AllOf(View, FChar::IsDigit))
		{
			Exists[FChar::ConvertCharDigitToInt(View[0])] = true;
		}
		return !bExit;
	});
	if (Exists.FindFirstSetBit() == INDEX_NONE && BasePath.Len() > CachePath.Len())
	{
		DeleteDirectory(*BasePath);
	}
	return Exists;
}

void FFileSystemCacheStoreMaintainer::CreateLegacyRoot()
{
	TStringBuilder<256> Path;
	FPathViews::Append(Path, CachePath);
	TStaticBitArray<10> Exists = ScanLegacyDirectory(Path);
	if (Exists.FindFirstSetBit() != INDEX_NONE)
	{
		LegacyRoot = MakeUnique<FLegacyRoot>(Random);
		LegacyRoot->ExistsLevel0 = Exists;
	}
}

void FFileSystemCacheStoreMaintainer::ScanLegacyRoot()
{
	FLegacyRoot& Root = *LegacyRoot;
	const uint32 DirectoryIndex = Root.Order.GetNext();
	const int32 IndexLevel0 = int32(DirectoryIndex / 100) % 10;
	const int32 IndexLevel1 = int32(DirectoryIndex / 10) % 10;
	const int32 IndexLevel2 = int32(DirectoryIndex / 1) % 10;

	bool bScanned = false;
	ON_SCOPE_EXIT
	{
		if ((DirectoryIndex == Root.Order.GetFirst()) ||
			(bScanned && ++Root.DirectoryScanCount >= Params.MaxDirectoryScanCount))
		{
			LegacyRoot.Reset();
		}
	};

	if (!Root.ExistsLevel0[IndexLevel0])
	{
		return;
	}
	if (!Root.ScannedLevel0[IndexLevel0])
	{
		TStringBuilder<256> Path;
		FPathViews::Append(Path, CachePath, IndexLevel0);
		Root.ExistsLevel1[IndexLevel0] = ScanLegacyDirectory(Path);
		Root.ScannedLevel0[IndexLevel0] = true;
	}

	if (!Root.ExistsLevel1[IndexLevel0][IndexLevel1])
	{
		return;
	}
	if (!Root.ScannedLevel1[IndexLevel0][IndexLevel1])
	{
		TStringBuilder<256> Path;
		FPathViews::Append(Path, CachePath, IndexLevel0, IndexLevel1);
		Root.ExistsLevel2[IndexLevel0][IndexLevel1] = ScanLegacyDirectory(Path);
		Root.ScannedLevel1[IndexLevel0][IndexLevel1] = true;
	}

	if (!Root.ExistsLevel2[IndexLevel0][IndexLevel1][IndexLevel2])
	{
		return;
	}

	TStringBuilder<256> Path;
	FPathViews::Append(Path, CachePath, IndexLevel0, IndexLevel1, IndexLevel2);
	ProcessDirectory(*Path);

	bScanned = true;
}

void FFileSystemCacheStoreMaintainer::ResetRoots()
{
	Roots.Empty();
	LegacyRoot.Reset();
}

void FFileSystemCacheStoreMaintainer::ProcessDirectory(const TCHAR* const Path)
{
	++FolderCount;

	bool bTryDelete = true;

	FileManager.IterateDirectoryStat(Path, [this, &bTryDelete](const TCHAR* const Path, const FFileStatData& Stat) -> bool
	{
		bool bDeletedFile = false;
		ProcessFile(Path, Stat, bDeletedFile);
		bTryDelete &= bDeletedFile;
		return !bExit;
	});

	if (bTryDelete)
	{
		DeleteDirectory(Path);
	}

	ProcessWait();
}

void FFileSystemCacheStoreMaintainer::ProcessFile(const TCHAR* const Path, const FFileStatData& Stat, bool& bOutDeletedFile)
{
	bOutDeletedFile = false;

	if (Stat.bIsDirectory)
	{
		return;
	}

	++FileCount;
	ScannedSize += Stat.FileSize > 0 ? uint64(Stat.FileSize) : 0;

	const FDateTime Now = FDateTime::UtcNow();
	if (Stat.ModificationTime + Params.MaxFileAge < Now && Stat.AccessTime + Params.MaxFileAge < Now)
	{
		++DeleteFileCount;
		DeleteSize += Stat.FileSize > 0 ? uint64(Stat.FileSize) : 0;
		if (FileManager.Delete(Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true))
		{
			bOutDeletedFile = true;
			UE_LOGF(LogDerivedDataCache, VeryVerbose,
				"%ls: Maintenance deleted file %ls that was last modified at %ls.",
				*CachePath, Path, *Stat.ModificationTime.ToIso8601());
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Verbose,
				"%ls: Maintenance failed to delete file %ls that was last modified at %ls.",
				*CachePath, Path, *Stat.ModificationTime.ToIso8601());
		}
	}

	ProcessWait();
}

void FFileSystemCacheStoreMaintainer::ProcessWait()
{
	if (!bExit && !bIgnoreScanRate && Params.MaxScanRate && ++ProcessCount % Params.MaxScanRate == 0)
	{
		const double BatchEndTime = FPlatformTime::Seconds();
		if (const double BatchWaitTime = 1.0 - (BatchEndTime - BatchStartTime); BatchWaitTime > 0.0)
		{
			WaitEvent->Wait(FTimespan::FromSeconds(BatchWaitTime), /*bIgnoreThreadIdleStats*/ true);
			BatchStartTime = FPlatformTime::Seconds();
		}
		else
		{
			BatchStartTime = BatchEndTime;
		}
	}
}

void FFileSystemCacheStoreMaintainer::DeleteDirectory(const TCHAR* Path)
{
	if (FileManager.DeleteDirectory(Path))
	{
		++DeleteFolderCount;
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Maintenance deleted empty directory %ls.", *CachePath, Path);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FAccessLogWriter
{
public:
	FAccessLogWriter(const TCHAR* FileName, const FString& CachePath);

	void Append(const FIoHash& RawHash, FStringView Path);
	void Append(const FCacheKey& CacheKey, FStringView Path);

private:
	void AppendPath(FStringView Path);

	TUniquePtr<FArchive> Archive;
	FString BasePath;
	FCriticalSection CriticalSection;
	TSet<FIoHash> ContentKeys;
	TSet<FCacheKey> RecordKeys;
};

FAccessLogWriter::FAccessLogWriter(const TCHAR* const FileName, const FString& CachePath)
	: Archive(IFileManager::Get().CreateFileWriter(FileName, FILEWRITE_AllowRead))
	, BasePath(CachePath / TEXT(""))
{
}

void FAccessLogWriter::Append(const FIoHash& RawHash, const FStringView Path)
{
	FScopeLock Lock(&CriticalSection);

	bool bIsAlreadyInSet = false;
	ContentKeys.FindOrAdd(RawHash, &bIsAlreadyInSet);
	if (!bIsAlreadyInSet)
	{
		AppendPath(Path);
	}
}

void FAccessLogWriter::Append(const FCacheKey& CacheKey, const FStringView Path)
{
	FScopeLock Lock(&CriticalSection);

	bool bIsAlreadyInSet = false;
	RecordKeys.FindOrAdd(CacheKey, &bIsAlreadyInSet);
	if (!bIsAlreadyInSet)
	{
		AppendPath(Path);
	}
}

void FAccessLogWriter::AppendPath(const FStringView Path)
{
	if (Path.StartsWith(BasePath))
	{
		const FTCHARToUTF8 PathUtf8(Path.RightChop(BasePath.Len()));
		Archive->Serialize(const_cast<ANSICHAR*>(PathUtf8.Get()), PathUtf8.Length());
		Archive->Serialize(const_cast<ANSICHAR*>(LINE_TERMINATOR_ANSI), sizeof(LINE_TERMINATOR_ANSI) - 1);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FFileSystemCacheStoreParams
{
	/** Name of the cache, e.g., Local, Shared. */
	FString CacheName;

	/** Root path under which cache files are stored. */
	FString CachePath;

	/** Path to the optional access log that records every accessed file. */
	FString AccessLogPath;

	/** Optional name of a file containing redirection information so that if the file is present and valid, another cache store can be used in place of this FileSystemCacheStore. */
	FString RedirectionFileName;

	/** Optional name of a key within the RedirectionFileName containing redirection information so that if the file+key is present and valid, another cache store can be used in place of this FileSystemCacheStore. */
	FString RedirectionKeyName;

	/** Maximum total size of compressed data stored within a record package with multiple attachments. */
	uint64 MaxRecordSizeKB = 256;
	/** Maximum total size of compressed data stored within a value package, or a record package with one attachment. */
	uint64 MaxValueSizeKB = 1024;

	/** Maintenance will delete files older than this. */
	double MaxFileAgeInDays = 15.0;
	/** Maintenance will wait this long after initialization before starting. */
	double MaintenanceDelayInSeconds = 60.0;
	/** Limits the number of paths scanned by maintenance in one second. */
	uint32 MaxScanRate = MAX_uint32;
	/** Limits the number of directories scanned by maintenance in each cache bucket or content root. */
	uint32 MaxDirectoryScanCount = MAX_uint32;

	/** Latency lower than this is considered local. */
	float ConsiderFastAtMs = 10;
	/** Latency lower than this is considered ok, and anything higher is considered slow. */
	float ConsiderSlowAtMs = 50;
	/** Latency higher than this will deactivate the cache store for performance reasons. */
	float DeactivateAtMs = -1.0f;

	/** If true, skip the speed test to measure latency on startup; */
	bool bSkipSpeedTest = !WITH_EDITOR;
	/** If true, display a retry prompt in attended sessions when the cache store is missing. */
	bool bPromptIfMissing = false;
	/** If true, files older than the max file age will be deleted during maintenance. */
	bool bDeleteUnused = false;
	/** If true, do not write or read any files in this cache store, only delete expired files. */
	bool bDeleteOnly = false;
	/** If true, do not write any files in this cache store. */
	bool bReadOnly = false;
	/** If true, this cache store is considered to be remote. */
	bool bRemote = false;
	/** If true, block on maintenance of this cache store on startup. */
	bool bClean = false;
	/** If true, delete everything in this cache store on startup. */
	bool bFlush = false;
	/** If true, always update file timestamps on access, even when read-only. */
	bool bTouch = false;

	void Parse(const TCHAR* Name, const TCHAR* Config);
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FFileSystemCacheStore final : public ILegacyCacheStore
{
public:
	static ILegacyCacheStore* TryCreate(
		const FFileSystemCacheStoreParams& Params,
		ICacheStoreOwner& Owner);

private:
	FFileSystemCacheStore(
		ICacheStoreOwner& Owner,
		const FFileSystemCacheStoreParams& Params,
		const FDerivedDataCacheSpeedStats& SpeedStats);
	~FFileSystemCacheStore();

	static bool RunSpeedTest(
		FString CachePath,
		bool bReadOnly,
		bool bSeekTimeOnly,
		double& OutSeekTimeMS,
		double& OutReadSpeedMBs,
		double& OutWriteSpeedMBs,
		std::atomic<uint32>* NumLatencyTestsCompleted,
		std::atomic<bool>* AbandonRequest);

	// ICacheStore Interface

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) final;
	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final;
	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) final;
	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) final;
	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final;

	// ILegacyCacheStore

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final;

private:
	[[nodiscard]] FOptionalCacheRecord PutCacheRecord(FStringView Name, const FCacheRecord& Record, const FCacheRecordPolicy& Policy, FRequestStats& Stats);

	[[nodiscard]] FOptionalCacheRecord GetCacheRecordOnly(
		FStringView Name,
		const FCacheKey& Key,
		const FCacheRecordPolicy& Policy,
		FRequestStats& Stats);
	[[nodiscard]] FOptionalCacheRecord GetCacheRecord(
		FStringView Name,
		const FCacheKey& Key,
		const FCacheRecordPolicy& Policy,
		EStatus& OutStatus,
		FRequestStats& Stats);
	[[nodiscard]] FOptionalCacheRecord GetCacheRecordContent(
		const FStringView Name,
		const FCacheRecord& Record,
		const FCacheRecordPolicy& Policy,
		EStatus& OutStatus,
		FRequestStats& Stats);

	[[nodiscard]] TOptional<FValue> PutCacheValue(FStringView Name, const FCacheKey& Key, const FValue& Value, ECachePolicy Policy, FRequestStats& Stats);

	[[nodiscard]] bool GetCacheValueOnly(FStringView Name, const FCacheKey& Key, ECachePolicy Policy, FValue& OutValue, FRequestStats& Stats);
	[[nodiscard]] bool GetCacheValue(FStringView Name, const FCacheKey& Key, ECachePolicy Policy, FValue& OutValue, FRequestStats& Stats);

	[[nodiscard]] bool PutCacheContent(FStringView Name, const FCompressedBuffer& Content, FRequestStats& Stats) const;

	[[nodiscard]] bool GetCacheContentExists(const FCacheKey& Key, const FIoHash& RawHash, FRequestStats& Stats) const;
	[[nodiscard]] bool GetCacheContent(
		FStringView Name,
		const FCacheKey& Key,
		const FValueId& Id,
		const FValue& Value,
		ECachePolicy Policy,
		FValue& OutValue,
		FRequestStats& Stats) const;
	[[nodiscard]] bool GetCacheContent(
		FStringView Name,
		const FCacheKey& Key,
		const FValueId& Id,
		const FValue& Value,
		ECachePolicy Policy,
		FCompressedBufferReader& Reader,
		TUniquePtr<FArchive>& OutArchive,
		FRequestStats& Stats) const;

	void DeleteCacheContent(FStringView Name, const FValue& Value) const;
	void DeleteCacheContent(FStringView Name, const FValue& Value, FCompressedBufferReader& Reader, TUniquePtr<FArchive>& OutArchive) const;

	void BuildCachePackagePath(const FCacheKey& CacheKey, FStringBuilderBase& Path) const;
	void BuildCacheContentPath(const FIoHash& RawHash, FStringBuilderBase& Path) const;

	[[nodiscard]] bool SaveFileWithHash(FStringBuilderBase& Path, FStringView DebugName, FRequestStats& Stats, TFunctionRef<void (FArchive&)> WriteFunction, bool bReplaceExisting = false) const;
	[[nodiscard]] bool LoadFileWithHash(FStringBuilderBase& Path, FStringView DebugName, FRequestStats& Stats, TFunctionRef<void (FArchive&)> ReadFunction) const;
	[[nodiscard]] bool SaveFile(FStringBuilderBase& Path, FStringView DebugName, FRequestStats& Stats, TFunctionRef<void (FArchive&)> WriteFunction, bool bReplaceExisting = false) const;
	[[nodiscard]] bool LoadFile(FStringBuilderBase& Path, FStringView DebugName, FRequestStats& Stats, TFunctionRef<void (FArchive&)> ReadFunction) const;
	[[nodiscard]] TUniquePtr<FArchive> OpenFileWrite(FStringBuilderBase& Path, FStringView DebugName, FRequestStats& Stats) const;
	[[nodiscard]] TUniquePtr<FArchive> OpenFileRead(FStringBuilderBase& Path, FStringView DebugName, FRequestStats& Stats) const;

	[[nodiscard]] bool FileExists(FStringBuilderBase& Path, FRequestStats& Stats) const;

	[[nodiscard]] bool IsDeactivatedForPerformance();
	void UpdateStatus();

	static bool RunInitialSpeedTest(const FFileSystemCacheStoreParams& Params, FDerivedDataCacheSpeedStats& OutSpeedStats);

private:
	FString CachePath;
	ICacheStoreOwner& StoreOwner;
	ICacheStoreStats* StoreStats = nullptr;

	/** If true, this cache store is considered to be remote. */
	bool		bRemote;
	/** If true, do not attempt to write to this cache. */
	bool		bReadOnly;
	/** If true, always update file timestamps on access. */
	bool		bTouch;

	/** Age of file when it should be deleted from DDC cache. */
	double		MaxFileAgeInDays = 15.0;

	/** Maximum total size of compressed data stored within a record package with multiple attachments. */
	uint64		MaxRecordSizeKB = 256;
	/** Maximum total size of compressed data stored within a value package, or a record package with one attachment. */
	uint64		MaxValueSizeKB = 1024;

	/** Access log to write to */
	TUniquePtr<FAccessLogWriter> AccessLogWriter;

	/** Speed stats */
	FDerivedDataCacheSpeedStats SpeedStats;

	TUniquePtr<FFileSystemCacheStoreMaintainer> Maintainer;

	TUniquePtr<FFileSystemCacheStoreMaintainerParams> DeactivationDeferredMaintainerParams;

	inline static FMutex ActiveStoresMutex;
	inline static TArray<FFileSystemCacheStore*> ActiveStores;

	enum class EPerformanceReEvaluationResult
	{
		Invalid = 0,
		PerformanceActivate,
		PerformanceDeactivate
	};
	FRWLock PerformanceReEvaluationTaskLock;
	Tasks::TTask<std::atomic<EPerformanceReEvaluationResult>> PerformanceReEvaluationTask;
	std::atomic<int64> LastPerformanceEvaluationTicks;
	std::atomic<bool> bDeactivatedForPerformance = false;
	bool bDeactivationDeferredClean = false;
	float DeactivateAtMs;
};

ILegacyCacheStore* FFileSystemCacheStore::TryCreate(
	const FFileSystemCacheStoreParams& Params,
	ICacheStoreOwner& Owner)
{
	// If we find a platform that has more stringent limits, this needs to be rethought.
	checkf(GMaxCacheRootLen + GMaxCacheKeyLen <= FPlatformMisc::GetMaxPathLength(),
		TEXT("Not enough room left for cache keys in max path."));

	if (Params.CachePath.IsEmpty())
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Disabled because no path is configured.", *Params.CacheName);
		return nullptr;
	}
	else if (Params.CachePath == TEXTVIEW("None"))
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Disabled because the path is configured to 'None'", *Params.CacheName);
		return nullptr;
	}

	const auto HasSameCachePath = [&Params](const FFileSystemCacheStore* ActiveStore)
	{
		return FPaths::IsSamePath(Params.CachePath, ActiveStore->CachePath);
	};

	bool bRetryOnFailure;
	do
	{
		bRetryOnFailure = false;

		if (TUniqueLock Lock(ActiveStoresMutex); Algo::FindByPredicate(ActiveStores, HasSameCachePath))
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Process has an existing cache store at path %ls, and the duplicate is being ignored.", *Params.CacheName, *Params.CachePath);
			return nullptr;
		}

		// Skip creation of the cache store if the remote cache directory does not exist.
		if (!Params.bRemote || IFileManager::Get().DirectoryExists(*Params.CachePath))
		{
			FDerivedDataCacheSpeedStats LocalSpeedStats;
			LocalSpeedStats.ReadSpeedMBs = 999;
			LocalSpeedStats.WriteSpeedMBs = 999;
			LocalSpeedStats.LatencyMS = 0;
			if (Params.bSkipSpeedTest || RunInitialSpeedTest(Params, LocalSpeedStats))
			{
				UE_CLOGF(Params.bSkipSpeedTest, LogDerivedDataCache, Log, "%ls: Skipping speed test at path %ls and assuming local performance.", *Params.CacheName, *Params.CachePath);

				FFileSystemCacheStore* CacheStore = new FFileSystemCacheStore(Owner, Params, LocalSpeedStats);
				{
					TUniqueLock Lock(ActiveStoresMutex);
					ActiveStores.Add(CacheStore);
				}
				return CacheStore;
			}

			UE_LOGF(LogDerivedDataCache, Warning, "%ls: No read or write access to %ls, or the speed test was abandoned due to slow progress.", *Params.CacheName, *Params.CachePath);
		}

		// Give the user a chance to retry in case they need to take steps like connecting a network drive.
		if (Params.bPromptIfMissing && !FApp::IsUnattended() && !IS_PROGRAM)
		{
			TStringBuilder<512> Message(InPlace, Params.CacheName, TEXTVIEW(" cache store path "), Params.CachePath,
				TEXTVIEW(" is not available and this cache store will be disabled.\n\nRetry connection to "), Params.CachePath, TEXTVIEW("?"));
			bRetryOnFailure = FPlatformMisc::MessageBoxExt(EAppMsgType::YesNo, *Message, TEXT("Failed to access Derived Data Cache")) == EAppReturnType::Yes;
		}
	}
	while (bRetryOnFailure); //-V654

	UE_LOGF(LogDerivedDataCache, Warning, "%ls: Path %ls is not available and this cache store will be disabled.", *Params.CacheName, *Params.CachePath);
	return nullptr;
}

FFileSystemCacheStore::FFileSystemCacheStore(
	ICacheStoreOwner& Owner,
	const FFileSystemCacheStoreParams& Params,
	const FDerivedDataCacheSpeedStats& InSpeedStats)
	: CachePath(Params.CachePath)
	, StoreOwner(Owner)
	, bRemote(Params.bRemote)
	, bReadOnly(Params.bReadOnly)
	, bTouch(Params.bTouch)
	, MaxFileAgeInDays(Params.MaxFileAgeInDays)
	, SpeedStats(InSpeedStats)
	, DeactivateAtMs(Params.DeactivateAtMs)
{
#if PLATFORM_WINDOWS
	if (!bRemote)
	{
		// Query for remote file systems because the Remote option is new and may not be set consistently,
		// and there is a performance penalty when treating a remote cache as local.
		if (HANDLE CacheHandle = CreateFile(*CachePath, GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr); CacheHandle != INVALID_HANDLE_VALUE)
		{
			FILE_REMOTE_PROTOCOL_INFO RemoteProtocolInfo;
			bRemote = !!GetFileInformationByHandleEx(CacheHandle, FileRemoteProtocolInfo, &RemoteProtocolInfo, sizeof(RemoteProtocolInfo));
			CloseHandle(CacheHandle);
		}
	}
#endif // PLATFORM_WINDOWS

	LastPerformanceEvaluationTicks.store(FDateTime::UtcNow().GetTicks(), std::memory_order_relaxed);

	bool bReadTestPassed = SpeedStats.ReadSpeedMBs > 0.0;
	bool bWriteTestPassed = SpeedStats.WriteSpeedMBs > 0.0;

	// if we failed writes mark this as read only
	bReadOnly = bReadOnly || !bWriteTestPassed;

	const bool bLocalDeactivatedForPerformance = (Params.DeactivateAtMs > 0.f) && (SpeedStats.LatencyMS >= Params.DeactivateAtMs);
	bDeactivatedForPerformance.store(bLocalDeactivatedForPerformance, std::memory_order_relaxed);

	// classify and report on these times
	EBackendSpeedClass SpeedClass;
	if (SpeedStats.LatencyMS < 1)
	{
		SpeedClass = EBackendSpeedClass::Local;
	}
	else if (SpeedStats.LatencyMS <= Params.ConsiderFastAtMs)
	{
		SpeedClass = EBackendSpeedClass::Fast;
	}
	else if (SpeedStats.LatencyMS >= Params.ConsiderSlowAtMs)
	{
		SpeedClass = EBackendSpeedClass::Slow;
	}
	else
	{
		SpeedClass = EBackendSpeedClass::Ok;
	}

	UE_LOGF(LogDerivedDataCache, Display,
		"%ls: Performance: Latency=%.02fms. RandomReadSpeed=%.02fMBs, RandomWriteSpeed=%.02fMBs. Assigned SpeedClass '%ls'",
		*CachePath, SpeedStats.LatencyMS, SpeedStats.ReadSpeedMBs, SpeedStats.WriteSpeedMBs, LexToString(SpeedClass));

	if (bLocalDeactivatedForPerformance)
	{
		if (GIsBuildMachine)
		{
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Performance does not meet minimum criteria. "
					"It will be deactivated until performance measurements improve. "
					"If this is consistent, consider disabling this cache store through "
					"environment variables or other configuration.",
				*CachePath);
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Warning,
				"%ls: Performance does not meet minimum criteria. "
					"It will be deactivated until performance measurements improve. "
					"If this is consistent, consider disabling this cache store through "
					"environment variables or other configuration.",
				*CachePath);
		}
	}

	if (SpeedClass <= EBackendSpeedClass::Slow && !bReadOnly)
	{
		if (GIsBuildMachine)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Access is slow. "
				"'Touch' will be disabled and queries/writes will be limited.", *CachePath);
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Access is slow. "
				"'Touch' will be disabled and queries/writes will be limited.", *CachePath);
		}
		bTouch = false;
		//bReadOnly = true;
	}

	if (FString(FCommandLine::Get()).Contains(TEXT("Run=DerivedDataCache")))
	{
		bTouch = true; // we always touch files when running the DDC commandlet
	}

	if (bTouch)
	{
		UE_LOGF(LogDerivedDataCache, Display, "%ls: Files will be touched when accessed.", *CachePath);
	}

	// Flush the cache if requested.
	if (!bReadOnly && Params.bFlush)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Flush);
		IFileManager::Get().DeleteDirectory(*(CachePath / TEXT("")), /*bRequireExists*/ false, /*bTree*/ true);
	}

	if (Params.bClean && bLocalDeactivatedForPerformance)
	{
		bDeactivationDeferredClean = true;
	}

	if (Params.bClean || Params.bDeleteUnused)
	{
		FFileSystemCacheStoreMaintainerParams* MaintainerParams;
		FFileSystemCacheStoreMaintainerParams LocalMaintainerParams;
		if (bLocalDeactivatedForPerformance)
		{
			DeactivationDeferredMaintainerParams = MakeUnique<FFileSystemCacheStoreMaintainerParams>();
			MaintainerParams = DeactivationDeferredMaintainerParams.Get();
		}
		else
		{
			MaintainerParams = &LocalMaintainerParams;
		}
		MaintainerParams->MaxFileAge = FTimespan::FromDays(MaxFileAgeInDays);
		if (Params.bDeleteUnused)
		{
			MaintainerParams->MaxScanRate = Params.MaxScanRate;
			MaintainerParams->MaxDirectoryScanCount = Params.MaxDirectoryScanCount;
		}
		else
		{
			MaintainerParams->ScanFrequency = FTimespan::MaxValue();
		}
		if (Params.bClean)
		{
			MaintainerParams->TimeToWaitAfterInit = FTimespan::Zero();
		}
		else
		{
			MaintainerParams->TimeToWaitAfterInit = FTimespan::FromSeconds(Params.MaintenanceDelayInSeconds);
		}

		if (!bLocalDeactivatedForPerformance)
		{
			Maintainer = MakeUnique<FFileSystemCacheStoreMaintainer>(*MaintainerParams, CachePath, StoreOwner);

			if (Params.bClean)
			{
				Maintainer->BoostPriority();
				Maintainer->WaitForIdle();
			}
		}
	}

	if (!Params.AccessLogPath.IsEmpty())
	{
		AccessLogWriter.Reset(new FAccessLogWriter(*Params.AccessLogPath, CachePath));
	}

	ECacheStoreFlags Flags = ECacheStoreFlags::None;
	Flags |= Params.bDeleteOnly ? ECacheStoreFlags::None : ECacheStoreFlags::Query;
	Flags |= Params.bDeleteOnly || bReadOnly ? ECacheStoreFlags::None : ECacheStoreFlags::Store;
	Flags |= bRemote ? ECacheStoreFlags::Remote : ECacheStoreFlags::Local;

	StoreOwner.Add({Params.CacheName}, this, Flags);
	if (!Params.bDeleteOnly)
	{
		StoreStats = StoreOwner.CreateStats(this, Flags, TEXTVIEW("File System"), Params.CacheName, CachePath);
	}

	UpdateStatus();

	UE_LOGF(LogDerivedDataCache, Log, "%ls: Using data cache path %ls: %ls", *Params.CacheName, *CachePath,
		EnumHasAnyFlags(Flags, ECacheStoreFlags::Store) ? TEXT("Writable") :
		EnumHasAnyFlags(Flags, ECacheStoreFlags::Query) ? TEXT("ReadOnly") : TEXT("DeleteOnly"));
}

FFileSystemCacheStore::~FFileSystemCacheStore()
{
	if (StoreStats)
	{
		StoreOwner.DestroyStats(StoreStats);
	}

	TUniqueLock Lock(ActiveStoresMutex);
	ActiveStores.Remove(this);
}

bool FFileSystemCacheStore::RunSpeedTest(
	FString CachePath,
	bool bReadOnly,
	bool bSeekTimeOnly,
	double& OutSeekTimeMS,
	double& OutReadSpeedMBs,
	double& OutWriteSpeedMBs,
	std::atomic<uint32>* NumLatencyTestsCompleted,
	std::atomic<bool>* AbandonRequest)
{
	SCOPED_BOOT_TIMING("RunSpeedTest");
	UE_SCOPED_ENGINE_ACTIVITY("Running IO speed test");

	//  files of increasing size. Most DDC data falls within this range so we don't want to skew by reading
	// large amounts of data. Ultimately we care most about latency anyway.
	const int FileSizes[] = { 4, 8, 16, 64, 128, 256 };
	const int NumTestFolders = 2; //(0-9)
	const int FileSizeCount = UE_ARRAY_COUNT(FileSizes);

	bool bWriteTestPassed = true;
	bool bReadTestPassed = true;
	bool bTestDataExists = true;

	double TotalSeekTime = 0;
	double TotalReadTime = 0;
	double TotalWriteTime = 0;
	int TotalDataRead = 0;
	int TotalDataWritten = 0;

	TArray<FString> Paths;
	TArray<FString> MissingFiles;

	MissingFiles.Reserve(NumTestFolders * FileSizeCount);

	const FString TestDataPath = FPaths::Combine(CachePath, TEXT("TestData"));

	// create an upfront map of paths to data size in bytes
	// create the paths we'll use. <path>/0/TestData.dat, <path>/1/TestData.dat etc. If those files don't exist we'll
	// create them which will likely give an invalid result when measuring them now but not in the future...
	TMap<FString, int> TestFileEntries;
	for (int iSize = 0; iSize < FileSizeCount; iSize++)
	{
		// make sure we dont stat/read/write to consecuting files in folders
		for (int iFolder = 0; iFolder < NumTestFolders; iFolder++)
		{
			int FileSizeKB = FileSizes[iSize];
			FString Path = FPaths::Combine(CachePath, TEXT("TestData"), *FString::FromInt(iFolder),
				*FString::Printf(TEXT("TestData_%dkb.dat"), FileSizeKB));
			TestFileEntries.Add(Path, FileSizeKB * 1024);
		}
	}

	// measure latency by checking for the presence of all these files. We'll also track which don't exist..
	const double StatStartTime = FPlatformTime::Seconds();
 	for (auto& KV : TestFileEntries)
	{
		FFileStatData StatData = IFileManager::Get().GetStatData(*KV.Key);

		if (NumLatencyTestsCompleted)
		{
			NumLatencyTestsCompleted->fetch_add(1, std::memory_order_release);
		}

		if (AbandonRequest && AbandonRequest->load(std::memory_order_relaxed))
		{
			const double TotalTestTime = FPlatformTime::Seconds() - StatStartTime;
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Speed tests abandoned on request after %.02f seconds.", *CachePath, TotalTestTime);
			return false;
		}

		if (!StatData.bIsValid || StatData.FileSize != KV.Value)
		{
			MissingFiles.Add(KV.Key);
		}
	}

	// save total stat time
	TotalSeekTime = (FPlatformTime::Seconds() - StatStartTime);

	// calculate seek time here
	OutSeekTimeMS = (TotalSeekTime / TestFileEntries.Num()) * 1000;

	UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Stat tests took %.02f seconds.", *CachePath, TotalSeekTime);

	if (bSeekTimeOnly)
	{
		return true;
	}

	// create any files that were missing
	if (!bReadOnly)
	{
		TArray<uint8> Data;
		for (auto& File : MissingFiles)
		{
			const int DesiredSize = TestFileEntries[File];
			Data.SetNumUninitialized(DesiredSize);

			if (!FFileHelper::SaveArrayToFile(Data, *File, &IFileManager::Get(), FILEWRITE_Silent))
			{
				// Handle the case where something else may have created the path at the same time.
				// This is less about multiple users and more about things like SCW's / UnrealPak
				// that can spin up multiple instances at once.
				if (!IFileManager::Get().FileExists(*File))
				{
					uint32 ErrorCode = FPlatformMisc::GetLastError();
					TCHAR ErrorBuffer[1024];
					FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, ErrorCode);
					UE_LOGF(LogDerivedDataCache, Warning,
						"%ls: Failed to create %ls, this cache store will be read-only. "
							"WriteError: %u (%ls)", *CachePath, *File, ErrorCode, ErrorBuffer);
					bTestDataExists = false;
					bWriteTestPassed = false;
					break;
				}
			}

			if (AbandonRequest && AbandonRequest->load(std::memory_order_relaxed))
			{
				const double TotalTestTime = FPlatformTime::Seconds() - StatStartTime;
				UE_LOGF(LogDerivedDataCache, Log, "%ls: Speed tests abandoned on request after %.02f seconds.", *CachePath, TotalTestTime);
				return false;
			}
		}
	}

	// now read all sizes from random folders
	{
		const int ArraySize = UE_ARRAY_COUNT(FileSizes);
		TArray<uint8> TempData;
		TempData.Empty(FileSizes[ArraySize - 1] * 1024);

		const double ReadStartTime = FPlatformTime::Seconds();

		for (auto& KV : TestFileEntries)
		{
			const int FileSize = KV.Value;
			const FString& FilePath = KV.Key;

			if (!FFileHelper::LoadFileToArray(TempData, *FilePath, FILEREAD_Silent))
			{
				uint32 ErrorCode = FPlatformMisc::GetLastError();
				TCHAR ErrorBuffer[1024];
				FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, ErrorCode);
				UE_LOGF(LogDerivedDataCache, Warning,
					"%ls: Failed to read from %ls. ReadError: %u (%ls)",
					*CachePath, *FilePath, ErrorCode, ErrorBuffer);
				bReadTestPassed = false;
				break;
			}

			TotalDataRead += TempData.Num();
			
			if (AbandonRequest && AbandonRequest->load(std::memory_order_relaxed))
			{
				const double TotalTestTime = FPlatformTime::Seconds() - StatStartTime;
				UE_LOGF(LogDerivedDataCache, Log, "%ls: Speed tests abandoned on request after %.02f seconds.", *CachePath, TotalTestTime);
				return false;
			}
		}

		TotalReadTime = FPlatformTime::Seconds() - ReadStartTime;

		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Read tests %ls and took %.02f seconds.",
			*CachePath, bReadTestPassed ? TEXT("passed") : TEXT("failed"), TotalReadTime);
	}

	// do write tests if or read tests passed and our seeks were below the cut-off
	if (bReadTestPassed && !bReadOnly)
	{
		// do write tests but use a unique folder that is cleaned up afterwards
		FString CustomPath = FPaths::Combine(CachePath, TEXT("TestData"), *FGuid::NewGuid().ToString());

		const int ArraySize = UE_ARRAY_COUNT(FileSizes);
		TArray<uint8> TempData;
		TempData.Empty(FileSizes[ArraySize - 1] * 1024);

		const double WriteStartTime = FPlatformTime::Seconds();

		for (auto& KV : TestFileEntries)
		{
			const int FileSize = KV.Value;
			FString FilePath = KV.Key;

			TempData.SetNumUninitialized(FileSize);

			FilePath = FilePath.Replace(*CachePath, *CustomPath);

			if (!FFileHelper::SaveArrayToFile(TempData, *FilePath, &IFileManager::Get(), FILEWRITE_Silent))
			{
				uint32 ErrorCode = FPlatformMisc::GetLastError();
				TCHAR ErrorBuffer[1024];
				FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, ErrorCode);
				UE_LOGF(LogDerivedDataCache, Warning,
					"%ls: Failed to write to %ls. WriteError: %u (%ls)",
					*CachePath, *FilePath, ErrorCode, ErrorBuffer);
				bWriteTestPassed = false;
				break;
			}

			TotalDataWritten += TempData.Num();
			
			if (AbandonRequest && AbandonRequest->load(std::memory_order_relaxed))
			{
				const double TotalTestTime = FPlatformTime::Seconds() - StatStartTime;
				UE_LOGF(LogDerivedDataCache, Log, "%ls: Speed tests abandoned on request after %.02f seconds.", *CachePath, TotalTestTime);
				return false;
			}
		}

		TotalWriteTime = FPlatformTime::Seconds() - WriteStartTime;

		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Write tests %ls and took %.02f seconds.",
			*CachePath, bWriteTestPassed ? TEXT("passed") : TEXT("failed"), TotalReadTime)

			
		if (AbandonRequest && AbandonRequest->load(std::memory_order_relaxed))
		{
			const double TotalTestTime = FPlatformTime::Seconds() - StatStartTime;
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Speed tests abandoned on request after %.02f seconds.", *CachePath, TotalTestTime);
			return false;
		}
		
		// remove the custom path but do it async as this can be slow on remote drives
		AsyncTask(ENamedThreads::AnyThread, [CustomPath]
		{
			IFileManager::Get().DeleteDirectory(*CustomPath, false, true);
		});
	}

	const double TotalTestTime = FPlatformTime::Seconds() - StatStartTime;

	UE_LOGF(LogDerivedDataCache, Log, "%ls: Speed tests took %.02f seconds.", *CachePath, TotalTestTime);

	// check latency and speed. Read values should always be valid
	OutReadSpeedMBs = (bReadTestPassed ? (TotalDataRead / TotalReadTime) : 0) / (1024 * 1024);
	OutWriteSpeedMBs = (bWriteTestPassed ? (TotalDataWritten / TotalWriteTime) : 0) / (1024 * 1024);

	return bWriteTestPassed || bReadTestPassed;
}

void FFileSystemCacheStore::LegacyStats(FDerivedDataCacheStatsNode& OutNode)
{
	checkNoEntry();
}

void FFileSystemCacheStore::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	for (const FCachePutRequest& Request : Requests)
	{
		FOptionalCacheRecord Record;
		FRequestStats RequestStats;
		RequestStats.Name = Request.Name;
		RequestStats.Bucket = Request.Record.GetKey().Bucket;
		RequestStats.Type = ERequestType::Record;
		RequestStats.Op = ERequestOp::Put;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Put);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_Put);
			FRequestTimer RequestTimer(RequestStats);
			uint64 WriteSize = 0;
			Record = PutCacheRecord(Request.Name, Request.Record, Request.Policy, RequestStats);
			if (Record)
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put complete for %ls from '%ls'",
					*CachePath, *WriteToString<96>(Record.Get().GetKey()), *Request.Name);
				TRACE_COUNTER_INCREMENT(FileSystemDDC_PutHit);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesRead, RequestStats.PhysicalReadSize);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesWritten, RequestStats.PhysicalWriteSize);
			}
		}
		const EStatus Status = Record ? EStatus::Ok : EStatus::Error;
		RequestStats.Status = Status;
		StoreStats->AddRequest(RequestStats);
		if (!Record)
		{
			Record = FCacheRecord(Request.Record.GetKey());
		}
		PRAGMA_DISABLE_DEPRECATION_WARNINGS;
		OnComplete({Request.Name, Record.Get().GetKey(), Record.Get(), Request.UserData, Status});
		PRAGMA_ENABLE_DEPRECATION_WARNINGS;
	}
}

void FFileSystemCacheStore::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	for (const FCacheGetRequest& Request : Requests)
	{
		EStatus Status = EStatus::Error;
		FOptionalCacheRecord Record;
		FRequestStats RequestStats;
		RequestStats.Name = Request.Name;
		RequestStats.Bucket = Request.Key.Bucket;
		RequestStats.Type = ERequestType::Record;
		RequestStats.Op = ERequestOp::Get;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Get);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_Get);
			FRequestTimer RequestTimer(RequestStats);
			if ((Record = GetCacheRecord(Request.Name, Request.Key, Request.Policy, Status, RequestStats)))
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
					*CachePath, *WriteToString<96>(Request.Key), *Request.Name);
				TRACE_COUNTER_INCREMENT(FileSystemDDC_GetHit);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesRead, RequestStats.PhysicalReadSize);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesWritten, RequestStats.PhysicalWriteSize);
			}
			else
			{
				Record = FCacheRecord(Request.Key);
			}
		}
		RequestStats.AddLogicalRead(Record.Get());
		RequestStats.Status = Status;
		StoreStats->AddRequest(RequestStats);
		OnComplete({Request.Name, MoveTemp(Record).Get(), Request.UserData, Status});
	}
}

void FFileSystemCacheStore::PutValue(
	const TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	for (const FCachePutValueRequest& Request : Requests)
	{
		TOptional<FValue> Value;
		FRequestStats RequestStats;
		RequestStats.Name = Request.Name;
		RequestStats.Bucket = Request.Key.Bucket;
		RequestStats.Type = ERequestType::Value;
		RequestStats.Op = ERequestOp::Put;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_PutValue);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_Put);
			FRequestTimer RequestTimer(RequestStats);
			uint64 WriteSize = 0;
			Value = PutCacheValue(Request.Name, Request.Key, Request.Value, Request.Policy, RequestStats);
			if (Value)
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put complete for %ls from '%ls'",
					*CachePath, *WriteToString<96>(Request.Key), *Request.Name);
				TRACE_COUNTER_INCREMENT(FileSystemDDC_PutHit);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesRead, RequestStats.PhysicalReadSize);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesWritten, RequestStats.PhysicalWriteSize);
			}
		}
		const EStatus Status = Value ? EStatus::Ok : EStatus::Error;
		RequestStats.Status = Status;
		StoreStats->AddRequest(RequestStats);
		OnComplete({Request.Name, Request.Key, Value.Get({}), Request.UserData, Status});
	}
}

void FFileSystemCacheStore::GetValue(
	const TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
{
	for (const FCacheGetValueRequest& Request : Requests)
	{
		bool bOk;
		FValue Value;
		FRequestStats RequestStats;
		RequestStats.Name = Request.Name;
		RequestStats.Bucket = Request.Key.Bucket;
		RequestStats.Type = ERequestType::Value;
		RequestStats.Op = ERequestOp::Get;
		{
			FRequestTimer RequestTimer(RequestStats);
			TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_GetValue);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_Get);
			bOk = GetCacheValue(Request.Name, Request.Key, Request.Policy, Value, RequestStats);
			if (bOk)
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
					*CachePath, *WriteToString<96>(Request.Key), *Request.Name);
				TRACE_COUNTER_INCREMENT(FileSystemDDC_GetHit);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesRead, RequestStats.PhysicalReadSize);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesWritten, RequestStats.PhysicalWriteSize);
			}
		}
		RequestStats.AddLogicalRead(Value);
		RequestStats.Status = bOk ? EStatus::Ok : EStatus::Error;
		StoreStats->AddRequest(RequestStats);
		OnComplete({Request.Name, Request.Key, Value, Request.UserData, bOk ? EStatus::Ok : EStatus::Error});
	}
}

void FFileSystemCacheStore::GetChunks(
	const TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	TArray<FCacheGetChunkRequest, TInlineAllocator<16>> SortedRequests(Requests);
	SortedRequests.StableSort(TChunkLess());

	bool bHasValue = false;
	FValue Value;
	FValueId ValueId;
	FCacheKey ValueKey;
	TUniquePtr<FArchive> ValueAr;
	FCompressedBufferReader ValueReader;
	FOptionalCacheRecord Record;
	for (const FCacheGetChunkRequest& Request : SortedRequests)
	{
		EStatus Status = EStatus::Error;
		FSharedBuffer Buffer;
		uint64 RawSize = 0;
		FRequestStats RequestStats;
		RequestStats.Name = Request.Name;
		RequestStats.Bucket = Request.Key.Bucket;
		RequestStats.Type = Request.Id.IsNull() ? ERequestType::Value : ERequestType::Record;
		RequestStats.Op = ERequestOp::GetChunk;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_GetChunks);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_Get);
			const bool bExistsOnly = EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData);
			FRequestTimer RequestTimer(RequestStats);
			if (!(bHasValue && ValueKey == Request.Key && ValueId == Request.Id) || ValueReader.HasSource() < !bExistsOnly)
			{
				ValueReader.ResetSource();
				ValueAr.Reset();
				ValueKey = {};
				ValueId.Reset();
				Value.Reset();
				bHasValue = false;
				if (Request.Id.IsValid())
				{
					if (!(Record && Record.Get().GetKey() == Request.Key))
					{
						FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::None);
						PolicyBuilder.AddValuePolicy(Request.Id, Request.Policy);
						Record.Reset();
						Record = GetCacheRecordOnly(Request.Name, Request.Key, PolicyBuilder.Build(), RequestStats);
					}
					if (Record)
					{
						if (const FValueWithId& ValueWithId = Record.Get().GetValue(Request.Id))
						{
							Value = ValueWithId;
							ValueId = Request.Id;
							ValueKey = Request.Key;
							bHasValue = GetCacheContent(Request.Name, Request.Key, ValueId, Value, Request.Policy, ValueReader, ValueAr, RequestStats);
						}
					}
				}
				else
				{
					ValueKey = Request.Key;
					bHasValue = GetCacheValueOnly(Request.Name, Request.Key, Request.Policy, Value, RequestStats);
					if (bHasValue)
					{
						bHasValue = GetCacheContent(Request.Name, Request.Key, Request.Id, Value, Request.Policy, ValueReader, ValueAr, RequestStats);
					}
				}
			}
			if (bHasValue)
			{
				const uint64 RawOffset = FMath::Min(Value.GetRawSize(), Request.RawOffset);
				RawSize = FMath::Min(Value.GetRawSize() - RawOffset, Request.RawSize);
				TRACE_COUNTER_INCREMENT(FileSystemDDC_GetHit);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesRead, RequestStats.PhysicalReadSize);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesWritten, RequestStats.PhysicalWriteSize);
				if (!bExistsOnly)
				{
					Buffer = ValueReader.Decompress(RawOffset, RawSize);
					RequestStats.LogicalReadSize += Buffer.GetSize();
					if (!Buffer)
					{
						UE_LOGFMT(LogDerivedDataCache, Display,
							"{Cache}: Cache miss with corrupted value {Id} with hash {RawHash} for {Key} from '{Name}'",
							CachePath, Request.Id, Value.GetRawHash(), Request.Key, Request.Name);
						DeleteCacheContent(Request.Name, Value, ValueReader, ValueAr);
					}
				}
				const bool bRawHashMatches = Request.RawHash.IsZero() || Request.RawHash == Value.GetRawHash();
				Status = bRawHashMatches && (bExistsOnly || Buffer.GetSize() == RawSize) ? EStatus::Ok : EStatus::Error;
				UE_CLOGFMT(!bRawHashMatches, LogDerivedDataCache, Verbose,
					"{Cache}: Cache miss with mismatched value {Id} received hash {ReceivedHash} when expected hash {RawHash} for {Key} from '{Name}'",
					CachePath, Request.Id, Value.GetRawHash(), Request.RawHash, Request.Key, Request.Name);
			}
		}
		RequestStats.Status = Status;
		StoreStats->AddRequest(RequestStats);
		UE_CLOGF(Status == EStatus::Ok, LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
			*CachePath, *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);
		OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset,
			RawSize, Value.GetRawHash(), MoveTemp(Buffer), Request.UserData, Status});
	}
}

FOptionalCacheRecord FFileSystemCacheStore::PutCacheRecord(
	const FStringView Name,
	const FCacheRecord& Record,
	const FCacheRecordPolicy& Policy,
	FRequestStats& Stats)
{
	const bool bLocalDeactivatedForPerformance = IsDeactivatedForPerformance();
	if (bLocalDeactivatedForPerformance || bReadOnly)
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose,
			"%ls: Skipped put of %ls from '%.*ls' because this cache store is %ls",
			*CachePath,
			*WriteToString<96>(Record.GetKey()),
			Name.Len(),
			Name.GetData(),
			bLocalDeactivatedForPerformance ? TEXT("deactivated due to low performance") : TEXT("read-only"));
		return {};
	}

	const FCacheKey& Key = Record.GetKey();
	const ECachePolicy RecordPolicy = Policy.GetRecordPolicy();

	// Skip the request if storing to the cache is disabled.
	const ECachePolicy StoreFlag = bRemote ? ECachePolicy::StoreRemote : ECachePolicy::StoreLocal;
	if (!EnumHasAnyFlags(RecordPolicy, StoreFlag))
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped put of %ls from '%.*ls' due to cache policy",
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return {};
	}

	TStringBuilder<256> Path;
	BuildCachePackagePath(Key, Path);

	// Check if there is an existing record package.
	FOptionalCacheRecord ExistingRecord;
	FCbPackage ExistingPackage;
	const ECachePolicy QueryFlag = bRemote ? ECachePolicy::QueryRemote : ECachePolicy::QueryLocal;
	bool bReplaceExisting = !EnumHasAnyFlags(RecordPolicy, QueryFlag);
	bool bSavePackage = bReplaceExisting;
	if (const bool bLoadPackage = !bReplaceExisting || !Algo::AllOf(Record.GetValues(), &FValue::HasData))
	{
		// Load the existing package to take its attachments into account.
		// Save the new package if there is no existing package or it fails to load.
		bSavePackage |= !LoadFileWithHash(Path, Name, Stats, [&ExistingPackage](FArchive& Ar) { ExistingPackage.TryLoad(Ar); });
		if (!bSavePackage)
		{
			// Save the new package if the existing package is invalid.
			ExistingRecord = FCacheRecord::Load(ExistingPackage);
			bSavePackage |= !ExistingRecord;
			if (ExistingRecord && !Algo::Compare(ExistingRecord.Get().GetValues(), Record.GetValues()))
			{
				// Content differs between the existing record and the new record.
				const bool bReportConflicts = !EnumHasAnyFlags(Policy.GetBasePolicy(), ECachePolicy::NonDeterministic) &&
					Algo::NoneOf(Policy.GetValuePolicies(), [](const FCacheValuePolicy& Value) { return EnumHasAnyFlags(Value.Policy, ECachePolicy::NonDeterministic); });
				const auto MakeCompareValue = [&Policy](const FValueWithId& Value) -> FValueWithId
				{
					return !EnumHasAnyFlags(Policy.GetValuePolicy(Value.GetId()), ECachePolicy::NonDeterministic)
						? Value : FValueWithId(Value.GetId(), FIoHash::Zero, 0);
				};
				UE_CLOGF(bReportConflicts && !Algo::CompareBy(ExistingRecord.Get().GetValues(), Record.GetValues(), MakeCompareValue),
					LogDerivedDataCache, Display, "%ls: Cache put found non-deterministic record for %ls from '%.*ls'",
					*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
				const auto HasValueContent = [this, Name, &Key, &Stats](const FValueWithId& Value) -> bool
				{
					if (!Value.HasData() && !GetCacheContentExists(Key, Value.GetRawHash(), Stats))
					{
						UE_LOGF(LogDerivedDataCache, Log,
							"%ls: Cache put of non-deterministic record will overwrite existing record due to "
							     "missing value %ls with hash %ls for %ls from '%.*ls'",
							*CachePath, *WriteToString<32>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()),
							*WriteToString<96>(Key), Name.Len(), Name.GetData());
						return false;
					}
					return true;
				};
				// Save the new package because the existing package differs and is missing content.
				bSavePackage |= !Algo::AllOf(ExistingRecord.Get().GetValues(), HasValueContent);
			}
			bReplaceExisting |= bSavePackage;
		}
	}

	// Serialize the record to a package and remove attachments that will be stored externally.
	FCbPackage Package = Record.Save();
	TArray<FCompressedBuffer, TInlineAllocator<8>> ExternalContent;
	if (ExistingPackage && !bSavePackage)
	{
		// Mirror the existing internal/external attachment storage.
		TArray<FCompressedBuffer, TInlineAllocator<8>> AllContent;
		Algo::Transform(Package.GetAttachments(), AllContent, &FCbAttachment::AsCompressedBinary);
		for (FCompressedBuffer& Content : AllContent)
		{
			const FIoHash RawHash = Content.GetRawHash();
			if (!ExistingPackage.FindAttachment(RawHash))
			{
				Package.RemoveAttachment(RawHash);
				ExternalContent.Add(MoveTemp(Content));
			}
		}
	}
	else
	{
		// Attempt to copy missing attachments from the existing package.
		if (ExistingPackage)
		{
			for (const FValue& Value : Record.GetValues())
			{
				if (!Value.HasData())
				{
					if (const FCbAttachment* Attachment = ExistingPackage.FindAttachment(Value.GetRawHash()))
					{
						Package.AddAttachment(*Attachment);
					}
				}
			}
		}

		// Remove the largest attachments from the package until it fits within the size limits.
		TArray<FCompressedBuffer, TInlineAllocator<8>> AllContent;
		Algo::Transform(Package.GetAttachments(), AllContent, &FCbAttachment::AsCompressedBinary);
		uint64 TotalSize = Algo::TransformAccumulate(AllContent, &FCompressedBuffer::GetCompressedSize, uint64(0));
		const uint64 MaxSize = (Record.GetValues().Num() == 1 ? MaxValueSizeKB : MaxRecordSizeKB) * 1024;
		if (TotalSize > MaxSize)
		{
			Algo::StableSortBy(AllContent, &FCompressedBuffer::GetCompressedSize, TGreater<>());
			for (FCompressedBuffer& Content : AllContent)
			{
				const uint64 CompressedSize = Content.GetCompressedSize();
				Package.RemoveAttachment(Content.GetRawHash());
				ExternalContent.Add(MoveTemp(Content));
				TotalSize -= CompressedSize;
				if (TotalSize <= MaxSize)
				{
					break;
				}
			}
		}
	}

	// Verify that missing content exists in storage.
	if (!EnumHasAnyFlags(RecordPolicy, ECachePolicy::PartialRecord))
	{
		for (const FValueWithId& Value : Record.GetValues())
		{
			if (!Value.HasData() &&
				!ExistingPackage.FindAttachment(Value.GetRawHash()) &&
				!GetCacheContentExists(Key, Value.GetRawHash(), Stats))
			{
				UE_LOGF(LogDerivedDataCache, Verbose,
					"%ls: Cache put failed due to missing value %ls with hash %ls for put of %ls from '%.*ls'",
					*CachePath, *WriteToString<32>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()),
					*WriteToString<96>(Key), Name.Len(), Name.GetData());
				return {};
			}
		}
	}

	// Save the external content to storage.
	for (FCompressedBuffer& Content : ExternalContent)
	{
		if (!PutCacheContent(Name, Content, Stats))
		{
			return {};
		}
	}

	// Save the record package to storage.
	const auto WritePackage = [&Package](FArchive& Ar) { Package.Save(Ar); };
	if (bSavePackage)
	{
		if (!SaveFileWithHash(Path, Name, Stats, WritePackage, bReplaceExisting))
		{
			return {};
		}
		if (const FCbObject& Meta = Record.GetMeta())
		{
			Stats.LogicalWriteSize += Meta.GetSize();
		}
		Stats.LogicalWriteSize += Algo::TransformAccumulate(Package.GetAttachments(),
			[](const FCbAttachment& Attachment) { return Attachment.AsCompressedBinary().GetRawSize(); }, uint64(0));
	}

	if (AccessLogWriter)
	{
		AccessLogWriter->Append(Key, Path);
	}

	// Load the value content if requested.
	EStatus Status;
	const FCacheRecordPolicy GetPolicy = Policy.Transform([](ECachePolicy P) { return P | ECachePolicy::Query; });
	return GetCacheRecordContent(Name, bSavePackage ? Record : ExistingRecord.Get(), GetPolicy, Status, Stats);
}

FOptionalCacheRecord FFileSystemCacheStore::GetCacheRecordOnly(
	const FStringView Name,
	const FCacheKey& Key,
	const FCacheRecordPolicy& Policy,
	FRequestStats& Stats)
{
	// Skip the request if querying the cache is disabled.
	const ECachePolicy QueryFlag = bRemote ? ECachePolicy::QueryRemote : ECachePolicy::QueryLocal;
	const bool bLocalDeactivatedForPerformance = IsDeactivatedForPerformance();
	if (bLocalDeactivatedForPerformance || !EnumHasAnyFlags(Policy.GetRecordPolicy(), QueryFlag))
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped get of %ls from '%.*ls' %ls",
			*CachePath,
			*WriteToString<96>(Key),
			Name.Len(),
			Name.GetData(),
			bLocalDeactivatedForPerformance ?
				TEXT("because this cache store is deactivated due to low performance") :
				TEXT("due to cache policy"));
		return FOptionalCacheRecord();
	}

	TStringBuilder<256> Path;
	BuildCachePackagePath(Key, Path);

	bool bDeletePackage = true;
	ON_SCOPE_EXIT
	{
		if (bDeletePackage && !bReadOnly)
		{
			IFileManager::Get().Delete(*Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
		}
	};

	FOptionalCacheRecord Record;
	{
		FCbPackage Package;
		if (!LoadFileWithHash(Path, Name, Stats, [&Package](FArchive& Ar) { Package.TryLoad(Ar); }))
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with missing package for %ls from '%.*ls'",
				*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
			return Record;
		}

		if (ValidateCompactBinary(Package, ECbValidateMode::Default | ECbValidateMode::Package) != ECbValidateError::None)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid package for %ls from '%.*ls'",
				*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
			return Record;
		}

		Record = FCacheRecord::Load(Package);
		if (Record.IsNull())
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with record load failure for %ls from '%.*ls'",
				*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
			return Record;
		}
	}

	if (AccessLogWriter)
	{
		AccessLogWriter->Append(Key, Path);
	}

	bDeletePackage = false;
	return Record;
}

FOptionalCacheRecord FFileSystemCacheStore::GetCacheRecord(
	const FStringView Name,
	const FCacheKey& Key,
	const FCacheRecordPolicy& Policy,
	EStatus& OutStatus,
	FRequestStats& Stats)
{
	FOptionalCacheRecord Record = GetCacheRecordOnly(Name, Key, Policy, Stats);
	if (Record)
	{
		return GetCacheRecordContent(Name, Record.Get(), Policy, OutStatus, Stats);
	}
	OutStatus = EStatus::Error;
	return Record;
}

FOptionalCacheRecord FFileSystemCacheStore::GetCacheRecordContent(
	const FStringView Name,
	const FCacheRecord& Record,
	const FCacheRecordPolicy& Policy,
	EStatus& OutStatus,
	FRequestStats& Stats)
{
	OutStatus = EStatus::Ok;

	const FCacheKey& Key = Record.GetKey();
	FCbObject Meta;
	TArray<FValueWithId> Values(Record.GetValues());

	const ECachePolicy RecordPolicy = Policy.GetRecordPolicy();
	if (!EnumHasAnyFlags(RecordPolicy, ECachePolicy::SkipMeta))
	{
		Meta = Record.GetMeta();
	}

	for (FValueWithId& Value : Values)
	{
		const FValueId& Id = Value.GetId();
		const ECachePolicy ValuePolicy = Policy.GetValuePolicy(Id);
		if (!GetCacheContent(Name, Key, Id, Value, ValuePolicy, Value, Stats))
		{
			OutStatus = EStatus::Error;
			if (!EnumHasAnyFlags(RecordPolicy, ECachePolicy::PartialRecord))
			{
				return {};
			}
		}
	}

	return FCacheRecord::CreateByMove(Key, MoveTemp(Meta), MoveTemp(Values));
}

TOptional<FValue> FFileSystemCacheStore::PutCacheValue(
	const FStringView Name,
	const FCacheKey& Key,
	const FValue& Value,
	const ECachePolicy Policy,
	FRequestStats& Stats)
{
	const bool bLocalDeactivatedForPerformance = IsDeactivatedForPerformance();
	if (bLocalDeactivatedForPerformance || bReadOnly)
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose,
			"%ls: Skipped put of %ls from '%.*ls' because this cache store is %ls",
			*CachePath,
			*WriteToString<96>(Key),
			Name.Len(),
			Name.GetData(),
			bLocalDeactivatedForPerformance ?
				TEXT("deactivated due to low performance") :
				TEXT("read-only"));
		return {};
	}

	// Skip the request if storing to the cache is disabled.
	const ECachePolicy StoreFlag = bRemote ? ECachePolicy::StoreRemote : ECachePolicy::StoreLocal;
	if (!EnumHasAnyFlags(Policy, StoreFlag))
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped put of %ls from '%.*ls' due to cache policy",
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return {};
	}

	// Check if there is an existing value package.
	FValue ExistingValue;
	FCbPackage ExistingPackage;
	TStringBuilder<256> Path;
	BuildCachePackagePath(Key, Path);
	const ECachePolicy QueryFlag = bRemote ? ECachePolicy::QueryRemote : ECachePolicy::QueryLocal;
	bool bReplaceExisting = !EnumHasAnyFlags(Policy, QueryFlag);
	bool bSavePackage = bReplaceExisting;
	if (const bool bLoadPackage = !bReplaceExisting || !Value.HasData())
	{
		// Load the existing package to take its attachments into account.
		// Save the new package if there is no existing package or it fails to load.
		bSavePackage |= !LoadFileWithHash(Path, Name, Stats, [&ExistingPackage](FArchive& Ar) { ExistingPackage.TryLoad(Ar); });
		if (!bSavePackage)
		{
			const FCbObjectView Object = ExistingPackage.GetObject();
			const FIoHash RawHash = Object["RawHash"].AsHash();
			const uint64 RawSize = Object["RawSize"].AsUInt64(MAX_uint64);
			ExistingValue = FValue(RawHash, RawSize);
			if (RawHash.IsZero() || RawSize == MAX_uint64)
			{
				// Save the new package because the existing package is invalid.
				UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache put found invalid existing value for %ls from '%.*ls'",
					*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
				bSavePackage = true;
			}
			else if (!(RawHash == Value.GetRawHash() && RawSize == Value.GetRawSize()))
			{
				// Content differs between the existing value and the new value.
				UE_CLOGF(!EnumHasAnyFlags(Policy, ECachePolicy::NonDeterministic),
					LogDerivedDataCache, Display, "%ls: Cache put found non-deterministic value "
					"with new hash %ls and existing hash %ls for %ls from '%.*ls'",
					*CachePath, *WriteToString<48>(Value.GetRawHash()), *WriteToString<48>(RawHash),
					*WriteToString<96>(Key), Name.Len(), Name.GetData());
				if (!ExistingPackage.FindAttachment(RawHash) && !GetCacheContentExists(Key, RawHash, Stats))
				{
					// Save the new package because the existing package differs and is missing content.
					UE_LOGF(LogDerivedDataCache, Log,
						"%ls: Cache put of non-deterministic value will overwrite existing value due to "
						"missing value with hash %ls for %ls from '%.*ls'",
						*CachePath, *WriteToString<48>(RawHash), *WriteToString<96>(Key), Name.Len(), Name.GetData());
					bSavePackage = true;
				}
			}
			bReplaceExisting |= bSavePackage;
		}
	}

	// Save the value to a package and save the data to external content depending on its size.
	FCbPackage Package;
	uint64 LogicalPackageSize = 0;
	TArray<FCompressedBuffer, TInlineAllocator<1>> ExternalContent;
	if (ExistingPackage && !bSavePackage)
	{
		if (Value.HasData() && !ExistingPackage.FindAttachment(Value.GetRawHash()))
		{
			ExternalContent.Add(Value.GetData());
		}
	}
	else
	{
		FCbWriter Writer;
		Writer.BeginObject();
		Writer.AddBinaryAttachment("RawHash", Value.GetRawHash());
		Writer.AddInteger("RawSize", Value.GetRawSize());
		Writer.EndObject();

		Package.SetObject(Writer.Save().AsObject());
		if (!Value.HasData())
		{
			// Verify that the content exists in storage.
			if (!GetCacheContentExists(Key, Value.GetRawHash(), Stats))
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put failed due to missing data for put of %ls from '%.*ls'",
					*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
				return {};
			}
		}
		else if (Value.GetData().GetCompressedSize() <= MaxValueSizeKB * 1024)
		{
			// Store the content in the package.
			LogicalPackageSize += Value.GetRawSize();
			Package.AddAttachment(FCbAttachment(Value.GetData()));
		}
		else
		{
			ExternalContent.Add(Value.GetData());
		}
	}

	// Save the external content to storage.
	for (FCompressedBuffer& Content : ExternalContent)
	{
		if (!PutCacheContent(Name, Content, Stats))
		{
			return {};
		}
	}

	// Save the value package to storage.
	const auto WritePackage = [&Package](FArchive& Ar) { Package.Save(Ar); };
	if (bSavePackage)
	{
		if (!SaveFileWithHash(Path, Name, Stats, WritePackage, bReplaceExisting))
		{
			return {};
		}
		Stats.LogicalWriteSize += LogicalPackageSize;
	}

	if (AccessLogWriter)
	{
		AccessLogWriter->Append(Key, Path);
	}

	// Load the value content if requested.
	if (!bSavePackage)
	{
		if (EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
		{
			return ExistingValue;
		}
		if (const FCbAttachment* const Attachment = ExistingPackage.FindAttachment(ExistingValue.GetRawHash()))
		{
			const FCompressedBuffer& Data = Attachment->AsCompressedBinary();
			if (Data.GetRawHash() == ExistingValue.GetRawHash() && Data.GetRawSize() == ExistingValue.GetRawSize())
			{
				return FValue(Data);
			}
		}
	}
	FValue OutValue;
	if (GetCacheContent(Name, Key, {}, bSavePackage ? Value : ExistingValue, Policy | ECachePolicy::Query, OutValue, Stats))
	{
		return OutValue;
	}
	return {};
}

bool FFileSystemCacheStore::GetCacheValueOnly(
	const FStringView Name,
	const FCacheKey& Key,
	const ECachePolicy Policy,
	FValue& OutValue,
	FRequestStats& Stats)
{
	// Skip the request if querying the cache is disabled.
	const ECachePolicy QueryFlag = bRemote ? ECachePolicy::QueryRemote : ECachePolicy::QueryLocal;
	const bool bLocalDeactivatedForPerformance = IsDeactivatedForPerformance();
	if (bLocalDeactivatedForPerformance || !EnumHasAnyFlags(Policy, QueryFlag))
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped get of %ls from '%.*ls' %ls",
			*CachePath,
			*WriteToString<96>(Key),
			Name.Len(),
			Name.GetData(),
			bLocalDeactivatedForPerformance ?
			TEXT("because this cache store is deactivated due to low performance") :
			TEXT("due to cache policy"));
		return false;
	}

	TStringBuilder<256> Path;
	BuildCachePackagePath(Key, Path);

	bool bDeletePackage = true;
	ON_SCOPE_EXIT
	{
		if (bDeletePackage && !bReadOnly)
		{
			IFileManager::Get().Delete(*Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
		}
	};

	FCbPackage Package;
	if (!LoadFileWithHash(Path, Name, Stats, [&Package](FArchive& Ar) { Package.TryLoad(Ar); }))
	{
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with missing package for %ls from '%.*ls'",
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return false;
	}

	if (ValidateCompactBinary(Package, ECbValidateMode::Default | ECbValidateMode::Package) != ECbValidateError::None)
	{
		UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid package for %ls from '%.*ls'",
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return false;
	}

	const FCbObjectView Object = Package.GetObject();
	const FIoHash RawHash = Object["RawHash"].AsHash();
	const uint64 RawSize = Object["RawSize"].AsUInt64(MAX_uint64);
	if (RawHash.IsZero() || RawSize == MAX_uint64)
	{
		UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid value for %ls from '%.*ls'",
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return false;
	}

	if (const FCbAttachment* const Attachment = Package.FindAttachment(RawHash))
	{
		const FCompressedBuffer& Data = Attachment->AsCompressedBinary();
		if (Data.GetRawHash() != RawHash || Data.GetRawSize() != RawSize)
		{
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Cache miss with invalid value attachment for %ls from '%.*ls'",
				*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
			return false;
		}
		OutValue = FValue(Data);
	}
	else
	{
		OutValue = FValue(RawHash, RawSize);
	}

	if (AccessLogWriter)
	{
		AccessLogWriter->Append(Key, Path);
	}

	bDeletePackage = false;
	return true;
}

bool FFileSystemCacheStore::GetCacheValue(
	const FStringView Name,
	const FCacheKey& Key,
	const ECachePolicy Policy,
	FValue& OutValue,
	FRequestStats& Stats)
{
	return GetCacheValueOnly(Name, Key, Policy, OutValue, Stats) && GetCacheContent(Name, Key, {}, OutValue, Policy, OutValue, Stats);
}

bool FFileSystemCacheStore::PutCacheContent(
	const FStringView Name,
	const FCompressedBuffer& Content,
	FRequestStats& Stats) const
{
	const FIoHash& RawHash = Content.GetRawHash();
	TStringBuilder<256> Path;
	BuildCacheContentPath(RawHash, Path);
	const auto WriteContent = [&Content](FArchive& Ar) { Content.Save(Ar); };
	if (!FileExists(Path, Stats))
	{
		if (!SaveFileWithHash(Path, Name, Stats, WriteContent))
		{
			return false;
		}
		Stats.LogicalWriteSize += Content.GetRawSize();
	}
	if (AccessLogWriter)
	{
		AccessLogWriter->Append(RawHash, Path);
	}
	return true;
}

bool FFileSystemCacheStore::GetCacheContentExists(const FCacheKey& Key, const FIoHash& RawHash, FRequestStats& Stats) const
{
	TStringBuilder<256> Path;
	BuildCacheContentPath(RawHash, Path);
	return FileExists(Path, Stats);
}

bool FFileSystemCacheStore::GetCacheContent(
	const FStringView Name,
	const FCacheKey& Key,
	const FValueId& Id,
	const FValue& Value,
	const ECachePolicy Policy,
	FValue& OutValue,
	FRequestStats& Stats) const
{
	if (!EnumHasAnyFlags(Policy, ECachePolicy::Query))
	{
		OutValue = Value.RemoveData();
		return true;
	}

	if (Value.HasData())
	{
		OutValue = ApplySkipPolicy(Value, Policy);
		return true;
	}

	const FIoHash& RawHash = Value.GetRawHash();

	TStringBuilder<256> Path;
	BuildCacheContentPath(RawHash, Path);
	if (EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
	{
		if (FileExists(Path, Stats))
		{
			if (AccessLogWriter)
			{
				AccessLogWriter->Append(RawHash, Path);
			}
			OutValue = Value;
			return true;
		}
	}
	else
	{
		FCompressedBuffer CompressedBuffer;
		if (LoadFileWithHash(Path, Name, Stats, [&CompressedBuffer](FArchive& Ar) { CompressedBuffer = FCompressedBuffer::Load(Ar); }))
		{
			if (CompressedBuffer.GetRawHash() == RawHash)
			{
				if (AccessLogWriter)
				{
					AccessLogWriter->Append(RawHash, Path);
				}
				OutValue = FValue(MoveTemp(CompressedBuffer));
				return true;
			}
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Cache miss with corrupted value %ls with hash %ls for %ls from '%.*ls'",
				*CachePath, *WriteToString<32>(Id), *WriteToString<48>(RawHash),
				*WriteToString<96>(Key), Name.Len(), Name.GetData());
			DeleteCacheContent(Name, Value);
			return false;
		}
	}

	UE_LOGF(LogDerivedDataCache, Verbose,
		"%ls: Cache miss with missing value %ls with hash %ls for %ls from '%.*ls'",
		*CachePath, *WriteToString<16>(Id), *WriteToString<48>(RawHash), *WriteToString<96>(Key),
		Name.Len(), Name.GetData());
	return false;
}

bool FFileSystemCacheStore::GetCacheContent(
	const FStringView Name,
	const FCacheKey& Key,
	const FValueId& Id,
	const FValue& Value,
	const ECachePolicy Policy,
	FCompressedBufferReader& Reader,
	TUniquePtr<FArchive>& OutArchive,
	FRequestStats& Stats) const
{
	class FStatsArchive final : TUniquePtr<FArchive>, public FArchiveProxy
	{
	public:
		FStatsArchive(FArchive& InArchive, FRequestStats& InStats)
			: TUniquePtr<FArchive>(&InArchive)
			, FArchiveProxy(InArchive)
			, Stats(InStats)
		{
		}

		void Serialize(void* V, int64 Length) final
		{
			Stats.PhysicalReadSize += uint64(Length);
			FArchiveProxy::Serialize(V, Length);
		}

	private:
		FRequestStats& Stats;
	};

	if (!EnumHasAnyFlags(Policy, ECachePolicy::Query))
	{
		return true;
	}

	if (Value.HasData())
	{
		if (!EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
		{
			Reader.SetSource(Value.GetData());
		}
		OutArchive.Reset();
		return true;
	}

	const FIoHash& RawHash = Value.GetRawHash();

	TStringBuilder<256> Path;
	BuildCacheContentPath(RawHash, Path);
	if (EnumHasAllFlags(Policy, ECachePolicy::SkipData))
	{
		if (FileExists(Path, Stats))
		{
			if (AccessLogWriter)
			{
				AccessLogWriter->Append(RawHash, Path);
			}
			return true;
		}
	}
	else
	{
		OutArchive = OpenFileRead(Path, Name, Stats);
		if (OutArchive)
		{
			OutArchive.Reset(new FStatsArchive(*OutArchive.Release(), Stats));
			UE_LOGF(LogDerivedDataCache, VeryVerbose,
				"%ls: Opened %ls from '%.*ls'",
				*CachePath, *Path, Name.Len(), Name.GetData());
			Reader.SetSource(*OutArchive);
			if (Reader.GetRawHash() == RawHash)
			{
				if (AccessLogWriter)
				{
					AccessLogWriter->Append(RawHash, Path);
				}
				return true;
			}
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Cache miss with corrupted value %ls with hash %ls for %ls from '%.*ls'",
				*CachePath, *WriteToString<16>(Id), *WriteToString<48>(RawHash),
				*WriteToString<96>(Key), Name.Len(), Name.GetData());
			DeleteCacheContent(Name, Value, Reader, OutArchive);
			return false;
		}
	}

	UE_LOGF(LogDerivedDataCache, Verbose,
		"%ls: Cache miss with missing value %ls with hash %ls for %ls from '%.*ls'",
		*CachePath, *WriteToString<16>(Id), *WriteToString<48>(RawHash), *WriteToString<96>(Key),
		Name.Len(), Name.GetData());
	return false;
}

void FFileSystemCacheStore::DeleteCacheContent(const FStringView Name, const FValue& Value) const
{
	if (!bReadOnly)
	{
		TStringBuilder<256> Path;
		BuildCacheContentPath(Value.GetRawHash(), Path);
		if (IFileManager::Get().Delete(*Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true))
		{
			UE_LOGFMT(LogDerivedDataCache, VeryVerbose, "{Cache}: Deleted {Path} from '{Name}'", CachePath, Path, Name);
		}
	}
}

void FFileSystemCacheStore::DeleteCacheContent(
	const FStringView Name,
	const FValue& Value,
	FCompressedBufferReader& Reader,
	TUniquePtr<FArchive>& OutArchive) const
{
	if (OutArchive)
	{
		Reader.ResetSource();
		OutArchive.Reset();
	}
	DeleteCacheContent(Name, Value);
}

void FFileSystemCacheStore::BuildCachePackagePath(const FCacheKey& CacheKey, FStringBuilderBase& Path) const
{
	Path << CachePath << TEXT('/');
	BuildPathForCachePackage(CacheKey, Path);
}

void FFileSystemCacheStore::BuildCacheContentPath(const FIoHash& RawHash, FStringBuilderBase& Path) const
{
	Path << CachePath << TEXT('/');
	BuildPathForCacheContent(RawHash, Path);
}

bool FFileSystemCacheStore::SaveFileWithHash(
	FStringBuilderBase& Path,
	const FStringView DebugName,
	FRequestStats& Stats,
	const TFunctionRef<void (FArchive&)> WriteFunction,
	const bool bReplaceExisting) const
{
	return SaveFile(Path, DebugName, Stats, [&WriteFunction](FArchive& Ar)
	{
		THashingArchiveProxy<FBlake3> HashAr(Ar);
		WriteFunction(HashAr);
		FBlake3Hash Hash = HashAr.GetHash();
		Ar << Hash;
	}, bReplaceExisting);
}

bool FFileSystemCacheStore::LoadFileWithHash(
	FStringBuilderBase& Path,
	const FStringView DebugName,
	FRequestStats& Stats,
	const TFunctionRef<void (FArchive& Ar)> ReadFunction) const
{
	return LoadFile(Path, DebugName, Stats, [this, &Path, &DebugName, &ReadFunction](FArchive& Ar)
	{
		THashingArchiveProxy<FBlake3> HashAr(Ar);
		ReadFunction(HashAr);
		const FBlake3Hash Hash = HashAr.GetHash();
		FBlake3Hash SavedHash;
		Ar << SavedHash;

		if (Hash != SavedHash && !Ar.IsError())
		{
			Ar.SetError();
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: File %ls from '%.*ls' is corrupted and has hash %ls when %ls is expected.",
				*CachePath, *Path, DebugName.Len(), DebugName.GetData(),
				*WriteToString<80>(Hash), *WriteToString<80>(SavedHash));
		}
	});
}

bool FFileSystemCacheStore::SaveFile(
	FStringBuilderBase& Path,
	const FStringView DebugName,
	FRequestStats& Stats,
	const TFunctionRef<void (FArchive&)> WriteFunction,
	const bool bReplaceExisting) const
{
	const double StartTime = FPlatformTime::Seconds();

	TStringBuilder<256> TempPath;
	TempPath << FPathViews::GetPath(Path) << TEXT("/Temp.") << FGuid::NewGuid();

	TUniquePtr<FArchive> Ar = OpenFileWrite(TempPath, DebugName, Stats);
	if (!Ar)
	{
		UE_LOGF(LogDerivedDataCache, Warning,
			"%ls: Failed to open temp file %ls for writing when saving %ls from '%.*ls'. Error 0x%08x.",
			*CachePath, *TempPath, *Path, DebugName.Len(), DebugName.GetData(), FPlatformMisc::GetLastError());
		return false;
	}

	WriteFunction(*Ar);
	const int64 WriteSize = Ar->Tell();

	if (!Ar->Close() || WriteSize == 0 || WriteSize != IFileManager::Get().FileSize(*TempPath))
	{
		UE_LOG(LogDerivedDataCache, Warning,
			TEXT("%s: Failed to write to temp file %s when saving %s from '%.*s'. Error 0x%08x. "
			"File is %" INT64_FMT " bytes when %" INT64_FMT " bytes are expected."),
			*CachePath, *TempPath, *Path, DebugName.Len(), DebugName.GetData(), FPlatformMisc::GetLastError(),
			IFileManager::Get().FileSize(*TempPath), WriteSize);
		IFileManager::Get().Delete(*TempPath, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
		return false;
	}

	if (!IFileManager::Get().Move(*Path, *TempPath, bReplaceExisting, /*bEvenIfReadOnly*/ false, /*bAttributes*/ false, /*bDoNotRetryOrError*/ true))
	{
		UE_LOGF(LogDerivedDataCache, Log,
			"%ls: Move collision when writing file %ls from '%.*ls'.",
			*CachePath, *Path, DebugName.Len(), DebugName.GetData());
		IFileManager::Get().Delete(*TempPath, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
	}

	const double WriteDuration = FPlatformTime::Seconds() - StartTime;
	const double WriteSpeed = WriteDuration > 0.001 ? (double(WriteSize) / WriteDuration) / (1024.0 * 1024.0) : 0.0;
	UE_LOG(LogDerivedDataCache, VeryVerbose,
		TEXT("%s: Saved %s from '%.*s' (%" INT64_FMT " bytes, %.02f secs, %.2f MiB/s)"),
		*CachePath, *Path, DebugName.Len(), DebugName.GetData(), WriteSize, WriteDuration, WriteSpeed);

	if (WriteSize > 0)
	{
		Stats.PhysicalWriteSize += uint64(WriteSize);
	}

	return true;
}

bool FFileSystemCacheStore::LoadFile(
	FStringBuilderBase& Path,
	const FStringView DebugName,
	FRequestStats& Stats,
	const TFunctionRef<void (FArchive& Ar)> ReadFunction) const
{
	const double StartTime = FPlatformTime::Seconds();

	TUniquePtr<FArchive> Ar = OpenFileRead(Path, DebugName, Stats);
	if (!Ar)
	{
		return false;
	}

	ReadFunction(*Ar);
	const int64 ReadSize = Ar->Tell();
	const bool bError = !Ar->Close();

	if (bError)
	{
		UE_LOGF(LogDerivedDataCache, Display,
			"%ls: Failed to load file %ls from '%.*ls'.",
			*CachePath, *Path, DebugName.Len(), DebugName.GetData());

		if (!bReadOnly)
		{
			IFileManager::Get().Delete(*Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
		}
	}
	else
	{
		const double ReadDuration = FPlatformTime::Seconds() - StartTime;
		const double ReadSpeed = ReadDuration > 0.001 ? (double(ReadSize) / ReadDuration) / (1024.0 * 1024.0) : 0.0;

		UE_LOG(LogDerivedDataCache, VeryVerbose,
			TEXT("%s: Loaded %s from '%.*s' (%" INT64_FMT " bytes, %.02f secs, %.2f MiB/s)"),
			*CachePath, *Path, DebugName.Len(), DebugName.GetData(), ReadSize, ReadDuration, ReadSpeed);

		if (!GIsBuildMachine && ReadDuration > 5.0)
		{
			// Slower than 0.5 MiB/s?
			UE_CLOG(ReadSpeed < 0.5, LogDerivedDataCache, Warning,
				TEXT("%s: Loading %s from '%.*s' is very slow (%.2f MiB/s); consider disabling this cache store."),
				*CachePath, *Path, DebugName.Len(), DebugName.GetData(), ReadSpeed);
		}
	}

	if (ReadSize > 0)
	{
		Stats.PhysicalReadSize += uint64(ReadSize);
	}

	return !bError && ReadSize > 0;
}

TUniquePtr<FArchive> FFileSystemCacheStore::OpenFileWrite(FStringBuilderBase& Path, const FStringView DebugName, FRequestStats& Stats) const
{
	const FMonotonicTimePoint StartTime = FMonotonicTimePoint::Now();
	ON_SCOPE_EXIT { Stats.AddLatency(FMonotonicTimePoint::Now() - StartTime); };

	// Retry to handle a race where the directory is deleted while the file is being created.
	constexpr int32 MaxAttemptCount = 3;
	for (int32 AttemptCount = 0; AttemptCount < MaxAttemptCount; ++AttemptCount)
	{
		if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_Silent)})
		{
			return Ar;
		}
	}
	return nullptr;
}

TUniquePtr<FArchive> FFileSystemCacheStore::OpenFileRead(FStringBuilderBase& Path, const FStringView DebugName, FRequestStats& Stats) const
{
	// Checking for existence may update the modification time to avoid the file being evicted from the cache.
	// Reduce Game Thread overhead by executing the update on a worker thread if the path implies higher latency.
	if (IsInGameThread() && FStringView(CachePath).StartsWith(TEXTVIEW("//"), ESearchCase::CaseSensitive))
	{
		FRequestOwner AsyncOwner(EPriority::Normal);
		Private::LaunchTaskInCacheThreadPool(AsyncOwner, [this, Path = MakeShared<TStringBuilder<256>>(InPlace, Path)]() mutable
		{
			FRequestStats UnusedStats;
			(void)FileExists(*Path, UnusedStats);
		});
		AsyncOwner.KeepAlive();
	}
	else
	{
		if (!FileExists(Path, Stats))
		{
			return nullptr;
		}
	}

	const FMonotonicTimePoint StartTime = FMonotonicTimePoint::Now();
	ON_SCOPE_EXIT { Stats.AddLatency(FMonotonicTimePoint::Now() - StartTime); };
	return TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*Path, FILEREAD_Silent));
}

bool FFileSystemCacheStore::FileExists(FStringBuilderBase& Path, FRequestStats& Stats) const
{
	const FMonotonicTimePoint StartTime = FMonotonicTimePoint::Now();
	const FDateTime TimeStamp = IFileManager::Get().GetTimeStamp(*Path);
	Stats.AddLatency(FMonotonicTimePoint::Now() - StartTime);
	if (TimeStamp == FDateTime::MinValue())
	{
		return false;
	}
	if (bTouch || (!bReadOnly && (FDateTime::UtcNow() - TimeStamp).GetTotalDays() > (MaxFileAgeInDays / 4)))
	{
		IFileManager::Get().SetTimeStamp(*Path, FDateTime::UtcNow());
	}
	return true;
}

bool FFileSystemCacheStore::IsDeactivatedForPerformance()
{
	if ((DeactivateAtMs <= 0.f) || !bDeactivatedForPerformance.load(std::memory_order_relaxed))
	{
		return false;
	}

	// Look for an opportunity to consume the output of an existing completed performance evaluation task
	{
		FReadScopeLock ReadLock(PerformanceReEvaluationTaskLock);
		if (PerformanceReEvaluationTask.IsValid())
		{
			if (PerformanceReEvaluationTask.IsCompleted())
			{
				EPerformanceReEvaluationResult Result =
					PerformanceReEvaluationTask.GetResult().exchange(
						EPerformanceReEvaluationResult::Invalid,
						std::memory_order_relaxed);

				if (Result != EPerformanceReEvaluationResult::Invalid)
				{
					LastPerformanceEvaluationTicks.store(FDateTime::UtcNow().GetTicks(), std::memory_order_relaxed);
					bool bLocalDeactivatedForPerformance =
						Result == EPerformanceReEvaluationResult::PerformanceDeactivate;

					if (!bLocalDeactivatedForPerformance)
					{
						// We're no longer deactivated for performance.  If maintenance was deferred, do it now.
						if (FFileSystemCacheStoreMaintainerParams* MaintainerParams = DeactivationDeferredMaintainerParams.Get())
						{
							Maintainer = MakeUnique<FFileSystemCacheStoreMaintainer>(*MaintainerParams, CachePath, StoreOwner);
							DeactivationDeferredMaintainerParams.Reset();

							if (bDeactivationDeferredClean)
							{
								Maintainer->BoostPriority();
								Maintainer->WaitForIdle();
							}
						}

						UE_LOGF(LogDerivedDataCache, Display,
							"%ls: Performance has improved and meets minimum performance criteria. "
								"It will be reactivated now.",
							*CachePath);
					}

					bDeactivatedForPerformance.store(bLocalDeactivatedForPerformance, std::memory_order_relaxed);
					UpdateStatus();
					return bLocalDeactivatedForPerformance;
				}
			}
			else
			{
				// Avoid attempting to get a write lock and see if you can spawn a new evaluation task
				return true;
			}
		}
	}

	// Look for an opportunity to start a new performance evaluation task
	FTimespan TimespanSinceLastPerfEval =
		FDateTime::UtcNow() - FDateTime(LastPerformanceEvaluationTicks.load(std::memory_order_relaxed));

	if (TimespanSinceLastPerfEval > FTimespan::FromMinutes(1))
	{
		FWriteScopeLock WriteLock(PerformanceReEvaluationTaskLock);
		// After acquiring the write lock, ensure that the task hasn't been re-launched
		// (and possibly completed and consumed) by someone else while we were waiting.
		// This is evaluated by checking that:
		// 1. Task is invalid or task is valid and has a consumed result
		// and
		// 2. Task consumption time is still larger than our re-evaluation interval
		if (!PerformanceReEvaluationTask.IsValid() ||
			(PerformanceReEvaluationTask.IsCompleted() &&
				(PerformanceReEvaluationTask.GetResult().load(std::memory_order_relaxed) ==
					EPerformanceReEvaluationResult::Invalid)
			))
		{
			TimespanSinceLastPerfEval =
				FDateTime::UtcNow() - FDateTime(LastPerformanceEvaluationTicks.load(std::memory_order_relaxed));

			if (TimespanSinceLastPerfEval > FTimespan::FromMinutes(1))
			{
				PerformanceReEvaluationTask = Tasks::Launch(TEXT("FFileSystemCacheStore::ReEvaluatePerformance"),
					[CachePath = this->CachePath, DeactivateAtMs = this->DeactivateAtMs]() ->
						std::atomic<EPerformanceReEvaluationResult>
					{
						check(DeactivateAtMs > 0.f);

						FDerivedDataCacheSpeedStats LocalSpeedStats;
						LocalSpeedStats.ReadSpeedMBs = 999;
						LocalSpeedStats.WriteSpeedMBs = 999;
						LocalSpeedStats.LatencyMS = 0;

						RunSpeedTest(CachePath,
							true /* bReadOnly */,
							true /* bSeekTimeOnly */,
							LocalSpeedStats.LatencyMS,
							LocalSpeedStats.ReadSpeedMBs,
							LocalSpeedStats.WriteSpeedMBs,
							nullptr,
							nullptr);

						if (LocalSpeedStats.LatencyMS >= DeactivateAtMs)
						{
							return EPerformanceReEvaluationResult::PerformanceDeactivate;
						}
						return EPerformanceReEvaluationResult::PerformanceActivate;
					});
			}
		}
	}

	return true;
}

void FFileSystemCacheStore::UpdateStatus()
{
	if (StoreStats)
	{
		if (bDeactivatedForPerformance.load(std::memory_order_relaxed))
		{
			StoreStats->SetStatus(ECacheStoreStatusCode::Warning, NSLOCTEXT("DerivedDataCache", "DeactivatedForPerformance", "Deactivated for performance"));
		}
		else
		{
			StoreStats->SetStatus(ECacheStoreStatusCode::None, {});
		}
	}
}

bool FFileSystemCacheStore::RunInitialSpeedTest(const FFileSystemCacheStoreParams& Params, FDerivedDataCacheSpeedStats& OutSpeedStats)
{
	struct FSpeedTestState : public FRefCountedObject
	{
		FDerivedDataCacheSpeedStats SpeedStats;
		std::atomic<uint32> NumLatencyTestsCompleted = 0;
		std::atomic<bool> AbandonRequest = false;
		bool bResult = false;
		FManualResetEvent CompletionEvent;
	};

	TRefCountPtr<FSpeedTestState> SpeedTestState = new FSpeedTestState();
	Tasks::Launch(TEXT("FFileSystemCacheStore::InitialEvaluation"),
		[CachePath = Params.CachePath, bReadOnly = Params.bReadOnly, SpeedTestState]()
		{
			SpeedTestState->bResult = RunSpeedTest(CachePath,
				bReadOnly,
				false /* bSeekTimeOnly */,
				SpeedTestState->SpeedStats.LatencyMS,
				SpeedTestState->SpeedStats.ReadSpeedMBs,
				SpeedTestState->SpeedStats.WriteSpeedMBs,
				&SpeedTestState->NumLatencyTestsCompleted,
				&SpeedTestState->AbandonRequest);
			SpeedTestState->CompletionEvent.Notify();
		});

	if (!GIsBuildMachine && FPlatformProcess::SupportsMultithreading() && (Params.DeactivateAtMs > 0.f))
	{
		if (SpeedTestState->CompletionEvent.WaitFor(FMonotonicTimeSpan::FromMilliseconds(Params.DeactivateAtMs * 2.f)))
		{
			// If the task completed in the initial wait period, return the result
			OutSpeedStats = SpeedTestState->SpeedStats;
			return SpeedTestState->bResult;
		}
		else
		{
			// If the task did not complete the initial wait period, evaluate if we're progressing fast enough to keep waiting
			// or we should abandon it and supply generic "bad" speed test results.
			if (SpeedTestState->NumLatencyTestsCompleted.load(std::memory_order_acquire) < 2)
			{
				SpeedTestState->AbandonRequest.store(true, std::memory_order_relaxed);
				OutSpeedStats.ReadSpeedMBs = 0.0;
				OutSpeedStats.WriteSpeedMBs = 0.0;
				OutSpeedStats.LatencyMS = 999.0;
				UE_LOGF(LogDerivedDataCache, Log, "%ls: Skipping speed test due to slow test progress. Assuming poor performance.", *Params.CachePath);
				return true;
			}
		}
	}

	// Wait indefinitely for completion
	SpeedTestState->CompletionEvent.Wait();
	OutSpeedStats = SpeedTestState->SpeedStats;
	return SpeedTestState->bResult;
}

void FFileSystemCacheStoreParams::Parse(const TCHAR* Name, const TCHAR* Config)
{
	auto RegisterInheritedCommandlineArg = [](const FStringView ArgName)
	{
		FCommandLine::RegisterArgument(ArgName, ECommandLineArgumentFlags::EditorContext | ECommandLineArgumentFlags::CommandletContext | ECommandLineArgumentFlags::Inherit);
	};
	CacheName = Name;

	// Default remote behavior based on historical cache names.
	bRemote = CacheName == TEXTVIEW("Shared");

	FString Key;

	// Path Params

	FParse::Value(Config, TEXT("Path="), CachePath);

	if (FParse::Value(Config, TEXT("EnvPathOverride="), Key))
	{
		if (FString Value = FPlatformMisc::GetEnvironmentVariable(*Key); !Value.IsEmpty())
		{
			CachePath = MoveTemp(Value);
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Found environment variable %ls=%ls", Name, *Key, *CachePath);
		}
		if (FString Value; FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("GlobalDataCachePath"), *Key, Value) && !Value.IsEmpty())
		{
			CachePath = MoveTemp(Value);
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Found registry key GlobalDataCachePath %ls=%ls", Name, *Key, *CachePath);
		}
	}

	if (FParse::Value(Config, TEXT("CommandLineOverride="), Key) &&
		FParse::Value(FCommandLine::Get(), *(Key + TEXT("=")), CachePath))
	{
		RegisterInheritedCommandlineArg(Key);
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Found command line override %ls=%ls", Name, *Key, *CachePath);
	}

	if (FParse::Value(Config, TEXT("EditorOverrideSetting="), Key))
	{
		if (FString Setting; GConfig->GetString(TEXT("/Script/UnrealEd.EditorSettings"), *Key, Setting, GEditorSettingsIni) && !Setting.IsEmpty())
		{
			if (FString Value; FParse::Value(*Setting, TEXT("Path="), Value))
			{
				Value.TrimQuotesInline();
				Value.ReplaceEscapedCharWithCharInline();
				if (!Value.IsEmpty())
				{
					CachePath = MoveTemp(Value);
					UE_LOGF(LogDerivedDataCache, Log, "%ls: Found editor settings override %ls=%ls", Name, *Key, *CachePath);
				}
			}
		}
	}

	// Paths that start with a '?' are config keys.
	if (CachePath.StartsWith(TEXT("?")) && !GConfig->GetString(TEXT("DerivedDataCacheSettings"), *CachePath + 1, CachePath, GEngineIni))
	{
		CachePath.Empty();
	}

	FPaths::NormalizeFilename(CachePath);

	if (const FString AbsoluteCachePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*CachePath); AbsoluteCachePath.Len() >= GMaxCacheRootLen)
	{
		const FText ErrorMessage = FText::Format(NSLOCTEXT("DerivedDataCache", "PathTooLong", "Cache path {0} is longer than {1} characters. Shorten the path to leave more characters for cache keys."),
			FText::FromString(AbsoluteCachePath), FText::AsNumber(GMaxCacheRootLen));
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
		UE_LOGF(LogDerivedDataCache, Fatal, "%ls", *ErrorMessage.ToString());
	}

	// Other Params

	FParse::Value(Config, TEXT("WriteAccessLog="), AccessLogPath);
	FParse::Value(Config, TEXT("MaxRecordSizeKB="), MaxRecordSizeKB);
	FParse::Value(Config, TEXT("MaxValueSizeKB="), MaxValueSizeKB);
	FParse::Value(Config, TEXT("UnusedFileAge="), MaxFileAgeInDays);
	FParse::Value(Config, TEXT("ConsiderSlowAt="), ConsiderSlowAtMs);
	FParse::Value(Config, TEXT("DeactivateAt="), DeactivateAtMs);
	FParse::Bool(Config, TEXT("PromptIfMissing="), bPromptIfMissing);
	FParse::Bool(Config, TEXT("DeleteOnly="), bDeleteOnly);
	FParse::Bool(Config, TEXT("ReadOnly="), bReadOnly);
	FParse::Bool(Config, TEXT("Remote="), bRemote);
	FParse::Bool(Config, TEXT("Clean="), bClean);
	FParse::Bool(Config, TEXT("Flush="), bFlush);

	FParse::Bool(Config, TEXT("Touch="), bTouch);
	bTouch = bTouch || FParse::Param(FCommandLine::Get(), TEXT("DDCTOUCH"));

	bSkipSpeedTest = bSkipSpeedTest || FParse::Param(FCommandLine::Get(), TEXT("DDCSkipSpeedTest"));

	bDeleteUnused = !bReadOnly;
	FParse::Bool(Config, TEXT("DeleteUnused="), bDeleteUnused);
	bDeleteUnused = bDeleteUnused && !FParse::Param(FCommandLine::Get(), TEXT("NoDDCCleanup"));

	if (!FParse::Value(Config, TEXT("MaxFileChecksPerSec="), MaxScanRate))
	{
		int32 MaxFileScanRate;
		if (GConfig->GetInt(TEXT("DDCCleanup"), TEXT("MaxFileChecksPerSec"), MaxFileScanRate, GEngineIni))
		{
			MaxScanRate = uint32(MaxFileScanRate);
		}
	}

	FParse::Value(Config, TEXT("FoldersToClean="), MaxDirectoryScanCount);

	GConfig->GetDouble(TEXT("DDCCleanup"), TEXT("TimeToWaitAfterInit"), MaintenanceDelayInSeconds, GEngineIni);

	FParse::Value(Config, TEXT("RedirectionFileName="), RedirectionFileName);
	FParse::Value(Config, TEXT("RedirectionKeyName="), RedirectionKeyName);
}

ILegacyCacheStore* CreateFileSystemCacheStore(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner)
{
	FFileSystemCacheStoreParams Params;
	Params.Parse(Name, Config);
	return FFileSystemCacheStore::TryCreate(Params, Owner);
}

} // UE::DerivedData
