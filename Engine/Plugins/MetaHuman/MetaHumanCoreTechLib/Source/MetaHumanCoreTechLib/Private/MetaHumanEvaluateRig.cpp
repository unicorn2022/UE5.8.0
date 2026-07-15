// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanEvaluateRig.h"
#include "api/EvaluateRigAPI.h"
#include "DNAAsset.h"
#include "FReader.h"
#include "OpenCVHelperLocal.h"

namespace UE
{
namespace Wrappers
{
	struct FMetaHumanEvaluateRig::Private
	{
		TITAN_API_NAMESPACE::EvaluateRigAPI API;
	};

	FMetaHumanEvaluateRig::FMetaHumanEvaluateRig()
	{
		Impl = MakePimpl<Private>();
	}

	bool FMetaHumanEvaluateRig::SetRigDNA(UDNA* InDNA)
	{
		// UDNA stores IDNAReader as TSharedPtr, get it and unwrap to dna::Reader*
		return Impl->API.LoadDNA(InDNA->GetDNAReader()->Unwrap());
	}

	bool FMetaHumanEvaluateRig::SetRigDNA(UDNAAsset* InDNAAsset)
	{
		// Legacy path - still supported but deprecated
		dna::FReader Reader(InDNAAsset);
		return Impl->API.LoadDNA(&Reader);
	}

	bool FMetaHumanEvaluateRig::IsRigDNASet() const
	{
		return Impl->API.IsRigDNASet();
	}

	bool FMetaHumanEvaluateRig::EvaluateRawControls(const TMap<FString, float>& InControls, const TArray<int>& InMeshIndices, int32 InLod, TArray<TArray<FVector>>& OutMeshVertices) const
	{
		std::map<std::string, float> Controls;
		for (const TPair<FString, float>& Pair : InControls)
		{
			// replace CTRL_expressions_ (used in the AS) with CTRL_expressions. (used in the DNA)
			FString RenamedControlName = Pair.Key.Replace(TEXT("CTRL_expressions_"), TEXT("CTRL_expressions."));
			Controls.emplace(
				std::string(TCHAR_TO_UTF8(*RenamedControlName)),
				Pair.Value
			);
		}

		std::vector<int> MeshIndices;
		MeshIndices.reserve(InMeshIndices.Num()); 

		for (int32 Index : InMeshIndices)
		{
			MeshIndices.push_back(static_cast<int>(Index));
		}

		std::vector<Eigen::Matrix<float, 3, -1>> MeshVertices;
		bool bSuccess = Impl->API.EvaluateRawControls(Controls, MeshIndices, static_cast<int>(InLod), MeshVertices);

		if (bSuccess)
		{
			OutMeshVertices.SetNum(MeshVertices.size());

			for (int32 I = 0; I < MeshVertices.size(); ++I)
			{
				const Eigen::Matrix<float, 3, -1>& CurMeshVertices = MeshVertices[static_cast<unsigned>(I)];
				int32 NumCols = CurMeshVertices.cols();
				TArray<FVector>& CurMeshVerticesUE = OutMeshVertices[I];
				CurMeshVerticesUE.SetNumUninitialized(NumCols);

				for (int32 Col = 0; Col < NumCols; ++Col)
				{
					CurMeshVerticesUE[Col] = FVector{ CurMeshVertices(2, Col), CurMeshVertices(0, Col), -CurMeshVertices (1, Col)}; // convert from OpenCV to UE
				}
			}
		}

		return bSuccess;
	}


	bool FMetaHumanEvaluateRig::GetNumMeshes(int32& OutNumMeshes) const
	{
		int NumMeshes;
		bool bSuccess = Impl->API.GetNumMeshes(NumMeshes);
		if (bSuccess)
		{
			OutNumMeshes = static_cast<int32>(NumMeshes);
		}

		return bSuccess;
	}


	bool FMetaHumanEvaluateRig::GetMeshIndex(const FString& InMeshName, int32& OutMeshIndex) const
	{
		int MeshIndex;
		std::string MeshName(TCHAR_TO_UTF8(*InMeshName));
		bool bSuccess = Impl->API.GetMeshIndex(MeshName, MeshIndex);
		if (bSuccess)
		{
			OutMeshIndex = static_cast<int32>(MeshIndex);
		}

		return bSuccess;
	}

	bool FMetaHumanEvaluateRig::GetNumLODs(int32& OutNumLods) const
	{
		int NumLods;
		bool bSuccess = Impl->API.GetNumLODs(NumLods);
		if (bSuccess)
		{
			OutNumLods = static_cast<int32>(NumLods);
		}

		return bSuccess;
	}


	bool FMetaHumanEvaluateRig::GetRawControlNames(TArray<FString>& OutRawControlNames)const
	{
		std::vector<std::string> RawControlNames;
		bool bSuccess = Impl->API.GetRawControlNames(RawControlNames);
		if (bSuccess)
		{
			OutRawControlNames.SetNum(RawControlNames.size());

			for (int32 I = 0; I < OutRawControlNames.Num(); ++I)
			{
				OutRawControlNames[I] = FString(UTF8_TO_TCHAR(RawControlNames[static_cast<unsigned>(I)].c_str()));
			}
		}

		return bSuccess;
	}


	bool FMetaHumanEvaluateRig::GetMeshNames(TArray<FString>& OutMeshNames)const
	{
		std::vector<std::string> MeshNames;
		bool bSuccess = Impl->API.GetMeshNames(MeshNames);

		if (bSuccess)
		{
			OutMeshNames.SetNum(MeshNames.size());

			for (int32 I = 0; I < OutMeshNames.Num(); ++I)
			{
				OutMeshNames[I] = FString(UTF8_TO_TCHAR(MeshNames[static_cast<unsigned>(I)].c_str()));
			}
		}

		return bSuccess;
	}


}
}
