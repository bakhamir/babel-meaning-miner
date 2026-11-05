#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bmm {
struct LanguageScore {
  double unigram{}, bigram{}, trigram{}, normalized{};
  std::size_t tokens{};
  std::size_t predicted_tokens{}, unknown_tokens{};
  double perplexity{};
  bool trained{};
  std::string model_id{"baseline-untrained"}, version{"1"};
};
class NGramLanguageModel {
  std::unordered_map<std::string, std::uint64_t> uni_, bi_, tri_;
  std::uint64_t total_tokens_{};
  std::unordered_map<std::string, std::uint64_t> bi_context_, tri_context_;
  std::string corpus_checksum_;
  std::string id_{"baseline-untrained"}, version_{"1"};
public:
  static std::vector<std::string> tokenize(std::string_view text);
  static NGramLanguageModel train(const std::string& corpus_path);
  static NGramLanguageModel load(const std::string& path);
  void save(const std::string& path) const;
  LanguageScore score(std::string_view text) const;
  bool trained() const { return total_tokens_ != 0; }
  const std::string& id() const { return id_; }
  const std::string& version() const { return version_; }
  std::size_t vocabulary_size() const { return uni_.size(); }
  const std::string& corpus_checksum() const { return corpus_checksum_; }
};
}
