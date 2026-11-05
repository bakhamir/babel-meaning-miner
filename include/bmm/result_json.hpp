#pragma once
#include "bmm/passage.hpp"
#include "bmm/json.hpp"

namespace bmm {
inline std::string finding_json(const Finding& f) {
    std::ostringstream o;
    o << "{\"page_address\":" << json_quote(f.page_address)
      << ",\"start_offset\":" << f.start << ",\"end_offset\":" << f.end
      << ",\"text\":" << json_quote(f.text) << ",\"classification\":" << json_quote(f.classification)
      << ",\"word_count\":" << f.word_count << ",\"lexical_score\":" << f.lexical_score
      << ",\"language_score\":" << f.language_score << ",\"grammar_score\":" << f.grammar_score
      << ",\"ngram_score\":" << f.ngram_score << ",\"completeness_score\":" << f.completeness_score
      << ",\"semantic_score\":" << (f.has_semantic_score ? std::to_string(f.semantic_score) : "null")
      << ",\"scoring_model\":" << json_quote(f.scoring_model) << ",\"scoring_version\":" << json_quote(f.scoring_version)
      << ",\"validation_method\":" << json_quote(f.validation_method) << ",\"source_mode\":" << json_quote(f.source_mode)
      << ",\"source_backend\":" << json_quote(f.source_backend) << ",\"verification_status\":" << json_quote(f.verification_status) << '}';
    return o.str();
}
inline std::string passage_json(const Passage& p) {
    std::ostringstream o;
    o << "{\"id\":" << p.id << ",\"experiment\":" << p.experiment_id << ",\"page_address\":" << json_quote(p.page_address)
      << ",\"source_mode\":" << json_quote(p.source_mode) << ",\"source_backend\":" << json_quote(p.source_backend)
      << ",\"generator_version\":" << json_quote(p.generator_version)
      << ",\"start_offset\":" << p.start << ",\"end_offset\":" << p.end << ",\"characters\":" << p.text.size()
      << ",\"text\":" << json_quote(p.text) << ",\"classification\":" << json_quote(p.classification)
      << ",\"sentence_count\":" << p.sentences.size() << ",\"coherence\":" << p.coherence_score
      << ",\"lexical_continuity\":" << p.lexical_continuity << ",\"topic_consistency\":" << p.topic_consistency
      << ",\"entity_repetition\":" << p.entity_repetition << ",\"pronoun_reference\":" << p.pronoun_reference
      << ",\"transition_score\":" << p.transition_score << ",\"ngram_score\":" << p.ngram_score
      << ",\"scoring_model\":" << json_quote(p.scoring_model) << ",\"scoring_version\":" << json_quote(p.scoring_version)
      << ",\"validation_method\":" << json_quote(p.validation_method) << ",\"verification_status\":" << json_quote(p.verification_status)
      << ",\"sentences\":[";
    for (std::size_t i=0; i<p.sentences.size(); ++i) { if(i)o<<','; o<<finding_json(p.sentences[i]); }
    o << "]}";
    return o.str();
}
}
