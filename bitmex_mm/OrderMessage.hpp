#include <string>
#include <atomic>


struct OrderMessage
{
	std::string single_order_msg;
	
	OrderMessage()
	{
		reset_single_msg();
	}
	
	inline void reset_single_msg()
	{
		single_order_msg = "";
	}
};


struct NewOrderMessage : OrderMessage
{
	inline void build_new_bid_order(std::atomic<uint64_t>& clOrdID, std::atomic<double>& target_price, std::atomic<uint64_t>& target_vol)
	{
		reset_single_msg();
		
		single_order_msg += "{\"symbol\":\"XBTUSD\",\"ordType\":\"Limit\",\"execInst\":\"ParticipateDoNotInitiate\",\"clOrdID\":\"" + std::to_string(clOrdID);
		single_order_msg += "\",\"side\":\"Buy";
		single_order_msg += "\",\"price\":"  + std::to_string(target_price);
		single_order_msg += ",\"orderQty\":" + std::to_string(target_vol) + "}";
	}
	
	inline void build_new_ask_order(std::atomic<uint64_t>& clOrdID, std::atomic<double>& target_price, std::atomic<uint64_t>& target_vol)
	{
		reset_single_msg();
		
		single_order_msg += "{\"symbol\":\"XBTUSD\",\"ordType\":\"Limit\",\"execInst\":\"ParticipateDoNotInitiate\",\"clOrdID\":\"" + std::to_string(clOrdID);
		single_order_msg += "\",\"side\":\"Sell";
		single_order_msg += "\",\"price\":"  + std::to_string(target_price);
		single_order_msg += ",\"orderQty\":" + std::to_string(target_vol) + "}";
	}
};


struct UpdateOrderMessage : OrderMessage
{	
	inline void build_update_order(std::atomic<uint64_t>& clOrdID, std::atomic<double>& target_price, std::atomic<uint64_t>& target_vol)
	{
		reset_single_msg();

		single_order_msg += "{\"origClOrdID\":\"" + std::to_string(clOrdID);
		single_order_msg += "\",\"price\":"       + std::to_string(target_price);
		single_order_msg += ",\"orderQty\":"      + std::to_string(target_vol) + "}";
	}
	
	inline void build_update_price_order(std::atomic<uint64_t>& clOrdID, std::atomic<double>& target_price)
	{
		reset_single_msg();

		single_order_msg += "{\"origClOrdID\":\"" + std::to_string(clOrdID);
		single_order_msg += "\",\"price\":"       + std::to_string(target_price) + "}";
	}
	
	inline void build_update_vol_order(std::atomic<uint64_t>& clOrdID, std::atomic<uint64_t>& target_vol)
	{
		reset_single_msg();

		single_order_msg += "{\"origClOrdID\":\"" + std::to_string(clOrdID);
		single_order_msg += "\",\"orderQty\":"    + std::to_string(target_vol) + "}";
	}
};