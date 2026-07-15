// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheRecord.h"

#include "Algo/Accumulate.h"
#include "Algo/BinarySearch.h"
#include "Algo/IsSorted.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataSerialization.h"
#include "DerivedDataValue.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryWriter.h"
#include <atomic>

namespace UE::DerivedData::Private
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheRecordBuilderInternal final : public ICacheRecordBuilderInternal
{
public:
	explicit FCacheRecordBuilderInternal(const FCacheKey& Key);
	~FCacheRecordBuilderInternal() final = default;

	void SetMeta(FCbObject&& Meta) final;

	void AddValue(const FValueId& Id, const FValue& Value) final;

	FCacheRecord Build() final;
	void BuildAsync(IRequestOwner& Owner, FOnCacheRecordComplete&& OnComplete) final;

	FCacheKey Key;
	FCbObject Meta;
	TArray<FValueWithId> Values;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheRecordInternal final : public ICacheRecordInternal
{
public:
	FCacheRecordInternal() = default;
	explicit FCacheRecordInternal(FCacheRecordBuilderInternal&& RecordBuilder);
	explicit FCacheRecordInternal(const FCacheKey& Key, FCbObject&& Meta, TArray<FValueWithId>&& Values);

	~FCacheRecordInternal() final = default;

	const FCacheKey& GetKey() const final;
	const FCbObject& GetMeta() const final;
	const FValueWithId& GetValue(const FValueId& Id) const final;
	TConstArrayView<FValueWithId> GetValues() const final;

	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	FCacheKey Key;
	FCbObject Meta;
	TArray<FValueWithId> Values;
	mutable std::atomic<uint32> ReferenceCount{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecordInternal::FCacheRecordInternal(FCacheRecordBuilderInternal&& RecordBuilder)
	: Key(RecordBuilder.Key)
	, Meta(MoveTemp(RecordBuilder.Meta))
	, Values(MoveTemp(RecordBuilder.Values))
{
}

FCacheRecordInternal::FCacheRecordInternal(const FCacheKey& InKey, FCbObject&& InMeta, TArray<FValueWithId>&& InValues)
	: Key(InKey)
	, Meta(MoveTemp(InMeta))
	, Values(MoveTemp(InValues))
{
}

const FCacheKey& FCacheRecordInternal::GetKey() const
{
	return Key;
}

const FCbObject& FCacheRecordInternal::GetMeta() const
{
	return Meta;
}

const FValueWithId& FCacheRecordInternal::GetValue(const FValueId& Id) const
{
	const int32 Index = Algo::BinarySearchBy(Values, Id, &FValueWithId::GetId);
	return Values.IsValidIndex(Index) ? Values[Index] : FValueWithId::Null;
}

TConstArrayView<FValueWithId> FCacheRecordInternal::GetValues() const
{
	return Values;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecordBuilderInternal::FCacheRecordBuilderInternal(const FCacheKey& InKey)
	: Key(InKey)
{
}

void FCacheRecordBuilderInternal::SetMeta(FCbObject&& InMeta)
{
	Meta = MoveTemp(InMeta);
	Meta.MakeOwned();
}

void FCacheRecordBuilderInternal::AddValue(const FValueId& Id, const FValue& Value)
{
	checkf(Id.IsValid(), TEXT("Failed to add value on %s because the ID is null."), *WriteToString<96>(Key));
	checkf(!(Value == FValue::Null), TEXT("Failed to add value on %s because the value is null."), *WriteToString<96>(Key));
	const int32 Index = Algo::LowerBoundBy(Values, Id, &FValueWithId::GetId);
	checkf(!(Values.IsValidIndex(Index) && Values[Index].GetId() == Id),
		TEXT("Failed to add value on %s with ID %s because it has an existing value with that ID."),
		*WriteToString<96>(Key), *WriteToString<32>(Id));
	Values.Insert(FValueWithId(Id, Value), Index);
}

FCacheRecord FCacheRecordBuilderInternal::Build()
{
	checkf(Algo::IsSortedBy(Values, &FValueWithId::GetId),
		TEXT("Values in the cache record %s are expected to be sorted when added."),
		*WriteToString<96>(Key));
	return CreateCacheRecord(new FCacheRecordInternal(MoveTemp(*this)));
}

void FCacheRecordBuilderInternal::BuildAsync(IRequestOwner& Owner, FOnCacheRecordComplete&& OnComplete)
{
	ON_SCOPE_EXIT { delete this; };
	checkf(OnComplete, TEXT("Failed to build cache record for %s because the completion callback is null."),
		*WriteToString<96>(Key));
	OnComplete(Build());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecord CreateCacheRecord(ICacheRecordInternal* Record)
{
	return FCacheRecord(Record);
}

} // UE::DerivedData::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData
{

FCacheRecord::FCacheRecord(const FCacheKey& Key)
	: Record(new Private::FCacheRecordInternal(Key, {}, {}))
{
}

FCbPackage FCacheRecord::Save() const
{
	FCbPackage Package;
	FCbWriter Writer;
	Save(Package, Writer);
	Package.SetObject(Writer.Save().AsObject());
	return Package;
}

void FCacheRecord::Save(FCbPackage& Attachments, FCbWriter& Writer) const
{
	Writer.BeginObject();
	SaveToCompactBinary(Writer, *this, EMetaSaveFormat::Object, [&Attachments](FCbAttachment&& Attachment)
	{
		Attachments.AddAttachment(MoveTemp(Attachment));
	});
	Writer.EndObject();
}

FOptionalCacheRecord FCacheRecord::Load(const FCbPackage& Attachments, const FCbObject& Object)
{
	FOptionalCacheRecord OptionalRecord;
	LoadFromCompactBinary(Object.AsFieldView(), OptionalRecord, [&Attachments](const FIoHash& Hash)
	{
		if (const FCbAttachment* Attachment = Attachments.FindAttachment(Hash))
		{
			return *Attachment;
		}
		return FCbAttachment();
	});
	return OptionalRecord;
}

FOptionalCacheRecord FCacheRecord::Load(const FCbPackage& Package)
{
	return Load(Package, Package.GetObject());
}

FCacheRecord FCacheRecord::CreateByMove(const FCacheKey& Key, FCbObject&& Meta, TArray<FValueWithId>&& Values)
{
	return FCacheRecord(new Private::FCacheRecordInternal(Key, MoveTemp(Meta), MoveTemp(Values)));
}

FCacheRecordBuilder::FCacheRecordBuilder(const FCacheKey& Key)
	: RecordBuilder(new Private::FCacheRecordBuilderInternal(Key))
{
}

void FCacheRecordBuilder::AddValue(const FValueId& Id, const FCompositeBuffer& Buffer, const uint64 BlockSize)
{
	return RecordBuilder->AddValue(Id, FValue::Compress(Buffer, BlockSize));
}

void FCacheRecordBuilder::AddValue(const FValueId& Id, const FSharedBuffer& Buffer, const uint64 BlockSize)
{
	return RecordBuilder->AddValue(Id, FValue::Compress(Buffer, BlockSize));
}

void FCacheRecordBuilder::AddValue(const FValueWithId& Value)
{
	return RecordBuilder->AddValue(Value.GetId(), Value);
}

} // UE::DerivedData

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData::Private
{

uint64 GetCacheRecordCompressedSize(const FCacheRecord& Record)
{
	const uint64 MetaSize = Record.GetMeta().GetSize();
	return int64(Algo::TransformAccumulate(Record.GetValues(),
		[](const FValueWithId& Value) { return Value.GetData().GetCompressedSize(); }, MetaSize));
}

uint64 GetCacheRecordTotalRawSize(const FCacheRecord& Record)
{
	const uint64 MetaSize = Record.GetMeta().GetSize();
	return int64(Algo::TransformAccumulate(Record.GetValues(), &FValueWithId::GetRawSize, MetaSize));
}

uint64 GetCacheRecordRawSize(const FCacheRecord& Record)
{
	const uint64 MetaSize = Record.GetMeta().GetSize();
	return int64(Algo::TransformAccumulate(Record.GetValues(),
		[](const FValueWithId& Value) { return Value.GetData().GetRawSize(); }, MetaSize));
}

} // UE::DerivedData::Private
