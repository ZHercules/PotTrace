#include "BridgeSupport.h"
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 using namespace pottrace;
 check(quote("a\"b\\c\n")=="\"a\\\"b\\\\c\\n\"","JSON escaping");
 Queue q;std::string original="Chinese path copied";check(q.push(original),"enqueue");original="changed";
 check(q.pop()=="Chinese path copied","synchronous copy");
 for(int i=0;i<4096;i++)check(q.push("x"),"queue limit");check(!q.push("overflow"),"bounded event count");
 Queue bytes;check(!bytes.push(std::string(8*1024*1024+1,'x')),"bounded bytes");
 check(classify(250,200,1).kind=="Snapshot","frame quantization");
 check(classify(250,500,1).kind=="RateChanged","speed change");
 check(classify(250,10000,1).kind=="Seeked","forward seek");
 check(classify(250,-5000,1).kind=="Seeked","loop seek");
 check(classify(10000,100,1).gap,"sleep gap");
 std::cout<<"All bridge behaviors passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
