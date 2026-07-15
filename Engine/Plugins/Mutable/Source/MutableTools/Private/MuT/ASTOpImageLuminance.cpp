// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpImageLuminance.h"

#include "Containers/Map.h"
#include "MuR/Model.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "MuR/ImagePrivate.h"
#include "MuT/ASTOpConditional.h"
#include "MuT/ASTOpSwitch.h"
#include "MuT/ASTOpImagePatch.h"
#include "MuT/ASTOpImagePixelFormat.h"
#include "MuT/ASTOpImageLayer.h"
#include "MuT/ASTOpImageLayerColor.h"
#include "MuT/ASTOpImageRasterMesh.h"
#include "MuT/ASTOpImageTransform.h"
#include "MuT/ASTOpImageSwizzle.h"
#include "MuT/ASTOpImageCompose.h"
#include "MuT/ASTOpImageMakeGrowMap.h"
#include "MuT/ASTOpImageResize.h"


namespace UE::Mutable::Private
{

	ASTOpImageLuminance::ASTOpImageLuminance()
		: Base(this)
	{
	}


	ASTOpImageLuminance::~ASTOpImageLuminance()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	bool ASTOpImageLuminance::IsEqual(const ASTOp& InOther) const
	{
		if (InOther.GetOpType()==GetOpType())
		{
			const ASTOpImageLuminance* Other = static_cast<const ASTOpImageLuminance*>(&InOther);
			return Base == Other->Base;
		}
		return false;
	}


	uint32 ASTOpImageLuminance::Hash() const
	{
		uint32 Result = GetTypeHash(GetOpType());
	
		Result = HashCombineFast(Result, GetTypeHash(Base));
		
		return Result;
	}


	Ptr<ASTOp> ASTOpImageLuminance::Clone(MapChildFuncRef MapChild) const
	{
		Ptr<ASTOpImageLuminance> New = new ASTOpImageLuminance();
		New->Base = MapChild(Base.child());
		return New;
	}


	void ASTOpImageLuminance::ForEachChild(const TFunctionRef<void(ASTChild&)> Func)
	{
		Func(Base);
	}


	void ASTOpImageLuminance::Link(FProgram& Program, FLinkerOptions*)
	{
		// Already linked?
		if (!LinkedAddress)
		{
			FOperation::ImageLuminanceArgs Args;
			FMemory::Memzero(Args);

			if (Base)
			{
				Args.Base = Base->LinkedAddress;
			}

			++Program.NumOps;
			LinkedAddress = MakeProgramAddress(GetOpType(), Program.ByteCode.Num());
			
			AppendCode(Program.ByteCode, GetOpType());
			AppendCode(Program.ByteCode, Args);
		}
	}


	FImageDesc ASTOpImageLuminance::GetImageDesc(bool bReturnBestOption, FGetImageDescContext* Context) const
	{
		FImageDesc Result;

		// Local context in case it is necessary
		FGetImageDescContext LocalContext;
		if (!Context)
		{
			Context = &LocalContext;
		}
		else
		{
			// Cached result?
			FImageDesc* PtrValue = Context->m_results.Find(this);
			if (PtrValue)
			{
				return *PtrValue;
			}
		}

		// Actual work
		if (Base)
		{
			Result = Base->GetImageDesc(bReturnBestOption, Context);
		}

		// Cache the result
		if (Context)
		{
			Context->m_results.Add(this, Result);
		}

		return Result;
	}


	Ptr<ImageSizeExpression> ASTOpImageLuminance::GetImageSizeExpression() const
	{
		if (Base)
		{
			return Base->GetImageSizeExpression();
		}

		return new ImageSizeExpression;
	}


	bool ASTOpImageLuminance::IsImagePlainConstant(FVector4f& OutColor) const
	{
		bool bResult = true;
		OutColor = FVector4f(0.0f,0.0f,0.0f,1.0f);

		if (Base)
		{
			bResult = Base->IsImagePlainConstant(OutColor);
			if (bResult)
			{
				float Luminance = OutColor[0] * 77.0f + OutColor[1] * 150.0f + OutColor[2] * 29.0f;
				Luminance /= 255.0f;
				OutColor = FVector4f(Luminance, Luminance, Luminance, OutColor[3]);
			}
		}

		return bResult;
	}


	FSourceDataDescriptor ASTOpImageLuminance::GetSourceDataDescriptor(FGetSourceDataDescriptorContext* Context) const
	{
		if (Base)
		{
			return Base->GetSourceDataDescriptor(Context);
		}

		return {};
	}

}
