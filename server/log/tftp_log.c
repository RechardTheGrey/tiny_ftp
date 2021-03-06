#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <sys/uio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <syslog.h>

#include "../util/sys_define.h"
#include "tftp_log.h"

void *log_thread_function(void *arg);

#define LOG_BUF_SIZE	4 * 1024

typedef struct log_node{
	int type;
	int len;
}log_item;

typedef struct {
	int fd;
	int total;
	int curr;
	char* buf;

	pthread_mutex_t buf_lock;
	pthread_cond_t  log_cond;
}log_queue;

log_queue log_que = {-1, 0, 0, NULL, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};

int init_log(const char* log_file)
{
	int ret;
	pthread_t tid;
	int fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 00600);
	if(fd < 0) {
		syslog(LOG_ERR, "open log file:%s failed", log_file);
		return T_FTP_FAIL;
	}
	
	syslog(LOG_DEBUG, "in init_log\n");	

	log_que.fd = fd;	
	log_que.buf = (char*)malloc(LOG_BUF_SIZE);
	if(log_que.buf == NULL) {
		syslog(LOG_ERR, "malloc log buf failed");
		return T_FTP_FAIL;
	}
	log_que.total = LOG_BUF_SIZE;
	log_que.curr = 0;

	if((ret = pthread_create(&tid, NULL, log_thread_function, NULL)) != 0) {
			syslog(LOG_ERR, "phtread_create:%s\n", strerror(ret));
			return T_FTP_FAIL;
	}

	return T_FTP_SUCCESS;
}

void* log_thread_function(void* arg)
{
	log_item* item;
	
	int len;
	int done;
	int total_size;
	time_t now;
	char* p = NULL;
	char* time_str;
	struct iovec iv[3];
	
	char end = '\n';

	char log_type_info[6][12] = {{"LOG_INFO\t"}, {"LOG_WARING\t"}, {"LOG_NOTICE\t"}, {"LOG_ERROR\t"}, {"LOG_DEBUG\t"}, {"LOG_CRIT\t"}};

	syslog(LOG_DEBUG, "log thread start\n");	

	assert(log_que.fd > 0 && log_que.buf);

	while(1) {
		pthread_mutex_lock(&log_que.buf_lock);
		while(log_que.curr == 0)
			pthread_cond_wait(&log_que.log_cond, &log_que.buf_lock);
		
		done = 0;
		while(done < log_que.curr) {
			p = log_que.buf + done;
			item = (log_item*)p;
		
			now = time(NULL);
			time_str = ctime(&now);
			len = strlen(time_str);
	
			// 去掉时间字符串最后的那个\n
			time_str[len-1] = '\0';
			strcat(time_str, "\t");

			iv[0].iov_len = len;
			iv[0].iov_base = (void*)time_str;	

			iv[1].iov_len = strlen(log_type_info[item->type]);
			iv[1].iov_base = (void*)log_type_info[item->type];	
			iv[2].iov_len = item->len;
			iv[2].iov_base = p + sizeof(log_item);
			
			if(iv[2].iov_len <= 0) {
				iv[2].iov_len = 1;
				iv[2].iov_base = &end;
			}

			total_size = iv[0].iov_len + iv[1].iov_len + iv[2].iov_len;
			while(total_size > 0) {
				if((len = writev(log_que.fd, iv, 3)) < 0) {
					if(errno == EINTR)
						continue;
					syslog(LOG_WARNING, "error occured:%s when write log", strerror(errno));
					pthread_mutex_unlock(&log_que.buf_lock);
					return NULL;
				}

				total_size -= len;
			}

			done += sizeof(log_item) + item->len;
		}
		
		log_que.curr = 0;
		pthread_mutex_unlock(&log_que.buf_lock);
	}
	
	return NULL;
}

void tftp_log(int log_mod, const char* str, ...)
{
	int len;
	char* p;
	va_list list;
		
	pthread_mutex_lock(&log_que.buf_lock);
	
	//if(log_que.curr)

	p  = log_que.buf + log_que.curr;
	*((int*)p) = log_mod;

	va_start(list, str);
    len = vsprintf(p + sizeof(log_item), str, list);
    va_end(list);
	
	*((int*)(p + sizeof(int))) = len;

	if(log_que.curr == 0)
		pthread_cond_signal(&log_que.log_cond);

	log_que.curr += len + sizeof(log_item);	
	pthread_mutex_unlock(&log_que.buf_lock);
}
