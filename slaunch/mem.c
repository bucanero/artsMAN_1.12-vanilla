#include <sys/memory.h>

#include "include/mem.h"
#include "include/vsh_exports.h"
//#include "include/network.h"	// debug


static sys_memory_container_t mc_app = (sys_memory_container_t)-1;
static sys_memory_container_t mc_game = (sys_memory_container_t)-1;
static sys_addr_t heap_mem = 0;
static uint32_t prx_heap = 0;


/***********************************************************************
* return heap pointer
***********************************************************************/
uint32_t get_heap(void)
{
	return prx_heap;
}

/***************************************
* create prx heap from memory container
***************************************/
/* int32_t create_heap(int target, int32_t size)
{
	// mc_app = vsh_memory_container_by_id(1);
	// if (!mc_app) return(-1);
	int32_t ret;

	// sys_memory_allocate_from_container(MB(size), mc_app, SYS_MEMORY_PAGE_SIZE_1M, &heap_mem);
	if(target==1) //memory container 1("app") for sman
		int32_t ret = sys_memory_allocate_from_container(KB(size * 1024), vsh_E7C34044(1), SYS_MEMORY_PAGE_SIZE_64K, &heap_mem);
	else if(target==2)//memory container 0("game") for artemis
		ret = sys_memory_allocate_from_container(KB(size * 64), vsh_E7C34044(0), SYS_MEMORY_PAGE_SIZE_64K, &heap_mem);
	if (ret != 0) {
		// memory allocate error
		heap_mem = 0;
		return(-1);
	}
	prx_heap = (uint32_t)heap_mem;
	return(0);
} */
int32_t create_heap(int target, int32_t size)
{
	// mc_app = vsh_memory_container_by_id(1);
	// mc_app = vsh_E7C34044(1);
	// if (!mc_app) return(-1);
	int32_t ret=0;

	// sys_memory_allocate_from_container(MB(size), mc_app, SYS_MEMORY_PAGE_SIZE_1M, &heap_mem);
	if(target==1) //memory container 1("app") for sman
	{
		mc_app = vsh_E7C34044(1);
		if (!mc_app) return(-1);
		ret = sys_memory_allocate_from_container(KB(size * 1024), mc_app, SYS_MEMORY_PAGE_SIZE_64K, &heap_mem);
	}
	else if(target==2)//memory container 0("game") for artemis
	{
		mc_game = vsh_E7C34044(0);
		if (!mc_app) return(-1);
		ret = sys_memory_allocate_from_container(KB(size * 64), mc_game, SYS_MEMORY_PAGE_SIZE_64K, &heap_mem);
	}
	if (ret != 0) {
		// memory allocate error
		heap_mem = 0;
		return(-1);
	}
	prx_heap = (uint32_t)heap_mem;
	return(0);
}

/******************
*
*******************/
void reset_heap(void)
{
	if (prx_heap)
		prx_heap = heap_mem;
}

/***********************************************************************
*
***********************************************************************/
void destroy_heap(void)
{

  sys_memory_free((sys_addr_t)heap_mem);
  prx_heap = 0;
  mc_app = (sys_memory_container_t)-1;

}

/***********************************************************************
*
***********************************************************************/
void *mem_alloc(uint32_t size)
{

	uint32_t add = prx_heap;
	prx_heap += size;
	return (void*)add;
}

/***********************************************************************
*
***********************************************************************/
int32_t mem_free(uint32_t size)
{
	if(prx_heap>=size) prx_heap -= size; else return (-1);
	return(0);
}
