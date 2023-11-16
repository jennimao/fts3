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

#include <monitoring/msg-ifce.h>
#include "Optimizer.h"
#include "OptimizerConstants.h"
#include "common/Exceptions.h"
#include "common/Logger.h"

using namespace fts3::common;


namespace fts3 {
namespace optimizer {


// borrowed from http://oroboro.com/irregular-ema/
static inline double exponentialMovingAverage(double sample, double alpha, double cur)
{
    if (sample > 0)
        cur = (sample * alpha) + ((1 - alpha) * cur);
    return cur;
}


static boost::posix_time::time_duration calculateTimeFrame(time_t avgDuration)
{
    if(avgDuration > 0 && avgDuration < 30) {
        return boost::posix_time::minutes(5);
    }
    else if(avgDuration > 30 && avgDuration < 900) {
        return boost::posix_time::minutes(15);
    }
    else {
        return boost::posix_time::minutes(30);
    }
}

// Purpose: Gets and saves current performance on all pairs in 
// memory stores in currentPairStateMap and currentSEStateMap
// Input: List of active pairs, as well as SQL database data.
void Optimizer::getCurrentIntervalInputState(const std::list<Pair> &pairs) {
    // Initializes currentSEStateMap with limit information from
    // t_se table in the SQL database.
    dataSource->getStorageStates(&currentSEStateMap);
    dataSource->getLinkStates(&currentLinkStateMap); 

    for (auto i = pairs.begin(); i != pairs.end(); ++i) {
        // ===============================================        
        // STEP 1: DERIVING PAIR STATE
        // ===============================================        
       
        // Initialize the PairState object we will store information in
        Pair pair = *i;
        PairState current;

        // Compute the values that will be saved in PairState
        current.timestamp = time(NULL);
        current.avgDuration = dataSource->getAverageDuration(pair, boost::posix_time::minutes(30));
        boost::posix_time::time_duration timeFrame = fts3::optimizer::calculateTimeFrame(current.avgDuration);

        current.activeCount = dataSource->getActive(pair);
        current.successRate = dataSource->getSuccessRateForPair(pair, timeFrame, &current.retryCount);
        current.queueSize = dataSource->getSubmitted(pair);

        // Compute the links associated with the source-destination pair 
        current.minLink = dataSource->getMinTputLink(pair); 

        // Compute throughput values (used in Step 2)      
        dataSource->getThroughputInfo(pair, timeFrame,
          &(current.throughput), &(current.filesizeAvg), &(current.filesizeStdDev));
        
        // Save to map
        currentPairStateMap[pair] = current;
        
        // ===============================================        
        // STEP 2: DERIVING SE STATE FROM PAIR STATE
        // ===============================================   

        // Increments SE throughput value by the pair's throughput value.
        // Because the default and current majority state is no throughput limitation,
        // the if condition is added.
        // Potential difference in list of SEs compared to list of pairs, does not get handled
        if (currentSEStateMap.find(pair.source) != currentSEStateMap.end() 
           && currentSEStateMap[pair.source].outboundMaxThroughput > 0) {
            currentSEStateMap[pair.source].asSourceThroughput += current.throughput;
        }

        if (currentSEStateMap.find(pair.destination) != currentSEStateMap.end()
           && currentSEStateMap[pair.destination].inboundMaxThroughput > 0) {
            currentSEStateMap[pair.destination].asDestThroughput += current.throughput;
        }

        // ===============================================        
        // STEP 3: DERIVING LINK STATE FROM PAIR STATE
        // ===============================================

        // Increments link throughput value by the pair's throughput value 
        if (currentLinkStateMap.find(pair.minLink) != currentLinkStateMap.end() 
            && currentLinkStateMap[pair.minLink].MaxThroughput > 0) {
                currentLinkStateMap[pair.minLink].Throughput += current.throughput; 
        }
    }
}

// Reads limits into Range object for a pair.
// Uses StorageLimits for a hard max.
void Optimizer::getOptimizerWorkingRange(const Pair &pair, const StorageLimits &limits, Range *range)
{
    // Query specific limits
    dataSource->getPairLimits(pair, range);

    // If range not set, use defaults
    if (range->min <= 0) {
        if (pair.isLanTransfer()) {
            range->min = DEFAULT_LAN_ACTIVE;
        }
        else {
            range->min = DEFAULT_MIN_ACTIVE;
        }
    }

    bool isMaxConfigured = (range->max > 0);
    if (!isMaxConfigured) {
        range->max = std::min({limits.source, limits.destination});
        range->storageSpecific = true;
        if (range->max < range->min) {
            range->max = range->min;
        }
    }

    BOOST_ASSERT(range->min > 0 && range->max >= range->min);
}

// To be called for low success rates (<= LOW_SUCCESS_RATE)
static int optimizeLowSuccessRate(const PairState &current, const PairState &previous, int previousValue,
    int baseSuccessRate, int decreaseStepSize, std::stringstream& rationale)
{
    int decision = previousValue;
    // If improving, keep it stable
    if (current.successRate > previous.successRate && current.successRate >= baseSuccessRate &&
        current.retryCount <= previous.retryCount) {
        rationale << "Bad link efficiency but progressively improving";
    }
        // If worse or the same, step back
    else if (current.successRate < previous.successRate) {
        decision = previousValue - decreaseStepSize;
        rationale << "Bad link efficiency";
    }
    else {
        decision = previousValue - decreaseStepSize;
        rationale << "Bad link efficiency, no changes";
    }

    return decision;
}

// To be called when there is not enough information to decide what to do
static int optimizeNotEnoughInformation(const PairState &, const PairState &, int previousValue,
    std::stringstream& rationale)
{
    rationale << "Steady, not enough throughput information";
    return previousValue;
}

// To be called when the success rate is good
static int optimizeGoodSuccessRate(const PairState &current, const PairState &previous, int previousValue,
    int decreaseStepSize, int increaseStepSize, std::stringstream& rationale)
{
    int decision = previousValue;

    if (current.queueSize < previousValue) {
        rationale << "Queue emptying. Hold on.";
    }
    else if (current.ema < previous.ema) {
        // If the throughput is worsening, we need to look at the file sizes.
        // If the file sizes are decreasing, then it could be that the throughput deterioration is due to
        // this. Thus, decreasing the number of actives will be a bad idea.
        if (round(log10(current.filesizeAvg)) < round(log10(previous.filesizeAvg))) {
            decision = previousValue + increaseStepSize;
            rationale << "Good link efficiency, throughput deterioration, avg. filesize decreasing";
        }
        // Compare on the logarithmic scale, to reduce sensitivity
        else if(round(log10(current.ema)) < round(log10(previous.ema))) {
            decision = previousValue - decreaseStepSize;
            rationale << "Good link efficiency, throughput deterioration";
        }
        // We have lost an order of magnitude, so drop actives
        else {
            decision = previousValue;
            rationale << "Good link efficiency, small throughput deterioration";
        }
    }
    else if (current.ema > previous.ema) {
        decision = previousValue + increaseStepSize;
        rationale << "Good link efficiency, current average throughput is larger than the preceding average";
    }
    else {
        decision = previousValue + increaseStepSize;
        rationale << "Good link efficiency. Increment";
    }

    return decision;
}

// Extracts only the limits from currentSEStateMap
void Optimizer::getStorageLimits(const Pair &pair, StorageLimits *limits) {

    for (const auto& pair : currentSEStateMap) {
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "SE: " << pair.first << commit;
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << pair.second.outboundMaxThroughput << commit;
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << pair.second.outboundMaxActive << commit;
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << pair.second.inboundMaxThroughput << commit;
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << pair.second.inboundMaxActive << commit;
    }

    if (currentSEStateMap.find(pair.source) != currentSEStateMap.end()) {
        limits->throughputSource = currentSEStateMap[pair.source].outboundMaxThroughput;
        limits->source = currentSEStateMap[pair.source].outboundMaxActive;
    }
    else {
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG ) << "Source SE not in t_se." << commit;
        limits->throughputSource = currentSEStateMap["*"].outboundMaxThroughput;
        limits->source = currentSEStateMap["*"].outboundMaxActive;
    }
    if (currentSEStateMap.find(pair.destination) != currentSEStateMap.end()) {
        limits->throughputDestination = currentSEStateMap[pair.destination].inboundMaxThroughput;
        limits->destination = currentSEStateMap[pair.destination].inboundMaxActive;          
    }
    else {
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG ) << "Destination SE not in t_se." << commit;
        limits->throughputDestination = currentSEStateMap["*"].inboundMaxThroughput;
        limits->destination = currentSEStateMap["*"].inboundMaxActive;        
    }
           
}

// Extracts only the limits from currentLinkStateMap
void Optimizer::getLinkLimits(const Pair &pair, LinkLimits *limits) {

    for (const auto& pair : currentLinkStateMap) {
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Link: " << pair.first << commit;
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << pair.second.MaxThroughput << commit;
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << pair.second.MaxActive << commit;
    }

    if (currentLinkStateMap.find(pair.minLink) != currentLinkStateMap.end()) {
        limits->throughput = currentLinkStateMap[pair.minLink].MaxThroughput;
        limits->active = currentLinkStateMap[pair.minLink].MaxActive;
    }
    else {
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG ) << "Min throughput link not in t_netlink_stat." << commit;
        limits->throughput = currentLinkStateMap["*"].MaxThroughput;
        limits->active = currentLinkStateMap["*"].MaxActive;
    }
}

// This algorithm idea is similar to the TCP congestion window.
// It gives priority to success rate. If it gets worse, it will back off reducing
// the total number of connections between storages.
// If the success rate is good, and the throughput improves, it will increase the number
// of connections.
// Purpose: Save new decision to database based on network state.
// Return: Boolean signifying whether or not the decision was modified.
bool Optimizer::optimizeConnectionsForPair(OptimizerMode optMode, const Pair &pair)
{
    int decision = 0;
    std::stringstream rationale;

    // Start ticking!
    boost::timer::cpu_timer timer;

    // Reads in range (pair decision's bounds) and limits (total throughut per SE limits)
    Range range;
    StorageLimits storageLimits;
    LinkLimits linkLimits; 
    
    getLinkLimits(pair, &linkLimits); 
    getStorageLimits(pair, &storageLimits);
    getOptimizerWorkingRange(pair, storageLimits, &range);
    
    FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Storage limits for " << pair << ": " << storageLimits.throughputSource
                                    << "/" << storageLimits.throughputDestination
                                    << ", " << storageLimits.source
                                    << "/" << storageLimits.destination << commit;

    FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Link limits for " << pair << ": " << linkLimits.throughput
                                    << ", " << storageLimits.active << commit;

    FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Optimizer range for " << pair << ": " << range  << commit;

    // Previous decision 
    int previousValue = dataSource->getOptimizerValue(pair);

    // Read in current pair state information.
    PairState currentPair = currentPairStateMap[pair];

    // Case 1a:
    // There is no value yet. In this case, pick the high value if configured, mid-range otherwise.
    if (previousValue == 0) {
        if (range.specific) {
            decision = range.max;
            rationale << "No information. Use configured range max.";
        } else {
            decision = range.min + (range.max - range.min) / 2;
            rationale << "No information. Start halfway.";
        }

        currentPair.ema = currentPair.throughput; 

        // Update previousPairStateMap variable + SQL tables (t_optimizer and t_optimizer_evolution)
        setOptimizerDecision(pair, decision, currentPair, decision, rationale.str(), timer.elapsed());

        return true;
    }

    // Case 1b: There is information, but it is the first time seen since the restart
    if (previousPairStateMap.find(pair) == previousPairStateMap.end()) {
        currentPair.ema = currentPair.throughput;
        previousPairStateMap[pair] = currentPair;
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG) << "Store first feedback from " << pair << commit;
        return false;
    }

    // Read out previous 
    const PairState previous = previousPairStateMap[pair];

    // Calculate new Exponential Moving Average
    currentPair.ema = exponentialMovingAverage(currentPair.throughput, emaAlpha, previous.ema);

    // If we have no range, leave it here
    if (range.min == range.max) {
        setOptimizerDecision(pair, range.min, currentPair, 0, "Range fixed", timer.elapsed());
        return true;
    }

    // Apply bandwidth limits (source) for both window based approximation and instantaneous throughput.
    if (storageLimits.throughputSource > 0) {
        if (currentSEStateMap[pair.source].asSourceThroughput > storageLimits.throughputSource) {
            decision = std::max(previousValue - decreaseStepSize, range.min);
            rationale << "Source throughput limitation reached (" << storageLimits.throughputSource << ")";
            setOptimizerDecision(pair, decision, currentPair, decision - previousValue, rationale.str(), timer.elapsed());
            return true;
        }
        if (currentSEStateMap[pair.source].asSourceThroughputInst > storageLimits.throughputSource) {
            decision = std::max(previousValue - decreaseStepSize, range.min);
            rationale << "Source (instantaneous) throughput limitation reached (" << storageLimits.throughputSource << ")";
            setOptimizerDecision(pair, decision, currentPair, decision - previousValue, rationale.str(), timer.elapsed());
            return true;
        }
    }

    // Apply bandwidth limits (destination) for both window based approximation and instantaneous throughput.
    if (storageLimits.throughputDestination > 0) {
        if (currentSEStateMap[pair.destination].asDestThroughput > storageLimits.throughputDestination) {
            decision = std::max(previousValue - decreaseStepSize, range.min);
            rationale << "Destination throughput limitation reached (" << storageLimits.throughputDestination << ")";
            setOptimizerDecision(pair, decision, currentPair, decision - previousValue, rationale.str(), timer.elapsed());
            return true;
        }
        if (currentSEStateMap[pair.destination].asDestThroughputInst > storageLimits.throughputDestination) {
            decision = std::max(previousValue - decreaseStepSize, range.min);
            rationale << "Destination (instantaneous) throughput limitation reached (" << storageLimits.throughputDestination << ")";
            setOptimizerDecision(pair, decision, currentPair, decision - previousValue, rationale.str(), timer.elapsed());
            return true;
        }        
    }

    // Apply bandwidth limits (bottleneck link) for both window based approximation and instantaneous throughput.
    if (linkLimits.throughput > 0) {
        if (currentLinkStateMap[pair.minLink].Throughput > linkLimits.throughput) {
            decision = std::max(previousValue - decreaseStepSize, range.min);
            rationale << "Bottleneck link throughput limitation reached (" << linkLimits.throughput << ")";
            setOptimizerDecision(pair, decision, currentPair, decision - previousValue, rationale.str(), timer.elapsed());
            return  true; 
        }
        if (currentLinkStateMap[pair.minLink].ThroughputInst > linkLimits.throughput) {
            decision = std::max(previousValue - decreaseStepSize, range.min);
            rationale << "Bottleneck link (instantaneous) throughput limitation reached (" << linkLimits.throughput << ")";
            setOptimizerDecision(pair, decision, currentPair, decision - previousValue, rationale.str(), timer.elapsed());
            return  true; 
        }
    }

    // Run only when it makes sense
    time_t timeSinceLastUpdate = currentPair.timestamp - previous.timestamp;

    if (currentPair.successRate == previous.successRate &&
        currentPair.ema == previous.ema &&
        timeSinceLastUpdate < optimizerSteadyInterval.total_seconds()) {
        FTS3_COMMON_LOGGER_NEWLOG(DEBUG)
            << "Optimizer for " << pair
            << ": Same success rate and throughput EMA, not enough time passed since last update. Skip"
            << commit;
        return false;
    }

    FTS3_COMMON_LOGGER_NEWLOG(DEBUG)
        << "Optimizer max possible number of actives for " << pair << ": " << range.max << commit;

    // For low success rates, do not even care about throughput
    if (currentPair.successRate < lowSuccessRate) {
        // optimizeLowSuccessRate decreases decision (unless success rate is currently increasing)
        decision = optimizeLowSuccessRate(currentPair, previous, previousValue,
            baseSuccessRate, decreaseStepSize,
            rationale);
    }
    // No throughput info
    else if (currentPair.ema == 0) {
        // Does nothing
        decision = optimizeNotEnoughInformation(currentPair, previous, previousValue, rationale);
    }
    // Good success rate, or not enough information to take any decision
    else {
        int localIncreaseStep = increaseStepSize;
        if (optMode >= kOptimizerNormal) {
            localIncreaseStep = increaseAggressiveStepSize;
        }
        // optimizeGoodSuccessRate increases decision if filesize is decreasing or
        // throughput is increasing.
        decision = optimizeGoodSuccessRate(currentPair, previous, previousValue,
            decreaseStepSize, localIncreaseStep,
            rationale);
    }

    // Apply margins to the decision
    if (decision < range.min) {
        decision = range.min;
        rationale << ". Hit lower range limit";
        if (!range.specific) {
            rationale <<". Using *->* link configuration";
        }
    }
    else if (decision > range.max) {
        decision = range.max;
        rationale << ". Hit upper range limit";
        if (!range.specific) {
            rationale <<". Using *->* link configuration";
        } else if (range.storageSpecific) {
            rationale <<". Link not configured. Using SE limits";
        }
    }

    // We may have a higher number of connections than available on the queue.
    // If stream optimize is enabled, push forward and let those extra connections
    // go into streams.
    // Otherwise, stop pushing.
    if (optMode == kOptimizerConservative) {
        if (decision > previousValue && currentPair.queueSize < decision) {
            decision = previousValue;
            rationale << ". Not enough files in the queue";
        }
    }
    // Do not go too far with the number of connections
    if (optMode >= kOptimizerNormal) {
        if (decision > currentPair.queueSize * maxNumberOfStreams) {
            decision = previousValue;
            rationale << ". Too many streams";
        }
    }

    BOOST_ASSERT(decision > 0);
    BOOST_ASSERT(!rationale.str().empty());

    setOptimizerDecision(pair, decision, currentPair, decision - previousValue, rationale.str(), timer.elapsed());
    return true;
}



// setOptimizerDecision does two things
//   - Update Optimizer struct variable (previousPairStateMap)
//   - Update SQL database (t_optimizer and t_optimizer_evolution)
void Optimizer::setOptimizerDecision(const Pair &pair, int decision, const PairState &current,
    int diff, const std::string &rationale, boost::timer::cpu_times elapsed)
{
    FTS3_COMMON_LOGGER_NEWLOG(INFO)
        << "Optimizer: Active for " << pair << " set to " << decision << ", running " << current.activeCount
        << " (" << elapsed.wall << "ns)" << commit;
    FTS3_COMMON_LOGGER_NEWLOG(INFO)
        << rationale << commit;

    //Stores current PairState (including the optimizer decision) in the previousPairStateMap
    previousPairStateMap[pair] = current;
    previousPairStateMap[pair].connections = decision;
    
    dataSource->storeOptimizerDecision(pair, decision, current, diff, rationale);

    if (callbacks) {
        callbacks->notifyDecision(pair, decision, current, diff, rationale);
    }
}

}
}