#include <unordered_map>
#include <iostream>
#include <memory>
#include <thread>

#include "../simdjson.h"

#include "WebSocketSecure.hpp"
#include "TradeLogic.hpp"
#include "Semaphore.hpp"
#include "Auth.hpp"

using namespace simdjson;


class OrderUpdateStream : public Auth
{
	Semaphore                tm;
	std::mutex&              q_mutex;
	std::condition_variable& q_cv;
	bool&                    can_mod;
	
	std::deque<std::string>& new_order_q;
	std::deque<std::string>& upd_order_q;
	
	WSS ws{"/realtime"};
	
	TradeVariables tv;
	
	NewOrderMessage    New_Order_Message;
	UpdateOrderMessage Upd_Order_Message;
	
	dom::parser json_parser;
	dom::array  msg_data;
	
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
		
		if (leavesQty == 0) { tv.bid_open = false; }
		
		if (tv.ask_posn_open)
		{
			Upd_Order_Message.build_update_vol_order(tv.ask_posn_clOrdID, tv.targ_ask_posn_vol);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			upd_order_q.push_back(Upd_Order_Message.single_order_msg);
			tm._unlock(q_mutex, q_cv, can_mod);
		}
		else
		{
			tv.ask_posn_clOrdID = ++tv.clOrdID_count;
			
			New_Order_Message.build_new_ask_order(tv.ask_posn_clOrdID, tv.targ_posn_ask, tv.targ_ask_posn_vol);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			new_order_q.push_back(New_Order_Message.single_order_msg);
			tv.curr_posn_ask = tv.targ_posn_ask.load();
			tv.ask_posn_open = true;
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}
	
	inline void on_ask_fill(const dom::object& d)
	{
		uint64_t leavesQty   = d["leavesQty"];
		tv.targ_bid_posn_vol = d["cumQty"];
		
		if (leavesQty == 0) { tv.ask_open = false; }
		
		if (tv.bid_posn_open)
		{
			Upd_Order_Message.build_update_vol_order(tv.bid_posn_clOrdID, tv.targ_bid_posn_vol);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			upd_order_q.push_back(Upd_Order_Message.single_order_msg);
			tm._unlock(q_mutex, q_cv, can_mod);
		}
		else
		{
			tv.bid_posn_clOrdID = ++tv.clOrdID_count;
			
			New_Order_Message.build_new_bid_order(tv.bid_posn_clOrdID, tv.targ_posn_bid, tv.targ_bid_posn_vol);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			new_order_q.push_back(New_Order_Message.single_order_msg);
			tv.curr_posn_bid = tv.targ_posn_bid.load();
			tv.bid_posn_open = true;
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}
	
	inline void on_bid_posn_fill(const dom::object& d)
	{
		uint64_t leavesQty = d["leavesQty"];
		tv.targ_ask_vol    = d["cumQty"];
		
		if (leavesQty == 0) { tv.bid_posn_open = false; }
		
		if (tv.ask_open)
		{
			Upd_Order_Message.build_update_vol_order(tv.ask_clOrdID, tv.targ_ask_vol);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			upd_order_q.push_back(Upd_Order_Message.single_order_msg);
			tm._unlock(q_mutex, q_cv, can_mod);
		}
		else
		{
			tv.ask_clOrdID = ++tv.clOrdID_count;
			
			New_Order_Message.build_new_ask_order(tv.ask_clOrdID, tv.targ_ask, tv.targ_ask_vol);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			new_order_q.push_back(New_Order_Message.single_order_msg);
			tv.curr_ask = tv.targ_ask.load();
			tv.ask_open = true;
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}
	
	inline void on_ask_posn_fill(const dom::object& d)
	{
		uint64_t leavesQty = d["leavesQty"];
		tv.targ_bid_vol    = d["cumQty"];
		
		if (leavesQty == 0) { tv.ask_posn_open = false; }
		
		if (tv.bid_open)
		{
			Upd_Order_Message.build_update_vol_order(tv.bid_clOrdID, tv.targ_bid_vol);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			upd_order_q.push_back(Upd_Order_Message.single_order_msg);
			tm._unlock(q_mutex, q_cv, can_mod);
		}
		else
		{
			tv.bid_clOrdID = ++tv.clOrdID_count;
			
			New_Order_Message.build_new_bid_order(tv.bid_clOrdID, tv.targ_bid, tv.targ_bid_vol);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			new_order_q.push_back(New_Order_Message.single_order_msg);
			tv.curr_bid = tv.targ_bid.load();
			tv.bid_open = true;
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}
	
	inline void on_bid_cancelled()
	{
		tv.bid_open = false;
		
		tv.bid_clOrdID = ++tv.clOrdID_count;
			
		New_Order_Message.build_new_bid_order(tv.bid_clOrdID, tv.targ_bid, tv.targ_bid_vol);
		tv.curr_bid = tv.targ_bid.load();
		
		tm._get_lock(q_mutex, q_cv, can_mod);
		new_order_q.push_back(New_Order_Message.single_order_msg);
		tv.curr_bid = tv.targ_bid.load();
		tv.bid_open = true;
		tm._unlock(q_mutex, q_cv, can_mod);
	}
	
	inline void on_ask_cancelled()
	{
		tv.ask_open = false;
		
		tv.ask_clOrdID = ++tv.clOrdID_count;
			
		New_Order_Message.build_new_ask_order(tv.ask_clOrdID, tv.targ_ask, tv.targ_ask_vol);
		
		tm._get_lock(q_mutex, q_cv, can_mod);
		new_order_q.push_back(New_Order_Message.single_order_msg);
		tv.curr_ask = tv.targ_ask.load();
		tv.ask_open = true;
		tm._unlock(q_mutex, q_cv, can_mod);
	}
	
	inline void on_bid_posn_cancelled()
	{
		tv.bid_posn_open = false;
		
		tv.bid_posn_clOrdID = ++tv.clOrdID_count;
			
		New_Order_Message.build_new_bid_order(tv.bid_posn_clOrdID, tv.targ_posn_bid, tv.targ_bid_posn_vol);
		
		tm._get_lock(q_mutex, q_cv, can_mod);
		new_order_q.push_back(New_Order_Message.single_order_msg);
		tv.curr_posn_bid = tv.targ_posn_bid.load();
		tv.bid_posn_open = true;
		tm._unlock(q_mutex, q_cv, can_mod);
	}
	
	inline void on_ask_posn_cancelled()
	{
		tv.ask_posn_open = false;
		
		tv.ask_posn_clOrdID = ++tv.clOrdID_count;
			
		New_Order_Message.build_new_ask_order(tv.ask_posn_clOrdID, tv.targ_posn_ask, tv.targ_ask_posn_vol);
		
		tm._get_lock(q_mutex, q_cv, can_mod);
		new_order_q.push_back(New_Order_Message.single_order_msg);
		tv.curr_posn_ask = tv.targ_posn_ask.load();
		tv.ask_posn_open = true;
		tm._unlock(q_mutex, q_cv, can_mod);
	}
	
	
public:
	explicit
	OrderUpdateStream(std::deque<std::string>& noq,
			  		  std::deque<std::string>& uoq,
			  		  std::mutex&              qm,
			  		  std::condition_variable& qcv,
			  		  bool&                    cm)
					  :
					  new_order_q(noq), upd_order_q(uoq),
			  		  q_mutex(qm), q_cv(qcv), can_mod(cm)
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
					else if (ordStatus == "Canceled" || ordStatus == "Rejected")
					{					
						if      (clOrdID == tv.bid_clOrdID)      { on_bid_cancelled();      }
						else if (clOrdID == tv.ask_clOrdID)      { on_ask_cancelled();      }
						else if (clOrdID == tv.bid_posn_clOrdID) { on_bid_posn_cancelled(); }
						else if (clOrdID == tv.ask_posn_clOrdID) { on_ask_posn_cancelled(); }
					}
				} catch (const simdjson_error &e) {}
			}
		}
	}
};