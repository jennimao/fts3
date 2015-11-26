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

#ifndef PYTHONPROXYDELEGATOR_H_
#define PYTHONPROXYDELEGATOR_H_

#include "ServiceAdapterFallbackFacade.h"
#include "MsgPrinter.h"

#include <sstream>

#include <boost/python.hpp>

namespace fts3
{
namespace cli
{

namespace py = boost::python;

class PythonProxyDelegator
{

public:
    PythonProxyDelegator(py::str endpoint, py::str delegationId, long expTime);
    virtual ~PythonProxyDelegator();

    void delegate();
    long isCertValid();

private:
    std::stringstream out;
    std::unique_ptr<ServiceAdapterFallbackFacade> delegator;
    std::string delegationId;
    long expTime;
};

}
}

#endif /* PYTHONPROXYDELEGATOR_H_ */
