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

#include "common/Uri.h"


namespace fts3 {
namespace optimizer {

struct Pair {
    std::string source, destination;

    Pair(const std::string &s, const std::string &d): source(s), destination(d) {
    }

    bool isLanTransfer() const {
        return common::isLanTransfer(source, destination);
    }
};

// Required so it can be used as a key on a std::map
inline bool operator < (const Pair &a, const Pair &b) {
    return a.source < b.source || (a.source == b.source && a.destination < b.destination);
}


struct Range {
    int min, max;

    Range(): min(0), max(0) {}
};


struct Limits {
    int source, destination, link;
    double throughputSource, throughputDestination;
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

    PairState(): timestamp(0), throughput(0), avgDuration(0), successRate(0), retryCount(0), activeCount(0),
                 queueSize(0), ema(0) {}

    PairState(time_t ts, double thr, time_t ad, double sr, int rc, int ac, int qs, double ema):
        timestamp(ts), throughput(thr), avgDuration(ad), successRate(sr), retryCount(rc),
        activeCount(ac), queueSize(qs), ema(ema) {}
};

// To decouple the optimizer core logic from the data storage/representation
class OptimizerDataSource {
public:
    virtual ~OptimizerDataSource()
    {}

    // Return a list of pairs with active or submitted transfers
    virtual std::list<Pair> getActivePairs(void) = 0;

    // Return the optimizer configuration value
    virtual int getOptimizerMode(void) = 0;

    // Return true if retry is enabled for this server
    virtual bool isRetryEnabled(void) = 0;

    // Get configured limits
    virtual int getGlobalStorageLimit(void) = 0;
    virtual int getGlobalLinkLimit(void) = 0;
    virtual void getPairLimits(const Pair &pair, Range *range, Limits *limits) = 0;

    // Get the stored optimizer value (current value)
    virtual int getOptimizerValue(const Pair&) = 0;

    // Get the weighted throughput for the pair
    virtual double getWeightedThroughput(const Pair&, const boost::posix_time::time_duration&) = 0;

    virtual time_t getAverageDuration(const Pair&, const boost::posix_time::time_duration&) = 0;

    // Get the success rate for the pair
    virtual double getSuccessRateForPair(const Pair&, const boost::posix_time::time_duration&, int *retryCount) = 0;

    // Get the number of transfers in the given state
    virtual int getActive(const Pair&) = 0;
    virtual int getSubmitted(const Pair&) = 0;

    // Get current throughput
    virtual double getThroughputAsSource(const std::string&) = 0;
    virtual double getThroughputAsDestination(const std::string&) = 0;

    // Permanently register the optimizer decision
    virtual void storeOptimizerDecision(const Pair &pair, int activeDecision, double bandwidthLimit,
        const PairState &newState, int diff, const std::string &rationale) = 0;
};

// Optimizer implementation
class Optimizer: public boost::noncopyable {
protected:
    std::map<Pair, PairState> inMemoryStore;
    OptimizerDataSource *dataSource;
    int globalMaxPerLink, globalMaxPerStorage;
    int optimizerSteadyInterval;

    void runForPair(const Pair&);

    // Stores into rangeActiveMin and rangeActiveMax the working range for the optimizer
    // Returns true if the range is configured, so the optimizer can start higher by default
    // Returns false if the range max is *not* configured, so the optimizer must be careful and start low
    bool getOptimizerWorkingRange(const Pair &pair, Range *range, Limits *limits);

public:
    Optimizer(OptimizerDataSource *ds);
    ~Optimizer();

    void setSteadyInterval(int);
    void run(void);
};

}
}

#endif // FTS3_OPTIMIZER_H