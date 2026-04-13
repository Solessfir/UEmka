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
	FString Script = TEXT("fn Hello*(Str: str): str {\n\tres := \"Hello \" + Str\n\tprintf(\"%s\", res)\n\treturn res\n}");

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

	// Last Umka compile result - updated on every script change and at BP compile time.
	// Line is 1-based; -1 means no error. Read by SGraphNode_UEmka to drive squiggly highlighting.
	mutable int32 LastErrorLine = -1;
	mutable FString LastErrorMessage;

private:
	static EUEmkaValueType ParseUmkaType(const FString& TypeName);

	static FEdGraphPinType GetPinTypeFor(EUEmkaValueType ValueType);

	// Cached result of the last successful parse - rebuilt on PostLoad and OnScriptChanged.
	FUEmkaSignature ParsedSignature;
};
