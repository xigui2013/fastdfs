/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include "tracker_global.h"

volatile bool g_continue_flag = true;
//�ṩ����Ķ˿�
int g_server_port = FDFS_TRACKER_SERVER_DEF_PORT;
/*
# ϵͳ�ṩ����ʱ�����������������V1.x����һ��������һ���̷߳���Ҳ���ǹ����߳�����
# ����V2.x������������͹����߳���û���κι�ϵ
*/
int g_max_connections = DEFAULT_MAX_CONNECTONS;
//accept thread count ��accept�̵߳��߳���
int g_accept_threads = 1;
//V2.0�������������������߳�����ͨ������ΪCPU��
int g_work_threads = DEFAULT_WORK_THREADS;
/*
# ͬ����ˢ����־��Ϣ��Ӳ�̵�ʱ��������λΪ��
# ע�⣺tracker server ����־����ʱʱдӲ�̵ģ�������д�ڴ档
*/
int g_sync_log_buff_interval = SYNC_LOG_BUFF_DEF_INTERVAL;
/*
��� storage server ����ʱ�������λΪ�롣
storage server������tracker server ��������
���tracker server��һ��check_active_interval�ڻ�û���յ�storage server
��һ���������Ǳ߽���Ϊ��storage server�Ѿ����ߡ�
���Ա�����ֵ�������storage server���õ�����ʱ������
ͨ������Ϊstorage server����ʱ������2����3����
*/
int g_check_active_interval = CHECK_ACTIVE_DEF_INTERVAL;

FDFSGroups g_groups;
int g_storage_stat_chg_count = 0;
int g_storage_sync_time_chg_count = 0; //sync timestamp
/*
# storage server �ϱ����Ŀռ䣬��֤ϵͳ������Ӧ������ռ䡣�����þ���ֵ���߰ٷֱȣ�V4��ʼ֧�ְٷֱȷ�ʽ����
#(ָ�� ���ͬ��ķ�������Ӳ�̴�Сһ��,����С��Ϊ׼,Ҳ����ֻҪͬ������һ̨�������ﵽ�����׼��,�����׼����Ч,ԭ�������Ϊ���ǽ��б���)
*/
FDFSStorageReservedSpace g_storage_reserved_space = { \
		TRACKER_STORAGE_RESERVED_SPACE_FLAG_MB};
//�������ӵ��� tracker server ��ip��Χ�����������͵����Ӷ���Ӱ�죬�����ͻ��ˣ�storage server��
int g_allow_ip_count = 0;
//ͬ��
in_addr_t *g_allow_ip_addrs = NULL;

struct base64_context g_base64_context;

gid_t g_run_by_gid;
uid_t g_run_by_uid;
//����ϵͳ����FastDFS���û��� (���� ���ǵ�ǰ�û���,�ĸ��������̾����ĸ�)
char g_run_by_group[32] = {0};
//����ϵͳ����FastDFS���û� (���� ���ǵ�ǰ�û�,�ĸ��������̾����ĸ�)
char g_run_by_user[32] = {0};
//����������Ƶ�storage server IP��ַ�ı�ʱ����Ⱥ�Ƿ��Զ�������ע��ֻ����storage server��������ʱ������Զ�������
bool g_storage_ip_changed_auto_adjust = true;
//�Ƿ�ʹ��server ID��Ϊstorage server��ʶ
bool g_use_storage_id = false;  //if use storage ID instead of IP address
byte g_id_type_in_filename = FDFS_ID_TYPE_IP_ADDRESS; //id type of the storage server in the filename
//�Ƿ�����תerror log��Ŀǰ��֧��һ����תһ��
bool g_rotate_error_log = false;  //if rotate the error log every day
//# error log������ת��ʱ��㣬ֻ�е�rotate_error_log����Ϊtrueʱ��Ч
TimeInfo g_error_log_rotate_time  = {0, 0, 0}; //rotate error log time base
/*
# �߳�ջ�Ĵ�С��FastDFS server�˲������̷߳�ʽ��
# �߳�ջԽ��һ���߳�ռ�õ�ϵͳ��Դ��Խ�ࡣ���Ҫ����������̣߳�V1.x��Ӧ�Ĳ���Ϊmax_connections��
V2.0Ϊwork_threads���������ʵ����ͱ�����ֵ��
*/
int g_thread_stack_size = 64 * 1024;
/*
# V2.0����Ĳ������洢������֮��ͬ���ļ�������ӳ�ʱ�䣬ȱʡΪ1�졣����ʵ��������е���
# ע������������Ӱ���ļ�ͬ�����̡����������������ļ�ʱ���ж��ļ��Ƿ��Ѿ���ͬ����ɵ�һ����ֵ������ֵ��
*/
int g_storage_sync_file_max_delay = DEFAULT_STORAGE_SYNC_FILE_MAX_DELAY;
/*
# V2.0����Ĳ������洢������ͬ��һ���ļ���Ҫ���ĵ����ʱ�䣬ȱʡΪ300s����5���ӡ�
# ע������������Ӱ���ļ�ͬ�����̡����������������ļ�ʱ����Ϊ�жϵ�ǰ�ļ��Ƿ�ͬ����ɵ�һ����ֵ������ֵ��
*/
int g_storage_sync_file_max_time = DEFAULT_STORAGE_SYNC_FILE_MAX_TIME;
/*
# �洢���ļ��Ƿ����symbol link���������ӣ���ʽ
# �������Ϊtrue��һ�����ļ���ռ�������ļ���ԭʼ�ļ���ָ�����ķ������ӡ�
*/
bool g_store_slave_file_use_link = false; //if store slave file use symbol link
//# V3.0����Ĳ������Ƿ�ʹ��С�ļ��ϲ��洢���ԣ�ȱʡ�ǹرյġ�
bool g_if_use_trunk_file = false;   //if use trunk file
/*# �Ƿ���ǰ����trunk file��ֻ�е��������Ϊtrue��
trunk_create_file_time_base 
trunk_create_file_interval 
trunk_create_file_space_threshold
��������Ч��
*/
bool g_trunk_create_file_advance = false;
//#trunk��ʼ��ʱ���Ƿ�����ÿռ��Ƿ�ռ��
bool g_trunk_init_check_occupying = false;
/*
# �Ƿ���������trunk binlog�м���trunk���ÿռ���Ϣ
# FastDFSȱʡ�Ǵӿ����ļ�storage_trunk.dat�м���trunk���ÿռ䣬
# ���ļ��ĵ�һ�м�¼����trunk binlog��offset��Ȼ���binlog��offset��ʼ����
*/
bool g_trunk_init_reload_from_binlog = false;
//# trunk file�������С�ֽ����������ļ�ֻ��16���ֽڣ�ϵͳҲ�����slot_min_size���ֽڡ�
int g_slot_min_size = 256;    //slot min size, such as 256 bytes
//ֻ���ļ���С<=�������ֵ���ļ����Ż�ϲ��洢�����һ���ļ��Ĵ�С�����������ֵ����ֱ�ӱ��浽һ���ļ��У��������úϲ��洢��ʽ����
int g_slot_max_size = 16 * 1024 * 1024;    //slot max size, such as 16MB
//�ϲ��洢��trunk file��С������4MB��ȱʡֵ��64MB�����������õù���
int g_trunk_file_size = 64 * 1024 * 1024;  //the trunk file size, such as 64MB
//��ǰ����trunk file����ʼʱ��㣨��׼ʱ�䣩��02:00��ʾ��һ�δ�����ʱ������賿2�㡣
TimeInfo g_trunk_create_file_time_base = {0, 0};
//����trunk file��ʱ��������λΪ�롣���ÿ��ֻ��ǰ����һ�Σ�������Ϊ86400
int g_trunk_create_file_interval = 86400;
//0����ѹ�������ֵָ�����ѹ��һ�ο���
int g_trunk_compress_binlog_min_interval = 0;
/*
# ��ǰ����trunk fileʱ����Ҫ�ﵽ�Ŀ���trunk��С
# ���籾����Ϊ20G������ǰ����trunkΪ4GB����ôֻ��Ҫ����16GB��trunk file���ɡ�
*/
int64_t g_trunk_create_file_space_threshold = 0;
//��������ʱ��(�п����ǵ�һ������ʱ��)
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

