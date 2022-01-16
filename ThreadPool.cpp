#include <iostream>
#include <queue>
#include <future>
#include <condition_variable>
#include <vector>
#include <mutex>
using namespace std;

typedef function<void()> task_type;
mutex coutLocker;

template<typename T>
class BlockedQueue
{
private:
    mutex m_locker;
    // очередь задач
    deque<T> m_task_queue;    

public:
    void m_push(T& item)
    {
        lock_guard<mutex> l(m_locker);
        // обычный потокобезопасный push
        m_task_queue.push_back(item);        
    }

    bool isEmpty()
    {
        return m_task_queue.empty();
    }
    
    bool fast_pop(T& item)
    {
        lock_guard<mutex> l(m_locker);
        if (m_task_queue.empty())
            // просто выходим
            return false;
        // забираем элемент
        item = m_task_queue.front();
        m_task_queue.pop_front();
        return true;
    }

    bool fast_pop_LIFO(T& item) {
        lock_guard<mutex> l(m_locker);
        if (m_task_queue.empty())
            // просто выходим
            return false;
        item = m_task_queue.back();
        m_task_queue.pop_back();
        return true;
    }

};

class ThreadPool
{
public:
    ThreadPool();
    // запуск:
    void start();
    // остановка:
    void stop();
    // проброс задач
    template<class Func, class... Arguments>void push_task(Func f, Arguments... args);
    // функция входа для потока
    void threadFunc(int qindex);
private:
    // количество потоков
    int m_thread_count;
    // потоки
    vector<thread> m_threads;
    // очереди задач для потоков
    vector<BlockedQueue<task_type>> m_thread_queues;
    // для равномерного распределения задач
    int m_index = 0;
    deque<task_type>m_global;
    mutex global;
    bool m_flag;
};

ThreadPool::ThreadPool() :
    m_thread_count(thread::hardware_concurrency() != 0 ? thread::hardware_concurrency() : 4),
    m_thread_queues(m_thread_count) {}

void ThreadPool::start()
{
    for (int i = 0; i < m_thread_count; i++)
    {        
        m_flag = true;
        m_threads.push_back(thread(&ThreadPool::threadFunc, this, i));
    }
}

void ThreadPool::stop()
{
    m_flag = false;    
    for (auto& t : m_threads)
    {
        t.join();
    }
}

template<class Func, class... Arguments>
void ThreadPool::push_task(Func f, Arguments... args)
{
    // вычисляем индекс очереди, куда положим задачу
    int queue_to_push = m_index++ % (m_thread_count + 1);
    // формируем функтор  
    task_type task ([=]{ f(args...); });
    // кладем в глобальную очередь   
    if (queue_to_push == m_thread_count)
    {
        lock_guard<mutex>l(global);
        m_global.push_back(task);
    }//кладем в локальную
    else m_thread_queues[queue_to_push].m_push(task);
}

void ThreadPool::threadFunc(int qindex)
{
    while (true)
    {
        task_type task_to_do;
        bool res;
        // Local LIFO
        if (res = m_thread_queues[(qindex) % m_thread_count].fast_pop_LIFO(task_to_do))
        {           
           task_to_do();            
        }
        else if (m_thread_queues[(qindex) % m_thread_count].isEmpty() && !m_flag)
            return;
         //Global FIFO
        {
            lock_guard<mutex>l(global);
           
            if (!m_global.empty())
            {
                task_to_do = m_global.front();
                m_global.pop_front();
                task_to_do();
            }
            else if (m_thread_queues[(qindex) % m_thread_count].isEmpty() && m_global.empty() && !m_flag)
                return;   
                      
        }
        // Stealing FIFO       
            for (int i = 1; i < m_thread_count; i++)
        {
            if (res = m_thread_queues[(qindex + i) % m_thread_count].fast_pop(task_to_do))                                      
                    task_to_do();
                    break;      
        } 
    }     
}

class RequestHandler
{
public:
    RequestHandler();
    ~RequestHandler();
    // отправка запроса на выполнение
    template<class Func, class... Arguments>void pushRequest(Func f, Arguments... args);
private:
    // пул потоков
    ThreadPool m_tpool;
};

RequestHandler::RequestHandler()
{
    m_tpool.start();
}

RequestHandler::~RequestHandler()
{
    m_tpool.stop();    
}

template<class Func, class... Arguments>
void RequestHandler::pushRequest(Func f, Arguments... args) {
    m_tpool.push_task(f, args...);
}

//функция, выполняющая задачу
void taskFunc(int id, int delay) {
    // имитируем время выполнения задачи
    this_thread::sleep_for(chrono::seconds(delay));
    // выводим информацию о завершении    
    unique_lock<mutex> l(coutLocker);
    cout << "task " << id << " made by thread_id " << this_thread::get_id() << endl;
}

int main()
{
    srand(0);
    RequestHandler rh;
    for (int i = 0; i < 20; i++)
        rh.pushRequest(taskFunc, i, 1 + rand() % 4);
    return 0;
}

