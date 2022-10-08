#include "db_impl.h"
#include <condition_variable>
#include <mutex>



namespace koishidb {

    struct DBimpl::Writer {
        Writer(WriteBatch* batch): batch(batch) {}
        Writer(const Writer& that) = default;
        WriteBatch* batch;
        bool done;
        std::condition_variable cv;
    };

  void DBimpl::Put(const Slice &key, const Slice &value) {
      WriteBatch write_batch;
      write_batch.Put(key, value);
      return Write(&write_batch);
  }

  void DBimpl::Delete(const Slice &key) {
      WriteBatch write_batch;
      write_batch.Delete(key);
      return Write(&write_batch);
  }

  void DBimpl::Write(WriteBatch *update) {
    Writer w(update); // generate a writer

    // exclusive lock
    std::unique_lock<std::shared_mutex> lock(rwlock_);
    writers_.push_back(&w);
    if (!w.done && &w != writers_.front()) {
        std::unique_lock<std::mutex> cv_lock(cv_lock_, std::adopt_lock);
        w.cv.wait(cv_lock);
        cv_lock.release();
    }
    // the task has been finished
    if (w.done) {
        return;
    }

    MakeRoomForWriter(); // immutable
    Writer *last_writer = &w;

    // TODO() BuildWriteBatch
    // get the write batch from the deque
    WriteBatch* writer_batch = BuildWriteBatch();

    // Insert into memtable_;
    writer_batch->InsertAll(&memtable_);

    while (true) {
          Writer* ready = writers_.front();
          writers_.pop_front();
          ready->cv.notify_one();
          if (ready == last_writer) break;
      }

      // Notify new head of write queue
      if (!writers_.empty()) {
          writers_.front()->cv.notify_one();
      }
  }


};