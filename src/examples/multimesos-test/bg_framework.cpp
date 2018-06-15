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
#include <iostream>
#include <string>
#include <mutex>
#include <random>

#include <boost/lexical_cast.hpp>

#include <mesos/resources.hpp>
#include <mesos/scheduler.hpp>
#include <mesos/type_utils.hpp>
#include <mesos/master/detector.hpp>

#include <mesos/authorizer/acls.hpp>

#include <stout/try.hpp>
#include <stout/check.hpp>
#include <stout/exit.hpp>
#include <stout/flags.hpp>
#include <stout/numify.hpp>
#include <stout/option.hpp>
#include <stout/os.hpp>
#include <stout/path.hpp>
#include <stout/protobuf.hpp>
#include <stout/stringify.hpp>

#include <stout/os/realpath.hpp>

#include "logging/flags.hpp"
#include "logging/logging.hpp"
#include "module/manager.hpp"

#include "examples/flags.hpp"

using namespace mesos;

using boost::lexical_cast;

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::string;
using std::vector;
using std::mutex;

using mesos::Resources;

using mesos::modules::ModuleManager;
using mesos::master::detector::MasterDetector;

const int32_t CPUS_PER_TASK = 1;
const int32_t MEM_PER_TASK = 32;

constexpr char EXECUTOR_BINARY[] = "test-executor";
constexpr char EXECUTOR_NAME[] = "Test Executor (C++)";
constexpr char FRAMEWORK_NAME[] = "Test Framework(Background)";

class TestScheduler : public Scheduler
{
public:
  TestScheduler(
      bool _implicitAcknowledgements,
      const ExecutorInfo& _executor,
      const string& _role)
    : implicitAcknowledgements(_implicitAcknowledgements),
      executor(_executor),
      role(_role),
      rdm_eng(),
      distribution(30,10),
      mutx(){}

  virtual ~TestScheduler() {}

  virtual void registered(SchedulerDriver*,
                          const FrameworkID&,
                          const MasterInfo&)
  {
    cout << "Registered!" << endl;
  }

  virtual void reregistered(SchedulerDriver*, const MasterInfo& masterInfo) {}

  virtual void disconnected(SchedulerDriver* driver) {}

  virtual void resourceOffers(SchedulerDriver* driver,
                              const vector<Offer>& offers)
  {
    foreach (const Offer& offer, offers) {
      cout << "Received offer " << offer.id() << " with " << offer.resources()
           << endl;

      Resources taskResources = Resources::parse(
          "cpus:" + stringify(CPUS_PER_TASK) +
          ";mem:" + stringify(MEM_PER_TASK)).get();
      taskResources.allocate(role);

      Resources remaining = offer.resources();

      // Launch tasks.
      vector<TaskInfo> tasks;

      if(remaining.toUnreserved().contains(taskResources)) {
        mutx.lock();
        int taskId = tasksLaunched;
        mutx.unlock();

        cout << "Launching task " << taskId << " using offer "
             << offer.id() << endl;

        TaskInfo task;
        CommandInfo commandInfo;
        
        double pick = distribution(rdm_eng);
        int second = (pick>0)?floor(pick):0;

        commandInfo.mutable_value()->assign("sleep "+std::to_string(second));
        task.set_name("Task " + lexical_cast<string>(taskId));
        task.mutable_task_id()->set_value(lexical_cast<string>(taskId));
        task.mutable_slave_id()->MergeFrom(offer.slave_id());
        task.mutable_command()->MergeFrom(commandInfo);

        Option<Resources> resources = [&]() {
          if (role == "*") {
            return remaining.find(taskResources);
          }

          Resource::ReservationInfo reservation;
          reservation.set_type(Resource::ReservationInfo::STATIC);
          reservation.set_role(role);

          return remaining.find(taskResources.pushReservation(reservation));
        }();

        CHECK_SOME(resources);
        task.mutable_resources()->MergeFrom(resources.get());
        remaining -= resources.get();

        mutx.lock();
        tasksLaunched++;
        mutx.unlock();
        tasks.push_back(task);
      }

      driver->launchTasks(offer.id(), tasks);
    }

    cout << "Done launching tasks" << endl;
  }

  virtual void offerRescinded(SchedulerDriver* driver, const OfferID& offerId)
  {}

  virtual void statusUpdate(SchedulerDriver* driver, const TaskStatus& status)
  {
    int taskId = lexical_cast<int>(status.task_id().value());

    cout << "Task " << taskId << " is in state " << status.state() << endl;

    if (status.state() == TASK_LOST ||
        status.state() == TASK_KILLED ||
        status.state() == TASK_FAILED) {
      cout << "Aborting because task " << taskId
           << " is in unexpected state " << status.state()
           << " with reason " << status.reason()
           << " from source " << status.source()
           << " with message '" << status.message() << "'" << endl;
      driver->abort();
    }

    if (!implicitAcknowledgements) {
      driver->acknowledgeStatusUpdate(status);
    }

    mutx.lock();
    if (status.state() == TASK_FINISHED) {
      tasksFinished++;
    }

    cout << "Finished " << tasksFinished << " tasks" << endl;
    mutx.unlock();

  }

  virtual void frameworkMessage(
      SchedulerDriver* driver,
      const ExecutorID& executorId,
      const SlaveID& slaveId,
      const string& data)
  {}

  virtual void slaveLost(SchedulerDriver* driver, const SlaveID& sid) {}

  virtual void executorLost(
      SchedulerDriver* driver,
      const ExecutorID& executorID,
      const SlaveID& slaveID,
      int status)
  {}

  virtual void error(SchedulerDriver* driver, const string& message)
  {
    cout << message << endl;
  }

private:
  const bool implicitAcknowledgements;
  const ExecutorInfo executor;
  string role;
  int tasksLaunched;
  int tasksFinished;
  mutex mutx;
  std::default_random_engine rdm_eng;
  std::normal_distribution<double> distribution;
};


void usage(const char* argv0, const flags::FlagsBase& flags)
{
  cerr << "Usage: " << Path(argv0).basename() << " [...]" << endl
       << endl
       << "Supported options:" << endl
       << flags.usage();
}

class Flags : public virtual mesos::internal::examples::Flags {};


int main(int argc, char** argv)
{
  Flags flags;

  Try<flags::Warnings> load = flags.load("MESOS_EXAMPLE_", argc, argv);

  // TODO(erin): change hacky workaround
  if (!flags.master.isSome()) {
     flags.master = Option<std::string>("");
  }

  if (flags.help) {
    cout << flags.usage() << endl;
    return EXIT_SUCCESS;
  }

  if (load.isError()) {
    cerr << flags.usage(load.error()) << endl;
    return EXIT_FAILURE;
  }

  internal::logging::initialize(argv[0], true, flags); // Catch signals.

  // Log any flag warnings (after logging is initialized).
  foreach (const flags::Warning& warning, load->warnings) {
    LOG(WARNING) << warning.message;
  }

  if (flags.modules.isSome()) {
    Try<Nothing> result = ModuleManager::load(flags.modules.get());
    if (result.isError()) {
      EXIT(EXIT_FAILURE) << "Error loading modules: " << result.error();
    }
  }

  ExecutorInfo executor;
  executor.mutable_executor_id()->set_value("Command");
  executor.set_name(EXECUTOR_NAME);

  FrameworkInfo framework;
  framework.set_user(""); // Have Mesos fill in the current user.
  framework.set_principal(flags.principal);
  framework.set_name(FRAMEWORK_NAME);
  framework.add_roles(flags.role);
  framework.add_capabilities()->set_type(
      FrameworkInfo::Capability::MULTI_ROLE);
  framework.add_capabilities()->set_type(
      FrameworkInfo::Capability::RESERVATION_REFINEMENT);
  framework.set_checkpoint(flags.checkpoint);

  bool implicitAcknowledgements = true;
  if (os::getenv("MESOS_EXPLICIT_ACKNOWLEDGEMENTS").isSome()) {
    cout << "Enabling explicit acknowledgements for status updates" << endl;

    implicitAcknowledgements = false;
  }

  if (flags.master.get() == "local") {
    // Configure master.
    os::setenv("MESOS_ROLES", flags.role);
    os::setenv("MESOS_AUTHENTICATE_FRAMEWORKS", stringify(flags.authenticate));

    ACLs acls;
    ACL::RegisterFramework* acl = acls.add_register_frameworks();
    acl->mutable_principals()->set_type(ACL::Entity::ANY);
    acl->mutable_roles()->add_values(flags.role);
    os::setenv("MESOS_ACLS", stringify(JSON::protobuf(acls)));

    // Configure agent.
    os::setenv("MESOS_DEFAULT_ROLE", flags.role);
  }

  MultiMesosSchedulerDriver* driver;
  TestScheduler scheduler(implicitAcknowledgements, executor, flags.role);

  if (flags.authenticate) {
    cout << "Enabling authentication for the framework" << endl;

    Credential credential;
    credential.set_principal(flags.principal);
    if (flags.secret.isSome()) {
      credential.set_secret(flags.secret.get());
    }

    driver = new MultiMesosSchedulerDriver(
        &scheduler,
        framework,
        flags.master.get(),
        implicitAcknowledgements,
        credential);
  } else {
    driver = new MultiMesosSchedulerDriver(
        &scheduler,
        framework,
        flags.master.get(),
        implicitAcknowledgements);
  }

  int status = driver->run() == DRIVER_STOPPED ? 0 : 1;

  // Ensure that the driver process terminates.
  driver->stop();

  delete driver;
  return status;
}