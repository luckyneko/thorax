
namespace thx
{
	// ---------------------------------------------------------------------------
	// Template implementation
	// ---------------------------------------------------------------------------

	template <typename T>
	std::shared_ptr<T> ServiceManager::get_service(ServiceID id) const
	{
		std::shared_lock lock(mutex_);
		auto it = services_.find(id);
		if (it == services_.end())
			return nullptr;
		return std::dynamic_pointer_cast<T>(it->second);
	}

	template <typename T>
	std::shared_ptr<T> ServiceManager::get_service() const
	{
		return get_service<T>(T::static_id());
	}

	template <typename T>
	bool ServiceManager::register_service(ServiceFactory factory)
	{
		return register_service(T::static_id(), T::static_version(), std::move(factory));
	}

	template <typename T>
	bool ServiceManager::unregister_service()
	{
		return unregister_service(T::static_id());
	}
} // namespace thx