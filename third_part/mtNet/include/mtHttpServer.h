#pragma once
/*****
 * @brief:基于mongoose实现的httpserver封装类
 * @使用例子
 * mtSer_->addRoute("GET", "/api/testAsync",
                     [](const mtNet::HttpRequest& req, const mtNet::HttpSender& res) {
        json j;
        j["code"] = 0;
        j["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz").toStdString();
        res.reply(j.dump());
    });
    mtSer_->addRoute("POST", "/api/testAsync",
                     [=](const mtNet::HttpRequest& req, const mtNet::HttpSender& res) {
        json j;
        j["code"] = 0;
        j["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz").toStdString();
        res.reply(j.dump());
    });
    // start(host, timeoutMs)：timeoutMs 为异步请求超时
    std::string host = "127.0.0.1:" + std::to_string(port);
    if (!mtSer_->start(host.c_str(), 6000)) {
        mtSer_.reset();
        return;
    }
*/
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mongoose.h"
#include <mtPool/MtPool.h>


namespace mtNet
{
	class MtHttpServer;

	// HTTP 请求封装：在事件线程内构建并整体拷贝，跨线程安全（只读）
	struct HttpRequest
	{
		std::string method;                                  // 请求方法，如 GET/POST
		std::string path;                                    // 不含 query 的路径，如 /api/queryUser
		std::string query_string;                            // 原始 query 串（不含 '?'）
		std::map<std::string, std::string> query;            // 解析后的 query 参数（已 URL 解码）
		std::map<std::string, std::string> headers;          // 请求头
		std::string body;                                    // 请求体
		std::string remote;                                  // 客户端 ip:port
	};

	// 响应发送器：线程安全，可在任意线程（含线程池）调用
	class HttpSender
	{
	public:
		HttpSender() = default;

		// 异步回复。返回 true 表示已入队；若服务器已停止/连接已关闭/请求已超时则返回 false
		// 注：handler 以 const HttpSender& 传入，因此 reply 需为 const
		bool reply(int status, const std::string& body, const std::string& headers = "") const;
		bool reply(const std::string& body) const { return reply(200, body); }

	private:
		friend class MtHttpServer;
		MtHttpServer* _server = nullptr;
		unsigned long _conn_id = 0;
	};

	// 基于 mongoose 的异步 HTTP 服务器
	class MtHttpServer
	{
	public:
		MtHttpServer();
		~MtHttpServer();

		using Handler = std::function<void(const HttpRequest&, const HttpSender&)>;

		// 注册路由：method + path（不含 query）精确匹配。需在 start() 之前调用。
		bool addRoute(const std::string& method, const std::string& path, Handler handler);

		// 启动服务。timeoutMs 为异步请求超时；handler 在全局单例线程池（mtPool）执行
		bool start(const char* host, int timeoutMs = 5000);
		// 停止服务（建议在非事件线程调用）
		void stop();

		// 当前 in-flight 请求数
		std::size_t inflightCount() const;

		// 错误通知回调：服务器内部自动兜底处理（如请求超时自动回 504）时通知上层。
		// 参数依次为：conn_id（连接标识）、status（状态码）、method、path（请求方法/路径，可能为空）
		using ErrorCallback = std::function<void(unsigned long conn_id, int status,
			const std::string& method, const std::string& path)>;
		// 注入错误回调。可在任意线程调用；触发时在事件线程执行（已拷贝，不持内部锁）
		void setErrorCallback(ErrorCallback cb);

	private:
		friend void mtHttpFunc(struct mg_connection* c, int ev, void* ev_data);
		friend void mtTimerFunc(void* arg);
		friend class HttpSender;

		struct Route
		{
			std::string method;
			std::string path;
			Handler handler;
		};

		// 待回复项：工作线程产出、事件线程消费
		struct ReplyItem
		{
			unsigned long conn_id = 0;
			int status = 200;
			std::string headers;
			std::string body;
		};

		// 异步请求状态：事件线程维护
		struct Inflight
		{
			std::chrono::steady_clock::time_point deadline;  // 超时时刻
			bool replied = false;                            // 已回复/已超时（防重复回复）
			std::string method;                              // 请求方法（供错误回调）
			std::string path;                                // 请求路径（供错误回调）
		};

		void onAccept(struct mg_connection* c);
		void onHttpMsg(struct mg_connection* c, struct mg_http_message* hm);
		void onClose(struct mg_connection* c);
		void flushReplies();
		void checkTimeout();
		bool enqueueReply(unsigned long conn_id, int status,
			const std::string& headers, const std::string& body);

	private:
		static constexpr std::size_t kMaxInflight = 1024;  // 最大并发异步请求数

		std::atomic<bool> _started{ false };
		std::atomic<bool> _stop{ false };
		std::atomic<std::thread::id> _event_td_id;
		std::unique_ptr<std::thread> _worker_thread;

		// 在途任务计数：投递前 +1、任务结束 -1；stop() 等待归零（全局单例池不能整体 purge/reset）
		std::atomic<int> _pending_tasks{ 0 };

		struct mg_mgr _mgr;
		int _timeout_ms = 5000;

		// 路由表：注册后事件线程只读
		mutable std::mutex _routes_mtx;
		std::vector<Route> _routes;

		// 连接表 conn_id -> connection（仅事件线程访问）
		std::map<unsigned long, struct mg_connection*> _conns;

		// 异步 in-flight 表 conn_id -> 状态（事件线程 + 停止线程访问）
		mutable std::mutex _inflight_mtx;
		std::map<unsigned long, Inflight> _inflight;

		// 错误回调（任意线程设置，事件线程拷贝后调用）
		mutable std::mutex _error_mtx;
		ErrorCallback _error_cb;

		// 待回复队列（工作线程写、事件线程读）
		mutable std::mutex _reply_mtx;
		std::deque<ReplyItem> _pending_replies;
	};
}


