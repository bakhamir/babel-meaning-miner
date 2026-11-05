#pragma once
#include "bmm/analyzer.hpp"
#include <cstdint>
#include <string>
#include <vector>
struct sqlite3;
namespace bmm {
struct Experiment { std::int64_t id{}; std::string backend; std::uint64_t seed{}; std::string configuration; std::string status; std::uint64_t pages_scanned{}; double elapsed_seconds{}; };
class Storage {
  sqlite3* db_{};
public:
  void migrate_v3();
  void commit_work(std::int64_t id, const PageAnalysis& result, std::uint64_t completed, double elapsed);
  void save_passages(std::int64_t id, const std::vector<Passage>& passages);
  std::vector<Passage> passages(std::int64_t id, std::size_t limit=20, const std::string& sort="coherence") const;
  Passage passage(std::int64_t id) const;
  void export_passages(std::int64_t id,const std::string& format,const std::string& path) const;
  std::string integrity_check() const;
  std::string summary_json(std::int64_t id) const;
  std::string occurrences_json(std::int64_t id, const std::string& type, std::size_t limit=100) const;
  explicit Storage(const std::string& path); ~Storage();
  Storage(const Storage&) = delete; Storage& operator=(const Storage&) = delete;
  std::int64_t create_experiment(const std::string& backend, std::uint64_t seed, const std::string& config, const std::string& generator_version="splitmix64-v1");
  void save_batch(std::int64_t experiment_id, const std::vector<Finding>& findings);
  void checkpoint(std::int64_t experiment_id, std::uint64_t completed, double elapsed, const std::string& status);
  Experiment experiment(std::int64_t id) const;
  std::vector<Finding> findings(std::int64_t experiment_id, std::size_t limit = 100, const std::string& type="all", const std::string& sort="score") const;
  std::vector<Finding> recent_findings(std::size_t limit = 20) const;
  void export_results(std::int64_t id, const std::string& format, const std::string& path) const;
};
}
