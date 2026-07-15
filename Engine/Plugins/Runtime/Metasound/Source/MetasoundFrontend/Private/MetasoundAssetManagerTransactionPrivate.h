// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundAssetKey.h"
#include "MetasoundAssetManagerTransaction.h"
#include "MetasoundFrontendRegistryTransaction.h"


namespace Metasound::Frontend
{
	using FAssetManagerTransactionBuffer = TTransactionBuffer<FAssetManagerTransaction>;
	using FAssetManagerTransactionStream = TTransactionStream<FAssetManagerTransaction>;

	class FAssetManagerTransactor : public IMetaSoundAssetTransactor
	{
	public:
		FAssetManagerTransactor();

		TUniquePtr<FAssetManagerTransactionStream> CreateTransactionStream();
		virtual void RegisterAsset(FMetaSoundAssetKey Key) override;
		virtual void UnregisterAsset(FMetaSoundAssetKey Key) override;

	private:
		TSharedRef<FAssetManagerTransactionBuffer> TransactionBuffer;
	};
} // namespace Metasound::Frontend