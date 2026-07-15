// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tests/InteractiveToolsFrameworkTestUtilities.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

#include "Components/ExponentialHeightFogComponent.h"
#include "EditorViewportClient.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "UnrealClient.h"

namespace UE::Editor::InteractiveToolsFramework::Tests
{
	inline TAutoConsoleVariable<bool> CVarResizeWindow(
		TEXT("Editor.ITF.Tests.ResizeWindow"),
		false,
		TEXT("Set to true to resize the main window to a consistent size for automated recording.")
	);

	inline TAutoConsoleVariable<int32> CVarRestoreMap(
		TEXT("Editor.ITF.Tests.RestoreMap"),
		0,
		TEXT("Controls how the map is restored after tests are run.\n")
		TEXT("	0 - Do not restore the map")
		TEXT("	1 - Restore to the default startup map")
	);

	inline TAutoConsoleVariable<bool> CVarRecordScreen(
		TEXT("Editor.ITF.Tests.RecordScreen"),
		true,
		TEXT("Whether to perform a screen recording or not (when available).")
	);

	template <typename ActorType>
	ActorType* FTestWorld::SpawnActor(const FName InName, const FTransform& InTransform)
	{
		if (World.IsValid())
		{
			if (ensureAlwaysMsgf(Actors.Find(InName) == nullptr, TEXT("Actor with name %s already exists in test world"), *InName.ToString()))
			{
				ActorType* SpawnedActor = World->SpawnActor<ActorType>(ActorType::StaticClass(), InTransform);
				Actors.Emplace(InName, SpawnedActor);

				return SpawnedActor;
			}
		}

		return nullptr;
	}

	template <typename ActorType>
	ActorType* FTestWorld::GetActor(const FName InName) const
	{
		if (const TWeakObjectPtr<AActor>* FoundActor = Actors.Find(InName);
			FoundActor && FoundActor->IsValid())
		{
			return Cast<ActorType>(FoundActor->Get());
		}

		// Otherwise, if the name is none/default, return the first valid actor
		if (InName == NAME_None || InName == NAME_Default)
		{
			if (!Actors.IsEmpty())
			{
				for (const TPair<FName, TWeakObjectPtr<AActor>>& NamedActor : Actors)
				{
					if (NamedActor.Value.IsValid())
					{
						return Cast<ActorType>(NamedActor.Value.Get());
					}
				}
			}
		}

		return nullptr;
	}

	template <typename ActorType>
	ActorType* FTestWorld::FindActorByName(const FName InName) const
	{
		if (World.IsValid())
		{
			for (TActorIterator<ActorType> ActorIterator(World.Get()); ActorIterator; ++ActorIterator)
			{
				if ((*ActorIterator)->GetName() == InName.ToString())
				{
					return *ActorIterator;
				}
			}
		}

		return nullptr;
	}

	template <typename ActorType>
	ActorType* FTestWorld::FindActorByLabel(const FStringView InLabel) const
	{
		if (World.IsValid())
		{
			for (TActorIterator<ActorType> ActorIterator(World.Get()); ActorIterator; ++ActorIterator)
			{
				if ((*ActorIterator)->GetActorLabel() == InLabel)
				{
					return *ActorIterator;
				}
			}
		}

		return nullptr;
	}

	template <typename Derived, typename AsserterType>
	void TInteractionTest<Derived, AsserterType>::AfterAll(const FString&)
	{
		if (CVarRecordScreen.GetValueOnAnyThread())
		{
			GetTestRecorder()->End().WaitFor(FTimespan::FromSeconds(10.0f));
		}

		if (ensure(SharedEnvironment.IsValid()))
		{
			for (const TSharedRef<SWindow>& Window : SharedEnvironment->SecondaryWindows)
			{
				if (Window->IsWindowMinimized())
				{
					Window->Restore();
				}
			}

			if (CVarResizeWindow.GetValueOnAnyThread() && SharedEnvironment->EditorProvider.IsValid() && SharedEnvironment->EditorProvider->TopLevelWindow.IsValid())
			{
				SharedEnvironment->EditorProvider->TopLevelWindow->Resize(SharedEnvironment->WindowSize);
			}

			// Restore MessageLog displayability
			FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
			MessageLogModule.EnableMessageLogDisplay(true);

			SharedEnvironment.Reset();
		}

		IAutomationDriverModule::Get().Disable();
		if (CVarRestoreMap.GetValueOnAnyThread() == 1)
		{
			FEditorFileUtils::LoadDefaultMapAtStartup();
		}
	}

	template <typename Derived, typename AsserterType>
	void TInteractionTest<Derived, AsserterType>::Setup()
	{
		// Setup Driver
		if (!Driver.IsValid())
		{
			if (IAutomationDriverModule::Get().IsEnabled())
			{
				IAutomationDriverModule::Get().Disable();
			}

			IAutomationDriverModule::Get().Enable();

			Driver = IAutomationDriverModule::Get().CreateAsyncDriver().ToSharedPtr();
		}

		if (!SharedEnvironment.IsValid())
		{
			SharedEnvironment = MakeUnique<FSharedEnvironment>();

			// Setup Editor Provider
			// @todo: refactor to allow tests to swap this out
			{
				const FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
				const TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
				ASSERT_THAT(IsTrue(LevelEditor.IsValid(), TEXT("Level Editor doesn't exist")));

				if (LevelEditor.IsValid())
				{
					SharedEnvironment->EditorProvider = MakeShared<FLevelEditorProvider>(LevelEditor.ToSharedRef());
				}
				else
				{
					return;
				}
			}

			// Setup initial window state
			{
				TSharedPtr<SWindow> TopLevelWindow = SharedEnvironment->EditorProvider->TopLevelWindow;
				ASSERT_THAT(IsNotNull(TopLevelWindow.Get(), TEXT("Cannot find Level Editor parent window")));

				SharedEnvironment->WindowSize = TopLevelWindow->GetSizeInScreen();
				if (CVarResizeWindow.GetValueOnAnyThread())
				{
					TopLevelWindow->Resize(SharedEnvironment->ViewportSize);
				}
				TopLevelWindow->BringToFront(true);

				TArray<TSharedRef<SWindow>> VisibleWindows;
				FSlateApplication::Get().GetAllVisibleWindowsOrdered(VisibleWindows);
				for (TSharedRef<SWindow> Window : VisibleWindows)
				{
					if (Window != TopLevelWindow && !Window->IsWindowMinimized())
					{
						Window->Minimize();
						SharedEnvironment->SecondaryWindows.Emplace(Window);
					}
				}

				// Prevent MessageLog popup/focus stealing
				FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));;
				MessageLogModule.EnableMessageLogDisplay(false);
			}
		}

		const uint64 StartTime = FPlatformTime::Cycles64();
		TSharedPtr<std::atomic<uint64>> LastTime = MakeShared<std::atomic<uint64>>(StartTime);

		if (!ViewportElement.IsValid())
		{
			if (!ensure(SharedEnvironment.IsValid()
				&& SharedEnvironment->EditorProvider.IsValid()
				&& SharedEnvironment->EditorProvider->ViewportLocator.IsValid()))
			{
				return;
			}

			// Init Viewport
			{
				this->TestCommandBuilder
				.template DoAsync<bool>(
					TEXT("Find Level Viewport"),
					[this, LastTime]()
					{
						const uint64 ThisTime = FPlatformTime::Cycles64();
						this->AddInfo(FString::Printf(TEXT("Pre Find Level Viewport took %f seconds"), FPlatformTime::ToSeconds(ThisTime - LastTime->load())));
						LastTime->store(ThisTime);

						ViewportElement = Driver->FindElement(SharedEnvironment->EditorProvider->ViewportLocator.ToSharedRef()).ToSharedPtr();
						if (ViewportElement.IsValid())
						{
							return ViewportElement->Exists();
						}

						return TAsyncResult<bool>(false);
					},
					[this, StartTime, LastTime](bool bExists)
					{
						const uint64 ThisTime = FPlatformTime::Cycles64();
						this->AddInfo(FString::Printf(TEXT("Find Level Viewport took %f seconds"), FPlatformTime::ToSeconds(ThisTime - LastTime->load())));
						LastTime->store(ThisTime);

						ASSERT_THAT(IsTrue(bExists, TEXT("Level Viewport doesn't exist")));
					})
				.template ThenAsync<bool>(
					TEXT("Verify Level Viewport is focused"),
					[this]()
					{
						return ViewportElement->Focus();
					},
					[this, StartTime, LastTime](bool bVisible)
					{
						const uint64 ThisTime = FPlatformTime::Cycles64();
						this->AddInfo(FString::Printf(TEXT("Focus Level Viewport took %f seconds"), FPlatformTime::ToSeconds(ThisTime - LastTime->load())));
						LastTime->store(ThisTime);

						ASSERT_THAT(IsTrue(bVisible, TEXT("Level Viewport isn't visible")));
					})
				.template ThenAsync<bool>(
					TEXT("Verify Level Viewport is clicked"),
					[this]()
					{
						return ViewportElement->Click();
					},
					[this, StartTime, LastTime](bool bWasClicked)
					{
						const uint64 ThisTime = FPlatformTime::Cycles64();
						this->AddInfo(FString::Printf(TEXT("Click Level Viewport took %f seconds"), FPlatformTime::ToSeconds(ThisTime - LastTime->load())));
						LastTime->store(ThisTime);

						ASSERT_THAT(IsTrue(bWasClicked, TEXT("Level Viewport wasn't clicked")));
					})
				.template ThenAsync<FVector2D>(
					TEXT("Get Level Viewport size"),
					[this]()
					{
						return ViewportElement->GetSize();
					},
					[this, StartTime, LastTime](const FVector2D InViewportSize)
					{
						const uint64 ThisTime = FPlatformTime::Cycles64();
					 this->AddInfo(FString::Printf(TEXT("Get Level Viewport size took %f seconds"), FPlatformTime::ToSeconds(ThisTime - LastTime->load())));
						LastTime->store(ThisTime);

						SharedEnvironment->EditorProvider->ViewportSize = InViewportSize;
					});
			}
		}
		else
		{
			this->TestCommandBuilder
				.template ThenAsync<bool>(
					TEXT("Verify Level Viewport is focused"),
					[this]()
					{
						return ViewportElement->Focus();
					},
					[this, StartTime](bool bVisible)
					{
						this->AddInfo(FString::Printf(TEXT("[ ] Focus Level Viewport took %f seconds"), FPlatformTime::ToSeconds(FPlatformTime::Cycles64() - StartTime)));
						ASSERT_THAT(IsTrue(bVisible, TEXT("Level Viewport isn't visible")));
					},
					DriverWaitTimeout)
				.template DoAsync<bool>(
					TEXT("Verify Level Viewport is clicked"),
					[this]()
					{
						return ViewportElement->Click();
					},
					[this, StartTime](bool bWasClicked)
					{
						this->AddInfo(FString::Printf(TEXT("[ ] Click Level Viewport took %f seconds"), FPlatformTime::ToSeconds(FPlatformTime::Cycles64() - StartTime)));
						ASSERT_THAT(IsTrue(bWasClicked, TEXT("Level Viewport wasn't clicked")));
					},
					DriverWaitTimeout);
			}

		if (!TestWorld.IsValid())
		{
			// Setup Test Map
			{
				TestWorld = MakeUnique<FTestWorld>();
				TestWorld->Initialize(SharedEnvironment->EditorProvider.ToSharedRef());

				this->TestCommandBuilder
					.template DoAsync<bool>(
						TEXT("Populate Test World"),
						[this]()
						{
							PopulateTestWorld();
							TestWorld->CaptureState();
							return true;
						},
						DriverWaitTimeout);
			}
		}

		if (CVarRecordScreen.GetValueOnAnyThread())
		{
			this->TestCommandBuilder
			.template ThenAsync<bool>(
			TEXT("Begin Screen Recording"),
			[this]()
			{
				return TAsyncResult<bool>(
					GetTestRecorder()->BeginOrResume()
					.Next([](const bool bBeganOrResumed)
					{
						if (!bBeganOrResumed)
						{
							// This is expected if OBS isn't available
							return false;
						}

						GetTestRecorder()->CreateNamedChapter(TInteractionTest<Derived, AsserterType>::TestRunner->GetTestContext());
						return true;
					}),
					nullptr,
					nullptr);
			},
			[](const bool)
			{
				// ResultCallback required for async, but we don't care about the result here
			},
			DriverWaitTimeout);
		}
	}

	template <typename Derived, typename AsserterType>
	void TInteractionTest<Derived, AsserterType>::TearDown()
	{
		if (CVarRecordScreen.GetValueOnAnyThread())
		{
			GetTestRecorder()->Pause();	
		}

		if (TestWorld.IsValid())
		{
			TestWorld->RestoreState();
		}
	}

	template <typename Derived, typename AsserterType>
	void TInteractionTest<Derived, AsserterType>::PopulateTestWorld()
	{
		PopulateDefaultTestWorld();
	}

	template <typename Derived, typename AsserterType>
	void TInteractionTest<Derived, AsserterType>::PopulateDefaultTestWorld()
	{
		if (ensureAlways(TestWorld.IsValid()))
		{
			// Camera
			{
				const FVector TestCameraLocation = FVector(246.0f, 363.0f, 109.0f);
				const FRotator TestCameraRotation = FRotator(-15.0f, -135.0f, 0.0f);

				TestWorld->SetViewportCamera(TestCameraLocation, TestCameraRotation);
			}

			// Fog (for non-black, non-selectable BG)
			{
				constexpr float FarDistance = 10000.0f;
				const FVector TestFogLocation = FVector(FarDistance, FarDistance, FarDistance);

				// Otherwise, needs light
				const FLinearColor FogInscatteringColor = FLinearColor::White;

				// Don't want to fog subjects - keep fog in background
				constexpr float StartDistance = FarDistance;

				const FName FogActorName("Fog");
				AExponentialHeightFog* TestFogActor = TestWorld->SpawnActor<AExponentialHeightFog>(FogActorName, FTransform(TestFogLocation));
				ASSERT_THAT(IsNotNull(TestFogActor, TEXT("Failed to spawn exponential height fog actor")));

				if (ensureAlways(TestFogActor->GetComponent() != nullptr))
				{
					TestFogActor->GetComponent()->SetFogInscatteringColor(FogInscatteringColor);
					TestFogActor->GetComponent()->SetStartDistance(StartDistance);
				}

				TestWorld->DeselectActor(TestFogActor);
			}

			// Actor
			{
				const FVector TestActorLocation = FVector(0.0f, 0.0f, 0.0f);
				const FRotator TestActorRotation = FRotator(30.0f, 0.0f, 0.0f);

				AStaticMeshActor* TestMeshActor = TestWorld->SpawnCube(NAME_Default, FTransform(TestActorRotation, TestActorLocation));
				ASSERT_THAT(IsNotNull(TestMeshActor, TEXT("Failed to spawn test mesh actor")));

				// Select
				TestWorld->SelectActor(TestMeshActor);
			}
		}
	}

	template <typename Derived, typename AsserterType>
	void TInteractionTest<Derived, AsserterType>::SetViewportType(const EViewportType InViewportType)
	{
		if (FEditorViewportClient* ViewportClient = SharedEnvironment->EditorProvider->GetEditorViewportClient())
		{
			const ELevelViewportType LevelViewportType = InViewportType == EViewportType::Perspective ? ELevelViewportType::LVT_Perspective : ELevelViewportType::LVT_OrthoXY;
			ViewportClient->SetViewportType(LevelViewportType);
		}
	}

#pragma region Interaction
	inline TAutoConsoleVariable<float> CVarActionDelay(
		TEXT("Editor.ITF.Tests.ActionDelay"),
		0.05f,
		TEXT("Controls the delay between actions when a simulated click & drag is performed.")
	);

	template <typename Derived, typename AsserterType>
	void TInteractionTest<Derived, AsserterType>::DoClickDrag(
		FTestCommandBuilder& InCommandBuilder, const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const FLocator& InStart, const FLocator& InEnd)
	{
		const float DragTime = InEnd.GetEstimatedTime();
		const float ExtraWaitTime = CVarActionDelay.GetValueOnAnyThread() * 3.0f; // The sum of any explicit Wait's within the sequence
		const FTimespan Timeout = FTimespan::FromSeconds(FMath::Max(DriverWaitTimeout.GetTotalSeconds(), DragTime + ExtraWaitTime + 1.0f));

		InCommandBuilder
		.DoAsync<bool>(
			TEXT("Begin drag sequence"),
			[this, InStart, InEnd, InModifierKeys, InMouseButtons]()
			{
				const TSharedRef<IAsyncDriverSequence> LocalSequence = Driver->CreateSequence();
				IAsyncActionSequence& Actions = LocalSequence->Actions();

				InStart.AppendToActions(Actions);

				Actions.Wait(FTimespan::FromSeconds(CVarActionDelay.GetValueOnAnyThread()));

				return LocalSequence->Perform();
			},
			Timeout)
		.template ThenAsync<bool>(
			TEXT("Perform drag sequence"),
			[this, InStart, InEnd, InModifierKeys, InMouseButtons]()
			{
				const TSharedRef<IAsyncDriverSequence> LocalSequence = Driver->CreateSequence();
				IAsyncActionSequence& Actions = LocalSequence->Actions();

				// @note: PressChord/ReleaseChord don't work with mouse buttons, so do a manual sequence of presses/releases

				// Initialize the drag
				{
					for (const FKey& ModifierKey : InModifierKeys)
					{
						Actions.Press(ModifierKey);
					}

					for (const EMouseButtons::Type& MouseButton : InMouseButtons)
					{
						Actions.Press(MouseButton);
					}
				}

				// Move to the end location
				InEnd.AppendToActions(Actions);

				Actions.Wait(FTimespan::FromSeconds(CVarActionDelay.GetValueOnAnyThread()));

				// Release the drag
				{
					for (const EMouseButtons::Type& MouseButton : InMouseButtons)
					{
						Actions.Release(MouseButton);
					}

					for (const FKey& ModifierKey : InModifierKeys)
					{
						Actions.Release(ModifierKey);
					}
				}

				Actions.Release(EKeys::AnyKey);
				Actions.Wait(FTimespan::FromSeconds(CVarActionDelay.GetValueOnAnyThread()));

				return LocalSequence->Perform();
			},
			Timeout);
	}

	template <typename Derived, typename AsserterType>
	TSharedRef<FTestRecorder> TInteractionTest<Derived, AsserterType>::GetTestRecorder()
	{
		static TSharedPtr<FTestRecorder> TestRecorder;
		static FString CachedTestName;

		const FString CurrentTestName = TInteractionTest<Derived, AsserterType>::TestRunner->GetTestName();
		if (!TestRecorder.IsValid() || CachedTestName != CurrentTestName)
		{
			CachedTestName = CurrentTestName;
			TestRecorder = MakeShared<FTestRecorder>(CachedTestName);
		}

		return TestRecorder.ToSharedRef();
	}
#pragma endregion Interaction
}

#undef UE_API

#endif
