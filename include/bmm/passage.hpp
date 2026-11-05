#pragma once
#include "bmm/sentence.hpp"
#include <cstdint>

namespace bmm {
struct Finding {
  std::string page_address, text, method;
  std::size_t start{}, end{};
  double lexical_score{}, language_score{};
  std::string classification{"WORD"};
  std::size_t word_count{1};
  double grammar_score{}, ngram_score{}, completeness_score{};
  double semantic_score{}; bool has_semantic_score{};
  std::string scoring_model{"baseline-untrained"}, scoring_version{"1"}, validation_method{"lexical"}, source_backend{"local"};
  std::string source_mode{"natural"}, verification_status{"source-exact"};
};
struct PassageOptions {
  std::size_t min_sentences{2}, max_sentences{8}, max_chars{8192};
  double coherence_threshold{0.6};
};
struct Passage {
  std::int64_t id{}, experiment_id{};
  std::string page_address, text, classification{"MULTI_SENTENCE_CANDIDATE"};
  std::size_t start{}, end{};
  std::vector<Finding> sentences;
  double coherence_score{}, lexical_continuity{}, topic_consistency{}, entity_repetition{}, pronoun_reference{}, transition_score{}, ngram_score{};
  std::string scoring_model{"baseline-untrained"}, scoring_version{"3"}, validation_method{"surface-coherence-v3"}, source_backend{"local"}, source_mode{"natural"}, verification_status{"source-exact"};
  std::string generator_version;
};
class PassageDiscoveryEngine {
  PassageOptions options_;
  SentenceScorer scorer_;
  Passage assess(std::string_view text, const std::string& address, const std::vector<Finding>& sentences) const;
public:
  PassageDiscoveryEngine(PassageOptions options = {}, SentenceScorer scorer = {});
  std::vector<Passage> discover(std::string_view text, const std::string& address,
                              const std::vector<Finding>& sentences) const;
  Passage score(std::string_view text, const std::string& address = "input") const;
};
}
