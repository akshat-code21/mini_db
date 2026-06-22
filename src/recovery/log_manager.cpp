#include "recovery/log_manager.h"
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace minidb {

LogManager::LogManager(const std::string& log_file_path) : log_file_path_(log_file_path) {
    auto parent = std::filesystem::path(log_file_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    log_file_.open(log_file_path, std::ios::app | std::ios::binary);
    lsn_t max_lsn = 0;
    for (const auto& record : ReadAllLogs()) max_lsn = std::max(max_lsn, record.lsn);
    next_lsn_ = max_lsn + 1;
}

LogManager::~LogManager() {
    Flush();
    if (log_file_.is_open()) log_file_.close();
}

lsn_t LogManager::AppendLog(LogRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    record.lsn = next_lsn_++;
    std::string data = record.Serialize();
    buffer_.push_back(std::move(data));
    return record.lsn;
}

void LogManager::Flush(lsn_t up_to_lsn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!log_file_.is_open()) return;
    for (const auto& data : buffer_) {
        log_file_.write(data.data(), data.size());
    }
    log_file_.flush();
    buffer_.clear();
    flushed_lsn_ = next_lsn_.load() - 1;
}

std::vector<LogRecord> LogManager::ReadAllLogs() {
    std::vector<LogRecord> records;
    std::ifstream in(log_file_path_, std::ios::binary);
    if (!in.is_open()) return records;

    while (in.good() && !in.eof()) {
        uint32_t record_len;
        in.read(reinterpret_cast<char*>(&record_len), sizeof(record_len));
        if (!in.good() || in.eof()) break;
        if (record_len == 0 || record_len > 1024 * 1024) break;

        std::vector<char> data(record_len);
        in.read(data.data(), record_len);
        if (!in.good() && !in.eof()) break;

        LogRecord rec = LogRecord::Deserialize(data.data(), record_len);
        records.push_back(std::move(rec));
    }
    return records;
}

void LogManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_.is_open()) log_file_.close();
    log_file_.open(log_file_path_, std::ios::out | std::ios::binary | std::ios::trunc);
    buffer_.clear();
}

}  // namespace minidb
