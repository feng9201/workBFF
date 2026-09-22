#include "mtHttpServer.h"
#include <chrono>
#include <cstdlib>


namespace mtNet
{
	// 在途任务守卫：任务结束时递减计数（所有路径含异常均生效）
	struct HttpTaskCounter
	{
		std::atomic<int>& count;
		~HttpTaskCounter() { count.fetch_sub(1); }
	};

	// ---------------------------------------------------------------
	// HttpSender
	// ---------------------------------------------------------------
	bool HttpSender::reply(int status, const std::string& body, const std::string& headers) const
	{
		if (_server == nullptr || _conn_id == 0) return false;
		return _server->enqueueReply(_conn_id, status, headers, body);
	}

	// ---------------------------------------------------------------
	// 静态回调（mongoose 事件线程内执行）
	// ---------------------------------------------------------------
	static void mtHttpFunc(struct mg_connection* c, int ev, void* ev_data)
	{
		MtHttpServer* server = (MtHttpServer*)(c->fn_data);
		if (server == nullptr) return;

		switch (ev)
		{
		case MG_EV_ACCEPT:
			server->onAccept(c);
			break;
		case MG_EV_HTTP_MSG:
			server->onHttpMsg(c, (struct mg_http_message*)ev_data);
			break;
		case MG_EV_WAKEUP:
			server->flushReplies();
			break;
		case MG_EV_CLOSE:
			server->onClose(c);
			break;
		default:
			break;
		}
	}

	// 超时/回复兜底定时器回调（事件线程内执行）
	static void mtTimerFunc(void* arg)
	{
		MtHttpServer* server = (MtHttpServer*)arg;
		server->checkTimeout();
		server->flushReplies();
	}

	// ---------------------------------------------------------------
	// 工具：query 解析（'&' 分隔、'=' 取值，支持 URL 编码）
	// ---------------------------------------------------------------
	static int hexVal(char ch)
	{
		if (ch >= '0' && ch <= '9') return ch - '0';
		if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
		if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
		return -1;
	}

	static std::string urlDecode(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		for (std::size_t i = 0; i < s.size(); ++i) {
			char ch = s[i];
			if (ch == '%' && i + 2 < s.size()) {
				int hi = hexVal(s[i + 1]);
				int lo = hexVal(s[i + 2]);
				if (hi >= 0 && lo >= 0) {
					out += (char)((hi << 4) | lo);
					i += 2;
					continue;
				}
			}
			out += (ch == '+') ? ' ' : ch;
		}
		return out;
	}

	static void parseQuery(const std::string& s, std::map<std::string, std::string>& out)
	{
		std::size_t pos = 0;
		while (pos < s.size()) {
			std::size_t amp = s.find('&', pos);
			std::string kv = s.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
			std::size_t eq = kv.find('=');
			if (eq != std::string::npos) {
				out[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
			} else if (!kv.empty()) {
				out[urlDecode(kv)] = "";
			}
			if (amp == std::string::npos) break;
			pos = amp + 1;
		}
	}

	// ---------------------------------------------------------------
	// MtHttpServer
	// ---------------------------------------------------------------
	MtHttpServer::MtHttpServer()
	{
	}

	MtHttpServer::~MtHttpServer()
	{
		stop();
	}

	bool MtHttpServer::addRoute(const std::string& method, const std::string& path, Handler handler)
	{
		if (_started.load()) return false;  // 启动后不允许再注册
		if (method.empty() || path.empty() || !handler) return false;
		std::lock_guard<std::mutex> lk(_routes_mtx);
		_routes.push_back(Route{ method, path, std::move(handler) });
		return true;
	}

	bool MtHttpServer::start(const char* host, int timeoutMs)
	{
		if (_started.exchange(true)) return false;
		_timeout_ms = timeoutMs > 0 ? timeoutMs : 5000;

		// handler 在全局单例线程池（mtPool）执行，惰性创建，无需在此初始化
		mg_mgr_init(&_mgr);
		_stop.store(false);
		// 初始化 wakeup 管道：否则 mg_wakeup() 直接失效（pipe 为无效 socket），
		// 回复队列只能等 800ms poll 超时后由定时器兜底 flush，请求延迟达一个 poll 周期
		mg_wakeup_init(&_mgr);

		if (mg_http_listen(&_mgr, host, mtHttpFunc, this) == nullptr) {
			mg_mgr_free(&_mgr);
			_started.store(false);
			return false;
		}
		// 周期定时器：超时检查 + 回复队列兜底（100ms 粒度）
		mg_timer_add(&_mgr, 100, MG_TIMER_REPEAT, mtTimerFunc, this);

		_worker_thread = std::make_unique<std::thread>([this] {
			_event_td_id.store(std::this_thread::get_id());
			while (!_stop.load()) {
				mg_mgr_poll(&_mgr, 800);
			}
			// 事件线程退出：回收 mongoose 资源（此刻已无其他线程访问 _mgr）
			mg_mgr_free(&_mgr);
		});
		return true;
	}

	void MtHttpServer::stop()
	{
		if (!_started.load()) return;
		_stop.store(true);

		auto this_id = std::this_thread::get_id();
		if (_worker_thread && _worker_thread->joinable()) {
			if (_event_td_id.load() != this_id) {
				_worker_thread->join();
				_worker_thread.reset();
			} else {
				// 从事件线程自身调用：不能 join 自己，事件线程会在 poll 退出后自行清理
				_worker_thread->detach();
				_worker_thread.reset();
			}
		}

		// 等待在途任务结束：全局单例池不能 purge/reset（会影响其他使用者），
		// 任务体开头检查 _stop，stop 后排队的任务会快速退出
		while (_pending_tasks.load() > 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		{
			std::lock_guard<std::mutex> lk(_reply_mtx);
			_pending_replies.clear();
		}
		// 释放错误回调持有的上层资源
		{
			std::lock_guard<std::mutex> lk(_error_mtx);
			_error_cb = nullptr;
		}
		_started.store(false);
	}

	std::size_t MtHttpServer::inflightCount() const
	{
		std::lock_guard<std::mutex> lk(_inflight_mtx);
		return _inflight.size();
	}

	void MtHttpServer::setErrorCallback(ErrorCallback cb)
	{
		std::lock_guard<std::mutex> lk(_error_mtx);
		_error_cb = std::move(cb);
	}

	void MtHttpServer::onAccept(struct mg_connection* c)
	{
		_conns[c->id] = c;
	}

	void MtHttpServer::onClose(struct mg_connection* c)
	{
		_conns.erase(c->id);
		std::lock_guard<std::mutex> lk(_inflight_mtx);
		_inflight.erase(c->id);
	}

	void MtHttpServer::onHttpMsg(struct mg_connection* c, struct mg_http_message* hm)
	{
		std::string method(hm->method.buf, hm->method.len);
		// 注意：mongoose 解析时已将 hm->uri 截断（不含 query），query 单独放在 hm->query
		std::string path(hm->uri.buf, hm->uri.len);
		std::string query_string;
		if (hm->query.len > 0) {
			query_string.assign(hm->query.buf, hm->query.len);
		}

		// 路由匹配：method + path 精确匹配
		Handler handler;
		{
			std::lock_guard<std::mutex> lk(_routes_mtx);
			for (const Route& r : _routes) {
				if (r.method == method && r.path == path) {
					handler = r.handler;
					break;
				}
			}
		}
		if (!handler) {
			mg_http_reply(c, 404, "", "Not Found\n");
			return;
		}

		// 并发限制：超限同步回 503
		{
			std::lock_guard<std::mutex> lk(_inflight_mtx);
			if (_inflight.size() >= kMaxInflight) {
				mg_http_reply(c, 503, "", "Too Many Requests\n");
				return;
			}
			// 保存 method/path 供错误回调使用（拷贝；method/path 稍后会被 move 进 req）
			_inflight[c->id] = Inflight{
				std::chrono::steady_clock::now() + std::chrono::milliseconds(_timeout_ms),
				false,
				method,
				path
			};
		}

		// 构建请求（整体拷贝，保证跨线程安全）
		HttpRequest req;
		req.method = std::move(method);
		req.path = std::move(path);
		req.query_string = std::move(query_string);
		parseQuery(req.query_string, req.query);
		for (const mg_http_header& h : hm->headers) {
			if (h.name.len == 0) break;
			req.headers[std::string(h.name.buf, h.name.len)] = std::string(h.value.buf, h.value.len);
		}
		req.body.assign(hm->body.buf, hm->body.len);
		char* addr = mg_mprintf("%M", mg_print_ip_port, &c->rem);
		req.remote = (addr != nullptr) ? addr : "";
		free(addr);

		// 提交异步任务到全局单例线程池
		HttpSender sender;
		sender._server = this;
		sender._conn_id = c->id;
		_pending_tasks.fetch_add(1);
		mtPool::pool().detach_task(
			[this, req = std::move(req), sender, handler = std::move(handler)]() {
				HttpTaskCounter counter{ _pending_tasks };
				if (_stop.load()) return;   // 已停止：丢弃排队任务（替代原 purge）
				handler(req, sender);
			});
	}

	void MtHttpServer::flushReplies()
	{
		std::deque<ReplyItem> items;
		{
			std::lock_guard<std::mutex> lk(_reply_mtx);
			items.swap(_pending_replies);
		}

		for (const ReplyItem& item : items) {
			// 已超时 / 已回复 / 连接已关闭 的请求直接丢弃，避免重复回复
			bool ok = false;
			{
				std::lock_guard<std::mutex> lk(_inflight_mtx);
				auto it = _inflight.find(item.conn_id);
				if (it != _inflight.end() && !it->second.replied) {
					it->second.replied = true;
					ok = true;
				}
			}
			if (!ok) continue;

			auto connIt = _conns.find(item.conn_id);
			if (connIt != _conns.end()) {
				const char* hdrs = item.headers.empty() ? "" : item.headers.c_str();
				mg_http_reply(connIt->second, item.status, hdrs, "%s", item.body.c_str());
			}
		}
	}

	void MtHttpServer::checkTimeout()
	{
		auto now = std::chrono::steady_clock::now();
		// 收集已超时项（含请求信息），避免在持锁状态下回调
		struct TimeoutItem { unsigned long id; std::string method; std::string path; };
		std::vector<TimeoutItem> timedout;
		{
			std::lock_guard<std::mutex> lk(_inflight_mtx);
			for (auto& kv : _inflight) {
				if (!kv.second.replied && now > kv.second.deadline) {
					kv.second.replied = true;  // 标记超时，晚到的回复将被丢弃
					timedout.push_back(TimeoutItem{ kv.first, kv.second.method, kv.second.path });
				}
			}
		}

		// 拷贝错误回调并释放锁，避免上层在回调中再次调用本服务器方法造成死锁
		ErrorCallback cb;
		{
			std::lock_guard<std::mutex> lk(_error_mtx);
			cb = _error_cb;
		}

		for (const TimeoutItem& t : timedout) {
			auto connIt = _conns.find(t.id);
			if (connIt != _conns.end()) {
				mg_http_reply(connIt->second, 504, "", "Timeout\n");
			}
			// 通知上层：该请求已超时并被自动回复 504
			if (cb) {
				cb(t.id, 504, t.method, t.path);
			}
		}
	}

	bool MtHttpServer::enqueueReply(unsigned long conn_id, int status,
		const std::string& headers, const std::string& body)
	{
		if (_stop.load()) return false;  // 服务器已停止：丢弃，避免访问已释放的 _mgr

		ReplyItem item;
		item.conn_id = conn_id;
		item.status = status;
		item.headers = headers;
		item.body = body;
		{
			std::lock_guard<std::mutex> lk(_reply_mtx);
			_pending_replies.push_back(std::move(item));
		}

		// 唤醒事件线程处理回复队列；若目标连接已关闭，wufn 找不到目标连接，
		// MG_EV_WAKEUP 不会触发，由定时器 flushReplies() 兜底清理
		mg_wakeup(&_mgr, conn_id, nullptr, 0);
		return true;
	}
}