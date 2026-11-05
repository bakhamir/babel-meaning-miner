#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "bmm/passage.hpp"

namespace bmm {
struct AnalysisMetrics {
  std::uint64_t lexical_matches{}, phrase_candidates{}, sentence_candidates{}, grammar_accepted{}, high_scoring{}, passage_candidates{}, grammatical_passages{}, coherent_passages{};
  double lexical_seconds{}, candidate_seconds{}, grammar_seconds{}, ngram_seconds{}, passage_seconds{};
};
struct PageAnalysis { std::vector<Finding> findings; std::vector<Passage> passages; AnalysisMetrics metrics; };
class Analyzer {
  struct Node { std::unordered_map<char, int> next; int fail{}; std::vector<int> out; };
  std::vector<Node> trie_;
  std::vector<std::string> words_;
  bool sentences_{};
  SentenceOptions sentence_options_{};
  SentenceScorer sentence_scorer_{};
  bool passages_{};
  PassageOptions passage_options_{};
  bool boundary(std::string_view text, std::size_t begin, std::size_t end) const;
public:
  Analyzer(std::vector<std::string> words, std::size_t min_len = 3, std::size_t max_len = 24);
  Analyzer(std::vector<std::string> words, SentenceOptions options, SentenceScorer scorer,
           std::size_t min_len = 1, std::size_t max_len = 24);
  static Analyzer from_file(const std::string& path, std::size_t min_len = 3, std::size_t max_len = 24);
  static Analyzer sentences_from_file(const std::string& path, SentenceOptions options,
                                      SentenceScorer scorer = {});
  static Analyzer passages_from_file(const std::string& path, SentenceOptions options,
                                    PassageOptions passages = {}, SentenceScorer scorer = {});
  PageAnalysis analyze_page(std::string_view text, const std::string& address) const;
  const std::vector<std::string>& dictionary() const { return words_; }
  std::vector<Finding> analyze(std::string_view text, const std::string& address) const;
};
}
