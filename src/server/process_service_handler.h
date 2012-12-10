/* Copyright @ Members of the EMI Collaboration, 2010.
See www.eu-emi.eu for details on the copyright holders.

Licensed under the Apache License, Version 2.0 (the "License"); 
you may not use this file except in compliance with the License. 
You may obtain a copy of the License at 

    http://www.apache.org/licenses/LICENSE-2.0 

Unless required by applicable law or agreed to in writing, software 
distributed under the License is distributed on an "AS IS" BASIS, 
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
See the License for the specific language governing permissions and 
limitations under the License. */

#pragma once

#include "server_dev.h"
#include "common/pointers.h"
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <string>
#include "SingleDbInstance.h"
#include "common/logger.h"
#include "common/error.h"
#include "process.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include "site_name.h"
#include "FileTransferScheduler.h"
#include "ProtocolResolver.h"
#include <signal.h>
#include "parse_url.h"
#include "cred-utility.h"
#include <sys/types.h>
#include <unistd.h>
#include <grp.h>
#include <sys/stat.h>
#include <pwd.h>
#include <fstream>
#include "config/serverconfig.h"
#include "definitions.h"
#include "DrainMode.h"
#include "StaticSslLocking.h"
#include "queue_updater.h"
#include <boost/algorithm/string.hpp>  
#include <sys/param.h>

extern bool  stopThreads;


FTS3_SERVER_NAMESPACE_START
using FTS3_COMMON_NAMESPACE::Pointer;
using namespace FTS3_COMMON_NAMESPACE;
using namespace db;
using namespace FTS3_CONFIG_NAMESPACE;


uid_t name_to_uid(char const *name) {
    if (!name)
        return static_cast<uid_t>(-1);
    long const buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buflen == -1)
        return static_cast<uid_t>(-1);

    char buf[buflen];
    struct passwd pwbuf, *pwbufp;
    if (0 != getpwnam_r(name, &pwbuf, buf, static_cast<size_t>(buflen), &pwbufp)
            || !pwbufp)
        return static_cast<uid_t>(-1);
    return pwbufp->pw_uid;
}

template <class T>
inline std::string to_string(const T& t) {
    std::stringstream ss;
    ss << t;
    return ss.str();
}

template
<
typename TRAITS
>
class ProcessServiceHandler : public TRAITS::ActiveObjectType {
protected:

    using TRAITS::ActiveObjectType::_enqueue;
    std::string enableOptimization;
    
public:

    /* ---------------------------------------------------------------------- */

    typedef ProcessServiceHandler <TRAITS> OwnType;

    /* ---------------------------------------------------------------------- */

    /** Constructor. */
    ProcessServiceHandler
    (
            const std::string& desc = "" /**< Description of this service handler
            (goes to log) */
            ) :
    TRAITS::ActiveObjectType("ProcessServiceHandler", desc) {
	enableOptimization = theServerConfig().get<std::string > ("Optimizer");
	char hostname[MAXHOSTNAMELEN];
        gethostname(hostname, MAXHOSTNAMELEN);
	ftsHostName = std::string(hostname);
	allowedVOs = std::string("");
	const vector<std::string> voNameList(theServerConfig().get< vector<string> >("AuthorizedVO"));
	if(voNameList.size()>0 && std::string(voNameList[0]).compare("*")!=0){
		std::vector<std::string>::const_iterator iterVO;
		allowedVOs+="(";
		for (iterVO = voNameList.begin(); iterVO != voNameList.end(); ++iterVO) {
			allowedVOs+="'";
			allowedVOs+=(*iterVO);
			allowedVOs+="',";			
		}
		allowedVOs = allowedVOs.substr(0, allowedVOs.size()-1);
		allowedVOs+=")";
		boost::algorithm::to_lower(allowedVOs);
	}
	
	
    }

    /* ---------------------------------------------------------------------- */

    /** Destructor */
    virtual ~ProcessServiceHandler() {
    }

    /* ---------------------------------------------------------------------- */

    void executeTransfer_p
    (
            ) {
	
        boost::function<void() > op = boost::bind(&ProcessServiceHandler::executeTransfer_a, this);
	this->_enqueue(op);
    }

protected:
    SiteName siteResolver;
    std::string ftsHostName;
    std::string allowedVOs;

    void killRunninfJob(std::vector<int>& requestIDs) {
        std::vector<int>::const_iterator iter;
        for (iter = requestIDs.begin(); iter != requestIDs.end(); ++iter) {
            int pid = *iter;
            FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Canceling and killing running processes: " << pid << commit;	    
            kill(pid, SIGTERM);
        }
    }

    std::string extractHostname(std::string surl) {
        std::string hostname("");
        char *base_scheme = NULL;
        char *base_host = NULL;
        char *base_path = NULL;
        int base_port = 0;
        parse_url(surl.c_str(), &base_scheme, &base_host, &base_port, &base_path);
	if(base_host)	
            hostname = std::string(base_host);
        if (base_scheme)
            free(base_scheme);
        if (base_host)
            free(base_host);
        if (base_path)
            free(base_path);
	    
        return hostname;
    }

    void createJobFile(std::string job_id, std::vector<std::string>& files) {
        std::vector<std::string>::const_iterator iter;
        std::string filename = "/var/lib/fts3/" + job_id;
        std::ofstream fout;
        fout.open(filename.c_str(), ios::out);
        for (iter = files.begin(); iter != files.end(); ++iter) {
            fout << *iter << std::endl;
        }
        fout.close();
    }
    
    void executeUrlcopy(std::vector<TransferJobs*>& jobs2, bool reuse) {
        const std::string cmd = "fts_url_copy";
        std::string params = std::string("");
        ExecuteProcess *pr = NULL;
        std::string sourceSiteName("");
        std::string destSiteName("");
        std::string source_hostname("");
        std::string destin_hostname("");
        SeProtocolConfig protocol;
        bool protocolExists;	
        std::string proxy_file("");
        bool debug = false;
        OptimizerSample* opt_config = NULL;
	
        if (reuse == false) {
   	    bool manualConfigExists = false;
	    std::string symbolicName("");	    
            if (!jobs2.empty()) {
                /*get the file for each job*/
        	std::vector<TransferJobs*>::const_iterator iter2;
        	std::vector<TransferFiles*> files;
		files.reserve(3000);
        	std::vector<TransferFiles*>::const_iterator fileiter;		
                DBSingleton::instance().getDBObjectInstance()->getByJobId(jobs2, files);
                for (fileiter = files.begin(); fileiter != files.end(); ++fileiter) {
		if(stopThreads){
		               /** cleanup resources */
                for (iter2 = jobs2.begin(); iter2 != jobs2.end(); ++iter2)
                    delete *iter2;
                jobs2.clear();
                for (fileiter = files.begin(); fileiter != files.end(); ++fileiter)
                    delete *fileiter;
                files.clear();		
			return;
		}

                    int BufSize = 0;
                    int StreamsperFile = 0;
                    int Timeout = 0;
		    std::stringstream internalParams;
                    TransferFiles* temp = (TransferFiles*) *fileiter;
                    source_hostname = extractHostname(temp->SOURCE_SURL);
                    destin_hostname = extractHostname(temp->DEST_SURL);
		    
		    /*check if manual config exist for this pair and vo*/
                manualConfigExists = DBSingleton::instance().getDBObjectInstance()->isThereJobShareConfig(temp->JOB_ID);
				
			
                    bool optimize = false;
                    if (enableOptimization.compare("true") == 0 && manualConfigExists==false) {		    	
		        optimize = true;
                        opt_config = new OptimizerSample();
			DBSingleton::instance().getDBObjectInstance()->initOptimizer(source_hostname, destin_hostname, 0);
                        DBSingleton::instance().getDBObjectInstance()->fetchOptimizationConfig2(opt_config, source_hostname, destin_hostname);
                        BufSize = opt_config->getBufSize();
                        StreamsperFile = opt_config->getStreamsperFile();
                        Timeout = opt_config->getTimeout();                       
                        delete opt_config;
                        opt_config = NULL;
                    }
  	    	    
                    FileTransferScheduler scheduler(temp);
                    if (scheduler.schedule(optimize, manualConfigExists)) { /*SET TO READY STATE WHEN TRUE*/
		    	FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Transfer start: " << source_hostname << " -> " << destin_hostname << commit;
		    if(optimize && manualConfigExists==false){
		    	DBSingleton::instance().getDBObjectInstance()->setAllowed(temp->JOB_ID,temp->FILE_ID,source_hostname, destin_hostname, StreamsperFile, Timeout, BufSize);
		    }else{
		        FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Check link config for: " << source_hostname << " -> " << destin_hostname << commit;
		        ProtocolResolver resolver(temp->JOB_ID);
		        protocolExists = resolver.resolve();			
			if(protocolExists){
				protocol.NOSTREAMS = resolver.NOSTREAMS;
				protocol.NO_TX_ACTIVITY_TO = resolver.NO_TX_ACTIVITY_TO;
				protocol.TCP_BUFFER_SIZE = resolver.TCP_BUFFER_SIZE;
				protocol.URLCOPY_TX_TO = resolver.URLCOPY_TX_TO;
			
				if(protocol.NOSTREAMS >= 0)
					internalParams << "nostreams:" << protocol.NOSTREAMS;
				if(protocol.URLCOPY_TX_TO >= 0)
					internalParams << ",timeout:"<< protocol.URLCOPY_TX_TO;
				if(protocol.TCP_BUFFER_SIZE >= 0)	
					internalParams << ",buffersize:" << protocol.TCP_BUFFER_SIZE;
			}else{
				internalParams << "nostreams:" << DEFAULT_NOSTREAMS << ",timeout:"<< DEFAULT_TIMEOUT << ",buffersize:" << DEFAULT_BUFFSIZE;			
			}
			DBSingleton::instance().getDBObjectInstance()->setAllowedNoOptimize(temp->JOB_ID, temp->FILE_ID, internalParams.str());			
		    }
		    
                   proxy_file = get_proxy_cert(
                            temp->DN, // user_dn
                            temp->CRED_ID, // user_cred
                            temp->VO_NAME, // vo_name
                            "",
                            "", // assoc_service
                            "", // assoc_service_type
                            false,
                            "");
		   	
                        sourceSiteName = siteResolver.getSiteName(temp->SOURCE_SURL);
                        destSiteName = siteResolver.getSiteName(temp->DEST_SURL);

                        debug = DBSingleton::instance().getDBObjectInstance()->getDebugMode(source_hostname, destin_hostname);

                        if (debug == true) {
                            params.append(" -F ");
                        }

                        if (proxy_file.length() > 0) {
                            params.append(" -proxy ");
                            params.append(proxy_file);
                            /*make sure proxy is readable    */
                            chmod(proxy_file.c_str(), (mode_t) 0600); //S_IRUSR|S_IRGRP|S_IROTH
                            char user[ ] = "fts3";
                            uid_t pw_uid;
                            pw_uid = name_to_uid(user);
                            int checkChown = chown(proxy_file.c_str(), pw_uid, getgid());
			    if(checkChown!=0){
			    	FTS3_COMMON_LOGGER_NEWLOG(ERR) << "Failed to chmod for proxy" << proxy_file << commit;
			    }

                        }

                        if (std::string(temp->CHECKSUM).length() > 0) { //checksum
                            params.append(" -z ");
                            params.append(temp->CHECKSUM);
                        }
                        if (std::string(temp->CHECKSUM_METHOD).length() > 0) { //checksum
                            params.append(" -A ");
                            params.append(temp->CHECKSUM_METHOD);
                        }
                        params.append(" -b ");
                        params.append(temp->SOURCE_SURL);
                        params.append(" -c ");
                        params.append(temp->DEST_SURL);
                        params.append(" -a ");
                        params.append(temp->JOB_ID);
                        params.append(" -B ");
                        params.append(to_string(temp->FILE_ID));
                        params.append(" -C ");
                        params.append(temp->VO_NAME);
                        if (sourceSiteName.length() > 0) {
                            params.append(" -D ");
                            params.append(sourceSiteName);
                        }
                        if (destSiteName.length() > 0) {
                            params.append(" -E ");
                            params.append(destSiteName);
                        }
                        if (std::string(temp->OVERWRITE).length() > 0) {
                            params.append(" -d ");
                        }
                        if (optimize && manualConfigExists==false) {
                            params.append(" -e ");
                            params.append(to_string(StreamsperFile));
                        } else {
                            if (protocol.NOSTREAMS >= 0) {
                                params.append(" -e ");
                                params.append(to_string(protocol.NOSTREAMS));
                            }
                        }

                        if (optimize && manualConfigExists==false) {
                            params.append(" -f ");
                            params.append(to_string(BufSize));
                        } else {
                            if (protocol.TCP_BUFFER_SIZE >= 0) {
                                params.append(" -f ");
                                params.append(to_string(protocol.TCP_BUFFER_SIZE));
                            }
                        }

                        if (optimize && manualConfigExists==false) {
                            params.append(" -h ");
                            params.append(to_string(Timeout));
                        } else {
                            if (protocol.URLCOPY_TX_TO >= 0) {
                                params.append(" -h ");
                                params.append(to_string(protocol.URLCOPY_TX_TO));
                            }
                        }
                        if (std::string(temp->SOURCE_SPACE_TOKEN).length() > 0) {
                            params.append(" -k ");
                            params.append(temp->SOURCE_SPACE_TOKEN);
                        }
                        if (std::string(temp->DEST_SPACE_TOKEN).length() > 0) {
                            params.append(" -j ");
                            params.append(temp->DEST_SPACE_TOKEN);
                        }

			std::string host = DBSingleton::instance().getDBObjectInstance()->transferHost(temp->FILE_ID);	
			bool ready = DBSingleton::instance().getDBObjectInstance()->isFileReadyState(temp->FILE_ID);
			
			if(host.compare(ftsHostName)==0 && ready==true){
			FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Transfer params: " << cmd << " " << params << commit;
                        pr = new ExecuteProcess(cmd, params, 0);			
                        if (pr) {		
			    /*check if fork/execvp failed, */			   
                            if(-1 == pr->executeProcessShell() ){
			    	DBSingleton::instance().getDBObjectInstance()->forkFailedRevertState(temp->JOB_ID, temp->FILE_ID);
			    }else{			        
			    	DBSingleton::instance().getDBObjectInstance()->setPid(temp->JOB_ID, to_string(temp->FILE_ID), pr->getPid());
				struct message_updater msg;
				strcpy(msg.job_id, std::string(temp->JOB_ID).c_str());
        			msg.file_id = temp->FILE_ID;
				msg.process_id = (int) pr->getPid();
				msg.timestamp = std::time(NULL);
				ThreadSafeList::get_instance().push_back(msg);
			    }
			    delete pr;
                         }
			}
                        params.clear();
                    }
                }

                /** cleanup resources */
                for (iter2 = jobs2.begin(); iter2 != jobs2.end(); ++iter2)
                    delete *iter2;
                jobs2.clear();
                for (fileiter = files.begin(); fileiter != files.end(); ++fileiter)
                    delete *fileiter;
                files.clear();
            }
        } else { /*reuse session*/
            if (!jobs2.empty()) {
  	   	bool manualConfigExists = false;
		std::string symbolicName("");
                std::vector<std::string> urls;
		std::map<int,std::string> fileIds;
                std::string job_id = std::string("");
                std::string vo_name = std::string("");
                std::string cred_id = std::string("");
                std::string dn = std::string("");
                std::string overwrite = std::string("");
                std::string source_space_token = std::string("");
                std::string dest_space_token = std::string("");
                std::string file_id = std::string("");
                std::string checksum = std::string("");
                std::string url = std::string("");
                std::string surl = std::string("");
                std::string durl = std::string("");
                int BufSize = 0;
                int StreamsperFile = 0;
                int Timeout = 0;
		    
                TransferFiles* tempUrl = NULL;
                /*get the file for each job*/
       		std::vector<TransferJobs*>::const_iterator iter2;
        	std::vector<TransferFiles*> files;
		files.reserve(300);
        	std::vector<TransferFiles*>::const_iterator fileiter;			
                DBSingleton::instance().getDBObjectInstance()->getByJobId(jobs2, files);
		if(files.empty()){
     			 /** cleanup resources */
                	for (iter2 = jobs2.begin(); iter2 != jobs2.end(); ++iter2)
                    		delete *iter2;
                	jobs2.clear();
			return;
		}
                for (fileiter = files.begin(); fileiter != files.end(); ++fileiter) {
		    if(stopThreads){
			return;
		    }
                    TransferFiles* temp = (TransferFiles*) *fileiter;
                    tempUrl = temp;
                    surl = temp->SOURCE_SURL;
                    durl = temp->DEST_SURL;
                    job_id = temp->JOB_ID;
                    vo_name = temp->VO_NAME;
                    cred_id = temp->CRED_ID;
                    dn = temp->DN;
                    file_id = to_string(temp->FILE_ID);
                    overwrite = temp->OVERWRITE;
                    source_hostname = extractHostname(temp->SOURCE_SURL);
                    destin_hostname = extractHostname(temp->DEST_SURL);
                    source_space_token = temp->SOURCE_SPACE_TOKEN;                  
		    dest_space_token = temp->DEST_SPACE_TOKEN;                    

                    if (std::string(temp->CHECKSUM_METHOD).length() > 0) {
                        if (std::string(temp->CHECKSUM).length() > 0)
                            checksum = temp->CHECKSUM;
                    }
                    url = file_id + " " + surl + " " + durl + " " + checksum;
                    urls.push_back(url);		    		    
                }

                sourceSiteName = siteResolver.getSiteName(surl);
                destSiteName = siteResolver.getSiteName(durl);

                createJobFile(job_id, urls);

		   /*check if manual config exist for this pair and vo*/
                manualConfigExists = DBSingleton::instance().getDBObjectInstance()->isThereJobShareConfig(job_id);

		bool optimize = false;
               
                if (enableOptimization.compare("true") == 0 && manualConfigExists==false) {
		    optimize = true;
                    opt_config = new OptimizerSample();
   		    DBSingleton::instance().getDBObjectInstance()->initOptimizer(source_hostname, destin_hostname, 0);
                    DBSingleton::instance().getDBObjectInstance()->fetchOptimizationConfig2(opt_config, source_hostname, destin_hostname);
                    BufSize = opt_config->getBufSize();
                    StreamsperFile = opt_config->getStreamsperFile();
                    Timeout = opt_config->getTimeout();
                    delete opt_config;
                    opt_config = NULL;
                }
		if(stopThreads){
		 /** cleanup resources */
                for (iter2 = jobs2.begin(); iter2 != jobs2.end(); ++iter2)
                    delete *iter2;
                jobs2.clear();
                for (fileiter = files.begin(); fileiter != files.end(); ++fileiter)
                    delete *fileiter;
                files.clear();
		fileIds.clear();
			return;
		}

                FileTransferScheduler scheduler(tempUrl);
                if (scheduler.schedule(optimize, manualConfigExists)) { /*SET TO READY STATE WHEN TRUE*/
 		    std::stringstream internalParams;
		    if (optimize && manualConfigExists==false) {
		    	DBSingleton::instance().getDBObjectInstance()->setAllowed(job_id, -1,source_hostname, destin_hostname, StreamsperFile, Timeout, BufSize);
		    } else {
		        FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Check link config for: " << source_hostname << " -> " << destin_hostname << " -> " << vo_name << commit;
		        ProtocolResolver resolver(job_id);
		        protocolExists =  resolver.resolve();
				if(protocolExists){
				protocol.NOSTREAMS = resolver.NOSTREAMS;
				protocol.NO_TX_ACTIVITY_TO = resolver.NO_TX_ACTIVITY_TO;
				protocol.TCP_BUFFER_SIZE = resolver.TCP_BUFFER_SIZE;
				protocol.URLCOPY_TX_TO = resolver.URLCOPY_TX_TO;
				
					if(protocol.NOSTREAMS >= 0)
						internalParams << "nostreams:" << protocol.NOSTREAMS;
					if(protocol.URLCOPY_TX_TO >= 0)
						internalParams << ",timeout:"<< protocol.URLCOPY_TX_TO;
					if(protocol.TCP_BUFFER_SIZE >= 0)
						internalParams << ",buffersize:" << protocol.TCP_BUFFER_SIZE;
				}else{
					internalParams << "nostreams:" << DEFAULT_NOSTREAMS << ",timeout:"<< DEFAULT_TIMEOUT << ",buffersize:" << DEFAULT_BUFFSIZE;
				}
				DBSingleton::instance().getDBObjectInstance()->setAllowedNoOptimize(job_id, 0, internalParams.str());
		    }		    

                proxy_file = get_proxy_cert(
                        dn, // user_dn
                        cred_id, // user_cred
                        vo_name, // vo_name
                        "",
                        "", // assoc_service
                        "", // assoc_service_type
                        false,
                        "");

                    /*set all to ready, special case for session reuse*/
                    for (fileiter = files.begin(); fileiter != files.end(); ++fileiter) {
                        TransferFiles* temp = (TransferFiles*) *fileiter;
                        DBSingleton::instance().getDBObjectInstance()->updateFileStatus(temp, "READY");
			fileIds.insert(std::make_pair<int, std::string>(temp->FILE_ID,temp->JOB_ID));
                    }

                    debug = DBSingleton::instance().getDBObjectInstance()->getDebugMode(source_hostname, destin_hostname);
                    if (debug == true) {
                        params.append(" -F ");
                    }

                    if (proxy_file.length() > 0) {
                        params.append(" -proxy ");
                        params.append(proxy_file);
                        /*make sure proxy is readable    */
                        chmod(proxy_file.c_str(), (mode_t) 0600);
                        char user[ ] = "fts3";
                        uid_t pw_uid;
                        pw_uid = name_to_uid(user);
                        int checkChown = chown(proxy_file.c_str(), pw_uid, getgid());
 			if(checkChown!=0){
			    	FTS3_COMMON_LOGGER_NEWLOG(ERR) << "Failed to chmod for proxy" << proxy_file << commit;
			    }			
                    }

                    params.append(" -G ");
                    params.append(" -a ");
                    params.append(job_id);
                    params.append(" -C ");
                    params.append(vo_name);
                    if (sourceSiteName.length() > 0) {
                        params.append(" -D ");
                        params.append(sourceSiteName);
                    }
                    if (destSiteName.length() > 0) {
                        params.append(" -E ");
                        params.append(destSiteName);
                    }
                    if (std::string(overwrite).length() > 0) {
                        params.append(" -d ");
                    }
                    if (optimize && manualConfigExists==false) {
                        params.append(" -e ");
                        params.append(to_string(StreamsperFile));
                    } else {
                        if (protocol.NOSTREAMS >= 0) {
                            params.append(" -e ");
                            params.append(to_string(protocol.NOSTREAMS));
                        }
                    }
                    if (optimize && manualConfigExists==false) {
                        params.append(" -f ");
                        params.append(to_string(BufSize));
                    } else {
                        if (protocol.TCP_BUFFER_SIZE >= 0) {
                            params.append(" -f ");
                            params.append(to_string(protocol.TCP_BUFFER_SIZE));
                        }
                    }
                    if (optimize && manualConfigExists==false) {
                        params.append(" -h ");
                        params.append(to_string(Timeout));
                    } else {
                        if (protocol.URLCOPY_TX_TO >= 0) {
                            params.append(" -h ");
                            params.append(to_string(protocol.URLCOPY_TX_TO));
                        }
                    }
                    if (std::string(source_space_token).length() > 0) {
                        params.append(" -k ");
                        params.append(source_space_token);
                    }
                    if (std::string(dest_space_token).length() > 0) {
                        params.append(" -j ");
                        params.append(dest_space_token);
                    }
                    
		    std::string host = DBSingleton::instance().getDBObjectInstance()->transferHostV(fileIds);	
		    bool ready = DBSingleton::instance().getDBObjectInstance()->isFileReadyStateV(fileIds);		
			
   		    if(host.compare(ftsHostName)==0 && ready==true){		    		    		
		    FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Transfer params: " << params << commit;
                    pr = new ExecuteProcess(cmd, params, 0);		    
                    if (pr) {		
		             /*check if fork failed , check if execvp failed, */			   
                            if(-1 == pr->executeProcessShell()){
			    	DBSingleton::instance().getDBObjectInstance()->forkFailedRevertStateV(fileIds);
			    }else{			       
			    	DBSingleton::instance().getDBObjectInstance()->setPidV(pr->getPid(), fileIds);				
				std::map<int, std::string>::const_iterator iterFileIds;
				for (iterFileIds = fileIds.begin(); iterFileIds != fileIds.end(); ++iterFileIds) {
				        struct message_updater msg;
					strcpy(msg.job_id, std::string(job_id).c_str());
        				msg.file_id = iterFileIds->first;
					msg.process_id = (int) pr->getPid();
					msg.timestamp = std::time(NULL);
					ThreadSafeList::get_instance().push_back(msg);
				}				
			    }
			    delete pr;
                     }
		    }
                    params.clear();
                }


                /** cleanup resources */
                for (iter2 = jobs2.begin(); iter2 != jobs2.end(); ++iter2)
                    delete *iter2;
                jobs2.clear();
                for (fileiter = files.begin(); fileiter != files.end(); ++fileiter)
                    delete *fileiter;
                files.clear();
		fileIds.clear();
                
            }
        }
    }

    /* ---------------------------------------------------------------------- */
    void executeTransfer_a() {
        std::vector<int> requestIDs;
        std::vector<TransferJobs*> jobs2;
	jobs2.reserve(25);
	static bool drainMode = false;
	static long double counter = 0;
	static unsigned int countReverted = 0;

        while (1) {	
	   	if(stopThreads){
		if (!jobs2.empty()) {
                    std::vector<TransferJobs*>::iterator iter2;
                    for (iter2 = jobs2.begin(); iter2 != jobs2.end(); ++iter2)
                        delete *iter2;
                    jobs2.clear();
                }				
			return;
		}
	                  
	    if(DrainMode::getInstance()){
                if(!drainMode)
	    		FTS3_COMMON_LOGGER_NEWLOG(INFO) << "Set to drain mode, no more transfers for this instance!" << commit;
		drainMode = true;
		continue;
	    }else{
		drainMode=false;
	    }
	
            try {

                try {
		/*force fail to stall transfers*/		
		counter++;
                    if(counter == 1000){
                            DBSingleton::instance().getDBObjectInstance()->forceFailTransfers();
                            counter=0;
                    }
                    countReverted++;
                    if(countReverted==100){
                            DBSingleton::instance().getDBObjectInstance()->revertToSubmitted();
                            countReverted=0;
                    }

                    /*get jobs in submitted state*/
                    DBSingleton::instance().getDBObjectInstance()->getSubmittedJobs(jobs2, allowedVOs);
                    /*also get jobs which have been canceled by the client*/
                    DBSingleton::instance().getDBObjectInstance()->getCancelJob(requestIDs);
                    if (requestIDs.size() > 0) /*if canceled jobs found and transfer already started, kill them*/
                        killRunninfJob(requestIDs);
                    requestIDs.clear(); /*clean the list*/

                    if (!jobs2.empty())
                        executeUrlcopy(jobs2, false);

                    /* --- session reuse section ---*/
                    /*get jobs in submitted state and session reuse on*/
                    DBSingleton::instance().getDBObjectInstance()->getSubmittedJobsReuse(jobs2, allowedVOs);
                    if (!jobs2.empty())
                        executeUrlcopy(jobs2, true);
                } catch (Err& e) {
                    FTS3_COMMON_LOGGER_NEWLOG(ERR) << e.what() << commit;
                    throw;
                }

            } catch (...) {
                if (!jobs2.empty()) {
                    std::vector<TransferJobs*>::iterator iter2;
                    for (iter2 = jobs2.begin(); iter2 != jobs2.end(); ++iter2)
                        delete *iter2;
                    jobs2.clear();
                }
            }
            usleep(500000);
        } /*end while*/
   }

    /* ---------------------------------------------------------------------- */
    struct TestHelper {

        TestHelper()
        : loopOver(false) {
        }

        bool loopOver;
    }
    _testHelper;
};

FTS3_SERVER_NAMESPACE_END

