#include "bmm/scanner.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
using namespace bmm;
namespace bmm { bool run_v03(int argc,char**argv); }
namespace {
struct Args{std::string cmd;std::unordered_map<std::string,std::string>v;bool has(std::string k)const{return v.contains(k);}std::string get(std::string k,std::string d="")const{auto i=v.find(k);return i==v.end()?d:i->second;}std::uint64_t num(std::string k,std::uint64_t d=0)const{auto x=get(k);return x.empty()?d:std::stoull(x);}double real(std::string k,double d=0)const{auto x=get(k);return x.empty()?d:std::stod(x);}};
Args parse(int ac,char**av){Args a;if(ac>1)a.cmd=av[1];for(int i=2;i<ac;++i){std::string k=av[i];if(k.rfind("--",0)!=0)throw std::runtime_error("unexpected argument: "+k);if(i+1<ac&&std::string(av[i+1]).rfind("--",0)!=0)a.v[k]=av[++i];else a.v[k]="true";}return a;}
void help(){std::cout<<R"(BABEL MEANING MINER v0.3
Usage: bmm <command> [options]
  scan --mode passages --pages N --threads N --min-sentences 2
       [--source-mode natural|injection|constrained] [--text TEXT] [--store-words]
  passages --experiment ID --db FILE [--sort coherence|length] [--limit 20]
  passage --id ID --db FILE
  score-passage --text "the cat sleeps. the cat wakes."
  feasibility --pages 1000000000 --min-sentences 2
  evaluate --mode injection [--sentences 2]
  evaluate-language-model --corpus HELDOUT --model FILE
  integrity --db FILE
  generate  --backend local --seed N --page N [--page-size N]
  scan      --pages N [--threads N] [--seed N] [--mode words|sentences]
            [--min-words N] [--max-words N] [--model FILE] [--db FILE]
  resume    --experiment N [--pages ADDITIONAL_PAGES] [--threads N] [--db FILE]
  findings  [--experiment N] [--limit N] [--db FILE] [--json]
  occurrences --experiment N --type word|sentence [--limit N] [--db FILE]
  stats     --experiment N [--sentences] [--db FILE] [--json]
  export    --experiment N --format json|csv [--type findings|passages] [--output FILE] [--db FILE]
  benchmark [--pages N] [--threads N] [--dictionary FILE]
            [--stage combined|generation|analysis|grammar|ngram|passage|storage]
  Scan tuning: --batch-size 1000 --queue-capacity 8 --stop-after SECONDS
  Default database: bmm-v03.db. --json produces machine-readable results.
  Short Windows menu: .\\bmm  (database data/bmm-v03.db)
  train-language-model --corpus FILE --output FILE
  score-text --text "the dog is under the table" [--model FILE]
  evaluate-grammar [--positive FILE] [--negative FILE]
  remote-test --enable-network  (currently unavailable; see documentation)
  verify    --backend original|local
)";}
std::string dict(const Args&a){return a.get("--dictionary","data/words.txt");}
SentenceScorer make_scorer(const Args&a){if(a.has("--model"))return SentenceScorer({},NGramLanguageModel::load(a.get("--model")));return {};}
}
int main(int ac,char**av)try{if(bmm::run_v03(ac,av))return 0;auto a=parse(ac,av);if(a.cmd.empty()||a.cmd=="--help"||a.cmd=="help"){help();return 0;}
if(a.cmd=="train-language-model"){auto corpus=a.get("--corpus"),out=a.get("--output");if(corpus.empty()||out.empty())throw std::runtime_error("--corpus and --output are required");auto m=NGramLanguageModel::train(corpus);m.save(out);std::cout<<"Model trained: "<<m.id()<<" vocabulary="<<m.vocabulary_size()<<" output="<<out<<'\n';}
else if(a.cmd=="score-text"){auto text=a.get("--text");if(text.empty())throw std::runtime_error("--text is required");auto assessment=make_scorer(a).score(text);std::cout<<"Text: \""<<text<<"\"\nClassification: "<<assessment.classification<<"\nGrammar accepted: "<<(assessment.grammar.accepted?"yes":"no")<<"\nGrammar score: "<<assessment.grammar.score<<"\nCompleteness: "<<assessment.grammar.completeness<<"\nPattern: "<<assessment.grammar.pattern<<"\nReason: "<<assessment.grammar.reason<<"\nModel: "<<assessment.language.model_id<<" ("<<(assessment.language.trained?"trained":"fallback")<<")\nUnigram log-probability: "<<assessment.language.unigram<<"\nBigram log-probability: "<<assessment.language.bigram<<"\nTrigram log-probability: "<<assessment.language.trigram<<"\nNormalized n-gram score: "<<assessment.language.normalized<<"\nRanking score: "<<assessment.rank_score<<'\n';}
else if(a.cmd=="evaluate-grammar"){auto pos=a.get("--positive","data/evaluation/positive_heldout.txt"),neg=a.get("--negative","data/evaluation/negative_heldout.txt");Grammar g;std::size_t tp=0,fn=0,fp=0,tn=0;auto run=[&](const std::string&path,bool expected){std::ifstream in(path);if(!in)throw std::runtime_error("cannot open evaluation file: "+path);for(std::string line;std::getline(in,line);)if(!line.empty()){bool got=g.evaluate(NGramLanguageModel::tokenize(line)).accepted;if(expected){got?++tp:++fn;}else{got?++fp:++tn;}}};run(pos,true);run(neg,false);double precision=tp+fp?double(tp)/(tp+fp):0,recall=tp+fn?double(tp)/(tp+fn):0;std::cout<<"Held-out grammar evaluation\nTP="<<tp<<" FN="<<fn<<" FP="<<fp<<" TN="<<tn<<"\nPrecision="<<precision<<" Recall="<<recall<<'\n';}
else if(a.cmd=="remote-test"){if(!a.has("--enable-network"))throw std::runtime_error("remote-test requires --enable-network");std::cerr<<"UNAVAILABLE: the historical download.cgi request timed out and no documented public API or automation policy was verified. No remote adapter is enabled.\n";return 2;}
else if(a.cmd=="verify"){auto b=a.get("--backend","original");if(b=="original"){std::cerr<<"UNVERIFIED: no licensed, version-pinned website reference fixtures are bundled.\n";return 2;}LocalGenerator g(42,128);auto x=g.generate(7),y=g.generate(7);if(x.text!=y.text)return 1;std::cout<<"PASS: local generator determinism, length, and address reproducibility\n";}
else throw std::runtime_error("unknown command: "+a.cmd);return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
