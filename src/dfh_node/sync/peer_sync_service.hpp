/// \file peer_sync_service.hpp
/// \brief Pull-based сервис межнодовой синхронизации.
/// \details Выполняет периодический опрос peer-нод по `/sync/meta` и
/// `/sync/block`, применяет freshness/divergence-правила и сохраняет блоки
/// через `merge_block_dfhbin()`.
///
#pragma once

#include "adapter/dfh_adapter.hpp"
#include "config/config.hpp"
#include "core/disk_monitor.hpp"

#include <server_http.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace dfh_node {

/// \brief Сервис pull-based синхронизации истории между нодами.
class PeerSyncService {
public:
    /// \brief Создаёт сервис синхронизации peer-нод.
    /// \param adapter Адаптер локального хранилища.
    /// \param cfg Полная конфигурация ноды.
    /// \param disk_monitor Монитор диска; допускается `nullptr`.
    PeerSyncService(IDfhAdapter &adapter, const config::Config &cfg, DiskMonitor *disk_monitor);

    /// \brief Останавливает фоновой поток синхронизации.
    ~PeerSyncService();

    /// \brief Запускает фоновый pull-loop.
    void start();

    /// \brief Останавливает pull-loop и дожидается завершения потока.
    void shutdown();

    /// \brief Возвращает признак запущенного фонового потока.
    /// \return `true`, если pull-loop запущен.
    bool is_running() const;

    /// \brief Возвращает время последней попытки sync-цикла.
    /// \return Unix epoch ms или `0`, если попыток ещё не было.
    std::int64_t last_attempt_at_ms() const;

    /// \brief Возвращает время последнего успешного опроса peer'а.
    /// \return Unix epoch ms или `0`, если успешных опросов ещё не было.
    std::int64_t last_success_at_ms() const;

    /// \brief Возвращает оценку отставания по времени.
    /// \return `now_epoch_ms() - last_success_at_ms()`, либо `0`, если успехов не было.
    std::int64_t estimated_lag_ms() const;

    /// \brief Возвращает общее число скачанных блоков.
    std::uint64_t blocks_downloaded_total() const;

    /// \brief Возвращает общее число merge-операций.
    std::uint64_t blocks_merged_total() const;

    /// \brief Возвращает число блоков, пропущенных по freshness-правилу.
    std::uint64_t blocks_skipped_total() const;

    /// \brief Возвращает число ошибок синхронизации.
    std::uint64_t sync_errors_total() const;

    /// \brief Возвращает число обнаруженных divergence-ситуаций.
    std::uint64_t divergence_total() const;

    /// \brief Выполняет один полный синхронный обход всех peers.
    /// \details Метод предназначен для детерминированных тестов и ручного вызова.
    void sync_once();

private:
    /// \brief Фоновый цикл с периодическим вызовом `sync_once()`.
    void loop();

    /// \brief Синхронизирует локальную ноду с одним peer'ом.
    /// \param peer Конфигурация peer-ноды.
    /// \param had_any_success Флаг успешного опроса хотя бы одного peer'а за цикл.
    void sync_peer(const config::PeerConfig &peer, bool &had_any_success);

    /// \brief Загружает метаданные блоков с peer-ноды.
    /// \param peer Конфигурация peer-ноды.
    /// \return Список метаданных блоков.
    std::vector<BlockMeta> fetch_peer_meta(const config::PeerConfig &peer);

    /// \brief Загружает raw `dfhbin` блок с peer-ноды.
    /// \param peer Конфигурация peer-ноды.
    /// \param key Ключ блока.
    /// \return Содержимое блока в формате `dfhbin`.
    std::vector<std::uint8_t> fetch_peer_block(const config::PeerConfig &peer, const BlockKey &key);

    /// \brief Добавляет заголовки авторизации и anti-replay для исходящего HTTP-запроса.
    /// \param headers Набор заголовков запроса.
    /// \param method HTTP-метод.
    /// \param path Путь без query string.
    /// \param query_string Query string без ведущего `?`.
    /// \param body_hash SHA-256 тела запроса в hex.
    void add_auth_headers(SimpleWeb::CaseInsensitiveMultimap &headers, const std::string &method,
                          const std::string &path, const std::string &query_string, const std::string &body_hash);

    IDfhAdapter &m_adapter;
    const config::Config &m_cfg;
    DiskMonitor *m_disk_monitor;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::condition_variable m_cv;
    std::mutex m_cv_mutex;
    std::atomic<std::int64_t> m_last_attempt_at_ms{0};
    std::atomic<std::int64_t> m_last_success_at_ms{0};
    std::atomic<std::uint64_t> m_blocks_downloaded{0};
    std::atomic<std::uint64_t> m_blocks_merged{0};
    std::atomic<std::uint64_t> m_blocks_skipped{0};
    std::atomic<std::uint64_t> m_sync_errors{0};
    std::atomic<std::uint64_t> m_divergence{0};
    mutable std::mutex m_inflight_mutex;
    std::set<BlockKey> m_inflight;
};

} // namespace dfh_node
