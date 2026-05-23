/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

// Cross-cutting primitives
#include "thx/string_view.h"
#include "thx/span.h"
#include "thx/version.h"
#include "thx/to_string.h"
#include "thx/result.h"
#include "thx/library.h"
#include "thx/log.h"

// Service layer
#include "thx/service/service_id.h"
#include "thx/service/iservice.h"
#include "thx/service/service_manager.h"
#include "thx/service/service.h"   // free-function facade

// Plugin layer
#include "thx/plugin/iplugin.h"
#include "thx/plugin/platform.h"
#include "thx/plugin/plugin_handle.h"
#include "thx/plugin/plugin_garbage.h"
#include "thx/plugin/plugin_manager.h"
#include "thx/plugin/plugin.h"     // free-function facade

// Top-level singleton
#include "thx/registry.h"
