// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Math/NumericLimits.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"


namespace UE::Mutable::Private
{
struct FProgram;


//---------------------------------------------------------------------------------------------
//! Adds a named ExtensionData to an Instance
//---------------------------------------------------------------------------------------------
class ASTOpAddExtensionData final : public ASTOp
{
public:
	static const EOpType Type = EOpType::IN_ADDEXTENSIONDATA;
		
	ASTChild Instance;
	ASTChild ExtensionData;

public:

	ASTOpAddExtensionData();
	ASTOpAddExtensionData(const ASTOpAddExtensionData&) = delete;
	virtual ~ASTOpAddExtensionData() override;

	virtual EOpType GetOpType() const override;
	virtual uint32 Hash() const override;
	virtual void ForEachChild(const TFunctionRef<void(ASTChild&)> F) override;
	virtual bool IsEqual(const ASTOp& OtherUntyped) const override;
	virtual Ptr<ASTOp> Clone(MapChildFuncRef MapChild) const override;
	virtual void Link(FProgram& Program, FLinkerOptions* Options) override;
};



}

