#include <unordered_map>
#include <iostream>
#include <memory>
#include <thread>

#include "../simdjson.h"

#include "WebSocketSecure.hpp"
#include "TradeLogic.hpp"
#include "Semaphore.hpp"
#include "Auth.hpp"
#include "REST.hpp"

using namespace simdjson;


class OrderUpdateStream : public Auth
{
	Semaphore                tm;
	std::mutex&              write_mutex;
	std::condition_variable& write_cv;
	bool&                    can_write;
	
	WSS ws{"/realtime"};
	
	TradeVariables tv;
	
	NewOrderMessage    New_Order_Message;
	UpdateOrderMessage Upd_Order_Message;
	
	dom::parser json_parser;
	dom::array  msg_data;
	
	std::shared_ptr<REST> rest_ptr;
	
	typedef void (OrderUpdateStream::*pfunc)(const dom::object& d);
	
	std::string ordStatus;
	std::string clOrdID_str;
	
	
	void authenticate()
	{
		std::string blank    = "";
		std::string url_str  = "GET/realtime";
		std::string t_valid  = std::to_string(time(0) + msg_t_expiry);
		std::string auth_str = HMAC_SHA256_hex(url_str, t_valid, blank);
		std::string api_key  = apiKeyCStr;
		std::string auth_msg = "{\"op\": \"authKeyExpires\", \"args\": [\"" + api_key + "\", " + t_valid + ", \"" + auth_str + "\"]}";
		
		ws.write(auth_msg);
	}
	
	void subscribe()
	{
		std:: string sub_msg = "{\"op\": \"subscribe\", \"args\": [\"order\"]}";
		
		ws.write(sub_msg);
	}
	
	// ORDER MANAGEMENT
	
	inline void on_bid_fill(const dom::object& d)
	{
		uint64_t leavesQty   = d["leavesQty"];
		tv.targ_ask_posn_vol = d["cumQty"];
		
		tm._write_lock(write_mutex, write_cv, can_write);
		
		if (leavesQty == 0) { tv.bid_clOrdID = 0; }
		
		if (tv.ask_posn_clOrdID)
		{
			Upd_Order_Message.build_update_vol_order(tv.ask_posn_clOrdID, tv.targ_ask_posn_vol);
			
			rest_ptr->send_update_order(Upd_Order_Message.single_order_msg);
		}
		else
		{
			tv.ask_posn_clOrdID = ++tv.clOrdID_count;
			
			New_Order_Message.build_new_ask_order(tv.ask_posn_clOrdID, tv.targ_posn_ask, tv.targ_ask_posn_vol);
			tv.curr_posn_ask = tv.targ_posn_ask.load();
			
			rest_ptr->send_new_order(New_Order_Message.single_order_msg);
		}
	}
	
	inline void on_ask_fill(const dom::object& d)
	{
		uint64_t leavesQty   = d["leavesQty"];
		tv.targ_bid_posn_vol = d["cumQty"];
		
		tm._write_lock(write_mutex, write_cv, can_write);
		
		if (leavesQty == 0) { tv.ask_clOrdID = 0; }
		
		if (tv.bid_posn_clOrdID)
		{
			Upd_Order_Message.build_update_vol_order(tv.bid_posn_clOrdID, tv.targ_bid_posn_vol);
			
			rest_ptr->send_update_order(Upd_Order_Message.single_order_msg);
		}
		else
		{
			tv.bid_posn_clOrdID = ++tv.clOrdID_count;
			
			New_Order_Message.build_new_bid_order(tv.bid_posn_clOrdID, tv.targ_posn_bid, tv.targ_bid_posn_vol);
			tv.curr_posn_bid = tv.targ_posn_bid.load();
			
			rest_ptr->send_new_order(New_Order_Message.single_order_msg);
		}
	}
	
	inline void on_bid_posn_fill(const dom::object& d)
	{
		uint64_t leavesQty = d["leavesQty"];
		tv.targ_ask_vol    = d["cumQty"];
		
		tm._write_lock(write_mutex, write_cv, can_write);
		
		if (leavesQty == 0) { tv.bid_posn_clOrdID = 0; }
		
		if (tv.ask_clOrdID)
		{
			Upd_Order_Message.build_update_vol_order(tv.ask_clOrdID, tv.targ_ask_vol);
			
			rest_ptr->send_update_order(Upd_Order_Message.single_order_msg);
		}
		else
		{
			tv.ask_clOrdID = ++tv.clOrdID_count;
			
			New_Order_Message.build_new_ask_order(tv.ask_clOrdID, tv.targ_ask, tv.targ_ask_vol);
			tv.curr_ask = tv.targ_ask.load();
			
			rest_ptr->send_new_order(New_Order_Message.single_order_msg);
		}
	}
	
	inline void on_ask_posn_fill(const dom::object& d)
	{
		uint64_t leavesQty = d["leavesQty"];
		tv.targ_bid_vol    = d["cumQty"];
		
		tm._write_lock(write_mutex, write_cv, can_write);
		
		if (leavesQty == 0) { tv.ask_posn_clOrdID = 0; }
		
		if (tv.bid_clOrdID)
		{
			Upd_Order_Message.build_update_vol_order(tv.bid_clOrdID, tv.targ_bid_vol);
			
			rest_ptr->send_update_order(Upd_Order_Message.single_order_msg);
		}
		else
		{
			tv.bid_clOrdID = ++tv.clOrdID_count;
			
			New_Order_Message.build_new_bid_order(tv.bid_clOrdID, tv.targ_bid, tv.targ_bid_vol);
			tv.curr_bid = tv.targ_bid.load();
			
			rest_ptr->send_new_order(New_Order_Message.single_order_msg);
		}
	}
	
public:
	OrderUpdateStream(std::mutex&            wm,
					std::condition_variable& wcv,
					bool&                    cw,
					std::shared_ptr<REST>&   rp)
					:
					write_mutex(wm), write_cv(wcv), can_write(cw), rest_ptr(rp)
	{}
	
	void run()
	{
		ws.connect();
		ws.read(); // welcome msg
		
		authenticate();
		ws.read(); // auth response
		
		subscribe();
		ws.read(); // subscribe response
		ws.read(); // snapshot
		
		for (;;)
		{
			auto [msg_data, json_error] = json_parser.parse(ws.read());
			
			for (const auto& order_msg : msg_data["data"])
			{
				std::cout << "Websocket order msg: " << order_msg << "\n\n";
				
				try
				{
					clOrdID_str = order_msg["clOrdID"];
					ordStatus   = order_msg["ordStatus"];

					uint64_t clOrdID = std::stoi(clOrdID_str);

					if (ordStatus == "Filled" || ordStatus == "PartiallyFilled")
					{					
						if      (clOrdID == tv.bid_clOrdID)      { on_bid_fill(order_msg);      }
						else if (clOrdID == tv.ask_clOrdID)      { on_ask_fill(order_msg);      }
						else if (clOrdID == tv.bid_posn_clOrdID) { on_bid_posn_fill(order_msg); }
						else if (clOrdID == tv.ask_posn_clOrdID) { on_ask_posn_fill(order_msg); }
					}
				} catch (const simdjson_error &e) {}
			}
		}
	}
};
















