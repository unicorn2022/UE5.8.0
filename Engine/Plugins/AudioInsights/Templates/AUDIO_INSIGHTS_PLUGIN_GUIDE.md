# Audio Insights Plugin — Standalone Authoring Guide

> **Purpose:** This guide lets you create a new Audio Insights dashboard plugin **without** the in-engine "Audio Insights Template" plugin template. Everything you need to scaffold a working plugin — directory layout, `.uplugin` descriptor, `Build.cs`, and every source file — is inlined below. You only need access to the public headers in `Engine/Plugins/AudioInsights/Source/AudioInsights/Public/` to compile against the framework.
>
> Load this file into an AI assistant context to drive the plugin scaffold and subsequent customization via natural-language prompts. No prior C++ knowledge of the Audio Insights framework is required.

---

## Optional Capabilities — Offer These to the Developer During Planning

Before scaffolding, ask the developer whether the new dashboard needs either of the following. Both are **opt-in** — do **not** add them by default. They introduce real complexity (extra includes, delegate plumbing, selection-state machines, two-way timing-view sync) and aren't appropriate for every dashboard.

1. **Editor asset context menu + Details dashboard integration** — when rows correspond to editor assets.
   - Right-click a row → **Browse to Asset** (Content Browser) and **Open in Editor**.
   - Selecting a row pushes the asset into the Audio Insights **Details** dashboard via `FAudioInsightsDetailsSelectionManager::SetSelectedAsset`.
   - **Reference implementation:** the **Sounds** tab — `Engine/Plugins/AudioInsights/Source/AudioInsights/Private/Views/SoundDashboardViewFactory.cpp`.
   - **How to add:** see [Recipe 11 — Editor Asset Context Menu & Details Dashboard Integration](#recipe-11--editor-asset-context-menu--details-dashboard-integration).

2. **Drive the cache timeline from row selection** — when rows carry a meaningful timestamp.
   - Selecting a row pauses the cache and seeks the Audio Insights timing view to that timestamp.
   - Deselecting (or clearing selection) resumes live playback.
   - Two-way sync: external time-marker changes highlight the matching row.
   - **Reference implementation:** the **Event Log** tab — `Engine/Plugins/AudioInsights/Source/AudioInsights/Private/Views/AudioEventLogDashboardViewFactory.cpp`.
   - **How to add:** see [Recipe 12 — Drive the Cache Timeline from Row Selection](#recipe-12--drive-the-cache-timeline-from-row-selection).

**Planning prompt template** — when working with an AI assistant, ask the developer something like:

> _"Will rows in this dashboard correspond to editor assets? If so, do you want right-click → Browse to Asset / Open in Editor, and selection to drive the Details dashboard?"_
>
> _"Do your messages carry a timestamp meaningful enough that the developer would want to scrub the cache timeline by clicking a row? (If yes, the Event Log tab's pattern applies.)"_

If the answer is no — or "not sure" — leave them out. They can always be added later via the recipes.

---

## What You Will Build

A new Unreal Engine plugin that adds a tab to the Audio Insights dashboard, displaying a table of "objects" populated from `UE_TRACE` events emitted by the engine or your game code.

The plugin uses a 4-layer pipeline:

```
Trace Events  →  Messages  →  Provider  →  Dashboard View Factory
(UE_TRACE)       (structs)     (data map)    (Slate table/tree UI)
```

**Layer 1: Trace Events** — Your sending-side code emits events with `UE_TRACE_LOG` macros on a named channel. Each event carries typed fields (uint32, float, strings, timestamps).

**Layer 2: Messages** — The analyzer (running on the trace analysis thread) deserializes trace events into C++ message structs. Messages are cached in a ring buffer and enqueued for processing.

**Layer 3: Provider** — The trace provider (ticking on the game thread) drains message queues and populates a `DeviceDataMap` — a per-audio-device map of entry key → dashboard entry. The provider also handles timeline scrubbing by replaying cached messages.

**Layer 4: Dashboard View Factory** — Defines the Slate UI: columns, sorting, filtering, icons, context menus. Reads from the provider's data map and renders a table or tree view inside an Audio Insights tab.

### Registration Flow

```
Plugin StartupModule()
  → Creates DashboardViewFactory
	→ Factory constructor creates TraceProvider + registers it with TraceModule
  → Registers factory with AudioInsightsModule
  → Tab appears in Audio Insights dashboard
```

---

## Prerequisites

- An Unreal Engine 5 source build (or installed build with source access) that contains the **Audio Insights** plugin at `Engine/Plugins/AudioInsights/`.
- Access to read the Audio Insights public headers (the framework you compile against):
  - `Engine/Plugins/AudioInsights/Source/AudioInsights/Public/`
  - `Engine/Plugins/AudioInsights/Source/AudioInsightsEditor/Public/` (editor-only registration)
- The `AudioInsights` plugin must be enabled in your project. The skeleton plugin's `.uplugin` declares a dependency on it (see below).

You do **not** need access to the in-engine plugin template, only the public framework headers listed above.

> **⚠️ Plugin isolation — read this before scaffolding.** Add new files **only** inside your new plugin's directory. Do **not** modify any existing engine files — that includes `AudioTraceUtil.cpp`/`.h`, anything under `Engine/Plugins/AudioInsights/Source/`, or any other engine source outside the new plugin folder. The plugin must compile against the public framework headers alone.
>
> **Why:** keeping the plugin self-contained lets future engine upgrades (UE version bumps, Epic hotfix integrations) cherry-pick cleanly with no merge conflicts in engine code. Every engine-side edit becomes a manual merge headache on the next sync.
>
> **If a feature seems to require an engine-side change:** stop and surface it. The framework almost certainly has an extension point (a virtual hook, a public helper, a registration call) that hasn't been used yet — modifying the engine is the wrong tool. Read the relevant header in `Engine/Plugins/AudioInsights/Source/AudioInsights/Public/` and look for the seam.

---

## Step 1 — Scaffold the Plugin Directory

Pick a plugin name (referred to below as `<PluginName>`, e.g. `ReverbInsights`). Replace every occurrence of `PLUGIN_NAME` and `<PluginName>` with that name. Create the following directory tree under your project's `Plugins/` folder (or under `Engine/Plugins/` for an engine-shared plugin):

```
<PluginName>/
├── <PluginName>.uplugin
├── Resources/                                ← optional: SVG/PNG icons
└── Source/
	└── <PluginName>/
		├── <PluginName>.Build.cs
		└── Private/
			├── <PluginName>Module.h
			├── <PluginName>Module.cpp
			├── <PluginName>Style.h
			├── <PluginName>Style.cpp
			├── Messages/
			│   ├── ObjectTraceMessages.h
			│   └── ObjectTraceMessages.cpp
			├── Providers/
			│   ├── ObjectTraceProvider.h
			│   └── ObjectTraceProvider.cpp
			└── Views/
				├── ObjectDashboardViewFactory.h
				└── ObjectDashboardViewFactory.cpp
```

> The file names use **"Object"** as a placeholder domain. After scaffolding, rename to your actual domain — see **Recipe 1**.

---

## Step 2 — Create the `.uplugin` Descriptor

Save as `<PluginName>/<PluginName>.uplugin`. Set `LoadingPhase` to `Default` so the module is loaded by the time the Audio Insights dashboard is constructed.

```json
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "1.0",
	"FriendlyName": "<PluginName>",
	"Description": "Audio Insights dashboard for <your domain>.",
	"Category": "Audio",
	"CreatedBy": "",
	"CreatedByURL": "",
	"DocsURL": "",
	"MarketplaceURL": "",
	"SupportURL": "",
	"EnabledByDefault": true,
	"CanContainContent": false,
	"IsBetaVersion": false,
	"IsExperimentalVersion": false,
	"Installed": false,
	"SupportedPrograms": [ "UnrealInsights" ],
	"Modules": [
		{
			"Name": "PLUGIN_NAME",
			"Type": "EditorAndProgram",
			"LoadingPhase": "Default",
			"ProgramAllowList": [ "UnrealInsights" ]
		}
	],
	"Plugins": [
		{
			"Name": "AudioInsights",
			"Enabled": true
		}
	]
}
```

Replace `PLUGIN_NAME` in `"Modules"[0].Name` with `<PluginName>`. `Type: EditorAndProgram` lets the module load both inside the Unreal Editor and inside the standalone UnrealInsights program.

---

## Step 3 — Create the `Build.cs`

Save as `Source/<PluginName>/<PluginName>.Build.cs`:

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PLUGIN_NAME : ModuleRules
{
	public PLUGIN_NAME(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			[
				"Core",
			]
		);

		PrivateDependencyModuleNames.AddRange(
			[
				"AudioInsights",
				"AudioMixerCore",
				"CoreUObject",
				"InputCore",
				"Projects",
				"Slate",
				"SlateCore",
				"TraceAnalysis",
			]
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				[
					"AudioInsightsEditor",
					"Engine",
				]
			);
		}
	}
}
```

Rename the C# class `PLUGIN_NAME` to `<PluginName>`.

---

## Step 4 — Module Files

These bootstrap the plugin and register the dashboard view factory with the Audio Insights module.

### `Private/<PluginName>Module.h`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FPLUGIN_NAMEModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
```

Rename `FPLUGIN_NAMEModule` → `F<PluginName>Module`. The plugin module lives in `Private/` and exports no symbols across module boundaries, so no `UE_API` / `<PLUGINNAME>_API` decoration is needed here. Reserve those macros for public headers that other modules link against.

### `Private/<PluginName>Module.cpp`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.
#include "PLUGIN_NAMEModule.h"

#include "IAudioInsightsModule.h"
#include "PLUGIN_NAMEStyle.h"
#include "Views/ObjectDashboardViewFactory.h"

#if WITH_EDITOR
#include "IAudioInsightsEditorModule.h"
#endif // WITH_EDITOR

void FPLUGIN_NAMEModule::StartupModule()
{
	if (IsRunningCommandlet())
	{
		return;
	}

	PLUGIN_NAME::FStyle::Initialize();

#if WITH_EDITOR
	IAudioInsightsEditorModule& AudioInsightsModule = IAudioInsightsEditorModule::GetChecked();
#else
	IAudioInsightsModule& AudioInsightsModule = IAudioInsightsModule::GetChecked();
#endif // WITH_EDITOR

	AudioInsightsModule.RegisterDashboardViewFactory(MakeShared<PLUGIN_NAME::FObjectDashboardViewFactory>());
}

void FPLUGIN_NAMEModule::ShutdownModule()
{
	if (IsRunningCommandlet())
	{
		return;
	}

	// Unregister the dashboard view factory only if Audio Insights is still loaded —
	// but always tear down the style set below, regardless, while Slate is still alive.
#if WITH_EDITOR
	if (IAudioInsightsEditorModule::IsModuleLoaded())
	{
		IAudioInsightsEditorModule::GetChecked().UnregisterDashboardViewFactory(PLUGIN_NAME::ObjectDashboardViewFactoryName);
	}
#else
	if (IAudioInsightsModule::IsModuleLoaded())
	{
		IAudioInsightsModule::GetChecked().UnregisterDashboardViewFactory(PLUGIN_NAME::ObjectDashboardViewFactoryName);
	}
#endif // WITH_EDITOR

	PLUGIN_NAME::FStyle::Shutdown();
}

IMPLEMENT_MODULE(FPLUGIN_NAMEModule, PLUGIN_NAME);
```

Rename the class and the `IMPLEMENT_MODULE` second argument to `<PluginName>`. The `PLUGIN_NAME::` namespace and `FObjectDashboardViewFactory` reference will be defined in **Step 7**.

---

## Step 5 — Style Files

The style set holds Slate brushes for any custom icons your dashboard renders. Even with no custom icons it must be present, because the view factory's `GetIcon()` reads from it (or falls back to `AudioInsights.Icon.Dashboard`).

**Lifetime:** the style is owned by an explicit `Initialize()` / `Shutdown()` pair invoked from the module's `StartupModule` / `ShutdownModule` — **not** a function-local static. Function-local statics tear down during program exit, after `main()` returns, at which point Slate's registry may already be gone — calling `UnRegisterSlateStyle` then risks touching destroyed state. This mirrors the pattern in `TraceInsights/Private/Insights/InsightsStyle.cpp`.

### `Private/<PluginName>Style.h`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Templates/SharedPointer.h"
#include "Textures/SlateIcon.h"
#include "UObject/NameTypes.h"

namespace PLUGIN_NAME
{
	class FStyle final : public FSlateStyleSet
	{
	public:
		explicit FStyle(const FName& InStyleSetName);
		virtual ~FStyle() = default;

		// Call from StartupModule / ShutdownModule. Do not rely on function-local-static destruction
		// for UnRegisterSlateStyle — Slate may already be torn down by then.
		static void Initialize();
		static void Shutdown();

		static const FStyle& Get();
		static const FName& GetStyleName();

		FSlateIcon CreateIcon(const FName& InName) const;

	private:
		static TSharedPtr<FStyle> StyleInstance;
	};
}
```

### `Private/<PluginName>Style.cpp`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "PLUGIN_NAMEStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "PLUGIN_NAME"

namespace PLUGIN_NAME
{
	TSharedPtr<FStyle> FStyle::StyleInstance = nullptr;

	FStyle::FStyle(const FName& InStyleSetName)
		: FSlateStyleSet(InStyleSetName)
	{
		SetParentStyleName(FAppStyle::GetAppStyleSetName());

		const TSharedPtr<const IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PLUGIN_NAME"));
		if (Plugin.IsValid())
		{
			SetContentRoot(Plugin->GetBaseDir() / "Resources");
		}
		SetCoreContentRoot(FPaths::EngineContentDir() / "Slate");

		const FVector2D Icon16(16.0f, 16.0f);

		// Add your custom icons here, e.g.:
		// Set("PLUGIN_NAME.Icon.Active", new IMAGE_BRUSH(TEXT("Icons/Active"), Icon16));
		// Set("PLUGIN_NAME.Icon.Status", new IMAGE_BRUSH_SVG(TEXT("Icons/Status"), Icon16));
	}

	void FStyle::Initialize()
	{
		if (!StyleInstance.IsValid())
		{
			StyleInstance = MakeShared<FStyle>(GetStyleName());
			FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
		}
	}

	void FStyle::Shutdown()
	{
		if (StyleInstance.IsValid())
		{
			FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
			ensure(StyleInstance.IsUnique());
			StyleInstance.Reset();
		}
	}

	const FStyle& FStyle::Get()
	{
		// Initialize() must have run from StartupModule before any caller dereferences the style.
		check(StyleInstance.IsValid());
		return *StyleInstance;
	}

	const FName& FStyle::GetStyleName()
	{
		static const FName StyleName = "PLUGIN_NAMEStyle";
		return StyleName;
	}

	FSlateIcon FStyle::CreateIcon(const FName& InName) const
	{
		return { GetStyleName(), InName };
	}
} // namespace PLUGIN_NAME

#undef LOCTEXT_NAMESPACE
```

The `IPluginManager::FindPlugin(TEXT("PLUGIN_NAME"))` call must receive your literal plugin folder name as a string — replace `PLUGIN_NAME` accordingly.

---

## Step 6 — Messages (Layer 2)

These define the trace event message types and the per-row dashboard data struct.

### `Private/Messages/ObjectTraceMessages.h`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

// Trace event schema reference — sending-side code lives in the engine/game (not in this plugin).
//
// Wrap declarations AND emissions in #if UE_AUDIO_PROFILERTRACE_ENABLED (compile-time gate; shipping
// builds compile this out entirely). Inside that gate, guard each emission with
// UE_TRACE_CHANNELEXPR_IS_ENABLED at runtime so cost is zero when the channel is off — this matters
// especially for high-frequency emissions on the audio render thread.
//
// Channel selection (both declared in Engine/Source/Runtime/AudioMixerCore/Public/AudioMixerTrace.h):
//   AudioChannel       — event-based messages: creation, destruction, state changes, infrequent updates.
//   AudioMixerChannel  — parametric / high-frequency messages: per-tick values, envelopes, meter data.
//
//   #include "AudioMixerTrace.h"   // declares AudioChannel and AudioMixerChannel
//
//   #if UE_AUDIO_PROFILERTRACE_ENABLED
//   UE_TRACE_EVENT_BEGIN(Object, ObjectCreated)
//       UE_TRACE_EVENT_FIELD(uint32, DeviceId)
//       UE_TRACE_EVENT_FIELD(uint32, ID)
//       UE_TRACE_EVENT_FIELD(uint64, Timestamp)
//       UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
//   UE_TRACE_EVENT_END()
//
//   UE_TRACE_EVENT_BEGIN(Object, ObjectValue)
//       UE_TRACE_EVENT_FIELD(uint32, DeviceId)
//       UE_TRACE_EVENT_FIELD(uint32, ID)
//       UE_TRACE_EVENT_FIELD(uint64, Timestamp)
//       UE_TRACE_EVENT_FIELD(float,  Value)
//   UE_TRACE_EVENT_END()
//   #endif // UE_AUDIO_PROFILERTRACE_ENABLED
//
// Event-based emission — AudioChannel:
//
//   #if UE_AUDIO_PROFILERTRACE_ENABLED
//   if (UE_TRACE_CHANNELEXPR_IS_ENABLED(AudioChannel))
//   {
//       UE_TRACE_LOG(Object, ObjectCreated, AudioChannel)
//           << ObjectCreated.DeviceId(InDeviceId)
//           << ObjectCreated.ID(InObjectId)
//           << ObjectCreated.Timestamp(FPlatformTime::Cycles64())
//           << ObjectCreated.Name(*InObjectName);
//   }
//   #endif
//
// Parametric / high-frequency emission — AudioMixerChannel (bail early so callsite work is also skipped):
//
//   #if UE_AUDIO_PROFILERTRACE_ENABLED
//   if (UE_TRACE_CHANNELEXPR_IS_ENABLED(AudioMixerChannel))
//   {
//       UE_TRACE_LOG(Object, ObjectValue, AudioMixerChannel)
//           << ObjectValue.DeviceId(InDeviceId)
//           << ObjectValue.ID(InObjectId)
//           << ObjectValue.Timestamp(FPlatformTime::Cycles64())
//           << ObjectValue.Value(InValue);
//   }
//   #endif
//
// See Engine/Source/Runtime/Engine/Private/Audio/AudioTraceUtil.cpp (AudioChannel) and
// Engine/Source/Runtime/AudioMixer/Private/AudioMixerSubmix.cpp::TraceSubmixEnvelopeValues
// (AudioMixerChannel) for the canonical patterns in the Sounds / Submix trace plumbing.
//
// Channel/event names ("Object"/"ObjectCreated") must match the RouteEvent calls in ConstructAnalyzer,
// and field names must match the GetValue/GetString calls in each message constructor below.

#pragma once

#include "Cache/IAudioCachedMessage.h"
#include "Messages/AnalyzerMessageQueue.h"
#include "Views/TableDashboardViewFactory.h"

namespace PLUGIN_NAME
{
	namespace ObjectMessageNames
	{
		extern const FName CreatedName;
		extern const FName ValueName;
		extern const FName DestroyedName;
	};

	struct FObjectMessageBase : UE::Audio::Insights::IAudioCachedMessage
	{
		FObjectMessageBase() = default;
		explicit FObjectMessageBase(const UE::Trace::IAnalyzer::FOnEventContext& InContext);

		virtual uint64 GetID() const override { return ID; }

		::Audio::FDeviceId DeviceId = INDEX_NONE;
		uint32 ID = INDEX_NONE;
	};

	struct FObjectMessageCreatedMessage : public FObjectMessageBase
	{
		FObjectMessageCreatedMessage() = default;
		FObjectMessageCreatedMessage(const UE::Trace::IAnalyzer::FOnEventContext& InContext);

		virtual const FName GetMessageName() const override { return ObjectMessageNames::CreatedName; }
		virtual uint32 GetSizeOf() const override;
		virtual UE::Audio::Insights::FCacheWriteHandler GetCacheWriteHandler() const override;

		FString Name;
	};

	struct FObjectMessageValueMessage : public FObjectMessageBase
	{
		FObjectMessageValueMessage() = default;
		FObjectMessageValueMessage(const UE::Trace::IAnalyzer::FOnEventContext& InContext);

		virtual const FName GetMessageName() const override { return ObjectMessageNames::ValueName; }
		virtual uint32 GetSizeOf() const override;

		// Opt-in for snapshot (.utrace) export. Base returns {}; messages without this override are excluded.
		virtual UE::Audio::Insights::FCacheWriteHandler GetCacheWriteHandler() const override;

		float Value = 0.0f;
	};

	struct FObjectMessageDestroyedMessage : public FObjectMessageBase
	{
		FObjectMessageDestroyedMessage() = default;
		FObjectMessageDestroyedMessage(const UE::Trace::IAnalyzer::FOnEventContext& InContext);

		virtual const FName GetMessageName() const override { return ObjectMessageNames::DestroyedName; }
		virtual uint32 GetSizeOf() const override;
		virtual UE::Audio::Insights::FCacheWriteHandler GetCacheWriteHandler() const override;
	};

	class FObjectMessages
	{
		UE::Audio::Insights::TAnalyzerMessageQueue<FObjectMessageCreatedMessage>   CreatedMessages;
		UE::Audio::Insights::TAnalyzerMessageQueue<FObjectMessageValueMessage>     ValueMessages;
		UE::Audio::Insights::TAnalyzerMessageQueue<FObjectMessageDestroyedMessage> DestroyedMessages;

		friend class FObjectTraceProvider;
	};

	struct FObjectDashboardEntry : UE::Audio::Insights::IObjectDashboardEntry
	{
		FObjectDashboardEntry() = default;
		virtual ~FObjectDashboardEntry() = default;

		virtual bool IsValid() const override;

		virtual FText GetDisplayName() const override;
		virtual FString GetObjectPath() const override { return Name; }
		virtual const UObject* GetObject() const override;
		virtual UObject* GetObject() override;

		FString Name;

		::Audio::FDeviceId DeviceId = INDEX_NONE;
		uint32 ID = INDEX_NONE;

		double Timestamp = 0.0;

		float Value = 0.0f;
	};
} // namespace PLUGIN_NAME
```

### `Private/Messages/ObjectTraceMessages.cpp`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectTraceMessages.h"

#include "AudioInsightsConstants.h"
#include "Trace/TraceWriter.h"
#include "TraceWriter/CacheWriteHandler.h"

namespace PLUGIN_NAME
{
	using ::UE::Trace::IAnalyzer;
	using ::UE::Audio::Insights::FCacheWriteHandler;

	namespace ObjectMessageNames
	{
		extern const FName CreatedName   = "ObjectCreated";
		extern const FName ValueName     = "ObjectValue";
		extern const FName DestroyedName = "ObjectDestroyed";
	};

	// FObjectMessageBase
	FObjectMessageBase::FObjectMessageBase(const IAnalyzer::FOnEventContext& InContext)
	{
		const IAnalyzer::FEventData& EventData = InContext.EventData;

		DeviceId  = EventData.GetValue<uint32>("DeviceId");
		ID        = EventData.GetValue<uint32>("ID");
		Timestamp = InContext.EventTime.AsSeconds(EventData.GetValue<uint64>(UE::Audio::Insights::TimestampFieldName));
	}

	// FObjectMessageCreatedMessage
	FObjectMessageCreatedMessage::FObjectMessageCreatedMessage(const IAnalyzer::FOnEventContext& InContext)
		: FObjectMessageBase(InContext)
	{
		const IAnalyzer::FEventData& EventData = InContext.EventData;

		EventData.GetString("Name", Name);
	}

	uint32 FObjectMessageCreatedMessage::GetSizeOf() const
	{
		return sizeof(FObjectMessageCreatedMessage);
	}

	FCacheWriteHandler FObjectMessageCreatedMessage::GetCacheWriteHandler() const
	{
		return {
			[](UE::Trace::FTraceWriter& Writer) -> uint32
			{
				return Writer.DeclareEvent(ANSITEXTVIEW("Object"), ANSITEXTVIEW("ObjectCreated"))
					.Field(ANSITEXTVIEW("DeviceId"),  UE::Trace::ETraceWriterFieldType::Uint32)
					.Field(ANSITEXTVIEW("ID"),        UE::Trace::ETraceWriterFieldType::Uint32)
					.Field(ANSITEXTVIEW("Timestamp"), UE::Trace::ETraceWriterFieldType::Uint64)
					.Field(ANSITEXTVIEW("Name"),      UE::Trace::ETraceWriterFieldType::WideString)
					.End();
			},
			[](UE::Trace::FTraceWriter& Writer, uint32 EventId, const IAudioCachedMessage& BaseMsg, uint64 TimestampCycles)
			{
				const FObjectMessageCreatedMessage& Msg = static_cast<const FObjectMessageCreatedMessage&>(BaseMsg);

				Writer.WriteEvent(EventId)
					.Field("DeviceId",  Msg.DeviceId)
					.Field("ID",        Msg.ID)
					.Field("Timestamp", TimestampCycles)
					.Field("Name",      FStringView(Msg.Name))
					.End();
			}
		};
	}

	// FObjectMessageValueMessage
	FObjectMessageValueMessage::FObjectMessageValueMessage(const IAnalyzer::FOnEventContext& InContext)
		: FObjectMessageBase(InContext)
	{
		const IAnalyzer::FEventData& EventData = InContext.EventData;

		Value = EventData.GetValue<float>("Value");
	}

	uint32 FObjectMessageValueMessage::GetSizeOf() const
	{
		return sizeof(FObjectMessageValueMessage);
	}

	FCacheWriteHandler FObjectMessageValueMessage::GetCacheWriteHandler() const
	{
		return {
			[](UE::Trace::FTraceWriter& Writer) -> uint32
			{
				return Writer.DeclareEvent(ANSITEXTVIEW("Object"), ANSITEXTVIEW("ObjectValue"))
					.Field(ANSITEXTVIEW("DeviceId"),  UE::Trace::ETraceWriterFieldType::Uint32)
					.Field(ANSITEXTVIEW("ID"),        UE::Trace::ETraceWriterFieldType::Uint32)
					.Field(ANSITEXTVIEW("Timestamp"), UE::Trace::ETraceWriterFieldType::Uint64)
					.Field(ANSITEXTVIEW("Value"),     UE::Trace::ETraceWriterFieldType::Float32)
					.End();
			},
			[](UE::Trace::FTraceWriter& Writer, uint32 EventId, const IAudioCachedMessage& BaseMsg, uint64 TimestampCycles)
			{
				const FObjectMessageValueMessage& Msg = static_cast<const FObjectMessageValueMessage&>(BaseMsg);

				Writer.WriteEvent(EventId)
					.Field("DeviceId",  Msg.DeviceId)
					.Field("ID",        Msg.ID)
					.Field("Timestamp", TimestampCycles)
					.Field("Value",     Msg.Value)
					.End();
			}
		};
	}

	// FObjectMessageDestroyedMessage
	FObjectMessageDestroyedMessage::FObjectMessageDestroyedMessage(const IAnalyzer::FOnEventContext& InContext)
		: FObjectMessageBase(InContext)
	{
	}

	uint32 FObjectMessageDestroyedMessage::GetSizeOf() const
	{
		return sizeof(FObjectMessageDestroyedMessage);
	}

	FCacheWriteHandler FObjectMessageDestroyedMessage::GetCacheWriteHandler() const
	{
		return {
			[](UE::Trace::FTraceWriter& Writer) -> uint32
			{
				return Writer.DeclareEvent(ANSITEXTVIEW("Object"), ANSITEXTVIEW("ObjectDestroyed"))
					.Field(ANSITEXTVIEW("DeviceId"),  UE::Trace::ETraceWriterFieldType::Uint32)
					.Field(ANSITEXTVIEW("ID"),        UE::Trace::ETraceWriterFieldType::Uint32)
					.Field(ANSITEXTVIEW("Timestamp"), UE::Trace::ETraceWriterFieldType::Uint64)
					.End();
			},
			[](UE::Trace::FTraceWriter& Writer, uint32 EventId, const IAudioCachedMessage& BaseMsg, uint64 TimestampCycles)
			{
				const FObjectMessageDestroyedMessage& Msg = static_cast<const FObjectMessageDestroyedMessage&>(BaseMsg);

				Writer.WriteEvent(EventId)
					.Field("DeviceId",  Msg.DeviceId)
					.Field("ID",        Msg.ID)
					.Field("Timestamp", TimestampCycles)
					.End();
			}
		};
	}

	// FObjectDashboardEntry
	FText FObjectDashboardEntry::GetDisplayName() const
	{
		const FString DisplayName = FSoftObjectPath(Name).GetAssetName();
		return DisplayName.IsEmpty() ? FText::FromString(Name) : FText::FromString(DisplayName);
	}

	const UObject* FObjectDashboardEntry::GetObject() const
	{
		return FSoftObjectPath(Name).ResolveObject();
	}

	UObject* FObjectDashboardEntry::GetObject()
	{
		return FSoftObjectPath(Name).ResolveObject();
	}

	bool FObjectDashboardEntry::IsValid() const
	{
		return ID != static_cast<uint32>(INDEX_NONE);
	}
} // namespace PLUGIN_NAME
```

---

## Step 7 — Provider (Layer 3)

The provider owns the analyzer, drains the message queues each tick, and reconstructs state for timeline scrubbing.

### `Private/Providers/ObjectTraceProvider.h`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioInsightsTraceProviderBase.h"
#include "Messages/ObjectTraceMessages.h"

namespace PLUGIN_NAME
{
	class FObjectTraceProvider
		: public UE::Audio::Insights::TDeviceDataMapTraceProvider<uint32, TSharedPtr<FObjectDashboardEntry>>
		, public TSharedFromThis<FObjectTraceProvider>
	{
	public:
		FObjectTraceProvider();
		virtual ~FObjectTraceProvider() = default;

		// Analyzer is defined as an inner class in the .cpp so each provider can own its own RouteId enum and dispatch.
		virtual UE::Trace::IAnalyzer* ConstructAnalyzer(TraceServices::IAnalysisSession& InSession) override;

		// Required by FTraceDashboardViewFactoryBase::FindProvider.
		static FName GetName_Static();

	private:
		virtual bool ProcessMessages() override;
		virtual void OnTimingViewTimeMarkerChanged(double TimeMarker) override;

		FObjectMessages TraceMessages;
	};
} // namespace PLUGIN_NAME
```

### `Private/Providers/ObjectTraceProvider.cpp`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectTraceProvider.h"

#include "AudioInsightsConstants.h"
#include "Cache/AudioInsightsCacheManager.h"
#include "IAudioInsightsModule.h"

namespace PLUGIN_NAME
{
	using ::UE::Trace::IAnalyzer;

	FObjectTraceProvider::FObjectTraceProvider()
		: UE::Audio::Insights::TDeviceDataMapTraceProvider<uint32, TSharedPtr<FObjectDashboardEntry>>(GetName_Static())
	{
	}

	FName FObjectTraceProvider::GetName_Static()
	{
		static const FLazyName TraceProviderName = "ObjectProvider";
		return TraceProviderName;
	}

	IAnalyzer* FObjectTraceProvider::ConstructAnalyzer(TraceServices::IAnalysisSession& InSession)
	{
		class FTraceAnalyzer : public FTraceAnalyzerBase
		{
		public:
			FTraceAnalyzer(TSharedRef<FObjectTraceProvider> InProvider, TraceServices::IAnalysisSession& InSession)
				: FTraceAnalyzerBase(InProvider)
				, Session(InSession)
			{
			}

			virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override
			{
				FTraceAnalyzerBase::OnAnalysisBegin(Context);

				UE::Trace::IAnalyzer::FInterfaceBuilder& Builder = Context.InterfaceBuilder;

				Builder.RouteEvent(RouteId_Created,   "Object", "ObjectCreated");
				Builder.RouteEvent(RouteId_Value,     "Object", "ObjectValue");
				Builder.RouteEvent(RouteId_Destroyed, "Object", "ObjectDestroyed");
			}

			virtual bool OnHandleEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override
			{
				LLM_SCOPE_BYNAME(TEXT("Insights/FTraceAnalyzer"));

				FObjectMessages& Messages = GetProvider<FObjectTraceProvider>().TraceMessages;

				switch (RouteId)
				{
					case RouteId_Created:
					{
						CacheMessage<FObjectMessageCreatedMessage>(Context, Messages.CreatedMessages);
						break;
					}
					case RouteId_Value:
					{
						CacheMessage<FObjectMessageValueMessage>(Context, Messages.ValueMessages);
						break;
					}
					case RouteId_Destroyed:
					{
						CacheMessage<FObjectMessageDestroyedMessage>(Context, Messages.DestroyedMessages);
						break;
					}
					default:
					{
						return OnEventFailure(RouteId, Style, Context);
					}
				}

				const double Timestamp = Context.EventTime.AsSeconds(Context.EventData.GetValue<uint64>(UE::Audio::Insights::TimestampFieldName));

				{
					TraceServices::FAnalysisSessionEditScope SessionEditScope(Session);
					Session.UpdateDurationSeconds(Timestamp);
				}

				return OnEventSuccess(RouteId, Style, Context);
			}

		private:
			enum : uint16
			{
				RouteId_Created,
				RouteId_Value,
				RouteId_Destroyed
			};

			TraceServices::IAnalysisSession& Session;
		};

		// AsShared() assumes this provider is owned by a TSharedPtr/TSharedRef — the dashboard factory
		// creates it via MakeShared<FObjectTraceProvider>(). If that ownership strategy changes,
		// AsShared() will assert.
		return new FTraceAnalyzer(AsShared(), InSession);
	}

	bool FObjectTraceProvider::ProcessMessages()
	{
		auto CreateEntryFunc = [this](const FObjectMessageBase& Msg)
		{
			TSharedPtr<FObjectDashboardEntry>* ToReturn = nullptr;

			UpdateDeviceEntry(Msg.DeviceId, Msg.ID, [&ToReturn, &Msg](TSharedPtr<FObjectDashboardEntry>& Entry)
			{
				if (!Entry.IsValid())
				{
					Entry = MakeShared<FObjectDashboardEntry>();

					Entry->DeviceId = Msg.DeviceId;
					Entry->ID       = Msg.ID;
				}

				Entry->Timestamp = Msg.Timestamp;

				ToReturn = &Entry;
			});

			return ToReturn;
		};

		auto GetEntryFunc = [this](const FObjectMessageBase& Msg)
		{
			return FindDeviceEntry(Msg.DeviceId, Msg.ID);
		};

		ProcessMessageQueue<FObjectMessageCreatedMessage>(TraceMessages.CreatedMessages, CreateEntryFunc,
		[](const FObjectMessageCreatedMessage& Msg, TSharedPtr<FObjectDashboardEntry>* OutEntry)
		{
			if (OutEntry != nullptr)
			{
				(*OutEntry)->Name = Msg.Name;
			}
		});

		ProcessMessageQueue<FObjectMessageValueMessage>(TraceMessages.ValueMessages, GetEntryFunc,
		[](const FObjectMessageValueMessage& Msg, TSharedPtr<FObjectDashboardEntry>* OutEntry)
		{
			if (OutEntry != nullptr)
			{
				(*OutEntry)->Value = Msg.Value;
			}
		});

		ProcessMessageQueue<FObjectMessageDestroyedMessage>(TraceMessages.DestroyedMessages, GetEntryFunc,
		[this](const FObjectMessageDestroyedMessage& Msg, TSharedPtr<FObjectDashboardEntry>* OutEntry)
		{
			if (OutEntry != nullptr)
			{
				RemoveDeviceEntry(Msg.DeviceId, Msg.ID);
			}
		});

		return true;
	}

	void FObjectTraceProvider::OnTimingViewTimeMarkerChanged(double TimeMarker)
	{
		using namespace ::UE::Audio::Insights;

		DeviceDataMap.Empty();

		const FAudioInsightsCacheManager& CacheManager = IAudioInsightsModule::GetChecked().GetCacheManager();

		CacheManager.IterateTo<FObjectMessageCreatedMessage>(ObjectMessageNames::CreatedName, TimeMarker,
		[this](const FObjectMessageCreatedMessage& Msg)
		{
			UpdateDeviceEntry(Msg.DeviceId, Msg.ID, [&Msg](TSharedPtr<FObjectDashboardEntry>& Entry)
			{
				if (!Entry.IsValid())
				{
					Entry = MakeShared<FObjectDashboardEntry>();
					Entry->DeviceId = Msg.DeviceId;
					Entry->ID       = Msg.ID;
				}

				Entry->Timestamp = Msg.Timestamp;
				Entry->Name      = Msg.Name;
			});
		});

		CacheManager.IterateTo<FObjectMessageDestroyedMessage>(ObjectMessageNames::DestroyedName, TimeMarker,
		[this](const FObjectMessageDestroyedMessage& Msg)
		{
			auto* OutEntry = FindDeviceEntry(Msg.DeviceId, Msg.ID);

			if (OutEntry != nullptr && (*OutEntry)->Timestamp < Msg.Timestamp)
			{
				RemoveDeviceEntry(Msg.DeviceId, Msg.ID);
			}
		});

		const FDeviceData* DeviceData = FindFilteredDeviceData();

		if (DeviceData != nullptr)
		{
			for (auto& [ID, Entry] : *DeviceData)
			{
				const FObjectMessageValueMessage* FoundMessage = CacheManager.FindClosestMessage<FObjectMessageValueMessage>(
					ObjectMessageNames::ValueName, TimeMarker, ID);

				if (FoundMessage != nullptr)
				{
					Entry->Timestamp = FoundMessage->Timestamp;
					Entry->Value     = FoundMessage->Value;
				}
			}
		}

		// Parent call advances LastMessageId — required so the dashboard refresh tick picks up the new state.
		FTraceProviderBase::OnTimingViewTimeMarkerChanged(TimeMarker);
	}
} // namespace PLUGIN_NAME
```

---

## Step 8 — Dashboard View Factory (Layer 4)

This defines the actual tab UI — columns, sorting, filtering, icon, and tab group.

### `Private/Views/ObjectDashboardViewFactory.h`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Views/TableDashboardViewFactory.h"

namespace PLUGIN_NAME
{
	extern const FLazyName ObjectDashboardViewFactoryName;

	class FObjectDashboardViewFactory : public UE::Audio::Insights::FTraceObjectTableDashboardViewFactory
	{
	public:
		FObjectDashboardViewFactory();
		virtual ~FObjectDashboardViewFactory() = default;

		virtual FName GetName() const override;
		virtual FText GetDisplayName() const override;
		virtual FSlateIcon GetIcon() const override;
		virtual UE::Audio::Insights::EDefaultDashboardTabStack GetDefaultTabStack() const override;

	private:
		virtual TSharedRef<SWidget> MakeWidget(TSharedRef<SDockTab> OwnerTab, const FSpawnTabArgs& SpawnTabArgs) override;

		virtual const TMap<FName, FColumnData>& GetColumns() const override;
		virtual void ProcessEntries(EProcessReason Reason) override;
		virtual void SortTable() override;
	};
} // namespace PLUGIN_NAME
```

### `Private/Views/ObjectDashboardViewFactory.cpp`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectDashboardViewFactory.h"

#include "AudioInsightsStyle.h"
#include "IAudioInsightsModule.h"
#include "PLUGIN_NAMEStyle.h"
#include "Providers/ObjectTraceProvider.h"
#include "Templates/SharedPointer.h"

#define LOCTEXT_NAMESPACE "PLUGIN_NAME"

namespace PLUGIN_NAME
{
	using namespace ::UE::Audio::Insights;

	namespace FObjectDashboardViewFactoryPrivate
	{
		const FName NameColumnName  = "Name";
		const FName ValueColumnName = "Value";
	} // namespace FObjectDashboardViewFactoryPrivate

	const FLazyName ObjectDashboardViewFactoryName("Object");

	FObjectDashboardViewFactory::FObjectDashboardViewFactory()
	{
		IAudioInsightsTraceModule& InsightsTraceModule = IAudioInsightsModule::GetChecked().GetTraceModule();

		TSharedPtr<FObjectTraceProvider> Provider = MakeShared<FObjectTraceProvider>();
		InsightsTraceModule.AddTraceProvider(Provider);

		Providers =
		{
			StaticCastSharedPtr<FTraceProviderBase>(Provider)
		};

		SortByColumn = FObjectDashboardViewFactoryPrivate::NameColumnName;
		SortMode     = EColumnSortMode::Ascending;
	}

	FName FObjectDashboardViewFactory::GetName() const
	{
		return PLUGIN_NAME::ObjectDashboardViewFactoryName;
	}

	FText FObjectDashboardViewFactory::GetDisplayName() const
	{
		return LOCTEXT("Object_Name", "Object");
	}

	FSlateIcon FObjectDashboardViewFactory::GetIcon() const
	{
		// To use a custom icon, register one in FStyle and reference it here:
		// return PLUGIN_NAME::FStyle::Get().CreateIcon("PLUGIN_NAME.Icon.TabIcon");
		return UE::Audio::Insights::FSlateStyle::Get().CreateIcon("AudioInsights.Icon.Dashboard");
	}

	EDefaultDashboardTabStack FObjectDashboardViewFactory::GetDefaultTabStack() const
	{
		return EDefaultDashboardTabStack::Log;
	}

	TSharedRef<SWidget> FObjectDashboardViewFactory::MakeWidget(TSharedRef<SDockTab> OwnerTab, const FSpawnTabArgs& SpawnTabArgs)
	{
		return FTraceTableDashboardViewFactory::MakeWidget(OwnerTab, SpawnTabArgs);
	}

	const TMap<FName, FTraceTableDashboardViewFactory::FColumnData>& FObjectDashboardViewFactory::GetColumns() const
	{
		using namespace FObjectDashboardViewFactoryPrivate;

		auto CreateColumnData = []()
		{
			return TMap<FName, FTraceTableDashboardViewFactory::FColumnData>
			{
				{
					NameColumnName,
					{
						LOCTEXT("ObjectDashboard_NameColumnDisplayName", "Name"),
						[](const IDashboardDataViewEntry& InData)
						{
							return CastEntry<FObjectDashboardEntry>(InData).GetDisplayName();
						},
						nullptr /*GetIconName*/,
						false   /* bDefaultHidden */,
						0.85f   /* FillWidth */
					}
				},
				{
					ValueColumnName,
					{
						LOCTEXT("ObjectDashboard_ValueColumnDisplayName", "Value"),
						[](const IDashboardDataViewEntry& InData)
						{
							const FObjectDashboardEntry& ObjectDashboardEntry = CastEntry<FObjectDashboardEntry>(InData);
							return FText::AsNumber(ObjectDashboardEntry.Value);
						},
						nullptr /*GetIconName*/,
						false   /* bDefaultHidden */,
						0.15f   /* FillWidth */
					}
				}
			};
		};

		static const TMap<FName, FTraceTableDashboardViewFactory::FColumnData> ColumnData = CreateColumnData();

		return ColumnData;
	}

	void FObjectDashboardViewFactory::ProcessEntries(EProcessReason Reason)
	{
		const FString FilterString = GetSearchFilterText().ToString();

		FilterEntries<FObjectTraceProvider>([&FilterString](const IDashboardDataViewEntry& Entry)
		{
			const FObjectDashboardEntry& DashboardEntry = CastEntry<FObjectDashboardEntry>(Entry);
			return DashboardEntry.GetDisplayName().ToString().Contains(FilterString);
		});
	}

	void FObjectDashboardViewFactory::SortTable()
	{
		using namespace FObjectDashboardViewFactoryPrivate;

		if (SortByColumn == NameColumnName)
		{
			SortByPredicate<FObjectDashboardEntry>([](const FObjectDashboardEntry& First, const FObjectDashboardEntry& Second)
			{
				const int32 NameComparison = First.GetDisplayName().CompareToCaseIgnored(Second.GetDisplayName());

				if (NameComparison != 0)
				{
					return NameComparison < 0;
				}

				return First.ID < Second.ID;
			});
		}
		else if (SortByColumn == ValueColumnName)
		{
			SortByPredicate<FObjectDashboardEntry>([](const FObjectDashboardEntry& First, const FObjectDashboardEntry& Second)
			{
				if (!FMath::IsNearlyEqual(First.Value, Second.Value, UE_KINDA_SMALL_NUMBER))
				{
					return First.Value < Second.Value;
				}

				return First.ID < Second.ID;
			});
		}
	}
} // namespace PLUGIN_NAME

#undef LOCTEXT_NAMESPACE
```

---

## Step 9 — Replace `PLUGIN_NAME` Throughout

Once all files are in place, do a project-wide find-and-replace of the literal token `PLUGIN_NAME` with your plugin's name. Examples for `<PluginName>` = `ReverbInsights`:

| Token | Replacement |
|-------|-------------|
| `PLUGIN_NAME` (folder + namespace + module name) | `ReverbInsights` |
| `FPLUGIN_NAMEModule` | `FReverbInsightsModule` |
| `PLUGIN_NAMEStyle` (header includes) | `ReverbInsightsStyle` |
| `"PLUGIN_NAME"` string literal (LOCTEXT, plugin lookup) | `"ReverbInsights"` |

The `Object*` names (`FObjectTraceProvider`, `ObjectDashboardViewFactoryName`, etc.) are the **domain** placeholder, not the plugin name — leave them alone for now and rename them in **Recipe 1**.

---

## Step 10 — Generate, Build, Verify

1. Regenerate project files: run `GenerateProjectFiles.bat` (Windows) or `GenerateProjectFiles.sh` (Mac/Linux) at the engine root.
2. Build the project (e.g. `Build.bat <ProjectName>Editor Win64 Development`).
3. Launch the Unreal Editor. Confirm your plugin is enabled in **Edit → Plugins** under the **Audio** category.
4. Open **Window → Audio Insights**. A new **Object** tab should appear in the Log tab stack. It will be empty until your sending-side code emits matching `UE_TRACE` events — see the comment block at the top of `ObjectTraceMessages.h` for the schema your emitting code must match.

> The scaffold keeps a few orientation comments (the trace schema example, the `AsShared` ownership note, the `LastMessageId` parent-call note). Once you have the pattern in your head, strip any remaining narration comments — Epic's UE5 convention is that well-named identifiers document themselves, and comments should only flag non-obvious *why*.

---

# Customization Recipes

> All recipes assume you are editing the files produced above. They use the `Object` domain placeholder; once renamed (Recipe 1), substitute your domain.

## Recipe 1 — Rename the Domain

The scaffold uses **"Object"** as the placeholder domain. To rename it to your domain (e.g. **"Reverb"**):

**Files to change:** everything in `Messages/`, `Providers/`, `Views/`.

**What to rename:**
- Struct names: `FObjectMessageBase` → `FReverbMessageBase`, `FObjectDashboardEntry` → `FReverbDashboardEntry`, etc.
- Namespace members: `ObjectMessageNames` → `ReverbMessageNames`
- File names: `ObjectTraceMessages.h` → `ReverbTraceMessages.h`, and so on
- Factory name constant: `ObjectDashboardViewFactoryName` → `ReverbDashboardViewFactoryName`
- Display name in `GetDisplayName()`: `"Object"` → `"Reverb"`
- LOCTEXT keys: update to match the new domain name
- Provider name in `GetName_Static()`: `"ObjectProvider"` → `"ReverbProvider"`

**Important:** the trace channel/event names in `RouteEvent()` calls (e.g. `"Object"`, `"ObjectCreated"`) must match what your sending-side code emits via `UE_TRACE_LOG`. If you change them here, you must also change the `UE_TRACE_EVENT_BEGIN` declarations on the emitting side.

---

## Recipe 2 — Add a New Message Type

To add a new trace event — e.g. a `StatusChanged` event with a status enum:

> **Channel reminder for the sending side:** on the engine/game side that emits this event, use `AudioChannel` for one-shot / state-change events (`Created`, `StatusChanged`, `Destroyed`) and `AudioMixerChannel` for per-tick parametric streams (`Value`, envelope, meter). Wrap declarations and emissions in `#if UE_AUDIO_PROFILERTRACE_ENABLED` and gate each emission with `UE_TRACE_CHANNELEXPR_IS_ENABLED(<channel>)` — see the schema block at the top of [`ObjectTraceMessages.h`](#step-6--messages-layer-2) for the full pattern.

**Step 1: `ObjectTraceMessages.h`** — add the message struct:
```cpp
// Add to the MessageNames namespace:
extern const FName StatusChangedName;

// Add the struct:
struct FObjectStatusChangedMessage : public FObjectMessageBase
{
	FObjectStatusChangedMessage() = default;
	FObjectStatusChangedMessage(const UE::Trace::IAnalyzer::FOnEventContext& InContext);

	virtual const FName GetMessageName() const override { return ObjectMessageNames::StatusChangedName; }
	virtual uint32 GetSizeOf() const override;
	virtual UE::Audio::Insights::FCacheWriteHandler GetCacheWriteHandler() const override;

	uint8 Status = 0;
};
```

**Step 2: `ObjectTraceMessages.cpp`** — implement constructor and cache write handler:
```cpp
// Add name definition (match the route event name):
extern const FName StatusChangedName = "ObjectStatusChanged";

// Constructor — deserialize from trace event:
FObjectStatusChangedMessage::FObjectStatusChangedMessage(const IAnalyzer::FOnEventContext& InContext)
	: FObjectMessageBase(InContext)
{
	Status = InContext.EventData.GetValue<uint8>("Status");
}

uint32 FObjectStatusChangedMessage::GetSizeOf() const
{
	return sizeof(FObjectStatusChangedMessage);
}

// Cache write handler — enables snapshot export:
FCacheWriteHandler FObjectStatusChangedMessage::GetCacheWriteHandler() const
{
	return {
		[](UE::Trace::FTraceWriter& Writer) -> uint32
		{
			return Writer.DeclareEvent(ANSITEXTVIEW("Object"), ANSITEXTVIEW("ObjectStatusChanged"))
				.Field(ANSITEXTVIEW("DeviceId"),  UE::Trace::ETraceWriterFieldType::Uint32)
				.Field(ANSITEXTVIEW("ID"),        UE::Trace::ETraceWriterFieldType::Uint32)
				.Field(ANSITEXTVIEW("Timestamp"), UE::Trace::ETraceWriterFieldType::Uint64)
				.Field(ANSITEXTVIEW("Status"),    UE::Trace::ETraceWriterFieldType::Uint8)
				.End();
		},
		[](UE::Trace::FTraceWriter& Writer, uint32 EventId,
		   const IAudioCachedMessage& BaseMsg, uint64 TimestampCycles)
		{
			const FObjectStatusChangedMessage& Msg =
				static_cast<const FObjectStatusChangedMessage&>(BaseMsg);

			Writer.WriteEvent(EventId)
				.Field("DeviceId",  Msg.DeviceId)
				.Field("ID",        Msg.ID)
				.Field("Timestamp", TimestampCycles)
				.Field("Status",    Msg.Status)
				.End();
		}
	};
}
```

**Step 3: `ObjectTraceMessages.h`** — add a queue to `FObjectMessages`:
```cpp
UE::Audio::Insights::TAnalyzerMessageQueue<FObjectStatusChangedMessage> StatusChangedMessages;
```

**Step 4: `ObjectTraceProvider.cpp` — `ConstructAnalyzer`** — add a route:
```cpp
// In OnAnalysisBegin:
Builder.RouteEvent(RouteId_StatusChanged, "Object", "ObjectStatusChanged");

// In OnHandleEvent switch:
case RouteId_StatusChanged:
{
	CacheMessage<FObjectStatusChangedMessage>(Context, Messages.StatusChangedMessages);
	break;
}

// In the RouteId enum:
RouteId_StatusChanged
```

**Step 5: `ObjectTraceProvider.cpp` — `ProcessMessages`** — handle the new queue:
```cpp
ProcessMessageQueue<FObjectStatusChangedMessage>(TraceMessages.StatusChangedMessages, GetEntryFunc,
[](const FObjectStatusChangedMessage& Msg, TSharedPtr<FObjectDashboardEntry>* OutEntry)
{
	if (OutEntry != nullptr)
	{
		(*OutEntry)->Status = Msg.Status;
	}
});
```

**Step 6: `ObjectTraceMessages.h`** — add the field to the dashboard entry:
```cpp
uint8 Status = 0;
```

**Step 7:** Add a column to the dashboard (see **Recipe 4**).

---

## Recipe 3 — Remove a Message Type

To remove a message type (e.g. `Value`):

1. Delete the struct from `ObjectTraceMessages.h`
2. Delete the constructor/handlers from `ObjectTraceMessages.cpp`
3. Remove the queue from `FObjectMessages`
4. Remove the `RouteEvent` and `case` from `ConstructAnalyzer`
5. Remove the `ProcessMessageQueue` call from `ProcessMessages`
6. Remove the field from `FObjectDashboardEntry`
7. Remove related column(s) from the dashboard factory

---

## Recipe 4 — Add a Dashboard Column

In `ObjectDashboardViewFactory.cpp`, add an entry to the map returned by `CreateColumnData()`:

```cpp
{
	FName("Status"),
	{
		LOCTEXT("StatusColumn", "Status"),          // Display name
		[](const IDashboardDataViewEntry& InData)   // Cell text
		{
			const FObjectDashboardEntry& Entry = CastEntry<FObjectDashboardEntry>(InData);
			return FText::AsNumber(Entry.Status);
		},
		nullptr,  // GetIconName (nullptr = no icon)
		false,    // bDefaultHidden
		0.15f     // FillWidth (proportion of table width)
	}
}
```

### Column with Icon

```cpp
{
	FName("Active"),
	{
		LOCTEXT("ActiveColumn", "Active"),
		[](const IDashboardDataViewEntry& InData)
		{
			return FText::GetEmpty(); // Icon-only column
		},
		[](const IDashboardDataViewEntry& InData) -> FName
		{
			const FObjectDashboardEntry& Entry = CastEntry<FObjectDashboardEntry>(InData);
			return Entry.bIsActive
				? "AudioInsights.Icon.Active"
				: NAME_None;
		},
		false,         // bDefaultHidden
		0.08f,         // FillWidth — narrow for icon columns
		HAlign_Center  // Alignment
	}
}
```

### `FColumnData` full field reference

```cpp
struct FColumnData
{
	FText DisplayName;                                                    // Column header text
	TFunction<FText(const IDashboardDataViewEntry&)> GetDisplayValue;     // Cell text content
	TFunction<FName(const IDashboardDataViewEntry&)> GetIconName;         // Icon brush name (optional)
	bool bDefaultHidden = false;                                          // Hidden by default
	float FillWidth = 1.0f;                                               // Width proportion
	EHorizontalAlignment Alignment = HAlign_Left;                         // Cell alignment
	TFunction<FLinearColor(const IDashboardDataViewEntry&)> GetIconColor; // Dynamic icon color (optional)
	TFunction<FText(const IDashboardDataViewEntry&)> GetIconTooltip;      // Icon tooltip (optional)
	FName HeaderRowIconName = NAME_None;                                  // Header icon
	FText HeaderRowTooltip = FText::GetEmpty();                           // Header tooltip
	bool bShowDisplayName = true;                                         // Show header text
};
```

---

## Recipe 5 — Add Sorting for a Column

In `ObjectDashboardViewFactory.cpp`, add a case in `SortTable()`:

```cpp
else if (SortByColumn == FName("Status"))
{
	SortByPredicate<FObjectDashboardEntry>(
		[](const FObjectDashboardEntry& A, const FObjectDashboardEntry& B)
	{
		if (A.Status != B.Status)
		{
			return A.Status < B.Status;
		}
		return A.ID < B.ID;  // Stable secondary sort
	});
}
```

The base class handles the ascending/descending toggle automatically — you only need to define the ascending predicate.

---

## Recipe 6 — Add Search Filtering

The scaffold filters on `GetDisplayName()`. To filter on additional fields, extend `ProcessEntries`:

```cpp
void FObjectDashboardViewFactory::ProcessEntries(EProcessReason Reason)
{
	const FString FilterString = GetSearchFilterText().ToString();

	FilterEntries<FObjectTraceProvider>([&FilterString](const IDashboardDataViewEntry& Entry)
	{
		const FObjectDashboardEntry& DashboardEntry = CastEntry<FObjectDashboardEntry>(Entry);

		// Match against name OR status text
		return DashboardEntry.GetDisplayName().ToString().Contains(FilterString)
			|| FString::Printf(TEXT("%d"), DashboardEntry.Status).Contains(FilterString);
	});
}
```

---

## Recipe 7 — Timeline Scrubbing

`OnTimingViewTimeMarkerChanged` is called when the user scrubs the Audio Insights timeline. It must reconstruct dashboard state at the given time by replaying cached messages.

The scaffold provides a working implementation. The pattern is:

1. **Clear** the device data map
2. **Replay Created** messages up to the time marker (populates entries)
3. **Replay Destroyed** messages up to the time marker (removes entries destroyed before that time)
4. **For surviving entries**, find the closest Value message at the given timestamp
5. **Call parent** `FTraceProviderBase::OnTimingViewTimeMarkerChanged(TimeMarker)` to update `LastMessageId`

Key API methods for cache replay:
```cpp
// Iterate all messages of type T up to a timestamp:
CacheManager.IterateTo<T>(MessageName, TimeMarker, [](const T& Msg) { ... });

// Find the closest message for a specific entry ID:
CacheManager.FindClosestMessage<T>(MessageName, TimeMarker, EntryID);
```

---

## Recipe 8 — Change the Dashboard Tab Group

In `ObjectDashboardViewFactory.cpp`, change `GetDefaultTabStack()`:

```cpp
EDefaultDashboardTabStack FObjectDashboardViewFactory::GetDefaultTabStack() const
{
	return EDefaultDashboardTabStack::Analysis; // or any value below
}
```

Available values:

| Value | Description |
|-------|-------------|
| `Viewport` | 3D viewport area |
| `Log` | Text log output (default for the scaffold) |
| `Analysis` | Analysis/statistics panels |
| `AudioMeters` | Audio meter display |
| `Plots` | Time-series plot area |
| `OutputMetering` | Output metering |
| `AudioAnalyzerRack` | Analyzer rack |

---

## Recipe 9 — Custom Icons

**Step 1:** Place SVG or PNG files in your plugin's `Resources/` directory.

**Step 2:** Register them in your `FStyle::FStyle()` constructor (`<PluginName>Style.cpp`):
```cpp
const FVector2D Icon16(16.0f, 16.0f);
const FVector2D Icon20(20.0f, 20.0f);

// For PNG:
Set("PLUGIN_NAME.Icon.Active", new IMAGE_BRUSH(TEXT("Icons/Active"), Icon16));

// For SVG:
Set("PLUGIN_NAME.Icon.Status", new IMAGE_BRUSH_SVG(TEXT("Icons/Status"), Icon20));
```

**Step 3:** Reference them by name from column definitions or `GetIcon()`:
```cpp
FSlateIcon FObjectDashboardViewFactory::GetIcon() const
{
	return PLUGIN_NAME::FStyle::Get().CreateIcon("PLUGIN_NAME.Icon.TabIcon");
}
```

---

## Recipe 10 — Skip Cache Write Handlers

If you don't need snapshot export for a message type, return an empty handler:

```cpp
FCacheWriteHandler FMyMessage::GetCacheWriteHandler() const
{
	return {}; // Excluded from cache snapshots
}
```

---

## Recipe 11 — Editor Asset Context Menu & Details Dashboard Integration

**Opt-in.** Add only if the developer confirmed it during planning (see [Optional Capabilities](#optional-capabilities--offer-these-to-the-developer-during-planning)). Pattern lifted from `SoundDashboardViewFactory.cpp` + `FAssetEditorContextMenuHelper`.

**Prerequisites:**
- `FObjectDashboardEntry::GetObject()` must return the resolvable `UObject*` for the row (the scaffold already does this via `FSoftObjectPath(Name).ResolveObject()`).
- `FObjectDashboardEntry::GetObjectPath()` must return the asset path string (the scaffold already does this — returns `Name`).

**Step 1 — `Build.cs`:** ensure `AudioInsightsEditor` is in the editor-only dependency block (the scaffold already includes it).

**Step 2 — `ObjectDashboardViewFactory.h`:** add the helper and an editor-only selection hook.

```cpp
#if WITH_EDITOR
#include "Views/AssetEditorContextMenuHelper.h"
#endif

// inside FObjectDashboardViewFactory:
protected:
	virtual TSharedPtr<SWidget> OnConstructContextMenu() override;
	virtual void OnSelectionChanged(TSharedPtr<IDashboardDataViewEntry> SelectedItem, ESelectInfo::Type SelectInfo) override;
	virtual FReply OnDataRowKeyInput(const FGeometry& Geometry, const FKeyEvent& KeyEvent) const override;

#if WITH_EDITOR
private:
	UE::Audio::Insights::FAssetEditorContextMenuHelper AssetEditorContextMenuHelper;
#endif
```

**Step 3 — `ObjectDashboardViewFactory.cpp`:** include the headers and implement.

```cpp
#if WITH_EDITOR
#include "AudioInsightsDetailsSelectionManager.h"
#include "Framework/Commands/UICommandList.h"
#endif

TSharedPtr<SWidget> FObjectDashboardViewFactory::OnConstructContextMenu()
{
#if WITH_EDITOR
	return AssetEditorContextMenuHelper.ContructContextMenuOptions();
#else
	return nullptr;
#endif
}

void FObjectDashboardViewFactory::OnSelectionChanged(TSharedPtr<IDashboardDataViewEntry> SelectedItem, ESelectInfo::Type SelectInfo)
{
	FTraceObjectTableDashboardViewFactory::OnSelectionChanged(SelectedItem, SelectInfo);

#if WITH_EDITOR
	const TSharedPtr<IObjectDashboardEntry> ObjectEntry = StaticCastSharedPtr<IObjectDashboardEntry>(SelectedItem);

	AssetEditorContextMenuHelper.SetAssetEntry(ObjectEntry);

	// Push the selected asset into the Details dashboard (only on real user-driven changes, not programmatic).
	if (SelectInfo != ESelectInfo::Type::Direct)
	{
		FAudioInsightsDetailsSelectionManager& SelectionManager = IAudioInsightsModule::GetChecked().GetDetailsSelectionManager();

		if (ObjectEntry.IsValid())
		{
			if (UObject* Asset = ObjectEntry->GetObject(); Asset != nullptr && Asset->IsAsset())
			{
				SelectionManager.SetSelectedAsset(Asset);
			}
			else
			{
				SelectionManager.ClearSelection();
			}
		}
		else
		{
			SelectionManager.ClearSelection();
		}
	}
#endif // WITH_EDITOR
}

FReply FObjectDashboardViewFactory::OnDataRowKeyInput(const FGeometry& Geometry, const FKeyEvent& KeyEvent) const
{
#if WITH_EDITOR
	if (AssetEditorContextMenuHelper.ProcessCommandBindings(KeyEvent))
	{
		return FReply::Handled();
	}
#endif
	return FTraceObjectTableDashboardViewFactory::OnDataRowKeyInput(Geometry, KeyEvent);
}
```

Notes:
- `FAssetEditorContextMenuHelper` builds the menu (Browse / Open) using the engine-side `FAssetEditorCommands` from AudioInsights — you do not need to define your own commands.
- The `SelectInfo != ESelectInfo::Type::Direct` gate prevents recursion when the Details dashboard pushes selection back at you.
- The helper handles its own `nullptr` asset case — it returns `SNullWidget::NullWidget` from `ContructContextMenuOptions` if no row is selected, which suppresses the menu entirely.
- API typo warning: the helper method is `ContructContextMenuOptions` (missing an `s`). That's how it's spelled in the engine; don't "fix" it on the call site.

---

## Recipe 12 — Drive the Cache Timeline from Row Selection

**Opt-in.** Add only if the developer confirmed during planning that rows carry meaningful timestamps (see [Optional Capabilities](#optional-capabilities--offer-these-to-the-developer-during-planning)). Pattern lifted from `AudioEventLogDashboardViewFactory.cpp`.

**Step 1 — `ObjectDashboardViewFactory.h`:** add overrides and the timing-view delegate handlers.

```cpp
// inside FObjectDashboardViewFactory:
public:
	virtual TSharedRef<SWidget> MakeWidget(TSharedRef<SDockTab> OwnerTab, const FSpawnTabArgs& SpawnTabArgs) override;

protected:
	virtual void OnSelectionChanged(TSharedPtr<IDashboardDataViewEntry> SelectedItem, ESelectInfo::Type SelectInfo) override;

private:
	void OnTimingViewTimeMarkerChanged(double InTimeMarker);
	void OnTimeControlMethodReset();
```

If you also implemented Recipe 11, fold these into the same `OnSelectionChanged` override rather than duplicating it.

**Step 2 — `ObjectDashboardViewFactory.cpp`:** bind in `MakeWidget`, drive the time marker from selection.

```cpp
#include "AudioInsightsTimingViewExtender.h"

TSharedRef<SWidget> FObjectDashboardViewFactory::MakeWidget(TSharedRef<SDockTab> OwnerTab, const FSpawnTabArgs& SpawnTabArgs)
{
	FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();
	AudioInsightsModule.GetTimingViewExtender().OnTimingViewTimeMarkerChanged.AddSP(this, &FObjectDashboardViewFactory::OnTimingViewTimeMarkerChanged);
	AudioInsightsModule.GetTimingViewExtender().OnTimeControlMethodReset.AddSP(this, &FObjectDashboardViewFactory::OnTimeControlMethodReset);

	return FTraceTableDashboardViewFactory::MakeWidget(OwnerTab, SpawnTabArgs);
}

void FObjectDashboardViewFactory::OnSelectionChanged(TSharedPtr<IDashboardDataViewEntry> SelectedItem, ESelectInfo::Type SelectInfo)
{
	FTraceObjectTableDashboardViewFactory::OnSelectionChanged(SelectedItem, SelectInfo);

	// Only react to user-driven selection changes — Direct fires programmatically when the timing view pushes back at us.
	if (SelectInfo == ESelectInfo::Type::Direct)
	{
		return;
	}

	FAudioInsightsTimingViewExtender& TimingView = IAudioInsightsModule::GetChecked().GetTimingViewExtender();

	if (SelectedItem.IsValid())
	{
		const FObjectDashboardEntry& Entry = CastEntry<FObjectDashboardEntry>(*SelectedItem);
		TimingView.PauseTimeMarker(Entry.Timestamp, ESystemControllingTimeMarker::External);
	}
	else
	{
		TimingView.ResumeTimeMarker();
	}
}

void FObjectDashboardViewFactory::OnTimingViewTimeMarkerChanged(double InTimeMarker)
{
	// Highlight the row whose timestamp is at or just before InTimeMarker.
	// Pattern: walk DataViewEntries in timestamp order and pick the last entry with Timestamp <= InTimeMarker.
	// See AudioEventLogDashboardViewFactory::OnTimingViewTimeMarkerChanged for a full reference implementation.

	if (!FilteredEntriesListView.IsValid() || DataViewEntries.IsEmpty())
	{
		return;
	}

	TSharedPtr<IDashboardDataViewEntry> BestMatch;
	for (const TSharedPtr<IDashboardDataViewEntry>& Entry : DataViewEntries)
	{
		if (!Entry.IsValid())
		{
			continue;
		}

		const double EntryTimestamp = CastEntry<FObjectDashboardEntry>(*Entry).Timestamp;
		if (EntryTimestamp <= InTimeMarker)
		{
			BestMatch = Entry;
		}
		else
		{
			break;
		}
	}

	if (BestMatch.IsValid())
	{
		FilteredEntriesListView->SetSelection(BestMatch, ESelectInfo::Type::Direct);
	}
	else
	{
		FilteredEntriesListView->ClearSelection();
	}
}

void FObjectDashboardViewFactory::OnTimeControlMethodReset()
{
	if (FilteredEntriesListView.IsValid())
	{
		FilteredEntriesListView->ClearSelection();
	}
}
```

Notes:
- Use `ESystemControllingTimeMarker::External` for plugin-authored dashboards. The other enum values (`EventLog`, `PlotsWidget`, `SignalFlow`) are reserved for the built-in tabs.
- Always pass `ESelectInfo::Type::Direct` when programmatically setting selection from `OnTimingViewTimeMarkerChanged` — otherwise the bidirectional sync loops.
- If your dashboard needs the auto-scroll / focus-on-update behavior the Event Log has, copy the `FocusedItem` / `bAutoScroll` machinery from `AudioEventLogDashboardViewFactory.cpp`. The recipe above is the minimal pause-on-select / highlight-on-scrub version.

---

# Base Class API Quick Reference

## `TDeviceDataMapTraceProvider<KeyType, EntryType>`

Core data structure: `TMap<Audio::FDeviceId, TSortedMap<KeyType, EntryType>>` (nested per-device map).

| Method | Purpose |
|--------|---------|
| `UpdateDeviceEntry(DeviceId, Key, Lambda)` | Find-or-add entry, mutate via lambda |
| `FindDeviceEntry(DeviceId, Key)` | Returns `EntryType*` or `nullptr` |
| `RemoveDeviceEntry(DeviceId, Key)` | Remove entry, cleans up empty device maps |
| `FindFilteredDeviceData()` | Get the current audio device's data map |
| `ProcessMessageQueue<T>(Queue, GetEntryFunc, ProcessFunc)` | Drain a queue and process each message |
| `ConstructAnalyzer(Session)` | **Override** — create your trace analyzer |
| `ProcessMessages()` | **Override** — drain queues and update entries |
| `OnTimingViewTimeMarkerChanged(Time)` | **Override** — reconstruct state at a given time |

## `FTraceAnalyzerBase` (inner class)

| Method | Purpose |
|--------|---------|
| `OnAnalysisBegin(Context)` | **Override** — register `RouteEvent` calls |
| `OnHandleEvent(RouteId, Style, Context)` | **Override** — dispatch events to message queues |
| `CacheMessage<T>(Context, Queue)` | Cache message + enqueue for processing |
| `ShouldProcessNewEvents()` | Check if processing is enabled (respects pause/stop) |

## `FTraceTableDashboardViewFactory`

| Method | Purpose |
|--------|---------|
| `GetName()` | **Override** — unique factory name |
| `GetDisplayName()` | **Override** — tab display text |
| `GetIcon()` | **Override** — tab icon |
| `GetDefaultTabStack()` | **Override** — which tab group |
| `GetColumns()` | **Override** — define table columns |
| `SortTable()` | **Override** — implement sort logic |
| `ProcessEntries(Reason)` | **Override** — implement search/filter |
| `FilterEntries<ProviderT>(Predicate)` | Helper — filter entries from provider data |
| `SortByPredicate<EntryT>(Predicate)` | Helper — sort entries with a comparator |
| `GetSearchFilterText()` | Get current search box text |
| `MakeWidget(OwnerTab, SpawnTabArgs)` | Override for custom widget layout |

## `IAudioCachedMessage`

| Member | Purpose |
|--------|---------|
| `double Timestamp` | Event timestamp (trace time domain) |
| `GetID()` → `uint64` | Unique ID for dedup/lookup |
| `GetMessageName()` → `FName` | Message type identifier (used for cache iteration) |
| `GetSizeOf()` → `uint32` | Memory tracking |
| `GetCacheWriteHandler()` → `FCacheWriteHandler` | Snapshot export (return `{}` to skip) |

## `FCacheWriteHandler`

A pair of lambdas for trace snapshot export:
```
{
	DeclareEvent: (Writer) → EventId       // Declare schema (field names + types)
	WriteEvent:   (Writer, EventId, Msg, TimestampCycles) → void  // Write one message
}
```

Available field types: `Uint8`, `Uint16`, `Uint32`, `Uint64`, `Int8`, `Int16`, `Int32`, `Int64`, `Float32`, `Float64`, `Bool8`, `WideString`, `AnsiString`

## `FAudioInsightsCacheManager` (for timeline scrubbing)

| Method | Purpose |
|--------|---------|
| `IterateTo<T>(MessageName, TimeMarker, Callback)` | Replay all messages of type T up to a timestamp |
| `FindClosestMessage<T>(MessageName, TimeMarker, ID)` | Find nearest message for a given entry ID |

---

# Trace Event Field Types

When deserializing in message constructors, use these `EventData` methods:

| Trace Field Type | C++ Read Method |
|------------------|-----------------|
| `uint8` / `uint16` / `uint32` / `uint64` | `EventData.GetValue<T>("FieldName")` |
| `int8` / `int16` / `int32` / `int64` | `EventData.GetValue<T>("FieldName")` |
| `float` | `EventData.GetValue<float>("FieldName")` |
| `double` | `EventData.GetValue<double>("FieldName")` |
| `bool` | `EventData.GetValue<bool>("FieldName")` |
| `WideString` | `EventData.GetString("FieldName", OutFString)` |
| `Timestamp` (uint64 cycles) | `InContext.EventTime.AsSeconds(EventData.GetValue<uint64>("Timestamp"))` |

---

# Example Prompts

These prompts are designed to be given to an AI assistant with this guide loaded in context and your plugin files open.

### Getting Started

> "I just scaffolded a new Audio Insights plugin called `ReverbInsights` using the standalone guide. Rename everything from the 'Object' domain to 'Reverb' — update all struct names, file names, namespaces, display names, and LOCTEXT keys."

### Changing Message Types

> "Remove the Value message type entirely. Instead, add two new message types: `ReverbParametersMessage` with fields `float WetLevel`, `float DecayTime`, `float RoomSize`, and `ReverbPresetChangedMessage` with a `FString PresetName` field. Update the provider to route and process both. Add matching columns to the dashboard."

### Customizing the Dashboard

> "I want the dashboard to show these columns: Name (text, 0.5 width), Wet Level (number, 0.15), Decay Time (number, 0.15), Room Size (number, 0.15), and Preset (text, 0.15, hidden by default). All numeric columns should be sortable. The search bar should filter by name and preset."

### Adding Icons

> "Add an activity indicator icon column as the first column (0.05 width). Show a green dot icon when the reverb is active (has received a parameters update in the last 2 seconds) and no icon when inactive."

### Timeline Scrubbing

> "Update `OnTimingViewTimeMarkerChanged` to reconstruct the dashboard state at a given time. Replay `ReverbParametersMessage` to show the closest known parameter values, and `ReverbPresetChangedMessage` to show the active preset. Entries that were destroyed before the time marker should not appear."

### Cache Snapshot Export

> "Add cache write handlers for all three message types so they're included when saving an Audio Insights cache snapshot. Match the trace event schema from the send side."

### Advanced: Adding a Context Menu

> "Add a right-click context menu on dashboard rows with two options: 'Copy Reverb Name' (copies the name to clipboard) and 'Browse to Asset' (selects the asset in the Content Browser if it's an asset-based reverb)."

---

# Audio Insights Framework Paths (Engine)

For reference when reading the framework API. You compile against these headers but should not need to modify them:

```
Engine/Plugins/AudioInsights/Source/AudioInsights/
├── Public/
│   ├── AudioInsightsTraceProviderBase.h    ← TDeviceDataMapTraceProvider, FTraceAnalyzerBase
│   ├── AudioInsightsConstants.h            ← TimestampFieldName and other shared names
│   ├── IAudioInsightsModule.h              ← Module accessor, trace module, cache manager
│   ├── Cache/
│   │   ├── IAudioCachedMessage.h           ← Message interface
│   │   └── AudioInsightsCacheManager.h     ← Cache iteration/lookup API
│   ├── Messages/
│   │   └── AnalyzerMessageQueue.h          ← TAnalyzerMessageQueue
│   ├── TraceWriter/
│   │   └── CacheWriteHandler.h             ← FCacheWriteHandler struct
│   └── Views/
│       ├── DashboardViewFactory.h          ← EDefaultDashboardTabStack enum
│       └── TableDashboardViewFactory.h     ← FColumnData, FTraceTableDashboardViewFactory
└── ...

Engine/Plugins/AudioInsights/Source/AudioInsightsEditor/
└── Public/
	└── IAudioInsightsEditorModule.h        ← Editor-only registration accessor
```
