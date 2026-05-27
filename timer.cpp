/* timer.cpp
 * 
 * author: GaleInk
 * date: 2026/05/21
 */

#include "include/timer.h"
#include "include/http_conn.h"
#include "include/log.h"
#include <ctime>
#include <cstdio>

TimerList::TimerList()
{
    m_head = nullptr;
    m_tail = nullptr;
}

TimerList::~TimerList()
{
    // 节点是 WebServer::m_timer_nodes 数组的元素，不在此 delete
    // 链表只是串起它们，不拥有内存
}

void TimerList::add_to_tail(TimerNode* node)
{
    if (!m_tail) {
        m_head = m_tail = node;
        return ;
    }
    m_tail->next = node;
    node->prev = m_tail;
    node->next = nullptr;
    m_tail = node;
}

void TimerList::add_timer(TimerNode* node)
{
    node->expire = time(nullptr) + TIMEOUT_SEC;
    add_to_tail(node); //新连接直接放在末尾，表示对其加了闹钟（10分钟闹钟？）
}

void TimerList::adjust_timer(TimerNode* node)
{
    // 有活动，从当前位置取下，放到末尾，重新计时
    if (node == m_tail) return ;
    if (node == m_head) {
        m_head = node->next;
        m_head->prev = nullptr;
    } else {
        // middle
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    node->expire = time(nullptr) + TIMEOUT_SEC; //重置超时时间
    add_to_tail(node);
}

void TimerList::del_timer(TimerNode* node)
{
    if (node == m_head && node == m_tail) {
        m_head = m_tail = nullptr;
    }
    else if (node == m_head) {
        m_head = node->next;
        m_head->prev = nullptr;
    }
    else if (node == m_tail)                                    
    {                                                           
        m_tail = m_tail->prev;
        m_tail->next = nullptr;                                 
    }                                                           
    else                                                        
    {                                                           
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }
    // 不 delete — 节点是 WebServer 预分配数组的元素
}

void TimerList::tick()
{                                                               
    time_t now = time(nullptr);                                 
    while (m_head && m_head->expire <= now)                     
    {                    
        // 已经超时，去掉这个节点                                       
        TimerNode* tmp = m_head;                                
        m_head = m_head->next;                                  
        if (m_head) m_head->prev = nullptr;                     
                                                                
          // 如果连接还没关（真的超时了），关它                                                
          if (tmp->conn && tmp->conn->get_sockfd() != -1)                                      
          {                                                                                    
              LOG_WARN("[TIMER] fd=%d timeout, closing", tmp->conn->get_sockfd());
              tmp->conn->close_conn();
          }
        // 不 delete — 节点是 WebServer 预分配数组的元素
    }                                                           
    if (!m_head) m_tail = nullptr;                              
} 

int TimerList::get_next_timeout() const
{                                                               
    if (!m_head) return -1;  // 没有计时器，无限等
                                                                
    time_t now = time(nullptr);                                 
    int diff = (m_head->expire - now) * 1000;  // 秒 → 毫秒     
    return diff > 0 ? diff : 0;                                 
}   