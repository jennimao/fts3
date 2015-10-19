/*
 * Copyright (c) CERN 2013-2015
 *
 * Copyright (c) Members of the EMI Collaboration. 2010-2013
 *  See  http://www.eu-emi.eu/partners for details on the copyright
 *  holders.
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

#include "Server.h"

#include "../config/ServerConfig.h"
#include "services/cleaner/CleanerService.h"
#include "services/transfers/TransfersService.h"
#include "services/transfers/MultihopTransfersService.h"
#include "services/transfers/ReuseTransfersService.h"
#include "services/transfers/CancelerService.h"
#include "services/heartbeat/HeartBeat.h"
#include "services/optimizer/OptimizerService.h"
#include "services/transfers/MessageProcessingService.h"
#include "services/webservice/WebService.h"


namespace fts3 {
namespace server {


Server::~Server()
{
    try {
        this->stop();
        this->wait();
    }
    catch (...) {
        // pass
    }
}


void Server::start()
{
    services.emplace_back(new CleanerService);
    services.emplace_back(new MessageProcessingService);
    services.emplace_back(new HeartBeat);

    for (auto i = services.begin(); i != services.end(); ++i) {
        systemThreads.create_thread(boost::ref(*(i->get())));
    }

    // Give cleaner and heartbeat some time ahead
    if (!config::ServerConfig::instance().get<bool> ("rush"))
        sleep(8);

    services.emplace_back(new CancelerService);
    systemThreads.create_thread(boost::ref(*services.back().get()));

    // Wait for status updates to be processed
    if (!config::ServerConfig::instance().get<bool> ("rush"))
        sleep(12);

    services.emplace_back(new OptimizerService);
    systemThreads.create_thread(boost::ref(*services.back().get()));

    services.emplace_back(new TransfersService);
    systemThreads.create_thread(boost::ref(*services.back().get()));

    services.emplace_back(new ReuseTransfersService);
    systemThreads.create_thread(boost::ref(*services.back().get()));

    services.emplace_back(new MultihopTransfersService);
    systemThreads.create_thread(boost::ref(*services.back().get()));

    unsigned int port = config::ServerConfig::instance().get<unsigned int>("Port");
    const std::string& ip = config::ServerConfig::instance().get<std::string>("IP");

    services.emplace_back(new WebService(port, ip));
    systemThreads.create_thread(boost::ref(*services.back().get()));
}


void Server::wait()
{
    systemThreads.join_all();
}


void Server::stop()
{
    systemThreads.interrupt_all();
}

} // end namespace server
} // end namespace fts3