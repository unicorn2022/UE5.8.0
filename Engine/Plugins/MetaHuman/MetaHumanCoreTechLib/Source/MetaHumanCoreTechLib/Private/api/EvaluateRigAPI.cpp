// Copyright Epic Games, Inc. All Rights Reserved.

#include <EvaluateRigAPI.h>

#include <Common.h>
#include <rig/Rig.h>
#include <pma/PolyAllocator.h>
#include <dna/Reader.h>
#include <cstring>
#include <map>
#include <memory>
#include <algorithm>
#include <cctype>

namespace TITAN_API_NAMESPACE
{

struct EvaluateRigAPI::Private
{
	std::shared_ptr<TITAN_NAMESPACE::Rig<float>> dnaRig;
};

EvaluateRigAPI::EvaluateRigAPI()
    : m(new Private())
{}

EvaluateRigAPI::~EvaluateRigAPI()
{
    delete m;
}

bool EvaluateRigAPI::LoadDNA(dna::Reader* InDNAStream)
{
	try
	{
		TITAN_RESET_ERROR;
		TITAN_CHECK_OR_RETURN(InDNAStream, false, "input dna stream not valid");
		pma::PolyAllocator<TITAN_NAMESPACE::Rig<float>> polyAllocator{ MEM_RESOURCE };

		m->dnaRig = std::allocate_shared<TITAN_NAMESPACE::Rig<float>>(polyAllocator);
		TITAN_CHECK_OR_RETURN(m->dnaRig->LoadRig(InDNAStream), false, "failed to load DNA rig");

		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("LoadDNA failed with error: {}", e.what());
	}
}


bool EvaluateRigAPI::IsRigDNASet() const
{
	return m->dnaRig != nullptr;
}

std::string ToLower(const std::string& s) {
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return out;
}


bool EvaluateRigAPI::EvaluateRawControls(const std::map<std::string, float>& InControls, const std::vector<int>& InMeshIndices, int InLod, std::vector<Eigen::Matrix<float, 3, -1>>& OutMeshVertices) const
{
	try
	{
		TITAN_RESET_ERROR;
		TITAN_CHECK_OR_RETURN(m->dnaRig != nullptr, false, "No DNA rig loaded");
		TITAN_CHECK_OR_RETURN(InLod >= 0 && InLod < m->dnaRig->GetRigGeometry()->NumLODs(), false, "Lod is invalid");

		const auto& RawControlNames = m->dnaRig->GetRawControlNames();

		// Anim Sequences are not case sensitive, so we have to support both upper and lower case InControls
		// Convert all control names to lower case and create a map back to the original control names
		// Detect and reject duplicate controls that differ only by case
		std::map<std::string, std::string> LowerControlNamesToInputControlNames;
		for (const auto& [k, v] : InControls) {
			const std::string lowerKey = ToLower(k);
			auto [it, inserted] = LowerControlNamesToInputControlNames.insert({lowerKey, k});
			TITAN_CHECK_OR_RETURN(inserted, false, "Input controls contain duplicate names differing only by case: '{}' and '{}'", it->second, k);
		}

		Eigen::VectorX<float> Controls(static_cast<int>(RawControlNames.size()));
		int NumMeshes = m->dnaRig->GetRigGeometry()->NumMeshes();

		for (unsigned i = 0; i < InMeshIndices.size(); ++i)
		{
			TITAN_CHECK_OR_RETURN(InMeshIndices[i] >= 0 && InMeshIndices[i] < NumMeshes, false, "Input mesh index is invalid");
		}

		for (unsigned i = 0; i < RawControlNames.size(); ++i)
		{
			// convert reference raw control name to lower case and search for corresponding name in list of lowercase input controls
			const auto LowerRawControlName = ToLower(RawControlNames[i]);
			auto LowerIt = LowerControlNamesToInputControlNames.find(LowerRawControlName);

			if (LowerIt == LowerControlNamesToInputControlNames.end())
			{
				Controls(static_cast<int>(i)) = 0; // set control to zero
			}
			else
			{
				Controls(static_cast<int>(i)) = InControls.at(LowerIt->second);
			}
		}
		OutMeshVertices = m->dnaRig->EvaluateVertices(Controls, InLod, InMeshIndices, TITAN_NAMESPACE::Rig<float>::ControlsType::RawControls);

		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("EvaluateRawControls failed with error: {}", e.what());
	}
}


bool EvaluateRigAPI::GetNumMeshes(int& OutNumMeshes) const
{
	try
	{
		TITAN_RESET_ERROR;
		TITAN_CHECK_OR_RETURN(m->dnaRig != nullptr, false, "No DNA rig loaded");

		OutNumMeshes = m->dnaRig->GetRigGeometry()->NumMeshes();

		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("GetNumMeshes failed with error: {}", e.what());
	}
}

bool EvaluateRigAPI::GetNumLODs(int& OutNumLODs) const
{
	try
	{
		TITAN_RESET_ERROR;
		TITAN_CHECK_OR_RETURN(m->dnaRig != nullptr, false, "No DNA rig loaded");

		OutNumLODs = m->dnaRig->GetRigGeometry()->NumLODs();

		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("GetNumLODs failed with error: {}", e.what());
	}
}



bool EvaluateRigAPI::GetMeshIndex(const std::string& InMeshName, int& OutMeshIndex) const
{
	try
	{
		TITAN_RESET_ERROR;
		TITAN_CHECK_OR_RETURN(m->dnaRig != nullptr, false, "No DNA rig loaded");

		OutMeshIndex = m->dnaRig->GetRigGeometry()->GetMeshIndex(InMeshName);
		TITAN_CHECK_OR_RETURN(OutMeshIndex >= 0, false, "Invalid Mesh Name passed");

		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("GetMeshIndex failed with error: {}", e.what());
	}
}

bool EvaluateRigAPI::GetRawControlNames(std::vector<std::string>& OutRawControlNames)const
{
	try
	{
		TITAN_RESET_ERROR;
		TITAN_CHECK_OR_RETURN(m->dnaRig != nullptr, false, "No DNA rig loaded");

		OutRawControlNames = m->dnaRig->GetRawControlNames();

		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("GetRawControlNames failed with error: {}", e.what());
	}
}


bool EvaluateRigAPI::GetMeshNames(std::vector<std::string>& OutMeshNames)const
{
	try
	{
		TITAN_RESET_ERROR;
		TITAN_CHECK_OR_RETURN(m->dnaRig != nullptr, false, "No DNA rig loaded");

		OutMeshNames.resize(static_cast<unsigned>(m->dnaRig->GetRigGeometry()->NumMeshes()));
		for (size_t i = 0; i < OutMeshNames.size(); ++i)
		{
			OutMeshNames[i] = m->dnaRig->GetRigGeometry()->GetMeshName(static_cast<int>(i));
		}

		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("GetMeshNames failed with error: {}", e.what());
	}
}



} // namespace TITAN_API_NAMESPACE
