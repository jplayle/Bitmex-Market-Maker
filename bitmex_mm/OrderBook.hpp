#include <cstdint>
#include <string>

struct orderBookLevel 
{
	struct orderBookLevel *child  = 0;
	struct orderBookLevel *parent = 0;
	uint64_t               vol    = 0;
	double                 price  = 0;
	bool                   is_set = false;
};

class OrderBook
{	
protected:
	struct orderBookLevel* level  = 0;
	struct orderBookLevel* child  = 0;
	struct orderBookLevel* parent = 0;
	
	int price_array_len = 2000001;
	
	struct orderBookLevel* orderBook_ = new orderBookLevel[price_array_len];
	
public:	
	OrderBook()
	{
		struct orderBookLevel* blank_level = 0;
		
		double price = 0.0;
		
		for (int i = 0; i < price_array_len; ++i)
		{
			blank_level         = &orderBook_[i];
			blank_level->price  = price;
			blank_level->vol    = 0;
			blank_level->parent = 0;
			blank_level->child  = 0;
			blank_level->is_set = false;

			price += 0.5;
		}
	}
	
	inline int price_index(uint64_t& id)
	{
		return 176000000 - (id / 50);
	}
	
	inline void ob_update(uint64_t& id, uint64_t vol)
	{
		orderBook_[price_index(id)].vol = vol;
	}
	
	virtual orderBookLevel* ob_snapshot_insert(uint64_t& id, uint64_t& vol, orderBookLevel* level_0) { return new orderBookLevel; }
	
	virtual bool ob_insert(uint64_t& id, uint64_t& vol) { return false; }
	virtual bool ob_delete(uint64_t& id)                { return false; }
	
	virtual inline double   best_price() { return 0.0; }
	virtual inline uint64_t best_vol()   { return 0;   }
};


class BidOrderBook : public OrderBook
{
	struct orderBookLevel* head;
	
	public:
	BidOrderBook() : OrderBook() { head = &orderBook_[0]; }
	
	orderBookLevel* ob_snapshot_insert(uint64_t& id, uint64_t& vol, orderBookLevel* level_0)
	{
		level           = &orderBook_[price_index(id)];
		level_0->parent = level;

		level->vol    = vol;
		level->child  = level_0;
		level->is_set = true;
		
		if (level->price > head->price) { head = level; }
		
		return level;
	}
	
	bool ob_insert(uint64_t& id, uint64_t& vol)
	{
		uint64_t pi = price_index(id);
		
		bool bba_upd = false;
		
		level      = &orderBook_[pi];
		level->vol = vol;
		
		if (level->price > head->price)
		{
			level->parent = head;
			head->child   = level;
			head          = level;
			bba_upd       = true;
		}
		else
		{
			++pi;
			child = &orderBook_[pi];

			while (!child->is_set)
			{
				++pi;
				child = &orderBook_[pi];
			}

			parent        = child->parent;
			level->child  = child;
			level->parent = parent;
			child->parent = level;
			parent->child = level;
		}
		level->is_set = true;
		
		return bba_upd;
	}
	
	bool ob_delete(uint64_t& id)
	{
		level  = &orderBook_[price_index(id)];
		parent = level->parent;

		double price = level->price;
		double bbid  = head->price;

		int    upd_bid_sw  = !(bool)(bbid - price); //1 if best bid update, 0 else
		double bbid_p_diff = (bbid - parent->price) * upd_bid_sw;

		head -= (int)(2 * bbid_p_diff);

		child         = level  - ((level  - level->child) * !upd_bid_sw);
		child->parent = parent - ((parent - orderBook_)   *  upd_bid_sw);
		parent->child = child  - ((child  - orderBook_)   *  upd_bid_sw);

		level->child  = 0;
		level->parent = 0;

		level->is_set = false;
		
		return upd_bid_sw;
	}
	
	inline double best_price()
	{
		return head->price;
	}
	
	inline uint64_t best_vol()
	{
		return head->vol;
	}
};


class AskOrderBook : public OrderBook
{
	struct orderBookLevel* head;
	
	public:
	AskOrderBook() : OrderBook() { head = &orderBook_[price_array_len-1];  }
	
	orderBookLevel* ob_snapshot_insert(uint64_t& id, uint64_t& vol, orderBookLevel* level_0)
	{
		level          = &orderBook_[price_index(id)];
		level_0->child = level;

		level->vol    = vol;
		level->parent = level_0;
		level->is_set = true;
		
		if (level->price < head->price) { head = level; }
		
		return level;
	}
	
	bool ob_insert(uint64_t& id, uint64_t& vol)
	{
		uint64_t pi = price_index(id);
		
		bool bba_upd = false;

		level      = &orderBook_[pi];
		level->vol = vol;
		
		if (level->price < head->price)
		{
			level->parent = head;
			head->child   = level;
			head          = level;
			bba_upd       = true;
		}
		else
		{
			--pi;
			child = &orderBook_[pi];

			while (!child->is_set)
			{
				--pi;
				child = &orderBook_[pi];
			}
			parent        = child->parent;
			level->child  = child;
			level->parent = parent;
			child->parent = level;
			parent->child = level;
		}
		level->is_set = true;
		
		return bba_upd;
	}
	
	bool ob_delete(uint64_t& id)
	{
		level  = &orderBook_[price_index(id)];
		parent = level->parent;

		double price = level->price;
		double bask  = head->price;

		int    upd_ask_sw  = !(bool)(bask - price); //1 if best ask update, 0 else
		double bask_p_diff = (parent->price - bask) * upd_ask_sw;

		head += (int)(2 * bask_p_diff);

		child         = level  - ((level  - level->child) * !upd_ask_sw);
		child->parent = parent - ((parent - orderBook_)   *  upd_ask_sw);
		parent->child = child  - ((child  - orderBook_)   *  upd_ask_sw);

		level->child  = 0;
		level->parent = 0;

		level->is_set = false;
		
		return upd_ask_sw;
	}
	
	inline double best_price()
	{
		return head->price;
	}
	
	inline uint64_t best_vol()
	{
		return head->vol;
	}
};