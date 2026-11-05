#pragma once
#include "bmm/language_model.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bmm {
enum class Pos : unsigned { none=0,noun=1,verb=2,pronoun=4,determiner=8,adjective=16,adverb=32,preposition=64,conjunction=128,auxiliary=256 };
struct SentenceOptions { std::size_t min_words{3}, max_words{20}, min_chars{8}; double grammar_threshold{0.65}, ngram_threshold{-20.0}; };
struct GrammarResult { bool accepted{}; double score{}; double completeness{}; std::string pattern, reason; };
class Grammar {
  std::unordered_map<std::string,unsigned> lexicon_;
  bool has(std::string_view word, Pos pos) const;
public:
  Grammar();
  static Grammar from_file(const std::string& path);
  GrammarResult evaluate(const std::vector<std::string>& words) const;
  std::vector<std::string> vocabulary() const;
};
struct SentenceAssessment { GrammarResult grammar; LanguageScore language; std::string classification; double rank_score{}; };
class SentenceScorer {
  Grammar grammar_;
  NGramLanguageModel model_;
public:
  SentenceScorer(Grammar grammar = {}, NGramLanguageModel model = {}):grammar_(std::move(grammar)),model_(std::move(model)){}
  SentenceAssessment score(std::string_view text) const;
  const NGramLanguageModel& model() const { return model_; }
  const Grammar& grammar() const { return grammar_; }
};
}
