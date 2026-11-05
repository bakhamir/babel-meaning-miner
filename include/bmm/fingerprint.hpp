#pragma once
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
namespace bmm {
// Stable integrity fingerprint, not a cryptographic signature.
inline std::string file_fingerprint(const std::string& path) {
  std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot read: "+path);
  std::uint64_t hash=14695981039346656037ULL;char buffer[8192];
  while(f.read(buffer,sizeof(buffer))||f.gcount())for(std::streamsize i=0;i<f.gcount();++i){hash^=static_cast<unsigned char>(buffer[i]);hash*=1099511628211ULL;}
  if(f.bad())throw std::runtime_error("file read failed: "+path);
  std::ostringstream out;out<<"fnv1a64:"<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
}
