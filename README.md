# Bitmex-Market-Maker

C++ market maker for BitMEX.

## Order Book Representation
- O(1) lookup of best bid/ask price.
- O(1) update volume at a given price.
- O(1) delete price from the book.
- O(k) insert new price into the book ("k" is the difference between the new price and the next price in the book).
- Most new prices are inserted at the front so insertion is also O(1) >90% of the time.

## Order Book Stream
- Streams all updates to the limit order book, updates the local order book data and updates orders according to the price/volume changes.
- Updates to the buy and sell sides of the orderbook are handled polymorphically.
- Send orders via asynchronous call handled by the REST thread.

## Order Update Stream
- Streams all updates to orders and trades that were sent to the exchange and reacts accordingly to filled orders.
- Send orders via asynchronous call handled by the REST thread.

## General
- Threads are managed with a semaphore.
- Development is ongoing for full trade management i.e. handling orders that are cancelled or never make it to the exchange.

## Resources
simdjson: https://github.com/simdjson/simdjson
