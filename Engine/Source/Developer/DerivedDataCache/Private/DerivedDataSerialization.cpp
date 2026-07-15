// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataSerialization.h"

#include "Algo/Compare.h"
#include "Algo/IsSorted.h"
#include "Algo/Sort.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheMethod.h"
#include "DerivedDataCachePolicy.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataValue.h"
#include "DerivedDataValueId.h"
#include "Misc/NotNull.h"
#include "Misc/Optional.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValue.h"
#include "Serialization/CompactBinaryWriter.h"

namespace UE::DerivedData
{

FCbWriter& operator<<(FCbWriter& Writer, const FCacheGetRequest& Request)
{
	SaveToCompactBinary(Writer, Request);
	return Writer;
}

FCbWriter& operator<<(FCbWriter& Writer, const FCacheGetValueRequest& Request)
{
	SaveToCompactBinary(Writer, Request);
	return Writer;
}

FCbWriter& operator<<(FCbWriter& Writer, const FCacheGetChunkRequest& Request)
{
	SaveToCompactBinary(Writer, Request);
	return Writer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SaveMetaToCompactBinary(FCbWriter& Writer, const FCbObject& Meta, EMetaSaveFormat MetaFormat, FSaveAttachmentFn SaveAttachment)
{
	if (Meta)
	{
		if (MetaFormat == EMetaSaveFormat::Object)
		{
			Writer.AddObject(ANSITEXTVIEW("Meta"), Meta);
		}
		else
		{
			FCbAttachment MetaAttachment(Meta);
			Writer.AddObjectAttachment(ANSITEXTVIEW("MetaHash"), MetaAttachment.GetHash());
			Writer.AddInteger(ANSITEXTVIEW("MetaSize"), MetaAttachment.AsObject().GetSize());
			if (SaveAttachment)
			{
				SaveAttachment(MoveTemp(MetaAttachment));
			}
		}
	}
}

inline bool LoadMetaFromObject(FCbValue Value, FCbObject& OutMeta)
{
	if (FCbFieldType::IsObject(Value.GetType()))
	{
		OutMeta = FCbObject::Clone(Value.AsObjectView());
		return true;
	}
	return false;
}

inline bool LoadMetaFromHashAndSize(FCbValue Hash, FCbValue Size, FCbObject& OutMeta, TNotNull<FLoadAttachmentFn> LoadAttachment)
{
	if (FCbFieldType::IsHash(Hash.GetType()) && Size.GetType() == ECbFieldType::IntegerPositive)
	{
		FCbObject Meta = LoadAttachment(Hash.AsHash()).AsObject();
		if (Meta.GetSize() == Size.AsIntegerPositive())
		{
			OutMeta = MoveTemp(Meta);
			return true;
		}
	}
	return false;
}

bool LoadMetaFromCompactBinary(FCbFieldView Field, FCbObject& OutMeta, FLoadAttachmentFn LoadAttachment)
{
	if (LoadMetaFromObject(Field[ANSITEXTVIEW("Meta")], OutMeta))
	{
		return true;
	}
	else if (LoadAttachment)
	{
		if (LoadMetaFromHashAndSize(Field[ANSITEXTVIEW("MetaHash")], Field[ANSITEXTVIEW("MetaSize")], OutMeta, LoadAttachment))
		{
			return true;
		}
	}
	return false;
}

template <CIntegral FoundMaskType, FoundMaskType FoundMeta, FoundMaskType FoundMetaHash, FoundMaskType FoundMetaSize>
inline void LoadMetaFromCompactBinaryIterator(
	FoundMaskType& FoundMask,
	FCbFieldViewIterator& It,
	FCbValue& MetaObjectValue,
	FCbValue& MetaHashValue,
	FCbValue& MetaSizeValue)
{
	if (!(FoundMask & FoundMeta) && It.GetName() == ANSITEXTVIEW("Meta"))
	{
		MetaObjectValue = It;
		FoundMask |= FoundMeta;
		++It;
	}
	else
	{
		if (!(FoundMask & FoundMetaHash) && It.GetName() == ANSITEXTVIEW("MetaHash"))
		{
			MetaHashValue = It;
			FoundMask |= FoundMetaHash;
			++It;
		}
		if (!(FoundMask & FoundMetaSize) && It.GetName() == ANSITEXTVIEW("MetaSize"))
		{
			MetaSizeValue = It;
			FoundMask |= FoundMetaSize;
			++It;
		}
	}
}

inline bool LoadMetaFromCompactBinaryValues(
	FCbValue& MetaObjectValue,
	FCbValue& MetaHashValue,
	FCbValue& MetaSizeValue,
	FCbObject& OutMeta,
	FLoadAttachmentFn LoadAttachment)
{
	if (MetaObjectValue)
	{
		return LoadMetaFromObject(MetaObjectValue, OutMeta);
	}
	else if (LoadAttachment && MetaHashValue && MetaSizeValue)
	{
		return LoadMetaFromHashAndSize(MetaHashValue, MetaSizeValue, OutMeta, LoadAttachment);
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline bool LoadValuesFromCompactBinary(FCbArrayView ValuesArray, TArray<FValueWithId>& OutValues, FLoadAttachmentFn LoadAttachment)
{
	bool bOk = true;
	OutValues.Reset(int32(ValuesArray.Num()));
	for (FCbFieldView ValueField : ValuesArray)
	{
		FValueWithId& Value = OutValues.AddDefaulted_GetRef();
		bOk &= LoadFromCompactBinary(ValueField, Value, LoadAttachment);
	}
	if (UNLIKELY(!Algo::IsSorted(OutValues)))
	{
		Algo::Sort(OutValues);
	}
	return bOk;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SaveToCompactBinary(FCbWriter& Writer, const FCachePutRequest& Request, const FCachePutResponse* Response, FSaveAttachmentFn SaveAttachment)
{
	check(!Response || Request.Record.GetKey() == Response->Record.GetKey());
	check(!Response || Request.UserData == Response->UserData);
	Writer.BeginObject();
	if (!Request.Name.IsEmpty())
	{
		Writer << ANSITEXTVIEW("Name") << Request.Name;
	}
	Writer << ANSITEXTVIEW("Key") << Request.Record.GetKey();
	SaveMetaToCompactBinary(Writer, Request.Record.GetMeta(), EMetaSaveFormat::Attachment, SaveAttachment);
	if (const TConstArrayView<FValueWithId> Values = Request.Record.GetValues(); !Values.IsEmpty())
	{
		Writer.BeginArray(ANSITEXTVIEW("Values"));
		for (const FValueWithId& Value : Values)
		{
			SaveToCompactBinary(Writer, Value, SaveAttachment);
		}
		Writer.EndArray();
	}
	if (!Request.Policy.IsDefault())
	{
		Writer << ANSITEXTVIEW("Policy") << Request.Policy;
	}
	if (Request.UserData != 0)
	{
		Writer << ANSITEXTVIEW("UserData") << Request.UserData;
	}
	if (Response && Response->Status != EStatus::Ok)
	{
		Writer << ANSITEXTVIEW("Status") << Response->Status;
	}
	if (Response)
	{
		const bool bMetaConflict = !Response->Record.GetMeta().Equals(Request.Record.GetMeta());
		const bool bValueConflict = !Algo::Compare(Response->Record.GetValues(), Request.Record.GetValues());
		if (bMetaConflict || bValueConflict)
		{
			Writer.BeginObject(ANSITEXTVIEW("Conflict"));
			SaveMetaToCompactBinary(Writer, Response->Record.GetMeta(), EMetaSaveFormat::Attachment, SaveAttachment);
			if (const TConstArrayView<FValueWithId> Values = Response->Record.GetValues(); !Values.IsEmpty())
			{
				Writer.BeginArray(ANSITEXTVIEW("Values"));
				for (const FValueWithId& Value : Values)
				{
					SaveToCompactBinary(Writer, Value, SaveAttachment);
				}
				Writer.EndArray();
			}
			Writer.EndObject();
		}
	}
	Writer.EndObject();
}

bool LoadFromCompactBinary(FCbFieldView Field, TOptional<FCachePutRequest>& OutRequest, TOptional<FCachePutResponse>* OutResponse, FLoadAttachmentFn LoadAttachment)
{
	bool bOk = Field.IsObject();

	FSharedString Name;
	FCacheKey Key;
	FCbValue MetaObjectValue;
	FCbValue MetaHashValue;
	FCbValue MetaSizeValue;
	FCbArrayView ValuesArray;
	FCacheRecordPolicy Policy;
	uint64 UserData = 0;
	EStatus Status = EStatus::Ok;

	FCbValue ConflictMetaObjectValue;
	FCbValue ConflictMetaHashValue;
	FCbValue ConflictMetaSizeValue;
	FCbArrayView ConflictValuesArray;

	constexpr static uint32 FoundName     = 1 << 0;
	constexpr static uint32 FoundKey      = 1 << 1;
	constexpr static uint32 FoundMeta     = 1 << 2;
	constexpr static uint32 FoundMetaHash = 1 << 3;
	constexpr static uint32 FoundMetaSize = 1 << 4;
	constexpr static uint32 FoundValues   = 1 << 5;
	constexpr static uint32 FoundPolicy   = 1 << 6;
	constexpr static uint32 FoundUserData = 1 << 7;
	constexpr static uint32 FoundStatus   = 1 << 8;
	constexpr static uint32 FoundConflict = 1 << 9;

	constexpr static uint32 FoundConflictMeta     = 1 << 28;
	constexpr static uint32 FoundConflictMetaHash = 1 << 29;
	constexpr static uint32 FoundConflictMetaSize = 1 << 30;
	constexpr static uint32 FoundConflictValues   = 1 << 31;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundName) && It.GetName() == ANSITEXTVIEW("Name"))
		{
			LoadFromCompactBinary(It, Name);
			FoundMask |= FoundName;
			++It;
		}
		if (!(FoundMask & FoundKey) && It.GetName() == ANSITEXTVIEW("Key"))
		{
			bOk &= LoadFromCompactBinary(It, Key);
			FoundMask |= FoundKey;
			++It;
		}
		LoadMetaFromCompactBinaryIterator<uint32, FoundMeta, FoundMetaHash, FoundMetaSize>(FoundMask, It, MetaObjectValue, MetaHashValue, MetaSizeValue);
		if (!(FoundMask & FoundValues) && It.GetName() == ANSITEXTVIEW("Values"))
		{
			ValuesArray = It.AsArrayView();
			FoundMask |= FoundValues;
			++It;
		}
		if (!(FoundMask & FoundPolicy) && It.GetName() == ANSITEXTVIEW("Policy"))
		{
			LoadFromCompactBinary(It, Policy);
			FoundMask |= FoundPolicy;
			++It;
		}
		if (!(FoundMask & FoundUserData) && It.GetName() == ANSITEXTVIEW("UserData"))
		{
			LoadFromCompactBinary(It, UserData, 0);
			FoundMask |= FoundUserData;
			++It;
		}
		if (!(FoundMask & FoundStatus) && It.GetName() == ANSITEXTVIEW("Status"))
		{
			LoadFromCompactBinary(It, Status, EStatus::Ok);
			FoundMask |= FoundStatus;
			++It;
		}
		if (!(FoundMask & FoundConflict) && It.GetName() == ANSITEXTVIEW("Conflict"))
		{
			for (FCbFieldViewIterator ConflictIt = It.CreateViewIterator(); ConflictIt;)
			{
				const uint32 LastFoundConflictMask = FoundMask;
				LoadMetaFromCompactBinaryIterator<uint32, FoundConflictMeta, FoundConflictMetaHash, FoundConflictMetaSize>(
					FoundMask, ConflictIt, ConflictMetaObjectValue, ConflictMetaHashValue, ConflictMetaSizeValue);
				if (!(FoundMask & FoundConflictValues) && ConflictIt.GetName() == ANSITEXTVIEW("Values"))
				{
					ConflictValuesArray = ConflictIt.AsArrayView();
					FoundMask |= FoundConflictValues;
					++ConflictIt;
				}
				if (LastFoundConflictMask == FoundMask)
				{
					++ConflictIt;
				}
			}
			FoundMask |= FoundConflict;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	FCbObject Meta;
	LoadMetaFromCompactBinaryValues(MetaObjectValue, MetaHashValue, MetaSizeValue, Meta, LoadAttachment);

	TArray<FValueWithId> Values;
	bOk &= LoadValuesFromCompactBinary(ValuesArray, Values, LoadAttachment);

	const bool bConflict = !!(FoundMask & FoundConflict);
	FCbObject ConflictMeta;
	TArray<FValueWithId> ConflictValues;
	if (OutResponse && bConflict)
	{
		LoadMetaFromCompactBinaryValues(ConflictMetaObjectValue, ConflictMetaHashValue, ConflictMetaSizeValue, ConflictMeta, LoadAttachment);
		bOk &= LoadValuesFromCompactBinary(ConflictValuesArray, ConflictValues, LoadAttachment);
	}

	if (bOk)
	{
		FCacheRecord Record = FCacheRecord::CreateByMove(Key, MoveTemp(Meta), MoveTemp(Values));
		if (OutResponse)
		{
			FCacheRecord ResponseRecord = bConflict ? FCacheRecord::CreateByMove(Key, MoveTemp(ConflictMeta), MoveTemp(ConflictValues)) : Record;
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			*OutResponse = {Name, Key, MoveTemp(ResponseRecord), UserData, Status};
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
		}
		OutRequest = {Name, MoveTemp(Record), Policy, UserData};
	}
	else
	{
		if (OutResponse)
		{
			OutResponse->Reset();
		}
		OutRequest.Reset();
	}

	return bOk;
}

void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetRequest& Request, const FCacheGetResponse* Response, FSaveAttachmentFn SaveAttachment)
{
	check(!Response || Request.Key == Response->Record.GetKey());
	check(!Response || Request.UserData == Response->UserData);
	Writer.BeginObject();
	if (!Request.Name.IsEmpty())
	{
		Writer << ANSITEXTVIEW("Name") << Request.Name;
	}
	Writer << ANSITEXTVIEW("Key") << Request.Key;
	if (Response)
	{
		SaveMetaToCompactBinary(Writer, Response->Record.GetMeta(), EMetaSaveFormat::Attachment, SaveAttachment);
		if (const TConstArrayView<FValueWithId> Values = Response->Record.GetValues(); !Values.IsEmpty())
		{
			Writer.BeginArray(ANSITEXTVIEW("Values"));
			for (const FValueWithId& Value : Values)
			{
				SaveToCompactBinary(Writer, Value, SaveAttachment);
			}
			Writer.EndArray();
		}
	}
	if (!Request.Policy.IsDefault())
	{
		Writer << ANSITEXTVIEW("Policy") << Request.Policy;
	}
	if (Request.UserData != 0)
	{
		Writer << ANSITEXTVIEW("UserData") << Request.UserData;
	}
	if (Response && Response->Status != EStatus::Ok)
	{
		Writer << ANSITEXTVIEW("Status") << Response->Status;
	}
	Writer.EndObject();
}

bool LoadFromCompactBinary(FCbFieldView Field, FCacheGetRequest& OutRequest, TOptional<FCacheGetResponse>* OutResponse, FLoadAttachmentFn LoadAttachment)
{
	bool bOk = Field.IsObject();

	FCbValue MetaObjectValue;
	FCbValue MetaHashValue;
	FCbValue MetaSizeValue;
	FCbArrayView ValuesArray;
	EStatus Status = EStatus::Ok;

	constexpr static uint32 FoundName     = 1 << 0;
	constexpr static uint32 FoundKey      = 1 << 1;
	constexpr static uint32 FoundMeta     = 1 << 2;
	constexpr static uint32 FoundMetaHash = 1 << 3;
	constexpr static uint32 FoundMetaSize = 1 << 4;
	constexpr static uint32 FoundValues   = 1 << 5;
	constexpr static uint32 FoundPolicy   = 1 << 6;
	constexpr static uint32 FoundUserData = 1 << 7;
	constexpr static uint32 FoundStatus   = 1 << 8;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundName) && It.GetName() == ANSITEXTVIEW("Name"))
		{
			LoadFromCompactBinary(It, OutRequest.Name);
			FoundMask |= FoundName;
			++It;
		}
		if (!(FoundMask & FoundKey) && It.GetName() == ANSITEXTVIEW("Key"))
		{
			bOk &= LoadFromCompactBinary(It, OutRequest.Key);
			FoundMask |= FoundKey;
			++It;
		}
		LoadMetaFromCompactBinaryIterator<uint32, FoundMeta, FoundMetaHash, FoundMetaSize>(FoundMask, It, MetaObjectValue, MetaHashValue, MetaSizeValue);
		if (!(FoundMask & FoundValues) && It.GetName() == ANSITEXTVIEW("Values"))
		{
			ValuesArray = It.AsArrayView();
			FoundMask |= FoundValues;
			++It;
		}
		if (!(FoundMask & FoundPolicy) && It.GetName() == ANSITEXTVIEW("Policy"))
		{
			LoadFromCompactBinary(It, OutRequest.Policy);
			FoundMask |= FoundPolicy;
			++It;
		}
		if (!(FoundMask & FoundUserData) && It.GetName() == ANSITEXTVIEW("UserData"))
		{
			LoadFromCompactBinary(It, OutRequest.UserData, 0);
			FoundMask |= FoundUserData;
			++It;
		}
		if (!(FoundMask & FoundStatus) && It.GetName() == ANSITEXTVIEW("Status"))
		{
			LoadFromCompactBinary(It, Status, EStatus::Ok);
			FoundMask |= FoundStatus;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	if (OutResponse)
	{
		FCbObject Meta;
		LoadMetaFromCompactBinaryValues(MetaObjectValue, MetaHashValue, MetaSizeValue, Meta, LoadAttachment);

		TArray<FValueWithId> Values;
		bOk &= LoadValuesFromCompactBinary(ValuesArray, Values, LoadAttachment);

		if (bOk)
		{
			*OutResponse = {OutRequest.Name, FCacheRecord::CreateByMove(OutRequest.Key, MoveTemp(Meta), MoveTemp(Values)), OutRequest.UserData, Status};
		}
		else
		{
			OutResponse->Reset();
		}
	}

	return bOk;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SaveToCompactBinary(FCbWriter& Writer, const FCachePutValueRequest& Request, const FCachePutValueResponse* Response, FSaveAttachmentFn SaveAttachment)
{
	check(!Response || Request.Key == Response->Key);
	check(!Response || Request.UserData == Response->UserData);
	Writer.BeginObject();
	if (!Request.Name.IsEmpty())
	{
		Writer << ANSITEXTVIEW("Name") << Request.Name;
	}
	Writer << ANSITEXTVIEW("Key") << Request.Key;
	if (Request.Value != FValue::Null)
	{
		Writer.SetName(ANSITEXTVIEW("Value"));
		SaveToCompactBinary(Writer, Request.Value, SaveAttachment);
	}
	if (Request.Policy != ECachePolicy::Default)
	{
		Writer << ANSITEXTVIEW("Policy") << Request.Policy;
	}
	if (Request.UserData != 0)
	{
		Writer << ANSITEXTVIEW("UserData") << Request.UserData;
	}
	if (Response && Response->Status != EStatus::Ok)
	{
		Writer << ANSITEXTVIEW("Status") << Response->Status;
	}
	if (Response && Request.Value != Response->Value)
	{
		Writer.BeginObject(ANSITEXTVIEW("Conflict"));
		if (Response->Value != FValue::Null)
		{
			Writer.SetName(ANSITEXTVIEW("Value"));
			SaveToCompactBinary(Writer, Response->Value, SaveAttachment);
		}
		Writer.EndObject();
	}
	Writer.EndObject();
}

bool LoadFromCompactBinary(FCbFieldView Field, FCachePutValueRequest& OutRequest, FCachePutValueResponse* OutResponse, FLoadAttachmentFn LoadAttachment)
{
	bool bOk = Field.IsObject();

	FValue ConflictValue;
	EStatus Status = EStatus::Ok;

	constexpr static uint32 FoundName     = 1 << 0;
	constexpr static uint32 FoundKey      = 1 << 1;
	constexpr static uint32 FoundValue    = 1 << 2;
	constexpr static uint32 FoundPolicy   = 1 << 3;
	constexpr static uint32 FoundUserData = 1 << 4;
	constexpr static uint32 FoundStatus   = 1 << 5;
	constexpr static uint32 FoundConflict = 1 << 6;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundName) && It.GetName() == ANSITEXTVIEW("Name"))
		{
			LoadFromCompactBinary(It, OutRequest.Name);
			FoundMask |= FoundName;
			++It;
		}
		if (!(FoundMask & FoundKey) && It.GetName() == ANSITEXTVIEW("Key"))
		{
			bOk &= LoadFromCompactBinary(It, OutRequest.Key);
			FoundMask |= FoundKey;
			++It;
		}
		if (!(FoundMask & FoundValue) && It.GetName() == ANSITEXTVIEW("Value"))
		{
			LoadFromCompactBinary(It, OutRequest.Value, LoadAttachment);
			FoundMask |= FoundValue;
			++It;
		}
		if (!(FoundMask & FoundPolicy) && It.GetName() == ANSITEXTVIEW("Policy"))
		{
			LoadFromCompactBinary(It, OutRequest.Policy, ECachePolicy::Default);
			FoundMask |= FoundPolicy;
			++It;
		}
		if (!(FoundMask & FoundUserData) && It.GetName() == ANSITEXTVIEW("UserData"))
		{
			LoadFromCompactBinary(It, OutRequest.UserData, 0);
			FoundMask |= FoundUserData;
			++It;
		}
		if (!(FoundMask & FoundStatus) && It.GetName() == ANSITEXTVIEW("Status"))
		{
			LoadFromCompactBinary(It, Status, EStatus::Ok);
			FoundMask |= FoundStatus;
			++It;
		}
		if (!(FoundMask & FoundConflict) && It.GetName() == ANSITEXTVIEW("Conflict"))
		{
			if (OutResponse)
			{
				if (FCbFieldViewIterator ConflictIt = It.CreateViewIterator(); ConflictIt && ConflictIt.GetName() == ANSITEXTVIEW("Value"))
				{
					LoadFromCompactBinary(ConflictIt, ConflictValue, LoadAttachment);
				}
			}
			FoundMask |= FoundConflict;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	if (OutResponse)
	{
		FValue ResponseValue = (FoundMask & FoundConflict) ? MoveTemp(ConflictValue) : CopyTemp(OutRequest.Value);
		*OutResponse = {OutRequest.Name, OutRequest.Key, MoveTemp(ResponseValue), OutRequest.UserData, Status};
	}

	return bOk;
}

void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetValueRequest& Request, const FCacheGetValueResponse* Response, FSaveAttachmentFn SaveAttachment)
{
	check(!Response || Request.Key == Response->Key);
	check(!Response || Request.UserData == Response->UserData);
	Writer.BeginObject();
	if (!Request.Name.IsEmpty())
	{
		Writer << ANSITEXTVIEW("Name") << Request.Name;
	}
	Writer << ANSITEXTVIEW("Key") << Request.Key;
	if (Response && Response->Value != FValue::Null)
	{
		Writer.SetName(ANSITEXTVIEW("Value"));
		SaveToCompactBinary(Writer, Response->Value, SaveAttachment);
	}
	if (Request.Policy != ECachePolicy::Default)
	{
		Writer << ANSITEXTVIEW("Policy") << Request.Policy;
	}
	if (Request.UserData != 0)
	{
		Writer << ANSITEXTVIEW("UserData") << Request.UserData;
	}
	if (Response && Response->Status != EStatus::Ok)
	{
		Writer << ANSITEXTVIEW("Status") << Response->Status;
	}
	Writer.EndObject();
}

bool LoadFromCompactBinary(FCbFieldView Field, FCacheGetValueRequest& OutRequest, FCacheGetValueResponse* OutResponse, FLoadAttachmentFn LoadAttachment)
{
	bool bOk = Field.IsObject();

	FValue Value;
	EStatus Status = EStatus::Ok;

	constexpr static uint32 FoundName     = 1 << 0;
	constexpr static uint32 FoundKey      = 1 << 1;
	constexpr static uint32 FoundValue    = 1 << 2;
	constexpr static uint32 FoundPolicy   = 1 << 3;
	constexpr static uint32 FoundUserData = 1 << 4;
	constexpr static uint32 FoundStatus   = 1 << 5;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundName) && It.GetName() == ANSITEXTVIEW("Name"))
		{
			LoadFromCompactBinary(It, OutRequest.Name);
			FoundMask |= FoundName;
			++It;
		}
		if (!(FoundMask & FoundKey) && It.GetName() == ANSITEXTVIEW("Key"))
		{
			bOk &= LoadFromCompactBinary(It, OutRequest.Key);
			FoundMask |= FoundKey;
			++It;
		}
		if (!(FoundMask & FoundValue) && It.GetName() == ANSITEXTVIEW("Value"))
		{
			LoadFromCompactBinary(It, Value, LoadAttachment);
			FoundMask |= FoundValue;
			++It;
		}
		if (!(FoundMask & FoundPolicy) && It.GetName() == ANSITEXTVIEW("Policy"))
		{
			LoadFromCompactBinary(It, OutRequest.Policy, ECachePolicy::Default);
			FoundMask |= FoundPolicy;
			++It;
		}
		if (!(FoundMask & FoundUserData) && It.GetName() == ANSITEXTVIEW("UserData"))
		{
			LoadFromCompactBinary(It, OutRequest.UserData, 0);
			FoundMask |= FoundUserData;
			++It;
		}
		if (!(FoundMask & FoundStatus) && It.GetName() == ANSITEXTVIEW("Status"))
		{
			LoadFromCompactBinary(It, Status, EStatus::Ok);
			FoundMask |= FoundStatus;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	if (OutResponse)
	{
		*OutResponse = {OutRequest.Name, OutRequest.Key, Value, OutRequest.UserData, Status};
	}

	return bOk;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetChunkRequest& Request, const FCacheGetChunkResponse* Response, FSaveAttachmentFn SaveAttachment)
{
	check(!Response || Request.Key == Response->Key);
	check(!Response || Request.UserData == Response->UserData);
	Writer.BeginObject();
	if (!Request.Name.IsEmpty())
	{
		Writer << ANSITEXTVIEW("Name") << Request.Name;
	}
	Writer << ANSITEXTVIEW("Key") << Request.Key;
	if (Request.Id.IsValid())
	{
		Writer << ANSITEXTVIEW("Id") << Request.Id;
	}
	if (Request.RawOffset != 0)
	{
		Writer << ANSITEXTVIEW("RawOffset") << Request.RawOffset;
	}
	if (Request.RawSize != MAX_uint64)
	{
		Writer << ANSITEXTVIEW("RawSize") << Request.RawSize;
	}
	if (!Request.RawHash.IsZero())
	{
		Writer << ANSITEXTVIEW("RawHash") << Request.RawHash;
	}
	if (Response && !Response->RawHash.IsZero())
	{
		Writer << ANSITEXTVIEW("Value") << FValue(Response->RawHash, Response->RawSize);
		if (SaveAttachment && Response->RawData)
		{
			const FIoHash FragmentHash = FIoHash::HashBuffer(Response->RawData);
			FCbAttachment Attachment(Response->RawData, FragmentHash);
			if (FragmentHash != Response->RawHash)
			{
				Writer.AddAttachment(ANSITEXTVIEW("FragmentHash"), Attachment);
			}
			SaveAttachment(MoveTemp(Attachment));
		}
	}
	if (Request.Policy != ECachePolicy::Default)
	{
		Writer << ANSITEXTVIEW("Policy") << Request.Policy;
	}
	if (Request.UserData != 0)
	{
		Writer << ANSITEXTVIEW("UserData") << Request.UserData;
	}
	if (Response && Response->Status != EStatus::Ok)
	{
		Writer << ANSITEXTVIEW("Status") << Response->Status;
	}
	Writer.EndObject();
}

bool LoadFromCompactBinary(FCbFieldView Field, FCacheGetChunkRequest& OutRequest, FCacheGetChunkResponse* OutResponse, FLoadAttachmentFn LoadAttachment)
{
	bool bOk = Field.IsObject();

	FValue Value;
	FIoHash FragmentHash;
	EStatus Status = EStatus::Ok;

	constexpr static uint32 FoundName         = 1 << 0;
	constexpr static uint32 FoundKey          = 1 << 1;
	constexpr static uint32 FoundId           = 1 << 2;
	constexpr static uint32 FoundRawOffset    = 1 << 3;
	constexpr static uint32 FoundRawSize      = 1 << 4;
	constexpr static uint32 FoundRawHash      = 1 << 5;
	constexpr static uint32 FoundValue        = 1 << 6;
	constexpr static uint32 FoundFragmentHash = 1 << 7;
	constexpr static uint32 FoundPolicy       = 1 << 8;
	constexpr static uint32 FoundUserData     = 1 << 9;
	constexpr static uint32 FoundStatus       = 1 << 10;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundName) && It.GetName() == ANSITEXTVIEW("Name"))
		{
			LoadFromCompactBinary(It, OutRequest.Name);
			FoundMask |= FoundName;
			++It;
		}
		if (!(FoundMask & FoundKey) && It.GetName() == ANSITEXTVIEW("Key"))
		{
			bOk &= LoadFromCompactBinary(It, OutRequest.Key);
			FoundMask |= FoundKey;
			++It;
		}
		if (!(FoundMask & FoundId) && It.GetName() == ANSITEXTVIEW("Id"))
		{
			bOk &= LoadFromCompactBinary(It, OutRequest.Id);
			FoundMask |= FoundId;
			++It;
		}
		if (!(FoundMask & FoundRawOffset) && It.GetName() == ANSITEXTVIEW("RawOffset"))
		{
			bOk &= LoadFromCompactBinary(It, OutRequest.RawOffset, 0);
			FoundMask |= FoundRawOffset;
			++It;
		}
		if (!(FoundMask & FoundRawSize) && It.GetName() == ANSITEXTVIEW("RawSize"))
		{
			bOk &= LoadFromCompactBinary(It, OutRequest.RawSize, MAX_uint64);
			FoundMask |= FoundRawSize;
			++It;
		}
		if (!(FoundMask & FoundRawHash) && It.GetName() == ANSITEXTVIEW("RawHash"))
		{
			bOk &= LoadFromCompactBinary(It, OutRequest.RawHash);
			FoundMask |= FoundRawHash;
			++It;
		}
		if (!(FoundMask & FoundValue) && It.GetName() == ANSITEXTVIEW("Value"))
		{
			if (OutResponse)
			{
				LoadFromCompactBinary(It, Value);
			}
			FoundMask |= FoundValue;
			++It;
		}
		if (!(FoundMask & FoundFragmentHash) && It.GetName() == ANSITEXTVIEW("FragmentHash"))
		{
			if (OutResponse)
			{
				LoadFromCompactBinary(It, FragmentHash);
			}
			FoundMask |= FoundFragmentHash;
			++It;
		}
		if (!(FoundMask & FoundPolicy) && It.GetName() == ANSITEXTVIEW("Policy"))
		{
			LoadFromCompactBinary(It, OutRequest.Policy, ECachePolicy::Default);
			FoundMask |= FoundPolicy;
			++It;
		}
		if (!(FoundMask & FoundUserData) && It.GetName() == ANSITEXTVIEW("UserData"))
		{
			LoadFromCompactBinary(It, OutRequest.UserData, 0);
			FoundMask |= FoundUserData;
			++It;
		}
		if (!(FoundMask & FoundStatus) && It.GetName() == ANSITEXTVIEW("Status"))
		{
			if (OutResponse)
			{
				LoadFromCompactBinary(It, Status, EStatus::Ok);
			}
			FoundMask |= FoundStatus;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	if (OutResponse)
	{
		OutResponse->Name = OutRequest.Name;
		OutResponse->Key = OutRequest.Key;
		OutResponse->Id = OutRequest.Id;
		OutResponse->RawOffset = OutRequest.RawOffset;
		OutResponse->UserData = OutRequest.UserData;
		OutResponse->Status = Status;
		OutResponse->RawHash = Value.GetRawHash();
		OutResponse->RawSize = Value.GetRawSize();
		if (LoadAttachment && !OutResponse->RawHash.IsZero())
		{
			const FCbAttachment Attachment = LoadAttachment(!FragmentHash.IsZero() ? FragmentHash : OutResponse->RawHash);
			if (FSharedBuffer Data = Attachment.AsBinary())
			{
				OutResponse->RawData = MoveTemp(Data);
			}
			else if (const FCompressedBuffer& CompressedData = Attachment.AsCompressedBinary())
			{
				if (CompressedData.GetRawHash() == OutResponse->RawHash && CompressedData.GetRawSize() == OutResponse->RawSize)
				{
					OutResponse->RawData = CompressedData.Decompress();
				}
			}
		}
	}

	return bOk;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbWriter& operator<<(FCbWriter& Writer, const FCacheBucket Bucket)
{
	Writer.AddString(Bucket.ToString());
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FCacheBucket& OutBucket)
{
	if (const FUtf8StringView Bucket = Field.AsString(); !Field.HasError() && FCacheBucket::IsValidName(Bucket))
	{
		OutBucket = FCacheBucket(Bucket);
		return true;
	}
	OutBucket.Reset();
	return false;
}

FCbWriter& operator<<(FCbWriter& Writer, const FCacheKey& Key)
{
	Writer.BeginObject();
	Writer << ANSITEXTVIEW("Bucket") << Key.Bucket;
	Writer << ANSITEXTVIEW("Hash") << Key.Hash;
	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(const FCbFieldView Field, FCacheKey& OutKey)
{
	bool bOk = Field.IsObject();

	constexpr static uint32 FoundBucket = 1 << 0;
	constexpr static uint32 FoundHash   = 1 << 1;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundBucket) && It.GetName() == ANSITEXTVIEW("Bucket"))
		{
			bOk &= LoadFromCompactBinary(It, OutKey.Bucket);
			FoundMask |= FoundBucket;
			++It;
		}
		if (!(FoundMask & FoundHash) && It.GetName() == ANSITEXTVIEW("Hash"))
		{
			bOk &= LoadFromCompactBinary(It, OutKey.Hash);
			FoundMask |= FoundHash;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	return bOk;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbWriter& operator<<(FCbWriter& Writer, const ECacheMethod Method)
{
	Writer.AddString(WriteToUtf8String<16>(Method));
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, ECacheMethod& OutMethod)
{
	if (TryLexFromString(OutMethod, Field.AsString()))
	{
		return true;
	}
	OutMethod = {};
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbWriter& operator<<(FCbWriter& Writer, const ECachePolicy Policy)
{
	Writer.AddString(WriteToUtf8String<64>(Policy));
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, ECachePolicy& OutPolicy, const ECachePolicy Default)
{
	if (TryLexFromString(OutPolicy, Field.AsString()))
	{
		return true;
	}
	OutPolicy = Default;
	return false;
}

FCbWriter& operator<<(FCbWriter& Writer, const FCacheValuePolicy& Policy)
{
	Writer.BeginObject();
	Writer << ANSITEXTVIEW("Id") << Policy.Id;
	Writer << ANSITEXTVIEW("Policy") << Policy.Policy;
	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FCacheValuePolicy& OutPolicy)
{
	bool bOk = Field.IsObject();

	constexpr static uint32 FoundId     = 1 << 0;
	constexpr static uint32 FoundPolicy = 1 << 1;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundId) && It.GetName() == ANSITEXTVIEW("Id"))
		{
			bOk &= LoadFromCompactBinary(It, OutPolicy.Id);
			FoundMask |= FoundId;
			++It;
		}
		if (!(FoundMask & FoundPolicy) && It.GetName() == ANSITEXTVIEW("Policy"))
		{
			bOk &= LoadFromCompactBinary(It, OutPolicy.Policy, ECachePolicy::Default);
			FoundMask |= FoundPolicy;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	return bOk;
}

FCbWriter& operator<<(FCbWriter& Writer, const FCacheRecordPolicy& Policy)
{
	Writer.BeginObject();
	Writer << ANSITEXTVIEW("BasePolicy") << Policy.GetBasePolicy();
	if (!Policy.IsUniform())
	{
		Writer.BeginArray(ANSITEXTVIEW("ValuePolicies"));
		for (const FCacheValuePolicy& Value : Policy.GetValuePolicies())
		{
			Writer << Value;
		}
		Writer.EndArray();
	}
	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FCacheRecordPolicy& OutPolicy)
{
	ECachePolicy BasePolicy;
	if (!LoadFromCompactBinary(Field[ANSITEXTVIEW("BasePolicy")], BasePolicy, ECachePolicy::Default))
	{
		OutPolicy = {};
		return false;
	}
	FCacheRecordPolicyBuilder Builder(BasePolicy);

	for (FCbFieldView Value : Field[ANSITEXTVIEW("ValuePolicies")])
	{
		FCacheValuePolicy ValuePolicy;
		if (!LoadFromCompactBinary(Value, ValuePolicy) ||
			ValuePolicy.Id.IsNull() ||
			EnumHasAnyFlags(ValuePolicy.Policy, ~FCacheValuePolicy::PolicyMask))
		{
			OutPolicy = {};
			return false;
		}
		Builder.AddValuePolicy(ValuePolicy);
	}

	OutPolicy = Builder.Build();
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SaveToCompactBinary(FCbWriter& Writer, const FCacheRecord& Record, EMetaSaveFormat MetaFormat, FSaveAttachmentFn SaveAttachment)
{
	Writer << ANSITEXTVIEW("Key") << Record.GetKey();

	SaveMetaToCompactBinary(Writer, Record.GetMeta(), MetaFormat, SaveAttachment);

	if (const TConstArrayView<FValueWithId> Values = Record.GetValues(); !Values.IsEmpty())
	{
		Writer.BeginArray(ANSITEXTVIEW("Values"));
		for (const FValueWithId& Value : Values)
		{
			SaveToCompactBinary(Writer, Value, SaveAttachment);
		}
		Writer.EndArray();
	}
}

bool LoadFromCompactBinary(FCbFieldView Field, FOptionalCacheRecord& OutRecord, FLoadAttachmentFn LoadAttachment)
{
	FCacheKey Key;
	FCbObject Meta;
	TArray<FValueWithId> Values;

	if (!LoadFromCompactBinary(Field[ANSITEXTVIEW("Key")], Key))
	{
		return false;
	}

	if (FCbValue MetaValue = Field[ANSITEXTVIEW("Meta")])
	{
		LoadMetaFromObject(MetaValue, Meta);
	}
	else if (LoadAttachment)
	{
		FCbValue MetaHashValue = Field[ANSITEXTVIEW("MetaHash")];
		FCbValue MetaSizeValue = Field[ANSITEXTVIEW("MetaSize")];
		if (MetaHashValue && MetaSizeValue)
		{
			LoadMetaFromHashAndSize(MetaHashValue, MetaSizeValue, Meta, LoadAttachment);
		}
	}

	if (!LoadValuesFromCompactBinary(Field[ANSITEXTVIEW("Values")].AsArrayView(), Values, LoadAttachment))
	{
		return false;
	}

	OutRecord = FCacheRecord::CreateByMove(Key, MoveTemp(Meta), MoveTemp(Values));
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbWriter& operator<<(FCbWriter& Writer, const EPriority Priority)
{
	Writer.AddString(WriteToUtf8String<16>(Priority));
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, EPriority& OutPriority, const EPriority Default)
{
	if (TryLexFromString(OutPriority, Field.AsString()))
	{
		return true;
	}
	OutPriority = Default;
	return false;
}

FCbWriter& operator<<(FCbWriter& Writer, const EStatus Status)
{
	Writer.AddString(WriteToUtf8String<16>(Status));
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, EStatus& OutStatus, const EStatus Default)
{
	if (TryLexFromString(OutStatus, Field.AsString()))
	{
		return true;
	}
	OutStatus = Default;
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbWriter& operator<<(FCbWriter& Writer, const FValue& Value)
{
	SaveToCompactBinary(Writer, Value);
	return Writer;
}

void SaveToCompactBinary(FCbWriter& Writer, const FValue& Value, FSaveAttachmentFn SaveAttachment)
{
	if (SaveAttachment && Value.GetData())
	{
		SaveAttachment(FCbAttachment(Value.GetData()));
	}
	Writer.BeginObject();
	Writer.AddBinaryAttachment(ANSITEXTVIEW("RawHash"), Value.GetRawHash());
	Writer.AddInteger(ANSITEXTVIEW("RawSize"), Value.GetRawSize());
	Writer.EndObject();
}

bool LoadFromCompactBinary(FCbFieldView Field, FValue& OutValue, FLoadAttachmentFn LoadAttachment)
{
	bool bOk = Field.IsObject();

	FIoHash RawHash;
	uint64 RawSize = 0;

	constexpr static uint32 FoundRawHash = 1 << 0;
	constexpr static uint32 FoundRawSize = 1 << 1;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundRawHash) && It.GetName() == ANSITEXTVIEW("RawHash"))
		{
			bOk &= LoadFromCompactBinary(It, RawHash);
			FoundMask |= FoundRawHash;
			++It;
		}
		if (!(FoundMask & FoundRawSize) && It.GetName() == ANSITEXTVIEW("RawSize"))
		{
			bOk &= LoadFromCompactBinary(It, RawSize);
			FoundMask |= FoundRawSize;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	if (bOk && LoadAttachment)
	{
		const FCbAttachment Attachment = LoadAttachment(RawHash);
		const FCompressedBuffer& Compressed = Attachment.AsCompressedBinary();
		if (Compressed.GetRawHash() == RawHash && Compressed.GetRawSize() == RawSize)
		{
			OutValue = FValue(Compressed);
			return bOk;
		}
	}

	OutValue = FValue(RawHash, RawSize);
	return bOk;
}

FCbWriter& operator<<(FCbWriter& Writer, const FValueWithId& Value)
{
	SaveToCompactBinary(Writer, Value);
	return Writer;
}

void SaveToCompactBinary(FCbWriter& Writer, const FValueWithId& Value, FSaveAttachmentFn SaveAttachment)
{
	if (SaveAttachment && Value.GetData())
	{
		SaveAttachment(FCbAttachment(Value.GetData()));
	}
	Writer.BeginObject();
	Writer.AddObjectId(ANSITEXTVIEW("Id"), Value.GetId());
	Writer.AddBinaryAttachment(ANSITEXTVIEW("RawHash"), Value.GetRawHash());
	Writer.AddInteger(ANSITEXTVIEW("RawSize"), Value.GetRawSize());
	Writer.EndObject();
}

bool LoadFromCompactBinary(FCbFieldView Field, FValueWithId& OutValue, FLoadAttachmentFn LoadAttachment)
{
	bool bOk = Field.IsObject();

	FValueId Id;
	FIoHash RawHash;
	uint64 RawSize = 0;

	constexpr static uint32 FoundId      = 1 << 0;
	constexpr static uint32 FoundRawHash = 1 << 1;
	constexpr static uint32 FoundRawSize = 1 << 2;

	uint32 FoundMask = 0;
	for (FCbFieldViewIterator It = Field.CreateViewIterator(); It;)
	{
		const uint32 LastFoundMask = FoundMask;
		if (!(FoundMask & FoundId) && It.GetName() == ANSITEXTVIEW("Id"))
		{
			bOk &= LoadFromCompactBinary(It, Id);
			FoundMask |= FoundId;
			++It;
		}
		if (!(FoundMask & FoundRawHash) && It.GetName() == ANSITEXTVIEW("RawHash"))
		{
			bOk &= LoadFromCompactBinary(It, RawHash);
			FoundMask |= FoundRawHash;
			++It;
		}
		if (!(FoundMask & FoundRawSize) && It.GetName() == ANSITEXTVIEW("RawSize"))
		{
			bOk &= LoadFromCompactBinary(It, RawSize);
			FoundMask |= FoundRawSize;
			++It;
		}
		if (LastFoundMask == FoundMask)
		{
			++It;
		}
	}

	if (Id.IsNull())
	{
		// An invalid Id asserts in FValueWithId().
		return false;
	}

	if (bOk && LoadAttachment)
	{
		const FCbAttachment Attachment = LoadAttachment(RawHash);
		const FCompressedBuffer& Compressed = Attachment.AsCompressedBinary();
		if (Compressed.GetRawHash() == RawHash && Compressed.GetRawSize() == RawSize)
		{
			OutValue = FValueWithId(Id, Compressed);
			return bOk;
		}
	}

	OutValue = FValueWithId(Id, RawHash, RawSize);
	return bOk;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbWriter& operator<<(FCbWriter& Writer, const FValueId& Id)
{
	Writer.AddObjectId(Id);
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FValueId& OutId)
{
	if (const FCbObjectId ObjectId = Field.AsObjectId(); !Field.HasError())
	{
		OutId = ObjectId;
		return true;
	}
	OutId.Reset();
	return false;
}

} // UE::DerivedData
