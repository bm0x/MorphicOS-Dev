#pragma once

// Spinlock for critical sections
// Simple test-and-set lock for single-core use

#include <stdint.h>

struct Spinlock {
    volatile uint32_t locked;
};

#define SPINLOCK_INIT {0}

namespace Sync {
    // Acquire spinlock (busy wait)
    inline void Lock(Spinlock* lock) {
        while (__sync_lock_test_and_set(&lock->locked, 1)) {
            // Busy wait with CPU hint
            #if defined(__x86_64__)
            __asm__ volatile("pause" ::: "memory");
            #elif defined(__aarch64__)
            __asm__ volatile("yield" ::: "memory");
            #endif
        }
    }
    
    // Release spinlock
    inline void Unlock(Spinlock* lock) {
        __sync_lock_release(&lock->locked);
    }
    
    // Try to acquire (non-blocking)
    inline bool TryLock(Spinlock* lock) {
        return __sync_lock_test_and_set(&lock->locked, 1) == 0;
    }
}

// RAII lock guard
class LockGuard {
    Spinlock* lock_;
public:
    LockGuard(Spinlock* l) : lock_(l) { Sync::Lock(lock_); }
    ~LockGuard() { Sync::Unlock(lock_); }
};

#define CRITICAL_SECTION(lock) LockGuard _guard_(&(lock))

// ============= TICKET LOCK (Fair ordering) =============

struct TicketLock {
    volatile uint32_t next_ticket;
    volatile uint32_t now_serving;
};

#define TICKET_LOCK_INIT {0, 0}

namespace Sync {
    inline void LockTicket(TicketLock* lock) {
        uint32_t my_ticket = __sync_fetch_and_add(&lock->next_ticket, 1);
        while (lock->now_serving != my_ticket) {
            #if defined(__x86_64__)
            __asm__ volatile("pause" ::: "memory");
            #elif defined(__aarch64__)
            __asm__ volatile("yield" ::: "memory");
            #endif
        }
    }
    
    inline void UnlockTicket(TicketLock* lock) {
        __sync_fetch_and_add(&lock->now_serving, 1);
    }
}

// ============= READ-WRITE LOCK =============
// Multiple readers OR single writer

struct RWLock {
    volatile int32_t readers;      // Number of active readers
    volatile uint32_t writer;      // 1 if writer active
    Spinlock write_lock;           // Serializes writer access
};

#define RWLOCK_INIT {0, 0, SPINLOCK_INIT}

namespace Sync {
    // Acquire read lock (multiple readers allowed)
    inline void ReadLock(RWLock* lock) {
        while (true) {
            // Wait if writer is active
            while (lock->writer) {
                #if defined(__x86_64__)
                __asm__ volatile("pause" ::: "memory");
                #endif
            }
            
            // Increment reader count
            __sync_fetch_and_add(&lock->readers, 1);
            
            // Check if writer snuck in
            if (lock->writer) {
                __sync_fetch_and_sub(&lock->readers, 1);
                continue;  // Retry
            }
            
            break;  // Got read lock
        }
    }
    
    // Release read lock
    inline void ReadUnlock(RWLock* lock) {
        __sync_fetch_and_sub(&lock->readers, 1);
    }
    
    // Acquire write lock (exclusive)
    inline void WriteLock(RWLock* lock) {
        Lock(&lock->write_lock);
        
        // Indicate writer wants access
        __sync_fetch_and_add(&lock->writer, 1);
        
        // Wait for readers to finish
        while (lock->readers > 0) {
            #if defined(__x86_64__)
            __asm__ volatile("pause" ::: "memory");
            #endif
        }
    }
    
    // Release write lock
    inline void WriteUnlock(RWLock* lock) {
        __sync_fetch_and_sub(&lock->writer, 1);
        Unlock(&lock->write_lock);
    }
}

// RAII Read lock guard
class ReadGuard {
    RWLock* lock_;
public:
    ReadGuard(RWLock* l) : lock_(l) { Sync::ReadLock(lock_); }
    ~ReadGuard() { Sync::ReadUnlock(lock_); }
};

// RAII Write lock guard
class WriteGuard {
    RWLock* lock_;
public:
    WriteGuard(RWLock* l) : lock_(l) { Sync::WriteLock(lock_); }
    ~WriteGuard() { Sync::WriteUnlock(lock_); }
};

#define READ_SECTION(lock)  ReadGuard  _rguard_(&(lock))
#define WRITE_SECTION(lock) WriteGuard _wguard_(&(lock))

