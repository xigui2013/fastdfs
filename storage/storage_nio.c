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

//对于超时的处理:删除文件列表,释放任务到队列里面
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

//数据服务器socket事件回调,比如说在上传文件时,接收了一部分之后,调用storage_nio_notify(pTask)
//又重新发起接收读socket的操作,而pClientInfo->stage=FDFS_STORAGE_STAGE_NIO_RECV
//的这个赋值并没有发生改变
void storage_recv_notify_read(int sock, short event, void *arg)
{
	struct fast_task_info *pTask;
	StorageClientInfo *pClientInfo;//注意这个参数是不同的,一个是跟踪服务器参数,一个是数据服务器参数
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
		//socket接收时使用pTask来进行传递参数:接收从工作线程socket服务端的写入内容,开始去读
		pTask = (struct fast_task_info *)task_addr;
		pClientInfo = (StorageClientInfo *)pTask->arg;

		if (pTask->event.fd < 0)  //quit flag
		{
			return;
		}

		/* //logInfo("=====thread index: %d, pTask->event.fd=%d", \
			pClientInfo->nio_thread_index, pTask->event.fd);
		*/
		//刚从磁盘线程里出来的任务状态依然是dio_thread,去掉dio_thread状态
		if (pClientInfo->stage & FDFS_STORAGE_STAGE_DIO_THREAD)
		{
			pClientInfo->stage &= ~FDFS_STORAGE_STAGE_DIO_THREAD;
		}
		switch (pClientInfo->stage)
		{
			//初始化阶段，进行数据初始化
			case FDFS_STORAGE_STAGE_NIO_INIT:
				//数据服务器服务端socket接收过来的任务的pClientInfo->stage=FDFS_STORAGE_STAGE_NIO_INIT

            	//因此在这里在重新绑定读写事件

            	//每连接一个客户端,在这里都会触发这个动作
				result = storage_nio_init(pTask);
				break;
			//暂时略过，先看storage_nio_init
			case FDFS_STORAGE_STAGE_NIO_RECV:
				//在次接受包体时pTask->offset偏移量被重置
				pTask->offset = 0;
				//任务的长度=包的总长度-包的总偏移量
				//会出现接受客户端分块传来的字节流
				remain_bytes = pClientInfo->total_length - \
					       pClientInfo->total_offset;
				//总是试图将余下的自己一次接收收完

            	//pTask->length:数据长度,pTask->size:分配的缓冲大小
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

//初始化socket读写操作
static int storage_nio_init(struct fast_task_info *pTask)
{
	StorageClientInfo *pClientInfo;
	struct storage_nio_thread_data *pThreadData;

	pClientInfo = (StorageClientInfo *)pTask->arg;
	//取工作线程:依据pClientInfo->nio_thread_index 参数

    //已经进行了赋值,在建立客户端socket连接时

    //在重新绑定socket读写事件,绑定到Task上面
	pThreadData = g_nio_thread_data + pClientInfo->nio_thread_index;

	//状态设置为开始接收请求
	pClientInfo->stage = FDFS_STORAGE_STAGE_NIO_RECV;
	return ioevent_set(pTask, &pThreadData->thread_data,
			pTask->event.fd, IOEVENT_READ, client_sock_read,
			g_fdfs_network_timeout);
}

int storage_send_add_event(struct fast_task_info *pTask)
{
	//发送是先重置pTask->offset为0
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
	//得到任务信息
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
	//超时了，删除这个task
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
	//io错误，一样删
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
	//进入到while循环
	while (1)
	{
		//pClientInfo的total_length域为0，说明头还没接收，接收一个头
		//初始时pClientInfo->total_length=0 pTask->offset=0
		if (pClientInfo->total_length == 0) //recv header
		{
			recv_bytes = sizeof(TrackerHeader) - pTask->offset;
		}
		else
		{
			//在次接受上传文件的数据包时,因为发生storage_nio_notify(pTask)

            //所以重新进入到void storage_recv_notify_read()函数中,而pTask->offset被重新设置

            //而pTask->length也被重置设为一次性接收剩余的字节数(如果大于分配的pTask->size,又重新设置为这个pTask->size)
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
		//用包头数据对pClientInfo进行初始化
		if (pClientInfo->total_length == 0) //header
		{
			if (pTask->offset + bytes < sizeof(TrackerHeader))
			{
				pTask->offset += bytes;
				return;
			}
			//确定包的总长度:比如下载文件时,接收的包,就只有包的长度
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
			//包的总长度=包头+包体的长度

            //设想发送的场景:包头+包体+包体+...(其中在包头里面含有多个包体的总长度)

            //因为默认的接收缓冲只有K,所以会分次发送
			pClientInfo->total_length += sizeof(TrackerHeader);
			//如果需要接受的数据总长大于pTask的固定长度阀值，那么暂时只接受那么长
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
		//接受完了当前的包
		if (pTask->offset >= pTask->length) //recv current pkg done
		{
			//这个req接受完毕，准备反馈rsp
			if (pClientInfo->total_offset + pTask->length >= \
					pClientInfo->total_length)
			{
				/* current req recv done */
				//重新设置为可以发送的状态

                //下载文件的流程:

                //1.接收完客户端发起下载文件的请求包后,pClientInfo->stage = FDFS_STORAGE_STAGE_NIO_SEND设置为

                //  发送的状态;

                //2.数据服务器分片读取文件,发送到客户端,每次读取一片完成,准备发送的时候,就触发storage_nio_notify(pTask)

                //  函数调用,然后在进入到void storage_recv_notify_read()函数里面,触发写socket事件

                //3.因此触发通知函数去调用void storage_recv_notify_read()函数,上传文件触发也是调用void storage_recv_notify_read()函数
				pClientInfo->stage = FDFS_STORAGE_STAGE_NIO_SEND;
				pTask->req_count++;
			}
			//刚接受了包头，那么由storage_deal_task分发任务
			//初始时pClientInfo->total_offset==0
			if (pClientInfo->total_offset == 0)
			{
				pClientInfo->total_offset = pTask->length;
				storage_deal_task(pTask);//数据服务器进行处理
			}
			else
			{
				//否则继续写文件
				//接受的是数据包，压入磁盘线程
				pClientInfo->total_offset += pTask->length;

				/* continue write to file */
				storage_dio_queue_push(pTask);
			}

			return;
		}
	}

	return;
}

//socket客户端写操作
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
		//如果包已经发送完毕
		if (pTask->offset >= pTask->length)
		{
			if (set_recv_event(pTask) != 0)
			{
				return;
			}
			
			pClientInfo->total_offset += pTask->length;
			//客户端发起下载文件命令时,pClientInfo->total_length就是整个文件长度+包上的大小
			//可在void storage_read_from_file()函数里面见到pClientInfo->total_length = sizeof(TrackerHeader) + download_bytes; 
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
				//发送响应,继续接收
				/*  reponse done, try to recv again */
				pClientInfo->total_length = 0;
				pClientInfo->total_offset = 0;
				pTask->offset = 0;
				pTask->length = 0;
				//然后重新设置为FDFS_STORAGE_STAGE_NIO_RECV接收的状态,因为与客户端建立的是长连接
				pClientInfo->stage = FDFS_STORAGE_STAGE_NIO_RECV;
			}
			else  //continue to send file content
			{
				//否则的话清空数据缓冲,继续发送文件
				pTask->length = 0;

				/* continue read from file */
				storage_dio_queue_push(pTask);
			}

			return;
		}
	}
}

