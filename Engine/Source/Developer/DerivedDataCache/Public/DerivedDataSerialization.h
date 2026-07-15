// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/OptionalFwd.h"
#include "Templates/FunctionWithContext.h"

#define UE_API DERIVEDDATACACHE_API

class FCbAttachment;
class FCbFieldView;
class FCbObject;
class FCbWriter;
struct FIoHash;

namespace UE::DerivedData { class FCacheBucket; }
namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class FCacheRecordPolicy; }
namespace UE::DerivedData { class FOptionalCacheRecord; }
namespace UE::DerivedData { class FValue; }
namespace UE::DerivedData { class FValueWithId; }
namespace UE::DerivedData { struct FCacheGetChunkRequest; }
namespace UE::DerivedData { struct FCacheGetChunkResponse; }
namespace UE::DerivedData { struct FCacheGetRequest; }
namespace UE::DerivedData { struct FCacheGetResponse; }
namespace UE::DerivedData { struct FCacheGetValueRequest; }
namespace UE::DerivedData { struct FCacheGetValueResponse; }
namespace UE::DerivedData { struct FCacheKey; }
namespace UE::DerivedData { struct FCachePutRequest; }
namespace UE::DerivedData { struct FCachePutResponse; }
namespace UE::DerivedData { struct FCachePutValueRequest; }
namespace UE::DerivedData { struct FCachePutValueResponse; }
namespace UE::DerivedData { struct FCacheValuePolicy; }
namespace UE::DerivedData { struct FValueId; }
namespace UE::DerivedData { enum class ECacheMethod : uint8; }
namespace UE::DerivedData { enum class ECachePolicy : uint32; }
namespace UE::DerivedData { enum class EPriority : uint8; }
namespace UE::DerivedData { enum class EStatus : uint8; }

namespace UE::DerivedData
{

using FSaveAttachmentFn = TFunctionWithContext<void (FCbAttachment&& Attachment)>;
using FLoadAttachmentFn = TFunctionWithContext<FCbAttachment (const FIoHash& Hash)>;

/** Format in which to save metadata. */
enum class EMetaSaveFormat
{
	/** Save metadata as an object field. */
	Object,
	/** Save metadata as an attachment referenced by its hash and size. */
	Attachment,
};

/** Save metadata to a stream of named fields. Caller must call BeginObject/EndObject to save this into an object. */
UE_API void SaveMetaToCompactBinary(FCbWriter& Writer, const FCbObject& Meta, EMetaSaveFormat MetaFormat, FSaveAttachmentFn SaveAttachment = nullptr);
UE_API bool LoadMetaFromCompactBinary(FCbFieldView Field, FCbObject& OutMeta, FLoadAttachmentFn LoadAttachment = nullptr);

// Types from DerivedDataCache.h

UE_DEPRECATED(5.8, "Call SaveToCompactBinary(Writer, Request).")
UE_API FCbWriter& operator<<(FCbWriter& Writer, const FCacheGetRequest& Request);
UE_DEPRECATED(5.8, "Call SaveToCompactBinary(Writer, Request).")
UE_API FCbWriter& operator<<(FCbWriter& Writer, const FCacheGetValueRequest& Request);
UE_DEPRECATED(5.8, "Call SaveToCompactBinary(Writer, Request).")
UE_API FCbWriter& operator<<(FCbWriter& Writer, const FCacheGetChunkRequest& Request);

UE_API void SaveToCompactBinary(FCbWriter& Writer, const FCachePutRequest& Request, const FCachePutResponse* Response = nullptr, FSaveAttachmentFn SaveAttachment = nullptr);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, TOptional<FCachePutRequest>& OutRequest, TOptional<FCachePutResponse>* OutResponse = nullptr, FLoadAttachmentFn LoadAttachment = nullptr);

UE_API void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetRequest& Request, const FCacheGetResponse* Response = nullptr, FSaveAttachmentFn SaveAttachment = nullptr);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FCacheGetRequest& OutRequest, TOptional<FCacheGetResponse>* OutResponse = nullptr, FLoadAttachmentFn LoadAttachment = nullptr);

UE_API void SaveToCompactBinary(FCbWriter& Writer, const FCachePutValueRequest& Request, const FCachePutValueResponse* Response = nullptr, FSaveAttachmentFn SaveAttachment = nullptr);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FCachePutValueRequest& OutRequest, FCachePutValueResponse* OutResponse = nullptr, FLoadAttachmentFn LoadAttachment = nullptr);

UE_API void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetValueRequest& Request, const FCacheGetValueResponse* Response = nullptr, FSaveAttachmentFn SaveAttachment = nullptr);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FCacheGetValueRequest& OutRequest, FCacheGetValueResponse* OutResponse = nullptr, FLoadAttachmentFn LoadAttachment = nullptr);

UE_API void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetChunkRequest& Request, const FCacheGetChunkResponse* Response = nullptr, FSaveAttachmentFn SaveAttachment = nullptr);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FCacheGetChunkRequest& OutRequest, FCacheGetChunkResponse* OutResponse = nullptr, FLoadAttachmentFn LoadAttachment = nullptr);

// Types from DerivedDataCacheKey.h

UE_API FCbWriter& operator<<(FCbWriter& Writer, ECachePolicy Policy);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, ECachePolicy& OutPolicy, ECachePolicy Default);
UE_DEPRECATED(5.8, "Call the overload with an explicit default of ECachePolicy::Default.")
UE_API bool LoadFromCompactBinary(FCbFieldView Field, ECachePolicy& OutPolicy);

UE_API FCbWriter& operator<<(FCbWriter& Writer, FCacheBucket Bucket);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FCacheBucket& OutBucket);

UE_API FCbWriter& operator<<(FCbWriter& Writer, const FCacheKey& Key);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FCacheKey& OutKey);

// Types from DerivedDataCacheMethod.h

UE_API FCbWriter& operator<<(FCbWriter& Writer, ECacheMethod Method);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, ECacheMethod& OutMethod);

// Types from DerivedDataCachePolicy.h

UE_API FCbWriter& operator<<(FCbWriter& Writer, const FCacheValuePolicy& Policy);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FCacheValuePolicy& OutPolicy);

UE_API FCbWriter& operator<<(FCbWriter& Writer, const FCacheRecordPolicy& Policy);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FCacheRecordPolicy& OutPolicy);

// Types from DerivedDataCacheRecord.h

/**
 * Save a cache record as a stream of named fields. Attachments optionally saved to a callback.
 *
 * It is the responsibility of the caller to call BeginObject/EndObject to save this into an object.
 * When SaveAttachment is provided, it is called for each attachment referenced by the saved record.
 */
UE_API void SaveToCompactBinary(FCbWriter& Writer, const FCacheRecord& Record, EMetaSaveFormat MetaFormat, FSaveAttachmentFn SaveAttachment = nullptr);

/**
 * Load a cache record from an object. Attachments optionally loaded from a callback.
 *
 * When LoadAttachment is provided, it is called for each attachment referenced by the loaded object.
 * An attachment that cannot be loaded will be omitted from the returned record without failing the load.
 */
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOptionalCacheRecord& OutRecord, FLoadAttachmentFn LoadAttachment = nullptr);

// Types from DerivedDataRequestTypes.h

UE_API FCbWriter& operator<<(FCbWriter& Writer, EPriority Priority);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, EPriority& OutPriority, EPriority Default);
UE_DEPRECATED(5.8, "Call the overload with an explicit default of EPriority::Normal.")
UE_API bool LoadFromCompactBinary(FCbFieldView Field, EPriority& OutPriority);

UE_API FCbWriter& operator<<(FCbWriter& Writer, EStatus Status);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, EStatus& OutStatus, EStatus Default);
UE_DEPRECATED(5.8, "Call the overload with an explicit default of EStatus::Ok.")
UE_API bool LoadFromCompactBinary(FCbFieldView Field, EStatus& OutStatus);

// Types from DerivedDataValue.h

UE_API FCbWriter& operator<<(FCbWriter& Writer, const FValue& Value);
UE_API FCbWriter& operator<<(FCbWriter& Writer, const FValueWithId& Value);

UE_API void SaveToCompactBinary(FCbWriter& Writer, const FValue& Value, FSaveAttachmentFn SaveAttachment = nullptr);
UE_API void SaveToCompactBinary(FCbWriter& Writer, const FValueWithId& Value, FSaveAttachmentFn SaveAttachment = nullptr);

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FValue& OutValue, FLoadAttachmentFn LoadAttachment = nullptr);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FValueWithId& OutValue, FLoadAttachmentFn LoadAttachment = nullptr);

// Types from DerivedDataValueId.h

UE_API FCbWriter& operator<<(FCbWriter& Writer, const FValueId& Id);
UE_API bool LoadFromCompactBinary(FCbFieldView Field, FValueId& OutId);

} // UE::DerivedData

#undef UE_API
