// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "DSP/Filter.h"
#include "MetasoundChannelAgnosticType.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundVertex.h"
#include "Templates/UniquePtr.h"

#define LOCTEXT_NAMESPACE "MetasoundExperimental_CatLadderFilterNode"

PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS

namespace Metasound
{
	namespace CatLadderFilterVertexNames
	{
		METASOUND_PARAM(ParamAudioInput,		"In",					"Audio to be processed by the filter.");
		METASOUND_PARAM(ParamCutoffFrequency,	"Cutoff Frequency",		"Controls cutoff frequency. Shared across all channels.");
		METASOUND_PARAM(ParamResonance,			"Resonance",			"Controls filter resonance. Shared across all channels.");
		METASOUND_PARAM(ParamAudioOutput,		"Out",					"Audio processed by the filter.");
	}

	/**
	 * Channel-Agnostic ladder filter operator.
	 *
	 * Cutoff and Resonance are shared across channels. Coefficient updates are
	 * replicated to every per-channel filter whenever either control value
	 * changes — the coefficient work is small relative to per-sample processing.
	 */
	class FCatLadderFilterOperator final : public TExecutableOperator<FCatLadderFilterOperator>
	{
	private:
		static constexpr float InvalidValue = -1.f;

	public:
		FCatLadderFilterOperator(
			const FOperatorSettings& InSettings,
			FChannelAgnosticTypeReadRef&& InAudioInput,
			FFloatReadRef&& InFrequency,
			FFloatReadRef&& InResonance,
			FChannelAgnosticTypeWriteRef&& InAudioOutput)
			: AudioInput(MoveTemp(InAudioInput))
			, Frequency(MoveTemp(InFrequency))
			, Resonance(MoveTemp(InResonance))
			, AudioOutput(MoveTemp(InAudioOutput))
			, OperatorSettings(InSettings)
			, NumChannels(AudioInput->NumChannels())
		{
			check(AudioOutput->NumChannels() == NumChannels);
			check(AudioOutput->NumFrames() == OperatorSettings.GetNumFramesPerBlock());

			UpdateCatFormat();
		}

		virtual ~FCatLadderFilterOperator() override = default;

		void UpdateCatFormat()
		{
			NumChannels = AudioInput->NumChannels();
			InitChannelState();

			if (AudioOutput->GetTypeName() != AudioInput->GetTypeName())
			{
				AudioOutput = TDataWriteReferenceFactory<FChannelAgnosticType>::CreateAny(OperatorSettings, AudioInput->GetTypeName());
			}
			check(AudioInput->NumChannels() == AudioOutput->NumChannels());
		}

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace CatLadderFilterVertexNames;
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(ParamAudioInput), AudioInput);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(ParamCutoffFrequency), Frequency);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(ParamResonance), Resonance);

			// Update after bind in case input format changed. 
			UpdateCatFormat();
		}

		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override
		{
			using namespace CatLadderFilterVertexNames;
			InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(ParamAudioOutput), AudioOutput);
		}

		void Reset(const IOperator::FResetParams& InParams)
		{
			// Sample rate may differ from construction time (cached operators can
			// be revived after an audio-device change), so refresh cached settings
			// from the incoming params before re-initializing filters.
			OperatorSettings = InParams.OperatorSettings;

			PreviousFrequency = InvalidValue;
			PreviousResonance = InvalidValue;
			AudioOutput->Zero();

			InitChannelState();
		}

		void Execute()
		{
			// Clamp below Nyquist to avoid FastTan(pi/2) divide-by-zero in the
			// FLadderFilter coefficient update (the tan warp blows up at the
			// Nyquist corner and poisons filter state with NaN forever). The
			// 0.499 factor mirrors the ~50 Hz safety margin the legacy mono
			// ladder node uses at 48 kHz.
			const float MaxCutoffFrequency = 0.499f * OperatorSettings.GetSampleRate();
			const float CurrentFrequency = FMath::Clamp(*Frequency, 0.f, MaxCutoffFrequency);
			const float CurrentResonance = FMath::Clamp(*Resonance, 1.0f, 10.0f);

			const bool bNeedsUpdate =
				!FMath::IsNearlyEqual(PreviousFrequency, CurrentFrequency)
				|| !FMath::IsNearlyEqual(PreviousResonance, CurrentResonance);

			if (bNeedsUpdate)
			{
				// Coefficients are data-identical across channels (same cutoff +
				// resonance); replicating the SetQ/SetFrequency/Update call is
				// cheap compared to per-sample work.
				for (Audio::FLadderFilter& Filter : PerChannelFilters)
				{
					Filter.SetQ(CurrentResonance);
					Filter.SetFrequency(CurrentFrequency);
					Filter.Update();
				}

				PreviousFrequency = CurrentFrequency;
				PreviousResonance = CurrentResonance;
			}

			// Planar per-channel processing. Each channel view into the CAT is a
			// contiguous mono buffer, so we can invoke the single-buffer
			// ProcessAudio form on each per-channel filter with zero copies.
			for (int32 ChannelIdx = 0; ChannelIdx < NumChannels; ++ChannelIdx)
			{
				const TArrayView<const float> InChannel = AudioInput->GetChannel(ChannelIdx);
				const TArrayView<float> OutChannel = AudioOutput->GetChannel(ChannelIdx);
				check(InChannel.Num() == OperatorSettings.GetNumFramesPerBlock());
				check(OutChannel.Num() == OperatorSettings.GetNumFramesPerBlock());
				PerChannelFilters[ChannelIdx].ProcessAudio(InChannel.GetData(), OperatorSettings.GetNumFramesPerBlock(), OutChannel.GetData());
			}
		}

		static const FVertexInterface& DeclareVertexInterface()
		{
			auto CreateDefaultInterface = []() -> FVertexInterface
			{
				using namespace CatLadderFilterVertexNames;

				const FInputVertexInterface InputInterface(
					TInputDataVertex<FChannelAgnosticType>(METASOUND_GET_PARAM_NAME_AND_METADATA(ParamAudioInput)),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(ParamCutoffFrequency), 20000.f),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(ParamResonance), 1.f)
				);

				const FOutputVertexInterface OutputInterface(
					TOutputDataVertex<FChannelAgnosticType>(METASOUND_GET_PARAM_NAME_AND_METADATA(ParamAudioOutput))
				);

				return FVertexInterface(InputInterface, OutputInterface);
			};

			static const FVertexInterface Interface = CreateDefaultInterface();
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { TEXT("Experimental"), TEXT("Ladder Filter"), TEXT("Audio") };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = METASOUND_LOCTEXT("Metasound_CatLadderFilterNodeDisplayName", "Ladder Filter (Audio)");
				Info.Description = METASOUND_LOCTEXT("Metasound_CatLadderFilterNodeDescription", "Multichannel audio ladder (Moog-style) low-pass filter with per-channel state.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = METASOUND_LOCTEXT("Metasound_CatLadderFilterPromptIfMissing", "Enable the MetaSoundExperimental Plugin");
				Info.DefaultInterface = DeclareVertexInterface();
				Info.CategoryHierarchy.Emplace(NodeCategories::Filters);
				Info.Keywords =
				{
					METASOUND_LOCTEXT("CatLadderFilter_FilterKeyword",  "filter"),
					METASOUND_LOCTEXT("CatLadderFilter_LadderKeyword",  "ladder"),
					METASOUND_LOCTEXT("CatLadderFilter_LowpassKeyword", "lowpass"),
					METASOUND_LOCTEXT("CatLadderFilter_MoogKeyword",    "moog"),
					METASOUND_LOCTEXT("CatLadderFilter_AudioKeyword",   "audio")
				};
				return Info;
			};

			static const FNodeClassMetadata Info = InitNodeInfo();
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			using namespace CatLadderFilterVertexNames;

			const FOperatorSettings& Settings = InParams.OperatorSettings;
			const FInputVertexInterfaceData& InputData = InParams.InputData;

			FChannelAgnosticTypeReadRef AudioIn = InputData.GetOrCreateDefaultDataReadReference<FChannelAgnosticType>(
				METASOUND_GET_PARAM_NAME(ParamAudioInput), Settings);
			FFloatReadRef FrequencyIn = InputData.GetOrCreateDefaultDataReadReference<float>(
				METASOUND_GET_PARAM_NAME(ParamCutoffFrequency), Settings);
			FFloatReadRef ResonanceIn = InputData.GetOrCreateDefaultDataReadReference<float>(
				METASOUND_GET_PARAM_NAME(ParamResonance), Settings);

			FChannelAgnosticTypeWriteRef AudioOut = TDataWriteReferenceFactory<FChannelAgnosticType>::CreateAny(
				Settings, AudioIn->GetTypeName());

			return MakeUnique<FCatLadderFilterOperator>(
				Settings,
				MoveTemp(AudioIn),
				MoveTemp(FrequencyIn),
				MoveTemp(ResonanceIn),
				MoveTemp(AudioOut));
		}

	private:
		// Input pins
		FChannelAgnosticTypeReadRef AudioInput;
		FFloatReadRef Frequency;
		FFloatReadRef Resonance;

		// Output pin
		FChannelAgnosticTypeWriteRef AudioOutput;

		// Cached state used to detect control changes between blocks.
		float PreviousFrequency { InvalidValue };
		float PreviousResonance { InvalidValue };

		FOperatorSettings OperatorSettings;
		int32 NumChannels;

		// One mono filter instance per input channel.
		TArray<Audio::FLadderFilter> PerChannelFilters;

		void InitChannelState()
		{
			PerChannelFilters.Reset();
			if (NumChannels > 0)
			{
				PerChannelFilters.SetNum(NumChannels);
				for (Audio::FLadderFilter& Filter : PerChannelFilters)
				{
					Filter.Init(OperatorSettings.GetSampleRate(), /*NumChannels=*/1);
				}
			}
		}
	};

	using FCatLadderFilterNode = TNodeFacade<FCatLadderFilterOperator>;
	METASOUND_REGISTER_NODE(FCatLadderFilterNode);

} // namespace Metasound

PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS

#undef LOCTEXT_NAMESPACE
