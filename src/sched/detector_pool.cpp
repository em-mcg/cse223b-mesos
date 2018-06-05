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
#include "sched/detector_pool.hpp"

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

DetectorPool::~DetectorPool() {
}

Try<shared_ptr<MasterDetector>> DetectorPool::get(const string url,
        const Option<string> masterDetectorModule) {
    synchronized (DetectorPool::instance()->poolMutex){
    // Get or create the `weak_ptr` map entry.
    shared_ptr<MasterDetector> result =
    DetectorPool::instance()->pool[url].lock();

    if (result) {
        // Return existing master detector.
        return result;
    } else {
        // Else, create the master detector and record it in the map.
        Try<MasterDetector*> detector =
        MasterDetector::create(url, masterDetectorModule);
        if (detector.isError()) {
            return Error(detector.error());
        }

        result = shared_ptr<MasterDetector>(detector.get());
        DetectorPool::instance()->pool[url] = result;
        return result;
    }
}
}

// Hide the constructors and assignment operator.
DetectorPool::DetectorPool() {
}

DetectorPool::DetectorPool(const DetectorPool&) = delete;

DetectorPool::DetectorPool& operator=(const DetectorPool&) = delete;

// Internal Singleton.
DetectorPool::DetectorPool* instance() {
    static DetectorPool* singleton = new DetectorPool();
    return singleton;
}

} // namespace internal {
} // namespace mesos {
