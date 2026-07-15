// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPipelineMediaPlayerUENode.h"
#include "MetaHumanTrace.h"
#include "MetaHumanLocalLiveLinkSubject.h"

#include "UObject/Package.h"

#include "MediaPlayerFacade.h"
#include "MediaBundle.h"
#include "Modules/ModuleManager.h"
#include "IMediaModule.h"
#include "IMediaPlayerFactory.h"
#include "FileMediaSource.h"
#include "MediaTexture.h"

#include "Profile/MediaProfile.h"
#include "Profile/IMediaProfileManager.h"
#include "Profile/MediaProfilePlaybackManager.h"

namespace UE::MetaHuman::Pipeline
{

template<typename T>
void ConvertAudioSamples(const void* InRawBuffer, int32 InNumDataItems, TArray<float>& OutBuffer)
{
	const T* Data = static_cast<const T*>(InRawBuffer);

	// For signed types, use -Min() as the divisor rather than Max(). Two's complement signed
	// integers have one more negative value than positive (e.g. int16 range is -32768 to 32767),
	// so dividing by Max() would push the minimum value past -1.0. Using -Min() (32768 for int16)
	// keeps all output within [-1.0, 1.0]. For unsigned types, Max() is the correct divisor.
	constexpr float MaxMagnitude = TIsSigned<T>::Value
		? -static_cast<float>(TNumericLimits<T>::Min())
		:  static_cast<float>(TNumericLimits<T>::Max());

	for (int32 i = 0; i < InNumDataItems; ++i)
	{
		OutBuffer.Add(Data[i] / MaxMagnitude);
	}
}

template<>
void ConvertAudioSamples<float>(const void* InRawBuffer, int32 InNumDataItems, TArray<float>& OutBuffer)
{
	const float* Data = static_cast<const float*>(InRawBuffer);
	OutBuffer.Append(Data, InNumDataItems);
}

template<>
void ConvertAudioSamples<double>(const void* InRawBuffer, int32 InNumDataItems, TArray<float>& OutBuffer)
{
	const double* Data = static_cast<const double*>(InRawBuffer);
	for (int32 i = 0; i < InNumDataItems; ++i)
	{
		OutBuffer.Add(static_cast<float>(Data[i]));
	}
}

FMediaPlayerUENode::FMediaPlayerUENode(const FString& InName) : FMediaPlayerNode("MediaPlayerUE", InName)
{
}

bool FMediaPlayerUENode::Start(const TSharedPtr<FPipelineData>& InPipelineData)
{
	double Start = FPlatformTime::Seconds();

	if (!VideoURL.IsEmpty() && !VideoPlayer)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::NoVideoPlayer);
		InPipelineData->SetErrorNodeMessage(TEXT("Failed to setup video player"));

		return false;
	}

	if (!AudioURL.IsEmpty() && !AudioPlayer)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::NoAudioPlayer);
		InPipelineData->SetErrorNodeMessage(TEXT("Failed to setup audio player"));

		return false;
	}

#if WITH_EDITORONLY_DATA
	if (VideoPlayer)
	{
		VideoPlayer->AffectedByPIEHandling = false;
	}

	if (AudioPlayer)
	{
		AudioPlayer->AffectedByPIEHandling = false;
	}
#endif

	AudioBuffer.Reset();
	AudioBufferSampleRate = -1;
	AudioBufferNumChannels = -1;
	AudioBufferDataSizePerFrame = -1;
	AudioFrameNumber = -1;
	AudioFrameStartTime = -1;

	bool bVideoReady = !VideoPlayer;
	bool bAudioReady = !AudioPlayer;

	while (FPlatformTime::Seconds() - Start < StartTimeout)
	{
		if (!bVideoReady && VideoPlayer->GetPlayerFacade()->IsReady())
		{
			if (VideoTrack >= 0)
			{
				if (!VideoPlayer->SelectTrack(EMediaPlayerTrack::Video, VideoTrack))
				{
					InPipelineData->SetErrorNodeCode(ErrorCode::BadVideoTrack);
					InPipelineData->SetErrorNodeMessage(TEXT("Failed to set video track"));

					return false;
				}

				if (VideoTrackFormat >= 0 && !VideoPlayer->SetTrackFormat(EMediaPlayerTrack::Video, VideoTrack, VideoTrackFormat))
				{
					InPipelineData->SetErrorNodeCode(ErrorCode::BadVideoTrackFormat);
					InPipelineData->SetErrorNodeMessage(TEXT("Failed to set video track format"));

					return false;
				}
			}

			bVideoReady = true;
		}
		else if (!bAudioReady && AudioPlayer->GetPlayerFacade()->IsReady())
		{
			if (AudioTrack >= 0)
			{
				if (!AudioPlayer->SelectTrack(EMediaPlayerTrack::Audio, AudioTrack))
				{
					InPipelineData->SetErrorNodeCode(ErrorCode::BadAudioTrack);
					InPipelineData->SetErrorNodeMessage(TEXT("Failed to set audio track"));

					return false;
				}

				if (AudioTrackFormat >= 0 && !AudioPlayer->SetTrackFormat(EMediaPlayerTrack::Audio, AudioTrack, AudioTrackFormat))
				{
					InPipelineData->SetErrorNodeCode(ErrorCode::BadAudioTrackFormat);
					InPipelineData->SetErrorNodeMessage(TEXT("Failed to set audio track format"));

					return false;
				}
			}

			bAudioReady = true;
		}
		else if (bVideoReady && bAudioReady)
		{
			FPlatformProcess::Sleep(FormatWaitTime); // For track/format change to take effect. Could not find a suitable event to be notified of this.

			if (VideoPlayer && !VideoPlayer->Play())
			{
				InPipelineData->SetErrorNodeCode(ErrorCode::FailedToPlayVideo);
				InPipelineData->SetErrorNodeMessage(TEXT("Failed to play video"));

				return false;
			}

			if (AudioPlayer && !AudioPlayer->Play())
			{
				InPipelineData->SetErrorNodeCode(ErrorCode::FailedToPlayAudio);
				InPipelineData->SetErrorNodeMessage(TEXT("Failed to play audio"));

				return false;
			}

			bIsFirstVideoFrame = true;
			bIsFirstAudioFrame = true;

			return true;
		}
		else
		{
			FPlatformProcess::Sleep(StartWaitTime);
		}
	}

	if (!bVideoReady)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::VideoTimeout);
		InPipelineData->SetErrorNodeMessage(TEXT("Timeout opening video"));
	}
	else
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::AudioTimeout);
		InPipelineData->SetErrorNodeMessage(TEXT("Timeout opening audio"));
	}

	return false;
}

bool FMediaPlayerUENode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	double Start = FPlatformTime::Seconds();

	TSharedPtr<IMediaTextureSample> VideoSample;
	TSharedPtr<IMediaAudioSample> AudioSample;
	int32 NumDroppedFrames = 0;

	{
		MHA_CPUPROFILER_EVENT_SCOPE_STR("Get Frame");

		while (true)
		{
			if (*bAbort)
			{
				return false;
			}

			if (bIsFirstVideoFrame && bIsFirstAudioFrame && FPlatformTime::Seconds() > Start + SampleTimeout) // Only timeout on first frame - sample are not delivered if game thread is blocked
			{
				if (VideoPlayer && !VideoSample)
				{
					InPipelineData->SetErrorNodeCode(ErrorCode::VideoTimeout);
					InPipelineData->SetErrorNodeMessage(TEXT("Timeout sampling video"));
				}
				else
				{
					InPipelineData->SetErrorNodeCode(ErrorCode::AudioTimeout);
					InPipelineData->SetErrorNodeMessage(TEXT("Timeout sampling audio"));
				}

				return false;
			}

			if (VideoPlayer && VideoFrames->Dequeue(VideoSample) && VideoSample.IsValid())
			{
				if (bIsFirstVideoFrame)
				{
					const float Rate = VideoPlayer->GetVideoTrackFrameRate(VideoTrack, VideoTrackFormat);

					int32 Numerator = 0;
					int32 Denominator = 0;

					if (Rate - static_cast<int32>(Rate) > 1.0e-5) // The only fractional frame rates supported are the 29.97 types
					{
						Numerator = FMath::CeilToInt(Rate) * 1000;
						Denominator = 1001;
					}
					else
					{
						Numerator = Rate;
						Denominator = 1;
					}

					FrameRate = FFrameRate(Numerator, Denominator);

					bIsFirstVideoFrame = false;
				}

				if (bAllowFrameDropping)
				{
					TSharedPtr<IMediaTextureSample> LatestVideoSample;
					while (VideoFrames->Dequeue(LatestVideoSample) && LatestVideoSample.IsValid())
					{
						VideoSample = LatestVideoSample;
						NumDroppedFrames++;
					}
				}
			}

			if (AudioPlayer && AudioFrames->Dequeue(AudioSample) && AudioSample.IsValid())
			{
				// Temporary measure. Audio samples come through from underlying UE at about 25Hz.
				// This is with the WFM "Low latency" project setting unchecked. With it checked
				// samples come through faster, at around 100Hz, but seem to have a very large
				// delay on them. This makes the setting unusable at the moment. If this node output 
				// audio samples at 25Hz it would make for jerky animation or whatever was using this
				// data downstream. Instead we buffer the audio and feed it out in parts at a faster rate.
				// For now this is 50Hz since that ties in with immediate use case of realtime audio-to-animation.
				// Maybe this should be a configurable parameter? but ideally I'd like to get away from using
				// it altogether and instead get the "low latency" issues fixed and run at 100Hz.

				if (bIsFirstAudioFrame)
				{
					FrameRate = FFrameRate(50, 1);

					AudioFrameNumber = 0;
					AudioFrameStartTime = FPlatformTime::Seconds();

					bIsFirstAudioFrame = false;
				}

				if (bAllowFrameDropping)
				{
					TSharedPtr<IMediaAudioSample> LatestAudioSample;
					while (AudioFrames->Dequeue(LatestAudioSample) && LatestAudioSample.IsValid())
					{
						AudioSample = LatestAudioSample;
						NumDroppedFrames++;
						AudioBuffer.Reset();
					}
				}

				if (AudioBufferDataSizePerFrame < 0)
				{
					AudioBufferSampleRate = AudioSample->GetSampleRate();
					AudioBufferNumChannels = AudioSample->GetChannels();
					AudioBufferDataSizePerFrame = AudioBufferSampleRate / 50.0 * AudioBufferNumChannels;
				}

				const int32 NumDataItems = AudioSample->GetFrames() * AudioSample->GetChannels();
				const void* RawBuffer = AudioSample->GetBuffer();

				switch (AudioSample->GetFormat())
				{
				case EMediaAudioSampleFormat::Float:
					ConvertAudioSamples<float>(RawBuffer, NumDataItems, AudioBuffer);
					break;
				case EMediaAudioSampleFormat::Double:
					ConvertAudioSamples<double>(RawBuffer, NumDataItems, AudioBuffer);
					break;
				case EMediaAudioSampleFormat::Int16:
					ConvertAudioSamples<int16>(RawBuffer, NumDataItems, AudioBuffer);
					break;
				case EMediaAudioSampleFormat::Int32:
					ConvertAudioSamples<int32>(RawBuffer, NumDataItems, AudioBuffer);
					break;
				case EMediaAudioSampleFormat::Int8:
					ConvertAudioSamples<int8>(RawBuffer, NumDataItems, AudioBuffer);
					break;
				default:
					UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Error, "Unsupported audio sample format: %d", static_cast<int32>(AudioSample->GetFormat()));
					break;
				}

				if (AudioSample->GetTimecode().IsSet())
				{
					UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "Ignoring audio sample timecode");
				}
			}

			if (VideoSample)
			{
				break;
			}
			
			if (AudioBufferDataSizePerFrame > 0 && AudioBuffer.Num() >= AudioBufferDataSizePerFrame)
			{
				// Need to feed audio samples out at a steady rate. Data arrive from the mic as 40 or 50ms samples.
				// We feed data out as 20ms samples (50Hz). Without rate limiting how those smaller
				// samples are fed out you would get two 20ms samples in very quick succession for 
				// every 40/50ms sample received from the mic. There is variability in when the 40/50ms samples
				// are received. We apply as small buffer of 1.5 frames to help even this out. That number
				// was found experimentally as the best balance of smoothness and added latency.
				if (FPlatformTime::Seconds() > AudioFrameStartTime + ((AudioFrameNumber + 1.5) / 50.0))
				{
					AudioFrameNumber++;

					break;
				}
			}

			FPlatformProcess::Sleep(SampleWaitTime);
		}
	}

	FUEImageDataType Image;
	FAudioDataType Audio;
	FQualifiedFrameTime ImageSampleTime;
	FQualifiedFrameTime AudioSampleTime;
	FMetaHumanLocalLiveLinkSubject::ETimeSource ImageSampleTimeSource = FMetaHumanLocalLiveLinkSubject::ETimeSource::NotSet;
	FMetaHumanLocalLiveLinkSubject::ETimeSource AudioSampleTimeSource = FMetaHumanLocalLiveLinkSubject::ETimeSource::NotSet;

	if (VideoSample)
	{ 
		FMetaHumanLocalLiveLinkSubject::GetSampleTime(VideoSample->GetTimecode(), FrameRate, ImageSampleTime, ImageSampleTimeSource);

		MHA_CPUPROFILER_EVENT_SCOPE_STR("Video Conversion");

		if (VideoSample->GetFormat() != EMediaTextureSampleFormat::CharNV12 &&
			VideoSample->GetFormat() != EMediaTextureSampleFormat::CharYUY2 &&
			VideoSample->GetFormat() != EMediaTextureSampleFormat::CharUYVY &&
			VideoSample->GetFormat() != EMediaTextureSampleFormat::CharBGRA &&
			!(VideoSample->GetFormat() == EMediaTextureSampleFormat::YUVv210 && VideoSample->GetOutputDim().X % 6 == 0))
		{
			InPipelineData->SetErrorNodeCode(ErrorCode::UnsupportedVideoFormat);
			InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Unsupported video format %i"), (int32)VideoSample->GetFormat()));

			return false;
		}

		if (!VideoSample->GetBuffer()) // Some player backends, eg Electra, dont appear to fill in buffer. Maybe their image is in a texture (in GPU memory)
		{
			InPipelineData->SetErrorNodeCode(ErrorCode::NoVideoSampleData);
			InPipelineData->SetErrorNodeMessage(TEXT("No video sample data"));

			return false;
		}

		ConvertSample(VideoSample->GetOutputDim(), VideoSample->GetStride(), VideoSample->GetFormat(), (const uint8*) VideoSample->GetBuffer(), Image);
	}

	if (AudioBufferDataSizePerFrame > 0 && AudioBuffer.Num() >= AudioBufferDataSizePerFrame)
	{
		FMetaHumanLocalLiveLinkSubject::GetSampleTime(FrameRate, AudioSampleTime, AudioSampleTimeSource);

		Audio.NumChannels = AudioBufferNumChannels;
		Audio.SampleRate = AudioBufferSampleRate;

		Audio.Data = TArray<float>(AudioBuffer.GetData(), AudioBufferDataSizePerFrame);
		AudioBuffer = TArray<float>(AudioBuffer.GetData() + AudioBufferDataSizePerFrame, AudioBuffer.Num() - AudioBufferDataSizePerFrame);

		Audio.NumSamples = Audio.Data.Num() / Audio.NumChannels;
	}

	InPipelineData->SetData<FUEImageDataType>(Pins[0], MoveTemp(Image));
	InPipelineData->SetData<FAudioDataType>(Pins[1], MoveTemp(Audio));
	InPipelineData->SetData<FQualifiedFrameTime>(Pins[2], ImageSampleTime);
	InPipelineData->SetData<FQualifiedFrameTime>(Pins[3], AudioSampleTime);
	InPipelineData->SetData<int32>(Pins[4], NumDroppedFrames);
	InPipelineData->SetData<int32>(Pins[5], static_cast<uint8>(ImageSampleTimeSource));
	InPipelineData->SetData<int32>(Pins[6], static_cast<uint8>(AudioSampleTimeSource));

	return true;
}

bool FMediaPlayerUENode::End(const TSharedPtr<FPipelineData>& InPipelineData)
{
	AudioBuffer.Reset();
	
	if (VideoFrames)
	{
		VideoFrames->RequestFlush();
		VideoFrames.Reset();
	}

	return true;
}

bool FMediaPlayerUENode::Play(const FString& InVideoURL, int32 InVideoTrack, int32 InVideoTrackFormat,
							  const FString& InAudioURL, int32 InAudioTrack, int32 InAudioTrackFormat)
{
	// Must be done in game thread
	check(IsInGameThread());

	VideoURL = InVideoURL;
	VideoTrack = InVideoTrack;
	VideoTrackFormat = InVideoTrackFormat;

	AudioURL = InAudioURL;
	AudioTrack = InAudioTrack;
	AudioTrackFormat = InAudioTrackFormat;

	bool bOpenedOk = (VideoPlayer || AudioPlayer);

	if (!VideoURL.IsEmpty())
	{
		bIsBundle = VideoURL.StartsWith(BundleURL);
		bIsMediaSource = VideoURL.StartsWith(MediaSourceURL);
		bIsMediaProfile = VideoURL.StartsWith(MediaProfileURL);

		if (bIsBundle)
		{
			Bundle = LoadObject<UMediaBundle>(GetTransientPackage(), *VideoURL.Mid(BundleURL.Len()));
			if (!Bundle)
			{
				return false;
			}

			VideoPlayer = Bundle->GetMediaPlayer();
			if (!VideoPlayer)
			{
				return false;
			}
			
			if (Bundle->MediaSource && Bundle->MediaSource.IsA(UFileMediaSource::StaticClass()))
			{
				// Only WmfMedia (Windows) and AvfMedia (Apple platforms) are supported - other backends deliver the texture sample in GPU memory not the main memory we need
#if PLATFORM_WINDOWS
				const TCHAR* RequiredPlayerName = TEXT("WmfMedia");
#elif PLATFORM_APPLE
				const TCHAR* RequiredPlayerName = TEXT("AvfMedia");
#else
				const TCHAR* RequiredPlayerName = nullptr;
#endif

				if (!RequiredPlayerName)
				{
					UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Error, "No supported media player backend on this platform");
					return false;
				}

				IMediaPlayerFactory* MediaFactory = FModuleManager::LoadModuleChecked<IMediaModule>("Media").GetPlayerFactory(RequiredPlayerName);

				if (MediaFactory)
				{
					VideoPlayer->GetPlayerFacade()->DesiredPlayerName = MediaFactory->GetPlayerName();
				}
				else
				{
					UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "%ls not found", RequiredPlayerName);
					return false;
				}
			}

			Bundle->bLoopMediaSource = true;
		}
		else if (bIsMediaProfile)
		{
			IMediaProfileManager& MediaProfileManager = IMediaProfileManager::Get();
			UMediaProfile* CurrentMediaProfile = MediaProfileManager.GetCurrentMediaProfile();

			if (!CurrentMediaProfile)
			{
				UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "No media profile found");
				return false;
			}

			FString MediaSourcePath = *VideoURL.Mid(MediaProfileURL.Len());
			bool bFound = false;
			for (int32 Index = 0; Index < CurrentMediaProfile->NumMediaSources(); ++Index)
			{
				UMediaSource* TempMediaSource = CurrentMediaProfile->GetMediaSource(Index);
				if (TempMediaSource && TempMediaSource->GetPathName() == MediaSourcePath)
				{
					VideoMediaSource = TempMediaSource;
					bFound = true;
					break;
				}
			}

			if (bFound)
			{
				UMediaTexture* MediaTexture = CurrentMediaProfile->GetPlaybackManager()->OpenSource(VideoMediaSource, this);
				if (MediaTexture)
				{ 
					VideoPlayer = MediaTexture->GetMediaPlayer();
					MediaProfile = CurrentMediaProfile;
					check(VideoPlayer);
				}
				else
				{
					UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "Failed to open media source '%ls'", *VideoMediaSource->GetPathName());
					return false;
				}
			}
			else
			{
				UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "No matching media source found in media profile");
				return false;
			}
		}
		else
		{
			VideoPlayer = NewObject<UMediaPlayer>(GetTransientPackage());
			check(VideoPlayer);

			VideoPlayer->SetLooping(true);
		}

		VideoPlayer->PlayOnOpen = false;

		VideoFrames = MakeShared<FMediaTextureSampleQueue>();
		check(VideoFrames);

		VideoPlayer->GetPlayerFacade()->AddVideoSampleSink(VideoFrames.ToSharedRef());

		if (bIsBundle)
		{
			bOpenedOk &= Bundle->OpenMediaSource();
		}
		else if (bIsMediaSource)
		{
			VideoMediaSource = LoadObject<UMediaSource>(GetTransientPackage(), *VideoURL.Mid(MediaSourceURL.Len()));
			if (!VideoMediaSource)
			{
				UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "No matching media source found in media profile");
				return false;
			}

			bOpenedOk &= VideoPlayer->OpenSource(VideoMediaSource);
		}
		else if (bIsMediaProfile)
		{
			TSharedRef<FMediaPlayerFacade> PlayerFacade = VideoPlayer->GetPlayerFacade();
			bOpenedOk &= PlayerFacade->IsReady() || PlayerFacade->IsPlaying() ? true : false;
		}
		else
		{
			bOpenedOk &= VideoPlayer->OpenUrl(VideoURL);
		}
	}

	if (!AudioURL.IsEmpty())
	{
		bIsBundle = AudioURL.StartsWith(BundleURL);
		bIsMediaSource = AudioURL.StartsWith(MediaSourceURL);
		bIsMediaProfile = AudioURL.StartsWith(MediaProfileURL);

		if (bIsBundle)
		{
			Bundle = LoadObject<UMediaBundle>(GetTransientPackage(), *AudioURL.Mid(BundleURL.Len()));
			if (!Bundle)
			{
				return false;
			}

			AudioPlayer = Bundle->GetMediaPlayer();
			if (!AudioPlayer)
			{
				return false;
			}

			Bundle->bLoopMediaSource = true;
		}
		else if (bIsMediaProfile)
		{
			IMediaProfileManager& MediaProfileManager = IMediaProfileManager::Get();
			UMediaProfile* CurrentMediaProfile = MediaProfileManager.GetCurrentMediaProfile();
			if (!CurrentMediaProfile)
			{
				UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "No media profile found");
				return false;
			}

			FString MediaSourcePath = *AudioURL.Mid(MediaProfileURL.Len());
			bool bFound = false;
			for (int32 Index = 0; Index < CurrentMediaProfile->NumMediaSources(); ++Index)
			{
				UMediaSource* TempMediaSource = CurrentMediaProfile->GetMediaSource(Index);
				if (TempMediaSource && TempMediaSource->GetPathName() == MediaSourcePath)
				{
					AudioMediaSource = TempMediaSource;
					bFound = true;
					break;
				}
			}

			if (bFound)
			{
				UMediaTexture* MediaTexture = CurrentMediaProfile->GetPlaybackManager()->OpenSource(AudioMediaSource, this);
				if (MediaTexture)
				{
					AudioPlayer = MediaTexture->GetMediaPlayer();
					MediaProfile = CurrentMediaProfile;
					check(AudioPlayer);
				}
				else
				{
					UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "Failed to open media source '%ls'", *AudioMediaSource->GetPathName());
					return false;
				}
			}
			else
			{
				UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "No matching media source found in media profile");
				return false;
			}
		}
		else
		{
			AudioPlayer = NewObject<UMediaPlayer>(GetTransientPackage());
			check(AudioPlayer);

			AudioPlayer->SetLooping(true);
		}

		AudioPlayer->PlayOnOpen = false;

		AudioFrames = MakeShared<FMediaAudioSampleQueue>();
		check(AudioFrames);

		AudioPlayer->GetPlayerFacade()->AddAudioSampleSink(AudioFrames.ToSharedRef());

		if (bIsBundle)
		{
			bOpenedOk &= Bundle->OpenMediaSource();
		}
		else if (bIsMediaSource)
		{
			AudioMediaSource = LoadObject<UMediaSource>(GetTransientPackage(), *AudioURL.Mid(MediaSourceURL.Len()));
			if (!AudioMediaSource)
			{
				UE_LOGF(LogMetaHumanLocalLiveLinkSubject, Warning, "No matching media source found in media profile");
				return false;
			}

			bOpenedOk &= AudioPlayer->OpenSource(AudioMediaSource);
		}
		else if (bIsMediaProfile)
		{
			TSharedRef<FMediaPlayerFacade> PlayerFacade = AudioPlayer->GetPlayerFacade();
			bOpenedOk &= PlayerFacade->IsReady() || PlayerFacade->IsPlaying() ? true : false;
		}
		else
		{
			bOpenedOk &= AudioPlayer->OpenUrl(AudioURL);
		}
	}

	return bOpenedOk;
}

bool FMediaPlayerUENode::Close()
{
	// Must be done in game thread
	check(IsInGameThread());
	
	if (VideoFrames)
	{
		VideoFrames->RequestFlush();
		VideoFrames.Reset();
	}

	// Don't close the video player as it is owned by the media profile
	if (VideoPlayer && !bIsMediaProfile)
	{
		VideoPlayer->Close();
	}

	// Don't close the audio player as it is owned by the media profile
	if (AudioPlayer && !bIsMediaProfile)
	{
		AudioPlayer->Close();
	}

	if (bIsMediaProfile)
	{
		if (TStrongObjectPtr<UMediaProfile> StrongMediaProfile = MediaProfile.Pin())
		{
			UMediaProfilePlaybackManager::FCloseSourceArgs Args;
			Args.Consumer = this;

			if (VideoMediaSource)
			{
				StrongMediaProfile->GetPlaybackManager()->CloseSource(VideoMediaSource, Args);
			}

			if (AudioMediaSource)
			{
				StrongMediaProfile->GetPlaybackManager()->CloseSource(AudioMediaSource, Args);
			}
		}
	}

	return true;
}

void FMediaPlayerUENode::AddReferencedObjects(FReferenceCollector& InCollector)
{
	InCollector.AddReferencedObject(VideoPlayer);
	InCollector.AddReferencedObject(AudioPlayer);
	InCollector.AddReferencedObject(Bundle);
	InCollector.AddReferencedObject(VideoMediaSource);
	InCollector.AddReferencedObject(AudioMediaSource);
}

FString FMediaPlayerUENode::GetReferencerName() const
{
	return TEXT("FMediaPlayerNodeUE");
}

}