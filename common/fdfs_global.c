/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include "logger.h"
#include "fdfs_global.h"

//���ӳ�ʱʱ�䣬���socket�׽��ֺ���connect
int g_fdfs_connect_timeout = DEFAULT_CONNECT_TIMEOUT;
//tracker server�����糬ʱ����λΪ�롣���ͻ��������ʱ������ڳ�ʱʱ��󻹲��ܷ��ͻ�������ݣ��򱾴�����ͨ��ʧ�ܡ�
int g_fdfs_network_timeout = DEFAULT_NETWORK_TIMEOUT;
/*
# base_path Ŀ¼��ַ(��Ŀ¼�������,��Ŀ¼���Զ�����)
# ��Ŀ¼˵��:
  tracker serverĿ¼���ļ��ṹ��
  ${base_path}
    |__data
    |     |__storage_groups.dat���洢������Ϣ
    |     |__storage_servers.dat���洢�������б�
    |__logs
          |__trackerd.log��tracker server��־�ļ�

�����ļ�storage_groups.dat��storage_servers.dat�еļ�¼֮���Ի��з���\n���ָ����ֶ�֮�������Ķ��ţ�,���ָ���
storage_groups.dat�е��ֶ�����Ϊ��
  1. group_name������
  2. storage_port��storage server�˿ں�

storage_servers.dat�м�¼storage server�����Ϣ���ֶ�����Ϊ��
  1. group_name����������
  2. ip_addr��ip��ַ
  3. status��״̬
  4. sync_src_ip_addr�����storage serverͬ�����������ļ���Դ������
  5. sync_until_timestamp��ͬ�����������ļ��Ľ���ʱ�䣨UNIXʱ�����
  6. stat.total_upload_count���ϴ��ļ�����
  7. stat.success_upload_count���ɹ��ϴ��ļ�����
  8. stat.total_set_meta_count������meta data����
  9. stat.success_set_meta_count���ɹ�����meta data����
  10. stat.total_delete_count��ɾ���ļ�����
  11. stat.success_delete_count���ɹ�ɾ���ļ�����
  12. stat.total_download_count�������ļ�����
  13. stat.success_download_count���ɹ������ļ�����
  14. stat.total_get_meta_count����ȡmeta data����
  15. stat.success_get_meta_count���ɹ���ȡmeta data����
  16. stat.last_source_update�����һ��Դͷ����ʱ�䣨���²������Կͻ��ˣ�
  17. stat.last_sync_update�����һ��ͬ������ʱ�䣨���²�����������storage server��ͬ����
*/
char g_fdfs_base_path[MAX_PATH_SIZE] = {'/', 't', 'm', 'p', '\0'};
Version g_fdfs_version = {5, 8};
bool g_use_connection_pool = false;
ConnectionPool g_connection_pool;
int g_connection_pool_max_idle_time = 3600;

/*
data filename format:
HH/HH/filename: HH for 2 uppercase hex chars
*/
int fdfs_check_data_filename(const char *filename, const int len)
{
	if (len < 6)
	{
		logError("file: "__FILE__", line: %d, " \
			"the length=%d of filename \"%s\" is too short", \
			__LINE__, len, filename);
		return EINVAL;
	}

	if (!IS_UPPER_HEX(*filename) || !IS_UPPER_HEX(*(filename+1)) || \
	    *(filename+2) != '/' || \
	    !IS_UPPER_HEX(*(filename+3)) || !IS_UPPER_HEX(*(filename+4)) || \
	    *(filename+5) != '/')
	{
		logError("file: "__FILE__", line: %d, " \
			"the format of filename \"%s\" is invalid", \
			__LINE__, filename);
		return EINVAL;
	}

	if (strchr(filename + 6, '/') != NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"the format of filename \"%s\" is invalid", \
			__LINE__, filename);
		return EINVAL;
	}

	return 0;
}

int fdfs_gen_slave_filename(const char *master_filename, \
		const char *prefix_name, const char *ext_name, \
		char *filename, int *filename_len)
{
	char true_ext_name[FDFS_FILE_EXT_NAME_MAX_LEN + 2];
	char *pDot;
	int master_file_len;

	master_file_len = strlen(master_filename);
	if (master_file_len < 28 + FDFS_FILE_EXT_NAME_MAX_LEN)
	{
		logError("file: "__FILE__", line: %d, " \
			"master filename \"%s\" is invalid", \
			__LINE__, master_filename);
		return EINVAL;
	}

	pDot = strchr(master_filename + (master_file_len - \
			(FDFS_FILE_EXT_NAME_MAX_LEN + 1)), '.');
	if (ext_name != NULL)
	{
		if (*ext_name == '\0')
		{
			*true_ext_name = '\0';
		}
		else if (*ext_name == '.')
		{
			snprintf(true_ext_name, sizeof(true_ext_name), \
				"%s", ext_name);
		}
		else
		{
			snprintf(true_ext_name, sizeof(true_ext_name), \
				".%s", ext_name);
		}
	}
	else
	{
		if (pDot == NULL)
		{
			*true_ext_name = '\0';
		}
		else
		{
			strcpy(true_ext_name, pDot);
		}
	}

	if (*true_ext_name == '\0' && strcmp(prefix_name, "-m") == 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"prefix_name \"%s\" is invalid", \
			__LINE__, prefix_name);
		return EINVAL;
	}

	/* when prefix_name is empty, the extension name of master file and 
	   slave file can not be same
	*/
	if ((*prefix_name == '\0') && ((pDot == NULL && *true_ext_name == '\0')
		 || (pDot != NULL && strcmp(pDot, true_ext_name) == 0)))
	{
		logError("file: "__FILE__", line: %d, " \
			"empty prefix_name is not allowed", __LINE__);
		return EINVAL;
	}

	if (pDot == NULL)
	{
		*filename_len = sprintf(filename, "%s%s%s", master_filename, \
			prefix_name, true_ext_name);
	}
	else
	{
		*filename_len = pDot - master_filename;
		memcpy(filename, master_filename, *filename_len);
		*filename_len += sprintf(filename + *filename_len, "%s%s", \
			prefix_name, true_ext_name);
	}

	return 0;
}

