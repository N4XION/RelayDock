// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "network/http_client.h"

#include "network/winhttp_async.h"
#include "utils/strings.h"

#include <charconv>
#include <format>

namespace rd {

// ---- Shared WinHTTP pieces -------------------------------------------------------------------

namespace winhttp {

std::wstring widen(const std::string &text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
	return out;
}

std::string describeError(DWORD error)
{
	const char *text = "The request failed";
	switch (error) {
	case ERROR_WINHTTP_TIMEOUT:
		text = "The server did not answer in time";
		break;
	case ERROR_WINHTTP_NAME_NOT_RESOLVED:
		text = "The server's name could not be found. Check your internet connection";
		break;
	case ERROR_WINHTTP_CANNOT_CONNECT:
		text = "The connection to the server failed";
		break;
	case ERROR_WINHTTP_CONNECTION_ERROR:
		text = "The connection to the server broke off";
		break;
	case ERROR_WINHTTP_SECURE_FAILURE:
		text = "The secure connection could not be set up, because Windows did not accept the server's certificate";
		break;
	case ERROR_WINHTTP_OPERATION_CANCELLED:
		text = "The request was cancelled";
		break;
	default:
		break;
	}
	return std::format("{} (Windows error {}).", text, error);
}

AsyncState::AsyncState() : step(CreateEventW(nullptr, FALSE, FALSE, nullptr)), closed(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

AsyncState::~AsyncState()
{
	if (step)
		CloseHandle(step);
	if (closed)
		CloseHandle(closed);
}

namespace {

// Runs on a WinHTTP thread. It records what happened and wakes the waiting thread. It must not
// call back into WinHTTP and must not block.
void CALLBACK onStatus(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info, DWORD length)
{
	auto *state = reinterpret_cast<AsyncState *>(context);
	if (!state)
		return;

	switch (status) {
	case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
	case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
	case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
	case WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE:
	case WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE:
		state->error = 0;
		SetEvent(state->step);
		break;
	case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
		if (state->webSocket) {
			const auto *read = static_cast<const WINHTTP_WEB_SOCKET_STATUS *>(info);
			state->bytes = read ? read->dwBytesTransferred : 0;
			state->bufferType = read ? static_cast<DWORD>(read->eBufferType) : 0;
		} else {
			state->bytes = length;
		}
		state->error = 0;
		SetEvent(state->step);
		break;
	case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
		// A WebSocket error starts with the same two fields.
		const auto *result = static_cast<const WINHTTP_ASYNC_RESULT *>(info);
		const DWORD error = result ? result->dwError : 0;
		state->error = error != 0 ? error : static_cast<DWORD>(ERROR_WINHTTP_INTERNAL_ERROR);
		SetEvent(state->step);
		break;
	}
	case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
		SetEvent(state->closed);
		break;
	default:
		break;
	}
}

} // namespace

bool attach(HINTERNET handle, AsyncState *state)
{
	if (!state->step || !state->closed)
		return false;
	if (WinHttpSetStatusCallback(handle, onStatus, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) ==
	    WINHTTP_INVALID_STATUS_CALLBACK)
		return false;
	DWORD_PTR context = reinterpret_cast<DWORD_PTR>(state);
	return WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) != FALSE;
}

StepResult waitStep(AsyncState &state, const std::atomic<bool> &cancel, long long timeoutMs, DWORD &error)
{
	const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs > 0 ? timeoutMs : 0);
	for (;;) {
		if (cancel.load())
			return StepResult::Cancelled;
		if (WaitForSingleObject(state.step, 50) == WAIT_OBJECT_0) {
			error = state.error.load();
			return error == 0 ? StepResult::Done : StepResult::Failed;
		}
		if (GetTickCount64() >= deadline)
			return StepResult::TimedOut;
	}
}

void closeAndRelease(HINTERNET handle, AsyncState *state)
{
	if (!handle) {
		delete state;
		return;
	}
	WinHttpCloseHandle(handle);
	// WinHTTP ends whatever was under way and then makes its last callback. That takes
	// milliseconds. Should it ever not come, the state stays allocated, because a late callback
	// would otherwise write into freed memory.
	if (state && WaitForSingleObject(state->closed, 10000) == WAIT_OBJECT_0)
		delete state;
}

} // namespace winhttp

// ---- Addresses -------------------------------------------------------------------------------

namespace {

bool isThisPc(std::string_view host)
{
	return host == "127.0.0.1" || host == "localhost";
}

bool validHost(std::string_view host)
{
	if (host.empty() || host.size() > 253 || host.front() == '.' || host.front() == '-')
		return false;
	for (const char c : host) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
		if (!ok)
			return false;
	}
	return true;
}

} // namespace

bool parseHttpUrl(std::string_view url, HttpUrl &out)
{
	struct Scheme {
		std::string_view prefix;
		bool secure;
	};
	static constexpr Scheme schemes[] = {{"https://", true}, {"wss://", true}, {"http://", false}, {"ws://", false}};

	HttpUrl parsed;
	std::string_view rest;
	bool known = false;
	for (const Scheme &scheme : schemes) {
		if (startsWithNoCase(url, scheme.prefix)) {
			parsed.secure = scheme.secure;
			rest = url.substr(scheme.prefix.size());
			known = true;
			break;
		}
	}
	if (!known)
		return false;

	// A fragment never goes to the server.
	if (const size_t hash = rest.find('#'); hash != std::string_view::npos)
		rest = rest.substr(0, hash);

	const size_t pathStart = rest.find_first_of("/?");
	std::string_view authority = rest.substr(0, pathStart);
	std::string_view path = pathStart == std::string_view::npos ? std::string_view() : rest.substr(pathStart);

	// No user name or password in an address, and no IPv6 literal.
	if (authority.find('@') != std::string_view::npos || authority.find('[') != std::string_view::npos)
		return false;

	parsed.port = parsed.secure ? 443 : 80;
	if (const size_t colon = authority.find(':'); colon != std::string_view::npos) {
		const std::string_view digits = authority.substr(colon + 1);
		if (digits.empty() || digits.size() > 5)
			return false;
		int port = 0;
		const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), port);
		if (result.ec != std::errc() || result.ptr != digits.data() + digits.size() || port < 1 || port > 65535)
			return false;
		parsed.port = port;
		authority = authority.substr(0, colon);
	}

	parsed.host = toLower(authority);
	if (!validHost(parsed.host))
		return false;
	if (!parsed.secure && !isThisPc(parsed.host))
		return false;

	for (const char c : path) {
		// A space or a control character has no place in a request line.
		if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f)
			return false;
	}
	if (path.empty())
		parsed.path = "/";
	else if (path.front() == '?')
		parsed.path = "/" + std::string(path);
	else
		parsed.path = std::string(path);

	out = std::move(parsed);
	return true;
}

std::string urlEncode(std::string_view value)
{
	static constexpr char digits[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(value.size());
	for (const char c : value) {
		const auto byte = static_cast<unsigned char>(c);
		const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
				   c == '_' || c == '.' || c == '~';
		if (plain) {
			out += c;
		} else {
			out += '%';
			out += digits[byte >> 4];
			out += digits[byte & 0x0f];
		}
	}
	return out;
}

// ---- Requests --------------------------------------------------------------------------------

namespace {

bool validHeader(const std::string &name, const std::string &value)
{
	if (name.empty())
		return false;
	for (const char c : name) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
		if (!ok)
			return false;
	}
	for (const char c : value) {
		if (c == '\r' || c == '\n' || c == '\0')
			return false;
	}
	return true;
}

std::string narrowText(const std::wstring &text)
{
	if (text.empty())
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
	return out;
}

bool validMethod(const std::string &method)
{
	return method == "GET" || method == "POST" || method == "DELETE" || method == "PUT" || method == "PATCH";
}

} // namespace

HttpResponse httpRequest(const HttpRequest &request, const std::atomic<bool> &cancel)
{
	using namespace winhttp;

	HttpResponse response;
	HttpUrl url;
	if (!parseHttpUrl(request.url, url)) {
		response.error = "The address is not a web address RelayDock accepts.";
		return response;
	}
	if (!validMethod(request.method)) {
		response.error = "The request method is not supported.";
		return response;
	}
	for (const auto &[name, value] : request.headers) {
		if (!validHeader(name, value)) {
			response.error = "A request header is not valid.";
			return response;
		}
	}
	if (cancel.load()) {
		response.error = "cancelled";
		return response;
	}

	const int timeoutMs = request.timeoutMs > 0 ? request.timeoutMs : 10000;

	const PlainHandle session{WinHttpOpen(widen(request.userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
					      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
	if (!session) {
		response.error = std::format("Windows could not open an HTTP session (error {}).", GetLastError());
		return response;
	}
	WinHttpSetTimeouts(session.handle, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

	const PlainHandle connection{
		WinHttpConnect(session.handle, widen(url.host).c_str(), static_cast<INTERNET_PORT>(url.port), 0)};
	if (!connection) {
		response.error = std::format("Windows could not prepare the connection (error {}).", GetLastError());
		return response;
	}

	// WINHTTP_FLAG_SECURE: TLS with Windows' certificate checks. They stay at their defaults.
	const HINTERNET handle = WinHttpOpenRequest(connection.handle, widen(request.method).c_str(), widen(url.path).c_str(),
						    nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
						    url.secure ? WINHTTP_FLAG_SECURE : 0);
	if (!handle) {
		response.error = std::format("Windows could not create the request (error {}).", GetLastError());
		return response;
	}

	auto *state = new AsyncState();
	if (!attach(handle, state)) {
		response.error = std::format("Windows could not watch the request (error {}).", GetLastError());
		// No callback is registered, so nothing can arrive later.
		WinHttpCloseHandle(handle);
		delete state;
		return response;
	}

	// From here on, every way out closes the handle and waits for WinHTTP's last callback.
	// `chunk` and the request body must outlive that, and they do: they are locals of this
	// function, which returns only after `finish`.
	std::vector<char> chunk(16 * 1024);
	const auto finish = [&]() -> HttpResponse & {
		closeAndRelease(handle, state);
		return response;
	};
	const auto fail = [&](StepResult result, DWORD error) -> HttpResponse & {
		response.ok = false;
		response.body.clear();
		if (result == StepResult::Cancelled)
			response.error = "cancelled";
		else if (result == StepResult::TimedOut)
			response.error = describeError(ERROR_WINHTTP_TIMEOUT);
		else
			response.error = describeError(error);
		return finish();
	};

	if (!request.followRedirects) {
		DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
		if (!WinHttpSetOption(handle, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) {
			response.error = std::format("Windows could not switch off redirects (error {}).", GetLastError());
			return finish();
		}
	}

	for (const auto &[name, value] : request.headers) {
		const std::wstring line = widen(name + ": " + value + "\r\n");
		if (!WinHttpAddRequestHeaders(handle, line.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD)) {
			response.error = std::format("Windows refused a request header (error {}).", GetLastError());
			return finish();
		}
	}

	// Sending covers three of WinHTTP's steps: finding the server, connecting and sending.
	// WinHTTP ends each on its own timeout. The wait here is the net under that.
	const long long sendWait = 3LL * timeoutMs + 5000;
	const long long stepWait = static_cast<long long>(timeoutMs) + 5000;
	DWORD error = 0;

	void *body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(request.body.data());
	const auto bodySize = static_cast<DWORD>(request.body.size());
	if (!WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, bodySize, bodySize,
				reinterpret_cast<DWORD_PTR>(state)))
		return fail(StepResult::Failed, GetLastError());
	if (const StepResult result = waitStep(*state, cancel, sendWait, error); result != StepResult::Done)
		return fail(result, error);

	if (!WinHttpReceiveResponse(handle, nullptr))
		return fail(StepResult::Failed, GetLastError());
	if (const StepResult result = waitStep(*state, cancel, stepWait, error); result != StepResult::Done)
		return fail(result, error);

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				 &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
		response.error = std::format("The answer had no status (Windows error {}).", GetLastError());
		return finish();
	}
	response.status = static_cast<int>(status);

	// A redirect that was not followed names its target.
	if (!request.followRedirects && status >= 300 && status < 400) {
		DWORD size = 0;
		WinHttpQueryHeaders(handle, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size,
				    WINHTTP_NO_HEADER_INDEX);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 0 && size <= 16 * 1024) {
			std::wstring location(size / sizeof(wchar_t), L'\0');
			if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location.data(), &size,
						WINHTTP_NO_HEADER_INDEX)) {
				location.resize(size / sizeof(wchar_t));
				response.location = narrowText(location);
			}
		}
	}

	// The length the server announced, for the progress report. 0 when it announced none.
	DWORD announced = 0;
	DWORD announcedSize = sizeof(announced);
	if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				 &announced, &announcedSize, WINHTTP_NO_HEADER_INDEX))
		announced = 0;
	const bool report = request.progress && status >= 200 && status < 300;

	for (;;) {
		if (!WinHttpReadData(handle, chunk.data(), static_cast<DWORD>(chunk.size()), nullptr))
			return fail(StepResult::Failed, GetLastError());
		if (const StepResult result = waitStep(*state, cancel, stepWait, error); result != StepResult::Done)
			return fail(result, error);

		const DWORD read = state->bytes.load();
		if (read == 0)
			break;
		if (response.body.size() + read > request.maxBytes) {
			response.error = "The answer was larger than expected and was discarded.";
			response.body.clear();
			return finish();
		}
		response.body.append(chunk.data(), read);
		if (report)
			request.progress(response.body.size(), announced);
	}

	response.ok = true;
	return finish();
}

} // namespace rd
