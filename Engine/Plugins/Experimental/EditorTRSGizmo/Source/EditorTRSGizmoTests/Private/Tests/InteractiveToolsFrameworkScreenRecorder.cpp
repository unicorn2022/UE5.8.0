// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/InteractiveToolsFrameworkTestUtilities.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

#include "Tests/InteractiveToolsFrameworkTestUtilities.inl"

#include "Algo/ForEach.h"
#include "Async/ParallelFor.h"
#include "EditorModeTools.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "HAL/FileManager.h"
#include "OBSClient.h"
#include "OBSClient.inl"
#include "OBSMessages.h"
#include "OBSMessages.inl"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SLevelViewport.h"
#include "Tests/AutomationEditorCommon.h"
#include "Widgets/Docking/SDockTab.h"

namespace UE::Editor::InteractiveToolsFramework::Tests
{
	namespace Private
	{
		TSharedPtr<SWindow> GetLevelEditorWindow()
		{
			const FName LevelEditorTabId("LevelEditor");
			if (const TSharedPtr<SDockTab> LevelEditorTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(LevelEditorTabId)))
			{
				if (TSharedPtr<SWindow> ParentWindow = LevelEditorTab->GetParentWindow())
				{
					return ParentWindow;
				}
			}

			// If we haven't found it by tab, look for it by title
			for (const TSharedRef<SWindow>& TopLevelWindow : FSlateApplicationBase::Get().GetTopLevelWindows())
			{
				if (TopLevelWindow->GetTitle().ToString().Contains(TEXT("Unreal Editor")))
				{
					return TopLevelWindow;
				}
			}

			return nullptr;
		}
	}

	FTestRecorder::FTestRecorder(const FString& InFileName, const bool bInAddOverlay)
		: OBSClient(MakeShared<OBS::FOBSClient>())
		, RecorderState(ERecorderState::None)
		, FileName(InFileName)
		, bHasOverlay(bInAddOverlay)
		, RecorderScene(MakeShared<FTestRecorder::FScene>())
	{
	}

	TFuture<bool> FTestRecorder::Begin()
	{
		// Update State
		if (GetState() != ERecorderState::Connected)
		{
			SetState(ERecorderState::Connecting);
		}

		// Setup Recorder Settings (for OBS)
		{
			static FString RecordingPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*FPaths::Combine(FPaths::ProjectSavedDir()));
			RecorderSettings.AdvancedOutputFilePath.Value = RecordingPath;

			RecorderSettings.OutputFilenameFormatting.Value = FString::Format(TEXT("OBSTestRecording_{0}"), FStringFormatOrderedArguments({ FileName }));

			RecorderSettings.AdvancedRecordingFormat2.Value = TEXT("hybrid_mp4"); // mkv, mp4 don't support chapters - this does

			RecorderSettings.CurrentScene.Name = RecorderScene->Name;
		}

		TSharedPtr<TPromise<bool>> StartRecordPromise = MakeShared<TPromise<bool>>();

		OBSClient->Connect()
		.Next([WeakThis = AsWeak(), OBS = OBSClient, StartRecordPromise](bool bSuccess)
		{
			TSharedPtr<FTestRecorder> StrongThis = WeakThis.Pin();
			if (!bSuccess || !StrongThis.IsValid())
			{
				// Update State
				if (StrongThis.IsValid())
				{
					// We tried to connect, but couldn't, so flag as unavailable
					StrongThis->SetState(ERecorderState::Unavailable);
				}

				StartRecordPromise->SetValue(false);
				return;
			}

			// Update State
			StrongThis->SetState(ERecorderState::Connected);

			// Get and Store User Settings
			TFuture<bool> GetUserSettingsFuture = StrongThis->GetSettings(StrongThis->UserSettings)
			.Next([WeakThis, OBS, StartRecordPromise](const TValueOrError<FSettings, void>& InUserSettings) -> bool
			{
				if (InUserSettings.HasError())
				{
					StartRecordPromise->SetValue(false);
					return false;
				}

				const TSharedPtr<FTestRecorder> StrongerThis = WeakThis.Pin();
				if (!StrongerThis.IsValid())
				{
					StartRecordPromise->SetValue(false);
					return false;
				}

				StrongerThis->UserSettings = InUserSettings.GetValue();

				return true;
			});

			TSharedPtr<TPromise<bool>> SetRecorderSettingsPromise = MakeShared<TPromise<bool>>();

			GetUserSettingsFuture
			.Next([WeakThis, OBS, StartRecordPromise, SetRecorderSettingsPromise](const bool bGotUserSettings)
			{
				TSharedPtr<FTestRecorder> StrongerThis = WeakThis.Pin();
				if (!bGotUserSettings || !StrongerThis.IsValid())
				{
					SetRecorderSettingsPromise->SetValue(false);
					StartRecordPromise->SetValue(false);
					return;
				}

				StrongerThis->SetSettings(StrongerThis->RecorderSettings)
				.Next([SetRecorderSettingsPromise](const bool bDidSetSettings)
				{
					SetRecorderSettingsPromise->SetValue(bDidSetSettings);
				});
			});

			TSharedPtr<TPromise<bool>> SetupScenePromise = MakeShared<TPromise<bool>>();
			{
				SetRecorderSettingsPromise->GetFuture()
				.Next([WeakThis, OBS, StartRecordPromise, SetupScenePromise](const bool bDidSetSettings)
				{
					const TSharedPtr<FTestRecorder> StrongerThis = WeakThis.Pin();
					if (!bDidSetSettings || !StrongerThis.IsValid())
					{
						SetupScenePromise->SetValue(false);
						return;
					}

					StrongerThis->SetupScene()
					.Next([WeakThis, SetupScenePromise](const TValueOrError<FTestRecorder::FScene, void>& InScene)
					{
						SetupScenePromise->SetValue(!InScene.HasError());
					});
				});
			}

			SetupScenePromise->GetFuture()
			.Next([WeakThis, OBS, StartRecordPromise](const bool bDidSetupScene)
			{
				if (!bDidSetupScene)
				{
					StartRecordPromise->SetValue(false);
					return;
				}

				// Update State
				if (const TSharedPtr<FTestRecorder> StrongerThis = WeakThis.Pin())
				{
					StrongerThis->SetState(ERecorderState::StartingRecording);
				}

				OBS->StartRecord()
				.Next([WeakThis, StartRecordPromise](const bool bSentMessage)
				{
					if (const TSharedPtr<FTestRecorder> SuperStrongThis = WeakThis.Pin())
					{
						// Update State
						if (bSentMessage)
						{
							SuperStrongThis->SetState(ERecorderState::Recording);
						}
					}

					StartRecordPromise->SetValue(bSentMessage);
				});
			});
		});

		return StartRecordPromise->GetFuture()
		.Next([WeakThis = AsWeak()](const bool bBeganRecording)
		{
			// Update State
			if (const TSharedPtr<FTestRecorder> StrongThis = WeakThis.Pin())
			{
				if (StrongThis->GetState() != ERecorderState::Unavailable && !bBeganRecording)
				{
					// We successfully connected, but didn't begin recording
					StrongThis->SetState(ERecorderState::Connected);
				}
			}

			return bBeganRecording;
		});
	}

	TFuture<bool> FTestRecorder::BeginOrResume()
	{
		TSharedPtr<TPromise<bool>> BeginOrResumePromise = MakeShared<TPromise<bool>>();

		const ERecorderState State = GetState();

		// Already recording (or about to)
		if (State == ERecorderState::StartingRecording
			|| State == ERecorderState::Recording)
		{
			// @note: this isn't ideal - we might be still in the process of starting to record, but not actually recording
			BeginOrResumePromise->SetValue(true);
		}
		else if (State == ERecorderState::Paused)
		{
			// Verify that we are actually paused
			OBSClient->GetRecordStatus()
			.Next([WeakThis = AsWeak(), OBS = OBSClient, BeginOrResumePromise](const TValueOrError<OBS::FGetRecordStatusResponse, void>& InStatus)
			{
				if (InStatus.HasError() || !InStatus.GetValue().bIsOutputPaused)
				{
					if (const TSharedPtr<FTestRecorder> StrongThis = WeakThis.Pin())
					{
						StrongThis->Begin()
						.Next([BeginOrResumePromise](const bool bInDidBegin)
						{
							BeginOrResumePromise->SetValue(bInDidBegin);
						});
					}
					else
					{
						BeginOrResumePromise->SetValue(false);
					}

					return;
				}

				OBS->ResumeRecord()
				.Next([WeakThis, BeginOrResumePromise](const bool bResumed)
				{
					if (const TSharedPtr<FTestRecorder> StrongThis = WeakThis.Pin())
					{
						// Update State
						StrongThis->SetState(ERecorderState::Recording);						
					}

					BeginOrResumePromise->SetValue(bResumed);
				});
			});
		}
		else
		{
			// Otherwise, begin
			Begin()
			.Next([BeginOrResumePromise](const bool bInDidBegin)
			{
				BeginOrResumePromise->SetValue(bInDidBegin);
			});
		}

		return BeginOrResumePromise->GetFuture();
	}

	TFuture<bool> FTestRecorder::Pause()
	{
		if (!IsRecording() || !OBSClient->IsConnected())
		{
			return MakeFulfilledPromise<bool>(false).GetFuture();
		}

		return OBSClient->PauseRecord()
		.Next([WeakThis = AsWeak()](const bool bInPaused)
		{
			if (bInPaused)
			{
				if (const TSharedPtr<FTestRecorder> StrongThis = WeakThis.Pin())
				{
					// Update State
					StrongThis->SetState(ERecorderState::Paused);
				}
			}

			return bInPaused;
		});
	}

	void FTestRecorder::CreateNamedChapter(const FString& InChapterName) const
	{
		if (OBSClient->IsConnected())
		{
			OBSClient->CreateRecordChapter(InChapterName);

			if (bHasOverlay)
			{
				// We only need to set the text here, rather than send the whole settings object
				const TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
				Settings->SetStringField(TEXT("text"), InChapterName);

				// @note: this assumes the TextItem has already been validated/setup
				OBSClient->SetInputSettings(
					RecorderScene->TextItem.SourceName,
					RecorderScene->TextItem.SourceUniqueId,
					Settings);
			}
		}
	}

	TFuture<bool> FTestRecorder::End()
	{
		if (!IsRecording() || !OBSClient->IsConnected())
		{
			return MakeFulfilledPromise<bool>(false).GetFuture();
		}

		TSharedPtr<TPromise<bool>> StopRecordPromise = MakeShared<TPromise<bool>>();

		// Update State
		const ERecorderState PreviousState = GetState();
		SetState(ERecorderState::StoppingRecording);

		OBSClient->StopRecord()
		.Next([OBS = OBSClient, WeakThis = AsWeak(), StopRecordPromise](bool bSentMessage)
		{
			const TSharedPtr<FTestRecorder> StrongThis = WeakThis.Pin();
			if (!bSentMessage || !StrongThis.IsValid())
			{
				StopRecordPromise->SetValue(false);
				return;
			}

			// Restore user parameters
			TSharedPtr<TPromise<bool>> SetProfileParametersPromise = MakeShared<TPromise<bool>>();
			{
				StrongThis->SetSettings(StrongThis->UserSettings)
				.Next([SetProfileParametersPromise](const bool bDidSetSettings)
				{
					SetProfileParametersPromise->SetValue(bDidSetSettings);
				});
			}

			SetProfileParametersPromise->GetFuture()
			.Next([OBS, WeakThis, StopRecordPromise](const bool bDidSetSettings)
			{
				// Update State
				if (const TSharedPtr<FTestRecorder> StrongerThis = WeakThis.Pin())
				{
					StrongerThis->SetState(ERecorderState::Disconnected);
				}

				OBS->Close();
				StopRecordPromise->SetValue(bDidSetSettings);
			});
		});

		return StopRecordPromise->GetFuture()
		.Next([WeakThis = AsWeak(), PreviousState](const bool bStoppedRecording)
		{
			if (const TSharedPtr<FTestRecorder> StrongThis = WeakThis.Pin())
			{
				// Update State, unless we successfully closed the connection
				if (!bStoppedRecording || StrongThis->GetState() != ERecorderState::Disconnected)
				{
					StrongThis->SetState(PreviousState);
				}
			}

			return bStoppedRecording;
		});
	}

	bool FTestRecorder::IsRecording() const
	{
		const ERecorderState State = GetState();
		return State == ERecorderState::Recording || State == ERecorderState::StartingRecording || State == ERecorderState::Paused;
	}

	FTestRecorder::ERecorderState FTestRecorder::GetState() const
	{
		const ERecorderState State = RecorderState.load();
		return State;
	}

	TFuture<TValueOrError<FTestRecorder::FSettings, void>> FTestRecorder::GetSettings(const FSettings& InSettings) const
	{
		TSharedPtr<TPromise<TValueOrError<FTestRecorder::FSettings, void>>> GetSettingsPromise = MakeShared<TPromise<TValueOrError<FSettings, void>>>();

		/** Make ptr so we can write/return values. */
		TSharedPtr<FSettings> Settings = MakeShared<FSettings>(InSettings);

		// Connect if needed
		OBSClient->Connect()
		.Next([OBS = OBSClient, GetSettingsPromise, Settings](bool bSuccess)
		{
			if (!bSuccess)
			{
				GetSettingsPromise->SetValue(MakeError());
				return;
			}

			OBS->GetProfileParameters(Settings->GetParameters())
			.Next([OBS, GetSettingsPromise, Settings](const TValueOrError<TArray<OBS::FParameter>, void>& InParameters)
			{
				if (InParameters.HasError())
				{
					GetSettingsPromise->SetValue(MakeError());
					return;
				}

				Settings->SetParameters(InParameters.GetValue());

				OBS->GetCurrentProgramScene()
				.Next([Settings, GetSettingsPromise](const TValueOrError<OBS::FScene, void>& InCurrentScene)
				{
					if (InCurrentScene.HasValue())
					{
						Settings->CurrentScene = InCurrentScene.GetValue();
						GetSettingsPromise->SetValue(MakeValue(*Settings));
						return;
					}

					GetSettingsPromise->SetValue(MakeError());
				});
			});
		});

		return GetSettingsPromise->GetFuture();
	}

	TFuture<bool> FTestRecorder::SetSettings(const FSettings& InSettings) const
	{
		TSharedPtr<TPromise<bool>> SetSettingsPromise = MakeShared<TPromise<bool>>();

		OBSClient->Connect()
		.Next([OBS = OBSClient, SetSettingsPromise, InSettings](const bool bSuccess)
		{
			if (!bSuccess)
			{
				SetSettingsPromise->SetValue(false);
				return;
			}

			OBS->SetProfileParameters(InSettings.GetParameters())
			.Next([OBS, SetSettingsPromise, InSettings](const bool bInSuccess)
			{
				if (!bInSuccess)
				{
					SetSettingsPromise->SetValue(false);
					return;
				}

				OBS->SetCurrentProgramScene(InSettings.CurrentScene)
				.Next([SetSettingsPromise](const bool bDidSetCurrentScene)
				{
					SetSettingsPromise->SetValue(bDidSetCurrentScene);
				});
			});
		});

		return SetSettingsPromise->GetFuture();
	}

	TFuture<TValueOrError<FTestRecorder::FScene, void>> FTestRecorder::SetupScene()
	{
		// Creates/Modifies the scene with desired items

		if (!ensure(OBSClient->IsConnected()))
		{
			return MakeFulfilledPromise<TValueOrError<FTestRecorder::FScene, void>>(MakeError()).GetFuture();
		}

		// Initialize varying scene params
		{
			auto GetOrCreateSettingsObject = [](OBS::FSceneItem& InSceneItem) -> TSharedPtr<FJsonObject>
			{
				if (InSceneItem.Settings.IsValid())
				{
					return InSceneItem.Settings;
				}

				TSharedRef<FJsonObject> SettingsObject = MakeShared<FJsonObject>();
				InSceneItem.Settings = SettingsObject;
				return SettingsObject;
			};

			// Window Capture
			{
				const TSharedPtr<FJsonObject> Settings = GetOrCreateSettingsObject(RecorderScene->CaptureItem);

				const TSharedPtr<SWindow> TopLevelWindow = Private::GetLevelEditorWindow();
				FString WindowTitle;
				if (TopLevelWindow.IsValid())
				{
					WindowTitle = TopLevelWindow->GetTitle().ToString();
				}

				const FString ExecutableName = FPlatformProcess::ExecutableName(false);

				Settings->SetStringField(
					TEXT("window"),
					FString::Format(TEXT("{0}:{1}:{2}"),
					FStringFormatOrderedArguments({
					WindowTitle,
						TEXT("UnrealWindow"),
					ExecutableName
				})));
			}

			// Text
			{
				const TSharedPtr<FJsonObject> Settings = GetOrCreateSettingsObject(RecorderScene->TextItem);

				RecorderScene->TextItem.Settings = Settings;
			}
		}

		TSharedPtr<TPromise<TValueOrError<FTestRecorder::FScene, void>>> SetupScenePromise = MakeShared<TPromise<TValueOrError<FTestRecorder::FScene, void>>>();

		TSharedPtr<FTestRecorder::FScene> OutputScene = RecorderScene;

		auto GetOrCreateScene = [WeakThis = AsWeak(), OBS = OBSClient, SceneName = RecorderScene->Name, OutputScene]() -> TFuture<TValueOrError<OBS::FScene, void>>
		{
			TSharedPtr<TPromise<TValueOrError<OBS::FScene, void>>> GetOrCreateScenePromise = MakeShared<TPromise<TValueOrError<OBS::FScene, void>>>();
			OBS->GetSceneList()
			.Next([GetOrCreateScenePromise, SceneName](const TValueOrError<OBS::FGetSceneListResponse, void>& InResponse) -> TValueOrError<TOptional<OBS::FScene>, void>
			{
				if (InResponse.HasError())
				{
					GetOrCreateScenePromise->SetValue(MakeError());
					return MakeError();
				}

				if (const OBS::FScene* FoundScene = InResponse.GetValue().Scenes.FindByPredicate(
					[SceneName](const OBS::FScene& InScene)
					{
						return InScene.Name == SceneName;
					}))
				{
					return MakeValue(TOptional<OBS::FScene>(*FoundScene));
				}

				// Return unset scene
				return MakeValue(TOptional<OBS::FScene>());
			})
			.Next([WeakThis, OBS, GetOrCreateScenePromise, OutputScene](const TValueOrError<TOptional<OBS::FScene>, void>& InScene)
			{
				if (InScene.HasError())
				{
					return;
				}

				// Scene exists already, make current and continue
				if (InScene.GetValue().IsSet())
				{
					const OBS::FScene& FoundScene = InScene.GetValue().GetValue();
					OutputScene->UniqueId = FoundScene.UniqueId;
					GetOrCreateScenePromise->SetValue(MakeValue(FoundScene));
					return;
				}

				// Scene doesn't exist, create it
				OBS->CreateScene(OutputScene->Name)
				.Next([GetOrCreateScenePromise, OutputScene](const TValueOrError<OBS::FScene, void>& InCreatedScene)
				{
					if (InCreatedScene.HasError())
					{
						GetOrCreateScenePromise->SetValue(MakeError());
						return;
					}

					OutputScene->UniqueId = InCreatedScene.GetValue().UniqueId;
					GetOrCreateScenePromise->SetValue(MakeValue(InCreatedScene.GetValue()));
				});
			});

			return GetOrCreateScenePromise->GetFuture();
		};

		auto GetOrCreateSceneItems = [OBS = OBSClient, OutputScene](const OBS::FScene& InScene) -> TFuture<TValueOrError<TArray<OBS::FSceneItem>, void>>
		{
			TSharedPtr<TPromise<TValueOrError<TArray<OBS::FSceneItem>, void>>> GetOrCreateSceneItemsPromise = MakeShared<TPromise<TValueOrError<TArray<OBS::FSceneItem>, void>>>();

			OBS->GetSceneItemList(InScene)
			.Next([OBS, GetOrCreateSceneItemsPromise, OutputScene](const TValueOrError<TArray<OBS::FSceneItem>, void>& InSceneItemList)
			{
				if (InSceneItemList.HasError())
				{
					GetOrCreateSceneItemsPromise->SetValue(MakeError());
					return;
				}

				TSharedPtr<TArray<OBS::FSceneItem>> ExistingOrCreatedSceneItems = MakeShared<TArray<OBS::FSceneItem>>(OutputScene->GetSceneItems());

				for (OBS::FSceneItem& SceneItem : *ExistingOrCreatedSceneItems)
				{
					if (const OBS::FSceneItem* FoundItem = InSceneItemList.GetValue().FindByPredicate(
					[ExpectedItemKind = SceneItem.InputKind, ExpectedItemName = SceneItem.SourceName](const OBS::FSceneItem& InSceneItem)
					{
						return InSceneItem.InputKind == ExpectedItemKind && InSceneItem.SourceName == ExpectedItemName;
					}))
					{
						SceneItem.SceneItemId = FoundItem->SceneItemId;
						SceneItem.SourceUniqueId = FoundItem->SourceUniqueId;
					}
				}

				auto CreateInputs = [OBS, ExistingOrCreatedSceneItems, OutputScene]() -> TFuture<bool>
				{
					// Now, create any scene item that has an invalid id
					TArray<OBS::FCreateInputRequest> CreateInputRequests;
					CreateInputRequests.Reserve(ExistingOrCreatedSceneItems->Num());

					for (OBS::FSceneItem& SceneItem : *ExistingOrCreatedSceneItems)
					{
						if (SceneItem.SourceUniqueId.IsEmpty())
						{
							CreateInputRequests.Emplace(
								OBS::FCreateInputRequest{
									*OutputScene,
									SceneItem.SourceName,
									SceneItem.InputKind,
									SceneItem.Settings,
									true
								});
						}
					}

					TSharedPtr<TPromise<bool>> CreateInputsPromise = MakeShared<TPromise<bool>>();
					if (CreateInputRequests.IsEmpty())
					{
						// The bool is a response success rather than indicating if inputs were created
						CreateInputsPromise->SetValue(true);
					}
					else
					{
						OBS->SendBatch<OBS::FCreateInputRequest, OBS::FCreateInputRequest::FResponse>(CreateInputRequests)
						.Next([CreateInputsPromise, ExistingOrCreatedSceneItems](const TValueOrError<TMap<int32, OBS::FCreateInputRequest::FResponse>, void>& InCreatedItems)
						{
							if (InCreatedItems.HasError())
							{
								CreateInputsPromise->SetValue(false);
								return;
							}

							// Keyed by RequestId
							for (const TPair<int32, OBS::FSceneItem>& CreatedItemPair : InCreatedItems.GetValue())
							{
								if (OBS::FSceneItem* FoundMatchingExpectedItem = ExistingOrCreatedSceneItems->FindByPredicate(
									[CreatedItemName = CreatedItemPair.Value.SourceName](const OBS::FSceneItem& InExpectedItem)
									{
										return InExpectedItem.SourceName == CreatedItemName;
									}))
								{
									FoundMatchingExpectedItem->SceneItemId = CreatedItemPair.Value.SceneItemId;
									FoundMatchingExpectedItem->SourceUniqueId = CreatedItemPair.Value.SourceUniqueId;
								}
							}

							CreateInputsPromise->SetValue(true);
						});
					}

					return CreateInputsPromise->GetFuture();
				};

				CreateInputs()
				.Next([GetOrCreateSceneItemsPromise, ExistingOrCreatedSceneItems](const bool bCreatedInputsIfNeeded)
				{
					if (!bCreatedInputsIfNeeded)
					{
						GetOrCreateSceneItemsPromise->SetValue(MakeError());
						return;
					}

					GetOrCreateSceneItemsPromise->SetValue(MakeValue(*ExistingOrCreatedSceneItems));
				});
			});

			return GetOrCreateSceneItemsPromise->GetFuture();
		};

		GetOrCreateScene()
		.Next([WeakThis = AsWeak(), OBS = OBSClient, SetupScenePromise, OutputScene, GetOrCreateSceneItems](const TValueOrError<OBS::FScene, void>& InScene)
		{
			if (InScene.HasError())
			{
				SetupScenePromise->SetValue(MakeError());
				return;
			}

			TSharedPtr<TPromise<bool>> ModifiedPromise = MakeShared<TPromise<bool>>();
			GetOrCreateSceneItems(InScene.GetValue())
			.Next([OBS, OutputScene, SetupScenePromise, ModifiedPromise](const TValueOrError<TArray<OBS::FSceneItem>, void>& InExistingOrCreatedSceneItems)
			{
				if (InExistingOrCreatedSceneItems.HasError())
				{
					SetupScenePromise->SetValue(MakeError());
					return;
				}

				OutputScene->SetSceneItems(InExistingOrCreatedSceneItems.GetValue());

				TArray<OBS::FSetInputSettingsRequest> SetInputSettingsRequests;
				SetInputSettingsRequests.Reserve(OutputScene->GetMutableSceneItems().Num());

				for (OBS::FSceneItem* SceneItem : OutputScene->GetMutableSceneItems())
				{
					SetInputSettingsRequests.Emplace(OBS::FSetInputSettingsRequest{ SceneItem->SourceName, SceneItem->SourceUniqueId, SceneItem->Settings });
				}

				OBS->SendBatch<OBS::FSetInputSettingsRequest>(SetInputSettingsRequests)
				.Next([OutputScene, SetupScenePromise, ModifiedPromise](const bool bSuccessfullySetSettings)
				{
					if (!bSuccessfullySetSettings)
					{
						ModifiedPromise->SetValue(false);
						SetupScenePromise->SetValue(MakeError());
						return;
					}

					ModifiedPromise->SetValue(true);
				});
			});

			TSharedPtr<TPromise<bool>> SetTransformPromise = MakeShared<TPromise<bool>>();
			ModifiedPromise->GetFuture()
			.Next([OutputScene, OBS, SetupScenePromise, SetTransformPromise](const bool bSuccessfullyModifiedInputs)
			{
				if (!bSuccessfullyModifiedInputs)
				{
					SetTransformPromise->SetValue(false);
					return;
				}

				// Finally, set input transforms
				TArray<OBS::FSetSceneItemTransformRequest> SetSceneItemTransformRequests;
				SetSceneItemTransformRequests.Reserve(OutputScene->GetMutableSceneItems().Num());

				for (OBS::FSceneItem* SceneItem : OutputScene->GetMutableSceneItems())
				{
					SetSceneItemTransformRequests.Emplace(OBS::FSetSceneItemTransformRequest{ *OutputScene, *SceneItem, SceneItem->SceneItemTransform });
				}

				OBS->SendBatch<OBS::FSetSceneItemTransformRequest>(SetSceneItemTransformRequests)
				.Next([OutputScene, SetupScenePromise, SetTransformPromise](const bool bSuccessfullySetSettings)
				{
					if (!bSuccessfullySetSettings)
					{
						SetTransformPromise->SetValue(false);
						SetupScenePromise->SetValue(MakeError());
						return;
					}

					SetTransformPromise->SetValue(true);
				});
			});

			SetTransformPromise->GetFuture()
			.Next([WeakThis, SetupScenePromise, OutputScene](const bool bSuccessfullyTransformedItems)
			{
				if (!bSuccessfullyTransformedItems)
				{
					return;
				}

				SetupScenePromise->SetValue(MakeValue(*OutputScene));
			});
		});

		return SetupScenePromise->GetFuture();
	}

	void FTestRecorder::SetState(const ERecorderState InNewState)
	{
		RecorderState.store(InNewState);
	}

	FTestRecorder::FSettings::FSettings()
	{
		BaseCaptureSizeX.Value = OutputCaptureSizeX.Value = FString::FromInt(FSettings::GetDefaultOutputSizeX());
		BaseCaptureSizeY.Value = OutputCaptureSizeY.Value = FString::FromInt(FSettings::GetDefaultOutputSizeY());
	}

	TArray<OBS::FParameter> FTestRecorder::FSettings::GetParameters() const
	{
		return {
			OutputFilenameFormatting,
			AdvancedOutputFilePath,
			AdvancedRecordingFormat2,
			BaseCaptureSizeX,
			BaseCaptureSizeY,
			OutputCaptureSizeX,
			OutputCaptureSizeY
		};
	}

	void FTestRecorder::FSettings::SetParameters(const TArray<OBS::FParameter>& InSourceParameters)
	{
		TArray<OBS::FParameter*> DestinationParameters = GetMutableParameters();

		Algo::ForEach(InSourceParameters, [this, &DestinationParameters](const OBS::FParameter& InSourceParameter)
		{
			if (OBS::FParameter* const* FoundDestination = DestinationParameters.FindByPredicate(
				[&InSourceParameter](const OBS::FParameter* InDestinationParameter)
				{
					return InDestinationParameter && (*InDestinationParameter == InSourceParameter);
				}))
			{
				if (*FoundDestination)
				{
					(*FoundDestination)->Value = InSourceParameter.Value;
				}
			}
		});
	}

	int32 FTestRecorder::FSettings::GetDefaultOutputSizeX()
	{
		constexpr int32 DefaultWidth = 1920;
		return DefaultWidth;
	}

	int32 FTestRecorder::FSettings::GetDefaultOutputSizeY()
	{
		constexpr int32 DefaultHeight = 1080;
		return DefaultHeight;
	}

	FTestRecorder::FScene::FScene()
	{
		constexpr const TCHAR* SceneName = TEXT("UETestScene");
		Name = SceneName;

		// Capture Item
		{
			constexpr const TCHAR* CaptureItemKind = TEXT("window_capture");
			constexpr const TCHAR* CaptureItemName = TEXT("UETestWindowCaptureInput");

			CaptureItem.InputKind = CaptureItemKind;
			CaptureItem.SourceName = CaptureItemName;
			CaptureItem.SceneItemIndex = 0; // Bottom most item

			const TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
			CaptureItem.Settings = Settings;

			Settings->SetBoolField(TEXT("capture_audio"), false);
			Settings->SetBoolField(TEXT("compatibility"), false);
			Settings->SetNumberField(TEXT("method"), 2);
			Settings->SetNumberField(TEXT("priority"), 2);

			OBS::FSceneItem::FSceneItemTransform Transform;
			Transform.Alignment = OBS::FSceneItem::FSceneItemTransform::EAlignment::TopLeft;
			Transform.BoundsType = OBS::FSceneItem::FSceneItemTransform::EBoundsType::ScaleToOuterBounds;
			Transform.BoundsAlignment = OBS::FSceneItem::FSceneItemTransform::EAlignment::TopLeft;
			Transform.BoundsWidth = FSettings::GetDefaultOutputSizeX();
			Transform.BoundsHeight = FSettings::GetDefaultOutputSizeY();

			CaptureItem.SceneItemTransform = Transform;
		}

		// Text Item
		{
			constexpr const TCHAR* TextItemKind = TEXT("text_gdiplus_v3");
			constexpr const TCHAR* TextItemName = TEXT("UETestOverlayTextItem");

			TextItem.InputKind = TextItemKind;
			TextItem.SourceName = TextItemName;
			TextItem.SourceType = TEXT("OBS_SOURCE_TYPE_INPUT");
			TextItem.SceneItemIndex = 1; // Top most item

			const TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
			TextItem.Settings = Settings;

			Settings->SetBoolField(TEXT("outline"), true);
			Settings->SetNumberField(TEXT("outline_color"), 0xFF000000);
			Settings->SetNumberField(TEXT("outline_size"), 16);
			Settings->SetStringField(TEXT("text"), TEXT("")); // Start empty
			Settings->SetStringField(TEXT("valign"), TEXT("center"));

			TSharedPtr<FJsonObject> FontSettings = MakeShared<FJsonObject>();
			Settings->SetObjectField(TEXT("font"), FontSettings);

			FontSettings->SetStringField(TEXT("face"), TEXT("Calibri"));
			FontSettings->SetNumberField(TEXT("size"), 72);

			OBS::FSceneItem::FSceneItemTransform Transform;
			Transform.Alignment = OBS::FSceneItem::FSceneItemTransform::EAlignment::BottomLeft;
			Transform.BoundsAlignment = OBS::FSceneItem::FSceneItemTransform::EAlignment::Center;

			constexpr float Padding = 8.0f;
			Transform.Position = { Padding, static_cast<double>(FSettings::GetDefaultOutputSizeY()) - Padding };
			Transform.BoundsType = OBS::FSceneItem::FSceneItemTransform::EBoundsType::None;
			Transform.BoundsAlignment = OBS::FSceneItem::FSceneItemTransform::EAlignment::Center;

			TextItem.SceneItemTransform = Transform;
		}
	}

	TArray<OBS::FSceneItem> FTestRecorder::FScene::GetSceneItems() const
	{
		return {
			CaptureItem,
			TextItem
		};
	}

	void FTestRecorder::FScene::SetSceneItems(const TArray<OBS::FSceneItem>& InSourceItems)
	{
		TArray<OBS::FSceneItem*> DestinationItems = GetMutableSceneItems();
		Algo::ForEach(InSourceItems, [this, &DestinationItems](const OBS::FSceneItem& InSourceItem)
		{
			if (OBS::FSceneItem** FoundDestination = DestinationItems.FindByPredicate(
			[&InSourceItem](const OBS::FSceneItem* InDestinationItem)
			{
				return InDestinationItem && (*InDestinationItem == InSourceItem);
			}))
			{
				if (*FoundDestination)
				{
					*(*FoundDestination) = InSourceItem;
				}
			}
		});
	}
}

#endif
