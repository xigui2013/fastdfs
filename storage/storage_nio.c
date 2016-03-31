/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include "shared_func.h"
#include "sched_thread.h"
#include "logger.h"
#include "sockopt.h"
#include "fast_task_queue.h"
#include "tracker_types.h"
#include "tracker_proto.h"
#include "storage_global.h"
#include "storage_service.h"
#include "ioevent_loop.h"
#include "storage_dio.h"
#include "storage_nio.h"

static void client_sock_read(int sock, short event, void *arg);
static void client_sock_write(int sock, short event, void *arg);
static int storage_nio_init(struct fast_task_info *pTask);

void add_to_deleted_list(struct fast_task_info *pTask)
{
	((StorageClientInfo *)pTask->arg)->canceled = true;
	pTask->next = pTask->thread_data->deleted_list;
	pTask->thread_data->deleted_list = pTask;
}

//���ڳ�ʱ�Ĵ���:ɾ���ļ��б�,�ͷ����񵽶�������
void task_finish_clean_up(struct fast_task_info *pTask)
{
	StorageClientInfo *pClientInfo;

	pClientInfo = (StorageClientInfo *)pTask->arg;
	if (pClientInfo->clean_func != NULL)
	{
		pClientInfo->clean_func(pTask);
	}

	ioevent_detach(&pTask->thread_data->ev_puller, pTask->event.fd);
	close(pTask->event.fd);
	pTask->event.fd = -1;

	if (pTask->event.timer.expires > 0)
	{
		fast_timer_remove(&pTask->thread_data->timer,
			&pTask->event.timer);
		pTask->event.timer.expires = 0;
	}

	memset(pTask->arg, 0, sizeof(StorageClientInfo));
	free_queue_push(pTask);

    __sync_fetch_and_sub(&g_storage_stat.connection.current_count, 1);
    ++g_stat_change_count;
}

static int set_recv_event(struct fast_task_info *pTask)
{
	int result;

	if (pTask->event.callback == client_sock_read)
	{
		return 0;
	}

	pTask->event.callback = client_sock_read;
	if (ioevent_modify(&pTask->thread_data->ev_puller,
		pTask->event.fd, IOEVENT_READ, pTask) != 0)
	{
		result = errno != 0 ? errno : ENOENT;
		add_to_deleted_list(pTask);

		logError("file: "__FILE__", line: %d, "\
			"ioevent_modify fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}
	return 0;
}

static int set_send_event(struct fast_task_info *pTask)
{
	int result;

	if (pTask->event.callback == client_sock_write)
	{
		return 0;
	}

	pTask->event.callback = client_sock_write;
	if (ioevent_modify(&pTask->thread_data->ev_puller,
		pTask->event.fd, IOEVENT_WRITE, pTask) != 0)
	{
		result = errno != 0 ? errno : ENOENT;
		add_to_deleted_list(pTask);

		logError("file: "__FILE__", line: %d, "\
			"ioevent_modify fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, STRERROR(result));
		return result;
	}
	return 0;
}

//���ݷ�����socket�¼��ص�,����˵���ϴ��ļ�ʱ,������һ����֮��,����storage_nio_notify(pTask)
//�����·�����ն�socket�Ĳ���,��pClientInfo->stage=FDFS_STORAGE_STAGE_NIO_RECV
//�������ֵ��û�з����ı�
void storage_recv_notify_read(int sock, short event, void *arg)
{
	struct fast_task_info *pTask;
	StorageClientInfo *pClientInfo;//ע����������ǲ�ͬ��,һ���Ǹ��ٷ���������,һ�������ݷ���������
	long task_addr;
	int64_t remain_bytes;
	int bytes;
	int result;

	while (1)
	{
		if ((bytes=read(sock, &task_addr, sizeof(task_addr))) < 0)
		{
			if (!(errno == EAGAIN || errno == EWOULDBLOCK))
			{
				logError("file: "__FILE__", line: %d, " \
					"call read failed, " \
					"errno: %d, error info: %s", \
					__LINE__, errno, STRERROR(errno));
			}

			break;
		}
		else if (bytes == 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"call read failed, end of file", __LINE__);
			break;
		}
		//socket����ʱʹ��pTask�����д��ݲ���:���մӹ����߳�socket����˵�д������,��ʼȥ��
		pTask = (struct fast_task_info *)task_addr;
		pClientInfo = (StorageClientInfo *)pTask->arg;

		if (pTask->event.fd < 0)  //quit flag
		{
			return;
		}

		/* //logInfo("=====thread index: %d, pTask->event.fd=%d", \
			pClientInfo->nio_thread_index, pTask->event.fd);
		*/
		//�մӴ����߳������������״̬��Ȼ��dio_thread,ȥ��dio_thread״̬
		if (pClientInfo->stage & FDFS_STORAGE_STAGE_DIO_THREAD)
		{
			pClientInfo->stage &= ~FDFS_STORAGE_STAGE_DIO_THREAD;
		}
		switch (pClientInfo->stage)
		{
			//��ʼ���׶Σ��������ݳ�ʼ��
			case FDFS_STORAGE_STAGE_NIO_INIT:
				//���ݷ����������socket���չ����������pClientInfo->stage=FDFS_STORAGE_STAGE_NIO_INIT

            	//��������������°󶨶�д�¼�

            	//ÿ����һ���ͻ���,�����ﶼ�ᴥ���������
				result = storage_nio_init(pTask);
				break;
			//��ʱ�Թ����ȿ�storage_nio_init
			case FDFS_STORAGE_STAGE_NIO_RECV:
				//�ڴν��ܰ���ʱpTask->offsetƫ����������
				pTask->offset = 0;
				//����ĳ���=�����ܳ���-������ƫ����
				//����ֽ��ܿͻ��˷ֿ鴫�����ֽ���
				remain_bytes = pClientInfo->total_length - \
					       pClientInfo->total_offset;
				//������ͼ�����µ��Լ�һ�ν�������

            	//pTask->length:���ݳ���,pTask->size:����Ļ����С
				if (remain_bytes > pTask->size)
				{
					pTask->length = pTask->size;
				}
				else
				{
					pTask->length = remain_bytes;
				}

				if (set_recv_event(pTask) == 0)
				{
					client_sock_read(pTask->event.fd,
						IOEVENT_READ, pTask);
				}
				result = 0;
				break;
			case FDFS_STORAGE_STAGE_NIO_SEND:
				result = storage_send_add_event(pTask);
				break;
			case FDFS_STORAGE_STAGE_NIO_CLOSE:
				result = EIO;   //close this socket
				break;
			default:
				logError("file: "__FILE__", line: %d, " \
					"invalid stage: %d", __LINE__, \
					pClientInfo->stage);
				result = EINVAL;
				break;
		}

		if (result != 0)
		{
			add_to_deleted_list(pTask);
		}
	}
}

//��ʼ��socket��д����
static int storage_nio_init(struct fast_task_info *pTask)
{
	StorageClientInfo *pClientInfo;
	struct storage_nio_thread_data *pThreadData;

	pClientInfo = (StorageClientInfo *)pTask->arg;
	//ȡ�����߳�:����pClientInfo->nio_thread_index ����

    //�Ѿ������˸�ֵ,�ڽ����ͻ���socket����ʱ

    //�����°�socket��д�¼�,�󶨵�Task����
	pThreadData = g_nio_thread_data + pClientInfo->nio_thread_index;

	//״̬����Ϊ��ʼ��������
	pClientInfo->stage = FDFS_STORAGE_STAGE_NIO_RECV;
	return ioevent_set(pTask, &pThreadData->thread_data,
			pTask->event.fd, IOEVENT_READ, client_sock_read,
			g_fdfs_network_timeout);
}

int storage_send_add_event(struct fast_task_info *pTask)
{
	//������������pTask->offsetΪ0
	pTask->offset = 0;

	/* direct send */
	client_sock_write(pTask->event.fd, IOEVENT_WRITE, pTask);

	return 0;
}

static void client_sock_read(int sock, short event, void *arg)
{
	int bytes;
	int recv_bytes;
	struct fast_task_info *pTask;
        StorageClientInfo *pClientInfo;
	//�õ�������Ϣ
	pTask = (struct fast_task_info *)arg;
        pClientInfo = (StorageClientInfo *)pTask->arg;
	if (pClientInfo->canceled)
	{
		return;
	}

	if (pClientInfo->stage != FDFS_STORAGE_STAGE_NIO_RECV)
	{
		if (event & IOEVENT_TIMEOUT) {
			pTask->event.timer.expires = g_current_time +
				g_fdfs_network_timeout;
			fast_timer_add(&pTask->thread_data->timer,
				&pTask->event.timer);
		}

		return;
	}
	//��ʱ�ˣ�ɾ�����task
	if (event & IOEVENT_TIMEOUT)
	{
		if (pClientInfo->total_offset == 0 && pTask->req_count > 0)
		{
			pTask->event.timer.expires = g_current_time +
				g_fdfs_network_timeout;
			fast_timer_add(&pTask->thread_data->timer,
				&pTask->event.timer);
		}
		else
		{
			logError("file: "__FILE__", line: %d, " \
				"client ip: %s, recv timeout, " \
				"recv offset: %d, expect length: %d", \
				__LINE__, pTask->client_ip, \
				pTask->offset, pTask->length);

			task_finish_clean_up(pTask);
		}

		return;
	}
	//io����һ��ɾ
	if (event & IOEVENT_ERROR)
	{
		logDebug("file: "__FILE__", line: %d, " \
			"client ip: %s, recv error event: %d, "
			"close connection", __LINE__, pTask->client_ip, event);

		task_finish_clean_up(pTask);
		return;
	}

	fast_timer_modify(&pTask->thread_data->timer,
		&pTask->event.timer, g_current_time +
		g_fdfs_network_timeout);
	//���뵽whileѭ��
	while (1)
	{
		//pClientInfo��total_length��Ϊ0��˵��ͷ��û���գ�����һ��ͷ
		//��ʼʱpClientInfo->total_length=0 pTask->offset=0
		if (pClientInfo->total_length == 0) //recv header
		{
			recv_bytes = sizeof(TrackerHeader) - pTask->offset;
		}
		else
		{
			//�ڴν����ϴ��ļ������ݰ�ʱ,��Ϊ����storage_nio_notify(pTask)

            //�������½��뵽void storage_recv_notify_read()������,��pTask->offset����������

            //��pTask->lengthҲ��������Ϊһ���Խ���ʣ����ֽ���(������ڷ����pTask->size,����������Ϊ���pTask->size)
			recv_bytes = pTask->length - pTask->offset;
		}

		/*
		logInfo("total_length=%"PRId64", recv_bytes=%d, "
			"pTask->length=%d, pTask->offset=%d",
			pClientInfo->total_length, recv_bytes, 
			pTask->length, pTask->offset);
		*/

		bytes = recv(sock, pTask->data + pTask->offset, recv_bytes, 0);
		if (bytes < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
			}
			else if (errno == EINTR)
			{
				continue;
			}
			else
			{
				logError("file: "__FILE__", line: %d, " \
					"client ip: %s, recv failed, " \
					"errno: %d, error info: %s", \
					__LINE__, pTask->client_ip, \
					errno, STRERROR(errno));

				task_finish_clean_up(pTask);
			}

			return;
		}
		else if (bytes == 0)
		{
			logDebug("file: "__FILE__", line: %d, " \
				"client ip: %s, recv failed, " \
				"connection disconnected.", \
				__LINE__, pTask->client_ip);

			task_finish_clean_up(pTask);
			return;
		}
		//�ð�ͷ���ݶ�pClientInfo���г�ʼ��
		if (pClientInfo->total_length == 0) //header
		{
			if (pTask->offset + bytes < sizeof(TrackerHeader))
			{
				pTask->offset += bytes;
				return;
			}
			//ȷ�������ܳ���:���������ļ�ʱ,���յİ�,��ֻ�а��ĳ���
			pClientInfo->total_length=buff2long(((TrackerHeader *) \
						pTask->data)->pkg_len);
			if (pClientInfo->total_length < 0)
			{
				logError("file: "__FILE__", line: %d, " \
					"client ip: %s, pkg length: " \
					"%"PRId64" < 0", \
					__LINE__, pTask->client_ip, \
					pClientInfo->total_length);

				task_finish_clean_up(pTask);
				return;
			}
			//�����ܳ���=��ͷ+����ĳ���

            //���뷢�͵ĳ���:��ͷ+����+����+...(�����ڰ�ͷ���溬�ж��������ܳ���)

            //��ΪĬ�ϵĽ��ջ���ֻ��K,���Ի�ִη���
			pClientInfo->total_length += sizeof(TrackerHeader);
			//�����Ҫ���ܵ������ܳ�����pTask�Ĺ̶����ȷ�ֵ����ô��ʱֻ������ô��
			if (pClientInfo->total_length > pTask->size)
			{
				pTask->length = pTask->size;
			}
			else
			{
				pTask->length = pClientInfo->total_length;
			}
		}

		pTask->offset += bytes;
		//�������˵�ǰ�İ�
		if (pTask->offset >= pTask->length) //recv current pkg done
		{
			//���req������ϣ�׼������rsp
			if (pClientInfo->total_offset + pTask->length >= \
					pClientInfo->total_length)
			{
				/* current req recv done */
				//��������Ϊ���Է��͵�״̬

                //�����ļ�������:

                //1.������ͻ��˷��������ļ����������,pClientInfo->stage = FDFS_STORAGE_STAGE_NIO_SEND����Ϊ

                //  ���͵�״̬;

                //2.���ݷ�������Ƭ��ȡ�ļ�,���͵��ͻ���,ÿ�ζ�ȡһƬ���,׼�����͵�ʱ��,�ʹ���storage_nio_notify(pTask)

                //  ��������,Ȼ���ڽ��뵽void storage_recv_notify_read()��������,����дsocket�¼�

                //3.��˴���֪ͨ����ȥ����void storage_recv_notify_read()����,�ϴ��ļ�����Ҳ�ǵ���void storage_recv_notify_read()����
				pClientInfo->stage = FDFS_STORAGE_STAGE_NIO_SEND;
				pTask->req_count++;
			}
			//�ս����˰�ͷ����ô��storage_deal_task�ַ�����
			//��ʼʱpClientInfo->total_offset==0
			if (pClientInfo->total_offset == 0)
			{
				pClientInfo->total_offset = pTask->length;
				storage_deal_task(pTask);//���ݷ��������д���
			}
			else
			{
				//�������д�ļ�
				//���ܵ������ݰ���ѹ������߳�
				pClientInfo->total_offset += pTask->length;

				/* continue write to file */
				storage_dio_queue_push(pTask);
			}

			return;
		}
	}

	return;
}

//socket�ͻ���д����
static void client_sock_write(int sock, short event, void *arg)
{
	int bytes;
	struct fast_task_info *pTask;
        StorageClientInfo *pClientInfo;

	pTask = (struct fast_task_info *)arg;
        pClientInfo = (StorageClientInfo *)pTask->arg;
	if (pClientInfo->canceled)
	{
		return;
	}

	if (event & IOEVENT_TIMEOUT)
	{
		logError("file: "__FILE__", line: %d, " \
			"send timeout", __LINE__);

		task_finish_clean_up(pTask);
		return;
	}

	if (event & IOEVENT_ERROR)
	{
		logDebug("file: "__FILE__", line: %d, " \
			"client ip: %s, recv error event: %d, "
			"close connection", __LINE__, pTask->client_ip, event);

		task_finish_clean_up(pTask);
		return;
	}

	while (1)
	{
		fast_timer_modify(&pTask->thread_data->timer,
			&pTask->event.timer, g_current_time +
			g_fdfs_network_timeout);
		bytes = send(sock, pTask->data + pTask->offset, \
				pTask->length - pTask->offset,  0);
		//printf("%08X sended %d bytes\n", (int)pTask, bytes);
		if (bytes < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				set_send_event(pTask);
			}
			else if (errno == EINTR)
			{
				continue;
			}
			else
			{
				logError("file: "__FILE__", line: %d, " \
					"client ip: %s, recv failed, " \
					"errno: %d, error info: %s", \
					__LINE__, pTask->client_ip, \
					errno, STRERROR(errno));

				task_finish_clean_up(pTask);
			}

			return;
		}
		else if (bytes == 0)
		{
			logWarning("file: "__FILE__", line: %d, " \
				"send failed, connection disconnected.", \
				__LINE__);

			task_finish_clean_up(pTask);
			return;
		}

		pTask->offset += bytes;
		//������Ѿ��������
		if (pTask->offset >= pTask->length)
		{
			if (set_recv_event(pTask) != 0)
			{
				return;
			}
			
			pClientInfo->total_offset += pTask->length;
			//�ͻ��˷��������ļ�����ʱ,pClientInfo->total_length���������ļ�����+���ϵĴ�С
			//����void storage_read_from_file()�����������pClientInfo->total_length = sizeof(TrackerHeader) + download_bytes; 
			if (pClientInfo->total_offset>=pClientInfo->total_length)
			{
				if (pClientInfo->total_length == sizeof(TrackerHeader)
					&& ((TrackerHeader *)pTask->data)->status == EINVAL)
				{
					logDebug("file: "__FILE__", line: %d, "\
						"close conn: #%d, client ip: %s", \
						__LINE__, pTask->event.fd,
						pTask->client_ip);
					task_finish_clean_up(pTask);
					return;
				}
				//������Ӧ,��������
				/*  reponse done, try to recv again */
				pClientInfo->total_length = 0;
				pClientInfo->total_offset = 0;
				pTask->offset = 0;
				pTask->length = 0;
				//Ȼ����������ΪFDFS_STORAGE_STAGE_NIO_RECV���յ�״̬,��Ϊ��ͻ��˽������ǳ�����
				pClientInfo->stage = FDFS_STORAGE_STAGE_NIO_RECV;
			}
			else  //continue to send file content
			{
				//����Ļ�������ݻ���,���������ļ�
				pTask->length = 0;

				/* continue read from file */
				storage_dio_queue_push(pTask);
			}

			return;
		}
	}
}

