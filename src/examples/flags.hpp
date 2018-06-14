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

#ifndef __EXAMPLES_FLAGS_HPP__
#define __EXAMPLES_FLAGS_HPP__

#include <string>

#include <stout/flags.hpp>
#include <stout/option.hpp>

#include <mesos/module/module.hpp>
#include <mesos/type_utils.hpp>

#include <stout/error.hpp>
#include <stout/json.hpp>
#include <stout/os.hpp>
#include <stout/path.hpp>

#include "common/http.hpp"
#include "common/parse.hpp"
#include "common/protobuf_utils.hpp"


#include "logging/flags.hpp"

namespace mesos {
namespace internal {
namespace examples {

class Flags : public virtual mesos::internal::logging::Flags
{
public:
  Flags()
  {
    add(&Flags::master,
        "master",
        "The master to connect to. May be one of:\n"
        "  `host:port`\n"
        "  `master@host:port` (PID of the master)\n"
        "  `zk://host1:port1,host2:port2,.../path`\n"
        "  `zk://username:password@host1:port1,host2:port2,.../path`\n"
        "  `file://path/to/file` (where file contains one of the above)\n"
        "  `local` (launches mesos-local)");

    add(&Flags::authenticate,
        "authenticate",
        "Set to 'true' to enable framework authentication.",
        false);

    add(&Flags::principal,
        "principal",
        "The principal used to identify this framework.",
        "test");

    add(&Flags::secret,
        "secret",
        "The secret used to authenticate this framework.\n"
        "If the value starts with '/' or 'file://' it will be parsed as the\n"
        "path to a file containing the secret. Otherwise the string value is\n"
        "treated as the secret.");

    add(&Flags::checkpoint,
        "checkpoint",
        "Whether this framework should be checkpointed.",
        false);

    add(&Flags::role,
        "role",
        "Role to use when registering.",
        "*");

    add(&Flags::master_detector,
        "master_detector",
        "The symbol name of the master detector to use. This symbol\n"
        "should exist in a module specified through the --modules flag.\n"
        "Cannot be used in conjunction with --zk.\n");

    add(&Flags::modules,
        "modules",
        "List of modules to be loaded and be available to the internal\n"
        "subsystems.\n"
        "\n"
        "Use `--modules=filepath` to specify the list of modules via a\n"
        "file containing a JSON-formatted string. `filepath` can be\n"
        "of the form `file:///path/to/file` or `/path/to/file`.\n"
        "\n"
        "Use `--modules=\"{...}\"` to specify the list of modules inline.\n"
        "Cannot be used in conjunction with --modules_dir.\n");
  }

  Option<Modules> modules;
  Option<std::string> master_detector;
  std::string master;
  bool authenticate;
  std::string principal;
  Option<std::string> secret;
  bool checkpoint;
  std::string role;
};


} // namespace examples {
} // namespace internal {
} // namespace mesos {

#endif // __EXAMPLES_FLAGS_HPP__
