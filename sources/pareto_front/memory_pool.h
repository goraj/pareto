//
// Created by Alan Freitas on 2020-06-03.
//

#ifndef PARETO_FRONT_MEMORY_POOL_H
#define PARETO_FRONT_MEMORY_POOL_H

#include <climits>
#include <cstddef>

namespace pareto {

    /// \class Interleaved memory pool allocator
    ///
    /// The list of free chunks is kept inside the free chunks
    /// themselves.
    /// --->>> This is great for containers that don't need contiguous memory.
    /// --->>> This is VERY BAD for containers that need contiguous memory
    ///
    /// Each block has room for INITIAL_BLOCK_SIZE elements.
    ///
    /// When a block is full, we allocate another block for
    /// more INITIAL_BLOCK_SIZE elements.
    ///
    /// Although possible, interleaved free lists make it impractical
    /// to use it for contiguous allocation. In contiguous allocation,
    /// we need to keep the free list ordered and deallocation costs O(n).
    /// If we are using this pool for only one container, destroying
    /// the container ends up costing O(n^2).
    ///
    /// Another problem is that data structures that need contiguous
    /// allocation usually increment their demands exponentially, just
    /// like this allocator. This usually wastes a lot of space, as
    /// if we are never deallocating. It might be even worse than the
    /// O(n^2), because it wastes O(n^2) without any gain over
    /// the usual std::allocator.
    ///
    /// By default, the first block will have room for 512 elements
    /// \see https://github.com/mtrebi/memory-allocators#pool-allocator
    template <typename T, bool allow_contiguous_allocation = false, size_t INITIAL_BLOCK_SIZE = 512 * sizeof(T)>
    class interleaved_memory_pool {
    public:
        typedef T               value_type;
        typedef T*              pointer;
        typedef T&              reference;
        typedef const T*        const_pointer;
        typedef const T&        const_reference;
        typedef size_t          size_type;
        typedef ptrdiff_t       difference_type;
        typedef std::false_type propagate_on_container_copy_assignment;
        typedef std::true_type  propagate_on_container_move_assignment;
        typedef std::true_type  propagate_on_container_swap;

        template <typename U>
        struct rebind {
            typedef interleaved_memory_pool<U, allow_contiguous_allocation> other;
        };

        /// Construct with all pointers to nullptr
        interleaved_memory_pool() noexcept {
            current_block_ = nullptr;
            current_slot_ = nullptr;
            last_slot_ = nullptr;
            free_slots_ = nullptr;
            n_blocks_ = 0;
        }

        /// Construct and set everything to null
        interleaved_memory_pool(const interleaved_memory_pool& memoryPool) noexcept : interleaved_memory_pool() {
            // the copy constructor doesn't really copy because it wouldn't make much sense
        }

        /// Move constructor: copy all pointers
        interleaved_memory_pool(interleaved_memory_pool&& memoryPool) noexcept {
            current_block_ = memoryPool.current_block_;
            memoryPool.current_block_ = nullptr;
            current_slot_ = memoryPool.current_slot_;
            last_slot_ = memoryPool.last_slot_;
            free_slots_ = memoryPool.free_slots_;
            n_blocks_ = memoryPool.n_blocks_;
        }

        template <class U> interleaved_memory_pool(const interleaved_memory_pool<U>& memoryPool) noexcept : interleaved_memory_pool() {
            // the copy constructor doesn't really copy because it wouldn't make much sense
            // This simply converts the types
        }

        /// Delete all blocks
        ~interleaved_memory_pool() noexcept {
            // pointer to current block
            slot_pointer_type curr = current_block_;
            // if current block is not null
            while (curr != nullptr) {
                // get previous block
                slot_pointer_type prev = curr->next;
                assert(prev != curr);
                // delete current block
                --n_blocks_;
                operator delete(reinterpret_cast<void*>(curr), INITIAL_BLOCK_SIZE << n_blocks_);
                // current block is now the previous block
                curr = prev;
            }
        }

        /// Copy pointers
        interleaved_memory_pool& operator=(const interleaved_memory_pool& memoryPool) = delete;

        interleaved_memory_pool& operator=(interleaved_memory_pool&& memoryPool) noexcept {
            if (this != &memoryPool) {
                std::swap(current_block_, memoryPool.current_block_);
                current_slot_ = memoryPool.current_slot_;
                last_slot_ = memoryPool.last_slot_;
                free_slots_ = memoryPool.free_slots_;
                n_blocks_ = memoryPool.n_blocks_;
            }
            return *this;
        }

        /// Return a pointer to the element to which the reference x refers
        pointer address(reference x) const noexcept {
            return &x;
        }

        /// Return a const pointer to the element to which the reference x refers
        const_pointer address(const_reference x) const noexcept {
            return &x;
        }

        /// Allocate n objects
        /// Can only allocate one object at a time.
        /// n and hint are ignored because memory pools cannot
        /// efficiently guarantee a sequence of elements
        pointer allocate(size_type n = 1, const_pointer hint = 0) {
            // std::cout << "allocate" << std::endl;
            // free list is sorted, we allocate there, or in contiguous memory
            if constexpr (allow_contiguous_allocation) {
                if (n == 1) {
                    return allocate_one();
                } else {
                    // look for contiguous memory for n elements in the free slots
                    // if there are free slots
                    slot_pointer_type free_slots_last = free_slots_;
                    if (free_slots_ != nullptr) {
                        // iterate them until we find one that
                        // gives us n contiguous elements
                        // slot_pointer_type cur = free_slots_;
                        slot_pointer_type contiguous_start = free_slots_last;
                        slot_pointer_type before_contiguous_start = nullptr;
                        size_t n_contiguous = 0;
                        while (free_slots_last->next != nullptr) {
                            // for each element, it's one more contiguous
                            ++n_contiguous;
                            // if we got to n contiguous elements
                            if (n_contiguous == n) {
                                break;
                            }
                            // if next element is not contiguous
                            if (free_slots_last->next > free_slots_last + 1) {
                                n_contiguous = 0;
                                contiguous_start = free_slots_last->next;
                                before_contiguous_start = free_slots_last;
                            }
                            // go to next free slot
                            free_slots_last = free_slots_last->next;
                        }
                        // if that's why we left the loop
                        if (free_slots_last->next == nullptr) {
                            // the last element also counts as contiguous
                            ++n_contiguous;
                        }
                        // did we find n contiguous elements?
                        if (n_contiguous >= n) {
                            // the elements are no longer free then
                            // update the list
                            if (before_contiguous_start == nullptr) {
                                free_slots_ = free_slots_last->next;
                            } else {
                                before_contiguous_start = free_slots_last->next;
                            }
                            // return pointer to the position we found
                            return reinterpret_cast<pointer>(free_slots_last);
                        }
                    }
                    // if there are no free slots for n contiguous elements
                    // check if we need to allocate another block
                    if (current_slot_ + n > last_slot_) {
                        // all elements from current_slot_ to last_slot_
                        // become part of the free list
                        // if list is not empty
                        if (free_slots_ != nullptr) {
                            // finish iterating to the free list end
                            while (free_slots_last->next != nullptr) {
                                free_slots_last = free_slots_last->next;
                            }
                            // all positions from current slot to last_slot go to the free list
                            free_slots_last->next = current_slot_;
                            free_slots_last = free_slots_last->next;
                            while (free_slots_last < last_slot_) {
                                free_slots_last->next = free_slots_last + 1;
                                ++free_slots_last;
                                ++current_slot_;
                            }
                            free_slots_last->next = nullptr;
                        } else if (current_slot_ != nullptr){
                            // if free list was empty
                            // it now starts at the current slot
                            free_slots_ = current_slot_;
                            // we iterate until last slot making it part of
                            // the free list
                            slot_pointer_type i = current_slot_;
                            while (i < last_slot_) {
                                i->next = i + 1;
                                ++i;
                                ++current_slot_;
                            }
                            i->next = nullptr;
                        }
                        // allocate another block of elements
                        allocate_block();

                        // if there is no room even in this new block
                        // try everything again
                        if (current_slot_ + n > last_slot_) {
                            return allocate(n, hint);
                        } else {
                            // if there is room in the new block
                            slot_pointer_type res = current_slot_;
                            current_slot_ += n;
                            return reinterpret_cast<pointer>(res);
                        }
                    } else {
                        // if there is room at the end of the block
                        // just move the current slot forward
                        slot_pointer_type res = current_slot_;
                        current_slot_ += n;
                        return reinterpret_cast<pointer>(res);
                    }
                }
            } else {
                // free list is not sorted, we can only allocate one at a time
                assert(n == 1);
                return allocate_one();
            }
        }

        /// Deallocate p
        /// Position p becomes the first free slot pointing
        /// to the previous first free slot
        void deallocate(pointer p, size_type n = 1) {
            // std::cout << "deallocate" << std::endl;
            if constexpr (allow_contiguous_allocation) {
                // Free list needs to be sorted!
                // O(n) cost
                // if pointer is valid
                if (p != nullptr) {
                    // if we are deallocating before the start of the free list
                    if (comes_before(reinterpret_cast<slot_pointer_type>(p), free_slots_)) {
                        if (n == 1) {
                            // the position now has a pointer to where we our first free slot was
                            reinterpret_cast<slot_pointer_type>(p)->next = free_slots_;
                            // the point p becomes the first free slot
                            free_slots_ = reinterpret_cast<slot_pointer_type>(p);
                        } else {
                            for (size_t i = 0; i < n - 1; ++i) {
                                (reinterpret_cast<slot_pointer_type>(p) + i)->next = (reinterpret_cast<slot_pointer_type>(p) + i + 1);
                            }
                            (reinterpret_cast<slot_pointer_type>(p) + n - 1)->next = free_slots_;
                            free_slots_ = reinterpret_cast<slot_pointer_type>(p);
                        }
                    } else {
                        // we are deallocating after the free list beginning
                        // free_slots_ (free list begin) stays the same
                        // the last element in the list points to nullptr
                        // and we iterate the free list to find a place for p
                        // the element before p points to p
                        // p points to the element before p
                        slot_pointer_type cur = free_slots_;
                        while (cur->next != nullptr) {
                            // in an intermediary element, we want to know if it points to someone beyond p
                            // we want to know if p comes before cur->next
                            if (comes_before(reinterpret_cast<slot_pointer_type>(p), cur->next)) {
                                if (n == 1) {
                                    // if so, p should come between these elements now
                                    reinterpret_cast<slot_pointer_type>(p)->next = cur->next;
                                    cur->next = reinterpret_cast<slot_pointer_type>(p);
                                    // and we are done (in worst-case O(n))
                                    break;
                                } else {
                                    for (size_t i = 0; i < n - 1; ++i) {
                                        (reinterpret_cast<slot_pointer_type>(p) + i)->next = (reinterpret_cast<slot_pointer_type>(p) + i + 1);
                                    }
                                    (reinterpret_cast<slot_pointer_type>(p) + n - 1)->next = cur->next;
                                    cur->next = reinterpret_cast<slot_pointer_type>(p);
                                    break;
                                }
                            }
                            // if the element didn't point to someone beyond p
                            // we move to the next
                            cur = cur->next;
                        }
                        // if we got to the last element (we didn't break the loop)
                        if (cur->next == nullptr) {
                            // last element now points to p
                            cur->next = reinterpret_cast<slot_pointer_type>(p);
                            // elements inside p point to the next
                            for (size_t i = 0; i < n - 1; ++i) {
                                (reinterpret_cast<slot_pointer_type>(p) + i)->next = (reinterpret_cast<slot_pointer_type>(p) + i + 1);
                            }
                            // p is now the last element and points to nullptr
                            reinterpret_cast<slot_pointer_type>(p + n - 1)->next = nullptr;
                        }
                    }
                }
            } else {
                // unordered free list O(1)
                // if pointer is valid
                if (p != nullptr) {
                    // the position now has a pointer to where we have our first free slot
                    reinterpret_cast<slot_pointer_type>(p)->next = free_slots_;
                    // the point p becomes the first free slot
                    free_slots_ = reinterpret_cast<slot_pointer_type>(p);
                }
            }
        }

        /// Max number of elements in this pool
        size_type max_size() const noexcept {
            size_type max_blocks = -1 / INITIAL_BLOCK_SIZE;
            return (INITIAL_BLOCK_SIZE - sizeof(binary_data_pointer_type)) / sizeof(slot_type_) * max_blocks;
        }

        /// Construct new element of type U at position p
        template <class U, class... Args> void construct(U* p, Args&&... args) {
            // construct new element of type U at address p
            new (p) U (std::forward<Args>(args)...);
        }

        /// Destroy element of type U at position p
        template <class U> void destroy(U* p) {
            // Call U's destructor but leave the garbage there
            p->~U();
        }

        /// Allocate space for a new element T, construct it, and return a pointer
        template <class... Args> pointer new_element(Args&&... args) {
            // allocate room for an element
            pointer result = allocate();
            // construct element in this address
            construct<value_type>(result, std::forward<Args>(args)...);
            // return pointer
            return result;
        }

        /// Delete element of type T at position p
        void delete_element(pointer p) {
            // if p is valid
            if (p != nullptr) {
                // call its destructor
                p->~value_type();
                // deallocate address p
                deallocate(p);
            }
        }

    private:
        pointer allocate_one() {

            // if there are free slots
            if (free_slots_ != nullptr) {
                // get the pointer to first free slot
                pointer result = reinterpret_cast<pointer>(free_slots_);
                // update the first free slot
                free_slots_ = free_slots_->next;
                // return the pointer to the slot
                return result;
            }
            // if there are no free slots
            else {
                // check if we need to allocate another block
                if (current_slot_ > last_slot_) {
                    // allocate another block of elements
                    allocate_block();
                } else if (current_slot_ == nullptr && last_slot_ == nullptr) {
                    allocate_block();
                }
                // move current slot forward
                // return pointer to current slot, which is where we
                // keep the element in lack of a free_slot_ != nullptr
                return reinterpret_cast<pointer>(current_slot_++);
            }
        }

        /// A slot might store an element or a pointer to the next position available
        union slot_type {
            value_type element;
            slot_type* next;
        };

        typedef char* binary_data_pointer_type;
        typedef slot_type slot_type_;
        typedef slot_type* slot_pointer_type;

        /// \brief True if address a comes before address b in the pool
        /// This considers not only the address but also which blocks
        /// we created first. This keeps an order relationship that
        /// allows us to keep free chunks ordered.
        bool comes_before(slot_pointer_type a, slot_pointer_type b) {
            // for our purposes, nullptr represents +inf because the last element points to it
            if (b == nullptr) {
                return true;
            }
            if (a == nullptr) {
                return false;
            }
            // Iterate blocks and block sizes
            slot_pointer_type curr = current_block_;
            auto n_blocks = n_blocks_;
            // If current block is not null
            while (curr != nullptr) {
                // Are a or b in this block?
                bool a_in_block = (curr <= a && a < curr + (INITIAL_BLOCK_SIZE << (n_blocks - 1)));
                bool b_in_block = (curr <= a && a < curr + (INITIAL_BLOCK_SIZE << (n_blocks - 1)));
                if (!a_in_block && b_in_block) {
                    return true;
                }
                if (a_in_block && !b_in_block) {
                    return false;
                }
                if (a_in_block && b_in_block) {
                    return a < b;
                }
                // If none of them in this block, try next block
                --n_blocks;
                curr = curr->next;
            }
            throw std::invalid_argument("We can't find the pointers in any of the blocks");
        }

        /// Pointer to where the current block begins
        /// When we run out of space, we allocate a new block
        /// This slot pointer next member points to the
        /// previous block
        slot_pointer_type current_block_;

        /// Slot where we should allocate the next element
        /// If the user doesn't deallocate anything, free_slots_
        /// will be nullptr, and we start allocating at current_slot_
        /// This starts as the first slot of current block
        slot_pointer_type current_slot_;

        /// Last slot of current block
        /// When the current slot gets there, we allocate a new block
        slot_pointer_type last_slot_;

        /// First free slot
        /// It only becomes != nullptr when the user deallocates
        /// something. Otherwise, we allocate from current slot.
        /// The free slots are not ordered. This makes it hard to
        /// allocate contiguous memory.
        slot_pointer_type free_slots_;

        /// Number of blocks in this allocator
        size_t n_blocks_ = 0;

        /// Pad pointer to respect the alignment required by T
        size_type pad_pointer(binary_data_pointer_type p, size_type align) const noexcept {
            // get p as uint
            uintptr_t result = reinterpret_cast<uintptr_t>(p);
            // return aligned size
            return ((align - result) % align);
        }

        /// Allocate space for the new block and store a pointer to the previous one
        void allocate_block() {
            // size of this new block grows exponentially with number of blocks
            const size_t block_size = INITIAL_BLOCK_SIZE << n_blocks_;

            // keep track of the number of blocks so we can increase
            // the size of new blocks exponentially
            ++n_blocks_;

            // create a new block of char* with block size
            binary_data_pointer_type new_block = reinterpret_cast<binary_data_pointer_type>
            (operator new(block_size));

            // this first slot of a block is a special slot that points to the previous block
            // - interpret this as a pointer to slot and set its next field to current block
            // - the first slot of a new block will point to the previous block
            // - if this is the first block being allocated, the previous block is nullptr
            reinterpret_cast<slot_pointer_type>(new_block)->next = current_block_;

            // current block object member points to this new block
            // - we need this to keep track of blocks when we need to destroy them
            current_block_ = reinterpret_cast<slot_pointer_type>(new_block);

            // Pad block body to satisfy the alignment requirements for elements
            // the block body is everything after the first pointer to previous block

            // Body starts after one slot size
            binary_data_pointer_type body = new_block + sizeof(slot_pointer_type);

            // Get size in bytes we need to align the body to a slot_type
            // - The second slot might need to start a little after sizeof(slot_pointer_type) bytes
            size_type body_padding = pad_pointer(body, alignof(slot_type_));

            // Current slot points to first address of the body
            // We will keep moving this slot forward whenever the user asks
            // to allocate something
            current_slot_ = reinterpret_cast<slot_pointer_type>(body + body_padding);

            // Last slot points to last position available in the block
            last_slot_ = reinterpret_cast<slot_pointer_type>
            (new_block + block_size - sizeof(slot_type_));
        }

        /// Initial block size needs to have room for at least 2 slots
        static_assert(INITIAL_BLOCK_SIZE >= 2 * sizeof(slot_type_), "INITIAL_BLOCK_SIZE too small.");
    };

    /// \class Not-interleaved memory pool allocator
    ///
    /// The list of free chunks is kept in a separate set of chunks.
    /// The set of chunks is then always sorted.
    ///
    /// This spends a lot of memory but makes the free lists a little
    /// more practical because we deallocation doesn't take O(n^2).
    /// It takes only O(log n) now.
    ///
    /// --->>> This is better for containers that need contiguous memory.
    /// --->>> However, it's still worth comparing with new
    ///
    /// Each block has room for INITIAL_BLOCK_SIZE elements.
    ///
    /// When a block is full, we allocate another block for
    /// more INITIAL_BLOCK_SIZE elements.
    template <typename T, bool allow_contiguous_allocation = true, size_t INITIAL_BLOCK_SIZE = 512 * sizeof(T)>
    class free_memory_pool {
    public:
        typedef T               value_type;
        typedef T*              pointer;
        typedef T&              reference;
        typedef const T*        const_pointer;
        typedef const T&        const_reference;
        typedef size_t          size_type;
        typedef ptrdiff_t       difference_type;
        typedef std::false_type propagate_on_container_copy_assignment;
        typedef std::true_type  propagate_on_container_move_assignment;
        typedef std::true_type  propagate_on_container_swap;

        template <typename U>
        struct rebind {
            typedef free_memory_pool<U, allow_contiguous_allocation> other;
        };

        /// Construct with all pointers to nullptr
        free_memory_pool() noexcept : free_slots_{} {
            current_block_ = nullptr;
            current_slot_ = nullptr;
            last_slot_ = nullptr;
            n_blocks_ = 0;
        }

        /// Construct and set everything to null
        free_memory_pool(const free_memory_pool& memoryPool) noexcept : free_memory_pool() {
            // the copy constructor doesn't really copy because it wouldn't make much sense
        }

        /// Move constructor: copy all pointers
        free_memory_pool(free_memory_pool&& memoryPool) noexcept {
            current_block_ = memoryPool.current_block_;
            memoryPool.current_block_ = nullptr;
            current_slot_ = memoryPool.current_slot_;
            last_slot_ = memoryPool.last_slot_;
            free_slots_ = std::move(memoryPool.free_slots_);
            n_blocks_ = memoryPool.n_blocks_;
        }

        template <class U> free_memory_pool(const free_memory_pool<U>& memoryPool) noexcept : free_memory_pool() {
            // the copy constructor doesn't really copy because it wouldn't make much sense
            // This simply converts the types
        }

        /// Delete all blocks
        ~free_memory_pool() noexcept {
            // pointer to current block
            slot_pointer_type curr = current_block_;
            // if current block is not null
            while (curr != nullptr) {
                // get previous block
                slot_pointer_type prev = curr->next;
                assert(prev != curr);
                // delete current block
                --n_blocks_;
                operator delete(reinterpret_cast<void*>(curr), INITIAL_BLOCK_SIZE << n_blocks_);
                // current block is now the previous block
                curr = prev;
            }
        }

        /// Copy pointers
        free_memory_pool& operator=(const free_memory_pool& memoryPool) = delete;

        free_memory_pool& operator=(free_memory_pool&& memoryPool) noexcept {
            if (this != &memoryPool)
            {
                std::swap(current_block_, memoryPool.current_block_);
                current_slot_ = memoryPool.current_slot_;
                last_slot_ = memoryPool.last_slot_;
                free_slots_ = std::move(memoryPool.free_slots_);
                n_blocks_ = memoryPool.n_blocks_;
            }
            return *this;
        }

        /// Return a pointer to the element to which the reference x refers
        pointer address(reference x) const noexcept {
            return &x;
        }

        /// Return a const pointer to the element to which the reference x refers
        const_pointer address(const_reference x) const noexcept {
            return &x;
        }

        /// Allocate n objects
        /// Can only allocate one object at a time.
        /// n and hint are ignored because memory pools cannot
        /// efficiently guarantee a sequence of elements
        pointer allocate(size_type n = 1, const_pointer hint = 0) {
            // std::cout << "allocate" << std::endl;
            // free list is sorted, we allocate there, or in contiguous memory
            if constexpr (allow_contiguous_allocation) {
                if (n == 1) {
                    return allocate_one();
                } else {
                    // look for contiguous memory for n elements in the free slots
                    // if there are free slots
                    if (!free_slots_.empty()) {
                        // count how many contiguous elements we can find
                        auto [it, it_block] = first_free_slot();
                        auto first_contiguous_candidate = it;
                        auto first_contiguous_candidate_block = it_block;
                        size_t n_contiguous = 0;
                        while (!is_end_iterator(it, it_block)) {
                            ++n_contiguous;
                            if (n_contiguous == n) {
                                break;
                            }
                            auto [next, next_block] = next_set_iterator(it, it_block);
                            if (*next != *it + 1) {
                                n_contiguous = 0;
                                first_contiguous_candidate = next;
                                first_contiguous_candidate_block = next_block;
                            }
                            it = next;
                            it_block = next_block;
                        }
                        // if we found it
                        if (n_contiguous == n) {
                            auto res = reinterpret_cast<pointer>(*first_contiguous_candidate);
                            auto n_contiguous_end = first_contiguous_candidate;
                            std::advance(n_contiguous_end, n);
                            free_slots_[first_contiguous_candidate_block].erase(first_contiguous_candidate, n_contiguous_end);
                            return res;
                        }
                    }
                    // if there are no free slots for n contiguous elements
                    // check if we need to allocate another block
                    if (current_slot_ + n > last_slot_) {
                        // all elements from current_slot_ to last_slot_
                        // become part of the free list
                        while (current_slot_ < last_slot_) {
                            free_slots_[n_blocks_-1].emplace(current_slot_);
                            ++current_slot_;
                        }

                        // allocate another block of elements
                        allocate_block();

                        // if there is no room even in this new block
                        // try everything again
                        if (current_slot_ + n > last_slot_) {
                            return allocate(n, hint);
                        } else {
                            // if there is room in the new block
                            slot_pointer_type res = current_slot_;
                            current_slot_ += n;
                            return reinterpret_cast<pointer>(res);
                        }
                    } else {
                        // if there is room at the end of the block
                        // just move the current slot forward
                        slot_pointer_type res = current_slot_;
                        current_slot_ += n;
                        return reinterpret_cast<pointer>(res);
                    }
                }
            } else {
                return allocate_one();
            }
        }

        /// Deallocate p
        /// Position p becomes the first free slot pointing
        /// to the previous first free slot
        void deallocate(pointer p, size_type n = 1) {
            // if pointer is valid
            if (p != nullptr) {
                // put p in the appropriate free list
                // Iterate blocks and block sizes
                slot_pointer_type curr = current_block_;
                auto n_blocks = n_blocks_;
                // If current block is not null
                while (curr != nullptr) {
                    // is p in this block
                    bool p_in_block = (curr <= reinterpret_cast<slot_pointer_type>(p) && reinterpret_cast<slot_pointer_type>(p) < curr + (INITIAL_BLOCK_SIZE << (n_blocks - 1)));
                    if (p_in_block) {
                        // put p in the appropriate free list
                        for (size_t i = 0; i < n; ++i) {
                            free_slots_[n_blocks-1].emplace(reinterpret_cast<slot_pointer_type>(p) + i);
                        }
                        break;
                    }
                    // If p is not in this block, try next block
                    --n_blocks;
                    curr = curr->next;
                }
            }
        }

        /// Max number of elements in this pool
        size_type max_size() const noexcept {
            size_type max_blocks = -1 / INITIAL_BLOCK_SIZE;
            return (INITIAL_BLOCK_SIZE - sizeof(binary_data_pointer_type)) / sizeof(slot_type_) * max_blocks;
        }

        /// Construct new element of type U at position p
        template <class U, class... Args> void construct(U* p, Args&&... args) {
            // construct new element of type U at address p
            new (p) U (std::forward<Args>(args)...);
        }

        /// Destroy element of type U at position p
        template <class U> void destroy(U* p) {
            // Call U's destructor but leave the garbage there
            p->~U();
        }

        /// Allocate space for a new element T, construct it, and return a pointer
        template <class... Args> pointer new_element(Args&&... args) {
            // allocate room for an element
            pointer result = allocate();
            // construct element in this address
            construct<value_type>(result, std::forward<Args>(args)...);
            // return pointer
            return result;
        }

        /// Delete element of type T at position p
        void delete_element(pointer p) {
            // if p is valid
            if (p != nullptr) {
                // call its destructor
                p->~value_type();
                // deallocate address p
                deallocate(p);
            }
        }

    private:
        /// A slot might store an element or a pointer to the next position available
        union slot_type {
            value_type element;
            slot_type* next;
        };

        typedef char* binary_data_pointer_type;
        typedef slot_type slot_type_;
        typedef slot_type* slot_pointer_type;

        /// Sets of free slots
        /// The interesting thing here is that we use the interleaved fast memory
        /// pool to keep track of these free positions
        using free_slots_set_type = std::set<slot_pointer_type>;
        // using free_slots_set_type = std::set<slot_pointer_type, std::less<slot_pointer_type>, interleaved_memory_pool<slot_pointer_type, false>>;
        using free_slots_type = std::vector<free_slots_set_type>;
        free_slots_type free_slots_;

        using set_iterator = typename free_slots_type::value_type::iterator;

        // advance to a valid iterator (as if free slots were a range)
        std::pair<set_iterator, size_t> advance_to_valid (set_iterator it, size_t iterator_block) {
            while (it == free_slots_[iterator_block].end()) {
                ++iterator_block;
                if (iterator_block == free_slots_.size()) {
                    break;
                } else {
                    it = free_slots_[iterator_block].begin();
                }
            }
            return std::make_pair(it, iterator_block);
        }

        // get first iterator
        std::pair<set_iterator, size_t> first_free_slot () {
            set_iterator it = free_slots_[0].begin();
            return advance_to_valid(it, 0);
        }

        // is last iterator
        bool is_end_iterator (set_iterator it, size_t iterator_block) {
            if (iterator_block >= free_slots_.size()) {
                return true;
            }
            if (it == free_slots_.rbegin()->end()) {
                return true;
            }
            return false;
        }

        // get next iterator
        std::pair<set_iterator, size_t> next_set_iterator (set_iterator it, size_t iterator_block) {
            ++it;
            return advance_to_valid(it, iterator_block);
        };

        pointer allocate_one() {
            // if there are free slots
            if (!free_slots_.empty()) {
                auto [begin_it, begin_it_block] = first_free_slot();
                if (!is_end_iterator(begin_it, begin_it_block)) {
                    auto p = *begin_it;
                    // update the first free slot
                    free_slots_[begin_it_block].erase(begin_it);
                    // return the pointer to the slot
                    return reinterpret_cast<pointer>(p);
                }
            }
            // if there are no free slots
            // check if we need to allocate another block
            if (current_slot_ > last_slot_) {
                // allocate another block of elements
                allocate_block();
            } else if (current_slot_ == nullptr && last_slot_ == nullptr) {
                allocate_block();
            }

            // move current slot forward
            // return pointer to current slot, which is where we
            // keep the element in lack of a free_slot_ != nullptr
            return reinterpret_cast<pointer>(current_slot_++);
        }

        /// Pointer to where the current block begins
        /// When we run out of space, we allocate a new block
        /// This slot pointer next member points to the
        /// previous block
        slot_pointer_type current_block_;

        /// Slot where we should allocate the next element
        /// If the user doesn't deallocate anything, free_slots_
        /// will be nullptr, and we start allocating at current_slot_
        /// This starts as the first slot of current block
        slot_pointer_type current_slot_;

        /// Last slot of current block
        /// When the current slot gets there, we allocate a new block
        slot_pointer_type last_slot_;



        /// Number of blocks in this allocator
        size_t n_blocks_ = 0;

        /// Pad pointer to respect the alignment required by T
        size_type pad_pointer(binary_data_pointer_type p, size_type align) const noexcept {
            // get p as uint
            uintptr_t result = reinterpret_cast<uintptr_t>(p);
            // return aligned size
            return ((align - result) % align);
        }

        /// Allocate space for the new block and store a pointer to the previous one
        void allocate_block() {
            // size of this new block grows exponentially with number of blocks
            const size_t block_size = INITIAL_BLOCK_SIZE << n_blocks_;

            // keep track of the number of blocks so we can increase
            // the size of new blocks exponentially
            ++n_blocks_;

            // add a new set of free pointers
            free_slots_.emplace_back();

            // create a new block of char* with block size
            binary_data_pointer_type new_block = reinterpret_cast<binary_data_pointer_type>
            (operator new(block_size));

            // this first slot of a block is a special slot that points to the previous block
            // - interpret this as a pointer to slot and set its next field to current block
            // - the first slot of a new block will point to the previous block
            // - if this is the first block being allocated, the previous block is nullptr
            reinterpret_cast<slot_pointer_type>(new_block)->next = current_block_;

            // current block object member points to this new block
            // - we need this to keep track of blocks when we need to destroy them
            current_block_ = reinterpret_cast<slot_pointer_type>(new_block);

            // Pad block body to satisfy the alignment requirements for elements
            // the block body is everything after the first pointer to previous block

            // Body starts after one slot size
            binary_data_pointer_type body = new_block + sizeof(slot_pointer_type);

            // Get size in bytes we need to align the body to a slot_type
            // - The second slot might need to start a little after sizeof(slot_pointer_type) bytes
            size_type body_padding = pad_pointer(body, alignof(slot_type_));

            // Current slot points to first address of the body
            // We will keep moving this slot forward whenever the user asks
            // to allocate something
            current_slot_ = reinterpret_cast<slot_pointer_type>(body + body_padding);

            // Last slot points to last position available in the block
            last_slot_ = reinterpret_cast<slot_pointer_type>
            (new_block + block_size - sizeof(slot_type_));
        }

        /// Initial block size needs to have room for at least 2 slots
        static_assert(INITIAL_BLOCK_SIZE >= 2 * sizeof(slot_type_), "INITIAL_BLOCK_SIZE too small.");
    };

    template <typename T, size_t INITIAL_BLOCK_SIZE = 512 * sizeof(T)>
    using fast_memory_pool = interleaved_memory_pool<T, false, INITIAL_BLOCK_SIZE>;

    template <typename T>
    using default_fast_memory_pool = interleaved_memory_pool<T, false, 512 * sizeof(T)>;

    template <typename T, size_t INITIAL_BLOCK_SIZE = 512 * sizeof(T)>
    using unordered_memory_pool = interleaved_memory_pool<T, false, INITIAL_BLOCK_SIZE>;

    template <typename T, size_t INITIAL_BLOCK_SIZE = 512 * sizeof(T)>
    using contiguous_memory_pool = interleaved_memory_pool<T, true, INITIAL_BLOCK_SIZE>;

    template <typename T, size_t INITIAL_BLOCK_SIZE = 512 * sizeof(T)>
    using ordered_memory_pool = interleaved_memory_pool<T, true, INITIAL_BLOCK_SIZE>;


}

#endif //PARETO_FRONT_MEMORY_POOL_H