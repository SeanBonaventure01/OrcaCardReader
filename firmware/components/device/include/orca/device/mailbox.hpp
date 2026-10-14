#pragma once

// A FreeRTOS queue of owned C++ objects. FreeRTOS queues copy raw bytes, so we queue
// pointers and transfer ownership: post() never blocks (drops when full) and take() waits.

#include <memory>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace orca::device {

template <typename T>
class Mailbox {
 public:
  explicit Mailbox(UBaseType_t depth) : q_(xQueueCreate(depth, sizeof(T*))) {}
  ~Mailbox() {
    T* p;
    while (xQueueReceive(q_, &p, 0) == pdTRUE) delete p;
    vQueueDelete(q_);
  }
  Mailbox(const Mailbox&) = delete;
  Mailbox& operator=(const Mailbox&) = delete;

  // Returns false (and drops the item) if the mailbox is full.
  bool post(T value, TickType_t wait = 0) {
    T* p = new T(std::move(value));
    if (xQueueSend(q_, &p, wait) != pdTRUE) {
      delete p;
      return false;
    }
    return true;
  }

  std::unique_ptr<T> take(TickType_t wait) {
    T* p = nullptr;
    if (xQueueReceive(q_, &p, wait) != pdTRUE) return nullptr;
    return std::unique_ptr<T>(p);
  }

 private:
  QueueHandle_t q_;
};

}  // namespace orca::device
