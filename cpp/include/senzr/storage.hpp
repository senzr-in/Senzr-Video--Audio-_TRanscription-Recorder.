#pragma once

#include "app/database.hpp"
#include "app/database_models.hpp"

namespace senzr {

/**
 * @brief Enterprise-grade SQLite Offline Storage engine for the Senzr SDK.
 *
 * Implements WAL mode, thread-safe write locks, atomic RAII transactions,
 * prepared statement execution, dual-write upload queues, and automated schema maintenance.
 */
using Storage = app::Database;

// Typed SDK models
using DetectionEventRow = app::DetectionEventRow;
using UploadQueueRow    = app::UploadQueueRow;
using TelemetryRow      = app::TelemetryRow;

} // namespace senzr
