// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaEditor.h"
#include "EdGraphUtilities.h"
#include "K2Node_UEmka.h"
#include "PropertyEditorModule.h"
#include "SGraphNode_UEmka.h"
#include "UEmkaScriptAsset.h"
#include "UEmkaScriptAssetDetails.h"
#include "UObject/UObjectIterator.h"

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
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditor.RegisterCustomClassLayout(UUEmkaScriptAsset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FUEmkaScriptAssetDetails::MakeInstance));
	PropertyEditor.NotifyCustomizationModuleChanged();

	NodeFactory = MakeShareable(new FUEmkaNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
	ScriptAssetChangedHandle = UUEmkaScriptAsset::OnScriptAssetChanged.AddLambda([](UUEmkaScriptAsset* Asset)
	{
		for (TObjectIterator<UK2Node_UEmka> Node; Node; ++Node)
		{
			if (!Node->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_NeedPostLoad))
			{
				Node->OnScriptAssetChanged(Asset);
			}
		}
	});
}

void FUEmkaEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.UnregisterCustomClassLayout(UUEmkaScriptAsset::StaticClass()->GetFName());
		PropertyEditor.NotifyCustomizationModuleChanged();
	}

	UUEmkaScriptAsset::OnScriptAssetChanged.Remove(ScriptAssetChangedHandle);
	FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
	NodeFactory.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEmkaEditorModule, UEmkaEditor)
