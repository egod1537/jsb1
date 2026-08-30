namespace sim {
template <typename T, typename... Args>
T *Simulation::AddComponent(Args &&...args) {
  static_assert(std::is_base_of_v<Component, T>,
      "T must inherit from sim::Component");

  auto component = std::make_unique<T>(std::forward<Args>(args)...);
  T *result = component.get();
  component->owner_ = this;
  components_.push_back(std::move(component));

  if (initialized_ && !InitializeComponent(*result)) {
    for (auto iterator = components_.begin(); iterator != components_.end();
        ++iterator) {
      if (iterator->get() == result) {
        result->owner_ = nullptr;
        components_.erase(iterator);
        break;
      }
    }
    errorTracker_.SetErrorIfEmpty("Failed to initialize component.");
    return nullptr;
  }

  return result;
}

template <typename T> T *Simulation::GetComponent() {
  static_assert(std::is_base_of_v<Component, T>,
      "T must inherit from sim::Component");

  for (const auto &component : components_) {
    if (auto *result = dynamic_cast<T *>(component.get())) {
      return result;
    }
  }

  return nullptr;
}

template <typename T> const T *Simulation::GetComponent() const {
  static_assert(std::is_base_of_v<Component, T>,
      "T must inherit from sim::Component");

  for (const auto &component : components_) {
    if (const auto *result = dynamic_cast<const T *>(component.get())) {
      return result;
    }
  }

  return nullptr;
}

template <typename T> bool Simulation::RemoveComponent() {
  static_assert(std::is_base_of_v<Component, T>,
      "T must inherit from sim::Component");

  for (auto iterator = components_.begin(); iterator != components_.end();
      ++iterator) {
    if (dynamic_cast<T *>(iterator->get()) == nullptr) {
      continue;
    }

    (*iterator)->OnShutdown();
    (*iterator)->initialized_ = false;
    (*iterator)->owner_ = nullptr;
    components_.erase(iterator);
    return true;
  }

  return false;
}
} // namespace sim
