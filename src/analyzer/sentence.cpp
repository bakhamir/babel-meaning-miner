#include "bmm/sentence.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace bmm {
namespace {
void add(std::unordered_map<std::string,unsigned>&m,Pos p,const char* s){std::istringstream in(s);for(std::string w;in>>w;)m[w]|=static_cast<unsigned>(p);}
bool in(const std::string&w,const char* list){return (std::string(" ")+list+" ").find(" "+w+" ")!=std::string::npos;}
}
Grammar::Grammar(){
 add(lexicon_,Pos::pronoun,"i you he she it we they someone");
 add(lexicon_,Pos::determiner,"a an the this that these those my your his her its our their");
 add(lexicon_,Pos::noun,"bird birds cat cats dog dogs man men woman women child children table garden house world book library time water earth life truth story light night day mind page word language sun economy people");
 add(lexicon_,Pos::verb,"fly flies flew sing sings sang see sees saw walk walks walked run runs ran read reads write writes wrote love loves loved find finds found know knows knew think thinks thought live lives lived grow grows grew shine shines shone sleep sleeps slept wake wakes woke rise rises rose eat eats ate chase chases chased");
 add(lexicon_,Pos::auxiliary,"am is are was were do does did can could will would shall should may might must");
 add(lexicon_,Pos::adjective,"happy sad old young good bad great small dark bright quiet red blue true false purple");
 add(lexicon_,Pos::adverb,"quickly slowly always never well quietly brightly not");
 add(lexicon_,Pos::preposition,"in on under over by with from to at for of through near inside outside");
 add(lexicon_,Pos::conjunction,"and but or");
}
Grammar Grammar::from_file(const std::string&path){Grammar g;std::ifstream f(path);if(!f)throw std::runtime_error("cannot open POS lexicon: "+path);const std::map<std::string,Pos> tags={{"NOUN",Pos::noun},{"VERB",Pos::verb},{"PRON",Pos::pronoun},{"DET",Pos::determiner},{"ADJ",Pos::adjective},{"ADV",Pos::adverb},{"PREP",Pos::preposition},{"CONJ",Pos::conjunction},{"AUX",Pos::auxiliary}};for(std::string line;std::getline(f,line);){if(line.empty()||line[0]=='#')continue;std::istringstream in(line);std::string w,p;in>>w>>p;if(!tags.contains(p))throw std::runtime_error("invalid POS tag");g.lexicon_[w]|=static_cast<unsigned>(tags.at(p));}return g;}
bool Grammar::has(std::string_view w,Pos p)const{auto it=lexicon_.find(std::string(w));return it!=lexicon_.end()&&(it->second&static_cast<unsigned>(p));}
std::vector<std::string> Grammar::vocabulary()const{std::vector<std::string> v;for(auto&[w,p]:lexicon_){(void)p;v.push_back(w);}std::sort(v.begin(),v.end());return v;}
GrammarResult Grammar::evaluate(const std::vector<std::string>&w)const{
 GrammarResult r;auto reject=[&](const char*s){r.reason=s;return r;};if(w.size()<2||w.size()>80)return reject("word count out of grammar bounds");
 std::size_t i=0;bool plural=false,first=false,second=false;
 auto np=[&](bool subject){bool det=false;std::string d;if(i<w.size()&&has(w[i],Pos::determiner)){d=w[i++];det=true;}while(i<w.size()&&has(w[i],Pos::adjective))++i;if(i==w.size()||!has(w[i],Pos::noun))return false;auto n=w[i++];bool pl=in(n,"men women children people")||(n.back()=='s'&&n!="news");if((d=="a"||d=="an"||d=="this"||d=="that")&&pl)return false;if((d=="these"||d=="those")&&!pl)return false;if(!det&&!pl&&!in(n,"time water earth life truth light language"))return false;if(subject)plural=pl;return true;};
 if(has(w[0],Pos::pronoun)){plural=in(w[0],"we they");first=w[0]=="i";second=w[0]=="you";++i;}else if(!np(true))return reject("invalid subject noun phrase");
 const bool third=!plural&&!first&&!second;std::size_t predicates=0;
 do {
  if(++predicates>4)return reject("too many predicates");bool aux=false,link=false;std::string verb;
  if(i<w.size()&&has(w[i],Pos::auxiliary)){verb=w[i++];aux=true;link=in(verb,"am is are was were");
   if((verb=="am"&&!first)||(verb=="is"&&!third)||(verb=="are"&&!plural&&!second)||(verb=="was"&&(plural||second))||(verb=="were"&&third)||(verb=="does"&&!third)||(verb=="do"&&third))return reject("subject-verb disagreement");
   if(i<w.size()&&w[i]=="not")++i;
  }
  if(link){bool complement=false;if(i<w.size()&&has(w[i],Pos::adjective)){++i;complement=true;}else if(i<w.size()&&!has(w[i],Pos::preposition))complement=np(false);if(!complement&&(i==w.size()||!has(w[i],Pos::preposition)))return reject("linking verb lacks complement");r.pattern="SUBJ LINK COMPLEMENT";}
  else {
   if(i>=w.size()||!has(w[i],Pos::verb))return reject("missing lexical verb");verb=w[i++];bool past=in(verb,"flew sang saw walked ran wrote loved found knew thought lived grew shone slept woke rose ate chased");bool sform=verb.back()=='s';
   if(aux&&(past||sform))return reject("auxiliary requires base verb");if(!aux&&!past&&third!=sform)return reject("present subject-verb disagreement");
   if(i<w.size()&&has(w[i],Pos::adverb)&&w[i]!="not")++i;
   bool object=false;if(i<w.size()&&(has(w[i],Pos::determiner)||has(w[i],Pos::noun)||has(w[i],Pos::adjective))){object=np(false);if(!object)return reject("invalid object");}
   bool transitive=in(verb,"see sees saw love loves loved find finds found know knows knew chase chases chased");bool intransitive=in(verb,"sleep sleeps slept wake wakes woke fly flies flew rise rises rose walk walks walked run runs ran live lives lived");
   if(transitive&&!object)return reject("transitive verb lacks object");if(intransitive&&object)return reject("intransitive verb has direct object");r.pattern=object?"SUBJ VERB OBJECT":"SUBJ VERB";
  }
  for(unsigned pp=0;i<w.size()&&has(w[i],Pos::preposition)&&pp<3;++pp){++i;if(!np(false))return reject("invalid prepositional object");r.pattern+=" PP";}
  if(i<w.size()&&has(w[i],Pos::adverb)&&w[i]!="not")++i;
  if(i==w.size())break;if(!in(w[i],"and but or"))return reject("unparsed trailing words");++i;if(i==w.size())return reject("unfinished compound predicate");
 }while(i<w.size());
 r.accepted=true;r.score=1;r.completeness=1;r.reason="finite-rule-v3: "+r.pattern;return r;
}
SentenceAssessment SentenceScorer::score(std::string_view text)const{auto g=grammar_.evaluate(NGramLanguageModel::tokenize(text));auto l=model_.score(text);return {g,l,g.accepted?"GRAMMATICAL_SENTENCE":"SENTENCE_CANDIDATE",g.score*.7+(l.trained?std::exp(l.normalized)*.3:0)};}
}
