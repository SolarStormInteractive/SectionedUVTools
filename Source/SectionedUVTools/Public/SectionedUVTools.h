// Copyright (c) 2022 Solar Storm Interactive

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FSectionedUVToolsModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
