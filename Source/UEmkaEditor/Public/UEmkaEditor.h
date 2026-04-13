// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FUEmkaEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

private:
	TSharedPtr<struct FGraphPanelNodeFactory> NodeFactory;
};
