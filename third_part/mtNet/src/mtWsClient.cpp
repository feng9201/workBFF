#include "mtWsClient.h"

namespace mtNet {

	MtWsClient::MtWsClient()
	{
	}

	MtWsClient::~MtWsClient()
	{
		stopWs();
	}

	// ---------- 事件回调分派（保持回调/继承两种用法） ----------
	void MtWsClient::notifyConn()
	{
#ifdef WS_USE_OVERRIDE
		onConn();
#endif
		if (_events.onConn) {
			_events.onConn();
		}
	}

	void MtWsClient::notifyError(const char* err_msg)
	{
#ifdef WS_USE_OVERRIDE
		onError(err_msg);
#endif
		if (_events.onError) {
			_events.onError(err_msg);
		}
	}

	void MtWsClient::notifyDisconn()
	{
#ifdef WS_USE_OVERRIDE
		onDisconn();
#endif
		if (_events.onDisconn) {
			_events.onDisconn();
		}
	}

	void MtWsClient::notifyReceiveMsg(const char* msg, int len)
	{
#ifdef WS_USE_OVERRIDE
		onReceiveMsg(msg, len);
#endif
		if (_events.onReceiveMsg) {
			_events.onReceiveMsg(msg, len);
		}
	}

	// ---------- mongoose 事件回调（事件线程内执行） ----------
	void wsFn(struct mg_connection* c, int ev, void* ev_data)
	{
		MtWsClient* ws = (c && c->fn_data) ? static_cast<MtWsClient*>(c->fn_data) : nullptr;
		if (ws == nullptr) return;

		switch (ev) {
		case MG_EV_ERROR: {
			// 连接失败（DNS/连接/TLS/握手错误），mongoose 随后会触发 MG_EV_CLOSE
			const char* err_msg = static_cast<const char*>(ev_data);
			ws->notifyError(err_msg);
			break;
		}
		case MG_EV_CONNECT: {
			// 客户端 TCP 已建立：wss:// 需在此初始化 TLS（mongoose 不自动做），
			// 且必须早于 WS 握手请求的发送；opts 必须零初始化
			if (c->is_tls) {
				struct mg_tls_opts opts = {};
				opts.skip_verification = 1;   // 跳过证书校验
				mg_tls_init(c, &opts);
			}
			break;
		}
		case MG_EV_WS_OPEN: {
			// WS 握手完成，连接就绪
			ws->_ws_opened.store(true);
			ws->notifyConn();
			break;
		}
		case MG_EV_WS_MSG: {
			struct mg_ws_message* wm = static_cast<struct mg_ws_message*>(ev_data);
			if (wm) {
				ws->notifyReceiveMsg(wm->data.buf, static_cast<int>(wm->data.len));
			}
			break;
		}
		case MG_EV_WS_CTL: {
			struct mg_ws_message* wm = static_cast<struct mg_ws_message*>(ev_data);
			int op = wm ? (wm->flags & 15) : 0;
			if (op == WEBSOCKET_OP_PONG) {
				ws->_heart_count = 0;   // 收到 PONG，连接存活
			}
			// PING 由 mongoose 自动回 PONG；CLOSE 交由 MG_EV_CLOSE 统一处理
			break;
		}
		case MG_EV_POLL: {
			// 每轮事件循环：冲刷业务线程投递的发送队列
			ws->flushSendQueue(c);
			break;
		}
		case MG_EV_CLOSE: {
			ws->_ws_opened.store(false);
			ws->notifyDisconn();
			// 事件线程内停止：仅置标志，worker 循环退出后自行 freeWs() 收尾
			ws->stopWs();
			break;
		}
		default:
			break;
		}
	}

	// 心跳定时器（事件线程内执行）
	void timer_fn(void* arg)
	{
		MtWsClient* ws = static_cast<MtWsClient*>(arg);
		if (ws == nullptr) return;

		// 连续 3 个周期未收到任何数据/PONG，判定连接异常
		if (ws->_heart_count >= 3) {
			ws->_ws_opened.store(false);
			ws->stopWs();
			return;
		}
		if (ws->_mg_conn) {
			ws->_heart_count++;
			mg_ws_send(ws->_mg_conn, nullptr, 0, WEBSOCKET_OP_PING);
		}
	}

	// ---------- 对外接口 ----------
	// 异步发送：仅入队，实际发送在事件线程（MG_EV_POLL）统一执行，
	// 避免跨线程直接访问 mg_connection 造成数据竞争/UAF
	bool MtWsClient::sendMsg(const char* msg, int len)
	{
		if (msg == nullptr || len <= 0) return false;
		if (!_ws_opened.load() || _is_stop.load()) return false;

		std::lock_guard<std::mutex> lk(_send_mtx);
		_send_queue.emplace_back(msg, len);
		return true;
	}

	void MtWsClient::flushSendQueue(struct mg_connection* c)
	{
		if (c == nullptr) return;
		std::deque<std::string> items;
		{
			std::lock_guard<std::mutex> lk(_send_mtx);
			items.swap(_send_queue);
		}
		for (const std::string& s : items) {
			// 发送失败即丢弃：连接即将关闭，由 CLOSE 流程兜底清理
			mg_ws_send(c, s.data(), s.size(), WEBSOCKET_OP_TEXT);
		}
	}

	void MtWsClient::stopWs()
	{
		if (_is_stop.exchange(true)) return;   // 幂等，防重入

		auto this_id = std::this_thread::get_id();
		if (_worker_thread && _worker_thread->joinable()) {
			if (_worker_td_id.load() != this_id) {
				// 业务线程：等待事件线程退出，由本线程完成资源回收
				_worker_thread->join();
				_worker_thread.reset();
			}
			// 事件线程自身调用：不能 join 自己，worker 循环退出后自行 freeWs()
		}
		if (_worker_td_id.load() != this_id) {
			freeWs();   // 事件线程已退出（join 完成），此时回收安全
		}
	}

	bool MtWsClient::isConn()
	{
		// 仅当 WS 握手完成且未停止时视为已连接
		return _ws_opened.load() && !_is_stop.load();
	}

	void MtWsClient::freeWs()
	{
		if (!_init_ws.exchange(false)) return;   // 幂等，防双重释放
		_ws_opened.store(false);
		if (_heart_timer) {
			mg_timer_free(&_heart_timer, _heart_timer);
			_heart_timer = nullptr;
		}
		mg_mgr_free(&_mgr);
		_mg_conn = nullptr;
		_heart_count = 0;
		std::lock_guard<std::mutex> lk(_send_mtx);
		_send_queue.clear();
	}

	bool MtWsClient::openWs(const char* url, int timeout, int heart_time)
	{
		if (_init_ws.load()) return false;   // 已在运行

		if (_worker_thread && _worker_thread->joinable()) {
			// 事件线程内禁止重连：join 自身会导致死锁，返回 false 由上层投递到业务线程处理
			if (_worker_td_id.load() == std::this_thread::get_id()) return false;
			_worker_thread->join();
			_worker_thread.reset();
		}

		_ws_opened.store(false);
		_is_stop.store(false);

		mg_mgr_init(&_mgr);
		_mgr.dnstimeout = timeout > 0 ? timeout : 5000;
		_mg_conn = mg_ws_connect(&_mgr, url, &wsFn, this, nullptr);
		if (_mg_conn == nullptr) {
			mg_mgr_free(&_mgr);
			_mg_conn = nullptr;
			return false;
		}

		// 心跳定时器：周期不小于 1s，默认 8s；连续 3 个周期无响应判定断线
		heart_time = heart_time > 0 ? heart_time : 8000;
		if (heart_time < 1000) heart_time = 1000;
		_heart_count = 0;
		_heart_timer = mg_timer_add(&_mgr, heart_time, MG_TIMER_REPEAT, timer_fn, this);

		_init_ws.store(true);
		_worker_thread = std::make_unique<std::thread>([this] {
			_worker_td_id.store(std::this_thread::get_id());
			while (!_is_stop.load() && _mg_conn != nullptr) {
				mg_mgr_poll(&_mgr, 100);
			}
			freeWs();   // 事件线程收尾：统一回收
		});
		return true;
	}
};