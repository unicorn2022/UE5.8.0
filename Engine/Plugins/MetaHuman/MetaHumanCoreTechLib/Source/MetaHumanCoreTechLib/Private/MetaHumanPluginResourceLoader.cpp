// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPluginResourceLoader.h"
#include "MetaHumanCoreTechLibGlobals.h"

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Logging/StructuredLog.h"


bool FMetaHumanPluginResourceLoader::Exists(const char* InPathUtf8) const
{
	const FString Path = StringCast<TCHAR>(InPathUtf8).Get();
	return IFileManager::Get().FileExists(*Path);
}

bool FMetaHumanPluginResourceLoader::ReadAllText(const char* InPathUtf8, char*& OutData, std::size_t& OutSize) const
{
	OutData = nullptr;
	OutSize = 0;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FString(StringCast<TCHAR>(InPathUtf8))))
	{
		return false;
	}

	const auto Conv = StringCast<UTF8CHAR>(*Text);
	const int32 Num = Conv.Length();
	
	char* Buf = static_cast<char*>(FMemory::Malloc(static_cast<SIZE_T>(Num)));
	if (!Buf)
	{
		return false;
	}
	
	FMemory::Memcpy(Buf, Conv.Get(), Num);
	OutData = Buf;
	OutSize = static_cast<std::size_t>(Num);
	return true;
}

bool FMetaHumanPluginResourceLoader::ReadAllBytes(const char* InPathUtf8, void*& OutData, std::size_t& OutSize) const
{
	OutData = nullptr;
	OutSize = 0;
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FString(StringCast<TCHAR>(InPathUtf8))))
	{
		return false;
	}

	void* Buf = FMemory::Malloc(Bytes.Num());
	if (!Buf)
	{
		return false;
	}

	FMemory::Memcpy(Buf, Bytes.GetData(), Bytes.Num());
	OutData = Buf;
	OutSize = static_cast<std::size_t>(Bytes.Num());
	return true;
}

// TODO: Re-visit this path resolver for possible path setups in the configs
bool FMetaHumanPluginResourceLoader::ResolveDataLoadingPath(const char* InBasePathUtf8, const std::size_t& InBaseSize, const char* InAppendPathUtf8,
	std::size_t& InAppendSize, char*& OutPathUtf8, std::size_t& OutSize) const
{
	if ((!InBasePathUtf8 && InBaseSize != 0) || (!InAppendPathUtf8 && InAppendSize != 0))
	{
		UE_LOGFMT(LogMetaHumanCoreTechLib, Error, "Unable to resolve data loading path. Input data is invalid");
		return false;
	}
	
	FString ResolvedPath;
	const FString BasePath = StringCast<TCHAR>(InBasePathUtf8).Get();
	FString AppendPath = StringCast<TCHAR>(InAppendPathUtf8).Get();
	
	if (InBaseSize == 0 && InAppendSize == 0)
	{
		UE_LOGFMT(LogMetaHumanCoreTechLib, Error, "Unable to resolve data loading path as input data was empty");
	}
	else if (InAppendSize == 0)
	{
		ResolvedPath = BasePath;
	}
	else if (InBaseSize == 0)
	{
		if (!Exists(InAppendPathUtf8))
		{
			UE_LOGFMT(LogMetaHumanCoreTechLib, Error, "Invalid data path for creator API: {0}", InAppendPathUtf8);
			return false;
		}

		ResolvedPath = AppendPath;
	}
	
	if (FPaths::IsRelative(BasePath) && !FPaths::IsDrive(AppendPath))
	{
		FPaths::CollapseRelativeDirectories(AppendPath);
		ResolvedPath = FPaths::Combine(BasePath, AppendPath);
	}

	ResolvedPath = GetResolvedMetaHumanPluginContentPath(ResolvedPath);

	// TODO: Paths in the configs can sometimes contain full path. Need to check and convert to valid relative path
	if (ResolvedPath.IsEmpty())
	{
		UE_LOGFMT(LogMetaHumanCoreTechLib, Error, "Unable to resolve a file path for {0} and {1}", BasePath, AppendPath);
		return false;
	}
	
	const auto Utf8Str = StringCast<UTF8CHAR>(*ResolvedPath);
	const int32 Num = Utf8Str.Length();
	
	OutSize = static_cast<std::size_t>(Num);
	OutPathUtf8 = static_cast<char*>(FMemory::Malloc(OutSize));
	FMemory::Memcpy(OutPathUtf8, Utf8Str.Get(), Num);
	
	return true;
}

FString FMetaHumanPluginResourceLoader::GetResolvedMetaHumanPluginContentPath(const FString& InPath) const
{
	const FString Anchor = TEXT("/Plugins/MetaHuman/");
	FString ResolvedPath;
	FString Path = InPath;
	FPaths::NormalizeFilename(Path);

	const int32 AnchorIdx = Path.Find(Anchor);
	if (AnchorIdx != INDEX_NONE)
	{
		FString PluginName, AfterPlugin;
		// Slice after the anchor: "<PluginName>/Content/…"
		const int32 AfterAnchor = AnchorIdx + Anchor.Len();
		const FString After = Path.Mid(AfterAnchor);
		if (After.Split(TEXT("/"), &PluginName, &AfterPlugin))
		{
			const FString ContentPrefix = TEXT("Content/");
			const FString Remainder = AfterPlugin.Mid(ContentPrefix.Len());

			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName))
			{
				const FString ContentDir = Plugin->GetContentDir();
				ResolvedPath = FPaths::Combine(ContentDir, Remainder);
			}			
		}
	}

	return ResolvedPath;
}

void FMetaHumanPluginResourceLoader::Free(const void* InPtr) const
{
	FMemory::Free(const_cast<void*>(InPtr));
}
