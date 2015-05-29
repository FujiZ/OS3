#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include "vmm.h"
/* 产生访存请求 */
void do_request(Ptr_MemoryAccessRequest ptr_memAccReq){
	char reqType;
	printf("请输入访存请求的地址 进程号\n");
	scanf("%lu%u", &ptr_memAccReq->virAddr, &ptr_memAccReq->proccessNum);
	while (getchar() != '\n');
	printf("请输入访存请求的类型（r/w/e)\n");
	reqType = getchar();
	while (getchar() != '\n');
	switch (reqType){
	case 'r'://读请求
	{
		ptr_memAccReq->reqType = REQUEST_READ;
		printf("产生请求：\n地址：%lu\t进程: %u\t类型：读取\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum);
		break;
	}
	case 'w': //写请求
	{
		ptr_memAccReq->reqType = REQUEST_WRITE;
		/* 读入待写入的值 */
		printf("请输入要修改的值\n");
		scanf("%hhu", &ptr_memAccReq->value);
		while (getchar() != '\n');
		ptr_memAccReq->value = random() % 0xFFu;
		printf("产生请求：\n地址：%lu\t进程: %u\t类型：写入\t值：%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum, ptr_memAccReq->value);
		break;
	}
	case 'e':
	{
		ptr_memAccReq->reqType = REQUEST_EXECUTE;
		printf("产生请求：\n地址：%lu\t进程: %u\t类型：执行\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum);
		break;
	}
	default:
		break;
	}
}

void do_request_r(Ptr_MemoryAccessRequest ptr_memAccReq)
{

	/* 请求地址 */
	ptr_memAccReq->virAddr = random() % VIRTUAL_MEMORY_SIZE;
	/* 请求进程 */
	ptr_memAccReq->proccessNum = random() % PROCESS_SUM;
	/* 随机产生请求类型 */
	switch (random() % 3)
	{
		case 0: //读请求
		{
			ptr_memAccReq->reqType = REQUEST_READ;
			printf("产生请求：\n地址：%lu\t进程: %u\t类型：读取\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum);
			break;
		}
		case 1: //写请求
		{
			ptr_memAccReq->reqType = REQUEST_WRITE;
			/* 随机产生待写入的值 */
			ptr_memAccReq->value = random() % 0xFFu;
			printf("产生请求：\n地址：%lu\t进程: %u\t类型：写入\t值：%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum,ptr_memAccReq->value);
			break;
		}
		case 2:
		{
			ptr_memAccReq->reqType = REQUEST_EXECUTE;
			printf("产生请求：\n地址：%lu\t进程: %u\t类型：执行\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum);
			break;
		}
		default:
			break;
	}
}

int main(int argc, char* argv[]){
	int fd;
	Ptr_MemoryAccessRequest ptr_memAccReq = (Ptr_MemoryAccessRequest)malloc(sizeof(MemoryAccessRequest));
	do_request(ptr_memAccReq);
	if ((fd = open("/tmp/server", O_WRONLY))<0){
		printf("req open fifo failed");
		exit(-1);
	}
	if (write(fd, ptr_memAccReq, DATALEN)<0){
		printf("req write failed");
		exit(-1);
	}
	close(fd);
	return 0;
}
