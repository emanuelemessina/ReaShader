#ifdef _WIN32
#include <SDKDDKVer.h> // don't know why triggers warning without
#endif

#include <restinio/all.hpp> // must be included in this translation unit otherwise conflict with windows.h
#include <restinio/websocket/websocket.hpp> // must be included right after restinio/all .. (otherwise syntax error wtf)
namespace rws = restinio::websocket::basic;
using ws_registry_t = std::map<std::uint64_t, rws::ws_handle_t>;

#include <nlohmann/json.hpp>

#include "tools/mime_types.h"
#include "tools/paths.h"

#include "backend.h"

#include "tools/exceptions.h"
#include "tools/logging.h"

namespace ReaShader
{
	bool port_in_use(unsigned short port)
	{
		using namespace restinio::asio_ns;
		using ip::tcp;

		io_service svc;
		tcp::acceptor a(svc);

		error_code ec;
		a.open(tcp::v4(), ec) || a.bind({ tcp::v4(), port }, ec);

		return ec == error::address_in_use;
	}

	using router_t = restinio::router::express_router_t<>;

	using uiserver_traits_t = restinio::traits_t<restinio::asio_timer_manager_t, restinio::null_logger_t, router_t>;

	using uiserver_instance_t = restinio::running_server_instance_t<restinio::http_server_t<uiserver_traits_t>>;

	using json = nlohmann::json;

	auto RSUIServer::_uiserver_handler()
	{
		auto router = std::make_unique<router_t>();

		// root path request
		router->http_get("/", [](auto req, auto) {
			try
			{
				auto sf = restinio::sendfile(tools::paths::join({ RSUI_DIR, "rsui.html" }));

				return req->create_response()
					.append_header(restinio::http_field::server, "ReaShader UI Server")
					.append_header_date_field()
					.append_header(restinio::http_field::content_type, "text/html")
					.set_body(std::move(sf))
					.done();
			}
			catch (STDEXC e)
			{
				std::string body = fmt::format("rsui.html not found! <br> Details: <br> {0}", e.what());

				LOG(e, toConsole | toFile | toBox, "RSUIServer", "Filesystem error", "rsui.html not found!");

				return req->create_response(restinio::status_not_found())
					.set_body(body)
					.append_header_date_field()
					.connection_close()
					.done();
			}
		});

		// generic file request
		router->http_get(
			R"(/:path(.*)\.:ext(.*))", restinio::path2regex::options_t{}.strict(true), [&](auto req, auto params) {
				auto path = req->header().path();

				if (std::string::npos == path.find(".."))
				{
					// A nice path.

					const auto file_path = tools::paths::join({ RSUI_DIR, std::string{ path.data(), path.size() } });

					try
					{
						auto sf = restinio::sendfile(file_path);

						return req->create_response()
							.append_header(restinio::http_field::content_type,
										   content_type_by_file_extention(params["ext"]))
							.set_body(std::move(sf))
							.done();
					}
					catch (const std::exception&)
					{
						LOG(WARNING, toConsole | toFile, "RSUIServer", "Requested file not found",
							std::string(path).c_str());

						return req->create_response(restinio::status_not_found())
							.append_header_date_field()
							.connection_close()
							.done();
					}
				}
				else
				{
					LOG(WARNING, toConsole | toFile, "RSUIServer", "Directory traversal is forbidden",
						std::string(path).c_str());

					// Bad path.
					return req->create_response(restinio::status_forbidden())
						.append_header_date_field()
						.connection_close()
						.done();
				}
			});

		// legacy messaging over http put
		router->http_put("/", [&](auto req, auto) {
			_incomingTextMessageHandler(req->body().c_str());

			return req->create_response(restinio::status_ok()).append_header_date_field().done();
		});

		// websocket server
		router->http_get(R"(/ws)", [&](auto req, auto) {
			if (restinio::http_connection_header_t::upgrade == req->header().connection())
			{
				auto wsh = rws::upgrade<uiserver_traits_t>(*req, rws::activation_t::immediate, [&](auto wsh, auto m) {
					if (rws::opcode_t::text_frame == m->opcode())
					{
						_incomingTextMessageHandler(m->payload().c_str());
					}
					else if (rws::opcode_t::binary_frame == m->opcode() ||
							 rws::opcode_t::continuation_frame == m->opcode())
					{
						// other type of message recieved
						// wsh->send_message(*m);
						//
						// Convert the string to a vector of chars
						std::vector<char> charVector(m->payload().begin(), m->payload().end());
						_incomingBinaryMessageHandler(charVector);
					}
					else if (rws::opcode_t::ping_frame == m->opcode())
					{
						auto resp = *m;
						resp.set_opcode(rws::opcode_t::pong_frame);
						wsh->send_message(resp);
					}
					else if (rws::opcode_t::connection_close_frame == m->opcode())
					{
						std::any_cast<ws_registry_t*>(_ws_registry)->erase(wsh->connection_id());
					}
				});

				std::any_cast<ws_registry_t*>(_ws_registry)->emplace(wsh->connection_id(), wsh);

				return restinio::request_accepted();
			}

			return restinio::request_rejected();
		});

		// non matched request -> 404
		router->non_matched_request_handler([](auto req) {
			return req->create_response(restinio::status_not_found())
				.append_header_date_field()
				.connection_close()
				.done();
		});

		return router;
	}

	RSUIServer::RSUIServer(IncomingTextMessageHandler incomingTextMessageHandler,
						   IncomingBinaryMessageHandler incomingBinaryMessageHandler)
		: _incomingTextMessageHandler(incomingTextMessageHandler),
		  _incomingBinaryMessageHandler(incomingBinaryMessageHandler)
	{
		_ws_registry = new ws_registry_t();

		try
		{
			// random available port
			do
			{
				// random port in iana ephemeral range
				_port = rand() % (65535 - 49152 + 1) + 49152;
			} while (port_in_use(_port));

			_uiserver_handle = restinio::run_async(restinio::own_io_context(),
												   restinio::server_settings_t<uiserver_traits_t>{}
													   .port(_port)
													   .address("localhost")
													   .request_handler(_uiserver_handler())
													   .cleanup_func([&] {

													   }),
												   1)
								   .release();
		}
		catch (const std::exception& e)
		{
			LOG(e, toConsole | toFile | toBox, "RSUIServer", "Server error",
				std::format("Couldn't start ui server at port {}", _port).c_str());
			_failed = true;
		}
	}
	RSUIServer::~RSUIServer()
	{
		uiserver_instance_t* uiserver_handle = std::any_cast<uiserver_instance_t*>(_uiserver_handle);
		if (uiserver_handle)
		{
			uiserver_handle->stop();
		}
		std::any_cast<ws_registry_t*>(_ws_registry)->clear();
		delete std::any_cast<ws_registry_t*>(_ws_registry);
	}

	/**
	 * Send broadcast message to each ws connection (from server to browser).
	 */
	void RSUIServer::sendWSTextMessage(std::string& msg)
	{
		_sendWSMessage(msg, false);
	}
	void RSUIServer::sendWSBinaryMessage(std::vector<char>& msg)
	{
		std::string str(msg.data(), msg.size());
		_sendWSMessage(str, true);
	}

	void RSUIServer::_sendWSMessage(std::string& msg, bool binary)
	{
		auto message =
			rws::message_t(rws::final_frame, binary ? rws::opcode_t::binary_frame : rws::opcode_t::text_frame, msg);
		for (const auto& [id, handle] : *std::any_cast<ws_registry_t*>(_ws_registry))
		{
			handle->send_message(message);
		}
	}
}; // namespace ReaShader
