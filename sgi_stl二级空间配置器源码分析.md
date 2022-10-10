## SGI STL一级,二级空间配置器

~~~c++
//SGI STL一级空间配置器类似STL的空间配置器，分离了内存开辟和对象构造，内存释放和对象析构
//STL的空间配置器
template<typename T>
class Allocator
{
public:
    T* allocate(size_t size)
    {
        return (T*)malloc(sizeof(T)*size);
    }
    void deallocate(void *p)
    {
        free(p);
    }
    void construct(T *p,const T &val)
    {
        new (p) T(val);//对象的构造，定位new
    }
    void destory(T *p)
    {
        p->~T();//对象的析构，调用析构函数
    }
};
~~~



对于小块内存频繁的分配和释放，如果一直使用malloc和free，会对我们的应用程序的运行在内存管理的效率降低，造成内存很多的碎片出来,内存没有更多的连续的大内存块。所以应用对于小块内存的操作,一般都会使用内存池来进行管理。

SGI STL其二级空间配置器allocator采用了基于freelist自由链表原理的内存池机制实现并且线程安全，有以下优点:
1.对于每一个字节数的内存块分配，都是给出一部分进行使用，另一部分作为备用，这个备用可以给当前字节数使用，也可以给其它字节数使用
2.对于备用内存池划分完chunk块以后，如果还有剩余的很小的内存块,再次分配的时候，会把这些小的内存块再次分配出去,备用内存池使用的干干净净!
3.当指定字节数内存分配失败以后，有一个异常处理的过程，byte -128字节所有的内存块进行查看，如果哪个字节数有空闲的内存块，直接借一个出去
如果上面操作失败，还会调用oom _malloc这么一个预先设置好的malloc内存分配失败以后的回调函数,没设置 throw bad_alloc



## 二级空间配置器的相关定义



### 重要类型和变量定义

~~~c++
enum { _ALIGN = 8 };//自由链表是从8字节开始以8字节为对齐方式一直扩充到128字节
enum { _MAX_BYTES = 128 };//内存池最大的内存块
enum { _NFREELISTS = 16 };// _MAX_BYTES/_ALIGN自由链表（数组节点）的个数

//每一个内存块的头部信息，_M_free_list_link存储下一个内存块头部的地址
union _Obj 
{
	union _Obj* _M_free_list_link;
	char _M_client_data[1]; 
};

/*
_S_free_list二级指针指向长度为16的静态指针数组首地址，指针数组的每一个元素的类型是_Obj*，指向内存块的头部，，第一个节点存储8字节大小的内存块的头部地址，每个节点比之前的节点存储 大8字节的内存块的头部地址，最大128字节，超过128通过malloc分配。初始化为nullptr
__STL_VOLATILE==VOLATILE，防止多线程给这个数组的元素进行缓存，导致一个线程对它的修改另外一个线程无法及时看到
*/
static _Obj* __STL_VOLATILE _S_free_list[_NFREELISTS];

//内存块分配情况，初始化为nullptr/0
static char* _S_start_free; 
static char* _S_end_free; 
static size_t _S_heap_size;
~~~



### 2个重要的辅助接口函数



~~~c++
//enum{_ALIGN=8};
/*对申请的字节大小__bytes进行封装，上调至最邻近的8的倍数*/
static size_t _S_round_up(size_t __bytes) { return (((__bytes) + (size_t) _ALIGN-1) & ~((size_t) _ALIGN - 1)); }
/*
1-8=>8
9-16=>16
...
举例:如果传入_bytes=1
_bytes+(size_t)_ALIGN-1=>8用32位无符号整形表示
	00000000 00000000 00000000 0000 1000
~(size_t)__ALIGN-1)=>7用32位无符号整形表示再取反
	11111111 11111111 11111111 1111 1000
&后  00000000 00000000 00000000 0000 1000=8
*/

/*返回 __bytes 大小的内存块位于 free-list 中的编号*/ 
static size_t _S_freelist_index(size_t __bytes) { return (((__bytes) + (size_t)_ALIGN-1)/(size_t)_ALIGN - 1); }
~~~




## allocate函数 

分配内存的入口函数

~~~c++
//_n是用户要开辟内存空间的大小
static void* allocate(size_t __n)
  {
    void* __ret = 0;
//如果大于128字节就一级空间配置器管理，如果小于等于128字节就内存池管理
    if (__n > (size_t) _MAX_BYTES) {
      __ret = malloc_alloc::allocate(__n);
    }
/*
__my_free_list指向_S_free_list这个指针数组 _n大小对应的元素
*/
    else {
      _Obj* __STL_VOLATILE* __my_free_list
          = _S_free_list + _S_freelist_index(__n);
#     ifndef _NOTHREADS
//自定义的智能锁，构造加锁，出作用域解锁,保证了指针数组_S_free_list在多线程环境下的安全
      _Lock __lock_instance;
#     endif
      _Obj* __RESTRICT __result = *__my_free_list;
      if (__result == 0)
        __ret = _S_refill(_S_round_up(__n));
      else {
        *__my_free_list = __result -> _M_free_list_link;
        __ret = __result;
      }
    }
    return __ret;
  };
/*
__result = *__my_free_list一级指针指向对应的元素，如果_result是0，说明下面没有分配任何内存块，就调用_S_refill给对应元素分配对应大小的内存块(obj*指向内存块头部)，_ret返回第一个内存块首地址
如果_result不是0，对应元素已经分配内存块
*__my_free_list = __result -> _M_free_list_link
__result移动到下一个内存块，ret返回_result指向的内存块
*/
~~~

总的来说：通过 S_freelist_index(n)找到指针数组对应元素，二级指针my_free_list指向这个数组的这个元素，_result一级指针指向该元素，元素中存储指向内存块头部的地址，指针为空->需要分配内存块(S_refill函数)，不为空，内存块头部的指针域指向下个内存块头部，节点中的指针指向下个内存块的头部，前面的内存块就被分配出去了
智能锁使得指针数组在被访问，改变元素时是线程安全的



## _S_refill函数

负责把开辟的连续内存划分成对应大小的内存块并将它们头部连接起来，返回内存首地址

~~~c++
template <bool __threads, int __inst>
void*
__default_alloc_template<__threads, __inst>::_S_refill(size_t __n)
{
    int __nobjs = 20;//要划分内存块数
    //_S_chunk_alloc开辟对应大小内存空间返回首地址
    char* __chunk = _S_chunk_alloc(__n, __nobjs);
    _Obj* __STL_VOLATILE* __my_free_list;
    _Obj* __result;
    _Obj* __current_obj;
    _Obj* __next_obj;
    int __i;
//由于_S_chunk_alloc是引用接收__nobjs，如果实际只开辟了一块就直接返回
    if (1 == __nobjs) return(__chunk);
    
    __my_free_list = _S_free_list + _S_freelist_index(__n);

      __result = (_Obj*)__chunk;
      *__my_free_list = __next_obj = (_Obj*)(__chunk + __n);
//将开辟的连续内存划分成对应大小的内存块并将它们头部连接起来
      for (__i = 1; ; __i++) {
        __current_obj = __next_obj;
        __next_obj = (_Obj*)((char*)__next_obj + __n);
        if (__nobjs - 1 == __i) {
            __current_obj -> _M_free_list_link = 0;
            break;
        } else {
            __current_obj -> _M_free_list_link = __next_obj;
        }
      }
    return(__result);
}
~~~

![image-20221001163232173](F:\C-C++linux服务器开发\C++项目\image\image-20221001163232173.png)

总的来说:chunk指向S_chunk_alloc开辟的一块连续内存空间首地址，result指向首地址并强转为obj*返回第一个内存块,之后以n大小(S_round_up(_n)8的倍数)将内存空间划分为一个个内存块，内存块union头部被用来连接内存块，最后一个内存块头部被置空，数组的节点(obj指针)指向第二个内存块头部。
在allocate函数中，通过内存块的头部去访问下个内存块



## _S_chunk_alloc函数

开辟一块连续的大小的内存空间

allocate(size_t __n)返回对应大小的内存块首地址，如果发现n对应的指针数组为，需要开辟内存空间，S_refill函数只是将开辟的内存空间划分并连接，实际开辟内存空间由S_chunk_alloc完成

由于_S_chunk_alloc是递归函数，里面有许多情况的处理，以下展示对应情况的代码

### 第一次分配

~~~c++
//假设分配8字节大小20个内存块
/*
初始值都为0
static char* _S_start_free; //内存块起始地址
static char* _S_end_free; //内存块结束地址
static size_t _S_heap_size;//内存块所占字节大小
*/
char* __result;
size_t __total_bytes = __size * __nobjs;//需要开辟内存大小(160)
size_t __bytes_left = _S_end_free - _S_start_free;//备用内存0

if (__bytes_left >= __total_bytes) {}//备用内存>=需要开辟内存
else if(__bytes_left >= __size){}//备用内存>=一个内存块大小
else{
size_t __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);//(2*160+0)

//实际开辟了320字节大小
 _S_start_free = (char*)malloc(__bytes_to_get);
}
//如果开辟内存成功, _S_start_free!=0

_S_heap_size += __bytes_to_get;//记录开辟内存空间的大小，再开辟会用到
_S_end_free = _S_start_free + __bytes_to_get;//移动到尾
return(_S_chunk_alloc(__size, __nobjs));//再次调用自己

char* __result;
size_t __total_bytes = __size * __nobjs;//需要开辟内存大小160
size_t __bytes_left = _S_end_free - _S_start_free;//备用内存320

if (__bytes_left >= __total_bytes) {//320>160
__result = _S_start_free;
_S_start_free += __total_bytes;
return(__result)
}
/*
把前面160字节大小内存空间使用起来，后160字节大小内存备用,最后把这块连续内存空间首地址返回，_S_start_free和_S_end_free指向备用内存首尾，_S_refill将这块连续内存空间的160划分成8字节大小的内存块并将它们头部连接，调用一次allocate函数就使用一个内存块，数组元素(obj指针)指向下个内存块头部，直到数组元素为空，即20个内存块都分配完了(最后一个内存块头部指向空)，再次调用_S_refill
*/
~~~

![image-20221002171143655](F:\C-C++linux服务器开发\C++项目\image\image-20221002171143655.png)





### 备用内存的使用

~~~c++
/*
1.接上面情况，还是给字节大小为8的内存块分配内存空间，160字节大小的内存使用完了，再调用allocate发现数组元素为空，所以调用_S_refill函数，再调用_S_chunk_alloc函数，因为_S_start_free和_S_end_free指向备用内存首尾，所以不用开辟新内存，移动_S_start_free和_S_end_free把备用内存返回给_S_refill划分，备用内存剩0
*/
char* __result;
size_t __total_bytes = __size * __nobjs;//要开辟160
size_t __bytes_left = _S_end_free - _S_start_free;//备用160

if (__bytes_left >= __total_bytes) {
   __result = _S_start_free;
   _S_start_free += __total_bytes;
   return(__result);
}


//2.假设现在给大小为128字节的内存块分配内存空间，由于有备用内存空间，所以不用开辟新内存，计算只能分配一个，那么实际只能分配1*128,将_nobjs置1，返回备用内存首地址，备用内存剩下32字节大小
char* __result;
size_t __total_bytes = __size * __nobjs;//要分配20*128
size_t __bytes_left = _S_end_free - _S_start_free;//备用160

if (__bytes_left >= __total_bytes) {}//160<20*128
else if (__bytes_left >= __size) {//160>128
        __nobjs = (int)(__bytes_left/__size);//__nobjs=1
        __total_bytes = __size * __nobjs;//实际分配1*128
        __result = _S_start_free;
        _S_start_free += __total_bytes;//剩下32字节
        return(__result);
}
~~~



### 再分配

~~~c++
//1.之前分配过，备用的内存空间为0，继续分配20个8字节大小的内存块
//与第一次分配相比，_S_heap_size是记录之前开辟内存的大小并且_S_end_free，_S_start_free不为空

char* __result;
size_t __total_bytes = __size * __nobjs;//160
//0,但是指针不再指向空
size_t __bytes_left = _S_end_free - _S_start_free;

if (__bytes_left >= __total_bytes) {}//0<160
else if (__bytes_left >= __size) {}//0<8
else{
size_t __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);//2*160+160=480
	if (__bytes_left > 0){}//0,0
}
_S_start_free = (char*)malloc(__bytes_to_get);//实际开辟60*8=480
//如果开辟成功，_S_start_free！=0
_S_heap_size += __bytes_to_get;//继续累加_S_heap_size=800
_S_end_free = _S_start_free + __bytes_to_get;//+480
return(_S_chunk_alloc(__size, __nobjs));

char* __result;
size_t __total_bytes = __size * __nobjs;//需要开辟160
size_t __bytes_left = _S_end_free - _S_start_free;//备用480

if (__bytes_left >= __total_bytes) {//480>160
   __result = _S_start_free;
   _S_start_free += __total_bytes;//移动了160
   return(__result);
}
//开辟480字节大小内存空间，返回160字节大小给_S_refill,320备用
//只要新开辟内存空间，_S_heap_size就会累加记录，下次开辟更大的内存空间，但是_S_refill只会划分指定大小的内存，所以备用的更大了
~~~



~~~c++
//2.之前分配过，备用的内存有剩余，但是连一个指定大小的内存块都分配不了
//例如，之前160字节的备用内存空间分配了一个128字节的内存块，剩下32，再去分配一块128字节的内存块，备用的内存连一个也不能分配，那么这块内存复用给其他内存块(32大小的)，再为128大小的 开辟新内存

char* __result;
size_t __total_bytes = __size * __nobjs;//需要开辟20*128
size_t __bytes_left = _S_end_free - _S_start_free;//剩下32

if (__bytes_left >= __total_bytes){}//32<128*20
else if (__bytes_left >= __size) {}//32<128
else{
size_t __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);//新开辟内存空间大小
//备用的内存复用给其他内存块
if (__bytes_left > 0) {
_Obj* __STL_VOLATILE* __my_free_list =_S_free_list + _S_freelist_index(__bytes_left);
 ((_Obj*)_S_start_free) -> _M_free_list_link = *__my_free_list;
 *__my_free_list = (_Obj*)_S_start_free;
	}
//开辟新内存成功,_S_start_free!=0
_S_start_free = (char*)malloc(__bytes_to_get);//实际开辟20*128
}

 _S_heap_size += __bytes_to_get;//累加记录
_S_end_free = _S_start_free + __bytes_t_get;
return(_S_chunk_alloc(__size, __nobjs));
~~~

备用内存头部指针域指向数组元素(obj*，即指向另一个内存块头部)，数组元素指向备用内存头部

![image-20221003155402275](F:\C-C++linux服务器开发\C++项目\image\image-20221003155402275.png)





### 开辟内存失败处理

~~~c++
 _S_start_free = (char*)malloc(__bytes_to_get);//开辟失败返回空
if (0 == _S_start_free) {
	size_t __i;
	_Obj* __STL_VOLATILE* __my_free_list;
	_Obj* __p;
//遍历_size(8的倍数)到128的数组元素
    for (__i = __size;
         __i <= (size_t) _MAX_BYTES;
         __i += (size_t) _ALIGN) 
    {
         __my_free_list = _S_free_list + _S_freelist_index(__i);
         __p = *__my_free_list;
     if (0 != __p) {
         *__my_free_list = __p -> _M_free_list_link;
          _S_start_free = (char*)__p;
          _S_end_free = _S_start_free + __i;
          return(_S_chunk_alloc(__size, __nobjs));
          }
	}
//_S_start_free和_S_end_free移动到其他内存空间上，刚好够一个内存块所需空间，__nobjs=1，直接返回，由于是大内存复用给小内存块，所以剩下的内存 算 备用内存，之后会复用给其他内存块
~~~

![image-20221003165414813](F:\C-C++linux服务器开发\C++项目\image\image-20221003165414813.png)





~~~c++
//如果在剩下的数组节点中没有找到空闲的内存空间
_S_end_free = 0;
_S_start_free = (char*)malloc_alloc::allocate(__bytes_to_get);

template <int __inst>
class __malloc_alloc_template {
	static void* allocate(size_t __n)
	{
    	void* __result = malloc(__n);
    	if (0 == __result) __result = _S_oom_malloc(__n);
    	return __result;
	}
}

template <int __inst>
void*
__malloc_alloc_template<__inst>::_S_oom_malloc(size_t __n)
{
    void (* __my_malloc_handler)();//函数指针
    void* __result;

    for (;;) {
//void (*__malloc_alloc_template<__inst>::__malloc_alloc_oom_handler)() = 0; 用户可以指定开辟失败后的处理

        __my_malloc_handler = __malloc_alloc_oom_handler;
        //默认throw bad_alloc
        if (0 == __my_malloc_handler) { __THROW_BAD_ALLOC; }
        (*__my_malloc_handler)();//调用
        __result = malloc(__n);
        if (__result) return(__result);
    }
}

_S_heap_size += __bytes_to_get;
_S_end_free = _S_start_free + __bytes_to_get;
return(_S_chunk_alloc(__size, __nobjs));

//假设处理成功了，_S_start_free等于新开辟内存空间的首地址,返回给_S_refill函数
~~~



### _S_chunk_alloc函数整个过程

~~~c++
template <bool __threads, int __inst>
char*
__default_alloc_template<__threads, __inst>::_S_chunk_alloc(size_t __size, int& __nobjs)
{
    char* __result;
    size_t __total_bytes = __size * __nobjs;//要开辟的内存
    size_t __bytes_left = _S_end_free - _S_start_free;//备用的内存
//备用内存够大，不用开辟新内存，直接返回备用的内存空间首地址
    if (__bytes_left >= __total_bytes) {
        __result = _S_start_free;
        _S_start_free += __total_bytes;
        return(__result);
//备用内存小于要开辟的内存但能够分配一个以上，修改__nobjs的值(改变_S_refill处理数量)，并返回
    } else if (__bytes_left >= __size) {
        __nobjs = (int)(__bytes_left/__size);
        __total_bytes = __size * __nobjs;
        __result = _S_start_free;
        _S_start_free += __total_bytes;
        return(__result);
//第一次开辟内存/备用的内存有剩余并且小于一个指定内存块大小
    } else {
		//实际开辟的大小，多于要开辟的
        size_t __bytes_to_get = 
	  2 * __total_bytes + _S_round_up(_S_heap_size >> 4);
		//备用内存复用给其他内存块
        if (__bytes_left > 0) {
            _Obj* __STL_VOLATILE* __my_free_list =
			_S_free_list + _S_freelist_index(__bytes_left);

            ((_Obj*)_S_start_free) -> _M_free_list_link = *__my_free_list;
            *__my_free_list = (_Obj*)_S_start_free;
        }
		//开辟新内存
        _S_start_free = (char*)malloc(__bytes_to_get);
		//开辟失败处理
        if (0 == _S_start_free) {
            size_t __i;
            _Obj* __STL_VOLATILE* __my_free_list;
	    	_Obj* __p;
			//遍历剩下的节点，复用它们的空闲内存空间
            for (__i = __size;
                 __i <= (size_t) _MAX_BYTES;
                 __i += (size_t) _ALIGN) {
                __my_free_list = _S_free_list + 				_S_freelist_index(__i);
                __p = *__my_free_list;
                if (0 != __p) {
                    *__my_free_list = __p -> _M_free_list_link;
                    _S_start_free = (char*)__p;
                    _S_end_free = _S_start_free + __i;
                    return(_S_chunk_alloc(__size, __nobjs));
                }
            }
	    _S_end_free = 0;	
_S_start_free = (char*)malloc_alloc::allocate(__bytes_to_get);
        }
		//新开辟内存成功，累加记录开辟大小，下次开辟更大内存
        _S_heap_size += __bytes_to_get;
        _S_end_free = _S_start_free + __bytes_to_get;
        return(_S_chunk_alloc(__size, __nobjs));
    }
}
~~~





## deallocate函数

把内存块归还到内存池

~~~c++
/*
把某内存首地址和字节大小传入
如果大于128字节就free释放，小于128字节归还到内存池
二级指针__my_free_list指向数组对应元素(_S_freelist_index(_n)返回对应节点编号)_q指向这块_n大小内存块的前4个字节的头部(obj)
接下来需要归还内存块并改变数组元素的指向，加上智能锁，保证多线程下的安全
*/
static void deallocate(void* __p, size_t __n)
  {
    if (__n > (size_t) _MAX_BYTES)
      malloc_alloc::deallocate(__p, __n);
    else {
      _Obj* __STL_VOLATILE*  __my_free_list
          = _S_free_list + _S_freelist_index(__n);
      _Obj* __q = (_Obj*)__p;
#       ifndef _NOTHREADS
      _Lock __lock_instance;
#       endif 
      __q -> _M_free_list_link = *__my_free_list;
      *__my_free_list = __q;
      // lock is released here
    }
  }
~~~

假设1,2号内存块被使用，所以数组元素指向3号内存块
现在要归还1号内存块，一号内存块的头部指向数组节点指向3号内存块头部，将它们连接起来方便下次再分配，数组节点重新指向1号内存块头部

![image-20221002210205242](F:\C-C++linux服务器开发\C++项目\image\image-20221002210205242.png)



## reallocate函数

~~~C++
//扩容/缩容一个内存块
//__p内存块首地址，__old_sz，__new_sz
template <bool threads, int inst>
void*
__default_alloc_template<threads, inst>::reallocate(void* __p,
size_t __old_sz,size_t __new_sz)
{
    void* __result;
    size_t __copy_sz;
//大于128字节，不是从内存池中分配，realloc扩容/缩容
    if (__old_sz > (size_t) _MAX_BYTES && __new_sz > (size_t) _MAX_BYTES) {
        return(realloc(__p, __new_sz));
    }
	//内存块不需要扩容/缩容 直接返回
    if (_S_round_up(__old_sz) == _S_round_up(__new_sz)) return(__p);
	//allocate返回新内存块起始地址
    __result = allocate(__new_sz);
    __copy_sz = __new_sz > __old_sz? __old_sz : __new_sz;//扩容/缩容字节大小
    memcpy(__result, __p, __copy_sz);//扩容/缩容
    deallocate(__p, __old_sz);//归还旧的
    return(__result);
}
~~~





























