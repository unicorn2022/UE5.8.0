// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorCommandletValidator.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"

#include "CommandLine/CmdLineParameters.h"

#include "Configuration/Configuration.h"
#include "Logging/SubmitToolLog.h"
#include "Logic/Validators/ValidatorFactory.h"
#include "Models/ModelInterface.h"
#include "Models/SubmitToolUserPrefs.h"
#include "SubmitToolCoreUtils.h"

#include "ProjectEditorRecords.h"

REGISTER_VALIDATOR_TYPE(SubmitToolParseConstants::EditorValidator, FEditorCommandletValidator)

FEditorCommandletValidator::FEditorCommandletValidator(const FName& InNameId, const FSubmitToolParameters& InParameters, TSharedRef<FSubmitToolServiceProvider> InServiceProvider, const FString& InDefinition) :
	FValidatorRunExecutable(InNameId, InParameters, InServiceProvider, InDefinition)
{
	ParseDefinition(InDefinition);
}

void FEditorCommandletValidator::ParseDefinition(const FString& InDefinition)
{
	FStringOutputDevice Errors;
	Definition = MakeUnique<FEditorCommandletValidatorDefinition>();
	FEditorCommandletValidatorDefinition* ModifyableDefinition = const_cast<FEditorCommandletValidatorDefinition*>(GetTypedDefinition<FEditorCommandletValidatorDefinition>());
	FEditorCommandletValidatorDefinition::StaticStruct()->ImportText(*InDefinition, ModifyableDefinition, nullptr, 0, &Errors, FEditorCommandletValidatorDefinition::StaticStruct()->GetName());

	if(!Errors.IsEmpty())
	{
		UE_LOGF(LogSubmitTool, Error, "[%ls] Error loading parameter file %ls", *GetValidatorName(), *Errors);
		FModelInterface::SetErrorState();
	}
}

bool FEditorCommandletValidator::Activate()
{
	FEditorCommandletValidatorDefinition* ModifiableDefinition = const_cast<FEditorCommandletValidatorDefinition*>(GetTypedDefinition<FEditorCommandletValidatorDefinition>());
	ModifiableDefinition->EditorRecordsFile = FConfiguration::Substitute(ModifiableDefinition->EditorRecordsFile);
	ModifiableDefinition->bValidateExecutableExists = false;

	BuiltRegexProcessing = MakeShared<FRegexPattern>(RegexMessageProcessing, ERegexPatternFlags::CaseInsensitive);

	return FValidatorRunExecutable::Activate();
}

bool FEditorCommandletValidator::Validate(const FString& InCLDescription, const TArray<FSCFileRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags)
{
	TMap<FString, FEditorParameters> EditorParameters;
	GetEditorsForPaths(InFilteredFilesInCL, EditorParameters);

	// Only create a temporary user directory if the validator is set up that way
	const FString TempUserDir = GetTypedDefinition<FEditorCommandletValidatorDefinition>()->bIgnoreUserEditorPrefs ? MakeTemporaryUserDirectory() : FString();

	bool bProcessesStarted = false;
	for (const TPair<FString, FEditorParameters>& EditorParametersSet : EditorParameters)
	{
		FString EditorArguments = EditorParametersSet.Value.EditorArguments;

		// If we have a temporary folder for -UserDir, use it to avoid getting an saved editor preferences from the user when running commandlet
		const bool bUseTempUserDir = !TempUserDir.IsEmpty() && !EditorParametersSet.Value.EditorArguments.Contains(TEXT("-UserDir"));
		if (bUseTempUserDir)
		{
			EditorArguments += FString::Printf(TEXT(" -UserDir=\"%s\""), *TempUserDir);
		}

		if (CorrelationId.IsValid())
		{
			EditorArguments += FString::Printf(TEXT(" -SubmitToolCorrelationId=\"%s\""), *CorrelationId.ToString());
		}

		bProcessesStarted |= QueueProcess(EditorParametersSet.Key, EditorParametersSet.Value.EditorExePath, EditorArguments);
	}

	return bProcessesStarted;
}

FString FEditorCommandletValidator::MakeTemporaryUserDirectory() const
{
	FString TempDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Intermediate/"), TEXT("SubmitTool/"), TEXT("SavedEditor/"));
	FString TempDirSaved = FPaths::Combine(TempDir, TEXT("Saved"));
	IFileManager::Get().DeleteDirectory(*TempDirSaved, false, true);

	if (IFileManager::Get().MakeDirectory(*TempDir, true))
	{
		return FPaths::ConvertRelativePathToFull(TempDir);
	}

	return FString();
}

void FEditorCommandletValidator::ProcessSingleMessageForTelemetry(FString& InOutMessage) const
{
	FValidatorRunExecutable::ProcessSingleMessageForTelemetry(InOutMessage);

	// Regex used for clearing the editor log timestamp, looks for "]Log******:" and removes everything before it
	FRegexMatcher Matcher = FRegexMatcher(*BuiltRegexProcessing, InOutMessage);
	InOutMessage = Matcher.ReplaceFirst(TEXT(""));
}

void FEditorCommandletValidator::GetEditorsForPaths(const TArray<FSCFileRef>& InFilteredFilesInCL, TMap<FString, FEditorParameters>& OutProjectEditorParameters) const
{
	const FEditorCommandletValidatorDefinition* TypedDefinition = GetTypedDefinition<FEditorCommandletValidatorDefinition>();

	TSharedPtr<FJsonObject> Projects;
	TSharedPtr<FJsonObject> SubProjects;

	if (!TypedDefinition->EditorRecordsFile.IsEmpty())
	{
		FProjectEditorRecord RecordsFile = FProjectEditorRecord::Load();

		const TSharedPtr<FJsonObject>* ProjectsPtr;
		if (RecordsFile.ProjectEditorJson->TryGetObjectField(FProjectEditorRecord::ProjectsProperty, ProjectsPtr))
		{
			Projects = *ProjectsPtr;
		}

		const TSharedPtr<FJsonObject>* SubProjectsPtr;
		if (RecordsFile.ProjectEditorJson->TryGetObjectField(FProjectEditorRecord::SubProjectProperty, SubProjectsPtr))
		{
			SubProjects = *SubProjectsPtr;
		}
	}

	TMap<FString, TArray<FSCFileRef>> ProjectSet;
	TMap<FString, TArray<FSCFileRef>> SubProjectSet;
	SortFilesByProjects(InFilteredFilesInCL, ProjectSet, SubProjectSet);

	for (const TPair<FString, TArray<FSCFileRef>>& UProjectFiles : ProjectSet)
	{
		FEditorParameters EditorParams;
		EditorParams.EditorArguments = FConfiguration::Substitute(TypedDefinition->ExecutableArguments).Replace(TEXT("$(ProjectName)"), *FPaths::GetBaseFilename(UProjectFiles.Key));

		if (Projects && Projects->HasTypedField(UProjectFiles.Key, EJson::Object))
		{
			EditorParams.EditorExePath = Projects->GetObjectField(UProjectFiles.Key)->GetStringField(FProjectEditorRecord::EngineLocationProperty);
		}
		else
		{
			if (TypedDefinition->ExecutableCandidates.Num() != 0)
			{
				EditorParams.EditorExePath = OptionsProvider.GetSelectedOptionValue(ExecutableOptions);
			}
			else
			{
				EditorParams.EditorExePath = TypedDefinition->ExecutablePath;
			}
		}

		TMap<FString, TSet<FString>> PluginsAdditionalArgs = GetEnableDisablePluginArguments(UProjectFiles.Value);
		if (!PluginsAdditionalArgs.IsEmpty())
		{
			for (TPair<FString, TSet<FString>>& Pair : PluginsAdditionalArgs)
			{
				ExtractArgumentValues(EditorParams.EditorArguments, Pair.Key, Pair.Value);

				EditorParams.EditorArguments.Append(FString::Printf(TEXT(" %s%s"), *Pair.Key, *FString::Join(Pair.Value, TEXT(","))));
			}
		}

		if (!TypedDefinition->FromEditorOnlyArguments.IsEmpty() && FCmdLineParameters::Get().Contains(FSubmitToolCmdLine::EditorFlag))
		{
			EditorParams.EditorArguments.Append(TEXT(" "));
			EditorParams.EditorArguments.Append(TypedDefinition->FromEditorOnlyArguments);
		}

		OutProjectEditorParameters.Add(FPaths::GetBaseFilename(UProjectFiles.Key), MoveTemp(EditorParams));
	}

	for (const TPair<FString, TArray<FSCFileRef>>& SubProjectFiles : SubProjectSet)
	{
		FEditorParameters EditorParams;
		EditorParams.EditorArguments = FConfiguration::Substitute(TypedDefinition->ExecutableArguments).Replace(TEXT("$(ProjectName)"), *TypedDefinition->MainProject);

		if (SubProjects && SubProjects->HasTypedField(SubProjectFiles.Key, EJson::Object))
		{
			const TSharedPtr<FJsonObject> SubProjectJson = SubProjects->GetObjectField(SubProjectFiles.Key);

			if (SubProjectJson->HasTypedField(FProjectEditorRecord::EpicAppProperty, EJson::String))
			{
				EditorParams.EditorArguments.Append(TEXT(" -epicapp=") + SubProjectJson->GetStringField(FProjectEditorRecord::EpicAppProperty));
			}

			if (SubProjectJson->HasTypedField(FProjectEditorRecord::BaseDirProperty, EJson::String))
			{
				EditorParams.EditorArguments.Append(TEXT(" -BaseDir=") + SubProjectJson->GetStringField(FProjectEditorRecord::BaseDirProperty));
			}

			EditorParams.EditorExePath = SubProjectJson->GetStringField(FProjectEditorRecord::EngineLocationProperty);
		}
		else
		{
			if (TypedDefinition->ExecutableCandidates.Num() != 0)
			{
				EditorParams.EditorExePath = OptionsProvider.GetSelectedOptionValue(ExecutableOptions);
			}
			else
			{
				EditorParams.EditorExePath = TypedDefinition->ExecutablePath;
			}
		}

		TMap<FString, TSet<FString>> PluginsAdditionalArgs = GetEnableDisablePluginArguments(SubProjectFiles.Value);
		if (!PluginsAdditionalArgs.IsEmpty())
		{
			for (TPair<FString, TSet<FString>>& Pair : PluginsAdditionalArgs)
			{
				ExtractArgumentValues(EditorParams.EditorArguments, Pair.Key, Pair.Value);

				EditorParams.EditorArguments.Append(FString::Printf(TEXT(" %s%s"), *Pair.Key, *FString::Join(Pair.Value, TEXT(","))));
			}
		}

		if (!TypedDefinition->FromEditorOnlyArguments.IsEmpty() && FCmdLineParameters::Get().Contains(FSubmitToolCmdLine::EditorFlag))
		{
			EditorParams.EditorArguments.Append(TEXT(" "));
			EditorParams.EditorArguments.Append(TypedDefinition->FromEditorOnlyArguments);
		}

		EditorParams.EditorArguments.Append(TEXT(" -ValkyrieProject=") + SubProjectFiles.Key);

		OutProjectEditorParameters.Add(FPaths::GetBaseFilename(SubProjectFiles.Key), MoveTemp(EditorParams));
	}
}


void FEditorCommandletValidator::SortFilesByProjects(const TArray<FSCFileRef>& InFiles, TMap<FString, TArray<FSCFileRef>>& OutProjects, TMap<FString, TArray<FSCFileRef>>& OutSubProjects) const
{
	for (const FSCFileRef& File : InFiles)
	{
		if (FSubmitToolCoreUtils::IsFileInHierarchy(TEXT("*.uefnproject"), File->GetFilename()) || FSubmitToolCoreUtils::IsFileInHierarchy(TEXT("*.uproject"), File->GetFilename()))
		{
			FString CurrentDir = FPaths::GetPath(File->GetFilename());
			while (!CurrentDir.IsEmpty())
			{
				TArray<FString> Projects;
				TArray<FString> SubProjects;
				IFileManager::Get().FindFiles(SubProjects, *(CurrentDir / TEXT("*.uefnproject")), true, false);
				IFileManager::Get().FindFiles(Projects, *(CurrentDir / TEXT("*.uproject")), true, false);

				if (SubProjects.Num() > 0)
				{
					TArray<FSCFileRef>& Filelist = OutSubProjects.FindOrAdd(CurrentDir + "/" + SubProjects[0]);
					Filelist.Add(File);
					break;
				}
				else if (Projects.Num() > 0)
				{
					TArray<FSCFileRef>& Filelist = OutProjects.FindOrAdd(CurrentDir + "/" + Projects[0]);
					Filelist.Add(File);
					break;
				}
				else
				{
					CurrentDir = FPaths::GetPath(CurrentDir);
				}
			}
		}
	}
}

TMap<FString, TSet<FString>> FEditorCommandletValidator::GetEnableDisablePluginArguments(const TArray<FSCFileRef>& InFilesInCL) const
{
	TMap<FSCFileRef, int8> AddRemoveBalance;
	for (const FSCFileRef& File : InFilesInCL)
	{
		if (File->GetFilename().EndsWith(TEXT(".uplugin")))
		{
			if (File->IsAdded())
			{
				int8& CurrentBalance = AddRemoveBalance.FindOrAdd(File);
				++CurrentBalance;
			}
			else if (File->IsDeleted())
			{
				int8& CurrentBalance = AddRemoveBalance.FindOrAdd(File);
				--CurrentBalance;
			}
		}
	}

	const FEditorCommandletValidatorDefinition* TypedDefinition = GetTypedDefinition<FEditorCommandletValidatorDefinition>();
	TSet<FString> PluginsToEnable = TSet<FString>(TypedDefinition->EnablePlugins);
	TSet<FString> PluginsToDisable = TSet<FString>(TypedDefinition->DisablePlugins);

	for (const TPair<FSCFileRef, int8>& Balance : AddRemoveBalance)
	{
		if (Balance.Value > 0)
		{
			PluginsToEnable.Add(FPaths::GetBaseFilename(Balance.Key->GetFilename()));
		}
		else if (Balance.Value < 0)
		{
			PluginsToDisable.Add(FPaths::GetBaseFilename(Balance.Key->GetFilename()));
		}
	}

	TMap<FString, TSet<FString>> CommandlineExtraArgs;

	if (PluginsToDisable.Num() > 0)
	{
		CommandlineExtraArgs.Add(TEXT("-DisablePlugins="), MoveTemp(PluginsToDisable));
	}

	if (PluginsToEnable.Num() > 0)
	{
		CommandlineExtraArgs.Add(TEXT("-EnablePlugins="), MoveTemp(PluginsToEnable));
	}

	return CommandlineExtraArgs;
}

void FEditorCommandletValidator::ExtractArgumentValues(FString& InArguments, const FString& InKey, TSet<FString>& InOutValues, const TCHAR InDelimiter) const
{
	int32 Idx = InArguments.Find(InKey);

	if (Idx != INDEX_NONE)
	{
		int32 LoopIdx = Idx + InKey.Len();
		int32 StartIdx = LoopIdx;

		while (LoopIdx != InArguments.Len())
		{
			if (InArguments[LoopIdx] == InDelimiter)
			{
				InOutValues.Add(InArguments.Mid(StartIdx, LoopIdx - StartIdx));
				StartIdx = LoopIdx + 1;
			}
			else if (InArguments[LoopIdx] == TEXT(' ') || LoopIdx == InArguments.Len() - 1)
			{
				++LoopIdx;
				InOutValues.Add(InArguments.Mid(StartIdx, LoopIdx - StartIdx).TrimEnd());
				break;
			}

			++LoopIdx;
		}

		InArguments.RemoveAt(Idx, LoopIdx - Idx);
		InArguments.TrimEndInline();
	}
}
