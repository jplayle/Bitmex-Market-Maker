#include <deque>

#include <boost/asio.hpp>

#include "OrderBookStream.hpp"
#include "OrderUpdateStream.hpp"
#include "REST_Sync.hpp"


class BitMEXMarketMaker
{
	std::mutex              q_mutex;
	std::condition_variable q_cv;
	bool                    can_mod = true;
	
	std::deque<std::string> new_order_q;
	std::deque<std::string> upd_order_q;
	
	
public:
	void run()
	{
		std::cout << "BitMEX Market Maker." << '\n';
		
		// start REST worker thread
		REST_Sync rest_service(new_order_q, upd_order_q, q_mutex, q_cv, can_mod);
		std::thread rest_thread([&]{ rest_service.run(); });
		
		// start order management stream
		OrderUpdateStream ous(new_order_q, upd_order_q, q_mutex, q_cv, can_mod);
		std::thread ous_thread([&]{ ous.run(); });
		
		// start order book stream
		OrderBookStream obs(new_order_q, upd_order_q, q_mutex, q_cv, can_mod);
		obs.run();
	}
};