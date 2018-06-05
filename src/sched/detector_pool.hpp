// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __WINDOWS__
#include <dlfcn.h>
#endif // __WINDOWS__
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __WINDOWS__
#include <unistd.h>
#endif // __WINDOWS__

#ifndef __WINDOWS__
#include <arpa/inet.h>
#endif // __WINDOWS__

#include <mutex>
#include <string>

#include <mesos/master/detector.hpp>

using mesos::master::detector::MasterDetector;

using std::mutex;
using std::shared_ptr;
using std::string;
using std::weak_ptr;

namespace mesos {
namespace internal {

// TODO(erin): move code shared with sched.cpp into its own file
// The DetectorPool is responsible for tracking single detector per url
// to avoid having multiple detectors per url when multiple frameworks
// are instantiated per process. See MESOS-3595.

class DetectorPool
{
public:
  virtual ~DetectorPool();

  static Try<shared_ptr<MasterDetector>> get(const string url,
      const Option<string> masterDetectorModule);

private:
  // Hide the constructors and assignment operator.
  DetectorPool() {}
  DetectorPool(const DetectorPool&) = delete;
  DetectorPool& operator=(const DetectorPool&) = delete;

  // Instead of having multiple detectors for multiple frameworks,
  // keep track of one detector per url.
  hashmap<string, weak_ptr<MasterDetector>> pool;
  std::mutex poolMutex;

  // Internal Singleton.
  static DetectorPool* instance();
};

} // namespace internal {
} // namespace mesos {
