// Copyright Epic Games, Inc. All Rights Reserved.

#include "P4LockdownService.h"
#include "CommandLine/CmdLineParameters.h"
#include "Logic/Services/Interfaces/ISTSourceControlService.h"
#include "Logic/Services/SubmitToolServiceProvider.h"
#include "Logging/SubmitToolLog.h"
#include "SubmitToolCoreUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Models/SCFile.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Configuration/Configuration.h"
#include "Templates/UniquePtr.h"


FP4LockdownService::FP4LockdownService(const FP4LockdownParameters& InParameters, TWeakPtr<FSubmitToolServiceProvider> InServiceProvider)
	: ServiceProvider(InServiceProvider)
	, Parameters(InParameters)
{
	GetAdditionalHardlockedPaths();
	DownloadAllowListData();
}

void FP4LockdownService::DownloadAllowListData()
{
	if (Parameters.ConfigPaths.Num() != 0 || Parameters.DynamicConfigPaths.Num() != 0)
	{
		TArray<FString> DepotPaths;
		for (const TPair<FString, FString>& Pair : Parameters.ConfigPaths)
		{
			DepotPaths.Add(Pair.Value);
		}

		for (const TPair<FString, FString>& Pair : Parameters.DynamicConfigPaths)
		{
			DepotPaths.Add(Pair.Value);
		}

		DownloadTask = ServiceProvider.Pin()->GetService<ISTSourceControlService>()->DownloadFiles(MoveTemp(DepotPaths), DownloadedFiles, true);
	}
}

bool FP4LockdownService::WaitForAllowListData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FP4LockdownService::WaitForAllowListData);

	if (!DownloadTask.IsValid())
	{
		return false;
	}

	if (!DownloadTask.IsCompleted())
	{		
		UE_LOGF(LogSubmitToolP4, Log, "Waiting for download of Stream Hardlock data...");
		DownloadTask.Wait(FTimespan::FromSeconds(5));
		if (!DownloadTask.IsCompleted())
		{
			UE_LOGF(LogSubmitToolP4, Warning, "Downloading config files from P4 timed out, hardlock status is not latest, will fallback to use cached files.");
			return false; 
		}
	}

	// Write the downloaded files to disk and return true if at least one file is written
	bool bSuccess = DownloadTask.IsCompleted() && DownloadTask.GetResult();
	if (bSuccess)
	{
		bSuccess = false;
		int32 I = 0;

		auto SaveFiles = [this](const TMap<FString, FString>& InPaths) {
			bool bSuccess = false;
			for (const TPair<FString, FString>& Pair : InPaths)
			{
				if (DownloadedFiles.Contains(Pair.Value))
				{
					FSharedBuffer FileBuffer = DownloadedFiles[Pair.Value];
					const FString Path = GetFilePath(Pair.Key);
					if (TUniquePtr<FArchive> File{ IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_EvenIfReadOnly) })
					{
						File->Serialize(const_cast<void*>(FileBuffer.GetData()), FileBuffer.GetSize());
						bSuccess = true;
					}
					else
					{
						UE_LOGF(LogSubmitToolP4, Warning, "Couldn't create lockdown file %ls", *Path)
					}
				}
			}
			return bSuccess;
		};

		bSuccess |= SaveFiles(Parameters.ConfigPaths);
		bSuccess |= SaveFiles(Parameters.DynamicConfigPaths);
	}
	else
	{
		UE_LOGF(LogSubmitToolP4, Warning, "Downloading config files from P4 failed, hardlock status is not latest, will fallback to use cached files.");
	}
	DownloadTask = {};
	return bSuccess;
}

FSubmitToolLockdownData FP4LockdownService::ArePathsInLockdown(const TArray<FSCFileRef>& InPaths, const TSet<FString>& InDynamicFiles)
{	
	TRACE_CPUPROFILER_EVENT_SCOPE(FP4LockdownService::ArePathsInLockdown);

	FSubmitToolLockdownData GlobalResult;

	if (InPaths.IsEmpty())
	{
		UE_LOGF(LogSubmitToolP4, Warning, "No files to check for lockdown");
		return GlobalResult;
	}

	// Only allow one thread at a time to check ArePathsInLockdown
	FScopeLock Scope(&Mutex);

	// Wait for download
	bool bShouldParseDownloadedData = (AllowListState < EAllowListState::Downloaded) ? WaitForAllowListData() : false;
	
	// Parse new allow list data
	if (AllowListState == EAllowListState::Missing || bShouldParseDownloadedData)
	{
		ParseAllowListData();
		AllowListState = bShouldParseDownloadedData ? EAllowListState::Downloaded : EAllowListState::Cached;
	}

	// Finally validate the paths
	for(const FSCFileRef& Path : InPaths)
	{
		GlobalResult.Append(IsPathInLockdown(Path, InDynamicFiles));
	}

	return GlobalResult;
}

FSubmitToolLockdownData FP4LockdownService::IsPathInLockdown(const FSCFileRef& InPath, const TSet<FString>& InDynamicFiles) const
{
	FString PerforceUserName;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4User, PerforceUserName);

	FSubmitToolLockdownData bOverallLockdownResult;

	for(const FAllowListData& data : AllowListData)
	{
		if (!data.bIsEnabled && !InDynamicFiles.Contains(data.FileId))
		{
			continue;
		}

		bool bIsLocked = false;
		auto EvaluateViews = [&bIsLocked, &InPath](const TArray<TPair<bool, FString>>& Views)
		{
			for (const TPair<bool, FString>& View : Views)
			{
				if (bIsLocked != View.Key)
				{
					FRegexPattern Pattern = FRegexPattern(View.Value, ERegexPatternFlags::CaseInsensitive);
					FRegexMatcher regex = FRegexMatcher(MoveTemp(Pattern), InPath->GetDepotPath());

					if (regex.FindNext())
					{
						bIsLocked = View.Key;
					}
				}
			}
		};
		EvaluateViews(data.Views);

		bool bIsInOverrideAllowlist = false;
		for (const FOverrideData& Override : OverrideData)
		{
			if (Override.Sections.Contains(data.GroupName) && Override.AllowListers.Contains(PerforceUserName))
			{
				EvaluateViews(Override.Views);
				bIsInOverrideAllowlist = true;
			}
		}

		if (bIsLocked)
		{
			const bool bAllowlisted = bIsInOverrideAllowlist || data.AllowListers.Contains(PerforceUserName);
			const FString Message = bAllowlisted ? FString::Printf(TEXT("[Allowlisted] %s"), *data.Message) : data.Message;
			bOverallLockdownResult.GroupNames.Add(data.GroupName);
			bOverallLockdownResult.GroupMessages.Add(Message);
			if (bOverallLockdownResult.LockdownType < data.LockdownType)
			{
				bOverallLockdownResult.LockdownType = data.LockdownType;
			}
			bOverallLockdownResult.bIsAllowlisted &= bAllowlisted;

			if (bOverallLockdownResult.LockdownType == ELockdownType::Controlled && !bAllowlisted)
			{
				bOverallLockdownResult.RequiredTags.Add(data.ValidTag, data.Members);
			}
		}
	}

	for(const FString& AdditionalHardlockedPath : AdditionalHardlocks)
	{
		FRegexPattern Pattern = FRegexPattern(AdditionalHardlockedPath, ERegexPatternFlags::CaseInsensitive);
		FRegexMatcher regex = FRegexMatcher(MoveTemp(Pattern), InPath->GetDepotPath());

		if(regex.FindNext())
		{
			bOverallLockdownResult.LockdownType = ELockdownType::Hardcore;
			bOverallLockdownResult.bIsAllowlisted = false;
			break;
		}
	}

	if (bOverallLockdownResult.LockdownType != ELockdownType::None)
	{
		if (bOverallLockdownResult.LockdownType == ELockdownType::Hardcore)
		{
			bOverallLockdownResult.RequiredTags.Reset();
		}

		UE_LOGF(LogSubmitToolP4Debug, Warning, "File %ls is %ls locked down by %ls.\n%ls", *InPath->GetDepotPath(), *StaticEnum<ELockdownType>()->GetNameStringByValue(static_cast<int64>(bOverallLockdownResult.LockdownType)), *FSubmitToolCoreUtils::StringBuilderJoin<64>(bOverallLockdownResult.GroupNames, TEXT(", ")), *FSubmitToolCoreUtils::StringBuilderJoin(bOverallLockdownResult.GroupMessages, TEXT("\n")));
	}

	return bOverallLockdownResult;
}

void RegexEscapeInline(FString& InOutRegex)
{
	for (int i = InOutRegex.Len() - 1; i >= 0; --i)
	{
		switch (InOutRegex[i])
		{
			case TEXT('\\'):
			case TEXT('*'):
			case TEXT('+'):
			case TEXT('?'):
			case TEXT('|'):
			case TEXT('{'):
			case TEXT('}'):
			case TEXT('['):
			case TEXT(']'):
			case TEXT('('):
			case TEXT(')'):
			case TEXT('^'):
			case TEXT('$'):
			case TEXT('.'):
			case TEXT('#'):
			case TEXT(' '):
				InOutRegex.InsertAt(i, TEXT('\\'));
				break;
		}
	}
}

void FP4LockdownService::GetAdditionalHardlockedPaths()
{
	AdditionalHardlocks.Reserve(Parameters.AdditionalHardlockedPaths.Num());
	for (FString EscapedViewLine : Parameters.AdditionalHardlockedPaths)
	{
		RegexEscapeInline(EscapedViewLine);
		EscapedViewLine.ReplaceInline(TEXT("\\*"), TEXT("[^/]*"));
		EscapedViewLine.ReplaceInline(TEXT("\\.\\.\\."), TEXT(".*"));

		AdditionalHardlocks.Add(MoveTemp(EscapedViewLine));
	}
}

void FP4LockdownService::ParseAllowListData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FP4LockdownService::ParseAllowListData);
	AllowListData.Reset();
	OverrideData.Reset();

	ParseAllowListFiles(Parameters.ConfigPaths, true);
	ParseAllowListFiles(Parameters.DynamicConfigPaths, false);
}

void FP4LockdownService::ParseAllowListFiles(const TMap<FString, FString>& InFiles, bool bEnabled)
{
	for (const TPair<FString, FString>& Config : InFiles)
	{
		FString Filepath = GetFilePath(Config.Key);
		if (IFileManager::Get().FileExists(*Filepath))
		{
			FConfigFile LockdownConfig;
			LockdownConfig.bPythonConfigParserMode = true;
			LockdownConfig.Read(Filepath);

			for (const TPair<FString, FConfigSection>& ConfigPair : AsConst(LockdownConfig))
			{
				const FConfigSection& ConfigSection = ConfigPair.Value;
				
				const FConfigValue* AllowList = ConfigSection.Find(TEXT("allowlist"));
				if (AllowList == nullptr)
				{
					continue;
				}

				const FConfigValue* Status = ConfigSection.Find(TEXT("status"));
				if (Status == nullptr)
				{
					continue;
				}

				FAllowListData* data;
				if (Status->GetSavedValue() == TEXT("hardcore"))
				{
					data = &AllowListData.AddDefaulted_GetRef();
					data->LockdownType = ELockdownType::Hardcore;
				}
				else if (Status->GetSavedValue() == TEXT("override"))
				{
					const FConfigValue* SectionList = ConfigSection.Find(TEXT("sectionlist"));
					if (SectionList == nullptr)
					{
						continue;
					}

					FOverrideData& Override = OverrideData.AddDefaulted_GetRef();
					data = &Override;

					TArray<FString> Sections;
					SectionList->GetSavedValue().ParseIntoArray(Sections, TEXT(","), true);

					for (FString& Section : Sections)
					{
						Section.ToLowerInline();
						Override.Sections.Add(MoveTemp(Section));
					}
				}
				else if (Status->GetSavedValue() == TEXT("controlled"))
				{
					data = &AllowListData.AddDefaulted_GetRef();
					data->LockdownType = ELockdownType::Controlled;

					const FConfigValue* CustomTag = ConfigSection.Find(TEXT("customTag"));
					if (CustomTag != nullptr)
					{
						data->ValidTag = CustomTag->GetSavedValue();
					}
					else
					{
						data->ValidTag = TEXT("#lockdown");
					}
				}
				else
				{
					continue;
				}
				
				data->FileId = Config.Key;
				data->bIsEnabled = bEnabled;
				data->GroupName = ConfigPair.Key;

				const FConfigValue* Message = ConfigSection.Find(TEXT("message"));
				if (Message != nullptr)
				{
					data->Message = Message->GetSavedValue();
				}

				TArray<FString> Allowlisters;
				AllowList->GetSavedValue().ParseIntoArray(Allowlisters, TEXT(","), true);

				for (FString& AllowLister : Allowlisters)
				{
					AllowLister.ToLowerInline();
					AllowLister.TrimStartAndEndInline();
					data->AllowListers.Add(MoveTemp(AllowLister));
				}


				const FConfigValue* MemberList = ConfigSection.Find(TEXT("members"));
				if (MemberList != nullptr)
				{
					TArray<FString> MemberValues;
					MemberList->GetSavedValue().ParseIntoArray(MemberValues, TEXT(","), true);

					for (FString& Member : MemberValues)
					{
						Member.ToLowerInline();
						Member.TrimStartAndEndInline();
						data->Members.Add(MoveTemp(Member));
					}
				}


				TArray<FString> View;
				ConfigSection.MultiFind(TEXT("view"), View, true);
				for (FString& ViewLine : View)
				{
					if (ViewLine.IsEmpty())
					{
						continue;
					}

					bool bIsLocked = true;
					if (ViewLine.StartsWith(TEXT("-"), ESearchCase::CaseSensitive))
					{
						bIsLocked = false;
						ViewLine.MidInline(1);
					}

					RegexEscapeInline(ViewLine);
					ViewLine.ReplaceInline(TEXT("\\*"), TEXT("[^/]*"), ESearchCase::CaseSensitive);
					ViewLine.ReplaceInline(TEXT("\\.\\.\\."), TEXT(".*"), ESearchCase::CaseSensitive);

					data->Views.Add(TPair<bool, FString>(bIsLocked, MoveTemp(ViewLine)));
				}
			}
		}
		else
		{
			UE_LOGF(LogSubmitToolP4Debug, Error, "File %ls doesn't exist", *Filepath);
		}
	}
}

FString FP4LockdownService::GetFilePath(const FString& InConfigId)
{
	FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	FPaths::NormalizeDirectoryName(EngineDir);
	
	const FGuid guid = FGuid::NewGuid();
	FString LocalFilePath = EngineDir + TEXT("/Intermediate/SubmitTool/P4Lockdown/") + InConfigId + TEXT(".ini");
	LocalFilePath = FPaths::ConvertRelativePathToFull(LocalFilePath);
	FPaths::NormalizeFilename(LocalFilePath);
		
	return LocalFilePath;
}
