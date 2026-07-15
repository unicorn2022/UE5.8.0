// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Platform.h"
#include "IStereoLayers.h"
#include "IXRLoadingScreen.h"
#include "Misc/AssertionMacros.h"
#include "Misc/ScopeLock.h"
#include "RHI.h"
#include "Templates/Function.h"
#include "Templates/RefCounting.h"
#include "Templates/TypeHash.h"
#include "UObject/GCObject.h"

/**
	FSimpleLayerManager can be extended if, for example, one needs to do something in UpdateLayer().
	
	Thread safety:
	All functions and state in this class should only be accessed from the game thread.
*/

class FSimpleLayerManager : public IStereoLayers, public FGCObject
{
private:
	bool bStereoLayersDirty;

	struct FLayerData {
		TMap<uint32, FLayerDesc> Layers;
		uint32 NextLayerId;
		bool bShowBackground;

		FLayerData(uint32 InNext, bool bInShowBackground = true) : Layers(), NextLayerId(InNext), bShowBackground(bInShowBackground) {}
		FLayerData(const FLayerData& In) : Layers(In.Layers), NextLayerId(In.NextLayerId), bShowBackground(In.bShowBackground) {}
	};
	TArray<FLayerData> LayerStack;

	FLayerData& LayerState(int Level=0) { return LayerStack.Last(Level); }
	const FLayerData& LayerState(int Level = 0) const { return LayerStack.Last(Level); }
	TMap<uint32, FLayerDesc>& StereoLayers(int Level = 0) { return LayerState(Level).Layers; }
	const TMap<uint32, FLayerDesc>& StereoLayers(int Level = 0) const { return LayerState(Level).Layers; }
	uint32 MakeLayerId() { return LayerState().NextLayerId++; }
	
	FLayerDesc* FindLayerById(uint32 LayerId, int32& OutLevel)
	{
		if (LayerId == FLayerDesc::INVALID_LAYER_ID || LayerId >= LayerState().NextLayerId)
		{
			return nullptr;
		}

		// If the layer id does not exist in the current state, we'll search up the stack for the last version of the layer.
		for (int32 I = 0; I < LayerStack.Num(); I++)
		{
			FLayerDesc* Found = StereoLayers(I).Find(LayerId);
			if (Found)
			{
				OutLevel = I;
				return Found;
			}
		}
		return nullptr;
	}

protected:

	virtual void UpdateLayer(FLayerDesc& Layer, uint32 LayerId, bool bIsValid)
	{}

	bool GetStereoLayersDirty()
	{
		check(IsInGameThread());

		return bStereoLayersDirty;
	}


	void ForEachLayer(TFunction<void(uint32, FLayerDesc&)> Func, bool bMarkClean = true)
	{
		check(IsInGameThread());

		for (auto& Pair : StereoLayers())
		{
			Func(Pair.Key, Pair.Value);
		}

		if (bMarkClean)
		{
			bStereoLayersDirty = false;
		}
	}

public:

	FSimpleLayerManager()
		: bStereoLayersDirty(false)
		, LayerStack{ 1 }
	{
	}

	virtual ~FSimpleLayerManager()
	{}

	FSimpleLayerManager(const FSimpleLayerManager&) = default;
	FSimpleLayerManager(FSimpleLayerManager&&) = default;
	FSimpleLayerManager& operator=(const FSimpleLayerManager&) = default;
	FSimpleLayerManager& operator=(FSimpleLayerManager&&) = default;

	// IStereoLayers interface
	virtual uint32 CreateLayer(const FLayerDesc& InLayerDesc) override
	{
		check(IsInGameThread());

		uint32 LayerId = MakeLayerId();
		check(LayerId != FLayerDesc::INVALID_LAYER_ID);
		FLayerDesc& NewLayer = StereoLayers().Emplace(LayerId, InLayerDesc);
		NewLayer.Id = LayerId;
		UpdateLayer(NewLayer, LayerId, InLayerDesc.IsVisible());
		bStereoLayersDirty = true;
		return LayerId;
	}

	virtual void DestroyLayer(uint32 LayerId) override
	{
		check(IsInGameThread());

		if (LayerId == FLayerDesc::INVALID_LAYER_ID || LayerId >= LayerState().NextLayerId)
		{
			return;
		}

		int32 FoundLevel;
		FLayerDesc* Found = FindLayerById(LayerId, FoundLevel);

		// Destroy layer will delete the last active copy of the layer even if it's currently not active
		if (Found)
		{
			if (FoundLevel == 0)
			{
				UpdateLayer(*Found, LayerId, false);
				bStereoLayersDirty = true;
			}

			StereoLayers(FoundLevel).Remove(LayerId);
		}
	}

	virtual void SetLayerDesc(uint32 LayerId, const FLayerDesc& InLayerDesc) override
	{
		check(IsInGameThread());

		if (LayerId == FLayerDesc::INVALID_LAYER_ID)
		{
			return;
		}

		// SetLayerDesc layer will update the last active copy of the layer.
		int32 FoundLevel;
		FLayerDesc* Found = FindLayerById(LayerId, FoundLevel);
		if (Found)
		{
			*Found = InLayerDesc;
			Found->Id = LayerId;

			// If the layer is currently active, update layer state
			if (FoundLevel == 0)
			{
				UpdateLayer(*Found, LayerId, InLayerDesc.IsVisible());
				bStereoLayersDirty = true;
			}
		}
	}

	virtual bool GetLayerDesc(uint32 LayerId, FLayerDesc& OutLayerDesc) override
	{
		check(IsInGameThread());

		if (LayerId == FLayerDesc::INVALID_LAYER_ID)
		{
			return false;
		}

		int32 FoundLevel;
		FLayerDesc* Found = FindLayerById(LayerId, FoundLevel);
		if (Found)
		{
			OutLayerDesc = *Found;
			return true;
		}
		else
		{
			return false;
		}
	}

	virtual const FLayerDesc* FindLayerDesc(uint32 LayerId) const override
	{
		return StereoLayers().Find(LayerId);
	}

	virtual void MarkTextureForUpdate(uint32 LayerId) override 
	{
		check(IsInGameThread());

		if (LayerId == FLayerDesc::INVALID_LAYER_ID)
		{
			return;
		}
		// If the layer id does not exist in the current state, we'll search up the stack for the last version of the layer.
		int32 FoundLevel;
		FLayerDesc* Found = FindLayerById(LayerId, FoundLevel);
		if (Found)
		{
			UpdateLayer(*Found, LayerId, true);
		}
	}

	virtual void PushLayerState(bool bPreserve = false) override
	{
		check(IsInGameThread());

		const FLayerData CurrentState = LayerState(); // Copy because we modify the Stack that LayerState returns an element of.

		if (bPreserve)
		{
			// If bPreserve is true, copy the entire state.
			LayerStack.Emplace(CurrentState);
			// We don't need to mark stereo layers as dirty as the new state is a copy of the existing one.
		}
		else
		{
			// Else start with an empty set of layers, but preserve NextLayerId.
			for (auto& Pair : StereoLayers())
			{
				// We need to mark the layers going out of scope as invalid, so implementations will remove them from the screen.
				UpdateLayer(Pair.Value, Pair.Key, false);
			}

			// New layers should continue using unique layer ids
			LayerStack.Emplace(CurrentState.NextLayerId, CurrentState.bShowBackground);
			bStereoLayersDirty = true;
		}
	}

	virtual void PopLayerState() override
	{
		check(IsInGameThread());

		// Ignore if there is only one element on the stack
		if (LayerStack.Num() <= 1)
		{
			return;
		}

		// First mark all layers in the current state as invalid if they did not exist previously.
		for (auto& Pair : StereoLayers(0))
		{
			if (!StereoLayers(1).Contains(Pair.Key))
			{
				UpdateLayer(Pair.Value, Pair.Key, false);
			}
		}

		// Destroy the top of the stack
		LayerStack.Pop();

		// Update the layers in the new current state to mark them as valid and restore previous state
		for (auto& Pair : StereoLayers(0))
		{
			FLayerDesc LayerDesc;
			LayerDesc = Pair.Value;
			UpdateLayer(Pair.Value, Pair.Key, LayerDesc.IsVisible());
		}

		bStereoLayersDirty = true;
	}

	virtual bool SupportsLayerState() override { return true; }

	virtual void HideBackgroundLayer() { LayerState().bShowBackground = false; }
	virtual void ShowBackgroundLayer() { LayerState().bShowBackground = true; }
	virtual bool IsBackgroundLayerVisible() const { return LayerStack.Last().bShowBackground; }

	// FGCObject interface ********************************************************************
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		for (FLayerData& Snapshot : LayerStack)
		{
			for (auto& Pair : Snapshot.Layers)
			{
				Collector.AddReferencedObject(Pair.Value.TextureObj);
				Collector.AddReferencedObject(Pair.Value.LeftTextureObj);
			}
		}
	}
	virtual FString GetReferencerName() const override { return TEXT("FSimpleLayerManager"); }
};

