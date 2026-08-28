#ifndef CCQUEUE_H
#define CCQUEUE_H

#include <list>
#include <mutex>
#include <cstddef>

template <class T>
class CCMediaQueue : public std::list<T>
{
public:
    CCMediaQueue() = default;
    ~CCMediaQueue() = default;

    void enqueue(const T& t)
    {
        m_mutex.lock();
        std::list<T>::push_back(t);
        m_mutex.unlock();
    }

    T dequeue()
    {
        m_mutex.lock();
        T t = nullptr;
        if (!std::list<T>::empty())
        {
            t = std::list<T>::front();
            std::list<T>::pop_front();
        }
        m_mutex.unlock();
        return t;
    }

    bool isEmpty() const
    {
        m_mutex.lock();
        bool b = std::list<T>::empty();
        m_mutex.unlock();
        return b;
    }

private:
    mutable std::mutex m_mutex;
};

#endif // CCQUEUE_H
