/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

// Cross-cutting primitives
#include "thx/io/io.h"
#include "thx/lifecycle.h"
#include "thx/log/log.h"
#include "thx/log/log_service.h"
#include "thx/result.h"
#include "thx/span.h"
#include "thx/string_view.h"
#include "thx/to_string.h"
#include "thx/version.h"

// Service layer
#include "thx/service/iservice.h"
#include "thx/service/service.h" // free-function facade
#include "thx/service/service_id.h"

// Plugin layer
#include "thx/plugin/iplugin.h"
#include "thx/plugin/manifest.h"
#include "thx/plugin/platform.h"
#include "thx/plugin/plugin.h" // free-function facade
