#include "play-policy.h"
#include <cassert>
#include <iostream>
int main(){
    auto first=play::parse("depth 19 multipv 1 score cp 100 nodes 40 pv e2e4 e7e5");
    auto second=play::parse("depth 19 multipv 2 score cp 90 pv d2d4");
    auto third=play::parse("depth 19 multipv 3 score cp 80 pv g1f3");
    assert(first.move=="e2e4"&&first.cp==100&&first.depth==19);
    std::vector<play::Candidate> lines{first,second,third};std::mt19937 random(73);int counts[3]{};
    for(int i=0;i<10000;i++)counts[play::choose(lines,32,random)]++;
    assert(counts[0]>counts[1]&&counts[1]>counts[2]&&counts[2]>0);
    lines[2].cp=-100;assert(play::eligible(lines,32).size()==2);
    lines[1].cp=70;assert(play::eligible(lines,8).size()==1);
    auto mate=play::parse("depth 20 score mate 3 pv h5f7");
    assert(mate.mating&&mate.mate==3);assert(play::eligible({mate,first,third},15).size()==1);
    lines={first,second,third};
    for(int i=0;i<1000;i++){
        int complex=play::delay(lines,30,32,200,1500,random);assert(complex>=3000&&complex<=9500);
        int forced=play::delay(lines,30,1,200,1500,random);assert(forced<=complex);
        int lowClock=play::delay(lines,30,32,9,1500,random);assert(lowClock<=500);
    }
    assert(play::retryAllowed(0,true,true,true));assert(play::retryAllowed(2,true,true,true));
    assert(!play::retryAllowed(3,true,true,true));assert(!play::retryAllowed(1,false,true,true));
    assert(!play::retryAllowed(1,true,false,true));assert(!play::retryAllowed(1,true,true,false));
    std::cout<<"Policy verified: weighted alternatives, endgame/mate safeguards, bounded timing, three guarded attempts.\n";
}
