#pragma once
/*****
* @brief: 基于 mongoose 的异步 websocket 客户端
* @auth:  hxf
* @date:  2025-3-25
*
* 1. 基于回调的使用方式
*   MtWsClient wsClient;
*   wsClient.events().onConn = [] {
*       std::cout << "conn ok" << std::endl;
*   };
*   wsClient.events().onError = [](const char* err_msg) {
*       std::cout << "conn error: " << err_msg << std::endl;
*   };
*   wsClient.events().onDisconn = [] {
*       std::cout << "disconn" << std::endl;
*   };
*   wsClient.events().onReceiveMsg = [](const char* msg, int len) {
*       std::cout.write(msg, len);   // msg 不保证以 '\0' 结尾，须用长度
*       std::cout << std::endl;
*   };
*
*   wsClient.openWs("ws://127.0.0.1:8080/ws");   // 异步连接
*   wsClient.sendMsg("hello", 5);                // 异步发送：入队即返回，可跨线程调用
*   wsClient.stopWs();                           // 阻塞至事件线程退出（勿在回调内调用）
*
* 2. 基于继承的使用方式：定义宏 WS_USE_OVERRIDE 并实现四个纯虚函数
*   #define WS_USE_OVERRIDE
*
* 说明：
* - 支持 ws:// 与 wss://（wss 自动跳过证书校验）
* - 所有回调均在事件线程执行：内部勿做耗时操作；
*   不要在回调内直接调用 openWs 重连（会 join 自身导致死锁，openWs 返回 false），
*   请投递到业务线程（如 QTimer::singleShot / 线程池）后再重连
*   @WS_USE_OPENSSL
#ifndef MG_TLS
#define MG_TLS MG_TLS_OPENSSL
#endif
*/
#include "mongoose.h"
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>


namespace mtNet {
	class MtWsClient
	{
	public:
		MtWsClient();
		virtual ~MtWsClient();

		struct wsEvents {

			using wsConnHandler = std::function<void()>;
			using wsErrorHandler = std::function<void(const char* err_msg)>;
			using wsDisconnHandler = std::function<void()>;
			using wsReceiveMsgHandler = std::function<void(const char* err_msg, int len)>;
			
			wsConnHandler		onConn;
			wsErrorHandler		onError;
			wsDisconnHandler    onDisconn;
			wsReceiveMsgHandler onReceiveMsg;
		};

		/*
		* url: ws/wss 地址（wss 自动跳过证书校验）
		* timeout: DNS 解析超时时间(ms)
		* heart_time: 心跳间隔(ms)，连续 3 个周期未收到任何数据/PONG 判定断线
		* 注意：不要在事件回调(onConn/onError/onDisconn/onReceiveMsg)内直接调用 openWs 重连，
		* 事件线程内重连会 join 自身导致死锁（openWs 会返回 false）。如需重连请投递到业务线程。
		*/
		bool openWs(const char* url, int timeout = 5000, int heart_time = 8000);
		// 异步发送：消息入队后由事件线程统一发送；返回 true 仅表示入队成功
		bool sendMsg(const char* msg, int len);
		// 停止连接。从业务线程调用会等待事件线程退出；从事件回调内调用仅置停止标志，由事件线程自行收尾
		void stopWs();
		bool isConn();
		wsEvents& events() { return _events; }
	protected:
#ifdef  WS_USE_OVERRIDE
		virtual void onConn() = 0;
		virtual void onError(const char* err_msg) = 0;
		virtual void onDisconn() = 0;
		virtual void onReceiveMsg(const char* msg,int len)=0;
#endif
	private:
		friend void wsFn(struct mg_connection* c, int ev, void* ev_data);
		friend void timer_fn(void* arg);
		void freeWs();
		void flushSendQueue(struct mg_connection* c);
		void notifyConn();
		void notifyError(const char* err_msg);
		void notifyDisconn();
		void notifyReceiveMsg(const char* msg, int len);
	private:
		std::atomic<bool>   _init_ws{ false };      // 已初始化（openWs 成功），防止重复启动/双重释放
		std::atomic<bool>   _is_stop{ false };      // 停止标志
		std::atomic<bool>   _ws_opened{ false };    // WS 握手已完成，isConn() 依据
		std::unique_ptr<std::thread> _worker_thread;
		std::atomic<std::thread::id> _worker_td_id; // 事件线程 id（自 join 防护）
		wsEvents _events;

		// 以下成员仅在事件线程内访问（openWs 在启动事件线程前赋值/失败清理）
		mg_mgr _mgr;
		mg_connection* _mg_conn = nullptr;
		mg_timer* _heart_timer = nullptr;
		int _heart_count = 0;                       // 连续无数据/PONG 的心跳周期数

		// 发送队列：业务线程写（sendMsg）、事件线程消费（MG_EV_POLL）
		std::mutex _send_mtx;
		std::deque<std::string> _send_queue;
	};
};
