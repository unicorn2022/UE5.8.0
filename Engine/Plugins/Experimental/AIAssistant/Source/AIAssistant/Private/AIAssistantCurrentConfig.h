// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <atomic>

#include "CoreGlobals.h"
#include "Delegates/Delegate.h"
#include "Misc/CoreDelegates.h"

#include "AIAssistantConfig.h"
#include "ToolsetRegistry/DelegateHandle.h"

namespace UE::AIAssistant
{
	// Central location for the plugin's static configuration.
	//
	// At the moment, FWebApplication currently independently manages subscriptions to
	// FUefnModeSubscription and FInternationalization::Get().OnCultureChanged() to monitor
	// for restricted / unrestricted and language changes but they should probably be moved
	// into this class to make it easier to test that functionality without modifying the
	// global state of the editor.
	class FCurrentConfig
	{
	public:
		// Arguments for the constructor so that the caller can override arguments without
		// repeating default values.
		struct FConstructorArgs
		{
			// Explicitly define a constructor to workaround Clang DR 1351/1397.
			FConstructorArgs() {} // = default;
			FString EditorIniFilename = GEditorIni;
			// IMPORTANT: The provided delegate lifetime must be longer than this object and 
			// FCurrentConfig constructed with this object.
			FSimpleMulticastDelegate* OnPreExit = &FCoreDelegates::OnPreExit;
		};

	public:
		FCurrentConfig(const FConstructorArgs& Args = FConstructorArgs());
		~FCurrentConfig() = default;

		// Load or reload the static configuration.
		TSharedPtr<const FConfig> Load();

		// Get the currently loaded configuration.
		TSharedPtr<const FConfig> GetOrLoad();

		// Whether the assistant plugin should be enabled.
		// This is set on construction and never modified for the lifetime of this instance.
		bool IsEnabled() const { return bIsEnabled;  }

		// Multicast delegate that can be used to watch for application pre-exit events.
		FSimpleMulticastDelegate& OnPreExit() const { return OnPreExitDelegate; }

		// Returns true if the application is exiting.
		bool IsExiting() const { return bIsExiting.load();  }

	private:
		// Handles OnPreExit event.
		void OnPreExitHandler();

	private:
		TSharedPtr<const FConfig> Config;
		std::atomic<bool> bIsExiting;
		FSimpleMulticastDelegate& OnPreExitDelegate;
		UE::ToolsetRegistry::FDelegateHandleRaii OnPreExitDelegateHandle;
		bool bIsEnabled = true;
	};

}