/*
 * main.cpp
 *
 *  Created on: Mar 14, 2009
 *      Author: begray
 */
#include "inc/common.h"
#include "inc/tunnel.h"

#include <boost/program_options.hpp>

const char * program_name = "Tunneler";
const char * program_version = "v0.1";
const char * program_author = "begray (b@stdcall.com)";

namespace po = boost::program_options;

int main(int argc, char** argv)
{
	try
	{
		po::options_description desc("Allowed options");
		desc.add_options()
			("help", "show help message")
			("listen-port,l", po::value<int>(), "listening port")
			("host,h", po::value<std::string>(), "remote server host address")
			("port,p", po::value<std::string>(), "remote server port");

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		std::cout << program_name << " " << program_version << " by " << program_author << "\n";

		if (vm.count("listen-port") && vm.count("host") && vm.count("port"))
		{
			using namespace boost::asio;
			using namespace boost::asio::ip;

			io_service service;

			tunnel::tun_server<ip::tcp> server(service, tcp::endpoint(tcp::v4(), vm["listen-port"].as<int>()));
			server.initialize(vm["host"].as<std::string>(), vm["port"].as<std::string>());

			std::cout << "Ready for client processing.\n";

			service.run();

			return 0;
		}
		else
		{
			std::cout << desc << "\n";

			return 1;
		}
	}
	catch(std::exception & e)
	{
		std::cerr << "Exception occurred: " << e.what() << "\nProgram terminated.";

		return 2;
	}
}
