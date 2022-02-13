#pragma once

#include <atomic>
#include <memory>
#include <deque>

#include "BitwiseOperations.hpp"
#include "OrderMessage.hpp"
#include "Semaphore.hpp"


struct TradeVariables {
	// assume any order sent to the exchange succeeds and target price/vol = current price/vol
	// else amend the curr_* variables to reflect the fact that the order failed
	// therefore curr_* variables have write operations by more than one thread (OrderBookStream and OrderUpdateStream)
	// everything else is read by one thread and written by another
	
	static std::atomic<double> xbt_total;
    static std::atomic<double> bid_xbt;
    static std::atomic<double> ask_xbt;
	
	static std::atomic<double> targ_bid;
	static std::atomic<double> targ_ask;
	static std::atomic<double> curr_bid;
    static std::atomic<double> curr_ask;
	
	static std::atomic<double> targ_posn_bid;
	static std::atomic<double> targ_posn_ask;
	static std::atomic<double> curr_posn_bid;
    static std::atomic<double> curr_posn_ask;

	static std::atomic<uint64_t> net_posn_vol;
	
	static std::atomic<uint64_t> targ_bid_vol;
	static std::atomic<uint64_t> targ_ask_vol;
	static std::atomic<uint64_t> curr_bid_vol;
	static std::atomic<uint64_t> curr_ask_vol;
	
	static std::atomic<uint64_t> targ_bid_posn_vol;
	static std::atomic<uint64_t> targ_ask_posn_vol;
	static std::atomic<uint64_t> curr_bid_posn_vol;
	static std::atomic<uint64_t> curr_ask_posn_vol;
	
    static std::atomic<bool> bid_open;
	static std::atomic<bool> ask_open;
	static std::atomic<bool> bid_posn_open;
	static std::atomic<bool> ask_posn_open;
    
	static std::atomic<uint64_t> clOrdID_count;
	static std::atomic<uint64_t> bid_clOrdID;
    static std::atomic<uint64_t> ask_clOrdID;
    static std::atomic<uint64_t> bid_posn_clOrdID;
    static std::atomic<uint64_t> ask_posn_clOrdID;
};


class TradeLogic : public Bitwise
{	
protected:
	Semaphore tm;
	std::mutex&              q_mutex;
	std::condition_variable& q_cv;
	bool&                    can_mod;
	
	std::deque<std::string>& new_order_q;
	std::deque<std::string>& upd_order_q;
	
	TradeVariables tv;
	
	NewOrderMessage    New_Order_Msg;
	UpdateOrderMessage Upd_Order_Msg;
	
	const double offr_offset = 100.0;
	
	bool is_upd = false;
	
	
public:
	double vol_ratio;
	
	TradeLogic(std::deque<std::string>& noq,
			   std::deque<std::string>& uoq,
			   std::mutex&              qm,
			   std::condition_variable& qcv,
			   bool&                    cm)
			   :
			   new_order_q(noq), upd_order_q(uoq),
			   q_mutex(qm), q_cv(qcv), can_mod(cm)
			   {}
	
	inline void update_targ_prices(double& vol_ratio, double& best_bid, double& best_ask)
	{
		double spread = best_ask - best_bid;
		
		tv.targ_posn_bid = best_bid;
		tv.targ_posn_ask = best_ask;
		
		if (spread > 0.5)
		{
			tv.targ_posn_bid = best_bid + 0.5;
			spread = best_ask - tv.targ_posn_bid;
			if (spread > 0.5)
			{
				tv.targ_posn_ask = best_ask - 0.5;
			}
		}
		
		tv.targ_ask = tv.targ_posn_ask + (offr_offset * !bitwise_non_zero((int)(1 / vol_ratio)));
		tv.targ_bid = tv.targ_posn_bid - (offr_offset * !bitwise_non_zero((int)vol_ratio));
	}
	
	inline uint64_t get_order_vol(double price, double xbt_vol)
	{
		//bitmex order volumes must be multiples of 100
		int raw_vol = (int)(price * xbt_vol);
		return ((uint64_t)(raw_vol / 100)) * 100;
	}
	
	inline void init_bid_order()
	{
		tv.targ_bid_vol = get_order_vol(tv.targ_posn_bid, tv.bid_xbt);
		
		tv.clOrdID_count++;
		tv.bid_clOrdID = tv.clOrdID_count.load();
		
		New_Order_Msg.build_new_bid_order(tv.bid_clOrdID, tv.targ_bid, tv.targ_bid_vol);
		
		tm._get_lock(q_mutex, q_cv, can_mod);
		
		new_order_q.push_back(New_Order_Msg.single_order_msg);
		
		tv.curr_bid = tv.targ_bid.load();
		tv.bid_open = true;
		
		tm._unlock(q_mutex, q_cv, can_mod);
	}
	
	inline void init_ask_order()
	{
		tv.targ_ask_vol = get_order_vol(tv.targ_posn_bid, tv.bid_xbt);
		
		tv.clOrdID_count++;
		tv.ask_clOrdID = tv.clOrdID_count.load();
		
		New_Order_Msg.build_new_ask_order(tv.ask_clOrdID, tv.targ_ask, tv.targ_ask_vol);
		
		tm._get_lock(q_mutex, q_cv, can_mod);
		std::cout << "ask put" << '\n';
		new_order_q.push_back(New_Order_Msg.single_order_msg);
	
		tv.curr_ask = tv.targ_ask.load();
		tv.ask_open = true;
		
		tm._unlock(q_mutex, q_cv, can_mod);
	}
	
	inline void check_bid_upd()
	{	// bid offer
		if ((tv.targ_bid - tv.curr_bid) * tv.bid_open)
		{			
			Upd_Order_Msg.build_update_price_order(tv.bid_clOrdID, tv.targ_bid);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			
			upd_order_q.push_back(Upd_Order_Msg.single_order_msg);
			tv.curr_bid = tv.targ_bid.load();
			
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}

	inline void check_ask_upd()
	{	// ask offer
		if ((tv.targ_ask - tv.curr_ask) * tv.ask_open)
		{			
			Upd_Order_Msg.build_update_price_order(tv.ask_clOrdID, tv.targ_ask);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			
			upd_order_q.push_back(Upd_Order_Msg.single_order_msg);
			tv.curr_ask = tv.targ_ask.load();
			
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}

	inline void check_bid_posn_upd()
	{	// bid position
		if ((tv.targ_posn_bid - tv.curr_posn_bid) * tv.bid_posn_open)
		{			
			Upd_Order_Msg.build_update_price_order(tv.bid_posn_clOrdID, tv.targ_posn_bid);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			
			upd_order_q.push_back(Upd_Order_Msg.single_order_msg);
			tv.curr_posn_bid = tv.targ_posn_bid.load();
			
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}

	inline void check_ask_posn_upd()
	{	// ask position
		if ((tv.targ_posn_ask - tv.curr_posn_ask) * tv.ask_posn_open)
		{			
			Upd_Order_Msg.build_update_price_order(tv.ask_posn_clOrdID, tv.targ_posn_ask);
			
			tm._get_lock(q_mutex, q_cv, can_mod);
			
			upd_order_q.push_back(Upd_Order_Msg.single_order_msg);
			tv.curr_posn_ask = tv.targ_posn_ask.load();
			
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}
};


std::atomic<double> TradeVariables::xbt_total = 0.013;
std::atomic<double> TradeVariables::bid_xbt = TradeVariables::xbt_total / 2.0;
std::atomic<double> TradeVariables::ask_xbt = TradeVariables::xbt_total / 2.0;

std::atomic<double> TradeVariables::targ_bid = 0.0;
std::atomic<double> TradeVariables::targ_ask = 0.0;
std::atomic<double> TradeVariables::curr_bid = 0.0;
std::atomic<double> TradeVariables::curr_ask = 0.0;

std::atomic<double> TradeVariables::targ_posn_bid = 0.0;
std::atomic<double> TradeVariables::targ_posn_ask = 0.0;
std::atomic<double> TradeVariables::curr_posn_bid = 0.0;
std::atomic<double> TradeVariables::curr_posn_ask = 0.0;

std::atomic<uint64_t> TradeVariables::net_posn_vol = 0;
std::atomic<uint64_t> TradeVariables::targ_bid_vol = 0;
std::atomic<uint64_t> TradeVariables::targ_ask_vol = 0;
std::atomic<uint64_t> TradeVariables::curr_bid_vol = 0;
std::atomic<uint64_t> TradeVariables::curr_ask_vol = 0;

std::atomic<uint64_t> TradeVariables::targ_bid_posn_vol = 0;
std::atomic<uint64_t> TradeVariables::targ_ask_posn_vol = 0;
std::atomic<uint64_t> TradeVariables::curr_bid_posn_vol = 0;
std::atomic<uint64_t> TradeVariables::curr_ask_posn_vol = 0;

std::atomic<bool> TradeVariables::bid_open      = false;
std::atomic<bool> TradeVariables::ask_open      = false;
std::atomic<bool> TradeVariables::bid_posn_open = false;
std::atomic<bool> TradeVariables::ask_posn_open = false;

std::atomic<uint64_t> TradeVariables::clOrdID_count    = 0;
std::atomic<uint64_t> TradeVariables::bid_clOrdID      = 0;
std::atomic<uint64_t> TradeVariables::ask_clOrdID      = 0;
std::atomic<uint64_t> TradeVariables::bid_posn_clOrdID = 0;
std::atomic<uint64_t> TradeVariables::ask_posn_clOrdID = 0;