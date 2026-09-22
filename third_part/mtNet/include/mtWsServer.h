#pragma once
/*****
* @brief: 基于 mongoose 的异步 websocket 服务端，调用方式仿 Qt QWebSocketServer
* @auth:  hxf
*
*   MtWsServer server;
*   // 新连接回调（事件线程执行，内部勿做耗时操作）
*   server.events().onNewConnection = [](mtNet::MtWsConnection* conn) {
*       conn->events().onTextMessageReceived = [conn](const std::string& msg) {
*           conn->sendTextMessage("echo: " + msg);   // 回调在线程池执行，send 线程安全
*       };
*       conn->events().onBinaryMessageReceived = [conn](const uint8_t* data, size_t len) {
*           conn->sendBinaryMessage(data, len);
*       };
*       conn->events().onDisconnected = [] {
*           // 此时连接已关闭，勿再使用 conn 指针
*       };
*   };
*
*   if (server.listen("0.0.0.0", 5305)) {
*       server.setHeartbeat(8000, 24000);   // 可选：8s 心跳，24s 无响应断开
*   }
*   // 程序退出时：server.stop();（析构自动调用，阻塞至事件线程退出）
*
* 线程模型：
*   - 事件线程：mongoose 事件循环，负责升级/接收/发送/关闭（mg_ws_send 仅在此线程调用）
 *   - 全局单例线程池（mtPool）：onTextMessageReceived/onBinaryMessageReceived 回调
*   - sendTextMessage/sendBinaryMessage/close 可跨线程调用（入队后由事件线程执行）
*   - 同一连接的多个消息回调可能并发执行（线程池调度），如需严格有序请业务侧自行串行化
******/
#include "mongoose.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <mtPool/MtPool.h>


namespace mtNet {

	class MtWsServer;
	class MtWsConnection;

	// 连接共享状态：事件线程写、业务线程只读
	struct MtWsConnState
	{
		std::atomic<bool> open{ false };   // 连接是否处于打开状态
		std::string path;                  // 升级时请求的路径（不含 query）
		std::string remote;                // 客户端 ip:port
	};

	// WebSocket 连接抽象（仿 Qt QWebSocket）
	class MtWsConnection
	{
	public:
		struct Events
		{
			std::function<void(const std::string& msg)> onTextMessageReceived;                       // 仿 textMessageReceived
			std::function<void(const uint8_t* data, size_t len)> onBinaryMessageReceived;            // 仿 binaryMessageReceived
			std::function<void()> onDisconnected;                                                    // 仿 disconnected
		};

		Events& events() { return _events; }

		// 异步发送：入队后由事件线程发送，可跨线程调用；返回 true 仅表示入队成功
		bool sendTextMessage(const std::string& msg);                 // 仿 sendTextMessage
		bool sendBinaryMessage(const void* data, size_t len);         // 仿 sendBinaryMessage
		void close();                                                 // 仿 close()：发 CLOSE 帧优雅关闭
		bool isOpen() const;                                          // 仿 state() == QAbstractSocket::ConnectedState
		uint64_t id() const { return _conn_id; }
		std::string path() const { return _state->path; }
		std::string peerAddress() const { return _state->remote; }    // ip:port

	private:
		friend class MtWsServer;
		MtWsConnection(MtWsServer* server, unsigned long conn_id)
			: _server(server), _conn_id(conn_id), _state(std::make_shared<MtWsConnState>()) {}

		MtWsServer* _server;                       // 用于入队发送（server 生命周期长于所有连接）
		unsigned long _conn_id;                    // mongoose 连接 id（单调递增，不复用）
		std::shared_ptr<MtWsConnState> _state;     // 跨线程安全状态
		Events _events;
	};

	// 基于 mongoose 的异步 WebSocket 服务端（仿 Qt QWebSocketServer）
	class MtWsServer
	{
	public:
		MtWsServer();
		~MtWsServer();   // 自动 stop()

		struct Events
		{
			// 新连接建立（事件线程执行；conn 指针在 onDisconnected 回调之前有效）
			std::function<void(MtWsConnection*)> onNewConnection;
			// 连接关闭通知（事件线程执行，conn 指针在回调内有效）
			std::function<void(MtWsConnection*)> onConnectionClosed;
		};
		Events& events() { return _events; }

		// 监听 host:port（host 如 "0.0.0.0"/"127.0.0.1"，为空则默认 0.0.0.0）
		bool listen(const char* host, int port);
		// 停止服务：join 事件线程并等待线程池任务完成（勿在事件回调内调用，否则请投递到业务线程）
		void stop();
		bool isListening() const { return _listening.load(); }
		std::size_t connectionCount() const { return _conn_count.load(); }

		// 心跳：每 intervalMs 发 PING，超过 timeoutMs 未收到 PONG 则断开该连接。
		// 参数 <= 0 表示关闭心跳（默认关闭）
		void setHeartbeat(int intervalMs, int timeoutMs);

	private:
		friend void wsServerFn(struct mg_connection* c, int ev, void* ev_data);
		friend void wsTimerFunc(void* arg);
		friend class MtWsConnection;

		struct Conn
		{
			struct mg_connection* c = nullptr;
			std::shared_ptr<MtWsConnection> ws;
		};
		// 发送队列 op 标记：正常为 WEBSOCKET_OP_*，kOpClose 表示主动关闭连接
		static constexpr int kOpClose = WEBSOCKET_OP_CLOSE + 0x10;
		struct SendItem
		{
			unsigned long conn_id;
			int op;
			std::string payload;
		};

		void onWsOpen(struct mg_connection* c, struct mg_http_message* hm);
		void onWsMsg(struct mg_connection* c, struct mg_ws_message* wm);
		void onClose(struct mg_connection* c);
		void flushSendQueue();
		void checkHeartbeat();
		bool enqueueSend(unsigned long conn_id, int op, std::string payload);
		void enqueueClose(unsigned long conn_id);

	private:
		std::atomic<bool> _listening{ false };
		std::atomic<bool> _stop{ false };
		std::atomic<std::thread::id> _event_td_id;
		std::unique_ptr<std::thread> _worker_thread;

		// 在途任务计数：投递前 +1、任务结束 -1；stop() 等待归零（全局单例池不能整体 purge/reset）
		std::atomic<int> _pending_tasks{ 0 };

		struct mg_mgr _mgr;
		struct mg_connection* _listen_conn = nullptr;
		std::atomic<std::size_t> _conn_count{ 0 };
		int _listen_port = 0;

		// 连接表（仅事件线程访问）
		std::map<unsigned long, Conn> _conns;
		// 心跳：conn_id -> 最近一次收到 PONG 的时刻（仅事件线程访问）
		std::map<unsigned long, uint64_t> _pong_time;
		int _heartbeat_interval = 0;
		int _heartbeat_timeout = 0;

		// 发送/关闭队列：业务线程写、事件线程消费
		mutable std::mutex _send_mtx;
		std::deque<SendItem> _pending_sends;

		Events _events;
	};
}