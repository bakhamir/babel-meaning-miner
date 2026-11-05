#include "bmm/passage.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace bmm {
namespace {
bool ws(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
bool sentence_bytes(std::string_view s){return std::all_of(s.begin(),s.end(),[](char c){return ws(c)||(c>='a'&&c<='z')||(c>='A'&&c<='Z');});}
using Words=std::set<std::string>;
double jaccard(const Words& a,const Words& b){std::size_t shared=0;for(const auto&w:a)shared+=b.contains(w);const auto total=a.size()+b.size()-shared;return total?double(shared)/total:0;}
double dice(const Words& a,const Words& b){std::size_t shared=0;for(const auto&w:a)shared+=b.contains(w);return a.empty()&&b.empty()?0:2.0*shared/(a.size()+b.size());}
// This deliberately finite table describes surface noun features, not a semantic model.
struct Entity {std::string lemma,topic;bool plural{},human{};char gender{'?'};};
const std::map<std::string,Entity>& entities(){
 static const auto table=[] {
  std::map<std::string,Entity> t;
  auto add=[&](const char* one,const char* many,const char* topic,bool human=false,char gender='?'){
   t[one]={one,topic,false,human,gender};if(*many)t[many]={one,topic,true,human,gender};
  };
  add("cat","cats","animals");add("dog","dogs","animals");add("bird","birds","animals");
  add("man","men","people",true,'m');add("woman","women","people",true,'f');add("child","children","people",true);add("person","people","people",true);
  add("table","tables","places");add("garden","gardens","nature");add("house","houses","places");
  add("sun","","nature");add("earth","","nature");add("water","","nature");add("light","lights","nature");add("night","nights","time");add("day","days","time");add("time","","time");
  add("book","books","text");add("library","libraries","text");add("story","stories","text");add("page","pages","text");add("word","words","text");add("language","languages","text");
  add("economy","economies","economics");add("life","lives","abstract");add("truth","truths","abstract");add("mind","minds","abstract");add("world","worlds","places");return t;
 }();return table;
}
struct Features {Words lexical,nouns,topics;std::vector<Entity> mentions;std::string pronoun,predicate;};
Features features(const Finding& f){
 static const Words stop={"a","an","the","and","but","or","am","is","are","was","were","do","does","did","not","can","could","will","would","shall","should","may","might","must","i","you","he","she","it","we","they","my","your","his","her","its","our","their","in","on","at","to","of","for","by","from","with","under","over","near","this","that","these","those"};
 static const Words pronouns={"he","she","it","they"};
 static const std::map<std::string,std::string> predicates={{"sleep","sleep"},{"sleeps","sleep"},{"slept","sleep"},{"wake","wake"},{"wakes","wake"},{"woke","wake"},{"rise","rise"},{"rises","rise"},{"rose","rise"},{"shine","shine"},{"shines","shine"},{"shone","shine"},{"eat","eat"},{"eats","eat"},{"ate","eat"}};
 Features x;auto tokens=NGramLanguageModel::tokenize(f.text);
 if(!tokens.empty()&&pronouns.contains(tokens.front()))x.pronoun=tokens.front();
 for(const auto&w:tokens){if(!stop.contains(w))x.lexical.insert(w);auto e=entities().find(w);if(e!=entities().end()){x.nouns.insert(e->second.lemma);x.topics.insert(e->second.topic);x.mentions.push_back(e->second);}auto p=predicates.find(w);if(x.predicate.empty()&&p!=predicates.end())x.predicate=p->second;}
 return x;
}
double reference(const Features& left,const std::string& pronoun){
 std::map<std::string,double> candidates;
 for(const auto&e:left.mentions){double compatibility=0;
  if(pronoun=="they"&&e.plural)compatibility=1;
  else if(pronoun=="it"&&!e.plural&&!e.human)compatibility=1;
  else if((pronoun=="he"||pronoun=="she")&&!e.plural&&e.human){char gender=pronoun=="he"?'m':'f';compatibility=e.gender==gender?1:e.gender=='?'?.5:0;}
  if(compatibility)candidates[e.lemma]=compatibility;
 }
 if(candidates.empty())return 0;double best=0;for(const auto&[name,value]:candidates){(void)name;best=std::max(best,value);}return candidates.size()==1?best:.5*best;
}
double transition(const Features& left,const Features& right,double supported_reference){
 // Explicit progression pairs are illustrative discourse cues, never entailment evidence.
 const bool common=dice(left.nouns,right.nouns)>0||supported_reference>0;
 if(!common)return 0;
 if((left.predicate=="sleep"&&right.predicate=="wake")||(left.predicate=="wake"&&right.predicate=="sleep")||(left.predicate=="rise"&&right.predicate=="shine")||(left.predicate=="wake"&&right.predicate=="eat"))return 1;
 return supported_reference>0?.5:.25;
}
std::size_t terminated_end(std::string_view text,std::size_t end){
 while(end<text.size()&&ws(text[end]))++end;
 return end<text.size()&&text[end]=='.'?end+1:std::string_view::npos;
}
bool sentence_start(std::string_view text,std::size_t start){
 if(start==0)return true;const auto original=start;while(start&&ws(text[start-1]))--start;
 return start==0||(original>start&&text[start-1]=='.');
}
}
PassageDiscoveryEngine::PassageDiscoveryEngine(PassageOptions o,SentenceScorer s):options_(o),scorer_(std::move(s)){if(o.min_sentences<2||o.max_sentences<o.min_sentences||o.max_sentences>32||o.max_chars>1000000||o.max_chars==0||!std::isfinite(o.coherence_threshold)||o.coherence_threshold<0||o.coherence_threshold>1)throw std::runtime_error("invalid passage bounds");}
Passage PassageDiscoveryEngine::assess(std::string_view text,const std::string&addr,const std::vector<Finding>&sentences)const {
 Passage p;p.page_address=addr;p.sentences=sentences;if(sentences.empty())return p;p.start=sentences.front().start;p.end=terminated_end(text,sentences.back().end);
 if(p.end==text.npos)p.end=sentences.back().end;p.text=std::string(text.substr(p.start,p.end-p.start));bool accepted=true;std::size_t references=0;double support=0;
 auto previous=features(sentences.front());
 for(std::size_t i=0;i<sentences.size();++i){accepted&=sentences[i].classification=="GRAMMATICAL_SENTENCE";if(!i)continue;auto next=features(sentences[i]);double ref=0;
  if(!next.pronoun.empty()){++references;ref=reference(previous,next.pronoun);p.pronoun_reference+=ref;}
  p.lexical_continuity+=jaccard(previous.lexical,next.lexical);p.entity_repetition+=dice(previous.nouns,next.nouns);p.topic_consistency+=jaccard(previous.topics,next.topics);p.transition_score+=transition(previous,next,ref);
  support+=dice(previous.nouns,next.nouns)>0||ref>0?1:0;previous=std::move(next);
 }
 const double pairs=double(std::max<std::size_t>(1,sentences.size()-1));p.lexical_continuity/=pairs;p.entity_repetition/=pairs;p.topic_consistency/=pairs;p.transition_score/=pairs;
 p.pronoun_reference=references?p.pronoun_reference/references:.5;
 // Correlated surface proxies provide an ordering, not calibrated probabilities.
 const double base=.2*p.lexical_continuity+.25*p.entity_repetition+.2*p.topic_consistency+.25*p.transition_score;
 p.coherence_score=references?base+.1*p.pronoun_reference:base/.9;
 p.ngram_score=scorer_.model().score(p.text).normalized;p.scoring_model=scorer_.model().id();p.scoring_version=scorer_.model().version();p.validation_method="surface-features-v3.1: lexical Jaccard, entity Dice, finite-topic Jaccard, explicit discourse cues, local pronoun compatibility; no semantic validation";
 if(accepted&&sentences.size()>=options_.min_sentences)p.classification=p.coherence_score>=options_.coherence_threshold&&support==pairs?"COHERENT_PASSAGE":"GRAMMATICAL_PASSAGE";return p;
}
std::vector<Passage> PassageDiscoveryEngine::discover(std::string_view text,const std::string&addr,const std::vector<Finding>&sentences)const{
 std::vector<Passage> out;std::vector<Finding> run;std::size_t previous_end=0;bool seen=false;
 for(const auto&s:sentences)if(s.method=="sentence"){
  if(s.start>=s.end||s.end>text.size()||(seen&&s.start<previous_end)||s.page_address!=addr||text.substr(s.start,s.end-s.start)!=s.text||!sentence_bytes(s.text)||ws(s.text.front())||ws(s.text.back()))
   throw std::invalid_argument("sentence provenance, order or source span mismatch");
  previous_end=s.end;seen=true;
 }
 auto flush=[&]{if(run.size()>=options_.min_sentences)out.push_back(assess(text,addr,run));run.clear();};
 for(auto&s:sentences){if(s.method!="sentence")continue;const auto end=terminated_end(text,s.end);if(end==text.npos||!sentence_start(text,s.start)){flush();continue;}
  if(!run.empty()){auto prev=terminated_end(text,run.back().end);bool consecutive=prev<s.start&&std::all_of(text.begin()+prev,text.begin()+s.start,ws);if(!consecutive||run.size()==options_.max_sentences||end-run.front().start>options_.max_chars)flush();}
  if(end-s.start<=options_.max_chars)run.push_back(s);
 }flush();return out;
}
Passage PassageDiscoveryEngine::score(std::string_view text,const std::string&addr)const{
 std::vector<Finding> ss;std::size_t begin=0;bool valid=text.size()<=options_.max_chars;
 while(valid&&begin<text.size()){
  while(begin<text.size()&&ws(text[begin]))++begin;if(begin==text.size())break;
  if(ss.size()==options_.max_sentences){valid=false;break;}
  const auto end=text.find('.',begin);if(end==text.npos){valid=false;break;}auto trim=end;while(trim>begin&&ws(text[trim-1]))--trim;if(trim==begin){valid=false;break;}
  auto source=text.substr(begin,trim-begin);if(!sentence_bytes(source)){valid=false;break;}auto s=scorer_.score(source);Finding f;f.page_address=addr;f.text=std::string(source);f.method="sentence";f.start=begin;f.end=trim;f.word_count=NGramLanguageModel::tokenize(f.text).size();f.classification=s.classification;f.grammar_score=s.grammar.score;f.completeness_score=s.grammar.completeness;f.ngram_score=s.language.normalized;f.language_score=s.rank_score;f.scoring_model=s.language.model_id;f.scoring_version=s.language.version;f.validation_method=s.grammar.reason;f.source_mode="user-input";f.source_backend="user-input";ss.push_back(std::move(f));begin=end+1;
  if(begin<text.size()&&!ws(text[begin]))valid=false;
 }
 auto p=assess(text,addr,ss);p.source_mode="user-input";p.source_backend="user-input";
 p.start=0;p.end=text.size();p.text=std::string(text); // Preserve the entire submitted text even on rejection.
 p.ngram_score=text.size()<=options_.max_chars?scorer_.model().score(text).normalized:0;
 if(!valid||ss.size()<options_.min_sentences||begin<text.size())p.classification="MULTI_SENTENCE_CANDIDATE";
 return p;
}
}
