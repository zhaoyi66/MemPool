
//��ֲNGINX�ڴ�أ�OOPʵ��
#include"ngx_mem_pool.h"
#include<iostream>
#include<string>
using namespace std;

//������С���ڴ�ʹ���ڴ���ռ���ⲿ��Դ���ͷ�
typedef struct Data stData;
struct Data
{
    char* ptr;//ռ�ö��ڴ�
    FILE* pfile;//��Ҫfclose
};
//Ҫ��ӵ���Դ�ͷź���
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


    void* p1 = mempool.ngx_palloc(128); //128<max ���ڴ���Ϸ���С���ڴ�
    if (nullptr==p1)
    {
        cout << "ngx_palloc fail..." << endl;
        return -1;
    }
    //512>max����Ҫ���ٴ���ڴ棬���ڴ�ص�С���ڴ汣�����ڴ�����ͷ
    stData* p2 = (stData*)mempool.ngx_palloc(512);
    if (nullptr==p2)
    {
        cout << "ngx_palloc fail..." << endl;
        return -1;
    }

    //ռ���ⲿ��Դ
    p2->ptr = (char*)malloc(12);
    strcpy(p2->ptr, "hello world");
    p2->pfile = fopen("data.txt", "w");

    //���ڴ���������������������������ͷngx_palloc�����ڴ�
    ngx_pool_cleanup_s* c1 = mempool.ngx_pool_cleanup_add(sizeof(char*));
    c1->handler = func1;
    c1->data = p2->ptr;

    ngx_pool_cleanup_s* c2 = mempool.ngx_pool_cleanup_add(sizeof(FILE*));
    c2->handler = func2;
    c2->data = p2->pfile;

    //���������Զ�������ngx_destroy_pool
    // 1.�������е�Ԥ�õ������� 2.�ͷŴ���ڴ� 3.�ͷ��ڴ��
    return 0;
}

