// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChannelAgnostic/ChannelAgnosticTypeUtils.h"
#include "DSP/FloatArrayMath.h"
#include "MetasoundBuildError.h"
#include "MetasoundChannelAgnosticType.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundVertex.h"
#include "TypeFamily/ChannelTypeFamily.h"

#define LOCTEXT_NAMESPACE "MetasoundExperimental_CatMathNodes"

PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS

namespace Metasound
{
	namespace CatMathPrivate
	{
		METASOUND_PARAM(InputOperand,  "In",   "Audio input.")
		METASOUND_PARAM(GainOperand,   "Gain", "Float gain applied to the input. Interpolated across the render block on value change.")
		METASOUND_PARAM(SecondInput,   "In 2", "Second audio input. Multiplied sample-by-sample against In; a mono operand broadcasts to the other operand's channel count.")
		METASOUND_PARAM(OutCat,        "Out",  "Result.")

		static const TArray<FText>& GetMultiplyCatKeywords()
		{
			static const TArray<FText> Keywords =
			{
				METASOUND_LOCTEXT("MultiplyCatKeyword_Star",   "*"),
				METASOUND_LOCTEXT("MultiplyCatKeyword_Mult",   "multiply"),
				METASOUND_LOCTEXT("MultiplyCatKeyword_Audio",  "audio"),
				METASOUND_LOCTEXT("MultiplyCatKeyword_Multi",  "multichannel"),
			};
			return Keywords;
		}

		static const TArray<FText>& GetMultiplyCatByFloatKeywords()
		{
			static const TArray<FText> Keywords =
			{
				METASOUND_LOCTEXT("MultiplyCatByFloatKeyword_Star",  "*"),
				METASOUND_LOCTEXT("MultiplyCatByFloatKeyword_Gain",  "gain"),
				METASOUND_LOCTEXT("MultiplyCatByFloatKeyword_Scale", "scale"),
				METASOUND_LOCTEXT("MultiplyCatByFloatKeyword_Vca",   "vca"),
				METASOUND_LOCTEXT("MultiplyCatByFloatKeyword_Audio", "audio"),
			};
			return Keywords;
		}

		static FChannelAgnosticTypeWriteRef CreateOutputForInput(const FChannelAgnosticType& InPrimary, const FOperatorSettings& InSettings)
		{
			return FChannelAgnosticTypeWriteRef::CreateNew(InSettings, InPrimary.GetTypeName());
		}

		static void CopyChannels(const FChannelAgnosticType& InSrc, FChannelAgnosticType& InDst)
		{
			const int32 NumChannels = FMath::Min(InSrc.NumChannels(), InDst.NumChannels());
			for (int32 ChIdx = 0; ChIdx < NumChannels; ++ChIdx)
			{
				TArrayView<const float> Src = InSrc.GetChannel(ChIdx);
				TArrayView<float> Dst = InDst.GetChannel(ChIdx);
				const int32 NumSamples = FMath::Min(Src.Num(), Dst.Num());
				FMemory::Memcpy(Dst.GetData(), Src.GetData(), NumSamples * sizeof(float));
			}
		}

		static const FName CatMismatchErrorType = TEXT("CatMultiplyChannelMismatch");
	} // namespace CatMathPrivate

	// -----------------------------------------------------------------------
	// FCatMultiplyByFloatOperator — CAT x Float (scalar, interpolated).
	// -----------------------------------------------------------------------
	class FCatMultiplyByFloatOperator final : public TExecutableOperator<FCatMultiplyByFloatOperator>
	{
	public:
		FCatMultiplyByFloatOperator(
			const FBuildOperatorParams& InParams,
			FChannelAgnosticTypeReadRef&& InInput,
			FFloatReadRef&& InGain)
			: InputOperand(MoveTemp(InInput))
			, Gain(MoveTemp(InGain))
			, Output(CatMathPrivate::CreateOutputForInput(*InputOperand, InParams.OperatorSettings))
			, Settings(InParams.OperatorSettings)
		{
			UpdateCatFormat();
		}

		virtual ~FCatMultiplyByFloatOperator() override = default;

		void UpdateCatFormat()
		{
			// Reallocate Output if the input format changed since construction (e.g. an
			// upstream node was reconfigured between operator build and bind, or the
			// graph rebound to a different concrete CAT format).
			if (Output->GetTypeName() != InputOperand->GetTypeName())
			{
				Output = TDataWriteReferenceFactory<FChannelAgnosticType>::CreateAny(Settings, InputOperand->GetTypeName());
			}
		}

		static const FVertexInterface& GetVertexInterface()
		{
			using namespace CatMathPrivate;

			auto MakeInterface = []()
			{
				static const FDataVertexMetadata InputMetadata
				{
					  METASOUND_LOCTEXT("CatMultiplyByFloat_InputTooltip", "Audio multiplicand. Output format matches this input.")
					, METASOUND_GET_PARAM_DISPLAYNAME(InputOperand)
				};

				static const FDataVertexMetadata OutputMetadata
				{
					  METASOUND_LOCTEXT("CatMultiplyByFloat_OutputTooltip", "Audio result. Format matches the input.")
					, METASOUND_GET_PARAM_DISPLAYNAME(OutCat)
				};

				FInputVertexInterface InputInterface;
				InputInterface.Add(TInputDataVertex<FChannelAgnosticType>(METASOUND_GET_PARAM_NAME(InputOperand), InputMetadata));
				InputInterface.Add(TInputDataVertex<float>(METASOUND_GET_PARAM_NAME(GainOperand), FDataVertexMetadata{METASOUND_GET_PARAM_TT(GainOperand)}, 1.0f));

				FOutputVertexInterface OutputInterface;
				OutputInterface.Add(TOutputDataVertex<FChannelAgnosticType>(METASOUND_GET_PARAM_NAME(OutCat), OutputMetadata));

				return FVertexInterface(MoveTemp(InputInterface), MoveTemp(OutputInterface));
			};
			static const FVertexInterface Interface = MakeInterface();
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto Init = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = FNodeClassName{ "Experimental", TEXT("Multiply"), TEXT("Audio by Float") };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = METASOUND_LOCTEXT("CatMultiplyByFloat_DisplayName", "Multiply (Audio by Float)");
				Info.Description = METASOUND_LOCTEXT("CatMultiplyByFloat_Description", "Multiplies an audio signal by a float gain. Gain is applied per-channel and interpolated across the render block on value change.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy = { NodeCategories::Math };
				Info.Keywords = CatMathPrivate::GetMultiplyCatByFloatKeywords();
				return Info;
			};
			static const FNodeClassMetadata Info = Init();
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			using namespace CatMathPrivate;

			FChannelAgnosticTypeReadRef Input = InParams.InputData.GetOrCreateDefaultDataReadReference<FChannelAgnosticType>(
				METASOUND_GET_PARAM_NAME(InputOperand), InParams.OperatorSettings);

			FFloatReadRef Gain = InParams.InputData.GetOrCreateDefaultDataReadReference<float>(
				METASOUND_GET_PARAM_NAME(GainOperand), InParams.OperatorSettings);

			return MakeUnique<FCatMultiplyByFloatOperator>(InParams, MoveTemp(Input), MoveTemp(Gain));
		}

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace CatMathPrivate;
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputOperand), InputOperand);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(GainOperand),  Gain);

			// Update after bind in case input format changed.
			UpdateCatFormat();
		}

		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override
		{
			using namespace CatMathPrivate;
			InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutCat), Output);
		}

		void Reset(const FResetParams&)
		{
			Output->Zero();
			PrevGain = 1.0f;
			bInitialized = false;
		}

		void Execute()
		{
			constexpr float MaxGain = TNumericLimits<float>::Max() / 1024.f;
			constexpr float MinGain = -MaxGain;

			const int32 NumChannels = Output->NumChannels();
			check(NumChannels == InputOperand->NumChannels());

			const float RawGain = *Gain;

			if (!bInitialized)
			{
				bInitialized = true;
				// NaN / Inf guard on first block: keep the default 1.0f if upstream is
				// non-finite so the interpolator doesn't poison subsequent blocks.
				PrevGain = FMath::IsFinite(RawGain) ? FMath::Clamp(RawGain, MinGain, MaxGain) : 1.0f;
			}

			const float TargetGain = FMath::IsFinite(RawGain) ? FMath::Clamp(RawGain, MinGain, MaxGain) : PrevGain;
			const float PrevGainValue = PrevGain;

			for (int32 ChIdx = 0; ChIdx < NumChannels; ++ChIdx)
			{
				TArrayView<const float> Src = InputOperand->GetChannel(ChIdx);
				TArrayView<float> Dst = Output->GetChannel(ChIdx);
				const int32 NumSamples = FMath::Min(Src.Num(), Dst.Num());
				Audio::ArrayFade(
					TArrayView<const float>(Src.GetData(), NumSamples),
					PrevGainValue,
					TargetGain,
					TArrayView<float>(Dst.GetData(), NumSamples));
			}

			PrevGain = TargetGain;
		}

	private:
		FChannelAgnosticTypeReadRef InputOperand;
		FFloatReadRef Gain;
		FChannelAgnosticTypeWriteRef Output;
		FOperatorSettings Settings;
		float PrevGain = 1.0f;
		bool bInitialized = false;
	};

	using FCatMultiplyByFloatNode = TNodeFacade<FCatMultiplyByFloatOperator>;

	// -----------------------------------------------------------------------
	// FCatMultiplyOperator — CAT x CAT (sample-by-sample, with mono broadcast).
	// -----------------------------------------------------------------------
	class FCatMultiplyOperator final : public TExecutableOperator<FCatMultiplyOperator>
	{
	public:
		FCatMultiplyOperator(
			const FBuildOperatorParams& InParams,
			FChannelAgnosticTypeReadRef&& InPrimary,
			FChannelAgnosticTypeReadRef&& InSecond)
			: PrimaryOperand(MoveTemp(InPrimary))
			, SecondOperand(MoveTemp(InSecond))
			, Output(CatMathPrivate::CreateOutputForInput(*PrimaryOperand, InParams.OperatorSettings))
			, Settings(InParams.OperatorSettings)
		{
			UpdateCatFormat();
		}

		virtual ~FCatMultiplyOperator() override = default;

		void UpdateCatFormat()
		{
			// Output format basis is the operand with more channels — multiplication
			// is commutative, so mono x stereo and stereo x mono both produce stereo.
			// Ties favor the primary operand to keep type-name behavior stable when
			// channel counts match.
			const FChannelAgnosticType& FormatBasis =
				(SecondOperand->NumChannels() > PrimaryOperand->NumChannels())
					? *SecondOperand
					: *PrimaryOperand;

			if (Output->GetTypeName() != FormatBasis.GetTypeName())
			{
				Output = TDataWriteReferenceFactory<FChannelAgnosticType>::CreateAny(Settings, FormatBasis.GetTypeName());
			}

			// Re-evaluate broadcast strategy and channel-mismatch state against the
			// currently-bound operand channel counts. Runtime channel-count changes
			// can flip strategy (e.g. SameChannels ↔ BroadcastSecondMono) or move
			// in/out of the fail-safe bBindError NOP path.
			EvaluateStrategy();
		}

		static const FVertexInterface& GetVertexInterface()
		{
			using namespace CatMathPrivate;

			auto MakeInterface = []()
			{
				static const FDataVertexMetadata PrimaryMetadata
				{
					  METASOUND_LOCTEXT("CatMultiplyCat_PrimaryTooltip", "Audio operand. Multiplied sample-by-sample against In 2; if it is mono and In 2 is multichannel, it broadcasts to In 2's channel count.")
					, METASOUND_GET_PARAM_DISPLAYNAME(InputOperand)
				};

				static const FDataVertexMetadata SecondMetadata
				{
					  METASOUND_LOCTEXT("CatMultiplyCat_SecondTooltip", "Audio operand. Multiplied sample-by-sample against In; if it is mono and In is multichannel, it broadcasts to In's channel count.")
					, METASOUND_GET_PARAM_DISPLAYNAME(SecondInput)
				};

				static const FDataVertexMetadata OutputMetadata
				{
					  METASOUND_LOCTEXT("CatMultiplyCat_OutputTooltip", "Sample-wise product. Output format follows the operand with more channels (mono x stereo and stereo x mono both produce stereo).")
					, METASOUND_GET_PARAM_DISPLAYNAME(OutCat)
				};

				FInputVertexInterface InputInterface;
				InputInterface.Add(TInputDataVertex<FChannelAgnosticType>(METASOUND_GET_PARAM_NAME(InputOperand), PrimaryMetadata));
				InputInterface.Add(TInputDataVertex<FChannelAgnosticType>(METASOUND_GET_PARAM_NAME(SecondInput),  SecondMetadata));

				FOutputVertexInterface OutputInterface;
				OutputInterface.Add(TOutputDataVertex<FChannelAgnosticType>(METASOUND_GET_PARAM_NAME(OutCat), OutputMetadata));

				return FVertexInterface(MoveTemp(InputInterface), MoveTemp(OutputInterface));
			};
			static const FVertexInterface Interface = MakeInterface();
			return Interface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto Init = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = FNodeClassName{ "Experimental", TEXT("Multiply"), TEXT("Audio") };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = METASOUND_LOCTEXT("CatMultiplyCat_DisplayName", "Multiply (Audio)");
				Info.Description = METASOUND_LOCTEXT("CatMultiplyCat_Description", "Multiplies two audio signals sample-by-sample. Output format follows the operand with more channels — mono x stereo and stereo x mono both produce stereo. Mono operands broadcast to the multichannel operand's channel count; other channel-count mismatches produce a bind-time error.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy = { NodeCategories::Math };
				Info.Keywords = CatMathPrivate::GetMultiplyCatKeywords();
				return Info;
			};
			static const FNodeClassMetadata Info = Init();
			return Info;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults)
		{
			using namespace CatMathPrivate;

			FChannelAgnosticTypeReadRef Primary = InParams.InputData.GetOrCreateDefaultDataReadReference<FChannelAgnosticType>(
				METASOUND_GET_PARAM_NAME(InputOperand), InParams.OperatorSettings);

			FChannelAgnosticTypeReadRef Second = InParams.InputData.GetOrCreateDefaultDataReadReference<FChannelAgnosticType>(
				METASOUND_GET_PARAM_NAME(SecondInput), InParams.OperatorSettings);

			const int32 PrimaryChannels = Primary->NumChannels();
			const int32 SecondChannels = Second->NumChannels();
			const bool bIsBroadcastable = (SecondChannels == PrimaryChannels) || (SecondChannels == 1) || (PrimaryChannels == 1);
			if (!bIsBroadcastable)
			{
				AddBuildError<FBuildErrorBase>(
					OutResults.Errors,
					CatMismatchErrorType,
					FText::Format(
						METASOUND_LOCTEXT("CatMultiplyCat_ChannelMismatchError",
							"Multiply (Audio): cannot multiply operands with {0} and {1} channels — channel counts differ and neither side is mono. Insert an audio Cast node upstream to match channel counts."),
						FText::AsNumber(PrimaryChannels),
						FText::AsNumber(SecondChannels)));
			}

			return MakeUnique<FCatMultiplyOperator>(InParams, MoveTemp(Primary), MoveTemp(Second));
		}

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace CatMathPrivate;
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputOperand), PrimaryOperand);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(SecondInput),  SecondOperand);

			// Update after bind in case input format / channel counts changed.
			UpdateCatFormat();
		}

		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override
		{
			using namespace CatMathPrivate;
			InOutVertexData.BindWriteVertex(METASOUND_GET_PARAM_NAME(OutCat), Output);
		}

		void Reset(const FResetParams&)
		{
			Output->Zero();
		}

		void Execute()
		{
			if (bBindError)
			{
				// Fail-safe NOP: pass primary through unchanged.
				CatMathPrivate::CopyChannels(*PrimaryOperand, *Output);
				return;
			}

			const FChannelAgnosticType& Primary = *PrimaryOperand;
			const FChannelAgnosticType& Second  = *SecondOperand;
			const int32 NumOutputChannels = Output->NumChannels();

			auto MultiplyInto = [](TArrayView<const float> A, TArrayView<const float> B, TArrayView<float> Dst)
			{
				const int32 NumSamples = FMath::Min3(A.Num(), B.Num(), Dst.Num());
				Audio::ArrayMultiply(
					TArrayView<const float>(A.GetData(), NumSamples),
					TArrayView<const float>(B.GetData(), NumSamples),
					TArrayView<float>(Dst.GetData(), NumSamples));
			};

			switch (Strategy)
			{
			case EOperandStrategy::BroadcastSecondMono:
			{
				// Primary is multichannel, Second is mono — broadcast Second across primary channels.
				TArrayView<const float> SecondMono = Second.GetChannel(0);
				for (int32 ChIdx = 0; ChIdx < NumOutputChannels; ++ChIdx)
				{
					MultiplyInto(Primary.GetChannel(ChIdx), SecondMono, Output->GetChannel(ChIdx));
				}
				break;
			}
			case EOperandStrategy::BroadcastPrimaryMono:
			{
				// Primary is mono, Second is multichannel — broadcast Primary across second channels.
				TArrayView<const float> PrimaryMono = Primary.GetChannel(0);
				for (int32 ChIdx = 0; ChIdx < NumOutputChannels; ++ChIdx)
				{
					MultiplyInto(PrimaryMono, Second.GetChannel(ChIdx), Output->GetChannel(ChIdx));
				}
				break;
			}
			case EOperandStrategy::SameChannels:
			default:
			{
				for (int32 ChIdx = 0; ChIdx < NumOutputChannels; ++ChIdx)
				{
					MultiplyInto(Primary.GetChannel(ChIdx), Second.GetChannel(ChIdx), Output->GetChannel(ChIdx));
				}
				break;
			}
			}
		}

	private:
		enum class EOperandStrategy : uint8
		{
			SameChannels,
			BroadcastSecondMono,    // Primary is multichannel, Second is mono.
			BroadcastPrimaryMono,   // Primary is mono, Second is multichannel.
		};

		void EvaluateStrategy()
		{
			const int32 PrimaryChannels = PrimaryOperand->NumChannels();
			const int32 SecondChannels = SecondOperand->NumChannels();
			if (SecondChannels == PrimaryChannels)
			{
				Strategy = EOperandStrategy::SameChannels;
				bBindError = false;
			}
			else if (SecondChannels == 1)
			{
				Strategy = EOperandStrategy::BroadcastSecondMono;
				bBindError = false;
			}
			else if (PrimaryChannels == 1)
			{
				Strategy = EOperandStrategy::BroadcastPrimaryMono;
				bBindError = false;
			}
			else
			{
				// Channel counts differ and neither side is mono — Execute will
				// fail-safe to passing the primary through unchanged.
				bBindError = true;
				UE_LOGF(LogMetaSound, Warning, "Could not determine how to align channels for multiplication with channel counts %d and %d", PrimaryChannels, SecondChannels);
			}
		}

		FChannelAgnosticTypeReadRef PrimaryOperand;
		FChannelAgnosticTypeReadRef SecondOperand;
		EOperandStrategy Strategy = EOperandStrategy::SameChannels;
		FChannelAgnosticTypeWriteRef Output;
		FOperatorSettings Settings;
		bool bBindError = false;
	};

	using FCatMultiplyNode = TNodeFacade<FCatMultiplyOperator>;

	METASOUND_REGISTER_NODE(FCatMultiplyNode)
	METASOUND_REGISTER_NODE(FCatMultiplyByFloatNode)

} // namespace Metasound

PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS

#undef LOCTEXT_NAMESPACE
