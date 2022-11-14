//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#include "myplugincontroller.h"
#include "myplugincids.h"
#include "base/source/fstreamer.h"

#include <stdio.h>
#include <chrono>

#include "tools/paths.h"

#include "rsui/rseditor.h"

#include <restinio/all.hpp> // must be included in this translation unit otherwise conflict with windows.h

#include <nlohmann/json.hpp>

#include <fmt/core.h>

using namespace Steinberg;
using namespace VSTGUI;

namespace ReaShader {

	//------------------------------------------------------------------------
	// ReaShaderController Implementation
	//------------------------------------------------------------------------

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

	using json = nlohmann::json;

	using uiserver_traits_t =
		restinio::traits_t<
		restinio::asio_timer_manager_t,
		restinio::null_logger_t,
		router_t >;

	using uiserver_instance_t = restinio::running_server_instance_t < restinio::http_server_t<uiserver_traits_t>>;

	auto ui_server_handler(ReaShaderController* rs)
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
		catch (const std::exception& e)
		{
			std::string body = fmt::format("rsui.html not found! <br> Details: <br> {0}", e.what());

			return
				req->create_response(restinio::status_not_found())
				.set_body(body)
				.append_header_date_field()
				.connection_close()
				.done();
		}

			});

		router->http_put("/", [rs](auto req, auto) {

			try {

			//auto msg = json::parse(req->body().c_str());
			//rs->setParamNormalized((Steinberg::Vst::ParamID)msg["paramId"], (Steinberg::Vst::ParamValue)msg["value"]);
			rs->sendTextMessage(req->body().c_str());
		}
		catch (const std::exception& e) {

			std::string body = fmt::format("invalid message format! <br> Details: <br> {0}", e.what());

			return
				req->create_response(restinio::status_not_acceptable())
				.set_body(body)
				.append_header_date_field()
				.done();
		}

		return
			req->create_response(restinio::status_ok())
			.append_header_date_field()
			.done();
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

	tresult PLUGIN_API ReaShaderController::initialize(FUnknown* context)
	{
		// Here the Plug-in will be instanciated

		//---do not forget to call parent ------
		tresult result = EditControllerEx1::initialize(context);
		if (result != kResultOk)
		{
			return result;
		}

		// Here you could register some parameters

		parameters.addParameter(STR16("Audio Gain"), STR16("dB"), 0, .5, Vst::ParameterInfo::kCanAutomate, ReaShaderParamIds::uAudioGain);
		parameters.addParameter(STR16("Video Param"), STR16("units"), 0, .5, Vst::ParameterInfo::kCanAutomate, ReaShaderParamIds::uVideoParam);

		// Launch UI Server Thread

		// random available port
		uint16_t port;
		do {
			// random port in iana ephemeral range
			port = rand() % (65535 - 49152 + 1) + 49152;
		}
		while(port_in_use(port));

		uiserver_instance_t* uiserver_handle = restinio::run_async(
			restinio::own_io_context(),
			restinio::server_settings_t<uiserver_traits_t>{}
		.port(port)
			.address("localhost")
			.request_handler(ui_server_handler(this))
			.cleanup_func([&] {
			// called when server is shutting down
				}),
				1
				).release();

		_uiserver = uiserver_handle;

		return result;
	}

	tresult ReaShaderController::receiveText(const char* text)
	{
		// received from Component
		if (text)
		{
			fprintf(stderr, "[AGainController] received: ");
			fprintf(stderr, "%s", text);
			fprintf(stderr, "\n");
		}

		return kResultOk;
	}

	tresult PLUGIN_API ReaShaderController::notify(IMessage* message)
	{
		if (!message)
			return kInvalidArgument;

		if (strcmp(message->getMessageID(), "BinaryMessage") == 0)
		{
			const void* data;
			uint32 size;
			if (message->getAttributes()->getBinary("MyData", data, size) == kResultOk)
			{
				// we are in UI thread
				// size should be 100
				if (size == 100 && ((char*)data)[1] == 1) // yeah...
				{
					fprintf(stderr, "[ReaShaderController] received the binary message!\n");
				}
				return kResultOk;
			}
		}

		return EditControllerEx1::notify(message);
	}


	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::terminate()
	{
		// Here the Plug-in will be de-instanciated, last possibility to remove some memory!

		// terminate uiserver
		std::any_cast<uiserver_instance_t*>(_uiserver)->stop();

		//---do not forget to call parent ------
		return EditControllerEx1::terminate();
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::setComponentState(IBStream* state)
	{
		// Here you get the state of the component (Processor part)
		if (!state)
			return kResultFalse;

		// basically the same code in the processor
		// update ui with processor state (setParam)

		IBStreamer streamer(state, kLittleEndian);

		for (int uParamId = 0; uParamId < uNumParams; uParamId++) {
			float savedParam = 0.f;
			if (streamer.readFloat((float&)savedParam) == false)
				return kResultFalse;
			// in the processor: rParams.at(uParamId) = savedParam;
			setParamNormalized(uParamId, savedParam);
		}

		return kResultOk;
	}

	//------------------------------------------------------------------------
	IPlugView* PLUGIN_API ReaShaderController::createView(FIDString name)
	{
		// Here the Host wants to open your editor (if you have one)
		if (FIDStringsEqual(name, Vst::ViewType::kEditor))
		{
			// create your editor here and return a IPlugView ptr of it
			editor = new RSEditor(this, "view", "myplugineditor.uidesc");

			auto description = editor->getUIDescription();

			std::string viewName = "myview";
			auto* debugAttr = new VSTGUI::UIAttributes();
			debugAttr->setAttribute(VSTGUI::UIViewCreator::kAttrClass, "CViewContainer");
			debugAttr->setAttribute("size", "300, 300");

			description->addNewTemplate(viewName.c_str(), debugAttr);

			//view->exchangeView("myview");
			return editor;
		}
		return nullptr;
	}

	IController* ReaShaderController::createSubController(UTF8StringPtr name,
		const IUIDescription* description,
		VST3Editor* editor)
	{
		if (UTF8StringView(name) == "RSUI")
		{
			auto* controller = new RSUIController(this, editor, description);
			RSEditor* rseditor = dynamic_cast<RSEditor*>(editor);
			rseditor->subController = controller;
			return controller;
		}
		return nullptr;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::setParamNormalized(Vst::ParamID tag, Vst::ParamValue value)
	{
		// called by host to update your parameters
		tresult result = EditControllerEx1::setParamNormalized(tag, value);
		return result;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::getParamStringByValue(Vst::ParamID tag, Vst::ParamValue valueNormalized, Vst::String128 string)
	{
		// called by host to get a string for given normalized value of a specific parameter
		// (without having to set the value!)
		return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::getParamValueByString(Vst::ParamID tag, Vst::TChar* string, Vst::ParamValue& valueNormalized)
	{
		// called by host to get a normalized value from a string representation of a specific parameter
		// (without having to set the value!)
		return EditControllerEx1::getParamValueByString(tag, string, valueNormalized);
	}

	//------------------------------------------------------------------------

	tresult PLUGIN_API ReaShaderController::setState(IBStream* state)
	{
		// Here you get the state of the controller

		return kResultTrue;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::getState(IBStream* state)
	{
		// Here you are asked to deliver the state of the controller (if needed)
		// Note: the real state of your plug-in is saved in the processor

		return kResultTrue;
	}
	//------------------------------------------------------------------------

} // namespace ReaShader
