/*
* Copyright (c) 2017 Sprint
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "async_shell_cmd.hpp"
#include "common_defs.h"
#include "itti.hpp"
#include "logger.hpp"
#include "options.hpp"
#include "pid_file.hpp"
#include "pgw_app.hpp"
#include "pgw_config.hpp"
#include "sgwc_app.hpp"
#include "sgwc_config.hpp"

#include <iostream>
#include <thread>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h> // srand
#include <unistd.h> // get_pid(), pause()

using namespace gtpv2c;
using namespace pgwc;
using namespace sgwc;
using namespace util;
using namespace std;

itti_mw *itti_inst = nullptr;
async_shell_cmd *async_shell_cmd_inst = nullptr;
pgw_app *pgw_app_inst = nullptr;
sgwc_app *sgwc_app_inst = nullptr;
pgw_config *pgw_cfg = nullptr;
sgwc_config *sgwc_cfg = nullptr;

void send_heartbeat_to_tasks(const uint32_t sequence);

//------------------------------------------------------------------------------
void send_heartbeat_to_tasks(const uint32_t sequence)
{
  itti_msg_ping *itti_msg = new itti_msg_ping(TASK_SGWC_APP, TASK_ALL, sequence);
  std::shared_ptr<itti_msg_ping> i = std::shared_ptr<itti_msg_ping>(itti_msg);
  int ret = itti_inst->send_broadcast_msg(i);
  if (RETURNok != ret) {
    Logger::sgwc_app().error( "Could not send ITTI message %s to task TASK_ALL", i->get_msg_name());
  }
}

//------------------------------------------------------------------------------
void my_app_signal_handler(int s)
{
  std::cout << "Caught signal " << s << std::endl;
  Logger::system().startup( "exiting..." );
  itti_inst->send_terminate_msg(TASK_SGWC_APP);
  itti_inst->wait_tasks_end();
  Logger::system().startup( "exiting now" );
  exit(0);
}
//------------------------------------------------------------------------------
int main(int argc, char **argv)
{
  srand (time(NULL));

  // Command line options
  if ( !Options::parse( argc, argv ) )
  {
     std::cout << "Options::parse() failed" << std::endl;
     return 1;
  }

  // Logger
  Logger::init( "spgwc" , Options::getlogStdout() , Options::getlogRotFilelog());

  Logger::sgwc_app().startup( "Options parsed" );

  struct sigaction sigIntHandler;
  sigIntHandler.sa_handler = my_app_signal_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  sigaction(SIGINT, &sigIntHandler, NULL);

  // Config
  sgwc_config asgwc_config{};
  sgwc_cfg = &asgwc_config;
  pgw_config apgwc_config{};
  pgw_cfg = &apgwc_config;

  sgwc_cfg->load(Options::getlibconfigConfig());
  sgwc_cfg->display();
  pgw_cfg->load(Options::getlibconfigConfig());
  pgw_cfg->display();

  // Inter task Interface
  itti_mw  aitti_mw{};
  itti_inst = &aitti_mw;
  itti_inst->start(pgw_cfg->itti.itti_timer_sched_params);

  // system command
  async_shell_cmd aasync_shell_cmd{sgwc_cfg->itti.async_cmd_sched_params};
  async_shell_cmd_inst = &aasync_shell_cmd;

  // PGW application layer
  pgw_app apgw_app{Options::getlibconfigConfig()};
  pgw_app_inst = &apgw_app;

  // PID file
  // Currently hard-coded value. TODO: add as config option.
   string pid_file_name = get_exe_absolute_path("/var/run", pgw_cfg->instance);
  if (! is_pid_file_lock_success(pid_file_name.c_str())) {
    Logger::pgwc_app().error( "Lock PID file %s failed\n", pid_file_name.c_str());
    exit (-EDEADLK);
  }

  // SGW application layer
  sgwc_app asgwc_app{Options::getlibconfigConfig()};
  sgwc_app_inst = &asgwc_app;


  FILE *fp = NULL;
  std::string filename = fmt::format("/tmp/spgwc_{}.status", getpid());
  fp = fopen(filename.c_str(), "w+");
  fprintf(fp, "STARTED\n");
  fflush(fp);
  fclose(fp);

  pause();
  return 0;
}
