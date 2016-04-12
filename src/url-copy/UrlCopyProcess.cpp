/*
 * Copyright (c) CERN 2016
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

#include <boost/filesystem/operations.hpp>
#include "common/Logger.h"

#include "LogHelper.h"
#include "heuristics.h"
#include "AutoInterruptThread.h"
#include "UrlCopyProcess.h"
#include "version.h"

using fts3::common::commit;


static void setupGlobalGfal2Config(const UrlCopyOpts &opts, Gfal2 &gfal2)
{
    if (!opts.oauthFile.empty()) {
        try {
            gfal2.loadConfigFile(opts.oauthFile);
            unlink(opts.oauthFile.c_str());
        }
        catch (const std::exception &ex) {
            FTS3_COMMON_LOGGER_NEWLOG(ERR) << "Could not load OAuth config file: " << ex.what() << commit;
        }
    }

    gfal2.set("GRIDFTP PLUGIN", "SESSION_REUSE", true);
    gfal2.set("GRIDFTP PLUGIN", "ENABLE_UDT", opts.enableUdt);
    gfal2.set("GRIDFTP PLUGIN", "IPV6", opts.enableIpv6);


    if (opts.infosys.compare("false") == 0) {
        gfal2.set("BDII", "ENABLED",false);
    }
    else {
        gfal2.set("BDII", "ENABLED",true);
        gfal2.set("BDII", "LCG_GFAL_INFOSYS", opts.infosys);
    }

    gfal2.setUserAgent("fts_url_copy", VERSION);

    gfal2.set("X509", "CERT", opts.proxy);
    gfal2.set("X509", "KEY", opts.proxy);
}


UrlCopyProcess::UrlCopyProcess(const UrlCopyOpts &opts, Reporter &reporter):
    opts(opts), reporter(reporter), canceled(false), multihopFailed(false), timeoutExpired(false)
{
    todoTransfers = opts.transfers;
    setupGlobalGfal2Config(opts, gfal2);
}


static void setupTransferConfig(const UrlCopyOpts &opts, const Transfer &transfer,
    Gfal2 &gfal2, Gfal2TransferParams &params)
{
    params.setStrictCopy(opts.strictCopy);
    params.setCreateParentDir(true);
    params.setReplaceExistingFile(opts.overwrite);

    if (!transfer.sourceTokenDescription.empty()) {
        params.setSourceSpacetoken(transfer.sourceTokenDescription);
    }
    if (!transfer.destTokenDescription.empty()) {
        params.setDestSpacetoken(transfer.destTokenDescription);
    }

    switch (transfer.checksumMethod) {
        case Transfer::kChecksumStrict:
            params.enableChecksum(true);
            gfal2.set("SRM PLUGIN", "ALLOW_EMPTY_SOURCE_CHECKSUM", false);
            gfal2.set("GRIDFTP PLUGIN", "SKIP_SOURCE_CHECKSUM", false);
            gfal2.set("XROOTD PLUGIN", "COPY_CHECKSUM_MODE", "end2end");
            break;
        case Transfer::kChecksumRelaxed:
            params.enableChecksum(true);
            gfal2.set("SRM PLUGIN", "ALLOW_EMPTY_SOURCE_CHECKSUM", true);
            gfal2.set("GRIDFTP PLUGIN", "SKIP_SOURCE_CHECKSUM", true);
            gfal2.set("XROOTD PLUGIN", "COPY_CHECKSUM_MODE", "target");
            break;
        case Transfer::kChecksumDoNotCheck:
            params.enableChecksum(false);
            break;
    }

    params.setUserDefinedChecksum(transfer.checksumAlgorithm, transfer.checksumValue);
}


static void timeoutTask(boost::posix_time::time_duration &duration, UrlCopyProcess *urlCopyProcess)
{
    try {
        boost::this_thread::sleep(duration);
        FTS3_COMMON_LOGGER_NEWLOG(WARNING) << "Timeout expired" << commit;
        urlCopyProcess->timeout();
    }
    catch (const boost::thread_interrupted&) {
        FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Timeout stopped" << commit;
    }
    catch (const std::exception &ex) {
        FTS3_COMMON_LOGGER_NEWLOG(ERR) << "Unexpected exception in the timeout task: " << ex.what() << commit;
    }
}


static void pingTask(Transfer *transfer, Reporter *reporter)
{
    try {
        while (!boost::this_thread::interruption_requested()) {
            boost::this_thread::sleep(boost::posix_time::seconds(60));
            reporter->sendPing(*transfer);
        }
    }
    catch (const boost::thread_interrupted&) {
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Ping stopped" << commit;
    }
    catch (const std::exception &ex) {
        FTS3_COMMON_LOGGER_NEWLOG(ERR) << "Unexpected exception in the ping task: " << ex.what() << commit;
    }
}


void UrlCopyProcess::runTransfer(Transfer &transfer, Gfal2TransferParams &params)
{
    // Log info
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Proxy: " << opts.proxy << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "User DN: " << opts.userDn << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "VO: " << opts.voName << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Job id: " << transfer.jobId << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "File id: " << transfer.fileId << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Source url: " << transfer.source << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Dest url: " << transfer.destination << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Overwrite enabled: " << opts.overwrite << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Dest space token: " << transfer.destTokenDescription << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Source space token: " << transfer.sourceTokenDescription << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Checksum: " << transfer.checksumValue << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Checksum enabled: " << transfer.checksumMethod << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "User filesize: " << transfer.userFileSize << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "File metadata: " << transfer.fileMetadata << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Job metadata: " << opts.jobMetadata << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Bringonline token: " << transfer.tokenBringOnline << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Multihop: " << opts.isMultihop << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "UDT: " << opts.enableUdt << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "BDII:" << opts.infosys << commit;

    if (opts.strictCopy) {
        FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Copy only transfer!" << commit;
        transfer.fileSize = transfer.userFileSize;
    }
    else {
        try {
            FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Getting source file size" << commit;
            transfer.fileSize = gfal2.stat(transfer.source).st_size;
            FTS3_COMMON_LOGGER_NEWLOG(INFO) << "File size: " << transfer.fileSize << commit;
        }
        catch (const Gfal2Exception &ex) {
            throw UrlCopyError(SOURCE, TRANSFER_PREPARATION, ex);
        }
        catch (const std::exception &ex) {
            throw UrlCopyError(SOURCE, TRANSFER_PREPARATION, EINVAL, ex.what());
        }

        if (!opts.overwrite) {
            try {
                FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Checking existence of destination file" << commit;
                gfal2.stat(transfer.destination);
                throw UrlCopyError(DESTINATION, TRANSFER_PREPARATION, EEXIST,
                    "Destination file exists and overwrite is not enabled");
            }
            catch (const Gfal2Exception &ex) {
                if (ex.code() != ENOENT) {
                    throw UrlCopyError(DESTINATION, TRANSFER_PREPARATION, ex);
                }
            }
        }
    }

    // Set protocol parameters
    params.setNumberOfStreams(opts.nStreams);
    params.setTcpBuffersize(opts.tcpBuffersize);

    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "TCP streams: " << params.getNumberOfStreams() << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "TCP buffer size: " << opts.tcpBuffersize << commit;

    reporter.sendProtocol(transfer, params);

    // Install callbacks
    params.addEventCallback(eventCallback, &transfer);
    params.addMonitorCallback(performanceCallback, &transfer);

    // Timeout
    unsigned timeout = opts.timeout;
    if (timeout == 0) {
        timeout = adjustTimeoutBasedOnSize(transfer.fileSize, opts.addSecPerMb) + 600;
    }

    timeoutExpired = false;
    AutoInterruptThread timeoutThread(
        boost::bind(&timeoutTask, boost::posix_time::seconds(timeout), this)
    );
    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Timeout set to: " << timeout << commit;

    // Ping thread
    AutoInterruptThread pingThread(boost::bind(&pingTask, &transfer, &reporter));

    // Transfer
    FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Starting transfer" << commit;
    try {
        gfal2.copy(params, transfer.source, transfer.destination);
    }
    catch (const Gfal2Exception &ex) {
        if (timeoutExpired) {
            throw UrlCopyError(TRANSFER, TRANSFER, ETIMEDOUT, ex.what());
        }
        else {
            throw UrlCopyError(TRANSFER, TRANSFER, ex);
        }
    }
    catch (const std::exception &ex) {
        throw UrlCopyError(TRANSFER, TRANSFER, EINVAL, ex.what());
    }

    // Validate destination size
    if (!opts.strictCopy) {
        uint64_t destSize;
        try {
            destSize = gfal2.stat(transfer.destination).st_size;
        }
        catch (const Gfal2Exception &ex) {
            throw UrlCopyError(DESTINATION, TRANSFER_FINALIZATION, ex);
        }
        catch (const std::exception &ex) {
            throw UrlCopyError(DESTINATION, TRANSFER_FINALIZATION, EINVAL, ex.what());
        }

        if (destSize != transfer.fileSize) {
            throw UrlCopyError(DESTINATION, TRANSFER_FINALIZATION, EINVAL,
                "Source and destination file size mismatch");
        }
        else {
            FTS3_COMMON_LOGGER_NEWLOG(INFO) << "DESTINATION Source and destination file size matching" << commit;
        }
    }
}


void UrlCopyProcess::run(void)
{
    while (!todoTransfers.empty() && !canceled && !multihopFailed) {

        Transfer transfer;
        {
            boost::lock_guard<boost::mutex> lock(transfersMutex);
            transfer = todoTransfers.front();
        }

        // Prepare gfal2 transfer parameters
        Gfal2TransferParams params;
        setupTransferConfig(opts, transfer, gfal2, params);

        // Prepare logging
        transfer.stats.process.start = Transfer::Statistics::timestampMilliseconds();
        transfer.logFile = generateLogPath(opts.logDir, transfer);
        if (opts.debugLevel) {
            transfer.debugLogFile = transfer.logFile + ".debug";
        }
        else {
            transfer.debugLogFile = "/dev/null";
        }

        if (!opts.logToStderr) {
            fts3::common::theLogger().redirect(transfer.logFile, transfer.debugLogFile);
        }

        // Notify we got it
        FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Transfer accepted" << commit;
        reporter.sendTransferStart(transfer, params);

        // Run the transfer
        try {
            runTransfer(transfer, params);
        }
        catch (const UrlCopyError &ex) {
            transfer.error.reset(new UrlCopyError(ex));
        }
        catch (const std::exception &ex) {
            transfer.error.reset(new UrlCopyError(AGENT, TRANSFER_SERVICE, EINVAL, ex.what()));
        }
        catch (...) {
            transfer.error.reset(new UrlCopyError(AGENT, TRANSFER_SERVICE, EINVAL, "Unknown exception"));
        }

        // Log error if any
        if (transfer.error) {
            if (transfer.error->isRecoverable()) {
                FTS3_COMMON_LOGGER_NEWLOG(ERR)
                    << "Recoverable error: [" << transfer.error->code() << "] "
                    << transfer.error->what()
                    << commit;
            }
            else {
                FTS3_COMMON_LOGGER_NEWLOG(ERR)
                << "Non recoverable error: [" << transfer.error->code() << "] "
                << transfer.error->what()
                << commit;
            }

            // Do not continue after this failure if this is a multihop transfer
            if (opts.isMultihop) {
                multihopFailed = true;
            }
        }
        else {
            FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Transfer finished succesfully" << commit;
        }

        // Archive log
        archiveLogs(transfer);

        // Notify back the final state
        transfer.stats.process.end = Transfer::Statistics::timestampMilliseconds();
        {
            boost::lock_guard<boost::mutex> lock(transfersMutex);
            doneTransfers.push_back(transfer);
            // todoTransfers may have been emptied by panic()
            if (!todoTransfers.empty()) {
                todoTransfers.pop_front();
                reporter.sendTransferCompleted(transfer, params);
            }
        }
    }

    // On cancellation, todoTransfers will not be empty, and a termination message must be sent
    // for them
    for (auto transfer = todoTransfers.begin(); transfer != todoTransfers.end(); ++transfer) {
        Gfal2TransferParams params;
        if (multihopFailed) {
            transfer->error.reset(new UrlCopyError(TRANSFER, TRANSFER_PREPARATION, EMULTIHOP,
                "Transfer canceled because a previous hop failed"));
        }
        else {
            transfer->error.reset(new UrlCopyError(TRANSFER, TRANSFER_PREPARATION, ECANCELED, "Transfer canceled"));
        }
        reporter.sendTransferCompleted(*transfer, params);
    }
}


void UrlCopyProcess::archiveLogs(Transfer &transfer)
{
    std::string archivedLogFile;

    try {
        archivedLogFile = generateArchiveLogPath(opts.logDir, transfer);
        boost::filesystem::rename(transfer.logFile, archivedLogFile);
        transfer.logFile = archivedLogFile;
    }
    catch (const std::exception &e) {
        FTS3_COMMON_LOGGER_NEWLOG(ERR) << "Failed to archive the log: "
            << e.what() << commit;
    }

    try {
        if (opts.debugLevel > 0) {
            std::string archivedDebugLogFile = archivedLogFile + ".debug";
            boost::filesystem::rename(transfer.debugLogFile, archivedDebugLogFile);
            transfer.debugLogFile = archivedDebugLogFile;
        }
    }
    catch (const std::exception &e) {
        FTS3_COMMON_LOGGER_NEWLOG(ERR) << "Failed to archive the debug log: "
            << e.what() << commit;
    }
}


void UrlCopyProcess::cancel(void)
{
    canceled = true;
    gfal2.cancel();
}


void UrlCopyProcess::timeout(void)
{
    timeoutExpired = true;
    gfal2.cancel();
}


void UrlCopyProcess::panic(const std::string &msg)
{
    boost::lock_guard<boost::mutex> lock(transfersMutex);
    for (auto transfer = todoTransfers.begin(); transfer != todoTransfers.end(); ++transfer) {
        Gfal2TransferParams params;
        transfer->error.reset(new UrlCopyError(AGENT, TRANSFER_SERVICE, EINTR, msg));
        reporter.sendTransferCompleted(*transfer, params);
    }
    todoTransfers.clear();
}