// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrossChangelistValidator.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Logging/SubmitToolLog.h"
#include "Logic/Services/Interfaces/IChangelistService.h"
#include "CommandLine/CmdLineParameters.h"
#include "Serialization/JsonSerializer.h"
#include "SubmitToolCoreUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Logic/Validators/ValidatorFactory.h"

REGISTER_VALIDATOR_TYPE(SubmitToolParseConstants::CrossChangelistValidator, FCrossChangelistValidator)

bool FCrossChangelistValidator::Validate(const FString& InCLDescription, const TArray<FSCFileRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FCrossChangelistValidator::Validate);

	bool bValid = CheckHeaderAndCppInDifferentChangelist();

	TArray<FString> AssetPaths;
	const FString UAssetExt(TEXT(".uasset"));
	const FString UMapExt(TEXT(".umap"));
	const FString UPluginExt(TEXT(".uplugin"));
	for (FSCFileRef FileInCL : InFilteredFilesInCL)
	{
		const FString& Filename = FileInCL->GetFilename();

		if (Filename.EndsWith(UAssetExt, ESearchCase::IgnoreCase)
			|| Filename.EndsWith(UMapExt, ESearchCase::IgnoreCase)
			|| Filename.EndsWith(UPluginExt, ESearchCase::IgnoreCase))
		{
			AssetPaths.Add(Filename);
		}
	}

	TSet<FString> UProjects = ExtractUProjectFiles(AssetPaths);
	TSet<FString> UEFNProjects = ExtractSubProjectFiles(AssetPaths);

	bValid &= CheckForFilesInUncontrolledCLFile(UProjects, UEFNProjects);

	ValidationFinished(bValid);
	return true;
}

bool FCrossChangelistValidator::CheckHeaderAndCppInDifferentChangelist()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FCrossChangelistValidator::CheckHeaderAndCppInDifferentChangelist);

	enum class EExtension : uint8
	{
		Unknown,
		Header,
		Cpp,
		C,
	};

	auto GetExtension = [](FStringView Extension)
	{
		if (Extension == TEXTVIEW("h"))
		{
			return EExtension::Header;
		}
		if (Extension == TEXTVIEW("cpp"))
		{
			return EExtension::Cpp;
		}
		if (Extension == TEXTVIEW("c"))
		{
			return EExtension::C;
		}
		return EExtension::Unknown;
	};

	TSharedPtr<IChangelistService> ChangelistService = ServiceProvider.Pin()->GetService<IChangelistService>();
	const TArray<FSCFileRef>& FilesInChangelist = ChangelistService->GetFilesInCL();
	TMap<FString, FString> OpenFilesInChangelists = ChangelistService->GetOpenFilesInChangelists();
	FString CLID = ChangelistService->GetCLID();

	bool bValid = true;

	struct FOpenFile
	{
		FStringView FullFilename;
		FStringView CleanFilename;
		FStringView Changelist;
		EExtension Extension;
	};

	// Extract all code files in the changelist
	TMap<FStringView, FOpenFile> CodeFilesInChangelist;
	CodeFilesInChangelist.Reserve(FilesInChangelist.Num());
	for (FSCFileRef FileInCL : FilesInChangelist)
	{
		EExtension Extension = GetExtension(FPathViews::GetExtension(FileInCL->GetFilename()));
		if (Extension != EExtension::Unknown)
		{
			FOpenFile OpenFile;
			OpenFile.FullFilename = FStringView(FileInCL->GetFilename()); // clientPath
			OpenFile.CleanFilename = FPathViews::GetCleanFilename(OpenFile.FullFilename);
			OpenFile.Extension = Extension;
			OpenFile.Changelist = CLID;
			CodeFilesInChangelist.Add(OpenFile.CleanFilename, OpenFile);
		}
	}

	if (!CodeFilesInChangelist.Num())
	{
		return bValid;
	}

	// Extract all open code files in other changelists
	TMap<FStringView, FOpenFile> OtherOpenFiles;
	OtherOpenFiles.Reserve(OpenFilesInChangelists.Num());
	for (TPair<FString, FString>& Pair : OpenFilesInChangelists)
	{
		if (Pair.Value == CLID)
		{
			continue;
		}

		EExtension Extension = GetExtension(FPathViews::GetExtension(Pair.Key));
		if (Extension == EExtension::Unknown)
		{
			continue;
		}

		FOpenFile OpenFile;
		OpenFile.FullFilename = FStringView(Pair.Key); // depotPath
		OpenFile.CleanFilename = FPathViews::GetCleanFilename(OpenFile.FullFilename);
		OpenFile.Changelist = Pair.Value;
		OpenFile.Extension = Extension;

		// Ignore any files that also exist in the current changelist since
		// the same cleanfilename may exist in multiple folder locations
		if (CodeFilesInChangelist.Contains(OpenFile.CleanFilename))
		{
			continue;
		}

		OtherOpenFiles.Add(OpenFile.CleanFilename, OpenFile);
	}

	for (TPair<FStringView, FOpenFile>& FileInCL : CodeFilesInChangelist)
	{
		FStringView CleanFilename = FileInCL.Value.CleanFilename;

		TArray<FString, TInlineAllocator<2>> FilenamesToCheck;
		if (FileInCL.Value.Extension == EExtension::Header)
		{
			FilenamesToCheck.Add(FPathViews::ChangeExtension(CleanFilename, TEXT("cpp")));
			FilenamesToCheck.Add(FPathViews::ChangeExtension(CleanFilename, TEXT("c")));
		}
		else
		{
			FilenamesToCheck.Add(FPathViews::ChangeExtension(CleanFilename, TEXT("h")));
		}

		for (const FString& OtherFilename : FilenamesToCheck)
		{
			FOpenFile* OtherFile = OtherOpenFiles.Find(FStringView(OtherFilename));
			if (OtherFile)
			{
				bValid = false;

				FString Message = FString::Printf(TEXT("[%s] A matching %s file '%.*s' for '%.*s' was found in another open CL '%.*s'."),
						*GetValidatorName(),
						OtherFile->Extension == EExtension::Header ? TEXT("Header") : TEXT("CPP | C"),
						OtherFile->CleanFilename.Len(), OtherFile->CleanFilename.GetData(),
						FileInCL.Value.FullFilename.Len(), FileInCL.Value.FullFilename.GetData(),
						OtherFile->Changelist.Len(), OtherFile->Changelist.GetData());

				LogFailure(Message);
			}
		}
	}

	return bValid;
}

bool FCrossChangelistValidator::CheckForFilesInUncontrolledCLFile(const TSet<FString>& InUProjects, const TSet<FString>& InUEFNProjects)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FCrossChangelistValidator::CheckForFilesInUncontrolledCLFile);

	bool bAllValid = true;
	bool bValid = true;
	for (const FString& ProjectFile : InUProjects)
	{
		const FString UncontrolledCLPath = FPaths::Combine(FPaths::GetPath(ProjectFile), TEXT("Saved"), TEXT("SourceControl"), TEXT("UncontrolledChangelists.json"));
		TMap<FString, TArray<FString>> UncontrolledCls = LoadUncontrolledCLs(UncontrolledCLPath);

		for (const TPair<FString, TArray<FString>>& Pair : UncontrolledCls)
		{
			if (Pair.Value.Num() > 0)
			{
				if (bValid)
				{
					LogFailure(FString::Printf(TEXT("[%s] Found Uncontrolled CLs with files in project %s (%s), please check files are not missing from your change."), *GetValidatorName(), *ProjectFile, *UncontrolledCLPath));
					bAllValid = false;
					bValid = false;
				}

				LogFailure(FString::Printf(TEXT("[%s] Uncontrolled changelist '%s' found with %d files: \n\t-\t%s"), *GetValidatorName(), *Pair.Key, Pair.Value.Num(), *FString::Join(Pair.Value, TEXT("\n\t-\t"))));
			}
		}
	}

	bValid = true;
	if (!InUEFNProjects.IsEmpty())
	{
		const FString GenericUncontrolledCLPath = FPaths::Combine(FSubmitToolCoreUtils::GetLocalAppDataPath(), TEXT("UnrealEditorFortnite"), TEXT("SourceControl"), TEXT("UncontrolledChangelists.json"));
		TMap<FString, TArray<FString>> GenericUncontrolledCls = LoadUncontrolledCLs(GenericUncontrolledCLPath);

		for (const TPair<FString, TArray<FString>>& Pair : GenericUncontrolledCls)
		{
			if (Pair.Value.Num() > 0)
			{
				if (bValid)
				{
					LogFailure(FString::Printf(TEXT("[%s] Found Uncontrolled CLs with files in the global settings (%s), please check files are not missing from your change."), *GetValidatorName(), *GenericUncontrolledCLPath));
					bAllValid = false;
					bValid = false;
				}

				LogFailure(FString::Printf(TEXT("[%s] Uncontrolled changelist '%s' found with %d files: \n\t-\t%s"), *GetValidatorName(), *Pair.Key, Pair.Value.Num(), *FString::Join(Pair.Value, TEXT("\n\t-\t"))));
			}
		}
	}

	bValid = true;
	for (const FString& UEFNProject : InUEFNProjects)
	{
		const FString UncontrolledCLProjectFilename = FString::Printf(TEXT("UncontrolledChangelists_%s.json"), *FPaths::GetPathLeaf(FPaths::GetPath(UEFNProject)));
		const FString ProjectUncontrolledCLPath = FPaths::Combine(FSubmitToolCoreUtils::GetLocalAppDataPath(), TEXT("UnrealEditorFortnite"), TEXT("SourceControl"), UncontrolledCLProjectFilename);
		TMap<FString, TArray<FString>> ProjectUncontrolledCls = LoadUncontrolledCLs(ProjectUncontrolledCLPath);

		for (const TPair<FString, TArray<FString>>& Pair : ProjectUncontrolledCls)
		{
			if (Pair.Value.Num() > 0)
			{
				if (bValid)
				{
					LogFailure(FString::Printf(TEXT("[%s] Found Uncontrolled CLs with files in project %s (%s), please check files are not missing from your change."), *GetValidatorName(), *UEFNProject, *ProjectUncontrolledCLPath));
					bAllValid = false;
					bValid = false;
				}

				LogFailure(FString::Printf(TEXT("[%s] Uncontrolled changelist '%s' found with %d files: \n\t-\t%s"), *GetValidatorName(), *Pair.Key, Pair.Value.Num(), *FString::Join(Pair.Value, TEXT("\n\t-\t"))));
			}
		}
	}

	return bAllValid;
}

static constexpr const TCHAR* VERSION_NAME = TEXT("version");
static constexpr const TCHAR* CHANGELISTS_NAME = TEXT("changelists");
static constexpr uint32 VERSION_NUMBER = 1;
static constexpr const TCHAR* GUID_NAME = TEXT("guid");
static constexpr const TCHAR* FILES_NAME = TEXT("files");
static constexpr const TCHAR* NAME_NAME = TEXT("name");
static constexpr const TCHAR* DESCRIPTION_NAME = TEXT("description");
TMap<FString, TArray<FString>> FCrossChangelistValidator::LoadUncontrolledCLs(const FString& InFile) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FCrossChangelistValidator::LoadUncontrolledCLs);

	TMap<FString, TArray<FString>> FilesInUncontrolledCL;
	FString ImportJsonString;
	TSharedPtr<FJsonObject> RootObject;
	uint32 VersionNumber = 0;
	const TArray<TSharedPtr<FJsonValue>>* UncontrolledChangelistsArray = nullptr;

	if (IFileManager::Get().FileExists(*InFile) && FFileHelper::LoadFileToString(ImportJsonString, *InFile))
	{
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ImportJsonString);

		if (!FJsonSerializer::Deserialize(JsonReader, RootObject))
		{
			UE_LOGF(LogValidators, Warning, "[%ls] Cannot deserialize RootObject.", *GetValidatorName());
			return FilesInUncontrolledCL;
		}

		if (!RootObject->TryGetNumberField(VERSION_NAME, VersionNumber))
		{
			UE_LOGF(LogValidators, Warning, "[%ls] Cannot get field %ls.", *GetValidatorName(), VERSION_NAME);
			return FilesInUncontrolledCL;
		}

		if (VersionNumber > VERSION_NUMBER)
		{
			UE_LOGF(LogValidators, Warning, "[%ls] Version number is invalid (file: %u, current: %u).", *GetValidatorName(), VersionNumber, VERSION_NUMBER);
			return FilesInUncontrolledCL;
		}

		if (!RootObject->TryGetArrayField(CHANGELISTS_NAME, UncontrolledChangelistsArray))
		{
			UE_LOGF(LogValidators, Warning, "[%ls] Cannot get field %ls.", *GetValidatorName(), CHANGELISTS_NAME);
			return FilesInUncontrolledCL;
		}

		for (const TSharedPtr<FJsonValue>& JsonValue : *UncontrolledChangelistsArray)
		{
			TSharedRef<FJsonObject> JsonObject = JsonValue->AsObject().ToSharedRef();
			const TArray<TSharedPtr<FJsonValue>>* FileValues = nullptr;

			FString CLDescription;
			if (!JsonObject->TryGetStringField(DESCRIPTION_NAME, CLDescription))
			{
				UE_LOGF(LogValidators, Warning, "[%ls] Cannot get field %ls.", *GetValidatorName(), DESCRIPTION_NAME);
			}


			if ((!JsonObject->TryGetArrayField(FILES_NAME, FileValues)) || (FileValues == nullptr))
			{
				UE_LOGF(LogValidators, Warning, "[%ls] Cannot get field %ls.", *GetValidatorName(), FILES_NAME);
				return FilesInUncontrolledCL;
			}

			TArray<FString> Filenames;
			Algo::Transform(*FileValues, Filenames, [](const TSharedPtr<FJsonValue>& File)
				{
					return File->AsString();
				});

			FilesInUncontrolledCL.FindOrAdd(CLDescription, Filenames);
		}

		UE_LOGF(LogValidators, Display, "[%ls] Uncontrolled Changelist persistency file loaded %ls, %d uncontrolled CLs", *GetValidatorName(), *InFile, FilesInUncontrolledCL.Num());
	}

	return FilesInUncontrolledCL;
}

TSet<FString> FCrossChangelistValidator::ExtractUProjectFiles(const TArray<FString>& InFiles)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FCrossChangelistValidator::ExtractUProjectFiles);

	TSet<FString> CheckedDirectories;
	TSet<FString> ProjectFiles;

	for (const FString& File : InFiles)
	{
		FString CurrentDir = FPaths::GetPath(File);

		while (!CurrentDir.IsEmpty())
		{
			bool bIsAlreadyInSet = false;
			CheckedDirectories.Add(CurrentDir, &bIsAlreadyInSet);
			if (bIsAlreadyInSet)
			{
				break;
			}

			TArray<FString> Projects;
			IFileManager::Get().FindFiles(Projects, *(CurrentDir / TEXT("*.uproject")), true, false);

			for (const FString& Project : Projects)
			{
				ProjectFiles.Add(CurrentDir + "/" + Project);
			}

			CurrentDir = FPaths::GetPath(CurrentDir);
		}
	}

	return ProjectFiles;
}


TSet<FString> FCrossChangelistValidator::ExtractSubProjectFiles(const TArray<FString>& InFiles)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FCrossChangelistValidator::ExtractSubProjectFiles);

	TSet<FString> CheckedDirectories;
	TSet<FString> ProjectFiles;

	for (const FString& File : InFiles)
	{
		FString CurrentDir = FPaths::GetPath(File);

		while (!CurrentDir.IsEmpty())
		{
			bool bIsAlreadyInSet = false;
			CheckedDirectories.Add(CurrentDir, &bIsAlreadyInSet);
			if (bIsAlreadyInSet)
			{
				break;
			}

			TArray<FString> Projects;
			IFileManager::Get().FindFiles(Projects, *(CurrentDir / TEXT("*.uefnproject")), true, false);

			for (const FString& Project : Projects)
			{
				ProjectFiles.Add(CurrentDir + "/" + Project);
			}

			CurrentDir = FPaths::GetPath(CurrentDir);
		}
	}

	return ProjectFiles;
}
