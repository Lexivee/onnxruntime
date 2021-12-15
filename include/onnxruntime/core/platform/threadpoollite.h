// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "threadpool.h"
#include "core/platform/EigenNonBlockingThreadPool.h"
#include <thread>
#include <atomic>

#define MAX_NUM_TASK 8

namespace onnxruntime {

namespace concurrency {

using Fn = std::function<void(std::ptrdiff_t, std::ptrdiff_t)>;
using SimpleFn = std::function<void(std::ptrdiff_t)>;
using SchdFn = std::function<void()>;

#if defined(_MSC_VER)
#pragma warning(disable : 4324)
#endif

class ThreadPoolLite : public ThreadPool {
 public:
  ThreadPoolLite(Env*,
                 const ThreadOptions&,
                 const NAME_CHAR_TYPE*,
                 int num_threads,
                 bool);
  ORT_DISALLOW_COPY_AND_ASSIGNMENT(ThreadPoolLite);
  ~ThreadPoolLite();

  int NumThreads() const override { return static_cast<int>(sub_threads_.size()); }
  void ParallelFor(std::ptrdiff_t, double, const Fn&) override;
  void ParallelFor(std::ptrdiff_t, const TensorOpCost&, const Fn&) override;
  void SimpleParallelFor(std::ptrdiff_t, const SimpleFn&) override;
  void Schedule(SchdFn) override;
  void StartProfiling() override;
  std::string StopProfiling() override;

 private:
  void ParallelForImpl(const SchdFn&, std::ptrdiff_t);
  void ThreadLoop(int);

  enum Stage {
    empty = 0,
    loading,
    ready,
    running,
    done
  };

  struct Slot {
    ORT_ALIGN_TO_AVOID_FALSE_SHARING std::atomic<Stage> stage_{empty};
    char place_holder_1[64];
    ORT_ALIGN_TO_AVOID_FALSE_SHARING SchdFn schd_fn_;
    char place_holder_2[64];
    Slot() {}
    Slot(const Slot& slot) {
      stage_.store(slot.stage_, std::memory_order_relaxed);
      schd_fn_ = slot.schd_fn_;
    }
    Slot& operator=(const Slot& slot) {
      if (this == &slot) {
        return *this;
      }
      stage_.store(slot.stage_, std::memory_order_relaxed);
      schd_fn_ = slot.schd_fn_;
      return *this;
    }
  };

  std::vector<Slot> slots_;
  std::vector<std::thread> sub_threads_;
  ThreadPoolProfiler profiler_;
  int num_sub_threads_{0};
  bool set_denormal_as_zero_{false};
  bool exit_ = false;
};

////////////////////////////////////////////////////////////////////////////////////

using integer = std::ptrdiff_t;
using func = std::function<void(integer, integer)>;

struct node {
  func fn_;
  integer block_size_;
  ORT_ALIGN_TO_AVOID_FALSE_SHARING std::atomic<integer> available_{};
  ORT_ALIGN_TO_AVOID_FALSE_SHARING std::atomic<integer> done_{};
  std::atomic<bool> empty_{true};
};

enum class state : int8_t {
  free = 0,
  occupied,
};

struct landmark {
  integer at_{};
  state state_{};
};

struct ret {
  integer at_{-1};
  integer from_{};
  integer to_{};
};

template <size_t size>
struct mpmcqueue final {
  ret pop();

  ret pop_at(integer at);

  integer push(func fn, integer available, integer block_size = 1);

  inline integer next(integer at) const { return (at + 1) % size; }
  ORT_ALIGN_TO_AVOID_FALSE_SHARING node nodes_[size];
  ORT_ALIGN_TO_AVOID_FALSE_SHARING std::atomic<integer> front_{};
  ORT_ALIGN_TO_AVOID_FALSE_SHARING std::atomic<landmark> back_{};
};  //mpmcqueue

class ThreadPoolLiteII : public ThreadPool {
 public:
  ThreadPoolLiteII(Env*,
                 const ThreadOptions&,
                 const NAME_CHAR_TYPE*,
                 int num_threads,
                 bool);
  ORT_DISALLOW_COPY_AND_ASSIGNMENT(ThreadPoolLiteII);
  ~ThreadPoolLiteII();

  int NumThreads() const override { return static_cast<int>(sub_threads_.size()); }
  void ParallelFor(std::ptrdiff_t, double, const Fn&) override;
  void ParallelFor(std::ptrdiff_t, const TensorOpCost&, const Fn&) override;
  void SimpleParallelFor(std::ptrdiff_t, const SimpleFn&) override;
  void Schedule(SchdFn) override;
  void StartProfiling() override;
  std::string StopProfiling() override;

 private:
  std::vector<std::thread> sub_threads_;
  int num_sub_threads_{0};
  bool set_denormal_as_zero_{false};
  bool exit_ = false;
  mpmcqueue<1024> que_{};
};

}  // namespace concurrency
}  // namespace onnxruntime