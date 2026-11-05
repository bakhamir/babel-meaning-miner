#include "bmm/language_model.hpp"
#include "bmm/fingerprint.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <limits>
#include <stdexcept>
namespace bmm {
namespace {std::string bi(const std::string&a,const std::string&b){return a+'\t'+b;}std::string tri(const std::string&a,const std::string&b,const std::string&c){return bi(a,b)+'\t'+c;}template<class M>std::uint64_t count(const M&m,const std::string&k){auto i=m.find(k);return i==m.end()?0:i->second;}}
std::vector<std::string> NGramLanguageModel::tokenize(std::string_view s){std::vector<std::string> out;std::string w;for(unsigned char c:s){if(c>='A'&&c<='Z')c+=32;if(c>='a'&&c<='z')w+=char(c);else if(!w.empty()){out.push_back(w);w.clear();}}if(!w.empty())out.push_back(w);return out;}
NGramLanguageModel NGramLanguageModel::train(const std::string&p){std::ifstream f(p);if(!f)throw std::runtime_error("cannot open corpus: "+p);NGramLanguageModel m;m.corpus_checksum_=file_fingerprint(p);m.id_=m.corpus_checksum_;m.version_="interpolated-add01-v3";m.uni_["<unk>"]=0;
 auto sentence=[&](std::string_view s){auto words=tokenize(s);if(words.empty())return;std::string a="<s>",b="<s>";words.push_back("</s>");for(auto&w:words){++m.uni_[w];++m.total_tokens_;++m.bi_[bi(b,w)];++m.tri_[tri(a,b,w)];++m.bi_context_[b];++m.tri_context_[bi(a,b)];a=b;b=w;}};
 for(std::string line;std::getline(f,line);){std::size_t start=0;for(std::size_t i=0;i<line.size();++i)if(line[i]=='.'||line[i]=='!'||line[i]=='?'){sentence(std::string_view(line).substr(start,i-start));start=i+1;}sentence(std::string_view(line).substr(start));}
 if(!m.total_tokens_)throw std::runtime_error("empty corpus");return m;
}
void NGramLanguageModel::save(const std::string&p)const{auto path=std::filesystem::path(p);if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());std::ofstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot save model");f<<"BMMNGRAM3 "<<std::quoted(id_)<<' '<<std::quoted(version_)<<' '<<std::quoted(corpus_checksum_)<<' '<<total_tokens_<<'\n';auto save=[&](char tag,const auto&m){std::map<std::string,std::uint64_t> sorted(m.begin(),m.end());for(auto&[k,v]:sorted)f<<tag<<' '<<v<<' '<<std::quoted(k)<<'\n';};save('U',uni_);save('B',bi_);save('T',tri_);if(!f)throw std::runtime_error("model write failed");}
NGramLanguageModel NGramLanguageModel::load(const std::string& path) {
 std::ifstream input(path,std::ios::binary);
 NGramLanguageModel m;
 std::string magic;
 if(!(input>>magic>>std::quoted(m.id_)>>std::quoted(m.version_)>>std::quoted(m.corpus_checksum_)>>m.total_tokens_)||magic!="BMMNGRAM3"||!m.total_tokens_)
  throw std::runtime_error("invalid/legacy model; retrain with v0.3");
 char tag;std::uint64_t value;std::string key;
 while(input>>tag){
  if(!(input>>value>>std::quoted(key)))throw std::runtime_error("truncated model");
  auto* counts=tag=='U'?&m.uni_:tag=='B'?&m.bi_:tag=='T'?&m.tri_:nullptr;
  if(!counts||!counts->emplace(key,value).second)throw std::runtime_error("invalid/duplicate model row");
 }
 if(!input.eof()||!m.uni_.contains("<unk>")||!m.uni_.contains("</s>"))throw std::runtime_error("invalid model vocabulary");
 auto add=[](std::uint64_t& total,std::uint64_t n){if(n>std::numeric_limits<std::uint64_t>::max()-total)throw std::runtime_error("model count overflow");total+=n;};
 std::uint64_t total=0;
 for(auto&[word,n]:m.uni_){
  if(word!="<unk>"&&word!="</s>"&&(word.empty()||word.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=word.npos))
   throw std::runtime_error("invalid vocabulary token");
  if(!n&&word!="<unk>")throw std::runtime_error("zero model count");
  add(total,n);
 }
 if(total!=m.total_tokens_)throw std::runtime_error("model count mismatch");
 std::unordered_map<std::string,std::uint64_t> predicted,suffix;
 auto context=[&](const std::string& w){return w=="<s>"||(m.uni_.contains(w)&&w!="</s>");};
 total=0;
 for(auto&[key,n]:m.bi_){
  auto split=key.find('\t');
  if(!n||split==key.npos||key.find('\t',split+1)!=key.npos)throw std::runtime_error("invalid bigram");
  auto left=key.substr(0,split),right=key.substr(split+1);
  if(!context(left)||!m.uni_.contains(right))throw std::runtime_error("invalid bigram vocabulary");
  add(m.bi_context_[left],n);add(predicted[right],n);add(total,n);
 }
 if(total!=m.total_tokens_)throw std::runtime_error("bigram total mismatch");
 for(auto&[word,n]:m.uni_)if(predicted[word]!=n)throw std::runtime_error("bigram marginal mismatch");
 total=0;
 for(auto&[key,n]:m.tri_){
  auto one=key.find('\t'),two=one==key.npos?key.npos:key.find('\t',one+1);
  if(!n||two==key.npos||key.find('\t',two+1)!=key.npos)throw std::runtime_error("invalid trigram");
  auto a=key.substr(0,one),b=key.substr(one+1,two-one-1),c=key.substr(two+1);
  if(!context(a)||!context(b)||!m.uni_.contains(c)||(b=="<s>"&&a!="<s>"))throw std::runtime_error("invalid trigram vocabulary");
  add(m.tri_context_[bi(a,b)],n);add(suffix[bi(b,c)],n);add(total,n);
 }
 if(total!=m.total_tokens_||suffix!=m.bi_)throw std::runtime_error("trigram marginal mismatch");
 return m;
}
LanguageScore NGramLanguageModel::score(std::string_view text)const{LanguageScore s;s.trained=trained();s.model_id=id_;s.version=version_;auto words=tokenize(text);s.tokens=words.size();if(words.empty()||!trained())return s;for(auto&w:words)if(!uni_.contains(w)){w="<unk>";++s.unknown_tokens;}words.push_back("</s>");s.predicted_tokens=words.size();double V=double(uni_.size());std::string a="<s>",b="<s>";double logp=0;for(auto&w:words){double u=(count(uni_,w)+.1)/(total_tokens_+.1*V);double bprob=(count(bi_,bi(b,w))+.1)/(count(bi_context_,b)+.1*V);double t=(count(tri_,tri(a,b,w))+.1)/(count(tri_context_,bi(a,b))+.1*V);s.unigram+=std::log(u);s.bigram+=std::log(bprob);s.trigram+=std::log(t);logp+=std::log(.2*u+.3*bprob+.5*t);a=b;b=w;}s.normalized=logp/words.size();s.perplexity=std::exp(-s.normalized);return s;}
}
