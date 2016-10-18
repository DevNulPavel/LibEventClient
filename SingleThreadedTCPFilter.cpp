#include "SingleThreadedTCPFilter.h"
// std
#include <stdexcept>
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <cstdint>
#include <vector>
#include <memory>
#include <queue>
#include <string>
#include <list>
#include <unordered_map>
// other
#include <arpa/inet.h>
// libevent
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <event.h>
#include <evhttp.h>

// примеры
// https://habrahabr.ru/post/217437/
// http://incpp.blogspot.ru/2009/04/libevent.html
// https://www.ibm.com/developerworks/ru/library/l-Libevent1/
// работа с буфферами - http://www.wangafu.net/~nickm/libevent-book/Ref6_bufferevent.html
//                      http://www.wangafu.net/~nickm/libevent-book/Ref6a_advanced_bufferevents.html

//using namespace std;

typedef std::shared_ptr<event_base>  EventBasePtr;
typedef std::unique_ptr<evconnlistener, decltype(&evconnlistener_free)> ServerListenerPtr;  // указатель на сервер + функция, вызываемая при уничтожении
typedef std::unique_ptr<std::thread, std::function<void(std::thread*)>> ThreadPtr;  // указатель на поток + функция, вызываемая при уничтожении
typedef std::vector<ThreadPtr> ThreadPool;  // пулл потоков
typedef std::function<void()> Task;
typedef std::queue<Task> TasksQueue;
typedef std::lock_guard<std::mutex> LockGuard;
typedef std::unique_lock<std::mutex> UniqueLock;


//////////////////////////////////////////////////
// TCP Server
//////////////////////////////////////////////////
int singleThreadedTcpClientFilter() {
    std::uint16_t const serverPort = 5555;
    const char* serverAddress = "127.0.0.1";
    
    //////////////////////////////////////////////////
    // Setup
    //////////////////////////////////////////////////
    // каждый поток имеет свой объект обработки событий, в однопотоном варианте - это event_init
    EventBasePtr eventBase(event_base_new(), &event_base_free);
    if (!eventBase){
        std::cout << "Ошибка при создании объекта event_base." << std::endl;
        return 0;
    }
    
    // При обработке запроса нового соединения необходимо создать для него объект bufferevent
    bufferevent* buf_ev_classic = bufferevent_socket_new(eventBase.get(), -1,
                                                         BEV_OPT_CLOSE_ON_FREE /*| BEV_OPT_THREADSAFE | BEV_OPT_DEFER_CALLBACKS | BEV_OPT_UNLOCK_CALLBACKS*/);
    if (buf_ev_classic == nullptr) {
        std::cout << "Ошибка при создании объекта bufferevent." << std::endl;
        return 0;
    }
    
    // адрес
    sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;    /* работа с доменом IP-адресов */
    sin.sin_addr.s_addr = inet_addr(serverAddress);  // сервер
    sin.sin_port = htons(serverPort);               // порт
    
    // подключение к серверу
    int connectResult = bufferevent_socket_connect(buf_ev_classic, (sockaddr*)&sin, sizeof(sin));
    if (connectResult < 0) {
        std::cout << "Ошибка соединения с сервером." << std::endl;
        return 0;
    }
    
    // размер информации о размере
    typedef char DataSizeType;
    
    // обертка-фильтр
    auto inputFilter = [](evbuffer *src, evbuffer *dst, ev_ssize_t dst_limit, bufferevent_flush_mode mode, void *ctx)-> bufferevent_filter_result {
        // Читаем данные
        size_t receivedDataSize = evbuffer_get_length(src);
        
        // проверка на большие размеры буффера (10Mb)
        /*if (receivedDataSize < 1024 * 1024 * 10) {
         return bufferevent_filter_result::BEV_NEED_MORE;
         }*/
        
        // если мало данных о размере - ждем
        if (receivedDataSize < sizeof(DataSizeType)) {
            return bufferevent_filter_result::BEV_NEED_MORE;
        }
        
        int64_t dataSize = 0;
        evbuffer_copyout(src, &dataSize, sizeof(DataSizeType));
        
        // если мало данных в буффере - ждем еще
        if (receivedDataSize < (sizeof(DataSizeType) + dataSize)) {
            return bufferevent_filter_result::BEV_NEED_MORE;
        }
        
        // удаляем данные о размере из начала
        evbuffer_drain(src, sizeof(DataSizeType));
        // TODO: копируем в выходной буффер (или перемещаем?????)
        evbuffer_add_buffer(dst, src);
        
        return bufferevent_filter_result::BEV_OK;
    };
    
    // выходной фильтр
    auto outFilter = [](evbuffer *src, evbuffer *dst, ev_ssize_t dst_limit, bufferevent_flush_mode mode, void *ctx)-> bufferevent_filter_result {
        
        // добавление информации о размере в начало
        DataSizeType dataSize = evbuffer_get_length(src);
        // TODO: копируем в выходной буффер (или перемещаем?????)
        evbuffer_add(dst, &dataSize, sizeof(DataSizeType));
        evbuffer_add_buffer(dst, src);
        
        return bufferevent_filter_result::BEV_OK;
    };
    
    // разрушение контекста фильтра
    auto filterDestroyCallback = [](void*){
    };
    
    // TODO: уничтожение базового ивента
    bufferevent* buf_ev = bufferevent_filter_new(buf_ev_classic, inputFilter, outFilter, 0, filterDestroyCallback, nullptr);
    if (buf_ev_classic == nullptr) {
        std::cout << "Ошибка при создании ФИЛЬТРУЮЩЕГО объекта bufferevent." << std::endl;
        return 0;
    }
    
    // Функция обратного вызова для события: данные готовы для чтения в buf_ev
    auto echo_read_cb = [](bufferevent* buf_ev, void *arg) {        
        evbuffer* buf_input = bufferevent_get_input(buf_ev);
        size_t inputSize = evbuffer_get_length(buf_input);
        
        std::string answer;
        answer.resize(inputSize);
        evbuffer_copyout(buf_input, (void*)answer.data(), inputSize);
        
        evbuffer_drain(buf_input, inputSize);
        
        std::cout << answer << std::endl;

        evbuffer* buf_output = bufferevent_get_output(buf_ev);

        // читаем из консоли
        std::string input;
        std::cin >> input;

        // пишем в буффер
        evbuffer_add(buf_output, input.data(), input.size());

        // выводим
        int outSize = evbuffer_get_length(buf_output);
        evbuffer_drain(buf_output, outSize);
    };
    
    // Функция обратного вызова для события: данные готовы для записи в buf_ev
    auto echo_write_cb = [](bufferevent* buf_ev, void *arg) {
    };
    
    // коллбек обработки ивента
    auto echo_event_cb = [](bufferevent* buf_ev, short events, void *arg){
        if(events & BEV_EVENT_CONNECTED){
            std::cout << "Подсоединились к серверу" << std::endl;
            
            evbuffer* buf_output = bufferevent_get_output(buf_ev);
            
            // читаем из консоли
            std::string input;
            std::cin >> input;
            
            // пишем в буффер
            evbuffer_add(buf_output, input.data(), input.size());

            // выводим
            int outSize = evbuffer_get_length(buf_output);
            evbuffer_drain(buf_output, outSize);
        }
        if(events & BEV_EVENT_READING){
            std::cout << "Ошибка во время чтения bufferevent" << std::endl;
        }
        if(events & BEV_EVENT_WRITING){
            std::cout << "Ошибка во время записи bufferevent" << std::endl;
        }
        if(events & BEV_EVENT_ERROR){
            std::cout << "Ошибка объекта bufferevent" << std::endl;
        }
        if(events & BEV_EVENT_TIMEOUT){
            // пишем в буффер об долгом пинге
            //evbuffer* buf_output = bufferevent_get_output(buf_ev);
            //evbuffer_add_printf(buf_output, "Kick by timeout\n");
            // уничтожаем объект буффер
            if (buf_ev) {
                bufferevent_free(buf_ev);
                buf_ev = nullptr;
            }
            std::cout << "Таймаут bufferevent" << std::endl;
        }
        if(events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)){
            std::cout << "Завершение bufferevent" << std::endl;
            // уничтожаем объект буффер
            if (buf_ev) {
                bufferevent_free(buf_ev);
                buf_ev = nullptr;
            }
        }
    };
    
    // коллбеки обработи
    bufferevent_setcb(buf_ev, echo_read_cb, echo_write_cb, echo_event_cb, nullptr);
    bufferevent_enable(buf_ev, (EV_READ | EV_WRITE));
    
    // размеры буффера для вызова коллбеков
    //bufferevent_setwatermark(buf_ev, EV_READ, 2, 0);   // 2+
    //bufferevent_setwatermark(buf_ev, EV_WRITE, 2, 0);   // 20+
    
    // таймауты
    timeval readWriteTimeout;
    readWriteTimeout.tv_sec = 600;
    readWriteTimeout.tv_usec = 0;
    bufferevent_set_timeouts(buf_ev, &readWriteTimeout, &readWriteTimeout);
    
    // запуск (неблокирующий)
    // event_base_loop(eventBase.get(), EVLOOP_NONBLOCK);
    
    // запуск цикла - блокирующий
    event_base_dispatch(eventBase.get());
    
    std::cout << "Выход из цикла обработки" << std::endl;

    return 0;
}

