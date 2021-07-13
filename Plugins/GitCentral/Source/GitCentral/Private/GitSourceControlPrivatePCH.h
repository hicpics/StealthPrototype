// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "ISourceControlModule.h"
#include "Templates/SharedPointer.h"
#include "Runtime/Launch/Resources/Version.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Logging/MessageLog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"

//This log will always appear, use DEBUG versions for debug only
#define GITCENTRAL_LOG(format, ...) UE_LOG(LogSourceControl, Log, format, ##__VA_ARGS__);
#define GITCENTRAL_ERROR(format, ...) UE_LOG(LogSourceControl, Error, format, ##__VA_ARGS__);
#define GITCENTRAL_VERBOSE(format, ...) UE_LOG(LogSourceControl, Verbose, format, ##__VA_ARGS__);

#define UENUM_TO_DISPLAYNAME(EnumType, EnumValue)                                                                                          \
	FindObject<UEnum>(ANY_PACKAGE, TEXT(#EnumType), true)->GetDisplayNameTextByValue((int64)EnumValue).ToString()

