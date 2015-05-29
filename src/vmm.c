#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include "vmm.h"

#define DEBUG
/* 页表 */
PageTableItem pageTable[OUTER_PAGE_SUM][INNER_PAGE_SUM];
/* 实存空间 */
BYTE actMem[ACTUAL_MEMORY_SIZE];
/* 用文件模拟辅存空间 */
FILE *ptr_auxMem;
/* 物理块使用标识 */
BOOL blockStatus[BLOCK_SUM];
/* 访存请求 */
Ptr_MemoryAccessRequest ptr_memAccReq;

int fifo;//fifo文件
int count;//表示当前周期数

void init_file(){
	int i;

	if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "w"))){
		do_error(ERROR_FILE_OPEN_FAILED);
		exit(1);
	}
	//随机生成256位字符串
	for(i=0; i<VIRTUAL_MEMORY_SIZE; i++){
		fputc((unsigned char)random() % 256,ptr_auxMem);
	}

	fclose(ptr_auxMem);
	printf("系统提示：初始化辅存模拟文件完成\n");
	
}

/* 初始化环境 */
void do_init()
{
	int i, j, blockCount;

	srandom(time(NULL));
	
	for (i = 0, blockCount=0; i < OUTER_PAGE_SUM; ++i){
		for (j = 0; j < INNER_PAGE_SUM; ++j){
			pageTable[i][j].pageIndex = i;
			pageTable[i][j].pageNum = j;
			pageTable[i][j].filled = FALSE;
			pageTable[i][j].edited = FALSE;
			pageTable[i][j].count = 0;
			pageTable[i][j].r = FALSE;//初始化每个页的访问位
			pageTable[i][j].shiftReg = 0;//初始化每个页的移位寄存器
			pageTable[i][j].proccessNum = random() % PROCESS_SUM;//设置该页所属的进程
			/* 使用随机数设置该页的保护类型 */
			switch (random() % 7)
			{
			case 0:
			{
				pageTable[i][j].proType = READABLE;
				break;
			}
			case 1:
			{
				pageTable[i][j].proType = WRITABLE;
				break;
			}
			case 2:
			{
				pageTable[i][j].proType = EXECUTABLE;
				break;
			}
			case 3:
			{
				pageTable[i][j].proType = READABLE | WRITABLE;
				break;
			}
			case 4:
			{
				pageTable[i][j].proType = READABLE | EXECUTABLE;
				break;
			}
			case 5:
			{
				pageTable[i][j].proType = WRITABLE | EXECUTABLE;
				break;
			}
			case 6:
			{
				pageTable[i][j].proType = READABLE | WRITABLE | EXECUTABLE;
				break;
			}
			default:
				break;
			}
			/* 设置该页对应的辅存地址 */
			pageTable[i][j].auxAddr = (blockCount++) * PAGE_SIZE;
		}
	}

	for (i = 0, blockCount=0; i < 2; ++i){
		for (j = 0; j < INNER_PAGE_SUM; ++j){
			/* 随机选择一些物理块进行页面装入 */
			//if(1)//将所有块填满，用来测试页表调度
			if (random() % 2 == 0)
			{
				do_page_in(&pageTable[i][j], blockCount);
				pageTable[i][j].blockNum = blockCount;
				pageTable[i][j].filled = TRUE;
				blockStatus[blockCount] = TRUE;
			}
			else
				blockStatus[j] = FALSE;
			++blockCount;
		}
	}

}
//初始化FIFO
void init_fifo(){
	struct stat statbuf;
	if (stat("/tmp/server", &statbuf) == 0){
		/* 如果FIFO文件存在,删掉 */
		if (remove("/tmp/server")<0){
			printf("remove failed");
			exit(-1);
		}
	}

	if (mkfifo("/tmp/server", 0666)<0){
		printf("mkfifo failed");
		exit(-1);
	}
	/* 在阻塞模式下打开FIFO */
	if ((fifo = open("/tmp/server", O_RDONLY))<0){
		printf("open fifo failed");
		exit(-1);
	}
}

/* 响应请求 */
void do_response()
{
	Ptr_PageTableItem ptr_pageTabIt;
	unsigned int pageIndex, pageNum, offAddr;
	unsigned int actAddr;
	/* 检查地址是否越界 */
	if (ptr_memAccReq->virAddr < 0 || ptr_memAccReq->virAddr >= VIRTUAL_MEMORY_SIZE)
	{
		do_error(ERROR_OVER_BOUNDARY);
		return;
	}
	
	/* 计算页号和页内偏移值 */
	pageIndex = ptr_memAccReq->virAddr / OUTER_PAGE_SIZE;
	pageNum = ptr_memAccReq->virAddr / PAGE_SIZE - pageIndex * INNER_PAGE_SUM;
	offAddr = ptr_memAccReq->virAddr % PAGE_SIZE;
	printf("页目录号为：%u\t页号为：%u\t页内偏移为：%u\n", pageIndex, pageNum, offAddr);

	/* 获取对应页表项 */
	ptr_pageTabIt = &pageTable[pageIndex][pageNum];
	//检查是否为对应进程
	if (ptr_pageTabIt->proccessNum != ptr_memAccReq->proccessNum){
		do_error(ERROE_PROCESS_DENY);
		return;
	}

	/* 根据特征位决定是否产生缺页中断 */
	if (!ptr_pageTabIt->filled)
	{
		do_page_fault(ptr_pageTabIt);
	}
	
	actAddr = ptr_pageTabIt->blockNum * PAGE_SIZE + offAddr;
	printf("实地址为：%u\n", actAddr);
	/* 检查页面访问权限并处理访存请求 */
	switch (ptr_memAccReq->reqType)
	{
		case REQUEST_READ: //读请求
		{
			ptr_pageTabIt->count++;
			ptr_pageTabIt->r = TRUE;

			if (!(ptr_pageTabIt->proType & READABLE)) //页面不可读
			{
				do_error(ERROR_READ_DENY);
				return;
			}
			/* 读取实存中的内容 */
			printf("读操作成功：值为%02X\n", actMem[actAddr]);
			break;
		}
		case REQUEST_WRITE: //写请求
		{
			ptr_pageTabIt->count++;
			ptr_pageTabIt->r = TRUE;
			if (!(ptr_pageTabIt->proType & WRITABLE)) //页面不可写
			{
				do_error(ERROR_WRITE_DENY);	
				return;
			}
			/* 向实存中写入请求的内容 */
			actMem[actAddr] = ptr_memAccReq->value;
			ptr_pageTabIt->edited = TRUE;			
			printf("写操作成功\n");
			break;
		}
		case REQUEST_EXECUTE: //执行请求
		{
			ptr_pageTabIt->count++;
			ptr_pageTabIt->r = TRUE;
			if (!(ptr_pageTabIt->proType & EXECUTABLE)) //页面不可执行
			{
				do_error(ERROR_EXECUTE_DENY);
				return;
			}			
			printf("执行成功\n");
			break;
		}
		default: //非法请求类型
		{	
			do_error(ERROR_INVALID_REQUEST);
			return;
		}
	}

}

/* 处理缺页中断 */
void do_page_fault(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i;
	printf("产生缺页中断，开始进行调页...\n");
	for (i = 0; i < BLOCK_SUM; i++)
	{
		if (!blockStatus[i])
		{
			/* 读辅存内容，写入到实存 */
			do_page_in(ptr_pageTabIt, i);
			
			/* 更新页表内容 */
			ptr_pageTabIt->blockNum = i;
			ptr_pageTabIt->filled = TRUE;
			ptr_pageTabIt->edited = FALSE;
			ptr_pageTabIt->count = 0;
			ptr_pageTabIt->shiftReg = 0;
			ptr_pageTabIt->r = FALSE;
			blockStatus[i] = TRUE;
			return;
		}
	}
	/* 没有空闲物理块，进行页面替换 */
	//do_LFU(ptr_pageTabIt);
	do_LRU(ptr_pageTabIt);
}

/* 根据LFU算法进行页面替换 */
void do_LFU(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i, j, min, index, page;
	printf("没有空闲物理块，开始进行LFU页面替换...\n");
	for (i = 0, min = 0xFFFFFFFF, index = 0, page = 0; i < OUTER_PAGE_SUM; i++){
		for (j = 0; j < INNER_PAGE_SUM; ++j){
			if (pageTable[i][j].count < min){
				min = pageTable[i][j].count;
				index = i;
				page = j;
			}
		}
	}
	printf("选择第%u_%u页进行替换\n", index, page);
	if (pageTable[index][page].edited)
	{
		/* 页面内容有修改，需要写回至辅存 */
		printf("该页内容有修改，写回至辅存\n");
		do_page_out(&pageTable[index][page]);
	}
	pageTable[index][page].filled = FALSE;
	pageTable[index][page].count = 0;

	/* 读辅存内容，写入到实存 */
	do_page_in(ptr_pageTabIt, pageTable[index][page].blockNum);
	
	/* 更新页表内容 */
	ptr_pageTabIt->blockNum = pageTable[index][page].blockNum;
	ptr_pageTabIt->filled = TRUE;
	ptr_pageTabIt->edited = FALSE;
	ptr_pageTabIt->count = 0;
	printf("页面替换成功\n");
}

/* 根据页面老化算法进行页面替换 */
void do_LRU(Ptr_PageTableItem ptr_pageTabIt){
	unsigned int i, j, index, page;
	unsigned char min;
	printf("没有空闲物理块，开始进行页面老化页面替换...\n");
	for (i = 0, min = 0xFF, index = 0, page = 0; i < OUTER_PAGE_SUM; i++){
		for (j = 0; j < INNER_PAGE_SUM; ++j){
			if (pageTable[i][j].filled==TRUE&&pageTable[i][j].shiftReg < min){
				min = pageTable[i][j].shiftReg;
				index = i;
				page = j;
			}
		}
	}
	printf("选择第%u_%u页进行替换\n", index, page);
	if (pageTable[index][page].edited)
	{
		/* 页面内容有修改，需要写回至辅存 */
		printf("该页内容有修改，写回至辅存\n");
		do_page_out(&pageTable[index][page]);
	}
	pageTable[index][page].filled = FALSE;
	pageTable[index][page].count = 0;
	pageTable[index][page].shiftReg = 0;
	pageTable[index][page].r = FALSE;
	/* 读辅存内容，写入到实存 */
	do_page_in(ptr_pageTabIt, pageTable[index][page].blockNum);

	/* 更新页表内容 */
	ptr_pageTabIt->blockNum = pageTable[index][page].blockNum;
	ptr_pageTabIt->filled = TRUE;
	ptr_pageTabIt->edited = FALSE;
	ptr_pageTabIt->count = 0;
	ptr_pageTabIt->shiftReg = 0;
	ptr_pageTabIt->r = FALSE;
	printf("页面替换成功\n");
}

/* 将辅存内容写入实存 */
void do_page_in(Ptr_PageTableItem ptr_pageTabIt, unsigned int blockNum)
{
	unsigned int readNum;
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%lu\tftell=%lu\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if ((readNum = fread(actMem + blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%lu\tftell=%lu\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: blockNum=%u\treadNum=%u\n", blockNum, readNum);
		printf("DEBUG: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_READ_FAILED);
		exit(1);
	}
	printf("调页成功：辅存地址%lu-->>物理块%u\n", ptr_pageTabIt->auxAddr, blockNum);
}

/* 将被替换页面的内容写回辅存 */
void do_page_out(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int writeNum;
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%ld\n", (unsigned int)ptr_pageTabIt, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if ((writeNum = fwrite(actMem + ptr_pageTabIt->blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%u\tftell=%ld\n", (unsigned int)ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: writeNum=%u\n", writeNum);
		printf("DEBUG: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_WRITE_FAILED);
		exit(1);
	}
	printf("写回成功：物理块%lu-->>辅存地址%03X\n", ptr_pageTabIt->auxAddr, ptr_pageTabIt->blockNum);
}

/* 错误处理 */
void do_error(ERROR_CODE code)
{
	switch (code)
	{
		case ERROE_PROCESS_DENY:{
			printf("访存失败：该页不属于该进程\n");
			break;
		}
		case ERROR_READ_DENY:
		{
			printf("访存失败：该地址内容不可读\n");
			break;
		}
		case ERROR_WRITE_DENY:
		{
			printf("访存失败：该地址内容不可写\n");
			break;
		}
		case ERROR_EXECUTE_DENY:
		{
			printf("访存失败：该地址内容不可执行\n");
			break;
		}		
		case ERROR_INVALID_REQUEST:
		{
			printf("访存失败：非法访存请求\n");
			break;
		}
		case ERROR_OVER_BOUNDARY:
		{
			printf("访存失败：地址越界\n");
			break;
		}
		case ERROR_FILE_OPEN_FAILED:
		{
			printf("系统错误：打开文件失败\n");
			break;
		}
		case ERROR_FILE_CLOSE_FAILED:
		{
			printf("系统错误：关闭文件失败\n");
			break;
		}
		case ERROR_FILE_SEEK_FAILED:
		{
			printf("系统错误：文件指针定位失败\n");
			break;
		}
		case ERROR_FILE_READ_FAILED:
		{
			printf("系统错误：读取文件失败\n");
			break;
		}
		case ERROR_FILE_WRITE_FAILED:
		{
			printf("系统错误：写入文件失败\n");
			break;
		}
		default:
		{
			printf("未知错误：没有这个错误代码\n");
		}
	}
}

/* 打印页表 */
void do_print_info()
{
	unsigned int i, j, k;
	char str[4];
	printf("目录\t页号\t块号\t进程\t装入\t修改\t保护\t计数\t辅存\tSftReg\n");

	for (i = 0; i < OUTER_PAGE_SUM; ++i){
		for (j = 0; j < INNER_PAGE_SUM; ++j){
			printf("%u\t%u\t%u\t%u\t%u\t%u\t%s\t%lu\t%lu\t%02X\n", i, j, pageTable[i][j].blockNum, pageTable[i][j].proccessNum, pageTable[i][j].filled,
				pageTable[i][j].edited, get_proType_str(str, pageTable[i][j].proType),
				pageTable[i][j].count, pageTable[i][j].auxAddr, pageTable[i][j].shiftReg);
		}
	}

}

/* 打印实存 */
void do_print_real(){
	int i, j;
	printf("块号\t使用\t内容\n");
	for (i = 0; i < BLOCK_SUM; ++i){
		printf("%d\t%d\t", i, (int)blockStatus[i]);
		for (j = 0; j < PAGE_SIZE; ++j){
			printf("%02X", actMem[i*PAGE_SIZE + j]);
		}
		printf("\n");
	}
}
/* 打印辅存 */
void do_print_virtual(){
	int i, j;
	FILE* fp = NULL;
	fp = fopen(AUXILIARY_MEMORY, "r");
	printf("块号\t内容\n");
	for (i = 0; i < PAGE_SUM; ++i){
		printf("%d\t", i);
		for (j = 0; j < PAGE_SIZE; ++j){
			printf("%02X",fgetc(fp));
		}
		printf("\n");
	}
	fclose(fp);
}
/* 获取页面保护类型字符串 */
char *get_proType_str(char *str, BYTE type)
{
	if (type & READABLE)
		str[0] = 'r';
	else
		str[0] = '-';
	if (type & WRITABLE)
		str[1] = 'w';
	else
		str[1] = '-';
	if (type & EXECUTABLE)
		str[2] = 'x';
	else
		str[2] = '-';
	str[3] = '\0';
	return str;
}

void do_update(){
	int i, j;
	if ((++count) >= CYCLE){//到达更新时间片的时间
		for (i = 0; i < OUTER_PAGE_SUM; ++i){
			for (j = 0; j < INNER_PAGE_SUM; ++j){
				//将shiftReg右移1位
				pageTable[i][j].shiftReg = pageTable[i][j].shiftReg >> 1;
				//判断读取位
				if (pageTable[i][j].r){
					pageTable[i][j].shiftReg = pageTable[i][j].shiftReg | 0x80;
					pageTable[i][j].r = 0;
				}
			}
		}
		count = 0;
	}
}

int main(int argc, char* argv[])
{
	char c;
	int i;
	init_file();
	if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "r+")))
	{
		do_error(ERROR_FILE_OPEN_FAILED);
		exit(1);
	}
	
	do_init();
	do_print_info();
	init_fifo();
	ptr_memAccReq = (Ptr_MemoryAccessRequest) malloc(sizeof(MemoryAccessRequest));
	/* 在循环中模拟访存请求与处理过程 */
	while (TRUE)
	{
		//从FIFO中读取命令
		//memset(ptr_memAccReq, 0, DATALEN);
		if (read(fifo, ptr_memAccReq, DATALEN)==DATALEN){
			printf("收到请求\n");
			do_response();
			do_update();
			printf("按A打印实存内容，按其他键不打印...\n");
			if ((c = getchar()) == 'a' || c == 'A')
				do_print_real();
			while (c != '\n')
				c = getchar();
			printf("按B打印虚存内容，按其他键不打印...\n");
			if ((c = getchar()) == 'b' || c == 'B')
				do_print_virtual();
			while (c != '\n')
				c = getchar();
			printf("按Y打印页表，按其他键不打印...\n");
			if ((c = getchar()) == 'y' || c == 'Y')
				do_print_info();
			while (c != '\n')
				c = getchar();
			printf("按X退出程序，按其他键继续...\n");
			if ((c = getchar()) == 'x' || c == 'X')
				break;
			while (c != '\n')
				c = getchar();
		}
	}

	if (fclose(ptr_auxMem) == EOF)
	{
		do_error(ERROR_FILE_CLOSE_FAILED);
		exit(1);
	}

	close(fifo);
	return (0);
}
