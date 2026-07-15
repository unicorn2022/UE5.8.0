// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditSession.h"

#include "PipelineCacheUtilities.h"
#include "ShaderBytecodeDatabase.h"

// ============================================================================
// Shader Filter -- Parser
// ============================================================================

/** Parse a size literal like "1MB", "512KB", "2GB", "1024" into bytes. */
static bool ParseSizeLiteral(const FString& Str, int64& OutBytes)
{
	FString Upper = Str.ToUpper().TrimStartAndEnd();
	double Multiplier = 1.0;
	FString NumPart = Upper;

	if (Upper.EndsWith(TEXT("GB")))
	{
		Multiplier = 1024.0 * 1024.0 * 1024.0;
		NumPart = Upper.LeftChop(2);
	}
	else if (Upper.EndsWith(TEXT("MB")))
	{
		Multiplier = 1024.0 * 1024.0;
		NumPart = Upper.LeftChop(2);
	}
	else if (Upper.EndsWith(TEXT("KB")))
	{
		Multiplier = 1024.0;
		NumPart = Upper.LeftChop(2);
	}
	else if (Upper.EndsWith(TEXT("B")))
	{
		NumPart = Upper.LeftChop(1);
	}

	if (NumPart.IsEmpty())
	{
		return false;
	}

	double Val = FCString::Atod(*NumPart);
	OutBytes = static_cast<int64>(Val * Multiplier);
	return true;
}

bool ParseFilterFieldName(FStringView Name, EShaderFilterField& OutField)
{
	static const TMap<FString, EShaderFilterField> Map = {
		{ TEXT("asset.name"),    EShaderFilterField::AssetName },
		{ TEXT("asset.path"),    EShaderFilterField::AssetPath },
		{ TEXT("asset.class"),   EShaderFilterField::AssetClass },
		{ TEXT("shader.type"),   EShaderFilterField::ShaderType },
		{ TEXT("shader.vftype"), EShaderFilterField::VFType },
		{ TEXT("shader.perm"),   EShaderFilterField::Permutation },
		{ TEXT("shader.hash"),   EShaderFilterField::Hash },
		{ TEXT("shader.refcount"), EShaderFilterField::RefCount },
		{ TEXT("shader.size"),   EShaderFilterField::Size },
	};

	const EShaderFilterField* Found = Map.Find(FString(Name).ToLower());
	if (Found)
	{
		OutField = *Found;
		return true;
	}
	return false;
}

bool ParseFilterOp(FStringView OpStr, EShaderFilterOp& OutOp)
{
	if (OpStr == TEXT("=="))       { OutOp = EShaderFilterOp::Equal; return true; }
	if (OpStr == TEXT("!="))       { OutOp = EShaderFilterOp::NotEqual; return true; }
	if (OpStr == TEXT("<"))        { OutOp = EShaderFilterOp::LessThan; return true; }
	if (OpStr == TEXT(">"))        { OutOp = EShaderFilterOp::GreaterThan; return true; }
	if (OpStr == TEXT("<="))       { OutOp = EShaderFilterOp::LessEqual; return true; }
	if (OpStr == TEXT(">="))       { OutOp = EShaderFilterOp::GreaterEqual; return true; }
	if (OpStr.Equals(TEXT("contains"), ESearchCase::IgnoreCase)) { OutOp = EShaderFilterOp::Contains; return true; }
	return false;
}

static bool IsOperatorChar(TCHAR Ch)
{
	return Ch == '=' || Ch == '!' || Ch == '<' || Ch == '>' || Ch == '&' || Ch == '|';
}

TArray<FStringView> TokenizeFilterExpression(const FString& Input)
{
	enum class EState : uint8 { Whitespace, Word, Operator, Quoted };

	TArray<FStringView> Tokens;
	const TCHAR* Data = *Input;
	const int32 Len = Input.Len();
	EState State = EState::Whitespace;
	int32 TokenStart = 0;
	TCHAR QuoteChar = 0;

	auto Emit = [&](int32 Start, int32 End)
	{
		if (End > Start)
		{
			Tokens.Add(FStringView(Data + Start, End - Start));
		}
	};

	for (int32 Pos = 0; Pos < Len; ++Pos)
	{
		TCHAR Ch = Data[Pos];

		switch (State)
		{
		case EState::Whitespace:
			if (FChar::IsWhitespace(Ch))
			{
				// stay
			}
			else if (Ch == '"' || Ch == '\'')
			{
				QuoteChar = Ch;
				TokenStart = Pos + 1;
				State = EState::Quoted;
			}
			else if (Ch == '(' || Ch == ')')
			{
				Emit(Pos, Pos + 1);
			}
			else if (IsOperatorChar(Ch))
			{
				TokenStart = Pos;
				State = EState::Operator;
			}
			else
			{
				TokenStart = Pos;
				State = EState::Word;
			}
			break;

		case EState::Word:
			if (FChar::IsWhitespace(Ch))
			{
				Emit(TokenStart, Pos);
				State = EState::Whitespace;
			}
			else if (Ch == '"' || Ch == '\'')
			{
				Emit(TokenStart, Pos);
				QuoteChar = Ch;
				TokenStart = Pos + 1;
				State = EState::Quoted;
			}
			else if (Ch == '(' || Ch == ')')
			{
				Emit(TokenStart, Pos);
				Emit(Pos, Pos + 1);
				State = EState::Whitespace;
			}
			else if (IsOperatorChar(Ch))
			{
				Emit(TokenStart, Pos);
				TokenStart = Pos;
				State = EState::Operator;
			}
			// else: stay in Word
			break;

		case EState::Operator:
			if (IsOperatorChar(Ch))
			{
				// accumulate (handles ==, !=, <=, >=, &&, ||)
			}
			else
			{
				Emit(TokenStart, Pos);
				if (FChar::IsWhitespace(Ch))
				{
					State = EState::Whitespace;
				}
				else if (Ch == '"' || Ch == '\'')
				{
					QuoteChar = Ch;
					TokenStart = Pos + 1;
					State = EState::Quoted;
				}
				else if (Ch == '(' || Ch == ')')
				{
					Emit(Pos, Pos + 1);
					State = EState::Whitespace;
				}
				else
				{
					TokenStart = Pos;
					State = EState::Word;
				}
			}
			break;

		case EState::Quoted:
			if (Ch == QuoteChar)
			{
				Emit(TokenStart, Pos);
				State = EState::Whitespace;
			}
			// else: stay in Quoted
			break;
		}
	}

	// Flush trailing token
	if (State != EState::Whitespace)
	{
		Emit(TokenStart, Len);
	}

	return Tokens;
}

/**
 * Recursive descent parser.
 * Grammar:
 *   Expr     = OrExpr
 *   OrExpr   = AndExpr ( "||" AndExpr )*
 *   AndExpr  = UnaryExpr ( "&&" UnaryExpr )*
 *   UnaryExpr = "!" UnaryExpr | Atom
 *   Atom     = "(" Expr ")" | Clause
 *   Clause   = FieldName Op Value
 */
struct FFilterParser
{
	const TArray<FStringView>& Tokens;
	int32 Pos = 0;
	FString Error;

	FFilterParser(const TArray<FStringView>& InTokens) : Tokens(InTokens) {}

	bool AtEnd() const { return Pos >= Tokens.Num(); }
	FStringView Peek() const { return AtEnd() ? FStringView() : Tokens[Pos]; }
	FStringView Consume() { return AtEnd() ? FStringView() : Tokens[Pos++]; }

	bool ParseExpr(FShaderFilterNode& Out)
	{
		return ParseOr(Out);
	}

	bool ParseOr(FShaderFilterNode& Out)
	{
		FShaderFilterNode Left;
		if (!ParseAnd(Left)) return false;

		if (!AtEnd() && Peek() == TEXT("||"))
		{
			FShaderFilterNode OrNode;
			OrNode.Type = FShaderFilterNode::EType::Or;
			OrNode.Children.Add(MoveTemp(Left));

			while (!AtEnd() && Peek() == TEXT("||"))
			{
				Consume(); // ||
				FShaderFilterNode Right;
				if (!ParseAnd(Right)) return false;
				OrNode.Children.Add(MoveTemp(Right));
			}
			Out = MoveTemp(OrNode);
		}
		else
		{
			Out = MoveTemp(Left);
		}
		return true;
	}

	bool ParseAnd(FShaderFilterNode& Out)
	{
		FShaderFilterNode Left;
		if (!ParseUnary(Left)) return false;

		if (!AtEnd() && Peek() == TEXT("&&"))
		{
			FShaderFilterNode AndNode;
			AndNode.Type = FShaderFilterNode::EType::And;
			AndNode.Children.Add(MoveTemp(Left));

			while (!AtEnd() && Peek() == TEXT("&&"))
			{
				Consume(); // &&
				FShaderFilterNode Right;
				if (!ParseUnary(Right)) return false;
				AndNode.Children.Add(MoveTemp(Right));
			}
			Out = MoveTemp(AndNode);
		}
		else
		{
			Out = MoveTemp(Left);
		}
		return true;
	}

	bool ParseUnary(FShaderFilterNode& Out)
	{
		if (!AtEnd() && Peek() == TEXT("!"))
		{
			Consume(); // !
			FShaderFilterNode Inner;
			if (!ParseUnary(Inner)) return false;

			Out.Type = FShaderFilterNode::EType::Not;
			Out.Children.Add(MoveTemp(Inner));
			return true;
		}
		return ParseAtom(Out);
	}

	bool ParseAtom(FShaderFilterNode& Out)
	{
		if (!AtEnd() && Peek() == TEXT("("))
		{
			Consume(); // (
			if (!ParseExpr(Out)) return false;
			if (AtEnd() || Peek() != TEXT(")"))
			{
				Error = TEXT("Expected ')'");
				return false;
			}
			Consume(); // )
			return true;
		}

		// Clause: field op value
		if (AtEnd())
		{
			Error = TEXT("Expected field name");
			return false;
		}

		FStringView FieldStr = Consume();
		EShaderFilterField Field;
		if (!ParseFilterFieldName(FieldStr, Field))
		{
			Error = FString::Printf(TEXT("Unknown field: '%.*s'"), FieldStr.Len(), FieldStr.GetData());
			return false;
		}

		if (AtEnd())
		{
			Error = TEXT("Expected operator after field name");
			return false;
		}

		FStringView OpStr = Consume();
		EShaderFilterOp Op;
		if (!ParseFilterOp(OpStr, Op))
		{
			Error = FString::Printf(TEXT("Unknown operator: '%.*s'"), OpStr.Len(), OpStr.GetData());
			return false;
		}

		if (AtEnd())
		{
			Error = TEXT("Expected value after operator");
			return false;
		}

		Out.Type = FShaderFilterNode::EType::Clause;
		Out.Field = Field;
		Out.Op = Op;
		Out.Value = FString(Consume());
		return true;
	}
};

bool FShaderFilterNode::Parse(const FString& Expression, FShaderFilterNode& OutRoot, FString& OutError)
{
	TArray<FStringView> Tokens = TokenizeFilterExpression(Expression);
	if (Tokens.Num() == 0)
	{
		OutError = TEXT("Empty expression");
		return false;
	}

	FFilterParser Parser(Tokens);
	if (!Parser.ParseExpr(OutRoot))
	{
		OutError = Parser.Error;
		return false;
	}

	if (!Parser.AtEnd())
	{
		OutError = FString::Printf(TEXT("Unexpected token: '%.*s'"), Parser.Peek().Len(), Parser.Peek().GetData());
		return false;
	}

	return true;
}

// ============================================================================
// Shader Filter -- Evaluator
// ============================================================================

bool FShaderFilterNode::Evaluate(int32 Idx, const FShaderAuditSession& Session) const
{
	switch (Type)
	{
	case EType::And:
		for (const FShaderFilterNode& Child : Children)
		{
			if (!Child.Evaluate(Idx, Session)) return false;
		}
		return true;

	case EType::Or:
		for (const FShaderFilterNode& Child : Children)
		{
			if (Child.Evaluate(Idx, Session)) return true;
		}
		return false;

	case EType::Not:
		return Children.Num() > 0 && !Children[0].Evaluate(Idx, Session);

	case EType::Clause:
		break;
	}

	// Clause evaluation: get the field value, compare
	check(Session.StableShaderKeyAndValueArray.IsValidIndex(Idx));
	check(Session.EntryMaterialIndex.IsValidIndex(Idx));
	const FStableShaderKeyAndValue& Entry = Session.StableShaderKeyAndValueArray[Idx];

	// For numeric fields
	auto CompareNumeric = [this](int64 FieldVal) -> bool
	{
		int64 TestVal = 0;
		ParseSizeLiteral(Value, TestVal);

		switch (Op)
		{
		case EShaderFilterOp::Equal:        return FieldVal == TestVal;
		case EShaderFilterOp::NotEqual:     return FieldVal != TestVal;
		case EShaderFilterOp::LessThan:     return FieldVal < TestVal;
		case EShaderFilterOp::GreaterThan:  return FieldVal > TestVal;
		case EShaderFilterOp::LessEqual:    return FieldVal <= TestVal;
		case EShaderFilterOp::GreaterEqual: return FieldVal >= TestVal;
		default: return false;
		}
	};

	// For string fields
	auto CompareString = [this](const FString& FieldVal) -> bool
	{
		switch (Op)
		{
		case EShaderFilterOp::Equal:        return FieldVal.Equals(Value, ESearchCase::IgnoreCase);
		case EShaderFilterOp::NotEqual:     return !FieldVal.Equals(Value, ESearchCase::IgnoreCase);
		case EShaderFilterOp::Contains:     return FieldVal.Contains(Value, ESearchCase::IgnoreCase);
		case EShaderFilterOp::LessThan:     return FieldVal < Value;
		case EShaderFilterOp::GreaterThan:  return FieldVal > Value;
		case EShaderFilterOp::LessEqual:    return FieldVal <= Value;
		case EShaderFilterOp::GreaterEqual: return FieldVal >= Value;
		default: return false;
		}
	};

	switch (Field)
	{
	case EShaderFilterField::AssetName:
	{
		const FString& Path = Session.UniqueMaterials[Session.EntryMaterialIndex[Idx]].Path;
		int32 LastSlash = INDEX_NONE;
		Path.FindLastChar(TEXT('/'), LastSlash);
		FString Name = (LastSlash != INDEX_NONE) ? Path.Mid(LastSlash + 1) : Path;
		return CompareString(Name);
	}
	case EShaderFilterField::AssetPath:
		return CompareString(Session.UniqueMaterials[Session.EntryMaterialIndex[Idx]].Path);

	case EShaderFilterField::AssetClass:
		return CompareString(Session.UniqueMaterials[Session.EntryMaterialIndex[Idx]].ClassName);

	case EShaderFilterField::ShaderType:
		return CompareString(Entry.ShaderType.ToString());

	case EShaderFilterField::VFType:
		return CompareString(Entry.VFType.ToString());

	case EShaderFilterField::Permutation:
		return CompareString(Entry.PermutationId.ToString());

	case EShaderFilterField::Hash:
		return CompareString(Entry.OutputHash.ToString());

	case EShaderFilterField::RefCount:
	{
		return CompareNumeric(Session.GetHashRefCount(Entry.OutputHash));
	}
	case EShaderFilterField::Size:
	{
		int64 Size = 0;
		uint8 Frequency = 0;
		uint32 CompressedSize = 0;
		uint32 UncompressedSize = 0;
		if (Session.GetShaderBytecodeInfo(Entry.OutputHash, Frequency, CompressedSize, UncompressedSize))
		{
			Size = CompressedSize;
		}
		return CompareNumeric(Size);
	}
	default:
		return false;
	}
}

// ============================================================================
// Shader Filter -- Autocompletion Suggestions
// ============================================================================

static void CollectFieldValues(
	const FShaderAuditSession& Session,
	EShaderFilterField InField,
	TSet<FString>& OutValues)
{
	const int32 Num = Session.StableShaderKeyAndValueArray.Num();
	const int32 MaxSamples = FMath::Min(Num, 50000);
	for (int32 Idx = 0; Idx < MaxSamples; ++Idx)
	{
		FString Val;
		switch (InField)
		{
		case EShaderFilterField::AssetClass:  Val = Session.UniqueMaterials[Session.EntryMaterialIndex[Idx]].ClassName; break;
		case EShaderFilterField::ShaderType:  Val = Session.StableShaderKeyAndValueArray[Idx].ShaderType.ToString(); break;
		case EShaderFilterField::VFType:      Val = Session.StableShaderKeyAndValueArray[Idx].VFType.ToString(); break;
		case EShaderFilterField::Permutation: Val = Session.StableShaderKeyAndValueArray[Idx].PermutationId.ToString(); break;
		default: return;
		}
		if (!Val.IsEmpty()) { OutValues.Add(Val); }
	}
}

// State machine for determining what the user is typing at the cursor position.
enum class EFilterSuggestionState
{
	ExpectField,       // start, after && || ( !
	ExpectOperator,    // after a complete field name
	ExpectValue,       // after a complete operator
	ExpectLogicOp,     // after a complete value (clause finished)
	PartialField,      // typing an incomplete field name
	PartialOperator,   // typing an incomplete operator
	PartialValue,      // typing an incomplete value
};

static EFilterSuggestionState ClassifyTokens(
	const TArray<FStringView>& Tokens,
	bool bTrailingWhitespace)
{
	if (Tokens.Num() == 0)
	{
		return EFilterSuggestionState::ExpectField;
	}

	// Walk all tokens except the last to build the "committed" state.
	// Each complete token transitions the state machine forward.
	EFilterSuggestionState State = EFilterSuggestionState::ExpectField;
	EShaderFilterField DummyField;
	EShaderFilterOp DummyOp;

	const int32 CommittedCount = bTrailingWhitespace ? Tokens.Num() : Tokens.Num() - 1;
	for (int32 i = 0; i < CommittedCount; ++i)
	{
		FStringView Token = Tokens[i];
		bool bIsField = ParseFilterFieldName(Token, DummyField);
		bool bIsOp = ParseFilterOp(Token, DummyOp);
		bool bIsLogic = (Token == TEXTVIEW("&&") || Token == TEXTVIEW("||") ||
			Token == TEXTVIEW("(") || Token == TEXTVIEW("!"));

		switch (State)
		{
		case EFilterSuggestionState::ExpectField:
			if (bIsField)      { State = EFilterSuggestionState::ExpectOperator; }
			else if (bIsLogic) { State = EFilterSuggestionState::ExpectField; }
			break;
		case EFilterSuggestionState::ExpectOperator:
			if (bIsOp) { State = EFilterSuggestionState::ExpectValue; }
			break;
		case EFilterSuggestionState::ExpectValue:
			// Any token after an operator is a value
			State = EFilterSuggestionState::ExpectLogicOp;
			break;
		case EFilterSuggestionState::ExpectLogicOp:
			if (bIsLogic) { State = EFilterSuggestionState::ExpectField; }
			break;
		default:
			break;
		}
	}

	// If trailing whitespace, the cursor is past the last committed token
	// -- we expect the next full token.
	if (bTrailingWhitespace)
	{
		return State;
	}

	// Otherwise, the last token is being actively typed -- convert the
	// expected state into the corresponding Partial* variant.
	switch (State)
	{
	case EFilterSuggestionState::ExpectField:    return EFilterSuggestionState::PartialField;
	case EFilterSuggestionState::ExpectOperator: return EFilterSuggestionState::PartialOperator;
	case EFilterSuggestionState::ExpectValue:    return EFilterSuggestionState::PartialValue;
	default:                                     return State;
	}
}

void GetShaderFilterSuggestions(
	const FString& Text,
	const FShaderAuditSession* Session,
	TArray<FString>& OutSuggestions)
{
	static const FString FieldNames[] = {
		TEXT("asset.name"), TEXT("asset.path"), TEXT("asset.class"),
		TEXT("shader.type"), TEXT("shader.vftype"), TEXT("shader.perm"),
		TEXT("shader.hash"), TEXT("shader.refcount"), TEXT("shader.size")
	};
	static const FString Operators[] = {
		TEXT("=="), TEXT("!="), TEXT("<"), TEXT(">"), TEXT("<="), TEXT(">="), TEXT("contains")
	};
	static const FString LogicOps[] = {
		TEXT("&&"), TEXT("||")
	};

	// Suppress suggestions if cursor is not at the end of the text
	TArray<FStringView> Tokens = TokenizeFilterExpression(Text);
	if (Tokens.Num() > 0 && Text.Len() > 0)
	{
		FStringView Last = Tokens.Last();
		const int32 TokenEnd = static_cast<int32>(Last.GetData() + Last.Len() - *Text);
		if (!FChar::IsWhitespace(Text[Text.Len() - 1]) && TokenEnd != Text.Len())
		{
			return;
		}
	}

	bool bTrailingWhitespace = Text.Len() > 0 && FChar::IsWhitespace(Text[Text.Len() - 1]);
	EFilterSuggestionState State = ClassifyTokens(Tokens, bTrailingWhitespace);

	// Compute prefix: everything in Text before the last token.
	FString Prefix;
	FStringView LastToken;
	if (Tokens.Num() > 0 && !bTrailingWhitespace)
	{
		LastToken = Tokens.Last();
		const int32 LastTokenOffset = static_cast<int32>(LastToken.GetData() - *Text);
		Prefix = Text.Left(LastTokenOffset);
	}
	else
	{
		Prefix = Text;
	}

	// Find the active field for value suggestions (walk back to find the field token)
	auto FindActiveField = [&]() -> EShaderFilterField
	{
		EShaderFilterField Field;
		for (int32 i = Tokens.Num() - 1; i >= 0; --i)
		{
			if (ParseFilterFieldName(Tokens[i], Field))
			{
				return Field;
			}
		}
		return EShaderFilterField::AssetName;
	};

	auto Suggest = [&](const FString& Word)
	{
		OutSuggestions.Add(Prefix + Word);
	};

	FString Partial(LastToken);

	switch (State)
	{
	case EFilterSuggestionState::ExpectField:
		for (const FString& F : FieldNames) { Suggest(F); }
		break;

	case EFilterSuggestionState::ExpectOperator:
		for (const FString& Op : Operators) { Suggest(Op); }
		break;

	case EFilterSuggestionState::ExpectValue:
		if (Session)
		{
			TSet<FString> Values;
			CollectFieldValues(*Session, FindActiveField(), Values);
			for (const FString& V : Values) { Suggest(V); }
			OutSuggestions.Sort();
		}
		break;

	case EFilterSuggestionState::ExpectLogicOp:
		for (const FString& L : LogicOps) { Suggest(L); }
		break;

	case EFilterSuggestionState::PartialField:
		for (const FString& F : FieldNames)
		{
			if (F.Contains(Partial, ESearchCase::IgnoreCase)) { Suggest(F); }
		}
		break;

	case EFilterSuggestionState::PartialOperator:
		for (const FString& Op : Operators)
		{
			if (Op.Contains(Partial, ESearchCase::IgnoreCase)) { Suggest(Op); }
		}
		break;

	case EFilterSuggestionState::PartialValue:
		if (Session)
		{
			TSet<FString> Values;
			CollectFieldValues(*Session, FindActiveField(), Values);
			for (const FString& V : Values)
			{
				if (V.Contains(Partial, ESearchCase::IgnoreCase)) { Suggest(V); }
			}
			OutSuggestions.Sort();
		}
		break;
	}
}

// ============================================================================
// Shader Filter -- Bit Array Builder
// ============================================================================

void BuildVisibleShaders(
	const FShaderAuditSession& Session,
	const TArray<FShaderFilterNode>& Filters,
	int32 MaxRefCount,
	TBitArray<>& OutVisible)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildVisibleShaders);

	const int32 Num = Session.StableShaderKeyAndValueArray.Num();
	OutVisible.Init(true, Num);

	for (int32 Idx = 0; Idx < Num; ++Idx)
	{
		// Refcount filter (fast path -- avoids evaluating text filters if refcount fails)
		if (MaxRefCount > 0)
		{
			const int32 RC = Session.GetHashRefCount(Session.StableShaderKeyAndValueArray[Idx].OutputHash);
			if (RC > MaxRefCount)
			{
				OutVisible[Idx] = false;
				continue;
			}
		}

		// Text filters (AND'd together)
		for (const FShaderFilterNode& Filter : Filters)
		{
			if (!Filter.Evaluate(Idx, Session))
			{
				OutVisible[Idx] = false;
				break;
			}
		}
	}
}


