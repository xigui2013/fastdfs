/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include "tracker_global.h"

volatile bool g_continue_flag = true;
//提供服务的端口
int g_server_port = FDFS_TRACKER_SERVER_DEF_PORT;
/*
# 系统提供服务时的最大连接数。对于V1.x，因一个连接由一个线程服务，也就是工作线程数。
# 对于V2.x，最大连接数和工作线程数没有任何关系
*/
int g_max_connections = DEFAULT_MAX_CONNECTONS;
//accept thread count 是accept线程的线程数
int g_accept_threads = 1;
//V2.0引入的这个参数，工作线程数，通常设置为CPU数
int g_work_threads = DEFAULT_WORK_THREADS;
/*
# 同步或刷新日志信息到硬盘的时间间隔，单位为秒
# 注意：tracker server 的日志不是时时写硬盘的，而是先写内存。
*/
int g_sync_log_buff_interval = SYNC_LOG_BUFF_DEF_INTERVAL;
/*
检测 storage server 存活的时间隔，单位为秒。
storage server定期向tracker server 发心跳，
如果tracker server在一个check_active_interval内还没有收到storage server
的一次心跳，那边将认为该storage server已经下线。
所以本参数值必须大于storage server配置的心跳时间间隔。
通常配置为storage server心跳时间间隔的2倍或3倍。
*/
int g_check_active_interval = CHECK_ACTIVE_DEF_INTERVAL;

FDFSGroups g_groups;
int g_storage_stat_chg_count = 0;
int g_storage_sync_time_chg_count = 0; //sync timestamp
/*
# storage server 上保留的空间，保证系统或其他应用需求空间。可以用绝对值或者百分比（V4开始支持百分比方式）。
#(指出 如果同组的服务器的硬盘大小一样,以最小的为准,也就是只要同组中有一台服务器达到这个标准了,这个标准就生效,原因就是因为他们进行备份)
*/
FDFSStorageReservedSpace g_storage_reserved_space = { \
		TRACKER_STORAGE_RESERVED_SPACE_FLAG_MB};
//可以连接到此 tracker server 的ip范围（对所有类型的连接都有影响，包括客户端，storage server）
int g_allow_ip_count = 0;
//同上
in_addr_t *g_allow_ip_addrs = NULL;

struct base64_context g_base64_context;

gid_t g_run_by_gid;
uid_t g_run_by_uid;
//操作系统运行FastDFS的用户组 (不填 就是当前用户组,哪个启动进程就是哪个)
char g_run_by_group[32] = {0};
//操作系统运行FastDFS的用户 (不填 就是当前用户,哪个启动进程就是哪个)
char g_run_by_user[32] = {0};
//这个参数控制当storage server IP地址改变时，集群是否自动调整。注：只有在storage server进程重启时才完成自动调整。
bool g_storage_ip_changed_auto_adjust = true;
//是否使用server ID作为storage server标识
bool g_use_storage_id = false;  //if use storage ID instead of IP address
byte g_id_type_in_filename = FDFS_ID_TYPE_IP_ADDRESS; //id type of the storage server in the filename
//是否定期轮转error log，目前仅支持一天轮转一次
bool g_rotate_error_log = false;  //if rotate the error log every day
//# error log定期轮转的时间点，只有当rotate_error_log设置为true时有效
TimeInfo g_error_log_rotate_time  = {0, 0, 0}; //rotate error log time base
/*
# 线程栈的大小。FastDFS server端采用了线程方式。
# 线程栈越大，一个线程占用的系统资源就越多。如果要启动更多的线程（V1.x对应的参数为max_connections，
V2.0为work_threads），可以适当降低本参数值。
*/
int g_thread_stack_size = 64 * 1024;
/*
# V2.0引入的参数。存储服务器之间同步文件的最大延迟时间，缺省为1天。根据实际情况进行调整
# 注：本参数并不影响文件同步过程。本参数仅在下载文件时，判断文件是否已经被同步完成的一个阀值（经验值）
*/
int g_storage_sync_file_max_delay = DEFAULT_STORAGE_SYNC_FILE_MAX_DELAY;
/*
# V2.0引入的参数。存储服务器同步一个文件需要消耗的最大时间，缺省为300s，即5分钟。
# 注：本参数并不影响文件同步过程。本参数仅在下载文件时，作为判断当前文件是否被同步完成的一个阀值（经验值）
*/
int g_storage_sync_file_max_time = DEFAULT_STORAGE_SYNC_FILE_MAX_TIME;
/*
# 存储从文件是否采用symbol link（符号链接）方式
# 如果设置为true，一个从文件将占用两个文件：原始文件及指向它的符号链接。
*/
bool g_store_slave_file_use_link = false; //if store slave file use symbol link
//# V3.0引入的参数。是否使用小文件合并存储特性，缺省是关闭的。
bool g_if_use_trunk_file = false;   //if use trunk file
/*# 是否提前创建trunk file。只有当这个参数为true，
trunk_create_file_time_base 
trunk_create_file_interval 
trunk_create_file_space_threshold
参数才有效。
*/
bool g_trunk_create_file_advance = false;
//#trunk初始化时，是否检查可用空间是否被占用
bool g_trunk_init_check_occupying = false;
/*
# 是否无条件从trunk binlog中加载trunk可用空间信息
# FastDFS缺省是从快照文件storage_trunk.dat中加载trunk可用空间，
# 该文件的第一行记录的是trunk binlog的offset，然后从binlog的offset开始加载
*/
bool g_trunk_init_reload_from_binlog = false;
//# trunk file分配的最小字节数。比如文件只有16个字节，系统也会分配slot_min_size个字节。
int g_slot_min_size = 256;    //slot min size, such as 256 bytes
//只有文件大小<=这个参数值的文件，才会合并存储。如果一个文件的大小大于这个参数值，将直接保存到一个文件中（即不采用合并存储方式）。
int g_slot_max_size = 16 * 1024 * 1024;    //slot max size, such as 16MB
//合并存储的trunk file大小，至少4MB，缺省值是64MB。不建议设置得过大。
int g_trunk_file_size = 64 * 1024 * 1024;  //the trunk file size, such as 64MB
//提前创建trunk file的起始时间点（基准时间），02:00表示第一次创建的时间点是凌晨2点。
TimeInfo g_trunk_create_file_time_base = {0, 0};
//创建trunk file的时间间隔，单位为秒。如果每天只提前创建一次，则设置为86400
int g_trunk_create_file_interval = 86400;
//0代表不压缩，这个值指定多久压缩一次快照
int g_trunk_compress_binlog_min_interval = 0;
/*
# 提前创建trunk file时，需要达到的空闲trunk大小
# 比如本参数为20G，而当前空闲trunk为4GB，那么只需要创建16GB的trunk file即可。
*/
int64_t g_trunk_create_file_space_threshold = 0;
//记入启动时间(有可能是第一次启动时间)
time_t g_up_time = 0;
TrackerStatus g_tracker_last_status = {0, 0};

#ifdef WITH_HTTPD
FDFSHTTPParams g_http_params;
int g_http_check_interval = 30;
int g_http_check_type = FDFS_HTTP_CHECK_ALIVE_TYPE_TCP;
char g_http_check_uri[128] = {0};
bool g_http_servers_dirty = false;
#endif

#if defined(DEBUG_FLAG) && defined(OS_LINUX)
char g_exe_name[256] = {0};
#endif

int g_log_file_keep_days = 0;
FDFSConnectionStat g_connection_stat = {0, 0};

