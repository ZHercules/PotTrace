#pragma once
#include <string>
#include <string_view>
#include <deque>
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace pottrace {
inline std::string quote(std::string_view text) {
 std::string result="\"";const char* hex="0123456789abcdef";
 for(unsigned char c:text){switch(c){case '"':result+="\\\"";break;case '\\':result+="\\\\";break;case '\n':result+="\\n";break;case '\r':result+="\\r";break;case '\t':result+="\\t";break;
 default:if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}else result+=static_cast<char>(c);}}
 return result+'"';
}
class Queue {
 std::deque<std::string> items;size_t bytes=0;
public:
 bool push(std::string_view value) {if(items.size()>=4096||bytes+value.size()>8*1024*1024)return false;items.emplace_back(value);bytes+=value.size();return true;}
 std::string pop() {if(items.empty())return {};auto value=std::move(items.front());items.pop_front();bytes-=value.size();return value;}
 size_t size()const{return items.size();}
};
struct Observation {std::string kind="Snapshot";double rate=1;bool gap=false;};
// No verified speed query: cut uncertain changes, then estimate the new slope.
inline Observation classify(int64_t dt,int64_t delta,double rate){
 if(dt>1500||dt<=0)return {"Gap",rate,true};
 if(delta<0)return {"Seeked",rate,true};
 if(delta==0)return {"BufferingStarted",rate,false};
 if(std::abs(delta-dt*rate)>std::max(150.0,dt*rate*.35)){
  double measured=static_cast<double>(delta)/dt;
  if(measured>=.2&&measured<=12)return {"RateChanged",measured,true};
  return {"Seeked",rate,true};
 }
 return {"Snapshot",rate,false};
}
}
