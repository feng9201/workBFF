#include "mtWsServer.h"
#include <chrono>
#include <cstdlib>


namespace mtNet {

	// 在途任务守卫：任务结束时递减计数（所有路径含异常均生效）
	struct WsTaskCounter
	{
		std::atomic<int>& count;
		~WsTaskCounter() { count.fetch_sub(1); }
	};

	// ---------------------------------------------------------------
	// 静态回调（mongoose 事件线程内执行）
	// ---------------------------------------------------------------
	static void wsServerFn(struct mg_connection* c, int ev, void* ev_data)
	{
		MtWsServer* server = (c && c->fn_data) ? static_cast<MtWsServer*>(c->fn_data) : nullptr;
		if (server == nullptr) return;

		switch (ev) {
		case MG_EV_HTTP_MSG: {
			// WebSocket 升级请求：存在 Upgrade 头即尝试升级（mg_ws_upgrade 内部校验 Sec-WebSocket-Key）
			struct mg_http_message* hm = static_cast<struct mg_http_message*>(ev_data);
			if (mg_http_get_header(hm, "Upgrade") != nullptr) {
				mg_ws_upgrade(c, hm, nullptr);   // 升级成功后触发 MG_EV_WS_OPEN
			} else {
				mg_http_reply(c, 400, "", "Bad Request\n");
			}
			break;
		}
		case MG_EV_WS_OPEN: {
			struct mg_http_message* hm = static_cast<struct mg_http_message*>(ev_data);
			server->onWsOpen(c, hm);
			break;
		}
		case MG_EV_WS_MSG: {
			struct mg_ws_message* wm = static_cast<struct mg_ws_message*>(ev_data);
			server->onWsMsg(c, wm);
			break;
		}
		case MG_EV_WS_CTL: {
			// PING 由 mongoose 自动回 PONG；CLOSE 由 mongoose 自动回执并进入 draining
			struct mg_ws_message* wm = static_cast<struct mg_ws_message*>(ev_data);
			if (wm && (wm->flags & 15) == WEBSOCKET_OP_PONG) {
				server->_pong_time[c->id] = mg_millis();
			}
			break;
		}
		case MG_EV_WAKEUP:
			server->flushSendQueue();
			break;
		case MG_EV_CLOSE:
			server->onClose(c);
			break;
		default:
			break;
		}
	}

	// 兜底定时器（事件线程内执行）：发送队列兜底 + 心跳检查
	static void wsTimerFunc(void* arg)
	{
		MtWsServer* server = static_cast<MtWsServer*>(arg);
		if (server == nullptr) return;
		server->flushSendQueue();
		server->checkHeartbeat();
	}

	// ---------------------------------------------------------------
	// MtWsServer
	// ---------------------------------------------------------------
	MtWsServer::MtWsServer()
	{
	}

	MtWsServer::~MtWsServer()
	{
		stop();
	}

	bool MtWsServer::listen(const char* host, int port)
	{
		if (_listening.exchange(true)) return false;   // 幂等

		// 消息回调走全局单例线程池（mtPool，惰性创建，无需在此初始化）
		mg_mgr_init(&_mgr);
		_stop.store(false);
		// 初始化 wakeup 管道：否则 mg_wakeup() 失效，发送队列只能等 poll 周期兜底
		mg_wakeup_init(&_mgr);

		std::string addr = std::string(host != nullptr && host[0] != '\0' ? host : "0.0.0.0")
			+ ":" + std::to_string(port);
		_listen_conn = mg_http_listen(&_mgr, addr.c_str(), wsServerFn, this);
		if (_listen_conn == nullptr) {
			mg_mgr_free(&_mgr);
			_listening.store(false);
			return false;
		}
		_listen_port = port;
		// 100ms 兜底定时器：flush 发送队列 + 心跳
		mg_timer_add(&_mgr, 100, MG_TIMER_REPEAT, wsTimerFunc, this);

		_worker_thread = std::make_unique<std::thread>([this] {
			_event_td_id.store(std::this_thread::get_id());
			while (!_stop.load()) {
				mg_mgr_poll(&_mgr, 800);
			}
			// 事件线程退出：回收 mongoose 资源
			mg_mgr_free(&_mgr);
		});
		return true;
	}

	void MtWsServer::stop()
	{
		if (!_listening.load()) return;
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
		// 任务体内部检查 open 状态，stop 后排队的任务会快速退出
		while (_pending_tasks.load() > 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		{
			std::lock_guard<std::mutex> lk(_send_mtx);
			_pending_sends.clear();
		}
		_listening.store(false);
	}

	void MtWsServer::setHeartbeat(int intervalMs, int timeoutMs)
	{
		if (intervalMs > 0 && timeoutMs > 0) {
			_heartbeat_interval = intervalMs;
			_heartbeat_timeout = timeoutMs;
		} else {
			_heartbeat_interval = 0;
			_heartbeat_timeout = 0;
		}
	}

	// ---------------------------------------------------------------
	// 连接生命周期（事件线程）
	// ---------------------------------------------------------------
	void MtWsServer::onWsOpen(struct mg_connection* c, struct mg_http_message* hm)
	{
		std::string path;
		if (hm != nullptr) {
			path.assign(hm->uri.buf, hm->uri.len);
		}
		char* addr = mg_mprintf("%M", mg_print_ip_port, &c->rem);
		std::string remote = (addr != nullptr) ? addr : "";
		free(addr);

		// 注：不用 make_shared —— MSVC 下 make_shared 无法访问私有构造函数
		auto conn = std::shared_ptr<MtWsConnection>(new MtWsConnection(this, c->id));
		conn->_state->open.store(true);
		conn->_state->path = std::move(path);
		conn->_state->remote = std::move(remote);

		_conns[c->id] = Conn{ c, conn };
		_pong_time[c->id] = mg_millis();
		_conn_count.fetch_add(1);

		auto cb = _events.onNewConnection;
		if (cb) {
			cb(conn.get());
		}
	}

	void MtWsServer::onWsMsg(struct mg_connection* c, struct mg_ws_message* wm)
	{
		if (wm == nullptr) return;
		auto it = _conns.find(c->id);
		if (it == _conns.end()) return;

		// shared_ptr 保活：即使任务执行期间连接关闭，连接对象也不会被析构
		auto conn = it->second.ws;
		int op = wm->flags & 15;
		std::string payload(wm->data.buf, wm->data.len);

		_pending_tasks.fetch_add(1);
		mtPool::pool().detach_task([this, conn, op, payload = std::move(payload)]() {
			WsTaskCounter counter{ _pending_tasks };
			if (!conn->isOpen()) return;
			if (op == WEBSOCKET_OP_TEXT && conn->_events.onTextMessageReceived) {
				conn->_events.onTextMessageReceived(payload);
			} else if (op == WEBSOCKET_OP_BINARY && conn->_events.onBinaryMessageReceived) {
				conn->_events.onBinaryMessageReceived(
					reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
			}
		});
	}

	void MtWsServer::onClose(struct mg_connection* c)
	{
		auto it = _conns.find(c->id);
		if (it == _conns.end()) return;

		auto conn = it->second.ws;   // 保活：回调执行期间连接对象不会被析构
		conn->_state->open.store(false);
		_conns.erase(it);
		_pong_time.erase(c->id);
		_conn_count.fetch_sub(1);

		auto disc = conn->_events.onDisconnected;
		if (disc) disc();
		auto cb = _events.onConnectionClosed;
		if (cb) cb(conn.get());
	}

	// ---------------------------------------------------------------
	// 发送/关闭（可跨线程调用）
	// ---------------------------------------------------------------
	bool MtWsServer::enqueueSend(unsigned long conn_id, int op, std::string payload)
	{
		if (_stop.load()) return false;
		{
			std::lock_guard<std::mutex> lk(_send_mtx);
			_pending_sends.push_back(SendItem{ conn_id, op, std::move(payload) });
		}
		// 唤醒事件线程立即处理；若目标连接已关闭，wufn 找不到目标连接时
		// MG_EV_WAKEUP 不触发，由 100ms 定时器 flushSendQueue() 兜底清理
		mg_wakeup(&_mgr, conn_id, nullptr, 0);
		return true;
	}

	void MtWsServer::enqueueClose(unsigned long conn_id)
	{
		if (_stop.load()) return;
		{
			std::lock_guard<std::mutex> lk(_send_mtx);
			_pending_sends.push_back(SendItem{ conn_id, kOpClose, std::string() });
		}
		mg_wakeup(&_mgr, conn_id, nullptr, 0);
	}

	void MtWsServer::flushSendQueue()
	{
		std::deque<SendItem> items;
		{
			std::lock_guard<std::mutex> lk(_send_mtx);
			items.swap(_pending_sends);
		}
		for (const SendItem& item : items) {
			auto it = _conns.find(item.conn_id);
			if (it == _conns.end()) continue;   // 连接已关闭，丢弃
			if (item.op == kOpClose) {
				// 优雅关闭：发 CLOSE 帧，draining 后自动断开
				mg_ws_send(it->second.c, nullptr, 0, WEBSOCKET_OP_CLOSE);
				it->second.c->is_draining = 1;
			} else {
				mg_ws_send(it->second.c, item.payload.data(), item.payload.size(), item.op);
			}
		}
	}

	void MtWsServer::checkHeartbeat()
	{
		if (_heartbeat_interval <= 0 || _heartbeat_timeout <= 0) return;
		uint64_t now = mg_millis();

		// 先收集超时连接，再统一关闭（避免遍历中修改连接表）
		std::vector<unsigned long> to_close;
		for (const auto& kv : _conns) {
			auto pit = _pong_time.find(kv.first);
			uint64_t last = (pit != _pong_time.end()) ? pit->second : 0;
			if (now - last > static_cast<uint64_t>(_heartbeat_timeout)) {
				to_close.push_back(kv.first);
			}
		}
		for (unsigned long id : to_close) {
			auto it = _conns.find(id);
			if (it != _conns.end()) {
				mg_close_conn(it->second.c);
			}
		}
		// 存活连接发送心跳
		for (const auto& kv : _conns) {
			mg_ws_send(kv.second.c, nullptr, 0, WEBSOCKET_OP_PING);
		}
	}

	// ---------------------------------------------------------------
	// MtWsConnection
	// ---------------------------------------------------------------
	bool MtWsConnection::sendTextMessage(const std::string& msg)
	{
		if (!_state->open.load()) return false;
		return _server->enqueueSend(_conn_id, WEBSOCKET_OP_TEXT, msg);
	}

	bool MtWsConnection::sendBinaryMessage(const void* data, size_t len)
	{
		if (!_state->open.load() || data == nullptr) return false;
		return _server->enqueueSend(_conn_id, WEBSOCKET_OP_BINARY,
			std::string(static_cast<const char*>(data), len));
	}

	void MtWsConnection::close()
	{
		if (!_state->open.load()) return;
		_server->enqueueClose(_conn_id);
	}

	bool MtWsConnection::isOpen() const
	{
		return _state->open.load();
	}
}