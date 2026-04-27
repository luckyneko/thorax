/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/service.h>
#include <thx/service_manager.h>

#include <atomic>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Test fixtures
// ---------------------------------------------------------------------------

namespace
{

	struct TestService : thx::IService
	{
		thx::ServiceID const m_id;
		thx::Version const m_version;
		bool m_construct_result{true};
		bool* m_constructed{nullptr};
		bool* m_destroyed{nullptr};

		TestService(const char* id, thx::Version ver,
					bool* constructed = nullptr, bool* destroyed = nullptr)
			: m_id(id)
			, m_version(ver)
			, m_constructed(constructed)
			, m_destroyed(destroyed)
		{
		}

		~TestService() override
		{
			if (m_destroyed)
				*m_destroyed = true;
		}

		thx::ServiceID id() const override { return m_id; }
		thx::Version version() const override { return m_version; }

		bool onConstruct() override
		{
			if (m_constructed)
				*m_constructed = true;
			return m_construct_result;
		}

		void onDestroy() override
		{
			if (m_destroyed)
				*m_destroyed = true;
		}
	};

	// A distinct service type used to verify dynamic_cast behaviour.
	struct OtherService : thx::IService
	{
		thx::ServiceID id() const override { return thx::ServiceID("thx.test.Other"); }
		thx::Version version() const override { return thx::make_version(1, 0, 0); }
	};

	constexpr auto kServiceA = thx::ServiceID("thx.test.ServiceA");
	constexpr auto kServiceB = thx::ServiceID("thx.test.ServiceB");
	constexpr auto kV100 = thx::make_version(1, 0, 0);
	constexpr auto kV110 = thx::make_version(1, 1, 0);
	constexpr auto kV090 = thx::make_version(0, 9, 0);
	constexpr auto kV200 = thx::make_version(2, 0, 0);

	// Helper: register a TestService without needing to name the factory every time.
	bool reg(thx::ServiceManager& sm, const char* id, thx::Version ver,
			 bool* constructed = nullptr, bool* destroyed = nullptr)
	{
		return sm.register_service(
			thx::ServiceID(id), ver,
			[=]()
			{ return std::make_shared<TestService>(id, ver, constructed, destroyed); });
	}

} // namespace

// ---------------------------------------------------------------------------
// Registration lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - register and retrieve a service", "[service_manager]")
{
	thx::ServiceManager sm;

	REQUIRE(reg(sm, "thx.test.ServiceA", kV100));
	REQUIRE(sm.get_service<TestService>(kServiceA) != nullptr);
}

TEST_CASE("ServiceManager - get_service returns nullptr for unknown ID", "[service_manager]")
{
	thx::ServiceManager sm;

	REQUIRE(sm.get_service<TestService>(kServiceA) == nullptr);
}

TEST_CASE("ServiceManager - get_service returns nullptr for wrong type", "[service_manager]")
{
	thx::ServiceManager sm;
	reg(sm, "thx.test.ServiceA", kV100);

	REQUIRE(sm.get_service<OtherService>(kServiceA) == nullptr);
}

TEST_CASE("ServiceManager - unregister removes a service", "[service_manager]")
{
	thx::ServiceManager sm;
	reg(sm, "thx.test.ServiceA", kV100);

	REQUIRE(sm.unregister_service(kServiceA));
	REQUIRE(sm.get_service<TestService>(kServiceA) == nullptr);
}

TEST_CASE("ServiceManager - unregister unknown ID returns false", "[service_manager]")
{
	thx::ServiceManager sm;

	REQUIRE_FALSE(sm.unregister_service(kServiceA));
}

TEST_CASE("ServiceManager - null factory rejected", "[service_manager]")
{
	thx::ServiceManager sm;

	REQUIRE_FALSE(sm.register_service(kServiceA, kV100, nullptr));
}

TEST_CASE("ServiceManager - factory returning null rejected", "[service_manager]")
{
	thx::ServiceManager sm;

	REQUIRE_FALSE(sm.register_service(kServiceA, kV100,
									  []() -> std::shared_ptr<thx::IService>
									  { return nullptr; }));
	REQUIRE(sm.get_service<TestService>(kServiceA) == nullptr);
}

// ---------------------------------------------------------------------------
// onConstruct / onDestroy lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - onConstruct called on first registration", "[service_manager]")
{
	thx::ServiceManager sm;
	bool constructed = false;

	reg(sm, "thx.test.ServiceA", kV100, &constructed);

	REQUIRE(constructed);
}

TEST_CASE("ServiceManager - duplicate registration is rejected",
		  "[service_manager]")
{
	thx::ServiceManager sm;
	int construct_count = 0;
	bool flag = false;

	REQUIRE(sm.register_service(kServiceA, kV100, [&]()
								{
		++construct_count;
		return std::make_shared<TestService>("thx.test.ServiceA", kV100, &flag); }));

	// Second registration with the same ID must be rejected; the factory is
	// never invoked.
	REQUIRE_FALSE(sm.register_service(kServiceA, kV100, [&]()
									  {
		++construct_count;
		return std::make_shared<TestService>("thx.test.ServiceA", kV100); }));

	REQUIRE(construct_count == 1);
	REQUIRE(sm.list_services().size() == 1);
}

TEST_CASE("ServiceManager - onConstruct failure aborts registration",
		  "[service_manager]")
{
	thx::ServiceManager sm;

	bool aborted = false;
	sm.register_service(kServiceA, kV100, [&]()
						{
		auto svc = std::make_shared<TestService>("thx.test.ServiceA", kV100);
		svc->m_construct_result = false;
		aborted = true;
		return svc; });

	REQUIRE(aborted);
	REQUIRE(sm.get_service<TestService>(kServiceA) == nullptr);
}

TEST_CASE("ServiceManager - onDestroy called when last registrant unregisters",
		  "[service_manager]")
{
	thx::ServiceManager sm;
	bool destroyed = false;

	sm.register_service(kServiceA, kV100, [&]()
						{ return std::make_shared<TestService>("thx.test.ServiceA", kV100,
															   nullptr, &destroyed); });

	REQUIRE_FALSE(destroyed);
	sm.unregister_service(kServiceA);
	REQUIRE(destroyed);
}

TEST_CASE("ServiceManager - re-registration after unregister succeeds",
		  "[service_manager]")
{
	thx::ServiceManager sm;
	bool destroyed_first = false;

	REQUIRE(sm.register_service(kServiceA, kV100, [&]()
								{ return std::make_shared<TestService>("thx.test.ServiceA", kV100,
																	   nullptr, &destroyed_first); }));
	sm.unregister_service(kServiceA);
	REQUIRE(destroyed_first);

	// After unregister, the slot is free for a fresh registration.
	REQUIRE(sm.register_service(kServiceA, kV100,
								[]() { return std::make_shared<TestService>("thx.test.ServiceA", kV100); }));
}

TEST_CASE("ServiceManager - unregister releases shared_ptr ownership after onDestroy",
		  "[service_manager]")
{
	thx::ServiceManager sm;
	bool dtor_called = false;

	{
		// Outer scope holds no reference after registration.
		auto factory = [&]()
		{
			return std::make_shared<TestService>("thx.test.ServiceA", kV100,
												 nullptr, &dtor_called);
		};
		sm.register_service(kServiceA, kV100, std::move(factory));
	}

	REQUIRE_FALSE(dtor_called);
	sm.unregister_service(kServiceA);
	REQUIRE(dtor_called);
}

// ---------------------------------------------------------------------------
// Single-owner registration semantics
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - second registration with any version is rejected",
		  "[service_manager]")
{
	thx::ServiceManager sm;
	reg(sm, "thx.test.ServiceA", kV100);

	// All of these must fail under single-owner semantics, regardless of
	// whether the version is older, newer-compatible, or different major.
	REQUIRE_FALSE(reg(sm, "thx.test.ServiceA", kV090));
	REQUIRE_FALSE(reg(sm, "thx.test.ServiceA", kV110));
	REQUIRE_FALSE(reg(sm, "thx.test.ServiceA", kV200));

	// Original registration survives.
	auto svc = sm.get_service<TestService>(kServiceA);
	REQUIRE(svc != nullptr);
	REQUIRE(svc->version() == kV100);
}

// ---------------------------------------------------------------------------
// Multiple independent services
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - independent services coexist", "[service_manager]")
{
	thx::ServiceManager sm;
	reg(sm, "thx.test.ServiceA", kV100);
	reg(sm, "thx.test.ServiceB", kV100);

	REQUIRE(sm.get_service<TestService>(kServiceA) != nullptr);
	REQUIRE(sm.get_service<TestService>(kServiceB) != nullptr);

	sm.unregister_service(kServiceA);
	REQUIRE(sm.get_service<TestService>(kServiceA) == nullptr);
	REQUIRE(sm.get_service<TestService>(kServiceB) != nullptr);
}

// ---------------------------------------------------------------------------
// Thread safety
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - concurrent get_service is safe", "[service_manager]")
{
	thx::ServiceManager sm;
	reg(sm, "thx.test.ServiceA", kV100);

	constexpr int kThreads = 8;
	constexpr int kItersEach = 1000;
	std::atomic<int> successes = 0;

	std::vector<std::thread> threads;
	threads.reserve(kThreads);
	for (int i = 0; i < kThreads; ++i)
	{
		threads.emplace_back([&]()
							 {
			for (int j = 0; j < kItersEach; ++j)
				if (sm.get_service<TestService>(kServiceA) != nullptr)
					++successes; });
	}
	for (auto& t : threads)
		t.join();

	REQUIRE(successes == kThreads * kItersEach);
}

TEST_CASE("ServiceManager - concurrent register and get_service is safe",
		  "[service_manager]")
{
	thx::ServiceManager sm;
	std::atomic<int> registered = 0;

	constexpr int kThreads = 8;
	std::vector<std::thread> threads;
	threads.reserve(kThreads);
	for (int i = 0; i < kThreads; ++i)
	{
		threads.emplace_back([&]()
							 {
			if (sm.register_service(kServiceA, kV100,
			        []() { return std::make_shared<TestService>(
			                   "thx.test.ServiceA", kV100); }))
				++registered;

			sm.get_service<TestService>(kServiceA); });
	}
	for (auto& t : threads)
		t.join();

	// Single-owner semantics: exactly one register_service call wins; the
	// other seven find the entry already present and return false.
	REQUIRE(registered == 1);

	REQUIRE(sm.unregister_service(kServiceA));
	REQUIRE(sm.get_service<TestService>(kServiceA) == nullptr);
}

// ---------------------------------------------------------------------------
// Type-deducing API (Service<Derived> CRTP)
// ---------------------------------------------------------------------------

namespace
{

	// Interface header — what both Plugin A and Plugin B would include.
	struct ICountingService : thx::Service<ICountingService>
	{
		static constexpr thx::ServiceID static_id()
		{
			return thx::ServiceID("thx.test.CountingService");
		}
		static constexpr thx::Version static_version()
		{
			return thx::make_version(1, 0, 0);
		}

		virtual int value() const = 0;
		virtual void increment() = 0;
	};

	// Concrete implementation — what Plugin A's .cpp would contain.
	struct CountingServiceImpl : ICountingService
	{
		int m_value{0};
		int value() const override { return m_value; }
		void increment() override { ++m_value; }
	};

} // namespace

TEST_CASE("ServiceManager - type-deducing register and get", "[service_manager][crtp]")
{
	thx::ServiceManager sm;

	REQUIRE(sm.register_service<ICountingService>(
		[]()
		{ return std::make_shared<CountingServiceImpl>(); }));

	auto svc = sm.get_service<ICountingService>();
	REQUIRE(svc != nullptr);
	svc->increment();
	REQUIRE(svc->value() == 1);
}

TEST_CASE("ServiceManager - type-deducing unregister", "[service_manager][crtp]")
{
	thx::ServiceManager sm;
	sm.register_service<ICountingService>(
		[]()
		{ return std::make_shared<CountingServiceImpl>(); });

	REQUIRE(sm.unregister_service<ICountingService>());
	REQUIRE(sm.get_service<ICountingService>() == nullptr);
}

TEST_CASE("ServiceManager - id() and version() match static metadata", "[service_manager][crtp]")
{
	thx::ServiceManager sm;
	sm.register_service<ICountingService>(
		[]()
		{ return std::make_shared<CountingServiceImpl>(); });

	auto svc = sm.get_service<ICountingService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->id() == ICountingService::static_id());
	REQUIRE(svc->version() == ICountingService::static_version());
}

TEST_CASE("ServiceManager - type-deducing and explicit-ID APIs are interchangeable",
		  "[service_manager][crtp]")
{
	thx::ServiceManager sm;

	// Register via type-deducing API.
	sm.register_service<ICountingService>(
		[]()
		{ return std::make_shared<CountingServiceImpl>(); });

	// Retrieve via explicit-ID API — same entry.
	auto svc = sm.get_service<ICountingService>(ICountingService::static_id());
	REQUIRE(svc != nullptr);

	// Unregister via explicit-ID API.
	REQUIRE(sm.unregister_service(ICountingService::static_id()));
	REQUIRE(sm.get_service<ICountingService>() == nullptr);
}

// ---------------------------------------------------------------------------
// Auto-derived ServiceID from C++ type name
// ---------------------------------------------------------------------------

namespace thx
{
	namespace test
	{

		struct AutoService : thx::Service<AutoService>
		{
			static constexpr thx::Version static_version()
			{
				return thx::make_version(1, 0, 0);
			}

			virtual int ping() const = 0;
		};

		struct AutoServiceImpl : AutoService
		{
			int ping() const override { return 42; }
		};

	} // namespace test
} // namespace thx

TEST_CASE("service_id_of - auto-derived ID matches dotted qualified name",
		  "[service_manager][type_name]")
{
	// thx::test::AutoService → "thx.test.AutoService"
	constexpr auto id = thx::ServiceID::from<thx::test::AutoService>();
	STATIC_REQUIRE(id == thx::ServiceID("thx.test.AutoService"));
}

TEST_CASE("ServiceManager - auto-derived ID used for register and get",
		  "[service_manager][type_name]")
{
	thx::ServiceManager sm;

	REQUIRE(sm.register_service<thx::test::AutoService>(
		[]()
		{ return std::make_shared<thx::test::AutoServiceImpl>(); }));

	auto svc = sm.get_service<thx::test::AutoService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->ping() == 42);
	REQUIRE(std::string(svc->id().name()) == "thx.test.AutoService");
}

TEST_CASE("ServiceManager - explicit static_id overrides auto-derived name",
		  "[service_manager][type_name]")
{
	// ICountingService overrides static_id() to "thx.test.CountingService",
	// which differs from the auto-derived "ICountingService".
	constexpr auto explicit_id = ICountingService::static_id();
	STATIC_REQUIRE(explicit_id == thx::ServiceID("thx.test.CountingService"));
}
