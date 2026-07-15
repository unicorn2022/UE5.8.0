// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMediaSourceWidget.h"
#include "MetaHumanStringCombo.h"
#include "MetaHumanPipelineMediaPlayerNode.h"
#include "MetaHumanLocalLiveLinkSourceBlueprint.h"

#include "MetaHumanLocalLiveLinkSourceSettings.h"

#include "Misc/ConfigCacheIni.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Input/SNumericEntryBox.h"

#if WITH_EDITOR
#include "SEnumCombo.h"
#include "DetailLayoutBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "MediaSource.h"
#include "MediaBundle.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#endif

#define LOCTEXT_NAMESPACE "MetaHumanLocalLiveLinkSource"



class SMetaHumanMediaSourceWidgetImpl : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMetaHumanMediaSourceWidgetImpl) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, SMetaHumanMediaSourceWidget::EMediaType InMediaType);

	void OnAssetsAddedOrDeleted(TConstArrayView<FAssetData> InAssets);
	void OnAssetRenamed(const FAssetData& InAsset, const FString& InOldObjectPath);
	void PopulateDevices();

	SMetaHumanMediaSourceWidget::EMediaType MediaType;

	TMap<FString, FMetaHumanLiveLinkVideoDevice> VideoDevices;
	TMap<FString, FMetaHumanLiveLinkVideoTrack> VideoTracks;
	TMap<FString, FMetaHumanLiveLinkVideoFormat> VideoTrackFormats;

	TArray<SMetaHumanStringCombo::FComboItemType> VideoDeviceItems;
	TArray<SMetaHumanStringCombo::FComboItemType> VideoTrackItems;
	TArray<SMetaHumanStringCombo::FComboItemType> VideoTrackFormatItems;
	bool bVideoTrackFormatItemsFiltered = true;

#if WITH_EDITOR
	TSharedPtr<SEnumComboBox> VideoDeviceType;
#endif
	TSharedPtr<SMetaHumanStringCombo> VideoDeviceCombo;
	TSharedPtr<SMetaHumanStringCombo> VideoTrackCombo;
	TSharedPtr<SMetaHumanStringCombo> VideoTrackFormatCombo;
#if WITH_EDITOR
	TSharedPtr<SWidgetSwitcher> VideoDeviceSwitcher;
	TSharedPtr<SObjectPropertyEntryBox> VideoDeviceMediaSourcePicker;
	TSharedPtr<SObjectPropertyEntryBox> VideoDeviceMediaBundlePicker;
	FString DeviceAssetPath;
#endif

	FString CachedVideoDeviceUrl;
	TArray<FMetaHumanLiveLinkVideoTrack> CachedVideoTracks;
	bool bVideoTracksCached = false;

	void OnVideoDeviceSelected(SMetaHumanStringCombo::FComboItemType InItem);
	void OnVideoTrackSelected(SMetaHumanStringCombo::FComboItemType InItem);
	void OnVideoTrackFormatSelected(SMetaHumanStringCombo::FComboItemType InItem);
#if WITH_EDITOR
	DECLARE_DELEGATE_OneParam(FOnDeviceSelectedDelegate, SMetaHumanStringCombo::FComboItemType);

	void OnMediaAssetSelected(const FAssetData& InAssetData, 
							  TRetainedRef<const FString> InPrefixUrl, 
							  FOnDeviceSelectedDelegate OnDeviceSelected);
	int32 GetDeviceSwitcherIndex() const;
#endif

	TMap<FString, FMetaHumanLiveLinkAudioDevice> AudioDevices;
	TMap<FString, FMetaHumanLiveLinkAudioTrack> AudioTracks;
	TMap<FString, FMetaHumanLiveLinkAudioFormat> AudioTrackFormats;

	TArray<SMetaHumanStringCombo::FComboItemType> AudioDeviceItems;
	TArray<SMetaHumanStringCombo::FComboItemType> AudioTrackItems;
	TArray<SMetaHumanStringCombo::FComboItemType> AudioTrackFormatItems;

#if WITH_EDITOR
	TSharedPtr<SEnumComboBox> AudioDeviceType;
#endif
	TSharedPtr<SMetaHumanStringCombo> AudioDeviceCombo;
	TSharedPtr<SMetaHumanStringCombo> AudioTrackCombo;
	TSharedPtr<SMetaHumanStringCombo> AudioTrackFormatCombo;
#if WITH_EDITOR
	TSharedPtr<SWidgetSwitcher> AudioDeviceSwitcher;
	TSharedPtr<SObjectPropertyEntryBox> AudioDeviceMediaSourcePicker;
	TSharedPtr<SObjectPropertyEntryBox> AudioDeviceMediaBundlePicker;
#endif

	void OnAudioDeviceSelected(SMetaHumanStringCombo::FComboItemType InItem);
	void OnAudioTrackSelected(SMetaHumanStringCombo::FComboItemType InItem);
	void OnAudioTrackFormatSelected(SMetaHumanStringCombo::FComboItemType InItem);

	int32 GetVideoCurrentDeviceTypeValue() const;
	void OnVideoDeviceTypeValueChanged(int32 InNewValue, ESelectInfo::Type);

	int32 GetAudioCurrentDeviceTypeValue() const;
	void OnAudioDeviceTypeValueChanged(int32 InNewValue, ESelectInfo::Type);

	TSharedPtr<SCheckBox> AdvancedCheckBox;

	TSharedPtr<SCheckBox> FilteredWidget;
	TSharedPtr<SNumericEntryBox<float>> StartTimeoutWidget;
	TSharedPtr<SNumericEntryBox<float>> FormatWaitTimeWidget;
	TSharedPtr<SNumericEntryBox<float>> SampleTimeoutWidget;

	bool CanCreate() const;

	double StartTimeout = 5;
	double FormatWaitTime = 0.1;
	double SampleTimeout = 5;

	EMetaHumanLocalLiveLinkSourceDeviceType CurrentVideoDeviceTypeValue = EMetaHumanLocalLiveLinkSourceDeviceType::CaptureDevice;
	EMetaHumanLocalLiveLinkSourceDeviceType CurrentAudioDeviceTypeValue = EMetaHumanLocalLiveLinkSourceDeviceType::CaptureDevice;

	bool IsBundle() const;
	bool IsMediaSource() const;
	bool IsMediaProfile() const;
	EVisibility GetTrackVisibility() const;
	bool IsTrackEnabled() const;
	FText GetTrackTooltip() const;
	EVisibility GetAdvancedVisibility() const;

	FMetaHumanMediaSourceCreateParams GetCreateParams() const;
};







void SMetaHumanMediaSourceWidgetImpl::Construct(const FArguments& InArgs, SMetaHumanMediaSourceWidget::EMediaType InMediaType)
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	AssetRegistryModule.Get().OnAssetsAdded().AddSP(this, &SMetaHumanMediaSourceWidgetImpl::OnAssetsAddedOrDeleted);
	AssetRegistryModule.Get().OnAssetsRemoved().AddSP(this, &SMetaHumanMediaSourceWidgetImpl::OnAssetsAddedOrDeleted);
	AssetRegistryModule.Get().OnAssetRenamed().AddSP(this, &SMetaHumanMediaSourceWidgetImpl::OnAssetRenamed);

	MediaType = InMediaType;

#if WITH_EDITOR
	VideoDeviceType = SNew(SEnumComboBox, StaticEnum<EMetaHumanLocalLiveLinkSourceDeviceType>())

		.Font(IDetailLayoutBuilder::GetDetailFont())
		.CurrentValue(this, &SMetaHumanMediaSourceWidgetImpl::GetVideoCurrentDeviceTypeValue)
		.OnEnumSelectionChanged(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoDeviceTypeValueChanged)
		.EnumValueSubset(GConfig->GetBoolOrDefault(TEXT("LiveLink"), TEXT("bCreateLiveLinkHubInstance"), false, GEngineIni)
			? TArray<int32>({ (int32)EMetaHumanLocalLiveLinkSourceDeviceType::CaptureDevice, (int32)EMetaHumanLocalLiveLinkSourceDeviceType::MediaProfile })
			: TArray<int32>());
#endif

	VideoDeviceCombo = SNew(SMetaHumanStringCombo, &VideoDeviceItems)
					   .OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoDeviceSelected);

#if WITH_EDITOR
	VideoDeviceMediaSourcePicker = SNew(SObjectPropertyEntryBox)
		.AllowedClass(UMediaSource::StaticClass())
		.ObjectPath_Lambda([this]() { return DeviceAssetPath; })
		.OnObjectChanged(this, 
						 &SMetaHumanMediaSourceWidgetImpl::OnMediaAssetSelected, 
						 TRetainedRef<const FString>(UE::MetaHuman::Pipeline::FMediaPlayerNode::MediaSourceURL), 
						 FOnDeviceSelectedDelegate::CreateSP(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoDeviceSelected))
		.AllowClear(true)
		.DisplayThumbnail(true);

	VideoDeviceMediaBundlePicker = SNew(SObjectPropertyEntryBox)
		.AllowedClass(UMediaBundle::StaticClass())
		.ObjectPath_Lambda([this]() { return DeviceAssetPath; })
		.OnObjectChanged(this,
						 &SMetaHumanMediaSourceWidgetImpl::OnMediaAssetSelected,
						 TRetainedRef<const FString>(UE::MetaHuman::Pipeline::FMediaPlayerNode::BundleURL),
						 FOnDeviceSelectedDelegate::CreateSP(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoDeviceSelected))
		.AllowClear(true)
		.DisplayThumbnail(true);

	VideoDeviceSwitcher = SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda([this]() { return GetDeviceSwitcherIndex(); })
		+ SWidgetSwitcher::Slot()
		[
			VideoDeviceCombo.ToSharedRef()
		]
		+ SWidgetSwitcher::Slot()
		[
			VideoDeviceMediaSourcePicker.ToSharedRef()
		]
		+ SWidgetSwitcher::Slot()
		[
			VideoDeviceMediaBundlePicker.ToSharedRef()
		];
#endif

	VideoTrackCombo = SNew(SMetaHumanStringCombo, &VideoTrackItems)
					  .Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
					  .IsEnabled(this, &SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled)
					  .ToolTipText(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip)
					  .OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoTrackSelected);

	VideoTrackFormatCombo = SNew(SMetaHumanStringCombo, &VideoTrackFormatItems)
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
							.IsEnabled(this, &SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled)
							.ToolTipText(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip)
							.OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoTrackFormatSelected);

#if WITH_EDITOR
	AudioDeviceType = SNew(SEnumComboBox, StaticEnum<EMetaHumanLocalLiveLinkSourceDeviceType>())

		.Font(IDetailLayoutBuilder::GetDetailFont())
		.CurrentValue(this, &SMetaHumanMediaSourceWidgetImpl::GetAudioCurrentDeviceTypeValue)
		.OnEnumSelectionChanged(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioDeviceTypeValueChanged)
		.EnumValueSubset(GConfig->GetBoolOrDefault(TEXT("LiveLink"), TEXT("bCreateLiveLinkHubInstance"), false, GEngineIni)
			? TArray<int32>({ (int32)EMetaHumanLocalLiveLinkSourceDeviceType::CaptureDevice, (int32)EMetaHumanLocalLiveLinkSourceDeviceType::MediaProfile })
			: TArray<int32>());
#endif

	AudioDeviceCombo = SNew(SMetaHumanStringCombo, &AudioDeviceItems)
					   .OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioDeviceSelected);

#if WITH_EDITOR
	AudioDeviceMediaSourcePicker = SNew(SObjectPropertyEntryBox)
		.AllowedClass(UMediaSource::StaticClass())
		.ObjectPath_Lambda([this]() { return DeviceAssetPath; })
		.OnObjectChanged(this,
						 &SMetaHumanMediaSourceWidgetImpl::OnMediaAssetSelected,
						 TRetainedRef<const FString>(UE::MetaHuman::Pipeline::FMediaPlayerNode::MediaSourceURL),
						 FOnDeviceSelectedDelegate::CreateSP(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioDeviceSelected))
		.AllowClear(true)
		.DisplayThumbnail(true);

	AudioDeviceMediaBundlePicker = SNew(SObjectPropertyEntryBox)
		.AllowedClass(UMediaBundle::StaticClass())
		.ObjectPath_Lambda([this]() { return DeviceAssetPath; })
		.OnObjectChanged(this,
						 &SMetaHumanMediaSourceWidgetImpl::OnMediaAssetSelected,
						 TRetainedRef<const FString>(UE::MetaHuman::Pipeline::FMediaPlayerNode::BundleURL),
						 FOnDeviceSelectedDelegate::CreateSP(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioDeviceSelected))
		.AllowClear(true)
		.DisplayThumbnail(true);

	AudioDeviceSwitcher = SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda([this]() { return GetDeviceSwitcherIndex(); })
		+ SWidgetSwitcher::Slot()
		[
			AudioDeviceCombo.ToSharedRef()
		]
		+ SWidgetSwitcher::Slot()
		[
			AudioDeviceMediaSourcePicker.ToSharedRef()
		]
		+ SWidgetSwitcher::Slot()
		[
			AudioDeviceMediaBundlePicker.ToSharedRef()
		];
#endif

	AudioTrackCombo = SNew(SMetaHumanStringCombo, &AudioTrackItems)
					  .Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
					  .IsEnabled(this, &SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled)
					  .ToolTipText(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip)
					  .OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioTrackSelected);

	AudioTrackFormatCombo = SNew(SMetaHumanStringCombo, &AudioTrackFormatItems)
						    .Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
							.IsEnabled(this, &SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled)
							.ToolTipText(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip)
							.OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioTrackFormatSelected);

	AdvancedCheckBox = SNew(SCheckBox);

	FilteredWidget = SNew(SCheckBox)
		.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
		.ToolTipText(LOCTEXT("FilteredTooltip", "Filter the formats to show only the most relevant ones"))
		.IsChecked_Lambda([this]()
		{
			return bVideoTrackFormatItemsFiltered ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState InState)
		{
			bVideoTrackFormatItemsFiltered = (InState == ECheckBoxState::Checked);

			OnVideoTrackSelected(VideoTrackCombo->CurrentItem);
		});

	StartTimeoutWidget = SNew(SNumericEntryBox<float>)
		.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
#if WITH_EDITOR
		.Font(IDetailLayoutBuilder::GetDetailFont())
#endif
		.ToolTipText(LOCTEXT("StartTimeoutTooltip", "Timeout for waiting for media to open"))
		.Value_Lambda([this]()
		{
			return StartTimeout;
		})
		.OnValueCommitted_Lambda([this](float InValue, ETextCommit::Type)
		{
			StartTimeout = InValue;
		});

	FormatWaitTimeWidget = SNew(SNumericEntryBox<float>)
		.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
#if WITH_EDITOR
		.Font(IDetailLayoutBuilder::GetDetailFont())
#endif
		.ToolTipText(LOCTEXT("FormatWaitTimeTooltip", "Time to wait for format changes to take effect"))
		.Value_Lambda([this]()
		{
			return FormatWaitTime;
		})
		.OnValueCommitted_Lambda([this](float InValue, ETextCommit::Type)
		{
			FormatWaitTime = InValue;
		});

	SampleTimeoutWidget = SNew(SNumericEntryBox<float>)
		.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
#if WITH_EDITOR
		.Font(IDetailLayoutBuilder::GetDetailFont())
#endif
		.ToolTipText(LOCTEXT("SampleTimeoutTooltip", "Timeout for waiting on first media sample to arrive"))
		.Value_Lambda([this]()
		{
			return SampleTimeout;
		})
		.OnValueCommitted_Lambda([this](float InValue, ETextCommit::Type)
		{
			SampleTimeout = InValue;
		});

	PopulateDevices();

	const float Padding = 5;
	const float FirstColWidth = 140;

	TSharedPtr<SVerticalBox> Layout = SNew(SVerticalBox);

	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Video || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		Layout->AddSlot()
			.AutoHeight()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("VideoDevice", "Video Device"))
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
#if WITH_EDITOR
						VideoDeviceSwitcher.ToSharedRef()
#else
						VideoDeviceCombo.ToSharedRef()
#endif
					]
				]
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("VideoTrack", "Video Track"))
						.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						VideoTrackCombo.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("VideoTrackFormat", "Video Track Format"))
						.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						VideoTrackFormatCombo.ToSharedRef()
					]
				]
			];
	}

	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Audio || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		Layout->AddSlot()
			.AutoHeight()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AudioDevice", "Audio Device"))
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
#if WITH_EDITOR
						AudioDeviceSwitcher.ToSharedRef()
#else
						AudioDeviceCombo.ToSharedRef()
#endif
					]
				]
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AudioTrack", "Audio Track"))
						.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						AudioTrackCombo.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AudioTrackFormat", "Audio Track Format"))
						.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						AudioTrackFormatCombo.ToSharedRef()
					]
				]
			];
	}

	Layout->AddSlot()
		.AutoHeight()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(Padding)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Advanced", "Advanced"))
					.MinDesiredWidth(FirstColWidth)
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Top)
				.AutoWidth()
				[
					AdvancedCheckBox.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.Padding(Padding * 6, 0)
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(Padding)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Filtered", "Filter Format List"))
							.ToolTipText(LOCTEXT("FilteredTooltip", "Filter the formats to show only the most relevant ones"))
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
							.MinDesiredWidth(FirstColWidth)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							FilteredWidget.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.Padding(Padding)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StartTimeout", "Start Timeout"))
							.ToolTipText(LOCTEXT("StartTimeoutTooltip", "Timeout for waiting for media to open"))
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
							.MinDesiredWidth(FirstColWidth)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							StartTimeoutWidget.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.Padding(Padding)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FormatWaitTime", "Format Wait Time"))
							.ToolTipText(LOCTEXT("FormatWaitTimeTooltip", "Time to wait for format changes to take effect"))
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
							.MinDesiredWidth(FirstColWidth)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							FormatWaitTimeWidget.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.Padding(Padding)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SampleTimeout", "Sample Timeout"))
							.ToolTipText(LOCTEXT("SampleTimeoutTooltip", "Timeout for waiting on first media sample to arrive"))
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
							.MinDesiredWidth(FirstColWidth)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SampleTimeoutWidget.ToSharedRef()
						]
					]
				]
			]
		];

	ChildSlot
	[
		Layout.ToSharedRef()
	];
}

void SMetaHumanMediaSourceWidgetImpl::OnAssetsAddedOrDeleted(TConstArrayView<FAssetData> InAssets)
{
	PopulateDevices();
}

void SMetaHumanMediaSourceWidgetImpl::OnAssetRenamed(const FAssetData& InAsset, const FString& InOldObjectPath)
{
	PopulateDevices();
}

void SMetaHumanMediaSourceWidgetImpl::PopulateDevices()
{
#if WITH_EDITOR
	DeviceAssetPath.Empty();
#endif
	bVideoTracksCached = false;

	VideoDevices.Reset();
	VideoDeviceItems.Reset();

	TArray<FMetaHumanLiveLinkVideoDevice> VideoDevicesArray;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoDevicesOfType(VideoDevicesArray, CurrentVideoDeviceTypeValue);

	for (const FMetaHumanLiveLinkVideoDevice& VideoDevice : VideoDevicesArray)
	{
		VideoDevices.Add(VideoDevice.Url, VideoDevice);
		VideoDeviceItems.Add(MakeShared<TPair<FString, FString>>(VideoDevice.Name, VideoDevice.Url));
	}

	VideoDeviceCombo->RefreshOptions();

	OnVideoDeviceSelected(VideoDeviceItems.IsEmpty() ? nullptr : VideoDeviceItems[0]);


	AudioDevices.Reset();
	AudioDeviceItems.Reset();

	TArray<FMetaHumanLiveLinkAudioDevice> AudioDevicesArray;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioDevicesOfType(AudioDevicesArray, CurrentAudioDeviceTypeValue);

	for (const FMetaHumanLiveLinkAudioDevice& AudioDevice : AudioDevicesArray)
	{
		AudioDevices.Add(AudioDevice.Url, AudioDevice);
		AudioDeviceItems.Add(MakeShared<TPair<FString, FString>>(AudioDevice.Name, AudioDevice.Url));
	}

	AudioDeviceCombo->RefreshOptions();

	OnAudioDeviceSelected(AudioDeviceItems.IsEmpty() ? nullptr : AudioDeviceItems[0]);
}

void SMetaHumanMediaSourceWidgetImpl::OnVideoDeviceSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	VideoDeviceCombo->CurrentItem = InItem;

	VideoTracks.Reset();
	VideoTrackItems.Reset();

	if (InItem)
	{
		TArray<FMetaHumanLiveLinkVideoTrack> VideoTracksArray;
		bool bTimedOut = false;
		// GetVideoTracks blocks the UI thread (synchronous WMF device query, up to 5s).
		// Cache results so repeated select/deselect cycles don't re-enumerate.
		if (bVideoTracksCached && CachedVideoDeviceUrl == InItem->Value)
		{
			VideoTracksArray = CachedVideoTracks;
		}
		else
		{
			UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoTracks(VideoDevices[InItem->Value], VideoTracksArray, bTimedOut);
			CachedVideoDeviceUrl = InItem->Value;
			CachedVideoTracks = VideoTracksArray;
			bVideoTracksCached = true;
		}

		for (const FMetaHumanLiveLinkVideoTrack& VideoTrack : VideoTracksArray)
		{
			VideoTracks.Add(FString::FromInt(VideoTrack.Index), VideoTrack);
			VideoTrackItems.Add(MakeShared<TPair<FString, FString>>(VideoTrack.Name, FString::FromInt(VideoTrack.Index)));
		}
	}

	VideoTrackCombo->RefreshOptions();

	if (VideoTrackItems.IsEmpty())
	{
		OnVideoTrackSelected(nullptr);
	}
	else
	{
		for (int32 VideoTrack = 0; VideoTrack < VideoTrackItems.Num(); ++VideoTrack)
		{
			OnVideoTrackSelected(VideoTrackItems[VideoTrack]);

			if (!VideoTrackFormatItems.IsEmpty())
			{
				break;
			}
		}
	}
}

void SMetaHumanMediaSourceWidgetImpl::OnVideoTrackSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	VideoTrackCombo->CurrentItem = InItem;

	VideoTrackFormats.Reset();
	VideoTrackFormatItems.Reset();

	if (InItem)
	{
		TArray<FMetaHumanLiveLinkVideoFormat> VideoFormatsArray;
		bool bTimedOut = false;
		UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoFormats(VideoTracks[InItem->Value], VideoFormatsArray, bTimedOut, bVideoTrackFormatItemsFiltered);

		for (const FMetaHumanLiveLinkVideoFormat& VideoFormat : VideoFormatsArray)
		{
			VideoTrackFormats.Add(FString::FromInt(VideoFormat.Index), VideoFormat);
			VideoTrackFormatItems.Add(MakeShared<TPair<FString, FString>>(VideoFormat.Name, FString::FromInt(VideoFormat.Index)));
		}
	}

	VideoTrackFormatCombo->RefreshOptions();

	OnVideoTrackFormatSelected(VideoTrackFormatItems.IsEmpty() ? nullptr : VideoTrackFormatItems[0]);
}

void SMetaHumanMediaSourceWidgetImpl::OnVideoTrackFormatSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	VideoTrackFormatCombo->CurrentItem = InItem;
}

void SMetaHumanMediaSourceWidgetImpl::OnAudioDeviceSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	AudioDeviceCombo->CurrentItem = InItem;

	AudioTracks.Reset();
	AudioTrackItems.Reset();

	if (InItem)
	{
		TArray<FMetaHumanLiveLinkAudioTrack> AudioTracksArray;
		bool bTimedOut = false;
		UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioTracks(AudioDevices[InItem->Value], AudioTracksArray, bTimedOut);

		for (const FMetaHumanLiveLinkAudioTrack& AudioTrack : AudioTracksArray)
		{
			AudioTracks.Add(FString::FromInt(AudioTrack.Index), AudioTrack);
			AudioTrackItems.Add(MakeShared<TPair<FString, FString>>(AudioTrack.Name, FString::FromInt(AudioTrack.Index)));
		}
	}

	AudioTrackCombo->RefreshOptions();

	OnAudioTrackSelected(AudioTrackItems.IsEmpty() ? nullptr : AudioTrackItems[0]);
}

void SMetaHumanMediaSourceWidgetImpl::OnAudioTrackSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	AudioTrackCombo->CurrentItem = InItem;
	
	AudioTrackFormats.Reset();
	AudioTrackFormatItems.Reset();

	if (InItem)
	{
		TArray<FMetaHumanLiveLinkAudioFormat> AudioFormatsArray;
		bool bTimedOut = false;
		UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioFormats(AudioTracks[InItem->Value], AudioFormatsArray, bTimedOut);

		for (const FMetaHumanLiveLinkAudioFormat& AudioFormat : AudioFormatsArray)
		{
			AudioTrackFormats.Add(FString::FromInt(AudioFormat.Index), AudioFormat);
			AudioTrackFormatItems.Add(MakeShared<TPair<FString, FString>>(AudioFormat.Name, FString::FromInt(AudioFormat.Index)));
		}
	}

	AudioTrackFormatCombo->RefreshOptions();

	OnAudioTrackFormatSelected(AudioTrackFormatItems.IsEmpty() ? nullptr : AudioTrackFormatItems[0]);
}

void SMetaHumanMediaSourceWidgetImpl::OnAudioTrackFormatSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	AudioTrackFormatCombo->CurrentItem = InItem;
}

#if WITH_EDITOR
void SMetaHumanMediaSourceWidgetImpl::OnMediaAssetSelected(const FAssetData& InAssetData, 
														   TRetainedRef<const FString> InPrefixUrl,
														   FOnDeviceSelectedDelegate InOnDeviceSelected)
{
	if (InAssetData.IsValid())
	{
		UObject* Asset = InAssetData.GetAsset();
		if (Asset)
		{
			FString Name = Asset->GetName();
			FString Url = InPrefixUrl.Get() + Asset->GetPathName();
			DeviceAssetPath = Asset->GetPathName();

			TSharedPtr<TPair<FString, FString>> Item = MakeShared<TPair<FString, FString>>(MoveTemp(Name), MoveTemp(Url));
			InOnDeviceSelected.Execute(Item);
			return;
		}
	}
	DeviceAssetPath.Empty();
	InOnDeviceSelected.Execute(nullptr);
}

int32 SMetaHumanMediaSourceWidgetImpl::GetDeviceSwitcherIndex() const
{
	if (IsMediaSource()) return 1;
	if (IsBundle()) return 2;
	return 0;
}

#endif

int32 SMetaHumanMediaSourceWidgetImpl::GetVideoCurrentDeviceTypeValue() const
{
	return static_cast<int32>(CurrentVideoDeviceTypeValue);
}

void SMetaHumanMediaSourceWidgetImpl::OnVideoDeviceTypeValueChanged(int32 InNewValue, ESelectInfo::Type)
{
	CurrentVideoDeviceTypeValue = static_cast<EMetaHumanLocalLiveLinkSourceDeviceType>(InNewValue);
	bVideoTracksCached = false;

	PopulateDevices();
}

int32 SMetaHumanMediaSourceWidgetImpl::GetAudioCurrentDeviceTypeValue() const
{
	return static_cast<int32>(CurrentAudioDeviceTypeValue);
}

void SMetaHumanMediaSourceWidgetImpl::OnAudioDeviceTypeValueChanged(int32 InNewValue, ESelectInfo::Type)
{
	CurrentAudioDeviceTypeValue = static_cast<EMetaHumanLocalLiveLinkSourceDeviceType>(InNewValue);

	PopulateDevices();
}

bool SMetaHumanMediaSourceWidgetImpl::CanCreate() const
{
	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Video || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		return VideoDeviceCombo->CurrentItem.IsValid() && (IsBundle() || IsMediaSource() || IsMediaProfile() ||  (VideoTrackCombo->CurrentItem.IsValid() && VideoTrackFormatCombo->CurrentItem.IsValid()));
	}
	else
	{
		return AudioDeviceCombo->CurrentItem.IsValid() && (IsBundle() || IsMediaSource() || IsMediaProfile() || (AudioTrackCombo->CurrentItem.IsValid() && AudioTrackFormatCombo->CurrentItem.IsValid()));
	}
}

bool SMetaHumanMediaSourceWidgetImpl::IsBundle() const
{
	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Video || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		return CurrentVideoDeviceTypeValue == EMetaHumanLocalLiveLinkSourceDeviceType::MediaBundle;
	}

	return CurrentAudioDeviceTypeValue == EMetaHumanLocalLiveLinkSourceDeviceType::MediaBundle;
}

bool SMetaHumanMediaSourceWidgetImpl::IsMediaSource() const
{
	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Video || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		return CurrentVideoDeviceTypeValue == EMetaHumanLocalLiveLinkSourceDeviceType::MediaSource;
	}

	return CurrentAudioDeviceTypeValue == EMetaHumanLocalLiveLinkSourceDeviceType::MediaSource;
}

bool SMetaHumanMediaSourceWidgetImpl::IsMediaProfile() const
{
	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Video || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		return CurrentVideoDeviceTypeValue == EMetaHumanLocalLiveLinkSourceDeviceType::MediaProfile;
	}

	return CurrentAudioDeviceTypeValue == EMetaHumanLocalLiveLinkSourceDeviceType::MediaProfile;
}

EVisibility SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility() const
{
	return EVisibility::Visible;
}

EVisibility SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility() const
{
	return EVisibility::Visible;
}

bool SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled() const 
{ 
	return !IsBundle() && !IsMediaSource() && !IsMediaProfile();
}

FText SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip() const 
{
	if (IsBundle())
	{
		return LOCTEXT("DisabledBundle", "Disabled for Media Bundles");
	}
	else if (IsMediaSource())
	{
		return LOCTEXT("DisabledMediaSource", "Disabled for Media Sources");
	}
	else if (IsMediaProfile())
	{
		return LOCTEXT("DisabledMediaProfile", "Disabled for Media Profiles");
	}

	return FText();
}

FMetaHumanMediaSourceCreateParams SMetaHumanMediaSourceWidgetImpl::GetCreateParams() const
{
	FMetaHumanMediaSourceCreateParams CreateParams;

	CreateParams.VideoName = VideoDeviceCombo->CurrentItem.IsValid() ? VideoDeviceCombo->CurrentItem->Key : TEXT("");
	CreateParams.VideoURL = VideoDeviceCombo->CurrentItem.IsValid() ? VideoDeviceCombo->CurrentItem->Value : TEXT("");
	CreateParams.VideoTrack = VideoTrackCombo->CurrentItem.IsValid() ? FCString::Atoi(*VideoTrackCombo->CurrentItem->Value) : -1;
	CreateParams.VideoTrackFormat = VideoTrackFormatCombo->CurrentItem.IsValid() ? FCString::Atoi(*VideoTrackFormatCombo->CurrentItem->Value) : -1;
	CreateParams.VideoTrackFormatName = VideoTrackFormatCombo->CurrentItem.IsValid() ? VideoTrackFormatCombo->CurrentItem->Key : TEXT("");

	CreateParams.AudioName = AudioDeviceCombo->CurrentItem.IsValid() ? AudioDeviceCombo->CurrentItem->Key : TEXT("");
	CreateParams.AudioURL = AudioDeviceCombo->CurrentItem.IsValid() ? AudioDeviceCombo->CurrentItem->Value : TEXT("");
	CreateParams.AudioTrack = AudioTrackCombo->CurrentItem.IsValid() ? FCString::Atoi(*AudioTrackCombo->CurrentItem->Value) : -1;
	CreateParams.AudioTrackFormat = AudioTrackFormatCombo->CurrentItem.IsValid() ? FCString::Atoi(*AudioTrackFormatCombo->CurrentItem->Value) : -1;
	CreateParams.AudioTrackFormatName = AudioTrackFormatCombo->CurrentItem.IsValid() ? AudioTrackFormatCombo->CurrentItem->Key : TEXT("");

	CreateParams.StartTimeout = StartTimeout;
	CreateParams.FormatWaitTime = FormatWaitTime;
	CreateParams.SampleTimeout = SampleTimeout;

	return CreateParams;
}



void SMetaHumanMediaSourceWidget::Construct(const FArguments& InArgs, EMediaType InMediaType)
{
	Impl = SNew(SMetaHumanMediaSourceWidgetImpl, InMediaType);

	ChildSlot
	[
		Impl.ToSharedRef()
	];
}

bool SMetaHumanMediaSourceWidget::CanCreate() const
{
	return Impl->CanCreate();
}

FMetaHumanMediaSourceCreateParams SMetaHumanMediaSourceWidget::GetCreateParams() const
{
	return Impl->GetCreateParams();
}

TSharedPtr<SWidget> SMetaHumanMediaSourceWidget::GetWidget(EWidgetType InWidgetType) const
{
	TSharedPtr<SWidget> Widget;

	switch (InWidgetType)
	{
		case EWidgetType::VideoDevice:
#if WITH_EDITOR
			Widget = Impl->VideoDeviceSwitcher;
#else
			Widget = Impl->VideoDeviceCombo;
#endif
			break;

		case EWidgetType::VideoTrack:
			Widget = Impl->VideoTrackCombo;
			break;

		case EWidgetType::VideoTrackFormat:
			Widget = Impl->VideoTrackFormatCombo;
			break;

		case EWidgetType::AudioDevice:
#if WITH_EDITOR
			Widget = Impl->AudioDeviceSwitcher;
#else
			Widget = Impl->AudioDeviceCombo;
#endif
			break;

		case EWidgetType::AudioTrack:
			Widget = Impl->AudioTrackCombo;
			break;

		case EWidgetType::AudioTrackFormat:
			Widget = Impl->AudioTrackFormatCombo;
			break;

		case EWidgetType::Filtered:
			Widget = Impl->FilteredWidget;
			break;

		case EWidgetType::StartTimeout:
			Widget = Impl->StartTimeoutWidget;
			break;

		case EWidgetType::FormatWaitTime:
			Widget = Impl->FormatWaitTimeWidget;
			break;

		case EWidgetType::SampleTimeout:
			Widget = Impl->SampleTimeoutWidget;
			break;
#if WITH_EDITOR
		case EWidgetType::VideoDeviceType:
			Widget = Impl->VideoDeviceType;
			break;

		case EWidgetType::AudioDeviceType:
			Widget = Impl->AudioDeviceType;
			break;
#endif
		default:
			check(false);
			break;
	}

	return Widget;
}

void SMetaHumanMediaSourceWidget::Repopulate()
{
	Impl->PopulateDevices();
}

#undef LOCTEXT_NAMESPACE
