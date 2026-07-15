// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/HyprsenseRealtimeNode.h"

#include "Pipeline/Log.h"
#include "CoreUtils.h"
#include "Interfaces/IPluginManager.h"

#include "UObject/Package.h"
#include "Math/TransformCalculus2D.h"

#include "NNE.h"
#include "NNETypes.h"
#include "NNEModelData.h"
#include "NNERuntimeGPU.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeNPU.h"

#include "GuiToRawControlsUtils.h"
#include "OpenCVHelperLocal.h"

#ifdef USE_OPENCV
#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include "ThirdParty/OpenCV/include/opencv2/imgproc.hpp"
#include "PostOpenCVHeaders.h"
#endif

//#define PRINT_RESULTS

#include UE_INLINE_GENERATED_CPP_BY_NAME(HyprsenseRealtimeNode)

TAutoConsoleVariable<FString> CVarMetaHumanRealtimeVideoBackend
{
	TEXT("mh.RealtimeVideo.Backend"),
#if PLATFORM_WINDOWS
	"NNERuntimeORTDml",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTDml\" or \"NNERuntimeORTCpu\""),
#elif PLATFORM_LINUX
	"NNERuntimeORTCpu",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTCpu\""),
#elif PLATFORM_APPLE
	"NNERuntimeCoreML",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeCoreML\" or \"NNERuntimeORTCpu\""),
#else // Console
	"NNERuntimeORTCpu",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTCpu\""),
#endif
	ECVF_Default
};



FMonocularAnimationPipelineModels::FMonocularAnimationPipelineModels()
{
	NNEBackend = CVarMetaHumanRealtimeVideoBackend.GetValueOnAnyThread();
	SetModelConfiguration(EHyprsenseRealtimeNodeModelConfiguration::Default);
}

bool FMonocularAnimationPipelineModels::IsValid() const
{
	return FaceDetector.IsValid() && FaceHeadPoseTracker.IsValid() && FaceSolver.IsValid();
}

void FMonocularAnimationPipelineModels::SetModelConfiguration(EHyprsenseRealtimeNodeModelConfiguration InModelConfiguration)
{
	ModelConfiguration = InModelConfiguration;

	const FString ContentRoot = (NNEBackend == TEXT("NNERuntimeCoreML")) ? TEXT("/MetaHumanCoreML") : TEXT("/MetaHumanCoreTech");
	const FString RealtimeMonoRoot = ContentRoot + TEXT("/RealtimeMono");

	FaceHeadPoseTracker = RealtimeMonoRoot + TEXT("/HeadPoseTracker.HeadPoseTracker");

	bool bFoundBodyTrackerPlugin = false;
	const FString MetaHumanBodyPluginContentDir = TEXT("/MetaHumanBodyTracker");;
	if (TSharedPtr<IPlugin> BodyPlugin = IPluginManager::Get().FindPlugin(TEXT("MetaHumanBodyTracker")))
	{
		bFoundBodyTrackerPlugin = true;
	}

	switch (ModelConfiguration)
	{
	case EHyprsenseRealtimeNodeModelConfiguration::Default:
		FaceDetector = ContentRoot + TEXT("/GenericTracker/FaceDetector.FaceDetector");
		FaceSolver = RealtimeMonoRoot + TEXT("/GenericRigSolver.GenericRigSolver");
		break;

	case EHyprsenseRealtimeNodeModelConfiguration::StereoHMC:
		FaceDetector = ContentRoot + TEXT("/GenericTracker/FaceDetector.FaceDetector");
		FaceSolver = RealtimeMonoRoot + TEXT("/StereoHMCRigSolver.StereoHMCRigSolver");
		break;

	case EHyprsenseRealtimeNodeModelConfiguration::SmallFace:
		if (bFoundBodyTrackerPlugin)
		{
			FaceDetector = MetaHumanBodyPluginContentDir + TEXT("/FaceModels/SmallFaceDetector.SmallFaceDetector");
			FaceSolver = MetaHumanBodyPluginContentDir + TEXT("/FaceModels/SmallFaceSolver.SmallFaceSolver");
		}
		else
		{
			UE_LOGF(LogMetaHumanPipeline, Warning, "MetaHumanBodyTracker plugin not found; using default face model configuration");
			FaceDetector = ContentRoot + TEXT("/GenericTracker/FaceDetector.FaceDetector");
			FaceSolver = RealtimeMonoRoot + TEXT("/GenericRigSolver.GenericRigSolver");
		}
		break;

	default:
		UE_LOGF(LogMetaHumanPipeline, Error, "Unknown realtime model configuration");
		break;
	}
}


namespace UE::MetaHuman::Pipeline
{

// Logical output role names — constant across backends. Used as keys in FModelVariant maps
// and for looking up resolved tensor descriptions in Process().
namespace OutputRole
{
	static const FString Scores    = TEXT("Scores");
	static const FString Boxes     = TEXT("Boxes");
	static const FString Landmarks = TEXT("Landmarks");
	static const FString Score     = TEXT("Score");
	static const FString HeadPose  = TEXT("HeadPose");
	static const FString Joints    = TEXT("Joints");
	static const FString Controls  = TEXT("Controls");
}

// Active control flags for CoreML solver variants. Each array has 174 entries matching
// SolverControlNames. true = control present in compact output, false = zeroed on expansion.
// ONNX solvers output all 174 controls directly and do not need expansion flags.
namespace CoreMLActiveControlFlags
{
	// GenericRigSolver — 105 active controls
	static const bool GenericRigSolver[174] = {
		true,  true,  true,  true,  true,  true,  true,  true,  false, false, // brow
		true,  true,  false, false, true,  true,  true,  true,  true,  true,  // eye blink..faceScrunch
		false, false, false, false, true,  true,  true,  true,  false, false, // eyelid..eye
		false, false, false, false, false, true,  true,  true,  true,  false, // eyelashes..nose
		false, true,  true,  true,  true,  false, false, false, true,  true,  // nasolabial..mouth
		true,  true,  true,  true,  true,  true,  true,  false, false, true,  // upperLip..dimple
		true,  true,  true,  false, false, false, false, true,  true,  true,  // cornerDepress..purse
		true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  // towards..lipsTogether
		true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  // lipBite..tighten
		true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  // lipsPress..stickyOuter
		true,  true,  true,  true,  true,  false, false, true,  true,  true,  // stickyD..pushPull
		true,  true,  true,  true,  true,  false, false, false, false, true,  // thickness..cornerSharpness
		true,  true,  true,  false, false, false, false, true,  true,  true,  // lipsTowardsTeeth..lipsRoll
		true,  true,  true,  true,  false, true,  false, false, false, false, // corner..tongue
		false, false, false, false, false, false, false, false, true,  true,  // tongue..jaw
		true,  false, false, false, false, true,  true,  false, false, true,  // jaw..jawOpen
		false, false, false, false, false, false, false, false, false, false, // neck
		false, false, false, false,                                           // teeth
	};
	static_assert(UE_ARRAY_COUNT(GenericRigSolver) == 174);

	// StereoHMCRigSolver — 115 active controls
	static const bool StereoHMCRigSolver[174] = {
		true,  true,  true,  true,  true,  true,  true,  true,  false, false, // brow
		true,  true,  false, false, true,  true,  true,  true,  true,  true,  // eye blink..faceScrunch
		false, false, false, false, true,  true,  true,  true,  false, false, // eyelid..eye
		false, false, false, false, false, true,  true,  true,  true,  true,  // eyelashes..nose
		true,  true,  true,  true,  true,  true,  true,  false, true,  true,  // nasolabial..mouth
		true,  true,  true,  true,  true,  true,  true,  false, false, true,  // upperLip..dimple
		true,  true,  true,  false, false, false, false, true,  true,  true,  // cornerDepress..purse
		true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  // towards..lipsTogether
		true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  // lipBite..tighten
		true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  // lipsPress..stickyOuter
		true,  true,  true,  true,  true,  false, false, true,  true,  true,  // stickyD..pushPull
		true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  // thickness..thicknessInward
		true,  true,  true,  false, false, false, false, true,  true,  true,  // cornerSharpness..lipsRoll
		true,  true,  true,  true,  false, true,  false, false, false, false, // corner..tongue
		false, false, false, false, false, false, false, false, true,  true,  // tongue..jaw
		true,  false, false, false, false, true,  true,  true,  true,  true,  // jaw..jawOpen
		false, false, false, false, false, false, false, false, false, false, // neck
		false, false, false, false,                                           // teeth
	};
	static_assert(UE_ARRAY_COUNT(StereoHMCRigSolver) == 174);
}

// Known model variants per model type. Each variant maps output roles to the actual
// tensor names and shapes used by that backend. First match wins.
namespace ModelVariants
{
	static const TArray<FModelVariant> Detector =
	{
		// CoreML variant
		{
			{ OutputRole::Scores, { TEXT("scores"), UE::NNE::FTensorShape::Make({1, 4212, 2}) } },
			{ OutputRole::Boxes,  { TEXT("boxes"),  UE::NNE::FTensorShape::Make({1, 4212, 4}) } },
		},
		// ONNX variant
		{
			{ OutputRole::Scores, { TEXT("output"), UE::NNE::FTensorShape::Make({1, 4212, 2}) } },
			{ OutputRole::Boxes,  { TEXT("865"),    UE::NNE::FTensorShape::Make({1, 4212, 4}) } },
		},
		// Small faces ONNX variant (46,104 detections)
		{
			{ OutputRole::Scores, { TEXT("output"), UE::NNE::FTensorShape::Make({1, 46104, 2}) } },
			{ OutputRole::Boxes,  { TEXT("873"),    UE::NNE::FTensorShape::Make({1, 46104, 4}) } },
		},
	};

	static const TArray<FModelVariant> Headpose =
	{
		// ONNX variant
		{
			{ OutputRole::Landmarks, { TEXT("landmarks"), UE::NNE::FTensorShape::Make({1, 1573, 2}) } },
			{ OutputRole::Score,     { TEXT("score"),     UE::NNE::FTensorShape::Make({1, 1}) } },
			{ OutputRole::HeadPose,  { TEXT("head_pose"), UE::NNE::FTensorShape::Make({1, 3, 3}) } },
			{ OutputRole::Joints,    { TEXT("joints"),    UE::NNE::FTensorShape::Make({1, 268, 2}) } },
		},
		// CoreML variant (head_pose is 1x6 compact representation)
		{
			{ OutputRole::Landmarks, { TEXT("landmarks"), UE::NNE::FTensorShape::Make({1, 1573, 2}) } },
			{ OutputRole::Score,     { TEXT("score"),     UE::NNE::FTensorShape::Make({1, 1}) } },
			{ OutputRole::HeadPose,  { TEXT("head_pose"), UE::NNE::FTensorShape::Make({1, 6}) } },
			{ OutputRole::Joints,    { TEXT("joints"),    UE::NNE::FTensorShape::Make({1, 268, 2}) } },
		},
	};

	static const TArray<FModelVariant> Solver =
	{
		// ONNX variant (174 controls, no expansion needed)
		{
			{ OutputRole::Controls, { TEXT("controls"), UE::NNE::FTensorShape::Make({1, 174}) } },
		},
		// GenericRigSolver CoreML (105 active controls)
		{
			{ OutputRole::Controls, { TEXT("controls"), UE::NNE::FTensorShape::Make({1, 105}), CoreMLActiveControlFlags::GenericRigSolver } },
		},
		// StereoHMCRigSolver CoreML (115 active controls)
		{
			{ OutputRole::Controls, { TEXT("controls"), UE::NNE::FTensorShape::Make({1, 115}), CoreMLActiveControlFlags::StereoHMCRigSolver } },
		},
	};
}

/**
 * Helper: build a name-to-shape map from a model's output tensor descriptors.
 */
static TMap<FString, UE::NNE::FSymbolicTensorShape> GetOutputShapeMap(const TSharedPtr<UE::NNE::IModelInstanceRunSync>& Model)
{
	TMap<FString, UE::NNE::FSymbolicTensorShape> Map;
	for (const UE::NNE::FTensorDesc& Desc : Model->GetOutputTensorDescs())
	{
		Map.Add(Desc.GetName(), Desc.GetShape());
	}
	return Map;
}

static bool ShapeMatches(const UE::NNE::FSymbolicTensorShape& Actual, const UE::NNE::FTensorShape& Expected)
{
	TConstArrayView<int32> ActualData = Actual.GetData();
	TConstArrayView<uint32> ExpectedData = Expected.GetData();
	if (ActualData.Num() != ExpectedData.Num()) return false;
	for (int32 Index = 0; Index < ActualData.Num(); ++Index)
	{
		if (ActualData[Index] != static_cast<int32>(ExpectedData[Index])) return false;
	}
	return true;
}

/**
 * Find which variant in the given array matches the model's actual output tensors.
 * Returns the index of the first variant where every entry matches by name AND shape,
 * or INDEX_NONE if no variant matches.
 */
static int32 FindMatchingVariant(
	const TSharedPtr<UE::NNE::IModelInstanceRunSync>& Model,
	const TArray<FModelVariant>& Variants)
{
	TMap<FString, UE::NNE::FSymbolicTensorShape> Outputs = GetOutputShapeMap(Model);
	for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
	{
		bool bAllMatch = true;
		for (const TPair<FString, FModelOutputDesc>& Entry : Variants[VariantIndex])
		{
			const UE::NNE::FSymbolicTensorShape* ActualShape = Outputs.Find(Entry.Value.Name);
			if (!ActualShape || !ShapeMatches(*ActualShape, Entry.Value.Shape))
			{
				bAllMatch = false;
				break;
			}
		}
		if (bAllMatch)
		{
			return VariantIndex;
		}
	}
	return INDEX_NONE;
}

/**
 * Build a name→position index map for a model's output tensors.
 * Resolved once at load time, then used every frame to place output buffers at the correct index.
 */
static TMap<FString, int32> BuildOutputIndexMap(const TSharedPtr<UE::NNE::IModelInstanceRunSync>& Model)
{
	TMap<FString, int32> IndexMap;
	TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = Model->GetOutputTensorDescs();
	for (int32 i = 0; i < OutputDescs.Num(); ++i)
	{
		IndexMap.Add(OutputDescs[i].GetName(), i);
	}
	return IndexMap;
}

static const TArray<FString> SolverControlNames = { "CTRL_L_brow_down.ty", "CTRL_R_brow_down.ty", "CTRL_L_brow_lateral.ty", "CTRL_R_brow_lateral.ty", "CTRL_L_brow_raiseIn.ty", "CTRL_R_brow_raiseIn.ty", "CTRL_L_brow_raiseOut.ty", "CTRL_R_brow_raiseOut.ty", "CTRL_L_ear_up.ty", "CTRL_R_ear_up.ty", "CTRL_L_eye_blink.ty", "CTRL_R_eye_blink.ty", "CTRL_L_eye_lidPress.ty", "CTRL_R_eye_lidPress.ty", "CTRL_L_eye_squintInner.ty", "CTRL_R_eye_squintInner.ty", "CTRL_L_eye_cheekRaise.ty", "CTRL_R_eye_cheekRaise.ty", "CTRL_L_eye_faceScrunch.ty", "CTRL_R_eye_faceScrunch.ty", "CTRL_L_eye_eyelidU.ty", "CTRL_R_eye_eyelidU.ty", "CTRL_L_eye_eyelidD.ty", "CTRL_R_eye_eyelidD.ty", "CTRL_L_eye.ty", "CTRL_R_eye.ty", "CTRL_L_eye.tx", "CTRL_R_eye.tx", "CTRL_L_eye_pupil.ty", "CTRL_R_eye_pupil.ty", "CTRL_C_eye_parallelLook.ty", "CTRL_L_eyelashes_tweakerIn.ty", "CTRL_R_eyelashes_tweakerIn.ty", "CTRL_L_eyelashes_tweakerOut.ty", "CTRL_R_eyelashes_tweakerOut.ty", "CTRL_L_nose.ty", "CTRL_R_nose.ty", "CTRL_L_nose.tx", "CTRL_R_nose.tx", "CTRL_L_nose_wrinkleUpper.ty", "CTRL_R_nose_wrinkleUpper.ty", "CTRL_L_nose_nasolabialDeepen.ty", "CTRL_R_nose_nasolabialDeepen.ty", "CTRL_L_mouth_suckBlow.ty", "CTRL_R_mouth_suckBlow.ty", "CTRL_L_mouth_lipsBlow.ty", "CTRL_R_mouth_lipsBlow.ty", "CTRL_C_mouth.ty", "CTRL_C_mouth.tx", "CTRL_L_mouth_upperLipRaise.ty", "CTRL_R_mouth_upperLipRaise.ty", "CTRL_L_mouth_lowerLipDepress.ty", "CTRL_R_mouth_lowerLipDepress.ty", "CTRL_L_mouth_cornerPull.ty", "CTRL_R_mouth_cornerPull.ty", "CTRL_L_mouth_stretch.ty", "CTRL_R_mouth_stretch.ty", "CTRL_L_mouth_stretchLipsClose.ty", "CTRL_R_mouth_stretchLipsClose.ty", "CTRL_L_mouth_dimple.ty", "CTRL_R_mouth_dimple.ty", "CTRL_L_mouth_cornerDepress.ty", "CTRL_R_mouth_cornerDepress.ty", "CTRL_L_mouth_pressU.ty", "CTRL_R_mouth_pressU.ty", "CTRL_L_mouth_pressD.ty", "CTRL_R_mouth_pressD.ty", "CTRL_L_mouth_purseU.ty", "CTRL_R_mouth_purseU.ty", "CTRL_L_mouth_purseD.ty", "CTRL_R_mouth_purseD.ty", "CTRL_L_mouth_towardsU.ty", "CTRL_R_mouth_towardsU.ty", "CTRL_L_mouth_towardsD.ty", "CTRL_R_mouth_towardsD.ty", "CTRL_L_mouth_funnelU.ty", "CTRL_R_mouth_funnelU.ty", "CTRL_L_mouth_funnelD.ty", "CTRL_R_mouth_funnelD.ty", "CTRL_L_mouth_lipsTogetherU.ty", "CTRL_R_mouth_lipsTogetherU.ty", "CTRL_L_mouth_lipsTogetherD.ty", "CTRL_R_mouth_lipsTogetherD.ty", "CTRL_L_mouth_lipBiteU.ty", "CTRL_R_mouth_lipBiteU.ty", "CTRL_L_mouth_lipBiteD.ty", "CTRL_R_mouth_lipBiteD.ty", "CTRL_L_mouth_tightenU.ty", "CTRL_R_mouth_tightenU.ty", "CTRL_L_mouth_tightenD.ty", "CTRL_R_mouth_tightenD.ty", "CTRL_L_mouth_lipsPressU.ty", "CTRL_R_mouth_lipsPressU.ty", "CTRL_L_mouth_sharpCornerPull.ty", "CTRL_R_mouth_sharpCornerPull.ty", "CTRL_C_mouth_stickyU.ty", "CTRL_L_mouth_stickyInnerU.ty", "CTRL_R_mouth_stickyInnerU.ty", "CTRL_L_mouth_stickyOuterU.ty", "CTRL_R_mouth_stickyOuterU.ty", "CTRL_C_mouth_stickyD.ty", "CTRL_L_mouth_stickyInnerD.ty", "CTRL_R_mouth_stickyInnerD.ty", "CTRL_L_mouth_stickyOuterD.ty", "CTRL_R_mouth_stickyOuterD.ty", "CTRL_L_mouth_lipSticky.ty", "CTRL_R_mouth_lipSticky.ty", "CTRL_L_mouth_pushPullU.ty", "CTRL_R_mouth_pushPullU.ty", "CTRL_L_mouth_pushPullD.ty", "CTRL_R_mouth_pushPullD.ty", "CTRL_L_mouth_thicknessU.ty", "CTRL_R_mouth_thicknessU.ty", "CTRL_L_mouth_thicknessD.ty", "CTRL_R_mouth_thicknessD.ty", "CTRL_L_mouth_thicknessInwardU.ty", "CTRL_R_mouth_thicknessInwardU.ty", "CTRL_L_mouth_thicknessInwardD.ty", "CTRL_R_mouth_thicknessInwardD.ty", "CTRL_L_mouth_cornerSharpnessU.ty", "CTRL_R_mouth_cornerSharpnessU.ty", "CTRL_L_mouth_cornerSharpnessD.ty", "CTRL_R_mouth_cornerSharpnessD.ty", "CTRL_L_mouth_lipsTowardsTeethU.ty", "CTRL_R_mouth_lipsTowardsTeethU.ty", "CTRL_L_mouth_lipsTowardsTeethD.ty", "CTRL_R_mouth_lipsTowardsTeethD.ty", "CTRL_C_mouth_lipShiftU.ty", "CTRL_C_mouth_lipShiftD.ty", "CTRL_L_mouth_lipsRollU.ty", "CTRL_R_mouth_lipsRollU.ty", "CTRL_L_mouth_lipsRollD.ty", "CTRL_R_mouth_lipsRollD.ty", "CTRL_L_mouth_corner.ty", "CTRL_L_mouth_corner.tx", "CTRL_R_mouth_corner.ty", "CTRL_R_mouth_corner.tx", "CTRL_C_tongue_inOut.ty", "CTRL_C_tongue_move.ty", "CTRL_C_tongue_move.tx", "CTRL_C_tongue_press.ty", "CTRL_C_tongue_wideNarrow.ty", "CTRL_C_tongue_bendTwist.ty", "CTRL_C_tongue_bendTwist.tx", "CTRL_C_tongue_roll.ty", "CTRL_C_tongue_tipMove.ty", "CTRL_C_tongue_tipMove.tx", "CTRL_C_tongue_thickThin.ty", "CTRL_C_jaw.ty", "CTRL_C_jaw.tx", "CTRL_C_jaw_fwdBack.ty", "CTRL_L_jaw_clench.ty", "CTRL_R_jaw_clench.ty", "CTRL_L_jaw_ChinRaiseU.ty", "CTRL_R_jaw_ChinRaiseU.ty", "CTRL_L_jaw_ChinRaiseD.ty", "CTRL_R_jaw_ChinRaiseD.ty", "CTRL_L_jaw_chinCompress.ty", "CTRL_R_jaw_chinCompress.ty", "CTRL_C_jaw_openExtreme.ty", "CTRL_L_neck_stretch.ty", "CTRL_R_neck_stretch.ty", "CTRL_C_neck_swallow.ty", "CTRL_L_neck_mastoidContract.ty", "CTRL_R_neck_mastoidContract.ty", "CTRL_neck_throatUpDown.ty", "CTRL_neck_digastricUpDown.ty", "CTRL_neck_throatExhaleInhale.ty", "CTRL_C_teethU.ty", "CTRL_C_teethU.tx", "CTRL_C_teeth_fwdBackU.ty", "CTRL_C_teethD.ty", "CTRL_C_teethD.tx", "CTRL_C_teeth_fwdBackD.ty" };

static TArray<float> UEImageToHSImage(int32 InWidth, int32 InHeight, const uint8* InData, bool bInNorm)
{
	TArray<float> Output;
	Output.SetNumUninitialized(InWidth * InHeight * 3);

	const int32 FullSize = InHeight * InWidth;
	const int32 TwiceFullSize = 2 * FullSize;
	int32 OutputIndex = 0;
	const float Sqrt2 = FMath::Sqrt(2.f);
	const float ImageMean = 127.0f;
	const float ImageStd = 128.0f;

	for (int32 Y = 0; Y < InHeight; ++Y)
	{
		for (int32 X = 0; X < InWidth; ++X, ++OutputIndex, InData += 4)
		{
			const float Blue = InData[0];
			const float Green = InData[1];
			const float Red = InData[2];

			if (bInNorm)
			{
				Output[OutputIndex] = (((Red / 255.f) - 0.5) * Sqrt2);
				Output[OutputIndex + FullSize] = (((Green / 255.f) - 0.5) * Sqrt2);
				Output[OutputIndex + TwiceFullSize] = (((Blue / 255.f) - 0.5) * Sqrt2);
			}
			else
			{
				Output[OutputIndex] = (Red - ImageMean) / ImageStd;
				Output[OutputIndex + FullSize] = (Green - ImageMean) / ImageStd;
				Output[OutputIndex + TwiceFullSize] = (Blue - ImageMean) / ImageStd;
			}
		}
	}

	return Output;
}

static TArray<uint8> HSImageToUEImage(int32 InWidth, int32 InHeight, const TArray<float, TAlignedHeapAllocator<64>>& InData, bool bInNorm)
{
	TArray<uint8> Output;
	Output.SetNumUninitialized(InWidth * InHeight * 4);

	const int32 FullSize = InHeight * InWidth;
	const int32 TwiceFullSize = 2 * FullSize;
	int32 InputIndex = 0;
	int32 OutputIndex = 0;
	const float Sqrt2 = FMath::Sqrt(2.f);

	for (int32 Y = 0; Y < InHeight; ++Y)
	{
		for (int32 X = 0; X < InWidth; ++X, OutputIndex += 4, ++InputIndex)
		{
			if (bInNorm)
			{
				Output[OutputIndex] = ((InData[InputIndex + TwiceFullSize] / Sqrt2) + 0.5) * 255;
				Output[OutputIndex + 1] = ((InData[InputIndex + FullSize] / Sqrt2) + 0.5) * 255;
				Output[OutputIndex + 2] = ((InData[InputIndex] / Sqrt2) + 0.5) * 255;
			}
			else
			{
				Output[OutputIndex] = (InData[InputIndex + TwiceFullSize] * 128) + 127;
				Output[OutputIndex + 1] = (InData[InputIndex + FullSize] * 128) + 127;
				Output[OutputIndex + 2] = (InData[InputIndex] * 128) + 127;
			}

			Output[OutputIndex + 3] = 255;
		}
	}

	return Output;
}

#ifdef USE_OPENCV
static cv::Mat EigenToCV(const Eigen::Matrix<float, 2, 3>& InMatrix)
{
	cv::Mat Matrix(2, 3, CV_64FC1);

	for (int32 Row = 0; Row < 2; ++Row)
	{
		for (int32 Col = 0; Col < 3; ++Col)
		{
			Matrix.at<double>(Row, Col) = InMatrix(Row, Col);
		}
	}

	return Matrix;
}
#endif

// Start of head pose estimation code.
// This code is provided by a team who work outside of UE. As such the code does not follow UE coding standards.
// The code may change in future and to ease integrating any changes we are leaving the code in its original form.
// This code is internal to this file.

//  265 vertices, excluding three joints (corresponding to the indices from 1 to 3: two eyes and facial_c)
//  from the 268 'joints' landmark set
constexpr int32_t num_skull_points = 265;

constexpr int32_t num_joint_landmark_points = 268;

const Eigen::Matrix3f coordinate_shifter{ { 1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } };

//  Average skull landmarks
const float skull_mean_shape_in_cm[265][3] = {
	{ 0.0f, 0.0f, 0.0f },
	{ 0.0f, 9.064771f, 10.714114f },
	{ -0.35572574f, 9.080679f, 10.711132f },
	{ -0.7047408f, 9.118496f, 10.695984f },
	{ -1.0408206f, 9.162449f, 10.663597f },
	{ -1.3883348f, 9.212189f, 10.604985f },
	{ -1.7732228f, 9.268808f, 10.508484f },
	{ -2.1940017f, 9.320387f, 10.375083f },
	{ -2.6446958f, 9.350742f, 10.209621f },
	{ -3.1249084f, 9.347136f, 10.009478f },
	{ -3.629829f, 9.3028755f, 9.7717705f },
	{ -4.129295f, 9.224873f, 9.500156f },
	{ -4.5853233f, 9.11475f, 9.202723f },
	{ -4.9580355f, 8.953342f, 8.901176f },
	{ -5.220229f, 8.727922f, 8.622347f },
	{ -5.392555f, 8.456596f, 8.378587f },
	{ -5.5086617f, 8.160261f, 8.170922f },
	{ -5.6075563f, 7.8437386f, 7.988229f },
	{ -5.676009f, 7.501348f, 7.8198385f },
	{ -5.701f, 7.1431437f, 7.683574f },
	{ -5.7117567f, 6.7793913f, 7.6047297f },
	{ -5.7293377f, 6.411813f, 7.5676765f },
	{ -5.770344f, 6.042312f, 7.540431f },
	{ -5.808794f, 5.6755276f, 7.5106544f },
	{ -5.8818693f, 5.3314953f, 7.442479f },
	{ -5.8900604f, 5.0997906f, 7.540354f },
	{ -5.850459f, 4.851129f, 7.687504f },
	{ -5.7870994f, 4.560954f, 7.8344274f },
	{ -5.7324915f, 4.2184925f, 7.9301834f },
	{ -5.6815443f, 3.870152f, 7.9868712f },
	{ -5.6333046f, 3.549659f, 8.015254f },
	{ -5.5926313f, 3.2649684f, 8.019542f },
	{ -5.557212f, 3.0152194f, 8.00358f },
	{ -5.5168014f, 2.8029242f, 7.9714527f },
	{ -5.2935505f, 2.6744585f, 8.127856f },
	{ -5.0189786f, 2.6061401f, 8.257733f },
	{ -4.6955824f, 2.5821397f, 8.341583f },
	{ -4.372637f, 2.5599995f, 8.3238945f },
	{ -4.0780163f, 2.6005778f, 8.351778f },
	{ -3.88686f, 2.5945566f, 8.353683f },
	{ -3.6997824f, 2.560552f, 8.3556385f },
	{ -3.507236f, 2.4456468f, 8.290636f },
	{ -3.3274715f, 2.2612472f, 8.162617f },
	{ -3.1821842f, 2.0699625f, 8.058613f },
	{ -3.0623298f, 1.8707128f, 7.982608f },
	{ -2.9588294f, 1.6626211f, 7.9338894f },
	{ -2.878439f, 1.4493685f, 7.9030886f },
	{ -2.8294172f, 1.2333735f, 7.87984f },
	{ -2.7857738f, 1.0165255f, 7.844682f },
	{ -2.7170043f, 0.81728494f, 7.786527f },
	{ -2.6226134f, 0.65815085f, 7.7157774f },
	{ -2.5723257f, 0.6221302f, 7.963957f },
	{ -2.509824f, 0.5852673f, 8.236382f },
	{ -2.416068f, 0.54535383f, 8.547819f },
	{ -2.2740936f, 0.49588245f, 8.895996f },
	{ -2.0807695f, 0.44075966f, 9.254065f },
	{ -1.8551157f, 0.3948689f, 9.576664f },
	{ -1.6178403f, 0.35759974f, 9.844225f },
	{ -1.3822787f, 0.32201475f, 10.057026f },
	{ -1.1463567f, 0.28440598f, 10.218607f },
	{ -0.89775455f, 0.24438202f, 10.340034f },
	{ -0.6360675f, 0.20680138f, 10.427404f },
	{ -0.3779509f, 0.18347979f, 10.486902f },
	{ -0.12577611f, 0.17622426f, 10.518052f },
	{ 0.12577611f, 0.17622426f, 10.518052f },
	{ 0.3779509f, 0.18347979f, 10.486902f },
	{ 0.6360675f, 0.20680138f, 10.427404f },
	{ 0.89775455f, 0.24438202f, 10.340034f },
	{ 1.1463567f, 0.28440598f, 10.218607f },
	{ 1.3822787f, 0.32201475f, 10.057026f },
	{ 1.6178403f, 0.35759974f, 9.844225f },
	{ 1.8551157f, 0.3948689f, 9.576664f },
	{ 2.0807695f, 0.44075966f, 9.254065f },
	{ 2.2740936f, 0.49588245f, 8.895996f },
	{ 2.416068f, 0.54535383f, 8.547819f },
	{ 2.509824f, 0.5852673f, 8.236382f },
	{ 2.5723257f, 0.6221302f, 7.963957f },
	{ 2.6226134f, 0.65815085f, 7.7157774f },
	{ 2.7170043f, 0.81728494f, 7.786527f },
	{ 2.7857738f, 1.0165255f, 7.844682f },
	{ 2.8294172f, 1.2333735f, 7.87984f },
	{ 2.878439f, 1.4493685f, 7.9030886f },
	{ 2.9588294f, 1.6626211f, 7.9338894f },
	{ 3.0623298f, 1.8707128f, 7.982608f },
	{ 3.1821842f, 2.0699625f, 8.058613f },
	{ 3.3274715f, 2.2612472f, 8.162617f },
	{ 3.507236f, 2.4456468f, 8.290636f },
	{ 3.6997824f, 2.560552f, 8.3556385f },
	{ 3.88686f, 2.5945566f, 8.353683f },
	{ 4.0780163f, 2.6005778f, 8.351778f },
	{ 4.372637f, 2.5599995f, 8.3238945f },
	{ 4.6955824f, 2.5821397f, 8.341583f },
	{ 5.0189786f, 2.6061401f, 8.257733f },
	{ 5.2935505f, 2.6744585f, 8.127856f },
	{ 5.5168014f, 2.8029242f, 7.9714527f },
	{ 5.557212f, 3.0152194f, 8.00358f },
	{ 5.5926313f, 3.2649684f, 8.019542f },
	{ 5.6333046f, 3.549659f, 8.015254f },
	{ 5.6815443f, 3.870152f, 7.9868712f },
	{ 5.7324915f, 4.2184925f, 7.9301834f },
	{ 5.7870994f, 4.560954f, 7.8344274f },
	{ 5.850459f, 4.851129f, 7.687504f },
	{ 5.8900604f, 5.0997906f, 7.540354f },
	{ 5.8818693f, 5.3314953f, 7.442479f },
	{ 5.808794f, 5.6755276f, 7.5106544f },
	{ 5.770344f, 6.042312f, 7.540431f },
	{ 5.7293377f, 6.411813f, 7.5676765f },
	{ 5.7117567f, 6.7793913f, 7.6047297f },
	{ 5.701f, 7.1431437f, 7.683574f },
	{ 5.676009f, 7.501348f, 7.8198385f },
	{ 5.6075563f, 7.8437386f, 7.988229f },
	{ 5.5086617f, 8.160261f, 8.170922f },
	{ 5.392555f, 8.456596f, 8.378587f },
	{ 5.220229f, 8.727922f, 8.622347f },
	{ 4.9580355f, 8.953342f, 8.901176f },
	{ 4.5853233f, 9.11475f, 9.202723f },
	{ 4.129295f, 9.224873f, 9.500156f },
	{ 3.629829f, 9.3028755f, 9.7717705f },
	{ 3.1249084f, 9.347136f, 10.009478f },
	{ 2.6446958f, 9.350742f, 10.209621f },
	{ 2.1940017f, 9.320387f, 10.375083f },
	{ 1.7732228f, 9.268808f, 10.508484f },
	{ 1.3883348f, 9.212189f, 10.604985f },
	{ 1.0408206f, 9.162449f, 10.663597f },
	{ 0.7047408f, 9.118496f, 10.695984f },
	{ 0.35572574f, 9.080679f, 10.711132f },
	{ 0.0f, 8.656193f, 10.753616f },
	{ 0.0f, 8.286388f, 10.707211f },
	{ 0.0f, 7.974279f, 10.603137f },
	{ 0.0f, 7.7060194f, 10.492035f },
	{ 0.0f, 7.465375f, 10.414276f },
	{ 0.0f, 7.2579193f, 10.383489f },
	{ 0.0f, 7.0787296f, 10.409057f },
	{ 0.0f, 6.8568115f, 10.502897f },
	{ 0.0f, 6.535432f, 10.665105f },
	{ 0.0f, 6.194317f, 10.847094f },
	{ 0.0f, 5.927672f, 11.005412f },
	{ 0.0f, 5.4399314f, 11.316417f },
	{ -0.10788158f, 5.4006495f, 11.294765f },
	{ -0.23085795f, 5.290925f, 11.231158f },
	{ -0.36310542f, 5.12276f, 11.131464f },
	{ -0.4975595f, 4.9097185f, 11.007837f },
	{ -0.62657154f, 4.6727533f, 10.874516f },
	{ -0.7626407f, 4.430338f, 10.755653f },
	{ -0.8835659f, 4.2053957f, 10.662713f },
	{ -0.9774412f, 4.0077066f, 10.586503f },
	{ -1.0508511f, 3.8341677f, 10.52065f },
	{ -1.1167439f, 3.6729665f, 10.458325f },
	{ -1.1830264f, 3.5105667f, 10.393387f },
	{ -1.2421821f, 3.327695f, 10.328738f },
	{ -1.2816288f, 3.1129715f, 10.2699585f },
	{ -1.2914957f, 2.891846f, 10.224287f },
	{ -1.2845047f, 2.6779797f, 10.174585f },
	{ -1.184876f, 2.4228315f, 10.117454f },
	{ -0.8812735f, 2.2135143f, 10.191338f },
	{ -0.5612408f, 2.134215f, 10.306191f },
	{ -0.27084506f, 2.0974174f, 10.397005f },
	{ 0.0f, 2.0887902f, 10.434594f },
	{ 0.27084506f, 2.0974174f, 10.397005f },
	{ 0.5612408f, 2.134215f, 10.306191f },
	{ 0.8812735f, 2.2135143f, 10.191338f },
	{ 1.184876f, 2.4228315f, 10.117454f },
	{ 1.2845047f, 2.6779797f, 10.174585f },
	{ 1.2914957f, 2.891846f, 10.224287f },
	{ 1.2816288f, 3.1129715f, 10.2699585f },
	{ 1.2421821f, 3.327695f, 10.328738f },
	{ 1.1830264f, 3.5105667f, 10.393387f },
	{ 1.1167439f, 3.6729665f, 10.458325f },
	{ 1.0508511f, 3.8341677f, 10.52065f },
	{ 0.9774412f, 4.0077066f, 10.586503f },
	{ 0.8835659f, 4.2053957f, 10.662713f },
	{ 0.7626407f, 4.430338f, 10.755653f },
	{ 0.62657154f, 4.6727533f, 10.874516f },
	{ 0.4975595f, 4.9097185f, 11.007837f },
	{ 0.36310542f, 5.12276f, 11.131464f },
	{ 0.23085795f, 5.290925f, 11.231158f },
	{ 0.10788158f, 5.4006495f, 11.294765f },
	{ -5.001498f, 7.7343926f, 8.755064f },
	{ -4.8664846f, 7.88568f, 8.959026f },
	{ -4.673849f, 8.028105f, 9.15465f },
	{ -4.4140997f, 8.160458f, 9.340817f },
	{ -4.0832024f, 8.277138f, 9.516504f },
	{ -3.7156425f, 8.369957f, 9.67105f },
	{ -3.349185f, 8.429964f, 9.799297f },
	{ -2.9753819f, 8.453596f, 9.915215f },
	{ -2.5792737f, 8.433409f, 10.029807f },
	{ -2.1735454f, 8.346458f, 10.141714f },
	{ -1.7763654f, 8.185146f, 10.241987f },
	{ -1.4181951f, 8.007441f, 10.315553f },
	{ -1.1735082f, 7.8066883f, 10.277129f },
	{ -0.9743082f, 7.6422005f, 10.2764225f },
	{ -0.83730644f, 7.408587f, 10.238022f },
	{ -0.7437445f, 7.1518135f, 10.205219f },
	{ -0.678272f, 6.907565f, 10.2002125f },
	{ -0.6530571f, 6.6508846f, 10.200544f },
	{ -0.68997055f, 6.3666496f, 10.165792f },
	{ -0.7841004f, 6.092394f, 10.104666f },
	{ -0.9407326f, 5.851326f, 10.027961f },
	{ -1.1434646f, 5.6361647f, 9.955016f },
	{ -1.3559535f, 5.4395056f, 9.892996f },
	{ -1.5756254f, 5.2666883f, 9.837721f },
	{ -1.8114226f, 5.118995f, 9.780801f },
	{ -2.0749743f, 4.993431f, 9.709002f },
	{ -2.3753853f, 4.887937f, 9.615028f },
	{ -2.7019386f, 4.7963057f, 9.510266f },
	{ -3.0426493f, 4.714306f, 9.417286f },
	{ -3.390891f, 4.6404657f, 9.344476f },
	{ -3.7366953f, 4.5790286f, 9.286493f },
	{ -4.064962f, 4.5603933f, 9.206291f },
	{ -4.380662f, 4.54904f, 9.078661f },
	{ -4.660027f, 4.6751533f, 8.897423f },
	{ -4.89849f, 4.854555f, 8.6982765f },
	{ -5.077415f, 5.08823f, 8.513255f },
	{ -5.186071f, 5.3796444f, 8.369019f },
	{ -5.2440014f, 5.718604f, 8.258554f },
	{ -5.2729993f, 6.085272f, 8.179075f },
	{ -5.267421f, 6.4395447f, 8.156724f },
	{ -5.237665f, 6.765045f, 8.195181f },
	{ -5.1980534f, 7.062338f, 8.280062f },
	{ -5.15299f, 7.329048f, 8.402868f },
	{ -5.0920963f, 7.555195f, 8.562624f },
	{ 5.001498f, 7.7343926f, 8.755064f },
	{ 4.8664846f, 7.88568f, 8.959026f },
	{ 4.673849f, 8.028105f, 9.15465f },
	{ 4.4140997f, 8.160458f, 9.340817f },
	{ 4.0832024f, 8.277138f, 9.516504f },
	{ 3.7156425f, 8.369957f, 9.67105f },
	{ 3.349185f, 8.429964f, 9.799297f },
	{ 2.9753819f, 8.453596f, 9.915215f },
	{ 2.5792737f, 8.433409f, 10.029807f },
	{ 2.1735454f, 8.346458f, 10.141714f },
	{ 1.7763654f, 8.185146f, 10.241987f },
	{ 1.4181951f, 8.007441f, 10.315553f },
	{ 1.1735082f, 7.8066883f, 10.277129f },
	{ 0.9743082f, 7.6422005f, 10.2764225f },
	{ 0.83730644f, 7.408587f, 10.238022f },
	{ 0.7437445f, 7.1518135f, 10.205219f },
	{ 0.678272f, 6.907565f, 10.2002125f },
	{ 0.6530571f, 6.6508846f, 10.200544f },
	{ 0.68997055f, 6.3666496f, 10.165792f },
	{ 0.7841004f, 6.092394f, 10.104666f },
	{ 0.9407326f, 5.851326f, 10.027961f },
	{ 1.1434646f, 5.6361647f, 9.955016f },
	{ 1.3559535f, 5.4395056f, 9.892996f },
	{ 1.5756254f, 5.2666883f, 9.837721f },
	{ 1.8114226f, 5.118995f, 9.780801f },
	{ 2.0749743f, 4.993431f, 9.709002f },
	{ 2.3753853f, 4.887937f, 9.615028f },
	{ 2.7019386f, 4.7963057f, 9.510266f },
	{ 3.0426493f, 4.714306f, 9.417286f },
	{ 3.390891f, 4.6404657f, 9.344476f },
	{ 3.7366953f, 4.5790286f, 9.286493f },
	{ 4.064962f, 4.5603933f, 9.206291f },
	{ 4.380662f, 4.54904f, 9.078661f },
	{ 4.660027f, 4.6751533f, 8.897423f },
	{ 4.89849f, 4.854555f, 8.6982765f },
	{ 5.077415f, 5.08823f, 8.513255f },
	{ 5.186071f, 5.3796444f, 8.369019f },
	{ 5.2440014f, 5.718604f, 8.258554f },
	{ 5.2729993f, 6.085272f, 8.179075f },
	{ 5.267421f, 6.4395447f, 8.156724f },
	{ 5.237665f, 6.765045f, 8.195181f },
	{ 5.1980534f, 7.062338f, 8.280062f },
	{ 5.15299f, 7.329048f, 8.402868f },
	{ 5.0920963f, 7.555195f, 8.562624f },
};

const Eigen::Matrix3Xf mat_skull_mean_shape_in_cm = Eigen::Map<const Eigen::Matrix<float, num_skull_points, 3, Eigen::RowMajor>>(skull_mean_shape_in_cm[0]).transpose();


/**
 *  @brief estimate the head rotation,
 *  @param in_image_width   The image frame width in pixels
 *  @param in_image_height  The image frame height in pixels
 *  @param in_focal         The camera focal length in pixels
 *  @param in_joint_landmarks The 'joints' landmarks consisting of 268 points within the image coordinate space in pixels (x==0 for left, y==0 for top)
 *  @param in_head_rotation   The 9 floating-point values of the 'head_pose' output
 *  @param in_translation     The head translation value of the previous frame to accelerate the computation, put a zero vector if you are unsure.
 *  @param out_head_rotation  The refined value of the head rotation. We advise ignore this value, and use the neural net output instead.
 *  @param out_translation    The x, y, z coordinate in the centimeter unit in the 3D space.
 *                            +x for the left ear, +y for the head top, +z for the face front. So the z value should typically be negative. (smaller the further)
 *  @return The error metric in the squared sum of image-space coordinate difference values.
 */
static void estimate_head_pose(
	const float in_image_width,
	const float in_image_height,
	const float in_focal,
	const Eigen::Matrix2Xf& in_joint_landmarks,
	const Eigen::Matrix3f& in_head_rotation,
	const Eigen::Vector3f& in_translation,
	Eigen::Matrix3f& out_rotation,
	Eigen::Vector3f& out_translation)
{
	//  Negate the x coordinate for correct alignment
	Eigen::Matrix3f intrinsic{
		{ -in_focal, 0.0f, in_image_width * 0.5f },
		{ 0.0f, in_focal, in_image_height * 0.5f },
		{ 0.0f, 0.0, 1.0f }
	};

	Eigen::Matrix3f intrinsic_inv = intrinsic.inverse();

	Eigen::Matrix2Xf S(2, num_skull_points);
	for (int i = 0; i < num_skull_points; ++i)
	{
		int j = (i == 0) ? 0 : i + 3;
		S.col(i) = (intrinsic_inv * in_joint_landmarks.col(j).homogeneous()).segment(0, 2);
	}

	float f = 1.0f;
	Eigen::Matrix3f R = coordinate_shifter * in_head_rotation.transpose();
	Eigen::Vector3f t = in_translation;

	//  If not set, (zero) set it to a default value
	if (t[2] >= 0.0f)
	{
		const float z_init = -50.0f;
		t = { S(0, 0) * z_init, S(0, 1) * z_init, z_init };
	}

	const int num_iterations = 20;

	Eigen::VectorXf X_0[6];
	Eigen::VectorXf Y_0;

	Eigen::VectorXf X_1[6];
	Eigen::VectorXf Y_1;

	for (int it = 0; it < num_iterations; ++it)
	{

		Eigen::MatrixXf XTX_0 = Eigen::MatrixXf::Zero(6, 6);
		Eigen::VectorXf XTY_0 = Eigen::VectorXf::Zero(6);

		Eigen::MatrixXf XTX_1 = Eigen::MatrixXf::Zero(6, 6);
		Eigen::VectorXf XTY_1 = Eigen::VectorXf::Zero(6);

		Eigen::Matrix3Xf P = R * mat_skull_mean_shape_in_cm;

		X_0[0] = Eigen::VectorXf::Constant(num_skull_points, f);
		X_0[1] = Eigen::VectorXf::Constant(num_skull_points, 0.0f);
		X_0[2] = -S.row(0).array() + f * t[0] / t[2];
		X_0[3] = f * P.row(0);
		X_0[4] = f * P.row(2).array() + P.row(0).array() * S.row(0).array();
		X_0[5] = P.row(1).array() * S.row(0).array();

		Y_0 = S.row(0).array() * (P.row(2).array() + t[2]) - f * (P.row(0).array() + t[0]);

		X_1[0] = Eigen::VectorXf::Constant(num_skull_points, 0.0f);
		X_1[1] = Eigen::VectorXf::Constant(num_skull_points, f);
		X_1[2] = -S.row(1).array() + f * t[1] / t[2];
		X_1[3] = -f * P.row(0);
		X_1[4] = P.row(0).array() * S.row(1).array();
		X_1[5] = f * P.row(2).array() + P.row(1).array() * S.row(1).array();

		Y_1 = S.row(1).array() * (P.row(2).array() + t[2]) - f * (P.row(1).array() + t[1]);

		for (int i = 0; i < 6; ++i)
		{
			for (int j = 0; j < 6; ++j)
			{
				if (j < i)
				{
					XTX_0(i, j) = XTX_0(j, i);
					XTX_1(i, j) = XTX_1(j, i);
				}
				else
				{
					XTX_0(i, j) = X_0[i].dot(X_0[j]);
					XTX_1(i, j) = X_1[i].dot(X_1[j]);
				}
			}
			XTY_0[i] = X_0[i].dot(Y_0);
			XTY_1[i] = X_1[i].dot(Y_1);
		}

		Eigen::VectorXf output = (XTX_0 + XTX_1).inverse() * (XTY_0 + XTY_1);

		float dx = output[0];
		float dy = output[1];
		float dz = output[2];

		float da = output[3];
		float db = output[4];
		float dc = output[5];

		float tx = t[0];
		float ty = t[1];
		float tz = t[2];

		dx += dz * tx / tz;
		dy += dz * ty / tz;

		t += Eigen::Vector3f{ dx, dy, dz };

		Eigen::Matrix3f Rp{
			{ 1.0f, da, db },
			{ -da, 1.0f, dc },
			{ -db, -dc, 1.0f }
		};

		Eigen::JacobiSVD<Eigen::Matrix3f> svd(Rp * R, Eigen::ComputeFullU | Eigen::ComputeFullV);
		R = (svd.matrixU()) * (svd.matrixV().transpose());
	}

	out_rotation = (coordinate_shifter * R).transpose();
	out_translation = t;
}


//  A helper function for find_focal_length
static float _find_focal_length_worker(
	int in_image_width, int in_image_height,
	float in_focal,
	const TArray<Eigen::Matrix2Xf>& in_joint_landmarks,
	const Eigen::Matrix3f& in_head_rotation)
{
	float result = 0.0f;

	for (const Eigen::Matrix2Xf& landmarks : in_joint_landmarks)
	{
		Eigen::Matrix3f rotation;
		Eigen::Vector3f translation;
		estimate_head_pose(in_image_width, in_image_height, in_focal, landmarks, in_head_rotation, { 0.0f, 0.0f, 0.0f }, rotation, translation);

		Eigen::Matrix3f intrinsic{
			{ -in_focal, 0.0f, in_image_width * 0.5f },
			{ 0.0f, in_focal, in_image_height * 0.5f },
			{ 0.0f, 0.0, 1.0f }
		};

		Eigen::MatrixXf out_head_pose(3, 4);

		out_head_pose.block(0, 0, 3, 3) = coordinate_shifter * rotation.transpose();
		out_head_pose.block(0, 3, 3, 1) = translation;

		Eigen::Matrix3Xf landmarks_inferred = intrinsic * out_head_pose * mat_skull_mean_shape_in_cm.colwise().homogeneous();
		float sum_squares = 0.0f;
		for (int i = 0; i < 1; ++i)
		{
//			int j = (i == 0) ? 0 : i + 3; // build waring fix
			int j = 0; // build warning fix
			sum_squares += (landmarks_inferred.col(i).hnormalized() - landmarks.col(j)).squaredNorm();
		}

		result += sum_squares;
	}

	return result;
}

/**
 *  @brief estimate the focal length from joint landmarks, using ternary search
 *  @param in_image_width   The image frame width in pixels
 *  @param in_image_height  The image frame height in pixels
 *  @param in_joint_landmarks The 'joints' landmarks consisting of 268 points within the image coordinate space in pixels (x==0 for left, y==0 for top)
 *  @param in_head_rotation   The 9 floating-point values of the 'head_pose' output
 *  @return estimated focal value
 */
static float find_focal_length(int in_image_width, int in_image_height,
	const TArray<Eigen::Matrix2Xf>& in_joint_landmarks,
	const Eigen::Matrix3f& in_head_rotation)
{
	float diagonal = sqrtf(in_image_width * in_image_width + in_image_height * in_image_height);

	float focal_low = diagonal * (10.0 / 43.27); // mimics a 10mm wide angle lens
	float focal_high = diagonal * (100.0 / 43.27); // mimics a 100mm zoom lens

	constexpr int num_iterations = 30;

	for (int it = 0; it < num_iterations; ++it)
	{
		//  Use harmonic means
		float focal_a = 3.0f / (2.0f / focal_low + 1.0f / focal_high);
		float focal_b = 3.0f / (1.0f / focal_low + 2.0f / focal_high);

		float error_a = _find_focal_length_worker(in_image_width, in_image_height, focal_a, in_joint_landmarks, in_head_rotation);
		float error_b = _find_focal_length_worker(in_image_width, in_image_height, focal_b, in_joint_landmarks, in_head_rotation);

		if (error_a < error_b)
		{
			focal_high = focal_b;
		}
		else
		{
			focal_low = focal_a;
		}
	}

	return 2.0f / (1.0f / focal_low + 1.0f / focal_high);
}

// End of head pose estimation code



FHyprsenseRealtimeNode::FHyprsenseRealtimeNode(const FString& InName) : FNode("HyprsenseRealtimeNode", InName)
{
	check(SolverControlNames.Num() == 174);

	Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
	Pins.Add(FPin("Neutral Frame In", EPinDirection::Input, EPinType::Bool));
	Pins.Add(FPin("Animation Out", EPinDirection::Output, EPinType::Animation));
	Pins.Add(FPin("Confidence Out", EPinDirection::Output, EPinType::Float, 0));
	Pins.Add(FPin("Debug UE Image Out", EPinDirection::Output, EPinType::UE_Image));
	Pins.Add(FPin("State Out", EPinDirection::Output, EPinType::Int));
	Pins.Add(FPin("Focal Length Out", EPinDirection::Output, EPinType::Float, 1));
}

void FHyprsenseRealtimeNode::SetModels(const FMonocularAnimationPipelineModels& InModels)
{
	MonocularAnimationPipelineModels = InModels;
}

void FHyprsenseRealtimeNode::SetDebugImage(EHyprsenseRealtimeNodeDebugImage InDebugImage)
{
	FScopeLock Lock(&DebugImageMutex);

	DebugImage = InDebugImage;
}

EHyprsenseRealtimeNodeDebugImage FHyprsenseRealtimeNode::GetDebugImage()
{
	FScopeLock Lock(&DebugImageMutex);

	return DebugImage;
}

void FHyprsenseRealtimeNode::SetFocalLength(float InFocalLength)
{
	FScopeLock Lock(&FocalLengthMutex);

	FocalLength = InFocalLength;
}

float FHyprsenseRealtimeNode::GetFocalLength()
{
	FScopeLock Lock(&FocalLengthMutex);

	return FocalLength;
}

void FHyprsenseRealtimeNode::SetHeadStabilization(bool bInHeadStabilization)
{
	bHeadStabilization = bInHeadStabilization;
}

bool FHyprsenseRealtimeNode::GetHeadStabilization() const
{
	return bHeadStabilization;
}

void FHyprsenseRealtimeNode::SetHeadAllowedRotation(bool bInEnabled, float InLeftRight, float InUpDown)
{
	bApplyAngleLimits = bInEnabled;
	LeftRightHeadRotationThreshold = InLeftRight;
	UpDownHeadRotationThreshold = InUpDown;
}

void FHyprsenseRealtimeNode::SetHeadRotationErrorHandler(EFaceUnsolvedFrameBehavior InHandler)
{
	HeadRotationErrorHandler = InHandler;
}

void FHyprsenseRealtimeNode::SetNNEBackend(const FString& InNNEBackend)
{
	MonocularAnimationPipelineModels.NNEBackend = InNNEBackend;
}

FString FHyprsenseRealtimeNode::GetNNEBackend() const
{
	return MonocularAnimationPipelineModels.NNEBackend;
}

bool FHyprsenseRealtimeNode::CheckModels(
	TSharedPtr<UE::NNE::IModelInstanceRunSync> InDetector,
	TSharedPtr<UE::NNE::IModelInstanceRunSync> InHeadpose,
	TSharedPtr<UE::NNE::IModelInstanceRunSync> InSolver,
	FModelValidationResult& OutValidationResult) const
{
	OutValidationResult = {};

	// Bind input shapes first. ONNX Runtime backends report symbolic output dimensions (-1)
	// until SetInputTensorShapes() is called, so this must happen before variant matching.
	if (InDetector->SetInputTensorShapes({ UE::NNE::FTensorShape::Make({1, 3, DetectorInputSizeY, DetectorInputSizeX}) }) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOGF(LogMetaHumanPipeline, Error, "Failed to set inputs for FaceDetector");
		return false;
	}
	if (InHeadpose->SetInputTensorShapes({ UE::NNE::FTensorShape::Make({1, 3, HeadposeInputSizeY, HeadposeInputSizeX}) }) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOGF(LogMetaHumanPipeline, Error, "Failed to set inputs for FaceHeadPoseTracker");
		return false;
	}
	// Solver input size can vary (standard 512x256 or small-faces 256x128).
	// Try each allowed size — whichever the model accepts determines the actual solver input dimensions.
	struct FAllowedSolverInput { uint32 SizeY; uint32 SizeX; };
	static const TArray<FAllowedSolverInput> AllowedSolverInputs = { {512, 256}, {256, 128} };
	bool bSolverInputSet = false;
	for (const FAllowedSolverInput& AllowedInput : AllowedSolverInputs)
	{
		if (InSolver->SetInputTensorShapes({ UE::NNE::FTensorShape::Make({1, 3, AllowedInput.SizeY, AllowedInput.SizeX}) }) == UE::NNE::EResultStatus::Ok)
		{
			OutValidationResult.SolverInputSizeY = AllowedInput.SizeY;
			OutValidationResult.SolverInputSizeX = AllowedInput.SizeX;
			bSolverInputSet = true;
			break;
		}
	}
	if (!bSolverInputSet)
	{
		UE_LOGF(LogMetaHumanPipeline, Error, "Failed to set inputs for FaceSolver");
		return false;
	}

	// Resolve which output tensor variant each model uses (ONNX vs CoreML names may differ).
	// This must happen after SetInputTensorShapes() so that output shapes are concrete, not symbolic.
	OutValidationResult.DetectorVariantIndex = FindMatchingVariant(InDetector, ModelVariants::Detector);
	if (OutValidationResult.DetectorVariantIndex == INDEX_NONE)
	{
		UE_LOGF(LogMetaHumanPipeline, Error, "FaceDetector: output tensor names/shapes don't match any known variant");
		return false;
	}

	OutValidationResult.HeadposeVariantIndex = FindMatchingVariant(InHeadpose, ModelVariants::Headpose);
	if (OutValidationResult.HeadposeVariantIndex == INDEX_NONE)
	{
		UE_LOGF(LogMetaHumanPipeline, Error, "FaceHeadPoseTracker: output tensor names/shapes don't match any known variant");
		return false;
	}

	OutValidationResult.SolverVariantIndex = FindMatchingVariant(InSolver, ModelVariants::Solver);
	if (OutValidationResult.SolverVariantIndex == INDEX_NONE)
	{
		UE_LOGF(LogMetaHumanPipeline, Error, "FaceSolver: output tensor names/shapes don't match any known variant");
		return false;
	}

	UE_LOGF(LogMetaHumanPipeline, Log, "Model variants resolved - Detector:%d, Headpose:%d, Solver:%d",
		OutValidationResult.DetectorVariantIndex, OutValidationResult.HeadposeVariantIndex, OutValidationResult.SolverVariantIndex);

	return true;
}

bool FHyprsenseRealtimeNode::LoadModels()
{
	using namespace UE::NNE;

	const FString Backend = GetNNEBackend();
	if (Backend.IsEmpty())
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "No NNE backend specified");
		return false;
	}
	
	if (!GetRuntime(Backend).IsValid())
	{
		const FString Registered = FString::Join(GetAllRuntimeNames(), TEXT("', '"));
		UE_LOGF(LogMetaHumanPipeline, Warning, "NNE backend '%ls' is not available. Registered backends: '%ls'", *Backend, *Registered);
		return false;
	}

	const FSoftObjectPtr FaceDetectorModelAsset(MonocularAnimationPipelineModels.FaceDetector);
	UNNEModelData* FaceDetectorModelData = Cast<UNNEModelData>(FaceDetectorModelAsset.LoadSynchronous());
	if (!FaceDetectorModelData)
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "Failed to load face detector model");
		return false;
	}
	
	const FSoftObjectPtr HeadposeModelAsset(MonocularAnimationPipelineModels.FaceHeadPoseTracker);
	UNNEModelData* HeadposeModelData = Cast<UNNEModelData>(HeadposeModelAsset.LoadSynchronous());
	if (!HeadposeModelData)
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "Failed to load headpose model");
		return false;
	}

	const FSoftObjectPtr SolverAsset(MonocularAnimationPipelineModels.FaceSolver);
	UNNEModelData* SolverModelData = Cast<UNNEModelData>(SolverAsset.LoadSynchronous());
	if (!SolverModelData)
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "Failed to load solver model");
		return false;
	}
	
	// Try creating each model instance using the best available NNE compute tier.
	// The cascade tries NPU → GPU → CPU in priority order. GetRuntime<T>() returns
	// an invalid pointer if the backend doesn't implement that tier, so this naturally
	// does the right thing for any backend:
	//   CoreML:  NPU succeeds (or falls to GPU, then CPU)
	//   ORTDml:  NPU invalid → GPU succeeds
	//   ORTCpu:  NPU and GPU invalid → CPU succeeds
	auto TryCreateModelInstance = [&Backend](UNNEModelData* ModelData, const TCHAR* ModelName) -> TSharedPtr<IModelInstanceRunSync>
	{
		// NPU (e.g. Apple Neural Engine via CoreML)
		TWeakInterfacePtr<INNERuntimeNPU> NPURuntime = GetRuntime<INNERuntimeNPU>(Backend);
		if (NPURuntime.IsValid() && NPURuntime->CanCreateModelNPU(ModelData) == INNERuntimeNPU::ECanCreateModelNPUStatus::Ok)
		{
			if (TSharedPtr<IModelNPU> Model = NPURuntime->CreateModelNPU(ModelData))
			{
				if (TSharedPtr<IModelInstanceRunSync> Instance = Model->CreateModelInstanceNPU())
				{
					UE_LOGF(LogMetaHumanPipeline, Log, "%ls: using NPU runtime (%ls)", ModelName, *Backend);
					return Instance;
				}
			}
		}

		// GPU (e.g. DirectML, CoreML GPU, Metal)
		TWeakInterfacePtr<INNERuntimeGPU> GPURuntime = GetRuntime<INNERuntimeGPU>(Backend);
		if (GPURuntime.IsValid() && GPURuntime->CanCreateModelGPU(ModelData) == INNERuntimeGPU::ECanCreateModelGPUStatus::Ok)
		{
			if (TSharedPtr<IModelGPU> Model = GPURuntime->CreateModelGPU(ModelData))
			{
				if (TSharedPtr<IModelInstanceRunSync> Instance = Model->CreateModelInstanceGPU())
				{
					UE_LOGF(LogMetaHumanPipeline, Log, "%ls: using GPU runtime (%ls)", ModelName, *Backend);
					return Instance;
				}
			}
		}

		// CPU (e.g. ONNX Runtime CPU, CoreML CPU-only)
		TWeakInterfacePtr<INNERuntimeCPU> CPURuntime = GetRuntime<INNERuntimeCPU>(Backend);
		if (CPURuntime.IsValid() && CPURuntime->CanCreateModelCPU(ModelData) == INNERuntimeCPU::ECanCreateModelCPUStatus::Ok)
		{
			if (TSharedPtr<IModelCPU> Model = CPURuntime->CreateModelCPU(ModelData))
			{
				if (TSharedPtr<IModelInstanceRunSync> Instance = Model->CreateModelInstanceCPU())
				{
					UE_LOGF(LogMetaHumanPipeline, Log, "%ls: using CPU runtime (%ls)", ModelName, *Backend);
					return Instance;
				}
			}
		}

		UE_LOGF(LogMetaHumanPipeline, Warning, "%ls: failed to create model instance (backend '%ls')", ModelName, *Backend);
		return nullptr;
	};

	FaceDetector = TryCreateModelInstance(FaceDetectorModelData, TEXT("FaceDetector"));
	Headpose = TryCreateModelInstance(HeadposeModelData, TEXT("Headpose"));
	Solver = TryCreateModelInstance(SolverModelData, TEXT("Solver"));

	if (!FaceDetector || !Headpose || !Solver)
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "Failed to create one or more NNE model instances");
		return false;
	}

	FModelValidationResult ValidationResult;
	const bool bModelsOK = CheckModels(FaceDetector, Headpose, Solver, ValidationResult);
	if (!bModelsOK)
	{
		FaceDetector.Reset();
		Headpose.Reset();
		Solver.Reset();
		return false;
	}

	// Apply validated results to member state
	DetectorModelVariant = ModelVariants::Detector[ValidationResult.DetectorVariantIndex];
	HeadposeModelVariant = ModelVariants::Headpose[ValidationResult.HeadposeVariantIndex];
	SolverModelVariant = ModelVariants::Solver[ValidationResult.SolverVariantIndex];

	DetectorOutputIndices = BuildOutputIndexMap(FaceDetector);
	HeadposeOutputIndices = BuildOutputIndexMap(Headpose);
	SolverOutputIndices = BuildOutputIndexMap(Solver);

	SolverInputSizeX = ValidationResult.SolverInputSizeX;
	SolverInputSizeY = ValidationResult.SolverInputSizeY;
	DetectorOutputCount = DetectorModelVariant.FindChecked(OutputRole::Scores).Shape.GetData()[1];

	return true;
}

bool FHyprsenseRealtimeNode::Start(const TSharedPtr<FPipelineData>& InPipelineData)
{
	if (!FaceDetector || !Headpose || !Solver)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToInitialize);
		InPipelineData->SetErrorNodeMessage(TEXT("Failed to initialize"));
		return false;
	}

	bFaceDetected = false;

	HeadTranslation = FVector::ZeroVector;

	return true;
}

bool FHyprsenseRealtimeNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEImageDataType& Input = InPipelineData->GetData<FUEImageDataType>(Pins[0]);
	const bool bIsNeutralFrame = InPipelineData->GetData<bool>(Pins[1]);
	float FocalLengthCopy = GetFocalLength();
	if (FocalLengthCopy < 0)
	{
		// Assume a 60 degree field of view if no focal length is set.
		constexpr float Tan30Times2 = 0.5774f * 2.f;
		FocalLengthCopy = FMath::Sqrt((float) (Input.Width * Input.Width + Input.Height * Input.Height)) / Tan30Times2;
	}

	FFrameAnimationData AnimOut;
	FUEImageDataType DebugImageOut;
	EHyprsenseRealtimeNodeState State = EHyprsenseRealtimeNodeState::Unknown;
	TArray<UE::NNE::FTensorBindingCPU> Inputs, Outputs;
	bool bHaveFace = false;
	alignas(64) float FaceScore = 0;
	Matrix23f HeadposeTransform;
#ifdef USE_OPENCV
	cv::Mat HeadposeTransformCV, HeadposeTransformInvCV;
#endif

	const EHyprsenseRealtimeNodeDebugImage DebugImageCopy = GetDebugImage();

	if (DebugImageCopy == EHyprsenseRealtimeNodeDebugImage::Input)
	{
		DebugImageOut = Input;
	}

	if (bFaceDetected)
	{
		Matrix23f HeadposeTransformInv;
		HeadposeTransform = GetTransformFromPoints(TrackingPoints, FVector2D(HeadposeInputSizeX, HeadposeInputSizeY), false, HeadposeTransformInv);
		bHaveFace = true;

		if (DebugImageCopy == EHyprsenseRealtimeNodeDebugImage::FaceDetect)
		{
			DebugImageOut.Width = DetectorInputSizeX;
			DebugImageOut.Height = DetectorInputSizeY;
			DebugImageOut.Data.SetNumZeroed(DebugImageOut.Width * DebugImageOut.Height * 4);
		}

#ifdef USE_OPENCV
		HeadposeTransformCV = EigenToCV(HeadposeTransform);
		HeadposeTransformInvCV = EigenToCV(HeadposeTransformInv);
#endif
	}
	else
	{
		// Prepare image for face detector
		const Bbox DetectorBox = { 0, 0, 1.f, 1.f };
		const Matrix23f DetectorTransform = GetTransformFromBbox(DetectorBox, Input.Width, Input.Height, DetectorInputSizeX, 0.0f, false, PartType::FaceDetector);
		TArray<float, TAlignedHeapAllocator<64>> DetectorInputArray = WarpAffineBilinear(Input.Data.GetData(), Input.Width, Input.Height, DetectorTransform, DetectorInputSizeX, DetectorInputSizeY, true);

#ifdef USE_OPENCV
		const cv::Mat InputCV(Input.Height, Input.Width, CV_8UC4, (uchar*)Input.Data.GetData());
		cv::Mat DetectorInputCV;
		cv::resize(InputCV, DetectorInputCV, cv::Size(DetectorInputSizeX, DetectorInputSizeY));
		DetectorInputArray = UEImageToHSImage(DetectorInputSizeX, DetectorInputSizeY, DetectorInputCV.data, false);
#endif

		if (DebugImageCopy == EHyprsenseRealtimeNodeDebugImage::FaceDetect)
		{
			DebugImageOut.Width = DetectorInputSizeX;
			DebugImageOut.Height = DetectorInputSizeY;
			DebugImageOut.Data = HSImageToUEImage(DebugImageOut.Width, DebugImageOut.Height, DetectorInputArray, false);
		}

		// Prepare output of face detector
		TArray<float, TAlignedHeapAllocator<64>> Scores, Boxes;
		Scores.SetNumUninitialized(1 * DetectorOutputCount * 2);
		Boxes.SetNumUninitialized(1 * DetectorOutputCount * 4);

		// Run face detector
		Inputs = { {(void*)DetectorInputArray.GetData(), DetectorInputArray.Num() * sizeof(float)} };
		Outputs.SetNum(DetectorOutputIndices.Num());
		Outputs[DetectorOutputIndices[DetectorModelVariant.FindChecked(OutputRole::Scores).Name]] = {(void*)Scores.GetData(), Scores.Num() * sizeof(float)};
		Outputs[DetectorOutputIndices[DetectorModelVariant.FindChecked(OutputRole::Boxes).Name]]   = {(void*)Boxes.GetData(), Boxes.Num() * sizeof(float)};

		if (FaceDetector->RunSync(Inputs, Outputs) != UE::NNE::EResultStatus::Ok)
		{
			InPipelineData->SetErrorNodeCode(ErrorCode::FailedToDetect);
			InPipelineData->SetErrorNodeMessage(TEXT("Failed to face detect"));
			return false;
		}

		const float IouThreshold = 0.45f;
		const float ProbThreshold = 0.3f;
		const int32 TopK = 10;

		// Calculate the most accurate face by score
		const TArray<Bbox> ResultBoxes = HardNMS(Scores, Boxes, IouThreshold, ProbThreshold, DetectorOutputCount, TopK);
		if (ResultBoxes.IsEmpty())
		{
			UE_LOGF(LogMetaHumanPipeline, Verbose, "No face detected");
			State = EHyprsenseRealtimeNodeState::NoFace;
		}
		else
		{
			Bbox Face = ResultBoxes[0];

			bFaceDetected = true;
			bHaveFace = true;

			const float BoxHeight = Face.Y2 - Face.Y1;
			Face.Y1 -= 0.33f * BoxHeight; // adjustment to take account of the forehead landmarks in order to be consistent with the bbox calculation based on the dense landmarks

			// Calculate image transform for headpose stage
			HeadposeTransform = GetTransformFromBbox(Face, Input.Width, Input.Height, HeadposeInputSizeX, 0.0f, false, PartType::SparseTracker);

			Face.X1 *= Input.Width;
			Face.X2 *= Input.Width;
			Face.Y1 *= Input.Height;
			Face.Y2 *= Input.Height;

#ifdef PRINT_RESULTS
			UE_LOGF(LogTemp, Warning, "JGC FACE1 %f", Face.X1);
			UE_LOGF(LogTemp, Warning, "JGC FACE2 %f", Face.Y1);
			UE_LOGF(LogTemp, Warning, "JGC FACE3 %f", Face.X2 - Face.X1);
			UE_LOGF(LogTemp, Warning, "JGC FACE4 %f", Face.Y2 - Face.Y1);
#endif

#ifdef USE_OPENCV
			HeadposeTransformInvCV = cv::Mat::eye(3, 3, CV_64FC1);

			const double W = Face.X2 - Face.X1;
			const double H = Face.Y2 - Face.Y1;
			const double CX = Face.X1 + 0.5 * W;
			const double CY = Face.Y1 + 0.5 * H;
			const double Size = FMath::Sqrt(double(W * W + H * H)) * 256.l / 192.l;
			const double Scale = HeadposeInputSizeX / Size;

			HeadposeTransformInvCV.at<double>(0, 0) = Scale;
			HeadposeTransformInvCV.at<double>(0, 2) = HeadposeInputSizeX / 2 - Scale * CX;
			HeadposeTransformInvCV.at<double>(1, 1) = Scale;
			HeadposeTransformInvCV.at<double>(1, 2) = HeadposeInputSizeY / 2 - Scale * CY;

			HeadposeTransformCV = HeadposeTransformInvCV.inv();
			HeadposeTransformInvCV = HeadposeTransformInvCV(cv::Rect(0, 0, 3, 2));
#endif
		}
	}

	if (bHaveFace)
	{
		// Prepare image for headpose
		TArray<float, TAlignedHeapAllocator<64>> HeadposeInputArray = WarpAffineBilinear(Input.Data.GetData(), Input.Width, Input.Height, HeadposeTransform, HeadposeInputSizeX, HeadposeInputSizeY, false);

#ifdef USE_OPENCV
		const cv::Mat InputCV(Input.Height, Input.Width, CV_8UC4, (uchar*)Input.Data.GetData());
		cv::Mat HeadposeInputCV;

		cv::warpAffine(InputCV, HeadposeInputCV, HeadposeTransformInvCV, cv::Size(HeadposeInputSizeX, HeadposeInputSizeY), cv::INTER_LANCZOS4);

		HeadposeInputArray = UEImageToHSImage(HeadposeInputSizeX, HeadposeInputSizeY, HeadposeInputCV.data, true);
#endif

		// Prepare output of headpose — buffer sizes come from the resolved variant shapes
		TArray<float, TAlignedHeapAllocator<64>> Points, Pose, Rigid;
		Points.SetNumUninitialized(HeadposeModelVariant.FindChecked(OutputRole::Landmarks).Shape.Volume());
		Pose.SetNumUninitialized(HeadposeModelVariant.FindChecked(OutputRole::HeadPose).Shape.Volume());
		Rigid.SetNumUninitialized(HeadposeModelVariant.FindChecked(OutputRole::Joints).Shape.Volume());

		Inputs = { {(void*)HeadposeInputArray.GetData(), HeadposeInputArray.Num() * sizeof(float)} };
		Outputs.SetNum(HeadposeOutputIndices.Num());
		Outputs[HeadposeOutputIndices[HeadposeModelVariant.FindChecked(OutputRole::Landmarks).Name]] = {(void*)Points.GetData(), Points.Num() * sizeof(float)};
		Outputs[HeadposeOutputIndices[HeadposeModelVariant.FindChecked(OutputRole::Score).Name]]     = {(void*)&FaceScore, sizeof(float)};
		Outputs[HeadposeOutputIndices[HeadposeModelVariant.FindChecked(OutputRole::HeadPose).Name]]  = {(void*)Pose.GetData(), Pose.Num() * sizeof(float)};
		Outputs[HeadposeOutputIndices[HeadposeModelVariant.FindChecked(OutputRole::Joints).Name]]    = {(void*)Rigid.GetData(), Rigid.Num() * sizeof(float)};
		
		// Run head pose
		if (Headpose->RunSync(Inputs, Outputs) != UE::NNE::EResultStatus::Ok)
		{
			InPipelineData->SetErrorNodeCode(ErrorCode::FailedToTrack);
			InPipelineData->SetErrorNodeMessage(TEXT("Failed to track"));
			return false;
		}

		// If the model output a compact 6-element rotation (two axis vectors) instead of
		// a full 3x3 matrix, reconstruct the 9-element row-major rotation matrix.
		// Layout: [Xx, Xy, Xz, Yx, Yy, Yz] → normalize X, derive Z = cross(X,Y), re-orthogonalize Y = cross(Z,X)
		if (Pose.Num() == 6)
		{
			Eigen::Vector3f X(Pose[0], Pose[1], Pose[2]);
			Eigen::Vector3f Y(Pose[3], Pose[4], Pose[5]);
			X.normalize();
			Y.normalize();
			const Eigen::Vector3f Z = X.cross(Y).normalized();
			Y = Z.cross(X);

			Pose.SetNumUninitialized(9);
			Pose[0] = X.x(); Pose[1] = X.y(); Pose[2] = X.z();
			Pose[3] = Y.x(); Pose[4] = Y.y(); Pose[5] = Y.z();
			Pose[6] = Z.x(); Pose[7] = Z.y(); Pose[8] = Z.z();
		}

		for (int32 PointIndex = 0; PointIndex < 268; ++PointIndex)
		{
			const Eigen::Vector3f Point((Rigid[PointIndex * 2] + 0.5) * HeadposeInputSizeX, (Rigid[PointIndex * 2 + 1] + 0.5) * HeadposeInputSizeY, 1.0f);
			const Eigen::Vector2f Transformed = HeadposeTransform * Point;

			Rigid[PointIndex * 2] = Transformed[0];
			Rigid[PointIndex * 2 + 1] = Transformed[1];
		}

		Eigen::Matrix<float, 2, 2> NormalizedTransform;
		NormalizedTransform(0, 0) = HeadposeTransform(0, 0);
		NormalizedTransform(0, 1) = HeadposeTransform(0, 1);
		NormalizedTransform(1, 0) = HeadposeTransform(1, 0);
		NormalizedTransform(1, 1) = HeadposeTransform(1, 1);
		NormalizedTransform /= FMath::Sqrt(NormalizedTransform.determinant());

		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			Eigen::Vector2f Axis;

			Axis(0) = Pose[AxisIndex * 3 + 0];
			Axis(1) = Pose[AxisIndex * 3 + 1];

			Axis = NormalizedTransform * Axis;

			Pose[AxisIndex * 3 + 0] = Axis(0);
			Pose[AxisIndex * 3 + 1] = Axis(1);
		}

		const Eigen::Matrix3f HeadPose = Eigen::Map<const Eigen::Matrix<float, 3, 3, Eigen::RowMajor>>(Pose.GetData());
		const Eigen::Matrix2Xf JointLandmarks = Eigen::Map<const Eigen::Matrix<float, 268, 2, Eigen::RowMajor>>(Rigid.GetData()).transpose();

		if (bIsNeutralFrame)
		{
			FocalLengthCopy = find_focal_length(Input.Width, Input.Height, { JointLandmarks }, HeadPose);
			SetFocalLength(FocalLengthCopy);
		}

		if (FocalLengthCopy > 0)
		{
			Eigen::Vector3f PreviousTranslation;
			Eigen::Matrix3f NewRotation;
			Eigen::Vector3f NewTranslation;

			PreviousTranslation(0) = HeadTranslation.X;
			PreviousTranslation(1) = HeadTranslation.Y;
			PreviousTranslation(2) = HeadTranslation.Z;

			estimate_head_pose(Input.Width, Input.Height, FocalLengthCopy, JointLandmarks, HeadPose, PreviousTranslation, NewRotation, NewTranslation);

			HeadTranslation.X = NewTranslation(0);
			HeadTranslation.Y = NewTranslation(1);
			HeadTranslation.Z = NewTranslation(2);
		}

		const Eigen::Vector3f HeadDown = HeadPose.row(1);
		const Eigen::Vector3f HeadForward = HeadPose.row(2);
		
		const float PitchRad = FMath::Atan2(HeadDown.z(), -HeadDown.y());
		const float YawRad = FMath::Atan2(HeadForward.x(), -HeadForward.z());

		bool bFaceNotDetectedDueRotation = false;

		const float PitchAngle = FMath::Abs(FMath::RadiansToDegrees(PitchRad));
		const float YawAngle = FMath::Abs(FMath::RadiansToDegrees(YawRad));

		if (bApplyAngleLimits && (YawAngle > LeftRightHeadRotationThreshold || PitchAngle > UpDownHeadRotationThreshold))
		{
			bFaceNotDetectedDueRotation = true;
		}

		if (FaceScore <= FaceScoreThreshold)
		{
			bFaceDetected = false;
			UE_LOGF(LogMetaHumanPipeline, Verbose, "No face detected");
			State = EHyprsenseRealtimeNodeState::NoFace;
		}
		else
		{
#ifdef PRINT_RESULTS
			UE_LOGF(LogTemp, Warning, "JGC LANDMARKS1 %f", Points[0]);
			UE_LOGF(LogTemp, Warning, "JGC LANDMARKS2 %f", Points[1]);
#endif

			// Tracking points in headpose image coords
			TrackingPoints.Reset();
			for (int32 Point = 0; Point < Points.Num(); Point += 2)
			{
				TrackingPoints.Add(FVector2D((Points[Point] + 0.5) * HeadposeInputSizeX, (Points[Point + 1] + 0.5) * HeadposeInputSizeY));
			}

#ifdef PRINT_RESULTS
			UE_LOGF(LogTemp, Warning, "JGC LANDMARKS3 %f", TrackingPoints[0].X);
			UE_LOGF(LogTemp, Warning, "JGC LANDMARKS4 %f", TrackingPoints[0].Y);
#endif

			if (DebugImageCopy == EHyprsenseRealtimeNodeDebugImage::Headpose)
			{
				DebugImageOut.Width = HeadposeInputSizeX;
				DebugImageOut.Height = HeadposeInputSizeY;
				DebugImageOut.Data = HSImageToUEImage(DebugImageOut.Width, DebugImageOut.Height, HeadposeInputArray, true);

				epic::core::BurnPointsIntoImage(TrackingPoints, DebugImageOut.Width, DebugImageOut.Height, DebugImageOut.Data, 0, 0, 255, 1);
			}

			// Tracking points in input image coords
			for (FVector2D& TrackingPoint : TrackingPoints)
			{
#ifdef USE_OPENCV
				cv::Mat Point(1, 1, CV_64FC3);
				Point.at<cv::Vec3d>(0, 0)[0] = TrackingPoint.X;
				Point.at<cv::Vec3d>(0, 0)[1] = TrackingPoint.Y;
				Point.at<cv::Vec3d>(0, 0)[2] = 1;

				cv::Mat Transformed;
				cv::transform(Point, Transformed, HeadposeTransformCV);

				TrackingPoint.X = Transformed.at<cv::Vec3d>(0, 0)[0];
				TrackingPoint.Y = Transformed.at<cv::Vec3d>(0, 0)[1];
#else
				const Eigen::Vector3f Point(TrackingPoint.X, TrackingPoint.Y, 1.0f);
				const Eigen::Vector2f Transformed = HeadposeTransform * Point;

				TrackingPoint.X = Transformed[0];
				TrackingPoint.Y = Transformed[1];
#endif
			}

#ifdef PRINT_RESULTS
			UE_LOGF(LogTemp, Warning, "JGC LANDMARKS8 %f", TrackingPoints[0].X);
			UE_LOGF(LogTemp, Warning, "JGC LANDMARKS9 %f", TrackingPoints[0].Y);
#endif

			if (DebugImageCopy == EHyprsenseRealtimeNodeDebugImage::Trackers)
			{
				DebugImageOut.Width = Input.Width;
				DebugImageOut.Height = Input.Height;
				DebugImageOut.Data = Input.Data;

				epic::core::BurnPointsIntoImage(TrackingPoints, DebugImageOut.Width, DebugImageOut.Height, DebugImageOut.Data, 0, 0, 255, 2);
			}

			// Warn if the source-image face region is smaller than half the solver
			// input. The solver input is the warp of the source image guided by
			// TrackingPoints, so the points' axis-aligned bbox (in source-image coords
			// here) is the area being sampled. SmallFaceSolver expects 128x256,
			// GenericRigSolver expects 256x512; sampling from a smaller source region
			// forces an upscale and can degrade animation quality.
			if (TrackingPoints.Num() > 0)
			{
				double MinX = TrackingPoints[0].X;
				double MaxX = TrackingPoints[0].X;
				double MinY = TrackingPoints[0].Y;
				double MaxY = TrackingPoints[0].Y;
				for (const FVector2D& TrackingPoint : TrackingPoints)
				{
					MinX = FMath::Min(MinX, TrackingPoint.X);
					MaxX = FMath::Max(MaxX, TrackingPoint.X);
					MinY = FMath::Min(MinY, TrackingPoint.Y);
					MaxY = FMath::Max(MaxY, TrackingPoint.Y);
				}
				const double FaceRegionWidth = MaxX - MinX;
				const double FaceRegionHeight = MaxY - MinY;
				// anecdotally, the image-based solver works surprisingly well at 25% of model input size
				// but not well at 12.5% of model input size, so setting threshold somewhere between the two
				// to avoid log spam
				const float FaceSizeThreshold = 0.15f;
				if (FaceRegionWidth < FaceSizeThreshold * SolverInputSizeX || FaceRegionHeight < FaceSizeThreshold * SolverInputSizeY)
				{
					UE_LOGF(LogMetaHumanPipeline, Warning,
						"Tracked face region (%.0fx%.0f px) is less than %.0f%% of the solver input size (%ux%u px). "
						"The face may be too small in the image, which can degrade animation quality.",
						FaceRegionWidth, FaceRegionHeight, FaceSizeThreshold*100, SolverInputSizeX, SolverInputSizeY);
				}
			}

			TMap<FString, float> SolverControlMap;
			if (!bFaceNotDetectedDueRotation)
			{
				// Prepare image for solver
				Matrix23f SolverTransformInv;
				const Matrix23f SolverTransform = GetTransformFromPoints(TrackingPoints, FVector2D(SolverInputSizeX, SolverInputSizeY), true, SolverTransformInv);

				TArray<float, TAlignedHeapAllocator<64>> SolverInputArray = WarpAffineBilinear(Input.Data.GetData(), Input.Width, Input.Height, SolverTransform, SolverInputSizeX, SolverInputSizeY, false);

#ifdef USE_OPENCV
				cv::Mat SolverInputCV;
				const cv::Mat SolverTransformInvCV = EigenToCV(SolverTransformInv);

				cv::warpAffine(InputCV, SolverInputCV, SolverTransformInvCV, cv::Size(SolverInputSizeX, SolverInputSizeY), cv::INTER_LANCZOS4);

				SolverInputArray = UEImageToHSImage(SolverInputSizeX, SolverInputSizeY, SolverInputCV.data, true);
#endif

				if (DebugImageCopy == EHyprsenseRealtimeNodeDebugImage::Solver)
				{
					DebugImageOut.Width = SolverInputSizeX;
					DebugImageOut.Height = SolverInputSizeY;
					DebugImageOut.Data = HSImageToUEImage(DebugImageOut.Width, DebugImageOut.Height, SolverInputArray, true);
				}

				// Prepare output of solver — buffer size comes from the resolved variant shape
				TArray<float, TAlignedHeapAllocator<64>> Controls;
				Controls.SetNumUninitialized(SolverModelVariant.FindChecked(OutputRole::Controls).Shape.Volume());

				Inputs = { {(void*) SolverInputArray.GetData(), SolverInputArray.Num() * sizeof(float)} };
				Outputs.SetNum(SolverOutputIndices.Num());
				Outputs[SolverOutputIndices[SolverModelVariant.FindChecked(OutputRole::Controls).Name]] = { (void*) Controls.GetData(), Controls.Num() * sizeof(float) };

				// Run solver
				if (Solver->RunSync(Inputs, Outputs) != UE::NNE::EResultStatus::Ok)
				{
					InPipelineData->SetErrorNodeCode(ErrorCode::FailedToSolve);
					InPipelineData->SetErrorNodeMessage(TEXT("Failed to solve"));
					return false;
				}

				// If the solver variant carries active-control flags (CoreML compact outputs),
				// expand back to the full 174-slot array.
				const TConstArrayView<const bool> ActiveControlFlags = SolverModelVariant.FindChecked(OutputRole::Controls).ActiveControlFlags;
				if (!ActiveControlFlags.IsEmpty())
				{
					check(ActiveControlFlags.Num() == SolverControlNames.Num());

					TArray<float, TAlignedHeapAllocator<64>> ExpandedControls;
					ExpandedControls.SetNumZeroed(SolverControlNames.Num());
					int32 ActiveIndex = 0;
					for (int32 i = 0; i < SolverControlNames.Num(); ++i)
					{
						if (ActiveControlFlags[i])
						{
							ExpandedControls[i] = Controls[ActiveIndex++];
						}
					}
					check(ActiveIndex == Controls.Num());
					Controls = MoveTemp(ExpandedControls);
				}

#ifdef PRINT_RESULTS
				UE_LOGF(LogTemp, Warning, "JGC CONTROL1 %f", Controls[0]);
				UE_LOGF(LogTemp, Warning, "JGC CONTROL2 %f", Controls[10]);
				UE_LOGF(LogTemp, Warning, "JGC CONTROL3 %f", Controls[30]);
#endif

				// Convert solver controls to raw controls
				for (int32 Index = 0; Index < SolverControlNames.Num(); ++Index)
				{
					SolverControlMap.Add(SolverControlNames[Index], Controls[Index]);
				}
			}
			else
			{
				if (HeadRotationErrorHandler == EFaceUnsolvedFrameBehavior::NeutralPose)
				{
					for (const FString& ControlName : SolverControlNames)
					{
						SolverControlMap.Add(ControlName, 0.0f);
					}
				}
			}
			// Fill in pipeline animation structure

			// The code below is largely a copy of that in FMetaHumanFaceTracker::GetTrackingState
			// The low-level mesh tracking produces a pose matrix in a similar manner to realtime, ie
			// OpenCV coordinate system and is based on the geometry of the DNA. 

			// The original rig is in Maya, ie Y up, X right, right-handed
			// By default this gets converted on import into UE, which is Z up, Y right, left-handed, such that it is the right way up and looking along 
			// the positive y axis .
			// So the first thing we need to do is to transform the rig in UE so that it looks the same orientation as the solver code sees it ie 
			// upside down, looking along the negative x axis (in UE). 
			// We do this using an initial offset transform, below, which is applied to the rig before the pose transform
			const FTransform Offset = FTransform(FRotator(0, 90, 180));

			// get the rotation and translation in OpenCV coordinate system
			FMatrix RotationOpenCV = FRotationMatrix::MakeFromXY(FVector(Pose[0], Pose[1], Pose[2]),
				FVector(Pose[3], Pose[4], Pose[5]));
			FVector TranslationOpenCV = FVector(0, 0, 0);

			// convert to UE coordinate system
			FRotator RotatorUE;
			FVector TranslationUE;
			FOpenCVHelperLocal::ConvertOpenCVToUnreal(RotationOpenCV, TranslationOpenCV, RotatorUE, TranslationUE);

			//	Apply the landmark aware smoothing here.
			FTransform Transform = FTransform(RotatorUE, FVector(HeadTranslation.Y, HeadTranslation.Z, -HeadTranslation.X));
			if (bHeadStabilization && FocalLengthCopy > 0)
			{
				Transform = LandmarkAwareSmooth(TrackingPoints, Transform, FocalLengthCopy);
			}

			// apply the offset transform then the transform from the solver
			AnimOut.Pose = Offset * FTransform(Transform.Rotator(), TranslationUE);

			// Apply translation. The axis swapping here covers a multitude of transformations
			// such as OpenCV conversion, Maya offset, and maybe others!.
			// Taking the short cut of accumulating these here for speed ahead of playtesting.
			// This will need to be changed soon anyway to support offline processing where translation
			// need to be root bone relative not head bone relative as it is here.
			AnimOut.Pose.SetTranslation(Transform.GetTranslation());

			// End of code above copied from FMetaHumanFaceTracker::GetTrackingState

			if (!bFaceNotDetectedDueRotation || 
				(bFaceNotDetectedDueRotation && HeadRotationErrorHandler == EFaceUnsolvedFrameBehavior::NeutralPose))
			{
				AnimOut.AnimationData = GuiToRawControlsUtils::ConvertGuiToRawControls(SolverControlMap);

				// noseWrinkle is not output by the model, but it should be one.
				AnimOut.AnimationData["CTRL_expressions_noseWrinkleUpperL"] = 1;
				AnimOut.AnimationData["CTRL_expressions_noseWrinkleUpperR"] = 1;

				AnimOut.AnimationQuality = EFrameAnimationQuality::PostFiltered;
				check(AnimOut.AnimationData.Num() == 251);
			}

			// An arbitrary indicator of subject being too far away is if head occupies <10% of image
			const FVector2d AnchorPt1 = TrackingPoints[838 + 56];
			const FVector2d AnchorPt2 = TrackingPoints[838 + 86];
			const double AnchorDist = FVector2D::Distance(AnchorPt1, AnchorPt2);
			State = AnchorDist / Input.Width > 0.1 ? EHyprsenseRealtimeNodeState::OK : EHyprsenseRealtimeNodeState::SubjectTooFar;
		}
	}
	
	if (State == EHyprsenseRealtimeNodeState::NoFace || bIsNeutralFrame)
	{
		PreviousTrackingPoints.Empty();
	}

	InPipelineData->SetData<FFrameAnimationData>(Pins[2], MoveTemp(AnimOut));
	InPipelineData->SetData<float>(Pins[3], FaceScore);
	InPipelineData->SetData<FUEImageDataType>(Pins[4], MoveTemp(DebugImageOut));
	InPipelineData->SetData<int32>(Pins[5], static_cast<int32>(State));
	InPipelineData->SetData<float>(Pins[6], FocalLengthCopy);

	return true;
}

FTransform FHyprsenseRealtimeNode::LandmarkAwareSmooth(const TArray<FVector2D>& InTrackingPoints, const FTransform& InTransform,
                                                       const float InFocalLength)
{
	if (PreviousTrackingPoints.Num() == 0)
	{
		PreviousTrackingPoints = InTrackingPoints;
		PreviousTransform = InTransform;
		return InTransform;
	}
    constexpr int32 NumGroups = 3;
	constexpr int32 LandmarkGroups[NumGroups][2] = { 
		{838, 894}, // Outer lip
		{894, 924},	// Left eye
		{924, 954}};  // Right eye

    //	Iterate over groups
	double MinGroupDistance = INFINITY;
	for (int32 GroupId = 0; GroupId < NumGroups; ++GroupId)
	{
		double SumDistance = 0.0f;
		const int32 RangeStart = LandmarkGroups[GroupId][0];
		const int32 RangeEnd = LandmarkGroups[GroupId][1];
		for (int32 Index = RangeStart; Index < RangeEnd; ++Index)
		{
			SumDistance += FVector2D::Distance(PreviousTrackingPoints[Index], InTrackingPoints[Index]);
		}
		const double AvgDistance = SumDistance / (RangeEnd - RangeStart);
		MinGroupDistance = FMath::Min(MinGroupDistance, AvgDistance);
	}
	
	const double MinGroupDistanceInCm = FMath::Abs(InTransform.GetTranslation().Y * MinGroupDistance / InFocalLength);
	const double SmoothFactor = MinGroupDistanceInCm / LandmarkAwareSmoothingThresholdInCm;
	
	//	TODO: this SmoothFactor could be stored somewhere to be used in the post-processing filters.
	if (SmoothFactor >= 1.0)
	{
		PreviousTrackingPoints = InTrackingPoints;
		PreviousTransform = InTransform;
		return PreviousTransform;
	}

	check(PreviousTrackingPoints.Num() == InTrackingPoints.Num());
	
    for (int32 Index = 0; Index < InTrackingPoints.Num(); ++Index)
	{
    	PreviousTrackingPoints[Index] = FMath::Lerp(PreviousTrackingPoints[Index], InTrackingPoints[Index], SmoothFactor);
	}

	const FVector Translation = FMath::Lerp(PreviousTransform.GetTranslation(), InTransform.GetTranslation(), SmoothFactor);
	const FQuat Rotation = FQuat::Slerp(PreviousTransform.GetRotation(), InTransform.GetRotation(), SmoothFactor);
	const FVector Scale = FMath::Lerp(PreviousTransform.GetScale3D(), InTransform.GetScale3D(), SmoothFactor);

	PreviousTransform = FTransform(Rotation, Translation, Scale);
	return PreviousTransform;
}

FHyprsenseRealtimeNode::Matrix23f FHyprsenseRealtimeNode::GetTransformFromPoints(const TArray<FVector2D>& InPoints, const FVector2D& InSize, bool bInIsStableBox, Matrix23f& OutTransformInv) const
{
	const FVector2d AnchorPt1 = InPoints[838 + 56];
	const FVector2d AnchorPt2 = InPoints[838 + 86];
	const double Angle = FMath::Atan2(AnchorPt2.Y - AnchorPt1.Y, AnchorPt2.X - AnchorPt1.X);

	const FTransform2d RotMat(FQuat2d(-Angle), FVector2D::ZeroVector);

	TArray<FVector2D> RotatedPoints;
	for (const FVector2D& Point : InPoints)
	{
		RotatedPoints.Add(RotMat.TransformPoint(Point));
	}

	double CX, CY, Scale;
	Matrix23f Transform;

	if (bInIsStableBox)
	{
#ifdef PRINT_RESULTS
		UE_LOGF(LogTemp, Warning, "JGC ROT1 %f", RotatedPoints[0].X);
		UE_LOGF(LogTemp, Warning, "JGC ROT2 %f", RotatedPoints[0].Y);
#endif

		const int32 AnchorIndex1 = 835;
		const int32 AnchorIndex2 = 837;
		const double PivotY = (RotatedPoints[AnchorIndex2].Y + RotatedPoints[AnchorIndex1].Y) * 0.5;
		const double LE = RotatedPoints[AnchorIndex1].X;
		const double RE = RotatedPoints[AnchorIndex2].X;
		const double Dist = RE - LE;
		const double XOffset = 0.08;
		const double YOffset = 0.83;
		double Height = 1.65;

		double XOff = int32(XOffset * Dist);
		double YOff = int32(YOffset * Dist);
		Height = int32(Height * Dist);

		const double MinX = LE - XOff;
		const double MinY = PivotY - YOff;
		const double MaxX = RE + XOff;
		const double MaxY = PivotY + Height;

		CX = (MinX + MaxX) * 0.5;
		CY = (MinY + MaxY) * 0.5;

		const double Width = MaxX - MinX;
		Height = MaxY - MinY;

		Scale = InSize.X / Width;

#ifdef PRINT_RESULTS
		UE_LOGF(LogTemp, Warning, "JGC SCALE %f", Scale);
#endif
	}
	else
	{
		double MinX = 0, MaxX = 0, MinY = 0, MaxY = 0;
		bool bIsFirstPoint = true;

		for (const FVector2D& RotatedPoint : RotatedPoints)
		{
			if (bIsFirstPoint || RotatedPoint.X < MinX)
			{
				MinX = RotatedPoint.X;
			}

			if (bIsFirstPoint || RotatedPoint.X > MaxX)
			{
				MaxX = RotatedPoint.X;
			}

			if (bIsFirstPoint || RotatedPoint.Y < MinY)
			{
				MinY = RotatedPoint.Y;
			}

			if (bIsFirstPoint || RotatedPoint.Y > MaxY)
			{
				MaxY = RotatedPoint.Y;
			}

			bIsFirstPoint = false;
		}

		CX = (MinX + MaxX) * 0.5;
		CY = (MinY + MaxY) * 0.5;

		const double Width = MaxX - MinX;
		const double Height = MaxY - MinY;

		const double OriginalImageSize = FMath::Sqrt(Width * Width + Height * Height) * 256.0l / 192.0l;

		Scale = InSize.X / OriginalImageSize;
	}

	const FTransform2d PosMat(Scale, FVector2D(InSize.X / 2 - Scale * CX, InSize.Y / 2 - Scale * CY));

	const FTransform2d TransformInvUE = RotMat.Concatenate(PosMat);
	const FMatrix44d TransformInvUE3D = TransformInvUE.To3DMatrix();
	const FMatrix44d TransformUE3D = TransformInvUE3D.Inverse();

	for (int32 I = 0; I < 2; ++I)
	{
		for (int32 J = 0; J < 2; ++J)
		{
			Transform(I, J) = TransformUE3D.M[J][I];
			OutTransformInv(I, J) = TransformInvUE3D.M[J][I];
		}
	}
	Transform(0, 2) = TransformUE3D.M[3][0];
	Transform(1, 2) = TransformUE3D.M[3][1];
	OutTransformInv(0, 2) = TransformInvUE3D.M[3][0];
	OutTransformInv(1, 2) = TransformInvUE3D.M[3][1];

	return Transform;
}

}
