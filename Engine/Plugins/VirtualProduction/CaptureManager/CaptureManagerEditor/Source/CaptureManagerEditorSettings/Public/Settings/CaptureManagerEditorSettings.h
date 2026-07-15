// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "Engine/DeveloperSettings.h"
#include "Engine/TimerHandle.h"

#include "CaptureManagerEditorSettings.generated.h"

#define UE_API CAPTUREMANAGEREDITORSETTINGS_API

class UCaptureManagerIngestNamingTokens;
class UCaptureManagerVideoNamingTokens;
class UCaptureManagerAudioNamingTokens;
class UCaptureManagerCalibrationNamingTokens;
class UCaptureManagerLensFileNamingTokens;
class UCaptureManagerVideoEncoderTokens;
class UCaptureManagerAudioEncoderTokens;

UCLASS(MinimalAPI, config = EditorPerProjectUserSettings, meta = (DisplayName = "Capture Manager"), defaultconfig)
class UCaptureManagerEditorSettings : public UDeveloperSettings
{
public:
	GENERATED_BODY()

	UE_API void Initialize();

	/** Get the import naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerIngestNamingTokens> GetGeneralNamingTokens() const;

	/** Get the video naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerVideoNamingTokens> GetVideoNamingTokens() const;

	/** Get the audio naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerAudioNamingTokens> GetAudioNamingTokens() const;

	/** Get the calibration naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerCalibrationNamingTokens> GetCalibrationNamingTokens() const;

	/** Get the calibration lens file naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerLensFileNamingTokens> GetLensFileNamingTokens() const;

	/** Get the video encoder naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerVideoEncoderTokens> GetVideoEncoderNamingTokens() const;

	/** Get the audio encoder naming tokens for Capture Manager Editor. */
	UE_API TObjectPtr<const UCaptureManagerAudioEncoderTokens> GetAudioEncoderNamingTokens() const;

	/**
	 * Get the Capture Manager Editor settings uobject
	 * @return Settings Object.
	 */
	UFUNCTION(BlueprintPure, Category = "Capture Manager|Settings")
	static UE_API UCaptureManagerEditorSettings* GetCaptureManagerEditorSettings();

#if WITH_EDITOR

	/** Set the Media Directory in Capture Manager Editor Settings */
	UFUNCTION(BlueprintCallable, Category = "Capture Manager|Settings", meta = (DisplayName = "Set Media Directory"))
	UE_API void SetMediaDirectory(const FDirectoryPath& InMediaDirectory);

	/** Set the Import Directory in Capture Manager Editor Settings.
	* Must be a valid UE package path not in a read-only content root
	* (e.g. /Engine, /Script, /Temp). Plugin content roots and UEFN
	* roots such as /VerseDevices are accepted. */
	UFUNCTION(BlueprintCallable, Category = "Capture Manager|Settings", meta = (DisplayName = "Set Import Directory",
		ToolTip = "Sets the Content Browser location where imported assets will be created. Must be within the project's Content folder (e.g. /Game/CaptureManager/Imports)."))
	UE_API void SetImportDirectory(const FDirectoryPath& InImportDirectory);

#endif

	/** Location to store ingested media data. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Import")
	FDirectoryPath MediaDirectory;

	/** Content Browser location where assets will be created. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Import",
		meta = (ContentDir, RelativeToGameContentDir, 
			ToolTip = "Content Browser location where assets will be created. Must point to a directory within the project's Content folder (e.g. /Game/CaptureManager/Imports)."))
	FDirectoryPath ImportDirectory;

	/** Option to automatically save the assets after the ingest process. */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	bool bAutoSaveAssets = true;

	/** Name for created Capture Data assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	FString CaptureDataAssetName;

	/** Name for created Image Media Source video assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Video")
	FString ImageSequenceAssetName;

	/** Name for created Image Media Source depth assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Video")
	FString DepthSequenceAssetName;

	/** Tokens compatible with video properties. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Video")
	FText VideoTokens;

	/** Name for created Soundwave assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Audio")
	FString SoundwaveAssetName;

	/** Tokens compatible with audio properties. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Audio")
	FText AudioTokens;

	/** Name for created Camera Calibration assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Calibration")
	FString CalibrationAssetName;

	/** Tokens compatible with calibration properties. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Calibration")
	FText CalibrationTokens;

	/** Name for created Lens File assets. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Calibration")
	FString LensFileAssetName;

	/** Tokens compatible with calibration properties. */
	UPROPERTY(config, EditAnywhere, Category = "Import|Calibration")
	FText LensFileTokens;

	/** Maximum number of ingest jobs allowed to run concurrently from Blueprint/script.
	 *  Higher values increase throughput on fast storage but raise CPU and disk I/O pressure.
	 *  If using a third-party encoder, each slot may spawn a separate encoder process. */
	UPROPERTY(config, EditAnywhere, Category = "Conversion",
		meta = (DisplayName = "Max Concurrent Ingests", UIMin = 1, ClampMin = 1, UIMax = 8, ClampMax = 8))
	int32 MaxConcurrentIngests = 2;

	/** Option to enable a third party encoder instead of the engine media readers and writers. */
	UPROPERTY(config, EditAnywhere, Category = "Conversion", meta = (DisplayName = "Use Third Party Encoder"))
	bool bEnableThirdPartyEncoder = false;

	/** Location to the third party encoder executable. */
	UPROPERTY(config, EditAnywhere, Category = "Conversion",
		meta = (DisplayName = "Third Party Encoder Path", EditCondition = "bEnableThirdPartyEncoder", EditConditionHides))
	FFilePath ThirdPartyEncoder;

	/** Custom video command arguments for the third party encoder. Leave empty to use the default arguments. */
	UPROPERTY(config, EditAnywhere, Category = "Conversion",
		meta = (EditCondition = "bEnableThirdPartyEncoder", EditConditionHides))
	FString CustomVideoCommandArguments;

	/** Custom audio command arguments for the third party encoder. Leave empty to use the default arguments. */
	UPROPERTY(config, EditAnywhere, Category = "Conversion",
		meta = (EditCondition = "bEnableThirdPartyEncoder", EditConditionHides))
	FString CustomAudioCommandArguments;

	/** Tokens compatible with video command arguments. */
	UPROPERTY(VisibleAnywhere, Category = "Conversion",
		meta = (EditCondition = "bEnableThirdPartyEncoder", EditConditionHides))
	FText VideoCommandTokens;

	/** Tokens compatible with audio command arguments. */
	UPROPERTY(VisibleAnywhere, Category = "Conversion",
		meta = (EditCondition = "bEnableThirdPartyEncoder", EditConditionHides))
	FText AudioCommandTokens;

	/** Option to launch the Ingest Server when a Live Link Hub connection is made. */
	UPROPERTY(config, EditAnywhere, Category = "Ingest Server")
	bool bLaunchIngestServerOnLiveLinkHubConnection = true;

	/** Option to choose a listening port for the Ingest Server. Leave 0 for automatic selection of the port. */
	UPROPERTY(config, EditAnywhere, Category = "Ingest Server")
	uint16 IngestServerPort = 0;

	/** Tokens compatible with import properties */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	FText ImportTokens;

	/** Global tokens. */
	UPROPERTY(VisibleAnywhere, Category = "Templates")
	FText GlobalTokens;

	/** Returns verified import directory. Avoid accessing Import Directory property directly. */
	UE_API FString GetVerifiedImportDirectory();

#if WITH_EDITOR

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCaptureManagerEditorSettingsChanged);

	/** Multicast delegate called whenever Capture Manager Editor settings are modified. */
	UPROPERTY(BlueprintAssignable, Category = "Capture Manager|Settings")
	FOnCaptureManagerEditorSettingsChanged OnCaptureManagerEditorSettingsChanged;
#endif

private:

	UE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;

	UE_API FString GetBaseImportDirectory() const;
	UE_API void ResetImportDirectory();
	UE_API void InitializeValuesIfNotSet();

	/** Handler used to update the connection state and source id when a connection with a hub instance is established. */
	UE_API void OnHubConnectionEstablished(FGuid SourceId);

	/** Check whether the hub connection is still active. */
	UE_API void CheckHubConnection();

	/** Start the ingest server */
	UE_API bool StartIngestServer();

	/**
	* Naming tokens for Capture Manager Editor, instantiated each load based on the naming tokens class.
	* This isn't serialized to the config file, and exists here for singleton-like access.
	*/
	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerIngestNamingTokens> GeneralNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerVideoNamingTokens> VideoNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerAudioNamingTokens> AudioNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerCalibrationNamingTokens> CalibrationNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerLensFileNamingTokens> LensFileNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerVideoEncoderTokens> VideoEncoderNamingTokens;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UCaptureManagerAudioEncoderTokens> AudioEncoderNamingTokens;

	class ILiveLinkHubMessagingModule* HubMessagingModule;

	/** LiveLink client used to retrieve the status of the hub connection. */
	class ILiveLinkClient* LiveLinkClient = nullptr;

	/** Cached list of detected LLH instance ids. */
	TArray<FGuid> DetectedHubsArray;

	/** Handle to the timer responsible for triggering CheckHubConnection. */
	FTimerHandle TimerHandle;

	/** Interval of the timer to check for connection validity. */
	static constexpr float CheckConnectionIntervalSeconds = 1.0f;

	/** Cached base of the import directory. The base directory differs when used in UE or UEFN */
	FString CachedBaseImportDirectory;

	/** Flag when properties are being set programmatically */
	bool bInProgrammaticSetter = false;
};

#undef UE_API
