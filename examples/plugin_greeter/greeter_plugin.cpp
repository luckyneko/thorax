/*
 *  Created by LuckyNeko on 31/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// A plugin with a real dependency. The greeter service emits its greeting
// through ILoggingService — so the greeter cannot do its job unless a logging
// provider is loaded first. The plugin declares that dependency in
// IPlugin::required(); the loader refuses to load the greeter until the
// requirement is satisfied, and thx::plugin::loadWithDependencies() resolves
// and loads the logging provider automatically.
//
// Because we need a custom IPlugin to declare required(), this uses
// THX_DEFINE_PLUGIN rather than THX_DEFINE_SERVICE_PLUGIN.

#include "interfaces/greeter_service.h"
#include "interfaces/logging_service.h"

#include <thx/plugin/platform.h>
#include <thx/service/service.h>

#include <cstdio>

namespace
{

	struct GreeterServiceImpl : examples::IGreeterService
	{
		void greet(const char* who) override
		{
			// Resolve the dependency at call time and route the greeting
			// through it. The requirement guarantees it is present.
			auto log = thx::service::getService<examples::ILoggingService>();
			char buffer[128];
			std::snprintf(buffer, sizeof(buffer), "Hello, %s!", who ? who : "world");
			if (log)
				log->log(buffer);
			else
				std::printf("%s\n", buffer); // should not happen given required()
		}
	};

	class GreeterPlugin : public thx::plugin::IPlugin
	{
	public:
		thx::StringView name() const override { return "examples.GreeterPlugin"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad() override
		{
			return thx::service::registerService<GreeterServiceImpl>();
		}

		void onUnload() override
		{
			thx::service::unregisterService<GreeterServiceImpl>();
		}

		thx::Span<const thx::plugin::ServiceRequirement> required() const override
		{
			static const thx::plugin::ServiceRequirement kRequires[] = {
				{examples::ILoggingService::staticId(), examples::ILoggingService::staticVersion()},
			};
			return {kRequires, 1};
		}

		thx::Span<const thx::service::ServiceID> provides() const override
		{
			static const thx::service::ServiceID kProvides[] = {
				GreeterServiceImpl::staticId(),
			};
			return {kProvides, 1};
		}
	};

} // namespace

THX_DEFINE_PLUGIN(GreeterPlugin)
