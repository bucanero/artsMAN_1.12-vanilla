#ifndef __MEM_H__
#define __MEM_H__


#define MB(x)      ((x)*1024*1024)
#define KB(x)      ((x)*1024)


uint32_t get_heap(void);
int32_t create_heap(int target, int32_t size);
void reset_heap(void);
void destroy_heap(void);
void *mem_alloc(uint32_t size);
int32_t mem_free(uint32_t size);

#endif // __MEM_H__
