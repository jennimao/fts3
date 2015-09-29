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

#include "JobCancelHandler.h"

#include "../CGsiAdapter.h"
#include "../AuthorizationManager.h"
#include "../SingleTrStateInstance.h"

#include "common/JobStatusHandler.h"

#include <algorithm>

#include <boost/bind.hpp>

#include "../../../../../common/Exceptions.h"
#include "common/Logger.h"

namespace fts3
{
namespace ws
{

std::string const JobCancelHandler::CANCELED = "CANCELED";
std::string const JobCancelHandler::DOES_NOT_EXIST = "DOES_NOT_EXIST";

void JobCancelHandler::cancel()
{
    // jobs that will be cancelled
    std::vector<std::string> cancel, cancel_dm;
    // get the user DN
    CGsiAdapter cgsi (ctx);
    std::string const dn = cgsi.getClientDn();
    FTS3_COMMON_LOGGER_NEWLOG (INFO) << "DN: " << dn << "is cancelling transfer jobs: ";
    // iterate over all jobs and check if they are suitable for cancelling
    std::vector<std::string>::const_iterator it;
    for (it = jobs.begin(); it != jobs.end(); ++it)
        {
            std::string const & job = *it;
            std::string status = get_state(job, dn);
            if (status == DOES_NOT_EXIST)
                throw UserError("Transfer job: " + job + " does not exist!");
            else if (status != CANCELED)
                throw UserError("Transfer job: " + job + " cannot be cancelled (it is in " + status + " state)");
            // if the job is OK add it to respective vector
            if (db.isDmJob(job))
                cancel_dm.push_back(job);
            else
                cancel.push_back(job);
            FTS3_COMMON_LOGGER_NEWLOG (INFO) << job << ", ";
        }
    FTS3_COMMON_LOGGER_NEWLOG (INFO) << commit;
    // cancel the jobs
    if (!cancel.empty())
        {
            db.cancelJob(cancel);
            // and send a state changed message to monitoring
            std::for_each(cancel.begin(), cancel.end(), boost::bind(&JobCancelHandler::send_msg, this, _1));
        }
    if (!cancel_dm.empty())
        {
            db.cancelDmJobs(cancel_dm);
            // and send a state changed message to monitoring
            std::for_each(cancel_dm.begin(), cancel_dm.end(), boost::bind(&JobCancelHandler::send_msg, this, _1));
        }
}

void JobCancelHandler::cancel(impltns__cancel2Response & resp)
{
    // create the response
    resp._jobIDs = soap_new_impltns__ArrayOf_USCOREsoapenc_USCOREstring(ctx, -1);
    resp._status = soap_new_impltns__ArrayOf_USCOREsoapenc_USCOREstring(ctx, -1);
    // use references for convenience
    std::vector<std::string> & resp_jobs = resp._jobIDs->item;
    std::vector<std::string> & resp_stat = resp._status->item;
    // jobs that will be cancelled
    std::vector<std::string> cancel, cancel_dm;
    // get the user DN
    CGsiAdapter cgsi (ctx);
    std::string const dn = cgsi.getClientDn();
    FTS3_COMMON_LOGGER_NEWLOG (INFO) << "DN: " << dn << "is cancelling transfer jobs: ";
    // iterate over all jobs and check if they are suitable for cancelling
    std::vector<std::string>::const_iterator it;
    for (it = jobs.begin(); it != jobs.end(); ++it)
        {
            std::string const & job = *it;
            std::string status = get_state(job, dn);
            resp_jobs.push_back(job);
            resp_stat.push_back(status);
            if (status == CANCELED)
                {
                    if (db.isDmJob(job))
                        cancel_dm.push_back(job);
                    else
                        cancel.push_back(job);
                    FTS3_COMMON_LOGGER_NEWLOG (INFO) << job << ", ";
                }
        }
    // in case no job was suitable for cancelling
    if (cancel.empty() && cancel_dm.empty())
        {
            FTS3_COMMON_LOGGER_NEWLOG (INFO) << " not a single job was suitable for cancelling ";
            return;
        }
    FTS3_COMMON_LOGGER_NEWLOG (INFO) << commit;
    // cancel the jobs
    if (!cancel.empty())
        {
            db.cancelJob(cancel);
            // and send a state changed message to monitoring
            std::for_each(cancel.begin(), cancel.end(), boost::bind(&JobCancelHandler::send_msg, this, _1));
        }
    if (!cancel_dm.empty())
        {
            db.cancelDmJobs(cancel_dm);
            // and send a state changed message to monitoring
            std::for_each(cancel_dm.begin(), cancel_dm.end(), boost::bind(&JobCancelHandler::send_msg, this, _1));
        }

}

std::string JobCancelHandler::get_state(std::string const & jobId, std::string const & dn)
{
    // get the transfer job object from DB
    boost::optional<Job> job (db.getJob(jobId, false));
    // if not throw an exception
    if (!job) return DOES_NOT_EXIST;
    // Authorise the operation
    AuthorizationManager::instance().authorize(ctx, AuthorizationManager::TRANSFER, job.get_ptr());
    // get the status
    std::string const & status = job->jobState;
    // make sure the transfer-job is not in terminal state
    if (JobStatusHandler::instance().isTransferFinished(status))
        {
            return status;
        }

    return CANCELED;
}

void JobCancelHandler::send_msg(std::string const & job)
{
    std::vector<int> files;
    db.getFilesForJobInCancelState(job, files);

    std::vector<int>::const_iterator it;
    for (it = files.begin(); it != files.end(); ++it)
        {
            fts3::server::SingleTrStateInstance::instance().sendStateMessage(job, *it);
        }
}

}
}
