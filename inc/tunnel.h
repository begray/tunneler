/*
 * tcptun.h
 *
 *  Created on: Mar 14, 2009
 *      Author: begray
 */

#ifndef TUNNEL_H_
#define TUNNEL_H_

namespace tunnel
{
	//TODO: consider udp support


	enum { default_buffer_size = 1024 };

	void report_boost_error(const char * msg, const boost::system::error_code & error);

	/*
	 * Simple memory buffers pool
	 * TODO: implement some kind of garbage collection
	 */
	class buffer_pool
	{
	public:
		boost::asio::mutable_buffer get();
		void put(const boost::asio::mutable_buffer & buffer);

	private:
		typedef std::vector<boost::shared_array<char> > buffer_list;

		buffer_list used_;
		buffer_list	unused_;
	};

	template<typename Proto>
	class tun_server;

	/*
	 * Active client session
	 */
	template <typename Proto>
	class tun_session
	{
	public:
		typedef Proto protocol_type;
		typedef boost::shared_ptr<tun_session<protocol_type> > tun_session_ptr;
		typedef std::vector<tun_session_ptr> tun_session_list;
	public:
		tun_session(boost::asio::io_service & service, tun_server<Proto> & server);

		typename Proto::socket & client_socket() { return client_socket_; }
		typename Proto::socket & server_socket() { return server_socket_; }

		void connect_to_server(tun_session_ptr shared_this, typename Proto::resolver::iterator endpoint_iterator);

		// handlers
		void handle_connect(tun_session_ptr session, const boost::system::error_code & error);

		void handle_server_receive(tun_session_ptr session, boost::asio::mutable_buffer buffer, const boost::system::error_code & error, std::size_t bytes_transferred);
		void handle_client_receive(tun_session_ptr session, boost::asio::mutable_buffer buffer, const boost::system::error_code & error, std::size_t bytes_transferred);

		void handle_server_send(tun_session_ptr session, boost::asio::mutable_buffer buffer, const boost::system::error_code & error, std::size_t bytes_transferred);
		void handle_client_send(tun_session_ptr session, boost::asio::mutable_buffer buffer, const boost::system::error_code & error, std::size_t bytes_transferred);
	private:
		// helpers
		void establish_duplex_transfer(tun_session_ptr shared_this);

		void receive_from_server(tun_session_ptr shared_this);
		void receive_from_client(tun_session_ptr shared_this);

		// handlers
		void handle_connection_error(tun_session_ptr shared_this, const typename Proto::socket & socket, const boost::system::error_code & error);
	private:
		boost::asio::io_service & service_;
		tun_server<Proto> & server_;

		typename Proto::socket client_socket_;
		typename Proto::socket server_socket_;
	};

	/*
	 * Main tunneling server class
	 */
	template <typename Proto>
	class tun_server
	{
	public:
		typedef Proto protocol_type;
		typedef tun_session<protocol_type> session_type;
	public:
		tun_server(boost::asio::io_service & service, const typename Proto::endpoint & listento);

		void initialize(const std::string & remote_host, const std::string & remote_port);

		buffer_pool & pool() { return buffer_pool_; }

		void handle_accept(typename session_type::tun_session_ptr session, const boost::system::error_code & error);
		void handle_disconnect(typename session_type::tun_session_ptr session, const typename protocol_type::socket & socket, const boost::system::error_code & error);
	private:
		void start_accept();
		void connect_to_server(typename session_type::tun_session_ptr session);

	private:
		boost::asio::io_service & service_;
		typename Proto::acceptor acceptor_;

		typename session_type::tun_session_list sessions_;
		buffer_pool buffer_pool_;

		std::string remote_host_;
		std::string remote_port_;
	};

#include "tunnel.inl"
}

#endif /* TCPTUN_H_ */
