/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

//tracker_proto.h

#ifndef _TRACKER_PROTO_H_
#define _TRACKER_PROTO_H_

#include "tracker_types.h"
#include "connection_pool.h"
#include "ini_file_reader.h"

//处理storage的加入请求，tracker是不会发送join请求的，
//tracker的server是通过storage的join请求加入的
/**
 * function: storage join to tracker (将storage加入到tracker的track列表)
 * request body:
	 @ FDFS_GROUP_NAME_MAX_LEN + 1 bytes: group name
	 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port
	 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage http server port
	 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: path count
	 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: subdir count per path
	 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: upload priority
	 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: join time (join timestamp)
	 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: up time (start timestamp)
	 @ FDFS_VERSION_SIZE bytes: storage server version
	 @ FDFS_DOMAIN_NAME_MAX_SIZE bytes: domain name of the web server on the storage server
	 @ 1 byte: init flag ( 1 for init done)
	 @ 1 byte: storage server status
	 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: tracker server count excluding current tracker
 * response body:
	 @ FDFS_IPADDR_SIZE bytes: sync source storage server ip address
 * memo: return all storage servers in the group only when storage servers changed or return none
 （仅当storage server生效才返回所有的storage server,否则返回空）

 */
#define TRACKER_PROTO_CMD_STORAGE_JOIN              81
/**
 * function: notify server connection will be closed
 * request body: none (no body part)
 * response: none (no header and no body)
 */
#define FDFS_PROTO_CMD_QUIT			    82
//storage心跳，定时发送storage自身信息
/**
 * 
# function: heart beat （心跳）
# request body: none or storage stat info （返回空或者storage状态信息）
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: total upload count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: success upload count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: total set metadata count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: success set metadata count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: total delete count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: success delete count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: total download count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: success download count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: total get metadata count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: success get metadata count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: total create link count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: success create link count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: total delete link count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: success delete link count
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: last source update timestamp
	@ TRACKER_PROTO_PKG_LEN_SIZE bytes: last sync update timestamp
	@TRACKER_PROTO_PKG_LEN_SIZE bytes:	last synced timestamp
	@TRACKER_PROTO_PKG_LEN_SIZE bytes:	last heart beat timestamp
# response body: same to command TRACKER_PROTO_CMD_STORAGE_JOIN
# memo: storage server sync it's stat info to tracker server only when storage stat info changed
（仅当storage server的状态信息改变时才将其同步到tracker）
 */
#define TRACKER_PROTO_CMD_STORAGE_BEAT              83  //storage heart beat
/*
# function: report disk usage(报告硬盘使用情况)
# request body:
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total space in MB（硬盘总体积）
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: free space in MB （空闲大小）
# response body: same to command TRACKER_PROTO_CMD_STORAGE_JOIN

*/
#define TRACKER_PROTO_CMD_STORAGE_REPORT_DISK_USAGE 84  //report disk usage
/*复制新的storage
# function: replica new storage servers which maybe not exist in the tracker server （？？？）
# request body: n * (1 + FDFS_IPADDR_SIZE) bytes, n >= 1. One storage entry format:
    @ 1 byte: storage server status
    @ FDFS_IPADDR_SIZE bytes: storage server ip address
# response body: none
*/
#define TRACKER_PROTO_CMD_STORAGE_REPLICA_CHG       85  //repl new storage servers
/*
# function: source storage require sync. when add a new storage server, the existed storage servers in the same group will ask the tracker server to tell the source storage server which will sync old data to it（当新添加一个storage server时，同组的其他storage server会向tracker请求告诉新的storage有数据要同步给他）
# request body:
    @ FDFS_IPADDR_SIZE bytes: dest storage server (new storage server) ip address （新加入的storage server的ip地址）
# response body: none or
    @ FDFS_IPADDR_SIZE bytes: source storage server ip address（源storage server的ip地址）
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: sync until timestamp
# memo: if the dest storage server not do need sync from one of storage servers in the group, the response body is emtpy（若新storage server不需要源storage server的同步，返回空）

*/
#define TRACKER_PROTO_CMD_STORAGE_SYNC_SRC_REQ      86  //src storage require sync
//本storage请求同步
/*
# function: dest storage server (new storage server) require sync（新加入的storage 向tracker请求同步）
# request body: none
# response body: none or
    @ FDFS_IPADDR_SIZE bytes: source storage server ip address
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: sync until timestamp
# memo: if the dest storage server not do need sync from one of storage servers in the group, the response body is emtpy（若新storage server不需要源storage server的同步，返回空）

*/
#define TRACKER_PROTO_CMD_STORAGE_SYNC_DEST_REQ     87  //dest storage require sync
/*
# function: new storage server sync notify(新storage同步通告)
# request body:
    @ FDFS_IPADDR_SIZE bytes: source storage server ip address
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: sync until timestamp
# response body: same to command TRACKER_PROTO_CMD_STORAGE_JOIN

*/
#define TRACKER_PROTO_CMD_STORAGE_SYNC_NOTIFY       88  //sync done notify
#define TRACKER_PROTO_CMD_STORAGE_SYNC_REPORT	    89  //report src last synced time as dest server
//查询同步源storage
#define TRACKER_PROTO_CMD_STORAGE_SYNC_DEST_QUERY   79 //dest storage query sync src storage server
#define TRACKER_PROTO_CMD_STORAGE_REPORT_IP_CHANGED 78  //storage server report it's ip changed
#define TRACKER_PROTO_CMD_STORAGE_CHANGELOG_REQ     77  //storage server request storage server's changelog
#define TRACKER_PROTO_CMD_STORAGE_REPORT_STATUS     76  //report specified storage server status
#define TRACKER_PROTO_CMD_STORAGE_PARAMETER_REQ	    75  //storage server request parameters
#define TRACKER_PROTO_CMD_STORAGE_REPORT_TRUNK_FREE 74  //storage report trunk free space
#define TRACKER_PROTO_CMD_STORAGE_REPORT_TRUNK_FID  73  //storage report current trunk file id
#define TRACKER_PROTO_CMD_STORAGE_FETCH_TRUNK_FID   72  //storage get current trunk file id
#define TRACKER_PROTO_CMD_STORAGE_GET_STATUS	    71  //get storage status from tracker
//获取storage的id
#define TRACKER_PROTO_CMD_STORAGE_GET_SERVER_ID	    70  //get storage server id from tracker
//获取所有storage的id
#define TRACKER_PROTO_CMD_STORAGE_FETCH_STORAGE_IDS 69  //get all storage ids from tracker
#define TRACKER_PROTO_CMD_STORAGE_GET_GROUP_NAME   109  //get storage group name from tracker

#define TRACKER_PROTO_CMD_TRACKER_GET_SYS_FILES_START    61  //start of tracker get system data files
#define TRACKER_PROTO_CMD_TRACKER_GET_SYS_FILES_END      62  //end of tracker get system data files
#define TRACKER_PROTO_CMD_TRACKER_GET_ONE_SYS_FILE       63  //tracker get a system data file
		//获取其他tracker的状态,选主的时候用
#define TRACKER_PROTO_CMD_TRACKER_GET_STATUS             64  //tracker get status of other tracker
		//ping请求，会收到tracker管理的组名及storage id 的返回
#define TRACKER_PROTO_CMD_TRACKER_PING_LEADER            65  //tracker ping leader
		//通知全tracker，清空leader信息
#define TRACKER_PROTO_CMD_TRACKER_NOTIFY_NEXT_LEADER     66  //notify next leader to other trackers
		//通知全tracker,设置新的leader信息
#define TRACKER_PROTO_CMD_TRACKER_COMMIT_NEXT_LEADER     67  //commit next leader to other trackers
#define TRACKER_PROTO_CMD_TRACKER_NOTIFY_RESELECT_LEADER 68  //storage notify reselect leader when split-brain

#define TRACKER_PROTO_CMD_SERVER_LIST_ONE_GROUP			90
/*
# function: list all groups （列出所有的group）
# request body: none
# response body: n group entries, n >= 0, the format of each entry:
 @ FDFS_GROUP_NAME_MAX_LEN+1 bytes: group name
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: free disk storage in MB
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server count
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server http port
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: active server count
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: current write server index
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: store path count on storage server
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: subdir count per path on storage server

*/
#define TRACKER_PROTO_CMD_SERVER_LIST_ALL_GROUPS		91
/*
# function: list storage servers of a group（列出某个组的所有storage server）
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: the group name to query(要列的组名)
# response body: n storage entries, n >= 0, the format of each entry:
   @ 1 byte: status
   @ FDFS_IPADDR_SIZE bytes: ip address
   @ FDFS_DOMAIN_NAME_MAX_SIZE  bytes : domain name of the web server
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: source storage server ip address
   @ FDFS_VERSION_SIZE bytes: storage server version
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: join time (join in timestamp)
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: up time (start timestamp)
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total space in MB
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: free space in MB
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: upload priority
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: store path count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: subdir count per path
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: current write path[
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage http port
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total upload count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: success upload count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total set metadata count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: success set metadata count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total delete count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: success delete count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total download count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: success download count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total get metadata count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: success get metadata count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total create link count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: success create link count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: total delete link count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: success delete link count
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: last source update timestamp
   @ TRACKER_PROTO_PKG_LEN_SIZE bytes: last sync update timestamp
   @TRACKER_PROTO_PKG_LEN_SIZE bytes:  last synced timestamp
   @TRACKER_PROTO_PKG_LEN_SIZE bytes:  last heart beat timestamp

*/
#define TRACKER_PROTO_CMD_SERVER_LIST_STORAGE			92
#define TRACKER_PROTO_CMD_SERVER_DELETE_STORAGE			93
#define TRACKER_PROTO_CMD_SERVER_SET_TRUNK_SERVER		94
//用户查询可用的storage
/*
# function: query which storage server to store file(查询向哪一个storage server保存文件)
# request body: none
# response body:
 @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
 @ FDFS_IPADDR_SIZE - 1 bytes: storage server ip address
 @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port
 @1 byte: store path index on the storage server

*/
#define TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITHOUT_GROUP_ONE	101
/*
# function: query which storage server to download the file（查询向哪个storage server 请求下载文件）
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename
# response body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ FDFS_IPADDR_SIZE - 1 bytes: storage server ip address
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port

*/
#define TRACKER_PROTO_CMD_SERVICE_QUERY_FETCH_ONE		102
#define TRACKER_PROTO_CMD_SERVICE_QUERY_UPDATE  		103
/*
# function: query which storage server to store file
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
# response body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ FDFS_IPADDR_SIZE - 1 bytes: storage server ip address
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port
    @1 byte: store path index on the storage server

*/
#define TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITH_GROUP_ONE	104
/*
# function: query all storage servers to download the file
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename
# response body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ FDFS_IPADDR_SIZE - 1 bytes: storage server ip address
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port
    @ n * (FDFS_IPADDR_SIZE - 1) bytes:  storage server ip addresses, n can be 0

*/
#define TRACKER_PROTO_CMD_SERVICE_QUERY_FETCH_ALL		105
/*
# function: query which storage server to store file
# request body: none
# response body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ FDFS_IPADDR_SIZE - 1 bytes: storage server ip address (* multi)
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port (*multi)
    @1 byte: store path index on the storage server

*/
#define TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITHOUT_GROUP_ALL	106
/*
# function: query which storage server to store file
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
# response body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ FDFS_IPADDR_SIZE - 1 bytes: storage server ip address  (* multi)
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: storage server port   (* multi)
    @1 byte: store path index on the storage server

*/
#define TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITH_GROUP_ALL	107
#define TRACKER_PROTO_CMD_SERVER_DELETE_GROUP			108

#define TRACKER_PROTO_CMD_RESP					100
/**
 * function: active test （测试是否active）
 * request body: none
 * response body: none
 */
#define FDFS_PROTO_CMD_ACTIVE_TEST				111  //active test, tracker and storage both support since V1.28
//storage 间发送自己id
#define STORAGE_PROTO_CMD_REPORT_SERVER_ID	9  
//往storage上传文件
/*
# function: upload file to storage server(上传文件到storage server)
# request body:
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: meta data bytes
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: file size
    @ meta data bytes: each meta data seperated by \x01,
                        name and value seperated by \x02
    @ file size bytes: file content
# response body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename

*/
#define STORAGE_PROTO_CMD_UPLOAD_FILE		11
/*
# function: delete file from storage server(从storage server中删除文件)
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename
# response body: none

*/

#define STORAGE_PROTO_CMD_DELETE_FILE		12
/**
# function: delete file from storage server（从storage server中删除文件）
# request body:
		@ TRACKER_PROTO_PKG_LEN_SIZE bytes: filename length
		@ TRACKER_PROTO_PKG_LEN_SIZE bytes: meta data size
		@ 1 bytes: operation flag,
			'O' for overwrite all old metadata（O, 覆盖所有metadata）
			'M' for merge, insert when the meta item not exist, otherwise update it（M, 插入或更新metadata）
		@ FDFS_GROUP_NAME_MAX_LEN bytes: group name
		@ filename bytes: filename
		@ meta data bytes: each meta data seperated by \x01,
							name and value seperated by \x02
# response body: none
	
*/
//设置文件属性
#define STORAGE_PROTO_CMD_SET_METADATA		13
/*
# function: download/fetch file from storage server（从storage server下载文件）
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename
# response body:
    @ file content

*/
#define STORAGE_PROTO_CMD_DOWNLOAD_FILE		14
/*
# function: get metat data from storage server（从storage server获取metadata）
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename
# response body
    @ meta data buff, each meta data seperated by \x01, name and value seperated by \x02

*/
#define STORAGE_PROTO_CMD_GET_METADATA		15
/*
# function: sync new created file （同步新创建的文件）
# request body:
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: filename bytes
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: file size/bytes
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes : filename
    @ file size bytes: file content
# response body: none

*/
#define STORAGE_PROTO_CMD_SYNC_CREATE_FILE	16
/*
# function: sync deleted file（同步删除的文件）
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename
# response body: none

*/
#define STORAGE_PROTO_CMD_SYNC_DELETE_FILE	17
/*
# function: sync updated file （同步更新的文件）
# request body: same to command STORAGE_PROTO_CMD_SYNC_CREATE_FILE
# respose body: none
*/
#define STORAGE_PROTO_CMD_SYNC_UPDATE_FILE	18
#define STORAGE_PROTO_CMD_SYNC_CREATE_LINK	19
#define STORAGE_PROTO_CMD_CREATE_LINK		20
/*
# function: upload slave file to storage server
# request body:
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: master filename length
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: file size
    @ FDFS_FILE_PREFIX_MAX_LEN bytes: filename prefix
    @ FDFS_FILE_EXT_NAME_MAX_LEN bytes: file ext name, do not include dot (.)
    @ master filename bytes: master filename
    @ file size bytes: file content
# response body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename

*/
#define STORAGE_PROTO_CMD_UPLOAD_SLAVE_FILE	21
/*
# function: query file info from storage server
# request body:
    @ FDFS_GROUP_NAME_MAX_LEN bytes: group name
    @ filename bytes: filename
# response body:
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: file size
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: file create timestamp
    @ TRACKER_PROTO_PKG_LEN_SIZE bytes: file CRC32 signature

*/
#define STORAGE_PROTO_CMD_QUERY_FILE_INFO	22
#define STORAGE_PROTO_CMD_UPLOAD_APPENDER_FILE	23   //create appender file
#define STORAGE_PROTO_CMD_APPEND_FILE		24   //append file
#define STORAGE_PROTO_CMD_SYNC_APPEND_FILE	25
//从某个storage获取binlog
#define STORAGE_PROTO_CMD_FETCH_ONE_PATH_BINLOG	26   //fetch binlog of one store path
#define STORAGE_PROTO_CMD_RESP			TRACKER_PROTO_CMD_RESP
#define STORAGE_PROTO_CMD_UPLOAD_MASTER_FILE	STORAGE_PROTO_CMD_UPLOAD_FILE

#define STORAGE_PROTO_CMD_TRUNK_ALLOC_SPACE   	     27  //since V3.00, storage to trunk server
#define STORAGE_PROTO_CMD_TRUNK_ALLOC_CONFIRM	     28  //since V3.00, storage to trunk server
#define STORAGE_PROTO_CMD_TRUNK_FREE_SPACE	     29  //since V3.00, storage to trunk server
#define STORAGE_PROTO_CMD_TRUNK_SYNC_BINLOG	     30  //since V3.00, trunk storage to storage
#define STORAGE_PROTO_CMD_TRUNK_GET_BINLOG_SIZE	     31  //since V3.07, tracker to storage
#define STORAGE_PROTO_CMD_TRUNK_DELETE_BINLOG_MARKS  32  //since V3.07, tracker to storage
#define STORAGE_PROTO_CMD_TRUNK_TRUNCATE_BINLOG_FILE 33  //since V3.07, trunk storage to storage

#define STORAGE_PROTO_CMD_MODIFY_FILE		     34  //since V3.08
#define STORAGE_PROTO_CMD_SYNC_MODIFY_FILE	     35  //since V3.08
#define STORAGE_PROTO_CMD_TRUNCATE_FILE		     36  //since V3.08
#define STORAGE_PROTO_CMD_SYNC_TRUNCATE_FILE	     37  //since V3.08

//for overwrite all old metadata
#define STORAGE_SET_METADATA_FLAG_OVERWRITE	'O'
#define STORAGE_SET_METADATA_FLAG_OVERWRITE_STR	"O"
//for replace, insert when the meta item not exist, otherwise update it
#define STORAGE_SET_METADATA_FLAG_MERGE		'M'
#define STORAGE_SET_METADATA_FLAG_MERGE_STR	"M"

#define FDFS_PROTO_PKG_LEN_SIZE		8
#define FDFS_PROTO_CMD_SIZE		1
#define FDFS_PROTO_IP_PORT_SIZE		(IP_ADDRESS_SIZE + 6)

#define TRACKER_QUERY_STORAGE_FETCH_BODY_LEN	(FDFS_GROUP_NAME_MAX_LEN \
			+ IP_ADDRESS_SIZE - 1 + FDFS_PROTO_PKG_LEN_SIZE)
#define TRACKER_QUERY_STORAGE_STORE_BODY_LEN	(FDFS_GROUP_NAME_MAX_LEN \
			+ IP_ADDRESS_SIZE - 1 + FDFS_PROTO_PKG_LEN_SIZE + 1)

#define STORAGE_TRUNK_ALLOC_CONFIRM_REQ_BODY_LEN  (FDFS_GROUP_NAME_MAX_LEN \
			+ sizeof(FDFSTrunkInfoBuff))

typedef struct
{
	char pkg_len[FDFS_PROTO_PKG_LEN_SIZE];  //body length, not including header
	char cmd;    //command code
	char status; //status code for response
} TrackerHeader;

typedef struct
{
	char group_name[FDFS_GROUP_NAME_MAX_LEN+1];
	char storage_port[FDFS_PROTO_PKG_LEN_SIZE];
	char storage_http_port[FDFS_PROTO_PKG_LEN_SIZE];
	char store_path_count[FDFS_PROTO_PKG_LEN_SIZE];
	char subdir_count_per_path[FDFS_PROTO_PKG_LEN_SIZE];
	char upload_priority[FDFS_PROTO_PKG_LEN_SIZE];
	char join_time[FDFS_PROTO_PKG_LEN_SIZE]; //storage join timestamp
	char up_time[FDFS_PROTO_PKG_LEN_SIZE];   //storage service started timestamp
	char version[FDFS_VERSION_SIZE];   //storage version
	char domain_name[FDFS_DOMAIN_NAME_MAX_SIZE];
	char init_flag;
	signed char status;
	char tracker_count[FDFS_PROTO_PKG_LEN_SIZE];  //all tracker server count
} TrackerStorageJoinBody;

typedef struct
{
	char src_id[FDFS_STORAGE_ID_MAX_SIZE];  //src storage id
} TrackerStorageJoinBodyResp;

typedef struct
{
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
	char sz_total_mb[FDFS_PROTO_PKG_LEN_SIZE]; //total disk storage in MB
	char sz_free_mb[FDFS_PROTO_PKG_LEN_SIZE];  //free disk storage in MB
	char sz_trunk_free_mb[FDFS_PROTO_PKG_LEN_SIZE];  //trunk free space in MB
	char sz_count[FDFS_PROTO_PKG_LEN_SIZE];    //server count
	char sz_storage_port[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_storage_http_port[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_active_count[FDFS_PROTO_PKG_LEN_SIZE]; //active server count
	char sz_current_write_server[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_store_path_count[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_subdir_count_per_path[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_current_trunk_file_id[FDFS_PROTO_PKG_LEN_SIZE];
} TrackerGroupStat;

typedef struct
{
	char status;
	char id[FDFS_STORAGE_ID_MAX_SIZE];
	char ip_addr[IP_ADDRESS_SIZE];
	char domain_name[FDFS_DOMAIN_NAME_MAX_SIZE];
	char src_id[FDFS_STORAGE_ID_MAX_SIZE];  //src storage id
	char version[FDFS_VERSION_SIZE];
	char sz_join_time[8];
	char sz_up_time[8];
	char sz_total_mb[8];
	char sz_free_mb[8];
	char sz_upload_priority[8];
	char sz_store_path_count[8];
	char sz_subdir_count_per_path[8];
	char sz_current_write_path[8];
	char sz_storage_port[8];
	char sz_storage_http_port[8];
	FDFSStorageStatBuff stat_buff;
	char if_trunk_server;
} TrackerStorageStat;

typedef struct
{
	char src_id[FDFS_STORAGE_ID_MAX_SIZE];   //src storage id
	char until_timestamp[FDFS_PROTO_PKG_LEN_SIZE];
} TrackerStorageSyncReqBody;

typedef struct
{
	char sz_total_mb[8];
	char sz_free_mb[8];
} TrackerStatReportReqBody;

typedef struct
{
        unsigned char store_path_index;
        unsigned char sub_path_high;
        unsigned char sub_path_low;
        char id[4];
        char offset[4];
	char size[4];
} FDFSTrunkInfoBuff;

#ifdef __cplusplus
extern "C" {
#endif

#define tracker_connect_server(pTrackerServer, err_no) \
	tracker_connect_server_ex(pTrackerServer, g_fdfs_connect_timeout, err_no)

/**
* connect to the tracker server
* params:
*	pTrackerServer: tracker server
*	connect_timeout: connect timeout in seconds
*	err_no: return the error no
* return: ConnectionInfo pointer for success, NULL for fail
**/
ConnectionInfo *tracker_connect_server_ex(ConnectionInfo *pTrackerServer, \
		const int connect_timeout, int *err_no);


/**
* connect to the tracker server directly without connection pool
* params:
*	pTrackerServer: tracker server
* return: 0 for success, none zero for fail
**/
int tracker_connect_server_no_pool(ConnectionInfo *pTrackerServer);

#define tracker_disconnect_server(pTrackerServer) \
	tracker_disconnect_server_ex(pTrackerServer, false)

/**
* close all connections to tracker servers
* params:
*	pTrackerServer: tracker server
*	bForceClose: if force close the connection when use connection pool
* return:
**/
void tracker_disconnect_server_ex(ConnectionInfo *pTrackerServer, \
	const bool bForceClose);

int fdfs_validate_group_name(const char *group_name);
int fdfs_validate_filename(const char *filename);
int metadata_cmp_by_name(const void *p1, const void *p2);

const char *get_storage_status_caption(const int status);

int fdfs_recv_header(ConnectionInfo *pTrackerServer, int64_t *in_bytes);

int fdfs_recv_response(ConnectionInfo *pTrackerServer, \
		char **buff, const int buff_size, \
		int64_t *in_bytes);
int fdfs_quit(ConnectionInfo *pTrackerServer);

#define fdfs_active_test(pTrackerServer) \
	fdfs_deal_no_body_cmd(pTrackerServer, FDFS_PROTO_CMD_ACTIVE_TEST)

int fdfs_deal_no_body_cmd(ConnectionInfo *pTrackerServer, const int cmd);

int fdfs_deal_no_body_cmd_ex(const char *ip_addr, const int port, const int cmd);

#define fdfs_split_metadata(meta_buff, meta_count, err_no) \
		fdfs_split_metadata_ex(meta_buff, FDFS_RECORD_SEPERATOR, \
		FDFS_FIELD_SEPERATOR, meta_count, err_no)

char *fdfs_pack_metadata(const FDFSMetaData *meta_list, const int meta_count, \
			char *meta_buff, int *buff_bytes);
FDFSMetaData *fdfs_split_metadata_ex(char *meta_buff, \
		const char recordSeperator, const char filedSeperator, \
		int *meta_count, int *err_no);

int fdfs_get_ini_context_from_tracker(TrackerServerGroup *pTrackerGroup, \
                IniContext *iniContext, bool * volatile continue_flag, \
                const bool client_bind_addr, const char *bind_addr);

int fdfs_get_tracker_status(ConnectionInfo *pTrackerServer, \
		TrackerRunningStatus *pStatus);

#ifdef __cplusplus
}
#endif

#endif

