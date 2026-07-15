// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "dna/BinaryStreamReader.h"
#include "DNAAsset.h"
#include "DNAReader.h"

namespace dna
{
	// Wrapper stream reader for accessing asset DNA data via the BinaryStreamReader interface
	// The code is similar to the FSkelMeshDNAReader but implements the dna::BinaryStreamReader interface instead of the IDNAReader one
	class FReader : public BinaryStreamReader
	{
	public:

		explicit FReader(UDNAAsset* DNAAsset)
		{
			DNAReader = DNAAsset->GetDNAReader()->Unwrap();
		}

		const Reader* DNAReader;

		// geometry
		virtual std::uint32_t getVertexPositionCount(std::uint16_t meshIndex) const override { return DNAReader->getVertexPositionCount(meshIndex); }
		virtual Position getVertexPosition(std::uint16_t meshIndex, std::uint32_t vertexIndex) const override { return DNAReader->getVertexPosition(meshIndex, vertexIndex); }
		virtual ConstArrayView<float> getVertexPositionXs(std::uint16_t meshIndex) const override { return DNAReader->getVertexPositionXs(meshIndex); }
		virtual ConstArrayView<float> getVertexPositionYs(std::uint16_t meshIndex) const override { return DNAReader->getVertexPositionYs(meshIndex); }
		virtual ConstArrayView<float> getVertexPositionZs(std::uint16_t meshIndex) const override { return DNAReader->getVertexPositionZs(meshIndex); }
		virtual std::uint32_t getVertexTextureCoordinateCount(std::uint16_t meshIndex) const override { return DNAReader->getVertexTextureCoordinateCount(meshIndex); }
		virtual TextureCoordinate getVertexTextureCoordinate(std::uint16_t meshIndex, std::uint32_t textureCoordinateIndex) const override { return DNAReader->getVertexTextureCoordinate(meshIndex, textureCoordinateIndex); }
		virtual ConstArrayView<float> getVertexTextureCoordinateUs(std::uint16_t meshIndex) const override { return DNAReader->getVertexTextureCoordinateUs(meshIndex); }
		virtual ConstArrayView<float> getVertexTextureCoordinateVs(std::uint16_t meshIndex) const override { return DNAReader->getVertexTextureCoordinateVs(meshIndex); }
		virtual std::uint32_t getVertexNormalCount(std::uint16_t meshIndex) const override { return DNAReader->getVertexNormalCount(meshIndex); }
		virtual Normal getVertexNormal(std::uint16_t meshIndex, std::uint32_t normalIndex) const override { return DNAReader->getVertexNormal(meshIndex, normalIndex); }
		virtual ConstArrayView<float> getVertexNormalXs(std::uint16_t meshIndex) const override { return DNAReader->getVertexNormalXs(meshIndex); }
		virtual ConstArrayView<float> getVertexNormalYs(std::uint16_t meshIndex) const override { return DNAReader->getVertexNormalYs(meshIndex); }
		virtual ConstArrayView<float> getVertexNormalZs(std::uint16_t meshIndex) const override { return DNAReader->getVertexNormalZs(meshIndex); }
		virtual std::uint32_t getVertexLayoutCount(std::uint16_t meshIndex) const override { return DNAReader->getVertexLayoutCount(meshIndex); }
		virtual VertexLayout getVertexLayout(std::uint16_t meshIndex, std::uint32_t layoutIndex) const override { return DNAReader->getVertexLayout(meshIndex, layoutIndex); }
		virtual ConstArrayView<std::uint32_t> getVertexLayoutPositionIndices(std::uint16_t meshIndex) const override { return DNAReader->getVertexLayoutPositionIndices(meshIndex); }
		virtual ConstArrayView<std::uint32_t> getVertexLayoutTextureCoordinateIndices(std::uint16_t meshIndex) const override { return DNAReader->getVertexLayoutTextureCoordinateIndices(meshIndex); }
		virtual ConstArrayView<std::uint32_t> getVertexLayoutNormalIndices(std::uint16_t meshIndex) const override { return DNAReader->getVertexLayoutNormalIndices(meshIndex); }
		virtual std::uint32_t getFaceCount(std::uint16_t meshIndex) const override { return DNAReader->getFaceCount(meshIndex); }
		virtual ConstArrayView<std::uint32_t> getFaceVertexLayoutIndices(std::uint16_t meshIndex, std::uint32_t faceIndex) const override { return DNAReader->getFaceVertexLayoutIndices(meshIndex, faceIndex); }
		virtual std::uint16_t getMaximumInfluencePerVertex(std::uint16_t meshIndex) const override { return DNAReader->getMaximumInfluencePerVertex(meshIndex); }
		virtual std::uint32_t getSkinWeightsCount(std::uint16_t meshIndex) const override { return DNAReader->getSkinWeightsCount(meshIndex); }
		virtual ConstArrayView<float> getSkinWeightsValues(std::uint16_t meshIndex, std::uint32_t vertexIndex) const override { return DNAReader->getSkinWeightsValues(meshIndex, vertexIndex); }
		virtual ConstArrayView<std::uint16_t> getSkinWeightsJointIndices(std::uint16_t meshIndex, std::uint32_t vertexIndex) const override { return DNAReader->getSkinWeightsJointIndices(meshIndex, vertexIndex); }
		virtual std::uint16_t getBlendShapeTargetCount(std::uint16_t meshIndex) const override { return DNAReader->getBlendShapeTargetCount(meshIndex); }
		virtual std::uint16_t getBlendShapeChannelIndex(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex) const override { return DNAReader->getBlendShapeChannelIndex(meshIndex, blendShapeTargetIndex); }
		virtual std::uint32_t getBlendShapeTargetDeltaCount(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex) const override { return DNAReader->getBlendShapeTargetDeltaCount(meshIndex, blendShapeTargetIndex); }
		virtual Delta getBlendShapeTargetDelta(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex, std::uint32_t deltaIndex) const override { return DNAReader->getBlendShapeTargetDelta(meshIndex, blendShapeTargetIndex, deltaIndex); }
		virtual ConstArrayView<float> getBlendShapeTargetDeltaXs(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex) const override { return DNAReader->getBlendShapeTargetDeltaXs(meshIndex, blendShapeTargetIndex); }
		virtual ConstArrayView<float> getBlendShapeTargetDeltaYs(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex) const override { return DNAReader->getBlendShapeTargetDeltaYs(meshIndex, blendShapeTargetIndex); }
		virtual ConstArrayView<float> getBlendShapeTargetDeltaZs(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex) const override { return DNAReader->getBlendShapeTargetDeltaZs(meshIndex, blendShapeTargetIndex); }
		virtual ConstArrayView<std::uint32_t> getBlendShapeTargetVertexIndices(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex) const override { return DNAReader->getBlendShapeTargetVertexIndices(meshIndex, blendShapeTargetIndex); }

		// behaviour
		virtual ConstArrayView<std::uint16_t> getGUIToRawInputIndices() const override { return DNAReader->getGUIToRawInputIndices(); }
		virtual ConstArrayView<std::uint16_t> getGUIToRawOutputIndices() const override { return DNAReader->getGUIToRawOutputIndices(); }
		virtual ConstArrayView<float> getGUIToRawFromValues() const override { return DNAReader->getGUIToRawFromValues(); }
		virtual ConstArrayView<float> getGUIToRawToValues() const override { return DNAReader->getGUIToRawToValues(); }
		virtual ConstArrayView<float> getGUIToRawSlopeValues() const override { return DNAReader->getGUIToRawSlopeValues(); }
		virtual ConstArrayView<float> getGUIToRawCutValues() const override { return DNAReader->getGUIToRawCutValues(); }
		virtual std::uint16_t getPSDCount() const override { return DNAReader->getPSDCount(); }
		virtual ConstArrayView<std::uint16_t> getPSDRowIndices() const override { return DNAReader->getPSDRowIndices(); }
		virtual ConstArrayView<std::uint16_t> getPSDColumnIndices() const override { return DNAReader->getPSDColumnIndices(); }
		virtual ConstArrayView<float> getPSDValues() const override { return DNAReader->getPSDValues(); }
		virtual std::uint16_t getJointRowCount() const override { return DNAReader->getJointRowCount(); }
		virtual std::uint16_t getJointColumnCount() const override { return DNAReader->getJointColumnCount(); }
		virtual ConstArrayView<std::uint16_t> getJointVariableAttributeIndices(std::uint16_t lod) const override { return DNAReader->getJointVariableAttributeIndices(lod); }
		virtual std::uint16_t getJointGroupCount() const override { return DNAReader->getJointGroupCount(); }
		virtual ConstArrayView<std::uint16_t> getJointGroupLODs(std::uint16_t jointGroupIndex) const override { return DNAReader->getJointGroupLODs(jointGroupIndex); }
		virtual ConstArrayView<std::uint16_t> getJointGroupInputIndices(std::uint16_t jointGroupIndex) const override { return DNAReader->getJointGroupInputIndices(jointGroupIndex); }
		virtual ConstArrayView<std::uint16_t> getJointGroupOutputIndices(std::uint16_t jointGroupIndex) const override { return DNAReader->getJointGroupOutputIndices(jointGroupIndex); }
		virtual ConstArrayView<float> getJointGroupValues(std::uint16_t jointGroupIndex) const override { return DNAReader->getJointGroupValues(jointGroupIndex); }
		virtual ConstArrayView<std::uint16_t> getJointGroupJointIndices(std::uint16_t jointGroupIndex) const override { return DNAReader->getJointGroupJointIndices(jointGroupIndex); }
		virtual ConstArrayView<std::uint16_t> getBlendShapeChannelLODs() const override { return DNAReader->getBlendShapeChannelLODs(); }
		virtual ConstArrayView<std::uint16_t> getBlendShapeChannelInputIndices() const override { return DNAReader->getBlendShapeChannelInputIndices(); }
		virtual ConstArrayView<std::uint16_t> getBlendShapeChannelOutputIndices() const override { return DNAReader->getBlendShapeChannelOutputIndices(); }
		virtual ConstArrayView<std::uint16_t> getAnimatedMapLODs() const override { return DNAReader->getAnimatedMapLODs(); }
		virtual ConstArrayView<std::uint16_t> getAnimatedMapInputIndices() const override { return DNAReader->getAnimatedMapInputIndices(); }
		virtual ConstArrayView<std::uint16_t> getAnimatedMapOutputIndices() const override { return DNAReader->getAnimatedMapOutputIndices(); }
		virtual ConstArrayView<float> getAnimatedMapFromValues() const override { return DNAReader->getAnimatedMapFromValues(); }
		virtual ConstArrayView<float> getAnimatedMapToValues() const override { return DNAReader->getAnimatedMapToValues(); }
		virtual ConstArrayView<float> getAnimatedMapSlopeValues() const override { return DNAReader->getAnimatedMapSlopeValues(); }
		virtual ConstArrayView<float> getAnimatedMapCutValues() const override { return DNAReader->getAnimatedMapCutValues(); }

		// definition
		virtual std::uint16_t getGUIControlCount() const override { return DNAReader->getGUIControlCount(); }
		virtual StringView getGUIControlName(std::uint16_t index) const override { return DNAReader->getGUIControlName(index); }
		virtual std::uint16_t getRawControlCount() const override { return DNAReader->getRawControlCount(); }
		virtual StringView getRawControlName(std::uint16_t index) const override { return DNAReader->getRawControlName(index); }
		virtual std::uint16_t getJointCount() const override { return DNAReader->getJointCount(); }
		virtual StringView getJointName(std::uint16_t index) const override { return DNAReader->getJointName(index); }
		virtual std::uint16_t getJointIndexListCount() const override { return DNAReader->getJointIndexListCount(); }
		virtual ConstArrayView<std::uint16_t> getJointIndicesForLOD(std::uint16_t lod) const override { return DNAReader->getJointIndicesForLOD(lod); }
		virtual std::uint16_t getJointParentIndex(std::uint16_t index) const override { return DNAReader->getJointParentIndex(index); }
		virtual std::uint16_t getBlendShapeChannelCount() const override { return DNAReader->getBlendShapeChannelCount(); }
		virtual StringView getBlendShapeChannelName(std::uint16_t index) const override { return DNAReader->getBlendShapeChannelName(index); }
		virtual std::uint16_t getBlendShapeChannelIndexListCount() const override { return DNAReader->getBlendShapeChannelIndexListCount(); }
		virtual ConstArrayView<std::uint16_t> getBlendShapeChannelIndicesForLOD(std::uint16_t lod) const override { return DNAReader->getBlendShapeChannelIndicesForLOD(lod); }
		virtual std::uint16_t getAnimatedMapCount() const override { return DNAReader->getAnimatedMapCount(); }
		virtual StringView getAnimatedMapName(std::uint16_t index) const override { return DNAReader->getAnimatedMapName(index); }
		virtual std::uint16_t getAnimatedMapIndexListCount() const override { return DNAReader->getAnimatedMapIndexListCount(); }
		virtual ConstArrayView<std::uint16_t> getAnimatedMapIndicesForLOD(std::uint16_t lod) const override { return DNAReader->getAnimatedMapIndicesForLOD(lod); }
		virtual std::uint16_t getMeshCount() const override { return DNAReader->getMeshCount(); }
		virtual StringView getMeshName(std::uint16_t index) const override { return DNAReader->getMeshName(index); }
		virtual std::uint16_t getMeshIndexListCount() const override { return DNAReader->getMeshIndexListCount(); }
		virtual ConstArrayView<std::uint16_t> getMeshIndicesForLOD(std::uint16_t lod) const override { return DNAReader->getMeshIndicesForLOD(lod); }
		virtual std::uint16_t getMeshBlendShapeChannelMappingCount() const override { return DNAReader->getMeshBlendShapeChannelMappingCount(); }
		virtual MeshBlendShapeChannelMapping getMeshBlendShapeChannelMapping(std::uint16_t index) const override { return DNAReader->getMeshBlendShapeChannelMapping(index); }
		virtual ConstArrayView<std::uint16_t> getMeshBlendShapeChannelMappingIndicesForLOD(std::uint16_t lod) const override { return DNAReader->getMeshBlendShapeChannelMappingIndicesForLOD(lod); }
		virtual Vector3 getNeutralJointTranslation(std::uint16_t index) const override { return DNAReader->getNeutralJointTranslation(index); }
		virtual ConstArrayView<float> getNeutralJointTranslationXs() const override { return DNAReader->getNeutralJointTranslationXs(); }
		virtual ConstArrayView<float> getNeutralJointTranslationYs() const override { return DNAReader->getNeutralJointTranslationYs(); }
		virtual ConstArrayView<float> getNeutralJointTranslationZs() const override { return DNAReader->getNeutralJointTranslationZs(); }
		virtual Vector3 getNeutralJointRotation(std::uint16_t index) const override { return DNAReader->getNeutralJointRotation(index); }
		virtual ConstArrayView<float> getNeutralJointRotationXs() const override { return DNAReader->getNeutralJointRotationXs(); }
		virtual ConstArrayView<float> getNeutralJointRotationYs() const override { return DNAReader->getNeutralJointRotationYs(); }
		virtual ConstArrayView<float> getNeutralJointRotationZs() const override { return DNAReader->getNeutralJointRotationZs(); }

		// description
		virtual StringView getName() const override { return DNAReader->getName(); }
		virtual Archetype getArchetype() const override { return DNAReader->getArchetype(); }
		virtual Gender getGender() const override { return DNAReader->getGender(); }
		virtual std::uint16_t getAge() const override { return DNAReader->getAge(); }
		virtual std::uint32_t getMetaDataCount() const override { return DNAReader->getMetaDataCount(); }
		virtual StringView getMetaDataKey(std::uint32_t index) const override { return DNAReader->getMetaDataKey(index); }
		virtual StringView getMetaDataValue(const char* key) const override { return DNAReader->getMetaDataValue(key); }
		virtual TranslationUnit getTranslationUnit() const override { return DNAReader->getTranslationUnit(); }
		virtual RotationUnit getRotationUnit() const override { return DNAReader->getRotationUnit(); }
		virtual CoordinateSystem getCoordinateSystem() const override { return DNAReader->getCoordinateSystem(); }
		virtual RotationSequence getRotationSequence() const override { return DNAReader->getRotationSequence(); }
		virtual RotationSign getRotationSign() const override { return DNAReader->getRotationSign(); }
		virtual FaceWindingOrder getFaceWindingOrder() const override { return DNAReader->getFaceWindingOrder(); }
		virtual std::uint16_t getLODCount() const override { return DNAReader->getLODCount(); }
		virtual std::uint16_t getDBMaxLOD() const override { return DNAReader->getDBMaxLOD(); }
		virtual StringView getDBComplexity() const override { return DNAReader->getDBComplexity(); }
		virtual StringView getDBName() const override { return DNAReader->getDBName(); }

		// Machine Learned Behavior
		virtual std::uint16_t getFileFormatGeneration() const override { return DNAReader->getFileFormatGeneration(); }
		virtual std::uint16_t getFileFormatVersion() const override { return DNAReader->getFileFormatVersion(); }
		virtual std::uint16_t getMLControlCount() const override { return DNAReader->getMLControlCount(); }
		virtual StringView getMLControlName(std::uint16_t index) const override { return DNAReader->getMLControlName(index); }
		virtual std::uint16_t getNeuralNetworkCount() const override { return DNAReader->getNeuralNetworkCount(); }
		virtual std::uint16_t getNeuralNetworkIndexListCount() const override { return DNAReader->getNeuralNetworkIndexListCount(); }
		virtual ConstArrayView<std::uint16_t> getNeuralNetworkIndicesForLOD(std::uint16_t lod) const override { return DNAReader->getNeuralNetworkIndicesForLOD(lod); }
		virtual std::uint16_t getMeshRegionCount(std::uint16_t meshIndex) const override { return DNAReader->getMeshRegionCount(meshIndex); }
		virtual StringView getMeshRegionName(std::uint16_t meshIndex, std::uint16_t regionIndex) const override { return DNAReader->getMeshRegionName(meshIndex, regionIndex); }
		virtual ConstArrayView<std::uint16_t> getNeuralNetworkIndicesForMeshRegion(std::uint16_t meshIndex, std::uint16_t regionIndex) const override { return DNAReader->getNeuralNetworkIndicesForMeshRegion(meshIndex, regionIndex); }
		virtual ConstArrayView<std::uint16_t> getNeuralNetworkInputIndices(std::uint16_t netIndex) const override { return DNAReader->getNeuralNetworkInputIndices(netIndex); }
		virtual ConstArrayView<std::uint16_t> getNeuralNetworkOutputIndices(std::uint16_t netIndex) const override { return DNAReader->getNeuralNetworkOutputIndices(netIndex); }
		virtual std::uint16_t getNeuralNetworkLayerCount(std::uint16_t netIndex) const override { return DNAReader->getNeuralNetworkLayerCount(netIndex); }
		virtual ActivationFunction getNeuralNetworkLayerActivationFunction(std::uint16_t netIndex, std::uint16_t layerIndex) const override { return DNAReader->getNeuralNetworkLayerActivationFunction(netIndex, layerIndex); }
		virtual ConstArrayView<float> getNeuralNetworkLayerActivationFunctionParameters(std::uint16_t netIndex, std::uint16_t layerIndex) const override { return DNAReader->getNeuralNetworkLayerActivationFunctionParameters(netIndex, layerIndex); }
		virtual ConstArrayView<float> getNeuralNetworkLayerBiases(std::uint16_t netIndex, std::uint16_t layerIndex) const override { return DNAReader->getNeuralNetworkLayerBiases(netIndex, layerIndex); }
		virtual ConstArrayView<float> getNeuralNetworkLayerWeights(std::uint16_t netIndex, std::uint16_t layerIndex) const override { return DNAReader->getNeuralNetworkLayerWeights(netIndex, layerIndex); }

		// RBFDNAReader methods
		virtual std::uint16_t getRBFPoseCount() const override { return DNAReader->getRBFPoseCount(); }
		virtual StringView getRBFPoseName(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseName(poseIndex); }
		virtual ConstArrayView<std::uint16_t> getRBFPoseJointOutputIndices(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseJointOutputIndices(poseIndex); }
		virtual ConstArrayView<std::uint16_t> getRBFPoseBlendShapeChannelOutputIndices(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseBlendShapeChannelOutputIndices(poseIndex); }
		virtual ConstArrayView<std::uint16_t> getRBFPoseAnimatedMapOutputIndices(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseAnimatedMapOutputIndices(poseIndex); }
		virtual ConstArrayView<float> getRBFPoseJointOutputValues(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseJointOutputValues(poseIndex); }
		virtual float getRBFPoseScale(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseScale(poseIndex); }
		virtual std::uint16_t getRBFPoseControlCount() const override { return DNAReader->getRBFPoseControlCount(); }
		virtual StringView getRBFPoseControlName(std::uint16_t poseControlIndex) const override { return DNAReader->getRBFPoseControlName(poseControlIndex); }
		virtual ConstArrayView<std::uint16_t> getRBFPoseInputControlIndices(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseInputControlIndices(poseIndex); }
		virtual ConstArrayView<std::uint16_t> getRBFPoseOutputControlIndices(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseOutputControlIndices(poseIndex); }
		virtual ConstArrayView<float> getRBFPoseOutputControlWeights(std::uint16_t poseIndex) const override { return DNAReader->getRBFPoseOutputControlWeights(poseIndex); }
		virtual std::uint16_t getRBFSolverCount() const override { return DNAReader->getRBFSolverCount(); }
		virtual std::uint16_t getRBFSolverIndexListCount() const override { return DNAReader->getRBFSolverIndexListCount(); }
		virtual ConstArrayView<std::uint16_t> getRBFSolverIndicesForLOD(std::uint16_t lod) const override { return DNAReader->getRBFSolverIndicesForLOD(lod); }
		virtual StringView getRBFSolverName(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverName(solverIndex); }
		virtual ConstArrayView<std::uint16_t> getRBFSolverRawControlIndices(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverRawControlIndices(solverIndex); }
		virtual ConstArrayView<std::uint16_t> getRBFSolverPoseIndices(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverPoseIndices(solverIndex); }
		virtual ConstArrayView<float> getRBFSolverRawControlValues(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverRawControlValues(solverIndex); }
		virtual RBFSolverType getRBFSolverType(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverType(solverIndex); }
		virtual float getRBFSolverRadius(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverRadius(solverIndex); }
		virtual AutomaticRadius getRBFSolverAutomaticRadius(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverAutomaticRadius(solverIndex); }
		virtual float getRBFSolverWeightThreshold(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverWeightThreshold(solverIndex); }
		virtual RBFDistanceMethod getRBFSolverDistanceMethod(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverDistanceMethod(solverIndex); }
		virtual RBFNormalizeMethod getRBFSolverNormalizeMethod(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverNormalizeMethod(solverIndex); }
		virtual RBFFunctionType getRBFSolverFunctionType(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverFunctionType(solverIndex); }
		virtual TwistAxis getRBFSolverTwistAxis(std::uint16_t solverIndex) const override { return DNAReader->getRBFSolverTwistAxis(solverIndex); }

		// JointBehaviorMetadataReader methods
		virtual TranslationRepresentation getJointTranslationRepresentation(std::uint16_t jointIndex) const override { return DNAReader->getJointTranslationRepresentation(jointIndex); }
		virtual RotationRepresentation getJointRotationRepresentation(std::uint16_t jointIndex) const override { return DNAReader->getJointRotationRepresentation(jointIndex); }
		virtual ScaleRepresentation getJointScaleRepresentation(std::uint16_t jointIndex) const override { return DNAReader->getJointScaleRepresentation(jointIndex); }

		// TwistSwingDNAReader methods
		virtual std::uint16_t getTwistCount() const override { return DNAReader->getTwistCount(); }
		virtual TwistAxis getTwistSetupTwistAxis(std::uint16_t twistIndex) const override { return DNAReader->getTwistSetupTwistAxis(twistIndex); }
		virtual ConstArrayView<std::uint16_t> getTwistInputControlIndices(std::uint16_t twistIndex) const override { return DNAReader->getTwistInputControlIndices(twistIndex); }
		virtual ConstArrayView<std::uint16_t> getTwistOutputJointIndices(std::uint16_t twistIndex) const override { return DNAReader->getTwistOutputJointIndices(twistIndex); }
		virtual ConstArrayView<float> getTwistBlendWeights(std::uint16_t twistIndex) const override { return DNAReader->getTwistBlendWeights(twistIndex); }
		virtual std::uint16_t getSwingCount() const override { return DNAReader->getSwingCount(); }
		virtual TwistAxis getSwingSetupTwistAxis(std::uint16_t swingIndex) const override { return DNAReader->getSwingSetupTwistAxis(swingIndex); }
		virtual ConstArrayView<std::uint16_t> getSwingInputControlIndices(std::uint16_t swingIndex) const override { return DNAReader->getSwingInputControlIndices(swingIndex); }
		virtual ConstArrayView<std::uint16_t> getSwingOutputJointIndices(std::uint16_t swingIndex) const override { return DNAReader->getSwingOutputJointIndices(swingIndex); }
		virtual ConstArrayView<float> getSwingBlendWeights(std::uint16_t swingIndex) const override { return DNAReader->getSwingBlendWeights(swingIndex); }

		// MachineLearnedBehaviorExtReader methods
		virtual uint16 getMLTypeCount() const override { return DNAReader->getMLTypeCount(); }
		uint16 getMLOperationSetCount(uint16 mlTypeIndex) const override { return DNAReader->getMLOperationSetCount(mlTypeIndex); }
		uint16 getMLOperationCount(uint16 mlTypeIndex, uint16 mlOperationSetIndex) const override { return DNAReader->getMLOperationCount(mlTypeIndex, mlOperationSetIndex); }
		dna::MachineLearnedBehaviorOperationType getMLOperationType(uint16 mlTypeIndex, uint16 mlOperationSetIndex, uint16 mlOperationIndex) const override { return DNAReader->getMLOperationType(mlTypeIndex, mlOperationSetIndex, mlOperationIndex); }
		dna::ConstArrayView<uint32> getMLOperationParameters(uint16 mlTypeIndex, uint16 mlOperationSetIndex, uint16 mlOperationIndex) const override { return DNAReader->getMLOperationParameters(mlTypeIndex, mlOperationSetIndex, mlOperationIndex); }
		dna::ConstArrayView<uint16> getMLOperationDependencyOperationSetIndices(uint16 mlTypeIndex, uint16 mlOperationSetIndex, uint16 mlOperationIndex) const override { return DNAReader->getMLOperationDependencyOperationSetIndices(mlTypeIndex, mlOperationSetIndex, mlOperationIndex); }
		dna::ConstArrayView<uint16> getMLOperationDependencyOperationIndices(uint16 mlTypeIndex, uint16 mlOperationSetIndex, uint16 mlOperationIndex) const override { return DNAReader->getMLOperationDependencyOperationIndices(mlTypeIndex, mlOperationSetIndex, mlOperationIndex); }
		uint16 getMLOperationIndexListCount(uint16 mlTypeIndex, uint16 mlOperationSetIndex) const override { return DNAReader->getMLOperationIndexListCount(mlTypeIndex, mlOperationSetIndex); }
		dna::ConstArrayView<uint16> getMLOperationIndicesForLOD(uint16 mlTypeIndex, uint16 mlOperationSetIndex, uint16 lod) const override { return DNAReader->getMLOperationIndicesForLOD(mlTypeIndex, mlOperationSetIndex, lod); }
		dna::ConstArrayView<uint16> getMLJointsInputIndices() const override { return DNAReader->getMLJointsInputIndices(); }
		dna::ConstArrayView<uint16> getMLJointsOutputIndices() const override { return DNAReader->getMLJointsOutputIndices(); }
		dna::ConstArrayView<uint16> getMLJointsParameterKeys() const override { return DNAReader->getMLJointsParameterKeys(); }
		dna::ConstArrayView<uint16> getMLJointsParameterValues() const override { return DNAReader->getMLJointsParameterValues(); }

		// Reader
		virtual void unload(dna::DataLayer Layer) override { ensureMsgf(false, TEXT("Assest are not unloadable")); }

		// StreamReader
		virtual void read() override { }
	};
}
