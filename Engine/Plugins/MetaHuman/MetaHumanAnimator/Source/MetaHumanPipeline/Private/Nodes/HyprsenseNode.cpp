// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/HyprsenseNode.h"
#include "MetaHumanTrace.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "MetaHuman"

namespace UE::MetaHuman::Pipeline
{
	FHyprsenseNode::FHyprsenseNode(const FString& InName) : FHyprsenseNodeBase("Hyprsense", InName)
	{
		Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
		Pins.Add(FPin("Contours Out", EPinDirection::Output, EPinType::Contours));
		ProcessPart = { true, true ,true, true, true, true, true, true, true, false, true };
		TrackerPartInputSizeX = { 256, 256, 512, 512, 512, 256, 256, 512, 256, 0, 256 };
		TrackerPartInputSizeY = { 256, 256, 512, 512, 512, 256, 256, 512, 256, 0, 256 };
	}

	bool FHyprsenseNode::Start(const TSharedPtr<FPipelineData>& InPipelineData)
	{
		if (!bIsInitialized)
		{
			EErrorCode = ErrorCode::InvalidTracker;
			ErrorMessage = "Not initialized.";
			InPipelineData->SetErrorNodeCode(EErrorCode);
			InPipelineData->SetErrorNodeMessage(ErrorMessage);
			return false;
		}


PRAGMA_DISABLE_DEPRECATION_WARNINGS
		NNEModels[1] = EyebrowTracker;
		NNEModels[3] = EyeTracker;
		NNEModels[4] = LipsTracker;
		NNEModels[5] = LipzipTracker;
		NNEModels[6] = NasolabialNoseTracker;
		NNEModels[7] = ChinTracker;
		NNEModels[8] = TeethTracker;
		NNEModels[10] = TeethConfidenceTracker;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		NNERunSyncModels[1] = EyebrowTrackerModel;
		NNERunSyncModels[3] = EyeTrackerModel;
		NNERunSyncModels[4] = LipsTrackerModel;
		NNERunSyncModels[5] = LipzipTrackerModel;
		NNERunSyncModels[6] = NasolabialNoseTrackerModel;
		NNERunSyncModels[7] = ChinTrackerModel;
		NNERunSyncModels[8] = TeethTrackerModel;
		NNERunSyncModels[10] = TeethConfidenceTrackerModel;

		InitTransformLandmark131to159();

		bIsFaceDetected = false;
		ErrorMessage = "";
		LastTransform << 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f;

		return true;
	}

	bool FHyprsenseNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
	{
		MHA_CPUPROFILER_EVENT_SCOPE(FHyprsenseNode::Process);

		if (!bIsInitialized)
		{
			EErrorCode = ErrorCode::InvalidTracker;
			ErrorMessage = "Not initialized.";
			InPipelineData->SetErrorNodeCode(EErrorCode);
			InPipelineData->SetErrorNodeMessage(ErrorMessage);
			return false;
		}

		const FUEImageDataType& Input = InPipelineData->GetData<FUEImageDataType>(Pins[0]);
		PartPoints SparseTrackerPointsInversed;
		TArray<PartPoints> OutputArrayPerModelInversed;
		bool bProcessedSuccessfully = ProcessLandmarks(Input, false, OutputArrayPerModelInversed, SparseTrackerPointsInversed, false);

		if (!bProcessedSuccessfully)
		{
			InPipelineData->SetErrorNodeCode(EErrorCode);
			InPipelineData->SetErrorNodeMessage(ErrorMessage);
			return false;
		}
		else
		{
			FFrameTrackingContourData Output;

			// Sparse tracker results
			if (bAddSparseTrackerResultsToOutput)
			{
				AddContourToOutput(SparseTrackerPointsInversed.Points, EmptyConfidences(SparseTrackerPointsInversed.Points.Num()),
					CurveSparseTrackerMap, LandmarkSparseTrackerMap, Output);
			}

			//Brow
			AddContourToOutput(OutputArrayPerModelInversed[1].Points, EmptyConfidences(OutputArrayPerModelInversed[1].Points.Num()),
				CurveBrowMap, LandmarkBrowMap, Output);

			//Eye-Iris
			AddContourToOutput(OutputArrayPerModelInversed[3].Points, EmptyConfidences(OutputArrayPerModelInversed[3].Points.Num()),
				CurveEyeIrisMap, LandmarkEyeIrisMap, Output);

			//Lip
			AddContourToOutput(OutputArrayPerModelInversed[4].Points, EmptyConfidences(OutputArrayPerModelInversed[4].Points.Num()),
				CurveLipMap, LandmarkLipMap, Output);

			//LipZip
			AddContourToOutput(OutputArrayPerModelInversed[5].Points, EmptyConfidences(OutputArrayPerModelInversed[5].Points.Num()),
				CurveLipzipMap, LandmarkLipzipMap, Output);

			TArray<float> NasoOutArray, NoseOutArray;
			if (!OutputArrayPerModelInversed[6].Points.IsEmpty())
			{
				NasoOutArray.SetNum(100); //The number of nasolabial landmarks (x,y) 50 * 2 = 10
				NoseOutArray.SetNum(98); //The number of nose landmarks (x,y) 49 * 2 = 98

				//Since nose & nasolabial are combined in the tracker, you need to separate output result
				FMemory::Memcpy(NasoOutArray.GetData(), OutputArrayPerModelInversed[6].Points.GetData(), 100 * sizeof(float));
				FMemory::Memcpy(NoseOutArray.GetData(), OutputArrayPerModelInversed[6].Points.GetData() + 100, 98 * sizeof(float));
			}

			//Nasolab
			AddContourToOutput(NasoOutArray, EmptyConfidences(NasoOutArray.Num()), CurveNasolabMap, LandmarkNasolabMap, Output);

			//Nose
			AddContourToOutput(NoseOutArray, EmptyConfidences(NoseOutArray.Num()), CurveNoseMap, LandmarkNoseMap, Output);

			//Chin
			AddContourToOutput(OutputArrayPerModelInversed[7].Points, EmptyConfidences(OutputArrayPerModelInversed[7].Points.Num()), CurveChinMap, LandmarkChinMap, Output);

			//Teeth
			AddContourToOutput(OutputArrayPerModelInversed[8].Points, OutputArrayPerModelInversed[10].Points, CurveTeethMap, LandmarkTeethMap, Output);

			InPipelineData->SetData<FFrameTrackingContourData>(Pins[1], MoveTemp(Output));
			return true;
		}
	}

	bool FHyprsenseNode::SetTrackers(const TSharedPtr<UE::NNE::IModelInstanceGPU>& InFaceTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InFaceDetector,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InEyebrowTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InEyeTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InLipsTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InLipZipTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InNasolabialNoseTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InChinTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InTeethTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceGPU>& InTeethConfidenceTracker)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FaceTracker = InFaceTracker;
		FaceDetector = InFaceDetector;
		EyebrowTracker = InEyebrowTracker;
		EyeTracker = InEyeTracker;
		LipsTracker = InLipsTracker;
		LipzipTracker = InLipZipTracker;
		NasolabialNoseTracker = InNasolabialNoseTracker;
		ChinTracker = InChinTracker;
		TeethTracker = InTeethTracker;
		TeethConfidenceTracker = InTeethConfidenceTracker;

		return SetTrackers(static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(FaceTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(FaceDetector),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(EyebrowTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(EyeTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(LipsTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(LipzipTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(NasolabialNoseTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(ChinTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(TeethTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(TeethConfidenceTracker));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	
	bool FHyprsenseNode::SetTrackers(const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InFaceTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InFaceDetector,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InEyebrowTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InEyeTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InLipsTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InLipZipTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InNasolabialNoseTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InChinTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InTeethTracker,
									 const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InTeethConfidenceTracker)
	{
		using namespace UE::NNE;

		FaceTrackerModel = InFaceTracker;
		FaceDetectorModel = InFaceDetector;
		EyebrowTrackerModel = InEyebrowTracker;
		EyeTrackerModel = InEyeTracker;
		LipsTrackerModel = InLipsTracker;
		LipzipTrackerModel = InLipZipTracker;
		NasolabialNoseTrackerModel = InNasolabialNoseTracker;
		ChinTrackerModel = InChinTracker;
		TeethTrackerModel = InTeethTracker;
		TeethConfidenceTrackerModel = InTeethConfidenceTracker;

		const TMap<TSharedPtr<IModelInstanceRunSync>, ETrackerType> TrackerTypeMap = { {FaceTrackerModel, ETrackerType::FaceTracker},
																						{FaceDetectorModel, ETrackerType::FaceDetector},
																						{EyebrowTrackerModel, ETrackerType::EyebrowTracker},
																						{EyeTrackerModel, ETrackerType::EyeTracker},
																						{LipsTrackerModel, ETrackerType::LipsTracker},
																						{LipzipTrackerModel, ETrackerType::LipzipTracker},
																						{NasolabialNoseTrackerModel, ETrackerType::NasoLabialTracker},
																						{ChinTrackerModel, ETrackerType::ChinTracker},
																						{TeethTrackerModel, ETrackerType::TeethTracker},
																						{TeethConfidenceTrackerModel, ETrackerType::TeethConfidenceTracker},
		};

		const TMap<ETrackerType, UE::NNE::FTensorShape> InputValidationMap = { { ETrackerType::FaceDetector, FTensorShape::Make({1,3, (unsigned int)DetectorInputSizeY, (unsigned int)DetectorInputSizeX})},
																 {ETrackerType::FaceTracker, FTensorShape::Make({1,3, (unsigned int)TrackerInputSizeY, (unsigned int)TrackerInputSizeX})},
																 {ETrackerType::EyebrowTracker, FTensorShape::Make({2, 3, (unsigned int)TrackerInputSizeY, (unsigned int)TrackerInputSizeX })},
																 {ETrackerType::EyeTracker, FTensorShape::Make({2, 3, 512, 512})},
																 {ETrackerType::LipsTracker, FTensorShape::Make({1, 3, 512, 512})},
																 {ETrackerType::LipzipTracker,FTensorShape::Make({1,3, (unsigned int)TrackerInputSizeY, (unsigned int)TrackerInputSizeX})},
																 {ETrackerType::NasoLabialTracker, FTensorShape::Make({1,3, (unsigned int)TrackerInputSizeY, (unsigned int)TrackerInputSizeX})},
																 {ETrackerType::ChinTracker, FTensorShape::Make({1, 3, 512, 512})},
																 {ETrackerType::TeethTracker, FTensorShape::Make({1,3, (unsigned int)TrackerInputSizeY, (unsigned int)TrackerInputSizeX})},
																 {ETrackerType::TeethConfidenceTracker,FTensorShape::Make({1,3, (unsigned int)TrackerInputSizeY, (unsigned int)TrackerInputSizeX})} };

		// note that we represent expected scalars as empty tensor shapes
		const TMap<ETrackerType, TArray<UE::NNE::FTensorShape>> OutputValidationMap = { { ETrackerType::FaceDetector, { FTensorShape::Make({1, 4212, 2}), FTensorShape::Make({1, 4212, 4})} },
																 {ETrackerType::FaceTracker, { FTensorShape::Make({1, 131, 2}), FTensorShape::Make({1, 1})} },
																 {ETrackerType::EyebrowTracker, { FTensorShape::Make({2, 48, 2}), FTensorShape{} } },
																 {ETrackerType::EyeTracker, { FTensorShape::Make({2, 64, 2}), FTensorShape{} } },
																 {ETrackerType::LipsTracker, { FTensorShape::Make({1, 216, 2}), FTensorShape{} } },
																 {ETrackerType::LipzipTracker, { FTensorShape::Make({1, 2, 2}), FTensorShape{} } },
																 {ETrackerType::NasoLabialTracker, { FTensorShape::Make({1, 99, 2}), FTensorShape{} } },
																 {ETrackerType::ChinTracker, { FTensorShape::Make({1, 49, 2}), FTensorShape{} } },
																 {ETrackerType::TeethTracker, { FTensorShape::Make({1, 4, 2}), FTensorShape{} } },
																 {ETrackerType::TeethConfidenceTracker,{ FTensorShape::Make({1, 4}) } } };

		return CheckTrackers(InputValidationMap, OutputValidationMap, TrackerTypeMap);
	}

	FHyprsenseManagedNode::FHyprsenseManagedNode(const FString& InName, const FString& InNNEBackend) : FHyprsenseNode(InName)
	{
		using namespace UE::NNE;

		UNNEModelData* FaceTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/FaceTracker.FaceTracker"));
		UNNEModelData* FaceDetectorModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/MetaHumanCoreTech/GenericTracker/FaceDetector.FaceDetector"));
		UNNEModelData* EyebrowTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/LeftBrowWholeFace.LeftBrowWholeFace"));
		UNNEModelData* EyeTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/LeftEye.LeftEye"));
		UNNEModelData* LipsTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/Lips.Lips"));
		UNNEModelData* LipZipTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/LipZip.LipZip"));
		UNNEModelData* NasolabialNoseTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/NasolabialNose.NasolabialNose"));
		UNNEModelData* ChinTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/Chin.Chin"));
		UNNEModelData* TeethTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/Teeth.Teeth"));
		UNNEModelData* TeethConfidenceModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/TeethConfidence.TeethConfidence"));

		NNEBackend = InNNEBackend;
		const FString Backend = GetNNEBackend();

		if (Backend == "NNERuntimeORTDml") // GPU cases
		{
			TWeakInterfacePtr<INNERuntimeGPU> Runtime = GetRuntime<INNERuntimeGPU>(Backend);
			if (!Runtime.IsValid())
			{
				return;
			}

			TSharedPtr<IModelGPU> FaceTrackerModelGPU = Runtime->CreateModelGPU(FaceTrackerModelData);
			if (!FaceTrackerModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> FaceDetectorModelGPU = Runtime->CreateModelGPU(FaceDetectorModelData);
			if (!FaceDetectorModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> EyebrowTrackerModelGPU = Runtime->CreateModelGPU(EyebrowTrackerModelData);
			if (!EyebrowTrackerModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> EyeTrackerModelGPU = Runtime->CreateModelGPU(EyeTrackerModelData);
			if (!EyeTrackerModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> LipsTrackerModelGPU = Runtime->CreateModelGPU(LipsTrackerModelData);
			if (!LipsTrackerModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> LipZipTrackerModelGPU = Runtime->CreateModelGPU(LipZipTrackerModelData);
			if (!LipZipTrackerModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> NasolabialNoseTrackerModelGPU = Runtime->CreateModelGPU(NasolabialNoseTrackerModelData);
			if (!NasolabialNoseTrackerModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> ChinTrackerModelGPU = Runtime->CreateModelGPU(ChinTrackerModelData);
			if (!ChinTrackerModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> TeethTrackerModelGPU = Runtime->CreateModelGPU(TeethTrackerModelData);
			if (!TeethTrackerModelGPU)
			{
				return;
			}

			TSharedPtr<IModelGPU> TeethConfidenceModelGPU = Runtime->CreateModelGPU(TeethConfidenceModelData);
			if (!TeethConfidenceModelGPU)
			{
				return;
			}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
			verify(SetTrackers(FaceTrackerModelGPU->CreateModelInstanceGPU(), FaceDetectorModelGPU->CreateModelInstanceGPU(), EyebrowTrackerModelGPU->CreateModelInstanceGPU(), EyeTrackerModelGPU->CreateModelInstanceGPU(), LipsTrackerModelGPU->CreateModelInstanceGPU(), LipZipTrackerModelGPU->CreateModelInstanceGPU(), NasolabialNoseTrackerModelGPU->CreateModelInstanceGPU(), ChinTrackerModelGPU->CreateModelInstanceGPU(), TeethTrackerModelGPU->CreateModelInstanceGPU(), TeethConfidenceModelGPU->CreateModelInstanceGPU()));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		else
		{
			TWeakInterfacePtr<INNERuntimeCPU> Runtime = GetRuntime<INNERuntimeCPU>(Backend);
			if (!Runtime.IsValid())
			{
				return;
			}

			TSharedPtr<IModelCPU> FaceTrackerModelCPU = Runtime->CreateModelCPU(FaceTrackerModelData);
			if (!FaceTrackerModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> FaceDetectorModelCPU = Runtime->CreateModelCPU(FaceDetectorModelData);
			if (!FaceDetectorModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> EyebrowTrackerModelCPU = Runtime->CreateModelCPU(EyebrowTrackerModelData);
			if (!EyebrowTrackerModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> EyeTrackerModelCPU = Runtime->CreateModelCPU(EyeTrackerModelData);
			if (!EyeTrackerModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> LipsTrackerModelCPU = Runtime->CreateModelCPU(LipsTrackerModelData);
			if (!LipsTrackerModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> LipZipTrackerModelCPU = Runtime->CreateModelCPU(LipZipTrackerModelData);
			if (!LipZipTrackerModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> NasolabialNoseTrackerModelCPU = Runtime->CreateModelCPU(NasolabialNoseTrackerModelData);
			if (!NasolabialNoseTrackerModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> ChinTrackerModelCPU = Runtime->CreateModelCPU(ChinTrackerModelData);
			if (!ChinTrackerModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> TeethTrackerModelCPU = Runtime->CreateModelCPU(TeethTrackerModelData);
			if (!TeethTrackerModelCPU)
			{
				return;
			}

			TSharedPtr<IModelCPU> TeethConfidenceModelCPU = Runtime->CreateModelCPU(TeethConfidenceModelData);
			if (!TeethConfidenceModelCPU)
			{
				return;
			}

			verify(SetTrackers(FaceTrackerModelCPU->CreateModelInstanceCPU(), FaceDetectorModelCPU->CreateModelInstanceCPU(), EyebrowTrackerModelCPU->CreateModelInstanceCPU(), EyeTrackerModelCPU->CreateModelInstanceCPU(), LipsTrackerModelCPU->CreateModelInstanceCPU(), LipZipTrackerModelCPU->CreateModelInstanceCPU(), NasolabialNoseTrackerModelCPU->CreateModelInstanceCPU(), ChinTrackerModelCPU->CreateModelInstanceCPU(), TeethTrackerModelCPU->CreateModelInstanceCPU(), TeethConfidenceModelCPU->CreateModelInstanceCPU()));
		}
	}
}
#undef LOCTEXT_NAMESPACE
