#include "bmm/generator.hpp"
#include <stdexcept>
#include <array>
#include <algorithm>
namespace bmm {
namespace { std::uint64_t splitmix64(std::uint64_t& x) { auto z=(x+=0x9e3779b97f4a7c15ULL); z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL; z=(z^(z>>27))*0x94d049bb133111ebULL; return z^(z>>31); } }
LocalGenerator::LocalGenerator(std::uint64_t seed, std::size_t size, std::string alphabet)
  : seed_(seed), page_size_(size), alphabet_(std::move(alphabet)) {
  if (alphabet_.empty() || page_size_ == 0) throw std::invalid_argument("page size and alphabet must be non-empty");
}
Page LocalGenerator::generate(std::uint64_t index) const {
  std::uint64_t state = seed_ ^ (index * 0xd6e8feb86659fd93ULL);
  Page p{"local:" + std::to_string(seed_) + ":" + std::to_string(index), {}, index};
  p.generator_version=version();
  p.text.resize(page_size_);
  for (auto& c : p.text) c = alphabet_[splitmix64(state) % alphabet_.size()];
  return p;
}
InjectionGenerator::InjectionGenerator(std::uint64_t seed,std::size_t size,std::string fixture,std::string alphabet)
 : base_(seed,size,std::move(alphabet)),fixture_(std::move(fixture)),seed_(seed) {
  if(fixture_.empty()||size<fixture_.size()+4)throw std::invalid_argument("injection fixture must fit with two boundary spaces");
  if(fixture_.find_first_not_of("abcdefghijklmnopqrstuvwxyz ,.!?\t\r\n")!=std::string::npos)
    throw std::invalid_argument("injection fixture must use lowercase English text and punctuation");
}
Page InjectionGenerator::generate(std::uint64_t index)const{
  auto p=base_.generate(index); std::uint64_t state=seed_^index;
  auto start=2+static_cast<std::size_t>(splitmix64(state)%(p.text.size()-fixture_.size()-3));
  p.text.replace(start,fixture_.size(),fixture_);p.text[start-2]='.';p.text[start-1]=' ';p.text[start+fixture_.size()]=' ';
  p.address="injection:"+std::to_string(seed_)+":"+std::to_string(index);
  p.source_mode=source_mode();p.generator_version=version();p.injection_start=start;p.injection_end=start+fixture_.size();return p;
}
ConstrainedGenerator::ConstrainedGenerator(std::uint64_t seed,std::size_t size,std::size_t sentences)
 :seed_(seed),size_(size),sentences_(sentences){
  if(sentences<1||sentences>128||size<sentences*24)throw std::invalid_argument("constrained generation requires 1..128 sentences and at least 24 characters per sentence");
}
Page ConstrainedGenerator::generate(std::uint64_t index)const{
  static constexpr std::array<const char*,4> subjects{"cat","dog","bird","man"};
  static constexpr std::array<const char*,3> predicates{"sleeps","walks","runs"};
  std::uint64_t state=seed_^index;const auto subject=subjects[splitmix64(state)%subjects.size()];
  Page p{"constrained:"+std::to_string(seed_)+":"+std::to_string(index),{},index};
  for(std::size_t i=0;i<sentences_;++i){if(i)p.text+=' ';p.text+="the ";p.text+=subject;p.text+=' ';p.text+=predicates[splitmix64(state)%predicates.size()];p.text+='.';}
  p.text.resize(size_,' ');p.source_mode=source_mode();p.generator_version=version();return p;
}
}
