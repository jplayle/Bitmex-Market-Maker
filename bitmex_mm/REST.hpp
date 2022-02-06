#pragma once

#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/bind/bind.hpp>
#include <boost/asio.hpp>

#include <functional>
#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <string>
#include <time.h>

#include "TradeLogic.hpp"
#include "Semaphore.hpp"
#include "Auth.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http  = beast::http;          // from <boost/beast/http.hpp>
namespace net   = boost::asio;          // from <boost/asio.hpp>
namespace ssl   = boost::asio::ssl;     // from <boost/asio/ssl.hpp>
using tcp       = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>


class REST : public Auth, public std::enable_shared_from_this<REST>
{
	Semaphore                tm;
	std::mutex&              write_mutex;
	std::condition_variable& write_cv;
	bool&                    can_write;
	
	beast::flat_buffer                   rest_buffer;
	tcp::resolver                        rest_resolver;
	net::deadline_timer                  rest_ping_timer;
	beast::ssl_stream<beast::tcp_stream> rest_stream;
	http::response<http::string_body>    rest_res;
	http::request<http::string_body>     ping_req;
    http::request<http::string_body>     post_req;
	http::request<http::string_body>     put_req;
	
	const char* host = "www.bitmex.com";
	const char* port = "443";
	
	const int rest_ping_interval = 60;
	
	const int minute_msg_limit = 120;
	const int second_msg_limit = 10;
	
	int minute_msgs_sent = 0;
	int second_msgs_sent = 0;
	
	std::chrono::steady_clock::time_point second_start;
	
	const std::string post_url = "POST/api/v1/order";
	const std::string put_url  = "PUT/api/v1/order";
	
	
    void
    on_resolve(beast::error_code ec, tcp::resolver::results_type results)
    {
        beast::get_lowest_layer(rest_stream).expires_after(std::chrono::seconds(30));

        beast::get_lowest_layer(rest_stream).async_connect(
            results,
            beast::bind_front_handler(
                &REST::on_connect,
                shared_from_this()));
    }

    void
    on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        rest_stream.async_handshake(
            ssl::stream_base::client,
            beast::bind_front_handler(
                &REST::on_handshake,
                shared_from_this()));
    }

    void
    on_handshake(beast::error_code ec)
    {
        beast::get_lowest_layer(rest_stream).expires_after(std::chrono::seconds(30));

		rest_ping_timer.expires_from_now(boost::posix_time::seconds(rest_ping_interval));
		rest_ping_timer.async_wait(boost::bind(&REST::send_keepalive, this, boost::asio::placeholders::error));
		
		second_start = std::chrono::steady_clock::now();
    }
	
	inline void on_new_order(beast::error_code ec, std::size_t bytes_transferred)
	{		
		http::async_read(rest_stream, rest_buffer, rest_res,
						 beast::bind_front_handler(&REST::on_read,
												   shared_from_this()));
	}
	
	inline void on_read(beast::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);
		
		if (rest_res.result_int() != 200)
		{
			usleep(5e5); // if system overloaded
			std::cout << "REST error: " << rest_res.result_int() << " " << rest_res.body() << "\n\n";
		}
		else
		{
			std::cout << "REST res: " << rest_res.body() << "\n\n";
		}
		
		rest_buffer.consume(rest_buffer.size());
		rest_res.body() = "";
		
		tm._write_unlock(write_mutex, write_cv, can_write);
	}
	
	inline void update_second_window()
	{
		auto t1 = std::chrono::steady_clock::now();
		
		auto dt = std::chrono::duration_cast<std::chrono::seconds>(t1 - second_start).count();
		
		if (dt >= 1)
		{
			second_msgs_sent = 0;
			second_start     = t1;
		}
	}
	
	// cyclic ping to sustain session and reset messages per minute count
	inline void send_keepalive(const boost::system::error_code& error)
	{
		rest_ping_timer.expires_from_now(boost::posix_time::seconds(rest_ping_interval));
		rest_ping_timer.async_wait(boost::bind(&REST::send_keepalive, this, boost::asio::placeholders::error));
		
		if (!minute_msgs_sent)
		{
			// if no requests were sent in the last window, send one to keep the connection alive
			if (tm._write_try_lock(write_mutex, write_cv, can_write))
			{				
				http::async_write(rest_stream, ping_req,
						beast::bind_front_handler(
							&REST::read_keepalive,
							shared_from_this()));
				
				minute_msgs_sent = 1;
				second_msgs_sent = 1;
				
				return;
			}
		}
		minute_msgs_sent = 0;
	}
	
	inline void read_keepalive(beast::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);
		
		http::async_read(rest_stream, rest_buffer, rest_res, beast::bind_front_handler(&REST::on_read_keepalive, shared_from_this()));
	}
	
	inline void on_read_keepalive(beast::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);
		
		rest_buffer.consume(rest_buffer.size());
		rest_res.body() = "";
		
		tm._write_unlock(write_mutex, write_cv, can_write);
	}
	
	// request objects
	void init_post_req()
	{
		post_req.version(11);
        post_req.method(http::verb::post);
        post_req.target("/api/v1/order");
        post_req.set(http::field::host, host);
        post_req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
		post_req.set(http::field::accept, "*/*");
		post_req.set(http::field::content_type, "application/json");
		post_req.set(http::field::connection, "Keep-Alive");
		post_req.set("api-key", apiKeyCStr);
		post_req.insert("Content-Length", "");
		post_req.insert("api-expires", "");
		post_req.insert("api-signature", "");
	}
	
	void init_put_req()
	{		
		put_req.version(11);
        put_req.method(http::verb::put);
        put_req.target("/api/v1/order");
        put_req.set(http::field::host, host);
        put_req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
		put_req.set(http::field::accept, "*/*");
		put_req.set(http::field::content_type, "application/json");
		put_req.set(http::field::connection, "Keep-Alive");
		put_req.set("api-key", apiKeyCStr);
		put_req.set("Content-Length", "");
		put_req.insert("api-expires", "");
		put_req.insert("api-signature", "");
	}
	
	void init_ping_req()
	{
		ping_req.version(11);
        ping_req.method(http::verb::get);
        ping_req.target("/api/v1/announcement");
        ping_req.set(http::field::host, host);
        ping_req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
		ping_req.set(http::field::accept, "*/*");
		ping_req.set(http::field::connection, "Keep-Alive");
	}
	
public:
    explicit
    REST(std::mutex&              wm,
		 std::condition_variable& wcv,
		 bool&                    cw,
		 net::any_io_executor     rest_ex,
		 ssl::context&            rest_ctx)
		:
		write_mutex(wm), write_cv(wcv), can_write(cw),
		rest_resolver(rest_ex),
		rest_stream(rest_ex, rest_ctx),
		rest_ping_timer(rest_ex)
    {
		init_ping_req();
		init_post_req();
		init_put_req();
	}

    void
    run_rest_service()
    {
        if(! SSL_set_tlsext_host_name(rest_stream.native_handle(), host))
        {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            std::cerr << ec.message() << "\n";
            return;
        }

        rest_resolver.async_resolve(
            host,
            port,
            beast::bind_front_handler(
                &REST::on_resolve,
                shared_from_this()));
    }
	
	inline void send_new_order(std::string& order_msg)
	{
		update_second_window();
		
		if (minute_msgs_sent == minute_msg_limit)
		{
			std::cout << "minute rate lim exceeed" << '\n';
			tm._write_unlock(write_mutex, write_cv, can_write);
			return;
		} else if (second_msgs_sent == second_msg_limit)
		{
			std::cout << "second rate lim exceeed" << '\n';
			tm._write_unlock(write_mutex, write_cv, can_write);
			return;
		}
		
		std::string valid_till = std::to_string(time(0) + msg_t_expiry);
		
		std::cout << post_url << " " << order_msg << "\n\n";
		
		post_req.set("api-expires",    valid_till);
		post_req.set("api-signature",  HMAC_SHA256_hex(post_url, valid_till, order_msg));
		post_req.set("Content-Length", std::to_string(order_msg.length()));
		post_req.body() = order_msg;
		
		
		http::async_write(rest_stream, post_req,
            beast::bind_front_handler(
                &REST::on_new_order,
                shared_from_this()));
		
		++minute_msgs_sent;
		++second_msgs_sent;
	}
	
	inline void send_update_order(std::string& order_msg)
	{
		update_second_window();
		
		if (minute_msgs_sent == minute_msg_limit)
		{
			std::cout << "minute rate lim exceeed" << '\n';
			tm._write_unlock(write_mutex, write_cv, can_write);
			return;
		} else if (second_msgs_sent == second_msg_limit)
		{
			std::cout << "second rate lim exceeed" << '\n';
			tm._write_unlock(write_mutex, write_cv, can_write);
			return;
		}
		
		std::cout << put_url << " " << order_msg << "\n\n";
		
		std::string valid_till = std::to_string(time(0) + msg_t_expiry);
		
		put_req.set("api-expires",    valid_till);
		put_req.set("api-signature",  HMAC_SHA256_hex(put_url, valid_till, order_msg));
		put_req.set("Content-Length", std::to_string(order_msg.length()));
		put_req.body() = order_msg;
		
		
		http::async_write(rest_stream, put_req,
            beast::bind_front_handler(
                &REST::on_new_order,
                shared_from_this()));
		
		++minute_msgs_sent;
		++second_msgs_sent;
	}
};