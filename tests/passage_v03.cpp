#include "bmm/analyzer.hpp"
#include "bmm/generator.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <limits>
#define CHECK(x) if(!(x))throw std::runtime_error(#x)
int main()try{using namespace bmm;
 auto a=Analyzer::passages_from_file("data/words.txt",{},{});std::string s="the cat sleeps. the cat wakes.";auto r=a.analyze_page(s,"test");CHECK(r.passages.size()==1);CHECK(r.passages[0].text==s);CHECK(r.passages[0].start==0);CHECK(r.passages[0].end==s.size());CHECK(r.passages[0].sentences.size()==2);CHECK(r.metrics.grammar_accepted==2);
 CHECK(a.analyze_page("the cat sleeps. qqq. the cat wakes.","x").passages.empty());CHECK(a.analyze_page("the cat sleeps the cat wakes.","x").passages.empty());CHECK(a.analyze_page("the cat sleeps. the cat wakes", "x").passages.empty());CHECK(a.analyze_page("", "x").passages.empty());
 auto good=PassageDiscoveryEngine{}.score(s),odd=PassageDiscoveryEngine{}.score("the cat sleeps. the economy is purple.");CHECK(good.coherence_score>odd.coherence_score);CHECK(good.classification!="SEMANTICALLY_VALIDATED_PASSAGE");
 CHECK(good.classification=="COHERENT_PASSAGE");CHECK(odd.classification=="GRAMMATICAL_PASSAGE");
 CHECK(std::abs(good.lexical_continuity-1.0/3.0)<1e-12);CHECK(good.entity_repetition==1);CHECK(good.topic_consistency==1);CHECK(good.transition_score==1);CHECK(good.pronoun_reference==.5);
 // Topic overlap is independent of shared words/entities, and is not sufficient for coherence.
 auto same_topic=PassageDiscoveryEngine{}.score("the cat sleeps. the dog wakes.");CHECK(same_topic.lexical_continuity==0);CHECK(same_topic.entity_repetition==0);CHECK(same_topic.topic_consistency==1);CHECK(same_topic.classification=="GRAMMATICAL_PASSAGE");
 auto pronoun_ok=PassageDiscoveryEngine{}.score("the man sleeps. he wakes."),pronoun_bad=PassageDiscoveryEngine{}.score("the man sleeps. she wakes.");CHECK(pronoun_ok.pronoun_reference==1);CHECK(pronoun_bad.pronoun_reference==0);CHECK(pronoun_ok.coherence_score>pronoun_bad.coherence_score);
 auto no_antecedent=PassageDiscoveryEngine{}.score("he sleeps. she wakes.");CHECK(no_antecedent.pronoun_reference==0);
 auto pronoun_number=PassageDiscoveryEngine{}.score("the cats sleep. they wake.");CHECK(pronoun_number.pronoun_reference==1);
 auto wrong_number=PassageDiscoveryEngine{}.score("the cat sleeps. they wake.");CHECK(wrong_number.pronoun_reference==0);
 auto ambiguous=PassageDiscoveryEngine{}.score("the dog sees a cat. it sleeps.");CHECK(ambiguous.pronoun_reference==.5);
 for(const std::string bad:{"", "   ", ".", "the cat sleeps.", "the cat sleeps. the cat wakes", "the cat sleeps.the cat wakes.", "the cat sleeps.. the cat wakes.", "the cat sleeps. the cat wakes. broken", "the cat sleeps. . the cat wakes.", "the cat 123 sleeps. the cat wakes.", "the cat sleeps! the cat wakes.", "the cat sleeps. the cat @ wakes."}){
  auto rejected=PassageDiscoveryEngine{}.score(bad);CHECK(rejected.classification=="MULTI_SENTENCE_CANDIDATE");CHECK(rejected.text==bad);CHECK(rejected.start==0);CHECK(rejected.end==bad.size());
 }
 const std::string padded="  the cat sleeps .\n the cat wakes. \t";auto padded_score=PassageDiscoveryEngine{}.score(padded);CHECK(padded_score.classification=="COHERENT_PASSAGE");CHECK(padded_score.text==padded);CHECK(padded_score.sentences[0].start==2);CHECK(padded_score.sentences[0].text=="the cat sleeps");
 CHECK(a.analyze_page("the cat sleeps.the cat wakes.","x").passages.empty());
 CHECK(a.analyze_page("the cat sleeps .\n the cat wakes.","x").passages.size()==1);
 auto direct=r.passages[0].sentences;PassageDiscoveryEngine engine;
 auto throws=[&](const std::vector<Finding>& findings){try{(void)engine.discover(s,"test",findings);}catch(const std::invalid_argument&){return true;}return false;};
 auto malformed=direct;malformed[0].end=std::numeric_limits<std::size_t>::max();CHECK(throws(malformed));
 malformed=direct;malformed[1].start=0;CHECK(throws(malformed));
 malformed=direct;std::swap(malformed[0],malformed[1]);CHECK(throws(malformed));
 malformed=direct;malformed[0].text="fabricated";CHECK(throws(malformed));
 malformed=direct;malformed[0].page_address="unrelated-page";CHECK(throws(malformed));
 CHECK(engine.discover(s,"test",direct).size()==1);
 auto small=PassageOptions{};small.max_chars=s.size()-1;CHECK(PassageDiscoveryEngine(small).score(s).classification=="MULTI_SENTENCE_CANDIDATE");CHECK(PassageDiscoveryEngine(small).score(s).text==s);
 auto nan=PassageOptions{};nan.coherence_threshold=std::numeric_limits<double>::quiet_NaN();bool invalid_option=false;try{PassageDiscoveryEngine invalid(nan);}catch(const std::runtime_error&){invalid_option=true;}CHECK(invalid_option);
 Grammar g;for(auto text:{"the dog sees a cat","the cat does not sleep","the cat slept","birds fly","the cat sleeps and wakes"})CHECK(g.evaluate(NGramLanguageModel::tokenize(text)).accepted);for(auto text:{"the dog see a cat","she are happy","the cat is","the cat sees","birds flies","the cat does sleeps"})CHECK(!g.evaluate(NGramLanguageModel::tokenize(text)).accepted);
 InjectionGenerator inj(7,256,s);auto p=inj.generate(0);CHECK(p.source_mode=="injection");auto injected=a.analyze_page(p.text,p.address);CHECK(injected.passages.size()==1);CHECK(injected.passages[0].start==*p.injection_start);CHECK(injected.passages[0].end==*p.injection_end);CHECK(inj.generate(0).text==p.text);
 Analyzer dedup({"cat","cat"});CHECK(dedup.analyze("cat","x").size()==1);std::cout<<"passage/grammar/modes passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
