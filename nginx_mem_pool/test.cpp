
//移植NGINX内存池，OOP实现
#include"ngx_mem_pool.h"
#include<iostream>
#include<string>
using namespace std;

//测试在小块内存和大块内存上占用外部资源并释放
typedef struct Data stData;
struct Data
{
    char* ptr;//占用堆内存
    FILE* pfile;//需要fclose
};
//要添加的资源释放函数
void func1(void* p)
{
    p = (char*)p;
    cout << "free ptr mem!" << endl;
    free(p);
}
void func2(void* pf)
{
    pf = (FILE*)pf;
    cout << "close file!" << endl;
    fclose((FILE*)pf);
}
int main()
{
    //ngx_create_pool(512)
    ngx_mem_pool mempool(512);


    void* p1 = mempool.ngx_palloc(128); //128<max 从内存池上分配小块内存
    if (nullptr==p1)
    {
        cout << "ngx_palloc fail..." << endl;
        return -1;
    }
    //512>max，需要开辟大块内存，在内存池的小块内存保存大块内存数据头
    stData* p2 = (stData*)mempool.ngx_palloc(512);
    if (nullptr==p2)
    {
        cout << "ngx_palloc fail..." << endl;
        return -1;
    }

    //占用外部资源
    p2->ptr = (char*)malloc(12);
    strcpy(p2->ptr, "hello world");
    p2->pfile = fopen("data.txt", "w");

    //向内存池添加清理操作，清理操作的数据头ngx_palloc分配内存
    ngx_pool_cleanup_s* c1 = mempool.ngx_pool_cleanup_add(sizeof(char*));
    c1->handler = func1;
    c1->data = p2->ptr;

    ngx_pool_cleanup_s* c2 = mempool.ngx_pool_cleanup_add(sizeof(FILE*));
    c2->handler = func2;
    c2->data = p2->pfile;

    //出作用域自动析构，ngx_destroy_pool
    // 1.调用所有的预置的清理函数 2.释放大块内存 3.释放内存池
    return 0;
}

