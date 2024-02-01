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

struct NetLinkLimits {
    int active; // rename to connections
    double throughput; 

    NetLinkLimits(): active(0), throughput(0) {}
};

struct PairState {
    time_t timestamp;
    double throughput;
    time_t avgDuration;
    double successRate;
    int retryCount;

    // Actual number of concurrent slots at the time that the optimizer accesses the database 
    int activeSlots;

    // Average number of concurrent connections (slots) in the past time interval 
    // Note: This is based off of the average file count (activeSlots) 
    // it is only accurate if there are no streams (one slot = one file)
    float avgActiveSlots; 

    int queueSize;
    // Exponential Moving Average
    double ema;
    // Filesize statistics
    double filesizeAvg, filesizeStdDev;
    // Optimizer decision (for interval n)
    int optimizerDecision;
    // Proposed optimizer decision (for interval n+1)
    int proposedDecision; 

    std::string rationale;

    // Links in src-dest pair 
    std::list<std::string> netLinks; 

    // User specified pair weight 
    int weight;
    
    PairState(): timestamp(0), throughput(0), avgDuration(0), successRate(0), retryCount(0), activeSlots(0), avgActiveSlots(0), 
                 queueSize(0), ema(0), filesizeAvg(0), filesizeStdDev(0), optimizerDecision(1), proposedDecision(1), netLinks(0), 
                 weight(1), rationale("") {}

    PairState(time_t ts, double thr, time_t ad, double sr, int rc, int ac, int qs, double ema, int conn):
        timestamp(ts), throughput(thr), avgDuration(ad), successRate(sr), retryCount(rc),
        activeSlots(ac), queueSize(qs), ema(ema), avgActiveSlots(0), filesizeAvg(0), filesizeStdDev(0), proposedDecision(1), 
        optimizerDecision(conn), netLinks(0), weight(1), rationale("") {}
};


struct StorageState {

    double instThroughput; 
    double avgThroughput; 
    int numPairs; 
    double maxThroughput;
    int maxActive;

    // added metrics 
    double avgActiveSlots;
    int activeSlots;
    double successRate;
    double ema;


    //The following two variables store instantaneous inbound (asDest) and outbound (asSource) throughput for a given Storage element.
    //The "Inst" throughput values are calculated by the getThroughputAsSourceInst and getThroughputAsDestinationInst methods (in OptimizerDataSource.cpp)
    //The "Inst" values store throughput based on the number of active transfers at the time the "Inst" methods are called 
    /*double asSourceThroughputInst;
    double asDestThroughputInst;

    //The following two variables store the window based inbound (asDest) and outbound (asSource) throughput for a given Storage element.
    //These throughput values are calculated in getCurrentIntervalInputState (OptimizerConnections.cpp) by iterating through 
    //all the active pairs and summing the corresponding throughput values returned by getCurrentIntervalTransferInfo (OptimizerDataSource.cpp) 
    //for a source-destination pair that involves a given storage element
    double asSourceThroughput;
    double asDestThroughput;

    //The following two variables store the total inbound (asDest) and outbound (asSource) pairs for a given Storage elemnt 
    //These values are calculated in getCurrentIntervalInputState (OptimizerConnections.cpp) by iterating through 
    //all the active pairs that involves a given storage element
    int asSourceNumPairs;
    int asDestNumPairs; 

    //These values are storage limits for the given storage element
    //They are populated in getStorageStates (OptimizerDataSource.cpp) via querying t_se
    int inboundMaxActive;
    int outboundMaxActive;
    double inboundMaxThroughput;
    double outboundMaxThroughput;*/

    StorageState(): instThroughput(0), avgThroughput(0),
                    numPairs(0), maxThroughput(0), maxActive(0), 
                    avgActiveSlots(0), activeSlots(0), successRate(0), ema(0) {}
    
                    /*
                    asSourceThroughputInst(0), asDestThroughputInst(0),
                    asSourceThroughput(0), asDestThroughput(0), 
                    asSourceNumPairs(0), asDestNumPairs(0), 
                    inboundMaxActive(0), outboundMaxActive(0),
                    inboundMaxThroughput(0),outboundMaxThroughput(0) {}
                    */

    StorageState(int maxA, double maxTput): 
                    instThroughput(0), avgThroughput(0),
                    numPairs(0), maxThroughput(maxTput), maxActive(maxA), 
                    avgActiveSlots(0), activeSlots(0), successRate(0), ema(0) {}

                /*
                asSourceThroughputInst(0),asDestThroughputInst(0),
                asSourceThroughput(0), asDestThroughput(0),
                asSourceNumPairs(0), asDestNumPairs(0), 
                inboundMaxActive(ia), outboundMaxActive(oa),
                inboundMaxThroughput(it), outboundMaxThroughput(ot) {}
                */
};	


struct NetLinkState {

    // Stores instantaneous throughput for a given Netlink element 
    // The "Inst" throughput values are calculated by the getThroughputAsLinkInst (in OptimizerDataSource.cpp)
    // The "Inst" values store throughput based on the number of active transfers at the time the "Inst" methods are called 
    double throughputInst;

    // Stores the window based throughput for a given Netlink element 
    // This throughput value is calculated in getCurrentIntervalInputState (OptimizerConnections.cpp) by iterating through 
    // all the active pairs and summing the corresponding throughput and connection values returned by getCurrentIntervalTransferInfo (OptimizerDataSource.cpp) 
    // for a source-destination pair that involves a given link element
    double throughput;

    // Stores the total connections for a given Netlink element 
    // This is calculated in getCurrentIntervalInputState (OptimizerConnections.cpp) by iterating through 
    // all the active pairs and summing the corresponding connections based on the pair's estimated average
    // actual connections for a source-destination pair that involves a given NetLink element 
    int numPairs; 

    // These values are storage the limits for the given NetLink element 
    // They are populated in getNetLinkStates (OptimizerDataSource.cpp) via querying t_netlink_stat
    int minActive, maxActive;
    double maxThroughput;

    NetLinkState(): throughputInst(0), throughput(0), numPairs(0),
                    minActive(0), maxActive(0), maxThroughput(0) {}
    
    NetLinkState(int a, double t):
                throughputInst(0), throughput(0), numPairs(0), 
                minActive(0), maxActive(a), maxThroughput(t) {}
};

// To decouple the optimizer core logic from the data storage/representation
class OptimizerDataSource {
public:
    virtual ~OptimizerDataSource()
    {}

    // Return a list of pairs with active or submitted transfers
    virtual std::list<Pair> getActivePairs(void) = 0;

    // Get active throughput and connection limits for every SE  
    virtual void getStorageStates(std::map<std::string, std::vector<StorageState>> *currentSEStateMap) = 0;

    // Get active throughput and connection limits for every link  
    virtual void getNetLinkStates(std::map<std::string, NetLinkState> *currentLinkStateMap) = 0;

    // Return the optimizer configuration value
    virtual OptimizerMode getOptimizerMode(const std::string &source, const std::string &dest) = 0;

    // Get configured storage and pair limits
    virtual void getPairLimits(const Pair &pair, Range *range) = 0;

    // Get configured pair weight
    virtual int getPairWeight(const Pair &pair) = 0;

    // Get the stored optimizer value (current value)
    virtual int getOptimizerValue(const Pair&) = 0;

    // Get the weighted throughput for the pair
    virtual void getCurrentIntervalTransferInfo(const Pair &pair, const boost::posix_time::time_duration &interval, int currentActiveConnections,
        double *throughput, double *filesizeAvg, double *filesizeStdDev, float *avgConnections) = 0;

    virtual time_t getAverageDuration(const Pair&, const boost::posix_time::time_duration&) = 0;

    // Get the success rate for the pair
    virtual double getSuccessRateForPair(const Pair&, const boost::posix_time::time_duration&, int *retryCount) = 0;

    // Get the number of transfers in the given state
    virtual int getActive(const Pair&) = 0;
    virtual int getSubmitted(const Pair&) = 0;

    // Get the bottleneck link (smallest throughput capacity)
    virtual std::list<std::string> getNetLinks(const Pair&) = 0;

    // Get current throughput
    virtual double getThroughputAsSourceInst(const std::string&) = 0;
    virtual double getThroughputAsDestinationInst(const std::string&) = 0;
    virtual double getThroughputOverNetLinkInst(const std::string&) = 0;

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
 *     x_{1,2}(n-1) in previousPairStateMap
 *     x_{1,2}(n) in currentPairStateMap
 *     x_{3}(n) in currentSEStateMap
*/
// Optimizer implementation
class Optimizer: public boost::noncopyable {
protected:
    std::map<Pair, PairState> previousPairStateMap;
    std::map<Pair, PairState> currentPairStateMap;
    std::map<std::string, std::vector<StorageState>> currentSEStateMap;
    std::map<std::string, std::vector<StorageState>> previousSEStateMap;
    std::map<std::string, NetLinkState> currentNetLinkStateMap; 

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

    bool windowBasedThroughputLimitEnforcement;
    bool netLinkThroughputLimitEnforcement;
    bool proportionalDecreaseThroughputLimitEnforcement;

    int sourceIndex = 0;
    int destinationIndex = 1;

    // Read currentSEStateMap values into a StorageLimits object for the purposes of a single pair.
    void getStorageLimits(const Pair &pair, StorageLimits *limits);

    // Read currentLinkStateMap values into a NetLinkLimits object for the purposes of a single pair.
    void getNetLinkLimits(const Pair &pair, std::map<std::string, NetLinkLimits> *limits);

    // Enforce throughput limits on storage elements and netlinks used by a given pair. 
    // Returns reduced optimizer decision for given pair if throughput limit is exceeded. 
    int enforceThroughputLimits(const Pair &pair, StorageLimits storageLimits, std::map<std::string, NetLinkLimits> netLinkLimits, Range range, int previousValue, int increaseStepSize);

    // Calculates the reduced optimizer decision if throughput limits on storage element or netlinks are exceeded 
    int getFairShareDecision(const Pair &pair, float tputLimit, float tput, int numPairs, Range range, int previousDecision, int increaseStepSize);

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


    //NEW METHOD
    //run optimizer on resources, calc gradient and prop decisions
    void runOptimizerForResources(const std::list<Pair> &pairs);
    void proposeWeightedPairIncrease(const std::list<Pair> &pairs, const std::string se, const int resourceIndex);
    void proposeDecreaseMaxPair(const std::list<Pair> &pairs, const std::string se, const int resourceIndex);
    void setDecisionforPairs(const std::list<Pair> &pairs, boost::timer::cpu_times elapsed); 

    // Gets and saves current performance on all pairs and storage elements 
    // in currentPairStateMap and currentSEStateMap
    int getAvgActiveConnections(const Pair &pair);

    void updateSEState(std::map<std::string, std::vector<StorageState>> &currentSEStateMap, std::string SE, int index, PairState &pair);

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
    void setSourceIndex(int);
    void setDestinationIndex(int);
    void updateDecisions(const std::list<Pair> &);
    void run(void);
    void runOptimizerForPair(const Pair&);
    void proposeDecisions(const std::list<Pair> &pairs);
};


inline std::ostream& operator << (std::ostream &os, const Range &range) {
    return (os << range.min << "/" << range.max);
}

}
}

#endif // FTS3_OPTIMIZER_H