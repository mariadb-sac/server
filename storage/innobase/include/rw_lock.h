/*****************************************************************************

Copyright (c) 2020, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

#pragma once
#include <atomic>
#include "my_dbug.h"

/** Simple read-write lock based on std::atomic */
class rw_lock
{
  /** The lock word */
  std::atomic<uint32_t> lock;
  char pad[CPU_LEVEL1_DCACHE_LINESIZE - sizeof lock];

  /** Available lock */
  static constexpr uint32_t UNLOCKED= 0;
  /** Flag to indicate that write_lock() is being held */
  static constexpr uint32_t WRITER= 1 << 31;
  /** Flag to indicate that write_lock_wait_for_unlock() is pending */
  static constexpr uint32_t WRITER_WAITING= 1 << 30;

  /** Wait for a shared lock */
  void read_lock_wait_for_write_unlock();
  /** Wait for an exclusive lock */
  void write_lock_wait_for_unlock();
public:
  /** Default constructor */
  rw_lock() : lock(UNLOCKED) {}

  /** Release a shared lock */
  void read_unlock()
  {
    IF_DBUG_ASSERT(auto l=,) lock.fetch_sub(1, std::memory_order_release);
    DBUG_ASSERT(l & ~(WRITER | WRITER_WAITING)); /* at least one read lock */
    DBUG_ASSERT(!(l & WRITER)); /* no write lock must have existed */
  }
  /** Release an exclusive lock */
  void write_unlock()
  {
    IF_DBUG_ASSERT(auto l=,) lock.fetch_sub(WRITER, std::memory_order_release);
    DBUG_ASSERT(l & WRITER); /* the write lock must have existed */
  }
  /** Acquire a shared lock */
  void read_lock()
  {
    if (lock.fetch_add(1, std::memory_order_acquire) &
        (WRITER | WRITER_WAITING))
      read_lock_wait_for_write_unlock();
  }
  /** Acquire an exclusive lock */
  void write_lock()
  {
    auto l= UNLOCKED;
    if (lock.compare_exchange_strong(l, WRITER, std::memory_order_acquire,
                                     std::memory_order_relaxed))
      return;
    write_lock_wait_for_unlock();
  }

  /** @return whether an exclusive lock is being held by any thread */
  bool is_write_locked() const
  { return !!(lock.load(std::memory_order_relaxed) & WRITER); }
  /** @return whether a shared lock is being held by any thread */
  bool is_read_locked() const
  {
    auto l= lock.load(std::memory_order_relaxed);
    return (l & ~(WRITER | WRITER_WAITING)) && !(l & WRITER);
  }
  /** @return whether any lock is being held by any thread */
  bool is_locked() const
  { return (lock.load(std::memory_order_relaxed) & ~WRITER_WAITING) != 0; }

  /** Acquire a lock */
  template<bool exclusive> void acquire()
  {
    if (exclusive)
      write_lock();
    else
      read_lock();
  }
  /** Release a lock */
  template<bool exclusive> void release()
  {
    if (exclusive)
      write_unlock();
    else
      read_unlock();
  }
};
