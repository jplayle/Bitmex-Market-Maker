#pragma once

#include <atomic>
#include <memory>

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
	
    static std::atomic<uint64_t> bid_cumQty;
    static std::atomic<uint64_t> ask_cumQty;
    static std::atomic<uint64_t> bid_posn_cumQty;
    static std::atomic<uint64_t> ask_posn_cumQty;
    
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
	std::mutex&              write_mutex;
	std::condition_variable& write_cv;
	bool&                    can_write;
	
	TradeVariables tv;
	
	NewOrderMessage    New_Order_Msg;
	UpdateOrderMessage Upd_Order_Msg;
	
	const double offr_offset = 100.0;
	
	bool is_upd = false;
	
	
public:
	double vol_ratio;
	
	TradeLogic(std::mutex& wm, std::condition_variable& wcv, bool& cw) : write_mutex(wm), write_cv(wcv), can_write(cw)
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
		
		tv.curr_bid = tv.targ_bid.load();
	}
	
	inline void init_ask_order()
	{
		tv.targ_ask_vol = get_order_vol(tv.targ_posn_bid, tv.bid_xbt);
		
		tv.clOrdID_count++;
		tv.ask_clOrdID = tv.clOrdID_count.load();
		
		New_Order_Msg.build_new_ask_order(tv.ask_clOrdID, tv.targ_ask, tv.targ_ask_vol);
	
		tv.curr_ask = tv.targ_ask.load();
	}
	
	inline bool check_bid_upd()
	{	// bid offer
		if ((tv.targ_bid - tv.curr_bid) * tv.bid_clOrdID)
		{
			tm._write_lock(write_mutex, write_cv, can_write);
			
			Upd_Order_Msg.build_update_price_order(tv.bid_clOrdID, tv.targ_bid);
			tv.curr_bid = tv.targ_bid.load();
			return true;
		}
		return false;
	}

	inline bool check_ask_upd()
	{	// ask offer
		if ((tv.targ_ask - tv.curr_ask) * tv.ask_clOrdID)
		{
			tm._write_lock(write_mutex, write_cv, can_write);
			
			Upd_Order_Msg.build_update_price_order(tv.ask_clOrdID, tv.targ_ask);
			tv.curr_ask = tv.targ_ask.load();
			return true;
		}
		return false;
	}

	inline bool check_bid_posn_upd()
	{	// bid position
		if ((tv.targ_posn_bid - tv.curr_posn_bid) * tv.bid_posn_clOrdID)
		{
			tm._write_lock(write_mutex, write_cv, can_write);
			
			Upd_Order_Msg.build_update_price_order(tv.bid_posn_clOrdID, tv.targ_posn_bid);
			tv.curr_posn_bid = tv.targ_posn_bid.load();
			return true;
		}
		return false;
	}

	inline bool check_ask_posn_upd()
	{	// ask position
		if ((tv.targ_posn_ask - tv.curr_posn_ask) * tv.ask_posn_clOrdID)
		{
			tm._write_lock(write_mutex, write_cv, can_write);
			
			Upd_Order_Msg.build_update_price_order(tv.ask_posn_clOrdID, tv.targ_posn_ask);
			tv.curr_posn_ask = tv.targ_posn_ask.load();
			return true;
		}
		return false;
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

std::atomic<uint64_t> TradeVariables::bid_cumQty      = 0;
std::atomic<uint64_t> TradeVariables::ask_cumQty      = 0;
std::atomic<uint64_t> TradeVariables::bid_posn_cumQty = 0;
std::atomic<uint64_t> TradeVariables::ask_posn_cumQty = 0;

std::atomic<uint64_t> TradeVariables::clOrdID_count    = 0;
std::atomic<uint64_t> TradeVariables::bid_clOrdID      = 0;
std::atomic<uint64_t> TradeVariables::ask_clOrdID      = 0;
std::atomic<uint64_t> TradeVariables::bid_posn_clOrdID = 0;
std::atomic<uint64_t> TradeVariables::ask_posn_clOrdID = 0;