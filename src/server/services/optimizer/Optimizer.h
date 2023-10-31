/*
 * Copyright (c) CERN 2013-2016
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

#ifndef FTS3_OPTIMIZER_H
#define FTS3_OPTIMIZER_H

#include <list>
#include <map>
#include <string>

#include <boost/noncopyable.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/timer/timer.hpp>
#include <db/generic/LinkConfig.h>
#include <db/generic/Pair.h>
#include <msg-bus/producer.h>

#include "common/Uri.h"


namespace fts3 {
namespace optimizer {


struct Range {
    int min, max;
    // Set to true if min,max is configured specifically, or is a *->* configuration
    bool specific;
    // Set to true if min,max is configured with SE limits instead of link configuration
    bool storageSpecific;

    Range(): min(0), max(0), specific(false), storageSpecific(false) {}
};


struct StorageLimits {
    int source, destination;
    double throughputSource, throughputDestination;

    StorageLimits(): source(0), destination(0),
              throughputSource(0), throughputDestination(0) {}
};

struct PairState {
    time_t timestamp;
    double throughput;
    time_t avgDuration;
    double successRate;
    int retryCount;
    int activeCount;
    int queueSize;
    // Exponential Moving Average
    double ema;
    // Filesize statistics
    double filesizeAvg, filesizeStdDev;
    // Optimizer last decision
    int connections;
    double avgTput;
    
    PairState(): timestamp(0), throughput(0), avgDuration(0), successRate(0), retryCount(0), activeCount(0),
                 queueSize(0), ema(0), filesizeAvg(0), filesizeStdDev(0), connections(1), avgTput(0) {}

    PairState(time_t ts, double thr, time_t ad, double sr, int rc, int ac, int qs, double ema, int conn):
        timestamp(ts), throughput(thr), avgDuration(ad), successRate(sr), retryCount(rc),
        activeCount(ac), queueSize(qs), ema(ema), filesizeAvg(0), filesizeStdDev(0), connections(conn),
        avgTput(0) {}
};

struct StorageState {
    double asSourceThroughput;
    double asSourceThroughputInst;
    double asDestThroughput;
    double asDestThroughputInst;

    int inbound_max_active;
    double inbound_max_throughput;
    int outbound_max_active;
    double outbound_max_throughput;

    StorageState(): asSourceThroughput(0), asSourceThroughputInst(0),
                    asDestThroughput(0), asDestThroughputInst(0),
                    inbound_max_active(0), inbound_max_throughput(0),
                    outbound_max_active(0), outbound_max_throughput(0) {}
    
    StorageState(int ia, double it, int oa, double ot):
        inbound_max_active(ia), inbound_max_throughput(it),
        outbound_max_active(oa), outbound_max_throughput(ot),
        asSourceThroughput(0), asSourceThroughputInst(0),
        asDestThroughput(0), asDestThroughputInst(0) {}
};

// To decouple the optimizer core logic from the data storage/representation
class OptimizerDataSource {
public:
    virtual ~OptimizerDataSource()
    {}

    // Return a list of pairs with active or submitted transfers
    virtual std::list<Pair> getActivePairs(void) = 0;

    // Get active throughput and connection limits for every SE  
    virtual void dumpStorageStates(std::map<std::string, StorageState> *currentSEStateMap) = 0;

    // Return the optimizer configuration value
    virtual OptimizerMode getOptimizerMode(const std::string &source, const std::string &dest) = 0;

    // Get configured storage and pair limits
    virtual void getPairLimits(const Pair &pair, Range *range) = 0;

    // Get the stored optimizer value (current value)
    virtual int getOptimizerValue(const Pair&) = 0;

    // Get the weighted throughput for the pair
    virtual void getThroughputInfo(const Pair &, const boost::posix_time::time_duration &,
        double *throughput, double *filesizeAvg, double *filesizeStdDev) = 0;

    virtual time_t getAverageDuration(const Pair&, const boost::posix_time::time_duration&) = 0;

    // Get the success rate for the pair
    virtual double getSuccessRateForPair(const Pair&, const boost::posix_time::time_duration&, int *retryCount) = 0;

    // Get the number of transfers in the given state
    virtual int getActive(const Pair&) = 0;
    virtual int getSubmitted(const Pair&) = 0;

    // Get current throughput
    virtual double getThroughputAsSource(const std::string&, const boost::posix_time::time_duration&) = 0;
    virtual double getThroughputAsSourceInst(const std::string&) = 0;
    virtual double getThroughputAsDestination(const std::string&, const boost::posix_time::time_duration&) = 0;
    virtual double getThroughputAsDestinationInst(const std::string&) = 0;

    // Permanently register the optimizer decision
    virtual void storeOptimizerDecision(const Pair &pair, int activeDecision,
        const PairState &newState, int diff, const std::string &rationale) = 0;

    // Permanently register the number of streams per active
    virtual void storeOptimizerStreams(const Pair &pair, int streams) = 0;
};

// Used by the optimizer to notify decisions
class OptimizerCallbacks {
public:
    virtual void notifyDecision(const Pair &pair, int decision, const PairState &current,
        int diff, const std::string &rationale) = 0;
};

/*
 * Conceptually the optimizer is a dynamic system, of the form
 * y(n) = f( x(n-1), x(n), y(n-1) ),
 * where n: current interval
 *       x(n): input state
 *       y(n): persistent, decision state
 * 
 * Note that the input state can be broken down into three components
 *     x1(n): is local and will be used only by a pair
 *     x2(n): is also local, but will be needed to compute x3
 *     x3(n): more global state and depends on the sum of x2(n)
 * 
 * At the moment, the optimizer has the following structure 
 *     y(n) = f( x1(n), x2(n), x3(n), y(n-1))
 * 
 * But for sake of clarity, we save the entire vectors:
 *     x_{1,2}(n-1) in memoryPairStateMap
 *     x_{1,2}(n) in currentPairStateMap
 *     x_{3}(n) in currentSEStateMap
*/
// Optimizer implementation
class Optimizer: public boost::noncopyable {
protected:
    std::map<Pair, PairState> memoryPairStateMap;
    std::map<Pair, PairState> currentPairStateMap;
    std::map<std::string, StorageState> currentSEStateMap;

    OptimizerDataSource *dataSource;
    OptimizerCallbacks *callbacks;
    boost::posix_time::time_duration optimizerSteadyInterval;
    int maxNumberOfStreams;
    int maxSuccessRate;
    int lowSuccessRate;
    int baseSuccessRate;

    int decreaseStepSize;
    int increaseStepSize, increaseAggressiveStepSize;
    double emaAlpha;

    // Read currentSEStateMap values into a StorageLimits object for the purposes of a single pair.
    void getStorageLimits(const Pair &pair, StorageLimits *limits);

    // Run the optimization algorithm for the number of connections.
    // Returns true if a decision is stored
    bool optimizeConnectionsForPair(OptimizerMode optMode, const Pair &);

    // Run the optimization algorithm for the number of streams.
    void optimizeStreamsForPair(OptimizerMode optMode, const Pair &);

    // Stores into rangeActiveMin and rangeActiveMax the working range for the optimizer
    void getOptimizerWorkingRange(const Pair &pair, const StorageLimits &limits, Range *range);

    // Updates decision
    void setOptimizerDecision(const Pair &pair, int decision, const PairState &current,
        int diff, const std::string &rationale, boost::timer::cpu_times elapsed);

    // Gets and saves current performance on all pairs and storage elements 
    // in currentPairStateMap and currentSEStateMap
    void getCurrentIntervalInputState(const std::list<Pair> &);
public:
    Optimizer(OptimizerDataSource *ds, OptimizerCallbacks *callbacks);
    ~Optimizer();

    // Calculates the ideal window size for throughput estimation
    boost::posix_time::time_duration calculateTimeFrame(time_t avgDuration);

    void setSteadyInterval(boost::posix_time::time_duration);
    void setMaxNumberOfStreams(int);
    void setMaxSuccessRate(int);
    void setLowSuccessRate(int);
    void setBaseSuccessRate(int);
    void setStepSize(int increase, int increaseAggressive, int decrease);
    void setEmaAlpha(double);
    void updateDecisions(const std::list<Pair> &);
    void run(void);
    void runOptimizerForPair(const Pair&);
};


inline std::ostream& operator << (std::ostream &os, const Range &range) {
    return (os << range.min << "/" << range.max);
}

}
}

#endif // FTS3_OPTIMIZER_H