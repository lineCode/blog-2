#pragma once

#include <vector>
#include <thread>
#include <memory>
#include <future>
#include <functional>
#include <type_traits>
#include <cassert>
#include "queue.h"

class simple_thread_pool
{
public:
	simple_thread_pool(
		unsigned int threads = std::thread::hardware_concurrency(),
		unsigned int queueDepth = std::thread::hardware_concurrency())
	: m_queue(queueDepth)
	{
		assert(queueDepth != 0);
		assert(threads != 0);
		for(unsigned int i = 0; i < threads; ++i)
			m_threads.emplace_back(std::thread([this]() {
				while(true)
				{
					auto workItem = m_queue.pop();
					if(workItem == nullptr)
					{
						m_queue.push(nullptr);
						break;
					}
					workItem();
				}
			}));
	}

	~simple_thread_pool() noexcept
	{
		m_queue.push(nullptr);
		for(auto& thread : m_threads)
			thread.join();
	}

	using Proc = std::function<void(void)>;

	template<typename F, typename... Args>
	void enqueue_work(F&& f, Args&&... args) noexcept(std::is_nothrow_invocable<decltype(&blocking_queue<Proc>::push<Proc&&>)>::value)
	{
		m_queue.push([=]() { f(args...); });
	}

	template<typename F, typename... Args>
	auto enqueue_task(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
	{
		using return_type = typename std::result_of<F(Args...)>::type;
		auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<return_type> res = task->get_future();
		m_queue.push([task](){ (*task)(); });
		return res;
	}

private:
	using Threads = std::vector<std::thread>;
	Threads m_threads;
	blocking_queue<Proc> m_queue;
};





class thread_pool
{
public:
	thread_pool(unsigned int threads = std::thread::hardware_concurrency())
	: m_queues(threads), m_count(threads)
	{
		assert(threads != 0);
		auto worker = [&](unsigned int i)
		{
			while(true)
			{
				Proc f;
				for(unsigned int n = 0; n < m_count; ++n)
					if(m_queues[(i + n) % m_count].try_pop(f)) break;
				if(!f) m_queues[i].pop(f);
				bool sentinel = f();
				if(sentinel) break;
			}
		};
		for(unsigned int i = 0; i < threads; ++i)
			m_threads.emplace_back(worker, i);
	}

	~thread_pool() noexcept
	{
		for(auto& queue : m_queues)
			queue.push([]() { return true; });
		for(auto& thread : m_threads)
			thread.join();
	}

	template<typename F, typename... Args>
	void enqueue_work(F&& f, Args&&... args)
	{
		auto work = [f,args...]() { f(args...); return false; };
		unsigned int i = m_index++;
		for(unsigned int n = 0; n < m_count; ++n)
			if(m_queues[(i + n) % m_count].try_push(work)) return;
		m_queues[i % m_count].push(work);
	}

	template<typename F, typename... Args>
	auto enqueue_task(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
	{
		using return_type = typename std::result_of<F(Args...)>::type;
		auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<return_type> res = task->get_future();

		unsigned int i = m_index++;
		auto work = [task](){ (*task)(); return false; };
		for(unsigned int n = 0; n < m_count; ++n)
			if(m_queues[(i + n) % m_count].try_push(work)) return res;
		m_queues[i % m_count].push(work);

		return res;
	}

private:
	using Proc = std::function<bool(void)>;
	using Queues = std::vector<simple_blocking_queue<Proc>>;
	Queues m_queues;

	using Threads = std::vector<std::thread>;
	Threads m_threads;

	unsigned int m_count;
	unsigned int m_index = 0;
};
