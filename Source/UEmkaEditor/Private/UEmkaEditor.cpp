// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaEditor.h"
#include "EdGraphUtilities.h"
#include "K2Node_UEmka.h"
#include "SGraphNode_UEmka.h"

#define LOCTEXT_NAMESPACE "FUEmkaEditorModule"

class FUEmkaNodeFactory : public FGraphPanelNodeFactory
{
public:
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* InNode) const override
	{
		if (UK2Node_UEmka* UEmkaNode = Cast<UK2Node_UEmka>(InNode))
		{
			return SNew(SGraphNode_UEmka, UEmkaNode);
		}
		return nullptr;
	}
};

void FUEmkaEditorModule::StartupModule()
{
	NodeFactory = MakeShareable(new FUEmkaNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
}

void FUEmkaEditorModule::ShutdownModule()
{
	FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
	NodeFactory.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEmkaEditorModule, UEmkaEditor)
