// Copyright Solessfir 2026. All Rights Reserved.

#include "Modules/ModuleManager.h"

// Library-only module - no startup/shutdown logic, just the vendored Umka C sources.
IMPLEMENT_MODULE(FDefaultModuleImpl, UmkaLib);
