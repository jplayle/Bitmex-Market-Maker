#include <boost/asio.hpp>

#include "OrderBookStream.hpp"
#include "OrderUpdateStream.hpp"


class BitMEXMarketMaker
{
	std::mutex              write_mutex;
	std::condition_variable write_cv;
	bool                    can_write = true;
	
	
public:
	void run()
	{
		std::cout << "BitMEX Market Maker." << '\n';
		
		// init REST thread objects
		net::io_context rest_ioc;
    	ssl::context    rest_ctx{ssl::context::tlsv12_client};
		
		auto rest_ptr = std::make_shared<REST>(write_mutex, write_cv, can_write, boost::asio::make_strand(rest_ioc), rest_ctx);
		
		// start order stream
		OrderUpdateStream ous(write_mutex, write_cv, can_write, rest_ptr);
		std::thread ous_thread([&]{ ous.run(); });
		
		// init order book and start rest thread
		OrderBookStream obs(write_mutex, write_cv, can_write, rest_ptr);
		std::thread rest_thread([&]{ rest_ioc.run(); });
		
		// stream order book updates and make trading decisions
		obs.run();
	}
};