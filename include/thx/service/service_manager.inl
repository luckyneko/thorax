
namespace thx::service
{
	// ---------------------------------------------------------------------------
	// Template implementation
	// ---------------------------------------------------------------------------

	template <typename T>
	std::shared_ptr<T> ServiceManager::getService(ServiceID id) const
	{
		static_assert(std::is_base_of_v<IService, T>,
		    "getService<T>: T must derive from thx::service::IService");
		std::shared_lock lock(m_mutex);
		auto it = m_services.find(id);
		if (it == m_services.end())
			return nullptr;
		// ServiceID is the type discriminator — registerService<T> tied this ID
		// to the type at registration time, so static_pointer_cast is sound for
		// any well-formed caller. We avoided dynamic_pointer_cast because user
		// service-interface typeinfo doesn't coalesce across DSO boundaries on
		// macOS (two-level namespace, no weak-symbol merging), causing the cast
		// to fail even when the types actually match. Callers that look up by a
		// hand-built ServiceID that disagrees with T are violating the framework
		// contract.
		return std::static_pointer_cast<T>(it->second);
	}

	template <typename T>
	std::shared_ptr<T> ServiceManager::getService() const
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