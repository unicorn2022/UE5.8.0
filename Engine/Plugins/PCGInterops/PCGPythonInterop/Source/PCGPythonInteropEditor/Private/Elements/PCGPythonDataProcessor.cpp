// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGPythonDataProcessor.h"

#include "PCGContext.h"
#include "PCGDynamicPins.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Utils/PCGLogErrors.h"

#include "Helpers/PCGPythonDataBridge.h"
#include "Helpers/PCGPythonHelpers.h"
#include "PCGEditorCommon.h"
#include "PCGGraph.h"
#include "PCGPythonInteropEditorModule.h"

#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "String/LineEndings.h"

#define LOCTEXT_NAMESPACE "PCGPythonDataProcessorElement"

namespace PCG::Python::DataProcessor::Constants
{
	static const FName ScriptSourceInputPinLabel = "Source";
	static const FText ScriptSourceInputPinTooltip = LOCTEXT("ScriptSourcePinTooltip", "The script may be passed in as an FString attribute input.");
	static constexpr TCHAR ExpectedFileExtension[] = TEXT("py");
	static constexpr TCHAR BridgeVarName[] = TEXT("_pcg_bridge");
	static constexpr TCHAR InputVarName[] = TEXT("data_in");
	static constexpr TCHAR OutputVarName[] = TEXT("data_out");
	static constexpr int32 ScriptBuilderInlineSize = 2048;
}

UPCGPythonDataProcessorSettings::UPCGPythonDataProcessorSettings()
{
	FPCGPinProperties& InputPin = InputDynamicPins.PinProperties.Emplace_GetRef(TEXT("Input"), EPCGDataType::Any, /*bMultiConnections=*/true, /*bMultiData=*/true);
	FPCGPinProperties& OutputPin = OutputDynamicPins.PinProperties.Emplace_GetRef(TEXT("Output"), EPCGDataType::Any, /*bMultiConnections=*/true, /*bMultiData=*/true);
#if WITH_EDITOR
	InputPin.Tooltip = LOCTEXT("DefaultInputPinTooltip", "Access data from this pin in your Python script via `data_in.get_typed_inputs_by_pin_label('Input', <unreal.PCGDataType>)`.");
	OutputPin.Tooltip = LOCTEXT("DefaultOutputPinTooltip", "Emit data to this pin in your Python script via `data_out.add_to_collection(<data>, 'Output', [<tags>])`.");
#endif
}

const FString UPCGPythonDataProcessorSettings::DefaultInlineScript = TEXT(
	"# This example expects PCGPointData on the 'Input' pin.\n"
	"data_list, _ = data_in.get_typed_inputs_by_pin_label('Input', unreal.PCGPointData)\n"
	"for data in data_list:\n"
	"    points = data.get_points()\n"
	"    # Halve the density of each point\n"
	"    new_points = []\n"
	"    for point in points:\n"
	"        point.density = 0.5\n"
	"        new_points.append(point)\n"
	"    result = unreal.PCGPointData()\n"
	"    result.set_points(new_points)\n"
	"    data_out.add_to_collection(result, 'Output', [])\n"
);

#if WITH_EDITOR

bool UPCGPythonDataProcessorSettings::CanEditChange(const FProperty* InProperty) const
{
	using namespace PCG::Python::DataProcessor::Constants;

	if (!InProperty || !Super::CanEditChange(InProperty))
	{
		return false;
	}

	if (ScriptInputMethod == EPCGPythonScriptInputMethod::Input && InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGPythonDataProcessorSettings, ScriptSource))
	{
		if (const UPCGNode* Node = Cast<UPCGNode>(GetOuter()))
		{
			const UPCGPin* Pin = Node->GetInputPin(ScriptSourceInputPinLabel);
			return (Pin && Pin->IsConnected()) || !IsPinDefaultValueActivated(ScriptSourceInputPinLabel);
		}
	}

	return true;
}

EPCGChangeType UPCGPythonDataProcessorSettings::GetChangeTypeForProperty(const FName& InPropertyName) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(InPropertyName);

	if (InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGPythonDataProcessorSettings, ScriptInputMethod))
	{
		ChangeType |= EPCGChangeType::Settings | EPCGChangeType::Structural | EPCGChangeType::Cosmetic;
	}
	else if (InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGPythonDataProcessorSettings, ScriptPath))
	{
		ChangeType |= EPCGChangeType::Settings | EPCGChangeType::Cosmetic;
	}

	return ChangeType;
}

FName UPCGPythonDataProcessorSettings::GetDefaultNodeName() const
{
	return FName(TEXT("PythonDataProcessor"));
}

FText UPCGPythonDataProcessorSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Python Data Processor");
}

FText UPCGPythonDataProcessorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Execute a Python script with direct access to PCG data collections.\n"
		"- `data_in`: the input collection containing all data from input pins.\n"
		"- `data_out`: the output writer. Call methods on it (e.g. `add_to_collection`, `set_output_collection`) to emit results to output pins.\n"
		"Dynamic pins accept any PCG data.");
}

EPCGSettingsType UPCGPythonDataProcessorSettings::GetType() const
{
	return EPCGSettingsType::Generic;
}

FString UPCGPythonDataProcessorSettings::GetAdditionalTitleInformation() const
{
	switch (ScriptInputMethod)
	{
		case EPCGPythonScriptInputMethod::Input:
			return LOCTEXT("InputTitleInformation", "Source Input").ToString();
		case EPCGPythonScriptInputMethod::File:
			return FText::Format(LOCTEXT("FileTitleInformation", "Source File: {0}"), FText::FromString(FPaths::GetCleanFilename(ScriptPath.FilePath))).ToString();
	}

	return {};
}

TArray<FPCGPinProperties> UPCGPythonDataProcessorSettings::InputPinProperties() const
{
	return GetCombinedPinProperties(EPCGPinDirection::Input);
}

TArray<FPCGPinProperties> UPCGPythonDataProcessorSettings::OutputPinProperties() const
{
	return GetCombinedPinProperties(EPCGPinDirection::Output);
}

#endif // WITH_EDITOR

FPCGElementPtr UPCGPythonDataProcessorSettings::CreateElement() const
{
	return MakeShared<FPCGPythonDataProcessorElement>();
}

const FPCGDynamicPinContainer* UPCGPythonDataProcessorSettings::GetDynamicPinContainer(const EPCGPinDirection Direction) const
{
	switch (Direction)
	{
		case EPCGPinDirection::Input:
			return &InputDynamicPins;
		case EPCGPinDirection::Output:
			return &OutputDynamicPins;
		default: return nullptr;
	}
}

FPCGDynamicPinContainer* UPCGPythonDataProcessorSettings::GetMutableDynamicPinContainer(const EPCGPinDirection Direction)
{
	switch (Direction)
	{
		case EPCGPinDirection::Input:
			return &InputDynamicPins;
		case EPCGPinDirection::Output:
			return &OutputDynamicPins;
		default: return nullptr;
	}
}

TArray<FPCGPinProperties> UPCGPythonDataProcessorSettings::GetStaticPinProperties(const EPCGPinDirection Direction) const
{
	using namespace PCG::Python::DataProcessor::Constants;

	TArray<FPCGPinProperties> StaticPins;

	if (Direction == EPCGPinDirection::Input)
	{
		if (ScriptInputMethod == EPCGPythonScriptInputMethod::Input)
		{
			FPCGPinProperties& ScriptSourcePin = StaticPins.Emplace_GetRef(ScriptSourceInputPinLabel, EPCGDataType::Param, /*bMultiConnections=*/false, /*bMultiData=*/false);
			ScriptSourcePin.AllowedTypes.CustomSubtype = static_cast<int32>(EPCGMetadataTypes::String);
#if WITH_EDITOR
			ScriptSourcePin.Tooltip = ScriptSourceInputPinTooltip;
#endif // WITH_EDITOR
		}
	}
	else
	{
		FPCGPinProperties& DepPin = StaticPins.Emplace_GetRef(PCGPinConstants::DefaultExecutionDependencyLabel, EPCGDataType::Any);
#if WITH_EDITOR
		DepPin.Tooltip = PCGPinConstants::Tooltips::ExecutionDependencyTooltip;
#endif // WITH_EDITOR
		DepPin.Usage = EPCGPinUsage::DependencyOnly;
		DepPin.SetAdvancedPin();
	}

	return StaticPins;
}

#if WITH_EDITOR
FPCGPinProperties UPCGPythonDataProcessorSettings::CreateDefaultDynamicPin(const EPCGPinDirection Direction) const
{
	const FName DefaultLabel = (Direction == EPCGPinDirection::Input) ? FName(TEXT("NewInput")) : FName(TEXT("NewOutput"));
	return FPCGPinProperties(DefaultLabel, EPCGDataType::Any, /*bMultiConnections=*/true, /*bMultiData=*/true);
}

EPCGSettingDefaultValueExtraFlags UPCGPythonDataProcessorSettings::GetDefaultValueExtraFlags(const FName PinLabel) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return EPCGSettingDefaultValueExtraFlags::WideText | EPCGSettingDefaultValueExtraFlags::MultiLineText;
	}

	return EPCGSettingDefaultValueExtraFlags::None;
}

FString UPCGPythonDataProcessorSettings::GetPinDefaultValueAsString(const FName PinLabel) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return InlineScript;
	}

	return {};
}

FString UPCGPythonDataProcessorSettings::GetPinInitialDefaultValueString(const FName PinLabel) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return DefaultInlineScript;
	}

	return {};
}

void UPCGPythonDataProcessorSettings::ResetDefaultValues()
{
	ResetDefaultValue(PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel);
}

void UPCGPythonDataProcessorSettings::ResetDefaultValue(const FName PinLabel)
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		SetPinDefaultValue(PinLabel, DefaultInlineScript);
	}
}

void UPCGPythonDataProcessorSettings::SetPinDefaultValue(const FName PinLabel, const FString& DefaultValue, bool bCreateIfNeeded)
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		Modify();
		InlineScript = DefaultValue;
		OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Node | EPCGChangeType::Edge);
	}
}

void UPCGPythonDataProcessorSettings::SetPinDefaultValueIsActivated(const FName PinLabel, const bool bIsActivated, const bool bDirtySettings)
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		if (bIsDefaultValueActivated != bIsActivated)
		{
			if (bDirtySettings) { Modify(); }
			bIsDefaultValueActivated = bIsActivated;
			if (bDirtySettings) { OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Node | EPCGChangeType::Edge); }
		}
	}
}
#endif // WITH_EDITOR

bool UPCGPythonDataProcessorSettings::DefaultValuesAreEnabled() const
{
	return true;
}

bool UPCGPythonDataProcessorSettings::IsPinDefaultValueEnabled(const FName PinLabel) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return ScriptInputMethod == EPCGPythonScriptInputMethod::Input;
	}

	return false;
}

bool UPCGPythonDataProcessorSettings::IsPinDefaultValueActivated(const FName PinLabel) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return bIsDefaultValueActivated;
	}

	return false;
}

EPCGMetadataTypes UPCGPythonDataProcessorSettings::GetPinDefaultValueType(const FName PinLabel) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return EPCGMetadataTypes::String;
	}

	return EPCGMetadataTypes::Unknown;
}

bool UPCGPythonDataProcessorSettings::IsPinDefaultValueMetadataTypeValid(const FName PinLabel, const EPCGMetadataTypes DataType) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return DataType == EPCGMetadataTypes::String;
	}

	return false;
}

bool UPCGPythonDataProcessorSettings::CreateInitialDefaultValueAttribute(const FName PinLabel, UPCGMetadata* OutMetadata) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return OutMetadata && OutMetadata->CreateAttribute<FString>(NAME_None, DefaultInlineScript, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
	}

	return false;
}

bool UPCGPythonDataProcessorSettings::IsInputPinRequiredByExecution(const UPCGPin* InPin) const
{
	return InPin && (InPin->IsConnected() || !IsPinDefaultValueEnabled(InPin->Properties.Label) || !IsPinDefaultValueActivated(InPin->Properties.Label));
}

EPCGMetadataTypes UPCGPythonDataProcessorSettings::GetPinInitialDefaultValueType(const FName PinLabel) const
{
	if (PinLabel == PCG::Python::DataProcessor::Constants::ScriptSourceInputPinLabel)
	{
		return EPCGMetadataTypes::String;
	}

	return EPCGMetadataTypes::Unknown;
}

bool FPCGPythonDataProcessorElement::ExecuteInternal(FPCGContext* InContext) const
{
	using namespace PCG::Python::DataProcessor::Constants;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGPythonDataProcessorElement::Execute);
	check(InContext);

#if WITH_EDITOR
	const UPCGPythonDataProcessorSettings* Settings = InContext->GetInputSettings<UPCGPythonDataProcessorSettings>();
	check(Settings);

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("PythonNotAvailable", "Python Interpreter is not available."), InContext);
		return true;
	}

	// Build script text from Source pin or file.
	FString UserScript;

	// From user input
	if (Settings->ScriptInputMethod == EPCGPythonScriptInputMethod::Input)
	{
		TArray<FPCGTaggedData> InputData = InContext->InputData.GetInputsByPin(ScriptSourceInputPinLabel);
		if (!InputData.IsEmpty())
		{
			const FPCGTaggedData& Input = InputData[0];
			FPCGAttributePropertyInputSelector Selector = Settings->ScriptSource.CopyAndFixLast(Input.Data);
			const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Input.Data, Selector);
			const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Input.Data, Selector);
			if (Accessor && Keys && Keys->GetNum() > 0)
			{
				if (!Accessor->Get<FString>(UserScript, 0, *Keys))
				{
					PCGLog::Metadata::LogFailToGetAttributeError<FString>(Selector, Accessor.Get(), InContext);
					return true;
				}
			}
		}
		else if (Settings->IsPinDefaultValueActivated(ScriptSourceInputPinLabel))
		{
			UserScript = Settings->GetPinDefaultValueAsString(ScriptSourceInputPinLabel);
		}

		if (UserScript.IsEmpty())
		{
			return true;
		}
	}
	else // From file
	{
		if (Settings->ScriptPath.FilePath.IsEmpty())
		{
			return true;
		}

		if (const FString Extension = FPaths::GetExtension(Settings->ScriptPath.FilePath); !Extension.Equals(ExpectedFileExtension))
		{
			const FString DisplayExtension = Extension.IsEmpty() ? LOCTEXT("NoExtension", "None").ToString() : FString::Printf(TEXT(".%s"), *Extension);
			PCGLog::InputOutput::LogInvalidFileType(DisplayExtension, ExpectedFileExtension, InContext);
			return true;
		}

		if (!IFileManager::Get().FileExists(*Settings->ScriptPath.FilePath))
		{
			PCGLog::InputOutput::LogFileNotFound(Settings->ScriptPath.FilePath, InContext);
			return true;
		}

		FString FileContent;
		if (!FFileHelper::LoadFileToString(FileContent, *Settings->ScriptPath.FilePath))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("FileReadFailed", "Failed to read Python file: {0}"), FText::FromString(Settings->ScriptPath.FilePath)), InContext);
			return true;
		}

		UserScript = MoveTemp(FileContent);
	}

	// Create the bridge UObject with a unique name for this execution.
	const FString BridgeName = TEXT("_PCGBridge_") + FGuid::NewGuid().ToString();
	UPCGPythonDataBridge* Bridge = NewObject<UPCGPythonDataBridge>(GetTransientPackage(), *BridgeName);
	Bridge->Initialize(InContext->InputData);

	// Generate the script with bridge injection. The user script is wrapped in try/finally so that
	// the injected names are always removed from the public scope, even if the script raises.
	const FString BridgePath = Bridge->GetPathName();

	TStringBuilder<ScriptBuilderInlineSize> ScriptBuilder;
	ScriptBuilder.Appendf(TEXT("%s = unreal.find_object(None, '%s')\n"), BridgeVarName, *BridgePath);
	ScriptBuilder.Appendf(TEXT("%s = %s.get_input_collection()\n"), InputVarName, BridgeVarName);
	ScriptBuilder.Appendf(TEXT("%s = %s\n"), OutputVarName, BridgeVarName);
	// Indent every line of the user script by 4 spaces so it becomes the body of the try block.
	// Normalize CRLF/CR to LF up front so the indent-insertion Replace handles every platform's line endings uniformly.
	// Tabs are expanded to 4 spaces so prefixing our a 4-space indent does not produce mixed tab/space indentation,
	// which Python 3 rejects with TabError.
	// TrimEnd ensures no trailing whitespace leaves `finally:` sitting on an indented line.
	FString NormalizedScript = UserScript;
	UE::String::FromHostLineEndingsInline(NormalizedScript);
	NormalizedScript.ReplaceInline(TEXT("\t"), TEXT("    "));
	NormalizedScript.TrimEndInline();

	ScriptBuilder.Append(TEXT("try:\n    "));
	ScriptBuilder.Append(NormalizedScript.Replace(TEXT("\n"), TEXT("\n    ")));
	ScriptBuilder.Append(TEXT("\n"));
	ScriptBuilder.Appendf(TEXT("finally:\n    del %s, %s, %s\n"), BridgeVarName, InputVarName, OutputVarName);

	FPythonCommandEx ExecCmd;
	ExecCmd.Command = FString(ScriptBuilder);
	ExecCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	ExecCmd.FileExecutionScope = EPythonFileExecutionScope::Public;

	const bool bExecSucceeded = PythonPlugin->ExecPythonCommandEx(ExecCmd);
	if (!bExecSucceeded)
	{
		const FString ErrorSummary = PCG::Python::Helpers::ExtractErrorSummary(ExecCmd.CommandResult);
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("PythonExecFailed", "Python: {0}"), FText::FromString(ErrorSummary)), InContext);
	}
	else if (Bridge->HasOutputCollection())
	{
		InContext->OutputData = Bridge->GetOutputCollection();
	}

	// Cleanup: mark the bridge for GC.
	Bridge->MarkAsGarbage();

	// Toast notification.
	if (!Settings->bMuteEditorToast && InContext->Node && InContext->Node->GetGraph())
	{
		const FText StatusText = bExecSucceeded ? LOCTEXT("PythonToastSuccess", "Success") : LOCTEXT("PythonToastFailure", "Failure");
		FPCGEditorCommon::Helpers::DispatchEditorToast(
			LOCTEXT("PythonScriptExecutionToast", "[PCG] Python"),
			FText::Format(INVTEXT("{0}: {1} - {2}"),
				FText::FromString(InContext->Node->GetGraph()->GetName()),
				InContext->Node->GetNodeTitle(EPCGNodeTitleType::ListView),
				StatusText));
	}

#else // WITH_EDITOR
	PCGLog::LogErrorOnGraph(LOCTEXT("NodeIsEditorOnly", "The Python Data Processor node is currently Editor-only and should not be used at runtime."), InContext);
#endif // WITH_EDITOR

	return true;
}

#undef LOCTEXT_NAMESPACE
