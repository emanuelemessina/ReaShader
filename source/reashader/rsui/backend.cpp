#ifdef _WIN32
#include <SDKDDKVer.h> // don't know why triggers warning without
#endif

#include <restinio/all.hpp> // must be included in this translation unit otherwise conflict with windows.h
#include <restinio/websocket/websocket.hpp> // must be included right after restinio/all .. (otherwise syntax error wtf)
namespace rws = restinio::websocket::basic;
using ws_registry_t = std::map< std::uint64_t, rws::ws_handle_t >;

#include <nlohmann/json.hpp>

#include "tools/paths.h"
#include "tools/exceptions.h"

#include "backend.h"

namespace ReaShader {
	bool port_in_use(unsigned short port) {
		using namespace restinio::asio_ns;
		using ip::tcp;

		io_service svc;
		tcp::acceptor a(svc);

		error_code ec;
		a.open(tcp::v4(), ec) || a.bind({ tcp::v4(), port }, ec);

		return ec == error::address_in_use;
	}

	using router_t = restinio::router::express_router_t<>;

	using uiserver_traits_t =
		restinio::traits_t<
		restinio::asio_timer_manager_t,
		restinio::null_logger_t,
		router_t >;

	using uiserver_instance_t = restinio::running_server_instance_t < restinio::http_server_t<uiserver_traits_t>>;

	using json = nlohmann::json;

	auto RSUIServer::_uiserver_handler()
	{
		auto router = std::make_unique< router_t >();

		router->http_get("/", [](auto req, auto) {
			try {
			auto sf = restinio::sendfile(tools::paths::join({ RSUI_DIR, "rsui.html" }));

			return
				req->create_response()
				.append_header(
					restinio::http_field::server,
					"ReaShader UI Server")
				.append_header_date_field()
				.append_header(
					restinio::http_field::content_type,
					"text/html")
				.set_body(std::move(sf))
				.done();
		}
		catch (STDEXC e)
		{
			std::string body = fmt::format("rsui.html not found! <br> Details: <br> {0}", e.what());

			EXCEPTION_OUT(e, "RSUIServer", "rsui.html not found!")

				return
				req->create_response(restinio::status_not_found())
				.set_body(body)
				.append_header_date_field()
				.connection_close()
				.done();
		}
			});

		router->http_put("/", [&](auto req, auto) {
			_update_param(req->body().c_str());

		return
			req->create_response(restinio::status_ok())
			.append_header_date_field()
			.done();
			});

		router->http_get("/ws", [&](auto req, auto) {
			if (restinio::http_connection_header_t::upgrade == req->header().connection())
			{
				auto wsh =
					rws::upgrade< uiserver_traits_t >(
						*req,
						rws::activation_t::immediate,
						[&](auto wsh, auto m) {
							if (rws::opcode_t::text_frame == m->opcode() ||
							rws::opcode_t::binary_frame == m->opcode() ||
								rws::opcode_t::continuation_frame == m->opcode())
							{
								// handle incoming message
								//wsh->send_message(*m);
								_update_param(m->payload().c_str());
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

		router->non_matched_request_handler(
			[](auto req) {
				return
				req->create_response(restinio::status_not_found())
			.append_header_date_field()
			.connection_close()
			.done();
			});

		return router;
	}

	RSUIServer::RSUIServer(controller* rsController) : _rsController(rsController) {
		_ws_registry = new ws_registry_t();

		try {
			// random available port
			do {
				// random port in iana ephemeral range
				_port = rand() % (65535 - 49152 + 1) + 49152;
			} while (port_in_use(_port));

			_uiserver_handle = restinio::run_async(
				restinio::own_io_context(),
				restinio::server_settings_t<uiserver_traits_t>{}
			.port(_port)
				.address("localhost")
				.request_handler(_uiserver_handler())
				.cleanup_func([&] {
				std::any_cast<ws_registry_t*>(_ws_registry)->clear();
					}),
				1
						).release();
		}
		catch (const std::exception& e) {
			EXCEPTION_OUT(e, "RSUIServer", "couldn't start ui server")
				_failed = true;
		}
	}
	RSUIServer::~RSUIServer() {
		std::any_cast<uiserver_instance_t*>(_uiserver_handle)->stop();
		delete std::any_cast<ws_registry_t*>(_ws_registry);
	}

	/**
		 * Sends raw JSON message to processor.
		 * Parse the JSON and set param normalized (to update the vst ui).
		 */
	void RSUIServer::_update_param(const char* json) {
		using njson = nlohmann::json;

		_rsController->sendTextMessage(json);
		try {
			auto msg = njson::parse(json);
			_rsController->setParamNormalized((Steinberg::Vst::ParamID)msg["paramId"], (Steinberg::Vst::ParamValue)msg["value"]);
		}
		catch (STDEXC e) {
			LOG_EXCEPTION(e, "RSUIServer", "malformed json message!");
		}
	}
};