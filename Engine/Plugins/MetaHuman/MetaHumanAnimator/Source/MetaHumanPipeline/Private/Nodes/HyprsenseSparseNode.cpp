// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/HyprsenseSparseNode.h"
#include "MetaHumanTrace.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "MetaHuman"

namespace UE::MetaHuman::Pipeline
{
	FHyprsenseSparseNode::FHyprsenseSparseNode(const FString& InName) : FHyprsenseNodeBase("HyprsenseSparse", InName)
	{
		Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
		Pins.Add(FPin("Contours Out", EPinDirection::Output, EPinType::Contours));
		TrackerPartInputSizeX = { 256, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		TrackerPartInputSizeY = { 256, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		ProcessPart = { true, true, false, false, false, false, false, false, false, false, false };
	}

	bool FHyprsenseSparseNode::Start(const TSharedPtr<FPipelineData>& InPipelineData)
	{
		if (!bIsInitialized)
		{
			EErrorCode = ErrorCode::InvalidTracker;
			ErrorMessage = "Not initialized.";
			InPipelineData->SetErrorNodeCode(EErrorCode);
			InPipelineData->SetErrorNodeMessage(ErrorMessage);
			return false;
		}

		InitTransformLandmark131to159();

		bIsFaceDetected = false;
		ErrorMessage = "";
		LastTransform << 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f;

		return true;
	}

	bool FHyprsenseSparseNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
	{
		MHA_CPUPROFILER_EVENT_SCOPE(FHyprsenseSparseNode::Process);

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
		bool bProcessedSuccessfully = ProcessLandmarks(Input, false, OutputArrayPerModelInversed, SparseTrackerPointsInversed, true);


		if (!bProcessedSuccessfully)
		{
			InPipelineData->SetErrorNodeCode(EErrorCode);
			InPipelineData->SetErrorNodeMessage(ErrorMessage);
			return false;
		}
		else
		{
			FFrameTrackingContourData Output;

			//Sparse Landmarks
			AddContourToOutput(SparseTrackerPointsInversed.Points, EmptyConfidences(SparseTrackerPointsInversed.Points.Num()),
				CurveSparseTrackerMap, LandmarkSparseTrackerMap, Output);

			InPipelineData->SetData<FFrameTrackingContourData>(Pins[1], MoveTemp(Output));
			return true;
		}
	}
	
	bool FHyprsenseSparseNode::SetTrackers(	const TSharedPtr<UE::NNE::IModelInstanceGPU>& InFaceTracker,
										const TSharedPtr<UE::NNE::IModelInstanceGPU>& InFaceDetector)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FaceTracker = InFaceTracker;
		FaceDetector = InFaceDetector;

		return SetTrackers(static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(FaceTracker),
						   static_cast<TSharedPtr<UE::NNE::IModelInstanceRunSync>>(FaceDetector));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	
	bool FHyprsenseSparseNode::SetTrackers(	const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InFaceTracker,
										const TSharedPtr<UE::NNE::IModelInstanceRunSync>& InFaceDetector)
	{
		using namespace UE::NNE;

		FaceTrackerModel = InFaceTracker;
		FaceDetectorModel = InFaceDetector;

		const TMap<TSharedPtr<IModelInstanceRunSync>, ETrackerType> TrackerTypeMap = { {FaceTrackerModel, ETrackerType::FaceTracker},
			{FaceDetectorModel, ETrackerType::FaceDetector} };

		const TMap<ETrackerType, UE::NNE::FTensorShape> InputValidationMap = { { ETrackerType::FaceDetector, FTensorShape::Make({1,3, (unsigned int)DetectorInputSizeY, (unsigned int)DetectorInputSizeX})},
																 {ETrackerType::FaceTracker, FTensorShape::Make({1,3, (unsigned int)TrackerInputSizeY, (unsigned int)TrackerInputSizeX})}};

		const TMap<ETrackerType, TArray<UE::NNE::FTensorShape>> OutputValidationMap = { { ETrackerType::FaceDetector, { FTensorShape::Make({1, 4212, 2}), FTensorShape::Make({1, 4212, 4})} },
																 {ETrackerType::FaceTracker, { FTensorShape::Make({1, 131, 2}), FTensorShape::Make({1, 1})} } };

		return CheckTrackers(InputValidationMap, OutputValidationMap, TrackerTypeMap);
	}

	FHyprsenseSparseManagedNode::FHyprsenseSparseManagedNode(const FString& InName, const FString& InNNEBackend) : FHyprsenseSparseNode(InName)
	{
		using namespace UE::NNE;

		UNNEModelData* FaceTrackerModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/" UE_PLUGIN_NAME "/GenericTracker/FaceTracker.FaceTracker"));
		UNNEModelData* FaceDetectorModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *FString("/MetaHumanCoreTech/GenericTracker/FaceDetector.FaceDetector"));

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

PRAGMA_DISABLE_DEPRECATION_WARNINGS
			verify(SetTrackers(FaceTrackerModelGPU->CreateModelInstanceGPU(), FaceDetectorModelGPU->CreateModelInstanceGPU()));
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

			verify(SetTrackers(FaceTrackerModelCPU->CreateModelInstanceCPU(), FaceDetectorModelCPU->CreateModelInstanceCPU()));
		}
	}
}
#undef LOCTEXT_NAMESPACE
