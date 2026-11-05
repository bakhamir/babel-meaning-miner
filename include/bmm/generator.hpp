#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <stdexcept>
#include <optional>

namespace bmm {
struct Page {
  std::string address; std::string text; std::uint64_t index{};
  std::string source_mode{"natural"}, generator_version;
  std::optional<std::size_t> injection_start, injection_end;
};
class Generator {
public:
  virtual ~Generator() = default;
  virtual Page generate(std::uint64_t index) const = 0;
  virtual std::string name() const = 0;
  virtual std::string version() const = 0;
  virtual std::string source_mode() const { return "natural"; }
  virtual std::string description() const { return name(); }
};
class LocalGenerator final : public Generator {
  std::uint64_t seed_; std::size_t page_size_; std::string alphabet_;
public:
  LocalGenerator(std::uint64_t seed, std::size_t page_size = 3200,
                 std::string alphabet = "abcdefghijklmnopqrstuvwxyz ,.");
  Page generate(std::uint64_t index) const override;
  std::string name() const override { return "local"; }
  std::string version() const override { return "splitmix64-v1"; }
  std::size_t page_size() const { return page_size_; }
  const std::string& alphabet() const { return alphabet_; }
};
// Evaluation source only. Replacement offsets refer to immutable generated output.
class InjectionGenerator final : public Generator {
  LocalGenerator base_; std::string fixture_; std::uint64_t seed_;
public:
  InjectionGenerator(std::uint64_t seed, std::size_t page_size, std::string fixture,
                     std::string alphabet = "abcdefghijklmnopqrstuvwxyz ,.");
  Page generate(std::uint64_t index) const override;
  std::string name() const override { return "local-injection"; }
  std::string version() const override { return "controlled-injection-v1"; }
  std::string source_mode() const override { return "injection"; }
  std::string description() const override { return fixture_; }
};
class ConstrainedGenerator final : public Generator {
  std::uint64_t seed_; std::size_t size_, sentences_;
public:
  ConstrainedGenerator(std::uint64_t seed, std::size_t page_size = 3200,
                       std::size_t sentences = 2);
  Page generate(std::uint64_t index) const override;
  std::string name() const override { return "constrained"; }
  std::string version() const override { return "finite-grammar-v1"; }
  std::string source_mode() const override { return "constrained"; }
  std::string description() const override { return std::to_string(sentences_); }
};
}
