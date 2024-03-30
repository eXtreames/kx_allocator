/*------------------------------------------------------------------------------------------------*/

#pragma once
#ifndef KX_ALLOCATOR_HPP
#define KX_ALLOCATOR_HPP

/*------------------------------------------------------------------------------------------------*/

#include <intrin.h>

/*------------------------------------------------------------------------------------------------*/

#define KX_ALLOCATOR_DEFAULT_BLOCK_COUNT 128
#define KX_ALLOCATOR_DEFAULT_BLOCK_SIZE  32

/*------------------------------------------------------------------------------------------------*/

#define KX_ALLOCATOR_ZERO_FREE_MEMORY      (1ULL << 1)
#define KX_ALLOCATOR_ZERO_ALLOCATED_MEMORY (1ULL << 2)

/*------------------------------------------------------------------------------------------------*/

#define KX_ALLOCATOR_USE_AUTOMATIC_GC 0

/*------------------------------------------------------------------------------------------------*/

#define KX_ALLOCATOR_MEMCPY(dst, src, count)    __movsb((unsigned char*)(dst), (const unsigned char*)(src), (unsigned __int64)(count));
#define KX_ALLOCATOR_MEMSET_B(dst, data, count) __stosb((unsigned char*)(dst), (unsigned char)(data), (unsigned __int64)(count));
#define KX_ALLOCATOR_MEMSET_Q(dst, data, count) __stosq((unsigned __int64*)(dst), (unsigned __int64)(data), (unsigned __int64)(count));

/*------------------------------------------------------------------------------------------------*/

namespace kx
{
    template <class _Ty> struct remove_reference { using type = _Ty; };
    template <class _Ty> struct remove_reference<_Ty&> { using type = _Ty; };
    template <class _Ty> struct remove_reference<_Ty&&> { using type = _Ty; };
    template <class _Ty> using remove_reference_t = typename remove_reference<_Ty>::type;

    template <class _Ty> constexpr _Ty&& forward(remove_reference_t<_Ty>& _Arg)  noexcept { return static_cast<_Ty&&>(_Arg); }
    template <class _Ty> constexpr _Ty&& forward(remove_reference_t<_Ty>&& _Arg) noexcept { return static_cast<_Ty&&>(_Arg); }

    template <int BLOCK_COUNT = KX_ALLOCATOR_DEFAULT_BLOCK_COUNT, int BLOCK_SIZE = KX_ALLOCATOR_DEFAULT_BLOCK_SIZE>
    class allocator
    {
    public:
        using u64 = unsigned long long;
        using u8  = unsigned char;
    public:
        typedef void*(*p_allocate)(u64, u64*);
        typedef void(*p_free)(void*);
        typedef bool(*p_is_needed_gc)(allocator*);
    private:
#pragma pack(push, 8)
        typedef struct _BLOCK
        {
            _BLOCK* next;
            _BLOCK* prev;

            u64 max_size;
            u64 used_size;
            u64 block_size;

            u8* blocks_state[BLOCK_COUNT];
            u8 blocks[1];
        } BLOCK, * PBLOCK;
        typedef struct _ALLOCATOR_INFORMATION
        {
            u64 buckets_count   = { };
            u64 free_buckets    = { };
            u64 block_size      = { };
            u64 allocated_space = { };
            u64 used_space      = { };
            u64 free_space      = { };
        } ALLOCATOR_INFORMATION, * PALLOCATOR_INFORMATION;
#pragma pack(pop)
    private:
        u64 block_size = { };
        u64 flags      = { };
    private:
        p_allocate allocate_routine = { };
        p_is_needed_gc is_needed_gc = { };
        p_free free_routine         = { };
    private:
        PBLOCK first_block = { };
        PBLOCK last_block  = { };
    public:
        allocator(p_allocate allocation_routine, p_free free_routine)
        {
            this->allocate_routine = allocation_routine;
            this->free_routine     = free_routine;

            this->set_block_size(BLOCK_SIZE);
            this->first_block = this->allocate_block(this->get_block_size());
        }
        ~allocator()
        {
            if (!this->first_block)
            {
                return;
            }
            this->free_blocks();
        }
    public:
        inline void set_block_size(u64 block_size)
        {
            this->block_size = block_size;
        }
        inline u64 get_block_size() const
        {
            return this->block_size;
        }
    public:
        inline void set_allocation_routine(p_allocate routine)
        {
            this->allocate_routine = routine;
        }
        inline void set_free_routine(p_free routine)
        {
            this->free_routine = routine;
        }
        inline void set_is_needed_gc_routine(p_is_needed_gc is_needed_gc)
        {
            this->is_needed_gc = is_needed_gc;
        }
    public:
        inline void add_flag(u64 flag)
        {
            this->flags |= flag;
        }
        inline void remove_flag(u64 flag)
        {
            this->flags &= ~flag;
        }
    private:
        inline u64 align_64(u64 value)
        {
            return (((value)+64 - 1) & (~(64 - 1)));
        }
    private:
        inline PBLOCK allocate_block(u64 block_size)
        {
            auto allocated_block_size = static_cast<u64>(0);
            auto allocated_block      = reinterpret_cast<PBLOCK>(this->allocate_routine(sizeof(BLOCK) + block_size * BLOCK_COUNT, &allocated_block_size));

            if (!allocated_block)
            {
                return { };
            }
            if (allocated_block_size)
            {
                block_size = (allocated_block_size - sizeof(BLOCK)) / BLOCK_COUNT;
            }

            allocated_block->block_size = block_size;
            allocated_block->max_size   = block_size * BLOCK_COUNT;
            allocated_block->used_size  = { };
            allocated_block->next       = { };
            allocated_block->prev       = this->last_block;
            KX_ALLOCATOR_MEMSET_B(allocated_block->blocks_state, 0, sizeof(allocated_block->blocks_state));

            if (this->last_block)
            {
                this->last_block->next = allocated_block;
            }
            this->last_block = allocated_block;

            return allocated_block;
        }
        inline void free_block(PBLOCK block)
        {
            if (block->prev)
            {
                block->prev->next = block->next;
            }
            if (block->next)
            {
                block->next->prev = block->prev;
            }

            auto block_prev = block->prev;
            this->free_routine(block);

            if (this->last_block == block)
            {
                this->last_block = block_prev;
            }
            if (this->first_block == block)
            {
                this->first_block = { };
            }
        }
    private:
        inline void* try_allocate(u64 size) const
        {
            auto block = this->first_block;
            do
            {
                if (block->max_size - block->used_size < size)
                {
                    continue;
                }

                u64 free_space = { };
                int start_iter = -1;

                for (int i = 0; i < BLOCK_COUNT; i++)
                {
                    if (!block->blocks_state[i])
                    {
                        if (start_iter == -1)
                        {
                            start_iter = i;
                        }
                        free_space += block->block_size;
                    }
                    else
                    {
                        start_iter = -1;
                        free_space = { };
                    }

                    if (free_space >= size)
                    {
                        auto allocated = block->blocks + (start_iter * block->block_size);
                        KX_ALLOCATOR_MEMSET_Q(&block->blocks_state[start_iter], allocated, i - start_iter + 1);

                        block->used_size += free_space;

                        if (this->flags & KX_ALLOCATOR_ZERO_ALLOCATED_MEMORY) { KX_ALLOCATOR_MEMSET_B(allocated, 0, size); }
                        return allocated;
                    }
                }
            } while (block = block->next);

            return { };
        }
        inline void* try_reallocate(void* memory, u64 new_size)
        {
            auto current_memory_size = static_cast<u64>(0);
            auto block = this->first_block;
            do
            {
                if (!block->used_size)
                {
                    continue;
                }

                u64 memory_size   = current_memory_size;
                int memory_found  = -1;
                int needed_blocks = { };

                for (int i = 0; i < BLOCK_COUNT; i++)
                {
                    if (block->blocks_state[i] == memory)
                    {
                        current_memory_size += block->block_size;
                        if (memory_found == -1)
                        {
                            memory_found = i;
                        }
                        needed_blocks++;
                    }
                    else if (memory_found == -1)
                    {
                        continue;
                    }
                    else
                    {
                        if (block->blocks_state[i])
                        {
                            memory_size = 0;
                            break;
                        }
                        else if (!memory_size)
                        {
                            memory_size = current_memory_size;
                        }

                        memory_size += block->block_size;
                        if (memory_size >= new_size)
                        {
                            break;
                        }
                        needed_blocks++;
                    }
                }

                if (memory_size >= new_size)
                {
                    auto additional_size = memory_size - current_memory_size;

                    block->used_size += additional_size;
                    KX_ALLOCATOR_MEMSET_Q(&block->blocks_state[memory_found], memory, needed_blocks + 1);
                    if (this->flags & KX_ALLOCATOR_ZERO_ALLOCATED_MEMORY) { KX_ALLOCATOR_MEMSET_B(reinterpret_cast<u64>(memory) + current_memory_size, 0, additional_size); }

                    return memory;
                }
                else if (memory_found != -1)
                {
                    break;
                }
            } while (block = block->next);

            if (!current_memory_size)
            {
                return { };
            }

            auto allocated_memory = this->allocate(new_size);
            KX_ALLOCATOR_MEMCPY(allocated_memory, memory, current_memory_size);
            this->free(memory);

            return allocated_memory;
        }
        inline void* try_free(void* memory) const
        {
            void* freed = { };

            auto block = this->first_block;
            do
            {
                if (!block->used_size)
                {
                    continue;
                }

                size_t erased_size = { };
                for (int i = 0; i < BLOCK_COUNT; i++)
                {
                    if (block->blocks_state[i] == memory)
                    {
                        erased_size += block->block_size;
                        if (!freed)
                        {
                            freed = block->blocks_state[i];
                        }
                        block->blocks_state[i] = { };
                    }
                }

                if (freed)
                {
                    block->used_size -= erased_size;
                    if (this->flags & KX_ALLOCATOR_ZERO_FREE_MEMORY)
                    {
                        KX_ALLOCATOR_MEMSET_B(freed, 0, erased_size);
                    }
                    break;
                }
            } while (block = block->next);

            return freed;
        }
    private:
        inline int free_unused_blocks(bool exclude_first = true)
        {
            bool first_erased = { };
            int freed_count   = { };

            PBLOCK block_prev = { };
            PBLOCK block_next = { };

            auto block = this->last_block;
            do
            {
                block_prev = block->prev;
                if (exclude_first && block == this->first_block)
                {
                    break;
                }

                bool is_empty = true;
                for (int i = 0; i < BLOCK_COUNT; i++)
                {
                    if (block->blocks_state[i])
                    {
                        is_empty = false;
                        break;
                    }
                }

                if (is_empty)
                {
                    if (block == this->first_block)
                    {
                        first_erased = true;
                        block_next = block->next;
                    }
                    this->free_block(block);
                    freed_count++;
                }

            } while (block = block_prev);

            if (first_erased)
            {
                if (block_next)
                {
                    this->first_block = block_next;
                    this->first_block->prev = { };
                }
                else
                {
                    this->first_block = this->allocate_block(this->get_block_size());
                }
            }
            return freed_count;
        }
        inline int free_blocks()
        {
            int freed_count = { };
            PBLOCK block_prev = { };

            auto block = this->last_block;
            do
            {
                block_prev = block->prev;
                this->free_block(block);
                freed_count++;

            } while (block = block_prev);

            return freed_count;

        }
    public:
        inline void* allocate(u64 size)
        {
#if KX_ALLOCATOR_USE_AUTOMATIC_GC
            if (this->is_needed_gc && this->is_needed_gc(this))
            {
                this->gc();
            }
#endif

            if (auto allocated = this->try_allocate(size))
            {
                return allocated;
            }

            auto block_size = size > (this->get_block_size() * BLOCK_COUNT) ? this->align_64(size / BLOCK_COUNT + (size % BLOCK_COUNT ? 1 : 0)) : this->get_block_size();
            if (!this->allocate_block(block_size))
            {
                return { };
            }
            return this->try_allocate(size);
        }
        inline void* reallocate(void* memory, u64 size)
        {
            return this->try_reallocate(memory, size);
        }
        inline void* free(void* memory)
        {
            return this->try_free(memory);
        }
    public:
        inline int gc()
        {
            return this->free_unused_blocks(true);
        }
        inline void change_block_size(u64 size)
        {
            this->set_block_size(size);
            this->free_unused_blocks(false);
        }
    public:
        inline ALLOCATOR_INFORMATION collect_information() const
        {
            ALLOCATOR_INFORMATION information = { };
            information.block_size = this->get_block_size();

            auto block = this->first_block;
            do
            {
                if (!block->used_size)
                {
                    information.free_buckets++;
                }
                information.buckets_count++;

                information.allocated_space += BLOCK_COUNT * block->block_size;
                information.used_space += block->used_size;
                information.free_space += block->max_size - block->used_size;
            } while (block = block->next);

            return information;
        }
    public:
        template <class T, class... args> inline T* allocate_instance(args&&... arguments)
        {
            bool needed_erase = !(this->flags & KX_ALLOCATOR_ZERO_ALLOCATED_MEMORY);
            auto instance     = reinterpret_cast<T*>(this->allocate(sizeof(T)));

            if (needed_erase) { KX_ALLOCATOR_MEMSET_B(instance, 0, sizeof(T)); }
            return ::new(instance) T(forward<args>(arguments)...);
        }
        template <class T> inline void free_instance(T* instance)
        {
            instance->~T();
            this->free(reinterpret_cast<void*>(instance));
        }
    };
    using default_allocator = allocator<KX_ALLOCATOR_DEFAULT_BLOCK_COUNT, KX_ALLOCATOR_DEFAULT_BLOCK_SIZE>;
}

/*------------------------------------------------------------------------------------------------*/

#undef KX_ALLOCATOR_MEMSET_Q
#undef KX_ALLOCATOR_MEMSET_B

/*------------------------------------------------------------------------------------------------*/

#endif

/*------------------------------------------------------------------------------------------------*/
