// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

#include "AutomationDriverCommon.h"
#include "CQTest.h"
#include "Editor.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "IAutomationDriver.h"
#include "IDriverElement.h"
#include "IDriverSequence.h"
#include "LevelEditor.h"
#include "OBSClient.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "Widgets/SWindow.h"
#include "UnrealWidgetFwd.h"

#define UE_API EDITORTRSGIZMOTESTS_API

namespace OBS
{
	class FOBSClient;
}

class IWebSocket;

namespace UE::Editor::InteractiveToolsFramework::Tests
{
	class FEditorProvider;

	inline FVector GetAxisVector(const EAxis::Type InAxis)
	{
		switch (InAxis)
		{
		case EAxis::X:
			return FVector::XAxisVector;

		case EAxis::Y:
			return FVector::YAxisVector;

		case EAxis::Z:
			return FVector::ZAxisVector;

		case EAxis::None:
		default:
			return FVector::OneVector;
		}
	}

	enum class EViewportType : uint8
	{
		Perspective,
		Orthographic,

		Max
	};

	class FTestWorld
	{
	public:
		void Initialize(const TSharedRef<FEditorProvider>& InEditorProvider);
		void InitializeWithMap(const FString& InMapName, const TSharedRef<FEditorProvider>& InEditorProvider);

		/** Empties the world of all actors without deleting the world itself. */
		void EmptyWorld();

		void Reset();

		/** Captures all stored actor transforms and view location for later restoration. */
		void CaptureState();

		/** Restores all stored actor transforms to previously captured state. */
		void RestoreState();

		void SetViewportCamera(const FVector& InLocation, const FRotator& InRotation);

		template <typename ActorType = AActor>
		ActorType* SpawnActor(const FName InName, const FTransform& InTransform);

		AStaticMeshActor* SpawnCube(const FName InName, const FTransform& InTransform);

		template <typename ActorType = AActor>
		ActorType* GetActor(const FName InName = NAME_Default) const;

		template <typename ActorType = AActor>
		ActorType* FindActorByName(const FName InName) const;

		template <typename ActorType = AActor>
		ActorType* FindActorByLabel(const FStringView InLabel) const;

		UWorld* GetWorld() const;

		void SelectActor(const AActor* InActor) const;

		void DeselectActor(const AActor* InActor) const;

		void ClearSelection() const;

		bool IsValid() const;

	private:
		TSharedPtr<FEditorProvider> EditorProvider = nullptr;

		TWeakObjectPtr<UWorld> World;
		TMap<FName, TWeakObjectPtr<AActor>> Actors;

		TMap<FName, FTransform> CapturedActorStates;
		FVector CapturedViewLocation = FVector::ZeroVector;
		FRotator CapturedViewRotation = FRotator::ZeroRotator;
		int32 CapturedTransactionId = INDEX_NONE;

		UTypedElementSelectionSet* ActorSelectionSet = nullptr;
	};

	/** Locator for mouse actions. */
	struct FLocator
	{
	private:
		FLocator();

	public:
		explicit FLocator(const TSharedRef<IElementLocator>& InElementLocator);

		bool bUseElementLocator = false;
		TSharedPtr<IElementLocator> ElementLocator = nullptr;

		int32 Steps = 0; // The number of simulated sub-steps used to emulate the movement
		TOptional<FIntPoint> Offset;	// Relative
		TOptional<FIntPoint> Location;	// Absolute

		using FOffsetFunction = TFunction<FVector2D(const float)>; 
		// An offset function. It is fed the current progress through the motion and the step size of the function
		FOffsetFunction OffsetFunction = nullptr;

		/** Get the estimated time based on both the specified Time, and Steps at 30fps. */
		float GetEstimatedTime() const;

		static FLocator Empty();

		static FLocator FromWorldPositionInViewport(const TSharedRef<IElementLocator>& InViewportLocator, const FSceneView* InSceneView, const FVector& InWorldPosition);

		static FLocator FromOffsetFunction(const FOffsetFunction& InOffsetFunction, const int32 InSteps);

		static FLocator FromOffset(const TSharedRef<IElementLocator>& InElementLocator, const FIntPoint& InOffset, int32 InSteps = 0);
		
		static FLocator FromOffset(const FIntPoint& InOffset, int32 Steps = 0);
		
		void AppendToActions(IAsyncActionSequence& InActions) const;
	};

	class FTestRecorder : public TSharedFromThis<FTestRecorder>
	{
	public:
		enum class ERecorderState : uint8
		{
			None = 0,						// Initial state
			Unavailable = 1,				// Tried connecting, couldn't
			Connecting = 2,					// Trying to connect
			Connected = 3,					// Connected and ready
			Disconnected = 4,				// Was connected, now isn't
			StartingRecording = 5,			// Starting or resuming recording
			Recording = 6,					// Currently recording
			Paused = 7,						// Recording paused
			StoppingRecording = 8,			// Stopping recording
		};

	public:
		/** AddOverlay = true will include a text overlay matching the current chapter name. */
		explicit FTestRecorder(const FString& InFileName = {}, const bool bAddOverlay = true);

		TFuture<bool> Begin();
		TFuture<bool> BeginOrResume();
		TFuture<bool> Pause();

		/** Creates a chapter with the given name at the current recording time. */
		void CreateNamedChapter(const FString& InChapterName) const;

		TFuture<bool> End();

		/** Returns true if the State indicates we're either recording or starting to record. */
		bool IsRecording() const;

		ERecorderState GetState() const;

	private:
		struct FSettings;

		TFuture<TValueOrError<FSettings, void>> GetSettings(const FSettings& InSettings) const;
		TFuture<bool> SetSettings(const FSettings& InSettings) const;

		struct FScene;

		TFuture<TValueOrError<FScene, void>> SetupScene();

		void SetState(const ERecorderState InNewState);

	private:
		TSharedPtr<OBS::FOBSClient> OBSClient = nullptr;

		std::atomic<ERecorderState> RecorderState = ERecorderState::None;

		FString FileName;
		bool bHasOverlay = true;

		/** Contains the Parameters and other settings that need to be captured, overridden then restored to user values. */
		struct FSettings
		{
			FSettings();

			OBS::FParameter OutputFilenameFormatting = OBS::FParameter{TEXT("Output"), TEXT("FilenameFormatting")};
			OBS::FParameter AdvancedOutputFilePath = OBS::FParameter{TEXT("AdvOut"), TEXT("RecFilePath")};
			OBS::FParameter AdvancedRecordingFormat2 = OBS::FParameter{TEXT("AdvOut"), TEXT("RecFormat2")};
			OBS::FParameter BaseCaptureSizeX = OBS::FParameter{TEXT("Video"), TEXT("BaseCX"), };
			OBS::FParameter BaseCaptureSizeY = OBS::FParameter{TEXT("Video"), TEXT("BaseCY")};
			OBS::FParameter OutputCaptureSizeX = OBS::FParameter{TEXT("Video"), TEXT("OutputCX")};
			OBS::FParameter OutputCaptureSizeY = OBS::FParameter{TEXT("Video"), TEXT("OutputCY")};

			OBS::FScene CurrentScene;
			bool bIsDesktopAudioEnabled = false;
			bool bIsMicAudioEnabled = false;

			TArray<OBS::FParameter> GetParameters() const;

			void SetParameters(const TArray<OBS::FParameter>& InSourceParameters);

			static int32 GetDefaultOutputSizeX();
			static int32 GetDefaultOutputSizeY();

		private:
			TArray<OBS::FParameter*> GetMutableParameters()
			{
				return {
					&OutputFilenameFormatting,
					&AdvancedOutputFilePath,
					&AdvancedRecordingFormat2,
					&BaseCaptureSizeX,
					&BaseCaptureSizeY,
					&OutputCaptureSizeX,
					&OutputCaptureSizeY
				};
			}
		};

		/** The expected scene structure in OBS, to be created and/or modified. */
		struct FScene : OBS::FScene
		{
			FScene();
			FScene(const FScene&) = default;

			OBS::FSceneItem CaptureItem;
			OBS::FSceneItem TextItem;

			TArray<OBS::FSceneItem> GetSceneItems() const;

			/** Overwrites the SceneItems with the provided SceneItems, matched by various criteria. */
			void SetSceneItems(const TArray<OBS::FSceneItem>& InSourceItems);

			TArray<OBS::FSceneItem*> GetMutableSceneItems()
			{
				return {
					&CaptureItem,
					&TextItem
				};
			}
		};

		TSharedPtr<FScene> RecorderScene = nullptr;

		/** User settings to capture and restore. */
		FSettings UserSettings;

		/** Settings to apply while recording. */
		FSettings RecorderSettings;
	};

	/** Wraps an editor for testing, with viewport access etc. */
	class FEditorProvider : public TSharedFromThis<FEditorProvider>
	{
	public:
		virtual ~FEditorProvider() = default;

		virtual FEditorViewportClient* GetEditorViewportClient() const = 0;
		virtual UTypedElementSelectionSet* GetActorSelectionSet() const = 0;

		virtual bool WorldToPixel(const FVector& InWorldPosition, FVector2D& OutPixelPosition) const;

	public:
		TSharedPtr<SWindow> TopLevelWindow;

		FString ViewportWidgetPath;
		TSharedPtr<IElementLocator> ViewportLocator;
		FVector2D ViewportSize = FVector2D::One();

		UInteractiveToolsContext* ToolsContext = nullptr;
	};

	class FLevelEditorProvider : public FEditorProvider
	{
	public:
		explicit FLevelEditorProvider(const TSharedRef<ILevelEditor>& InLevelEditor);

		virtual FEditorViewportClient* GetEditorViewportClient() const override;
		virtual UTypedElementSelectionSet* GetActorSelectionSet() const override;

	private:
		TSharedPtr<ILevelEditor> LevelEditor;
	};

	template <typename Derived, typename AsserterType>
	struct TInteractionTest : public TTest<Derived, AsserterType>
	{
	protected:
		using FInteractionTest = TInteractionTest<Derived, AsserterType>;

		const FTimespan DriverWaitTimeout = FTimespan::FromSeconds(2);

		TSharedPtr<IAsyncAutomationDriver> Driver;

		TSharedPtr<IAsyncDriverElement> ViewportElement;

		/** Shared state between tests to enable re-use where appropriate. */
		struct FSharedEnvironment
		{
			TSharedPtr<FEditorProvider> EditorProvider;

			/** Store, minimize and restore these windows on test teardown. */
			TArray<TSharedRef<SWindow>> SecondaryWindows;
			FVector2D WindowSize;
			FVector2D ViewportSize;

			FSharedEnvironment()
				: EditorProvider(nullptr)
				, WindowSize()
				, ViewportSize(1920.0f, 1080.0f)
			{
			}
		};

		static TUniquePtr<FSharedEnvironment> SharedEnvironment;

		/** Test world shared across tests. */
		TUniquePtr<FTestWorld> TestWorld;

	public:
		TInteractionTest() = default;

		static void AfterAll(const FString&);

		virtual void Setup() override;

		virtual void TearDown() override;

	protected:
		/** Setup the test world - TestWorld are always valid. */
		virtual void PopulateTestWorld();

		void PopulateDefaultTestWorld();

		/** Set via the Editor VPC, rather than user interaction. */
		void SetViewportType(const EViewportType InViewportType);

#pragma region Interaction
		void DoClickDrag(FTestCommandBuilder& InCommandBuilder, const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const FLocator& InStart, const FLocator& InEnd);
#pragma endregion Interaction
		
		static TSharedRef<UE::Editor::InteractiveToolsFramework::Tests::FTestRecorder> GetTestRecorder();
	};

	template <typename Derived, typename AsserterType>
	TUniquePtr<typename TInteractionTest<Derived, AsserterType>::FSharedEnvironment> TInteractionTest<Derived, AsserterType>::SharedEnvironment = nullptr;
}

const TCHAR* LexToString(UE::Editor::InteractiveToolsFramework::Tests::EViewportType InViewportType);

const TCHAR* LexToString(ECoordSystem InCoordinateSystem);

const TCHAR* LexToString(EAxis::Type InAxis);

const TCHAR* LexToString(EAxisList::Type InAxisList);

ENUM_RANGE_BY_COUNT(
	UE::Editor::InteractiveToolsFramework::Tests::EViewportType,
	UE::Editor::InteractiveToolsFramework::Tests::EViewportType::Max);

ENUM_RANGE_BY_FIRST_AND_LAST(
	ECoordSystem,
	ECoordSystem::COORD_World,
	ECoordSystem::COORD_Explicit);

#undef UE_API

#endif
