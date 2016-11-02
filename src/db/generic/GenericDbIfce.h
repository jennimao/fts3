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

#pragma once
#ifndef GENERICDBIFCE_H_
#define GENERICDBIFCE_H_

#include <list>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "JobStatus.h"
#include "common/JobParameterHandler.h"
#include "FileTransferStatus.h"
#include "SeConfig.h"
#include "SeGroup.h"
#include "SeProtocolConfig.h"
#include "QueueId.h"
#include "LinkConfig.h"
#include "ShareConfig.h"
#include "CloudStorageAuth.h"
#include "TransferState.h"

#include <boost/tuple/tuple.hpp>
#include <boost/optional.hpp>

#include "DeleteOperation.h"
#include "Job.h"
#include "MinFileStatus.h"
#include "StagingOperation.h"
#include "StorageElement.h"
#include "TransferFile.h"
#include "UserCredential.h"
#include "UserCredentialCache.h"

#include "msg-bus/events.h"

#include "server/services/optimizer/Optimizer.h"


/// Hold information about individual submitted transfers
struct SubmittedTransfer
{
    SubmittedTransfer(): filesize(0), fileIndex(0), hashedId(0) {}
    std::string source;
    std::string destination;
    std::string sourceSe;
    std::string destSe;
    std::string checksum;
    double filesize;
    std::string metadata;
    std::string selectionStrategy;
    int fileIndex;
    boost::optional<int> waitTimeout;
    std::string activity;
    std::string state;
    unsigned hashedId;
};

///
class GenericDbIfce
{
public:

    virtual ~GenericDbIfce() {};

    /// Initialize database connection by providing information from fts3config file
    /// @param nPooledConnections   The number connections to pool
    virtual void init(const std::string& username, const std::string& password,
            const std::string& connectString, int nPooledConnections) = 0;

    /// Recover from the DB transfers marked as ACTIVE for the host 'host'
    virtual std::list<fts3::events::MessageUpdater> getActiveInHost(const std::string &host) = 0;

    /// Get a list of transfers ready to go for the given queues
    /// When session reuse is enabled for a job, all the files belonging to that job should run at once
    /// @param queues       Queues for which to check (see getQueuesWithSessionReusePending)
    /// @param[out] files   A map where the key is the VO. The value is a queue of pairs (jobId, list of transfers)
    virtual void getReadySessionReuseTransfers(const std::vector<QueueId>& queues,
            std::map< std::string, std::queue< std::pair<std::string, std::list<TransferFile>>>>& files) = 0;

    /// Get a list of transfers ready to go for the given queues
    /// @param queues       Queues for which to check (see getQueuesWithPending)
    /// @param[out] files   A map where the key is the VO. The value is a list of transfers belonging to that VO
    virtual void getReadyTransfers(const std::vector<QueueId>& queues,
            std::map< std::string, std::list<TransferFile>>& files) = 0;

    /// Get a list of multihop jobs ready to go
    /// @param[out] files   A map where the key is the VO. The value is a queue of pairs (jobId, list of transfers)
    virtual void getMultihopJobs(std::map< std::string, std::queue<std::pair<std::string, std::list<TransferFile>>>>& files) = 0;

    /// Update the status of a transfer
    /// @param jobId            The job ID
    /// @param fileId           The file ID
    /// @param throughput       Transfer throughput
    /// @param transferState    Transfer statue
    /// @param errorReason      For failed states, the error message
    /// @param processId        fts_url_copy process ID running the transfer
    /// @param filesize         Actual filesize reported by the storage
    /// @param duration         How long (in seconds) took to transfer the file
    /// @param retry            If the error is considered recoverable by fts_url_copy
    /// @return                 true if an updated was done into the DB, false otherwise
    ///                         (i.e. trying to set ACTIVE an already ACTIVE transfer)
    /// @note                   If jobId is empty, or if fileId is 0, then processId will be used to decide
    ///                         which transfers to update
    virtual bool updateTransferStatus(const std::string& jobId, int fileId, double throughput,
            const std::string& transferState, const std::string& errorReason,
            int processId, double filesize, double duration, bool retry) = 0;

    /// Update the status of a job
    /// @param jobId            The job ID
    /// @param jobState         The job state
    /// @param pid              The PID of the fts_url_copy process
    /// @note                   If jobId is empty, the pid will be used to decide which job to update
    virtual bool updateJobStatus(const std::string& jobId, const std::string& jobState, int pid) = 0;

    /// Get the credentials associated with the given delegation ID and user
    /// @param delegationId     Delegation ID. See insertCredentialCache
    /// @param userDn           The user's DN
    /// @return                 The delegated credentials, if any
    virtual boost::optional<UserCredential> findCredential(const std::string& delegationId, const std::string& userDn) = 0;

    /// Check if the credential for the given delegation id and user is expired or not
    /// @param delegationId     Delegation ID. See insertCredentialCache
    /// @param userDn           The user's DN
    /// @return                 true if the stored credentials expired or do not exist
    virtual bool isCredentialExpired(const std::string& delegationId, const std::string &userDn) = 0;

    /// Get the debug level for the given pair
    /// @param sourceStorage    The source storage as protocol://host
    /// @param destStorage      The destination storage as protocol://host
    /// @return                 An integer with the debug level configured for the pair. 0 = no debug.
    virtual unsigned getDebugLevel(const std::string& sourceStorage, const std::string& destStorage) = 0;

    /// Check whether submission is allowed for the given storage
    /// @param storage          The storage to blacklist (as protocol://host)
    /// @param voName           The submitting VO
    virtual bool allowSubmit(const std::string& storage, const std::string& voName) = 0;

    /// Check is the user has been banned
    virtual bool isDnBlacklisted(const std::string& userDn) = 0;

    /// Optimizer data source
    virtual fts3::optimizer::OptimizerDataSource* getOptimizerDataSource() = 0;

    /// Checks if there are available slots to run transfers for the given pair
    /// @param sourceStorage        The source storage  (as protocol://host)
    /// @param destStorage          The destination storage  (as protocol://host)
    /// @param[out] currentActive   The current number of running transfers is put here
    virtual bool isTrAllowed(const std::string& sourceStorage, const std::string& destStorage, int &currentActive) = 0;

    /// Returns how many outbound connections a storage has towards the given set of destinations
    /// @param source       The origin of the connections
    /// @param destination  A set of destinations for the connections
    virtual int getSeOut(const std::string & source, const std::set<std::string> & destination) = 0;

    /// Returns how many inbound connections a storage has from the given set of sources
    /// @param source       A set of sources for the connections
    /// @param destination  The destination of the connections
    virtual int getSeIn(const std::set<std::string> & source, const std::string & destination) = 0;

    /// Mark a reuse or multihop job (and its files) as failed
    /// @param jobId    The job id
    /// @param pid      The PID of the fts_url_copy
    /// @param message  The error message
    /// @note           If jobId is empty, the implementation may look for the job bound to the pid.
    ///                 Note that I am not completely sure you can get an empty jobId.
    virtual bool terminateReuseProcess(const std::string & jobId, int pid, const std::string & message) = 0;

    /// Goes through transfers marked as 'ACTIVE' and make sure the timeout didn't expire
    /// @param[out] collectJobs A map of fileId with its corresponding jobId that have been cancelled
    virtual void forceFailTransfers(std::map<int, std::string>& collectJobs) = 0;

    /// Set the PID for all the files inside a reuse or multihop job
    /// @param jobId    The job id for which the files will be updated
    /// @param pid      The process ID
    /// @note           Transfers within reuse and multihop jobs go all together to a single fts_url_copy process
    virtual void setPidForJob(const std::string& jobId, int pid) = 0;

    /// Search for transfers stuck in 'READY' state and revert them to 'SUBMITTED'
    /// @note   AFAIK 'READY' only applies for reuse and multihop, but inside
    ///         MySQL reuse seems to be explicitly filtered out, so I am not sure
    ///         how much is this method actually doing
    virtual void revertToSubmitted() = 0;

    /// Moves old transfer and job records to the archive tables
    /// Delete old entries in other tables (i.e. t_optimize_evolution)
    /// @param[in]          How many jobs per iteration must be processed
    /// @param[out] nJobs   How many jobs have been moved
    /// @param[out] nFiles  How many files have been moved
    virtual void backup(long bulkSize, long* nJobs, long* nFiles) = 0;

    /// Mark all the transfers as failed because the process fork failed
    /// @param jobId    The job id for which url copy failed to fork
    /// @note           This method is used only for reuse and multihop jobs
    virtual void forkFailed(const std::string& jobId) = 0;

    /// Mark the files contained in 'messages' as stalled (FAILED)
    /// @param messages Only file_id, job_id and process_id from this is used
    /// @param diskFull Set to true if there are no messages because the disk is full
    virtual bool markAsStalled(const std::vector<fts3::events::MessageUpdater>& messages, bool diskFull) = 0;

    /// Return true if the group 'groupName' exists
    virtual bool checkGroupExists(const std::string & groupName) = 0;

    /// @return the group to which storage belong
    /// @note   It will be the empty string if there is no group
    virtual std::string getGroupForSe(const std::string storage) = 0;

    /// Get the link configuration for the link defined by the source and destination given
    virtual std::unique_ptr<LinkConfig> getLinkConfig(const std::string &source, const std::string &destination) = 0;

    /// Register a new VO share configuration
    virtual void addShareConfig(const ShareConfig& cfg) = 0;

    /// Get the VO share configuration for the given link and VO
    virtual std::unique_ptr<ShareConfig> getShareConfig(const std::string &source, const std::string &destination,
        const std::string &vo) = 0;

    /// Get the list of VO share configurations for the given link
    virtual std::vector<ShareConfig> getShareConfig(const std::string &source, const std::string &destination) = 0;

    /// Register in the DB that the given file ID has been scheduled for a share configuration
    virtual void addFileShareConfig(int fileId, const std::string &source, const std::string &destination,
        const std::string &vo) = 0;

    /// Returns how many active transfers there is for the given link and VO
    virtual int countActiveTransfers(const std::string &source, const std::string &destination,
        const std::string &vo) = 0;

    /// Returns how many outbound transfers there is from the given storage and VO
    virtual int countActiveOutboundTransfersUsingDefaultCfg(const std::string &se, const std::string &vo) = 0;

    /// Returns how many inbound transfers there is towards the given storage for the given VO
    virtual int countActiveInboundTransfersUsingDefaultCfg(const std::string &se, const std::string &vo) = 0;

    /// Returns the total value of all the shares for the given link and set of VO
    virtual int sumUpVoShares(const std::string &source, const std::string &destination,
        const std::set<std::string> &vos) = 0;

    /// Returns how many retries there is configured for the given jobId
    virtual int getRetry(const std::string & jobId) = 0;

    /// Returns how many thime the given file has been already retried
    virtual int getRetryTimes(const std::string & jobId, int fileId) = 0;

    /// Set to FAIL jobs that have been in the queue for more than its max in queue time
    /// @param jobs An output parameter, where the set of expired job ids is stored
    virtual void setToFailOldQueuedJobs(std::vector<std::string>& jobs) = 0;

    /// Update the protocol parameters used for each transfer
    virtual void updateProtocol(const std::vector<fts3::events::Message>& tempProtocol) = 0;

    /// Get the state the transfer identified by jobId/fileId
    virtual std::vector<TransferState> getStateOfTransfer(const std::string& jobId, int fileId) = 0;

    /// Cancel files that have been set to wait, but the wait time expired
    /// @param jobs An output parameter, where the set of expired job ids is stored
    virtual void cancelWaitingFiles(std::set<std::string>& jobs) = 0;

    // TODO: UNUSED
    virtual void revertNotUsedFiles() = 0;

    /// Run a set of sanity checks over the database, logging potential inconsistencies and logging them
    virtual void checkSanityState() = 0;

    /// Checks if the database schema has been loaded
    virtual void checkSchemaLoaded() = 0;

    /// Add a new retry to the transfer identified by fileId
    /// @param jobId    Job identifier
    /// @param fileId   Transfer identifier
    /// @param reason   String representation of the failure
    /// @param errcode  An integer representing the failure
    virtual void setRetryTransfer(const std::string & jobId, int fileId, int retry, const std::string& reason,
        int errcode) = 0;

    /// Bulk update of transfer progress
    virtual void updateFileTransferProgressVector(const std::vector<fts3::events::MessageUpdater> &messages) = 0;

    /// Bulk update for log files
    virtual void transferLogFileVector(std::map<int, fts3::events::MessageLog>& messagesLog) = 0;

    /**
     * Signals that the server is alive
     * The total number of running (alive) servers is put in count
     * The index of this specific machine is put in index
     * A default implementation is provided, as this is used for optimization,
     * so it is not mandatory.
     * start and end are set to the interval of hash values this host will process
     */
    virtual void updateHeartBeat(unsigned* index, unsigned* count, unsigned* start, unsigned* end, std::string service_name)
    {
        *index = 0;
        *count = 1;
        *start = 0x0000;
        *end   = 0xFFFF;
        service_name = std::string("");
    }

    /// Update the state of a trnasfer inside a session reuse job
    virtual unsigned int updateFileStatusReuse(const TransferFile &file, const std::string &status) = 0;

    /// Puts into requestIDs, jobs that have been cancelled, and for which the running fts_url_copy must be killed
    virtual void getCancelJob(std::vector<int>& requestIDs) = 0;

    /// Returns if this host has been set to drain
    virtual bool getDrain() = 0;

    /// Returns if for the given link, UDT has been enabled
    virtual bool isProtocolUDT(const std::string &sourceSe, const std::string &destSe) = 0;

    /// Returns if for the given link, IPv6 has been enabled
    virtual bool isProtocolIPv6(const std::string &sourceSe, const std::string &destSe) = 0;

    /// Returns how many streams must be used for the given link
    virtual int getStreamsOptimization(const std::string &voName,
        const std::string &sourceSe, const std::string &destSe)= 0;

    /// Returns the globally configured transfer timeout
    virtual int getGlobalTimeout(const std::string &voName) = 0;

    /// Returns how many seconds must be added to the timeout per MB to be transferred
    virtual int getSecPerMb(const std::string &voName) = 0;

    /// Returns the optimizer level for the TCP buffersize
    virtual int getBufferOptimization() = 0;

    /// Puts into the vector queue the Queues for which there are pending transfers
    virtual void getQueuesWithPending(std::vector<QueueId>& queues) = 0;

    /// Puts into the vector queues the Queues for which there are session-reuse pending transfers
    virtual void getQueuesWithSessionReusePending(std::vector<QueueId>& queues) = 0;

    /// Updates the status for delete operations
    /// @param delOpsStatus  Update for files in delete or started
    virtual void updateDeletionsState(const std::vector<MinFileStatus>& delOpsStatus) = 0;

    /// Gets a list of delete operations in the queue
    /// @params[out] delOps A list of namespace operations (deletion)
    virtual void getFilesForDeletion(std::vector<DeleteOperation>& delOps) = 0;

    /// Revert namespace operations already in 'STARTED' back to the 'DELETE'
    /// state, so they re-enter the queue
    virtual void requeueStartedDeletes() = 0;

    /// Updates the status for staging operations
    /// @param stagingOpStatus  Update for files in staging or started
    virtual void updateStagingState(const std::vector<MinFileStatus>& stagingOpStatus) = 0;

    /// Update the bring online token for the given set of transfers
    /// @param jobs     A map where the key is the job id, and the value another map where the key is a surl, and the
    ///                     value a file id
    /// @param token    The SRM token
    virtual void updateBringOnlineToken(const std::map< std::string, std::map<std::string, std::vector<int> > > &jobs,
        const std::string &token) = 0;

    /// Get staging operations ready to be started
    /// @params[out] stagingOps The list of staging operations will be put here
    virtual void getFilesForStaging(std::vector<StagingOperation> &stagingOps) = 0;

    /// Get staging operations already started
    /// @params[out] stagingOps The list of started staging operations will be put here
    virtual void getAlreadyStartedStaging(std::vector<StagingOperation> &stagingOps) = 0;

    /// Put into files a set of bring online requests that must be cancelled
    /// @param files    Each entry in the set if a pair of surl / token
    virtual void getStagingFilesForCanceling(std::set< std::pair<std::string, std::string> >& files) = 0;

    /// Retrieve the credentials for a cloud storage endpoint for the given user/VO
    virtual bool getCloudStorageCredentials(const std::string& userDn,
                                     const std::string& voName,
                                     const std::string& cloudName,
                                     CloudStorageAuth& auth) = 0;

    /// Get if the user dn should be visible or not in the messaging
    virtual bool publishUserDn(const std::string &vo) = 0;
};

#endif // GENERICDBIFCE_H_
