#pragma once
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <string>
#include <vector>
namespace play {
struct Candidate {std::string move;int cp=0,mate=0,depth=0;bool mating=false;};
inline Candidate parse(const std::string& info){
    Candidate c;std::istringstream in(info);std::string token;
    while(in>>token){if(token=="depth")in>>c.depth;else if(token=="score"){in>>token;if(token=="mate"){in>>c.mate;c.mating=true;}else if(token=="cp")in>>c.cp;}else if(token=="pv"){in>>c.move;break;}}
    return c;
}
inline std::vector<size_t> eligible(const std::vector<Candidate>& lines,int pieces){
    std::vector<size_t> out;if(lines.empty())return out;out.push_back(0);
    int tolerance=pieces<=10?20:55;if(std::abs(lines[0].cp)>=500)tolerance=15;
    for(size_t i=1;i<std::min<size_t>(3,lines.size());i++){
        if(lines[0].mating){if(lines[i].mating&&lines[0].mate>0&&lines[i].mate>0&&lines[i].mate<=lines[0].mate+2)out.push_back(i);}
        else if(!lines[i].mating&&lines[0].cp-lines[i].cp<=tolerance)out.push_back(i);
    }return out;
}
inline size_t choose(const std::vector<Candidate>& lines,int pieces,std::mt19937& random){
    auto pool=eligible(lines,pieces);if(pool.empty())return 0;std::vector<double> weights;
    for(auto i:pool)weights.push_back(i==0?65.0:i==1?27.0:8.0);
    return pool[std::discrete_distribution<size_t>(weights.begin(),weights.end())(random)];
}
inline int delay(const std::vector<Candidate>& lines,int pieces,int legalCount,double clockSeconds,int computeMs,std::mt19937& random){
    int low=1800,high=5500;
    if(legalCount>0&&legalCount<=2){low=650;high=1600;}
    else if(pieces<=10){low=1100;high=3800;}
    else if(lines.size()>1&&!lines[0].mating&&!lines[1].mating&&std::abs(lines[0].cp-lines[1].cp)<40&&legalCount>=18){low=4500;high=11000;}
    else if(lines.size()>1&&lines[0].cp-lines[1].cp>150){low=900;high=2400;}
    if(clockSeconds>0&&clockSeconds<15){low=400;high=1000;}
    else if(clockSeconds>0&&clockSeconds<60){low=650;high=2100;}
    return std::max(100,std::uniform_int_distribution<int>(low,high)(random)-computeMs);
}
inline bool retryAllowed(int attempts,bool samePosition,bool fresh,bool focused){return attempts<3&&samePosition&&fresh&&focused;}
}
