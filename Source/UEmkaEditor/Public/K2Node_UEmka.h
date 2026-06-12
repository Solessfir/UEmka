// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "K2Node.h"
#include "UEmkaFunctionLibrary.h"
#include "K2Node_UEmka.generated.h"

// A single parsed parameter from an Umka function signature.
struct FUEmkaPinDef
{
	FString Name;

	EUEmkaValueType Type = EUEmkaValueType::Int;

	bool bIsArray = false; // true for []type params

	FString EnumTypeName; // set when Type == EUEmkaValueType::Enum; stores the Umka type identifier

	FString FriendlyName; // optional display override, e.g. "p.x" for flattened struct fields
};

// One field of a user-defined struct type.
struct FUEmkaStructField
{
	FString Name;

	FString TypeText; // Umka source text of the field type, e.g. "real", "str", "Direction"
};

// A user-defined struct type parsed from the script (type Name = struct { ... }).
struct FUEmkaStructDef
{
	FString Name;

	TArray<FUEmkaStructField> Fields;

	// False when any field type can't cross the pin boundary (nested struct, array, map, fn, pointer).
	bool bPinSafe = true;
};

// Original (pre-flattening) parameter - drives shim codegen when the signature uses structs.
struct FUEmkaShimParam
{
	FString Name;

	FString TypeText; // Umka source text incl. array prefix, used verbatim in the shim signature

	FString StructName; // non-empty when the param is a struct (flattened into per-field pins)

	TArray<FUEmkaStructField> Fields; // struct fields, in declaration order
};

// Result of parsing the first exported function from a script.
struct FUEmkaSignature
{
	FString FunctionName;

	TArray<FUEmkaPinDef> Params;

	TOptional<EUEmkaValueType> ReturnType; // unset = void (single return)

	bool bReturnIsArray = false; // true for []type return

	FString ReturnEnumTypeName; // set when ReturnType == EUEmkaValueType::Enum

	TArray<FUEmkaPinDef> ReturnParams; // 2+ entries = multi-return (fn foo*(): (int, str))

	bool bValid = false;

	// Shim data - set when the signature uses struct params/returns. The node then compiles
	// the script with an appended wrapper function (__uemka_call) and calls that instead.
	bool bNeedsShim = false;

	TArray<FUEmkaShimParam> ShimParams; // original params, one per source parameter

	FString ReturnStructName; // set when the return type is a struct

	TArray<FUEmkaStructField> ReturnFields; // fields of the struct return

	FString ReturnTypeText; // raw return type text for non-struct forwarding, e.g. "(int, str)"

	// Non-empty when the signature uses a construct that can't cross pins ([]StructType,
	// struct inside multi-return, struct with unsupported field types). No data pins generated.
	FString UnsupportedReason;

	bool operator==(const FUEmkaSignature& Other) const;
};

// Custom Blueprint node that embeds an Umka script directly.
// Parses the first exported function (fn name*(...): type) and generates
// typed input/output pins from its signature. Code is stored inline on the node.
UCLASS()
class UEMKAEDITOR_API UK2Node_UEmka : public UK2Node
{
	GENERATED_BODY()

public:
	// The Umka script stored inline on this node.
	UPROPERTY()
	FString Script = TEXT("fn Hello*(Str: str): str {\n    res := \"Hello \" + Str\n    printf(\"%s\", res)\n    return res\n}");

	// UK2Node interface
	virtual void AllocateDefaultPins() override;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	virtual FText GetTooltipText() const override;

	virtual FLinearColor GetNodeTitleColor() const override;

	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	virtual bool ShouldShowNodeProperties() const override { return false; }

	virtual bool IsNodePure() const override { return false; }

	virtual FText GetMenuCategory() const override;

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;

	virtual void PostLoad() override;

	virtual void PostEditUndo() override;

	// Called by SGraphNode_UEmka when the code editor text is committed.
	void OnScriptChanged(const FString& NewScript);

	// Parse the first exported function signature from a script string.
	// Public so SGraphNode_UEmka can call it for live preview.
	static FUEmkaSignature ParseScript(const FString& InScript);

	// Returns the script that actually gets compiled and executed: the user script,
	// plus the generated __uemka_call wrapper when the signature uses structs.
	static FString GetEffectiveScript(const FString& InScript, const FUEmkaSignature& Sig);

	// Returns the function the runtime should call: __uemka_call when shimmed, else the user function.
	static FString GetEffectiveFunctionName(const FUEmkaSignature& Sig);

	// Last Umka compile result - updated on every script change and at BP compile time.
	// Line is 1-based; -1 means no error. Read by SGraphNode_UEmka to drive squiggly highlighting.
	mutable int32 LastErrorLine = -1;
	mutable FString LastErrorMessage;

private:
	static EUEmkaValueType ParseUmkaType(const FString& TypeName);

	static FEdGraphPinType GetPinTypeFor(EUEmkaValueType ValueType);

	// Parse all "type Name = struct { ... }" declarations from the script.
	static TArray<FUEmkaStructDef> ParseStructDefs(const FString& InScript);

	// Generate the __uemka_call wrapper function source for a struct-using signature.
	static FString BuildShimFunction(const FUEmkaSignature& Sig);

	// Cached result of the last successful parse - rebuilt on PostLoad and OnScriptChanged.
	FUEmkaSignature ParsedSignature;
};
