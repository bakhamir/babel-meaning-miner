#include "bmm/analyzer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <queue>
#include <set>
#include <stdexcept>
namespace bmm {
namespace {
bool letter(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
char lower(char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):c;}
bool spaces(std::string_view s){return !s.empty()&&s.find_first_not_of(" \t\r\n")==s.npos;}
std::vector<std::string> load_words(const std::string&p){std::ifstream f(p);if(!f)throw std::runtime_error("cannot open dictionary: "+p);std::vector<std::string> out;for(std::string w;std::getline(f,w);){if(!w.empty()&&w.back()=='\r')w.pop_back();out.push_back(w);}return out;}
using Clock=std::chrono::steady_clock;double elapsed(Clock::time_point t){return std::chrono::duration<double>(Clock::now()-t).count();}
}
Analyzer::Analyzer(std::vector<std::string> words,std::size_t min,std::size_t max){
 if(!min||min>max||max>256)throw std::runtime_error("invalid dictionary length bounds");trie_.emplace_back();std::set<std::string> unique;
 for(auto w:words){std::transform(w.begin(),w.end(),w.begin(),lower);if(w.size()>=min&&w.size()<=max&&std::all_of(w.begin(),w.end(),letter))unique.insert(w);}
 for(auto&w:unique){int n=0;for(char c:w){auto it=trie_[n].next.find(c);int child;if(it==trie_[n].next.end()){child=static_cast<int>(trie_.size());trie_[n].next[c]=child;trie_.emplace_back();}else child=it->second;n=child;}trie_[n].out.push_back(static_cast<int>(words_.size()));words_.push_back(w);}
 std::queue<int> q;for(auto[c,n]:trie_[0].next){(void)c;q.push(n);}while(!q.empty()){int v=q.front();q.pop();for(auto[c,u]:trie_[v].next){int f=trie_[v].fail;while(f&&!trie_[f].next.contains(c))f=trie_[f].fail;if(trie_[f].next.contains(c))f=trie_[f].next.at(c);trie_[u].fail=f;trie_[u].out.insert(trie_[u].out.end(),trie_[f].out.begin(),trie_[f].out.end());q.push(u);}}
}
Analyzer::Analyzer(std::vector<std::string>w,SentenceOptions o,SentenceScorer s,std::size_t min,std::size_t max):Analyzer(std::move(w),min,max){if(o.min_words<2||o.max_words<o.min_words||o.max_words>80||o.grammar_threshold<0||o.grammar_threshold>1)throw std::runtime_error("invalid sentence options");sentences_=true;sentence_options_=o;sentence_scorer_=std::move(s);}
Analyzer Analyzer::from_file(const std::string&p,std::size_t min,std::size_t max){return Analyzer(load_words(p),min,max);}
Analyzer Analyzer::sentences_from_file(const std::string&p,SentenceOptions o,SentenceScorer s){return Analyzer(load_words(p),o,std::move(s));}
Analyzer Analyzer::passages_from_file(const std::string&p,SentenceOptions o,PassageOptions po,SentenceScorer s){Analyzer a(load_words(p),o,std::move(s));PassageDiscoveryEngine check(po);a.passages_=true;a.passage_options_=po;return a;}
bool Analyzer::boundary(std::string_view s,std::size_t b,std::size_t e)const{return (b==0||!letter(s[b-1]))&&(e==s.size()||!letter(s[e]));}
PageAnalysis Analyzer::analyze_page(std::string_view text,const std::string&addr)const{
 PageAnalysis result;auto t=Clock::now();int n=0;auto&hits=result.findings;
 for(std::size_t i=0;i<text.size();++i){char c=lower(text[i]);while(n&&!trie_[n].next.contains(c))n=trie_[n].fail;if(trie_[n].next.contains(c))n=trie_[n].next.at(c);for(int wi:trie_[n].out){auto len=words_[wi].size(),b=i+1-len,e=i+1;if(boundary(text,b,e))hits.push_back({addr,std::string(text.substr(b,len)),"dictionary",b,e,double(len),std::log1p(double(len))});}}
 result.metrics.lexical_matches=hits.size();result.metrics.lexical_seconds=elapsed(t);t=Clock::now();std::vector<Finding> spans;
 for(std::size_t i=0;i<hits.size();){std::size_t j=i+1;while(j<hits.size()&&spaces(text.substr(hits[j-1].end,hits[j].start-hits[j-1].end)))++j;
  if(j-i>=2)++result.metrics.phrase_candidates;
  if(!sentences_){for(std::size_t b=i;b<j;++b)for(std::size_t e=b+1;e<j&&e-b<20;++e){Finding f{addr,std::string(text.substr(hits[b].start,hits[e].end-hits[b].start)),"phrase",hits[b].start,hits[e].end,0,0};f.classification="PHRASE";f.word_count=e-b+1;f.language_score=std::log1p(double(f.text.size()))*f.word_count;spans.push_back(std::move(f));}}
  else {
   // Full bounded contiguous runs only: never accept a grammatical prefix of an invalid longer run.
   auto count=j-i;if(count>=sentence_options_.min_words&&count<=sentence_options_.max_words&&hits[j-1].end-hits[i].start>=sentence_options_.min_chars){
    auto begin=hits[i].start,end=hits[j-1].end;std::string candidate(text.substr(begin,end-begin));++result.metrics.sentence_candidates;
    auto gt=Clock::now();auto g=sentence_scorer_.grammar().evaluate(NGramLanguageModel::tokenize(candidate));result.metrics.grammar_seconds+=elapsed(gt);auto nt=Clock::now();LanguageScore ls;if(g.accepted)ls=sentence_scorer_.model().score(candidate);result.metrics.ngram_seconds+=elapsed(nt);
    Finding f{addr,candidate,"sentence",begin,end,double(count),g.score*.7+(ls.trained?std::exp(ls.normalized)*.3:0)};f.word_count=count;f.grammar_score=g.score;f.completeness_score=g.completeness;f.ngram_score=ls.normalized;f.scoring_model=sentence_scorer_.model().id();f.scoring_version=sentence_scorer_.model().version();f.validation_method=g.reason;f.classification=g.accepted?"GRAMMATICAL_SENTENCE":"SENTENCE_CANDIDATE";
    if(g.accepted)++result.metrics.grammar_accepted;if(g.accepted&&g.score>=sentence_options_.grammar_threshold&&(!ls.trained||ls.normalized>=sentence_options_.ngram_threshold))++result.metrics.high_scoring;
    spans.push_back(std::move(f));
   }
  }i=j;
 }
 result.metrics.candidate_seconds=elapsed(t)-result.metrics.grammar_seconds-result.metrics.ngram_seconds;
 if(passages_){t=Clock::now();result.passages=PassageDiscoveryEngine(passage_options_,sentence_scorer_).discover(text,addr,spans);result.metrics.passage_candidates=result.passages.size();for(auto&p:result.passages){if(p.classification=="GRAMMATICAL_PASSAGE"||p.classification=="COHERENT_PASSAGE")++result.metrics.grammatical_passages;if(p.classification=="COHERENT_PASSAGE")++result.metrics.coherent_passages;}result.metrics.passage_seconds=elapsed(t);}
 hits.insert(hits.end(),std::make_move_iterator(spans.begin()),std::make_move_iterator(spans.end()));return result;
}
std::vector<Finding> Analyzer::analyze(std::string_view text,const std::string&addr)const{return analyze_page(text,addr).findings;}
}
