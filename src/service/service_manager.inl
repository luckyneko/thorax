
namespace thx::service
{
	// ---------------------------------------------------------------------------
	// Template implementation
	// ---------------------------------------------------------------------------

	template <typename T>
	ServiceHandle<T> ServiceManager::getService(ServiceID id) const
	{
		static_assert(std::is_base_of_v<IService, T>,
		    "getService<T>: T must derive from thx::service::IService");
		std::shared_lock lock(m_mutex);
		auto it = m_services.find(id);
		if (it == m_services.end())
			return {};
		// Build a fresh strong handle to the stored entry. ServiceHandle's
		// ctor retains (refcount up by 1); detach() returns the raw pointer
		// without releasing. We static_cast and adopt — the caller receives
		// its own share of the refcount.
		//
		// ServiceID is the type discriminator at lookup time, so static_cast
		// is sound for any well-formed caller. We avoid dynamic_cast because
		// user service-interface typeinfo doesn't coalesce across DSO
		// boundaries on macOS (two-level namespace, no weak-symbol merging),
		// causing the cast to fail even when the types actually match.
		// Callers that look up by a hand-built ServiceID disagreeing with T
		// are violating the framework contract.
		ServiceHandle<IService> retained(it->second.get());
		return ServiceHandle<T>::adopt(static_cast<T*>(retained.detach()));
	}

	template <typename T>
	ServiceHandle<T> ServiceManager::getService() const
	{
		return getService<T>(T::staticId());
	}

	template <typename T>
	bool ServiceManager::registerService(ServiceFactory factory)
	{
		return registerService(T::staticId(), T::staticVersion(), std::move(factory));
	}

	template <typename T>
	bool ServiceManager::unregisterService()
	{
		return unregisterService(T::staticId());
	}
} // namespace thx::service
