#include "bmm/language_model.hpp"
#include "bmm/feasibility.hpp"
#include "bmm/fingerprint.hpp"
#include "bmm/json.hpp"
#include <filesystem>
#include <iostream>
#include <cmath>
#define CHECK(x) if(!(x))throw std::runtime_error(#x)
int main()try{using namespace bmm;auto m=NGramLanguageModel::train("data/evaluation/positive_train.txt");auto p=std::filesystem::temp_directory_path()/"bmm-v03-model.ngram";m.save(p.string());auto hash=file_fingerprint(p.string());NGramLanguageModel::train("data/evaluation/positive_train.txt").save(p.string());CHECK(hash==file_fingerprint(p.string()));auto n=NGramLanguageModel::load(p.string());auto a=m.score("the dog sees a cat"),b=n.score("the dog sees a cat");CHECK(a.normalized==b.normalized);CHECK(a.predicted_tokens==6);CHECK(std::isfinite(m.score("zzzz qqqq").perplexity));CHECK(m.score("zzzz").unknown_tokens==1);CHECK(a.perplexity>=1);CHECK(!NGramLanguageModel{}.score("the dog sees a cat").trained);auto json=config_encode({{"path","E:\\Some Path\\file"},{"text","quote\""}});CHECK(config_decode(json).at("text")=="quote\"");auto report=feasibility_json("data/words.txt",3200,10000,2);CHECK(report.find("semantically_coherent_probability\":null")!=report.npos);std::cout<<"model/feasibility passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
