// Copyright Solessfir 2026. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetDefinition_UEmkaScriptAsset.h"
#include "AssetDefinitionRegistry.h"
#include "Engine/DataAsset.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaScriptAssetDefinitionTest, "UEmka.Editor.Assets.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaScriptAssetDefinitionTest::RunTest(const FString& Parameters)
{
	const UAssetDefinitionRegistry* Registry = UAssetDefinitionRegistry::Get();
	const UAssetDefinition* Definition = Registry->GetAssetDefinitionForClass(UUEmkaScriptAsset::StaticClass());
	if (!TestTrue(TEXT("Script assets use their registered definition"), Definition == GetDefault<UAssetDefinition_UEmkaScriptAsset>())) return false;
	const UAssetDefinition* DataAssetDefinition = Registry->GetAssetDefinitionForClass(UDataAsset::StaticClass());
	if (TestNotNull(TEXT("Generic data assets have a definition"), DataAssetDefinition))
	{
		TestTrue(TEXT("Script assets have a distinct Content Browser color"), Definition->GetAssetColor() != DataAssetDefinition->GetAssetColor());
	}
	return true;
}

#endif
