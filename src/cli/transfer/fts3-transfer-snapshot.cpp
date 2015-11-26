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

#include "ServiceAdapterFallbackFacade.h"

#include "MsgPrinter.h"
#include "ui/SnapshotCli.h"

#include "exception/cli_exception.h"
#include "JsonOutput.h"

#include <iostream>
#include <string>

#include <boost/property_tree/json_parser.hpp>

using namespace fts3::cli;


/**
 * This is the entry point for the fts3-transfer-cancel command line tool.
 */
int main(int ac, char* av[])
{
    try
        {
            SnapshotCli cli;
            // create and initialise the command line utility
            cli.parse(ac, av);
            if (cli.printHelp()) return 0;
            cli.validate();

            ServiceAdapterFallbackFacade ctx(cli.getService(), cli.capath(), cli.proxy());

            cli.printApiDetails(ctx);

            std::string src = cli.getSource(), dst = cli.getDestination(), vo = cli.getVo();

            std::vector<Snapshot> resp = ctx.getSnapShot(vo, src, dst);

            MsgPrinter::instance().print(resp);
        }
    catch(cli_exception const & ex)
        {
            MsgPrinter::instance().print(ex);
            return 1;
        }
    catch(std::exception& ex)
        {
            MsgPrinter::instance().print(ex);
            return 1;
        }
    catch(...)
        {
            MsgPrinter::instance().print("error", "exception of unknown type!");
            return 1;
        }

    return 0;
}
