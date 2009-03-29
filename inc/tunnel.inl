/*
 * tcptun.cpp
 *
 *  Created on: Mar 14, 2009
 *      Author: begray
 */

using namespace boost::asio::ip;
using namespace boost::asio;

void report_boost_error(const char * msg, const boost::system::error_code & error)
{
	std::cerr << msg << error << "(" << error.message() << ")\n";
}

// buffer pool
mutable_buffer buffer_pool::get()
{
	if (0 == unused_.size())
	{
		boost::shared_array<char> new_buffer(new char[default_buffer_size]);

		used_.push_back(new_buffer);

		return boost::asio::mutable_buffer(new_buffer.get(), default_buffer_size);
	}
	else
	{
		boost::shared_array<char> buffer = unused_.back();

		unused_.pop_back();
		used_.push_back(buffer);

		return boost::asio::mutable_buffer(buffer.get(), default_buffer_size);
	}
}

// compares mutable_buffer to shared_array using pointer comparison
struct buf_comparator : public std::unary_function<boost::shared_array<char>, bool >
{
	const boost::asio::mutable_buffer & buffer_;

	buf_comparator(const boost::asio::mutable_buffer & buffer)
	: buffer_(buffer)
	{}

	bool operator()(boost::shared_array<char> array) const
	{
		return boost::asio::buffer_cast<const char*>(buffer_) == array.get();
	}
};

void buffer_pool::put(const mutable_buffer & buffer)
{
	using namespace boost::asio;

	// move it to the end of used list
	buffer_list::iterator iter = std::remove_if(used_.begin(), used_.end(), buf_comparator(buffer));

	// copy it to unused list
	std::copy(iter, used_.end(), std::back_inserter(unused_));

	// finally erase from used
	used_.erase(iter, used_.end());
}

// session
template<typename Proto>
tun_session<Proto>::tun_session(boost::asio::io_service & service, tun_server<Proto> & server)
: service_(service)
, server_(server)
, client_socket_(service)
, server_socket_(service)
{

}

template<typename Proto>
void tun_session<Proto>::connect_to_server(tun_session_ptr shared_this, typename Proto::resolver::iterator endpoint_iterator)
{
	const basic_endpoint<Proto> & endpoint = *endpoint_iterator;

	std::cout << "Connecting to " << endpoint << "...\n";

	// connect to first resolved endpoint
	server_socket_.async_connect(
			*endpoint_iterator,
			boost::bind(
					&tun_session<Proto>::handle_connect,
					this,
					shared_this,
					boost::asio::placeholders::error));
}

template<typename Proto>
void tun_session<Proto>::receive_from_server(tun_session_ptr shared_this)
{
	// receive from server
	mutable_buffer server_buffer = server_.pool().get();
	server_socket_.async_receive(
			mutable_buffers_1(server_buffer),
			boost::bind(
					&tun_session<Proto>::handle_server_receive,
					this,
					shared_this,
					server_buffer,
					placeholders::error,
					placeholders::bytes_transferred));
}

template<typename Proto>
void tun_session<Proto>::receive_from_client(tun_session_ptr shared_this)
{
	// receive from client
	mutable_buffer client_buffer = server_.pool().get();
	client_socket_.async_receive(
			boost::asio::mutable_buffers_1(client_buffer),
			boost::bind(
					&tun_session<Proto>::handle_client_receive,
					this,
					shared_this,
					client_buffer,
					placeholders::error,
					placeholders::bytes_transferred));
}

template<typename Proto>
void tun_session<Proto>::establish_duplex_transfer(tun_session_ptr shared_this)
{
	receive_from_server(shared_this);
	receive_from_client(shared_this);
}

template<typename Proto>
void tun_session<Proto>::handle_server_receive(typename tun_session<Proto>::tun_session_ptr session, mutable_buffer buffer, const boost::system::error_code & error, std::size_t bytes_transferred)
{
	if (!error)
	{
		//std::clog << "<c" << bytes_transferred << ">";

		// resend received bytes to server
		client_socket_.async_send(
				const_buffers_1(const_buffer(buffer_cast<char*>(buffer), bytes_transferred)),
				boost::bind(
						&tun_session<Proto>::handle_client_send,
						this,
						session,
						buffer,
						placeholders::error,
						placeholders::bytes_transferred));

		// and receive again
		receive_from_server(session);
	}
	else
	{
		report_boost_error("Receive from server failed: ", error);

		handle_connection_error(session, server_socket_, error);
	}
}

template<typename Proto>
void tun_session<Proto>::handle_client_receive(typename tun_session<Proto>::tun_session_ptr session, mutable_buffer buffer, const boost::system::error_code & error, std::size_t bytes_transferred)
{
	if (!error)
	{
		//std::clog << "<s" << bytes_transferred << ">";

		// resend received bytes to client
		server_socket_.async_send(
				const_buffers_1(const_buffer(buffer_cast<char*>(buffer), bytes_transferred)),
				boost::bind(
						&tun_session<Proto>::handle_server_send,
						this,
						session,
						buffer,
						placeholders::error,
						placeholders::bytes_transferred));

		// and receive again
		receive_from_client(session);
	}
	else
	{
		report_boost_error("Receive from client failed: ", error);

		handle_connection_error(session, client_socket_, error);
	}
}

template<typename Proto>
void tun_session<Proto>::handle_server_send(typename tun_session<Proto>::tun_session_ptr session, mutable_buffer buffer, const boost::system::error_code & error, std::size_t bytes_transferred)
{
	// return buffer to pool
	server_.pool().put(buffer);

	if (!error)
	{
		//std::cout << "Data successfully sent to server.\n";
	}
	else
	{
		report_boost_error("Failed to send data to server: ", error);

		handle_connection_error(session, server_socket_, error);
	}
}

template<typename Proto>
void tun_session<Proto>::handle_client_send(typename tun_session<Proto>::tun_session_ptr session, mutable_buffer buffer, const boost::system::error_code & error, std::size_t bytes_transferred)
{
	// return buffer to pool
	server_.pool().put(buffer);

	if (!error)
	{
		//std::cout << "Data successfully sent to client.\n";
	}
	else
	{
		report_boost_error("Failed to send data to client: ", error);

		handle_connection_error(session, client_socket_, error);
	}
}

template<typename Proto>
void tun_session<Proto>::handle_connect(tun_session_ptr session, const boost::system::error_code & error)
{
	if (!error)
	{
		std::cout << "Connection to server established.\n";

		// establish data passing services in both directions
		establish_duplex_transfer(session);
	}
	else
	{
		report_boost_error("Failed to connect to server: ", error);

		handle_connection_error(session, server_socket_, error);
	}
}

template<typename Proto>
void tun_session<Proto>::handle_connection_error(tun_session_ptr shared_this, const typename Proto::socket & socket, const boost::system::error_code & error)
{
	// drop this session
	server_.handle_disconnect(shared_this, socket, error);
}

// tunneling server
template<typename Proto>
tun_server<Proto>::tun_server(boost::asio::io_service & service, const typename Proto::endpoint & listento)
: service_(service)
, acceptor_(service, listento)
{
}

template<typename Proto>
void tun_server<Proto>::initialize(const std::string & remote_host, const std::string & remote_port)
{
	remote_host_ = remote_host;
	remote_port_ = remote_port;

	start_accept();
}

template<typename Proto>
void tun_server<Proto>::start_accept()
{
	// create new session object
	typename tun_session<Proto>::tun_session_ptr session(new tun_session<Proto>(service_, *this));

	// and start to accept clients
	acceptor_.async_accept(
			session->client_socket(),
			boost::bind(
					&tun_server<Proto>::handle_accept,
					this,
					session,
					placeholders::error));
}

template<typename Proto>
void tun_server<Proto>::handle_accept(typename tun_session<Proto>::tun_session_ptr session, const boost::system::error_code & error)
{
	if (!error)
	{
		std::cout << "Client connection accepted: " << error << "\n";

		// connect this session to server
		connect_to_server(session);

		// remember it
		sessions_.push_back(session);

		// and start another pending accept
		start_accept();
	}
	else
		std::cerr << "Accept failed: " << error << "(" << error.message() << ")\n";
}

template<typename Proto>
void tun_server<Proto>::connect_to_server(typename tun_session<Proto>::tun_session_ptr session)
{
	// resolve provided host&port
	typename Proto::resolver resolver(service_);
	typename Proto::resolver::query query(remote_host_, remote_port_);
	typename Proto::resolver::iterator iter = resolver.resolve(query);

	// connect this session to server
	session->connect_to_server(session, iter);
}

template<typename Proto>
void tun_server<Proto>::handle_disconnect(typename tun_session<Proto>::tun_session_ptr session, const typename Proto::socket & socket, const boost::system::error_code & error)
{
	// close both connections
	session->client_socket().close();
	session->server_socket().close();

	// remove remembered session
	sessions_.erase(std::remove(sessions_.begin(), sessions_.end(), session), sessions_.end());
}
