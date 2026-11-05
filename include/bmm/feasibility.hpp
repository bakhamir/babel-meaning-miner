#pragma once
#include <string>
#include <cstddef>
namespace bmm {
std::string feasibility_json(const std::string& dictionary,std::size_t page_size,double pages,unsigned min_sentences=2);
}
