// g++ -std=c++17 -pthread -O3 -o BMM.out main.cpp ../simdjson.cpp -lssl -lcrypto && ./BMM.out

#include "BitMEXMarketMaker.hpp"


int main(int argc, char** argv)
{
	
	BitMEXMarketMaker bmm;
	
	bmm.run();
    
    return 0;
	
}
