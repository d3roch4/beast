//
// Copyright (c) 2016-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: WebSocket SSL server, coroutine
//
//------------------------------------------------------------------------------

#include "example/common/server_certificate.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace ssl = boost::asio::ssl;               // from <boost/asio/ssl.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

//------------------------------------------------------------------------------

// Report a failure
void
fail(boost::system::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

// Echoes back all received WebSocket messages
void
do_session(
    tcp::socket& socket,
    ssl::context& ctx,
    boost::asio::yield_context yield)
{
    boost::system::error_code ec;

    // Construct the stream by moving in the socket
    websocket::stream<ssl::stream<tcp::socket&>> ws{socket, ctx};

    // Perform the SSL handshake
    ws.next_layer().async_handshake(ssl::stream_base::server, yield[ec]);
    if(ec)
        return fail(ec, "handshake");

    // Accept the websocket handshake
    ws.async_accept(yield[ec]);
    if(ec)
        return fail(ec, "accept");

    for(;;)
    {
        // This buffer will hold the incoming message
        boost::beast::multi_buffer buffer;

        // Read a message
        ws.async_read(buffer, yield[ec]);

        // This indicates that the session was closed
        if(ec == websocket::error::closed)
            break;

        if(ec)
            return fail(ec, "read");

        // Echo the message back
        ws.text(ws.got_text());
        ws.async_write(buffer.data(), yield[ec]);
        if(ec)
            return fail(ec, "write");
    }
}

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
void
do_listen(
    boost::asio::io_service& ios,
    ssl::context& ctx,
    tcp::endpoint endpoint,
    boost::asio::yield_context yield)
{
    boost::system::error_code ec;

    // Open the acceptor
    tcp::acceptor acceptor(ios);
    acceptor.open(endpoint.protocol(), ec);
    if(ec)
        return fail(ec, "open");

    // Bind to the server address
    acceptor.bind(endpoint, ec);
    if(ec)
        return fail(ec, "bind");

    // Start listening for connections
    acceptor.listen(boost::asio::socket_base::max_connections, ec);
    if(ec)
        return fail(ec, "listen");

    for(;;)
    {
        tcp::socket socket(ios);
        acceptor.async_accept(socket, yield[ec]);
        if(ec)
            fail(ec, "accept");
        else
            boost::asio::spawn(
                acceptor.get_io_service(),
                std::bind(
                    &do_session,
                    std::move(socket),
                    std::ref(ctx),
                    std::placeholders::_1));
    }
}

int main(int argc, char* argv[])
{
    // Check command line arguments.
    if (argc != 4)
    {
        std::cerr <<
            "Usage: websocket-server-coro-ssl <address> <port> <threads>\n" <<
            "Example:\n" <<
            "    websocket-server-coro-ssl 0.0.0.0 8080 1\n";
        return EXIT_FAILURE;
    }
    auto const address = boost::asio::ip::address::from_string(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto const threads = std::max<std::size_t>(1, std::atoi(argv[3]));

    // The io_service is required for all I/O
    boost::asio::io_service ios{threads};

    // The SSL context is required, and holds certificates
    ssl::context ctx{ssl::context::sslv23};

    // This holds the self-signed certificate used by the server
    load_server_certificate(ctx);

    // Spawn a listening port
    boost::asio::spawn(ios,
        std::bind(
            &do_listen,
            std::ref(ios),
            std::ref(ctx),
            tcp::endpoint{address, port},
            std::placeholders::_1));

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for(auto i = threads - 1; i > 0; --i)
        v.emplace_back(
        [&ios]
        {
            ios.run();
        });
    ios.run();

    return EXIT_SUCCESS;
}
