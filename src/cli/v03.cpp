#include "bmm/scanner.hpp"
#include "bmm/feasibility.hpp"
#include "bmm/fingerprint.hpp"
#include "bmm/result_json.hpp"
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <thread>
namespace bmm {
namespace {
volatile std::sig_atomic_t interrupted=0;void interrupt(int){interrupted=1;}
struct Options {
 std::map<std::string,std::string> v;
 bool has(const std::string&k)const{return v.contains(k);}
 std::string get(const std::string&k,std::string d="")const{auto i=v.find(k);return i==v.end()?d:i->second;}
 std::uint64_t num(const std::string&k,std::uint64_t d)const{if(!has(k))return d;auto s=get(k);std::uint64_t n{};auto[p,e]=std::from_chars(s.data(),s.data()+s.size(),n);if(e!=std::errc{}||p!=s.data()+s.size())throw std::runtime_error("invalid integer: "+k);return n;}
 double real(const std::string&k,double d)const{if(!has(k))return d;std::size_t end;auto s=get(k);double n=std::stod(s,&end);if(end!=s.size()||!std::isfinite(n))throw std::runtime_error("invalid number: "+k);return n;}
};
Options parse(int argc,char**argv){
 Options a;
 const std::set<std::string> flags={"--quiet","--json","--store-words","--enable-network"};
 const std::set<std::string> values={"--mode","--backend","--source-mode","--seed","--pages","--page","--page-size","--alphabet","--threads","--batch-size","--queue-capacity","--dictionary","--model","--lexicon","--min-words","--max-words","--min-chars","--grammar-threshold","--ngram-threshold","--min-sentences","--max-sentences","--coherence-threshold","--max-passage-chars","--sentence-count","--text","--db","--experiment","--id","--limit","--sort","--type","--format","--output","--corpus","--stage","--stop-after"};
 for(int i=2;i<argc;++i){
  std::string key=argv[i];if(a.has(key))throw std::runtime_error("repeated option: "+key);
  if(flags.contains(key))a.v[key]="true";
  else if(key=="--sentences"){a.v[key]=(i+1<argc&&!std::string(argv[i+1]).starts_with("--"))?argv[++i]:"true";}
  else{
   if(!values.contains(key))throw std::runtime_error("unknown option: "+key);
   if(++i==argc||std::string(argv[i]).starts_with("--"))throw std::runtime_error("missing value: "+key);
   a.v[key]=argv[i];
  }
 }return a;
}
SentenceScorer scorer(const Options&a){return {a.has("--lexicon")?Grammar::from_file(a.get("--lexicon")):Grammar{},a.has("--model")?NGramLanguageModel::load(a.get("--model")):NGramLanguageModel{}};}
Analyzer analyzer(const Options&a){auto mode=a.get("--mode","words");auto path=a.get("--dictionary","data/words.txt");SentenceOptions so{a.num("--min-words",3),a.num("--max-words",20),a.num("--min-chars",8),a.real("--grammar-threshold",.65),a.real("--ngram-threshold",-20)};if(mode=="words")return Analyzer::from_file(path);if(mode=="sentences")return Analyzer::sentences_from_file(path,so,scorer(a));if(mode=="passages"){PassageOptions po;po.min_sentences=a.num("--min-sentences",2);po.max_sentences=a.num("--max-sentences",8);po.max_chars=a.num("--max-passage-chars",8192);po.coherence_threshold=a.real("--coherence-threshold",.6);return Analyzer::passages_from_file(path,so,po,scorer(a));}throw std::runtime_error("mode must be words|sentences|passages");}
std::unique_ptr<Generator> generator(const Options&a){if(a.get("--backend","local")!="local")throw std::runtime_error("backend unavailable: original pages cannot be substituted locally");auto mode=a.get("--source-mode","natural");auto size=a.num("--page-size",3200),seed=a.num("--seed",42);if(!size||size>1000000)throw std::runtime_error("page-size must be 1..1000000");if(mode=="natural")return std::make_unique<LocalGenerator>(seed,size,a.get("--alphabet","abcdefghijklmnopqrstuvwxyz ,."));if(mode=="injection")return std::make_unique<InjectionGenerator>(seed,size,a.get("--text"));if(mode=="constrained")return std::make_unique<ConstrainedGenerator>(seed,size,a.num("--sentence-count",2));throw std::runtime_error("source-mode must be natural|injection|constrained; reversible targeted mapping unavailable");}
std::string metrics(const AnalysisMetrics&m){std::ostringstream o;o<<"{\"lexical\":"<<m.lexical_matches<<",\"phrases\":"<<m.phrase_candidates<<",\"sentence_candidates\":"<<m.sentence_candidates<<",\"grammar_accepted\":"<<m.grammar_accepted<<",\"high_score\":"<<m.high_scoring<<",\"passages\":"<<m.passage_candidates<<",\"grammatical_passages\":"<<m.grammatical_passages<<",\"coherent_passages\":"<<m.coherent_passages<<",\"lexical_seconds\":"<<m.lexical_seconds<<",\"candidate_seconds\":"<<m.candidate_seconds<<",\"grammar_seconds\":"<<m.grammar_seconds<<",\"ngram_seconds\":"<<m.ngram_seconds<<",\"passage_seconds\":"<<m.passage_seconds<<'}';return o.str();}
void freeze(Options&a){a.v["--dictionary"]=a.get("--dictionary","data/words.txt");for(auto k:{"--dictionary","--model","--lexicon"})if(a.has(k)){a.v[k]=std::filesystem::absolute(a.get(k)).generic_string();a.v[std::string(k)+"-hash"]=file_fingerprint(a.get(k));}}
ScanStats run(Options a,bool resume,bool print=true){
 auto path=a.get("--db","bmm-v03.db");std::filesystem::path p(path);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());Storage db(path);std::int64_t id=0;std::uint64_t start=0;
 if(resume){id=a.num("--experiment",0);auto e=db.experiment(id);Options saved{config_decode(e.configuration)};if(saved.get("engine")!="v0.3")throw std::runtime_error("old experiment lacks reproducible v0.3 configuration; start a new experiment");for(auto&[k,v]:a.v){if(k=="--experiment"||k=="--db")continue;if(k=="--pages"||k=="--threads"||k=="--json"||k=="--quiet"||k=="--stop-after")saved.v[k]=v;else if(saved.get(k)!=v)throw std::runtime_error("resume cannot change "+k);}a=std::move(saved);start=e.pages_scanned;for(auto k:{"--dictionary","--model","--lexicon"})if(a.has(k)&&file_fingerprint(a.get(k))!=a.get(std::string(k)+"-hash"))throw std::runtime_error("input changed: "+std::string(k));}
 else{freeze(a);a.v["engine"]="v0.3";}
 auto threads=a.num("--threads",4);if(!threads||threads>256)throw std::runtime_error("threads must be 1..256");auto g=generator(a);auto az=analyzer(a);if(!resume)id=db.create_experiment(g->name(),a.num("--seed",42),config_encode(a.v),g->version());
 ScanConfig c{a.num("--pages",10000),start,static_cast<unsigned>(threads),static_cast<std::size_t>(a.num("--batch-size",1000)),a.has("--quiet")||a.has("--json")};c.queue_capacity=a.num("--queue-capacity",8);c.store_words=a.has("--store-words")||a.get("--mode","words")=="words";c.source_mode=g->source_mode();
 Scanner scan;interrupted=0;std::signal(SIGINT,interrupt);std::signal(SIGTERM,interrupt);auto began=std::chrono::steady_clock::now();double stop=a.real("--stop-after",0);std::jthread monitor([&](std::stop_token token){while(!token.stop_requested()){if(interrupted||(stop>0&&std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()>=stop)){scan.cancel();break;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}});
 if(!c.quiet&&print)std::cout<<"Experiment: "<<id<<"\nSource mode: "<<c.source_mode<<'\n';auto last=began;
 auto s=scan.run(*g,az,db,id,c,[&](const ScanStats&s){auto now=std::chrono::steady_clock::now();if(!c.quiet&&print&&now-last>std::chrono::seconds(1)){std::cerr<<"Processed "<<s.pages<<"/"<<c.pages<<", "<<s.pages_per_second<<" pages/s\n";last=now;}});monitor.request_stop();
 if(print)std::cout<<"{\"experiment\":"<<id<<",\"mode\":"<<json_quote(a.get("--mode","words"))<<",\"source_mode\":"<<json_quote(c.source_mode)<<",\"status\":"<<json_quote(s.status)<<",\"pages\":"<<s.pages<<",\"seconds\":"<<s.seconds<<",\"pages_per_second\":"<<s.pages_per_second<<",\"stored_findings\":"<<s.candidates<<",\"passages\":"<<s.passages<<",\"generation_seconds\":"<<s.generation_seconds<<",\"database_seconds\":"<<s.database_seconds<<",\"queue_wait_seconds\":"<<s.queue_wait_seconds<<",\"metrics\":"<<metrics(s.metrics)<<"}\n";return s;
}
}
bool run_v03(int argc,char**argv){if(argc<2)return false;std::string cmd=argv[1];std::set<std::string> commands={"scan","resume","generate","passages","passage","score-passage","evaluate","feasibility","benchmark","integrity","stats","findings","occurrences","evaluate-language-model"};if(!commands.contains(cmd)&&cmd!="export")return false;Options a=parse(argc,argv);
 if(cmd=="scan"||cmd=="resume"){run(a,cmd=="resume");return true;}
 if(cmd=="generate"){auto g=generator(a);auto p=g->generate(a.num("--page",0));std::cout<<"{\"address\":"<<json_quote(p.address)<<",\"source_mode\":"<<json_quote(g->source_mode())<<",\"text\":"<<json_quote(p.text)<<"}\n";return true;}
 if(cmd=="feasibility"){std::cout<<feasibility_json(a.get("--dictionary","data/words.txt"),a.num("--page-size",3200),double(a.num("--pages",1000000000)),a.num("--min-sentences",2))<<'\n';return true;}
 if(cmd=="score-passage"){if(!a.has("--text"))throw std::runtime_error("--text required");std::cout<<passage_json(PassageDiscoveryEngine({},scorer(a)).score(a.get("--text")))<<'\n';return true;}
 if(cmd=="evaluate-language-model"){auto m=NGramLanguageModel::load(a.get("--model"));std::ifstream f(a.get("--corpus"));if(!f)throw std::runtime_error("cannot open evaluation corpus");double logp=0;std::size_t count=0,unknown=0;for(std::string line;std::getline(f,line);){auto s=m.score(line);logp+=s.normalized*s.predicted_tokens;count+=s.predicted_tokens;unknown+=s.unknown_tokens;}if(!count)throw std::runtime_error("empty evaluation corpus");std::cout<<"{\"predicted_tokens\":"<<count<<",\"unknown_tokens\":"<<unknown<<",\"perplexity\":"<<std::exp(-logp/count)<<",\"model\":"<<json_quote(m.id())<<"}\n";return true;}
 if(cmd=="evaluate"){if(a.has("--sentences")&&a.num("--sentences",2)!=2)throw std::runtime_error("this curated evaluation supports --sentences 2");
  if(a.get("--mode","injection")!="injection")throw std::runtime_error("evaluate mode must be injection");
  std::vector<std::string> pos={"the cat sleeps. the cat wakes.","the dog walks. the dog runs.","the bird sings. the bird flies."};std::vector<std::string> neg={"the cat sleeps. qqq qqq. the cat wakes.","the dog sleep. the dog runs.","the bird sings the bird flies."};auto az=Analyzer::passages_from_file(a.get("--dictionary","data/words.txt"),{},{});std::size_t tp=0,fp=0,fn=0,tn=0;
  for(bool positive:{true,false})for(auto&text:positive?pos:neg)for(unsigned seed=0;seed<10;++seed){InjectionGenerator g(seed,3200,text);auto p=g.generate(seed);auto result=az.analyze_page(p.text,p.address);bool detected=false;for(auto&pass:result.passages)if(pass.classification!="MULTI_SENTENCE_CANDIDATE"&&p.injection_start&&pass.start==*p.injection_start&&pass.end==*p.injection_end)detected=true;if(positive){detected?++tp:++fn;}else{detected?++fp:++tn;}}
  std::cout<<"{\"source_mode\":\"injection\",\"TP\":"<<tp<<",\"FN\":"<<fn<<",\"FP\":"<<fp<<",\"TN\":"<<tn<<",\"precision\":"<<(tp+fp?double(tp)/(tp+fp):0)<<",\"recall\":"<<(tp+fn?double(tp)/(tp+fn):0)<<",\"scope\":\"explicit small synthetic fixtures, not natural discoveries\"}\n";return true;
 }
 if(cmd=="benchmark"){
  auto stage=a.get("--stage","combined");auto pages=a.num("--pages",10000);if(!pages||pages>1000000000)throw std::runtime_error("benchmark pages out of bounds");auto begin=std::chrono::steady_clock::now();auto cpu=std::clock();std::uint64_t check=0;
  if(stage=="combined"){auto stamp=std::chrono::system_clock::now().time_since_epoch().count();auto folder=std::filesystem::path("runs")/("bench-"+std::to_string(stamp));std::filesystem::create_directories(folder);for(unsigned n:{1u,2u,4u,8u,12u}){if(a.has("--threads")&&n!=a.num("--threads",4))continue;auto b=a;b.v["--threads"]=std::to_string(n);b.v["--quiet"]="true";b.v["--json"]="true";b.v["--db"]=(folder/(std::to_string(n)+".db")).string();auto s=run(b,false,false);std::cout<<"{\"stage\":\"combined\",\"threads\":"<<n<<",\"pages\":"<<s.pages<<",\"seconds\":"<<s.seconds<<",\"pages_per_second\":"<<s.pages_per_second<<",\"database_seconds\":"<<s.database_seconds<<",\"queue_wait_seconds\":"<<s.queue_wait_seconds<<",\"metrics\":"<<metrics(s.metrics)<<"}\n";}return true;}
  auto g=generator(a);auto az=analyzer(a);std::vector<Page> sample;if(stage=="analysis"){for(std::uint64_t i=0;i<std::min<std::uint64_t>(pages,256);++i)sample.push_back(g->generate(i));begin=std::chrono::steady_clock::now();cpu=std::clock();}
  for(std::uint64_t i=0;i<pages;++i){if(stage=="generation")check+=g->generate(i).text.size();else if(stage=="analysis")check+=az.analyze_page(sample[i%sample.size()].text,sample[i%sample.size()].address).findings.size();else if(stage=="grammar")check+=Grammar{}.evaluate(NGramLanguageModel::tokenize("the cat sleeps")).accepted;else if(stage=="ngram")check+=scorer(a).model().score("the cat sleeps").tokens;else if(stage=="passage")check+=PassageDiscoveryEngine{}.score("the cat sleeps. the cat wakes.").sentences.size();else throw std::runtime_error("unknown stage");}
  double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::cout<<"{\"stage\":"<<json_quote(stage)<<",\"items\":"<<pages<<",\"seconds\":"<<sec<<",\"items_per_second\":"<<pages/sec<<",\"runtime_clock_seconds\":"<<double(std::clock()-cpu)/CLOCKS_PER_SEC<<",\"checksum\":"<<check<<"}\n";return true;
 }
 auto path=a.get("--db","bmm-v03.db");if(!std::filesystem::exists(path))throw std::runtime_error("database not found");Storage db(path);auto id=a.num("--experiment",0);
 if(cmd=="stats"){std::cout<<db.summary_json(id)<<'\n';return true;}
 if(cmd=="integrity"){std::cout<<json_quote(db.integrity_check())<<'\n';return true;}
 if(cmd=="passage"){std::cout<<passage_json(db.passage(a.num("--id",0)))<<'\n';return true;}
 if(cmd=="occurrences"){std::cout<<db.occurrences_json(id,a.get("--type"),a.num("--limit",100))<<'\n';return true;}
 if(cmd=="export"){auto type=a.get("--type","findings");if(type=="passages")db.export_passages(id,a.get("--format","json"),a.get("--output","passages."+a.get("--format","json")));else if(type=="findings")db.export_results(id,a.get("--format","json"),a.get("--output","findings."+a.get("--format","json")));else throw std::runtime_error("export type must be findings|passages");return true;}
 if(a.num("--limit",20)>10000)throw std::runtime_error("limit must be 0..10000");
 if(cmd=="findings"){auto rows=a.has("--experiment")?db.findings(id,a.num("--limit",20),a.get("--type","all"),a.get("--sort","score")):db.recent_findings(a.num("--limit",20));std::cout<<'[';bool first=true;for(auto&f:rows){if(!first)std::cout<<',';first=false;std::cout<<finding_json(f);}std::cout<<"]\n";return true;}
 std::cout<<'[';bool first=true;for(auto&p:db.passages(id,a.num("--limit",20),a.get("--sort","coherence"))){if(!first)std::cout<<',';first=false;std::cout<<passage_json(p);}std::cout<<"]\n";return true;
}
}
