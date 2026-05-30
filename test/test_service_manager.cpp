/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <service/service_manager.h>
#include <thx/service/iservice.h>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Test fixtures
// ---------------------------------------------------------------------------

namespace
{

	struct TestService : thx::service::IService
	{
		thx::service::ServiceID const m_id;
		thx::Version const m_version;
		bool m_constructResult{true};
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

		thx::service::ServiceID id() const override { return m_id; }
		thx::Version version() const override { return m_version; }

		bool onConstruct() override
		{
			if (m_constructed)
				*m_constructed = true;
			return m_constructResult;
		}

		void onDestroy() override
		{
			if (m_destroyed)
				*m_destroyed = true;
		}
	};

	constexpr auto kServiceA = thx::service::ServiceID("thx.test.ServiceA");
	constexpr auto kServiceB = thx::service::ServiceID("thx.test.ServiceB");
	constexpr auto kV100 = thx::Version{1, 0, 0};
	constexpr auto kV110 = thx::Version{1, 1, 0};
	constexpr auto kV090 = thx::Version{0, 9, 0};
	constexpr auto kV200 = thx::Version{2, 0, 0};

	// Helper: register a TestService without needing to name the factory every time.
	bool reg(thx::service::ServiceManager& sm, const char* id, thx::Version ver,
			 bool* constructed = nullptr, bool* destroyed = nullptr)
	{
		return sm.registerService(
			thx::service::ServiceID(id), ver,
			thx::service::makeServiceFactory([=]() -> thx::service::IService*
											 { return new TestService(id, ver, constructed, destroyed); }));
	}

} // namespace

// ---------------------------------------------------------------------------
// Registration lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - register and retrieve a service", "[service_manager]")
{
	thx::service::ServiceManager sm;

	REQUIRE(reg(sm, "thx.test.ServiceA", kV100));
	REQUIRE(sm.getService<TestService>(kServiceA) != nullptr);
}

TEST_CASE("ServiceManager - getService returns nullptr for unknown ID", "[service_manager]")
{
	thx::service::ServiceManager sm;

	REQUIRE(sm.getService<TestService>(kServiceA) == nullptr);
}

// Note: there is no "wrong type" test. getService<T>(id) uses static_pointer_cast
// because dynamic_pointer_cast doesn't reliably work across DSO boundaries on
// macOS — see service_manager.inl. The framework's contract is that the
// ServiceID determines the registered type; callers who pass a mismatched T
// invoke undefined behaviour.

TEST_CASE("ServiceManager - unregister removes a service", "[service_manager]")
{
	thx::service::ServiceManager sm;
	reg(sm, "thx.test.ServiceA", kV100);

	REQUIRE(sm.unregisterService(kServiceA));
	REQUIRE(sm.getService<TestService>(kServiceA) == nullptr);
}

TEST_CASE("ServiceManager - unregister unknown ID returns false", "[service_manager]")
{
	thx::service::ServiceManager sm;

	REQUIRE_FALSE(sm.unregisterService(kServiceA));
}

TEST_CASE("ServiceManager - empty factory rejected", "[service_manager]")
{
	thx::service::ServiceManager sm;

	thx::service::ServiceFactory empty{}; // invoke=nullptr, etc.
	REQUIRE_FALSE(sm.registerService(kServiceA, kV100, empty));
}

TEST_CASE("ServiceManager - factory returning null rejected", "[service_manager]")
{
	thx::service::ServiceManager sm;

	REQUIRE_FALSE(sm.registerService(kServiceA, kV100,
									 []() -> thx::service::IService*
									 { return nullptr; }));
	REQUIRE(sm.getService<TestService>(kServiceA) == nullptr);
}

TEST_CASE("ServiceManager - factory throwing releases the reservation",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;

	// First call: factory throws. The reservation must not leak — a follow-up
	// registration with the same ID must succeed.
	REQUIRE_THROWS(sm.registerService(kServiceA, kV100,
									  []() -> thx::service::IService*
									  {
										  throw std::runtime_error("boom");
									  }));

	REQUIRE(reg(sm, "thx.test.ServiceA", kV100));
	REQUIRE(sm.getService<TestService>(kServiceA) != nullptr);
}

TEST_CASE("ServiceManager - declared version mismatch rejected",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;

	// Factory returns a service whose version() doesn't match the declared
	// version. registerService must refuse the registration (Release-safe;
	// previously this was assert-only and silently committed in Release).
	REQUIRE_FALSE(sm.registerService(
		kServiceA, kV100,
		[]() -> thx::service::IService*
		{ return new TestService("thx.test.ServiceA", kV200); }));
	REQUIRE(sm.getService<TestService>(kServiceA) == nullptr);
}

// ---------------------------------------------------------------------------
// onConstruct / onDestroy lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - onConstruct called on first registration", "[service_manager]")
{
	thx::service::ServiceManager sm;
	bool constructed = false;

	reg(sm, "thx.test.ServiceA", kV100, &constructed);

	REQUIRE(constructed);
}

TEST_CASE("ServiceManager - duplicate registration is rejected for every version",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;
	int construct_count = 0;
	bool flag = false;

	REQUIRE(sm.registerService(kServiceA, kV100, [&]() -> thx::service::IService*
							   {
		++construct_count;
		return new TestService("thx.test.ServiceA", kV100, &flag); }));

	// Second registration with the same ID must be rejected; the factory is
	// never invoked.
	REQUIRE_FALSE(sm.registerService(kServiceA, kV100, [&]() -> thx::service::IService*
									 {
		++construct_count;
		return new TestService("thx.test.ServiceA", kV100); }));

	// Single-owner semantics: re-registration is rejected for every version —
	// older, newer-compatible, and different-major — never invoking a factory.
	REQUIRE_FALSE(reg(sm, "thx.test.ServiceA", kV090));
	REQUIRE_FALSE(reg(sm, "thx.test.ServiceA", kV110));
	REQUIRE_FALSE(reg(sm, "thx.test.ServiceA", kV200));

	REQUIRE(construct_count == 1);
	REQUIRE(sm.listServices().size() == 1);

	// The original registration survives unchanged.
	auto svc = sm.getService<TestService>(kServiceA);
	REQUIRE(svc != nullptr);
	REQUIRE(svc->version() == kV100);
}

TEST_CASE("ServiceManager - onConstruct failure aborts registration",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;

	bool aborted = false;
	sm.registerService(kServiceA, kV100, [&]() -> thx::service::IService*
					   {
		auto* svc = new TestService("thx.test.ServiceA", kV100);
		svc->m_constructResult = false;
		aborted = true;
		return svc; });

	REQUIRE(aborted);
	REQUIRE(sm.getService<TestService>(kServiceA) == nullptr);
}

TEST_CASE("ServiceManager - onDestroy called when last registrant unregisters",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;
	bool destroyed = false;

	sm.registerService(kServiceA, kV100, [&]() -> thx::service::IService*
					   { return new TestService("thx.test.ServiceA", kV100,
												nullptr, &destroyed); });

	REQUIRE_FALSE(destroyed);
	sm.unregisterService(kServiceA);
	REQUIRE(destroyed);
}

TEST_CASE("ServiceManager - re-registration after unregister succeeds",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;
	bool destroyed_first = false;

	REQUIRE(sm.registerService(kServiceA, kV100, [&]() -> thx::service::IService*
							   { return new TestService("thx.test.ServiceA", kV100,
														nullptr, &destroyed_first); }));
	sm.unregisterService(kServiceA);
	REQUIRE(destroyed_first);

	// After unregister, the slot is free for a fresh registration.
	REQUIRE(sm.registerService(kServiceA, kV100,
							   []() -> thx::service::IService*
							   { return new TestService("thx.test.ServiceA", kV100); }));
}

TEST_CASE("ServiceManager - unregister releases shared_ptr ownership after onDestroy",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;
	bool dtor_called = false;

	{
		// Outer scope holds no reference after registration.
		auto factory = [&]() -> thx::service::IService*
		{
			return new TestService("thx.test.ServiceA", kV100,
								   nullptr, &dtor_called);
		};
		sm.registerService(kServiceA, kV100, std::move(factory));
	}

	REQUIRE_FALSE(dtor_called);
	sm.unregisterService(kServiceA);
	REQUIRE(dtor_called);
}

// ---------------------------------------------------------------------------
// Single-owner registration semantics
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager::clear - removes every service and runs onDestroy",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;
	bool destroyedA = false;
	bool destroyedB = false;
	reg(sm, "thx.test.ServiceA", kV100, nullptr, &destroyedA);
	reg(sm, "thx.test.ServiceB", kV100, nullptr, &destroyedB);
	REQUIRE(sm.listServices().size() == 2);

	sm.clear();

	REQUIRE(sm.listServices().empty());
	REQUIRE(destroyedA);
	REQUIRE(destroyedB);
}

// ---------------------------------------------------------------------------
// Multiple independent services
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - independent services coexist", "[service_manager]")
{
	thx::service::ServiceManager sm;
	reg(sm, "thx.test.ServiceA", kV100);
	reg(sm, "thx.test.ServiceB", kV100);

	REQUIRE(sm.getService<TestService>(kServiceA) != nullptr);
	REQUIRE(sm.getService<TestService>(kServiceB) != nullptr);

	sm.unregisterService(kServiceA);
	REQUIRE(sm.getService<TestService>(kServiceA) == nullptr);
	REQUIRE(sm.getService<TestService>(kServiceB) != nullptr);
}

// ---------------------------------------------------------------------------
// Thread safety
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - concurrent getService is safe", "[service_manager]")
{
	thx::service::ServiceManager sm;
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
				if (sm.getService<TestService>(kServiceA) != nullptr)
					++successes; });
	}
	for (auto& t : threads)
		t.join();

	REQUIRE(successes == kThreads * kItersEach);
}

TEST_CASE("ServiceManager - concurrent register and getService is safe",
		  "[service_manager]")
{
	thx::service::ServiceManager sm;
	std::atomic<int> registered = 0;

	constexpr int kThreads = 8;
	std::vector<std::thread> threads;
	threads.reserve(kThreads);
	for (int i = 0; i < kThreads; ++i)
	{
		threads.emplace_back([&]()
							 {
			if (sm.registerService(kServiceA, kV100,
			        []() -> thx::service::IService*
			        { return new TestService("thx.test.ServiceA", kV100); }))
				++registered;

			sm.getService<TestService>(kServiceA); });
	}
	for (auto& t : threads)
		t.join();

	// Single-owner semantics: exactly one registerService call wins; the
	// other seven find the entry already present and return false.
	REQUIRE(registered == 1);

	REQUIRE(sm.unregisterService(kServiceA));
	REQUIRE(sm.getService<TestService>(kServiceA) == nullptr);
}

// ---------------------------------------------------------------------------
// Type-deducing API (Service<Derived> CRTP)
// ---------------------------------------------------------------------------

namespace
{

	// Interface header — what both Plugin A and Plugin B would include.
	struct ICountingService : thx::service::Service<ICountingService>
	{
		static constexpr thx::service::ServiceID staticId()
		{
			return thx::service::ServiceID("thx.test.CountingService");
		}
		static constexpr thx::Version staticVersion()
		{
			return thx::Version{1, 0, 0};
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
	thx::service::ServiceManager sm;

	REQUIRE(sm.registerService<ICountingService>(
		[]()
		{ return new CountingServiceImpl(); }));

	auto svc = sm.getService<ICountingService>();
	REQUIRE(svc != nullptr);
	svc->increment();
	REQUIRE(svc->value() == 1);
}

TEST_CASE("ServiceManager - type-deducing unregister", "[service_manager][crtp]")
{
	thx::service::ServiceManager sm;
	sm.registerService<ICountingService>(
		[]()
		{ return new CountingServiceImpl(); });

	REQUIRE(sm.unregisterService<ICountingService>());
	REQUIRE(sm.getService<ICountingService>() == nullptr);
}

TEST_CASE("ServiceManager - id() and version() match static metadata", "[service_manager][crtp]")
{
	thx::service::ServiceManager sm;
	sm.registerService<ICountingService>(
		[]()
		{ return new CountingServiceImpl(); });

	auto svc = sm.getService<ICountingService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->id() == ICountingService::staticId());
	REQUIRE(svc->version() == ICountingService::staticVersion());
}

TEST_CASE("ServiceManager - type-deducing and explicit-ID APIs are interchangeable",
		  "[service_manager][crtp]")
{
	thx::service::ServiceManager sm;

	// Register via type-deducing API.
	sm.registerService<ICountingService>(
		[]()
		{ return new CountingServiceImpl(); });

	// Retrieve via explicit-ID API — same entry.
	auto svc = sm.getService<ICountingService>(ICountingService::staticId());
	REQUIRE(svc != nullptr);

	// Unregister via explicit-ID API.
	REQUIRE(sm.unregisterService(ICountingService::staticId()));
	REQUIRE(sm.getService<ICountingService>() == nullptr);
}

// ---------------------------------------------------------------------------
// Auto-derived ServiceID from C++ type name
// ---------------------------------------------------------------------------

namespace thx
{
	namespace test
	{

		struct AutoService : thx::service::Service<AutoService>
		{
			static constexpr thx::Version staticVersion()
			{
				return thx::Version{1, 0, 0};
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
	// thx::test::AutoService â†’ "thx.test.AutoService"
	constexpr auto id = thx::service::ServiceID::from<thx::test::AutoService>();
	STATIC_REQUIRE(id == thx::service::ServiceID("thx.test.AutoService"));
}

TEST_CASE("ServiceManager - auto-derived ID used for register and get",
		  "[service_manager][type_name]")
{
	thx::service::ServiceManager sm;

	REQUIRE(sm.registerService<thx::test::AutoService>(
		[]()
		{ return new thx::test::AutoServiceImpl(); }));

	auto svc = sm.getService<thx::test::AutoService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->ping() == 42);
	REQUIRE(std::string(svc->id().name()) == "thx.test.AutoService");
}

TEST_CASE("ServiceManager - explicit staticId overrides auto-derived name",
		  "[service_manager][type_name]")
{
	// ICountingService overrides staticId() to "thx.test.CountingService",
	// which differs from the auto-derived "ICountingService".
	constexpr auto explicit_id = ICountingService::staticId();
	STATIC_REQUIRE(explicit_id == thx::service::ServiceID("thx.test.CountingService"));
}
