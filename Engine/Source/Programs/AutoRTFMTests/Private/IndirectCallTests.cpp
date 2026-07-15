// Copyright Epic Games, Inc. All Rights Reserved.

// Verifies that AutoRTFM mode semantics survive indirect dispatch
// (virtual calls, function pointers, member function pointers,
// captureless-lambda invokers). EmitModeMarkers cannot wrap indirect
// call sites because the callee is unknown at IR-generation time;
// EmitModeTrampolinesForAddressTakes synthesizes an enable-mode
// trampoline at every address-take of an open / open_no_sanitize /
// internal / no_autortfm function, and the optimizer's eventual
// constant-folding or devirtualization resolves to the trampoline.
// The trampoline's begin_mode / end_mode markers survive into the
// closed clone, so the inlined body sits inside a correctly bracketed
// region.

#include "AutoRTFM.h"
#include "AutoRTFM/Testing.h"
#include "AutoRTFMTestUtils.h"
#include "Catch2Includes.h"

namespace
{

// External-visible side effect that defeats the optimizer eliding the
// callee body. Without it, the open write could be DCE'd and the test
// would not exercise the indirect-dispatch path.
extern "C" UE_AUTORTFM_FORCENOINLINE void IndirectCall_TouchAnchor()
{
	[[maybe_unused]] static volatile int Sink = 0;
	Sink = Sink + 1;
}

// ----------------------------------------------------------------------------
// Virtual dispatch
// ----------------------------------------------------------------------------

struct FVirtualBase
{
	virtual ~FVirtualBase() = default;
	virtual void Assign(int* P, int V) = 0;
};

struct FVirtualOpenDerived final : FVirtualBase
{
	AUTORTFM_OPEN void Assign(int* P, int V) final
	{
		*P = V;
		IndirectCall_TouchAnchor();
	}
};

struct FVirtualOpenNoSanitizeDerived final : FVirtualBase
{
	AUTORTFM_OPEN_NO_SANITIZE void Assign(int* P, int V) final
	{
		*P = V;
		IndirectCall_TouchAnchor();
	}
};

} // namespace

TEST_CASE("IndirectCall.Virtual.OpenOverride.WritePersistsAcrossAbort")
{
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		FVirtualOpenDerived D;
		FVirtualBase* B = &D;
		B->Assign(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}

TEST_CASE("IndirectCall.Virtual.OpenOverride.WriteCommits")
{
	int Value = 10;
	AutoRTFM::Testing::Commit([&]
	{
		FVirtualOpenDerived D;
		FVirtualBase* B = &D;
		B->Assign(&Value, 42);
	});
	REQUIRE(Value == 42);
}

TEST_CASE("IndirectCall.Virtual.OpenNoSanitizeOverride.WritePersistsAcrossAbort")
{
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		FVirtualOpenNoSanitizeDerived D;
		FVirtualBase* B = &D;
		B->Assign(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}

// ----------------------------------------------------------------------------
// Function pointer dispatch
// ----------------------------------------------------------------------------

namespace
{

AUTORTFM_OPEN static void FnPtr_OpenAssign(int* P, int V)
{
	*P = V;
	IndirectCall_TouchAnchor();
}

AUTORTFM_OPEN_NO_SANITIZE static void FnPtr_OpenNoSanitizeAssign(int* P, int V)
{
	*P = V;
	IndirectCall_TouchAnchor();
}

using FnPtrType = void (*)(int*, int);

} // namespace

TEST_CASE("IndirectCall.FunctionPointer.OpenCallee.WritePersistsAcrossAbort")
{
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		FnPtrType F = &FnPtr_OpenAssign;
		F(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}

TEST_CASE("IndirectCall.FunctionPointer.OpenNoSanitizeCallee.WritePersistsAcrossAbort")
{
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		FnPtrType F = &FnPtr_OpenNoSanitizeAssign;
		F(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}

TEST_CASE("IndirectCall.FunctionPointer.OpenCallee.PointerStoredInStatic")
{
	// Variant where the function pointer lives in a static-storage
	// variable. ThinLTO IPO can constant-propagate the pointer across
	// module boundaries.
	static FnPtrType F = &FnPtr_OpenAssign;
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		F(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}

// ----------------------------------------------------------------------------
// Captureless lambda decayed to function pointer
// ----------------------------------------------------------------------------

TEST_CASE("IndirectCall.CapturelessLambda.OpenInvoker.WritePersistsAcrossAbort")
{
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		FnPtrType F = +[] AUTORTFM_OPEN (int* P, int V)
		{
			*P = V;
			IndirectCall_TouchAnchor();
		};
		F(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}

// ----------------------------------------------------------------------------
// Pointer-to-member-function dispatch
// ----------------------------------------------------------------------------

namespace
{

struct FObj
{
	AUTORTFM_OPEN void OpenAssign(int* P, int V)
	{
		*P = V;
		IndirectCall_TouchAnchor();
	}
};

using PmfType = void (FObj::*)(int*, int);

} // namespace

TEST_CASE("IndirectCall.MemberFunctionPointer.OpenMethod.WritePersistsAcrossAbort")
{
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		FObj O;
		PmfType M = &FObj::OpenAssign;
		(O.*M)(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}

// ----------------------------------------------------------------------------
// `internal` mode via indirect dispatch
// ----------------------------------------------------------------------------

namespace
{

struct FVirtualInternalBase
{
	virtual ~FVirtualInternalBase() = default;
	virtual void Assign(int* P, int V) = 0;
};

struct FVirtualInternalDerived final : FVirtualInternalBase
{
	[[clang::autortfm(autortfm_mode_internal)]]
	void Assign(int* P, int V) final
	{
		*P = V;
		IndirectCall_TouchAnchor();
	}
};

[[clang::autortfm(autortfm_mode_internal)]]
static void FnPtr_InternalAssign(int* P, int V)
{
	*P = V;
	IndirectCall_TouchAnchor();
}

} // namespace

TEST_CASE("IndirectCall.Virtual.InternalOverride.WritePersistsAcrossAbort")
{
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		FVirtualInternalDerived D;
		FVirtualInternalBase* B = &D;
		B->Assign(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}

TEST_CASE("IndirectCall.FunctionPointer.InternalCallee.WritePersistsAcrossAbort")
{
	int Value = 10;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
	{
		using InternalFn = void (*)(int*, int);
		InternalFn F = &FnPtr_InternalAssign;
		F(&Value, 42);
		AutoRTFM::AbortTransaction();
	});
	REQUIRE(Result == AutoRTFM::ETransactionResult::AbortedByRequest);
	REQUIRE(Value == 42);
}
