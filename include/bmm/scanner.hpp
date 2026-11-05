#pragma once
#include "bmm/analyzer.hpp"
#include "bmm/generator.hpp"
#include "bmm/storage.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
namespace bmm {
struct ScanConfig { std::uint64_t pages{}, start_page{}; unsigned threads{1}; std::size_t batch_size{100}; bool quiet{}; std::size_t queue_capacity{8}; bool store_words{true}; std::string source_mode{"natural"}; };
struct ScanStats { std::uint64_t pages{}, characters{}, candidates{}; double seconds{}, pages_per_second{}, chars_per_second{}; std::string best; AnalysisMetrics metrics; std::uint64_t passages{}; double generation_seconds{},database_seconds{},queue_wait_seconds{}; std::string status; };
using ProgressCallback = std::function<void(const ScanStats&)>;
class Scanner {
public:
  ScanStats run(const Generator&, const Analyzer&, Storage&, std::int64_t experiment_id,
                const ScanConfig&, ProgressCallback progress = {});
  void cancel() { cancelled_.store(true); }
private: std::atomic<bool> cancelled_{false};
};
}
