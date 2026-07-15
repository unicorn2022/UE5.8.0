// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Operations.h"
#include "MuR/Types.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"


namespace UE::Mutable::Private
{
	struct FProgram;

	/** */
	class ASTOpMeshClipWithMesh final : public ASTOp
	{
	public:

		ASTChild Source;
		ASTChild ClipMesh;
		EFaceCullStrategy FaceCullStrategy = EFaceCullStrategy::AllVerticesCulled;
		
	public:

		ASTOpMeshClipWithMesh();
		ASTOpMeshClipWithMesh(const ASTOpMeshClipWithMesh&) = delete;
		~ASTOpMeshClipWithMesh();

		// ASTOp interface
		virtual EOpType GetOpType() const override { return EOpType::ME_CLIPWITHMESH; }
		virtual uint32 Hash() const override;
		virtual bool IsEqual(const ASTOp&) const override;
		virtual Ptr<ASTOp> Clone(MapChildFuncRef) const override;
		virtual void ForEachChild(const TFunctionRef<void(ASTChild&)>) override;
		virtual void Link(FProgram&, FLinkerOptions*) override;
		virtual Ptr<ASTOp> OptimiseSink(const FModelOptimizationOptions&, FOptimizeSinkContext&) const override;
		virtual FSourceDataDescriptor GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const override;
	};


}

