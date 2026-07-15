// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextObjectStore.h"
#include "CoreTypes.h"
#include "Editor.h"
#include "EditorInteractiveGizmoManager.h"
#include "Editor/EditorEngine.h"
#include "EditorGizmos/EditorTransformGizmoUtil.h"
#include "EditorGizmos/EditorTRSGizmoBuilder.h"
#include "EditorInteractiveGizmoSubsystem.h"
#include "EditorModeManager.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Tools/EdModeInteractiveToolsContext.h"

#include <span>

#include "EditorTRSSettings.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "FEditorTRSGizmoModule"

class FEditorTRSGizmoModule : public IModuleInterface
{
private:
	static void OnUsesNewTRSGizmosChanged(bool bInUsesNewTRSGizmos)
	{
		const bool bWillUseNewGizmos = bInUsesNewTRSGizmos && GetDefault<UEditorTRSEditorSettings>()->bEnableExperimentalGizmo58;
		
		static bool bUsesNewGizmos = false;

		if (bUsesNewGizmos == bWillUseNewGizmos)
		{
			return;
		}

		bUsesNewGizmos = bWillUseNewGizmos;

		constexpr const TCHAR* ITFGizmoEnabledCVars[] = {
			TEXT("Editor.EnableITFCursorOverrideSupport"),
			TEXT("DragTools.EnableITFTools"),
			TEXT("ViewportInteractions.EnableITFInteractions"),
			TEXT("Editor.UnboundedCursorDrag")
		};

		constexpr const TCHAR* ITFGizmoDisabledCVars[] = {
			TEXT("Editor.AlwaysSnapPercentageBasedScale")
		};

		auto ToggleCVars = [](auto& CVars, bool bEnable)
		{
			for (const TCHAR* CVarToToggle : CVars)
			{
				if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarToToggle))
				{
					if (CVar->GetBool() != bEnable)
					{
						CVar->Set(bEnable);
					}
				}
			}
		};

		ToggleCVars(ITFGizmoEnabledCVars, bWillUseNewGizmos);
		ToggleCVars(ITFGizmoDisabledCVars, !bWillUseNewGizmos);
	}
	void OnGizmoCreated(UTransformGizmo*)
	{
		OnUsesNewTRSGizmosChanged(UEditorInteractiveGizmoManager::UsesNewTRSGizmos());

		if (!TRSGizmoChangeDelegate.IsValid())
		{
			TRSGizmoChangeDelegate = UEditorInteractiveGizmoManager::OnUsesNewTRSGizmosChangedDelegate().AddStatic(
				&FEditorTRSGizmoModule::OnUsesNewTRSGizmosChanged
			);
		}
	}

	void OnPostEngineInit()
	{
		if (UEditorInteractiveGizmoSubsystem* GizmoSubsystem = GEditor->GetEditorSubsystem<UEditorInteractiveGizmoSubsystem>())
		{
			GizmoSubsystem->RegisterTransformGizmoBuilder(NewObject<UEditorTRSGizmoBuilder>());
		}
	}

	void OnLevelEditorCreated(TSharedPtr<ILevelEditor> InLevelEditor)
	{
		InLevelEditor->GetEditorModeManager().OnEditorModeIDChanged().AddRaw(this, &FEditorTRSGizmoModule::OnModeChanged);
	}

	void OnModeChanged(const FEditorModeID&, bool)
	{
		OnGizmoCreated(nullptr);
	}

public:
	virtual void StartupModule() override
	{
		if (!IsRunningCommandlet())
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			LevelEditorModule.OnLevelEditorCreated().AddRaw(this, &FEditorTRSGizmoModule::OnLevelEditorCreated);

			// The Interactive Tools Framework needs to be explicitly loaded, at the very least to make sure that
			// the InteractiveToolsSelectionStoreSubsystem gets initialized.
			FModuleManager::Get().LoadModule(TEXT("InteractiveToolsFramework"));

			if (GEngine && GEngine->IsInitialized())
			{
				OnPostEngineInit();
			}
			else
			{
				FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FEditorTRSGizmoModule::OnPostEngineInit);
			}
		}
	}

	virtual void ShutdownModule() override
	{
		if (GEngine && !IsRunningCommandlet())
		{
			FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);

			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			LevelEditorModule.OnLevelEditorCreated().RemoveAll(this);
		}

		if (TRSGizmoChangeDelegate.IsValid())
		{
			UEditorInteractiveGizmoManager::OnUsesNewTRSGizmosChangedDelegate().Remove(TRSGizmoChangeDelegate);
		}
	}

	FDelegateHandle TRSGizmoChangeDelegate;
};

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FEditorTRSGizmoModule, EditorTRSGizmo)
