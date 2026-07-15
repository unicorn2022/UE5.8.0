// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterAnalytics.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterPipeline.h"
#include "MetaHumanCharacterAssetEditor.h"
#include "MetaHumanCharacterEditorMode.h"
#include "Cloud/MetaHumanARServiceRequest.h"

#include "Editor.h"
#include "EngineAnalytics.h"
#include "Logging/LogMacros.h"
#include "Logging/StructuredLog.h"
#include "GeneralProjectSettings.h"
#include "ToolkitBuilder.h"

#include "Subsystems/AssetEditorSubsystem.h"
#include "Tools/UAssetEditor.h"

namespace UE::MetaHuman::Analytics
{
	namespace Private
	{
		const FString EventNamePrefix = TEXT("Editor.MetaHumanCharacter.");

		FString AnonymizeString(const FString& String)
		{
			FSHA1 Sha1;
			Sha1.UpdateWithString(*String, String.Len());
			const FSHAHash HashedName = Sha1.Finalize();
			return HashedName.ToString();
		}

		FString AnonymizeName(const FName& Name)
		{
			return AnonymizeString(Name.ToString());
		}

		FGuid TryGetSessionIdForCharacter(const UMetaHumanCharacter* InMetaHumanCharacter)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (IAssetEditorInstance* AssetEditorInstance = AssetEditorSubsystem->FindEditorForAsset(const_cast<UObject*>(Cast<const UObject>(InMetaHumanCharacter)), false))
			{
				if (UAssetEditor* AssetEditor = static_cast<UAssetEditor*>(AssetEditorInstance))
				{
					UMetaHumanCharacterAssetEditor* MetaHumanAssetEditor = static_cast<UMetaHumanCharacterAssetEditor*>(AssetEditor);
					{
						return MetaHumanAssetEditor->GetSessionGuid();
					}
				}
			}
			return {};
		}

		void SetSessionIdForCharacter(TArray<FAnalyticsEventAttribute>& EventAttributes, const UMetaHumanCharacter* InMetaHumanCharacter)
		{
			//NOTE: we will add the session ID even if it can't be found as 0000.... for now
			FString SessionId = TryGetSessionIdForCharacter(InMetaHumanCharacter).ToString();
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SessionId"), SessionId));
		}

		void StartRecordEvent(TArray<FAnalyticsEventAttribute>& EventAttributes, const UMetaHumanCharacter* InMetaHumanCharacter)
		{
			FPrimaryAssetId PrimaryAssetId(*InMetaHumanCharacter->GetPathName(), InMetaHumanCharacter->GetFName());
			const FString PrimaryAssetIdStr = PrimaryAssetId.PrimaryAssetType.GetName().ToString() / PrimaryAssetId.PrimaryAssetName.ToString();
			const UGeneralProjectSettings* GeneralProjectSettings = GetDefault<UGeneralProjectSettings>();
			const FString ProjectInfoStr = GeneralProjectSettings->ProjectID.ToString();
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CharacterId"), AnonymizeString(ProjectInfoStr / PrimaryAssetIdStr)));
		}

		void FinishRecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& EventAttributes)
		{
			// Soft early-return guards a shutdown race: EVENT_BEGIN_BODY checks IsAvailable() but FEngineAnalytics::Shutdown() can fire between the macro's gate and here.
			if (!FEngineAnalytics::IsAvailable())
			{
				return;
			}
			const FString FullEventName = EventNamePrefix + EventName;
			FEngineAnalytics::GetProvider().RecordEvent(FullEventName, EventAttributes);
		}

		void RecordBodyTypeInformation(TArray<FAnalyticsEventAttribute>& EventAttributes, const UMetaHumanCharacter* InMetaHumanCharacter)
		{
			if (UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterEditorSubsystem = UMetaHumanCharacterEditorSubsystem::Get())
			{
				TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterEditorSubsystem->GetBodyState(InMetaHumanCharacter);
				EMetaHumanBodyType BodyType = BodyState->GetMetaHumanBodyType();
				if (BodyType == EMetaHumanBodyType::BlendableBody)
				{
					EventAttributes.Add(FAnalyticsEventAttribute(TEXT("BlendableBody"), true));
				}
				else
				{
					EventAttributes.Add(FAnalyticsEventAttribute(TEXT("LegacyBodyType"), static_cast<int32>(BodyType)));
				}
			}
		}

		/**
		 * Optional overrides for tool-name suffixes. Keyed on the RAW command identifier (e.g. "BeginPresetsTool"). Starts empty
		 */
		const TMap<FString, FString>& GetToolNameOverrides()
		{
			static const TMap<FString, FString> Overrides = {
				// Example placeholder, leave empty unless a tool's stripped name reads poorly on the wire.
				// { TEXT("BeginSomeTool"), TEXT("FriendlyName") },
			};
			return Overrides;
		}


		FString DeriveToolEventSuffix(const FString& ToolNameString)
		{
			if (ToolNameString.IsEmpty())
			{
				return TEXT("Unknown");
			}

			if (const FString* Override = GetToolNameOverrides().Find(ToolNameString))
			{
				return *Override;
			}

			FString Result = ToolNameString;

			if (Result.StartsWith(TEXT("Begin"), ESearchCase::CaseSensitive))
			{
				Result.RightChopInline(5, EAllowShrinking::No);
			}

			if (Result.EndsWith(TEXT("Tools"), ESearchCase::CaseSensitive))
			{
				Result.LeftChopInline(5, EAllowShrinking::No);
			}
			else if (Result.EndsWith(TEXT("Tool"), ESearchCase::CaseSensitive))
			{
				Result.LeftChopInline(4, EAllowShrinking::No);
			}

			if (Result.IsEmpty())
			{
				return TEXT("Unknown");
			}

			for (const TCHAR C : Result)
			{
				if (!FChar::IsAlnum(C) && C != TEXT('_'))
				{
					return TEXT("Unknown");
				}
			}

			return Result;
		}
	}

#define NO_ANALYTICS_CIRCUIT_BREAK()\
if (!FEngineAnalytics::IsAvailable()) return

#define EVENT_BEGIN_BODY(EventName)\
	NO_ANALYTICS_CIRCUIT_BREAK();\
	const FString EventNameStr = TEXT(#EventName);\
	TArray<FAnalyticsEventAttribute> EventAttributes;\
	StartRecordEvent(EventAttributes, InMetaHumanCharacter);\
	SetSessionIdForCharacter(EventAttributes, InMetaHumanCharacter)

#define EVENT_END_BODY()\
	FinishRecordEvent(EventNameStr, EventAttributes)

#define BEGIN_RECORD_EVENT(EventName,FuncName,...)\
void Record##FuncName##Event(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, __VA_ARGS__)\
{\
	EVENT_BEGIN_BODY(EventName);

#define END_RECORD_EVENT()\
	EVENT_END_BODY();\
}

#define DEFINE_RECORD_EVENT(EventName,FuncName)\
void Record##FuncName##Event(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter)\
{\
	EVENT_BEGIN_BODY(EventName);\
	EVENT_END_BODY();\
}

	// Event implementations ==============================================================================================================================
	using namespace Private;

	DEFINE_RECORD_EVENT(New, NewCharacter);
	DEFINE_RECORD_EVENT(OpenEditor, OpenCharacterEditor);
	DEFINE_RECORD_EVENT(CloseEditor, CloseCharacterEditor);

	BEGIN_RECORD_EVENT(Build, BuildPipelineCharacter, const TSubclassOf<UMetaHumanCharacterPipeline> InMaybePipeline)
	{
		if (InMaybePipeline != nullptr)
		{
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("PipelineID"), AnonymizeString(InMaybePipeline->GetPathName())));
		}
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("HasSynthesisedTextures"), InMetaHumanCharacter->HasSynthesizedTextures()));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("HasHighResolutionTextures"), InMetaHumanCharacter->HasHighResolutionTextures()));

		RecordBodyTypeInformation(EventAttributes, InMetaHumanCharacter);
	}
	END_RECORD_EVENT();

	BEGIN_RECORD_EVENT(Autorig, RequestAutorig, UE::MetaHuman::ERigType RigType)
	{
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("RigType"), static_cast<int32>(RigType)));
		RecordBodyTypeInformation(EventAttributes, InMetaHumanCharacter);		
	}
	END_RECORD_EVENT();

	BEGIN_RECORD_EVENT(HighResolutionTextures, RequestHighResolutionTextures, ERequestTextureResolution RequestTextureResolution)
	{
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Resolution"), static_cast<int32>(RequestTextureResolution)));
	}
	END_RECORD_EVENT();

	DEFINE_RECORD_EVENT(SaveFaceDNA, SaveFaceDNA);
	DEFINE_RECORD_EVENT(SaveBodyDNA, SaveBodyDNA);
	DEFINE_RECORD_EVENT(SaveHighResolutionTextures, SaveHighResolutionTextures);
	DEFINE_RECORD_EVENT(ImportFaceDNA, ImportFaceDNA);
	DEFINE_RECORD_EVENT(ImportBodyDNA, ImportBodyDNA);
	DEFINE_RECORD_EVENT(CreateMeshFromDNA, CreateMeshFromDNA);
	DEFINE_RECORD_EVENT(RemoveFaceRig, RemoveFaceRig);
	
	BEGIN_RECORD_EVENT(WardrobeItemWorn, WardrobeItemWorn, const FName& SlotName, const FName& AssetName)
	{
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("AssetName"), AnonymizeName(AssetName)));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SlotName"), SlotName.ToString())); //< this doesn't need to be anonymized since it's something *we* have defined
	}
	END_RECORD_EVENT();

	BEGIN_RECORD_EVENT(WardrobeItemPrepared, WardrobeItemPrepared, const FName& SlotName, const FName& AssetName)
	{
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("AssetName"), AnonymizeName(AssetName)));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SlotName"), SlotName.ToString())); //< this doesn't need to be anonymized since it's something *we* have defined
	}
	END_RECORD_EVENT();

	
	void RecordOnToolStartEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FString& ToolNameString, const FString& ActivePaletteNameString)
	{
		NO_ANALYTICS_CIRCUIT_BREAK();

		TArray<FAnalyticsEventAttribute> EventAttributes;
		StartRecordEvent(EventAttributes, InMetaHumanCharacter);
		SetSessionIdForCharacter(EventAttributes, InMetaHumanCharacter);

		if (ActivePaletteNameString.Contains(TEXT("Head")))
		{
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Type"), TEXT("Face")));
		}
		else if (ActivePaletteNameString.Contains(TEXT("Body")))
		{
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Type"), TEXT("Body")));
		}

		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ToolNameVersion"), TEXT("2")));

		const FString ToolEventId = Private::DeriveToolEventSuffix(ToolNameString);
		FinishRecordEvent(TEXT("ToolActivate.") + ToolEventId, EventAttributes);
	}

	void RecordOnToolStartEvent(UMetaHumanCharacterEditorMode* MetaHumanCharacterEditorMode, FToolkitBuilder* ToolkitBuilder, const FString& ToolName)
	{
		if (MetaHumanCharacterEditorMode && ToolkitBuilder)
		{
			if (TObjectPtr<UMetaHumanCharacter> Character = MetaHumanCharacterEditorMode->GetCharacter())
			{
				UE::MetaHuman::Analytics::RecordOnToolStartEvent(Character, ToolName, ToolkitBuilder->GetActivePaletteName().ToString());
			}
		}
	}

	namespace Private
	{
		enum class EConformSource
		{
			DNA,
			Template,
			Identity,
		};

		static const TCHAR* OperationToString(EConformOperation Operation)
		{
			switch (Operation)
			{
				case EConformOperation::Conform:        return TEXT("Conform");
				case EConformOperation::ImportMesh:     return TEXT("ImportMesh");
				case EConformOperation::ImportWholeRig: return TEXT("ImportWholeRig");
				default:                                return TEXT("Unknown");
			}
		}

		static const TCHAR* PartsToString(EConformParts Parts)
		{
			switch (Parts)
			{
				case EConformParts::Head:        return TEXT("Head");
				case EConformParts::Body:        return TEXT("Body");
				case EConformParts::HeadAndBody: return TEXT("HeadAndBody");
				default:                         return TEXT("Unknown");
			}
		}

		static const TCHAR* SourceToString(EConformSource Source)
		{
			switch (Source)
			{
				case EConformSource::DNA:      return TEXT("DNA");
				case EConformSource::Identity: return TEXT("Identity");
				case EConformSource::Template: return TEXT("Template");
				default:                       return TEXT("Unknown");
			}
		}

		void RecordConformEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, EConformSource Source, const FConformEventExtras& InExtras)
		{
			EVENT_BEGIN_BODY(ToolActivate.Conform);

			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Source"),    SourceToString(Source)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Operation"), OperationToString(InExtras.Operation)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Parts"),     PartsToString(InExtras.Parts)));

			// Per-part outcomes — emit iff the caller marked them set (defensive default).
			if (InExtras.bHeadSuccess.IsSet())
			{
				EventAttributes.Add(FAnalyticsEventAttribute(TEXT("bHeadSuccess"), InExtras.bHeadSuccess.GetValue()));
			}
			if (InExtras.bBodySuccess.IsSet())
			{
				EventAttributes.Add(FAnalyticsEventAttribute(TEXT("bBodySuccess"), InExtras.bBodySuccess.GetValue()));
			}

			// Template-only extras
			if (InExtras.bImportEyes.IsSet())
			{
				EventAttributes.Add(FAnalyticsEventAttribute(TEXT("bImportEyes"), InExtras.bImportEyes.GetValue()));
			}
			if (InExtras.bImportTeeth.IsSet())
			{
				EventAttributes.Add(FAnalyticsEventAttribute(TEXT("bImportTeeth"), InExtras.bImportTeeth.GetValue()));
			}

			for (const FAnalyticsEventAttribute& Attr : InExtras.ExtraAttributes)
			{
				EventAttributes.Add(Attr);
			}

			EVENT_END_BODY();
		}
	}

	void RecordConformFromDnaEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FConformEventExtras& InExtras)
	{
		RecordConformEvent(InMetaHumanCharacter, EConformSource::DNA, InExtras);
	}

	void RecordConformFromIdentityEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FConformEventExtras& InExtras)
	{
		RecordConformEvent(InMetaHumanCharacter, EConformSource::Identity, InExtras);
	}

	void RecordConformFromTemplateEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FConformEventExtras& InExtras)
	{
		RecordConformEvent(InMetaHumanCharacter, EConformSource::Template, InExtras);
	}

	void RecordImportJointsEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FImportJointsEventExtras& InExtras)
	{
		EVENT_BEGIN_BODY(ToolActivate.ImportJoints);
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Source"), InExtras.Source));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("bSuccess"), InExtras.bSuccess));
		EVENT_END_BODY();
	}

	void RecordMeshConformEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FMeshConformEventExtras& InExtras)
	{
		EVENT_BEGIN_BODY(ToolActivate.MeshConform);
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Step"), InExtras.Step));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("TargetPartsType"), InExtras.TargetPartsType));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("bSuccess"), InExtras.bSuccess));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("bHasKeyPoints"), InExtras.bHasKeyPoints));
		EVENT_END_BODY();
	}
}

