// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreConfigHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/KeyChainUtilities.h"

DEFINE_LOG_CATEGORY_STATIC(LogIoStoreConfig, Log, All);

namespace IoStoreConfig
{
	bool FindZenExePath(FString& OutPath)
	{
		// Priority 1: Config setting (Engine.ini [IoStoreDependencyViewer] ZenExePath)
		FString CustomZenPath;
		if (GConfig && GConfig->GetString(TEXT("IoStoreDependencyViewer"), TEXT("ZenExePath"), CustomZenPath, GEngineIni))
		{
			OutPath = FPaths::ConvertRelativePathToFull(CustomZenPath);
			UE_LOGF(LogIoStoreConfig, Log, "Using zen.exe path from config: %ls", *OutPath);
			return true;
		}

		// Priority 2: Same directory as executable
		FString ExeDir = FPaths::ConvertRelativePathToFull(FPlatformProcess::BaseDir());
		OutPath = FPaths::Combine(ExeDir, TEXT("zen.exe"));

		if (!FPaths::FileExists(OutPath))
		{
			// Priority 3: Check -projectdir= command line argument
			FString ProjectDir;
			if (FParse::Value(FCommandLine::Get(), TEXT("projectdir="), ProjectDir))
			{
				ProjectDir = FPaths::ConvertRelativePathToFull(ProjectDir);
				OutPath = FPaths::Combine(ProjectDir, TEXT("Engine"), TEXT("Binaries"), TEXT("Win64"), TEXT("zen.exe"));
				UE_LOGF(LogIoStoreConfig, Log, "Checking for zen.exe using -projectdir: %ls", *OutPath);
			}
		}

		return true;
	}

	bool FindOidcTokenExePath(FString& OutPath, const FString& ZenExePath)
	{
		// Priority 1: Config setting (Engine.ini [IoStoreDependencyViewer] OidcTokenExePath)
		FString CustomOidcPath;
		if (GConfig && GConfig->GetString(TEXT("IoStoreDependencyViewer"), TEXT("OidcTokenExePath"), CustomOidcPath, GEngineIni))
		{
			OutPath = FPaths::ConvertRelativePathToFull(CustomOidcPath);
			UE_LOGF(LogIoStoreConfig, Log, "Using OidcToken.exe path from config: %ls", *OutPath);
			return true;
		}

		// Priority 2: Standard Unreal Engine location relative to executable
		OutPath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPlatformProcess::BaseDir(), TEXT(".."), TEXT("DotNET"), TEXT("OidcToken"), TEXT("win-x64"), TEXT("OidcToken.exe")));

		if (!FPaths::FileExists(OutPath))
		{
			// Priority 3: Check -projectdir= for OidcToken.exe
			FString ProjectDir;
			bool bCheckedProjectDir = FParse::Value(FCommandLine::Get(), TEXT("projectdir="), ProjectDir);
			if (bCheckedProjectDir)
			{
				ProjectDir = FPaths::ConvertRelativePathToFull(ProjectDir);
				OutPath = FPaths::Combine(ProjectDir, TEXT("Engine"), TEXT("Binaries"), TEXT("DotNET"), TEXT("OidcToken"), TEXT("win-x64"), TEXT("OidcToken.exe"));
				UE_LOGF(LogIoStoreConfig, Log, "OidcToken.exe not found in standard location, checking -projectdir: %ls", *OutPath);
			}

			if (!FPaths::FileExists(OutPath))
			{
				// Priority 4: Same directory as zen.exe (only if zen.exe path provided and exists)
				if (!ZenExePath.IsEmpty() && FPaths::FileExists(ZenExePath))
				{
					FString ZenDir = FPaths::GetPath(ZenExePath);
					OutPath = FPaths::Combine(ZenDir, TEXT("OidcToken.exe"));
					if (bCheckedProjectDir)
					{
						UE_LOGF(LogIoStoreConfig, Log, "OidcToken.exe not found in project directory, checking zen.exe directory: %ls", *OutPath);
					}
					else
					{
						UE_LOGF(LogIoStoreConfig, Log, "OidcToken.exe not found in standard location, checking zen.exe directory: %ls", *OutPath);
					}
				}
			}
		}

		return true;
	}

	bool ValidateZenExe(const FString& ZenExePath, bool bShowDialog)
	{
		if (FPaths::FileExists(ZenExePath))
		{
			return true;
		}

		// zen.exe not found
		UE_LOGF(LogIoStoreConfig, Error, "zen.exe not found at: %ls", *ZenExePath);
		UE_LOGF(LogIoStoreConfig, Error, "Cannot proceed without zen.exe. Please:");
		UE_LOGF(LogIoStoreConfig, Error, "  1. Place zen.exe in %ls, or", *FPaths::ConvertRelativePathToFull(FPlatformProcess::BaseDir()));
		UE_LOGF(LogIoStoreConfig, Error, "  2. Set ZenExePath in Engine.ini [IoStoreDependencyViewer] section");

		if (bShowDialog)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				FText::FromString(FString::Printf(
					TEXT("zen.exe not found at:\n%s\n\nCannot proceed without zen.exe.\n\nPlease place zen.exe in the application directory or configure ZenExePath in Engine.ini."),
					*ZenExePath)));
		}

		return false;
	}

	bool ValidateOidcTokenExe(const FString& OidcExePath, bool bShowWarning)
	{
		if (FPaths::FileExists(OidcExePath))
		{
			return true;
		}

		// OidcToken.exe not found - warning only (not fatal)
		if (bShowWarning)
		{
			UE_LOGF(LogIoStoreConfig, Warning, "OidcToken.exe not found at: %ls", *OidcExePath);
			UE_LOGF(LogIoStoreConfig, Warning, "Cloud authentication may fail without OidcToken.exe");
			UE_LOGF(LogIoStoreConfig, Warning, "Continuing anyway - zen.exe may handle authentication internally");
		}

		return false;
	}

	FString EscapeCommandLineArgument(const FString& Argument)
	{
		FString Result;
		Result.Reserve(Argument.Len() * 2);

		for (int32 i = 0; i < Argument.Len(); ++i)
		{
			int32 NumBackslashes = 0;

			// Count consecutive backslashes
			while (i < Argument.Len() && Argument[i] == TEXT('\\'))
			{
				++NumBackslashes;
				++i;
			}

			if (i == Argument.Len())
			{
				// Trailing backslashes before the closing quote - must be doubled
				for (int32 j = 0; j < NumBackslashes * 2; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				break;
			}
			else if (Argument[i] == TEXT('"'))
			{
				// Backslashes before a quote - double them and escape the quote
				// (numBackslashes * 2 + 1) backslashes, then the quote
				for (int32 j = 0; j < NumBackslashes * 2 + 1; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				Result.AppendChar(TEXT('"'));
			}
			else
			{
				// Normal character - backslashes are literal
				for (int32 j = 0; j < NumBackslashes; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				Result.AppendChar(Argument[i]);
			}
		}

		return Result;
	}

	bool TryLoadCryptoJsonFromSearchPaths(const TMap<FString, FString>& TemplateVars, const FString& DownloadDirectory)
	{
		// Read search path templates from config
		TArray<FString> SearchPaths;
		if (GConfig)
		{
			GConfig->GetArray(TEXT("IoStoreDependencyViewer"), TEXT("CryptoJsonSearchPath"), SearchPaths, GEngineIni);
			UE_LOGF(LogIoStoreConfig, Display, "GEngineIni path: %ls", *GEngineIni);
		}

		if (SearchPaths.Num() == 0)
		{
			UE_LOGF(LogIoStoreConfig, Log, "No CryptoJsonSearchPath entries configured in Engine.ini");
			return false;
		}

		UE_LOGF(LogIoStoreConfig, Display, "Loaded %d CryptoJsonSearchPath entries from config", SearchPaths.Num());

		// Extract platform for local directory structure (required)
		const FString* Platform = TemplateVars.Find(TEXT("platform"));
		if (!Platform || Platform->IsEmpty())
		{
			UE_LOGF(LogIoStoreConfig, Warning, "Cannot load crypto.json: 'platform' variable not provided");
			return false;
		}

		// Log template variables for debugging
		UE_LOGF(LogIoStoreConfig, Display, "Attempting to load crypto.json with variables:");
		for (const auto& Pair : TemplateVars)
		{
			UE_LOGF(LogIoStoreConfig, Display, "  %ls = %ls", *Pair.Key, *Pair.Value);
		}

		// Try each configured search path
		for (const FString& Template : SearchPaths)
		{
			// Substitute all template variables
			FString ResolvedPath = Template;
			for (const auto& Pair : TemplateVars)
			{
				FString Placeholder = FString::Printf(TEXT("<%s>"), *Pair.Key);
				ResolvedPath.ReplaceInline(*Placeholder, *Pair.Value);
			}

			UE_LOGF(LogIoStoreConfig, Display, "  Trying: %ls", *ResolvedPath);

			// Check if file exists
			if (!FPaths::FileExists(ResolvedPath))
			{
				UE_LOGF(LogIoStoreConfig, Log, "    Not found (this is OK if build is not encrypted)");
				continue;
			}

			UE_LOGF(LogIoStoreConfig, Display, "    Found crypto.json!");

			// Create local metadata directory
			FString LocalPlatformMetadataDir = FPaths::Combine(DownloadDirectory, *Platform, TEXT("Metadata"));
			if (!IFileManager::Get().DirectoryExists(*LocalPlatformMetadataDir))
			{
				if (!IFileManager::Get().MakeDirectory(*LocalPlatformMetadataDir, true))
				{
					UE_LOGF(LogIoStoreConfig, Warning, "    Failed to create local Metadata directory: %ls", *LocalPlatformMetadataDir);
					continue;
				}
			}

			FString LocalCryptoPath = FPaths::Combine(LocalPlatformMetadataDir, TEXT("crypto.json"));

			// Copy the file
			if (IFileManager::Get().Copy(*LocalCryptoPath, *ResolvedPath, true, true) == COPY_OK)
			{
				UE_LOGF(LogIoStoreConfig, Display, "    Successfully copied crypto.json to: %ls", *LocalCryptoPath);
				return true;
			}
			else
			{
				UE_LOGF(LogIoStoreConfig, Warning, "    Failed to copy crypto.json from %ls to %ls", *ResolvedPath, *LocalCryptoPath);
			}
		}

		UE_LOGF(LogIoStoreConfig, Log, "  crypto.json not found in any configured search paths");
		return false;
	}

	FString ParseBranchFromBuildName(const FString& BuildName)
	{
		// Parse branch name from build name
		// Examples: "Fortnite-Main-CL-123" → "Main", "Fortnite-Release-40.20-CL-456" → "Release-40.20"
		if (BuildName.Contains(TEXT("-CL-")))
		{
			int32 PrefixEnd = BuildName.Find(TEXT("-"), ESearchCase::IgnoreCase, ESearchDir::FromStart);
			int32 CLStart = BuildName.Find(TEXT("-CL-"), ESearchCase::IgnoreCase);
			if (PrefixEnd != INDEX_NONE && CLStart != INDEX_NONE && CLStart > PrefixEnd)
			{
				// Extract between first dash and "-CL-"
				return BuildName.Mid(PrefixEnd + 1, CLStart - PrefixEnd - 1);
			}
		}

		// Fallback: use entire build name
		return BuildName;
	}

	FString ParseChangelistFromBuildName(const FString& BuildName)
	{
		// Parse changelist number from build name
		// Examples: "Fortnite-Main-CL-12345" → "12345", "Fortnite-Release-40.20-CL-456789" → "456789"
		if (BuildName.Contains(TEXT("-CL-")))
		{
			int32 CLStart = BuildName.Find(TEXT("-CL-"), ESearchCase::IgnoreCase);
			if (CLStart != INDEX_NONE)
			{
				// Extract everything after "-CL-"
				FString CLPart = BuildName.Mid(CLStart + 4); // +4 to skip "-CL-"

				// Find the next non-digit character (if any) to handle cases like "CL-12345-Suffix"
				int32 EndIndex = 0;
				while (EndIndex < CLPart.Len() && FChar::IsDigit(CLPart[EndIndex]))
				{
					EndIndex++;
				}

				if (EndIndex > 0)
				{
					return CLPart.Left(EndIndex);
				}
			}
		}

		// Fallback: return empty string if no CL found
		return FString();
	}

	bool LoadEncryptionKeysFromDirectory(const FString& DirectoryPath, TMap<FGuid, FAES::FAESKey>& OutKeys)
	{
		OutKeys.Empty();

		// Find all crypto.json files recursively
		TArray<FString> CryptoFiles;
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.FindFilesRecursively(CryptoFiles, *DirectoryPath, TEXT("crypto.json"));

		if (CryptoFiles.Num() == 0)
		{
			UE_LOGF(LogIoStoreConfig, Display, "No crypto.json files found in %ls", *DirectoryPath);
			return false;
		}

		UE_LOGF(LogIoStoreConfig, Display, "Found %d crypto.json file(s), loading encryption keys...", CryptoFiles.Num());

		// Load keys from each crypto.json file and merge
		int32 TotalKeysLoaded = 0;
		for (const FString& CryptoFile : CryptoFiles)
		{
			UE_LOGF(LogIoStoreConfig, Display, "  Loading: %ls", *CryptoFile);

			// Use the refactored KeyChainUtilities::LoadKeyChainFromFileEx without signing keys
			// (signing keys require engine crypto system which isn't initialized in standalone tools)
			FKeyChain KeyChain;
			KeyChainUtilities::FKeyChainLoadResult Result = KeyChainUtilities::LoadKeyChainFromFileEx(
				CryptoFile,
				KeyChain,
				KeyChainUtilities::EKeyChainLoadFlags::None  // Don't load signing keys
			);

			if (!Result.IsOk())
			{
				UE_LOGF(LogIoStoreConfig, Warning, "    Failed to load: %ls", *Result.ErrorMessage);
				continue;
			}

			// Convert FKeyChain to TMap<FGuid, FAES::FAESKey>
			const TMap<FGuid, FNamedAESKey>& EncryptionKeys = KeyChain.GetEncryptionKeys();
			for (const auto& Pair : EncryptionKeys)
			{
				const FGuid& Guid = Pair.Key;
				const FNamedAESKey& NamedKey = Pair.Value;

				// Check for duplicate keys with different values (warning)
				if (OutKeys.Contains(Guid))
				{
					if (FMemory::Memcmp(OutKeys[Guid].Key, NamedKey.Key.Key, sizeof(FAES::FAESKey::Key)) != 0)
					{
						UE_LOGF(LogIoStoreConfig, Warning, "    Key GUID %ls found in multiple crypto.json files with different values! Using first occurrence.", *Guid.ToString());
					}
				}
				else
				{
					OutKeys.Add(Guid, NamedKey.Key);
					UE_LOGF(LogIoStoreConfig, Display, "    Loaded key: %ls (Name: %ls)", *Guid.ToString(), *NamedKey.Name);
					TotalKeysLoaded++;
				}
			}
		}

		UE_LOGF(LogIoStoreConfig, Display, "Successfully loaded %d unique encryption key(s) from %d file(s)", TotalKeysLoaded, CryptoFiles.Num());
		return TotalKeysLoaded > 0;
	}
}
