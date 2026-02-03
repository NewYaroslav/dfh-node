/**
 * \file interfaces.hpp
 * \brief Набор базовых интерфейсов и их системных реализаций.
 * \details Используются для инъекции времени, случайности и статуса.
 */
#pragma once

#include <cstdint>

#include "dfh_node/status.hpp"

namespace dfh_node {

/// \brief Абстрактный источник времени.
/// \details Реализация должна документировать потокобезопасность и монотонность.
class IClock {
  public:
    virtual ~IClock() = default;
    /// \brief Текущее время в миллисекундах.
    /// \return Время в мс; ожидается монотонность, если это требуется.
    /// \throws Не бросает.
    /// \note Потокобезопасность определяется реализацией.
    virtual std::uint64_t now_ms() const = 0;
};

/// \brief Абстрактный источник случайных чисел.
/// \details Контракт по криптостойкости определяется реализацией.
class IRandom {
  public:
    virtual ~IRandom() = default;
    /// \brief Следующее 64-битное значение.
    /// \return Случайное число.
    /// \throws Не бросает.
    /// \note Потокобезопасность определяется реализацией.
    virtual std::uint64_t next_uint64() = 0;
};

/// \brief Поставщик снимка статуса ноды.
/// \details Снимок должен быть согласован на момент вызова.
class IStatusProvider {
  public:
    virtual ~IStatusProvider() = default;
    /// \brief Возвращает снимок статуса.
    /// \return Снимок статуса.
    /// \throws Не бросает.
    /// \note Потокобезопасность определяется реализацией.
    virtual StatusSnapshot snapshot() const = 0;
};

/// \brief Системная реализация IClock на основе std::chrono.
/// \details Использует монотонные часы.
class SystemClock final : public IClock {
  public:
    /// \brief Текущее время в миллисекундах монотонных часов.
    /// \return Значение в миллисекундах.
    /// \throws Не бросает.
    /// \note Потокобезопасно, не имеет состояния.
    std::uint64_t now_ms() const override;
};

/// \brief Системная реализация IRandom на базе std::random_device.
/// \details Подходит для генерации nonce без детерминизма.
class SystemRandom final : public IRandom {
  public:
    /// \brief Генерирует 64-битное значение из std::random_device.
    /// \return Случайное число.
    /// \throws Не бросает.
    /// \note Потокобезопасность зависит от реализации std::random_device.
    std::uint64_t next_uint64() override;
};

} // namespace dfh_node
