#include "bmm/feasibility.hpp"
#include "bmm/analyzer.hpp"
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
namespace bmm {
std::string feasibility_json(const std::string&path,std::size_t size,double pages,unsigned ns){
 if(size<1||!std::isfinite(pages)||pages<=0||ns<2||ns>32)throw std::runtime_error("invalid feasibility options");auto words=Analyzer::from_file(path,1,24).dictionary();if(words.size()>256)throw std::runtime_error("exact grammar enumeration supports at most 256 unique words; use an explicit research subset");std::map<std::size_t,double> dist;double q=3.0/29;for(auto&w:words)dist[w.size()]+=std::pow(29.0,-static_cast<int>(w.size()));
 auto expectation=[&](const auto&d){double e=0;for(auto&[length,p]:d)if(length<=size){auto slots=size-length+1;e+=p*(slots==1?1:2*q+(slots-2)*q*q);}return e;};auto e1=expectation(dist);
 auto convolution=[&](const auto&a,const auto&b){std::map<std::size_t,double> out;for(auto&[la,pa]:a)for(auto&[lb,pb]:b)if(la+lb+1<=size)out[la+lb+1]+=pa*pb/29.0;return out;};auto two=convolution(dist,dist),three=convolution(two,dist);auto e2=expectation(two),e3=expectation(three);
 Grammar grammar;std::map<std::size_t,double> accepted;std::size_t count=0;for(auto&a:words)for(auto&b:words)for(auto&c:words)if(grammar.evaluate({a,b,c}).accepted){auto len=a.size()+b.size()+c.size()+2;accepted[len]+=std::pow(29.0,-static_cast<int>(len));++count;}
 auto eg=expectation(accepted);double mass=0;for(auto&[len,p]:accepted){(void)len;mass+=p;}double consecutive_bound=size*std::pow(mass,ns)*std::pow(1.0/(29*29),ns-1)/29;
 auto inverse=[](double value){if(value<=0||!std::isfinite(1/value))return std::string("null");std::ostringstream s;s<<std::setprecision(12)<<1/value;return s.str();};std::ostringstream o;o<<std::setprecision(12)<<"{\"assumption\":\"IID uniform 29-symbol approximation to fixed SplitMix64; single-space adjacency\",\"dictionary_unique\":"<<words.size()<<",\"pages\":"<<pages<<",\"expected_words_per_page\":"<<e1<<",\"expected_two_words_per_page\":"<<e2<<",\"expected_three_words_per_page\":"<<e3<<",\"finite_three_word_grammar_count\":"<<count<<",\"expected_finite_three_word_sentences_per_page\":"<<eg<<",\"expected_three_words_total\":"<<e3*pages<<",\"mean_wait_pages_finite_grammar_approx\":"<<inverse(eg)<<",\"min_sentences\":"<<ns<<",\"finite_three_word_passage_upper_bound_per_page\":"<<consecutive_bound<<",\"waiting_pages_lower_bound_finite_passage_approx\":"<<inverse(consecutive_bound)<<",\"zero_events_95pct_poisson_rate_bound_per_page\":"<<-std::log(.05)/pages<<",\"semantically_coherent_probability\":null,\"warning\":\"Finite 3-word subset, not all English sentences; grammar and coherence heuristics are not semantic truth. Poisson waiting approximations ignore overlap.\"}";return o.str();
}
}
