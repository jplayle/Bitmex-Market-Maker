#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <deque>

#include "Auth.hpp"

namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http  = beast::http;   // from <boost/beast/http.hpp>
namespace net   = boost::asio;   // from <boost/asio.hpp>
namespace ssl   = net::ssl;      // from <boost/asio/ssl.hpp>
using     tcp   = net::ip::tcp;  // from <boost/asio/ip/tcp.hpp>


class REST_Sync : public Auth
{		
	Semaphore                tm;
	std::mutex&              q_mutex;
	std::condition_variable& q_cv;
	bool&                    can_mod;
	
	std::deque<std::string>& new_order_q;
	std::deque<std::string>& upd_order_q;
	
	dom::parser json_parser;
	
	net::io_context ioc;
	ssl::context    ctx{ssl::context::tlsv12_client};
	
	tcp::resolver                        rest_resolver{ioc};
    beast::ssl_stream<beast::tcp_stream> rest_stream{ioc, ctx};
	
	beast::flat_buffer                   rest_buffer;
	http::response<http::string_body>    rest_res;
	http::request<http::string_body>     ping_req;
    http::request<http::string_body>     post_req;
	http::request<http::string_body>     put_req;
	
	std::string o_msg;
	
	const char* host = "www.bitmex.com";
	const char* port = "443";
	
	const int minute_msg_limit = 120;
	const int second_msg_limit = 10;
	
	int minute_msgs_sent = 0;
	int second_msgs_sent = 0;
	
	std::chrono::steady_clock::time_point second_start;
	std::chrono::steady_clock::time_point minute_start;
	
	const std::string post_url = "POST/api/v1/order";
	const std::string put_url  = "PUT/api/v1/order";
	
	bool was_new_order;
	bool was_upd_order;
	
	
	void connect()
	{		
        if(! SSL_set_tlsext_host_name(rest_stream.native_handle(), host))
        {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        // Look up the domain name
        auto const results = rest_resolver.resolve(host, port);

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(rest_stream).connect(results);

        // Perform the SSL handshake
        rest_stream.handshake(ssl::stream_base::client);
		
		refresh_keepalive();
		
		second_start = std::chrono::steady_clock::now();
		minute_start = std::chrono::steady_clock::now();
	}

    inline void send_new_order(std::string& order_msg)
	{
		std::string valid_till = std::to_string(time(0) + msg_t_expiry);
		
		std::cout << post_url << " " << order_msg << "\n\n";
		
		post_req.set("api-expires",    valid_till);
		post_req.set("api-signature",  HMAC_SHA256_hex(post_url, valid_till, order_msg));
		post_req.set("Content-Length", std::to_string(order_msg.length()));
		post_req.body() = order_msg;
		
		was_new_order = true;
		was_upd_order = false;
		
		http::write(rest_stream, post_req);
		
		++minute_msgs_sent;
		++second_msgs_sent;
		
		read();
	}
	
	inline void send_upd_order(std::string& order_msg)
	{
		std::cout << put_url << " " << order_msg << "\n\n";
		
		std::string valid_till = std::to_string(time(0) + msg_t_expiry);
		
		put_req.set("api-expires",    valid_till);
		put_req.set("api-signature",  HMAC_SHA256_hex(put_url, valid_till, order_msg));
		put_req.set("Content-Length", std::to_string(order_msg.length()));
		put_req.body() = order_msg;
		
		was_new_order = false;
		was_upd_order = true;
		
		http::write(rest_stream, put_req);
		
		++minute_msgs_sent;
		++second_msgs_sent;
		
		read();
	}
	
	inline void refresh_keepalive()
	{
		http::write(rest_stream, ping_req);
		
		minute_msgs_sent = 1;
		second_msgs_sent = 1;
		
		//reset second start and minute start
		
		read();
	}

	inline void read()
	{
		http::read(rest_stream, rest_buffer, rest_res);
		
		std::cout << "REST res: " << rest_res << "\n\n";
		
		rest_buffer.consume(rest_buffer.size());
		rest_res.body() = "";
	}
	
	inline bool update_second_window()
	{
		auto t1 = std::chrono::steady_clock::now();
		auto dt = std::chrono::duration_cast<std::chrono::seconds>(t1 - second_start).count();
		
		if (dt >= 1)
		{
			second_msgs_sent = 0;
			second_start     = t1;
			return true;
		}
		return false;
	}
	
	inline bool update_minute_window()
	{
		auto t1 = std::chrono::steady_clock::now();
		auto dt = std::chrono::duration_cast<std::chrono::seconds>(t1 - minute_start).count();
		
		if (dt >= 60)
		{
			minute_msgs_sent = 0;
			minute_start     = t1;
			return true;
		}
		return false;
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
	REST_Sync(
			  std::deque<std::string>& noq,
			  std::deque<std::string>& uoq,
			  std::mutex&              qm,
			  std::condition_variable& qcv,
			  bool&                    cm)
			  :
			  new_order_q(noq), upd_order_q(uoq),
			  q_mutex(qm), q_cv(qcv), can_mod(cm)
	{
		init_ping_req();
		init_post_req();
		init_put_req();
	}
			  
	void run()
	{		
		connect();
		
		for (;;)
		{
			update_second_window();
			update_minute_window();
			
			if (second_msgs_sent == second_msg_limit || minute_msgs_sent == minute_msg_limit) { continue; }
			
			tm._get_lock(q_mutex, q_cv, can_mod);
				
			if (new_order_q.size())
			{
				std::cout << new_order_q.size() << '\n';
				send_new_order(new_order_q.front());
				new_order_q.pop_front();
			}

			if (upd_order_q.size())
			{
				send_upd_order(upd_order_q.front());
				upd_order_q.pop_front();
			}
				
			tm._unlock(q_mutex, q_cv, can_mod);
		}
	}
};