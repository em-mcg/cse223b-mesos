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
#include <cmath>
#include <chrono>
#include <vector>

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
typedef std::chrono::high_resolution_clock Clock;

using mesos::Resources;

using mesos::modules::ModuleManager;
using mesos::master::detector::MasterDetector;

vector<double> time_sample;
const int SAMPLING_NUM = 3;

const int32_t CPUS_PER_TASK = 1;
const int32_t MEM_PER_TASK = 32;

constexpr char EXECUTOR_BINARY[] = "test-executor";
constexpr char EXECUTOR_NAME[] = "Test Executor (C++)";
constexpr char FRAMEWORK_NAME[] = "Measure!!!!!!!";

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
      issued(false),
      mutx(){}

  virtual ~TestScheduler() {}

  virtual void registered(SchedulerDriver*,
                          const FrameworkID&,
                          const MasterInfo&)
  {}

  virtual void reregistered(SchedulerDriver*, const MasterInfo& masterInfo) {}

  virtual void disconnected(SchedulerDriver* driver) {}

  virtual void resourceOffers(SchedulerDriver* driver,
                              const vector<Offer>& offers)
  {
    foreach (const Offer& offer, offers) {

      Resources taskResources = Resources::parse(
          "cpus:" + stringify(CPUS_PER_TASK) +
          ";mem:" + stringify(MEM_PER_TASK)).get();
      taskResources.allocate(role);

      Resources remaining = offer.resources();

      // Launch tasks.
      vector<TaskInfo> tasks;

      if(!issued
        && remaining.toUnreserved().contains(taskResources)) {
        mutx.lock();
        int taskId = 0;

        TaskInfo task;
        CommandInfo commandInfo;

        commandInfo.mutable_value()->assign("sleep 10");
        task.set_name("Measure " + lexical_cast<string>(taskId));
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
        issued = true;
        start = Clock::now();
        tasks.push_back(task);
      }
      driver->launchTasks(offer.id(), tasks);
    }
    mutx.unlock();
  }

  virtual void offerRescinded(SchedulerDriver* driver, const OfferID& offerId)
  {}

  virtual void statusUpdate(SchedulerDriver* driver, const TaskStatus& status)
  {
    mutx.lock();

    int taskId = lexical_cast<int>(status.task_id().value());

    if (status.state() == TASK_KILLED ||
        status.state() == TASK_FAILED) {
      driver->abort();
    }

    if (!implicitAcknowledgements) {
      driver->acknowledgeStatusUpdate(status);
    }
    if(status.state() == TASK_LOST){
      issued = false;
    }
    if (status.state() == TASK_FINISHED) {
      // Record the time and exit
      auto end = Clock::now();
      double interval = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
      time_sample.push_back(interval/1e3);
      issued = false;
      driver->stop();
    }
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

  }

private:
  const bool implicitAcknowledgements;
  const ExecutorInfo executor;
  string role;
  bool issued;
  Clock::time_point start;
  mutex mutx;
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

    implicitAcknowledgements = false;
  }

  if (flags.master == "local") {
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

  TestScheduler scheduler(implicitAcknowledgements, executor, flags.role);

  for(int i=0;i<SAMPLING_NUM;i++){
    MultiMesosSchedulerDriver* driver;
    if (flags.authenticate){
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
  }
  double mean=0.0,std=0.0;
  for(auto it : time_sample){
    mean += it;
  }
  mean /= time_sample.size();
  for(auto it : time_sample){
    std += (it-mean) * (it-mean);
  }
  std /= time_sample.size();
  std = std::sqrt(std);
  cout<<"Take "<<SAMPLING_NUM<<" Samples"<<endl;
  cout<<"The delay is: "<<mean-10<<"/"<<std<<"s"<<endl;
  return 0;
}