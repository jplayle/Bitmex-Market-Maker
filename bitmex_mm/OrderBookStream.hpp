#include <unordered_map>
#include <iostream>
#include <memory>
#include <deque>

#include "../simdjson.h"
#include "OrderBook.hpp"
#include "WebSocketSecure.hpp"
#include "TradeLogic.hpp"

using namespace simdjson;

class OrderBookStream : public TradeLogic
{
	WSS ws{"/realtime?subscribe=orderBookL2:XBTUSD"};
	
	BidOrderBook bid_ob;
	AskOrderBook ask_ob;
	std::unordered_map<std::string, OrderBook*> orderBook {
		{"Buy",  &bid_ob},
		{"Sell", &ask_ob},
	};
	
	typedef void (OrderBookStream::*pfunc)(const dom::array& msgs);
	pfunc func_ptr;
	
	std::unordered_map<std::string, pfunc> msg_func_map {
		{"update", &OrderBookStream::do_update},
		{"insert", &OrderBookStream::do_insert},
		{"delete", &OrderBookStream::do_delete},
	};
	
	std::string side;
	uint64_t    vol;
	uint64_t    id;
	
	dom::parser ob_json_parser;
	dom::array  ob_data;
	
	double best_bid;
	double best_ask;
	
	bool is_bba_upd;
	
	
	void get_orderBook_snapshot()
	{
		struct orderBookLevel* level_0 = new orderBookLevel;
		
		auto [snapshot, json_error] = ob_json_parser.parse(ws.read());
		
		for (const auto& ob_level : snapshot["data"])
        {
			side = ob_level["side"];
			
			if (side == "Buy")
            {
                id  = ob_level["id"];
				vol = ob_level["size"];
				
                level_0 = orderBook["Buy"]->ob_snapshot_insert(id, vol, level_0);
            }
		}
		
		struct orderBookLevel* level_01 = new orderBookLevel;
		
		for (const auto& ob_level : snapshot["data"])
        {
			side = ob_level["side"];
			
			if (side == "Sell")
            {
                id  = ob_level["id"];
				vol = ob_level["size"];
				
                level_01 = orderBook["Sell"]->ob_snapshot_insert(id, vol, level_01);
            }
		}
	}
	
	inline void do_update(const dom::array& msgs)
	{
		for (const auto& msg : msgs)
		{
			side = msg["side"];
			id   = msg["id"];
			vol  = msg["size"];
			
			orderBook[side]->ob_update(id, vol);
		}
		on_update();
	}
	
	inline void do_insert(const dom::array& msgs)
	{
		for (const auto& msg : msgs)
		{
			side = msg["side"];
			id   = msg["id"];
			vol  = msg["size"];
			
			is_bba_upd = orderBook[side]->ob_insert(id, vol);
		}
		
		if (is_bba_upd)
		{
			get_bba();
			update_targ_prices(vol_ratio, best_bid, best_ask);
			
			on_insert_or_delete();
		}
	}
	
	inline void do_delete(const dom::array& msgs)
	{
		for (const auto& msg : msgs)
		{
			side = msg["side"];
			id   = msg["id"];

			is_bba_upd = orderBook[side]->ob_delete(id);
		}
		
		if (is_bba_upd)
		{
			get_bba();
			update_targ_prices(vol_ratio, best_bid, best_ask);
			
			on_insert_or_delete();
		}
	}
	
	inline void get_bba()
	{
		// best bid/ask
		best_bid = orderBook["Buy"]->best_price();
		best_ask = orderBook["Sell"]->best_price();
	}
	
	inline double get_vol_ratio()
	{
		return orderBook["Buy"]->best_vol() / orderBook["Sell"]->best_vol();
	}
	
	inline void on_update()
	{
		vol_ratio = get_vol_ratio();
		
		check_bid_upd();
		check_ask_upd();
	}
	
	inline void on_insert_or_delete()
	{
		on_update();

		check_bid_posn_upd();
		best_bid = tv.targ_posn_bid.load();
		
		check_ask_posn_upd();
		best_ask = tv.targ_posn_ask.load();
	}
	
	void init_trading()
	{
		get_bba();
		
		vol_ratio = get_vol_ratio();
		
		update_targ_prices(vol_ratio, best_bid, best_ask);
		
		init_bid_order();
		init_ask_order();
	}
	
public:
	OrderBookStream(std::deque<std::string>& noq,
			  		std::deque<std::string>& uoq,
			  		std::mutex&              qm,
			  		std::condition_variable& qcv,
			  		bool&                    cm)
					:
					TradeLogic(noq, uoq, qm, qcv, cm)
	{}
	
	void run()
	{
		ws.connect();
		
		ws.read(); // welcome message
		ws.read(); // subscription message
		
		get_orderBook_snapshot();
		
		init_trading();
		
		for (;;)
		{			
			auto [ob_data, json_error] = ob_json_parser.parse(ws.read());

			func_ptr = msg_func_map[(std::string)ob_data["action"]];
			
			(this->*func_ptr)(ob_data["data"]);
		}
	}
};