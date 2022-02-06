#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

#include "Auth.hpp"

namespace beast     = boost::beast;         // from <boost/beast.hpp>
namespace http      = beast::http;          // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;     // from <boost/beast/websocket.hpp>
namespace net       = boost::asio;          // from <boost/asio.hpp>
namespace ssl       = boost::asio::ssl;     // from <boost/asio/ssl.hpp>
using     tcp       = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>


class WSS
{
	std::string host = "www.bitmex.com";
	std::string port = "443";
	const char* url;
	
	beast::flat_buffer buffer;

	net::io_context ioc;
	ssl::context ctx{ssl::context::tlsv12_client};

	tcp::resolver resolver{ioc};
	websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

public:
	WSS (const char* url) : url(url)
	{}
	
	void connect()
	{
		auto const results = resolver.resolve(host, port);

		auto ep = net::connect(get_lowest_layer(ws), results);

		if(! SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str()))
			throw beast::system_error(
				beast::error_code(
					static_cast<int>(::ERR_get_error()),
					net::error::get_ssl_category()),
				"Failed to set SNI Hostname");

		host += ':' + std::to_string(ep.port());

		ws.next_layer().handshake(ssl::stream_base::client);

		ws.set_option(websocket::stream_base::decorator(
			[](websocket::request_type& req)
			{
				req.set(http::field::user_agent,
					std::string(BOOST_BEAST_VERSION_STRING) +
						" websocket-client-coro");
			}));

		ws.handshake(host, url);
	}

	inline std::string read()
	{
		ws.read(buffer);
		
		std::string tmp = beast::buffers_to_string(buffer.data());
		
		buffer.consume(buffer.size());
		
		return tmp;
	}
	
	void write(const std::string& msg)
	{
		ws.write(net::buffer(msg));
	}
	
	void close()
	{
		ws.close(websocket::close_code::normal);
	}
};