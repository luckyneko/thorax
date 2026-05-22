
namespace thx
{
	// ---------------------------------------------------------------------------
	// Template implementation
	// ---------------------------------------------------------------------------

	template <typename T>
	std::shared_ptr<T> ServiceManager::getService(ServiceID id) const
	{
		std::shared_lock lock(m_mutex);
		auto it = m_services.find(id);
		if (it == m_services.end())
			return nullptr;
		return std::dynamic_pointer_cast<T>(it->second);
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
} // namespace thx