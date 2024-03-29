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

#ifndef __MESOS_SCHEDULER_HPP__
#define __MESOS_SCHEDULER_HPP__

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <mesos/mesos.hpp>

// Mesos scheduler interface and scheduler driver. A scheduler is used
// to interact with Mesos in order to run distributed computations.
//
// IF YOU FIND YOURSELF MODIFYING COMMENTS HERE PLEASE CONSIDER MAKING
// THE SAME MODIFICATIONS FOR OTHER LANGUAGE BINDINGS (e.g., Java:
// src/java/src/org/apache/mesos, Python: src/python/src, etc.).

// Forward declaration.
namespace process {
class Latch;
} // namespace process {

namespace mesos {

// A few forward declarations.
class SchedulerDriver;

namespace scheduler {
class MesosProcess;
} // namespace scheduler {

namespace internal {
class SchedulerProcess;
} // namespace internal {

namespace internal {
class MultiMasterSchedulerProcess;
} // namespace internal {

namespace master {
namespace detector {
class MasterDetector;
} // namespace detector {
} // namespace master {

// Callback interface to be implemented by frameworks' schedulers.
// Note that only one callback will be invoked at a time, so it is not
// recommended that you block within a callback because it may cause a
// deadlock.
//
// Each callback includes a pointer to the scheduler driver that was
// used to run this scheduler. The pointer will not change for the
// duration of a scheduler (i.e., from the point you do
// SchedulerDriver::start() to the point that SchedulerDriver::join()
// returns). This is intended for convenience so that a scheduler
// doesn't need to store a pointer to the driver itself.
class Scheduler
{
public:
  // Empty virtual destructor (necessary to instantiate subclasses).
  virtual ~Scheduler() {}

  // Invoked when the scheduler successfully registers with a Mesos
  // master. A unique ID (generated by the master) used for
  // distinguishing this framework from others and MasterInfo with the
  // ip and port of the current master are provided as arguments.
  virtual void registered(
      SchedulerDriver* driver,
      const FrameworkID& frameworkId,
      const MasterInfo& masterInfo) = 0;

  // Invoked when the scheduler reregisters with a newly elected
  // Mesos master. This is only called when the scheduler has
  // previously been registered. MasterInfo containing the updated
  // information about the elected master is provided as an argument.
  virtual void reregistered(
      SchedulerDriver* driver,
      const MasterInfo& masterInfo) = 0;

  // Invoked when the scheduler becomes "disconnected" from the master
  // (e.g., the master fails and another is taking over).
  virtual void disconnected(SchedulerDriver* driver) = 0;

  // Invoked when resources have been offered to this framework. A
  // single offer will only contain resources from a single slave.
  // Resources associated with an offer will not be re-offered to
  // _this_ framework until either (a) this framework has rejected
  // those resources (see SchedulerDriver::launchTasks) or (b) those
  // resources have been rescinded (see Scheduler::offerRescinded).
  // Note that resources may be concurrently offered to more than one
  // framework at a time (depending on the allocator being used). In
  // that case, the first framework to launch tasks using those
  // resources will be able to use them while the other frameworks
  // will have those resources rescinded (or if a framework has
  // already launched tasks with those resources then those tasks will
  // fail with a TASK_LOST status and a message saying as much).
  virtual void resourceOffers(
      SchedulerDriver* driver,
      const std::vector<Offer>& offers) = 0;

  // Invoked when an offer is no longer valid (e.g., the slave was
  // lost or another framework used resources in the offer). If for
  // whatever reason an offer is never rescinded (e.g., dropped
  // message, failing over framework, etc.), a framework that attempts
  // to launch tasks using an invalid offer will receive TASK_LOST
  // status updates for those tasks (see Scheduler::resourceOffers).
  virtual void offerRescinded(
      SchedulerDriver* driver,
      const OfferID& offerId) = 0;

  // Invoked when the status of a task has changed (e.g., a slave is
  // lost and so the task is lost, a task finishes and an executor
  // sends a status update saying so, etc). If implicit
  // acknowledgements are being used, then returning from this
  // callback _acknowledges_ receipt of this status update! If for
  // whatever reason the scheduler aborts during this callback (or
  // the process exits) another status update will be delivered (note,
  // however, that this is currently not true if the slave sending the
  // status update is lost/fails during that time). If explicit
  // acknowledgements are in use, the scheduler must acknowledge this
  // status on the driver.
  virtual void statusUpdate(
      SchedulerDriver* driver,
      const TaskStatus& status) = 0;

  // Invoked when an executor sends a message. These messages are best
  // effort; do not expect a framework message to be retransmitted in
  // any reliable fashion.
  virtual void frameworkMessage(
      SchedulerDriver* driver,
      const ExecutorID& executorId,
      const SlaveID& slaveId,
      const std::string& data) = 0;

  // Invoked when a slave has been determined unreachable (e.g.,
  // machine failure, network partition). Most frameworks will need to
  // reschedule any tasks launched on this slave on a new slave.
  //
  // NOTE: This callback is not reliably delivered. If a host or
  // network failure causes messages between the master and the
  // scheduler to be dropped, this callback may not be invoked.
  virtual void slaveLost(
      SchedulerDriver* driver,
      const SlaveID& slaveId) = 0;

  // Invoked when an executor has exited/terminated. Note that any
  // tasks running will have TASK_LOST status updates automagically
  // generated.
  //
  // NOTE: This callback is not reliably delivered. If a host or
  // network failure causes messages between the master and the
  // scheduler to be dropped, this callback may not be invoked.
  virtual void executorLost(
      SchedulerDriver* driver,
      const ExecutorID& executorId,
      const SlaveID& slaveId,
      int status) = 0;

  // Invoked when there is an unrecoverable error in the scheduler or
  // scheduler driver. The driver will be aborted BEFORE invoking this
  // callback.
  virtual void error(
      SchedulerDriver* driver,
      const std::string& message) = 0;
};


// Abstract interface for connecting a scheduler to Mesos. This
// interface is used both to manage the scheduler's lifecycle (start
// it, stop it, or wait for it to finish) and to interact with Mesos
// (e.g., launch tasks, kill tasks, etc.). See MesosSchedulerDriver
// below for a concrete example of a SchedulerDriver.
class SchedulerDriver
{
public:
  // Empty virtual destructor (necessary to instantiate subclasses).
  // It is expected that 'stop()' is called before this is called.
  virtual ~SchedulerDriver() {}

  // Starts the scheduler driver. This needs to be called before any
  // other driver calls are made.
  virtual Status start() = 0;

  // Stops the scheduler driver. If the 'failover' flag is set to
  // false then it is expected that this framework will never
  // reconnect to Mesos. So Mesos will unregister the framework and
  // shutdown all its tasks and executors. If 'failover' is true, all
  // executors and tasks will remain running (for some framework
  // specific failover timeout) allowing the scheduler to reconnect
  // (possibly in the same process, or from a different process, for
  // example, on a different machine).
  virtual Status stop(bool failover = false) = 0;

  // Aborts the driver so that no more callbacks can be made to the
  // scheduler. The semantics of abort and stop have deliberately been
  // separated so that code can detect an aborted driver (i.e., via
  // the return status of SchedulerDriver::join, see below), and
  // instantiate and start another driver if desired (from within the
  // same process). Note that 'stop()' is not automatically called
  // inside 'abort()'.
  virtual Status abort() = 0;

  // Waits for the driver to be stopped or aborted, possibly
  // _blocking_ the current thread indefinitely. The return status of
  // this function can be used to determine if the driver was aborted
  // (see mesos.proto for a description of Status).
  virtual Status join() = 0;

  // Starts and immediately joins (i.e., blocks on) the driver.
  virtual Status run() = 0;

  // Requests resources from Mesos (see mesos.proto for a description
  // of Request and how, for example, to request resources from
  // specific slaves). Any resources available are offered to the
  // framework via Scheduler::resourceOffers callback, asynchronously.
  virtual Status requestResources(const std::vector<Request>& requests) = 0;

  // Launches the given set of tasks. Any remaining resources (i.e.,
  // those that are not used by the launched tasks or their executors)
  // will be considered declined. Note that this includes resources
  // used by tasks that the framework attempted to launch but failed
  // (with TASK_ERROR) due to a malformed task description. The
  // specified filters are applied on all unused resources (see
  // mesos.proto for a description of Filters). Available resources
  // are aggregated when multiple offers are provided. Note that all
  // offers must belong to the same slave. Invoking this function with
  // an empty collection of tasks declines offers in their entirety
  // (see Scheduler::declineOffer).
  virtual Status launchTasks(
      const std::vector<OfferID>& offerIds,
      const std::vector<TaskInfo>& tasks,
      const Filters& filters = Filters()) = 0;

  // DEPRECATED: Use launchTasks(offerIds, tasks, filters) instead.
  virtual Status launchTasks(
      const OfferID& offerId,
      const std::vector<TaskInfo>& tasks,
      const Filters& filters = Filters()) = 0;

  // Kills the specified task. Note that attempting to kill a task is
  // currently not reliable. If, for example, a scheduler fails over
  // while it was attempting to kill a task it will need to retry in
  // the future. Likewise, if unregistered / disconnected, the request
  // will be dropped (these semantics may be changed in the future).
  virtual Status killTask(const TaskID& taskId) = 0;

  // Accepts the given offers and performs a sequence of operations on
  // those accepted offers. See Offer.Operation in mesos.proto for the
  // set of available operations. Any remaining resources (i.e., those
  // that are not used by the launched tasks or their executors) will
  // be considered declined. Note that this includes resources used by
  // tasks that the framework attempted to launch but failed (with
  // TASK_ERROR) due to a malformed task description. The specified
  // filters are applied on all unused resources (see mesos.proto for
  // a description of Filters). Available resources are aggregated
  // when multiple offers are provided. Note that all offers must
  // belong to the same slave.
  virtual Status acceptOffers(
      const std::vector<OfferID>& offerIds,
      const std::vector<Offer::Operation>& operations,
      const Filters& filters = Filters()) = 0;

  // Declines an offer in its entirety and applies the specified
  // filters on the resources (see mesos.proto for a description of
  // Filters). Note that this can be done at any time, it is not
  // necessary to do this within the Scheduler::resourceOffers
  // callback.
  virtual Status declineOffer(
      const OfferID& offerId,
      const Filters& filters = Filters()) = 0;

  // Removes all filters previously set by the framework (via
  // launchTasks()). This enables the framework to receive offers from
  // those filtered slaves.
  virtual Status reviveOffers() = 0;

  // Inform Mesos master to stop sending offers to the framework. The
  // scheduler should call reviveOffers() to resume getting offers.
  virtual Status suppressOffers() = 0;

  // Acknowledges the status update. This should only be called
  // once the status update is processed durably by the scheduler.
  // Not that explicit acknowledgements must be requested via the
  // constructor argument, otherwise a call to this method will
  // cause the driver to crash.
  virtual Status acknowledgeStatusUpdate(
      const TaskStatus& status) = 0;

  // Sends a message from the framework to one of its executors. These
  // messages are best effort; do not expect a framework message to be
  // retransmitted in any reliable fashion.
  virtual Status sendFrameworkMessage(
      const ExecutorID& executorId,
      const SlaveID& slaveId,
      const std::string& data) = 0;

  // Allows the framework to query the status for non-terminal tasks.
  // This causes the master to send back the latest task status for
  // each task in 'statuses', if possible. Tasks that are no longer
  // known will result in a TASK_LOST update. If statuses is empty,
  // then the master will send the latest status for each task
  // currently known.
  virtual Status reconcileTasks(
      const std::vector<TaskStatus>& statuses) = 0;
};


// Concrete implementation of a SchedulerDriver that connects a
// Scheduler with a Mesos master. The MesosSchedulerDriver is
// thread-safe.
//
// Note that scheduler failover is supported in Mesos. After a
// scheduler is registered with Mesos it may failover (to a new
// process on the same machine or across multiple machines) by
// creating a new driver with the ID given to it in
// Scheduler::registered.
//
// The driver is responsible for invoking the Scheduler callbacks as
// it communicates with the Mesos master.
//
// Note that blocking on the MesosSchedulerDriver (e.g., via
// MesosSchedulerDriver::join) doesn't affect the scheduler callbacks
// in anyway because they are handled by a different thread.
//
// Note that the driver uses GLOG to do its own logging. GLOG flags
// can be set via environment variables, prefixing the flag name with
// "GLOG_", e.g., "GLOG_v=1". For Mesos specific logging flags see
// src/logging/flags.hpp. Mesos flags can also be set via environment
// variables, prefixing the flag name with "MESOS_", e.g.,
// "MESOS_QUIET=1".
//
// See src/examples/test_framework.cpp for an example of using the
// MesosSchedulerDriver.
class MesosSchedulerDriver : public SchedulerDriver
{
public:
  // Creates a new driver for the specified scheduler. The master
  // should be one of:
  //
  //     host:port
  //     zk://host1:port1,host2:port2,.../path
  //     zk://username:password@host1:port1,host2:port2,.../path
  //     file:///path/to/file (where file contains one of the above)
  //
  // The driver will attempt to "failover" if the specified
  // FrameworkInfo includes a valid FrameworkID.
  //
  // Any Mesos configuration options are read from environment
  // variables, as well as any configuration files found through the
  // environment variables.
  //
  // TODO(vinod): Deprecate this once 'MesosSchedulerDriver' can take
  // 'Option<Credential>' as parameter. Currently it cannot because
  // 'stout' is not visible from here.
  MesosSchedulerDriver(
      Scheduler* scheduler,
      const FrameworkInfo& framework,
      const std::string& master);

  // Same as the above constructor but takes 'credential' as argument.
  // The credential will be used for authenticating with the master.
  MesosSchedulerDriver(
      Scheduler* scheduler,
      const FrameworkInfo& framework,
      const std::string& master,
      const Credential& credential);

  // These constructors are the same as the above two, but allow
  // the framework to specify whether implicit or explicit
  // acknowledgements are desired. See statusUpdate() for the
  // details about explicit acknowledgements.
  //
  // TODO(bmahler): Deprecate the above two constructors. In 0.22.0
  // these new constructors are exposed.
  MesosSchedulerDriver(
      Scheduler* scheduler,
      const FrameworkInfo& framework,
      const std::string& master,
      bool implicitAcknowledgements);

  MesosSchedulerDriver(
      Scheduler* scheduler,
      const FrameworkInfo& framework,
      const std::string& master,
      bool implicitAcknowlegements,
      const Credential& credential);

  // This destructor will block indefinitely if
  // MesosSchedulerDriver::start was invoked successfully (possibly
  // via MesosSchedulerDriver::run) and MesosSchedulerDriver::stop has
  // not been invoked.
  virtual ~MesosSchedulerDriver();

  // See SchedulerDriver for descriptions of these.
  virtual Status start();
  virtual Status stop(bool failover = false);
  virtual Status abort();
  virtual Status join();
  virtual Status run();

  virtual Status requestResources(
      const std::vector<Request>& requests);

  // TODO(nnielsen): launchTasks using single offer is deprecated.
  // Use launchTasks with offer list instead.
  virtual Status launchTasks(
      const OfferID& offerId,
      const std::vector<TaskInfo>& tasks,
      const Filters& filters = Filters());

  virtual Status launchTasks(
      const std::vector<OfferID>& offerIds,
      const std::vector<TaskInfo>& tasks,
      const Filters& filters = Filters());

  virtual Status killTask(const TaskID& taskId);

  virtual Status acceptOffers(
      const std::vector<OfferID>& offerIds,
      const std::vector<Offer::Operation>& operations,
      const Filters& filters = Filters());

  virtual Status declineOffer(
      const OfferID& offerId,
      const Filters& filters = Filters());

  virtual Status reviveOffers();

  virtual Status suppressOffers();

  virtual Status acknowledgeStatusUpdate(
      const TaskStatus& status);

  virtual Status sendFrameworkMessage(
      const ExecutorID& executorId,
      const SlaveID& slaveId,
      const std::string& data);

  virtual Status reconcileTasks(
      const std::vector<TaskStatus>& statuses);

protected:
  // Used to detect (i.e., choose) the master.
  std::shared_ptr<master::detector::MasterDetector> detector;

private:
  void initialize();

  Scheduler* scheduler;
  FrameworkInfo framework;
  std::string master;

  // Used for communicating with the master.
  internal::SchedulerProcess* process;

  // URL for the master (e.g., zk://, file://, etc).
  std::string url;

  // Mutex for enforcing serial execution of all non-callbacks.
  std::recursive_mutex mutex;

  // Latch for waiting until driver terminates.
  process::Latch* latch;

  // Current status of the driver.
  Status status;

  const bool implicitAcknowlegements;

  const Credential* credential;

  // Scheduler process ID.
  std::string schedulerId;
};


class MultiMesosSchedulerDriver : public SchedulerDriver
{
public:
  // Creates a new driver for the specified scheduler. The master
  // should be one of:
  //
  //     host:port
  //     zk://host1:port1,host2:port2,.../path
  //     zk://username:password@host1:port1,host2:port2,.../path
  //     file:///path/to/file (where file contains one of the above)
  //
  // The driver will attempt to "failover" if the specified
  // FrameworkInfo includes a valid FrameworkID.
  //
  // Any Mesos configuration options are read from environment
  // variables, as well as any configuration files found through the
  // environment variables.
  //
  // TODO(vinod): Deprecate this once 'MesosSchedulerDriver' can take
  // 'Option<Credential>' as parameter. Currently it cannot because
  // 'stout' is not visible from here.
  MultiMesosSchedulerDriver(
      Scheduler* scheduler,
      const FrameworkInfo& framework,
      const std::string& master);

  // Same as the above constructor but takes 'credential' as argument.
  // The credential will be used for authenticating with the master.
  MultiMesosSchedulerDriver(
      Scheduler* scheduler,
      const FrameworkInfo& framework,
      const std::string& master,
      const Credential& credential);

  // These constructors are the same as the above two, but allow
  // the framework to specify whether implicit or explicit
  // acknowledgements are desired. See statusUpdate() for the
  // details about explicit acknowledgements.
  //
  // TODO(bmahler): Deprecate the above two constructors. In 0.22.0
  // these new constructors are exposed.
  MultiMesosSchedulerDriver(
      Scheduler* scheduler,
      const FrameworkInfo& framework,
      const std::string& master,
      bool implicitAcknowledgements);

  MultiMesosSchedulerDriver(
      Scheduler* scheduler,
      const FrameworkInfo& framework,
      const std::string& master,
      bool implicitAcknowlegements,
      const Credential& credential);

  // This destructor will block indefinitely if
  // MesosSchedulerDriver::start was invoked successfully (possibly
  // via MesosSchedulerDriver::run) and MesosSchedulerDriver::stop has
  // not been invoked.
  virtual ~MultiMesosSchedulerDriver();

  // See SchedulerDriver for descriptions of these.
  virtual Status start();
  virtual Status stop(bool failover = false);
  virtual Status abort();
  virtual Status join();
  virtual Status run();

  virtual Status requestResources(
      const std::vector<Request>& requests);

  // TODO(nnielsen): launchTasks using single offer is deprecated.
  // Use launchTasks with offer list instead.
  virtual Status launchTasks(
      const OfferID& offerId,
      const std::vector<TaskInfo>& tasks,
      const Filters& filters = Filters());

  virtual Status launchTasks(
      const std::vector<OfferID>& offerIds,
      const std::vector<TaskInfo>& tasks,
      const Filters& filters = Filters());

  virtual Status killTask(const TaskID& taskId);

  virtual Status acceptOffers(
      const std::vector<OfferID>& offerIds,
      const std::vector<Offer::Operation>& operations,
      const Filters& filters = Filters());

  virtual Status declineOffer(
      const OfferID& offerId,
      const Filters& filters = Filters());

  virtual Status reviveOffers();

  virtual Status suppressOffers();

  virtual Status acknowledgeStatusUpdate(
      const TaskStatus& status);

  virtual Status sendFrameworkMessage(
      const ExecutorID& executorId,
      const SlaveID& slaveId,
      const std::string& data);

  virtual Status reconcileTasks(
      const std::vector<TaskStatus>& statuses);

protected:
  // Used to detect (i.e., choose) the master.
  std::shared_ptr<master::detector::MasterDetector> detector;

private:
  void initialize();

  Scheduler* scheduler;
  FrameworkInfo framework;
  std::string master;

  // Used for communicating with the master.
  internal::MultiMasterSchedulerProcess* process;

  // URL for the master (e.g., zk://, file://, etc).
  std::string url;

  // Mutex for enforcing serial execution of all non-callbacks.
  std::recursive_mutex mutex;

  // Latch for waiting until driver terminates.
  process::Latch* latch;

  // Current status of the driver.
  Status status;

  const bool implicitAcknowlegements;

  const Credential* credential;

  // Scheduler process ID.
  std::string schedulerId;
};

} // namespace mesos {

#endif // __MESOS_SCHEDULER_HPP__
