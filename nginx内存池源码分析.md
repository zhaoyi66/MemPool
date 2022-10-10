nginx是http服务器，是短连接的，，客户端（浏览器）发起一个request请求，到达nginx服务器以后，创建一个内存池，处理完成，nginx给客户端返回一个response响应，http服务器就主动断开tcp连接（http 1.1 keep-avlie: 60s)

返回响应以后，需要等待60s，60s之内客户端又发来请求，重置这个时间，否则60s之内没有客户端发来的响应，nginx就主动断开连接，此时nginx可以调用ngx_reset_pool重置内存池了，等待下一次客户端的请求



## 重要类型定义

~~~c++
//对于nginx，小块内存和大块内存区分的界线是1个页面（4k）
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)    //4095
//默认的池的大小（16k)
#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)
//内存池内存分配的字节对齐的数字
#define NGX_POOL_ALIGNMENT       16
//类似SGISTL的辅助函数，把内存开辟调整到16的倍数
#define NGX_MIN_POOL_SIZE  			\
ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),	\
	NGX_POOL_ALIGNMENT)


//内存池数据头
struct ngx_pool_s { 
ngx_pool_data_t d; // 小块内存的数据头
size_t max; // 小块内存分配的最大值 不能超过页面4k
ngx_pool_t *current; // 小块内存入口指针 
ngx_chain_t *chain; 
ngx_pool_large_t *large; // 大块内存分配入口指针 
ngx_pool_cleanup_t *cleanup; // 清理函数handler的入口指针 
ngx_log_t *log;
};

typedef struct ngx_pool_s ngx_pool_t; 

// 小块内存分配记录
typedef struct { 
u_char *last; // 可分配内存开始位置 
u_char *end; // 可分配内存末尾位置 
ngx_pool_t *next; // 指向下个内存池首地址 
ngx_uint_t failed; // 记录当前内存池分配小块失败的次数 
} ngx_pool_data_t;

typedef struct ngx_pool_large_s ngx_pool_large_t; //大块内存分配记录
struct ngx_pool_large_s {
ngx_pool_large_t *next; // 下一个大块内存 
void *alloc; // 记录分配的大块内存的起始地址 
};

typedef void (*ngx_pool_cleanup_pt)(void *data); // 清理回调函数的类型定义 
typedef struct ngx_pool_cleanup_s ngx_pool_cleanup_t; // 清理操作的类型定义，包括一个清理回调函数，传给回调函数的数据和下一个清理操作的地址 
struct ngx_pool_cleanup_s { 
ngx_pool_cleanup_pt handler; // 清理回调函数 
void *data; // 传递给回调函数的数据 
ngx_pool_cleanup_t *next; // 指向下一个清理操作 
};
~~~







## 内存池创建函数

开辟指定大小内存，内存池头部信息初始化

~~~c
ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;//指向内存池头部信息

    //根据用户指定的大小来开辟内存池，可以根据不同系统平台定义的宏调用不同系统平台，如果没有定义的话，其实就是调用底层的malloc函数，开辟的大小应该大于内存池头部
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
    if (p == NULL) {
        return NULL;
    }
	//小块内存起始结束记录
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    p->d.end = (u_char *) p + size;
    
    p->d.next = NULL;//下个小块内存
    p->d.failed = 0;//开辟失败记录
	
    size = size - sizeof(ngx_pool_t);//小块内存可用大小
	//维护nginx小块内存的操作，作为大小块内存分配的分界线，max一定小于4K
    //但是该内存池的可用内存不一定是max，可以大于4K
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;//指向当前块的首地址
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;

    return p;
}
~~~



![image-20221007152135762](https://github.com/zhaoyi66/MemPool/blob/main/image/image-20221007152135762.png)





## 内存分配函数

向内存池申请内存的函数

~~~c
void *ngx_palloc(ngx_pool_t *pool, size_t size); // 内存分配函数，支持内存对齐 
void *ngx_pnalloc(ngx_pool_t *pool, size_t size); // 内存分配函数，不支持内存对齐 
void *ngx_pcalloc(ngx_pool_t *pool, size_t size); // 内存分配函数，支持内存初始化0

//分析ngx_palloc函数
void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    //申请内存小于max，max小于4k，属于小块内存申请
    if (size <= pool->max) {
        return ngx_palloc_small(pool, size, 1);
    }
	//大于max，可能大于4k，属于大块内存申请
    return ngx_palloc_large(pool, size);
}
~~~



### 小块内存分配

~~~c
static ngx_inline void *
ngx_palloc_small(ngx_pool_t *pool, size_t size, ngx_uint_t align)
{
    u_char      *m;
    ngx_pool_t  *p;
	//current指向的是哪个内存池，就从哪个内存池开始分配
    p = pool->current;

    do {
        m = p->d.last//可分配内存的起始地址
		//如果align为1，考虑内存对齐，把m指针调整成平台相关的unsigned long的整数倍
        if (align) {
            m = ngx_align_ptr(m, NGX_ALIGNMENT);
        }
		//可分配内存的结束地址-可分配内存起始地址>=申请内存
        //移动可分配内存的结束指针，直接返回可分配内存首地址
        if ((size_t) (p->d.end - m) >= size) {
            p->d.last = m + size;

            return m;
        }
		//如果该内存池可分配内存不够，移动到下个内存池
        p = p->d.next;

    } while (p);
	//再开辟新内存池并连接起来
    return ngx_palloc_block(pool, size);
}
~~~

![image-20221007154747701](F:\C-C++linux服务器开发\C++项目\nginx_mempool\image\image-20221007154747701.png)



~~~c
//开辟新内存池并连接起来
static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new;//内存池头部
	//开辟和之前一样大小
    psize = (size_t) (pool->d.end - (u_char *) pool);
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
    if (m == NULL) {
        return NULL;
    }
	
    new = (ngx_pool_t *) m;//内存池的头部

    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;
    
/*
假如连续进行内存申请，申请的内存比较大，也就是说，每次去遍历已有的小块内存的时候，发现都获取不到相应大小的内存块。那只能重新开辟一块内存池了。开辟完后，还要for循环把前面都循环一下，前面的内存块的failed都要++,如果说某个内块的failed大于4，说明这个内存块分配了4次都没有成功过，所以把pool的current指向下一个内存块，从下一个开始遍历
*/
    for (p = pool->current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }
    p->d.next = new;//连接新内存池
    return m;
}

/*
总的来说:分配小块内存，先使用第一个内存池的current指向当前使用的内存池，指针last，end分别指向可用内存起始，结束地址，如果end-last>=要分配的size，返回last首地址，last+=size，否则当前使用的内存池可用内存小，查看next内存池链表，如果这些内存池可用内存不够，需要开辟新内存池，只需要记录可用内存的起始，结束，next用来连接，falied
这些被访问过的内存池falied++，failed>4，这些内存池的剩余内存将不会被使用
*/
~~~

![image-20221007162555999](F:\C-C++linux服务器开发\C++项目\nginx_mempool\image\image-20221007162555999.png)







### 大块内存分配

~~~c
//传入内存池的起始地址和想申请的内存的大小
static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;//大块内存数据头,next用来连接,alloc指向大块内存首地址

    p = ngx_alloc(size, pool->log);//malloc，p指向这块内存
    if (p == NULL) {
        return NULL;
    }

    n = 0;
/*
在内存池的小块内存中分配大块内存数据头之前，先遍历大块内存的数据头链表，如果发现alloc为空，直接使用它，如果遍历链表了4个节点还未找到，那么直接在在内存池的小块内存中分配大块内存数据头比较快
*/
    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {
            large->alloc = p;
            return p;
        }
		//提高效率
        if (n++ > 3) {
            break;
        }
    }
	//在内存池的小块内存中分配大块内存数据头
    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }
	//大块内存头部记录大块内存起始地址
    large->alloc = p;
    //链表头插法
    large->next = pool->large;
    pool->large = large;

    return p;
}
/*
总的来说:大块内存大于max不在内存池的小块内存分配，通过malloc开辟，大块内存的数据头是在内存池的小块内存中分配的，用来记录大块内存起始地址并连接成链表
*/
~~~



![image-20221008170526784](F:\C-C++linux服务器开发\C++项目\nginx_mempool\image\image-20221008170526784.png)



#### 大块内存的释放

~~~c
//传入内存池和要释放的大块内存起始地址
ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;
//遍历大块内存数据头链表，释放大块内存，alloc置空，链表还是连接在一起的
    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            ngx_free(l->alloc);//free
            l->alloc = NULL;

            return NGX_OK;
        }
    }
    return NGX_DECLINED;
}
~~~



小块内存是没办法释放/归还的，因为它是通过last指针偏移来分配内存的









## 重置内存池函数

~~~c
void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;
	//遍历大块内存数据头链表，将大块内存free(可能会发生内存泄漏)
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }
	//将last指针偏移来归还所有的小块内存(可能会发生内存泄漏)
	/*
	//内存池大小都一样但是第一个内存池包含内存池数据头，之后的内存池包含小块内存的数据头
	//内存利用可以优化
    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
        p->d.failed = 0;
    }
    */
    //处理第一块内存池
	p=pool;
    p->d.last=(u_char *)p+sizeof(ngx_pool_t);//内存池的数据头
    p->d.failed=0;
    //
    for (p = p->d.next; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);//小块内存的数据头
        p->d.failed = 0;
    }

    pool->current = pool;//指向当前内存池
    pool->chain = NULL;
    pool->large = NULL;//大块内存的数据头不用了
}
~~~





## 添加清理handler函数

~~~c
//向内存池添加资源清理回调函数
ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;
	//资源清理操作的数据头，小于max在内存池的小块内存分配，大于max，大块内存
    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;
    }
	//给传入资源清理回调函数的参数分配内存
    if (size) {
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }
	//清理回调函数没有参数
    } else {
        c->data = NULL;
    }
	
    c->handler = NULL;
    //头插法，连接资源清理操作的数据头形成链表
    c->next = p->cleanup;
    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}
~~~





## 内存池销毁函数

~~~c
void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;
	//遍历清理操作的数据头链表，调用回调函数，执行外部资源清理操作，释放大块内存/小块内存上的外部资源(堆上的或者一些像fclose(file)不能直接free的资源)
    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "run cleanup: %p", c);
            c->handler(c->data);
        }
    }
	//遍历大块内存的数据头链表，free大块内存
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }
	//遍历内存池链表，free内存池,大块内存数据头，清理操作的数据头都没了
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);

        if (n == NULL) {
            break;
        }
    }
}
//先释放大块内存和小块内存上占用的外部资源，因为清理操作的数据头在大块内存和小块内存上，再释放大块内存，因为大块内存数据头在小块内存，最后释放内存池，数据头都没了
~~~





## nginx内存池总结

开始创建内存池，开辟的size可以大于4k，但max不能大于4k，用于判断小块内存与大块内存



关于小块内存，内存池的小块内存数据头d，last指向可用内存起始地址，end指向末尾，next用来连接小块内存数据头链表，failed用来判断是否使用该内存池分配小块内存

如果分配的size<max，属于小块内存分配，第一个内存池的current指向当前使用的内存池，如果end-last>=size，返回last地址，last+=size。

可用内存不够，如果遍历小块内存数据头链表能找到其他内存池分配小块内存，之前被遍历的内存池failed++，failed>4说明可用内存太小，current移动，以后再也不使用这些内存池了

如果要开辟新内存池，开辟和第一个内存池一样大，之后的内存池只要保存小块内存数据头，next连接起来



关于大块内存，大块内存数据头，alloc指向大块内存首地址，next连接大块内存数据头链表

如果分配的size>max，属于大块内存分配，malloc开辟大块内存，在小块内存中分配大块内存数据头，alloc指向大块内存首地址，next连接



由于可能会在小块内存/大块内存上有指针指向堆内存或者其他不能free的资源，有必要执行清理回调函数

关于清理操作的数据头，函数指针handler，传入的参数data，next连接成链表

向内存池添加清理操作的数据头，小于max在内存池的小块内存分配，大于max，大块内存开辟



最后销毁内存池，先释放大块内存和小块内存上占用的外部资源，因为清理操作的数据头在大块内存和小块内存上，再释放大块内存，因为大块内存数据头在小块内存，最后释放内存池，数据头都没了

























































