/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unifex/coroutine.hpp>

#if !UNIFEX_NO_COROUTINES
#  if !UNIFEX_NO_EXCEPTIONS
#    include <unifex/async_resource.hpp>

#    include "async_resource_test.hpp"

#    include <functional>
#    include <gtest/gtest.h>

#    include <unifex/sync_wait.hpp>
#    include <unifex/task.hpp>

using namespace unifex;
using namespace unifex_test;

namespace {
struct Spawning {
  task<void> operator()(AsyncResourceTest* f) const {
    (void)co_await make_async_resource(
        f->ctx.get_scheduler(), f->outerScope, [f](auto scope, auto scheduler) {
          // constructor throws after spawning
          return ThrowingSpawningResource{scope, scheduler, f};
        });
  }
};

struct SpawningSenderFactory {
  task<void> operator()(AsyncResourceTest* f) const {
    (void)co_await make_async_resource<
        ThrowingSpawningResource<decltype(f->ctx.get_scheduler())>>(
        f->ctx.get_scheduler(),
        f->outerScope,
        [f](auto scope, auto scheduler) noexcept {
          // constructor throws
          return just(scope, scheduler, f);
        });
  }
};

struct Throwing {
  task<void> operator()(AsyncResourceTest* f) const {
    (void)co_await make_async_resource(
        f->ctx.get_scheduler(), f->outerScope, [](auto, auto) {
          // constructor throws
          return ThrowingResource{};
        });
  }
};

struct SenderFactoryConstructor {
  task<void> operator()(AsyncResourceTest* f) const {
    (void)co_await make_async_resource<ThrowingResource>(
        f->ctx.get_scheduler(), f->outerScope, [](auto, auto) noexcept {
          // constructor throws
          return just();
        });
  }
};

struct SenderFactory {
  task<void> operator()(AsyncResourceTest* f) const {
    (void)co_await make_async_resource<ThrowingResource>(
        f->ctx.get_scheduler(),
        f->outerScope,
        [](auto, auto) -> any_sender_of<> {
          // factory throws
          throw 42;
        });
  }
};
}  // namespace

TYPED_TEST_SUITE_P(AsyncResourceTypedTest);
TYPED_TEST_P(AsyncResourceTypedTest, throwing) {
  try {
    sync_wait(TypeParam{}(this));
    FAIL() << "throwing ResourceFactory should not succeed";
  } catch (int i) {
    ASSERT_EQ(i, 42);
  }
  sync_wait(this->outerScope.join());
}
REGISTER_TYPED_TEST_SUITE_P(AsyncResourceTypedTest, throwing);
using TestTypes = ::testing::Types<
    Spawning,
    SpawningSenderFactory,
    Throwing,
    SenderFactoryConstructor,
    SenderFactory>;
INSTANTIATE_TYPED_TEST_SUITE_P(Throwing, AsyncResourceTypedTest, TestTypes);

#  endif  // !UNIFEX_NO_EXCEPTIONS
#endif    // !UNIFEX_NO_COROUTINES