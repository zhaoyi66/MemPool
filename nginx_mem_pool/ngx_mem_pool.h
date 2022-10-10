//��ֲNGINX�ڴ�أ�OOPʵ��
#pragma once
#include<iostream>
#include<memory>

//�����ض���
using u_char = unsigned char;
using ngx_uint_t = unsigned int;
using ngx_pool_cleanup_pt = void(*)(void*);//����һ������ָ������

struct ngx_pool_s;//����ǰ������

//���������ص�������������ͷ
struct ngx_pool_cleanup_s
{
	ngx_pool_cleanup_pt handler;//����ָ��
	void* data;//���ݸ��ص������Ĳ���
	ngx_pool_cleanup_s* next;//ָ����һ��
};

//����ڴ������ͷ
struct ngx_pool_large_s
{
	ngx_pool_large_s* next;//ָ����һ��
	void* alloc;//��������ȥ�Ĵ���ڴ����ʼ��ַ
};

//С���ڴ������ͷ
struct ngx_pool_data_t
{
	u_char* last;//С���ڴ�����ڴ����ʼ��ַ
	u_char* end;//С���ڴ�����ڴ��ĩβ��ַ
	ngx_pool_s* next;//ָ����һ��
	ngx_uint_t failed;//��¼�˵�ǰС���ڴ���ڴ�ط����ڴ�ʧ�ܵĴ���
};

//�ڴ�ص�����ͷ
struct ngx_pool_s
{
	ngx_pool_data_t d;//С���ڴ�����ͷ
	size_t max;//С���ڴ�ʹ���ڴ�ķֽ���
	ngx_pool_s* current;//ָ���ṩС���ڴ������ڴ��(����)
	ngx_pool_large_s* large;//ָ�����ڴ������ͷ(����)
	ngx_pool_cleanup_s* cleanup;//ָ������Ԥ�õ���������ص����������
};


//����ֵd�������ٽ���a�ı���
#define ngx_align(d, a)     (((d) + (a - 1)) & ~(a - 1))
//С���ڴ���俼���ֽڶ���ʱ�ĵ�λ
#define NGX_ALIGNMENT   sizeof(unsigned long)
//��ָ��p������a���ٽ��ı���
#define ngx_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))
//buf��������0
#define ngx_memzero(buf, n)       (void) memset(buf, 0, n)

//Ĭ��һ������ҳ��Ĵ�С4K
const int ngx_pagesize = 4096;
//nginxС���ڴ�ɷ�������ռ�
const int NGX_MAX_ALLOC_FROM_POOL = ngx_pagesize - 1;
//���峣������ʾһ��Ĭ�ϵ�nginx�ڴ�ؿ��ٵĴ�С
const int NGX_DEFAULT_POOL_SIZE = 16 * 1024;//16K
//�ڴ�ش�С����16�ֽڽ��ж���
const int NGX_POOL_ALIGNMENT = 16;
//nginxС���ڴ����С��size������NGX_POOL_ALIGNMENT���ٽ��ı���
const int NGX_MIN_POOL_SIZE =
ngx_align((sizeof(ngx_pool_s) + 2 * sizeof(ngx_pool_large_s)),
	NGX_POOL_ALIGNMENT);


class ngx_mem_pool
{
public:
	//����ָ��size��С���ڴ��
	ngx_mem_pool(size_t size);

	//���������Զ�����
	~ngx_mem_pool();

	//�����ڴ��ֽڶ��룬���ڴ������size��С���ڴ�
	void* ngx_palloc(size_t size);
	//������ĺ���һ�������ǲ������ڴ��ֽڶ���
	void* ngx_pnalloc(size_t size);
	//���õ���ngx_pallocʵ���ڴ���䣬���ǻ��ʼ��0
	void* ngx_pcalloc(size_t size);

	//�ͷŴ���ڴ�
	void ngx_pfree(void* p);
	//�ڴ����ú���
	void ngx_reset_pool();
	//��ӻص������������
	ngx_pool_cleanup_s* ngx_pool_cleanup_add(size_t size);

private:
	ngx_pool_s* pool;//ָ��nginx�ڴ�ص����ָ��
	//С���ڴ����
	void* ngx_palloc_small(size_t size, ngx_uint_t align);
	//����ڴ����
	void* ngx_palloc_large(size_t size);
	//�����µ�С���ڴ��
	void* ngx_palloc_block(size_t size);

};













