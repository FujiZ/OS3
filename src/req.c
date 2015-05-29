#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include "vmm.h"
/* �����ô����� */
void do_request(Ptr_MemoryAccessRequest ptr_memAccReq){
	char reqType;
	printf("������ô�����ĵ�ַ ���̺�\n");
	scanf("%lu%u", &ptr_memAccReq->virAddr, &ptr_memAccReq->proccessNum);
	while (getchar() != '\n');
	printf("������ô���������ͣ�r/w/e)\n");
	reqType = getchar();
	while (getchar() != '\n');
	switch (reqType){
	case 'r'://������
	{
		ptr_memAccReq->reqType = REQUEST_READ;
		printf("��������\n��ַ��%lu\t����: %u\t���ͣ���ȡ\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum);
		break;
	}
	case 'w': //д����
	{
		ptr_memAccReq->reqType = REQUEST_WRITE;
		/* �����д���ֵ */
		printf("������Ҫ�޸ĵ�ֵ\n");
		scanf("%hhu", &ptr_memAccReq->value);
		while (getchar() != '\n');
		ptr_memAccReq->value = random() % 0xFFu;
		printf("��������\n��ַ��%lu\t����: %u\t���ͣ�д��\tֵ��%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum, ptr_memAccReq->value);
		break;
	}
	case 'e':
	{
		ptr_memAccReq->reqType = REQUEST_EXECUTE;
		printf("��������\n��ַ��%lu\t����: %u\t���ͣ�ִ��\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum);
		break;
	}
	default:
		break;
	}
}

void do_request_r(Ptr_MemoryAccessRequest ptr_memAccReq)
{

	/* �����ַ */
	ptr_memAccReq->virAddr = random() % VIRTUAL_MEMORY_SIZE;
	/* ������� */
	ptr_memAccReq->proccessNum = random() % PROCESS_SUM;
	/* ��������������� */
	switch (random() % 3)
	{
		case 0: //������
		{
			ptr_memAccReq->reqType = REQUEST_READ;
			printf("��������\n��ַ��%lu\t����: %u\t���ͣ���ȡ\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum);
			break;
		}
		case 1: //д����
		{
			ptr_memAccReq->reqType = REQUEST_WRITE;
			/* ���������д���ֵ */
			ptr_memAccReq->value = random() % 0xFFu;
			printf("��������\n��ַ��%lu\t����: %u\t���ͣ�д��\tֵ��%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum,ptr_memAccReq->value);
			break;
		}
		case 2:
		{
			ptr_memAccReq->reqType = REQUEST_EXECUTE;
			printf("��������\n��ַ��%lu\t����: %u\t���ͣ�ִ��\n", ptr_memAccReq->virAddr, ptr_memAccReq->proccessNum);
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
