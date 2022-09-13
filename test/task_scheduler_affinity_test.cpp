/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES

#include <thread>

#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/then.hpp>
#include <unifex/stop_if_requested.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/just.hpp>
#include <unifex/let_done.hpp>

#include <gtest/gtest.h>

using namespace unifex;

namespace {
struct TaskSchedulerAffinityTest : testing::Test {
  single_thread_context thread_ctx;
};

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::pair<std::thread::id, std::thread::id>> child(Scheduler s) {
  auto that_id =
      co_await then(schedule(s), []{ return std::this_thread::get_id(); });
  // Should have automatically transitioned back to the original thread:
  auto this_id = std::this_thread::get_id();
  co_return std::make_pair(this_id, that_id);
}

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::thread::id> inner(Scheduler s) {
  // Transition to the scheduler's context:
  co_await schedule(s);
  // Should return the new context
  co_return std::this_thread::get_id();
}

UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::pair<std::thread::id, std::thread::id>> outer(Scheduler s) {
  // Call a nested coroutine that transitions context:
  auto that_id = co_await inner(s);
  // Should have automatically transitioned back to the correct context
  auto this_id = std::this_thread::get_id();
  co_return std::make_pair(this_id, that_id);
}

// Test that after a co_await schedule(), the coroutine's current
// scheduler has changed:
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<bool> test_current_scheduler(Scheduler s) {
  auto before = co_await current_scheduler();
  co_await schedule(s);
  auto after = co_await current_scheduler();
  co_return before != after;
}

// Test that after a co_await schedule(), the coroutine's current
// scheduler is inherited by child tasks:
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::pair<bool, std::thread::id>> test_current_scheduler_is_inherited_impl(Scheduler s) {
  any_scheduler s2 = co_await current_scheduler();
  bool sameScheduler = (s2 == s);
  co_return std::make_pair(sameScheduler, std::this_thread::get_id());
}
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::pair<bool, std::thread::id>> test_current_scheduler_is_inherited(Scheduler s) {
  co_await schedule(s);
  co_return co_await test_current_scheduler_is_inherited_impl(s);
}

// Test that we properly transition back to the right context when
// the task is cancelled.
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<void> test_context_restored_on_cancel_2(Scheduler s) {
  co_await schedule(s);
  co_await stop();
  ADD_FAILURE() << "Coroutine did not stop!";
  co_return;
}
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::thread::id> test_context_restored_on_cancel(Scheduler s) {
  // swallow the cancellation signal:
  (void) co_await let_done(
      test_context_restored_on_cancel_2(s),
      []() noexcept { return just(); });
  co_return std::this_thread::get_id();
}

// Test that we properly transition back to the right context when
// the task fails.
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<void> test_context_restored_on_error_2(Scheduler s) {
  co_await schedule(s);
  throw std::runtime_error("whoops");
}
UNIFEX_TEMPLATE(typename Scheduler)
  (requires scheduler<Scheduler>)
task<std::thread::id> test_context_restored_on_error(Scheduler s) {
  std::thread::id id;
  // swallow the cancellation signal:
  try {
    co_await test_context_restored_on_error_2(s);
    ADD_FAILURE() << "Was expecting a throw";
  } catch(...) {
    id = std::this_thread::get_id();
  }
  co_return id;
}
} // anonymous namespace

TEST_F(TaskSchedulerAffinityTest, TransformSenderOnSeparateThread) {
  if(auto opt = sync_wait(child(thread_ctx.get_scheduler()))) {
    auto [this_id, that_id] = *opt;
    ASSERT_EQ(this_id, std::this_thread::get_id());
    ASSERT_EQ(that_id, thread_ctx.get_thread_id());
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

TEST_F(TaskSchedulerAffinityTest, InlineThreadHopInCoroutine) {
  if(auto opt = sync_wait(outer(thread_ctx.get_scheduler()))) {
    auto [this_id, that_id] = *opt;
    ASSERT_EQ(this_id, std::this_thread::get_id());
    ASSERT_EQ(that_id, thread_ctx.get_thread_id());
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

TEST_F(TaskSchedulerAffinityTest, CurrentSchedulerTest) {
  if(auto opt = sync_wait(test_current_scheduler(thread_ctx.get_scheduler()))) {
    ASSERT_TRUE(opt.has_value());
    EXPECT_TRUE(*opt);
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

TEST_F(TaskSchedulerAffinityTest, CurrentSchedulerIsInheritedTest) {
  if(auto opt = sync_wait(test_current_scheduler_is_inherited(thread_ctx.get_scheduler()))) {
    ASSERT_TRUE(opt.has_value());
    auto [success, thread_id] = *opt;
    EXPECT_TRUE(success);
    EXPECT_EQ(thread_id, thread_ctx.get_thread_id());
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

TEST_F(TaskSchedulerAffinityTest, ContextRestoredOnCancelTest) {
  if(auto opt = sync_wait(test_context_restored_on_cancel(thread_ctx.get_scheduler()))) {
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, std::this_thread::get_id());
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

TEST_F(TaskSchedulerAffinityTest, ContextRestoredOnErrrorTest) {
  if(auto opt = sync_wait(test_context_restored_on_error(thread_ctx.get_scheduler()))) {
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, std::this_thread::get_id());
  } else {
    ADD_FAILURE() << "child coroutine completed unexpectedly";
  }
}

#endif // !UNIFEX_NO_COROUTINES