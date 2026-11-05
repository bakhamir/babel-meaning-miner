#pragma once
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
namespace bmm {
inline std::string json_quote(std::string_view value) {
  static constexpr char hex[]="0123456789abcdef";
  std::string out="\"";
  for(unsigned char c:value) {
    if(c=='"'||c=='\\'){out+='\\';out+=c;}
    else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
    else out+=c;
  }
  return out+'"';
}
// Strict flat JSON string map for frozen experiment configurations.
inline std::string config_encode(const std::map<std::string,std::string>& values) {
  std::string out="{"; bool first=true;
  for(const auto& [k,v]:values){if(!first)out+=',';first=false;out+=json_quote(k)+':'+json_quote(v);}
  return out+'}';
}
inline std::map<std::string,std::string> config_decode(std::string_view s) {
  std::size_t i=0;auto ws=[&]{while(i<s.size()&&(s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t'))++i;};
  auto fail=[](){throw std::runtime_error("invalid or legacy experiment configuration; create a new experiment");};
  auto str=[&](){std::string out;ws();if(i>=s.size()||s[i++]!='"')fail();bool ended=false;
    while(i<s.size()){char c=s[i++];if(c=='"'){ended=true;break;}if(c=='\\'){if(i==s.size())fail();c=s[i++];if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c!='"'&&c!='\\'&&c!='/')fail();}out+=c;}if(!ended)fail();return out;};
  ws();if(i==s.size()||s[i++]!='{')fail();std::map<std::string,std::string> out;ws();
  if(i<s.size()&&s[i]=='}'){++i;ws();if(i!=s.size())fail();return out;}
  while(i<s.size()){auto k=str();ws();if(i==s.size()||s[i++]!=':')fail();auto v=str();if(!out.emplace(k,v).second)fail();ws();if(i==s.size())fail();if(s[i]=='}'){++i;ws();if(i!=s.size())fail();return out;}if(s[i++]!=',')fail();}
  fail();return out;
}
}
