/**
 * Conversion from and into SOCI
 */
#pragma once

#include <soci.h>

namespace soci
{
    template <>
    struct type_conversion<Cred> {
        typedef values base_type;

        static void from_base(values const& v, indicator, Cred& cred) {
            struct tm termination_st;
            cred.DN               = v.get<std::string>("dn");
            cred.delegationID     = v.get<std::string>("dlg_id");
            cred.proxy            = v.get<std::string>("proxy");
            termination_st        = v.get<struct tm>("termination_time");
            cred.termination_time = mktime(&termination_st);
            cred.vomsAttributes   = v.get<std::string>("voms_attrs", std::string());
        }
    };

    template <>
    struct type_conversion<CredCache> {
        typedef values base_type;

        static void from_base(values const& v, indicator, CredCache& ccache) {
            ccache.DN                 = v.get<std::string>("dn");
            ccache.certificateRequest = v.get<std::string>("cert_request");
            ccache.delegationID       = v.get<std::string>("dlg_id");
            ccache.privateKey         = v.get<std::string>("priv_key");
            ccache.vomsAttributes     = v.get<std::string>("voms_attrs", std::string());
        }
    };

    template <>
    struct type_conversion<TransferJobs> {
        typedef values base_type;

        static void from_base(values const& v, indicator, TransferJobs& job) {
            job.JOB_ID         = v.get<std::string>("job_id");
            job.JOB_STATE      = v.get<std::string>("job_state");
            job.VO_NAME        = v.get<std::string>("vo_name");
            job.PRIORITY       = v.get<int>("priority");
            job.SOURCE         = v.get<std::string>("source", "");
            job.DEST           = v.get<std::string>("dest", "");
            job.AGENT_DN       = v.get<std::string>("agent_dn", "");
            job.SUBMIT_HOST    = v.get<std::string>("submit_host");
            job.SOURCE_SE      = v.get<std::string>("source_se");
            job.DEST_SE        = v.get<std::string>("dest_se");
            job.USER_DN        = v.get<std::string>("user_dn");
            job.USER_CRED      = v.get<std::string>("user_cred");
            job.CRED_ID        = v.get<std::string>("cred_id");
            job.SPACE_TOKEN    = v.get<std::string>("space_token", "");
            job.STORAGE_CLASS  = v.get<std::string>("storage_class", "");
            job.INTERNAL_JOB_PARAMS = v.get<std::string>("job_params");
            job.OVERWRITE_FLAG = v.get<std::string>("overwrite_flag");
            job.SOURCE_SPACE_TOKEN = v.get<std::string>("source_space_token", "");
            job.SOURCE_TOKEN_DESCRIPTION = v.get<std::string>("source_token_description", "");
            job.COPY_PIN_LIFETIME  = v.get<int>("copy_pin_lifetime");
            job.CHECKSUM_METHOD    = v.get<std::string>("checksum_method");
        }
    };

    template <>
    struct type_conversion<TransferFiles> {
        typedef values base_type;

        static void from_base(values const& v, indicator, TransferFiles& file) {
            file.SOURCE_SURL = v.get<std::string>("source_surl");
            file.DEST_SURL   = v.get<std::string>("dest_surl");
            file.JOB_ID      = v.get<std::string>("job_id");
            file.VO_NAME     = v.get<std::string>("vo_name");
            file.FILE_ID     = v.get<int>("file_id");
            file.OVERWRITE   = v.get<std::string>("overwrite_flag");
            file.DN          = v.get<std::string>("user_dn");
            file.CRED_ID     = v.get<std::string>("cred_id");
            file.CHECKSUM    = v.get<std::string>("checksum");
            file.CHECKSUM_METHOD    = v.get<std::string>("checksum_method");
            file.SOURCE_SPACE_TOKEN = v.get<std::string>("source_space_token");
            file.DEST_SPACE_TOKEN   = v.get<std::string>("space_token");
        }
    };

    template <>
    struct type_conversion<SeAndConfig> {
        typedef values base_type;

        static void from_base(values const& v, indicator, SeAndConfig& sc) {
            sc.SE_NAME     = v.get<std::string>("se_name");
            sc.SHARE_ID    = v.get<std::string>("share_id");
            sc.SHARE_TYPE  = v.get<std::string>("share_type");
            sc.SHARE_VALUE = v.get<std::string>("share_value");
        }
    };

    template <>
    struct type_conversion<SeProtocolConfig> {
        typedef values base_type;

        static void from_base(values const& v, indicator, SeProtocolConfig& protoConfig) {
            struct tm aux_tm;

            protoConfig.ADMIN_DN  = v.get<std::string>("admin_dn", "");
            protoConfig.BANDWIDTH = v.get<int>("bandwith", 0);
            protoConfig.BLOCKSIZE = v.get<int>("blocksize", 0);
            protoConfig.CONTACT   = v.get<std::string>("contact", "");
            protoConfig.HTTP_TO   = v.get<int>("http_to", 0);
            aux_tm = v.get<struct tm>("LAST_ACTIVE");
            protoConfig.LAST_ACTIVE = mktime(&aux_tm);
            aux_tm = v.get<struct tm>("LAST_MODIFICATION");
            protoConfig.LAST_MODIFICATION = mktime(&aux_tm);
            protoConfig.MESSAGE = v.get<std::string>("message", "");
            protoConfig.NOFILES = v.get<int>("nofiles", 0);
            protoConfig.NOMINAL_THROUGHPUT = v.get<int>("nominal_throughput", 0);
            protoConfig.NOSTREAMS = v.get<int>("nostreams", 0);
            protoConfig.NO_TX_ACTIVITY_TO = v.get<int>("no_tx_activity_to", 0);
            protoConfig.PREPARING_FILES_RATIO = v.get<int>("preparing_files_ratio", 0);
            protoConfig.SE_GROUP_NAME = v.get<std::string>("se_group_name", "");
            protoConfig.SE_LIMIT = v.get<int>("se_limit", 0);
            protoConfig.SE_NAME = v.get<std::string>("se_name", "");
            protoConfig.SE_PAIR_STATE = v.get<std::string>("se_pair_state", "");
            protoConfig.SE_ROW_ID = v.get<int>("se_row_id");
            protoConfig.SRMCOPY_DIRECTION = v.get<std::string>("srmcopy_direction", "");
            protoConfig.SRMCOPY_REFRESH_TO = v.get<int>("srmcopy_refresh_to", 0);
        }
    };

    template <>
    struct type_conversion<JobStatus> {
        typedef values base_type;

        static void from_base(values const& v, indicator, JobStatus& job) {
            struct tm aux_tm;
            job.jobID      = v.get<std::string>("job_id");
            job.jobStatus  = v.get<std::string>("job_state");
            job.fileStatus = v.get<std::string>("file_state");
            job.clientDN   = v.get<std::string>("user_dn");
            job.reason     = v.get<std::string>("reason");
            aux_tm         = v.get<struct tm>("submit_time");
            job.submitTime = mktime(&aux_tm);
            job.priority   = v.get<int>("priority");
            job.voName     = v.get<std::string>("vo_name");
        }
    };
}
